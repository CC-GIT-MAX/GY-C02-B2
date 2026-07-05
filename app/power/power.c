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

/** @brief  Push the current state to the signal bus. */
static void prv_publish_state(void);

/** @brief  Classify a battery voltage sample into pwr_mode_t. */
static pwr_mode_t prv_classify_voltage(u16 mv);

/* mod_desc_t hooks ----------------------------------------------------- */

/**
 * @brief   mod_desc_t init hook: load initial ADC samples, register IGN IRQ
 * @brief   mod_desc_t init 钩子：读取初始 ADC 样本，注册 IGN 中断
 *
 * @param[in]  cold_boot  1 = cold boot (KAM lost), 0 = warm boot
 */
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

    /* Register the IGN edge IRQ callback. The handler (Power_OnIgnEdgeIrq)
     * simply resets the debounce counter so the tick can re-sample immediately. */
    Board_RegisterIgnIrq(Power_OnIgnEdgeIrq);

    /* Read initial KL30 voltage once so other modules see a valid value
     * before the first 10 ms tick. */
    u16 raw = Board_ADC_ReadRaw(BOARD_ADC_CH_KL30);
    s_ctx.bat_mv = Board_VoltageDivider_mV(raw);
    s_ctx.mode   = prv_classify_voltage(s_ctx.bat_mv);
    prv_publish_state();

    LOG_I("init: bat=%u mV, mode=%d, cold=%u", s_ctx.bat_mv, (int)s_ctx.mode, (unsigned)cold_boot);
}

/**
 * @brief   mod_desc_t on_ign_on hook: reset IGN off counter
 * @brief   mod_desc_t on_ign_on 钩子：清零 IGN off 计数器
 *
 * @details Clearing ign_off_ticks_100ms here ensures the
 *          sleep-aggregation logic (which requires >= 2 s of IGN-off
 *          before allowing sleep) starts fresh on every IGN cycle.
 */
static void prv_on_ign_on(void)
{
    s_ctx.ign_off_ticks_100ms = 0u;
    LOG_I("IGN ON (debounced)");
}

/**
 * @brief   mod_desc_t tick hook: sample ADC @ 10 ms, publish @ 100 ms
 * @brief   mod_desc_t tick 钩子：10ms 采 ADC，100ms 发布状态
 *
 * @details Two sub-periods:
 *   - 10 ms: raw ADC read + IGN debounce + voltage classify
 *   - 100 ms: re-classify + IGN-off counter tick + signal publish
 *
 * The debounce filter requires PWR_IGN_DEBOUNCE_TICK (3) consecutive
 * identical samples to assert a new IGN state. ISR (Power_OnIgnEdgeIrq)
 * resets the counter to 0 to force re-sampling on the next tick.
 */
