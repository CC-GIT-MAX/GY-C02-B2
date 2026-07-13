# DBC 变更操作指南



> 本指南面向后续 CAN 业务模块开发者：当你拿到一份新版本 DBC
> （新增信号、整条报文、删除报文、修改 signal 布局）时，怎么把
> 它合进工程、跑通编译 + 测试、最后干净提交。

## 0. 一句话规则

> **DBC 是 SoT（single source of truth）。DBC 改完，只跑
> `regen_can_artifacts.py --dbc <new.dbc>` 就够了——所有派生表
> （`can_db_ipk_gen.{h,c}` / `signal.h` / `can_db.c` /
> `can_tx.c` / `can_rx.c`）自动更新。**

如果 regen 完编译失败或测试失败，**不要手改派生文件**——回到
DBC，找问题，regen 重跑。手动改 gen 文件会在下一次 regen 时被
覆盖，等于白改。

## 1. 30 秒快速参考

| 你想做的事 | 怎么改 DBC | 然后做什么 |
|---|---|---|
| 加一个 signal 到现有报文 | `SG_` 行追加 | regen → 编译 → 测试 → commit |
| 加一整条 RX 报文 | `BO_` + `SG_` + `BU_:` | regen → 检查 `s_bit_to_can_id[]` 是否还有空位 → 编译 → 测试 → commit |
| 加一整条 TX 报文 | `BO_` + `SG_` + `BU_:` + `transmitter = IPK` | regen → 在 `can_tx.c` 写发送逻辑（见 § 3.3）→ 编译 → 测试 → commit |
| 删一条报文 | 整段 `BO_` 删 | regen → 检查 `.bitmap_state.json` 的 sentinel 记录 → 编译 → 测试 → commit |
| 删一个 signal | 删对应 `SG_` 行 | regen → 编译 → 测试 → commit |
| 改 signal 物理布局（factor / start_bit / length） | 改 `SG_` 行 | regen → **必须跑 `test_can_ipk.py` 全套** → 编译 → commit |
| 改报文名 / signal 名 | `BO_` / `SG_` 名字 | regen → 编译 → 测试（mapping 校验会 fail）→ 修引用代码 → commit |

## 2. 工具链概览

```
            DBC 文件 (.dbc)
                 |
                 v
   +---------------------------------+
   |  tools/dbc_parse.py             |  parser + emit
   |    --emit-signal-block OUT      |  -> app/signal/signal.h
   |    --emit-map OUT               |  -> app/drv_api/can/can_db.c
   |    --emit-tables TX RX          |  -> app/can/can_tx.c / can_rx.c
   |    --emit-bitmap-map OUT        |  -> can_db_ipk_gen.c (s_bit_to_can_id[])
   +---------------------------------+
                 |
                 v
   +---------------------------------+
   |  tools/regen_can_artifacts.py   |  一键跑 parser + splice 5 处
   |    --dbc path/to/IPK.dbc        |
   |    [--dry-run]                  |  只想看 diff 不动文件
   +---------------------------------+
                 |
                 v
       5 个被 splice 的源文件 + 1 个 sidecar (tools/.bitmap_state.json)
```

6 个 splice 目标：

  - `app/drv_api/can/can_db_ipk_gen.{h,c}`  --split 整体重写
  - `app/signal/signal.h`                   --emit-signal-block
  - `app/drv_api/can/can_db.c`              --emit-map
  - `app/can/can_tx.c`                      --emit-tables
  - `app/can/can_rx.c`                      --emit-tables
  - `app/drv_api/can/can_db_ipk_gen.c`      --emit-bitmap-map（额外一条 splice）
  - `tools/.bitmap_state.json`              -- Sentinel 历史状态

## 3. 详细操作

### 3.1 增加一个 signal 到现有报文

例：在 `EMS_EngineRPM`（CAN ID 0x0C）里加一个 `EMS_EngPower`，8 bit
unsigned，factor = 0.5，offset = 0。

