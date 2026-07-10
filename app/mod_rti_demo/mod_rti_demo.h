/**
 * @file    mod_rti_demo.h
 * @brief   RTI concurrent-slot demo module
 *
 * Demonstrates that two private RTI slots, both bound to
 * RTI_10MS, fire independently (each keeps its own last_ms and
 * is evaluated independently by RTI_SlotElapsed).
 *
 * Compile-time switch (see mod_rti_demo.c):
 *   MOD_RTI_DEMO_EN = 0 (default) - module descriptor registered
 *                                  but hooks are no-ops.
 *   MOD_RTI_DEMO_EN = 1            - full demo, two 10 ms slots,
 *                                  counter log every 1 s.
 *
 * Used as a reference template for module authors adding periodic
 * tasks. See docs/superpowers/specs/2026-07-09-sched-rti-design.md.
 */
#ifndef C02B2_MOD_RTI_DEMO_H
#define C02B2_MOD_RTI_DEMO_H

#include "scheduler.h"

extern const mod_desc_t mod_rti_demo;

#endif /* C02B2_MOD_RTI_DEMO_H */
