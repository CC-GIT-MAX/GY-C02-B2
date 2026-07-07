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
     *  Two int32 bitfields cover all 64 RX messages in the IPK DBC:    *
     *    SIG_CAN_RX_TIMEOUT_MAP_LO:  bits  0..31 -> rx_msg_idx 0..31    *
     *    SIG_CAN_RX_TIMEOUT_MAP_HI:  bits  0..31 -> rx_msg_idx 32..63   *
     *                                                                    *
     *  Bit set means "no frame received within                          *
     *  g_can_rx_timeout_table[rx_msg_idx] ms".  Updated by              *
     *  mod_can_rx::prv_check_timeouts() every 50 ms.  Consumers read    *
     *  individual flags via                                            *
     *    idx < 32  ->  (Signal_Get(SIG_CAN_RX_TIMEOUT_MAP_LO) >> idx) & 1*
     *    idx >= 32 ->  (Signal_Get(SIG_CAN_RX_TIMEOUT_MAP_HI) >> (idx-32)) & 1*
     *                                                                    *
     *  The early per-message bool signals (SIG_CAN_RX_TIMEOUT_0/_1)     *
     *  were removed; if a consumer needs a name, derive one from the    *
     *  bitfields instead of adding to the enum.                         *
     *                                                                    *
     *  NOTE: TX messages and rx_msg_idx >= 64 have no timeout tracking  *
     *  and never set a bit.  g_can_rx_timeout_table entries for those   *
     *  rows are 0 (never monitored).                                    *
     * ---------------------------------------------------------------- */
    SIG_CAN_RX_TIMEOUT_MAP_LO, /* int32, bits 0..31 = rx_msg_idx 0..31 timeout flags */
    SIG_CAN_RX_TIMEOUT_MAP_HI, /* int32, bits 0..31 = rx_msg_idx 32..63 timeout flags */

    /* ---------------------------------------------------------------- *
     *  CAN bus health (error interrupt driven)                         *
     *                                                                    *
     *  Published by app/drv_api/can/can_if.c::prv_flexcan_err_cb.       *
     *  Updated on every error interrupt from the SDK so any consumer   *
     *  (diag, NM, meter telltale) can react.                            *
     * ---------------------------------------------------------------- */
    SIG_CAN_BUS_OFF,         /* bool, 1 = instance currently in bus-off */
    SIG_CAN_BUS_OFF_COUNT,   /* int32, cumulative bus-off enter count   */
    SIG_CAN_TX_ERR_CNT,      /* int32, last TX error counter (0..255)    */
    SIG_CAN_RX_ERR_CNT,      /* int32, last RX error counter (0..255)    */

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

/* 0x2AF MMI_DateTime_Msg (RX)  dlc=8 */
    SIG_CAN_MMI_Second,/* 6bit unsigned raw*1 [0..63] */
    SIG_CAN_MMI_Minute,/* 6bit unsigned raw*1 [0..63] */
    SIG_CAN_MMI_Hour,/* 5bit unsigned raw*1 [0..31] */
    SIG_CAN_MMI_Day,/* 5bit unsigned raw*1 [0..31] */
    SIG_CAN_MMI_Month,/* 4bit unsigned raw*1 [0..15] */
    SIG_CAN_MMI_Year,/* 6bit unsigned raw*1 [0..63] */
/* 0x3E0 MMI_GPS_Info5 (RX)  dlc=8 */
    SIG_CAN_GPS_elevation_Info,/* 18bit unsigned raw*0.1-10000 [-10000..16214.3] */
    SIG_CAN_MMI_GPS_Info5_AliveCounter,/* 4bit unsigned raw*1 [0..15] */
    SIG_CAN_MMI_GPS_Info5_CheckSum,/* 8bit unsigned raw*1 [0..255] */
/* 0x2A4 MMI_Status_Info (RX)  dlc=8 */
    SIG_CAN_MMI_RightSwStatus,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_MMI_LeftSwStatus,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_MMI_UpSwStatus,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_MMI_DownSwStatus,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_MMI_ConfirmSwStatus,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_MMI_ReturnSwStatus,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_MMI_WorkMode,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_MMI_SkinMode,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_MMI_HUDAciveRequest,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_MMI_AVH_Request,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_MMI_OdometerClearReq,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_MMI_SkinModeCorrelativeSts,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_MMI_SVA_AudibleWarningOption,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_MMI_LCA_AudioWarning,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_MMI_ePTRegencyLevRequest,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_MMI_DM_SwitchModeSts,/* 3bit unsigned raw*1 [0..7] */
    SIG_CAN_MMI_RoadCameraWarning,/* 3bit unsigned raw*1 [0..7] */
    SIG_CAN_MMI_BCM_DchaChargehint,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_MMI_RDiffLockReq,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_MMI_NationalDrivingRank,/* 7bit unsigned raw*1 [0..127] */
    SIG_CAN_MMI_EnergySavingModeReq,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_MMI_CampModeReq,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_MMI_EpedalSettingInfo,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_MMI_ESCoffInfo,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_MMI_HDC_Request,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_MMI_IntellTurnAidReq,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_MMI_IntellTurnAidTerrSeln,/* 3bit unsigned raw*1 [0..7] */
    SIG_CAN_MMI_OnboardScaleReq,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_MMI_iTPMS_reset_Request,/* 1bit unsigned raw*1 [0..1] */
/* 0x2A8 MMI_Safety_Info (RX)  dlc=8 */
    SIG_CAN_MMI_Nav_SpeedLimit,/* 8bit unsigned raw*1 [0..255] */
    SIG_CAN_MMI_Nav_RampSts,/* 3bit unsigned raw*1 [0..7] */
    SIG_CAN_MMI_Nav_CurrRoadType,/* 3bit unsigned raw*1 [0..7] */
    SIG_CAN_MMI_Nav_status,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_MMI_Nav_CountryID,/* 4bit unsigned raw*1 [0..15] */
    SIG_CAN_MMI_TollGateIndication,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_MMI_SVC_status,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_MMI_APA_Function_Select,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_MMI_SVA_Request,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_MMI_APA_ConfirmButton,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_MMI_PSC_Function_Select,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_MMI_NavTunnelIndication,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_MMI_NavDestinationIndication,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_MMI_DayNightMode,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_MMI_IBBrakeModeSet,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_MMI_SailingSwitch,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_MMI_Start_Stop_switch,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_MMI_NavSyncDisplay,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_MMI_Safety_Info_AliveCounter,/* 4bit unsigned raw*1 [0..15] */
    SIG_CAN_MMI_Safety_Info_CheckSum,/* 8bit unsigned raw*1 [0..255] */
