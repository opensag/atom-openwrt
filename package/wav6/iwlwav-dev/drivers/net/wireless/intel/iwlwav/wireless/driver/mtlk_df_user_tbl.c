/******************************************************************************

                               Copyright (c) 2012
                            Lantiq Deutschland GmbH

  For licensing information, see the file 'LICENSE' in the root folder of
  this software module.

******************************************************************************/
/*
 * $Id$
 *
 *
 *
 * Written by: Grygorii Strashko
 *
 */

#include "mtlkinc.h"
#include "mtlk_df_user_priv.h"
#include "mhi_umi.h"

#include <linux/wireless.h>
#include <net/cfg80211-wext.h>

/********************************************************************
 *
 * DF UI tables definitions
 *
 ********************************************************************/

#define LOG_LOCAL_GID   GID_DFUSER
#define LOG_LOCAL_FID   2

/********************************************************************
 * Private definitions
 ********************************************************************/
#define TYPE_INT     (IW_PRIV_TYPE_INT|IW_PRIV_SIZE_FIXED|1)
#define TYPE_INTVEC  (IW_PRIV_TYPE_INT|INTVEC_SIZE)
#define TYPE_ADDR    (IW_PRIV_TYPE_ADDR|IW_PRIV_SIZE_FIXED|1)
#define TYPE_ADDRVEC (IW_PRIV_TYPE_ADDR|ADDRVEC_SIZE)
#define TYPE_TEXT    (IW_PRIV_TYPE_CHAR|TEXT_SIZE)

#define GET_PARAMETER( cmd , type , name ) { cmd , 0 , type , name },
#define SET_PARAMETER( cmd , type , name ) { cmd , type , 0 , name },

#define SET_INT( cmd , name )     SET_PARAMETER( cmd , TYPE_INT     , name )
#define GET_INT( cmd , name )     GET_PARAMETER( cmd , TYPE_INT     , name )
#define SET_INTVEC( cmd , name )  SET_PARAMETER( cmd , TYPE_INTVEC  , name )
#define GET_INTVEC( cmd , name )  GET_PARAMETER( cmd , TYPE_INTVEC  , name )
#define SET_ADDR( cmd , name )    SET_PARAMETER( cmd , TYPE_ADDR    , name )
#define GET_ADDR( cmd , name )    GET_PARAMETER( cmd , TYPE_ADDR    , name )
#define SET_ADDRVEC( cmd , name ) SET_PARAMETER( cmd , TYPE_ADDRVEC , name )
#define GET_ADDRVEC( cmd , name ) GET_PARAMETER( cmd , TYPE_ADDRVEC , name )
#define SET_TEXT( cmd , name )    SET_PARAMETER( cmd , TYPE_TEXT    , name )
#define GET_TEXT( cmd , name )    GET_PARAMETER( cmd , TYPE_TEXT    , name )

/* This dummy entry is required as a separator to ensure strcmp() works over 16
 * character iwpriv names, since there is no NULL-terminator in that case.
 * The iD has zero byte on both ends to ensure termination with either endian.
 * In future, iwpriv names must not exceed 15 characters.
 */
#define _DUMMY_                   GET_PARAMETER( 0x00FFFF00, TYPE_INT, "" )

/********************************************************************
 * IW private get/set IOCTL table
 ********************************************************************/
struct iw_priv_args wave_linux_iw_core_radio_privtab[] = {

  /*---- Radio and core valid ioctles ----------------------------------------*/

  SET_ADDR    (SIOCIWFIRSTPRIV + 16                        , "sMAC"            )
  GET_ADDR    (SIOCIWFIRSTPRIV + 17                        , "gMAC"            )

  /* Sub-ioctl handlers */
  SET_INT     (SIOCIWFIRSTPRIV + 0                         , ""                )
  GET_INT     (SIOCIWFIRSTPRIV + 1                         , ""                )
  /* 2, 3 are not used */
  SET_INTVEC  (SIOCIWFIRSTPRIV + 4                         , ""                )
  GET_INTVEC  (SIOCIWFIRSTPRIV + 5                         , ""                )
  SET_ADDR    (SIOCIWFIRSTPRIV + 6                         , ""                )
  GET_ADDR    (SIOCIWFIRSTPRIV + 7                         , ""                )
  SET_TEXT    (SIOCIWFIRSTPRIV + 8                         , ""                )
  GET_TEXT    (SIOCIWFIRSTPRIV + 9                         , ""                )
  SET_ADDRVEC (SIOCIWFIRSTPRIV + 10                        , ""                )
  GET_ADDRVEC (SIOCIWFIRSTPRIV + 11                        , ""                )
  SET_INTVEC  (SIOCIWFIRSTPRIV + 30                        , ""                )
  GET_TEXT    (SIOCIWFIRSTPRIV + 31                        , ""                )

