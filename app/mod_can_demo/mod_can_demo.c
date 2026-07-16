/**
 * @file    mod_can_demo.c
 * @brief   Demo / bring-up module that exercises the full CAN stack
 * @brief   演练完整 CAN 栈的 demo / 联调模块
 *
 * @details 在 1 秒 tick 上演示六件事：
 *
 *   1. 信号总线读      -> Signal_Get(SIG_CAN_*) + LOG_I
 *   2. 超时位图解码    -> 遍历 s_bit_to_can_id[] 以及三个
 *                          SIG_CAN_RX_TIMEOUT_MAP_LO/HI/HI2 槽位
 *   3. 原始帧缓存      -> 在选定的 IPK id 上调用 CanIf_RxGetLastRawFrame
 *   4. 整帧 TX         -> CanIf_TxPreparePayload + CanIf_TxTrigger
 *   5. 单信号 TX      -> CanIf_TxEncodeSignal + CanIf_TxTrigger
 *   6. raw 与物理量互转 -> CanDb_PackSignal / CanDb_GetRaw /
 *                          CanDb_DecodeSignal / CanDb_EncodeSignalValue
 *                          作用于 EMS_EngineSpeedRPM (0x85) 和
 *                          ESC_VehicleSpeed (0x125)；每次 tick
 *                          同时打印 Signal_Get + 物理量
 *
 * 所有 demo 仅面向 IPK 测试批次 (CAN_DB_IPK_*)。 */
#include "mod_can_demo.h"

#include "types.h"
#include "rti.h"
#include "signal.h"
#include "can_db.h"            /* can_msg_descs_ipk[] / CanDb_*              */
#include "can_db_ipk_gen.h"    /* CAN_DB_IPK_*_COUNT, signal enum            */
#include "drv_api/can/can_if.h"/* can_msg_t                                  */

#define MOD_NAME  "CDEM"
#include "log.h"

/* 编译期开关：MOD_CAN_DEMO_EN
 *   0（默认）——模块描述符已注册但不分配 RTI slot，tick 不做任何事。
 *               六个 demo 函数仍会被编译（它们引用了 DBC 表，删掉会在
 *               DBC 变更时破坏构建），但永远不会被调用。
 *   1           ——完整 demo，1 秒 tick：信号总线读、超时位图解码、
 *                 原始帧缓存、TX 整帧、TX 单信号、raw<->physical 来回。
 *
 * 在编译命令行上覆盖：
 *   iarbuild ... --define MOD_CAN_DEMO_EN=1
 *   gcc -DMOD_CAN_DEMO_EN=1 ...
 */
#ifndef MOD_CAN_DEMO_EN
  #define MOD_CAN_DEMO_EN  0
#endif

#if MOD_CAN_DEMO_EN
/* 调用方私有的 RTI slot，1 秒 demo 节奏。*/
static rti_slot_t s_slot_demo_1s;

/* ---------------------------------------------------------------- *
 *  Demo configuration
 *
 *  目标 ID/信号 ID 固定到具体的 IPK 上，以保证日志可复现且与 DBC 对齐。
 *  如需修改，只改这里。
 * ---------------------------------------------------------------- */

/* Demo #3（原始帧缓存读）使用的 RX id。挑选联调台大概率会注入的
 * IPK RX id。0x0286 在 IPK DBC 中是 BCM_RDoorWindowState（RX, dlc=8）。
 */
#define DEMO_RX_ID_RAW           0x0286u

/* Demo #4（整帧 TX）使用的 TX id。IPK_STS_Tx = 0x026D (IPK_STS)。*/
#define DEMO_TX_ID_PAYLOAD       0x026Du

/* Demo #5（单信号 TX）使用的 TX id。IPK_EngineService = 0x03E9。*/
#define DEMO_TX_ID_SIGNAL        0x03E9u

/* Demo #5 在 IPK_EngineService 内的信号 id。
 * IPK_DayToEngSrv (U16, length 9, factor 1) -> 0..511。
 */
#define DEMO_TX_SIGNAL_ID        CAN_DB_SIG_IPK_DayToEngSrv


/* 私有状态 -------------------------------------------------------- */
static struct {
    bool     inited;
    u32      sweep_count;
} s_demo;

/* ---------------------------------------------------------------- *
 *  Demo #1: 信号总线读
 * ---------------------------------------------------------------- */

/**
 * @brief   Print one signal line using its db / bus id pair
 * @brief   用 db / bus id 打印一行信号值
 */