/* 0x2A6 MMI_SOCSet (RX)  dlc=8 */
    SIG_CAN_MMI_DchaChargeControlCmd,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_MMI_SocWarValue,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_MMI_EVDTEodometer_ModeSet,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_MMI_FridgeCoolReq,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_MMI_SaveModeReq,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_MMI_ACDisChrgCmd,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_MMI_RearPanelDisChrgCmd,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_MMI_DCDisChrgCmd,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_MMI_FrontPanelDisChrgCmd,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_MMI_OTA_Status,/* 3bit unsigned raw*1 [0..7] */
    SIG_CAN_MMI_MidPanelDisChrgCmd,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_MMI_RefuUnlckReq,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_MMI_SOCPointSet,/* 7bit unsigned raw*1 [0..127] */
/* 0x084 EMS_EngRelateTrqSts (RX)  dlc=8 */
    SIG_CAN_EMS_ControlSetSpeed,/* 8bit unsigned raw*1 [0..255] */
    SIG_CAN_EMS_CruiseSwitchSts,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_EMS_CruiseControlSts,/* 3bit unsigned raw*1 [0..7] */
    SIG_CAN_EMS_Real_PedalPositionInvalid,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_EMS_Real_PedalPosition,/* 8bit unsigned raw*0.4 [0..102] */
/* 0x085 EMS_EngineRPM (RX)  dlc=8 */
    SIG_CAN_EMS_FuelPulsesRollingCounter,/* 8bit unsigned raw*0.0788519 [0..20.1072] */
    SIG_CAN_EMS_EngineSpeedRPM,/* 16bit unsigned raw*0.25 [0..16383.8] */
    SIG_CAN_EMS_EngineSpeedRPMInvalid,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_EMS_EngStatus,/* 3bit unsigned raw*1 [0..7] */
    SIG_CAN_EMS_AccelPedalPosition,/* 8bit unsigned raw*0.4 [0..102] */
    SIG_CAN_EMS_AccelPedalPositionInvalid,/* 1bit unsigned raw*1 [0..1] */
/* 0x288 EMS_EngineDriverInfo (RX)  dlc=8 */
    SIG_CAN_EMS_EngineCoolantTemperature,/* 8bit unsigned raw*0.75-36.8 [-36.8..154.45] */
    SIG_CAN_EMS_EngineCoolantTemperatureInva,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_EMS_EngineSVSTelltale,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_EMS_EngineMILTelltale,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_EMS_OilPressureWarning,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_EMS_Odometerbackup,/* 14bit unsigned raw*100 [0..1638300] */
/* 0x28A EMS_EnginePatsBatteryStat (RX)  dlc=8 */
    SIG_CAN_EMS_LIMSetSpeed,/* 8bit unsigned raw*1 [0..255] */
    SIG_CAN_EMS_EAV_ModSetStatus,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_EMS_LIMControlSts,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_EMS_LIMmemorySts_Reserved,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_EMS_LIMSwitchSts,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_EMS_LIMOverSpdWarningSts,/* 1bit unsigned raw*1 [0..1] */
/* 0x28C EMS_OBD_Info (RX)  dlc=8 */
    SIG_CAN_EMS_GPF_Warning,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_EMS_BrakeOverrideSts,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_EMS_TankLeakDiagSts,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_EMS_AdaptiveTargetMode,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_EMS_DM_ModeProgBar,/* 7bit unsigned raw*1 [0..127] */
/* 0x145 EGSM_Status (RX)  dlc=8 */
    SIG_CAN_EGSM_ParkReq,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_EGSM_DriveMod,/* 3bit unsigned raw*1 [0..7] */
    SIG_CAN_EGSM_LeverPosition,/* 3bit unsigned raw*1 [0..7] */
    SIG_CAN_EGSM_Paddleinputs,/* 3bit unsigned raw*1 [0..7] */
    SIG_CAN_EGSM_ReleaseReq,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_EGSM_Status_AliveCounter,/* 4bit unsigned raw*1 [0..15] */
    SIG_CAN_EGSM_Status_Checksum,/* 8bit unsigned raw*1 [0..255] */
/* 0x220 OBC_Sts (RX)  dlc=8 */
    SIG_CAN_ODP_OBC_ChrgrSt,/* 4bit unsigned raw*1 [0..15] */
    SIG_CAN_OBC_OnBdFailSt,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_OBC_OnBdDisChrgrCCline_PHEV,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_ODP_OBC_OnBdCCline,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_ODP_OBC_OnBdCPline,/* 3bit unsigned raw*1 [0..7] */
/* 0x221 OBC_Curr (RX)  dlc=8 */
    SIG_CAN_ODP_OBC_OnBdDisChrgrCCline_EV,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_ODP_OBC_iInAct,/* 10bit unsigned raw*0.1 [0..102.3] */
    SIG_CAN_OBC_uInAct,/* 12bit unsigned raw*0.1 [0..409.5] */
    SIG_CAN_ODP_OBC_POuptAct,/* 7bit unsigned raw*0.1 [0..12.7] */
/* 0x222 OBC_Failmode (RX)  dlc=8 */
    SIG_CAN_ODP_OBC_Failmode_ACVI_overtemp,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_ODP_OBC_Failmode_Hndl_Error,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_ODP_OBC_Failmode_ACVI_Invalid,/* 1bit unsigned raw*1 [0..1] */
/* 0x354 BMSH_sts (RX)  dlc=8 */
    SIG_CAN_BMSH_BattSOH,/* 10bit unsigned raw*0.1 [0..102.3] */
    SIG_CAN_BMSH_ChgTimeRemain,/* 11bit unsigned raw*1 [0..2047] */
    SIG_CAN_BMSH_BattRemainEnergy,/* 14bit unsigned raw*0.1 [0..1638.3] */
    SIG_CAN_BMSH_Discharge_Over_Current,/* 2bit unsigned raw*1 [0..3] */
/* 0x17B BMSH_Battery_chgstate (RX)  dlc=8 */
    SIG_CAN_BMSH_ThermalOutofControl,/* 1bit unsigned raw*1 [0..1] */
/* 0x210 BMSH_CellTempLimitValue (RX)  dlc=8 */
    SIG_CAN_BMSH_BattTempAvg,/* 8bit unsigned raw*0.5-40 [-40..87.5] */
    SIG_CAN_BMSH_TempSensor_MinTemp,/* 8bit unsigned raw*0.5-40 [-40..87.5] */
/* 0x162 VCU_Ctrl (RX)  dlc=8 */
    SIG_CAN_VCU_ePTModeSelect,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_VCU_ePTModeActual,/* 3bit unsigned raw*1 [0..7] */
    SIG_CAN_VCU_Warning_IMMO_Fail,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_VCU_ChargerFault,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_VCU_ePTReady,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_VCU_ePTDivPowerPcent,/* 8bit unsigned raw*1-100 [-100..155] */
    SIG_CAN_VCU_ePTReleaseSig,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_VCU_ePTRegencyLevInd,/* 3bit unsigned raw*1 [0..7] */
    SIG_CAN_VCU_ePTModReqRejt,/* 3bit unsigned raw*1 [0..7] */
    SIG_CAN_VCU_LimpHomeSts_HB,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_VCU_TurtleLampOn,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_VCU_ePTFault,/* 1bit unsigned raw*1 [0..1] */
