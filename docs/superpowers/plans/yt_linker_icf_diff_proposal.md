# yt_linker.icf 改动草稿

> 用于 review,确认 OK 后再 patch 原文件。
> 路径:oard/yt_linker.icf(由 EWARM/C02_B2.ewp line 832 引用)
> IAR 版本:9.40.1

## 目标

加一个 .sched_modules section 聚合所有模块描述符指针,
并通过 __sched_modules_start/end 两个 exported symbol
让 pp/scheduler/scheduler.c 在启动时遍历。

## 改动 1:在 TEXT_block 里加 .sched_modules section

**位置**:oard/yt_linker.icf 第 88-95 行附近(lock TEXT_block)

**原文**:
`
define block TEXT_block with fixed order, alignment = 8
{
    section TEXT_start_section, 
    section rodata_region_start_section,
    block rodata  {  section .rodata ,section .rodata* },
    section rodata_region_end_section,
    section text_region_start_section,
    block text  {  section .text ,section .text* },
    section text_region_end_section,
    section TEXT_end_section
};
`

**改为**:
`
define block TEXT_block with fixed order, alignment = 8
{
    section TEXT_start_section, 
    section rodata_region_start_section,
    block rodata  {  section .rodata ,section .rodata* },
    section rodata_region_end_section,
    section text_region_start_section,
    block text  {  section .text ,section .text* },
    block sched_modules { section .sched_modules },   /* 模块描述符表(SCHED_REGISTER 宏填充) */
    section text_region_end_section,
    section TEXT_end_section
};
`

**理由**:
- const mod_desc_t * const 是只读指针,放 TEXT 段合理
- 放 	ext block 之内、	ext_region_end_section 之前,
  确保指针数组在 .text 之后,便于调试器定位
- 用单独 lock sched_modules(而不是直接加 section .sched_modules 到 text block 里),
  这样如果以后想加到独立 region 可以无缝迁移

## 改动 2:加 2 个 exported symbol 标记段首尾

**位置**:oard/yt_linker.icf 末尾(do not initialize { ... }; 之后)

**追加**:
`
/* Scheduler module registry boundaries (populated by SCHED_REGISTER macro).
 * Scheduler_Init / WakeupInit / OnIgnOn / Run / Standby walk the range
 * [__sched_modules_start, __sched_modules_end). */
define exported symbol __sched_modules_start = start of section ".sched_modules";
define exported symbol __sched_modules_end   = end of section   ".sched_modules";
`

## 不动的地方

- 内存 region 定义(TEXT / RAM / STACK 等)
- IVT / 中断向量
- lock DATA_RAM_block / BSS_block
- initialize manually / do not initialize
- 其他所有 lock XXX_block

## 风险评估

| 风险 | 概率 | 影响 | 缓解 |
|---|---|---|---|
| .sched_modules 与现有 section 重名 | 0 | - | grep .icf 已确认无重名 |
| __sched_modules_* symbol 与现有 symbol 冲突 | 0 | - | grep .icf 无 __sched_modules_* 前缀 |
| ILINK 不识别新加 section | 极低 | link 错 | IAR 9.40.1 支持 section place 语法 |
| lock text 增加 section 后超 TEXT 区域大小 | 0 | - | 4 个模块指针 = 32 字节,可忽略 |

## 验证步骤(patch 后)

1. IAR 全量 build(Project → Rebuild All)
2. 0 错 0 警
3. 启动日志包含 init: 4 modules(当前是手写 4 个)
4. 烧录跑 demo,行为未变
