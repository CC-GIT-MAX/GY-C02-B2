/**
 * @file    signal_test.c
 * @brief   Pure-C unit tests for signal bus
 *
 * Designed to run in two environments:
 *   1. Host build (gcc/clang) for CI
 *   2. On-target (IAR) by calling Signal_TestRun() from a debug menu
 *
 * Uses a hand-rolled minimal assert to avoid <assert.h> side effects
 * in the IAR build.
 */
#include "signal.h"
#include "types.h"

#define LOG_NAME  "TEST"
#include "log.h"

/* Test result counters. */
static u32 s_failures = 0;
static u32 s_passed   = 0;

/**
 * @brief   Hand-rolled assertion macro.
 * @brief   自实现的断言宏
 *
 * @details Increments the global pass/fail counters and logs the
 *          failure with file/line. Avoids <assert.h> which would
 *          call abort() and break IAR builds.
 */
#define EXPECT(cond)                                                       \
    do {                                                                   \
        if (!(cond)) {                                                     \
            s_failures++;                                                  \
            LOG_E("FAIL: %s @ %s:%d", #cond, __FILE__, __LINE__);          \
        } else {                                                           \
            s_passed++;                                                    \
        }                                                                  \
    } while (0)

/** @brief  Test set/get round-trip and initial-validity state. */
static void test_basic_set_get(void)
{
    /* Unset signal returns 0 and reports invalid. */
    EXPECT(Signal_Get(SIG_VEH_SPEED_KPH_X10) == 0);
    EXPECT(Signal_IsValid(SIG_VEH_SPEED_KPH_X10) == false);

    /* Set then read-back: value matches, valid is true. */
    EXPECT(Signal_Set(SIG_VEH_SPEED_KPH_X10, 650) == LBX_OK);  /* 65.0 kph */
    EXPECT(Signal_Get(SIG_VEH_SPEED_KPH_X10) == 650);
    EXPECT(Signal_IsValid(SIG_VEH_SPEED_KPH_X10) == true);
}

/** @brief  Test Signal_Invalidate clears the valid bit only. */
static void test_invalidate(void)
{
    EXPECT(Signal_Set(SIG_IGN_ON, 1) == LBX_OK);
    EXPECT(Signal_IsValid(SIG_IGN_ON) == true);
    /* After invalidate, valid is false (value is implementation-defined). */
    Signal_Invalidate(SIG_IGN_ON);
    EXPECT(Signal_IsValid(SIG_IGN_ON) == false);
}

/** @brief  Test out-of-range / SIG_INVALID ids are rejected. */
static void test_invalid_id(void)
{
    /* Set on invalid ids returns PARAM error. */
    EXPECT(Signal_Set(SIG_INVALID, 0) == LBX_ERR_PARAM);
    EXPECT(Signal_Set((signal_id_t)9999, 0) == LBX_ERR_PARAM);
    /* Get on invalid id returns 0 (safe default). */
    EXPECT(Signal_Get(SIG_INVALID) == 0);
    EXPECT(Signal_IsValid(SIG_INVALID) == false);
    /* Invalidate with bad id should be silent (no crash). */
    Signal_Invalidate(SIG_INVALID);
    Signal_Invalidate((signal_id_t)9999);
}

/** @brief  Test Signal_InvalidateAll clears every slot. */
static void test_invalidate_all(void)
{
    EXPECT(Signal_Set(SIG_IGN_ON, 1) == LBX_OK);
    EXPECT(Signal_Set(SIG_FUEL_LEVEL_PCT, 50) == LBX_OK);
    /* Bulk invalidate wipes valid bits across the whole bus. */
    Signal_InvalidateAll();
    EXPECT(Signal_IsValid(SIG_IGN_ON) == false);
    EXPECT(Signal_IsValid(SIG_FUEL_LEVEL_PCT) == false);
}

/**
 * @brief   Run all signal-bus self-tests
 * @brief   运行所有信号总线自检
 *
 * @details Resets the pass/fail counters, runs every test in order,
 *          logs the summary, and returns the failure count.
 *
 * @return  uint32_t  Number of failed assertions (0 = all passed)
 */
u32 Signal_TestRun(void)
{
    s_failures = 0;
    s_passed   = 0;
    test_basic_set_get();
    test_invalidate();
    test_invalid_id();
    test_invalidate_all();
    LOG_I("signal test: %u passed, %u failed", s_passed, s_failures);
    return s_failures;
}