static void prv_emit_sig(u16 db_id, signal_id_t bus_id, const char *name)
{
    /* 信号总线上的 RAW u32（已经是字段的原始比特位型）。
     * 需要物理值的消费模块在其之上调用 CanDb_DecodeSignal()。*/
    const u32 raw = Signal_Get(bus_id);
    (void)db_id;
    #if CAN_DEMO_LOG
    LOG_I("  sig %s (bus=%u) raw=0x%08X", name, (unsigned)bus_id, (unsigned)raw);
    #endif
}

static void prv_demo_signals(void)
{
    #if CAN_DEMO_LOG
    LOG_I("[1/6] signals (live Signal_Get):");
    #endif
    /* 所有 demo 信号都取自已存在于 IPK DBC 中的 IPK RX 消息
     * （signal.h 中的 SIG_CAN_* 已经由 app/can/can_rx.c 填充好）。*/
    prv_emit_sig((u16)CAN_DB_SIG_EMS_EngineSpeedRPM,
                 SIG_CAN_EMS_EngineSpeedRPM,   "EMS_EngineSpeedRPM");
    prv_emit_sig((u16)CAN_DB_SIG_ESC_VehicleSpeed,
                 SIG_CAN_ESC_VehicleSpeed,     "ESC_VehicleSpeed");
    prv_emit_sig((u16)CAN_DB_SIG_BMSH_BattSOCDisp,
                 SIG_CAN_BMSH_BattSOCDisp,     "BMSH_BattSOCDisp");
}

/* 文件内辅助函数的前向声明（稍后定义）。*/
static u32 prv_bit_to_can_id(u32 bit);

/* ---------------------------------------------------------------- *
 *  Demo #2: 超时位图解码
 * ---------------------------------------------------------------- */
static void prv_demo_timeouts(void)
{
    const u32 lo  = Signal_Get(SIG_CAN_RX_TIMEOUT_MAP_LO);
    const u32 hi  = Signal_Get(SIG_CAN_RX_TIMEOUT_MAP_HI);
    const u32 hi2 = Signal_Get(SIG_CAN_RX_TIMEOUT_MAP_HI2);
    /* 统计三个 word 中置 1 的位数。简单 popcount。*/
    u32 to_cnt = 0u;
    for (u32 i = 0u; i < 32u; i++) {
        if ((lo  >> i) & 1) { to_cnt++; }
        if ((hi  >> i) & 1) { to_cnt++; }
        if ((hi2 >> i) & 1) { to_cnt++; }
    }
    #if CAN_DEMO_LOG
    LOG_I("[2/6] timeouts: LO=0x%08X HI=0x%08X HI2=0x%08X set=%u",
          (unsigned)lo, (unsigned)hi, (unsigned)hi2, (unsigned)to_cnt);
    #endif

    /* 把前几个置 1 的位反向枚举回 CAN id（Sentinel 映射；
     * DBC 顺序变动时 bit-N 保持稳定）。*/
    u32 printed = 0u;
    for (u32 bit = 0u; bit < (u32)CAN_BITMAP_MAX && printed < 4u; bit++) {
        const u32 word = (bit < 32u) ? lo : (bit < 64u) ? hi : hi2;
        const u32 shift = (bit < 32u) ? bit : (bit < 64u) ? (bit - 32u) : (bit - 64u);
        if (((word >> shift) & 1) == 0) { continue; }
        /* s_bit_to_can_id 由 tools/dbc_parse.py 生成到 can_db_ipk_gen.c。
         * 需要名字时用运行时辅助 CanDb_FindIpkById。*/
        const u32 can_id = prv_bit_to_can_id(bit);
        const char *name = (can_id != 0u)
            ? CanDb_FindIpkById(can_id)->name : "unused";
        #if CAN_DEMO_LOG
        LOG_I("  bit=%u -> id=0x%X (%s) TIMEOUT",
              (unsigned)bit, (unsigned)can_id, name);
        #endif
        printed++;
    }
}

/* ---------------------------------------------------------------- *
 *  Demo #3: 原始帧缓存（每个 IPK id 最近一帧的 8 字节负载）
 * ---------------------------------------------------------------- */
static void prv_demo_raw_frame(void)
{
    can_msg_t frame;
    const c02b2_result_t r = CanIf_RxGetLastRawFrame(DEMO_RX_ID_RAW, &frame);
    if (r == C02B2_ERR_NOT_FOUND) {
        #if CAN_DEMO_LOG
        LOG_I("[3/6] raw cache: id=0x%X never received yet",
              (unsigned)DEMO_RX_ID_RAW);
        #endif
        return;
    }
    if (r != C02B2_OK) {
        LOG_W("[3/6] raw cache lookup failed id=0x%X (%d)",
              (unsigned)DEMO_RX_ID_RAW, (int)r);
        return;
    }
    const char *name = CanDb_FindIpkById(frame.id)->name;
    #if CAN_DEMO_LOG
    LOG_I("[3/6] raw cache: id=0x%X (%s) dlc=%u bytes = %02X %02X %02X %02X %02X %02X %02X %02X",
          (unsigned)frame.id, name, (unsigned)frame.dlc,
          frame.data[0], frame.data[1], frame.data[2], frame.data[3],
          frame.data[4], frame.data[5], frame.data[6], frame.data[7]);
    #endif
}