/* 0x214 VCU_InforCAN (RX)  dlc=8 */
    SIG_CAN_VCU_EVDTEodometer,/* 12bit unsigned raw*1 [0..4095] */
    SIG_CAN_HmiEngHybSts,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_VCU_PtModAct,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_VCU_PtModBlkd,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_VCU_PwrLoIndcn,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_VCU_EngFltIndcn,/* 4bit unsigned raw*1 [0..15] */
    SIG_CAN_VCU_VehPullOverWarn,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_VCU_EpedalActive,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_VCU_MainDrivingDoorOpenHV,/* 1bit unsigned raw*1 [0..1] */
/* 0x161 VCU_DCDC_Ctrl (RX)  dlc=8 */
    SIG_CAN_DispWhlMotSysPrpsnMod,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_VCU_DriverRemind,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_VCU_DiscrgPanellSw,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_VCU_DischgFailSt,/* 3bit unsigned raw*1 [0..7] */
    SIG_CAN_VCU_MileageReward,/* 11bit unsigned raw*0.1 [0..204.7] */
    SIG_CAN_DispOfPrpsnMod,/* 4bit unsigned raw*1 [0..15] */
/* 0x1A5 VCU_CSControl1 (RX)  dlc=8 */
    SIG_CAN_VCU_StartActive,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_VCU_CruiseControlSts,/* 3bit unsigned raw*1 [0..7] */
    SIG_CAN_VCU_CruiseSwitchSts,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_VCU_ControlSetSpeed,/* 8bit unsigned raw*1 [0..255] */
    SIG_CAN_VCU_DM_FailureReasonAndSysFaultF,/* 4bit unsigned raw*1 [0..15] */
/* 0x165 VCU_ModeControl (RX)  dlc=8 */
    SIG_CAN_VCU_Real_PedalPosition,/* 8bit unsigned raw*0.4 [0..102] */
    SIG_CAN_VCU_ModeGearDisplaySts,/* 5bit unsigned raw*1 [0..31] */
    SIG_CAN_VCU_LIMOverSpdWarningSts,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_VCU_EVSpeedlimitedInf,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_VCU_ChargeHV_Status,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_VCU_WhlDrvSt_CS,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_VCU_LIMSetSpeed,/* 8bit unsigned raw*1 [0..255] */
    SIG_CAN_VCU_LIMSwitchSts,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_VCU_LIMControlSts,/* 2bit unsigned raw*1 [0..3] */
/* 0x0A8 IPU_TrqSpd (RX)  dlc=8 */
    SIG_CAN_IPU_IsgTqActIsgTqAct,/* 16bit unsigned raw*0.1-1200 [-1200..5353.5] */
    SIG_CAN_IPU_IsgSpdActSgn,/* 16bit unsigned raw*1-20000 [-20000..45535] */
    SIG_CAN_IPU_TrqSpd_AliveCounter,/* 4bit unsigned raw*1 [0..15] */
    SIG_CAN_IPU_TrqSpd_Checksum,/* 8bit unsigned raw*1 [0..255] */
/* 0x171 IPU_Sts (RX)  dlc=8 */
    SIG_CAN_IPU_TqAvlIsgMax,/* 11bit unsigned raw*1-1024 [-1024..1023] */
    SIG_CAN_IPU_TqAvlIsgMin,/* 11bit unsigned raw*1-1024 [-1024..1023] */
    SIG_CAN_IPU_Sts_AliveCounter,/* 4bit unsigned raw*1 [0..15] */
    SIG_CAN_IPU_Sts_Checksum,/* 8bit unsigned raw*1 [0..255] */
/* 0x2B2 AVAS_DisabledSts (RX)  dlc=8 */
    SIG_CAN_AVAS_AVASDisabledSts,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_AVAS_AVASBackwardDisabledSts,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_AVAS_AVASforwardDisabledSts,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_AVAS_AVASForwardWarningSts,/* 4bit unsigned raw*1 [0..15] */
    SIG_CAN_AVAS_AVASVolumeSts,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_AVAS_AVASBackwardWarningSts,/* 4bit unsigned raw*1 [0..15] */
/* 0x380 ACU_ChimeTelltaleReq (RX)  dlc=8 */
    SIG_CAN_ACU_DrvSeatbeltBucklestatus,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_ACU_PassSeatbeWarning,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_ACU_PassSeatOccupantSensorStat,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_ACU_2nd_RSeatbeltBucklestatus,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_ACU_PassengerAirbagStatus,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_ACU_AirbagWarningStatus,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_ACU_DrvSeatbeltBuckleInvalid,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_ACU_PassSeatbeltBuckleInvalid,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_ACU_2nd_LSeatbeltBucklestatus,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_ACU_2nd_MSeatbeltBucklestatus,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_ACU_3rd_LSeatbeltBucklestatus,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_ACU_3rd_RSeatbeltBucklestatus,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_ACU_3rd_MSeatbeltBucklestatus,/* 1bit unsigned raw*1 [0..1] */
/* 0x150 EPS_InformSts (RX)  dlc=8 */
    SIG_CAN_EPS_EpasFailed,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_EPS_ModSetInhibit,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_EPS_ModSts,/* 2bit unsigned raw*1 [0..3] */
/* 0x125 ESC_Status (RX)  dlc=8 */
    SIG_CAN_ESC_BrakePedalSwitchStatus,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_ESC_EBDFailed,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_ESC_ABSFailed,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_ESC_TCSFailed,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_ESC_ESPFailed,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_ESC_VehicleSpeed,/* 13bit unsigned raw*0.05625 [0..460.744] */
    SIG_CAN_ESC_PATAResponse,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_ESC_LampInfo,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_ESC_VehicleSpeedInvalid,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_ESC_AVHStatus,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_ESC_EPBStatus,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_ESC_EPBErrorStatus,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_ESC_BrakePedalSwitchInvalid,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_ESC_HHC_ErrorStatus,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_ESC_HDC_ErrorStatus,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_ESC_HDC_Active,/* 2bit unsigned raw*1 [0..3] */
