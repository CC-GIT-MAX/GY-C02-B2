/**
 * @file    can_db.c
 * @brief   DBC-driven CAN dispatch for the IPK test batch
 * @brief   IPK 测试批次的 DBC 驱动 CAN 分发
 *
 * Replaces the legacy hand-written sample frames.  Each IPK
 * message/signal descriptor is generated from the DBC by
 * `tools/dbc_parse.py` into `can_db_ipk_gen.c`; this file exposes
 * the runtime helpers that turn incoming payloads into int32 bus
 * values (RX) and look up descriptors by id (RX dispatch + TX build).
 */
#include "can_db.h"
#include "signal.h"

#define LOG_NAME  "CDB "
#include "log.h"

#include "can_db_ipk_gen.h"

/* ---------------------------------------------------------------- *
 *  Reverse maps for fast lookup                                     *
 * ---------------------------------------------------------------- */

/**
 * @brief   Map a CAN_DB_SIG_* id to its descriptor index in
 *          can_sig_descs_ipk[].
 * @brief   把 CAN_DB_SIG_* 映射到 can_sig_descs_ipk[] 描述符索引
 *
 * The signal enum in can_db_ipk_gen.h is contiguous starting at 1,
 * so the descriptor index equals (sig_id - 1).
 */
#define SIG_ID_TO_INDEX(sig_id)  ((u16)((sig_id) - 1u))

/* ---------------------------------------------------------------- *
 *  Explicit DBC enum -> signal bus id mapping table                 *
 *                                                                    *
 *  The DBC enum (CAN_DB_SIG_*) and the signal.h enum (SIG_CAN_*)    *
 *  do not share a guaranteed ordering -- signal.h carries pre-       *
 *  existing legacy ids (SIG_CAN_RX_TIMEOUT_*) that the DBC does     *
 *  not know about.  This table is built by reading both enums at    *
 *  generator time (see gen_can_db_map.py in the next batch) and     *
 *  gives O(1) lookup at runtime.                                     *
 * ---------------------------------------------------------------- */