/* ---------------------------------------------------------------- *
 *  Demo #4: 整帧 TX（8 字节原始数据）
 * ---------------------------------------------------------------- */
static void prv_demo_tx_payload(u32 sweep)
{
    /* 简单的循环字节模式让测试点在 CANalyzer 波形上一眼可辨。*/
    u8 buf[8];
    for (u32 i = 0u; i < 8u; i++) { buf[i] = (u8)((sweep + i) & 0xFFu); }
    const c02b2_result_t r = CanIf_TxPreparePayload(DEMO_TX_ID_PAYLOAD,
                                                 buf, 8u);
    if (r != C02B2_OK) {
        LOG_W("[4/6] PreparePayload id=0x%X failed (%d)",
              (unsigned)DEMO_TX_ID_PAYLOAD, (int)r);
        return;
    }
    const c02b2_result_t t = CanIf_TxTrigger(DEMO_TX_ID_PAYLOAD);
    if (t != C02B2_OK) {
        LOG_W("[4/6] Trigger id=0x%X failed (%d)",
              (unsigned)DEMO_TX_ID_PAYLOAD, (int)t);
        return;
    }
    #if CAN_DEMO_LOG
    LOG_I("[4/6] tx 0x%X (IPK_STS): 8-byte payload queued and triggered",
          (unsigned)DEMO_TX_ID_PAYLOAD);
    #endif
}

/* ---------------------------------------------------------------- *
 *  Demo #5: 单信号 TX（rebuild + trigger）
 * ---------------------------------------------------------------- */
static void prv_demo_tx_signal(u32 sweep)
{
    /* 模 512 保持数值在 length=9、factor=1 的信号量程内，
     * 防止 encode 步骤因超量程拒绝。*/
    /* 总线上传输的是 RAW；demo 发出的 raw 值会被
     * IPK_DayToEngSrv (length=9, factor=1, offset=0) 接受。
     * DBC 中 `+`（无符号）信号的 raw 范围是 0..511。*/
    const u32 v = (sweep * 7u) % 512u;
    const c02b2_result_t e = CanIf_TxEncodeSignal(DEMO_TX_ID_SIGNAL,
                                                DEMO_TX_SIGNAL_ID, v);
    if (e != C02B2_OK) {
        LOG_W("[5/6] EncodeSignal id=0x%X sig=%u v=%d failed (%d)",
              (unsigned)DEMO_TX_ID_SIGNAL,
              (unsigned)DEMO_TX_SIGNAL_ID, (int)v, (int)e);
        return;
    }
    const c02b2_result_t t = CanIf_TxTrigger(DEMO_TX_ID_SIGNAL);
    if (t != C02B2_OK) {
        LOG_W("[5/6] Trigger id=0x%X failed (%d)",
              (unsigned)DEMO_TX_ID_SIGNAL, (int)t);
        return;
    }
    #if CAN_DEMO_LOG
    LOG_I("[5/6] tx 0x%X (IPK_EngineService): IPK_DayToEngSrv=%d (queued)",
          (unsigned)DEMO_TX_ID_SIGNAL, (int)v);
    #endif
}

/* ---------------------------------------------------------------- *
 *  Demo #6: raw 与物理量在 RX 信号上的往返（SOC 联调）
 *
 *  在 1 秒 tick 上跑两个 Motorola 信号：
 *    - EMS_EngineSpeedRPM  (CAN ID 0x85,  start=23, len=16, factor=0.25)
 *    - ESC_VehicleSpeed    (CAN ID 0x125, start=15, len=13, factor=0.05625)
 *
 *  每个信号给出三轮独立往返，便于 CANalyzer sweep 与 SOC 真值核对编解码：
 *
 *  [A] raw   -> pack    -> GetRaw                  (必须等于 raw)
 *  [B] raw   -> decode  -> phys (s32 整数)         (raw * factor + offset，四舍五入)
 *  [C] phys  -> encode  -> pack -> decode -> phys  (必须等于 phys)
 *  [D] RX 端（实时）: Signal_Get + CanDb_DecodeSignal 作用于
 *      CANalyzer 最近发出的一帧。
 *
 *  对非 2 的幂因子（如 0.05625），[B] 的 phys 是量化后的 s32 ——
 *  一个 phys 步长覆盖 factor 个 raw 步。raw -> phys -> raw 往返可能
 *  偏移一个 raw 步，这是预期行为，与其它 CAN 工具链对该信号的处理一致。
 * ---------------------------------------------------------------- */

