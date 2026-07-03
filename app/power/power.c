/**
 * @file    power.c
 * @brief   Power management module implementation
 *
 *  - Owns IGN state and KL30 voltage.
 *  - Publishes SIG_IGN_ON, SIG_KL30_VOLTAGE_MV, SIG_PWR_MODE.
 *  - Tracks time since IGN OFF for graceful shutdown.
 *  - Aggregates sleep-readiness: SIG_SLEEP_READY = true when
 *    IGN off long enough AND no module requested stay-awake.
 */
#include "power.h"
#include "board_power.h"
#include "rti.h"
#include "signal.h"

#define LOG_NAME  "PWR "
#include "log.h"

/* Private state --------------------------------------------------------- */
typedef struct {
    u8         init_done;

    /* IGN debounce */
    u8         ign_raw;              /* 0 / 1 from last ADC read        */
    u8         ign_debounce;         /* counter                         */
    bool       ign_on;
    bool       ign_on_prev;

    /* Voltage */
    u16        bat_mv;
    pwr_mode_t mode;
    pwr_mode_t mode_prev;

    /* IGN off counter (in 100ms ticks) */
    u16        ign_off_ticks_100ms;

    /* Sleep readiness */
    u8         sleep_blockers;       /* 0 = can sleep                  */
} pwr_ctx_t;

static pwr_ctx_t s_ctx;

/* Forward declarations -------------------------------------------------- */
static void prv_publish_state(void);
static pwr_mode_t prv_classify_voltage(u16 mv);

/* mod_desc_t hooks ----------------------------------------------------- */
static void prv_init(u8 cold_boot)
{
    (void)cold_boot;
    s_ctx.init_done         = 1u;
    s_ctx.ign_raw           = 0u;
    s_ctx.ign_debounce       = 0u;
    s_ctx.ign_on             = false;
    s_ctx.ign_on_prev        = false;
    s_ctx.bat_mv             = 0u;
    s_ctx.mode               = PWR_MODE_NORMAL;
    s_ctx.mode_prev          = PWR_MODE_NORMAL;
    s_ctx.ign_off_ticks_100ms = 0u;
    s_ctx.sleep_blockers     = 0u;

    Board_RegisterIgnIrq(Power_OnIgnEdgeIrq);

    /* Read initial voltage so other modules see a valid value */
    u16 raw = Board_ADC_ReadRaw(BOARD_ADC_CH_KL30);
    s_ctx.bat_mv = Board_VoltageDivider_mV(raw);
    s_ctx.mode   = prv_classify_voltage(s_ctx.bat_mv);
    prv_publish_state();

    LOG_I("init: bat=%u mV, mode=%d, cold=%u", s_ctx.bat_mv, (int)s_ctx.mode, (unsigned)cold_boot);
}

static void prv_on_ign_on(void)
{
    s_ctx.ign_off_ticks_100ms = 0u;
    LOG_I("IGN ON (debounced)");
}

static void prv_tick(void)
{
    if (!s_ctx.init_done) {
        return;
    }

    /* 10ms: sample ADC, debounce IGN, classify voltage */
    if (RTI_IsElapsed(RTI_10MS)) {
        u16 raw_bat = Board_ADC_ReadRaw(BOARD_ADC_CH_KL30);
        s_ctx.bat_mv = Board_VoltageDivider_mV(raw_bat);

        u16 raw_ign = Board_ADC_ReadRaw(BOARD_ADC_CH_IGN);
        u8  ign_raw = (raw_ign > (BOARD_VREF_MV / 2u)) ? 1u : 0u;
        if (ign_raw != s_ctx.ign_raw) {
            s_ctx.ign_raw     = ign_raw;
            s_ctx.ign_debounce = 0u;
        } else if (s_ctx.ign_debounce < 0xFFu) {
            s_ctx.ign_debounce++;
            if (s_ctx.ign_debounce >= PWR_IGN_DEBOUNCE_TICK) {
                bool new_ign = (ign_raw != 0u);
                if (new_ign != s_ctx.ign_on) {
                    s_ctx.ign_on_prev = s_ctx.ign_on;
                    s_ctx.ign_on      = new_ign;
                    if (s_ctx.ign_on) {
                        /* Re-broadcast immediately; higher layers
                         * will see the rising edge in Signal_Get */
                        Signal_Set(SIG_IGN_ON, 1);
                        /* Edge event is handled via prv_on_ign_on
                         * which is invoked from Scheduler_OnIgnOn() */
                    } else {
                        Signal_Set(SIG_IGN_ON, 0);
                    }
                }
            }
        }
    }

    /* 100ms: re-classify, update IGN off counter, publish */
    if (RTI_IsElapsed(RTI_100MS)) {
        s_ctx.mode = prv_classify_voltage(s_ctx.bat_mv);
        if (s_ctx.ign_on) {
            s_ctx.ign_off_ticks_100ms = 0u;
        } else if (s_ctx.ign_off_ticks_100ms < 0xFFFFu) {
            s_ctx.ign_off_ticks_100ms++;
        }
        prv_publish_state();

        if (s_ctx.mode != s_ctx.mode_prev) {
            LOG_W("mode: %d -> %d (bat=%u mV)", (int)s_ctx.mode_prev,
                  (int)s_ctx.mode, s_ctx.bat_mv);
            s_ctx.mode_prev = s_ctx.mode;
        }
    }
}

