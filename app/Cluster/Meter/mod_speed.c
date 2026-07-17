/**
 * @file    mod_speed.c
 * @brief   车速表模块描述符 - 通过SCHED_REGISTER注册到C02调度器
 * @brief   Speedometer Module Descriptor
 *
 * 调度周期: 20ms (车速输入+计算+输出)
 */
#include "types.h"
#include "mod_speed.h"
#include "SPD_INPUT.h"
#include "SPD_CALCULATE.h"
#include "SPD_OUTPUT.h"
#include "rti.h"

#define MOD_NAME  "SPD"
#include "log.h"

static rti_slot_t s_slot_20ms;
static u8 s_init_done;

static void prv_mcu_init(u8 cold_boot)
{
    s_slot_20ms = RTI_OpenSlot(RTI_20MS);

    SPD_INPUT_INIT_RESET(cold_boot);
    SPD_CALC_INIT_RESET(cold_boot);
    SPD_OUTPUT_INIT_RESET(cold_boot);

    s_init_done = 1;
    LOG_I("init (cold_boot=%u)", (unsigned)cold_boot);
}

static void prv_wakeup_init(void)
{
    LOG_I("wakeup_init");
}

static void prv_on_ign_on(void)
{
    SPD_INPUT_INIT_IGN();
    SPD_CALC_INIT_IGN();
    SPD_OUTPUT_INIT_IGN();
    LOG_I("on_ign_on");
}

static void prv_tick(void)
{
    if (!s_init_done) return;

    if (RTI_SlotElapsed(&s_slot_20ms)) {
        SPD_INPUT();
        SPD_CALCULATE();
        SPD_MOVE();
    }
}

static void prv_standby(void)
{
    SPD_INPUT_STANDBY();
    SPD_CALC_STANDBY();
    SPD_OUTPUT_STANDBY();
    LOG_I("standby");
}

const mod_desc_t mod_speed = {
    .name      = "speed",
    .mcu_init   = prv_mcu_init,
    .wakeup_init = prv_wakeup_init,
    .on_ign_on = prv_on_ign_on,
    .tick      = prv_tick,
    .standby   = prv_standby,
};

SCHED_REGISTER(mod_speed);