  /* WDS ioctls */
  SET_ADDR    (PRM_ID_ADD_PEERAP                           , "sAddPeerAP"      )
  SET_ADDR    (PRM_ID_DEL_PEERAP                           , "sDelPeerAP"      )
  SET_INT     (PRM_ID_PEERAP_KEY_IDX                       , "sPeerAPkeyIdx"   )
  GET_INT     (PRM_ID_PEERAP_KEY_IDX                       , "gPeerAPkeyIdx"   )
  GET_TEXT    (PRM_ID_PEERAP_LIST                          , "gPeerAPs"        )

  /* STADB watchdog int ioctles */
  SET_INT     (PRM_ID_STA_KEEPALIVE_INTERVAL               , "sStaKeepaliveIN" )
  GET_INT     (PRM_ID_STA_KEEPALIVE_INTERVAL               , "gStaKeepaliveIN" )

  /* General int ioctles */
  SET_INT     (PRM_ID_BRIDGE_MODE                          , "sBridgeMode"     )
  GET_INT     (PRM_ID_BRIDGE_MODE                          , "gBridgeMode"     )
  SET_INT     (PRM_ID_RELIABLE_MULTICAST                   , "sReliableMcast"  )
  GET_INT     (PRM_ID_RELIABLE_MULTICAST                   , "gReliableMcast"  )
  SET_INT     (PRM_ID_AP_FORWARDING                        , "sAPforwarding"   )
  GET_INT     (PRM_ID_AP_FORWARDING                        , "gAPforwarding"   )
  GET_INT     (PRM_ID_NETWORK_MODE                         , "gNetworkMode"    )
  GET_INT     (PRM_ID_CDB_CFG                              , "gCdbCfg"         )
  SET_INT     (PRM_ID_BSS_BASIC_RATE_SET                   , "sBasicRateSet"   )
  GET_INT     (PRM_ID_BSS_BASIC_RATE_SET                   , "gBasicRateSet"   )
  SET_INT     (PRM_ID_HIDDEN_SSID                          , "sHiddenSSID"     )
  GET_INT     (PRM_ID_HIDDEN_SSID                          , "gHiddenSSID"     )

  /* HSTDB int ioctles */
  SET_INT     (PRM_ID_WDS_HOST_TIMEOUT                     , "sWDSHostTO"      )
  GET_INT     (PRM_ID_WDS_HOST_TIMEOUT                     , "gWDSHostTO"      )

  /* Availabale Admission Capacity value for BSS Load IE */
  SET_INT     (PRM_ID_ADMISSION_CAPACITY                    ,"sAvailAdmCap")
  GET_INT     (PRM_ID_ADMISSION_CAPACITY                    ,"gAvailAdmCap")

  /* BSS Load IE Enable/Disable in Beacon */
  SET_INT(PRM_ID_BSS_LOAD_IN_BEACON                        , "sBssLoadInBeacon")
  _DUMMY_
  GET_INT(PRM_ID_BSS_LOAD_IN_BEACON                        , "gBssLoadInBeacon")
  _DUMMY_

  /* Set High Reception Threshold */
  SET_INT     (PRM_ID_RX_THRESHOLD                         , "sSetRxTH")
  GET_INT     (PRM_ID_RX_THRESHOLD                         , "gSetRxTH")

   /* Four address mode */
  SET_INT     (PRM_ID_4ADDR_MODE                           , "sFourAddrMode")
  GET_INT     (PRM_ID_4ADDR_MODE                           , "gFourAddrMode")
  SET_ADDR    (PRM_ID_4ADDR_STA_ADD                        , "sAddFourAddrSta")
  SET_ADDR    (PRM_ID_4ADDR_STA_DEL                        , "sDelFourAddrSta")
  GET_TEXT    (PRM_ID_4ADDR_STA_LIST                       , "gFourAddrStas"  )

  /* AMSDU on/off and control of BA agreement */
  SET_INTVEC  (PRM_ID_AGGR_CONFIG                          , "sAggrConfig")
  GET_INTVEC  (PRM_ID_AGGR_CONFIG                          , "gAggrConfig")

#if MTLK_USE_DIRECTCONNECT_DP_API
  /* DC DP LitePath int ioctles */
  SET_INT     (PRM_ID_DCDP_API_LITEPATH                    , "sLtPathEnabled"  )
  GET_INT     (PRM_ID_DCDP_API_LITEPATH                    , "gLtPathEnabled"  )
  /* For compatibility only! */
  SET_INT     (PRM_ID_DCDP_API_LITEPATH                    , "sIpxPpaEnabled"  )
  GET_INT     (PRM_ID_DCDP_API_LITEPATH                    , "gIpxPpaEnabled"  )
#endif

  /* Protection method */
  SET_INT     (PRM_ID_PROTECTION_METHOD                    , "s11nProtection"  )
  GET_INT     (PRM_ID_PROTECTION_METHOD                    , "g11nProtection"  )

