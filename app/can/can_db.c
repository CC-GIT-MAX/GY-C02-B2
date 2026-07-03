/**
 * @file    can_db.c
 * @brief   CAN message database (skeleton)
 *
 * Empty tables by design. Business code adds entries as needed.
 * cbs are NULL until CanIf_RegisterRx() is called.
 *
 * AUTOGEN marker for future Python tool:
 *   // AUTOGEN: CAN_RX_DB_START / CAN_RX_DB_END
 *   // AUTOGEN: CAN_TX_DB_START / CAN_TX_DB_END
 */
#include "can_db.h"

const can_rx_desc_t g_can_rx_db[] = {
    /* Example:
     * { .can_id = 0x18FEF100, .ide = 1, .ch = CAN_CH_PUBLIC,
     *   .cycle_ms = 100, .timeout_ms = 250, .cb = NULL },
     */
};
const u16 g_can_rx_count = (u16)(sizeof(g_can_rx_db) / sizeof(g_can_rx_db[0]));

const can_tx_desc_t g_can_tx_db[] = {
    /* Example:
     * { .can_id = 0x18FF50E6, .ide = 1, .ch = CAN_CH_PRIVATE,
     *   .cycle_ms = 100, .pack = Pack_Heartbeat, .dlc = 8 },
     */
};
const u16 g_can_tx_count = (u16)(sizeof(g_can_tx_db) / sizeof(g_can_tx_db[0]));
