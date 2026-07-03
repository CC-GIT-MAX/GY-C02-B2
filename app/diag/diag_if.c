/**
 * @file    diag_if.c
 * @brief   Diagnostic interface skeleton
 *
 * Stubs returning LBX_ERR_NOT_SUPPORTED. Real implementation will
 * route to Dcm/Dem in a later batch.
 */
#include "diag_if.h"

#define LOG_NAME  "DGM"
#include "log.h"

static diag_session_t s_session = DIAG_SESSION_DEFAULT;

lbx_result_t Diag_Init(void)
{
    LOG_I("Diag_Init (skeleton)");
    s_session = DIAG_SESSION_DEFAULT;
    return LBX_OK;
}

lbx_result_t Diag_ReadDID(u16 did, u8 *out_buf, u8 *inout_len)
{
    (void)did; (void)out_buf; (void)inout_len;
    return LBX_ERR_NOT_SUPPORTED;
}

lbx_result_t Diag_WriteDID(u16 did, const u8 *buf, u8 len)
{
    (void)did; (void)buf; (void)len;
    return LBX_ERR_NOT_SUPPORTED;
}

lbx_result_t Diag_RoutineCtrl(u16 rid, u8 sub, const u8 *param, u8 param_len)
{
    (void)rid; (void)sub; (void)param; (void)param_len;
    return LBX_ERR_NOT_SUPPORTED;
}

lbx_result_t Diag_SetSession(diag_session_t s)
{
    s_session = s;
    return LBX_OK;
}

diag_session_t Diag_GetSession(void)
{
    return s_session;
}