  /* Last core parameter indicator */
  SET_INT     (PRM_ID_CORE_LAST                            , "")
  GET_INT     (PRM_ID_CORE_LAST                            , "")

  /*---- Radio only valid ioctles --------------------------------------------*/

  /* Sub-ioctls */
  /* MIB int ioctles */
  SET_INT     (MIB_LONG_RETRY_LIMIT                        , "sLongRetryLimit" )
  GET_INT     (MIB_LONG_RETRY_LIMIT                        , "gLongRetryLimit" )

  /* MIB text ioctles */
  SET_TEXT    (PRM_ID_COUNTRY                              , "sCountry"        )
  GET_TEXT    (PRM_ID_COUNTRY                              , "gCountry"        )

  /* 11H int ioctles */
  SET_INT     (PRM_ID_11H_RADAR_DETECTION                  , "s11hRadarDetect" )
  GET_INT     (PRM_ID_11H_RADAR_DETECTION                  , "g11hRadarDetect" )
  SET_INT     (PRM_ID_11H_BEACON_COUNT                     , "s11hBeaconCount" )
  GET_INT     (PRM_ID_11H_BEACON_COUNT                     , "g11hBeaconCount" )
  SET_INT     (PRM_ID_11H_CHANNEL_AVAILABILITY_CHECK_TIME  , "s11hChCheckTime" )
  GET_INT     (PRM_ID_11H_CHANNEL_AVAILABILITY_CHECK_TIME  , "g11hChCheckTime" )
  SET_INT     (PRM_ID_11H_NOP                              , "sNonOccupatePrd" )
  GET_INT     (PRM_ID_11H_NOP                              , "gNonOccupatePrd" )
  SET_INTVEC  (PRM_ID_11H_EMULATE_RADAR_DETECTION          , "s11hEmulatRadar" )

  /* 11D int ioctles */
  SET_INT     (PRM_ID_11D                                  , "s11dActive"      )
  GET_INT     (PRM_ID_11D                                  , "g11dActive"      )

  /* MAC watchdog int ioctles */
  SET_INT     (PRM_ID_MAC_WATCHDOG_TIMEOUT_MS              , "sMACWdTimeoutMs" )
  GET_INT     (PRM_ID_MAC_WATCHDOG_TIMEOUT_MS              , "gMACWdTimeoutMs" )
  SET_INT     (PRM_ID_MAC_WATCHDOG_PERIOD_MS               , "sMACWdPeriodMs"  )
  GET_INT     (PRM_ID_MAC_WATCHDOG_PERIOD_MS               , "gMACWdPeriodMs"  )

  /* General Radio int ioctles */
  SET_INT     (PRM_ID_UP_RESCAN_EXEMPTION_TIME             , "sUpRescanExmpTm" )
  GET_INT     (PRM_ID_UP_RESCAN_EXEMPTION_TIME             , "gUpRescanExmpTm" )

  /* General Radio intvec */
  SET_INTVEC  (PRM_ID_FW_LOG_SEVERITY                      , "sFwLogSeverity"  )

  /* EEPROM text ioctles */
  GET_TEXT    (PRM_ID_EEPROM                               , "gEEPROM"         )

#ifdef CPTCFG_IWLWAV_IRB_DEBUG
  /* IRB pinger int ioctles */
  SET_INT     (PRM_ID_IRB_PINGER_ENABLED                   , "sIRBPngEnabled"  )
  GET_INT     (PRM_ID_IRB_PINGER_ENABLED                   , "gIRBPngEnabled"  )
  SET_INT     (PRM_ID_IRB_PINGER_STATS                     , "sIRBPngStatsRst" )

  /* IRB pinger text ioctles */
  GET_TEXT    (PRM_ID_IRB_PINGER_STATS                     , "gIRBPngStats"    )
#endif

#if MTLK_USE_PUMA6_UDMA
  /* PUMA6 UDMA ioctles */
  SET_INT     (PRM_ID_UDMA_API                             , "sUdmaEnabled"    )
  GET_INT     (PRM_ID_UDMA_API                             , "gUdmaEnabled"    )
  SET_TEXT    (PRM_ID_UDMA_API_EXT                         , "sUdmaEnabledExt" )
  GET_TEXT    (PRM_ID_UDMA_API_EXT                         , "gUdmaEnabledExt" )
  SET_INT     (PRM_ID_UDMA_VLAN_ID                         , "sUdmaVlanId"     )
  GET_INT     (PRM_ID_UDMA_VLAN_ID                         , "gUdmaVlanId"     )
  SET_TEXT    (PRM_ID_UDMA_VLAN_ID_EXT                     , "sUdmaVlanIdExt"  )
  GET_TEXT    (PRM_ID_UDMA_VLAN_ID_EXT                     , "gUdmaVlanIdExt"  )
#endif