/* 0x12F ESC_DriverRemind (RX)  dlc=8 */
    SIG_CAN_ESC_AVH_Disp_WithoutSeatbelt,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_ESC_ParkingBrakeFail,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_ESC_EPB_Disp_WithoutSeatbelt,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_ESC_EPB_Disp_WithoutBrake,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_ESC_EPB_SlopeOverThresholdRemind,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_ESC_EPB_ReleaseInNonP,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_ESC_HDC_Disp_DiscTempHigh,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_ESC_HDC_TargetSpeed,/* 6bit unsigned raw*1 [0..63] */
    SIG_CAN_ESC_SystemWarning,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_ESC_iTPMS_FLTyreWarn,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_ESC_iTPMS_FRTyreWarn,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_ESC_iTPMS_RLTyreWarn,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_ESC_iTPMS_RRTyreWarn,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_ESC_iTPMS_comTyreWarn,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_ESC_iTPMS_SystemSt,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_ESC_iTPMSCalPsbl,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_ESC_iTPMSCalsts,/* 3bit unsigned raw*1 [0..7] */
/* 0x128 ESC_Regen (RX)  dlc=8 */
    SIG_CAN_ESC_BrakeFluidLevelLow,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_ESC_MbRegenTargetWheelSts,/* 2bit unsigned raw*1 [0..3] */
/* 0x2E4 RSRSR_InformStatus (RX)  dlc=8 */
    SIG_CAN_RSRSR_SystemStatus,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_RSRSR_LCA_Status,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_RSRSR_RCTA_Status,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_RSRSR_RCW_Status,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_RSRSR_LCA_WarningLeft,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_RSRSR_LCA_WarningRight,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_RSRSR_RCTA_WarningLeft,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_RSRSR_RCW_Warning,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_RSRSR_DOW_Status,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_RSRSR_DOW_WarningLeft,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_RSRSR_DOW_WarningRight,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_RSRSR_LCA_AudioSwitch,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_RSRSR_RCTA_WarningRight,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_RSRSR_RCTA_Brake,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_RSRSR_RCW_Brake,/* 1bit unsigned raw*1 [0..1] */
/* 0x1B0 FCS_ALAD_Status (RX)  dlc=8 */
    SIG_CAN_FCS_ALAD_SwitchStatus,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_FCS_ALAD_Type,/* 3bit unsigned raw*1 [0..7] */
    SIG_CAN_FCS_ALAD_WarningType,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_FCS_ALAD_Status,/* 4bit unsigned raw*1 [0..15] */
    SIG_CAN_FCS_FCS_Status,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_FCS_ALAD_Warning,/* 3bit unsigned raw*1 [0..7] */
/* 0x2E0 FCS_SLIF_IHBC_Status (RX)  dlc=8 */
    SIG_CAN_FCS_IHBC_Status,/* 3bit unsigned raw*1 [0..7] */
    SIG_CAN_FCS_IHBC_LightDistribReq,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_FCS_IHBC_Switch,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_FCS_SLIF_Switch,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_FCS_Sign_Speedlimit,/* 8bit unsigned raw*1 [0..255] */
    SIG_CAN_FCS_Sign_SpeedLimitCancelled,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_FCS_DrvOff_Switch,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_FCS_SLIF_Status,/* 3bit unsigned raw*1 [0..7] */
    SIG_CAN_FCS_SLIF_Warning,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_FCS_SLIF_WarningSwitch,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_FCS_Sign_Misc,/* 6bit unsigned raw*1 [0..63] */
    SIG_CAN_FCS_SLIF_IHBC_Status_AliveCounte,/* 4bit unsigned raw*1 [0..15] */
    SIG_CAN_FCS_SLIF_MiscSts,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_FCS_SLIF_IHBC_Status_CheckSum,/* 8bit unsigned raw*1 [0..255] */
/* 0x1B2 FCS_Road_Status (RX)  dlc=8 */
    SIG_CAN_FCS_LineLeft_D,/* 6bit unsigned raw*0.1 [0..6.3] */
    SIG_CAN_FCS_LineLeft_Type,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_FCS_LineLeft_Warning,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_FCS_LineLeft_Color,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_FCS_LineRight_Color,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_FCS_LineRight_Warning,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_FCS_LineRight_D,/* 6bit unsigned raw*0.1-6.2 [-6.2..0.1] */
    SIG_CAN_FCS_LineRight_Type,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_FCS_Lane_Radius,/* 7bit unsigned raw*50-3150 [-3150..3200] */
    SIG_CAN_FCS_DriveOnLine,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_FCS_LaneChange_Det,/* 3bit unsigned raw*1 [0..7] */
    SIG_CAN_FCS_LaneCenter_Color,/* 3bit unsigned raw*1 [0..7] */
/* 0x114 FCS_ELK_Status (RX)  dlc=8 */
    SIG_CAN_FCS_ELK_SwitchStatus,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_FCS_ELK_Status,/* 3bit unsigned raw*1 [0..7] */
    SIG_CAN_FCS_ELK_LeftMode,/* 3bit unsigned raw*1 [0..7] */
    SIG_CAN_FCS_ELK_RightMode,/* 3bit unsigned raw*1 [0..7] */
/* 0x1A2 FCS_AEB (RX)  dlc=8 */
    SIG_CAN_FCS_AEB_DecCtrlACT,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_FCS_AEB_WarningType,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_FCS_AEB_Switch,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_FCS_AEB_Status,/* 3bit unsigned raw*1 [0..7] */
    SIG_CAN_FCS_AEB_Warning,/* 3bit unsigned raw*1 [0..7] */
    SIG_CAN_FCS_AEB_FCWSenlevel,/* 2bit unsigned raw*1 [0..3] */
/* 0x1A3 FCS_Display (RX)  dlc=8 */
    SIG_CAN_FCS_ALOD_SpeedSetDisp,/* 8bit unsigned raw*1 [0..255] */
    SIG_CAN_FCS_ALOD_TimeGap,/* 3bit unsigned raw*1 [0..7] */
    SIG_CAN_FCS_ALOD_TimeGapDisp,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_FCS_ALOD_Status,/* 4bit unsigned raw*1 [0..15] */
    SIG_CAN_FCS_ALOD_ControlType,/* 3bit unsigned raw*1 [0..7] */
    SIG_CAN_FCS_ALOD_Warning,/* 3bit unsigned raw*1 [0..7] */
    SIG_CAN_FCS_FRS_Status,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_FCS_ALOD_Info,/* 5bit unsigned raw*1 [0..31] */
    SIG_CAN_FCS_Display_AliveCounter,/* 4bit unsigned raw*1 [0..15] */
    SIG_CAN_FCS_Display_CheckSum,/* 8bit unsigned raw*1 [0..255] */
