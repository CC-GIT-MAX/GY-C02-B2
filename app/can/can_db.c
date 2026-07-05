/**
 * @file    can_db.c
 * @brief   CAN message database
 *
 * 10 placeholder frames covering typical cluster inputs.
 * AUTOGEN marker retained for a future .dbc -> .c tool.
 *
 * Each `cb_xxx` decodes a known CAN frame's payload and writes one
 * or more signals on the bus. Each `pack_xxx` is the inverse: it
 * reads signals and builds a frame's payload.
 */
#include "can_db.h"
#include "signal.h"
#include "power.h"

#define LOG_NAME  "CDB "
#include "log.h"

/* ----- RX callbacks (static; called from can_rx.c tick context) ----- */

/**
 * @brief   Decode IGN status bit and publish to the signal bus.
 * @brief   解码 IGN 状态位并发布到信号总线
 *
 * @details Layout: byte0 bit0 = IGN state (1=on, 0=off).
 *
 * @param[in]  m  Received CAN frame
 */
static void cb_ign_status(const can_msg_t *m)
{
    /* byte0 bit0 = IGN state */
    bool ign = (m->data[0] & 0x01u) != 0u;
    Signal_Set(SIG_IGN_ON, ign ? 1 : 0);
}

/**
 * @brief   Decode vehicle speed (0.1 kph, little-endian) and keep-alive.
 * @brief   解码车速（0.1 kph，小端）并通知电源保持唤醒
 *
 * @details Layout: byte0 = LSB, byte1 = MSB, unit = 0.1 kph.
 *          Touching the power module on every frame extends the
 *          stay-awake window past the IGN-off debounce.
 *
 * @param[in]  m  Received CAN frame
 */
static void cb_veh_speed(const can_msg_t *m)
{
    /* byte0..1 little-endian, 0.1 kph */
    u16 v = (u16)((u16)m->data[1] << 8 | m->data[0]);
    Signal_Set(SIG_VEH_SPEED_KPH_X10, (int32_t)v);
    Power_OnCanBusActivity();
}

/**
 * @brief   Decode engine RPM (little-endian) and keep-alive.
 * @brief   解码发动机转速（小端）并通知电源保持唤醒
 *
 * @details Layout: byte0 = LSB, byte1 = MSB, unit = rpm.
 *
 * @param[in]  m  Received CAN frame
 */
static void cb_eng_rpm(const can_msg_t *m)
{
    /* byte0..1 little-endian, rpm */
    u16 r = (u16)((u16)m->data[1] << 8 | m->data[0]);
    Signal_Set(SIG_ENG_RPM, (int32_t)r);
    Power_OnCanBusActivity();
}

/**
 * @brief   Decode fuel level percentage and fuel-low telltale.
 * @brief   解码油量百分比并联动 fuel-low 指示灯
 *
 * @details Layout: byte0 = 0..100 (%).
 *          Below 10% the fuel-low telltale is asserted.
 *
 * @param[in]  m  Received CAN frame
 */
static void cb_fuel_level(const can_msg_t *m)
{
    /* byte0: 0..100 percent */
    u8 p = m->data[0];
    /* Clamp to valid range; some ECUs send 0xFF for "invalid". */
    if (p > 100u) p = 100u;
    Signal_Set(SIG_FUEL_LEVEL_PCT, (int32_t)p);
    Signal_Set(SIG_TT_FUEL_LOW, (p < 10u) ? 1 : 0);
}

/**
 * @brief   Decode coolant temperature (offset -40 degC).
 * @brief   解码冷却液温度（偏移量 -40 ℃）
 *
 * @details Layout: byte0 raw value, temperature = raw - 40 (degC).
 *          The -40 offset is the standard J1939 convention.
 *
 * @param[in]  m  Received CAN frame
 */
static void cb_coolant_temp(const can_msg_t *m)
{
    /* byte0: offset -40, degC */
    u8 raw = m->data[0];
    int32_t t = (int32_t)raw - 40;
    Signal_Set(SIG_COOLANT_TEMP_C, t);
}