  /* COC int ioctles */
  SET_INTVEC  (PRM_ID_COC_POWER_MODE                       , "sCoCPower"       )
  GET_INTVEC  (PRM_ID_COC_POWER_MODE                       , "gCoCPower"       )
  SET_INTVEC  (PRM_ID_COC_AUTO_PARAMS                      , "sCoCAutoCfg"     )
  GET_INTVEC  (PRM_ID_COC_AUTO_PARAMS                      , "gCoCAutoCfg"     )

#ifdef CPTCFG_IWLWAV_PMCU_SUPPORT
  /* PCOC int ioctles */
  SET_INTVEC  (PRM_ID_PCOC_POWER_MODE                       , "sPCoCPower"     )
  GET_INTVEC  (PRM_ID_PCOC_POWER_MODE                       , "gPCoCPower"     )
  SET_INTVEC  (PRM_ID_PCOC_AUTO_PARAMS                      , "sPCoCAutoCfg"   )
  GET_INTVEC  (PRM_ID_PCOC_AUTO_PARAMS                      , "gPCoCAutoCfg"   )
  SET_INT     (PRM_ID_PCOC_PMCU_DEBUG                       , "sPMCUDebug"     )
#endif
  /* MBSS ioctls*/
  SET_INT     (PRM_ID_VAP_ADD                              , "sAddVap"         )
  SET_INT     (PRM_ID_VAP_DEL                              , "sDelVap"         )

  /* TPC int ioctles */
  SET_INT     (PRM_ID_TPC_LOOP_TYPE                       , "sTpcLoopType"    )
  GET_INT     (PRM_ID_TPC_LOOP_TYPE                       , "gTpcLoopType"    )

  /* 20/40 coexistence */
  /* Interference Detection */
  SET_INTVEC  (PRM_ID_INTERFER_TIMEOUTS                    , "sInterfDetTime")
  SET_INTVEC  (PRM_ID_INTERFER_THRESH                      , "sInterfDetThresh")
  _DUMMY_
  SET_INTVEC  (PRM_ID_INTERFER_SCANTIMES                   , "sInterfDetScan")
  GET_INT     (PRM_ID_INTERFERENCE_MODE                    , "gInterfDetMode")

  /* Capabilities */
  GET_INT     (PRM_ID_AP_CAPABILITIES_MAX_STAs             , "gAPCapsMaxSTAs"  )
  GET_INT     (PRM_ID_AP_CAPABILITIES_MAX_VAPs             , "gAPCapsMaxVAPs"  )

  SET_INTVEC  (PRM_ID_11B_ANTENNA_SELECTION                , "s11bAntSelection")
  _DUMMY_
  GET_INTVEC  (PRM_ID_11B_ANTENNA_SELECTION                , "g11bAntSelection")
  _DUMMY_

  SET_INTVEC  (PRM_ID_DBG_CLI                              , "sDoSimpleCLI"  )
  SET_INTVEC  (PRM_ID_FW_DEBUG                             , "sDoFwDebug"    )

  /* Traffic Analyzer */
  SET_INT     (PRM_ID_TA_TIMER_RESOLUTION                  , "sTATimerRes" )
  GET_INT     (PRM_ID_TA_TIMER_RESOLUTION                  , "gTATimerRes" )
  GET_TEXT    (PRM_ID_TA_DBG                               , "gTADbg" )

  /* FW Recovery */
  SET_INTVEC  (PRM_ID_FW_RECOVERY                          , "sFWRecovery")
  GET_INTVEC  (PRM_ID_FW_RECOVERY                          , "gFWRecovery")
  GET_INTVEC  (PRM_ID_RECOVERY_STATS                       , "gFWRecoveryStat")

  /* Scan parameters */
  SET_INTVEC  (PRM_ID_SCAN_PARAMS                          , "sScanParams")
  GET_INTVEC  (PRM_ID_SCAN_PARAMS                          , "gScanParams")

  /* Background scan parameters */
  SET_INTVEC  (PRM_ID_SCAN_PARAMS_BG                       , "sScanParamsBG")
  GET_INTVEC  (PRM_ID_SCAN_PARAMS_BG                       , "gScanParamsBG")

  /* Scan modifier flags */
  SET_INT     (PRM_ID_SCAN_MODIFS                          , "sScanModifFlags")
  GET_INT     (PRM_ID_SCAN_MODIFS                          , "gScanModifFlags")

  /* Background scan parameters */
  SET_INTVEC  (PRM_ID_CALIB_CW_MASKS                       , "sScanCalCwMasks")
  GET_INTVEC  (PRM_ID_CALIB_CW_MASKS                       , "gScanCalCwMasks")

  /* Scan result expiration time */
  SET_INT(PRM_ID_SCAN_EXP_TIME                             , "sScanExpTime")
  GET_INT(PRM_ID_SCAN_EXP_TIME                             , "gScanExpTime")

  GET_INT     (PRM_ID_GENL_FAMILY_ID                       , "gGenlFamilyId")