DBC diff：

    BO_ 12 EMS_EngineRPM: 8 EMS
       SG_ EMS_EngineSpeedRPM : 0|16@1+ (0.1,0) [0|6553.5] "rpm"  Receiver
    -  SG_ EMS_EngStatus : 16|2@1+ (1,0) [0|3] ""  Receiver
    +  SG_ EMS_EngStatus : 16|2@1+ (1,0) [0|3] ""  Receiver
    +  SG_ EMS_EngPower : 24|8@1+ (0.5,0) [0|127.5] "kW"  Receiver
       SG_ EMS_EngineSpeedRPMInvalid : 32|1@1+ (1,0) [0|1] ""  Receiver

操作步骤：

    1. 修 DBC，加 SG_ 行
    2. cd D:\working_file\WorkSpace\Project\XTD\C02-B2\PJ
    3. python tools/regen_can_artifacts.py --dbc path/to/new.dbc
    4. 检查输出："all 6 artefacts refreshed"
    5. python tests/test_can_ipk.py
       -- [1] mapping 应该 207/207 + 1；[2] round-trip +1
    6. & "D:\IAR_AND_JLINK\IAR_IDE\common\bin\iarbuild.exe" `
         "EWARM\C02_B2.ewp" -build FLASH
    7. git diff --stat
    8. git add -A && git commit -m "feat(can): add EMS_EngPower signal"

#### 应该出现的变化

  - `signal.h` 多一行 `SIG_CAN_EMS_EngPower`
  - `can_db_ipk_gen.h` 枚举多一个 `CAN_DB_SIG_EMS_EngPower`
  - `can_db_ipk_gen.c` `can_sig_descs_ipk[]` 多一项
  - `can_msg_descs_ipk[]` 中 `EMS_EngineRPM` 的 `sig_count` +1
  - `s_dbc_to_bus[]` 多一项
  - `s_bit_to_can_id[]` 不变（报文 ID 没动，bit-N 稳定）
  - `tools/.bitmap_state.json` 不变

#### 不需要做的事

  - 不要改 `can_rx.c` / `can_tx.c` 的 timeout / cycle 表（除非改了报文 ID）
  - 不要改 `can_db.c` 的 `s_dbc_to_bus[]` 初始化（splice 自动覆盖）
  - 不要 bump `SOC_PROTOCOL_VERSION`（这个 signal 的 bit-N 没变）

### 3.2 增加一条整 RX 报文

例：增加 `VCU_NewState`（CAN ID 0x456），transmitter = `VCU`，1 个
signal，8 bit。

DBC diff：

    BO_ 1110 VCU_NewState: 8 VCU
       SG_ VCU_NewStateValue : 0|8@1+ (1,0) [0|255] ""  IPK

操作步骤：

    1. 修 DBC，加 BO_ + SG_ + 在 BU_: 节点列表里加 IPK
    2. python tools/regen_can_artifacts.py --dbc path/to/new.dbc
    3. **手动检查** `s_bit_to_can_id[]` 第几个 bit 分配到了 0x456：

        python -c "import re; s=open('app/drv_api/can/can_db_ipk_gen.c').read();
        m=re.search(r's_bit_to_can_id\[CAN_BITMAP_MAX\] = \{([^}]+)\}', s);
        ids=[int(x,16) for x in re.findall(r'0x[0-9A-Fa-f]+u', m.group(1))];
        print('VCU_NewState bit =', ids.index(0x456))"

    4. 在 `g_can_rx_timeout_table[]`（can_rx.c 里 hand-authored）对应
       idx 位置给 0x456 配一个 timeout 值（典型 = cycle_ms * 3，按
       50ms 取整）。如果忘了配，bit 永远不会被置位（但也不报
       错——timeout monitor 看到 timeout = 0 就 skip）。
    5. 编译 + 测试 + commit

#### Sentinel bit 分配规则（重要）

`assign_bitmap()` 在 DBC 解析时按以下顺序分配 bit：

    1. 读 .bitmap_state.json 里的 {bit: can_id} 历史映射
    2. 已存在的 CAN ID 沿用旧 bit（locked）
    3. 新 CAN ID 从 reserved pool (64..95) 或第一个未占用 bit 分配

如果 `rx_count` 已经在 64 个，新加的报文**不会**获得 bit 分配——
`assign_bitmap` 直接丢弃它（不在 s_bit_to_can_id[] 里出现）。
后果：

  - `can_rx.c` 收帧时 `prv_bit_for_can_id(m.id)` 返回 0xFF（not mapped）
  - `prv_drain` 调 `CanDb_DispatchByDb` 仍然能 dispatch（dispatch 用
    `prv_find_ipk_index` 走 descriptor table，跟 bit 无关）
  - 但 `prv_check_timeouts` 永远不会为这个报文置 bit
  - SOC 端 timeout bitfield 也看不到这报文超时

**遇到这种情况必须升级**：

  1. 把 `CAN_BITMAP_MAX` 从 96 提到 128（4 slots）
  2. 在 `app/signal/signal.h` 加 `SIG_CAN_RX_TIMEOUT_MAP_HI3`
  3. 在 `app/can/can_rx.c::prv_check_timeouts` 加 `map_hi3`
  4. regen
  5. 验证测试通过
  6. 联动 SOC 协议（参考 § 6）

### 3.3 增加一条整 TX 报文

例：增加 `IPK_DiagInfo`（CAN ID 0x777），transmitter = `IPK`，3 个
signal。

DBC diff：

    BO_ 1911 IPK_DiagInfo: 8 IPK
       SG_ IPK_DiagVer : 0|8@1+ (1,0) [0|255] ""  Vector__XXX
       SG_ IPK_DiagCode : 8|16@1+ (1,0) [0|65535] ""  Vector__XXX
       SG_ IPK_DiagCnt : 24|16@1+ (1,0) [0|65535] ""  Vector__XXX

操作步骤：

    1. 修 DBC，加 BO_ + SG_
    2. python tools/regen_can_artifacts.py --dbc path/to/new.dbc
    3. regen 会在 `g_can_tx_cycle_table[]` 末尾给 0x777 配一个默认
       cycle（heuristic：1000 ms 兜底；见 dbc_parse.py:_default_cycle_ms）
    4. 在 `app/can/can_tx.c` 写发送逻辑（参考已有 `IPK_STS` 模式）
    5. 编译 + 测试 + commit

#### 发送模式

按 cycle 自动发还是事件触发？

  - **周期**（如 `IPK_STS`）：在 `mod_can_tx::prv_tick` 用
    `RTI_OpenSlot(period)` + `RTI_SlotElapsed(&slot)` 触发；cycle 由
    `g_can_tx_cycle_table[]` 决定
  - **事件**（如 `IPK_Fuel_Sts` 油耗变化时发）：保留一个 last-sent
    tick 变量，值变化时调 `CanTx_Trigger(can_id, ...)`
  - **混合**（默认周期 + 强制重发）：周期 + 信号值变化时
    `CanTx_Trigger`

参考 `app/can/can_tx.c` 已有 IPK_* 处理。

### 3.4 删除一条报文

例：删除 `EMS_OBD_Info`（CAN ID 0x461）。

DBC diff：

    -BO_ 1121 EMS_OBD_Info: 8 EMS
    -   SG_ EMS_OBD_DTC : 0|16@1+ (1,0) [0|65535] ""  Receiver
    -   SG_ EMS_OBD_MIL : 16|1@1+ (1,0) [0|1] ""  Receiver
    -   SG_ EMS_OBD_Ready : 17|1@1+ (1,0) [0|1] ""  Receiver

操作步骤：

    1. 修 DBC，删整段 BO_ + 涉及的 SG_
    2. python tools/regen_can_artifacts.py --dbc path/to/new.dbc
    3. **检查** `s_bit_to_can_id[]`：

        python -c "import re; s=open('app/drv_api/can/can_db_ipk_gen.c').read();
        m=re.search(r's_bit_to_can_id\[CAN_BITMAP_MAX\] = \{([^}]+)\}', s);
        ids=[int(x,16) for x in re.findall(r'0x[0-9A-Fa-f]+u', m.group(1))];
        # 0x461 之前在哪一位，现在应该那一位置 0
        print('bit for 0x461 now:', ids.index(0x461) if 0x461 in ids else 'GONE (sentinel)')"

    4. 检查 `.bitmap_state.json`：

        应该能看到 `"<bit>": 1121` 这条 entry（1121 = 0x461）——

        但 sentinel 之后这条 entry 仍然在 .bitmap_state.json 里，
        assign_bitmap 读它时发现 0x461 不在当前 DBC 的 RX 集里，
        就把它放到 retired_bits 集合里，bit 保持原位，splice 时写
        0 进去。

    5. 编译 + 测试（[5] Sentinel 应通过） + commit

#### 关键不变量

  - `s_bit_to_can_id[]` 中被删报文对应的 bit 留 **0**（sentinel_unused）
  - **不**重分配给其他报文
  - SOC 端 bit-N 编号**永久不变**
  - 如果删的报文 SOC 协议在引用，看 § 6 决定是否 bump `SOC_PROTOCOL_VERSION`

#### 必须保留的"墓碑"

DBC 文件本身**不再**含 0x461 报文，但：

  - `.bitmap_state.json` 必须保留 `"<bit>": 1121` 这条
  - `s_bit_to_can_id[]` 对应 bit 写 0
  - **不要手改 .bitmap_state.json**——它由 regen 自动维护

万一以后 DBC 改回 0x461（比如 OBD-II 重新启用），那个 bit 会自动
恢复（locked bit 机制）。但**不是原 bit**——DBC parser 会按
locked 机制把它重新分配到 reserved pool 的第一个空位（见
assign_bitmap 的 `while next_bit in used_bits` 循环）。

### 3.5 改 signal 的物理布局

例：`EMS_EngineRPM.EMS_EngineSpeedRPM` 改 factor 从 0.1 改成 0.05。

DBC diff：

       SG_ EMS_EngineSpeedRPM : 0|16@1+
    -    (0.1,0) [0|6553.5] "rpm"  Receiver
    +    (0.05,0) [0|3276.75] "rpm"  Receiver

操作步骤：

    1. 修 DBC
    2. python tools/regen_can_artifacts.py --dbc path/to/new.dbc
    3. **必须跑** `tests/test_can_ipk.py`——[2] round-trip 会用新 factor
       重新计算 expected value
    4. 编译 + commit

#### 风险

  - 上层业务用 `Signal_Get(SIG_CAN_EMS_EngineSpeedRPM)` 读到的物理
    值会**整体偏移 2 倍**——所有依赖该信号的业务逻辑可能误判
  - SOC 协议里如果也按 factor=0.1 解析，会看到数值翻倍
  - **必须**联动 SOC 团队，确认他们也按新 factor 重定标

### 3.6 改报文名 / signal 名

DBC 改 BO_ 名字 或 SG_ 名字 = 改 DBC 字符串字段。

后果：

  - `signal.h` 的 `SIG_CAN_<Name>` 枚举名变了
  - `can_db_ipk_gen.h` 的 `CAN_DB_SIG_<Name>` 枚举名变了
  - 所有引用旧名的 C 代码 / 测试都会编译失败

操作步骤：

    1. 修 DBC
    2. regen
    3. IAR 编译 → 失败，所有引用旧名的地方都报错
    4. 在 IAR 输出里 grep 旧名，全局替换为新名（建议 IDE
       "Rename Symbol" 一次性搞完）
    5. python tests/test_can_ipk.py —— [1] mapping 必须 207/207 + N
       都过（splice 后新名都映射到了）
    6. 编译 + commit

如果旧名已经在 SOC 协议文档里固定了，**别改名**——SOC 解析就
废了。改名跟删/加 signal 等价，都需要 bump `SOC_PROTOCOL_VERSION`。

## 4. 验证流程（每次 DBC 变更后必跑）

    # 1. regen
    python tools/regen_can_artifacts.py --dbc path/to/new.dbc

    # 2. 单元测试（DBC-driven 信号解析全验证）
    python tests/test_can_ipk.py
    # 期望：[1] mapping 100% OK，[2] round-trip OK，
    #        [3] boundary OK，[4] cross-byte OK，
    #        [5] Sentinel bitmap OK

    # 3. IAR 编译
    & "D:\IAR_AND_JLINK\IAR_IDE\common\bin\iarbuild.exe" `
      "EWARM\C02_B2.ewp" -build FLASH
    # 期望：Total number of errors: 0

    # 4. 视觉抽检（重要！测试只覆盖数值对错，不看布局）
    #   - 打开 app/signal/signal.h，看 SIG_CAN_<新名> 那一行的注释
    #   - 打开 app/drv_api/can/can_db_ipk_gen.c，找新信号的 start_bit / length
    #     对照 DBC 原文确认
    #   - 如果是改 factor，splice 后 raw → physical 换算公式变了，业务
    #     逻辑要重新 review

    # 5. （如果删了报文）检查 .bitmap_state.json
    Get-Content tools\.bitmap_state.json
    # 期望：被删报文的 bit 还在 retired 状态（entry 保留）

