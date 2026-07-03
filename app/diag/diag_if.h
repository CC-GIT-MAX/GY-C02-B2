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

typedef enum {
    DIAG_SESSION_DEFAULT = 0x01,
    DIAG_SESSION_PROGRAM  = 0x02,
    DIAG_SESSION_EXTENDED = 0x03,
} diag_session_t;

lbx_result_t Diag_Init(void);
lbx_result_t Diag_ReadDID(u16 did, u8 *out_buf, u8 *inout_len);
lbx_result_t Diag_WriteDID(u16 did, const u8 *buf, u8 len);
lbx_result_t Diag_RoutineCtrl(u16 rid, u8 sub, const u8 *param, u8 param_len);
lbx_result_t Diag_SetSession(diag_session_t s);
diag_session_t Diag_GetSession(void);

#endif /* LBX_DIAG_IF_H */