static const signal_id_t s_dbc_to_bus[CAN_DB_IPK_SIG_COUNT] = {
    [0] = SIG_INVALID, /* CAN_DB_SIG_INVALID */
    /* --- RX: MMI_DateTime_Msg (0x02AF, dlc=8) --- */
    [CAN_DB_SIG_MMI_Second - 1u] = SIG_CAN_MMI_Second,
    [CAN_DB_SIG_MMI_Minute - 1u] = SIG_CAN_MMI_Minute,
    [CAN_DB_SIG_MMI_Hour - 1u] = SIG_CAN_MMI_Hour,
    [CAN_DB_SIG_MMI_Day - 1u] = SIG_CAN_MMI_Day,
    [CAN_DB_SIG_MMI_Month - 1u] = SIG_CAN_MMI_Month,
    [CAN_DB_SIG_MMI_Year - 1u] = SIG_CAN_MMI_Year,
    /* --- RX: MMI_GPS_Info5 (0x03E0, dlc=8) --- */
    [CAN_DB_SIG_GPS_elevation_Info - 1u] = SIG_CAN_GPS_elevation_Info,
    [CAN_DB_SIG_MMI_GPS_Info5_AliveCounter - 1u] = SIG_CAN_MMI_GPS_Info5_AliveCounter,
    [CAN_DB_SIG_MMI_GPS_Info5_CheckSum - 1u] = SIG_CAN_MMI_GPS_Info5_CheckSum,
    /* --- RX: MMI_Status_Info (0x02A4, dlc=8) --- */
    [CAN_DB_SIG_MMI_RightSwStatus - 1u] = SIG_CAN_MMI_RightSwStatus,
    [CAN_DB_SIG_MMI_LeftSwStatus - 1u] = SIG_CAN_MMI_LeftSwStatus,
    [CAN_DB_SIG_MMI_UpSwStatus - 1u] = SIG_CAN_MMI_UpSwStatus,
    [CAN_DB_SIG_MMI_DownSwStatus - 1u] = SIG_CAN_MMI_DownSwStatus,
    [CAN_DB_SIG_MMI_ConfirmSwStatus - 1u] = SIG_CAN_MMI_ConfirmSwStatus,
    [CAN_DB_SIG_MMI_ReturnSwStatus - 1u] = SIG_CAN_MMI_ReturnSwStatus,
    [CAN_DB_SIG_MMI_WorkMode - 1u] = SIG_CAN_MMI_WorkMode,
    [CAN_DB_SIG_MMI_SkinMode - 1u] = SIG_CAN_MMI_SkinMode,
    [CAN_DB_SIG_MMI_HUDAciveRequest - 1u] = SIG_CAN_MMI_HUDAciveRequest,
    [CAN_DB_SIG_MMI_AVH_Request - 1u] = SIG_CAN_MMI_AVH_Request,
    [CAN_DB_SIG_MMI_OdometerClearReq - 1u] = SIG_CAN_MMI_OdometerClearReq,
    [CAN_DB_SIG_MMI_SkinModeCorrelativeSts - 1u] = SIG_CAN_MMI_SkinModeCorrelativeSts,
    [CAN_DB_SIG_MMI_SVA_AudibleWarningOption - 1u] = SIG_CAN_MMI_SVA_AudibleWarningOption,
    [CAN_DB_SIG_MMI_LCA_AudioWarning - 1u] = SIG_CAN_MMI_LCA_AudioWarning,
    [CAN_DB_SIG_MMI_ePTRegencyLevRequest - 1u] = SIG_CAN_MMI_ePTRegencyLevRequest,
    [CAN_DB_SIG_MMI_DM_SwitchModeSts - 1u] = SIG_CAN_MMI_DM_SwitchModeSts,
    [CAN_DB_SIG_MMI_RoadCameraWarning - 1u] = SIG_CAN_MMI_RoadCameraWarning,
    [CAN_DB_SIG_MMI_BCM_DchaChargehint - 1u] = SIG_CAN_MMI_BCM_DchaChargehint,
    [CAN_DB_SIG_MMI_RDiffLockReq - 1u] = SIG_CAN_MMI_RDiffLockReq,
    [CAN_DB_SIG_MMI_NationalDrivingRank - 1u] = SIG_CAN_MMI_NationalDrivingRank,
    [CAN_DB_SIG_MMI_EnergySavingModeReq - 1u] = SIG_CAN_MMI_EnergySavingModeReq,
    [CAN_DB_SIG_MMI_CampModeReq - 1u] = SIG_CAN_MMI_CampModeReq,
    [CAN_DB_SIG_MMI_EpedalSettingInfo - 1u] = SIG_CAN_MMI_EpedalSettingInfo,
    [CAN_DB_SIG_MMI_ESCoffInfo - 1u] = SIG_CAN_MMI_ESCoffInfo,
    [CAN_DB_SIG_MMI_HDC_Request - 1u] = SIG_CAN_MMI_HDC_Request,
    [CAN_DB_SIG_MMI_IntellTurnAidReq - 1u] = SIG_CAN_MMI_IntellTurnAidReq,
    [CAN_DB_SIG_MMI_IntellTurnAidTerrSeln - 1u] = SIG_CAN_MMI_IntellTurnAidTerrSeln,
    [CAN_DB_SIG_MMI_OnboardScaleReq - 1u] = SIG_CAN_MMI_OnboardScaleReq,
    [CAN_DB_SIG_MMI_iTPMS_reset_Request - 1u] = SIG_CAN_MMI_iTPMS_reset_Request,
    /* --- RX: MMI_Safety_Info (0x02A8, dlc=8) --- */
    [CAN_DB_SIG_MMI_Nav_SpeedLimit - 1u] = SIG_CAN_MMI_Nav_SpeedLimit,
    [CAN_DB_SIG_MMI_Nav_RampSts - 1u] = SIG_CAN_MMI_Nav_RampSts,
    [CAN_DB_SIG_MMI_Nav_CurrRoadType - 1u] = SIG_CAN_MMI_Nav_CurrRoadType,
    [CAN_DB_SIG_MMI_Nav_status - 1u] = SIG_CAN_MMI_Nav_status,
    [CAN_DB_SIG_MMI_Nav_CountryID - 1u] = SIG_CAN_MMI_Nav_CountryID,
    [CAN_DB_SIG_MMI_TollGateIndication - 1u] = SIG_CAN_MMI_TollGateIndication,
    [CAN_DB_SIG_MMI_SVC_status - 1u] = SIG_CAN_MMI_SVC_status,
    [CAN_DB_SIG_MMI_APA_Function_Select - 1u] = SIG_CAN_MMI_APA_Function_Select,
    [CAN_DB_SIG_MMI_SVA_Request - 1u] = SIG_CAN_MMI_SVA_Request,
    [CAN_DB_SIG_MMI_APA_ConfirmButton - 1u] = SIG_CAN_MMI_APA_ConfirmButton,
    [CAN_DB_SIG_MMI_PSC_Function_Select - 1u] = SIG_CAN_MMI_PSC_Function_Select,
    [CAN_DB_SIG_MMI_NavTunnelIndication - 1u] = SIG_CAN_MMI_NavTunnelIndication,
    [CAN_DB_SIG_MMI_NavDestinationIndication - 1u] = SIG_CAN_MMI_NavDestinationIndication,
    [CAN_DB_SIG_MMI_DayNightMode - 1u] = SIG_CAN_MMI_DayNightMode,
    [CAN_DB_SIG_MMI_IBBrakeModeSet - 1u] = SIG_CAN_MMI_IBBrakeModeSet,
    [CAN_DB_SIG_MMI_SailingSwitch - 1u] = SIG_CAN_MMI_SailingSwitch,
    [CAN_DB_SIG_MMI_Start_Stop_switch - 1u] = SIG_CAN_MMI_Start_Stop_switch,
    [CAN_DB_SIG_MMI_NavSyncDisplay - 1u] = SIG_CAN_MMI_NavSyncDisplay,
    [CAN_DB_SIG_MMI_Safety_Info_AliveCounter - 1u] = SIG_CAN_MMI_Safety_Info_AliveCounter,
    [CAN_DB_SIG_MMI_Safety_Info_CheckSum - 1u] = SIG_CAN_MMI_Safety_Info_CheckSum,
    /* --- RX: MMI_SOCSet (0x02A6, dlc=8) --- */
    [CAN_DB_SIG_MMI_DchaChargeControlCmd - 1u] = SIG_CAN_MMI_DchaChargeControlCmd,
    [CAN_DB_SIG_MMI_SocWarValue - 1u] = SIG_CAN_MMI_SocWarValue,
    [CAN_DB_SIG_MMI_EVDTEodometer_ModeSet - 1u] = SIG_CAN_MMI_EVDTEodometer_ModeSet,
    [CAN_DB_SIG_MMI_FridgeCoolReq - 1u] = SIG_CAN_MMI_FridgeCoolReq,
    [CAN_DB_SIG_MMI_SaveModeReq - 1u] = SIG_CAN_MMI_SaveModeReq,
    [CAN_DB_SIG_MMI_ACDisChrgCmd - 1u] = SIG_CAN_MMI_ACDisChrgCmd,
    [CAN_DB_SIG_MMI_RearPanelDisChrgCmd - 1u] = SIG_CAN_MMI_RearPanelDisChrgCmd,
    [CAN_DB_SIG_MMI_DCDisChrgCmd - 1u] = SIG_CAN_MMI_DCDisChrgCmd,
    [CAN_DB_SIG_MMI_FrontPanelDisChrgCmd - 1u] = SIG_CAN_MMI_FrontPanelDisChrgCmd,
    [CAN_DB_SIG_MMI_OTA_Status - 1u] = SIG_CAN_MMI_OTA_Status,
    [CAN_DB_SIG_MMI_MidPanelDisChrgCmd - 1u] = SIG_CAN_MMI_MidPanelDisChrgCmd,
    [CAN_DB_SIG_MMI_RefuUnlckReq - 1u] = SIG_CAN_MMI_RefuUnlckReq,
    [CAN_DB_SIG_MMI_SOCPointSet - 1u] = SIG_CAN_MMI_SOCPointSet,
    /* --- RX: EMS_EngRelateTrqSts (0x0084, dlc=8) --- */
    [CAN_DB_SIG_EMS_ControlSetSpeed - 1u] = SIG_CAN_EMS_ControlSetSpeed,
    [CAN_DB_SIG_EMS_CruiseSwitchSts - 1u] = SIG_CAN_EMS_CruiseSwitchSts,
    [CAN_DB_SIG_EMS_CruiseControlSts - 1u] = SIG_CAN_EMS_CruiseControlSts,
    [CAN_DB_SIG_EMS_Real_PedalPositionInvalid - 1u] = SIG_CAN_EMS_Real_PedalPositionInvalid,
    [CAN_DB_SIG_EMS_Real_PedalPosition - 1u] = SIG_CAN_EMS_Real_PedalPosition,
    /* --- RX: EMS_EngineRPM (0x0085, dlc=8) --- */
    [CAN_DB_SIG_EMS_FuelPulsesRollingCounter - 1u] = SIG_CAN_EMS_FuelPulsesRollingCounter,
    [CAN_DB_SIG_EMS_EngineSpeedRPM - 1u] = SIG_CAN_EMS_EngineSpeedRPM,
    [CAN_DB_SIG_EMS_EngineSpeedRPMInvalid - 1u] = SIG_CAN_EMS_EngineSpeedRPMInvalid,
    [CAN_DB_SIG_EMS_EngStatus - 1u] = SIG_CAN_EMS_EngStatus,
    [CAN_DB_SIG_EMS_AccelPedalPosition - 1u] = SIG_CAN_EMS_AccelPedalPosition,
    [CAN_DB_SIG_EMS_AccelPedalPositionInvalid - 1u] = SIG_CAN_EMS_AccelPedalPositionInvalid,
    /* --- RX: EMS_EngineDriverInfo (0x0288, dlc=8) --- */
    [CAN_DB_SIG_EMS_EngineCoolantTemperature - 1u] = SIG_CAN_EMS_EngineCoolantTemperature,
    [CAN_DB_SIG_EMS_EngineCoolantTemperatureInva - 1u] = SIG_CAN_EMS_EngineCoolantTemperatureInva,
    [CAN_DB_SIG_EMS_EngineSVSTelltale - 1u] = SIG_CAN_EMS_EngineSVSTelltale,
    [CAN_DB_SIG_EMS_EngineMILTelltale - 1u] = SIG_CAN_EMS_EngineMILTelltale,
    [CAN_DB_SIG_EMS_OilPressureWarning - 1u] = SIG_CAN_EMS_OilPressureWarning,
    [CAN_DB_SIG_EMS_Odometerbackup - 1u] = SIG_CAN_EMS_Odometerbackup,
    /* --- RX: EMS_EnginePatsBatteryStat (0x028A, dlc=8) --- */
    [CAN_DB_SIG_EMS_LIMSetSpeed - 1u] = SIG_CAN_EMS_LIMSetSpeed,
    [CAN_DB_SIG_EMS_EAV_ModSetStatus - 1u] = SIG_CAN_EMS_EAV_ModSetStatus,
    [CAN_DB_SIG_EMS_LIMControlSts - 1u] = SIG_CAN_EMS_LIMControlSts,
    [CAN_DB_SIG_EMS_LIMmemorySts_Reserved - 1u] = SIG_CAN_EMS_LIMmemorySts_Reserved,
    [CAN_DB_SIG_EMS_LIMSwitchSts - 1u] = SIG_CAN_EMS_LIMSwitchSts,
    [CAN_DB_SIG_EMS_LIMOverSpdWarningSts - 1u] = SIG_CAN_EMS_LIMOverSpdWarningSts,
    /* --- RX: EMS_OBD_Info (0x028C, dlc=8) --- */
    [CAN_DB_SIG_EMS_GPF_Warning - 1u] = SIG_CAN_EMS_GPF_Warning,
    [CAN_DB_SIG_EMS_BrakeOverrideSts - 1u] = SIG_CAN_EMS_BrakeOverrideSts,
    [CAN_DB_SIG_EMS_TankLeakDiagSts - 1u] = SIG_CAN_EMS_TankLeakDiagSts,
    [CAN_DB_SIG_EMS_AdaptiveTargetMode - 1u] = SIG_CAN_EMS_AdaptiveTargetMode,
    [CAN_DB_SIG_EMS_DM_ModeProgBar - 1u] = SIG_CAN_EMS_DM_ModeProgBar,
    /* --- RX: EGSM_Status (0x0145, dlc=8) --- */
    [CAN_DB_SIG_EGSM_ParkReq - 1u] = SIG_CAN_EGSM_ParkReq,
    [CAN_DB_SIG_EGSM_DriveMod - 1u] = SIG_CAN_EGSM_DriveMod,
    [CAN_DB_SIG_EGSM_LeverPosition - 1u] = SIG_CAN_EGSM_LeverPosition,
    [CAN_DB_SIG_EGSM_Paddleinputs - 1u] = SIG_CAN_EGSM_Paddleinputs,
    [CAN_DB_SIG_EGSM_ReleaseReq - 1u] = SIG_CAN_EGSM_ReleaseReq,
    [CAN_DB_SIG_EGSM_Status_AliveCounter - 1u] = SIG_CAN_EGSM_Status_AliveCounter,
    [CAN_DB_SIG_EGSM_Status_Checksum - 1u] = SIG_CAN_EGSM_Status_Checksum,
    /* --- RX: OBC_Sts (0x0220, dlc=8) --- */
    [CAN_DB_SIG_ODP_OBC_ChrgrSt - 1u] = SIG_CAN_ODP_OBC_ChrgrSt,
    [CAN_DB_SIG_OBC_OnBdFailSt - 1u] = SIG_CAN_OBC_OnBdFailSt,
    [CAN_DB_SIG_OBC_OnBdDisChrgrCCline_PHEV - 1u] = SIG_CAN_OBC_OnBdDisChrgrCCline_PHEV,
    [CAN_DB_SIG_ODP_OBC_OnBdCCline - 1u] = SIG_CAN_ODP_OBC_OnBdCCline,
    [CAN_DB_SIG_ODP_OBC_OnBdCPline - 1u] = SIG_CAN_ODP_OBC_OnBdCPline,
    /* --- RX: OBC_Curr (0x0221, dlc=8) --- */
    [CAN_DB_SIG_ODP_OBC_OnBdDisChrgrCCline_EV - 1u] = SIG_CAN_ODP_OBC_OnBdDisChrgrCCline_EV,
    [CAN_DB_SIG_ODP_OBC_iInAct - 1u] = SIG_CAN_ODP_OBC_iInAct,
    [CAN_DB_SIG_OBC_uInAct - 1u] = SIG_CAN_OBC_uInAct,
    [CAN_DB_SIG_ODP_OBC_POuptAct - 1u] = SIG_CAN_ODP_OBC_POuptAct,
    /* --- RX: OBC_Failmode (0x0222, dlc=8) --- */
    [CAN_DB_SIG_ODP_OBC_Failmode_ACVI_overtemp - 1u] = SIG_CAN_ODP_OBC_Failmode_ACVI_overtemp,
    [CAN_DB_SIG_ODP_OBC_Failmode_Hndl_Error - 1u] = SIG_CAN_ODP_OBC_Failmode_Hndl_Error,
    [CAN_DB_SIG_ODP_OBC_Failmode_ACVI_Invalid - 1u] = SIG_CAN_ODP_OBC_Failmode_ACVI_Invalid,
    /* --- RX: BMSH_sts (0x0354, dlc=8) --- */
    [CAN_DB_SIG_BMSH_BattSOH - 1u] = SIG_CAN_BMSH_BattSOH,
    [CAN_DB_SIG_BMSH_ChgTimeRemain - 1u] = SIG_CAN_BMSH_ChgTimeRemain,
    [CAN_DB_SIG_BMSH_BattRemainEnergy - 1u] = SIG_CAN_BMSH_BattRemainEnergy,
    [CAN_DB_SIG_BMSH_Discharge_Over_Current - 1u] = SIG_CAN_BMSH_Discharge_Over_Current,
    /* --- RX: BMSH_Battery_chgstate (0x017B, dlc=8) --- */
    [CAN_DB_SIG_BMSH_ThermalOutofControl - 1u] = SIG_CAN_BMSH_ThermalOutofControl,
    /* --- RX: BMSH_CellTempLimitValue (0x0210, dlc=8) --- */
    [CAN_DB_SIG_BMSH_BattTempAvg - 1u] = SIG_CAN_BMSH_BattTempAvg,
    [CAN_DB_SIG_BMSH_TempSensor_MinTemp - 1u] = SIG_CAN_BMSH_TempSensor_MinTemp,
    /* --- RX: VCU_Ctrl (0x0162, dlc=8) --- */
    [CAN_DB_SIG_VCU_ePTModeSelect - 1u] = SIG_CAN_VCU_ePTModeSelect,
    [CAN_DB_SIG_VCU_ePTModeActual - 1u] = SIG_CAN_VCU_ePTModeActual,
    [CAN_DB_SIG_VCU_Warning_IMMO_Fail - 1u] = SIG_CAN_VCU_Warning_IMMO_Fail,
    [CAN_DB_SIG_VCU_ChargerFault - 1u] = SIG_CAN_VCU_ChargerFault,
    [CAN_DB_SIG_VCU_ePTReady - 1u] = SIG_CAN_VCU_ePTReady,
    [CAN_DB_SIG_VCU_ePTDivPowerPcent - 1u] = SIG_CAN_VCU_ePTDivPowerPcent,
    [CAN_DB_SIG_VCU_ePTReleaseSig - 1u] = SIG_CAN_VCU_ePTReleaseSig,
    [CAN_DB_SIG_VCU_ePTRegencyLevInd - 1u] = SIG_CAN_VCU_ePTRegencyLevInd,
    [CAN_DB_SIG_VCU_ePTModReqRejt - 1u] = SIG_CAN_VCU_ePTModReqRejt,
    [CAN_DB_SIG_VCU_LimpHomeSts_HB - 1u] = SIG_CAN_VCU_LimpHomeSts_HB,
    [CAN_DB_SIG_VCU_TurtleLampOn - 1u] = SIG_CAN_VCU_TurtleLampOn,
    [CAN_DB_SIG_VCU_ePTFault - 1u] = SIG_CAN_VCU_ePTFault,
    /* --- RX: VCU_InforCAN (0x0214, dlc=8) --- */
    [CAN_DB_SIG_VCU_EVDTEodometer - 1u] = SIG_CAN_VCU_EVDTEodometer,
    [CAN_DB_SIG_HmiEngHybSts - 1u] = SIG_CAN_HmiEngHybSts,
    [CAN_DB_SIG_VCU_PtModAct - 1u] = SIG_CAN_VCU_PtModAct,
    [CAN_DB_SIG_VCU_PtModBlkd - 1u] = SIG_CAN_VCU_PtModBlkd,
    [CAN_DB_SIG_VCU_PwrLoIndcn - 1u] = SIG_CAN_VCU_PwrLoIndcn,
    [CAN_DB_SIG_VCU_EngFltIndcn - 1u] = SIG_CAN_VCU_EngFltIndcn,
    [CAN_DB_SIG_VCU_VehPullOverWarn - 1u] = SIG_CAN_VCU_VehPullOverWarn,
    [CAN_DB_SIG_VCU_EpedalActive - 1u] = SIG_CAN_VCU_EpedalActive,
    [CAN_DB_SIG_VCU_MainDrivingDoorOpenHV - 1u] = SIG_CAN_VCU_MainDrivingDoorOpenHV,
    /* --- RX: VCU_DCDC_Ctrl (0x0161, dlc=8) --- */
    [CAN_DB_SIG_DispWhlMotSysPrpsnMod - 1u] = SIG_CAN_DispWhlMotSysPrpsnMod,
    [CAN_DB_SIG_VCU_DriverRemind - 1u] = SIG_CAN_VCU_DriverRemind,
    [CAN_DB_SIG_VCU_DiscrgPanellSw - 1u] = SIG_CAN_VCU_DiscrgPanellSw,
    [CAN_DB_SIG_VCU_DischgFailSt - 1u] = SIG_CAN_VCU_DischgFailSt,
    [CAN_DB_SIG_VCU_MileageReward - 1u] = SIG_CAN_VCU_MileageReward,
    [CAN_DB_SIG_DispOfPrpsnMod - 1u] = SIG_CAN_DispOfPrpsnMod,
    /* --- RX: VCU_CSControl1 (0x01A5, dlc=8) --- */
    [CAN_DB_SIG_VCU_StartActive - 1u] = SIG_CAN_VCU_StartActive,
    [CAN_DB_SIG_VCU_CruiseControlSts - 1u] = SIG_CAN_VCU_CruiseControlSts,
    [CAN_DB_SIG_VCU_CruiseSwitchSts - 1u] = SIG_CAN_VCU_CruiseSwitchSts,
    [CAN_DB_SIG_VCU_ControlSetSpeed - 1u] = SIG_CAN_VCU_ControlSetSpeed,
    [CAN_DB_SIG_VCU_DM_FailureReasonAndSysFaultF - 1u] = SIG_CAN_VCU_DM_FailureReasonAndSysFaultF,
    /* --- RX: VCU_ModeControl (0x0165, dlc=8) --- */
    [CAN_DB_SIG_VCU_Real_PedalPosition - 1u] = SIG_CAN_VCU_Real_PedalPosition,
    [CAN_DB_SIG_VCU_ModeGearDisplaySts - 1u] = SIG_CAN_VCU_ModeGearDisplaySts,
    [CAN_DB_SIG_VCU_LIMOverSpdWarningSts - 1u] = SIG_CAN_VCU_LIMOverSpdWarningSts,
    [CAN_DB_SIG_VCU_EVSpeedlimitedInf - 1u] = SIG_CAN_VCU_EVSpeedlimitedInf,
    [CAN_DB_SIG_VCU_ChargeHV_Status - 1u] = SIG_CAN_VCU_ChargeHV_Status,
    [CAN_DB_SIG_VCU_WhlDrvSt_CS - 1u] = SIG_CAN_VCU_WhlDrvSt_CS,
    [CAN_DB_SIG_VCU_LIMSetSpeed - 1u] = SIG_CAN_VCU_LIMSetSpeed,
    [CAN_DB_SIG_VCU_LIMSwitchSts - 1u] = SIG_CAN_VCU_LIMSwitchSts,
    [CAN_DB_SIG_VCU_LIMControlSts - 1u] = SIG_CAN_VCU_LIMControlSts,
    /* --- RX: IPU_TrqSpd (0x00A8, dlc=8) --- */
    [CAN_DB_SIG_IPU_IsgTqActIsgTqAct - 1u] = SIG_CAN_IPU_IsgTqActIsgTqAct,
    [CAN_DB_SIG_IPU_IsgSpdActSgn - 1u] = SIG_CAN_IPU_IsgSpdActSgn,
    [CAN_DB_SIG_IPU_TrqSpd_AliveCounter - 1u] = SIG_CAN_IPU_TrqSpd_AliveCounter,
    [CAN_DB_SIG_IPU_TrqSpd_Checksum - 1u] = SIG_CAN_IPU_TrqSpd_Checksum,
    /* --- RX: IPU_Sts (0x0171, dlc=8) --- */
    [CAN_DB_SIG_IPU_TqAvlIsgMax - 1u] = SIG_CAN_IPU_TqAvlIsgMax,
    [CAN_DB_SIG_IPU_TqAvlIsgMin - 1u] = SIG_CAN_IPU_TqAvlIsgMin,
    [CAN_DB_SIG_IPU_Sts_AliveCounter - 1u] = SIG_CAN_IPU_Sts_AliveCounter,
    [CAN_DB_SIG_IPU_Sts_Checksum - 1u] = SIG_CAN_IPU_Sts_Checksum,
    /* --- RX: AVAS_DisabledSts (0x02B2, dlc=8) --- */
    [CAN_DB_SIG_AVAS_AVASDisabledSts - 1u] = SIG_CAN_AVAS_AVASDisabledSts,
    [CAN_DB_SIG_AVAS_AVASBackwardDisabledSts - 1u] = SIG_CAN_AVAS_AVASBackwardDisabledSts,
    [CAN_DB_SIG_AVAS_AVASforwardDisabledSts - 1u] = SIG_CAN_AVAS_AVASforwardDisabledSts,
    [CAN_DB_SIG_AVAS_AVASForwardWarningSts - 1u] = SIG_CAN_AVAS_AVASForwardWarningSts,
    [CAN_DB_SIG_AVAS_AVASVolumeSts - 1u] = SIG_CAN_AVAS_AVASVolumeSts,
    [CAN_DB_SIG_AVAS_AVASBackwardWarningSts - 1u] = SIG_CAN_AVAS_AVASBackwardWarningSts,
    /* --- RX: ACU_ChimeTelltaleReq (0x0380, dlc=8) --- */
    [CAN_DB_SIG_ACU_DrvSeatbeltBucklestatus - 1u] = SIG_CAN_ACU_DrvSeatbeltBucklestatus,
    [CAN_DB_SIG_ACU_PassSeatbeWarning - 1u] = SIG_CAN_ACU_PassSeatbeWarning,
    [CAN_DB_SIG_ACU_PassSeatOccupantSensorStat - 1u] = SIG_CAN_ACU_PassSeatOccupantSensorStat,
    [CAN_DB_SIG_ACU_2nd_RSeatbeltBucklestatus - 1u] = SIG_CAN_ACU_2nd_RSeatbeltBucklestatus,
    [CAN_DB_SIG_ACU_PassengerAirbagStatus - 1u] = SIG_CAN_ACU_PassengerAirbagStatus,
    [CAN_DB_SIG_ACU_AirbagWarningStatus - 1u] = SIG_CAN_ACU_AirbagWarningStatus,
    [CAN_DB_SIG_ACU_DrvSeatbeltBuckleInvalid - 1u] = SIG_CAN_ACU_DrvSeatbeltBuckleInvalid,
    [CAN_DB_SIG_ACU_PassSeatbeltBuckleInvalid - 1u] = SIG_CAN_ACU_PassSeatbeltBuckleInvalid,
    [CAN_DB_SIG_ACU_2nd_LSeatbeltBucklestatus - 1u] = SIG_CAN_ACU_2nd_LSeatbeltBucklestatus,
    [CAN_DB_SIG_ACU_2nd_MSeatbeltBucklestatus - 1u] = SIG_CAN_ACU_2nd_MSeatbeltBucklestatus,
    [CAN_DB_SIG_ACU_3rd_LSeatbeltBucklestatus - 1u] = SIG_CAN_ACU_3rd_LSeatbeltBucklestatus,
    [CAN_DB_SIG_ACU_3rd_RSeatbeltBucklestatus - 1u] = SIG_CAN_ACU_3rd_RSeatbeltBucklestatus,
    [CAN_DB_SIG_ACU_3rd_MSeatbeltBucklestatus - 1u] = SIG_CAN_ACU_3rd_MSeatbeltBucklestatus,
    /* --- RX: EPS_InformSts (0x0150, dlc=8) --- */
    [CAN_DB_SIG_EPS_EpasFailed - 1u] = SIG_CAN_EPS_EpasFailed,
    [CAN_DB_SIG_EPS_ModSetInhibit - 1u] = SIG_CAN_EPS_ModSetInhibit,
    [CAN_DB_SIG_EPS_ModSts - 1u] = SIG_CAN_EPS_ModSts,
    /* --- RX: ESC_Status (0x0125, dlc=8) --- */
    [CAN_DB_SIG_ESC_BrakePedalSwitchStatus - 1u] = SIG_CAN_ESC_BrakePedalSwitchStatus,
    [CAN_DB_SIG_ESC_EBDFailed - 1u] = SIG_CAN_ESC_EBDFailed,
    [CAN_DB_SIG_ESC_ABSFailed - 1u] = SIG_CAN_ESC_ABSFailed,
    [CAN_DB_SIG_ESC_TCSFailed - 1u] = SIG_CAN_ESC_TCSFailed,
    [CAN_DB_SIG_ESC_ESPFailed - 1u] = SIG_CAN_ESC_ESPFailed,
    [CAN_DB_SIG_ESC_VehicleSpeed - 1u] = SIG_CAN_ESC_VehicleSpeed,
    [CAN_DB_SIG_ESC_PATAResponse - 1u] = SIG_CAN_ESC_PATAResponse,
    [CAN_DB_SIG_ESC_LampInfo - 1u] = SIG_CAN_ESC_LampInfo,
    [CAN_DB_SIG_ESC_VehicleSpeedInvalid - 1u] = SIG_CAN_ESC_VehicleSpeedInvalid,
    [CAN_DB_SIG_ESC_AVHStatus - 1u] = SIG_CAN_ESC_AVHStatus,
    [CAN_DB_SIG_ESC_EPBStatus - 1u] = SIG_CAN_ESC_EPBStatus,
    [CAN_DB_SIG_ESC_EPBErrorStatus - 1u] = SIG_CAN_ESC_EPBErrorStatus,
    [CAN_DB_SIG_ESC_BrakePedalSwitchInvalid - 1u] = SIG_CAN_ESC_BrakePedalSwitchInvalid,
    [CAN_DB_SIG_ESC_HHC_ErrorStatus - 1u] = SIG_CAN_ESC_HHC_ErrorStatus,
    [CAN_DB_SIG_ESC_HDC_ErrorStatus - 1u] = SIG_CAN_ESC_HDC_ErrorStatus,
    [CAN_DB_SIG_ESC_HDC_Active - 1u] = SIG_CAN_ESC_HDC_Active,
    /* --- RX: ESC_DriverRemind (0x012F, dlc=8) --- */
    [CAN_DB_SIG_ESC_AVH_Disp_WithoutSeatbelt - 1u] = SIG_CAN_ESC_AVH_Disp_WithoutSeatbelt,
    [CAN_DB_SIG_ESC_ParkingBrakeFail - 1u] = SIG_CAN_ESC_ParkingBrakeFail,
    [CAN_DB_SIG_ESC_EPB_Disp_WithoutSeatbelt - 1u] = SIG_CAN_ESC_EPB_Disp_WithoutSeatbelt,
    [CAN_DB_SIG_ESC_EPB_Disp_WithoutBrake - 1u] = SIG_CAN_ESC_EPB_Disp_WithoutBrake,
    [CAN_DB_SIG_ESC_EPB_SlopeOverThresholdRemind - 1u] = SIG_CAN_ESC_EPB_SlopeOverThresholdRemind,
    [CAN_DB_SIG_ESC_EPB_ReleaseInNonP - 1u] = SIG_CAN_ESC_EPB_ReleaseInNonP,
    [CAN_DB_SIG_ESC_HDC_Disp_DiscTempHigh - 1u] = SIG_CAN_ESC_HDC_Disp_DiscTempHigh,
    [CAN_DB_SIG_ESC_HDC_TargetSpeed - 1u] = SIG_CAN_ESC_HDC_TargetSpeed,
    [CAN_DB_SIG_ESC_SystemWarning - 1u] = SIG_CAN_ESC_SystemWarning,
    [CAN_DB_SIG_ESC_iTPMS_FLTyreWarn - 1u] = SIG_CAN_ESC_iTPMS_FLTyreWarn,
    [CAN_DB_SIG_ESC_iTPMS_FRTyreWarn - 1u] = SIG_CAN_ESC_iTPMS_FRTyreWarn,
    [CAN_DB_SIG_ESC_iTPMS_RLTyreWarn - 1u] = SIG_CAN_ESC_iTPMS_RLTyreWarn,
    [CAN_DB_SIG_ESC_iTPMS_RRTyreWarn - 1u] = SIG_CAN_ESC_iTPMS_RRTyreWarn,
    [CAN_DB_SIG_ESC_iTPMS_comTyreWarn - 1u] = SIG_CAN_ESC_iTPMS_comTyreWarn,
    [CAN_DB_SIG_ESC_iTPMS_SystemSt - 1u] = SIG_CAN_ESC_iTPMS_SystemSt,
    [CAN_DB_SIG_ESC_iTPMSCalPsbl - 1u] = SIG_CAN_ESC_iTPMSCalPsbl,
    [CAN_DB_SIG_ESC_iTPMSCalsts - 1u] = SIG_CAN_ESC_iTPMSCalsts,
    /* --- RX: ESC_Regen (0x0128, dlc=8) --- */
    [CAN_DB_SIG_ESC_BrakeFluidLevelLow - 1u] = SIG_CAN_ESC_BrakeFluidLevelLow,
    [CAN_DB_SIG_ESC_MbRegenTargetWheelSts - 1u] = SIG_CAN_ESC_MbRegenTargetWheelSts,
    /* --- RX: RSRSR_InformStatus (0x02E4, dlc=8) --- */
    [CAN_DB_SIG_RSRSR_SystemStatus - 1u] = SIG_CAN_RSRSR_SystemStatus,
    [CAN_DB_SIG_RSRSR_LCA_Status - 1u] = SIG_CAN_RSRSR_LCA_Status,
    [CAN_DB_SIG_RSRSR_RCTA_Status - 1u] = SIG_CAN_RSRSR_RCTA_Status,
    [CAN_DB_SIG_RSRSR_RCW_Status - 1u] = SIG_CAN_RSRSR_RCW_Status,
    [CAN_DB_SIG_RSRSR_LCA_WarningLeft - 1u] = SIG_CAN_RSRSR_LCA_WarningLeft,
    [CAN_DB_SIG_RSRSR_LCA_WarningRight - 1u] = SIG_CAN_RSRSR_LCA_WarningRight,
    [CAN_DB_SIG_RSRSR_RCTA_WarningLeft - 1u] = SIG_CAN_RSRSR_RCTA_WarningLeft,
    [CAN_DB_SIG_RSRSR_RCW_Warning - 1u] = SIG_CAN_RSRSR_RCW_Warning,
    [CAN_DB_SIG_RSRSR_DOW_Status - 1u] = SIG_CAN_RSRSR_DOW_Status,
    [CAN_DB_SIG_RSRSR_DOW_WarningLeft - 1u] = SIG_CAN_RSRSR_DOW_WarningLeft,
    [CAN_DB_SIG_RSRSR_DOW_WarningRight - 1u] = SIG_CAN_RSRSR_DOW_WarningRight,
    [CAN_DB_SIG_RSRSR_LCA_AudioSwitch - 1u] = SIG_CAN_RSRSR_LCA_AudioSwitch,
    [CAN_DB_SIG_RSRSR_RCTA_WarningRight - 1u] = SIG_CAN_RSRSR_RCTA_WarningRight,
    [CAN_DB_SIG_RSRSR_RCTA_Brake - 1u] = SIG_CAN_RSRSR_RCTA_Brake,
    [CAN_DB_SIG_RSRSR_RCW_Brake - 1u] = SIG_CAN_RSRSR_RCW_Brake,
    /* --- RX: FCS_ALAD_Status (0x01B0, dlc=8) --- */
    [CAN_DB_SIG_FCS_ALAD_SwitchStatus - 1u] = SIG_CAN_FCS_ALAD_SwitchStatus,
    [CAN_DB_SIG_FCS_ALAD_Type - 1u] = SIG_CAN_FCS_ALAD_Type,
    [CAN_DB_SIG_FCS_ALAD_WarningType - 1u] = SIG_CAN_FCS_ALAD_WarningType,
    [CAN_DB_SIG_FCS_ALAD_Status - 1u] = SIG_CAN_FCS_ALAD_Status,
    [CAN_DB_SIG_FCS_FCS_Status - 1u] = SIG_CAN_FCS_FCS_Status,
    [CAN_DB_SIG_FCS_ALAD_Warning - 1u] = SIG_CAN_FCS_ALAD_Warning,
    /* --- RX: FCS_SLIF_IHBC_Status (0x02E0, dlc=8) --- */
    [CAN_DB_SIG_FCS_IHBC_Status - 1u] = SIG_CAN_FCS_IHBC_Status,
    [CAN_DB_SIG_FCS_IHBC_LightDistribReq - 1u] = SIG_CAN_FCS_IHBC_LightDistribReq,
    [CAN_DB_SIG_FCS_IHBC_Switch - 1u] = SIG_CAN_FCS_IHBC_Switch,
    [CAN_DB_SIG_FCS_SLIF_Switch - 1u] = SIG_CAN_FCS_SLIF_Switch,
    [CAN_DB_SIG_FCS_Sign_Speedlimit - 1u] = SIG_CAN_FCS_Sign_Speedlimit,
    [CAN_DB_SIG_FCS_Sign_SpeedLimitCancelled - 1u] = SIG_CAN_FCS_Sign_SpeedLimitCancelled,
    [CAN_DB_SIG_FCS_DrvOff_Switch - 1u] = SIG_CAN_FCS_DrvOff_Switch,
    [CAN_DB_SIG_FCS_SLIF_Status - 1u] = SIG_CAN_FCS_SLIF_Status,
    [CAN_DB_SIG_FCS_SLIF_Warning - 1u] = SIG_CAN_FCS_SLIF_Warning,
    [CAN_DB_SIG_FCS_SLIF_WarningSwitch - 1u] = SIG_CAN_FCS_SLIF_WarningSwitch,
    [CAN_DB_SIG_FCS_Sign_Misc - 1u] = SIG_CAN_FCS_Sign_Misc,
    [CAN_DB_SIG_FCS_SLIF_IHBC_Status_AliveCounte - 1u] = SIG_CAN_FCS_SLIF_IHBC_Status_AliveCounte,
    [CAN_DB_SIG_FCS_SLIF_MiscSts - 1u] = SIG_CAN_FCS_SLIF_MiscSts,
    [CAN_DB_SIG_FCS_SLIF_IHBC_Status_CheckSum - 1u] = SIG_CAN_FCS_SLIF_IHBC_Status_CheckSum,
    /* --- RX: FCS_Road_Status (0x01B2, dlc=8) --- */
    [CAN_DB_SIG_FCS_LineLeft_D - 1u] = SIG_CAN_FCS_LineLeft_D,
    [CAN_DB_SIG_FCS_LineLeft_Type - 1u] = SIG_CAN_FCS_LineLeft_Type,
    [CAN_DB_SIG_FCS_LineLeft_Warning - 1u] = SIG_CAN_FCS_LineLeft_Warning,
    [CAN_DB_SIG_FCS_LineLeft_Color - 1u] = SIG_CAN_FCS_LineLeft_Color,
    [CAN_DB_SIG_FCS_LineRight_Color - 1u] = SIG_CAN_FCS_LineRight_Color,
    [CAN_DB_SIG_FCS_LineRight_Warning - 1u] = SIG_CAN_FCS_LineRight_Warning,
    [CAN_DB_SIG_FCS_LineRight_D - 1u] = SIG_CAN_FCS_LineRight_D,
    [CAN_DB_SIG_FCS_LineRight_Type - 1u] = SIG_CAN_FCS_LineRight_Type,
    [CAN_DB_SIG_FCS_Lane_Radius - 1u] = SIG_CAN_FCS_Lane_Radius,
    [CAN_DB_SIG_FCS_DriveOnLine - 1u] = SIG_CAN_FCS_DriveOnLine,
    [CAN_DB_SIG_FCS_LaneChange_Det - 1u] = SIG_CAN_FCS_LaneChange_Det,
    [CAN_DB_SIG_FCS_LaneCenter_Color - 1u] = SIG_CAN_FCS_LaneCenter_Color,
    /* --- RX: FCS_ELK_Status (0x0114, dlc=8) --- */
    [CAN_DB_SIG_FCS_ELK_SwitchStatus - 1u] = SIG_CAN_FCS_ELK_SwitchStatus,
    [CAN_DB_SIG_FCS_ELK_Status - 1u] = SIG_CAN_FCS_ELK_Status,
    [CAN_DB_SIG_FCS_ELK_LeftMode - 1u] = SIG_CAN_FCS_ELK_LeftMode,
    [CAN_DB_SIG_FCS_ELK_RightMode - 1u] = SIG_CAN_FCS_ELK_RightMode,
    /* --- RX: FCS_AEB (0x01A2, dlc=8) --- */
    [CAN_DB_SIG_FCS_AEB_DecCtrlACT - 1u] = SIG_CAN_FCS_AEB_DecCtrlACT,
    [CAN_DB_SIG_FCS_AEB_WarningType - 1u] = SIG_CAN_FCS_AEB_WarningType,
    [CAN_DB_SIG_FCS_AEB_Switch - 1u] = SIG_CAN_FCS_AEB_Switch,
    [CAN_DB_SIG_FCS_AEB_Status - 1u] = SIG_CAN_FCS_AEB_Status,
    [CAN_DB_SIG_FCS_AEB_Warning - 1u] = SIG_CAN_FCS_AEB_Warning,
    [CAN_DB_SIG_FCS_AEB_FCWSenlevel - 1u] = SIG_CAN_FCS_AEB_FCWSenlevel,
    /* --- RX: FCS_Display (0x01A3, dlc=8) --- */
    [CAN_DB_SIG_FCS_ALOD_SpeedSetDisp - 1u] = SIG_CAN_FCS_ALOD_SpeedSetDisp,
    [CAN_DB_SIG_FCS_ALOD_TimeGap - 1u] = SIG_CAN_FCS_ALOD_TimeGap,
    [CAN_DB_SIG_FCS_ALOD_TimeGapDisp - 1u] = SIG_CAN_FCS_ALOD_TimeGapDisp,
    [CAN_DB_SIG_FCS_ALOD_Status - 1u] = SIG_CAN_FCS_ALOD_Status,
    [CAN_DB_SIG_FCS_ALOD_ControlType - 1u] = SIG_CAN_FCS_ALOD_ControlType,
    [CAN_DB_SIG_FCS_ALOD_Warning - 1u] = SIG_CAN_FCS_ALOD_Warning,
    [CAN_DB_SIG_FCS_FRS_Status - 1u] = SIG_CAN_FCS_FRS_Status,
    [CAN_DB_SIG_FCS_ALOD_Info - 1u] = SIG_CAN_FCS_ALOD_Info,
    [CAN_DB_SIG_FCS_Display_AliveCounter - 1u] = SIG_CAN_FCS_Display_AliveCounter,
    [CAN_DB_SIG_FCS_Display_CheckSum - 1u] = SIG_CAN_FCS_Display_CheckSum,
    /* --- RX: FCS_FrontObject (0x01A6, dlc=8) --- */
    [CAN_DB_SIG_FCS_ObjFront1_Dx - 1u] = SIG_CAN_FCS_ObjFront1_Dx,
    [CAN_DB_SIG_FCS_ObjFront1_Dy - 1u] = SIG_CAN_FCS_ObjFront1_Dy,
    [CAN_DB_SIG_FCS_ObjFront1_Type - 1u] = SIG_CAN_FCS_ObjFront1_Type,
    [CAN_DB_SIG_FCS_ObjFront1_Color - 1u] = SIG_CAN_FCS_ObjFront1_Color,
    [CAN_DB_SIG_FCS_ObjFront1_Warning - 1u] = SIG_CAN_FCS_ObjFront1_Warning,
    [CAN_DB_SIG_FCS_ObjFront2_Dx - 1u] = SIG_CAN_FCS_ObjFront2_Dx,
    [CAN_DB_SIG_FCS_ObjFront2_Dy - 1u] = SIG_CAN_FCS_ObjFront2_Dy,
    [CAN_DB_SIG_FCS_ObjFront2_Type - 1u] = SIG_CAN_FCS_ObjFront2_Type,
    [CAN_DB_SIG_FCS_ObjFront2_Color - 1u] = SIG_CAN_FCS_ObjFront2_Color,
    [CAN_DB_SIG_FCS_ObjFront1_Heading - 1u] = SIG_CAN_FCS_ObjFront1_Heading,
    [CAN_DB_SIG_FCS_ObjFront2_Heading - 1u] = SIG_CAN_FCS_ObjFront2_Heading,
    /* --- RX: FCS_FrontSideObject (0x01A7, dlc=8) --- */
    [CAN_DB_SIG_FCS_ObjFrontLeft_Dx - 1u] = SIG_CAN_FCS_ObjFrontLeft_Dx,
    [CAN_DB_SIG_FCS_ObjFrontLeft_Dy - 1u] = SIG_CAN_FCS_ObjFrontLeft_Dy,
    [CAN_DB_SIG_FCS_ObjFrontLeft_Type - 1u] = SIG_CAN_FCS_ObjFrontLeft_Type,
    [CAN_DB_SIG_FCS_ObjFrontLeft_Color - 1u] = SIG_CAN_FCS_ObjFrontLeft_Color,
    [CAN_DB_SIG_FCS_ObjFrontRight_Dx - 1u] = SIG_CAN_FCS_ObjFrontRight_Dx,
    [CAN_DB_SIG_FCS_ObjFrontRight_Dy - 1u] = SIG_CAN_FCS_ObjFrontRight_Dy,
    [CAN_DB_SIG_FCS_ObjFrontRight_Type - 1u] = SIG_CAN_FCS_ObjFrontRight_Type,
    [CAN_DB_SIG_FCS_ObjFrontRight_Color - 1u] = SIG_CAN_FCS_ObjFrontRight_Color,
    [CAN_DB_SIG_FCS_ObjFrontLeft_Heading - 1u] = SIG_CAN_FCS_ObjFrontLeft_Heading,
    [CAN_DB_SIG_FCS_ObjFrontRight_Heading - 1u] = SIG_CAN_FCS_ObjFrontRight_Heading,
    /* --- RX: AC_ReqSts (0x02F1, dlc=8) --- */
    [CAN_DB_SIG_AC_ACCompReq - 1u] = SIG_CAN_AC_ACCompReq,
    [CAN_DB_SIG_AC_AmbientTemperatureInvalid - 1u] = SIG_CAN_AC_AmbientTemperatureInvalid,
    [CAN_DB_SIG_AC_InsideTemperatureInvalid - 1u] = SIG_CAN_AC_InsideTemperatureInvalid,
    [CAN_DB_SIG_AC_AmbientTemperature - 1u] = SIG_CAN_AC_AmbientTemperature,
    [CAN_DB_SIG_AC_PM25InDen - 1u] = SIG_CAN_AC_PM25InDen,
    [CAN_DB_SIG_AC_PM25OutDen - 1u] = SIG_CAN_AC_PM25OutDen,
    [CAN_DB_SIG_AC_AirInQLevel - 1u] = SIG_CAN_AC_AirInQLevel,
    [CAN_DB_SIG_AC_AirOutQLevel - 1u] = SIG_CAN_AC_AirOutQLevel,
    [CAN_DB_SIG_AC_PM25Sts - 1u] = SIG_CAN_AC_PM25Sts,
    [CAN_DB_SIG_AC_InsideTemperature - 1u] = SIG_CAN_AC_InsideTemperature,
    [CAN_DB_SIG_AC_ParkingClimateStatus - 1u] = SIG_CAN_AC_ParkingClimateStatus,
    [CAN_DB_SIG_AC_ParkingClimateFailStatus - 1u] = SIG_CAN_AC_ParkingClimateFailStatus,
    [CAN_DB_SIG_AC_PM25PopupReq - 1u] = SIG_CAN_AC_PM25PopupReq,
    /* --- RX: TPMS_TyreDataInfo (0x01F2, dlc=8) --- */
    [CAN_DB_SIG_TPMS_FLTyrePr - 1u] = SIG_CAN_TPMS_FLTyrePr,
    [CAN_DB_SIG_TPMS_FRTyrePr - 1u] = SIG_CAN_TPMS_FRTyrePr,
    [CAN_DB_SIG_TPMS_RLTyrePr - 1u] = SIG_CAN_TPMS_RLTyrePr,
    [CAN_DB_SIG_TPMS_RRTyrePr - 1u] = SIG_CAN_TPMS_RRTyrePr,
    [CAN_DB_SIG_TPMS_FLTyreTemp - 1u] = SIG_CAN_TPMS_FLTyreTemp,
    [CAN_DB_SIG_TPMS_FRTyreTemp - 1u] = SIG_CAN_TPMS_FRTyreTemp,
    [CAN_DB_SIG_TPMS_RLTyreTemp - 1u] = SIG_CAN_TPMS_RLTyreTemp,
    [CAN_DB_SIG_TPMS_RRTyreTemp - 1u] = SIG_CAN_TPMS_RRTyreTemp,
    /* --- RX: TPMS_TempStatusInfo (0x0370, dlc=8) --- */
    [CAN_DB_SIG_TPMS_FL_Learning_Sts - 1u] = SIG_CAN_TPMS_FL_Learning_Sts,
    [CAN_DB_SIG_TPMS_FR_Learning_Sts - 1u] = SIG_CAN_TPMS_FR_Learning_Sts,
    [CAN_DB_SIG_TPMS_RL_Learning_Sts - 1u] = SIG_CAN_TPMS_RL_Learning_Sts,
    [CAN_DB_SIG_TPMS_RR_Learning_Sts - 1u] = SIG_CAN_TPMS_RR_Learning_Sts,
    [CAN_DB_SIG_TPMS_Mode - 1u] = SIG_CAN_TPMS_Mode,
    [CAN_DB_SIG_TPMS_W_SensorVoltageFL - 1u] = SIG_CAN_TPMS_W_SensorVoltageFL,
    [CAN_DB_SIG_TPMS_W_SensorVoltageFR - 1u] = SIG_CAN_TPMS_W_SensorVoltageFR,
    [CAN_DB_SIG_TPMS_W_SensorVoltageRL - 1u] = SIG_CAN_TPMS_W_SensorVoltageRL,
    [CAN_DB_SIG_TPMS_W_SensorVoltageRR - 1u] = SIG_CAN_TPMS_W_SensorVoltageRR,
    /* --- RX: BCM_LightChimeReq (0x01F0, dlc=8) --- */
    [CAN_DB_SIG_BCM_TurnIndicatorLeft - 1u] = SIG_CAN_BCM_TurnIndicatorLeft,
    [CAN_DB_SIG_BCM_AntiPinchWarnSetResp - 1u] = SIG_CAN_BCM_AntiPinchWarnSetResp,
    [CAN_DB_SIG_BCM_TurnIndicatorRight - 1u] = SIG_CAN_BCM_TurnIndicatorRight,
    [CAN_DB_SIG_BCM_TurnLeverSts - 1u] = SIG_CAN_BCM_TurnLeverSts,
    [CAN_DB_SIG_BCM_LowBeamSts - 1u] = SIG_CAN_BCM_LowBeamSts,
    [CAN_DB_SIG_BCM_HighBeamSts - 1u] = SIG_CAN_BCM_HighBeamSts,
    [CAN_DB_SIG_BCM_PositionLightSts - 1u] = SIG_CAN_BCM_PositionLightSts,
    [CAN_DB_SIG_BCM_DayRunningLightSts - 1u] = SIG_CAN_BCM_DayRunningLightSts,
    [CAN_DB_SIG_BCM_FollowMeHomeActive - 1u] = SIG_CAN_BCM_FollowMeHomeActive,
    [CAN_DB_SIG_BCM_FrontFogLightSts - 1u] = SIG_CAN_BCM_FrontFogLightSts,
    [CAN_DB_SIG_BCM_RearFogLightSts - 1u] = SIG_CAN_BCM_RearFogLightSts,
    [CAN_DB_SIG_BCM_LightLeftOn - 1u] = SIG_CAN_BCM_LightLeftOn,
    [CAN_DB_SIG_BCM_Warning_RKE_LOW_BATT - 1u] = SIG_CAN_BCM_Warning_RKE_LOW_BATT,
    [CAN_DB_SIG_BCM_BrakeLampsFailure - 1u] = SIG_CAN_BCM_BrakeLampsFailure,
    [CAN_DB_SIG_BCM_PositionLampsFailure - 1u] = SIG_CAN_BCM_PositionLampsFailure,
    [CAN_DB_SIG_BCM_ReverseLampsFailure - 1u] = SIG_CAN_BCM_ReverseLampsFailure,
    [CAN_DB_SIG_BCM_RrFogLampsFailure - 1u] = SIG_CAN_BCM_RrFogLampsFailure,
    [CAN_DB_SIG_BCM_DI_LampsFailure - 1u] = SIG_CAN_BCM_DI_LampsFailure,
    [CAN_DB_SIG_BCM_LowBeamLampsFailure - 1u] = SIG_CAN_BCM_LowBeamLampsFailure,
    /* --- RX: BCM_LDoorWindowState (0x0285, dlc=8) --- */
    [CAN_DB_SIG_BCM_Drv_Wdw_valid - 1u] = SIG_CAN_BCM_Drv_Wdw_valid,
    [CAN_DB_SIG_BCM_Drv_Val_Wdw_Opened - 1u] = SIG_CAN_BCM_Drv_Val_Wdw_Opened,
    [CAN_DB_SIG_BCM_Drv_Wdw_OD_Sts - 1u] = SIG_CAN_BCM_Drv_Wdw_OD_Sts,
    [CAN_DB_SIG_BCM_Drv_Wdw_Obs_InhibitSts - 1u] = SIG_CAN_BCM_Drv_Wdw_Obs_InhibitSts,
    [CAN_DB_SIG_BCM_Drv_Wdw_Running_Sts - 1u] = SIG_CAN_BCM_Drv_Wdw_Running_Sts,
    [CAN_DB_SIG_BCM_Drv_Wdw_PositionSts - 1u] = SIG_CAN_BCM_Drv_Wdw_PositionSts,
    [CAN_DB_SIG_BCM_Drv_Wdw_Error - 1u] = SIG_CAN_BCM_Drv_Wdw_Error,
    [CAN_DB_SIG_BCM_RLD_Wdw_valid - 1u] = SIG_CAN_BCM_RLD_Wdw_valid,
    [CAN_DB_SIG_BCM_RLD_Val_Wdw_Opened - 1u] = SIG_CAN_BCM_RLD_Val_Wdw_Opened,
    [CAN_DB_SIG_BCM_RLD_Wdw_OD_Sts - 1u] = SIG_CAN_BCM_RLD_Wdw_OD_Sts,
    [CAN_DB_SIG_BCM_RLD_Wdw_Obs_InhibitSts - 1u] = SIG_CAN_BCM_RLD_Wdw_Obs_InhibitSts,
    [CAN_DB_SIG_BCM_RLD_Wdw_Running_Sts - 1u] = SIG_CAN_BCM_RLD_Wdw_Running_Sts,
    [CAN_DB_SIG_BCM_RLD_Wdw_PositionSts - 1u] = SIG_CAN_BCM_RLD_Wdw_PositionSts,
    [CAN_DB_SIG_BCM_RLD_Wdw_Error - 1u] = SIG_CAN_BCM_RLD_Wdw_Error,
    [CAN_DB_SIG_BCM_FrontLeftDoorAjarStatus - 1u] = SIG_CAN_BCM_FrontLeftDoorAjarStatus,
    [CAN_DB_SIG_BCM_RearLeftDoorAjarStatus - 1u] = SIG_CAN_BCM_RearLeftDoorAjarStatus,
    [CAN_DB_SIG_BCM_MechaKey_LockAction - 1u] = SIG_CAN_BCM_MechaKey_LockAction,
    [CAN_DB_SIG_BCM_WdwNotCloseWarning - 1u] = SIG_CAN_BCM_WdwNotCloseWarning,
    [CAN_DB_SIG_BCM_AutoLockFailWarning - 1u] = SIG_CAN_BCM_AutoLockFailWarning,
    [CAN_DB_SIG_BCM_DoorChildLockst - 1u] = SIG_CAN_BCM_DoorChildLockst,
    [CAN_DB_SIG_BCM_RearMirrorFoldSetResp - 1u] = SIG_CAN_BCM_RearMirrorFoldSetResp,
    [CAN_DB_SIG_BCM_DoorLockStatusRL - 1u] = SIG_CAN_BCM_DoorLockStatusRL,
    [CAN_DB_SIG_BCM_DoorLockStatusDrv - 1u] = SIG_CAN_BCM_DoorLockStatusDrv,
    /* --- RX: BCM_RDoorWindowState (0x0286, dlc=8) --- */
    [CAN_DB_SIG_BCM_Pas_Wdw_valid - 1u] = SIG_CAN_BCM_Pas_Wdw_valid,
    [CAN_DB_SIG_BCM_Pas_Val_Wdw_Opened - 1u] = SIG_CAN_BCM_Pas_Val_Wdw_Opened,
    [CAN_DB_SIG_BCM_Pas_Wdw_OD_Sts - 1u] = SIG_CAN_BCM_Pas_Wdw_OD_Sts,
    [CAN_DB_SIG_BCM_Pas_Wdw_Obs_InhibitSts - 1u] = SIG_CAN_BCM_Pas_Wdw_Obs_InhibitSts,
    [CAN_DB_SIG_BCM_Pas_Wdw_Running_Sts - 1u] = SIG_CAN_BCM_Pas_Wdw_Running_Sts,
    [CAN_DB_SIG_BCM_Pas_Wdw_PositionSts - 1u] = SIG_CAN_BCM_Pas_Wdw_PositionSts,
    [CAN_DB_SIG_BCM_Pas_Wdw_Error - 1u] = SIG_CAN_BCM_Pas_Wdw_Error,
    [CAN_DB_SIG_BCM_RRD_Wdw_valid - 1u] = SIG_CAN_BCM_RRD_Wdw_valid,
    [CAN_DB_SIG_BCM_RRD_Val_Wdw_Opened - 1u] = SIG_CAN_BCM_RRD_Val_Wdw_Opened,
    [CAN_DB_SIG_BCM_RRD_Wdw_OD_Sts - 1u] = SIG_CAN_BCM_RRD_Wdw_OD_Sts,
    [CAN_DB_SIG_BCM_RRD_Wdw_Obs_InhibitSts - 1u] = SIG_CAN_BCM_RRD_Wdw_Obs_InhibitSts,
    [CAN_DB_SIG_BCM_RRD_Wdw_Running_Sts - 1u] = SIG_CAN_BCM_RRD_Wdw_Running_Sts,
    [CAN_DB_SIG_BCM_RRD_Wdw_PositionSts - 1u] = SIG_CAN_BCM_RRD_Wdw_PositionSts,
    [CAN_DB_SIG_BCM_RRD_Wdw_Error - 1u] = SIG_CAN_BCM_RRD_Wdw_Error,
    [CAN_DB_SIG_BCM_FrontRightDoorAjarStatus - 1u] = SIG_CAN_BCM_FrontRightDoorAjarStatus,
    [CAN_DB_SIG_BCM_RearRightDoorAjarStatus - 1u] = SIG_CAN_BCM_RearRightDoorAjarStatus,
    [CAN_DB_SIG_BCM_CargoBoxLightSts - 1u] = SIG_CAN_BCM_CargoBoxLightSts,
    [CAN_DB_SIG_BCM_FueltankCapSts - 1u] = SIG_CAN_BCM_FueltankCapSts,
    [CAN_DB_SIG_BCM_DoorLockStatusRR - 1u] = SIG_CAN_BCM_DoorLockStatusRR,
    [CAN_DB_SIG_BCM_DoorLockStatusPass - 1u] = SIG_CAN_BCM_DoorLockStatusPass,
    /* --- RX: BCM_StateUpdate (0x0284, dlc=8) --- */
    [CAN_DB_SIG_BCM_RearDefrosterSts - 1u] = SIG_CAN_BCM_RearDefrosterSts,
    [CAN_DB_SIG_BCM_Central_Lock_CMD - 1u] = SIG_CAN_BCM_Central_Lock_CMD,
    [CAN_DB_SIG_BCM_Central_unLock_CMD - 1u] = SIG_CAN_BCM_Central_unLock_CMD,
    [CAN_DB_SIG_BCM_HoodAjarStatus - 1u] = SIG_CAN_BCM_HoodAjarStatus,
    [CAN_DB_SIG_BCM_TrunkAjarStatus - 1u] = SIG_CAN_BCM_TrunkAjarStatus,
    [CAN_DB_SIG_BCM_RainshedStatus - 1u] = SIG_CAN_BCM_RainshedStatus,
    [CAN_DB_SIG_BCM_FollowMeHomeTimeSelectResp - 1u] = SIG_CAN_BCM_FollowMeHomeTimeSelectResp,
    [CAN_DB_SIG_BCM_RearLightHardSwtSts - 1u] = SIG_CAN_BCM_RearLightHardSwtSts,
    [CAN_DB_SIG_BCM_AutoCloseWindowSetResp - 1u] = SIG_CAN_BCM_AutoCloseWindowSetResp,
    [CAN_DB_SIG_BCM_RKEUnlockSetResp - 1u] = SIG_CAN_BCM_RKEUnlockSetResp,
    [CAN_DB_SIG_BCM_KeyInwithDrvDoorAjar - 1u] = SIG_CAN_BCM_KeyInwithDrvDoorAjar,
    [CAN_DB_SIG_BCM_ATWS_St - 1u] = SIG_CAN_BCM_ATWS_St,
    [CAN_DB_SIG_BCM_RLSModeSts - 1u] = SIG_CAN_BCM_RLSModeSts,
    [CAN_DB_SIG_BCM_IndicationPressClutch - 1u] = SIG_CAN_BCM_IndicationPressClutch,
    [CAN_DB_SIG_BCM_DM_ReqType - 1u] = SIG_CAN_BCM_DM_ReqType,
    [CAN_DB_SIG_BCM_ATWarnTypeSetResp - 1u] = SIG_CAN_BCM_ATWarnTypeSetResp,
    [CAN_DB_SIG_BCM_DayRunLightSetResp - 1u] = SIG_CAN_BCM_DayRunLightSetResp,
    [CAN_DB_SIG_BCM_RearWiperAutoActiveAtReverse - 1u] = SIG_CAN_BCM_RearWiperAutoActiveAtReverse,
    [CAN_DB_SIG_BCM_ReverseGearInfo - 1u] = SIG_CAN_BCM_ReverseGearInfo,
    [CAN_DB_SIG_BCM_PowerMode - 1u] = SIG_CAN_BCM_PowerMode,
    [CAN_DB_SIG_BCM_PowertrainChainStatus - 1u] = SIG_CAN_BCM_PowertrainChainStatus,
    [CAN_DB_SIG_BCM_Warning_IMMO_Fail - 1u] = SIG_CAN_BCM_Warning_IMMO_Fail,
    [CAN_DB_SIG_BCM_RLS_WinCloseReminder - 1u] = SIG_CAN_BCM_RLS_WinCloseReminder,
    [CAN_DB_SIG_BCM_360LtgExecuteStsFB - 1u] = SIG_CAN_BCM_360LtgExecuteStsFB,
    [CAN_DB_SIG_BCM_360LtgExecuteZoneFB - 1u] = SIG_CAN_BCM_360LtgExecuteZoneFB,
    [CAN_DB_SIG_BCM_RKE_RemoteACCtrl - 1u] = SIG_CAN_BCM_RKE_RemoteACCtrl,
    /* --- RX: BCM_SunroofState (0x0287, dlc=8) --- */
    [CAN_DB_SIG_BCM_Odometerbackup - 1u] = SIG_CAN_BCM_Odometerbackup,
    [CAN_DB_SIG_BCM_BottomClutchSwitchInvalid - 1u] = SIG_CAN_BCM_BottomClutchSwitchInvalid,
    [CAN_DB_SIG_L_Sunroof_Operation_State - 1u] = SIG_CAN_L_Sunroof_Operation_State,
    [CAN_DB_SIG_BCM_sunroof_valid - 1u] = SIG_CAN_BCM_sunroof_valid,
    [CAN_DB_SIG_L_sunroof_Val_Opened - 1u] = SIG_CAN_L_sunroof_Val_Opened,
    [CAN_DB_SIG_L_sunroof_ap_event - 1u] = SIG_CAN_L_sunroof_ap_event,
    [CAN_DB_SIG_BCM_sunroof_Error - 1u] = SIG_CAN_BCM_sunroof_Error,
    [CAN_DB_SIG_L_Sunroof_Position - 1u] = SIG_CAN_L_Sunroof_Position,
    /* --- RX: PEPS_KeyReminder (0x027F, dlc=8) --- */
    [CAN_DB_SIG_PEPS_RKECommand - 1u] = SIG_CAN_PEPS_RKECommand,
    [CAN_DB_SIG_PEPS_Warning_Stop_Emergency - 1u] = SIG_CAN_PEPS_Warning_Stop_Emergency,
    [CAN_DB_SIG_PEPS_Warning_Stop_Moving - 1u] = SIG_CAN_PEPS_Warning_Stop_Moving,
    /* --- RX: GW_PEPS_Information (0x02FC, dlc=8) --- */
    [CAN_DB_SIG_PEPS_PowerModeValidity - 1u] = SIG_CAN_PEPS_PowerModeValidity,
    [CAN_DB_SIG_PEPS_PowerMode - 1u] = SIG_CAN_PEPS_PowerMode,
    [CAN_DB_SIG_PEPS_EngineforbidSt - 1u] = SIG_CAN_PEPS_EngineforbidSt,
    [CAN_DB_SIG_PEPS_EngForbidWarn - 1u] = SIG_CAN_PEPS_EngForbidWarn,
    [CAN_DB_SIG_PEPS_RemoteControlSt - 1u] = SIG_CAN_PEPS_RemoteControlSt,
    [CAN_DB_SIG_PEPS_FailReason2TBOX - 1u] = SIG_CAN_PEPS_FailReason2TBOX,
    [CAN_DB_SIG_PEPS_StatusResponse2TBOX - 1u] = SIG_CAN_PEPS_StatusResponse2TBOX,
    [CAN_DB_SIG_PEPS_Warning_No_key_found - 1u] = SIG_CAN_PEPS_Warning_No_key_found,
    [CAN_DB_SIG_PEPS_Indication_press_brake_clut - 1u] = SIG_CAN_PEPS_Indication_press_brake_clut,
    [CAN_DB_SIG_PEPS_Indication_shift_to_PN - 1u] = SIG_CAN_PEPS_Indication_shift_to_PN,
    [CAN_DB_SIG_PEPS_TrunkUnlock_Enable - 1u] = SIG_CAN_PEPS_TrunkUnlock_Enable,
    [CAN_DB_SIG_PEPS_Indication_shift_to_Park - 1u] = SIG_CAN_PEPS_Indication_shift_to_Park,
    [CAN_DB_SIG_PEPS_Warning_keyInReminder - 1u] = SIG_CAN_PEPS_Warning_keyInReminder,
    [CAN_DB_SIG_PEPS_IGN1FailureWarning - 1u] = SIG_CAN_PEPS_IGN1FailureWarning,
    [CAN_DB_SIG_PEPS_Warning_Auth_ESCL_Fail - 1u] = SIG_CAN_PEPS_Warning_Auth_ESCL_Fail,
    [CAN_DB_SIG_PEPS_Warning_UID_LOW_BATT - 1u] = SIG_CAN_PEPS_Warning_UID_LOW_BATT,
    [CAN_DB_SIG_PEPS_ChargerConnectStarter_Warnn - 1u] = SIG_CAN_PEPS_ChargerConnectStarter_Warnn,
    [CAN_DB_SIG_PEPS_Warning_IMMO_Fail - 1u] = SIG_CAN_PEPS_Warning_IMMO_Fail,
    [CAN_DB_SIG_PEPS_TELAuthenStatus - 1u] = SIG_CAN_PEPS_TELAuthenStatus,
    [CAN_DB_SIG_PEPS_Indication_UID_Closer - 1u] = SIG_CAN_PEPS_Indication_UID_Closer,
    [CAN_DB_SIG_PEPS_CrankAllowSts - 1u] = SIG_CAN_PEPS_CrankAllowSts,
    [CAN_DB_SIG_PEPS_Warning_PoweOnCounterRemain - 1u] = SIG_CAN_PEPS_Warning_PoweOnCounterRemain,
    [CAN_DB_SIG_PEPS_0x1E2_TimeoutFlag - 1u] = SIG_CAN_PEPS_0x1E2_TimeoutFlag,
    [CAN_DB_SIG_PEPS_0x270_TimeoutFlag - 1u] = SIG_CAN_PEPS_0x270_TimeoutFlag,
    [CAN_DB_SIG_PEPS_0x272_TimeoutFlag - 1u] = SIG_CAN_PEPS_0x272_TimeoutFlag,
    [CAN_DB_SIG_PEPS_SSB_Failure_warning - 1u] = SIG_CAN_PEPS_SSB_Failure_warning,
    [CAN_DB_SIG_PEPS_WelcomeLightSetResp - 1u] = SIG_CAN_PEPS_WelcomeLightSetResp,
    [CAN_DB_SIG_PEPS_APUCfgResult - 1u] = SIG_CAN_PEPS_APUCfgResult,
    [CAN_DB_SIG_PEPS_WALCfgResult - 1u] = SIG_CAN_PEPS_WALCfgResult,
    /* --- RX: GW_BCM_Information (0x02FD, dlc=8) --- */
    [CAN_DB_SIG_TPMS_FLTyreWarn - 1u] = SIG_CAN_TPMS_FLTyreWarn,
    [CAN_DB_SIG_TPMS_FLTyre_Temperature - 1u] = SIG_CAN_TPMS_FLTyre_Temperature,
    [CAN_DB_SIG_TPMS_FLTyre_Fast_Leak - 1u] = SIG_CAN_TPMS_FLTyre_Fast_Leak,
    [CAN_DB_SIG_TPMS_FLTyre_Sensor_Failure - 1u] = SIG_CAN_TPMS_FLTyre_Sensor_Failure,
    [CAN_DB_SIG_TPMS_0x1F1_TimeoutFlag - 1u] = SIG_CAN_TPMS_0x1F1_TimeoutFlag,
    [CAN_DB_SIG_TPMS_FRTyreWarn - 1u] = SIG_CAN_TPMS_FRTyreWarn,
    [CAN_DB_SIG_TPMS_FRTyre_Temperature - 1u] = SIG_CAN_TPMS_FRTyre_Temperature,
    [CAN_DB_SIG_TPMS_FRTyre_Fast_Leak - 1u] = SIG_CAN_TPMS_FRTyre_Fast_Leak,
    [CAN_DB_SIG_TPMS_FRTyre_Sensor_Failure - 1u] = SIG_CAN_TPMS_FRTyre_Sensor_Failure,
    [CAN_DB_SIG_BCM_DMSDriveModeReqRej - 1u] = SIG_CAN_BCM_DMSDriveModeReqRej,
    [CAN_DB_SIG_TPMS_RLTyreWarn - 1u] = SIG_CAN_TPMS_RLTyreWarn,
    [CAN_DB_SIG_TPMS_RLTyre_Temperature - 1u] = SIG_CAN_TPMS_RLTyre_Temperature,
    [CAN_DB_SIG_TPMS_RLTyre_Fast_Leak - 1u] = SIG_CAN_TPMS_RLTyre_Fast_Leak,
    [CAN_DB_SIG_TPMS_RLTyre_Sensor_Failure - 1u] = SIG_CAN_TPMS_RLTyre_Sensor_Failure,
    [CAN_DB_SIG_TPMS_RRTyreWarn - 1u] = SIG_CAN_TPMS_RRTyreWarn,
    [CAN_DB_SIG_TPMS_RRTyre_Temperature - 1u] = SIG_CAN_TPMS_RRTyre_Temperature,
    [CAN_DB_SIG_TPMS_RRTyre_Fast_Leak - 1u] = SIG_CAN_TPMS_RRTyre_Fast_Leak,
    [CAN_DB_SIG_TPMS_RRTyre_Sensor_Failure - 1u] = SIG_CAN_TPMS_RRTyre_Sensor_Failure,
    [CAN_DB_SIG_TPMS_SystemSt - 1u] = SIG_CAN_TPMS_SystemSt,
    [CAN_DB_SIG_BCM_DM_TargetModeReq - 1u] = SIG_CAN_BCM_DM_TargetModeReq,
    [CAN_DB_SIG_BCM_DM_SwitchModeSts - 1u] = SIG_CAN_BCM_DM_SwitchModeSts,
    [CAN_DB_SIG_BCM_DMSVehicleMode - 1u] = SIG_CAN_BCM_DMSVehicleMode,
    [CAN_DB_SIG_BCM_DM_ChangeModeFailureControll - 1u] = SIG_CAN_BCM_DM_ChangeModeFailureControll,
    [CAN_DB_SIG_BCM_DM_ChangeModeFailureReason - 1u] = SIG_CAN_BCM_DM_ChangeModeFailureReason,
    [CAN_DB_SIG_BCM_0x283_TimeoutFlag - 1u] = SIG_CAN_BCM_0x283_TimeoutFlag,
    [CAN_DB_SIG_BCM_DM_SwitchModeStsDisp - 1u] = SIG_CAN_BCM_DM_SwitchModeStsDisp,
    /* --- RX: VCU_DriverTqInfo (0x01BB, dlc=8) --- */
    [CAN_DB_SIG_VCU_SOCpointSetsts - 1u] = SIG_CAN_VCU_SOCpointSetsts,
    [CAN_DB_SIG_VCU_IntellTurnAidResp - 1u] = SIG_CAN_VCU_IntellTurnAidResp,
    [CAN_DB_SIG_VCU_IntellTurnAidOprNtc - 1u] = SIG_CAN_VCU_IntellTurnAidOprNtc,
    [CAN_DB_SIG_VCU_IntellTurnAidOprGuide - 1u] = SIG_CAN_VCU_IntellTurnAidOprGuide,
    [CAN_DB_SIG_VCU_IntellTurnAidSts - 1u] = SIG_CAN_VCU_IntellTurnAidSts,
    [CAN_DB_SIG_VCU_DriverTqInfo_AliveCounter - 1u] = SIG_CAN_VCU_DriverTqInfo_AliveCounter,
    [CAN_DB_SIG_VCU_DriverTqInfo_Checksum - 1u] = SIG_CAN_VCU_DriverTqInfo_Checksum,
    /* --- RX: BMSH_General (0x00B0, dlc=8) --- */
    [CAN_DB_SIG_BMSH_stMode - 1u] = SIG_CAN_BMSH_stMode,
    [CAN_DB_SIG_BMSH_VehicleHVILSts - 1u] = SIG_CAN_BMSH_VehicleHVILSts,
    [CAN_DB_SIG_BMSH_InterHVILSts - 1u] = SIG_CAN_BMSH_InterHVILSts,
    [CAN_DB_SIG_BMSH_InsulationSts - 1u] = SIG_CAN_BMSH_InsulationSts,
    [CAN_DB_SIG_BMSH_MainPrechgSt - 1u] = SIG_CAN_BMSH_MainPrechgSt,
    /* --- RX: BMSH_VoltCurr (0x0178, dlc=8) --- */
    [CAN_DB_SIG_BMSH_BattCurr - 1u] = SIG_CAN_BMSH_BattCurr,
    [CAN_DB_SIG_BMSH_HVBusVolt - 1u] = SIG_CAN_BMSH_HVBusVolt,
    [CAN_DB_SIG_BMSH_BattVolt - 1u] = SIG_CAN_BMSH_BattVolt,
    [CAN_DB_SIG_BMSH_BattFaultLampState - 1u] = SIG_CAN_BMSH_BattFaultLampState,
    /* --- RX: BMSH_OBC_Control (0x0211, dlc=8) --- */
    [CAN_DB_SIG_BMSH_ChargeLEDCtrl - 1u] = SIG_CAN_BMSH_ChargeLEDCtrl,
    [CAN_DB_SIG_BMSH_FastChgCC2ConntState - 1u] = SIG_CAN_BMSH_FastChgCC2ConntState,
    /* --- RX: BMSH_Info (0x02F4, dlc=8) --- */
    [CAN_DB_SIG_DCChrgrSt - 1u] = SIG_CAN_DCChrgrSt,
    [CAN_DB_SIG_BMSH_BattSOCDisp - 1u] = SIG_CAN_BMSH_BattSOCDisp,
    /* --- RX: EcmChas2Fr92 (0x029A, dlc=8) --- */
    [CAN_DB_SIG_FuCnsAvgIndcdFuCnsIndcdVal1 - 1u] = SIG_CAN_FuCnsAvgIndcdFuCnsIndcdVal1,
    [CAN_DB_SIG_PwrCnsAvgIndcdPwrCns1 - 1u] = SIG_CAN_PwrCnsAvgIndcdPwrCns1,
    [CAN_DB_SIG_PwrCnsAvgIndcdPwrCns2 - 1u] = SIG_CAN_PwrCnsAvgIndcdPwrCns2,
    [CAN_DB_SIG_FuCnsAvgIndcdFuCnsIndcdVal2 - 1u] = SIG_CAN_FuCnsAvgIndcdFuCnsIndcdVal2,
    [CAN_DB_SIG_FuCnsAvgIndcdFuCnsIndcdVal3 - 1u] = SIG_CAN_FuCnsAvgIndcdFuCnsIndcdVal3,
    [CAN_DB_SIG_PwrCnsAvgIndcdPwrCns3 - 1u] = SIG_CAN_PwrCnsAvgIndcdPwrCns3,
    [CAN_DB_SIG_EgyAvgCnsDstSg - 1u] = SIG_CAN_EgyAvgCnsDstSg,
    /* --- RX: EcmChas2Fr93 (0x029B, dlc=8) --- */
    [CAN_DB_SIG_DstToEmptyIndcdDstToEmpty1 - 1u] = SIG_CAN_DstToEmptyIndcdDstToEmpty1,
    [CAN_DB_SIG_AcEgyDistbn - 1u] = SIG_CAN_AcEgyDistbn,
    [CAN_DB_SIG_DstToEmptyIndcdDstToEmpty2 - 1u] = SIG_CAN_DstToEmptyIndcdDstToEmpty2,
    [CAN_DB_SIG_TotDrvPwrAct - 1u] = SIG_CAN_TotDrvPwrAct,
    [CAN_DB_SIG_ThmEgyDistbn - 1u] = SIG_CAN_ThmEgyDistbn,
    [CAN_DB_SIG_HvConvPwrAct - 1u] = SIG_CAN_HvConvPwrAct,
    /* --- RX: EcmChas2Fr33 (0x0255, dlc=8) --- */
    [CAN_DB_SIG_IdleChrgFctSts - 1u] = SIG_CAN_IdleChrgFctSts,
    [CAN_DB_SIG_EVBlkd - 1u] = SIG_CAN_EVBlkd,
    [CAN_DB_SIG_REVBlkd - 1u] = SIG_CAN_REVBlkd,
    [CAN_DB_SIG_DispIdlChrgPwr - 1u] = SIG_CAN_DispIdlChrgPwr,
    [CAN_DB_SIG_DstEstimdToEmptyForDrvgElec - 1u] = SIG_CAN_DstEstimdToEmptyForDrvgElec,
    [CAN_DB_SIG_DstEstimdToEmptyForDrvgElecPredT - 1u] = SIG_CAN_DstEstimdToEmptyForDrvgElecPredT,
    /* --- RX: PCM_Temperature (0x0364, dlc=8) --- */
    [CAN_DB_SIG_PCM_MotOverTemp - 1u] = SIG_CAN_PCM_MotOverTemp,
    /* --- RX: PCM_Warning (0x0365, dlc=8) --- */
    [CAN_DB_SIG_PCM_IsgDeratSts - 1u] = SIG_CAN_PCM_IsgDeratSts,
    [CAN_DB_SIG_PCM_MotTAlm - 1u] = SIG_CAN_PCM_MotTAlm,
    [CAN_DB_SIG_PCM_IsgTAlm - 1u] = SIG_CAN_PCM_IsgTAlm,
    /* --- RX: PcmChas1Fr19 (0x0041, dlc=8) --- */
    [CAN_DB_SIG_TrsmFltIndcn - 1u] = SIG_CAN_TrsmFltIndcn,
    /* --- RX: IPU_Temperature (0x0360, dlc=8) --- */
    [CAN_DB_SIG_IPU_IsgOverTemp - 1u] = SIG_CAN_IPU_IsgOverTemp,
    /* --- RX: IPU_Warning (0x0361, dlc=8) --- */
    [CAN_DB_SIG_IPU_MotTAlm - 1u] = SIG_CAN_IPU_MotTAlm,
    [CAN_DB_SIG_IPU_IPUTAlm - 1u] = SIG_CAN_IPU_IPUTAlm,
    [CAN_DB_SIG_IPU_IsgFAlm - 1u] = SIG_CAN_IPU_IsgFAlm,
    /* --- RX: TRM_StatusInfo (0x02EA, dlc=8) --- */
    [CAN_DB_SIG_TRM_TurnLeftLampsFailure - 1u] = SIG_CAN_TRM_TurnLeftLampsFailure,
    [CAN_DB_SIG_TRM_TurnRightLampsFailure - 1u] = SIG_CAN_TRM_TurnRightLampsFailure,
    [CAN_DB_SIG_TRM_EleIF_Connect_Status - 1u] = SIG_CAN_TRM_EleIF_Connect_Status,
    [CAN_DB_SIG_TRM_EleIF_Connect_Failure - 1u] = SIG_CAN_TRM_EleIF_Connect_Failure,
    [CAN_DB_SIG_TRM_Message_AliveCounter - 1u] = SIG_CAN_TRM_Message_AliveCounter,
    [CAN_DB_SIG_TRM_Message_CheckSum - 1u] = SIG_CAN_TRM_Message_CheckSum,
    /* --- TX: IPK_EngineService (0x03E9, dlc=8) --- */
    [CAN_DB_SIG_IPK_IPKEngineTotalOdometer - 1u] = SIG_CAN_IPK_IPKEngineTotalOdometer,
    [CAN_DB_SIG_IPK_DayToEngSrv - 1u] = SIG_CAN_IPK_DayToEngSrv,
    [CAN_DB_SIG_IPK_ServiceEngineMaintainInterva - 1u] = SIG_CAN_IPK_ServiceEngineMaintainInterva,
    /* --- TX: IPK_STS (0x026D, dlc=8) --- */
    [CAN_DB_SIG_IPK_AirbagUnitLEDSts - 1u] = SIG_CAN_IPK_AirbagUnitLEDSts,
    [CAN_DB_SIG_IPK_SVA_AudibleWarningCfgResult - 1u] = SIG_CAN_IPK_SVA_AudibleWarningCfgResult,
    [CAN_DB_SIG_IPK_FuelLowLevelWarning - 1u] = SIG_CAN_IPK_FuelLowLevelWarning,
    [CAN_DB_SIG_IPK_ESCoffInfo - 1u] = SIG_CAN_IPK_ESCoffInfo,
    [CAN_DB_SIG_IPK_Fail - 1u] = SIG_CAN_IPK_Fail,
    [CAN_DB_SIG_IPK_QDashALODFail - 1u] = SIG_CAN_IPK_QDashALODFail,
    [CAN_DB_SIG_IPK_FuelLevelSts - 1u] = SIG_CAN_IPK_FuelLevelSts,
    [CAN_DB_SIG_IPK_AverageVehicleSpeed - 1u] = SIG_CAN_IPK_AverageVehicleSpeed,
    [CAN_DB_SIG_IPK_HandBrakeSts - 1u] = SIG_CAN_IPK_HandBrakeSts,
    [CAN_DB_SIG_IPK_MaintanceWarningSts - 1u] = SIG_CAN_IPK_MaintanceWarningSts,
    [CAN_DB_SIG_IPK_LanguageMode - 1u] = SIG_CAN_IPK_LanguageMode,
    [CAN_DB_SIG_IPK_EPS_ModSetSelection - 1u] = SIG_CAN_IPK_EPS_ModSetSelection,
    [CAN_DB_SIG_IPK_EPS_DMCorrelativeMode - 1u] = SIG_CAN_IPK_EPS_DMCorrelativeMode,
    [CAN_DB_SIG_IPK_Backlightadjust - 1u] = SIG_CAN_IPK_Backlightadjust,
    [CAN_DB_SIG_IPK_vDisplay - 1u] = SIG_CAN_IPK_vDisplay,
    [CAN_DB_SIG_IPK_OilLowPressure - 1u] = SIG_CAN_IPK_OilLowPressure,
    [CAN_DB_SIG_IPK_LIMmemoryEnabe_Reserved - 1u] = SIG_CAN_IPK_LIMmemoryEnabe_Reserved,
    [CAN_DB_SIG_IPK_BattLowVoltageWarning - 1u] = SIG_CAN_IPK_BattLowVoltageWarning,
    [CAN_DB_SIG_IPK_driving_mode_light_sts - 1u] = SIG_CAN_IPK_driving_mode_light_sts,
    /* --- TX: IPK_SettingRequest (0x0260, dlc=8) --- */
    [CAN_DB_SIG_IPK_AEB_FCWStateReq - 1u] = SIG_CAN_IPK_AEB_FCWStateReq,
    [CAN_DB_SIG_IPK_AEB_AEBStateReq - 1u] = SIG_CAN_IPK_AEB_AEBStateReq,
    [CAN_DB_SIG_IPK_ALOD_ControlTypeReq - 1u] = SIG_CAN_IPK_ALOD_ControlTypeReq,
    [CAN_DB_SIG_IPK_LCA_EnableStatus - 1u] = SIG_CAN_IPK_LCA_EnableStatus,
    [CAN_DB_SIG_IPK_RCTA_EnableStatus - 1u] = SIG_CAN_IPK_RCTA_EnableStatus,
    [CAN_DB_SIG_IPK_RCW_EnableStatus - 1u] = SIG_CAN_IPK_RCW_EnableStatus,
    [CAN_DB_SIG_IPK_AEB_FCWSenlevel - 1u] = SIG_CAN_IPK_AEB_FCWSenlevel,
    [CAN_DB_SIG_IPK_LKS_LaneAssistTypeReq - 1u] = SIG_CAN_IPK_LKS_LaneAssistTypeReq,
    [CAN_DB_SIG_IPK_LDW_WarningTypeSetting - 1u] = SIG_CAN_IPK_LDW_WarningTypeSetting,
    [CAN_DB_SIG_IPK_IHBC_MenuReq - 1u] = SIG_CAN_IPK_IHBC_MenuReq,
    [CAN_DB_SIG_IPK_SLIF_MenuReq - 1u] = SIG_CAN_IPK_SLIF_MenuReq,
    /* --- TX: IPK_Fuel_Sts (0x02D8, dlc=8) --- */
    [CAN_DB_SIG_FuFillgDetnForUseInt - 1u] = SIG_CAN_FuFillgDetnForUseInt,
    [CAN_DB_SIG_RstTrip1 - 1u] = SIG_CAN_RstTrip1,
    [CAN_DB_SIG_RstTrip2 - 1u] = SIG_CAN_RstTrip2,
    /* --- TX: IPK_TotalOdometer (0x03F1, dlc=8) --- */
    [CAN_DB_SIG_IPK_IPKTotalOdometer - 1u] = SIG_CAN_IPK_IPKTotalOdometer,
    [CAN_DB_SIG_IPK_DTEodometer - 1u] = SIG_CAN_IPK_DTEodometer,
    [CAN_DB_SIG_IPK_OdometerbackupEnable - 1u] = SIG_CAN_IPK_OdometerbackupEnable,
    [CAN_DB_SIG_IPK_ServiceMaintainInterval - 1u] = SIG_CAN_IPK_ServiceMaintainInterval,
    /* --- TX: IPK_DateTime_Info (0x03F0, dlc=8) --- */
    [CAN_DB_SIG_IPK_Second - 1u] = SIG_CAN_IPK_Second,
    [CAN_DB_SIG_IPK_Minute - 1u] = SIG_CAN_IPK_Minute,
    [CAN_DB_SIG_IPK_Hour - 1u] = SIG_CAN_IPK_Hour,
    [CAN_DB_SIG_IPK_TimeDisplayMode - 1u] = SIG_CAN_IPK_TimeDisplayMode,
    [CAN_DB_SIG_IPK_Day - 1u] = SIG_CAN_IPK_Day,
    [CAN_DB_SIG_IPK_Month - 1u] = SIG_CAN_IPK_Month,
    [CAN_DB_SIG_IPK_Year - 1u] = SIG_CAN_IPK_Year,
    [CAN_DB_SIG_IPK_VehicleStopTime - 1u] = SIG_CAN_IPK_VehicleStopTime,
    /* --- TX: IPK_Fuel_Info (0x03F6, dlc=8) --- */
    [CAN_DB_SIG_IPK_FuelSensorVoltage - 1u] = SIG_CAN_IPK_FuelSensorVoltage,
    [CAN_DB_SIG_IPK_AverageFuelConsumptionOneCyc - 1u] = SIG_CAN_IPK_AverageFuelConsumptionOneCyc,
    [CAN_DB_SIG_IPK_FuelSensorShortOrOpenBatt - 1u] = SIG_CAN_IPK_FuelSensorShortOrOpenBatt,
    [CAN_DB_SIG_IPK_FuelSensorShortGND - 1u] = SIG_CAN_IPK_FuelSensorShortGND,
    [CAN_DB_SIG_IPK_FuelSensorUpperLimit - 1u] = SIG_CAN_IPK_FuelSensorUpperLimit,
    [CAN_DB_SIG_IPK_DayToSrv - 1u] = SIG_CAN_IPK_DayToSrv,
    [CAN_DB_SIG_IPK_InstanteFuelConsumption - 1u] = SIG_CAN_IPK_InstanteFuelConsumption,
    [CAN_DB_SIG_IPK_Fuel_Info_AliveCounter - 1u] = SIG_CAN_IPK_Fuel_Info_AliveCounter,
    [CAN_DB_SIG_IPK_Fuel_Info_Checksum - 1u] = SIG_CAN_IPK_Fuel_Info_Checksum,
    /* --- TX: IPK_ODO_Consump (0x03F7, dlc=8) --- */
    [CAN_DB_SIG_IPK_EVDTEodometer - 1u] = SIG_CAN_IPK_EVDTEodometer,
    [CAN_DB_SIG_IPK_AveragePowerConsumption - 1u] = SIG_CAN_IPK_AveragePowerConsumption,
    [CAN_DB_SIG_IPK_InstantPowerConsumption - 1u] = SIG_CAN_IPK_InstantPowerConsumption,
    [CAN_DB_SIG_IPK_AverageFuelConsumptionUnit - 1u] = SIG_CAN_IPK_AverageFuelConsumptionUnit,
    [CAN_DB_SIG_IPK_AverageFuelConsumption - 1u] = SIG_CAN_IPK_AverageFuelConsumption,
    /* --- TX: NWM_IPK (0x0402, dlc=8) --- */
    [CAN_DB_SIG_IPK_Address - 1u] = SIG_CAN_IPK_Address,
    [CAN_DB_SIG_IPK_RMR - 1u] = SIG_CAN_IPK_RMR,
    [CAN_DB_SIG_IPK_AWB - 1u] = SIG_CAN_IPK_AWB,
    [CAN_DB_SIG_IPK_Wakeup_reasons - 1u] = SIG_CAN_IPK_Wakeup_reasons,
    [CAN_DB_SIG_IPK_NMSts - 1u] = SIG_CAN_IPK_NMSts,
    [CAN_DB_SIG_IPK_Stayawake_reasons - 1u] = SIG_CAN_IPK_Stayawake_reasons,
};

