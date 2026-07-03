/**
 * @file    mod_template.c
 * @brief   Business module skeleton (copy this file to start a new module)
 *
 * Implements the four mod_desc_t hooks. Demonstrates:
 *   - static state only
 *   - sub-period control via RTI_IsElapsed()
 *   - Signal_* for inter-module communication
 *   - Log_* for leveled diagnostics
 *   - lbx_result_t for error reporting
 */
#include "mod_template.h"
#include "rti.h"
#include "signal.h"

#define LOG_NAME  "TPL"
#include "log.h"

/* Private state -------------------------------------------------------- */
static struct {
    uint8_t  init_done;
    uint32_t diag_value;
    uint32_t tick_count;
} s_ctx;

/* Private helpers ------------------------------------------------------ */
static lbx_result_t prv_do_10ms_job(void)
{
    s_ctx.tick_count++;
    LOG_D("10ms tick #%u", (unsigned)s_ctx.tick_count);
    return LBX_OK;
}

static lbx_result_t prv_do_100ms_job(void)
{
    /* Example: read a signal, compute, publish back */
    int32_t ign = Signal_Get(SIG_IGN_ON);
    LOG_D("ign=%d, diag=%u", (int)ign, (unsigned)s_ctx.diag_value);
    return LBX_OK;
}

/* mod_desc_t hooks ----------------------------------------------------- */
static void prv_init(uint8_t cold_boot)
{
    (void)cold_boot;
    s_ctx.init_done    = 1;
    s_ctx.diag_value   = 0;
    s_ctx.tick_count   = 0;
    LOG_I("init (cold_boot=%u)", (unsigned)cold_boot);
}

static void prv_on_ign_on(void)
{
    LOG_I("on_ign_on");
}

static void prv_tick(void)
{
    if (!s_ctx.init_done) {
        return;
    }
    if (RTI_IsElapsed(RTI_10MS))  (void)prv_do_10ms_job();
    if (RTI_IsElapsed(RTI_100MS)) (void)prv_do_100ms_job();
}

static void prv_standby(void)
{
    LOG_I("standby");
}

/* Module descriptor ---------------------------------------------------- */
const mod_desc_t mod_template = {
    .name      = "template",
    .init      = prv_init,
    .on_ign_on = prv_on_ign_on,
    .tick      = prv_tick,
    .standby   = prv_standby,
};

/* Public API ----------------------------------------------------------- */
lbx_result_t Template_SetDiagValue(uint32_t v)
{
    if (!s_ctx.init_done) {
        return LBX_ERR_NOT_READY;
    }
    s_ctx.diag_value = v;
    return LBX_OK;
}

uint32_t Template_GetDiagValue(void)
{
    return s_ctx.diag_value;
}
