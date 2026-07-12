/**
 * @file    mod_rti_demo.h
 * @brief   RTI concurrent-slot demo module
 *
 * 演示两个私有 RTI slot（都绑定到 RTI_10MS）独立触发
 * （每个 slot 各自维护 last_ms，并由 RTI_SlotElapsed 独立求值）。
 *
 * 编译期开关（见 mod_rti_demo.c）：
 *   MOD_RTI_DEMO_EN = 0（默认） —— 模块描述符已注册，
 *                                  但钩子为 no-op。
 *   MOD_RTI_DEMO_EN = 1          —— 完整 demo，两个 10 ms slot，
 *                                  每 1 s 打印计数器。
 *
 * 作为模块作者添加周期任务的参考模板。详见
 * docs/superpowers/specs/2026-07-09-sched-rti-design.md。
 */
#ifndef C02B2_MOD_RTI_DEMO_H
#define C02B2_MOD_RTI_DEMO_H

#include "scheduler.h"

extern const mod_desc_t mod_rti_demo;

#endif /* C02B2_MOD_RTI_DEMO_H */
