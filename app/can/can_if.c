/**
 * @file    can_if.c
 * @brief   CAN interface implementation (driver glue layer)
 *
 * Wraps flexcan_driver. Real implementation will be filled in
 * when the CAN module is wired up. For now this provides the
 * skeleton: function bodies are stubs that return LBX_OK with
 * TODO markers.
 *
 * DO NOT call flexcan APIs from business code; only this file
 * may include "flexcan_driver.h".
 */
#include "can_if.h"

#define LOG_NAME  "CAN"
#include "log.h"

lbx_result_t CanIf_Init(void)
{
    LOG_I("CanIf_Init (skeleton)");
    /* TODO(后续批次): flexcan init for private + public channels */
    return LBX_OK;
}

lbx_result_t CanIf_RegisterRx(can_channel_t ch, u32 can_id, u8 ide, can_rx_cb_t cb)
{
    (void)ch; (void)can_id; (void)ide; (void)cb;
    /* TODO(后续批次): 写入 can_db RX 表 */
    return LBX_OK;
}

lbx_result_t CanIf_Send(can_channel_t ch, const can_msg_t *msg)
{
    (void)ch; (void)msg;
    /* TODO(后续批次): 调 flexcan send */
    return LBX_OK;
}

lbx_result_t CanIf_SetTxEnabled(can_channel_t ch, bool en)
{
    (void)ch; (void)en;
    /* TODO */
    return LBX_OK;
}

lbx_result_t CanIf_GoToSleep(can_channel_t ch)
{
    (void)ch;
    /* TODO: enter low-power, disable transceiver */
    return LBX_OK;
}

lbx_result_t CanIf_WakeUp(can_channel_t ch)
{
    (void)ch;
    /* TODO: wake from CAN RX */
    return LBX_OK;
}

bool CanIf_IsBusOff(can_channel_t ch)
{
    (void)ch;
    return false;
}

u32 CanIf_GetBusOffCount(can_channel_t ch)
{
    (void)ch;
    return 0;
}
