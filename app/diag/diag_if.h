/**
 * @file    diag_if.h
 * @brief   UDS / K-line diagnostic interface (skeleton)
 *
 * Thin wrapper to keep transport (CAN TP / KWP) out of business code.
 * Full implementation will integrate Dcm / Dem when those are wired in.
 */
#ifndef LBX_DIAG_IF_H
#define LBX_DIAG_IF_H

#include "types.h"
#include "result.h"

/**
 * @brief   UDS diagnostic session types
 * @brief   UDS 诊断会话类型
 */
typedef enum {
    DIAG_SESSION_DEFAULT = 0x01,
    DIAG_SESSION_PROGRAM  = 0x02,
    DIAG_SESSION_EXTENDED = 0x03,
} diag_session_t;

/**
 * @brief   Initialize the diagnostic interface
 * @brief   初始化诊断接口
 *
 * @return  lbx_result_t
 * @retval  LBX_OK            Init succeeded
 * @retval  LBX_ERR_NOT_SUPPORTED  Skeleton only
 */
lbx_result_t Diag_Init(void);

/**
 * @brief   Read a Data Identifier (DID)
 * @brief   读取一个数据标识符（DID）
 *
 * @param[in]      did       16-bit Data Identifier
 * @param[out]     out_buf   Caller-provided buffer
 * @param[in,out]  inout_len [in] capacity, [out] actual bytes written
 *
 * @return  lbx_result_t
 * @retval  LBX_ERR_NOT_SUPPORTED  Skeleton only
 */
lbx_result_t Diag_ReadDID(u16 did, u8 *out_buf, u8 *inout_len);

/**
 * @brief   Write a Data Identifier (DID)
 * @brief   写入一个数据标识符（DID）
 *
 * @param[in]  did   16-bit Data Identifier
 * @param[in]  buf   Payload
 * @param[in]  len   Payload length
 *
 * @return  lbx_result_t
 * @retval  LBX_ERR_NOT_SUPPORTED  Skeleton only
 */
lbx_result_t Diag_WriteDID(u16 did, const u8 *buf, u8 len);

/**
 * @brief   Execute a routine control (RoutineControl 0x31)
 * @brief   执行一个例程控制（0x31 RoutineControl）
 *
 * @param[in]  rid         16-bit Routine Identifier
 * @param[in]  sub         Sub-function (start/stop/results)
 * @param[in]  param       Optional parameters
 * @param[in]  param_len   Parameters length
 *
 * @return  lbx_result_t
 * @retval  LBX_ERR_NOT_SUPPORTED  Skeleton only
 */
lbx_result_t Diag_RoutineCtrl(u16 rid, u8 sub, const u8 *param, u8 param_len);

/**
 * @brief   Switch the active diagnostic session
 * @brief   切换当前诊断会话
 *
 * @param[in]  s  Target session
 *
 * @return  lbx_result_t
 * @retval  LBX_OK  Session updated
 */
lbx_result_t Diag_SetSession(diag_session_t s);

/**
 * @brief   Get the currently active diagnostic session
 * @brief   获取当前激活的诊断会话
 *
 * @return  diag_session_t  Current session
 */
diag_session_t Diag_GetSession(void);

#endif /* LBX_DIAG_IF_H */