## 5. 提交规范

    git add -A
    git status
    # 期望看到 5~6 个 .h/.c 修改 + .bitmap_state.json
    # 不期望看到手改的 hand-authored 段被 splice 还原

    git commit -m "feat(can): <一句话描述 DBC 变更>"

    # 多步 DBC 变更（一次改多个）建议拆 commit：
    git add tools/dbc_parse.py         # parser
    git add tools/regen_can_artifacts.py
    git add app/drv_api/can/can_db_ipk_gen.{h,c}
    git add app/signal/signal.h
    git add app/can/can_tx.c
    git add app/can/can_rx.c
    git add app/drv_api/can/can_db.c
    git add tools/.bitmap_state.json   # Sentinel sidecar
    git commit -m "feat(can): ..."

如果有 `app/can/can_tx.c` 业务逻辑改动（新增 TX 报文发送函数），
拆第二个 commit：

    git add app/can/can_tx.c
    git commit -m "feat(can): <TX 报文名> send handler"

## 6. SOC 协议联动

`SOC_SIGNAL_CONTRACT.md` § "DBC 删报文 / 信号" 详细写了 SOC 协议修订
流程。本节只列判定：

| 变更 | SOC bump 协议? |
|---|---|
| 仅新增 signal，bit-N 没变 | **不**（bit-N 不动 → SOC 看不到差别） |
| 改 signal factor / start_bit | **是**（物理值变 → SOC 解析结果变） |
| 改 signal 名 | **是**（SOC 解析的字段名变） |
| 删 signal | **是**（SOC 端在引用就废） |
| 删整条报文 | **是**（timeout bit 永久为 0，SOC 看到报文消失） |
| 加新报文 | **是**（SOC 协议需要新增 timeout bit 处理） |
| 加新报文 + 升级 CAN_BITMAP_MAX | **必须 bump** + major version |
| 仅物理布局（bit 偏移）变 | 一般**不**（SOC 看物理值，bit layout 由 MCU 决定） |