/**
 * @brief   Decode EPS / brake / handbrake status bits.
 * @brief   解码 EPS / 制动 / 手刹状态位
 *
 * @details Layout: byte0 bit0 = handbrake, bit1 = brake pedal.
 *          Only the handbrake is wired to a telltale today;
 *          brake pedal info is reserved for a future signal.
 *
 * @param[in]  m  Received CAN frame
 */
static void cb_eps_status(const can_msg_t *m)
{
    /* byte0 bit0: handbrake, byte0 bit1: brake pedal */
    bool hb  = (m->data[0] & 0x01u) != 0u;
    bool brk = (m->data[0] & 0x02u) != 0u;
    Signal_Set(SIG_TT_BRAKE_FAULT, hb ? 1 : 0);
    /* SIG_BRAKE_PEDAL not in table yet; can be added later. */
    (void)brk;
}

/**
 * @brief   Decode door / left-right turn status (placeholders).
 * @brief   解码车门 / 左右转向状态（占位）
 *
 * @details Layout: byte0 bit0..3 = FL/FR/RL/RR doors.
 *          Currently FL/FR bits are reused for turn signal placeholders;
 *          real door mapping is pending customer-supplied spec.
 *
 * @param[in]  m  Received CAN frame
 */
static void cb_door_status(const can_msg_t *m)
{
    /* byte0 bit0..3 = FL/FR/RL/RR doors */
    Signal_Set(SIG_TT_LEFT_TURN,  (m->data[0] & 0x01u) ? 1 : 0);  /* reuse as FL placeholder */
    Signal_Set(SIG_TT_RIGHT_TURN, (m->data[0] & 0x02u) ? 1 : 0);
    (void)m;
}

/**
 * @brief   Decode light status (low/high/fog/position beams).
 * @brief   解码车灯状态（近/远/雾/位灯）
 *
 * @details Layout: byte0 bit0=low, bit1=high, bit2=front_fog,
 *                 bit3=rear_fog, bit4=position lamp.
 *
 * @param[in]  m  Received CAN frame
 */
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

/**
 * @brief   Pack a 1-Hz heartbeat frame with rolling counter.
 * @brief   打包 1Hz 心跳帧（带滚动计数）
 *
 * @details Layout: byte0 = 0xA5 magic, byte1 = counter, byte2 = 0x01 (alive).
 *          Counter is process-local so it wraps every 256 frames.
 *
 * @param[out]  data  8-byte payload buffer
 */
static void pack_heartbeat(u8 *data)
{
    static u8 cnt = 0;
    data[0] = 0xA5;          /* magic: receiver sanity check */
    data[1] = cnt++;         /* rolling counter for liveness */
    data[2] = 0x01;          /* app alive flag */
    data[3] = 0;
    data[4] = 0;
    data[5] = 0;
    data[6] = 0;
    data[7] = 0;
}

/**
 * @brief   Pack voltage / power-mode status frame from signal bus.
 * @brief   打包电压 / 电源模式状态帧（从信号总线读）
 *
 * @details Layout: byte0..1 = KL30 voltage in 0.1V (LE u16),
 *                   byte2 = pwr_mode_t.
 *
 * @param[out]  data  8-byte payload buffer
 */
static void pack_voltage_status(u8 *data)
{
    /* SIG_KL30_VOLTAGE_MV in 0.1V units, low byte first. */
    int32_t mv = Signal_Get(SIG_KL30_VOLTAGE_MV);
    if (mv < 0) mv = 0;
    u16 deci_volt = (u16)(mv / 100u);
    data[0] = (u8)(deci_volt & 0xFFu);   /* LSB */
    data[1] = (u8)(deci_volt >> 8);      /* MSB */
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

/** @brief  Internal helper that logs the RX/TX table sizes. */
static void prv_log_table(void)
{
    LOG_I("can_db: rx=%u tx=%u", (unsigned)g_can_rx_count, (unsigned)g_can_tx_count);
}

/**
 * @brief   Log a one-shot summary of the RX/TX tables
 * @brief   一次性打印 RX/TX 表的概要信息
 *
 * @details Invoked from CanIf_Init() after both controllers are up.
 */
void CanDb_LogOnInit(void) { prv_log_table(); }