/* 0x1A6 FCS_FrontObject (RX)  dlc=8 */
    SIG_CAN_FCS_ObjFront1_Dx,/* 8bit unsigned raw*1 [0..255] */
    SIG_CAN_FCS_ObjFront1_Dy,/* 8bit unsigned raw*0.1-12.7 [-12.7..12.8] */
    SIG_CAN_FCS_ObjFront1_Type,/* 3bit unsigned raw*1 [0..7] */
    SIG_CAN_FCS_ObjFront1_Color,/* 3bit unsigned raw*1 [0..7] */
    SIG_CAN_FCS_ObjFront1_Warning,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_FCS_ObjFront2_Dx,/* 8bit unsigned raw*1 [0..255] */
    SIG_CAN_FCS_ObjFront2_Dy,/* 8bit unsigned raw*0.1-12.7 [-12.7..12.8] */
    SIG_CAN_FCS_ObjFront2_Type,/* 3bit unsigned raw*1 [0..7] */
    SIG_CAN_FCS_ObjFront2_Color,/* 3bit unsigned raw*1 [0..7] */
    SIG_CAN_FCS_ObjFront1_Heading,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_FCS_ObjFront2_Heading,/* 2bit unsigned raw*1 [0..3] */
/* 0x1A7 FCS_FrontSideObject (RX)  dlc=8 */
    SIG_CAN_FCS_ObjFrontLeft_Dx,/* 8bit unsigned raw*1 [0..255] */
    SIG_CAN_FCS_ObjFrontLeft_Dy,/* 8bit unsigned raw*0.1-12.7 [-12.7..12.8] */
    SIG_CAN_FCS_ObjFrontLeft_Type,/* 3bit unsigned raw*1 [0..7] */
    SIG_CAN_FCS_ObjFrontLeft_Color,/* 3bit unsigned raw*1 [0..7] */
    SIG_CAN_FCS_ObjFrontRight_Dx,/* 8bit unsigned raw*1 [0..255] */
    SIG_CAN_FCS_ObjFrontRight_Dy,/* 8bit unsigned raw*0.1-12.7 [-12.7..12.8] */
    SIG_CAN_FCS_ObjFrontRight_Type,/* 3bit unsigned raw*1 [0..7] */
    SIG_CAN_FCS_ObjFrontRight_Color,/* 3bit unsigned raw*1 [0..7] */
    SIG_CAN_FCS_ObjFrontLeft_Heading,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_FCS_ObjFrontRight_Heading,/* 2bit unsigned raw*1 [0..3] */
/* 0x2F1 AC_ReqSts (RX)  dlc=8 */
    SIG_CAN_AC_ACCompReq,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_AC_AmbientTemperatureInvalid,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_AC_InsideTemperatureInvalid,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_AC_AmbientTemperature,/* 8bit unsigned raw*0.5-40 [-40..87.5] */
    SIG_CAN_AC_PM25InDen,/* 12bit unsigned raw*1 [0..4095] */
    SIG_CAN_AC_PM25OutDen,/* 12bit unsigned raw*1 [0..4095] */
    SIG_CAN_AC_AirInQLevel,/* 3bit unsigned raw*1 [0..7] */
    SIG_CAN_AC_AirOutQLevel,/* 3bit unsigned raw*1 [0..7] */
    SIG_CAN_AC_PM25Sts,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_AC_InsideTemperature,/* 8bit unsigned raw*0.5-40 [-40..87.5] */
    SIG_CAN_AC_ParkingClimateStatus,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_AC_ParkingClimateFailStatus,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_AC_PM25PopupReq,/* 2bit unsigned raw*1 [0..3] */
/* 0x1F2 TPMS_TyreDataInfo (RX)  dlc=8 */
    SIG_CAN_TPMS_FLTyrePr,/* 8bit unsigned raw*3.137 [0..799.935] */
    SIG_CAN_TPMS_FRTyrePr,/* 8bit unsigned raw*3.137 [0..799.935] */
    SIG_CAN_TPMS_RLTyrePr,/* 8bit unsigned raw*3.137 [0..799.935] */
    SIG_CAN_TPMS_RRTyrePr,/* 8bit unsigned raw*3.137 [0..799.935] */
    SIG_CAN_TPMS_FLTyreTemp,/* 8bit unsigned raw*1-50 [-50..205] */
    SIG_CAN_TPMS_FRTyreTemp,/* 8bit unsigned raw*1-50 [-50..205] */
    SIG_CAN_TPMS_RLTyreTemp,/* 8bit unsigned raw*1-50 [-50..205] */
    SIG_CAN_TPMS_RRTyreTemp,/* 8bit unsigned raw*1-50 [-50..205] */
/* 0x370 TPMS_TempStatusInfo (RX)  dlc=8 */
    SIG_CAN_TPMS_FL_Learning_Sts,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_TPMS_FR_Learning_Sts,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_TPMS_RL_Learning_Sts,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_TPMS_RR_Learning_Sts,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_TPMS_Mode,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_TPMS_W_SensorVoltageFL,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_TPMS_W_SensorVoltageFR,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_TPMS_W_SensorVoltageRL,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_TPMS_W_SensorVoltageRR,/* 1bit unsigned raw*1 [0..1] */
/* 0x1F0 BCM_LightChimeReq (RX)  dlc=8 */
    SIG_CAN_BCM_TurnIndicatorLeft,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_BCM_AntiPinchWarnSetResp,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_BCM_TurnIndicatorRight,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_BCM_TurnLeverSts,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_BCM_LowBeamSts,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_BCM_HighBeamSts,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_BCM_PositionLightSts,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_BCM_DayRunningLightSts,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_BCM_FollowMeHomeActive,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_BCM_FrontFogLightSts,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_BCM_RearFogLightSts,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_BCM_LightLeftOn,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_BCM_Warning_RKE_LOW_BATT,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_BCM_BrakeLampsFailure,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_BCM_PositionLampsFailure,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_BCM_ReverseLampsFailure,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_BCM_RrFogLampsFailure,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_BCM_DI_LampsFailure,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_BCM_LowBeamLampsFailure,/* 1bit unsigned raw*1 [0..1] */
/* 0x285 BCM_LDoorWindowState (RX)  dlc=8 */
    SIG_CAN_BCM_Drv_Wdw_valid,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_BCM_Drv_Val_Wdw_Opened,/* 7bit unsigned raw*1 [0..127] */
    SIG_CAN_BCM_Drv_Wdw_OD_Sts,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_BCM_Drv_Wdw_Obs_InhibitSts,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_BCM_Drv_Wdw_Running_Sts,/* 3bit unsigned raw*1 [0..7] */
    SIG_CAN_BCM_Drv_Wdw_PositionSts,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_BCM_Drv_Wdw_Error,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_BCM_RLD_Wdw_valid,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_BCM_RLD_Val_Wdw_Opened,/* 7bit unsigned raw*1 [0..127] */
    SIG_CAN_BCM_RLD_Wdw_OD_Sts,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_BCM_RLD_Wdw_Obs_InhibitSts,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_BCM_RLD_Wdw_Running_Sts,/* 3bit unsigned raw*1 [0..7] */
    SIG_CAN_BCM_RLD_Wdw_PositionSts,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_BCM_RLD_Wdw_Error,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_BCM_FrontLeftDoorAjarStatus,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_BCM_RearLeftDoorAjarStatus,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_BCM_MechaKey_LockAction,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_BCM_WdwNotCloseWarning,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_BCM_AutoLockFailWarning,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_BCM_DoorChildLockst,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_BCM_RearMirrorFoldSetResp,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_BCM_DoorLockStatusRL,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_BCM_DoorLockStatusDrv,/* 2bit unsigned raw*1 [0..3] */
