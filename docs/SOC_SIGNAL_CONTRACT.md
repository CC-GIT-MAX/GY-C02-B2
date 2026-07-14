# SOC 信号协议契约

## 背景

`cluster MCU` 通过 **UART0（LINFlexD0，PTC_3=TX / PTC_2=RX，250 kbps，DMA）**
与外部 `SOC` 芯片进行数据交互。所谓**协议**，就是协商一致的：

  - **哪些信号** 会出现在 SOC 端需要的字节流里
  - 每个信号的 **字节布局**（offset、长度、字节序、有符号、scale / offset）
  - **传输周期** / **触发时机** / **超时判定**

DBC 文件是 **CAN 总线** 上的报文定义。它与 **MCU ↔ SOC** 的协议契约
**不是同一回事**：

```
   CAN bus                                UART0 (LINFlexD0)        SOC board
   +-------+   DBC-frame  +-------+      +---------+   custom   +------+
   | ECU_A |  ----------> |       |  --> |  MCU    |  --------> | SOC  |
   | ECU_B |  ----------> | cluster|     | (本文档)|            |      |
   | ...   |              +-------+      +---------+             +------+
                              \_______________ _______________/
                                              v
                                    Signal bus (signal_id_t)
```

MCU 在 CAN 侧用 DBC 解析报文、写入 `Signal_Set(SIG_CAN_<Name>, ...)`；
在 UART0 侧则按**已发布**的协议字节流读取 `Signal_Get(...)`，把选定的信号
打包、加上帧头/校验、发给 SOC。两条链路通过**信号 id 平面** 间接对接，
DBC 改不改**都**不应破坏 SOC 协议。

本文档把“已发布的协议契约”建模成一个稳定的、显式的对象，并给出 DBC
增删之后修改这一契约的完整流程。

## 物理层 + 链路层（UART0）

链路全部由板级配置 + drv_api/uart/ 完成：

| 项 | 值 |
|---|---|
| MCU 外设 | `LINFlexD0` （即 UART0） |
| 引脚 | `PTC_3` = TX，`PTC_2` = RX |
| 配置结构 | `board/linflexd_uart_config.c::COMM_uart_config`（已就位） |
| 波特率 | **250 000 bps** |
| 字长 | 8 bits |
| 奇偶校验 | None (even as default if enable) |
| 停止位 | 1 |
| 收发模式 | **DMA** （通道：`txDMAChannel=1`，`rxDMAChannel=0`） |
| DMA 中断 | `DMA_UART0_TX_FUNCTION_CALLBACK`、`DMA_UART0_RX_FUNCTION_CALLBACK` |
| FIFO | DMA 接收模式默认 ring；字节级处理在 `app/drv_api/uart/` |

UART0 + DMA 通道**已经**在 `board/dma_config.c` 里就位：

```
DMA_REQ_LINFlexD0_RX = dma_config0  (rxDMAChannel 0)
DMA_REQ_LINFlexD0_TX = dma_config1  (txDMAChannel 1)
```

软件栈**仍需新增**：

  - `app/drv_api/uart/uart0_if.{c,h}`  — 收发 ring buffer + DMA 启动 / 完成回调
  - `app/drv_api/uart/soc_proto.{c,h}` — 帧头 / CRC / 命令字解析
  - `app/mod_soc_if/`                 — `mod_soc_if`：周期打包 `SIG_*` 推到 UART0
  - `app/drv_api/uart/uart0_register.c`— 在 `drv_init` 里调用 `UART0_Init()`、
                                        注册 DMA 回调

## 协议帧格式（MCU → SOC 方向；SOC → MCU 镜像对称）

`docs/soc_protocol/<version>.frame.md` 里以字节图说明。下表为 v1 默认值。

```
+--------+--------+--------+--------+--------+--------+
| 0xA5   | 0x5A   |  VER   |  LEN   |  CMD   | ...    |  CRC8 |
+--------+--------+--------+--------+--------+--------+
   ↑       ↑        ↑        ↑        ↑        ↑        ↑
   SOH1    SOH2     ver<<4   payload   cmd_id   payload  payload xor
            (固定)   | len<<0  bytes              bytes   checksum
```