  /* Tasklet limits */
  SET_INTVEC  (PRM_ID_TASKLET_LIMITS                       , "sTaskletLimits")
  GET_INTVEC  (PRM_ID_TASKLET_LIMITS                       , "gTaskletLimits")

  /* Radio on/off */
  SET_INT     (PRM_ID_RADIO_MODE                           , "sEnableRadio")
  GET_INT     (PRM_ID_RADIO_MODE                           , "gEnableRadio")

  /* AMSDU NUM */
  SET_INTVEC  (PRM_ID_AMSDU_NUM                            , "sNumMsduInAmsdu")
  GET_INTVEC  (PRM_ID_AMSDU_NUM                            , "gNumMsduInAmsdu")

  /* Set Aggregation-Rate Limit */
  SET_INTVEC  (PRM_ID_AGG_RATE_LIMIT                       , "sAggRateLimit")
  GET_INTVEC  (PRM_ID_AGG_RATE_LIMIT                       , "gAggRateLimit")

  /* Reception Duty Cycle settings */
  SET_INTVEC  (PRM_ID_RX_DUTY_CYCLE                        , "sRxDutyCyc")
  GET_INTVEC  (PRM_ID_RX_DUTY_CYCLE                        , "gRxDutyCyc")

  /* Tx power upper limit */
  SET_INT     (PRM_ID_TX_POWER_LIMIT_OFFSET                , "sPowerSelection")
  GET_INT     (PRM_ID_TX_POWER_LIMIT_OFFSET                , "gPowerSelection")

  GET_INT     (PRM_ID_BEAMFORM_EXPLICIT                    , "gBfExplicitCap" )

  SET_INT     (PRM_ID_TEMPERATURE_SENSOR                   , "sCalibOnDemand")
  GET_INTVEC  (PRM_ID_TEMPERATURE_SENSOR                   , "gTemperature")

  /* Power-voltage-temperature (PVT) sensor */
  GET_INTVEC  (PRM_ID_PVT_SENSOR                           , "gPVT")

  /* Test Bus enabled */
  SET_INT     (PRM_ID_TEST_BUS                             , "sEnableTestBus")

  /* QAMplus mode */
  SET_INT     (PRM_ID_QAMPLUS_MODE                         , "sQAMplus")
  GET_INT     (PRM_ID_QAMPLUS_MODE                         , "gQAMplus")

  /* AOCS info update timeout */
  SET_INT     (PRM_ID_ACS_UPDATE_TO                        , "sAcsUpdateTo")
  GET_INT     (PRM_ID_ACS_UPDATE_TO                        , "gAcsUpdateTo")

  /* Enable/Disable MU Grouping */
  SET_INT     (PRM_ID_MU_OPERATION                         , "sMuOperation")
  GET_INT     (PRM_ID_MU_OPERATION                         , "gMuOperation")

  /* Enable/Disable HE MU Grouping */
  SET_INT     (PRM_ID_HE_MU_OPERATION                      , "sHeMuOperation")
  GET_INT     (PRM_ID_HE_MU_OPERATION                      , "gHeMuOperation")

  /* CCA Threshold */
  SET_INTVEC  (PRM_ID_CCA_THRESHOLD                        , "sCcaTh")
  GET_INTVEC  (PRM_ID_CCA_THRESHOLD                        , "gCcaTh")

  /* CCA adaptation intervals */
  SET_INTVEC  (PRM_ID_CCA_ADAPT                            , "sCcaAdapt")
  GET_INTVEC  (PRM_ID_CCA_ADAPT                            , "gCcaAdapt")

  /* Radar Detection RSSI Threshold */
  SET_INT     (PRM_ID_RADAR_RSSI_TH                        , "sRadarRssiTh")
  GET_INT     (PRM_ID_RADAR_RSSI_TH                        , "gRadarRssiTh")

#ifdef CPTCFG_IWLWAV_SET_PM_QOS
  /* Change CPU DMA latency */
  SET_INT     (PRM_ID_CPU_DMA_LATENCY                      , "sCpuDmaLatency"  )
  GET_INT     (PRM_ID_CPU_DMA_LATENCY                      , "gCpuDmaLatency"  )
#endif

  /* RTS mode */
  SET_INTVEC  (PRM_ID_RTS_MODE                             , "sRTSmode")
  GET_INTVEC  (PRM_ID_RTS_MODE                             , "gRTSmode")

  /* Max MPDU length */
  SET_INT     (PRM_ID_MAX_MPDU_LENGTH                      , "sMaxMpduLen"     )
  GET_INT     (PRM_ID_MAX_MPDU_LENGTH                      , "gMaxMpduLen"     )

  /* Beamforming mode */
  SET_INT     (PRM_ID_BF_MODE                              , "sBfMode")
  GET_INT     (PRM_ID_BF_MODE                              , "gBfMode")

  /* Fixed Rate */
  SET_INTVEC  (PRM_ID_FIXED_RATE                           , "sFixedRateCfg")