/* 0x286 BCM_RDoorWindowState (RX)  dlc=8 */
    SIG_CAN_BCM_Pas_Wdw_valid,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_BCM_Pas_Val_Wdw_Opened,/* 7bit unsigned raw*1 [0..127] */
    SIG_CAN_BCM_Pas_Wdw_OD_Sts,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_BCM_Pas_Wdw_Obs_InhibitSts,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_BCM_Pas_Wdw_Running_Sts,/* 3bit unsigned raw*1 [0..7] */
    SIG_CAN_BCM_Pas_Wdw_PositionSts,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_BCM_Pas_Wdw_Error,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_BCM_RRD_Wdw_valid,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_BCM_RRD_Val_Wdw_Opened,/* 7bit unsigned raw*1 [0..127] */
    SIG_CAN_BCM_RRD_Wdw_OD_Sts,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_BCM_RRD_Wdw_Obs_InhibitSts,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_BCM_RRD_Wdw_Running_Sts,/* 3bit unsigned raw*1 [0..7] */
    SIG_CAN_BCM_RRD_Wdw_PositionSts,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_BCM_RRD_Wdw_Error,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_BCM_FrontRightDoorAjarStatus,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_BCM_RearRightDoorAjarStatus,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_BCM_CargoBoxLightSts,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_BCM_FueltankCapSts,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_BCM_DoorLockStatusRR,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_BCM_DoorLockStatusPass,/* 2bit unsigned raw*1 [0..3] */
/* 0x284 BCM_StateUpdate (RX)  dlc=8 */
    SIG_CAN_BCM_RearDefrosterSts,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_BCM_Central_Lock_CMD,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_BCM_Central_unLock_CMD,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_BCM_HoodAjarStatus,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_BCM_TrunkAjarStatus,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_BCM_RainshedStatus,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_BCM_FollowMeHomeTimeSelectResp,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_BCM_RearLightHardSwtSts,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_BCM_AutoCloseWindowSetResp,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_BCM_RKEUnlockSetResp,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_BCM_KeyInwithDrvDoorAjar,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_BCM_ATWS_St,/* 3bit unsigned raw*1 [0..7] */
    SIG_CAN_BCM_RLSModeSts,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_BCM_IndicationPressClutch,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_BCM_DM_ReqType,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_BCM_ATWarnTypeSetResp,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_BCM_DayRunLightSetResp,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_BCM_RearWiperAutoActiveAtReverse,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_BCM_ReverseGearInfo,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_BCM_PowerMode,/* 3bit unsigned raw*1 [0..7] */
    SIG_CAN_BCM_PowertrainChainStatus,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_BCM_Warning_IMMO_Fail,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_BCM_RLS_WinCloseReminder,/* 3bit unsigned raw*1 [0..7] */
    SIG_CAN_BCM_360LtgExecuteStsFB,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_BCM_360LtgExecuteZoneFB,/* 4bit unsigned raw*1 [0..15] */
    SIG_CAN_BCM_RKE_RemoteACCtrl,/* 2bit unsigned raw*1 [0..3] */
/* 0x287 BCM_SunroofState (RX)  dlc=8 */
    SIG_CAN_BCM_Odometerbackup,/* 14bit unsigned raw*100 [0..1638300] */
    SIG_CAN_BCM_BottomClutchSwitchInvalid,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_L_Sunroof_Operation_State,/* 4bit unsigned raw*1 [0..15] */
    SIG_CAN_BCM_sunroof_valid,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_L_sunroof_Val_Opened,/* 7bit unsigned raw*1 [0..127] */
    SIG_CAN_L_sunroof_ap_event,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_BCM_sunroof_Error,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_L_Sunroof_Position,/* 3bit unsigned raw*1 [0..7] */
/* 0x27F PEPS_KeyReminder (RX)  dlc=8 */
    SIG_CAN_PEPS_RKECommand,/* 4bit unsigned raw*1 [0..15] */
    SIG_CAN_PEPS_Warning_Stop_Emergency,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_PEPS_Warning_Stop_Moving,/* 1bit unsigned raw*1 [0..1] */
/* 0x2FC GW_PEPS_Information (RX)  dlc=8 */
    SIG_CAN_PEPS_PowerModeValidity,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_PEPS_PowerMode,/* 3bit unsigned raw*1 [0..7] */
    SIG_CAN_PEPS_EngineforbidSt,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_PEPS_EngForbidWarn,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_PEPS_RemoteControlSt,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_PEPS_FailReason2TBOX,/* 8bit unsigned raw*1 [0..255] */
    SIG_CAN_PEPS_StatusResponse2TBOX,/* 3bit unsigned raw*1 [0..7] */
    SIG_CAN_PEPS_Warning_No_key_found,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_PEPS_Indication_press_brake_clut,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_PEPS_Indication_shift_to_PN,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_PEPS_TrunkUnlock_Enable,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_PEPS_Indication_shift_to_Park,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_PEPS_Warning_keyInReminder,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_PEPS_IGN1FailureWarning,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_PEPS_Warning_Auth_ESCL_Fail,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_PEPS_Warning_UID_LOW_BATT,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_PEPS_ChargerConnectStarter_Warnn,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_PEPS_Warning_IMMO_Fail,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_PEPS_TELAuthenStatus,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_PEPS_Indication_UID_Closer,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_PEPS_CrankAllowSts,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_PEPS_Warning_PoweOnCounterRemain,/* 4bit unsigned raw*1 [0..15] */
    SIG_CAN_PEPS_0x1E2_TimeoutFlag,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_PEPS_0x270_TimeoutFlag,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_PEPS_0x272_TimeoutFlag,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_PEPS_SSB_Failure_warning,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_PEPS_WelcomeLightSetResp,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_PEPS_APUCfgResult,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_PEPS_WALCfgResult,/* 1bit unsigned raw*1 [0..1] */