升级 `SOC_PROTOCOL_VERSION` 的方法在 `SOC_SIGNAL_CONTRACT.md` 末尾
"协议版本号" 一节。

## 7. 常见坑

### 7.1 漏更新 .bitmap_state.json

症状：regen 后 `s_bit_to_can_id[]` 跟历史 bit-N 编号对不上。
原因：`.bitmap_state.json` 没 commit 进 git。
解决：必须随代码 commit。位置：`tools/.bitmap_state.json`。

### 7.2 regen 后 IAR 报 can_db_ipk_gen.h 大量 syntax error

症状：`expected an identifier`、`identifier "SIG_CAN_RX_TIMEOUT_MAP_"
is undefined`。
原因：上一版 regen 产物里 `L.append("/* ... *")` 注释起始行末尾
没 `*/`，导致 C 注释被错误闭合。
解决：跑 `git diff app/drv_api/can/can_db_ipk_gen.h`，检查 line 1
附近是否 `/* --- */` 完整闭合。如果不闭合，重跑 regen（极少见，
多见于手工 patch emit_header 的中间状态）。

### 7.3 DBC 里 `BU_:` 节点列表漏写 IPK

症状：regen 出来的 `can_msg_descs_ipk[]` 没这条报文
（`select_for_node` 看不到 IPK 是 receiver）。
解决：回到 DBC，在报文信号的 receivers 里加 `IPK`，或者在
`BU_:` 节点列表里加（但 BU_ 是 transmitter 列表，跟 receiver
无关——真正决定 receiver 的是各 SG_ 行的 receivers 字段）。