  /* Active Antenna mask */
  GET_INT     (PRM_ID_ACTIVE_ANT_MASK                      , "gActiveAntMask"  )
  SET_INT     (PRM_ID_ACTIVE_ANT_MASK                      , "sActiveAntMask"  )

  /* IRE Control B */
  SET_INT     (PRM_ID_IRE_CTRL_B                           , "sCtrlBFilterBank")
  _DUMMY_
  GET_INT     (PRM_ID_IRE_CTRL_B                           , "gCtrlBFilterBank")
  _DUMMY_

  /* Static Planer config */
  SET_INTVEC  (PRM_ID_STATIC_PLAN_CONFIG                   , "sMuStatPlanCfg")

  SET_INTVEC  (PRM_ID_TXOP_CONFIG                          , "sTxopConfig")
  GET_INTVEC  (PRM_ID_TXOP_CONFIG                          , "gTxopConfig")

  /* SSB mode */
  SET_INTVEC  (PRM_ID_SSB_MODE                             , "sSsbMode")
  GET_INTVEC  (PRM_ID_SSB_MODE                             , "gSsbMode")

  /* 2.4 GHz Coex */
  SET_INT     (PRM_ID_COEX_ENABLE                          , "sEnableMRCoex")
  GET_INT     (PRM_ID_COEX_ENABLE                          , "gEnableMRCoex")
  SET_INT     (PRM_ID_COEX_CFG                             , "sConfigMRCoex")
  GET_INT     (PRM_ID_COEX_CFG                             , "gConfigMRCoex")

  /* Slow probing mask */
  SET_INT     (PRM_ID_PROBING_MASK                         , "sSlowProbingMask")
  _DUMMY_
  GET_INT     (PRM_ID_PROBING_MASK                         , "gSlowProbingMask")
  _DUMMY_

  /* Multicast handling commands */
  SET_TEXT    (PRM_ID_MCAST_RANGE_SETUP                    , "sMcastRange")
  GET_TEXT    (PRM_ID_MCAST_RANGE_SETUP                    , "gMcastRange")
  SET_TEXT    (PRM_ID_MCAST_RANGE_SETUP_IPV6               , "sMcastRange6")
  GET_TEXT    (PRM_ID_MCAST_RANGE_SETUP_IPV6               , "gMcastRange6")

  SET_INT     (PRM_ID_ONLINE_CALIBRATION_ALGO_MASK         , "sOnlineACM"      )
  GET_INT     (PRM_ID_ONLINE_CALIBRATION_ALGO_MASK         , "gOnlineACM"      )
  SET_INT     (PRM_ID_CALIBRATION_ALGO_MASK                , "sAlgoCalibrMask" )
  GET_INT     (PRM_ID_CALIBRATION_ALGO_MASK                , "gAlgoCalibrMask" )
  SET_INT     (PRM_ID_USE_SHORT_CYCLIC_PREFIX              , "sShortCyclcPrfx" )
  GET_INT     (PRM_ID_USE_SHORT_CYCLIC_PREFIX              , "gShortCyclcPrfx" )

  /* Frequency Jump mode */
  SET_INT     (PRM_ID_FREQ_JUMP_MODE                       , "sFreqJumpMode")

  /* Control (change to a fixed value) TX beacon and management power on the fly */
  SET_INTVEC  (PRM_ID_FIXED_POWER                          , "sFixedPower")
  GET_INTVEC  (PRM_ID_FIXED_POWER                          , "gFixedPower")

  /* Unconnected STA scan time */
  SET_INT     (PRM_ID_UNCONNECTED_STA_SCAN_TIME            , "sUnconnTime")
  GET_INT     (PRM_ID_UNCONNECTED_STA_SCAN_TIME            , "gUnconnTime")

  /* Restricted AC mode */
  SET_INTVEC  (PRM_ID_RESTRICTED_AC_MODE                   , "sRestrictAcMode")
  GET_INTVEC  (PRM_ID_RESTRICTED_AC_MODE                   , "gRestrictAcMode")

  /* PD Threshold */
  SET_INTVEC  (PRM_ID_PD_THRESHOLD                         , "sPdThresh")
  GET_INTVEC  (PRM_ID_PD_THRESHOLD                         , "gPdThresh")

  /* Fast Drop */
  SET_INT     (PRM_ID_FAST_DROP                            , "sFastDrop")
  GET_INT     (PRM_ID_FAST_DROP                            , "gFastDrop")

  /* Fixed LTF and GI */
  SET_INTVEC  (PRM_ID_FIXED_LTF_AND_GI                     , "sFixedLtfGi")
  GET_INTVEC  (PRM_ID_FIXED_LTF_AND_GI                     , "gFixedLtfGi")

  /* Effective radiated power */
  SET_INTVEC  (PRM_ID_ERP                                  , "sErpSet")

