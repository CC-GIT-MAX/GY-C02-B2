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

static u32 s_failures = 0;
static u32 s_passed   = 0;

#define EXPECT(cond)                                                       \
    do {                                                                   \
        if (!(cond)) {                                                     \
            s_failures++;                                                  \
            LOG_E("FAIL: %s @ %s:%d", #cond, __FILE__, __LINE__);          \
        } else {                                                           \
            s_passed++;                                                    \
        }                                                                  \
    } while (0)

static void test_basic_set_get(void)
{
    EXPECT(Signal_Get(SIG_VEH_SPEED_KPH_X10) == 0);
    EXPECT(Signal_IsValid(SIG_VEH_SPEED_KPH_X10) == false);

    EXPECT(Signal_Set(SIG_VEH_SPEED_KPH_X10, 650) == LBX_OK);  /* 65.0 kph */
    EXPECT(Signal_Get(SIG_VEH_SPEED_KPH_X10) == 650);
    EXPECT(Signal_IsValid(SIG_VEH_SPEED_KPH_X10) == true);
}

static void test_invalidate(void)
{
    EXPECT(Signal_Set(SIG_IGN_ON, 1) == LBX_OK);
    EXPECT(Signal_IsValid(SIG_IGN_ON) == true);
    Signal_Invalidate(SIG_IGN_ON);
    EXPECT(Signal_IsValid(SIG_IGN_ON) == false);
}

static void test_invalid_id(void)
{
    EXPECT(Signal_Set(SIG_INVALID, 0) == LBX_ERR_PARAM);
    EXPECT(Signal_Set((signal_id_t)9999, 0) == LBX_ERR_PARAM);
    EXPECT(Signal_Get(SIG_INVALID) == 0);
    EXPECT(Signal_IsValid(SIG_INVALID) == false);
    /* Invalidate with bad id should be silent (no crash) */
    Signal_Invalidate(SIG_INVALID);
    Signal_Invalidate((signal_id_t)9999);
}

static void test_invalidate_all(void)
{
    EXPECT(Signal_Set(SIG_IGN_ON, 1) == LBX_OK);
    EXPECT(Signal_Set(SIG_FUEL_LEVEL_PCT, 50) == LBX_OK);
    Signal_InvalidateAll();
    EXPECT(Signal_IsValid(SIG_IGN_ON) == false);
    EXPECT(Signal_IsValid(SIG_FUEL_LEVEL_PCT) == false);
}

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
