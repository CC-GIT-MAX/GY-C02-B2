/**
 * @file    can_db.c
 * @brief   CAN message database
 *
 * 10 placeholder frames covering typical cluster inputs.
 * AUTOGEN marker retained for a future .dbc -> .c tool.
 */
#include "can_db.h"
#include "signal.h"
#include "power.h"

#define LOG_NAME  "CDB "
#include "log.h"

/* ----- RX callbacks (static; called from can_rx.c tick context) ----- */

static void cb_ign_status(const can_msg_t *m)
{
    /* byte0 bit0 = IGN state */
    bool ign = (m->data[0] & 0x01u) != 0u;
    Signal_Set(SIG_IGN_ON, ign ? 1 : 0);
}

static void cb_veh_speed(const can_msg_t *m)
{
    /* byte0..1 little-endian, 0.1 kph */
    u16 v = (u16)((u16)m->data[1] << 8 | m->data[0]);
    Signal_Set(SIG_VEH_SPEED_KPH_X10, (int32_t)v);
    Power_OnCanBusActivity();
}

static void cb_eng_rpm(const can_msg_t *m)
{
    /* byte0..1 little-endian, rpm */
    u16 r = (u16)((u16)m->data[1] << 8 | m->data[0]);
    Signal_Set(SIG_ENG_RPM, (int32_t)r);
    Power_OnCanBusActivity();
}

static void cb_fuel_level(const can_msg_t *m)
{
    /* byte0: 0..100 percent */
    u8 p = m->data[0];
    if (p > 100u) p = 100u;
    Signal_Set(SIG_FUEL_LEVEL_PCT, (int32_t)p);
    Signal_Set(SIG_TT_FUEL_LOW, (p < 10u) ? 1 : 0);
}

static void cb_coolant_temp(const can_msg_t *m)
{
    /* byte0: offset -40, degC */
    u8 raw = m->data[0];
    int32_t t = (int32_t)raw - 40;
    Signal_Set(SIG_COOLANT_TEMP_C, t);
}

static void cb_eps_status(const can_msg_t *m)
{
    /* byte0 bit0: handbrake, byte0 bit1: brake pedal */
    bool hb  = (m->data[0] & 0x01u) != 0u;
    bool brk = (m->data[0] & 0x02u) != 0u;
    Signal_Set(SIG_TT_BRAKE_FAULT, hb ? 1 : 0);
    /* SIG_BRAKE_PEDAL not in table yet; can be added later */
    (void)brk;
}

static void cb_door_status(const can_msg_t *m)
{
    /* byte0 bit0..3 = FL/FR/RL/RR doors */
    Signal_Set(SIG_TT_LEFT_TURN,  (m->data[0] & 0x01u) ? 1 : 0);  /* reuse as FL placeholder */
    Signal_Set(SIG_TT_RIGHT_TURN, (m->data[0] & 0x02u) ? 1 : 0);
    (void)m;
}

static void cb_light_status(const can_msg_t *m)
{
    /* byte0: bit0=low, bit1=high, bit2=front_fog, bit3=rear_fog, bit4=pos */
    Signal_Set(SIG_TT_LOW_BEAM,      (m->data[0] & 0x01u) ? 1 : 0);
    Signal_Set(SIG_TT_HIGH_BEAM,     (m->data[0] & 0x02u) ? 1 : 0);
    Signal_Set(SIG_TT_FRONT_FOG,     (m->data[0] & 0x04u) ? 1 : 0);
    Signal_Set(SIG_TT_REAR_FOG,      (m->data[0] & 0x08u) ? 1 : 0);
    Signal_Set(SIG_TT_POSITION_LAMP, (m->data[0] & 0x10u) ? 1 : 0);
}

/* ----- TX packers (static; fill 8-byte payload) ----- */

static void pack_heartbeat(u8 *data)
{
    static u8 cnt = 0;
    data[0] = 0xA5;          /* magic */
    data[1] = cnt++;
    data[2] = 0x01;          /* app alive */
    data[3] = 0;
    data[4] = 0;
    data[5] = 0;
    data[6] = 0;
    data[7] = 0;
}

static void pack_voltage_status(u8 *data)
{
    /* SIG_KL30_VOLTAGE_MV in 0.1V units, low byte first */
    int32_t mv = Signal_Get(SIG_KL30_VOLTAGE_MV);
    if (mv < 0) mv = 0;
    u16 deci_volt = (u16)(mv / 100u);
    data[0] = (u8)(deci_volt & 0xFFu);
    data[1] = (u8)(deci_volt >> 8);
    data[2] = (u8)Signal_Get(SIG_PWR_MODE);
    data[3] = 0; data[4] = 0; data[5] = 0; data[6] = 0; data[7] = 0;
}

/* ----- Tables ----- */

const can_rx_desc_t g_can_rx_db[] = {
    /* AUTOGEN: CAN_RX_DB_START */
    { .can_id = 0x18FF0001, .ide = 1, .ch = CAN_CH_PUBLIC,
      .cycle_ms = 100, .timeout_ms = 300, .cb = cb_ign_status },
    { .can_id = 0x18FEF100, .ide = 1, .ch = CAN_CH_PUBLIC,
      .cycle_ms = 20,  .timeout_ms = 100, .cb = cb_veh_speed },
    { .can_id = 0x18FEF101, .ide = 1, .ch = CAN_CH_PUBLIC,
      .cycle_ms = 10,  .timeout_ms = 50,  .cb = cb_eng_rpm },
    { .can_id = 0x18FEF102, .ide = 1, .ch = CAN_CH_PUBLIC,
      .cycle_ms = 200, .timeout_ms = 500, .cb = cb_fuel_level },
    { .can_id = 0x18FEF103, .ide = 1, .ch = CAN_CH_PUBLIC,
      .cycle_ms = 200, .timeout_ms = 500, .cb = cb_coolant_temp },
    { .can_id = 0x18FF0020, .ide = 1, .ch = CAN_CH_PUBLIC,
      .cycle_ms = 100, .timeout_ms = 300, .cb = cb_eps_status },
    { .can_id = 0x18FF0030, .ide = 1, .ch = CAN_CH_PUBLIC,
      .cycle_ms = 200, .timeout_ms = 600, .cb = cb_door_status },
    { .can_id = 0x18FF0040, .ide = 1, .ch = CAN_CH_PUBLIC,
      .cycle_ms = 100, .timeout_ms = 300, .cb = cb_light_status },
    /* AUTOGEN: CAN_RX_DB_END */
};
const u16 g_can_rx_count = (u16)(sizeof(g_can_rx_db) / sizeof(g_can_rx_db[0]));

const can_tx_desc_t g_can_tx_db[] = {
    /* AUTOGEN: CAN_TX_DB_START */
    { .can_id = 0x18FF50E6, .ide = 1, .ch = CAN_CH_PRIVATE,
      .cycle_ms = 100, .pack = pack_heartbeat,     .dlc = 8 },
    { .can_id = 0x18FF50E7, .ide = 1, .ch = CAN_CH_PRIVATE,
      .cycle_ms = 200, .pack = pack_voltage_status, .dlc = 8 },
    /* AUTOGEN: CAN_TX_DB_END */
};
const u16 g_can_tx_count = (u16)(sizeof(g_can_tx_db) / sizeof(g_can_tx_db[0]));

/* One-time table sanity log on first use */
static void prv_log_table(void)
{
    LOG_I("can_db: rx=%u tx=%u", (unsigned)g_can_rx_count, (unsigned)g_can_tx_count);
}

/* Exposed for can_rx to call after registration */
void CanDb_LogOnInit(void) { prv_log_table(); }
