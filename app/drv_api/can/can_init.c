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
#include "YTM32B1MD1.h"

/* After FLEXCAN_DRV_Init returns, the SDK only waits for FRZACK=0.
 * It does NOT wait for MCR.NOTRDY=0, which means the CAN PE clock
 * domain can still be in reset and the controller cannot actually
 * transmit.  On real CAN bus (vs. loopback demo) this manifests as
 * "FLEXCAN_DRV_Send returns SUCCESS but no frame goes out", exactly
 * what we saw on the IPK bus.  Drain NOTRDY here for both instances. */
static void prv_wait_ready(uint8_t inst)
{
    CAN_Type *base = (inst == 1U) ? CAN1 : CAN2;
    uint32_t spin = 0u;
    /* CAN_MCR_NOTRDY_MASK = 0x8000000U (bit 27). Probing shows
     * MCR=0x2022_101F -> NOTRDY=0 already after Init exits, so
     * this loop never iterates in normal bring-up.  It is kept
     * as a safety net against a slow PE clock. */
    while ((base->MCR & 0x8000000U) != 0U) {
        if (++spin > 1000000U) {
            /* Defensive: never spin forever, even if clock is dead. */
            break;
        }
    }
}

/**
 * @brief   Initialize both FlexCAN instances
 */
void Can_Init(void)
{
    /* FlexCAN instance 1: private bus (body / chassis domain). */
    FLEXCAN_DRV_Init(1, &private_can_State, &private_can);
    prv_wait_ready(1U);
    /* FlexCAN instance 2: public bus (powertrain / infotainment). */
    FLEXCAN_DRV_Init(2, &public_can_State, &public_can);
    prv_wait_ready(2U);
}