/* ---------------------------------------------------------------- *
 *  Mapping helper                                                   *
 * ---------------------------------------------------------------- */

/**
 * @brief   Resolve CAN_DB_SIG_* -> SIG_CAN_<Name> on signal bus
 * @brief   把 DBC 枚举名 CAN_DB_SIG_* 映射到 signal.h 里的 SIG_CAN_<Name>
 *
 * Uses the explicit table `s_dbc_to_bus` (see above).
 *
 * @param[in]  db_sig_id  CAN_DB_SIG_* enum value (>0)
 *
 * @return  signal_id_t  Matching SIG_CAN_* id, or SIG_INVALID if out of range
 */
signal_id_t CanDb_DbcSigToBus(u16 db_sig_id)
{
    if ((db_sig_id == 0u) || (db_sig_id > (u16)CAN_DB_IPK_SIG_COUNT)) {
        return SIG_INVALID;
    }
    return s_dbc_to_bus[db_sig_id - 1u];
}

/* ---------------------------------------------------------------- *
 *  Public API                                                       *
 * ---------------------------------------------------------------- */

/**
 * @brief   Decode every signal of `desc` out of `data` and publish
 *          the int32 signal-bus values via Signal_Set().
 * @brief   从 `data` 中按 `desc` 拆出每个信号, 并通过 Signal_Set
 *          发布到 int32 信号总线
 *
 * @param[in]  desc  Message descriptor (AUTOGEN, read-only)
 * @param[in]  data  8-byte payload (Intel or Motorola)
 */