| Field | Size | Value | Notes |
|---|---|---|---|
| `SOH1` | 1B | `0xA5` | Start-of-header high byte |
| `SOH2` | 1B | `0x5A` | Start-of-header low byte; 静默丢弃非此组合 |
| `VER` | 4b | `0x1` | 协议主版本 |
| `LEN` | 4b | `0`..`15` | payload 字节数（payload 长度 ≤ 240） |
| `CMD` | 1B | 命令字 | 见下表 |
| `payload` | `LEN` bytes | 信号数据 | 字节布局详见 `docs/soc_protocol/<version>.csv` |
| `CRC8` | 1B | bitwise XOR of all preceding bytes (excluding SOH1+SOH2) | 用 lookup table 加速 |

### 命令字 (`CMD`) 表

| CMD | 方向 | 名 | 用途 |
|---|---|---|---|
| `0x01` | MCU→SOC | `SOC_PUSH` | 周期/事件推送一批 SIG_* |
| `0x02` | MCU→SOC | `SOC_HANDSHAKE` | 启动握手：MCU 报告 `SOC_PROTOCOL_VERSION` |
| `0x10` | SOC→MCU | `SOC_REQ` | SOC 请求一组 SIG（payload 是 SIG ID 列表） |
| `0x80` | SOC→MCU | `SOC_DIAG` | 诊断/UDS 入口（暂未实现，转发到 `app/diag/` 后续） |
| `0xFF` | 双向 | `SOC_NACK` | 失败回包（payload = 原 CMD + error_code） |

任何 `CMD` 不在表里的帧，MCU 静默丢弃并 `LOG_W`。

### payload 字节布局

DBC 信号是物理量；SOC payload 是字节流。两者通过 `docs/soc_protocol/<version>.csv`
的逐行定义映射起来。每行描述 1 个被"打包传输的信号"：

```
idx,name,domain,type,byte_offset,bit_offset,length,byte_order,scale,offset,unit,cycle_ms,owner,note
```

`byte_offset` / `bit_offset` / `length` 描述位级布局；`scale` / `offset`
把 raw 转换为物理量；`cycle_ms` 决定 SOC_PUSH 周期；`owner` 是唯一
`Signal_Set` 模块。

**字节序策略**：单字节字段（≤8 bit）固定 big-endian bit 顺序；多字节字段
（>8 bit）固定 little-endian（与 ARM Cortex-M 自然对齐）。`byte_order`
列记录实际值（`LE` / `BE`），便于以后改。

## 一条铁律

> **SOC 协议一旦发布，**新版本必须通过 `app/soc/SOC_PROTOCOL_VERSION`
> 暴露给 SOC 端。任何 DBC 改动都不能在未升版本号的情况下修改：
>
>  - 已有信号的字节位置、长度、字节序、scale / offset
>  - 已有信号的传输周期
>  - 已存在信号 id 的语义
>
> 如果 DBC 改动会造成以上变化，必须先在 SOC 团队发起**协议修订 PR**，
> 通过后方可合并。

## SOC 协议表维护

协议表分两份：

  - `docs/soc_protocol/<version>.frame.md`  — 帧头/校验/CMD 表
  - `docs/soc_protocol/<version>.csv`       — payload 字节布局

新版本只需新增 `0002.*`，旧版本继续保留作为历史归档。**绝不** 修改
已经发布过的 `.<version>.*` 文件。

新表存放在 `docs/soc_protocol/<version>.csv`，每行：

```
idx,name,domain,type,byte_offset,bit_offset,length,byte_order,scale,offset,unit,cycle_ms,owner,note
```

例：

