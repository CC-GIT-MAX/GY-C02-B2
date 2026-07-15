/**
 * @file    mod_template.c
 * @brief   Business module skeleton (copy this file to start a new module)
 *
 * Implements the four mod_desc_t hooks. Demonstrates:
 *   - static state only
 *   - sub-period control via RTI slot API (RTI_OpenSlot / RTI_SlotElapsed)
 *   - Signal_* for inter-module communication
 *   - Log_* for leveled diagnostics
 *   - c02b2_result_t for error reporting
 */
#include "types.h"
#include "mod_template.h"
#include "rti.h"
#include "signal.h"

#define MOD_NAME  "TPL"
#include "log.h"

/* Caller-private RTI slots (replaces shared RTI_IsElapsed via RTI slot API). */
static rti_slot_t s_slot_10ms;
static rti_slot_t s_slot_100ms;

/* Private state -------------------------------------------------------- */
static struct {
    uint8_t  init_done;
    uint32_t diag_value;
    uint32_t tick_count;
} s_ctx;

/* Private helpers ------------------------------------------------------ */

/**
 * @brief   10 ms sub-task: increment tick counter, log @ DEBUG.
 * @brief   10ms 子任务：递增 tick 计数，按 DEBUG 等级记录
 *
 * @details Pure C DEBUG noise to demonstrate the 10 ms sub-period
 *          pattern. Replace with real work in actual modules.
 *
 * @return  c02b2_result_t  Always C02B2_OK
 */
static c02b2_result_t prv_do_10ms_job(void)
{
    s_ctx.tick_count++;
    /* LOG_D is compiled out when LOG_LEVEL < LOG_LVL_DEBUG. */
    LOG_D("10ms tick #%u", (unsigned)s_ctx.tick_count);
    return C02B2_OK;
}

/**
 * @brief   100 ms sub-task: read IGN signal, log a snapshot.
 * @brief   100ms 子任务：读取 IGN 信号，记录一次快照
 *
 * @details Demonstrates the Signal_Get() pattern and using multiple
 *          log args. Replace with real work in actual modules.
 *
 * @return  c02b2_result_t  Always C02B2_OK
 */
static c02b2_result_t prv_do_100ms_job(void)
{
    /* v0.5 example: 二件套用法.
     *   - 业务路径           -> Signal_Get
     *     (INIT_DBC 时, 超时后 Signal_Get() 返 DBC init_value;
     *      KEEP_LAST 时, Signal_Get() 保留 "timeout 前最后一帧" raw)
     *   - 门控逻辑           -> Signal_IsValid (boot_done && !timeout_bit)
     *     + Signal_HasEverReceived (查 SIG_CAN_RX_EVER_RECEIVED_*)
     * 实际开发中按需要组合使用即可。*/

    const u32 rpm     = Signal_Get(SIG_CAN_EMS_EngineSpeedRPM);
    const bool rpm_ok = Signal_IsValid(SIG_CAN_EMS_EngineSpeedRPM)
                       && Signal_HasEverReceived(SIG_CAN_EMS_EngineSpeedRPM);
    LOG_D("rpm=%u valid=%u diag=%u",
          (unsigned)rpm, (unsigned)rpm_ok, (unsigned)s_ctx.diag_value);
    return C02B2_OK;
}

/* mod_desc_t hooks ----------------------------------------------------- */

/**
 * @brief   mod_desc_t mcu_init hook: zero state, log cold/warm marker.
 * @brief   mod_desc_t init 钩子：清零状态，记录冷/热启动
 *
 * @param[in]  cold_boot  1 = cold boot, 0 = warm boot
 */
static void prv_mcu_init(uint8_t cold_boot)
{
    (void)cold_boot;
    s_slot_10ms  = RTI_OpenSlot(RTI_10MS);
    s_slot_100ms = RTI_OpenSlot(RTI_100MS);
    s_ctx.init_done    = 1;
    s_ctx.diag_value   = 0;
    s_ctx.tick_count   = 0;
    LOG_I("init (cold_boot=%u)", (unsigned)cold_boot);
}
/**
 * @brief   mod_desc_t wakeup_init hook: post-MCU-init restore.
 * @brief   mod_desc_t wakeup_init 钩子: MCU 初始化后的唤醒恢复
 *
 * @details Runs after mcu_init() and before on_ign_on(). Use this
 *          hook to re-arm NVIC priorities, restore wake-source
 *          state, or prime caches that mcu_init left in a known
 *          reset configuration. Currently a stub for all modules
 *          - extend when a module needs real wake-from-reset work.
 */
static void prv_wakeup_init(void)
{
    LOG_I("wakeup_init");
}

/**
 * @brief   mod_desc_t on_ign_on hook.
 * @brief   mod_desc_t on_ign_on 钩子
 */
static void prv_on_ign_on(void)
{
    LOG_I("on_ign_on");
}

/**
 * @brief   mod_desc_t tick hook: 10 ms + 100 ms sub-tasks
 * @brief   mod_desc_t tick 钩子：执行 10ms 和 100ms 子任务
 *
 * @details Pattern: cheap-then-expensive sub-periods. The 10 ms
 *          fast loop runs first so any time-critical work (e.g.
 *          PWM updates) is done before the 100 ms slower loop.
 */
static void prv_tick(void)
{
    if (!s_ctx.init_done) {
        return;
    }
    /* Each sub-period has its own static slot in RTI; calling
     * in this order is intentional (cheap work first). */
    if (RTI_SlotElapsed(&s_slot_10ms))  { (void)prv_do_10ms_job(); }
    if (RTI_SlotElapsed(&s_slot_100ms)) { (void)prv_do_100ms_job(); }
}

/**
 * @brief   mod_desc_t standby hook.
 * @brief   mod_desc_t standby 钩子
 */
static void prv_standby(void)
{
    LOG_I("standby");
}

/* Module descriptor ---------------------------------------------------- */

/**
 * @brief   Module descriptor registered in scheduler.c
 * @brief   在 scheduler.c 中注册的模块描述符
 */
const mod_desc_t mod_template = {
    .name      = "template",
    .mcu_init   = prv_mcu_init,
    .wakeup_init = prv_wakeup_init,
    .on_ign_on = prv_on_ign_on,
    .tick      = prv_tick,
    .standby   = prv_standby,
};

/* Public API ----------------------------------------------------------- */

/**
 * @brief   Set a diag value (for unit-test / manual injection)
 * @brief   设置一个诊断值（用于单元测试 / 手动注入）
 *
 * @details Rejects calls before init() by returning C02B2_ERR_NOT_READY;
 *          the unit test (`tests/test_mod_template.c`) covers this path.
 *
 * @param[in]  v  Value to store
 *
 * @return  c02b2_result_t
 * @retval  C02B2_OK            Stored
 * @retval  C02B2_ERR_NOT_READY Module not yet initialized
 */
c02b2_result_t Template_SetDiagValue(uint32_t v)
{
    if (!s_ctx.init_done) {
        /* Guard: refuse to store before init. */
        return C02B2_ERR_NOT_READY;
    }
    s_ctx.diag_value = v;
    return C02B2_OK;
}

/**
 * @brief   Get the previously-set diag value
 * @brief   获取最近一次设置的诊断值
 *
 * @return  uint32_t  Last value, or 0 if never set
 */
uint32_t Template_GetDiagValue(void)
{
    return s_ctx.diag_value;
}

SCHED_REGISTER(mod_template);