  /* Dynamic Multicast Rate */
  SET_INT     (PRM_ID_FAST_DYNAMIC_MC_RATE                 , "sDmrConfig")
  GET_INT     (PRM_ID_FAST_DYNAMIC_MC_RATE                 , "gDmrConfig")

  /* Select WLAN counters source */
  SET_INT     (PRM_ID_SWITCH_COUNTERS_SRC                  , "sCountersSrc"    )
  GET_INT     (PRM_ID_SWITCH_COUNTERS_SRC                  , "gCountersSrc"    )

  /* NFRP Configuration */
  SET_INTVEC  (PRM_ID_NFRP_CFG                             , "sNfrpCfg"        )
  
  /* RTS Protection Rate Configuration */
  SET_INT     (PRM_ID_RTS_RATE                             , "sRtsRate"    )
  GET_INT     (PRM_ID_RTS_RATE                             , "gRtsRate"    )
};

/********************************************************************
 * IW private IOCTL handlers table
 ********************************************************************/
static const iw_handler mtlk_linux_private_handler[] = {
  [ 0] = mtlk_df_ui_linux_ioctl_set_int,
  [ 1] = mtlk_df_ui_linux_ioctl_get_int,
  [ 4] = mtlk_df_ui_linux_ioctl_set_intvec,
  [ 5] = mtlk_df_ui_linux_ioctl_get_intvec,
  [ 6] = mtlk_df_ui_linux_ioctl_set_addr,
  [ 7] = mtlk_df_ui_linux_ioctl_get_addr,
  [ 8] = mtlk_df_ui_linux_ioctl_set_text,
  [ 9] = mtlk_df_ui_linux_ioctl_get_text,
  [10] = mtlk_df_ui_linux_ioctl_set_addrvec,
  [11] = mtlk_df_ui_linux_ioctl_get_addrvec,
  [16] = mtlk_df_ui_linux_ioctl_set_mac_addr,
  [17] = mtlk_df_ui_linux_ioctl_get_mac_addr,
  [20] = mtlk_df_ui_iw_bcl_mac_data_get,
  [21] = mtlk_df_ui_iw_bcl_mac_data_set,
  [22] = mtlk_df_ui_linux_ioctl_bcl_drv_data_exchange,
  [26] = mtlk_df_ui_linux_ioctl_stop_lower_mac,
  [27] = NULL,
  [28] = mtlk_df_ui_linux_ioctl_iw_generic,
};

/********************************************************************
 * IW driver IOCTL descriptor table
 ********************************************************************/