```
0,SOC_VERSION,meta,u32,0,0,32,LE,1,0,,0,mod_soc_if,"compile-time protocol version"
1,VEH_SPEED_KPH,vehicle,s16,4,0,16,LE,0.1,0,kph,100,mod_can_rx,"0..6553.5 kph"
2,ENG_RPM,vehicle,u16,6,0,16,LE,1,0,rpm,100,mod_can_rx,"0..65535 rpm"
3,COOLANT_T_C,vehicle,s8,8,0,8,LE,1,0,degC,1000,mod_can_rx,""
4,BUS_OFF_NOW,diag,bool,9,0,1,LE,1,0,,50,mod_can_error,"latched bus-off"
5,BUS_OFF_COUNT,diag,u32,10,0,32,LE,1,0,,50,mod_can_error,"cumulative"
```

字段含义：

  - `idx`             — `Signal id`（必须 == `signal_id_t` 枚举值）
  - `name`            — 信号名，必须 == `SIG_<...>` 去掉前缀的尾部
  - `domain`          — `vehicle / diag / power / soc / meta`
  - `type`            — `u8|u16|u32|s8|s16|s32|bool`
  - `byte_offset`     — 在 SOC 帧内的起始 byte
  - `bit_offset`      — 同一字节内 bit 偏移（0..7）
  - `length`          — 位长（1..32）
  - `byte_order`      — `LE | BE`（little / big endian）
  - `scale`           — 浮点放大因子
  - `offset`          — 浮点偏移
  - `unit`            — 物理单位（字符串）
  - `cycle_ms`        — 发送周期，0 = 仅事件触发
  - `owner`           — 唯一写入模块（owner module name）
  - `note`            — 备注（可空）

新增一行 + 在 `app/signal/signal.h` 末追加一格 `SIG_<NAME>` = 共生变化。
**绝不** 修改 `signal_id_t` 的已有顺序。

## 接受新信号 / 弃用旧信号

### 加信号

1. 在仓库开 issue 或 PR：`feature/soc-add-<name>`，描述：
    - 物理含义
    - 单位 / scale
    - 期望 cycle / 触发
    - 写入模块（owner）
2. 在 `app/signal/signal.h` 的对应 section 末尾追加枚举值；**只能 append，绝不重排**。
3. 在 `app/signal/signal.c::Signal_GetName()` 的同名表里加一行。
4. 在 `docs/soc_protocol/<next_version>.csv` 末尾追加一行。
5. owner 模块在 `mod_<owner>.c` 里 `Signal_Set`；其它模块只读。
6. 在 commit message 写清：
    ```
    feat(soc): add SOC_<NAME>

    - adds SIG_<NAME> in app/signal/signal.h section "..."
    - adds row N in docs/soc_protocol/<next_version>.csv
    - adds entry in Signal_GetName()'s k_names[] table
    - owner: mod_<owner>; updates <file>
    - bumps SOC_PROTOCOL_VERSION 0x0001 -> 0x0002
    ```
7. IAR build 0E/0W，doxygen/check/tests 全绿。

### 弃用信号

1. 在 issue / PR 中说明弃用原因。
2. **不要从 `signal.h` 删除枚举值**。在它的 `/* ... */` 注释里写明
   `DEPRECATED since 0002 - use SIG_<NEW> instead`。
3. owner 模块对应 `Signal_Set` 仍然保留（一个空 set 也行），避免裁掉
   生成器的 `signal_id_t`。
4. 在 `docs/soc_protocol/<version>.csv` 中：行不动，按需在 note 注明。
5. 后续 DBC 改动不重新触碰它。
6. SOC 团队确认完全弃用并下架后，方可在 **下一个协议主版本**
   （跨年 / 跨代项目，从 `0003 -> 0004` 之类）删除枚举值；删除要明确写
   在 commit message 里。

### 完全删除（慎重）

在极少数情况下确实需要删除（例如 SOC 端不再支持且 owner 也无源数据），
**禁止** 在原地删除。流程：

1. 弃用 → 经过 ≥ 1 个主版本周期 → 实际删除。
2. 删除时一并把 `k_names[]` 中对应条目移到末尾并标记 `REMOVED`。
3. 在 `docs/soc_protocol/` 中加一条 `0004.changelog.md`：
   ```
   removed: SIG_OLD_NAME (replaced by SIG_NEW_NAME)
   ```

