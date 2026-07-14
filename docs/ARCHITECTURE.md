# C02-B2 仪表 MCU 架构宪章

> 本文件是项目协作的"宪法"，所有新代码必须符合这里的规约。修改本文件需要至少 1 名维护者 + 1 名业务负责人同意。

## 1. 项目概述

- 目标平台：YTM32B1MD1（Cortex-M33 @ 120 MHz）
- 软件形态：裸机 + 自制 RTI 时间片调度（**不**使用 RTOS）
- 通信：2 路 CAN + 多路 UART/I2C/SPI
- 编译：IAR Embedded Workbench 9.x
- 项目代号：C02-B2

## 2. 目录结构

```
app/                业务层
  main.c            主入口（仅做初始化 + 调度循环）
  init/             BSP / DRV 初始化
  scheduler/        调度器（模块注册表 + super-loop）
  rti/              1ms 时间片源
  signal/           跨模块信号总线
  log/              四级日志
  can/              CAN 抽象与报文数据库（can_if / can_rx / can_tx / can_db / can_db_codec / can_db_ipk_gen）
  storage/          KV 存储（EEPROM/Flash 抽象）
  mod_template/     业务模块脚手架（复制此目录创建新业务模块）
  <feature>/        每个真实业务模块独占一个子目录
  result.h          错误码定义（c02b2_result_t）
  types.h           平台类型别名（u8/u16/u32/bool/...）
board/              板级配置（厂商 SDK config）
middleware/         第三方库（printf / osif）
platform/           厂商 SDK（devices / drivers）
rtos/               OSIF 适配
EWARM/              IAR 工程
docs/               架构与设计文档
tools/              本地脚本
```

每个业务模块独占一个子目录，目录名即模块名（小写、下划线分隔）。

## 3. 模块五件套

每个业务模块必须实现以下 5 个钩子，通过 `mod_desc_t` 注册到调度器：

```c
typedef struct mod_desc_s {
    const char *name;
    void (*mcu_init)(uint8_t cold_boot);
    void (*wakeup_init)(void);
    void (*on_ign_on)(void);
    void (*tick)(void);
    void (*standby)(void);
} mod_desc_t;
```

| 钩子 | 调用时机 | 用途 |
|---|---|---|
| `mcu_init` | MCU 上电 / 复位（Scheduler_Init 阶段） | 一次性硬件初始化（KAM 冷热启动分支由此参数区分）。早于所有外设与 wakeup |
| `wakeup_init` | Scheduler_WakeupInit 阶段 | 复位后 NVIC 优先级恢复、唤醒源重新 arm。与 mcu_init 解耦 |
| `on_ign_on` | Scheduler_OnIgnOn 阶段（KL15 上电边沿） | 重新加载需要 IGN 状态才能确定的状态 |
| `tick` | 主循环每周期 | 模块主逻辑；内部用 `RTI_SlotElapsed(&slot)` 自决子周期 |
| `standby` | 进入低功耗前 | 释放外设、保存恢复上下文 |

**加新模块的标准动作(4 步)**：
1. 在 `app/<feature>/` 实现上述 5 个钩子（NULL 自动跳过），并在 `app/<feature>/<feature>.h` 中声明 `extern const mod_desc_t mod_<feature>;`
2. 在 `app/scheduler/scheduler.c` 顶部加 `extern const mod_desc_t mod_<feature>;`，并把 `&mod_<feature>` 追加到 `g_sched_modules[]`
3. 在模块 .c 末尾加 `SCHED_REGISTER(mod_<feature>);`（保留符号 + 防 dead-code 消除）
4. **不要**改 `main.c`（`Scheduler_Init` / `Scheduler_WakeupInit` / `Scheduler_OnIgnOn` 已自动遍历注册表）



### 模块注册(SCHED_REGISTER)

`SCHED_REGISTER(mod_xxx)` 是一个跨编译器的纯 C 宏,作用是在模块 .c 里生成一个 `__root static const mod_desc_t *` 指针(指向该模块描述符),让链接器**保留符号**避免被 dead-code 优化掉。`scheduler.c` 维护一个固定大小的指针表 `g_sched_modules[]`,按表项顺序遍历各模块的 mcu_init / wakeup_init / on_ign_on / tick / standby 五个 hook(NULL 自动跳过)。

模块顺序 = `g_sched_modules[]` 数组里的顺序(不是链接顺序)。需要在特定位置跑的模块,直接调整数组项的顺序即可。


## 4. 跨模块通信

跨模块业务数据可直接 extern 全局变量，或由所有方模块暴露 getter/setter。
按需选择（参数校验 / 原子拷贝 / 跨上下文稳定读取），不设单一所有者。

### 4.1 数据传递通道选择

| 通道 | API | 适用 |
|---|---|---|
| **CAN 信号** | Signal_Set / Signal_Get / Signal_GetStored / Signal_IsValid / Signal_Invalidate / Signal_Reset | 207 个 SIG_CAN_* + 3 个 timeout bitmap + 4 个 bus health |
| **业务数据** | extern 全局变量 | 跨模块业务数据可直接读写 |
| **模块 getter/setter** | Module_GetFoo(...) | 需要做参数校验、原子拷贝、跨上下文稳定读取时 |
| **只读查找表 / 校准** | static const in .h | 编译期常量 |
| **持久化配置** | KV_* (pp/storage/kv.h) | 掉电保存的标定 / 用户偏好 |
| **事件** | Event_Post() (后续批次) | 一次性、不需要载荷的信号 |

