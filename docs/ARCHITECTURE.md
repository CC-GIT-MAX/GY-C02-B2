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

## 3. 模块四件套

每个业务模块必须实现以下 4 个钩子，通过 `mod_desc_t` 注册到调度器：

```c
typedef struct mod_desc_s {
    const char *name;
    void (*init)(uint8_t cold_boot);
    void (*on_ign_on)(void);
    void (*tick)(void);
    void (*standby)(void);
} mod_desc_t;
```

| 钩子 | 调用时机 | 用途 |
|---|---|---|
| `init` | MCU 上电 / 复位 | 一次性初始化（KAM 冷热启动分支由此参数区分） |
| `on_ign_on` | KL15 上电边沿 | 重新加载需要 IGN 状态才能确定的状态 |
| `tick` | 主循环每周期 | 模块主逻辑；内部用 `RTI_IsElapsed()` 自决子周期 |
| `standby` | 进入低功耗前 | 释放外设、保存恢复上下文 |

**加新模块的标准动作（3 步）**：
1. 在 `app/<feature>/` 实现上述 4 个函数 + `extern const mod_desc_t mod_<feature>;`
2. 在 `app/scheduler/scheduler.c` 的 `g_modules[]` 追加 `&mod_<feature>`
3. **不要**改 `main.c`

## 4. 跨模块通信

- **禁止** `extern` 全局变量出现在 `.c` 文件中
- 跨模块可变状态走 `Signal_*`（`app/signal/signal.h`）
- 一次性事件走 `Event_Post()`（后续批次添加）
- 模块私有常量放在 `.h` 中以 `static const` 或宏定义

## 5. 错误处理

- 所有公共 API 返回 `lbx_result_t`（`app/result.h`）
- `LBX_OK = 0`；其他值均为错误
- 错误码按段划分（Power 0x1xxx / CAN 0x2xxx / Storage 0x3xxx / ...）
- 不要用 `0/-1` 直接做返回值

## 6. 日志规范

- 业务代码只用 `LOG_E/W/I/D`，**不**直接调用 `printf`
- 每个 `.c` 顶部必须 `#define LOG_NAME "<3字母缩写>"`
- 默认等级 `LOG_LVL_INFO`（Release），开发期可临时改为 `LOG_LVL_DEBUG`
- LOG 宏在 `LOG_LEVEL` 之上时**零开销**（编译期消除参数）

## 7. 时间管理

- 全局 1ms tick 由 LPTMR 产生，ISR 中调用 `RTI_OnTick1ms()`
- 模块**不**持有自己的 RTI 标志变量，全部用 `RTI_IsElapsed(period)`
- 周期枚举见 `app/rti/rti.h`：5/10/20/50/100/250/500/1000 ms
- 新增周期需要修改 `rti.c` 的 `RTI_IsElapsed` slot 表

## 8. 编码风格

- 缩进 4 空格，禁止 Tab
- 行宽 ≤ 120 字符
- 括号同行（K&R 风格）
- 函数命名：`Module_Action()`（下划线分隔，PascalCase 词）
- 变量命名：`snake_case`
- 类型命名：`snake_case_t`
- 宏命名：`LBX_UPPER_SNAKE`
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

- ❌ 业务代码中 `#include "flexcan_driver.h"` 等具体驱动头——走 `can/can_if.h`
- ❌ `.c` 文件中 `extern` 任何变量
- ❌ `printf` / `sprintf` 出现在业务代码（只能用于日志）
- ❌ 在 `tick()` 中做阻塞延时（`OSIF_TimeDelay` 不允许）
- ❌ 修改 `platform/` 或 `CMSIS/` 下的厂商文件

## 11. 版本与变更记录

- 当前版本：`v0.0.0-dev`
- 修订记录：本文件随批次执行逐步完善