### 7.4 删了报文后 SOC 端 timeout 一直是 0

症状：SOC 反映"报文不来了"，但 timeout bitfield 是 0 而不是 1。
原因：Sentinel sentinel_unused 永远为 0。
正解：这是设计——Sentinel 表示"DBC 已删除，永久 0"。
SOC 端如果用"timeout bit = 1 表示超时"，看到 0 会判"正常"——这
是 bug。SOC 端必须知道哪些 bit 是 sentinel。

如果 SOC 端要识别 sentinel，可以：
  - 用 `s_bit_to_can_id[bit] == 0` 判断
  - 或者用 `SOC_SIGNAL_CONTRACT.md` 里列的协议版本号区分

### 7.5 改了 signal 名但忘了改引用代码

症状：IAR 报 `CAN_DB_SIG_<旧名> undefined`。
解决：在工程里 grep 旧名，全局替换。

### 7.6 regen --dry-run 一定要先看

症状：手滑改了 parser，regen 产物大变样。
建议：regen 前先 `--dry-run` 看 6 个 artefact 的 diff，确认是预期
的（signal count +N、bit map 变 X 处）再正式跑。

### 7.7 手改 signal.h 的 CAN autoblock 段

警告：**不要手动在 `signal.h` 的 CAN 段加任何内容**。

