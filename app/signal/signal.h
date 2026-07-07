/**
 * @file    signal.h
 * @brief   Central signal bus for inter-module communication
 *
 * Modules do NOT exchange data via extern globals. They publish/consume
 * through Signal_Set / Signal_Get. This keeps ownership clear and makes
 * modules individually testable.
 *
 * Signals are statically enumerated; no dynamic registration.
 */
#ifndef C02B2_SIGNAL_H
#define C02B2_SIGNAL_H

#include "types.h"
#include "result.h"

typedef enum {
    SIG_INVALID = 0,

    /* ---------------------------------------------------------------- *
     *  Power / ignition                                                *
     *                                                                    *
     *  LEGACY: kept for ABI/source compatibility with the hand-written  *
     *  CAN samples from earlier iterations.  The current DBC (IPK node) *
     *  does NOT publish any power-domain signal on CAN; if power         *
     *  management ever needs CAN-driven hints, prefer adding new        *
     *  SIG_PWR_CAN_* ids rather than overloading these.                  *
     * ---------------------------------------------------------------- */
    SIG_IGN_ON,            /* bool, 1 = KL15 on                       */
    SIG_ACC_ON,            /* bool, 1 = ACC on                        */
    SIG_KL30_VOLTAGE_MV,   /* int32, KL30 voltage in mV               */
    SIG_PWR_MODE,          /* int32, see pwr_mode_t                   */
    SIG_GC_POWER_ON,       /* bool                                    */
    SIG_IGN_OFF_COUNTER,   /* int32, ticks since IGN off              */
    SIG_SLEEP_READY,       /* bool, all modules allow sleep           */

    /* ---------------------------------------------------------------- *
     *  Vehicle                                                         *
     *                                                                    *
     *  LEGACY: names and units here do not match the IPK DBC (which      *
     *  uses e.g. ESC_VehicleSpeed, EMS_EngineSpeedRPM, EMS_Real_         *
     *  PedalPosition).  New modules should read SIG_CAN_<Name> below     *
     *  instead of these legacy ids.                                      *
     * ---------------------------------------------------------------- */
    SIG_VEH_SPEED_KPH_X10, /* int32, 0.1 kph                          */
    SIG_ENG_RPM,           /* int32, rpm                              */
    SIG_FUEL_LEVEL_PCT,    /* int32, 0..100                           */
    SIG_COOLANT_TEMP_C,    /* int32, degC                             */
    SIG_ODO_TOTAL_M,       /* int32, total odometer in meters         */
    SIG_ODO_TRIP_A_M,      /* int32, trip A in meters                 */
    SIG_ODO_TRIP_B_M,      /* int32, trip B in meters                 */
    SIG_GEAR_POS,          /* int32, see gear_t                       */

    /* ---------------------------------------------------------------- *
     *  Telltale / display                                              *
     *                                                                    *
     *  LEGACY: the DBC drives these via bitfields in dedicated          *
     *  IPK_STS / IPK_SettingRequest frames.  The SIG_TT_* ids are        *
     *  retained for the meter module that already subscribes to them;   *
     *  future work should derive SIG_TT_* from SIG_CAN_<Source>_<Field>.*
     * ---------------------------------------------------------------- */
    SIG_TT_LEFT_TURN,      /* bool                                    */
    SIG_TT_RIGHT_TURN,     /* bool                                    */
    SIG_TT_HIGH_BEAM,      /* bool                                    */
    SIG_TT_LOW_BEAM,       /* bool                                    */
    SIG_TT_FRONT_FOG,      /* bool                                    */
    SIG_TT_REAR_FOG,       /* bool                                    */
    SIG_TT_POSITION_LAMP,  /* bool                                    */
    SIG_TT_SEAT_BELT,      /* bool                                    */
    SIG_TT_BRAKE_FAULT,    /* bool                                    */
    SIG_TT_OIL_PRESS,      /* bool                                    */
    SIG_TT_ABS_FAULT,      /* bool                                    */
    SIG_TT_AIRBAG_FAULT,   /* bool                                    */
    SIG_TT_ENGINE_FAULT,   /* bool                                    */
    SIG_TT_BATTERY_FAULT,  /* bool                                    */
    SIG_TT_FUEL_LOW,       /* bool                                    */
    SIG_TT_CRUISE,         /* bool                                    */
    SIG_TT_EPB,            /* bool                                    */
    SIG_TT_AUTOHOLD,       /* bool                                    */
    SIG_TT_ESP_FAULT,      /* bool                                    */
    SIG_TT_TPMS_FAULT,     /* bool                                    */
    /* ... append new telltale signals here ... */

    /* ---------------------------------------------------------------- *
     *  Illumination                                                    *
     *                                                                    *
     *  LEGACY: brightness / day-night have no CAN source yet.  The      *
     *  IPK_DayNightMode bit in MMI_Safety_Info is the closest match.    *
     * ---------------------------------------------------------------- */
    SIG_ILLU_LCD_PCT,      /* int32, 0..100, LCD backlight            */
    SIG_ILLU_KEY_PCT,      /* int32, 0..100, key backlight            */
    SIG_ILLU_DAY_NIGHT,    /* bool, 1 = night                         */

    /* ---------------------------------------------------------------- *
     *  CAN receive status (timeout flags)                              *
     *                                                                    *
     *  SIG_CAN_RX_TIMEOUT_MAP is the single authoritative source: an    *
     *  int32 bitfield where bit i corresponds to                        *
     *  can_msg_descs_ipk[i].  Bit set = that message has not been       *
     *  received within g_can_rx_timeout_table[i] ms.  Updated by        *
     *  mod_can_rx::prv_check_timeouts() every 50 ms.  Consumers read    *
     *  individual flags via `(Signal_Get(SIG_CAN_RX_TIMEOUT_MAP) >> i)  *
     *  & 1` - no per-message bool signal is needed.                     *
     *                                                                    *
     *  The early per-message bool signals (SIG_CAN_RX_TIMEOUT_0/_1)     *
     *  were removed; if a consumer needs a name, derive one from the    *
     *  bitfield instead of adding to the enum.                         *
     * ---------------------------------------------------------------- */
    SIG_CAN_RX_TIMEOUT_MAP,/* int32, bitfield of timeout flags */

    /* ---------------------------------------------------------------- *
     *  System                                                          *
     * ---------------------------------------------------------------- */
    SIG_FW_VERSION,        /* int32, packed MAJOR/MINOR/PATCH          */
    SIG_BUILD_DATE,        /* int32, packed YYYYMMDD                  */
    SIG_WATCHDOG_KICK,     /* bool, debug: kicked this tick            */

    /* ---------------------------------------------------------------- *
     *  CAN signals (test batch -- IPK node, 13 messages, 132 signals)  *
     *  Source: C02-A1_J7_DHU_MMI_IntergrateTBOX_IPK_20250506_CAN.dbc   *
     *                                                                   *
     *  Each SIG_CAN_<Name> maps 1:1 to CAN_DB_SIG_<Name> in            *
     *  app/can/can_db_ipk_gen.h. Physical value is stored quantized     *
     *  to int32 using (raw * factor + offset); see can_db.c for the    *
     *  inverse on publish. Signal ID assignment is stable: never        *
     *  reorder existing entries, only append.                           *
     * ---------------------------------------------------------------- */

/* 0x02AF MMI_DateTime_Msg (RX)  dlc=8 */
    SIG_CAN_MMI_Second,/* 6bit unsigned raw*1              [0..59s] */
    SIG_CAN_MMI_Minute,/* 6bit unsigned raw*1              [0..59m] */
    SIG_CAN_MMI_Hour,/* 5bit unsigned raw*1              [0..23h] */
    SIG_CAN_MMI_Day,/* 5bit unsigned raw*1              [1..31d] */
    SIG_CAN_MMI_Month,/* 4bit unsigned raw*1              [1..12m] */
    SIG_CAN_MMI_Year,/* 6bit unsigned raw*1              [0..63y] */

/* 0x03E0 MMI_GPS_Info5 (RX)  dlc=8 */
    SIG_CAN_GPS_elevation_Info,/* 18bit unsigned raw*0.1+-10000     [-10000..10000m] */
    SIG_CAN_MMI_GPS_Info5_AliveCounter,/* 4bit unsigned raw*1              [0..15bit] */
    SIG_CAN_MMI_GPS_Info5_CheckSum,/* 8bit unsigned raw*1              [0..255bit] */

/* 0x02A4 MMI_Status_Info (RX)  dlc=8 */
    SIG_CAN_MMI_RightSwStatus,/* 2bit unsigned raw*1              [0..3bit] */
    SIG_CAN_MMI_LeftSwStatus,/* 2bit unsigned raw*1              [0..3bit] */
    SIG_CAN_MMI_UpSwStatus,/* 2bit unsigned raw*1              [0..3bit] */
    SIG_CAN_MMI_DownSwStatus,/* 2bit unsigned raw*1              [0..3bit] */
    SIG_CAN_MMI_ConfirmSwStatus,/* 2bit unsigned raw*1              [0..3bit] */
    SIG_CAN_MMI_ReturnSwStatus,/* 2bit unsigned raw*1              [0..3bit] */
    SIG_CAN_MMI_WorkMode,/* 2bit unsigned raw*1              [0..3bit] */
    SIG_CAN_MMI_SkinMode,/* 2bit unsigned raw*1              [0..3bit] */
    SIG_CAN_MMI_HUDAciveRequest,/* 2bit unsigned raw*1              [0..3bit] */
    SIG_CAN_MMI_AVH_Request,/* 2bit unsigned raw*1              [0..3bit] */
    SIG_CAN_MMI_OdometerClearReq,/* 1bit unsigned raw*1              [0..1bit] */
    SIG_CAN_MMI_SkinModeCorrelativeSts,/* 1bit unsigned raw*1              [0..1bit] */
    SIG_CAN_MMI_SVA_AudibleWarningOption,/* 2bit unsigned raw*1              [0..3bit] */
    SIG_CAN_MMI_LCA_AudioWarning,/* 2bit unsigned raw*1              [0..3bit] */
    SIG_CAN_MMI_ePTRegencyLevRequest,/* 2bit unsigned raw*1              [0..3bit] */
    SIG_CAN_MMI_DM_SwitchModeSts,/* 3bit unsigned raw*1              [0..7bit] */
    SIG_CAN_MMI_RoadCameraWarning,/* 3bit unsigned raw*1              [0..7bit] */
    SIG_CAN_MMI_BCM_DchaChargehint,/* 2bit unsigned raw*1              [0..3bit] */
    SIG_CAN_MMI_RDiffLockReq,/* 2bit unsigned raw*1              [0..3bit] */
    SIG_CAN_MMI_NationalDrivingRank,/* 7bit unsigned raw*1              [0..100bit] */
    SIG_CAN_MMI_EnergySavingModeReq,/* 2bit unsigned raw*1              [0..3bit] */
    SIG_CAN_MMI_CampModeReq,/* 2bit unsigned raw*1              [0..3bit] */
    SIG_CAN_MMI_EpedalSettingInfo,/* 1bit unsigned raw*1              [0..1bit] */
    SIG_CAN_MMI_ESCoffInfo,/* 1bit unsigned raw*1              [0..1bit] */
    SIG_CAN_MMI_HDC_Request,/* 2bit unsigned raw*1              [0..3bit] */
    SIG_CAN_MMI_IntellTurnAidReq,/* 2bit unsigned raw*1              [0..3bit] */
    SIG_CAN_MMI_IntellTurnAidTerrSeln,/* 3bit unsigned raw*1              [0..7bit] */
    SIG_CAN_MMI_OnboardScaleReq,/* 2bit unsigned raw*1              [0..3bit] */
    SIG_CAN_MMI_iTPMS_reset_Request,/* 1bit unsigned raw*1              [0..1bit] */

/* 0x02A8 MMI_Safety_Info (RX)  dlc=8 */
    SIG_CAN_MMI_Nav_SpeedLimit,/* 8bit unsigned raw*1              [0..255KPH] */
    SIG_CAN_MMI_Nav_RampSts,/* 3bit unsigned raw*1              [0..7bit] */
    SIG_CAN_MMI_Nav_CurrRoadType,/* 3bit unsigned raw*1              [0..7bit] */
    SIG_CAN_MMI_Nav_status,/* 2bit unsigned raw*1              [0..3bit] */
    SIG_CAN_MMI_Nav_CountryID,/* 4bit unsigned raw*1              [0..15bit] */
    SIG_CAN_MMI_TollGateIndication,/* 1bit unsigned raw*1              [0..1bit] */
    SIG_CAN_MMI_SVC_status,/* 2bit unsigned raw*1              [0..3bit] */
    SIG_CAN_MMI_APA_Function_Select,/* 2bit unsigned raw*1              [0..3bit] */
    SIG_CAN_MMI_SVA_Request,/* 1bit unsigned raw*1              [0..1bit] */
    SIG_CAN_MMI_APA_ConfirmButton,/* 2bit unsigned raw*1              [0..3bit] */
    SIG_CAN_MMI_PSC_Function_Select,/* 2bit unsigned raw*1              [0..3bit] */
    SIG_CAN_MMI_NavTunnelIndication,/* 1bit unsigned raw*1              [0..1bit] */
    SIG_CAN_MMI_NavDestinationIndication,/* 1bit unsigned raw*1              [0..1bit] */
    SIG_CAN_MMI_DayNightMode,/* 2bit unsigned raw*1              [0..3bit] */
    SIG_CAN_MMI_IBBrakeModeSet,/* 2bit unsigned raw*1              [0..3bit] */
    SIG_CAN_MMI_SailingSwitch,/* 1bit unsigned raw*1              [0..1bit] */
    SIG_CAN_MMI_Start_Stop_switch,/* 1bit unsigned raw*1              [0..1bit] */
    SIG_CAN_MMI_NavSyncDisplay,/* 2bit unsigned raw*1              [0..3bit] */
    SIG_CAN_MMI_Safety_Info_AliveCounter,/* 4bit unsigned raw*1              [0..15bit] */
    SIG_CAN_MMI_Safety_Info_CheckSum,/* 8bit unsigned raw*1              [0..255bit] */

/* 0x02A6 MMI_SOCSet (RX)  dlc=8 */
    SIG_CAN_MMI_DchaChargeControlCmd,/* 2bit unsigned raw*1              [0..3bit] */
    SIG_CAN_MMI_SocWarValue,/* 2bit unsigned raw*1              [0..3bit] */
    SIG_CAN_MMI_EVDTEodometer_ModeSet,/* 1bit unsigned raw*1              [0..1bit] */
    SIG_CAN_MMI_FridgeCoolReq,/* 2bit unsigned raw*1              [0..3bit] */
    SIG_CAN_MMI_SaveModeReq,/* 2bit unsigned raw*1              [0..2bit] */
    SIG_CAN_MMI_ACDisChrgCmd,/* 2bit unsigned raw*1              [0..3bit] */
    SIG_CAN_MMI_RearPanelDisChrgCmd,/* 2bit unsigned raw*1              [0..3bit] */
    SIG_CAN_MMI_DCDisChrgCmd,/* 2bit unsigned raw*1              [0..3bit] */
    SIG_CAN_MMI_FrontPanelDisChrgCmd,/* 2bit unsigned raw*1              [0..3bit] */
    SIG_CAN_MMI_OTA_Status,/* 3bit unsigned raw*1              [0..5bit] */
    SIG_CAN_MMI_MidPanelDisChrgCmd,/* 2bit unsigned raw*1              [0..3bit] */
    SIG_CAN_MMI_RefuUnlckReq,/* 2bit unsigned raw*1              [0..3bit] */
    SIG_CAN_MMI_SOCPointSet,/* 7bit unsigned raw*1              [0..100%] */

/* 0x0084 EMS_EngRelateTrqSts (RX)  dlc=8 */
    SIG_CAN_EMS_ControlSetSpeed,/* 8bit unsigned raw*1              [0..254kph] */
    SIG_CAN_EMS_CruiseSwitchSts,/* 1bit unsigned raw*1              [0..1bit] */
    SIG_CAN_EMS_CruiseControlSts,/* 3bit unsigned raw*1              [0..7bit] */
    SIG_CAN_EMS_Real_PedalPositionInvalid,/* 1bit unsigned raw*1              [0..1bit] */
    SIG_CAN_EMS_Real_PedalPosition,/* 8bit unsigned raw*0.4+0          [0..100%] */

/* 0x0085 EMS_EngineRPM (RX)  dlc=8 */
    SIG_CAN_EMS_FuelPulsesRollingCounter,/* 8bit unsigned raw*0.0788519+0    [0..20.0284ml] */
    SIG_CAN_EMS_EngineSpeedRPM,/* 16bit unsigned raw*0.25+0         [0..16383.3Rpm] */
    SIG_CAN_EMS_EngineSpeedRPMInvalid,/* 1bit unsigned raw*1              [0..1bit] */
    SIG_CAN_EMS_EngStatus,/* 3bit unsigned raw*1              [0..7bit] */
    SIG_CAN_EMS_AccelPedalPosition,/* 8bit unsigned raw*0.4+0          [0..100%] */
    SIG_CAN_EMS_AccelPedalPositionInvalid,/* 1bit unsigned raw*1              [0..1bit] */

/* 0x0288 EMS_EngineDriverInfo (RX)  dlc=8 */
    SIG_CAN_EMS_EngineCoolantTemperature,/* 8bit unsigned raw*0.75+-36.8     [-36.8..137.2��C] */
    SIG_CAN_EMS_EngineCoolantTemperatureInva,/* 1bit unsigned raw*1              [0..1bit] */
    SIG_CAN_EMS_EngineSVSTelltale,/* 1bit unsigned raw*1              [0..1bit] */
    SIG_CAN_EMS_EngineMILTelltale,/* 1bit unsigned raw*1              [0..1bit] */
    SIG_CAN_EMS_OilPressureWarning,/* 1bit unsigned raw*1              [0..1bit] */
    SIG_CAN_EMS_Odometerbackup,/* 14bit unsigned raw*100+0          [0..1.6382e+06km] */

/* 0x028A EMS_EnginePatsBatteryStat (RX)  dlc=8 */
    SIG_CAN_EMS_LIMSetSpeed,/* 8bit unsigned raw*1              [0..254kph] */
    SIG_CAN_EMS_EAV_ModSetStatus,/* 1bit unsigned raw*1              [0..1bit] */
    SIG_CAN_EMS_LIMControlSts,/* 2bit unsigned raw*1              [0..3bit] */
    SIG_CAN_EMS_LIMmemorySts_Reserved,/* 1bit unsigned raw*1              [0..1bit] */
    SIG_CAN_EMS_LIMSwitchSts,/* 1bit unsigned raw*1              [0..1bit] */
    SIG_CAN_EMS_LIMOverSpdWarningSts,/* 1bit unsigned raw*1              [0..1bit] */

/* 0x028C EMS_OBD_Info (RX)  dlc=8 */
    SIG_CAN_EMS_GPF_Warning,/* 2bit unsigned raw*1              [0..3bit] */
    SIG_CAN_EMS_BrakeOverrideSts,/* 1bit unsigned raw*1              [0..1bit] */
    SIG_CAN_EMS_TankLeakDiagSts,/* 2bit unsigned raw*1              [0..3bit] */
    SIG_CAN_EMS_AdaptiveTargetMode,/* 2bit unsigned raw*1              [0..3bit] */
    SIG_CAN_EMS_DM_ModeProgBar,/* 7bit unsigned raw*1              [0..100%] */

/* 0x03E9 IPK_EngineService (TX)  dlc=8 */
    SIG_CAN_IPK_IPKEngineTotalOdometer,/* 20bit unsigned raw*1              [0..999999KM] */
    SIG_CAN_IPK_DayToEngSrv,/* 9bit unsigned raw*1              [0..360Day] */
    SIG_CAN_IPK_ServiceEngineMaintainInterva,/* 16bit unsigned raw*1              [0..20000km] */

/* 0x026D IPK_STS (TX)  dlc=8 */
    SIG_CAN_IPK_AirbagUnitLEDSts,/* 2bit unsigned raw*1              [0..3bit] */
    SIG_CAN_IPK_SVA_AudibleWarningCfgResult,/* 1bit unsigned raw*1              [0..1bit] */
    SIG_CAN_IPK_FuelLowLevelWarning,/* 1bit unsigned raw*1              [0..1bit] */
    SIG_CAN_IPK_ESCoffInfo,/* 1bit unsigned raw*1              [0..1bit] */
    SIG_CAN_IPK_Fail,/* 1bit unsigned raw*1              [0..1bit] */
    SIG_CAN_IPK_QDashALODFail,/* 2bit unsigned raw*1              [0..3bit] */
    SIG_CAN_IPK_FuelLevelSts,/* 8bit unsigned raw*0.5+0          [0..127L] */
    SIG_CAN_IPK_AverageVehicleSpeed,/* 8bit unsigned raw*1              [0..254KPH] */
    SIG_CAN_IPK_HandBrakeSts,/* 1bit unsigned raw*1              [0..1bit] */
    SIG_CAN_IPK_MaintanceWarningSts,/* 1bit unsigned raw*1              [0..1bit] */
    SIG_CAN_IPK_LanguageMode,/* 4bit unsigned raw*1              [0..3bit] */
    SIG_CAN_IPK_EPS_ModSetSelection,/* 2bit unsigned raw*1              [0..3bit] */
    SIG_CAN_IPK_EPS_DMCorrelativeMode,/* 2bit unsigned raw*1              [0..3bit] */
    SIG_CAN_IPK_Backlightadjust,/* 5bit unsigned raw*1              [0..31bit] */
    SIG_CAN_IPK_vDisplay,/* 13bit unsigned raw*0.05625+0      [0..460.688KPH] */
    SIG_CAN_IPK_OilLowPressure,/* 1bit unsigned raw*1              [0..1bit] */
    SIG_CAN_IPK_LIMmemoryEnabe_Reserved,/* 2bit unsigned raw*1              [0..3bit] */
    SIG_CAN_IPK_BattLowVoltageWarning,/* 1bit unsigned raw*1              [0..1bit] */
    SIG_CAN_IPK_driving_mode_light_sts,/* 2bit unsigned raw*1              [0..3bit] */

/* 0x0260 IPK_SettingRequest (TX)  dlc=8 */
    SIG_CAN_IPK_AEB_FCWStateReq,/* 2bit unsigned raw*1              [0..3bit] */
    SIG_CAN_IPK_AEB_AEBStateReq,/* 2bit unsigned raw*1              [0..3bit] */
    SIG_CAN_IPK_ALOD_ControlTypeReq,/* 2bit unsigned raw*1              [0..3bit] */
    SIG_CAN_IPK_LCA_EnableStatus,/* 2bit unsigned raw*1              [0..3bit] */
    SIG_CAN_IPK_RCTA_EnableStatus,/* 2bit unsigned raw*1              [0..3bit] */
    SIG_CAN_IPK_RCW_EnableStatus,/* 2bit unsigned raw*1              [0..3bit] */
    SIG_CAN_IPK_AEB_FCWSenlevel,/* 2bit unsigned raw*1              [0..3bit] */
    SIG_CAN_IPK_LKS_LaneAssistTypeReq,/* 3bit unsigned raw*1              [0..7bit] */
    SIG_CAN_IPK_LDW_WarningTypeSetting,/* 2bit unsigned raw*1              [0..3bit] */
    SIG_CAN_IPK_IHBC_MenuReq,/* 2bit unsigned raw*1              [0..3bit] */
    SIG_CAN_IPK_SLIF_MenuReq,/* 2bit unsigned raw*1              [0..3bit] */


    SIG_MAX
} signal_id_t;