/* FIXCFG80211: Implement all commented functions to support IW */
static const iw_handler _cfg80211_handlers[] = {
  [IW_IOCTL_IDX(SIOCSIWCOMMIT)] = (iw_handler) NULL,

  /* use own handler instead of the provided by wext-compat.c */
  [IW_IOCTL_IDX(SIOCGIWNAME)]  = (iw_handler) mtlk_df_ui_linux_ioctl_getname,   /* wext-compat.c: cfg80211_wext_giwname  */
  [IW_IOCTL_IDX(SIOCGIWRANGE)] = (iw_handler) mtlk_df_ui_linux_ioctl_getrange,  /* wext-compat.c: cfg80211_wext_giwrange */

  [IW_IOCTL_IDX(SIOCSIWFREQ)] = (iw_handler) mtlk_df_ui_linux_ioctl_setfreq,
  [IW_IOCTL_IDX(SIOCGIWFREQ)] = (iw_handler) mtlk_df_ui_linux_ioctl_getfreq,
  [IW_IOCTL_IDX(SIOCSIWMODE)] = (iw_handler) cfg80211_wext_siwmode,
  [IW_IOCTL_IDX(SIOCGIWMODE)] = (iw_handler) cfg80211_wext_giwmode,
  [IW_IOCTL_IDX(SIOCSIWAP)] = (iw_handler) mtlk_df_ui_linux_ioctl_setap,
  [IW_IOCTL_IDX(SIOCGIWAP)] = (iw_handler) mtlk_df_ui_linux_ioctl_getap,
  [IW_IOCTL_IDX(SIOCSIWMLME)] = (iw_handler) mtlk_df_ui_linux_ioctl_setmlme,
#ifdef MTLK_LEGACY_STATISTICS
  [IW_IOCTL_IDX(SIOCGIWAPLIST)] = (iw_handler) mtlk_df_ui_linux_ioctl_getaplist,
#endif
  [IW_IOCTL_IDX(SIOCSIWSCAN)] = (iw_handler) cfg80211_wext_siwscan,
  [IW_IOCTL_IDX(SIOCGIWSCAN)] = (iw_handler) cfg80211_wext_giwscan,
  [IW_IOCTL_IDX(SIOCSIWESSID)] = (iw_handler) mtlk_df_ui_linux_ioctl_setessid,
  [IW_IOCTL_IDX(SIOCGIWESSID)] = (iw_handler) mtlk_df_ui_linux_ioctl_getessid,
  [IW_IOCTL_IDX(SIOCSIWNICKN)] = (iw_handler) mtlk_df_ui_linux_ioctl_setnick,
  [IW_IOCTL_IDX(SIOCGIWNICKN)] = (iw_handler) mtlk_df_ui_linux_ioctl_getnick,
  [IW_IOCTL_IDX(SIOCSIWRATE)] = (iw_handler) NULL,
  [IW_IOCTL_IDX(SIOCGIWRATE)] = (iw_handler) NULL,
  [IW_IOCTL_IDX(SIOCSIWRTS)] = (iw_handler) cfg80211_wext_siwrts,
  [IW_IOCTL_IDX(SIOCGIWRTS)] = (iw_handler) cfg80211_wext_giwrts,
  [IW_IOCTL_IDX(SIOCSIWFRAG)] = (iw_handler) cfg80211_wext_siwfrag,
  [IW_IOCTL_IDX(SIOCGIWFRAG)] = (iw_handler) cfg80211_wext_giwfrag,
  [IW_IOCTL_IDX(SIOCSIWTXPOW)] = (iw_handler) NULL,
  [IW_IOCTL_IDX(SIOCGIWTXPOW)] = (iw_handler) mtlk_df_ui_linux_ioctl_gettxpower,
  [IW_IOCTL_IDX(SIOCSIWRETRY)] = (iw_handler) mtlk_df_ui_linux_ioctl_setretry,
  [IW_IOCTL_IDX(SIOCGIWRETRY)] = (iw_handler) cfg80211_wext_giwretry,
  [IW_IOCTL_IDX(SIOCSIWENCODE)] = (iw_handler) mtlk_df_ui_linux_ioctl_setenc,
  [IW_IOCTL_IDX(SIOCGIWENCODE)] = (iw_handler) mtlk_df_ui_linux_ioctl_getenc,
  [IW_IOCTL_IDX(SIOCSIWPOWER)] = (iw_handler) NULL,
  [IW_IOCTL_IDX(SIOCGIWPOWER)] = (iw_handler) NULL,
  [IW_IOCTL_IDX(SIOCSIWGENIE)] = (iw_handler) NULL,
  [IW_IOCTL_IDX(SIOCSIWAUTH)] = (iw_handler) mtlk_df_ui_linux_ioctl_setauth,
  [IW_IOCTL_IDX(SIOCGIWAUTH)] = NULL,
  [IW_IOCTL_IDX(SIOCSIWENCODEEXT)] = NULL,
  [IW_IOCTL_IDX(SIOCGIWENCODEEXT)] = NULL,
  [IW_IOCTL_IDX(SIOCSIWPMKSA)] = (iw_handler) NULL,
};

struct iw_handler_def wave_linux_iw_radio_handler_def = {
  .num_standard = ARRAY_SIZE(_cfg80211_handlers),
  .num_private = ARRAY_SIZE(mtlk_linux_private_handler),
  .num_private_args = ARRAY_SIZE(wave_linux_iw_core_radio_privtab),
  .standard = (iw_handler *) _cfg80211_handlers,
  .private = (iw_handler *) mtlk_linux_private_handler,
  .private_args = (struct iw_priv_args *) wave_linux_iw_core_radio_privtab,
  .get_wireless_stats = mtlk_df_ui_linux_ioctl_get_iw_stats,
};

struct iw_handler_def wave_linux_iw_core_handler_def = {
  .num_standard = ARRAY_SIZE(_cfg80211_handlers),
  .num_private = ARRAY_SIZE(mtlk_linux_private_handler),
  .num_private_args = 0,
  .standard = (iw_handler *) _cfg80211_handlers,
  .private = (iw_handler *) mtlk_linux_private_handler,
  .private_args = (struct iw_priv_args *) wave_linux_iw_core_radio_privtab,
  .get_wireless_stats = mtlk_df_ui_linux_ioctl_get_iw_stats,
};

char *
wave_df_get_cmd_name (uint32 id, BOOL is_setter)
{
    const struct iw_priv_args *args;
    char  *name = "";
    uint32 i;

    args = wave_linux_iw_core_radio_privtab;
    for (i = 0; args && (i < ARRAY_SIZE(wave_linux_iw_core_radio_privtab)); i++, args++) {
        if (id == args->cmd) {
            if ((is_setter && args->set_args) ||
                (!is_setter && args->get_args)) {
                    name = (char *)args->name;
                    break;
            }
        }
    }

    return name;
}

int
wave_df_get_core_privtab_size (void)
{
    const struct iw_priv_args *args;
    uint32 i;

    args = wave_linux_iw_core_radio_privtab;
    for (i = 0; (i < ARRAY_SIZE(wave_linux_iw_core_radio_privtab)); i++, args++) {
      if(args->cmd == PRM_ID_CORE_LAST)
        break;
    }
    ILOG3_D("Core privtab size is:%d", i);
    return i;
}