`signal.h` 被划为 3 段，只有中间一段会被 regen 覆盖：

| 段 | 内容 | 谁维护 | regen 行为 |
|---|---|---|---|
| line 1 到第一个 `/* 0x... msg (RX|TX)  dlc=N */` banner | 手写枚举（IGN/PWR/VEHICLE/TIMEOUT） | 开发者 | **不动** |
| 第一个 banner 到 `SIG_MAX` 之前 | N 个 `SIG_CAN_<Name>`（随 DBC 变化） | `dbc_parse.py` 自动 emit | **整段 splice 覆盖** |
| `SIG_MAX` 之后 | 空（预留） | — | **不动** |

错误场景：

  - 某个模块为了 “借用” `SIG_CAN_` 命名空间，手动在
    CAN autoblock 底部加了一行 `SIG_CAN_MyCustom,`。
  - 下次 `regen_can_artifacts.py` 跑过后，该行被覆盖，`SIG_CAN_MyCustom`
    的枚举值被重新分配给了别的信号。
  - 所有 `Signal_Get(SIG_CAN_MyCustom)` 静默指向别的信号，**编译
    不报错**、**运行不报错**、只是读到错的值。

正确做法：

  - 要用 `SIG_CAN_` 前缀，**修改 DBC**，regen 自动生成。
  - 要用模块/个人独有信号，用 `SIG_<DOMAIN>_<NAME>` 命名，加在
    `signal.h` 上部手写段（line 1 到第一个 banner 之前）。
  - 临时内部状态（不跨模块），用模块内 `static` 变量，
    **不要**进 `signal.h`、**不要**跨文件。

### 7.8 跨模块全局变量 / 传参管理规约

架构原则（`docs/ARCHITECTURE.md`）：**不使用 `extern` 跨文件全局变量**。
所有跨模块可变状态都必须绕路 `Signal_Set / Signal_Get`。

三种信号分类及管理方式：

| 类型 | 例如 | 怎么管理 |
|---|---|---|
| **CAN 信号**（来自 DBC） | 车速、转速、SOC 上报状态 | 在 DBC 里定义，regen 自动生成 `SIG_CAN_<Name>`。业务代码只调 `Signal_Get(SIG_CAN_<Name>)`。**不要**新建 `extern` 变量。 |
| **手写信号**（车载 / 业务计算产生） | `SIG_IGN_ON` (KL15)、`SIG_VEH_SPEED_KPH_X10`、`SIG_FUEL_LEVEL_PCT` | 在 `signal.h` 的上部手写段（line 1 到第一个 banner 之前）手动加一行。**不要**加在 CAN autoblock 里。 |
| **模块内部状态**（不跨模块） | ADC 采样缓冲、按键 debounce 计数器 | 模块内 `static` 变量。**不进** `signal.h`、**不跨文件**。 |

