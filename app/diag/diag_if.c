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

/* Current UDS session. Defaults to DEFAULT (no special privileges). */
static diag_session_t s_session = DIAG_SESSION_DEFAULT;

/**
 * @brief   Initialize the diagnostic interface
 * @brief   初始化诊断接口
 *
 * @details Resets the session to DEFAULT. Real implementation will
 *          also initialize the transport layer (CAN TP / KWP) here.
 *
 * @return  lbx_result_t  Always LBX_OK
 */
lbx_result_t Diag_Init(void)
{
    LOG_I("Diag_Init (skeleton)");
    s_session = DIAG_SESSION_DEFAULT;
    return LBX_OK;
}

/**
 * @brief   Read a Data Identifier (DID) - skeleton returns NOT_SUPPORTED
 * @brief   读取一个数据标识符（DID）- 骨架实现返回 NOT_SUPPORTED
 *
 * @param[in]      did       16-bit Data Identifier (unused)
 * @param[out]     out_buf   Caller-provided buffer (unused)
 * @param[in,out]  inout_len [in] capacity, [out] actual bytes (unused)
 *
 * @return  lbx_result_t  Always LBX_ERR_NOT_SUPPORTED
 */
lbx_result_t Diag_ReadDID(u16 did, u8 *out_buf, u8 *inout_len)
{
    (void)did; (void)out_buf; (void)inout_len;
    return LBX_ERR_NOT_SUPPORTED;
}

/**
 * @brief   Write a Data Identifier (DID) - skeleton returns NOT_SUPPORTED
 * @brief   写入一个数据标识符（DID）- 骨架实现返回 NOT_SUPPORTED
 *
 * @param[in]  did   16-bit Data Identifier (unused)
 * @param[in]  buf   Payload (unused)
 * @param[in]  len   Payload length (unused)
 *
 * @return  lbx_result_t  Always LBX_ERR_NOT_SUPPORTED
 */
lbx_result_t Diag_WriteDID(u16 did, const u8 *buf, u8 len)
{
    (void)did; (void)buf; (void)len;
    return LBX_ERR_NOT_SUPPORTED;
}

/**
 * @brief   Execute a routine control - skeleton returns NOT_SUPPORTED
 * @brief   执行一个例程控制 - 骨架实现返回 NOT_SUPPORTED
 *
 * @param[in]  rid         16-bit Routine Identifier (unused)
 * @param[in]  sub         Sub-function (unused)
 * @param[in]  param       Optional parameters (unused)
 * @param[in]  param_len   Parameters length (unused)
 *
 * @return  lbx_result_t  Always LBX_ERR_NOT_SUPPORTED
 */
lbx_result_t Diag_RoutineCtrl(u16 rid, u8 sub, const u8 *param, u8 param_len)
{
    (void)rid; (void)sub; (void)param; (void)param_len;
    return LBX_ERR_NOT_SUPPORTED;
}

/**
 * @brief   Switch the active diagnostic session
 * @brief   切换当前诊断会话
 *
 * @details Real implementation must also enforce security access
 *          (e.g. seed/key) before allowing PROGRAM/EXTENDED.
 *
 * @param[in]  s  Target session
 *
 * @return  lbx_result_t  Always LBX_OK
 */
lbx_result_t Diag_SetSession(diag_session_t s)
{
    s_session = s;
    return LBX_OK;
}

/**
 * @brief   Get the currently active diagnostic session
 * @brief   获取当前激活的诊断会话
 *
 * @return  diag_session_t  Current session
 */
diag_session_t Diag_GetSession(void)
{
    return s_session;
}