### 4.2 业务数据走 extern 全局

- 业务模块可以直接使用 extern 全局变量 (结构体、数组、标量), 具体职责由模块自己决定。
- 写入责任可在多个模块之间分担, 由调用方负责保证跨上下文访问的时序安全。

### 4.3 Signal 总线职责

Signal 总线**只**承担 CAN 相关数据:

1. 207 个 SIG_CAN_* (DBC 自动生成);
2. 3 个 SIG_CAN_RX_TIMEOUT_MAP_{LO,HI,HI2} (接收超时 bitmap, **启动期全 1 = 全部超时**);
3. 4 个 SIG_CAN_BUS_OFF / _COUNT / TX_ERR_CNT / RX_ERR_CNT (总线健康, 由 can_if ISR 写入)。

三件套接口定义:

| API | 语义 | 适用 |
|---|---|---|
| Signal_Set(id, raw) | 写入并置 valid=true / ever_set=true | RX 序列内部使用; 业务方不应调用 |
| Signal_Get(id) | valid 时返回 raw; **超时 / 未收 / 越界 → 0 fallback** | 常规使用 |
| Signal_GetStored(id) | **强制**返回最近一次 Set 写入值, 不看 valid | 仪表降级显示、首屏兜底、TX loopback |
| Signal_IsValid(id) | valid && ever_set → true | 门控逻辑 (仅在业务需要「该信号现在可信任」时用) |
| Signal_Invalidate(id) / Signal_InvalidateAll() | 清 valid (保留 value + ever_set) | can_rx 50ms tick 边沿使用 |
| Signal_Reset(id) | 三位全清: value=0 / valid=false / ever_set=false | **仅**给各模块 prv_mcu_init 使用 |

超时状态查询走 pp/can/can_rx.h::CanRx_IsMsgedOut(can_id) / CanRx_GetMsgFreshness(can_id, &out), 不需要直接读 bitmap。

### 4.4 启动期 signal 清零分工

冷启动期不依赖 Signal_InvalidateAll 广播, 而是由各信号的实际写者在自己的 prv_mcu_init 中调 Signal_Reset 专项清零:

| 模块 | 调 Signal_Reset 的 signal | 备注 |
|---|---|---|
| mod_can_rx | 3 个 timeout map + 207 个 SIG_CAN_* | 启动期: timeout map 写 Signal_Set(0xFFFFFFFFu) 全 1 (默认全部超时); SIG_CAN_* 由 Signal_Reset 清零 |
| mod_can_tx | 4 个 SIG_CAN_BUS_OFF_* | 因 can_if 无 mod_desc 注册, 由 can_tx::mcu_init 代为清零 (走 Signal_Reset) |

详细设计及调用参见 docs/SIGNAL_GUIDE.md §2 / §8。

## 5. 错误处理

- 所有公共 API 返回 `c02b2_result_t`（`app/result.h`）
- `C02B2_OK = 0`；其他值均为错误
- 错误码按段划分（Power 0x1xxx / CAN 0x2xxx / Storage 0x3xxx / ...）
- 不要用 `0/-1` 直接做返回值

## 6. 日志规范

- 业务代码只用 `LOG_E/W/I/D`，**不**直接调用 `printf`
- 每个 `.c` 顶部必须 `#define LOG_NAME "<3字母缩写>"`
- 默认等级 `LOG_LVL_INFO`（Release），开发期可临时改为 `LOG_LVL_DEBUG`
- LOG 宏在 `LOG_LEVEL` 之上时**零开销**（编译期消除参数）

## 7. 时间管理

- 全局 1ms tick 由 `SysTick_Handler`（位于 `app/rti/rti.c`）产生，ISR 内调用 `RTI_OnTick1ms()`，并驱动 OSIF 滴答与 WDG 喂狗
- 模块**不**持有自己的 RTI 标志变量，全部用 `RTI_OpenSlot(period)` + `RTI_SlotElapsed(&slot)`
- 周期枚举见 `app/rti/rti.h`：5/10/20/50/100/250/500/1000 ms
- slot 池默认 64 个（`RTI_SLOT_POOL_SIZE`），多模块同 period 互不干扰；超出会 LOG_W 并跳过

## 8. 编码风格

- 缩进 4 空格，禁止 Tab
- 行宽 ≤ 120 字符
- 括号同行（K&R 风格）
- 函数命名：`Module_Action()`（下划线分隔，PascalCase 词）
- 变量命名：`snake_case`
- 类型命名：`snake_case_t`
- 宏命名：`C02B2_UPPER_SNAKE`
- 详见 `.clang-format`

## 9. 提交规范

参见 `docs/COMMIT_CONVENTION.md`。简单约定：

```
<type>(<scope>): <subject>

<body>
```

- `type`: `feat / fix / refactor / docs / test / build / ci / chore`
- `subject` 中文不超过 30 字
- `body` 列出主要改动点

## 10. 禁止事项

- ❌ 业务代码中 `#include "flexcan_driver.h"` 等具体驱动头 — 走 `can/can_if.h`

- ❌ printf / sprintf 出现在业务代码 (只能用于日志)
- ❌ 在 tick() 中做阻塞延时 (OSIF_TimeDelay 不允许)
- ❌ 修改 platform/ 或 CMSIS/ 下的厂商文件



## 11. 版本
- 当前版本：`v0.3`
- 历史变更：见 docs/CHANGELOG.md