static void prv_tick(void)
{
    if (!s_ctx.init_done) {
        return;
    }

    /* 10ms sub-task: sample ADC + debounce IGN. */
    if (RTI_IsElapsed(RTI_10MS)) {
        u16 raw_bat = Board_ADC_ReadRaw(BOARD_ADC_CH_KL30);
        s_ctx.bat_mv = Board_VoltageDivider_mV(raw_bat);

        u16 raw_ign = Board_ADC_ReadRaw(BOARD_ADC_CH_IGN);
        /* Compare against Vref/2 to decide raw 0/1. */
        u8  ign_raw = (raw_ign > (BOARD_VREF_MV / 2u)) ? 1u : 0u;
        if (ign_raw != s_ctx.ign_raw) {
            /* Raw state changed: reset the debounce counter. */
            s_ctx.ign_raw     = ign_raw;
            s_ctx.ign_debounce = 0u;
        } else if (s_ctx.ign_debounce < 0xFFu) {
            /* Same as last sample: increment debounce counter. */
            s_ctx.ign_debounce++;
            if (s_ctx.ign_debounce >= PWR_IGN_DEBOUNCE_TICK) {
                /* Threshold reached: accept the new state. */
                bool new_ign = (ign_raw != 0u);
                if (new_ign != s_ctx.ign_on) {
                    s_ctx.ign_on_prev = s_ctx.ign_on;
                    s_ctx.ign_on      = new_ign;
                    if (s_ctx.ign_on) {
                        /* Re-broadcast immediately so Signal_Get reflects
                         * the new state without waiting for 100 ms tick. */
                        Signal_Set(SIG_IGN_ON, 1);
                        /* The Scheduler_OnIgnOn() broadcast is triggered
                         * by main / board IRQ, not here. */
                    } else {
                        Signal_Set(SIG_IGN_ON, 0);
                    }
                }
            }
        }
    }

    /* 100ms sub-task: re-classify, tick IGN-off counter, publish. */
    if (RTI_IsElapsed(RTI_100MS)) {
        s_ctx.mode = prv_classify_voltage(s_ctx.bat_mv);
        if (s_ctx.ign_on) {
            /* IGN is on: keep counter at 0. */
            s_ctx.ign_off_ticks_100ms = 0u;
        } else if (s_ctx.ign_off_ticks_100ms < 0xFFFFu) {
            /* IGN is off: increment up to a safe ceiling. */
            s_ctx.ign_off_ticks_100ms++;
        }
        prv_publish_state();

        /* Log only on mode change to avoid spam. */
        if (s_ctx.mode != s_ctx.mode_prev) {
            LOG_W("mode: %d -> %d (bat=%u mV)", (int)s_ctx.mode_prev,
                  (int)s_ctx.mode, s_ctx.bat_mv);
            s_ctx.mode_prev = s_ctx.mode;
        }
    }
}

/**
 * @brief   mod_desc_t standby hook: log the current shutdown state
 * @brief   mod_desc_t standby 钩子：记录当前关机状态
 */
static void prv_standby(void)
{
    LOG_I("standby: ign=%d, blockers=%u, off_ticks=%u",
          (int)s_ctx.ign_on, (unsigned)s_ctx.sleep_blockers,
          (unsigned)s_ctx.ign_off_ticks_100ms);
}

/* Helpers --------------------------------------------------------------- */

/**
 * @brief   Classify a battery voltage sample into pwr_mode_t
 * @brief   将电池电压采样分类到 pwr_mode_t
 *
 * @details Linear scan through the thresholds. The check order
 *          (UV2 -> UV1 -> OV1 -> OV2 -> NORMAL) is important:
 *          UV2 < UV1 < OV1 < OV2 in millivolts, so a single
 *          mv value can only match at most one bucket.
 *
 * @param[in]  mv  Battery voltage in mV
 *
 * @return  pwr_mode_t  Classified mode
 */
