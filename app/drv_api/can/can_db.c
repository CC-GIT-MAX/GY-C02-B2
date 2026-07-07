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

static const signal_id_t s_dbc_to_bus[CAN_DB_IPK_SIG_COUNT] = {    [CAN_DB_SIG_MMI_Second - 1u] = SIG_CAN_MMI_Second,
    [CAN_DB_SIG_MMI_Minute - 1u] = SIG_CAN_MMI_Minute,
    [CAN_DB_SIG_MMI_Hour - 1u] = SIG_CAN_MMI_Hour,
    [CAN_DB_SIG_MMI_Day - 1u] = SIG_CAN_MMI_Day,
    [CAN_DB_SIG_MMI_Month - 1u] = SIG_CAN_MMI_Month,
    [CAN_DB_SIG_MMI_Year - 1u] = SIG_CAN_MMI_Year,
    [CAN_DB_SIG_GPS_elevation_Info - 1u] = SIG_CAN_GPS_elevation_Info,
    [CAN_DB_SIG_MMI_GPS_Info5_AliveCounter - 1u] = SIG_CAN_MMI_GPS_Info5_AliveCounter,
    [CAN_DB_SIG_MMI_GPS_Info5_CheckSum - 1u] = SIG_CAN_MMI_GPS_Info5_CheckSum,
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
    [CAN_DB_SIG_EMS_ControlSetSpeed - 1u] = SIG_CAN_EMS_ControlSetSpeed,
    [CAN_DB_SIG_EMS_CruiseSwitchSts - 1u] = SIG_CAN_EMS_CruiseSwitchSts,
    [CAN_DB_SIG_EMS_CruiseControlSts - 1u] = SIG_CAN_EMS_CruiseControlSts,
    [CAN_DB_SIG_EMS_Real_PedalPositionInvalid - 1u] = SIG_CAN_EMS_Real_PedalPositionInvalid,
    [CAN_DB_SIG_EMS_Real_PedalPosition - 1u] = SIG_CAN_EMS_Real_PedalPosition,
    [CAN_DB_SIG_EMS_FuelPulsesRollingCounter - 1u] = SIG_CAN_EMS_FuelPulsesRollingCounter,
    [CAN_DB_SIG_EMS_EngineSpeedRPM - 1u] = SIG_CAN_EMS_EngineSpeedRPM,
    [CAN_DB_SIG_EMS_EngineSpeedRPMInvalid - 1u] = SIG_CAN_EMS_EngineSpeedRPMInvalid,
    [CAN_DB_SIG_EMS_EngStatus - 1u] = SIG_CAN_EMS_EngStatus,
    [CAN_DB_SIG_EMS_AccelPedalPosition - 1u] = SIG_CAN_EMS_AccelPedalPosition,
    [CAN_DB_SIG_EMS_AccelPedalPositionInvalid - 1u] = SIG_CAN_EMS_AccelPedalPositionInvalid,
    [CAN_DB_SIG_EMS_EngineCoolantTemperature - 1u] = SIG_CAN_EMS_EngineCoolantTemperature,
    [CAN_DB_SIG_EMS_EngineCoolantTemperatureInva - 1u] = SIG_CAN_EMS_EngineCoolantTemperatureInva,
    [CAN_DB_SIG_EMS_EngineSVSTelltale - 1u] = SIG_CAN_EMS_EngineSVSTelltale,
    [CAN_DB_SIG_EMS_EngineMILTelltale - 1u] = SIG_CAN_EMS_EngineMILTelltale,
    [CAN_DB_SIG_EMS_OilPressureWarning - 1u] = SIG_CAN_EMS_OilPressureWarning,
    [CAN_DB_SIG_EMS_Odometerbackup - 1u] = SIG_CAN_EMS_Odometerbackup,
    [CAN_DB_SIG_EMS_LIMSetSpeed - 1u] = SIG_CAN_EMS_LIMSetSpeed,
    [CAN_DB_SIG_EMS_EAV_ModSetStatus - 1u] = SIG_CAN_EMS_EAV_ModSetStatus,
    [CAN_DB_SIG_EMS_LIMControlSts - 1u] = SIG_CAN_EMS_LIMControlSts,
    [CAN_DB_SIG_EMS_LIMmemorySts_Reserved - 1u] = SIG_CAN_EMS_LIMmemorySts_Reserved,
    [CAN_DB_SIG_EMS_LIMSwitchSts - 1u] = SIG_CAN_EMS_LIMSwitchSts,
    [CAN_DB_SIG_EMS_LIMOverSpdWarningSts - 1u] = SIG_CAN_EMS_LIMOverSpdWarningSts,
    [CAN_DB_SIG_EMS_GPF_Warning - 1u] = SIG_CAN_EMS_GPF_Warning,
    [CAN_DB_SIG_EMS_BrakeOverrideSts - 1u] = SIG_CAN_EMS_BrakeOverrideSts,
    [CAN_DB_SIG_EMS_TankLeakDiagSts - 1u] = SIG_CAN_EMS_TankLeakDiagSts,
    [CAN_DB_SIG_EMS_AdaptiveTargetMode - 1u] = SIG_CAN_EMS_AdaptiveTargetMode,
    [CAN_DB_SIG_EMS_DM_ModeProgBar - 1u] = SIG_CAN_EMS_DM_ModeProgBar,
    [CAN_DB_SIG_IPK_IPKEngineTotalOdometer - 1u] = SIG_CAN_IPK_IPKEngineTotalOdometer,
    [CAN_DB_SIG_IPK_DayToEngSrv - 1u] = SIG_CAN_IPK_DayToEngSrv,
    [CAN_DB_SIG_IPK_ServiceEngineMaintainInterva - 1u] = SIG_CAN_IPK_ServiceEngineMaintainInterva,
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