/* 0x2FD GW_BCM_Information (RX)  dlc=8 */
    SIG_CAN_TPMS_FLTyreWarn,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_TPMS_FLTyre_Temperature,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_TPMS_FLTyre_Fast_Leak,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_TPMS_FLTyre_Sensor_Failure,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_TPMS_0x1F1_TimeoutFlag,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_TPMS_FRTyreWarn,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_TPMS_FRTyre_Temperature,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_TPMS_FRTyre_Fast_Leak,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_TPMS_FRTyre_Sensor_Failure,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_BCM_DMSDriveModeReqRej,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_TPMS_RLTyreWarn,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_TPMS_RLTyre_Temperature,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_TPMS_RLTyre_Fast_Leak,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_TPMS_RLTyre_Sensor_Failure,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_TPMS_RRTyreWarn,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_TPMS_RRTyre_Temperature,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_TPMS_RRTyre_Fast_Leak,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_TPMS_RRTyre_Sensor_Failure,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_TPMS_SystemSt,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_BCM_DM_TargetModeReq,/* 3bit unsigned raw*1 [0..7] */
    SIG_CAN_BCM_DM_SwitchModeSts,/* 3bit unsigned raw*1 [0..7] */
    SIG_CAN_BCM_DMSVehicleMode,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_BCM_DM_ChangeModeFailureControll,/* 3bit unsigned raw*1 [0..7] */
    SIG_CAN_BCM_DM_ChangeModeFailureReason,/* 3bit unsigned raw*1 [0..7] */
    SIG_CAN_BCM_0x283_TimeoutFlag,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_BCM_DM_SwitchModeStsDisp,/* 3bit unsigned raw*1 [0..7] */
/* 0x1BB VCU_DriverTqInfo (RX)  dlc=8 */
    SIG_CAN_VCU_SOCpointSetsts,/* 7bit unsigned raw*1 [0..127] */
    SIG_CAN_VCU_IntellTurnAidResp,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_VCU_IntellTurnAidOprNtc,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_VCU_IntellTurnAidOprGuide,/* 3bit unsigned raw*1 [0..7] */
    SIG_CAN_VCU_IntellTurnAidSts,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_VCU_DriverTqInfo_AliveCounter,/* 4bit unsigned raw*1 [0..15] */
    SIG_CAN_VCU_DriverTqInfo_Checksum,/* 8bit unsigned raw*1 [0..255] */
/* 0x0B0 BMSH_General (RX)  dlc=8 */
    SIG_CAN_BMSH_stMode,/* 4bit unsigned raw*1 [0..15] */
    SIG_CAN_BMSH_VehicleHVILSts,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_BMSH_InterHVILSts,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_BMSH_InsulationSts,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_BMSH_MainPrechgSt,/* 3bit unsigned raw*1 [0..7] */
/* 0x178 BMSH_VoltCurr (RX)  dlc=8 */
    SIG_CAN_BMSH_BattCurr,/* 14bit unsigned raw*0.1-500 [-500..1138.3] */
    SIG_CAN_BMSH_HVBusVolt,/* 10bit unsigned raw*1 [0..1023] */
    SIG_CAN_BMSH_BattVolt,/* 13bit unsigned raw*0.1 [0..819.1] */
    SIG_CAN_BMSH_BattFaultLampState,/* 2bit unsigned raw*1 [0..3] */
/* 0x211 BMSH_OBC_Control (RX)  dlc=8 */
    SIG_CAN_BMSH_ChargeLEDCtrl,/* 4bit unsigned raw*1 [0..15] */
    SIG_CAN_BMSH_FastChgCC2ConntState,/* 2bit unsigned raw*1 [0..3] */
/* 0x2F4 BMSH_Info (RX)  dlc=8 */
    SIG_CAN_DCChrgrSt,/* 4bit unsigned raw*1 [0..15] */
    SIG_CAN_BMSH_BattSOCDisp,/* 10bit unsigned raw*0.1 [0..102.3] */
/* 0x29A EcmChas2Fr92 (RX)  dlc=8 */
    SIG_CAN_FuCnsAvgIndcdFuCnsIndcdVal1,/* 9bit unsigned raw*0.1 [0..51.1] */
    SIG_CAN_PwrCnsAvgIndcdPwrCns1,/* 11bit unsigned raw*0.1-102.3 [-102.3..102.4] */
    SIG_CAN_PwrCnsAvgIndcdPwrCns2,/* 11bit unsigned raw*0.1-102.3 [-102.3..102.4] */
    SIG_CAN_FuCnsAvgIndcdFuCnsIndcdVal2,/* 9bit unsigned raw*0.1 [0..51.1] */
    SIG_CAN_FuCnsAvgIndcdFuCnsIndcdVal3,/* 9bit unsigned raw*0.1 [0..51.1] */
    SIG_CAN_PwrCnsAvgIndcdPwrCns3,/* 11bit unsigned raw*0.1-102.3 [-102.3..102.4] */
    SIG_CAN_EgyAvgCnsDstSg,/* 1bit unsigned raw*1 [0..1] */
/* 0x29B EcmChas2Fr93 (RX)  dlc=8 */
    SIG_CAN_DstToEmptyIndcdDstToEmpty1,/* 11bit unsigned raw*1 [0..2047] */
    SIG_CAN_AcEgyDistbn,/* 10bit unsigned raw*20 [0..20460] */
    SIG_CAN_DstToEmptyIndcdDstToEmpty2,/* 11bit unsigned raw*1 [0..2047] */
    SIG_CAN_TotDrvPwrAct,/* 11bit unsigned raw*1-1000 [-1000..1047] */
    SIG_CAN_ThmEgyDistbn,/* 10bit unsigned raw*20 [0..20460] */
    SIG_CAN_HvConvPwrAct,/* 10bit unsigned raw*20 [0..20460] */
/* 0x255 EcmChas2Fr33 (RX)  dlc=8 */
    SIG_CAN_IdleChrgFctSts,/* 3bit unsigned raw*1 [0..7] */
    SIG_CAN_EVBlkd,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_REVBlkd,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_DispIdlChrgPwr,/* 10bit unsigned raw*0.5 [0..511.5] */
    SIG_CAN_DstEstimdToEmptyForDrvgElec,/* 10bit unsigned raw*1 [0..1023] */
    SIG_CAN_DstEstimdToEmptyForDrvgElecPredT,/* 10bit unsigned raw*1 [0..1023] */
/* 0x364 PCM_Temperature (RX)  dlc=8 */
    SIG_CAN_PCM_MotOverTemp,/* 1bit unsigned raw*1 [0..1] */
/* 0x365 PCM_Warning (RX)  dlc=8 */
    SIG_CAN_PCM_IsgDeratSts,/* 8bit unsigned raw*1 [0..255] */
    SIG_CAN_PCM_MotTAlm,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_PCM_IsgTAlm,/* 1bit unsigned raw*1 [0..1] */
/* 0x041 PcmChas1Fr19 (RX)  dlc=8 */
    SIG_CAN_TrsmFltIndcn,/* 5bit unsigned raw*1 [0..31] */
/* 0x360 IPU_Temperature (RX)  dlc=8 */
    SIG_CAN_IPU_IsgOverTemp,/* 1bit unsigned raw*1 [0..1] */
/* 0x361 IPU_Warning (RX)  dlc=8 */
    SIG_CAN_IPU_MotTAlm,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_IPU_IPUTAlm,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_IPU_IsgFAlm,/* 1bit unsigned raw*1 [0..1] */