static pwr_mode_t prv_classify_voltage(u16 mv)
{
    /* Zero reading typically means ADC not ready or wire broken. */
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

/**
 * @brief   Push the current state to the signal bus
 * @brief   将当前状态发布到信号总线
 *
 * @details Sleep-readiness aggregation logic (AND of 4 conditions):
 *   1. IGN is currently OFF (debounced)
 *   2. IGN has been off for at least 2 s (ign_off_ticks_100ms >= 20)
 *   3. No module holds a sleep blocker
 *   4. Voltage is in NORMAL range (not UV/OV/FAULT)
 * This 4-way AND protects against single-source false positives.
 */
static void prv_publish_state(void)
{
    (void)Signal_Set(SIG_KL30_VOLTAGE_MV, (int32_t)s_ctx.bat_mv);
    (void)Signal_Set(SIG_PWR_MODE,        (int32_t)s_ctx.mode);
    (void)Signal_Set(SIG_IGN_ON,          s_ctx.ign_on ? 1 : 0);
    (void)Signal_Set(SIG_IGN_OFF_COUNTER, (int32_t)s_ctx.ign_off_ticks_100ms);

    /* Sleep readiness: no IGN for >= 2s AND no blockers AND mode NORMAL. */
    bool ready = (!s_ctx.ign_on)
              && (s_ctx.ign_off_ticks_100ms >= 20u)
              && (s_ctx.sleep_blockers == 0u)
              && (s_ctx.mode == PWR_MODE_NORMAL);
    (void)Signal_Set(SIG_SLEEP_READY, ready ? 1 : 0);
}

/* Public API ------------------------------------------------------------ */

/**
 * @brief   Notify the power module of an IGN edge (called from ISR)
 * @brief   通知电源模块发生 IGN 边沿（由 ISR 调用）
 *
 * @details ISR-side optimization: rather than implementing a full
 *          debounce in interrupt context, we just clear the counter
 *          so the tick thread re-samples on its very next 10 ms pass.
 */
void Power_OnIgnEdgeIrq(void)
{
    /* ISR: just mark that we saw an edge; full debounce in tick(). */
    s_ctx.ign_debounce = 0u;
}

/**
 * @brief   Signal CAN bus activity to keep the cluster awake
 * @brief   通知电源模块 CAN 总线有活动以保持唤醒
 *
 * @details Called by can_rx callbacks for every received frame;
 *          resets the IGN off counter so sleep-aggregation is
 *          pushed back to the "needs 2 s of silence" requirement.
 */
void Power_OnCanBusActivity(void)
{
    /* Any CAN frame extends the stay-awake window. */
    if (s_ctx.ign_off_ticks_100ms < 0xFFFFu) {
        s_ctx.ign_off_ticks_100ms = 0u;
    }
    (void)Signal_Set(SIG_SLEEP_READY, 0);
}

/**
 * @brief   Mark the caller as a sleep blocker
 * @brief   将本调用方标记为阻止休眠
 *
 * @details Reference counter pattern: multiple modules can hold
 *          a blocker; the cluster only sleeps when the count
 *          returns to zero (all blockers released).
 */
void Power_RequestSleep(void)
{
    if (s_ctx.sleep_blockers < 0xFFu) {
        s_ctx.sleep_blockers++;
    }
    /* Immediately clear the sleep signal so the consumer reacts now. */
    (void)Signal_Set(SIG_SLEEP_READY, 0);
}

/**
 * @brief   Release a previously held sleep blocker
 * @brief   释放先前持有的休眠阻止请求
 *
 * @details After decrementing, recompute and publish the
 *          sleep-readiness signal so main() can re-evaluate.
 */
void Power_ClearSleepRequest(void)
{
    if (s_ctx.sleep_blockers > 0u) {
        s_ctx.sleep_blockers--;
    }
    /* Recompute and publish. */
    prv_publish_state();
}

/**
 * @brief   Get the current power-mode classification
 * @brief   获取当前电源模式分类
 *
 * @return  pwr_mode_t  Last classified mode
 */
pwr_mode_t Power_GetMode(void)
{
    return s_ctx.mode;
}

/**
 * @brief   Check whether IGN is currently considered ON
 * @brief   检查 IGN 当前是否处于 ON 状态
 *
 * @return  bool
 * @retval  true   IGN is ON (after debounce)
 * @retval  false  IGN is OFF or still debouncing
 */
bool Power_IsIgnOn(void)
{
    return s_ctx.ign_on;
}

/**
 * @brief   Check whether the cluster is ready to enter sleep
 * @brief   检查本机是否可以进入休眠
 *
 * @return  bool
 * @retval  true   SIG_SLEEP_READY = 1
 * @retval  false  Some blocker is still active
 */
bool Power_IsSleepReady(void)
{
    return Signal_Get(SIG_SLEEP_READY) != 0;
}

/* Module descriptor ----------------------------------------------------- */

/**
 * @brief   Module descriptor registered in scheduler.c
 * @brief   在 scheduler.c 中注册的模块描述符
 */
const mod_desc_t mod_power = {
    .name      = "power",
    .init      = prv_init,
    .on_ign_on = prv_on_ign_on,
    .tick      = prv_tick,
    .standby   = prv_standby,
};