void CanDb_DispatchByDb(const can_msg_desc_t *desc, const u8 *data)
{
    if ((desc == NULL) || (data == NULL)) { return; }

    const u16 sig_start = desc->sig_index;
    const u16 sig_end   = (u16)(sig_start + desc->sig_count);

    for (u16 i = sig_start; i < sig_end; i++) {
        const can_sig_desc_t *sig = &can_sig_descs_ipk[i];

        /* Decode raw -> physical, store on int32 bus. */
        const s32 physical = CanDb_DecodeSignal(data, sig);

        /* Translate DBC enum id -> signal bus id. */
        const u16 db_sig_id = (u16)(i + 1u);   /* enum is 1-based */
        const signal_id_t bus_id = CanDb_DbcSigToBus(db_sig_id);
        Signal_Set(bus_id, physical);
    }
}

/**
 * @brief   One-shot helper used during init to push the IPK test
 *          batch into the signal bus as "invalid".
 * @brief   初始化期间使用的一次性辅助函数, 把 IPK 测试批次的信号全部
 *          标记为无效
 */
void CanDb_InvalidateAllIpkSignals(void)
{
    const u16 last = (u16)CAN_DB_IPK_SIG_COUNT;
    for (u16 i = 0; i < last; i++) {
        const u16 db_sig_id = (u16)(i + 1u);
        const signal_id_t bus_id = CanDb_DbcSigToBus(db_sig_id);
        Signal_Invalidate(bus_id);
    }
}