/* 在信号量程内扫 raw，使每个 tick 都能打印一个与 CANalyzer 对照的新值。
 * LCG 保证值的确定性。*/
static u32 prv_demo_raw_step(u32 sweep, u32 mask)
{
    u32 v = (sweep * 2654435761u) + 1u;
    return v & mask;
}

/* 跨路径 A / B / C 以及实时 RX 路径 D 漂亮地打印一个信号。
 * 设计上每行都便于在 CANalyzer 录总线时手工解析。*/
static void prv_demo_roundtrip(u16 sig_id, const char *name, u32 raw_in)
{
    const can_sig_desc_t *sig = CanDb_FindIpkSig(sig_id);
    if (sig == NULL) {
        LOG_W("[6/6] %s: CanDb_FindIpkSig sig=%u -> NULL",
              name, (unsigned)sig_id);
        return;
    }

    /* [A] raw -> pack -> GetRaw */
    u8 payload[8] = {0};
    CanDb_PackSignal(payload, sig, raw_in);
    const u32 raw_a = CanDb_GetRaw(payload, sig);

    /* [B] raw -> decode -> phys (s32 整数) */
    const s32 phys_b = CanDb_DecodeSignal(payload, sig);

    /* [C] phys -> encode -> pack -> decode -> phys */
    const can_raw_t raw_c = CanDb_EncodeSignalValue(phys_b, sig);
    u8 payload_c[8] = {0};
    CanDb_PackSignal(payload_c, sig, raw_c);
    const s32 phys_c = CanDb_DecodeSignal(payload_c, sig);

    const bool ok_a = (raw_a == raw_in);
    const bool ok_c = (phys_c == phys_b);
    (void)ok_a;
    (void)ok_c;

    #if CAN_DEMO_LOG
    LOG_I("[6/6] %-18s A:raw 0x%04X->pack->0x%04X %s | "
          "B:phys %d | C:phys%d->encode 0x%04X->pack->phys%d %s | "
          "pld=%02X %02X %02X %02X %02X %02X %02X %02X",
          name,
          (unsigned)raw_in, (unsigned)raw_a, ok_a ? "OK" : "MISS",
          (int)phys_b,
          (int)phys_b, (unsigned)raw_c, (int)phys_c, ok_c ? "OK" : "MISS",
          payload[0], payload[1], payload[2], payload[3],
          payload[4], payload[5], payload[6], payload[7]);
    #endif
}

/* 打印实时 RX 端：Signal_Get 返回 RX 路径最近处理的一帧负载的 raw 位型。
 * 与 CANalyzer 配对使用 —— 如果 CANalyzer 向 0x85 发出一个 16-bit raw
 * 并在这里看到同样的 raw，说明 Motorola 解码正确。
 * `phys` 显示量化后的 s32 值（raw * factor + offset，四舍五入），
 * 用于 SOC 交叉核对。*/
/** @brief  演示 Signal_Get / HasEverReceived / IsValid 三件套 */
static void prv_demo_live_rx(u16 sig_id, signal_id_t bus_id, const char *name)
{
    const u32  raw_live     = Signal_Get(bus_id);
    const u32  raw_ever     = Signal_HasEverReceived(bus_id) ? 1u : 0u;
    const bool valid        = Signal_IsValid(bus_id);
    const can_sig_desc_t *sig = CanDb_FindIpkSig(sig_id);
    s32 phys_live = 0;
    if (sig != NULL) {
        /* 复用最近一帧缓存驱动一次合成解码，模拟消费模块的
         * 行为。不能直接对 Signal_Get() 调 CanDb_DecodeSignal() ——
         * 总线上是 raw，解码需要原始 8 字节负载。这里改为
         * 直接 inline 计算 phys = raw * factor + offset。*/
        phys_live = (s32)(((float)raw_live * sig->factor) + sig->offset + 0.5f);
    }
    (void)phys_live;
    #if CAN_DEMO_LOG
    LOG_I("[6/6] RX %-18s raw=0x%04X ever=%u valid=%u phys=%d",
          name, (unsigned)raw_live, (unsigned)raw_ever,
          (unsigned)valid, (int)phys_live);
    #endif
}