## DBC 增删对应的处理流程

DBC 是 CAN 侧的源数据。它**可以**频繁修订（DBC 是和车厂联合维护的，
每版小调整都正常）。MCU ↔ SOC 的协议契约**不应**随之抖动。

### DBC 加新报文 / 信号

```
                         评估是否触碰 SOC 协议
                                  │
                  ┌──────────────┴──────────────┐
                  │                              │
              没碰 SOC 协议                    触碰了 SOC 协议
              （DBC-only 新增）              （如：SOC 直接转发某信号 / SOC 关心此信号）
                  │                              │
                  ↓                              ↓
     regen + commit，仅触及           先开 PR，按“加信号”流程
     - can_db_ipk_gen.{h,c}            走 SOC 协议修订
     - signal.h 的 SIG_CAN_ 段           再 regen
     - can_db.c s_dbc_to_bus
     - 可以省略 owner / SOC csv
```

“触碰 SOC 协议”的判定：

  - 该报文里有 SOC 已对外协议中包含的信号的名字
  - 或者该报文涉及 SOC 关心的语义（例如 timeout flag 的来源）
  - 或者 `g_can_rx_timeout_table` 里有非零条目——SOC 协议把 timeout bitfield
    转成超时事件，需要重新建表

### Sentinel 超时位图策略

SOC 超时状态依赖 `SIG_CAN_RX_TIMEOUT_MAP_{LO,HI,HI2}` 这 3 个
`u32` 位字段，覆盖 `CAN_BITMAP_MAX = 96` 个 bit（bit 0..95）。
按 Sentinel 策略，**bit-N 与 CAN ID 一一绑定**，由
`s_bit_to_can_id[]` 查找表维护（生成器自动从
`tools/.bitmap_state.json` 加载历史映射）。

- **bit 0..63**：RX 报文分配池（上限 `CAN_BITMAP_RX_ALLOC_MAX = 64`）
- **bit 64..95**：reserved 池（预留仓位，临时为 0）
- **删除报文**：对应 bit 留为 `sentinel_unused = 0`，**永远为 0**，
  不重新分配，SOC 端 bit-N 编号不变。

#### DBC 变更时的保障

  1. 跑 `python tools/regen_can_artifacts.py --dbc <new.dbc>`。
  2. 生成器会读 `.bitmap_state.json`，以**历史 bit 编号**为准
     重新分配：
     - 已存在的 CAN ID 保持原 bit 不变（locked）
     - 新增的 CAN ID 从 reserved pool (64..95) 或未占用位位分配
     - 删除的 CAN ID 对应 bit 留 sentinel，不重分配
  3. `g_can_rx_timeout_table[]` 同步 regen，考虑到 DBC 可能修改
     了 cycle，需检查 timeout 是否合理。
  4. SOC_PROTOCOL_VERSION 在 Sentinel 策略下保持 v1.0（bit-N
     不变）；只有报文本身增减且 SOC 端重新跟进
     时才 bump。

#### 带来的限制

  - **RX 上限 64**：超出 64 个 RX 报文后，新增报文无
    法获得 bit，只能在 SOC 协议重新计划时一起升级
    `CAN_BITMAP_MAX`。
  - **DBC 文件保留**：删除报文后，对应 bit 不能重用。
    万一后续 DBC 又加上该报文，会被分到 reserved pool
    的第一个空位，不会恢复原 bit。
  - **.bitmap_state.json 需随工程提交**：丢失会导致 bit 重新
    分配，与 Sentinel 原意相反。

### DBC 删报文 / 信号

```
        DBC 上删除该报文 / 信号
                  │
                  ↓
        eval: 是否有 SIG_CAN_<X> 已被 SOC 协议引用？
                  │
       ┌──────────┴──────────┐
       │                      │
       否                     是
       │                      │
       ↓                      ↓
    regen + commit        先按“弃用信号”流程
    signal.h 删除         在 SOC 协议里退役它
    SIG_CAN_<X> 行        再让 DBC 删除
                          （仍然禁止原地重命名 / 重排 SIG id）
```