/**
 * @brief   Find an IPK message descriptor by can_id.
 * @brief   按 can_id 查找 IPK 报文描述符
 *
 * @param[in]  can_id  Standard 11-bit can_id
 *
 * @return  const can_msg_desc_t*  Pointer into can_msg_descs_ipk[], or NULL
 */
const can_msg_desc_t *CanDb_FindIpkById(u32 can_id)
{
    for (u16 i = 0; i < CAN_DB_IPK_MSG_COUNT; i++) {
        if (can_msg_descs_ipk[i].can_id == can_id) {
            return &can_msg_descs_ipk[i];
        }
    }
    return NULL;
}

/**
 * @brief   Find an IPK signal descriptor by enum id.
 * @brief   按枚举 id 查找 IPK 信号描述符
 *
 * @param[in]  sig_id  CAN_DB_SIG_* enum value
 *
 * @return  const can_sig_desc_t*  Pointer into can_sig_descs_ipk[], or NULL
 */
const can_sig_desc_t *CanDb_FindIpkSig(u16 sig_id)
{
    if ((sig_id == 0u) || (sig_id >= (u16)CAN_DB_SIG_MAX)) {
        return NULL;
    }
    return &can_sig_descs_ipk[SIG_ID_TO_INDEX(sig_id)];
}