/* 0x2EA TRM_StatusInfo (RX)  dlc=8 */
    SIG_CAN_TRM_TurnLeftLampsFailure,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_TRM_TurnRightLampsFailure,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_TRM_EleIF_Connect_Status,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_TRM_EleIF_Connect_Failure,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_TRM_Message_AliveCounter,/* 4bit unsigned raw*1 [0..15] */
    SIG_CAN_TRM_Message_CheckSum,/* 8bit unsigned raw*1 [0..255] */
/* 0x3E9 IPK_EngineService (TX)  dlc=8 */
    SIG_CAN_IPK_IPKEngineTotalOdometer,/* 20bit unsigned raw*1 [0..1048575] */
    SIG_CAN_IPK_DayToEngSrv,/* 9bit unsigned raw*1 [0..511] */
    SIG_CAN_IPK_ServiceEngineMaintainInterva,/* 16bit unsigned raw*1 [0..65535] */
/* 0x26D IPK_STS (TX)  dlc=8 */
    SIG_CAN_IPK_AirbagUnitLEDSts,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_IPK_SVA_AudibleWarningCfgResult,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_IPK_FuelLowLevelWarning,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_IPK_ESCoffInfo,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_IPK_Fail,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_IPK_QDashALODFail,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_IPK_FuelLevelSts,/* 8bit unsigned raw*0.5 [0..127.5] */
    SIG_CAN_IPK_AverageVehicleSpeed,/* 8bit unsigned raw*1 [0..255] */
    SIG_CAN_IPK_HandBrakeSts,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_IPK_MaintanceWarningSts,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_IPK_LanguageMode,/* 4bit unsigned raw*1 [0..15] */
    SIG_CAN_IPK_EPS_ModSetSelection,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_IPK_EPS_DMCorrelativeMode,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_IPK_Backlightadjust,/* 5bit unsigned raw*1 [0..31] */
    SIG_CAN_IPK_vDisplay,/* 13bit unsigned raw*0.05625 [0..460.744] */
    SIG_CAN_IPK_OilLowPressure,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_IPK_LIMmemoryEnabe_Reserved,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_IPK_BattLowVoltageWarning,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_IPK_driving_mode_light_sts,/* 2bit unsigned raw*1 [0..3] */
/* 0x260 IPK_SettingRequest (TX)  dlc=8 */
    SIG_CAN_IPK_AEB_FCWStateReq,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_IPK_AEB_AEBStateReq,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_IPK_ALOD_ControlTypeReq,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_IPK_LCA_EnableStatus,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_IPK_RCTA_EnableStatus,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_IPK_RCW_EnableStatus,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_IPK_AEB_FCWSenlevel,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_IPK_LKS_LaneAssistTypeReq,/* 3bit unsigned raw*1 [0..7] */
    SIG_CAN_IPK_LDW_WarningTypeSetting,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_IPK_IHBC_MenuReq,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_IPK_SLIF_MenuReq,/* 2bit unsigned raw*1 [0..3] */
/* 0x2D8 IPK_Fuel_Sts (TX)  dlc=8 */
    SIG_CAN_FuFillgDetnForUseInt,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_RstTrip1,/* 4bit unsigned raw*1 [0..15] */
    SIG_CAN_RstTrip2,/* 3bit unsigned raw*1 [0..7] */
/* 0x3F1 IPK_TotalOdometer (TX)  dlc=8 */
    SIG_CAN_IPK_IPKTotalOdometer,/* 24bit unsigned raw*0.1 [0..1.67772e+06] */
    SIG_CAN_IPK_DTEodometer,/* 12bit unsigned raw*1 [0..4095] */
    SIG_CAN_IPK_OdometerbackupEnable,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_IPK_ServiceMaintainInterval,/* 16bit unsigned raw*1 [0..65535] */
/* 0x3F0 IPK_DateTime_Info (TX)  dlc=8 */
    SIG_CAN_IPK_Second,/* 6bit unsigned raw*1 [0..63] */
    SIG_CAN_IPK_Minute,/* 6bit unsigned raw*1 [0..63] */
    SIG_CAN_IPK_Hour,/* 5bit unsigned raw*1 [0..31] */
    SIG_CAN_IPK_TimeDisplayMode,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_IPK_Day,/* 5bit unsigned raw*1 [0..31] */
    SIG_CAN_IPK_Month,/* 4bit unsigned raw*1 [0..15] */
    SIG_CAN_IPK_Year,/* 6bit unsigned raw*1 [0..63] */
    SIG_CAN_IPK_VehicleStopTime,/* 12bit unsigned raw*1 [0..4095] */
/* 0x3F6 IPK_Fuel_Info (TX)  dlc=8 */
    SIG_CAN_IPK_FuelSensorVoltage,/* 15bit unsigned raw*0.000156 [0..5.11165] */
    SIG_CAN_IPK_AverageFuelConsumptionOneCyc,/* 9bit unsigned raw*0.1 [0..51.1] */
    SIG_CAN_IPK_FuelSensorShortOrOpenBatt,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_IPK_FuelSensorShortGND,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_IPK_FuelSensorUpperLimit,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_IPK_DayToSrv,/* 9bit unsigned raw*1 [0..511] */
    SIG_CAN_IPK_InstanteFuelConsumption,/* 9bit unsigned raw*0.1 [0..51.1] */
    SIG_CAN_IPK_Fuel_Info_AliveCounter,/* 4bit unsigned raw*1 [0..15] */
    SIG_CAN_IPK_Fuel_Info_Checksum,/* 8bit unsigned raw*1 [0..255] */
/* 0x3F7 IPK_ODO_Consump (TX)  dlc=8 */
    SIG_CAN_IPK_EVDTEodometer,/* 12bit unsigned raw*1 [0..4095] */
    SIG_CAN_IPK_AveragePowerConsumption,/* 14bit unsigned raw*0.01 [0..163.83] */
    SIG_CAN_IPK_InstantPowerConsumption,/* 14bit unsigned raw*0.01 [0..163.83] */
    SIG_CAN_IPK_AverageFuelConsumptionUnit,/* 2bit unsigned raw*1 [0..3] */
    SIG_CAN_IPK_AverageFuelConsumption,/* 16bit unsigned raw*0.01 [0..655.35] */
/* 0x402 NWM_IPK (TX)  dlc=8 */
    SIG_CAN_IPK_Address,/* 8bit unsigned raw*1 [0..255] */
    SIG_CAN_IPK_RMR,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_IPK_AWB,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_IPK_Wakeup_reasons,/* 8bit unsigned raw*1 [0..255] */
    SIG_CAN_IPK_NMSts,/* 1bit unsigned raw*1 [0..1] */
    SIG_CAN_IPK_Stayawake_reasons,/* 32bit unsigned raw*1 [0..4294967295] */
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