/**
 * @brief   Publish a value on the signal bus
 * @brief   在信号总线上发布一个值
 *
 * @details Stamps the value and marks the slot as valid. The
 *          owner of the signal is responsible for the rate.
 *          Returns C02B2_ERR_PARAM for SIG_INVALID / out-of-range ids.
 *
 * @param[in]  id     Signal id (see signal_id_t)
 * @param[in]  value  32-bit payload; interpretation depends on signal
 *
 * @return  c02b2_result_t
 * @retval  C02B2_OK            Value stored
 * @retval  C02B2_ERR_PARAM     id invalid
 */
c02b2_result_t Signal_Set(signal_id_t id, int32_t value);

/**
 * @brief   Read the current value of a signal
 * @brief   读取信号的当前值
 *
 * @details Returns 0 for unknown / out-of-range ids. Callers
 *          that care about freshness should use Signal_IsValid().
 *
 * @param[in]  id  Signal id
 *
 * @return  int32_t  Last value set, or 0 if never set / invalid id
 */
int32_t      Signal_Get(signal_id_t id);

/**
 * @brief   Check whether the signal slot is currently valid
 * @brief   检查信号槽位当前是否有效
 *
 * @param[in]  id  Signal id
 *
 * @return  bool
 * @retval  true   Signal has been set and not invalidated
 * @retval  false  Never set, explicitly invalidated, or invalid id
 */
bool         Signal_IsValid(signal_id_t id);

/**
 * @brief   Mark a single signal as invalid (next Get returns 0)
 * @brief   将单个信号标记为无效（下次 Get 返回 0）
 *
 * @param[in]  id  Signal id
 */
void         Signal_Invalidate(signal_id_t id);

/**
 * @brief   Mark every signal as invalid
 * @brief   将所有信号标记为无效
 *
 * @details Used on power-mode transitions / factory reset to force
 *          all consumers to republish.
 */
void         Signal_InvalidateAll(void);

#endif /* C02B2_SIGNAL_H */