static void prv_standby(void)
{
    LOG_I("standby: ign=%d, blockers=%u, off_ticks=%u",
          (int)s_ctx.ign_on, (unsigned)s_ctx.sleep_blockers,
          (unsigned)s_ctx.ign_off_ticks_100ms);
}

/* Helpers --------------------------------------------------------------- */
static pwr_mode_t prv_classify_voltage(u16 mv)
{
    if (mv == 0u) {
        return PWR_MODE_FAULT;
    }
    if (mv < PWR_UV2_ENTER_MV) {
        return PWR_MODE_UV2;
    }
    if (mv < PWR_UV1_ENTER_MV) {
        return PWR_MODE_UV1;
    }
    if (mv > PWR_OV2_ENTER_MV) {
        return PWR_MODE_OV2;
    }
    if (mv > PWR_OV1_ENTER_MV) {
        return PWR_MODE_OV1;
    }
    return PWR_MODE_NORMAL;
}

static void prv_publish_state(void)
{
    (void)Signal_Set(SIG_KL30_VOLTAGE_MV, (int32_t)s_ctx.bat_mv);
    (void)Signal_Set(SIG_PWR_MODE,        (int32_t)s_ctx.mode);
    (void)Signal_Set(SIG_IGN_ON,          s_ctx.ign_on ? 1 : 0);
    (void)Signal_Set(SIG_IGN_OFF_COUNTER, (int32_t)s_ctx.ign_off_ticks_100ms);

    /* Sleep readiness: no IGN for >= 2s AND no blockers */
    bool ready = (!s_ctx.ign_on)
              && (s_ctx.ign_off_ticks_100ms >= 20u)
              && (s_ctx.sleep_blockers == 0u)
              && (s_ctx.mode == PWR_MODE_NORMAL);
    (void)Signal_Set(SIG_SLEEP_READY, ready ? 1 : 0);
}

/* Public API ------------------------------------------------------------ */
void Power_OnIgnEdgeIrq(void)
{
    /* ISR: just mark that we saw an edge; full debounce in tick() */
    s_ctx.ign_debounce = 0u;
}

void Power_OnCanBusActivity(void)
{
    /* Any CAN frame extends the stay-awake window */
    if (s_ctx.ign_off_ticks_100ms < 0xFFFFu) {
        s_ctx.ign_off_ticks_100ms = 0u;
    }
    (void)Signal_Set(SIG_SLEEP_READY, 0);
}

void Power_RequestSleep(void)
{
    if (s_ctx.sleep_blockers < 0xFFu) {
        s_ctx.sleep_blockers++;
    }
    (void)Signal_Set(SIG_SLEEP_READY, 0);
}

void Power_ClearSleepRequest(void)
{
    if (s_ctx.sleep_blockers > 0u) {
        s_ctx.sleep_blockers--;
    }
    /* Recompute and publish */
    prv_publish_state();
}

pwr_mode_t Power_GetMode(void)
{
    return s_ctx.mode;
}

bool Power_IsIgnOn(void)
{
    return s_ctx.ign_on;
}

bool Power_IsSleepReady(void)
{
    return Signal_Get(SIG_SLEEP_READY) != 0;
}

/* Module descriptor ----------------------------------------------------- */
const mod_desc_t mod_power = {
    .name      = "power",
    .init      = prv_init,
    .on_ign_on = prv_on_ign_on,
    .tick      = prv_tick,
    .standby   = prv_standby,
};