### DBC 修改已有信号的物理布局

```
        DBC 改了某个报文 / 信号的
        - scale / offset / length / start_bit
                  │
                  ↓
        如果 SOC 协议读的是 SIG_CAN_<X>，直接同步 SOC 协议表（增 idx 版本）
        如果 SOC 协议读的是 SIG_<domain>_<X>（不是 CAN 系列），
            则 SOC 协议不动；CAN 解析层在中间负责换算
        否则纯 CAN 内部
```

## 工具支持（`tools/`）

| 工具 | 用法 |
|---|---|
| `tools/dbc_parse.py --split ... --emit-*` | DBC → C 表 + 信号 id emit |
| `tools/dbc_parse.py --report-diffs OLD NEW` | 打印两条 DBC 之间的 message / signal 增删改；CI 可以 grep `diff-rc=` 自动 gate |
| `tools/regen_can_artifacts.py --dbc <...>` | 一键 regen 5 个 CAN 工件（commit `96adfc9`） |
| 计划：`tools/soc_check.py <csv>` | 校验 SOC 协议 csv 中所有 `idx` 都对应真实存在的 SIG id；所有 owner 模块都写得动；`SOC_PROTOCOL_VERSION` 与 csv 一致 |
| 计划：`tools/soc_emit_payload.py` | 读 csv 生成 `mod_soc_if::prv_pack_payload()` 的 C 源码 |

## 一份检查清单（每次 DBC 改动跑一遍）

- [ ] `tools/dbc_parse.py --report-diffs OLD NEW`：检查 rc 是否大于 0
- [ ] 看是否触碰 SOC 协议：是 → 开 PR；否 → 直接 regen
- [ ] `tools/regen_can_artifacts.py --dbc <new.dbc>`（同时写备份）
- [ ] 跑 `doxygen`、`check.sh`、`tests/test_can_ipk.py`
- [ ] IAR build 0 错 0 警
- [ ] 若触碰 SOC 协议：`SOC_PROTOCOL_VERSION` 自增；SOC 端 release notes
- [ ] commit 单一主题：`feat(can): regen DBC for <dbc-version>` 或
      `feat(soc): add/remove/deprecate <signal-name>`

## 失败模式与对策

| 症状 | 大概率原因 | 对策 |
|---|---|---|
| SOC 解码到 0 / NaN | owner 没在对应 cycle 写入 | 看 `Signal_IsValid()` + rx timeout flag |
| 协议不匹配 SOC 期望 | 协议未升版本就改了字节布局 | 强制 bump `SOC_PROTOCOL_VERSION` 流程 |
| OBD 失败 / 显示空 | DBC 删了报文但 SOC 还引用 | 看 `g_can_rx_timeout_table` 是否满 map；`SOC_PROTOCOL_VERSION` 联动 |
| flash 写超 | `SIG_MAX` 过大（每个信号 8 字节） | 拆 enum；只把 SOC 真用得到的 SIG 放 `s_signals[]` |
| UART0 DMA FIFO 溢出 | baud 太高或 DMA ring 太小 | `app/drv_api/uart/uart0_if.c` ring 深度 ≥ 2 帧，调 baudrate |

## 后续动作

- [ ] `app/drv_api/uart/uart0_if.{c,h}` — UART0 ring + DMA 回调（提供
      `SOC_UART0_Init / Send / Recv` 三个 API）
- [ ] `app/drv_api/uart/soc_proto.{c,h}` — 帧头/CRC/CMD 解析器
- [ ] `app/mod_soc_if/mod_soc_if.{c,h}` — 周期推送 `SOC_PUSH`；处理
      `SOC_REQ` / `SOC_HANDSHAKE`
- [ ] 在 `app/drv_init/drv_init.c` 注册 `SOC_UART0_Init()`；在
      `app/scheduler/scheduler.c` 注册 `mod_soc_if`
- [ ] 实现 `tools/soc_check.py`：校验 csv 与 enum 一致性
- [ ] 实现 `tools/soc_emit_payload.py`：csv → `prv_pack_payload()` 源