static void prv_demo_raw_physical(u32 sweep)
{
    /* 在完整 length 位宽内扫 raw，使 CANalyzer 能看到
     * 物理量跨越全量程的过渡。*/
    const u32 rpm_mask   = (1u << 16) - 1u;  /* EMS_EngineSpeedRPM len=16 */
    const u32 speed_mask = (1u << 13) - 1u;  /* ESC_VehicleSpeed   len=13 */

    const u32 rpm_raw   = prv_demo_raw_step(sweep * 2u + 1u, rpm_mask);
    const u32 speed_raw = prv_demo_raw_step(sweep * 2u + 2u, speed_mask);

    prv_demo_roundtrip((u16)CAN_DB_SIG_EMS_EngineSpeedRPM,
                       "EMS_EngineSpeedRPM", rpm_raw);
    prv_demo_roundtrip((u16)CAN_DB_SIG_ESC_VehicleSpeed,
                       "ESC_VehicleSpeed",   speed_raw);

    /* 实时 RX：编解码从最近一帧实际抽出的值。*/
    prv_demo_live_rx((u16)CAN_DB_SIG_EMS_EngineSpeedRPM,
                     SIG_CAN_EMS_EngineSpeedRPM, "EMS_EngineSpeedRPM");
    prv_demo_live_rx((u16)CAN_DB_SIG_ESC_VehicleSpeed,
                     SIG_CAN_ESC_VehicleSpeed,   "ESC_VehicleSpeed");
}

/* ---------------------------------------------------------------- *
 *  辅助函数
 * ---------------------------------------------------------------- */

/* 每位 CAN id 查表（Sentinel）。在 can_db_ipk_gen.h 中声明，
 * 在 can_db_ipk_gen.c 中定义。CAN_BITMAP_MAX 是位图宽度
 * （96 项，每位对应 SIG_CAN_RX_TIMEOUT_MAP_* 一个位）；
 * sentinel_unused 为 0，因此 id=0 表示该位保留或已删除。*/
static u32 prv_bit_to_can_id(u32 bit)
{
    if (bit >= (u32)CAN_BITMAP_MAX) { return 0u; }
    const u32 id = s_bit_to_can_id[bit];
    return (id == 0u) ? 0u : id;
}

#endif /* MOD_CAN_DEMO_EN */

/* ---------------------------------------------------------------- *
 *  mod_desc_t 钩子
 * ---------------------------------------------------------------- */
static void prv_mcu_init(uint8_t cold_boot)
{
#if MOD_CAN_DEMO_EN
    (void)cold_boot;
    s_demo.inited       = true;
    s_slot_demo_1s = RTI_OpenSlot(RTI_1000MS);
    s_demo.sweep_count  = 0u;
    LOG_I("init (cold_boot=%u, slot opened)", (unsigned)cold_boot);
#else
    (void)cold_boot;
    LOG_I("init (cold_boot=%u, DISABLED: MOD_CAN_DEMO_EN=0)", (unsigned)cold_boot);
#endif
}

static void prv_wakeup_init(void)
{
#if MOD_CAN_DEMO_EN
    LOG_I("wakeup_init");
#endif
}

static void prv_on_ign_on(void)
{
#if MOD_CAN_DEMO_EN
    LOG_I("on_ign_on");
#endif
}

static void prv_tick(void)
{
#if MOD_CAN_DEMO_EN
    if (!s_demo.inited) { return; }
    if (!RTI_SlotElapsed(&s_slot_demo_1s)) { return; }
    const u32 now = RTI_GetTick1ms();
    s_demo.sweep_count++;
    const u32 sweep = s_demo.sweep_count;
    #if CAN_DEMO_LOG
    LOG_I("=== can_demo sweep #%u (raw cache holds %u) ===",
          (unsigned)sweep, (unsigned)CanIf_RxGetRawFrameCount());
    #endif
    prv_demo_signals();
    prv_demo_timeouts();
    prv_demo_raw_frame();
    prv_demo_tx_payload(sweep);
    prv_demo_tx_signal(sweep);
    prv_demo_raw_physical(sweep);
#endif
}

static void prv_standby(void)
{
#if MOD_CAN_DEMO_EN
    LOG_I("standby");
#endif
}

/* 模块描述符 ---------------------------------------------------- */
/**
 * @brief   Module descriptor registered in scheduler.c
 * @brief   在 scheduler.c 中注册的模块描述符
 */
const mod_desc_t mod_can_demo = {
    .name      = "can_demo",
    .mcu_init   = prv_mcu_init,
    .wakeup_init = prv_wakeup_init,
    .on_ign_on = prv_on_ign_on,
    .tick      = prv_tick,
    .standby   = prv_standby,
};

SCHED_REGISTER(mod_can_demo);