需要传 “复杂结构体” 时怎么办？

`Signal_Set/Get` 只支持 `u32` / `bool`（CAN 信号以 RAW 形式存储,非物理量;模块消费时通过 `CanDb_DecodeSignal` 转 physical,或 `CanTx_EncodeSignal` 反向）。对于多字段数据：

  - **少量关联信号**（3 个以内）：拆多个 `SIG_<A>/SIG_<B>`。
    例：`SIG_VEH_SPEED_KPH_X10` + `SIG_VEH_SPEED_VALID` + `SIG_VEH_SPEED_SOURCE`
  - **两个 16 bit**：压入一个 `u32`，
    例：`Signal_Set(SIG_FOO_PACKED, ((u32)a << 16) | ((u32)b & 0xFFFFu))`
  - **真的是结构体 / 浮点 / 字符串**：重新思考设计，
    该是“跨模块状态”还是“业务逻辑输入”。
    如果是后者，为其建一个业务模块，让该模块拆解
    为多个 `SIG_`。**不要**引入 `extern struct foo_t` 跨文件变量。

该规约为架构硬约束，违反它会被 PR review 打回。

## 8. 速查表

| 操作 | DBC 改动 | regen 必要 | 测 | 编译 | bump SOC |
|---|---|---|---|---|---|
| 加 signal | 加 SG_ | Y | Y | Y | N |
| 删 signal | 删 SG_ | Y | Y | Y | Y |
| 加 RX 报文 | 加 BO_ + SG_ | Y | Y | Y | Y |
| 加 TX 报文 | 加 BO_ + SG_ | Y | Y | Y | Y |
| 删报文 | 删整段 BO_ | Y | Y | Y | Y |
| 改 factor/offset | 改 SG_ | Y | Y | Y | Y |
| 改 start_bit/length | 改 SG_ | Y | Y | Y | N（bit-N 稳定） |
| 改名 | 改 BO_/SG_ 名字 | Y | Y | Y | Y |
| 加 node 接收方 | SG_ receivers 加 IPK | Y | Y | Y | N |

---

参考：

  - `docs/SOC_SIGNAL_CONTRACT.md`  -- SOC ↔ MCU 协议契约
  - `docs/SIGNAL_GUIDE.md`         -- 信号总线使用
  - `tools/regen_can_artifacts.py` -- 一键 regen 入口
  - `tools/dbc_parse.py`           -- 解析器 + emitter


## 4.1 Phase 5 验收增强（docs 收口后新增）

Phase 5 完成 `CAN_ARCHITECTURE.md` / `SCHEDULER_GUIDE.md` 后，验证流程增强：

```
# 6. 文档一致性校验（regen 完成后做）
git diff docs/CAN_ARCHITECTURE.md  docs/SCHEDULER_GUIDE.md
# 期望：这两个文档本身在本次 DBC 变更中**不需要改**——
#   CAN_ARCHITECTURE.md 只在 CAN 模块新增/删除机制（如新加 bus-off
#   恢复策略）时才更新；
#   SCHEDULER_GUIDE.md 只在调度器机制变更时才更新。

# 7. 如果改了 timeout 表（CAN_RX_TIMEOUT_MAP_*），重新生成 SOC 协议版本
#    详见 SOC_SIGNAL_CONTRACT.md § "协议版本号"
```

**文档更新触发条件**：

| 触发场景 | 要更新的文档 |
|---|---|
| DBC 加 / 删 / 改 signal 布局 | docs/DBC_CHANGE_GUIDE.md（追加 case） |
| DBC 加 / 删报文 | docs/SOC_SIGNAL_CONTRACT.md（协议 bump） |
| CAN 模块机制变更（filter size、恢复策略等） | docs/CAN_ARCHITECTURE.md |
| Scheduler 机制变更（slot 池扩容、defer 多实例等） | docs/SCHEDULER_GUIDE.md |
| 工具链变化（regen / check.sh / 测试脚本） | docs/IAR_BUILD.md / DBC_CHANGE_GUIDE.md |

**禁止**：在 DBC 变更 commit 里"顺手"改架构文档。架构文档的修改应当独立 commit（`docs(<scope>): <变更>`），便于评审。
