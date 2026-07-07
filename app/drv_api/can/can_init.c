/**
 * @file    can_init.c
 * @brief   Initialize FlexCAN instances 1 (private) and 2 (public)
 *
 * @details Two CAN channels are wired on this board:
 *   - instance 1 = private bus (body / chassis domain, off-board sensors)
 *   - instance 2 = public bus (powertrain / infotainment, on the OBD link)
 *
 * Both stay in FLEXCAN_NORMAL_MODE (no loopback) so the cluster can
 * really talk to the rest of the car. Drivers register their user
 * callbacks via CanIf_InstallEventCallback (called from CanIf_Init in
 * main.c, AFTER this function returns).
 */
#include "sdk_project_config.h"

/**
 * @brief   Initialize both FlexCAN instances
 */
void Can_Init(void)
{
    /* FlexCAN instance 1: private bus (body / chassis domain). */
    FLEXCAN_DRV_Init(1, &private_can_State, &private_can);
    /* FlexCAN instance 2: public bus (powertrain / infotainment). */
    FLEXCAN_DRV_Init(2, &public_can_State, &public_can);
}
