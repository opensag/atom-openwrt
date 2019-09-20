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
*/

#ifndef __MTLK_COREUI_H__
#define __MTLK_COREUI_H__

#include "mtlkerr.h"
#include "txmm.h"
#include "channels.h"
#include "mtlkqos.h"
#include "bitrate.h"
#include "dataex.h"
#include "mtlkdfdefs.h"
#include "mtlk_ab_manager.h"
#include "mtlk_coc.h"
#ifdef CPTCFG_IWLWAV_PMCU_SUPPORT
#include "mtlk_pcoc.h"
#endif /*CPTCFG_IWLWAV_PMCU_SUPPORT*/
#include "ta.h"
#include "fw_recovery.h"
/* todo: check if can be removed */
#include "mtlkwssa_drvinfo.h"
#include "intel_vendor_shared.h"

#include <uapi/linux/nl80211.h> /* for conversions from NL80211 to our values */
#include <net/cfg80211.h> /* for conversions from ieee80211 to our values */

#define   MTLK_IDEFS_ON
#include "mtlkidefs.h"

#define LOG_LOCAL_GID   GID_DFUSER
#define LOG_LOCAL_FID   3


/**
*\file mtlk_coreui.h

*\brief Core interface for DF UI module

*\defgroup COREUI Core interface for DF UI
*\{

*/

#define MTLK_CHANNEL_NOT_USED 0
#define MTLK_ESSID_MAX_SIZE   (IEEE80211_MAX_SSID_LEN + 1)
#define COUNTRY_CODE_DFLT     "US"

#define WAVE_RADIO_OFF 0
#define WAVE_RADIO_ON  1

#define MTLK_MIN_RBM 0x01
#define MTLK_MAX_RBM 0xFF

/* FIXME: Should be defined in FW shared headers */
#define GEN5_ALL_STA_SID        0x0FF   /* 2*GEN5_MAX_SID - 1 */
#define GEN6_ALL_STA_SID        0x1FF   /* 2*GEN6_MAX_SID - 1 */
#define ALL_SID_MAX             MAX(GEN5_ALL_STA_SID, GEN6_ALL_STA_SID)

#define MTLK_INTERFDET_THRESHOLD_INVALID 1

#define MTLK_PARAM_DB_INVALID_UINT8  MAX_UINT8
#define MTLK_PARAM_DB_INVALID_UINT16 MAX_UINT16
#define MTLK_PARAM_DB_INVALID_UINT32 MAX_UINT32

/* Param DB supports values of size up to uint32. Thus INVALID_UINT32 will be used for uint64 too */
#define MTLK_PARAM_DB_VALUE_IS_INVALID(param)                                       \
        ((sizeof(param) == sizeof(uint8)  ? (((uint8)  param) == MTLK_PARAM_DB_INVALID_UINT8)  : \
         (sizeof(param) == sizeof(uint16) ? (((uint16) param) == MTLK_PARAM_DB_INVALID_UINT16) : \
       /* both sizes uint32 and uint64 */   (((uint32) param) == MTLK_PARAM_DB_INVALID_UINT32))))

/* adjust according to maximum SID supported by available platforms */
#define WAVE_MAX_SID                MAX(GEN5_MAX_SID, GEN6_MAX_SID)

void __MTLK_IFUNC
mtlk_core_handle_tx_data(mtlk_vap_handle_t vap_handle, mtlk_nbuf_t *nbuf);

void __MTLK_IFUNC
mtlk_core_handle_tx_ctrl(mtlk_vap_handle_t    vap_handle,
                         mtlk_user_request_t *req,
                         uint32               id,
                         mtlk_clpb_t         *data);

typedef enum
{
  WAVE_CORE_REQ_ACTIVATE_OPEN = MTLK_CORE_ABILITIES_START, /*!< Activate the core for AP */
  WAVE_CORE_REQ_SET_BEACON,          /*!< Beacon data */
  WAVE_CORE_REQ_SET_BEACON_INTERVAL, /*!< Set Peer AP Beacon interval (used for sta interface) */
  WAVE_CORE_REQ_REQUEST_SID,         /*!< Request SID */
  WAVE_CORE_REQ_REMOVE_SID,          /*!< Remvoe SID */
  WAVE_CORE_REQ_SYNC_DONE,           /*!< Recovery sync done */
  WAVE_CORE_REQ_SET_PROTECTION_MODE, /*!< Protection mode */
  WAVE_CORE_REQ_SET_SHORT_PREAMBLE,  /*!< Short preamble */
  WAVE_CORE_REQ_SET_SHORT_SLOT_TIME, /*!< Short slot time */
  WAVE_CORE_REQ_GET_STATION,         /*!< Get station */
  WAVE_CORE_REQ_SET_WMM_PARAM,       /*!< WMM parameters */
  WAVE_CORE_REQ_MGMT_TX,             /*!< Management packet on TX */
  WAVE_CORE_REQ_MGMT_RX,             /*!< Management packet on RX */
  WAVE_CORE_REQ_CONNECT_STA,         /*!< Connect and activate the core for STA */
#ifdef MTLK_LEGACY_STATISTICS
  WAVE_CORE_REQ_DISCONNECT_STA,      /*!< Disconnect for STA */
#endif
  WAVE_CORE_REQ_AP_CONNECT_STA,      /*!< Connect STA to AP */
  WAVE_CORE_REQ_AP_DISCONNECT_STA,   /*!< Disconnect STA for AP */
  WAVE_CORE_REQ_AP_DISCONNECT_ALL,   /*!< Disconnect all for AP */
  WAVE_CORE_REQ_AUTHORIZING_STA,     /*!< Authorizing STA */
  WAVE_CORE_REQ_DEACTIVATE,          /*!< Deactivate the core    */
  WAVE_CORE_REQ_SET_MAC_ADDR,        /*!< Assign MAC address     */
  WAVE_CORE_REQ_GET_MAC_ADDR,        /*!< Query MAC address      */
  WAVE_CORE_REQ_GET_STATUS,          /*!< Get core status & statistics    */
  WAVE_CORE_REQ_RESET_STATS,         /*!< Reset core statistics  */
  WAVE_CORE_REQ_SET_WDS_CFG,         /*!< Process WDS Configuration */
  WAVE_CORE_REQ_GET_WDS_CFG,         /*!< Process WDS Configuration */
  WAVE_CORE_REQ_GET_WDS_PEERAP,      /*!< Process WDS Configuration */
  WAVE_CORE_REQ_GET_STADB_CFG,       /*!< Process STADB */
  WAVE_CORE_REQ_SET_STADB_CFG,       /*!< Process STADB */
  WAVE_CORE_REQ_GET_CORE_CFG,        /*!< Process Core */
  WAVE_CORE_REQ_SET_CORE_CFG,        /*!< Process Core */
  WAVE_CORE_REQ_GET_HSTDB_CFG,       /*!< Process HSTDB */
  WAVE_CORE_REQ_SET_HSTDB_CFG,       /*!< Process HSTDB */
  WAVE_CORE_REQ_GEN_DATA_EXCHANGE,     /*!< Gen data exchange */
  WAVE_CORE_REQ_GET_IW_GENERIC,        /*!< IW Generic */
  WAVE_CORE_REQ_GET_EE_CAPS,           /*!< Get EE Caps */
  WAVE_CORE_REQ_GET_SQ_STATUS,         /*!< Get SQ status and statistic information */
  WAVE_CORE_REQ_GET_MC_IGMP_TBL,       /*!< Get IGMP MCAST table */
  WAVE_CORE_REQ_GET_MC_HW_TBL,         /*!< Get MCAST HW table */
  WAVE_CORE_REQ_GET_RANGE_INFO,        /*!< Get supported bitrates and channels info*/
  WAVE_CORE_REQ_GET_STADB_STATUS,      /*!< Get status&statistic data from STADB (list)*/
  WAVE_CORE_REQ_GET_STADB_STA_BY_ITER_ID,       /*!< Get station info from STADB (one station)*/
  WAVE_CORE_REQ_GET_HS20_INFO,                  /*!< Get status&statistic data related to Hotspot 2.0 */
  WAVE_CORE_REQ_SET_WDS_WEP_ENC_CFG,            /*!< Configure WDS WEP encryption */
  WAVE_CORE_REQ_GET_WDS_WEP_ENC_CFG,            /*!< Get WDS_WEP encryption configuration */
  WAVE_CORE_REQ_SET_AUTH_CFG,                   /*!< Configure authentication parameters */
  WAVE_CORE_REQ_GET_AUTH_CFG,                   /*!< Get authentication parameters */
  WAVE_CORE_REQ_GET_ENCEXT_CFG,                 /*!< Get Extended encoding data structure */
  WAVE_CORE_REQ_SET_ENCEXT_CFG,                 /*!< Set Extended encoding parmeters */
  WAVE_CORE_REQ_SET_DEFAULT_KEY_CFG,            /*!< Set default key parmeters */
  WAVE_CORE_REQ_GET_SERIALIZER_INFO,            /*!< Get Serializer Info */
  WAVE_CORE_REQ_SET_COEX_20_40_MODE_CFG,        /*!< Set Coex 20/40 mode configuration */
  WAVE_CORE_REQ_GET_COEX_20_40_MODE_CFG,        /*!< Get Coex 20/40 mode configuration */
  WAVE_CORE_REQ_SET_COEX_20_40_AP_FORCE_PARAMS_CFG,       /*!< Set Coex 20/40 ap force params configuration */
  WAVE_CORE_REQ_GET_COEX_20_40_AP_FORCE_PARAMS_CFG,       /*!< Get Coex 20/40 ap force params configuration */
  WAVE_CORE_REQ_SET_COEX_20_40_STA_EXEMPTION_REQ_CFG,     /*!< Set Coex 20/40 exemption request configuration */
  WAVE_CORE_REQ_GET_COEX_20_40_STA_EXEMPTION_REQ_CFG,     /*!< Get Coex 20/40 exemption request configuration */
  WAVE_CORE_REQ_SET_COEX_20_40_AP_MIN_NUM_OF_EXM_STA_CFG, /*!< Set Coex 20/40 minimum STAs to not exempt configuration */
  WAVE_CORE_REQ_GET_COEX_20_40_AP_MIN_NUM_OF_EXM_STA_CFG, /*!< Get Coex 20/40 minimum STAs to not exempt configuration */
  WAVE_CORE_REQ_SET_COEX_20_40_TIMES_CFG,       /*!< Set Coex 20/40 times configuration */
  WAVE_CORE_REQ_GET_COEX_20_40_TIMES_CFG,       /*!< Get Coex 20/40 times configuration */
  WAVE_CORE_REQ_GET_PS_STATUS,                  /*!< Get PS status */
  WAVE_CORE_REQ_CHANGE_BSS,                     /*!< Change BSS */
  WAVE_CORE_REQ_SET_RTLOG_CFG,                  /*!< Set RT-Logger cfg */
  MTLK_CORE_REQ_MCAST_HELPER_GROUP_ID_ACTION,   /*!< Multicast group id action */
  WAVE_CORE_REQ_GET_RX_TH,                      /*!< Get last High Reception Threshold */
  WAVE_CORE_REQ_SET_RX_TH,                      /*!< Set High Reception Threshold */
  WAVE_CORE_REQ_SET_QOS_MAP,                    /*!< Set QoS map */
  WAVE_CORE_REQ_SET_AGGR_CONFIG,                /*!< Set Aggregation config */
  WAVE_CORE_REQ_GET_AGGR_CONFIG,                /*!< Get Aggregation config */
  WAVE_CORE_REQ_SET_BLACKLIST_ENTRY,            /*!< Set blacklist entry */
  WAVE_CORE_REQ_GET_BLACKLIST_ENTRIES,          /*!< Get blacklist entries */
  WAVE_CORE_REQ_GET_MULTI_AP_BL_ENTRIES,        /*!< Get Multi-AP blacklist entries */
  WAVE_CORE_REQ_GET_STATION_MEASUREMENTS,       /*!< Get station measurements */
  WAVE_CORE_REQ_GET_VAP_MEASUREMENTS,           /*!< Get VAP measurements */
  WAVE_CORE_REQ_GET_UNCONNECTED_STATION,        /*!< Get unconnected station (currently only RSSI) */
  WAVE_CORE_REQ_SET_FOUR_ADDR_CFG,              /*!< Set four address configuration */
  WAVE_CORE_REQ_GET_FOUR_ADDR_CFG,              /*!< Get four address configuration */
  WAVE_CORE_REQ_GET_FOUR_ADDR_STA_LIST,         /*!< Get four address station list */
  WAVE_CORE_REQ_SET_ATF_QUOTAS,                 /*!< Set ATF quotas per station and per VAP */
  WAVE_CORE_REQ_SET_MCAST_RANGE,                /*!< Set mcast range (for both IPv4 and IPv6) */
  WAVE_CORE_REQ_GET_MCAST_RANGE_IPV4,           /*!< Get mcast range (IPv4) */
  WAVE_CORE_REQ_GET_MCAST_RANGE_IPV6,           /*!< Get mcast range (IPv6) */
  WAVE_CORE_REQ_SET_OPERATING_MODE,
  WAVE_CORE_REQ_SET_WDS_WPA_LIST_ENTRY,         /*!< Set WDS WPA list entry */
  WAVE_CORE_REQ_GET_WDS_WPA_LIST_ENTRIES,       /*!< Get WDS WPA list entries */
  WAVE_CORE_REQ_SET_DGAF_DISABLED,              /*!< Set dgaf_disabled */
  WAVE_CORE_REQ_CHECK_4ADDR_MODE,               /*!< Check 4-addr STA mode */
  WAVE_CORE_REQ_GET_TX_POWER,                   /*!< Get Tx Power */
  WAVE_CORE_REQ_SET_HT_PROTECTION,              /*!< Set last HT protection mode */
  WAVE_CORE_REQ_GET_HT_PROTECTION,              /*!< Get HT protection mode */
  WAVE_CORE_REQ_RCVRY_MSG_TX,                   /*!< FAPI packet on TX */
  WAVE_CORE_REQ_GET_NETWORK_MODE,               /*!< Get Network Mode */
  WAVE_CORE_REQ_SET_SOFTBLOCKLIST_ENTRY,        /*!< Set SoftBlock Thresholds */
  /* WAVE_CORE_REQ_SET_AOCS_CHANNELS_TBL_DBG, */
  /* WAVE_CORE_REQ_SET_AOCS_CL, */
  WAVE_RADIO_REQ_GET_RADIO_INFO,                 /*!< Get radio info */
  WAVE_RADIO_REQ_START_SCANNING,      /*!< Start scanning */
  WAVE_RADIO_REQ_GET_SCANNING_RES,    /*!< Get scanning results */
  WAVE_RADIO_REQ_GET_AOCS_CFG,        /*!< Process request to AOCS */
  WAVE_RADIO_REQ_SET_AOCS_CFG,        /*!< Process request to AOCS */
  WAVE_RADIO_REQ_GET_AOCS_TBL,        /*!< Get AOCS table */
  WAVE_RADIO_REQ_GET_AOCS_CHANNELS_TBL, /*!< Get AOCS channels table */
  WAVE_RADIO_REQ_GET_AOCS_HISTORY,      /*!< Get AOCS history */
  WAVE_RADIO_REQ_GET_AOCS_PENALTIES,    /*!< Get AOCS penalties table */
  WAVE_RADIO_REQ_STOP_LM,               /*!< Stop Lower MAC */
  WAVE_RADIO_REQ_SET_DBG_CLI,                   /*!< Send debug data to FW */
  WAVE_RADIO_REQ_SET_FW_LOG_SEVERITY,           /*!< Set FW Log Severity */
  WAVE_RADIO_REQ_SET_CHAN,                      /*!< Set channel */
  WAVE_RADIO_REQ_DO_SCAN,                       /*!< Do scan */
  WAVE_RADIO_REQ_START_SCHED_SCAN,              /*!< Start scheduled scan */
  WAVE_RADIO_REQ_STOP_SCHED_SCAN,               /*!< Stop scheduled scan */
  WAVE_RADIO_REQ_SCAN_TIMEOUT,                  /*!< Handle scan timeout */
  WAVE_RADIO_REQ_ALTER_SCAN,                    /*!< Alter current scan: abort/pause/resume */
  WAVE_RADIO_REQ_DUMP_SURVEY,                   /*!< Dump survey */
  WAVE_RADIO_REQ_FIN_PREV_FW_SC,                /*!< Finish and prevent FW set-channel process */
#ifdef MTLK_LEGACY_STATISTICS
  WAVE_RADIO_REQ_GET_MHI_STATS,                 /*!< Process MHI statistic */
#endif
  WAVE_RADIO_REQ_GET_PHY_RX_STATUS,             /*!< Process Phy Rx status */
  WAVE_RADIO_REQ_GET_HW_LIMITS,                 /*!< Get HW Limits */
  WAVE_RADIO_REQ_SET_MAC_ASSERT,                /*!< Perform MAC assert */
  WAVE_RADIO_REQ_GET_BCL_MAC_DATA,              /*!< Get BCL MAC data */
  WAVE_RADIO_REQ_SET_BCL_MAC_DATA,              /*!< Get BCL MAC data */
  WAVE_RADIO_REQ_SET_FW_DEBUG,                  /*!< Send debug data to FW */
  WAVE_RADIO_REQ_SET_RECOVERY_CFG,              /*!< Set FW Recovery configuration */
  WAVE_RADIO_REQ_GET_RECOVERY_CFG,              /*!< Get FW Recovery configuration */
  WAVE_RADIO_REQ_GET_RECOVERY_STATS,            /*!< Get FW Recovery statistics */
  WAVE_RADIO_REQ_GET_TX_RATE_POWER,             /*!< Get Tx Rate Power */
#ifdef CPTCFG_IWLWAV_SET_PM_QOS
  WAVE_RADIO_REQ_SET_CPU_DMA_LATENCY,           /*!< Set CPU DMA latency */
  WAVE_RADIO_REQ_GET_CPU_DMA_LATENCY,           /*!< Get CPU DMA latency */
#endif
  WAVE_RADIO_SET_STATIC_PLAN,                   /*!< Set Static Planner */
  WAVE_RADIO_REQ_GET_DOT11H_AP_CFG,             /*!< Process request to DOT11H on AP */
  WAVE_RADIO_REQ_SET_DOT11H_AP_CFG,             /*!< Process request to DOT11H on AP */
  WAVE_RADIO_REQ_GET_MIBS_CFG,                  /*!< Process MIBs */
  WAVE_RADIO_REQ_SET_MIBS_CFG,                  /*!< Process MIBs */
  WAVE_RADIO_REQ_GET_COUNTRY_CFG,               /*!< Process MIBs */
  WAVE_RADIO_REQ_SET_COUNTRY_CFG,               /*!< Process MIBs */
  WAVE_RADIO_REQ_GET_DOT11D_CFG,                /*!< Process DOT11D */
  WAVE_RADIO_REQ_SET_DOT11D_CFG,                /*!< Process DOT11D */
  WAVE_RADIO_REQ_GET_MAC_WATCHDOG_CFG,          /*!< Process MAC watchdog */
  WAVE_RADIO_REQ_SET_MAC_WATCHDOG_CFG,          /*!< Process MAC watchdog */
  WAVE_RADIO_REQ_GET_MASTER_CFG,                /*!< Process Core */
  WAVE_RADIO_REQ_SET_MASTER_CFG,                /*!< Process Core */
  WAVE_RADIO_REQ_GET_COC_CFG,                   /*!< Process COC */
  WAVE_RADIO_REQ_SET_COC_CFG,                   /*!< Process COC */
  WAVE_RADIO_REQ_GET_PCOC_CFG,                  /*!< Process PCOC */
  WAVE_RADIO_REQ_SET_PCOC_CFG,                  /*!< Process PCOC */
  WAVE_RADIO_REQ_GET_TPC_CFG,                   /*!< TPC config */
  WAVE_RADIO_REQ_SET_TPC_CFG,                   /*!< TPC config */
  WAVE_RADIO_REQ_MBSS_ADD_VAP_IDX,              /*!< MBSS AddVap issued by Master */
  WAVE_RADIO_REQ_MBSS_ADD_VAP_NAME,             /*!< MBSS AddVap issued by Master */
  WAVE_RADIO_REQ_MBSS_DEL_VAP_IDX,              /*!< MBSS DelVap issued by Master */
  WAVE_RADIO_REQ_MBSS_DEL_VAP_NAME,             /*!< MBSS DelVap issued by Master */
  WAVE_RADIO_REQ_SET_INTERFDET_PARAMS_CFG,      /*!< Set Interference Detection configuration */
  WAVE_RADIO_REQ_GET_INTERFDET_MODE_CFG,        /*!< Get Interference Detection mode */
  WAVE_RADIO_REQ_GET_TA_CFG,                    /*!< Process TA */
  WAVE_RADIO_REQ_SET_TA_CFG,                    /*!< Process TA */
  WAVE_RADIO_REQ_SET_11B_CFG,                   /*!< Set 11b configuration */
  WAVE_RADIO_REQ_GET_11B_CFG,                   /*!< Get 11b configuration */
  WAVE_RADIO_REQ_SET_SCAN_AND_CALIB_CFG,        /*!< Set scan and calib config */
  WAVE_RADIO_REQ_GET_SCAN_AND_CALIB_CFG,        /*!< Get scan and calib config */
  WAVE_RADIO_REQ_GET_AGG_RATE_LIMIT,            /*!< Get Aggregation-rate limit */
  WAVE_RADIO_REQ_SET_AGG_RATE_LIMIT,            /*!< Set Aggregation-rate limit */
  WAVE_RADIO_REQ_GET_RX_DUTY_CYCLE,             /*!< Get last Reception Duty Cycle Setting */
  WAVE_RADIO_REQ_SET_RX_DUTY_CYCLE,             /*!< Set Reception Duty Cycle Setting */
  WAVE_RADIO_REQ_GET_TX_POWER_LIMIT_OFFSET,     /*!< Get last TX Power Limit */
  WAVE_RADIO_REQ_SET_TX_POWER_LIMIT_OFFSET,     /*!< Set TX Power Limit */
  WAVE_RADIO_REQ_SET_QAMPLUS_MODE,              /*!< Set QAMplus mode on demand */
  WAVE_RADIO_REQ_GET_QAMPLUS_MODE,              /*!< Get QAMplus mode */
  WAVE_RADIO_REQ_SET_RADIO_MODE,                /*!< Set Radio mode on demand */
  WAVE_RADIO_REQ_GET_RADIO_MODE,                /*!< Get Radio mode */
  WAVE_RADIO_REQ_SET_AMSDU_NUM,                 /*!< Set AMSDU num on demand */
  WAVE_RADIO_REQ_GET_AMSDU_NUM,                 /*!< Get AMSDU num */
  WAVE_RADIO_REQ_SET_CCA_THRESHOLD,             /*!< Set CCA threshold */
  WAVE_RADIO_REQ_GET_CCA_THRESHOLD,             /*!< Get CCA threshold */
  WAVE_RADIO_REQ_SET_CCA_ADAPTIVE,              /*!< Set CCA adaptive intervals */
  WAVE_RADIO_REQ_GET_CCA_ADAPTIVE,              /*!< Get CCA adaptive intervals */
  WAVE_RADIO_REQ_SET_RADAR_RSSI_TH,             /*!< Set Radar Detection RSSI threshold */
  WAVE_RADIO_REQ_GET_RADAR_RSSI_TH,             /*!< Get Radar Detection RSSI threshold */
  WAVE_RADIO_REQ_SET_MAX_MPDU_LENGTH,           /*!< Set MAX MPDU length */
  WAVE_RADIO_REQ_GET_MAX_MPDU_LENGTH,           /*!< Get MAX MPDU length */
  WAVE_RADIO_REQ_SET_FIXED_RATE,                /*!< Set Fixed Rate */
  WAVE_RADIO_REQ_SET_IRE_CTRL_B,                /*!< Set IRE Control B */
  WAVE_RADIO_REQ_GET_IRE_CTRL_B,                /*!< Get IRE Control B */
  WAVE_RADIO_REQ_SET_IRE_STAND_ALONE,           /*!< Set IRE Stand Alone enable/disable */
  WAVE_RADIO_REQ_GET_IRE_STAND_ALONE,           /*!< Get IRE Stand Alone enable/disable */
  WAVE_RADIO_REQ_SET_AX_CONFIG,                 /*!< Set AX configuration */
  WAVE_RADIO_REQ_GET_AX_CONFIG,                 /*!< Get AX configuration */
  WAVE_RADIO_REQ_SET_SSB_MODE,                  /*!< Set SSB Mode */
  WAVE_RADIO_REQ_GET_SSB_MODE,                  /*!< Get SSB Mode */
  WAVE_RADIO_REQ_SET_COEX_CFG,                  /*!< Set 2.4 GHz BT, BLE, ZigBee Coex config */
  WAVE_RADIO_REQ_GET_COEX_CFG,                  /*!< Get 2.4 GHz BT, BLE, ZigBee Coex config */
  WAVE_RADIO_REQ_STA_CHANGE_BSS,                /*!< STA mode - Change BSS */
  WAVE_RADIO_REQ_SET_FREQ_JUMP_MODE,            /*!< Set Frequency Jump Mode */
  WAVE_RADIO_REQ_SET_RESTRICTED_AC_MODE,        /*!< Set Restricted AC Mode */
  WAVE_RADIO_REQ_GET_RESTRICTED_AC_MODE,        /*!< Get Restricted AC Mode */
  WAVE_RADIO_REQ_SET_TEST,                      /*!< Test for radio module, TODO: remove later */
  WAVE_RADIO_REQ_GET_TEST,                      /*!< Test for radio module, TODO: remove later */
  WAVE_RADIO_REQ_SET_FIXED_LTF_AND_GI,          /*!< Set fixed LTF and GI */
  WAVE_RADIO_REQ_GET_FIXED_LTF_AND_GI,          /*!< Get fixed LTF and GI */
  WAVE_RADIO_REQ_CAC_TIMEOUT,                   /*!< Handle CAC timer expired event */
  WAVE_RADIO_REQ_GET_CALIBRATION_MASK,          /*!< Get offline and online calibration mask */
  WAVE_RADIO_REQ_SET_CALIBRATION_MASK,          /*!< Set offline and online calibration mask */
  WAVE_RADIO_REQ_SET_NFRP_CFG,                  /*!< Set NFRP configuration */
  WAVE_RADIO_REQ_GET_RTS_RATE,                  /*!< Get RTS Protection Rate Configuration */
  WAVE_RADIO_REQ_GET_CDB_CFG,                   /*!< Get CDB mode */
  WAVE_RADIO_REQ_SET_RTS_RATE,                  /*!< Set RTS Protection Rate Configuration */
  WAVE_RADIO_REQ_GET_PHY_INBAND_POWER,          /*!< Get InBand Power */

  WAVE_HW_REQ_GET_AP_CAPABILITIES,              /*!< Get AP capabilities */
  WAVE_HW_REQ_GET_EEPROM_CFG,                   /*!< Process EEPROM */
  WAVE_HW_REQ_GET_TASKLET_LIMITS,               /*!< Get tasklet limits */
  WAVE_HW_REQ_SET_TASKLET_LIMITS,               /*!< Set tasklet limits */
  MTLK_HW_REQ_GET_COUNTERS_SRC,                 /*!< Get WAVE counters source */
  MTLK_HW_REQ_SET_COUNTERS_SRC,                 /*!< Set WAVE counters source */
  WAVE_HW_REQ_GET_BF_EXPLICIT_CAP,              /*!< Get BF Explicit Capable flag */
  WAVE_HW_REQ_SET_TEMPERATURE_SENSOR,           /*!< Set calibrate on demand */
  WAVE_HW_REQ_GET_TEMPERATURE_SENSOR,           /*!< Get temperature from sensor */
  WAVE_HW_REQ_GET_PVT_SENSOR,                   /*!< Get PVT sensor */
  WAVE_HW_REQ_SET_TEST_BUS,                     /*!< Set Test Bus enable/disable */
  WAVE_HW_REQ_SET_DYNAMIC_MU_CFG,               /*!< Set Dynamic MU Configuration */
  WAVE_HW_REQ_GET_DYNAMIC_MU_CFG,               /*!< Get Dynamic MU Configuration */
  WAVE_RCVRY_RESET,                             /*!< Reset recovery counters and fw dump */
  WAVE_CORE_REQ_GET_ASSOCIATED_DEV_STATS,       /*!< Get associated device statistics */
  WAVE_CORE_REQ_GET_ASSOCIATED_DEV_RATE_INFO_RX_STATS,    /*!< Get associated device rate info rx statistics */
  WAVE_CORE_REQ_GET_ASSOCIATED_DEV_RATE_INFO_TX_STATS,    /*!< Get associated device rate info tx statistics */
  WAVE_CORE_REQ_GET_CHANNEL_STATS,              /*!< Get radio channel statistics */
  WAVE_CORE_REQ_GET_ASSOCIATED_DEV_TID_STATS,
  WAVE_CORE_REQ_SET_STATS_POLL_PERIOD,          /*!< Set statistics poll period */
  WAVE_CORE_REQ_GET_STATS_POLL_PERIOD,          /*!< Get statistics poll period */
#ifndef MTLK_LEGACY_STATISTICS
  WAVE_CORE_REQ_GET_PEER_LIST,
  WAVE_CORE_REQ_GET_PEER_FLOW_STATUS,
  WAVE_CORE_REQ_GET_PEER_CAPABILITIES,
  WAVE_CORE_REQ_GET_PEER_RATE_INFO,
  WAVE_CORE_REQ_GET_RECOVERY_STATS,
  WAVE_CORE_REQ_GET_HW_FLOW_STATUS,
  WAVE_CORE_REQ_GET_TR181_WLAN_STATS,
  WAVE_CORE_REQ_GET_TR181_HW_STATS,
  WAVE_CORE_REQ_GET_TR181_PEER_STATS
#endif
} mtlk_core_tx_req_id_t;


typedef enum _mtlk_aocs_ac_e {
  AC_DISABLED,
  AC_ENABLED,
  AC_NOT_USED,
} mtlk_aocs_ac_e;

/* universal accessors for data, passed between Core and DF through clipboard */
#define MTLK_DECLARE_CFG_START(name)  typedef struct _##name {
#define MTLK_DECLARE_CFG_END(name)    } __MTLK_IDATA name;

#define MTLK_CFG_ITEM(type,name) type name; \
                                 uint8 name##_filled; \
                                 uint8 name##_requested;

#define MTLK_CFG_ITEM_ARRAY(type,name,num_of_elems) type name[num_of_elems]; \
                                                     uint8 name##_filled; \
                                                     uint8 name##_requested;

/* integer values */
#define MTLK_CFG_SET_ITEM(obj,name,src) \
  {\
    (obj)->name##_filled = 1; \
    (obj)->name = src; \
  }

#define MTLK_CFG_SET_ITEM_BY_FUNC(obj,name,func,func_params,func_res) \
  {\
    func_res = func func_params; \
    if (MTLK_ERR_OK == func_res) {\
      (obj)->name##_filled = 1;\
    }\
    else {\
      (obj)->name##_filled = 0;\
    }\
  }

#define MTLK_CFG_SET_ITEM_BY_FUNC_VOID(obj,name,func,func_params) \
  {\
    func func_params; \
    (obj)->name##_filled = 1; \
  }

#define MTLK_CFG_CHECK_AND_SET_ITEM(obj,name,src) \
  {                                               \
    if ((obj)->name##_requested) {                \
      (obj)->name##_filled = 1;                   \
      (obj)->name = src;                          \
    }                                             \
  }

#define MTLK_CFG_CHECK_AND_SET_ITEM_BY_FUNC(obj,name,func,func_params,func_res) \
  {                                                                             \
    if ((obj)->name##_requested) {                                              \
      func_res = func func_params;                                              \
      if (MTLK_ERR_OK == func_res) {                                            \
        (obj)->name##_filled = 1;                                               \
      }                                                                         \
      else {                                                                    \
        (obj)->name##_filled = 0;                                               \
      }                                                                         \
    }                                                                           \
  }

#define MTLK_CFG_CHECK_AND_SET_ITEM_BY_FUNC_VOID(obj,name,func,func_params) \
  {                                                                         \
    if ((obj)->name##_requested) {                                          \
      func func_params;                                                     \
      (obj)->name##_filled = 1;                                             \
    }                                                                       \
  }

#define MTLK_CFG_GET_ITEM(obj,name,dest) \
  if(1==(obj)->name##_filled){(dest)=(obj)->name;}

#define MTLK_CFG_GET_ITEM_BY_FUNC(obj,name,func,func_params,func_res) \
  if(1==(obj)->name##_filled){func_res=func func_params;}

#define MTLK_CFG_GET_ITEM_BY_FUNC_VOID(obj,name,func,func_params) \
  if(1==(obj)->name##_filled){func func_params;}

/* integer buffers -> used loop for provide different type sizes in assigning */
#define MTLK_CFG_SET_ARRAY_ITEM(obj,name,src,num_elems_received,result) \
  { \
    result=MTLK_ERR_PARAMS; \
    if (ARRAY_SIZE((obj)->name) == num_elems_received) {\
      uint32 counter; \
      for (counter=0;counter<num_elems_received;counter++) { \
        (obj)->name[counter]=(src)[counter]; \
      } \
      (obj)->name##_filled = 1; \
      result=MTLK_ERR_OK; \
    }\
  }

#define MTLK_CFG_GET_ARRAY_ITEM(obj,name,dst,num_elems) \
  { \
      if ((obj)->name##_filled == 1) {\
        uint32 counter; \
        for (counter=0;counter<num_elems;counter++) { \
          (dst)[counter]=(obj)->name[counter]; \
        }\
      }\
  }

/* FIXME: This is the same as MTLK_CFG_SET_ITEM_BY_FUNC, so redundant */
#define MTLK_CFG_SET_ARRAY_ITEM_BY_FUNC(obj,name,func,func_params,func_res) \
  {\
    func_res=func func_params; \
    if (MTLK_ERR_OK == func_res) {\
      (obj)->name##_filled = 1;\
    }\
    else {\
      (obj)->name##_filled = 0;\
    }\
  }

/* FIXME: This is the same as MTLK_CFG_SET_ITEM_BY_FUNC_VOID, so redundant */
#define MTLK_CFG_SET_ARRAY_ITEM_BY_FUNC_VOID(obj,name,func,func_params) \
  {\
    func func_params; \
    (obj)->name##_filled = 1; \
  }

/* Card Capabilities */
MTLK_DECLARE_CFG_START(mtlk_card_capabilities_t)
  MTLK_CFG_ITEM(uint32, max_vaps_supported)
  MTLK_CFG_ITEM(uint32, max_stas_supported)
MTLK_DECLARE_CFG_END(mtlk_card_capabilities_t)

/* DOT11H configuration structure for AP */
MTLK_DECLARE_CFG_START(mtlk_11h_ap_cfg_t)
  MTLK_CFG_ITEM(int, debug_chan)
  MTLK_CFG_ITEM(int, debugChannelSwitchCount)
  MTLK_CFG_ITEM(int, debugChannelAvailabilityCheckTime)
  MTLK_CFG_ITEM(uint32, debugNOP)
MTLK_DECLARE_CFG_END(mtlk_11h_ap_cfg_t)

/* MIBs configuration structure */
MTLK_DECLARE_CFG_START(mtlk_mibs_cfg_t)
  MTLK_CFG_ITEM(uint32, short_cyclic_prefix)
  MTLK_CFG_ITEM(uint16, long_retry_limit)
MTLK_DECLARE_CFG_END(mtlk_mibs_cfg_t)

/* radio calibration mask configuration structure */
MTLK_DECLARE_CFG_START(wave_radio_calibration_cfg_t)
  MTLK_CFG_ITEM(uint32, calibr_algo_mask)
  MTLK_CFG_ITEM(uint32, online_calibr_algo_mask)
MTLK_DECLARE_CFG_END(wave_radio_calibration_cfg_t)

typedef struct mtlk_country_code {
    char country[COUNTRY_CODE_MAX_LEN + 1]; /* NUL-terminated string */
} mtlk_country_code_t;

MTLK_DECLARE_CFG_START(mtlk_country_cfg_t)
  MTLK_CFG_ITEM(mtlk_country_code_t, country_code)
MTLK_DECLARE_CFG_END(mtlk_country_cfg_t)

/* WDS configuration structure */
MTLK_DECLARE_CFG_START(mtlk_wds_cfg_t)
  MTLK_CFG_ITEM(IEEE_ADDR, peer_ap_addr_add)
  MTLK_CFG_ITEM(IEEE_ADDR, peer_ap_addr_del)
  MTLK_CFG_ITEM(mtlk_clpb_t *, peer_ap_vect)
  MTLK_CFG_ITEM(uint8, peer_ap_key_idx)
MTLK_DECLARE_CFG_END(mtlk_wds_cfg_t)

MTLK_DECLARE_CFG_START(mtlk_wds_dbg_t)
  MTLK_CFG_ITEM(int, dummy)
MTLK_DECLARE_CFG_END(mtlk_wds_dbg_t)

/* DOT11D configuration structure */
MTLK_DECLARE_CFG_START(mtlk_dot11d_cfg_t)
  MTLK_CFG_ITEM(BOOL, is_dot11d)
MTLK_DECLARE_CFG_END(mtlk_dot11d_cfg_t)

/* MAC watchdog configuration structure */
MTLK_DECLARE_CFG_START(mtlk_mac_wdog_cfg_t)
  MTLK_CFG_ITEM(uint16, mac_watchdog_timeout_ms)
  MTLK_CFG_ITEM(uint32, mac_watchdog_period_ms)
MTLK_DECLARE_CFG_END(mtlk_mac_wdog_cfg_t)

/* STADB configuration structure */
MTLK_DECLARE_CFG_START(mtlk_stadb_cfg_t)
  MTLK_CFG_ITEM(uint32, keepalive_interval)
MTLK_DECLARE_CFG_END(mtlk_stadb_cfg_t)

/* FW log severity configuration structure */
MTLK_DECLARE_CFG_START(mtlk_fw_log_severity_t)
  MTLK_CFG_ITEM(uint32, newLevel)
  MTLK_CFG_ITEM(uint32, targetCPU)
MTLK_DECLARE_CFG_END(mtlk_fw_log_severity_t)

/* 11b configuration */
MTLK_DECLARE_CFG_START(mtlk_11b_antsel_t)
  MTLK_CFG_ITEM(uint8, txAnt)
  MTLK_CFG_ITEM(uint8, rxAnt)
  MTLK_CFG_ITEM(uint8, rate)
MTLK_DECLARE_CFG_END(mtlk_11b_antsel_t)

MTLK_DECLARE_CFG_START(mtlk_11b_cfg_t)
  MTLK_CFG_ITEM(mtlk_11b_antsel_t, antsel)
MTLK_DECLARE_CFG_END(mtlk_11b_cfg_t)

/* WME configuration structure */
MTLK_DECLARE_CFG_START(mtlk_gen_core_country_name_t)
  MTLK_CFG_ITEM_ARRAY(char, name, 3)
MTLK_DECLARE_CFG_END(mtlk_gen_core_country_name_t)

typedef struct mtlk_core_channel_def
{
  uint16 channel;
  uint8 bonding;
  uint8 spectrum_mode;
}mtlk_core_channel_def_t;

MTLK_DECLARE_CFG_START(mtlk_gen_core_cfg_t)
  MTLK_CFG_ITEM(int, bridge_mode)
  MTLK_CFG_ITEM(BOOL, dbg_sw_wd_enable)
  MTLK_CFG_ITEM(BOOL, reliable_multicast)
  MTLK_CFG_ITEM(BOOL, ap_forwarding)
  MTLK_CFG_ITEM(uint8, net_mode)
  MTLK_CFG_ITEM(uint32, bss_rate)
  MTLK_CFG_ITEM_ARRAY(char, nickname, MTLK_ESSID_MAX_SIZE)
  MTLK_CFG_ITEM_ARRAY(char, essid, MTLK_ESSID_MAX_SIZE)
  MTLK_CFG_ITEM(IEEE_ADDR, bssid)
  MTLK_CFG_ITEM(mtlk_core_channel_def_t, channel_def)
  MTLK_CFG_ITEM(uint8, frequency_band_cur)
  MTLK_CFG_ITEM(BOOL, is_hidden_ssid)
  MTLK_CFG_ITEM(uint32, up_rescan_exemption_time)
  MTLK_CFG_ITEM(BOOL, is_bss_load_enable)
  MTLK_CFG_ITEM(uint32, admission_capacity)
MTLK_DECLARE_CFG_END(mtlk_gen_core_cfg_t)

MTLK_DECLARE_CFG_START(wave_core_network_mode_cfg_t)
  MTLK_CFG_ITEM(uint8, net_mode)
MTLK_DECLARE_CFG_END(wave_core_network_mode_cfg_t)

MTLK_DECLARE_CFG_START(wave_core_cdb_cfg_t)
  MTLK_CFG_ITEM(uint8, cdb_cfg)
MTLK_DECLARE_CFG_END(wave_core_cdb_cfg_t)

MTLK_DECLARE_CFG_START(mtlk_core_rts_mode_t)
  MTLK_CFG_ITEM(uint8, dynamic_bw)
  MTLK_CFG_ITEM(uint8, static_bw)
MTLK_DECLARE_CFG_END(mtlk_core_rts_mode_t)

MTLK_DECLARE_CFG_START(mtlk_core_txop_mode_t)
  MTLK_CFG_ITEM(uint32, sid)
  MTLK_CFG_ITEM(uint32, mode)
MTLK_DECLARE_CFG_END(mtlk_core_txop_mode_t)

typedef struct mtlk_erp_cfg
{
  uint32 initial_wait_time; /* seconds */
  uint32 radio_off_time;    /* milliseconds */
  uint32 radio_on_time;     /* milliseconds */
  uint32 erp_enabled;       /* 0 - dsiabled, 1 - enabled */
} mtlk_erp_cfg_t;

MTLK_DECLARE_CFG_START(mtlk_master_core_cfg_t)
  MTLK_CFG_ITEM(uint32, acs_update_timeout)
  MTLK_CFG_ITEM(BOOL, mu_operation)
  MTLK_CFG_ITEM(BOOL, he_mu_operation)
  MTLK_CFG_ITEM(mtlk_core_rts_mode_t, rts_mode_params)
  MTLK_CFG_ITEM(uint8, bf_mode)
  MTLK_CFG_ITEM(mtlk_core_txop_mode_t, txop_mode_params)
  MTLK_CFG_ITEM(uint32, active_ant_mask)
  MTLK_CFG_ITEM(uint32, slow_probing_mask)
  MTLK_CFG_ITEM(FixedPower_t, fixed_pwr_params)
  MTLK_CFG_ITEM(uint32, unconnected_sta_scan_time)
  MTLK_CFG_ITEM(uint8, fast_drop)
  MTLK_CFG_ITEM(mtlk_erp_cfg_t, erp_cfg)
  MTLK_CFG_ITEM(uint32, dynamic_mc_rate)
MTLK_DECLARE_CFG_END(mtlk_master_core_cfg_t)

/* HSTDB structure */
MTLK_DECLARE_CFG_START(mtlk_hstdb_cfg_t)
  MTLK_CFG_ITEM(uint32, wds_host_timeout)
  MTLK_CFG_ITEM(IEEE_ADDR, address)
MTLK_DECLARE_CFG_END(mtlk_hstdb_cfg_t)

/* COC structure */
MTLK_DECLARE_CFG_START(mtlk_coc_mode_cfg_t)
  MTLK_CFG_ITEM(BOOL, is_auto_mode)
  MTLK_CFG_ITEM(mtlk_coc_antenna_cfg_t, antenna_params)
  MTLK_CFG_ITEM(mtlk_coc_auto_cfg_t, auto_params)
  MTLK_CFG_ITEM(uint8, cur_ant_mask)
  MTLK_CFG_ITEM(uint8, hw_ant_mask)
MTLK_DECLARE_CFG_END(mtlk_coc_mode_cfg_t)

#ifdef CPTCFG_IWLWAV_PMCU_SUPPORT
/* PCOC structure */
MTLK_DECLARE_CFG_START(mtlk_pcoc_mode_cfg_t)
  MTLK_CFG_ITEM(BOOL,  is_enabled)
  MTLK_CFG_ITEM(BOOL,  is_active)
  MTLK_CFG_ITEM(uint8, traffic_state)
  MTLK_CFG_ITEM(mtlk_pcoc_params_t, params)
  MTLK_CFG_ITEM(uint32, pmcu_debug)
MTLK_DECLARE_CFG_END(mtlk_pcoc_mode_cfg_t)
#endif /*CPTCFG_IWLWAV_PMCU_SUPPORT*/

/* EEPROM structure */
MTLK_DECLARE_CFG_START(mtlk_eeprom_data_cfg_t)
  MTLK_CFG_ITEM(uint16, eeprom_version)
  MTLK_CFG_ITEM_ARRAY(uint8, mac_address, ETH_ALEN)
  MTLK_CFG_ITEM_ARRAY(uint8, eeprom_type_str, MTLK_EEPROM_TYPE_LEN)
  MTLK_CFG_ITEM(uint8, eeprom_type_id)
  MTLK_CFG_ITEM(uint8, type)
  MTLK_CFG_ITEM(uint8, revision)
  MTLK_CFG_ITEM(uint8, is_asic)
  MTLK_CFG_ITEM(uint8, is_emul)
  MTLK_CFG_ITEM(uint8, is_fpga)
  MTLK_CFG_ITEM(uint8, is_phy_dummy)
  MTLK_CFG_ITEM(uint16, vendor_id)
  MTLK_CFG_ITEM(uint16, device_id)
  MTLK_CFG_ITEM(uint16, sub_vendor_id)
  MTLK_CFG_ITEM(uint16, sub_device_id)
  MTLK_CFG_ITEM(int16, hdr_size)
  MTLK_CFG_ITEM_ARRAY(uint8, sn, MTLK_EEPROM_SN_LEN)
  MTLK_CFG_ITEM(uint8, production_week)
  MTLK_CFG_ITEM(uint8, production_year)
  MTLK_CFG_ITEM(uint8, cal_file_type)
MTLK_DECLARE_CFG_END(mtlk_eeprom_data_cfg_t)

MTLK_DECLARE_CFG_START(mtlk_eeprom_cfg_t)
  MTLK_CFG_ITEM(mtlk_eeprom_data_cfg_t, eeprom_data)
  MTLK_CFG_ITEM(uint32, eeprom_total_size); /* real size of raw data */
  MTLK_CFG_ITEM_ARRAY(uint8, eeprom_raw_data, MTLK_MAX_EEPROM_SIZE)
MTLK_DECLARE_CFG_END(mtlk_eeprom_cfg_t)


/* TPC config */
MTLK_DECLARE_CFG_START(mtlk_tpc_cfg_t)
  MTLK_CFG_ITEM(uint32, loop_type)
MTLK_DECLARE_CFG_END(mtlk_tpc_cfg_t)

/* mbss add vap structure - special case: neither setter nor getter */
MTLK_DECLARE_CFG_START(mtlk_mbss_int_add_vap_cfg_t)
  MTLK_CFG_ITEM(uint32, added_vap_index)
MTLK_DECLARE_CFG_END(mtlk_mbss_int_add_vap_cfg_t)

#define MTLK_MBSS_VAP_LIMIT_DEFAULT ((uint32)-1)
#define VAP_LIMIT_SET_SIZE           (2)

MTLK_DECLARE_CFG_START(mtlk_mbss_cfg_t)
  MTLK_CFG_ITEM(uint32, deleted_vap_index)
  MTLK_CFG_ITEM(uint32, added_vap_index)
  MTLK_CFG_ITEM(uint8, role)
  MTLK_CFG_ITEM(BOOL, is_master)
  MTLK_CFG_ITEM(const char *, added_vap_name)
  MTLK_CFG_ITEM(void *, wiphy)
  MTLK_CFG_ITEM(void *, vap_handle)
  MTLK_CFG_ITEM(void *, df_user)
  MTLK_CFG_ITEM(void *, ndev)
MTLK_DECLARE_CFG_END(mtlk_mbss_cfg_t)

MTLK_DECLARE_CFG_START(mtlk_rcvry_cfg_t)
  MTLK_CFG_ITEM(wave_rcvry_cfg_t, recovery_cfg)
MTLK_DECLARE_CFG_END(mtlk_rcvry_cfg_t)

MTLK_DECLARE_CFG_START(mtlk_rcvry_stats_t)
  MTLK_CFG_ITEM(mtlk_wssa_drv_recovery_stats_t, recovery_stats)
MTLK_DECLARE_CFG_END(mtlk_rcvry_stats_t)

/* Fixed LTF and GI */
MTLK_DECLARE_CFG_START(mtlk_fixed_ltf_and_gi_t)
  MTLK_CFG_ITEM(UMI_FIXED_LTF_AND_GI_REQ_t, fixed_ltf_and_gi_params)
MTLK_DECLARE_CFG_END(mtlk_fixed_ltf_and_gi_t)

/* NFRP Configuration */
MTLK_DECLARE_CFG_START(mtlk_nfrp_cfg_t)
  MTLK_CFG_ITEM(UMI_NFRP_CONFIG, nfrp_cfg_params)
MTLK_DECLARE_CFG_END(mtlk_nfrp_cfg_t)

typedef struct _mtlk_mhi_stats_t {
  uint32            mhi_stats_size;
  uint8            *mhi_stats_data;
} mtlk_mhi_stats_t;

typedef struct {
  uint32         size;
  uint8         *buff;
} wave_bin_data_t;

typedef struct _mtlk_beacon_interval_t {
    uint32 beacon_interval;
    uint32 vap_id;
} mtlk_beacon_interval_t;

typedef struct _mtlk_operating_mode_t {
  uint8 channel_width;
  uint8 rx_nss;
  uint16 station_id;
} mtlk_operating_mode_t;

#ifdef MTLK_LEGACY_STATISTICS
typedef enum {
    /* STATISTIC_TYPE_HOSTIF_COUNTERS */
    STAT_VAP_TX_UNICAST_FRAMES = 0,
    STAT_VAP_TX_MULTICAST_FRAMES,
    STAT_VAP_TX_BROADCAST_FRAMES,

    STAT_VAP_TX_UNICAST_BYTES,
    STAT_VAP_TX_MULTICAST_BYTES,
    STAT_VAP_TX_BROADCAST_BYTES,

    STAT_VAP_RX_UNICAST_FRAMES,
    STAT_VAP_RX_MULTICAST_FRAMES,
    STAT_VAP_RX_BROADCAST_FRAMES,

    STAT_VAP_RX_UNICAST_BYTES,
    STAT_VAP_RX_MULTICAST_BYTES,
    STAT_VAP_RX_BROADCAST_BYTES,

    STAT_VAP_TX_AGER_COUNT,

    /* STATISTIC_TYPE_RX_COUNTERS */
    STAT_VAP_RX_DROP_MPDU,

    STAT_VAP_RX_MPDU_UNICAST_OR_MNGMNT,
    STAT_VAP_RX_MPDU_RETRY_COUNT,
    STAT_VAP_RX_AMSDU,
    STAT_VAP_RX_MPDU_TYPE_NOTSUPPORTED,
    STAT_VAP_RX_REPLAY_DATA,
    STAT_VAP_RX_REPLAY_MNGMNT,
    STAT_VAP_RX_TKIP_COUNT,
    STAT_VAP_RX_SECURITY_FAILURE,
    STAT_VAP_RX_AMSDU_BYTES,
    STAT_VAP_RX_AMPDU,
    STAT_VAP_RX_MPDUIN_AMPDU,
    STAT_VAP_RX_OCTETS_IN_AMPDU,
    STAT_VAP_RX_RX_CLASSIFIER_SECURITY_MISMATCH,
    STAT_VAP_RX_BC_MC_MPDUS,

    /* STATISTIC_TYPE_BAA_COUNTERS */
    STAT_VAP_BAA_RTS_SUCCESS_COUNT,
    STAT_VAP_BAA_QOS_TRANSMITTED_FRAMES,
    STAT_VAP_BAA_TRANSMITTED_AMSDU,
    STAT_VAP_BAA_TRANSMITTED_OCTETS_IN_AMSDU,
    STAT_VAP_BAA_TRANSMITTED_AMPDU,
    STAT_VAP_BAA_TRANSMITTED_MPDU_IN_AMPDU,
    STAT_VAP_BAA_TRANSMITTED_OCTETS_IN_AMPDU,
    STAT_VAP_BAA_BEAMFORMING_FRAMES,
    STAT_VAP_BAA_TRANSMIT_STREAM_RPRT_MSDU_FAILED,
    STAT_VAP_BAA_RTS_FAILURE,
    STAT_VAP_BAA_ACK_FAILURE,
    STAT_VAP_BAA_FAILED_AMSDU,
    STAT_VAP_BAA_RETRY_AMSDU,
    STAT_VAP_BAA_MULTIPLE_RETRY_AMSDU,
    STAT_VAP_BAA_AMSDU_ACK_FAILURE,
    STAT_VAP_BAA_IMPLICIT_BAR_FAILURE,
    STAT_VAP_BAA_EXPLICIT_BAR_FAILURE,
    STAT_VAP_BAA_TRANSMIT_STREAM_RPRT_MULTIPLE_RETRY_COUNT,
    STAT_VAP_BAA_TRANSMIT_BW20,
    STAT_VAP_BAA_TRANSMIT_BW40,
    STAT_VAP_BAA_TRANSMIT_BW80,
    STAT_VAP_BAA_RX_GROUP_FRAME,
    STAT_VAP_BAA_TX_SENDER_ERROR,

    STAT_VAP_TOTAL
} mtlk_mhi_stat_vap_e;

typedef enum {
    /* Extended 64-bit (calculated on driver-side) statistics */
    STAT64_VAP_TX_FRAMES,
    STAT64_VAP_RX_FRAMES,
    STAT64_VAP_TX_BYTES,
    STAT64_VAP_RX_BYTES,

    STAT64_VAP_TOTAL
} mtlk_mhi_stat64_vap_e;
#else /* MTLK_LEGACY_STATISTICS */

typedef struct mtlk_vap_stats64_t{
  uint64 txFrames;
  uint64 rxFrames;
  uint64 txBytes;
  uint64 rxBytes;
}mtlk_vap_stats64;

typedef struct mtlk_vap_stats_t{
  uint32 txInUnicastHd;
  uint32 txInMulticastHd;
  uint32 txInBroadcastHd;
  uint32 txInUnicastNumOfBytes;
  uint32 txInMulticastNumOfBytes;
  uint32 txInBroadcastNumOfBytes;
  uint32 rxOutUnicastHd;
  uint32 rxOutMulticastHd;
  uint32 rxOutBroadcastHd;
  uint32 rxOutUnicastNumOfBytes;
  uint32 rxOutMulticastNumOfBytes;
  uint32 rxOutBroadcastNumOfBytes;
  uint32 agerCount;
   /* Rx Counters */
  uint32 dropMpdu;
  uint32 mpduUnicastOrMngmnt;
  uint32 mpduRetryCount;
  uint32 amsdu;
  uint32 mpduTypeNotSupported;
  uint32 replayData;
  uint32 replayMngmnt;
  uint32 tkipCount;
  uint32 securityFailure;
  uint32 amsduBytes;
  uint32 ampdu;
  uint32 mpduInAmpdu;
  uint32 octetsInAmpdu;
  uint32 rxClassifierSecurityMismatch;
  uint32 bcMcCountVap;
  /* Baa Counters */
  uint32 rtsSuccessCount;
  uint32 qosTransmittedFrames;
  uint32 transmittedAmsdu;
  uint32 transmittedOctetsInAmsdu;
  uint32 transmittedAmpdu;
  uint32 transmittedMpduInAmpdu;
  uint32 transmittedOctetsInAmpdu;
  uint32 beamformingFrames;
  uint32 transmitStreamRprtMSDUFailed;
  uint32 rtsFailure;
  uint32 ackFailure;
  uint32 failedAmsdu;
  uint32 retryAmsdu;
  uint32 multipleRetryAmsdu;
  uint32 amsduAckFailure;
  uint32 implicitBarFailure;
  uint32 explicitBarFailure;
  uint32 transmitStreamRprtMultipleRetryCount;
  uint32 transmitBw20;
  uint32 transmitBw40;
  uint32 transmitBw80;
  uint32 rxGroupFrame;
  uint32 txSenderError;
}mtlk_vap_stats;

#endif

typedef struct _mtlk_mhi_stats_vap {
#ifdef MTLK_LEGACY_STATISTICS
    uint64      stat64[STAT64_VAP_TOTAL];
    uint32      stat[STAT_VAP_TOTAL];
#else
    mtlk_vap_stats64 stats64;
    mtlk_vap_stats stats;
#endif
} mtlk_mhi_stats_vap_t;


typedef struct _mtlk_core_ui_gen_data_t{
  WE_GEN_DATAEX_REQUEST request;
} mtlk_core_ui_gen_data_t;

typedef struct _mtlk_core_ui_range_info_t {
  uint8   num_bitrates;
  int     bitrates[BITRATES_MAX];
} mtlk_core_ui_range_info_t;

#define MTLK_AUTHENTICATION_OPEN_SYSTEM     0
#define MTLK_AUTHENTICATION_SHARED_KEY      1
#define MTLK_AUTHENTICATION_AUTO            2 /* STA connects to AP according to AP's algorithm */

typedef struct _mtlk_core_ui_enc_cfg_t {
  uint8   wep_enabled;
  uint8   key_id;
  MIB_WEP_DEF_KEYS wep_keys;
} mtlk_core_ui_enc_cfg_t;

typedef struct _mtlk_core_ui_auth_cfg_t {
  int16   wep_enabled;
  int16   rsn_enabled;
  int16   authentication;
} mtlk_core_ui_auth_cfg_t;

typedef struct _mtlk_core_ui_auth_state_t {
  int16       cipher_pairwise;
  uint8       group_cipher;
} mtlk_core_ui_auth_state_t;

typedef struct mtlk_core_ui_mlme_cfg_t {
  uint16      cmd;
  uint16      reason_code;
  IEEE_ADDR   sta_addr;
} mtlk_core_ui_mlme_cfg_t;

typedef enum {
  MTLK_IP_NONE,
  MTLK_IPv4,
  MTLK_IPv6,
} ip_type_t;

typedef struct mtlk_mc_addr
{
  ip_type_t type;
  union {
    struct in_addr  ip4_addr;
    struct in6_addr ip6_addr;
  } src_ip, grp_ip;
} mtlk_mc_addr_t;

typedef struct mtlk_ip_netmask
{
  ip_type_t type;
  union {
    struct in_addr ip4_addr;
    struct in6_addr ip6_addr;
  } addr, mask;
} mtlk_ip_netmask_t;

typedef enum {
  MTLK_MC_STA_ACTION_NONE,
  MTLK_MC_STA_JOIN_GROUP,
  MTLK_MC_STA_LEAVE_GROUP
} mc_action_t;

typedef enum {
  MTLK_GRID_ADD,
  MTLK_GRID_DEL
} mc_grid_action_t;

typedef enum {
  MTLK_MC_STADB_ADD,    /*!< Add new MAC's into Multicast STA DB */
  MTLK_MC_STADB_DEL,    /*!< Delete MAC's from Multicast STA DB */
  MTLK_MC_STADB_UPD     /*!< Update MAC's list in Multicast STA DB */
} mc_stadb_action_t;

typedef struct _mtlk_core_ui_mc_action_t {
  mc_action_t         action;
  uint8               grp_id;
  IEEE_ADDR           sta_mac_addr;
  mtlk_mc_addr_t      mc_addr;
} mtlk_core_ui_mc_action_t;

typedef struct _mtlk_core_ui_mc_grid_action_t {
  mc_grid_action_t    action;
  uint8               grp_id;
  mtlk_mc_addr_t      mc_addr;
} mtlk_core_ui_mc_grid_action_t;

typedef struct _mtlk_core_ui_mc_update_sta_db_t {
  mc_stadb_action_t   action;
  uint8               grp_id;
  unsigned            macs_num;
  IEEE_ADDR          *macs_list;
  mtlk_mc_addr_t      mc_addr;
} mtlk_core_ui_mc_update_sta_db_t;

MTLK_DECLARE_CFG_START(mtlk_core_ui_authorizing_cfg_t)
MTLK_CFG_ITEM(IEEE_ADDR, sta_addr)
MTLK_CFG_ITEM(BOOL, authorizing)
MTLK_DECLARE_CFG_END(mtlk_core_ui_authorizing_cfg_t)

typedef struct _mtlk_core_ui_encext_cfg_t
{
  uint16    alg_type;
  IEEE_ADDR sta_addr;
  uint16    key_idx;
  uint16    key_len;
  uint8     key[UMI_RSN_TK1_LEN + UMI_RSN_TK2_LEN];
  uint8     rx_seq[6];
} mtlk_core_ui_encext_cfg_t;

typedef struct _mtlk_core_ui_group_pn_t
{
  uint8     key[UMI_RSN_TK1_LEN + UMI_RSN_TK2_LEN];
  uint8     seq[8];
  uint16    key_len;
  uint16    seq_len;
  uint16    cipher;
  uint16    key_idx;
} mtlk_core_ui_group_pn_t;

typedef struct _mtlk_core_ui_default_key_cfg_t
{
  BOOL      is_mgmt_key;
  uint16    key_idx;
  IEEE_ADDR sta_addr;
} mtlk_core_ui_default_key_cfg_t;

MTLK_DECLARE_CFG_START(mtlk_serializer_command_info_t)
  MTLK_CFG_ITEM(uint32, priority)
  MTLK_CFG_ITEM(BOOL, is_current)
  MTLK_CFG_ITEM(mtlk_slid_t, issuer_slid)
MTLK_DECLARE_CFG_END(mtlk_serializer_command_info_t)

typedef struct _mtlk_core_ui_get_stadb_status_req_t {
  uint8      get_hostdb;
  uint8      use_cipher;
} mtlk_core_ui_get_stadb_status_req_t;

typedef struct _mtlk_core_hs20_info_t {
  BOOL   hs20_enabled;
  BOOL   arp_proxy;
  BOOL   dgaf_disabled;
  BOOL   osen_enabled;

  uint8  dscp_table[DSCP_NUM];
} mtlk_core_hs20_info_t;

/* 20/40 coexistence feature */

/* Interference Detection */
typedef struct
{
  uint32 active_polling_timeout;
  uint32 short_scan_polling_timeout;
  uint32 long_scan_polling_timeout;
  uint32 active_notification_timeout;
  uint32 short_scan_notification_timeout;
  uint32 long_scan_notification_timeout;
} __MTLK_IDATA mtlk_interfdet_req_timeouts_t;

typedef struct
{
  int8   detection_threshold_20mhz;
  int8   notification_threshold_20mhz;
  int8   detection_threshold_40mhz;
  int8   notification_threshold_40mhz;
  uint8  scan_noise_threshold;
  int8   scan_minimum_noise;
  BOOL   mode;
} __MTLK_IDATA mtlk_interfdet_req_thresh_t;

typedef struct
{
  uint32 short_scan_max_time;
  uint32 long_scan_max_time;
} __MTLK_IDATA mtlk_interfdet_req_scantimes_t;

MTLK_DECLARE_CFG_START(mtlk_interfdet_cfg_t)
  MTLK_CFG_ITEM(mtlk_interfdet_req_timeouts_t,  req_timeouts)
  MTLK_CFG_ITEM(mtlk_interfdet_req_thresh_t,    req_thresh)
  MTLK_CFG_ITEM(mtlk_interfdet_req_scantimes_t, req_scantimes)
MTLK_DECLARE_CFG_END(mtlk_interfdet_cfg_t)

MTLK_DECLARE_CFG_START(mtlk_interfdet_mode_cfg_t)
  MTLK_CFG_ITEM(BOOL, interfdet_mode)
MTLK_DECLARE_CFG_END(mtlk_interfdet_mode_cfg_t)

MTLK_DECLARE_CFG_START(mtlk_dbg_cli_cfg_t);
  MTLK_CFG_ITEM(UmiDbgCliReq_t,  DbgCliReq);
MTLK_DECLARE_CFG_END(mtlk_dbg_cli_cfg_t)

MTLK_DECLARE_CFG_START(mtlk_fw_debug_cfg_t);
  MTLK_CFG_ITEM(UMI_FW_DBG_REQ, FWDebugReq);
MTLK_DECLARE_CFG_END(mtlk_fw_debug_cfg_t)

typedef struct
{
  uint16  maxRate;
  uint8   mode;
} __MTLK_IDATA mtlk_agg_rate_limit_req_cfg_t;

MTLK_DECLARE_CFG_START(mtlk_agg_rate_limit_cfg_t);
  MTLK_CFG_ITEM(mtlk_agg_rate_limit_req_cfg_t, agg_rate_limit);
MTLK_DECLARE_CFG_END(mtlk_agg_rate_limit_cfg_t)

MTLK_DECLARE_CFG_START(mtlk_rx_th_cfg_t)
  MTLK_CFG_ITEM(int8, rx_threshold);
MTLK_DECLARE_CFG_END(mtlk_rx_th_cfg_t)

MTLK_DECLARE_CFG_START(mtlk_rx_duty_cycle_cfg_t)
  MTLK_CFG_ITEM(UMI_RX_DUTY_CYCLE, duty_cycle);
MTLK_DECLARE_CFG_END(mtlk_rx_duty_cycle_cfg_t)

MTLK_DECLARE_CFG_START(mtlk_tx_power_lim_cfg_t)
  MTLK_CFG_ITEM(uint8, powerLimitOffset);
MTLK_DECLARE_CFG_END(mtlk_tx_power_lim_cfg_t)

MTLK_DECLARE_CFG_START(mtlk_qamplus_mode_cfg_t)
  MTLK_CFG_ITEM(uint8, qamplus_mode);
MTLK_DECLARE_CFG_END(mtlk_qamplus_mode_cfg_t)

MTLK_DECLARE_CFG_START(mtlk_radio_mode_cfg_t)
  MTLK_CFG_ITEM(BOOL, radio_mode);
MTLK_DECLARE_CFG_END(mtlk_radio_mode_cfg_t)

#define MTLK_AGGR_CFG_WINDOW_SIZE_DFLT  WINDOW_SIZE_NO_CHANGE

typedef struct mtlk_core_aggr_cfg
{
  uint8 amsdu_mode;
  uint8 ba_mode;
  uint32 windowSize;
} mtlk_core_aggr_cfg_t;

MTLK_DECLARE_CFG_START(mtlk_aggr_cfg_t)
  MTLK_CFG_ITEM(mtlk_core_aggr_cfg_t, cfg);
MTLK_DECLARE_CFG_END(mtlk_aggr_cfg_t)

MTLK_DECLARE_CFG_START(mtlk_amsdu_num_cfg_t)
  MTLK_CFG_ITEM(uint32, amsdu_num);
  MTLK_CFG_ITEM(uint32, amsdu_vnum);
  MTLK_CFG_ITEM(uint32, amsdu_henum);
MTLK_DECLARE_CFG_END(mtlk_amsdu_num_cfg_t)

MTLK_DECLARE_CFG_START(mtlk_ht_protection_cfg_t)
  MTLK_CFG_ITEM(uint8, use_cts_prot);
MTLK_DECLARE_CFG_END(mtlk_ht_protection_cfg_t)

MTLK_DECLARE_CFG_START(mtlk_bf_explicit_cap_cfg_t)
  MTLK_CFG_ITEM(BOOL, bf_explicit_cap);
MTLK_DECLARE_CFG_END(mtlk_bf_explicit_cap_cfg_t)

MTLK_DECLARE_CFG_START(mtlk_temperature_sensor_t)
  MTLK_CFG_ITEM_ARRAY(uint32, temperature, NUM_OF_ANTS_FOR_TEMPERATURE)
  MTLK_CFG_ITEM(uint32, calibrate_mask)
MTLK_DECLARE_CFG_END(mtlk_temperature_sensor_t)

typedef struct {
  uint32 voltage;
  int32  temperature;
} iwpriv_pvt_t;

MTLK_DECLARE_CFG_START(wave_pvt_sensor_t)
  MTLK_CFG_ITEM(iwpriv_pvt_t, pvt_params)
MTLK_DECLARE_CFG_END(wave_pvt_sensor_t)

MTLK_DECLARE_CFG_START(wave_ui_mode_t)
  MTLK_CFG_ITEM(int, mode) /* BOOL */
MTLK_DECLARE_CFG_END(wave_ui_mode_t)

#ifdef CPTCFG_IWLWAV_SET_PM_QOS
MTLK_DECLARE_CFG_START(mtlk_pm_qos_cfg_t)
  MTLK_CFG_ITEM(s32, cpu_dma_latency);
MTLK_DECLARE_CFG_END(mtlk_pm_qos_cfg_t)
#endif

MTLK_DECLARE_CFG_START(mtlk_max_mpdu_len_cfg_t)
  MTLK_CFG_ITEM(uint32, max_mpdu_length);
MTLK_DECLARE_CFG_END(mtlk_max_mpdu_len_cfg_t)

MTLK_DECLARE_CFG_START(mtlk_four_addr_cfg_t)
  MTLK_CFG_ITEM(uint32, mode);
  MTLK_CFG_ITEM(IEEE_ADDR, addr_add)
  MTLK_CFG_ITEM(IEEE_ADDR, addr_del)
  MTLK_CFG_ITEM(mtlk_clpb_t *, sta_vect)
MTLK_DECLARE_CFG_END(mtlk_four_addr_cfg_t)

MTLK_DECLARE_CFG_START(mtlk_stats_poll_period_cfg_t)
  MTLK_CFG_ITEM(uint32, period);
MTLK_DECLARE_CFG_END(mtlk_stats_poll_period_cfg_t)

/* Calculate antennas factor depending on TX antennas number */
/* Exception: (-1) in case of 0 antennas number */
static __INLINE int
mtlk_antennas_factor(unsigned num)
{
  int table_log_mul10[] = {-1, 0, 24, 38, 48}; /* 8 * 10 * LOG10(num), num = [0..4] */
  MTLK_ASSERT(num < ARRAY_SIZE(table_log_mul10));
  return table_log_mul10[num];
}

/* Power: 8 uints per dBm */
#define POWER_PER_DBM       8
#define DBM_TO_POWER(dbm)   ((dbm) << 3)
#define POWER_TO_DBM(power) (((power) + 4) >> 3)  /* With rounding */
#define POWER_TO_MBM(power) POWER_TO_DBM(DBM_TO_MBM(power))

/* Power statistics: 2 uints per dBm */
#define STAT_PW_PER_DBM     2
#define DBM_TO_STAT_PW(dbm) ((dbm) * STAT_PW_PER_DBM)
#define STAT_PW_TO_DBM(pw)  (((pw) + (STAT_PW_PER_DBM / 2)) / STAT_PW_PER_DBM)  /* With rounding */
#define STAT_PW_TO_MBM(pw)  STAT_PW_TO_DBM(DBM_TO_MBM(pw))
#define STAT_PW_TO_POWER(pw) ((pw) * POWER_PER_DBM / STAT_PW_PER_DBM)

MTLK_DECLARE_CFG_START(mtlk_tx_power_cfg_t)
  MTLK_CFG_ITEM(uint32, tx_power);
MTLK_DECLARE_CFG_END(mtlk_tx_power_cfg_t)

#define NUM_SUPPORTED_BANDS 2
#define NUM_IWPRIV_CALIB_CW_MASKS NUM_SUPPORTED_BANDS
#define NUM_IWPRIV_SCAN_PARAMS 6
#define NUM_IWPRIV_SCAN_PARAMS_BG 6

typedef struct iwpriv_scan_params
{
  uint32 passive_scan_time;
  uint32 active_scan_time;
  uint32 num_probe_reqs;
  uint32 probe_req_interval;
  uint32 passive_scan_valid_time;
  uint32 active_scan_valid_time;
} iwpriv_scan_params_t;

typedef struct iwpriv_scan_params_bg
{
  uint32 passive_scan_time;
  uint32 active_scan_time;
  uint32 num_probe_reqs;
  uint32 probe_req_interval;
  uint32 num_chans_in_chunk;
  uint32 chan_chunk_interval;
} iwpriv_scan_params_bg_t;

MTLK_DECLARE_CFG_START(mtlk_scan_and_calib_cfg_t);
  MTLK_CFG_ITEM(uint32, scan_modifs);
  MTLK_CFG_ITEM(iwpriv_scan_params_t, scan_params);
  MTLK_CFG_ITEM(iwpriv_scan_params_bg_t, scan_params_bg);
  MTLK_CFG_ITEM_ARRAY(uint32, calib_cw_masks, NUM_IWPRIV_CALIB_CW_MASKS);
  MTLK_CFG_ITEM(uint8, rbm); /* radar Bit Map */
  MTLK_CFG_ITEM(uint32, radar_detect);
  MTLK_CFG_ITEM(uint32, scan_expire_time);
MTLK_DECLARE_CFG_END(mtlk_scan_and_calib_cfg_t)

typedef void (*vfunptr)(void);

MTLK_DECLARE_CFG_START(mtlk_ta_crit_t);
  MTLK_CFG_ITEM(uint32, id);
  MTLK_CFG_ITEM(uint32, signature);
  MTLK_CFG_ITEM(vfunptr, fcn);
  MTLK_CFG_ITEM(vfunptr, clb);
  MTLK_CFG_ITEM(uint32, clb_ctx);
  MTLK_CFG_ITEM(uint32, tmr_cnt);
  MTLK_CFG_ITEM(uint32, tmr_period);
MTLK_DECLARE_CFG_END(mtlk_ta_crit_t)

MTLK_DECLARE_CFG_START(mtlk_ta_wss_counter_t);
  MTLK_CFG_ITEM(uint32, prev);
  MTLK_CFG_ITEM(uint32, delta);
MTLK_DECLARE_CFG_END(mtlk_ta_wss_counter_t)

MTLK_DECLARE_CFG_START(mtlk_ta_sta_wss_t);
  MTLK_CFG_ITEM(IEEE_ADDR, addr);
  MTLK_CFG_ITEM(BOOL,   coc_wss_valid);
  MTLK_CFG_ITEM(mtlk_ta_wss_counter_t, coc_rx_bytes);
  MTLK_CFG_ITEM(mtlk_ta_wss_counter_t, coc_tx_bytes);
MTLK_DECLARE_CFG_END(mtlk_ta_sta_wss_t)

MTLK_DECLARE_CFG_START(mtlk_ta_debug_info_cfg_t)
  MTLK_CFG_ITEM(uint32, nof_crit);
  MTLK_CFG_ITEM(uint32, nof_sta_wss);
  MTLK_CFG_ITEM_ARRAY(mtlk_ta_crit_t, crit, TA_CRIT_NUM);
  MTLK_CFG_ITEM_ARRAY(mtlk_ta_sta_wss_t, sta_wss, WAVE_MAX_SID);
MTLK_DECLARE_CFG_END(mtlk_ta_debug_info_cfg_t)

MTLK_DECLARE_CFG_START(mtlk_ta_cfg_t)
  MTLK_CFG_ITEM(uint32, timer_resolution);
  MTLK_CFG_ITEM(mtlk_ta_debug_info_cfg_t, debug_info);
MTLK_DECLARE_CFG_END(mtlk_ta_cfg_t)

MTLK_DECLARE_CFG_START(mtlk_dot11w_cfg_t)
  MTLK_CFG_ITEM(uint32, pmf_activated);
  MTLK_CFG_ITEM(uint32, pmf_required);
  MTLK_CFG_ITEM(uint32, saq_retr_tmout);
  MTLK_CFG_ITEM(uint32, saq_max_tmout);
MTLK_DECLARE_CFG_END(mtlk_dot11w_cfg_t)

typedef struct {
  const uint8 *mac;
  struct station_info *stinfo; /* FIXME this also needs to be duplicated */
} st_info_data_t;

struct vendor_unconnected_sta_req_data_internal
{
  u32 chan_width; /* from enum nl80211_chan_width */
  u32 center_freq; /* in MHz */
  u32 cf1; /* in MHz */
  u32 cf2; /* in MHz */
  IEEE_ADDR addr;
  struct wireless_dev *wdev; /* wdev that requested the data, filled by driver */
};

struct driver_sta_info {
  mtlk_wssa_drv_peer_rates_info_t  rates_info;
  mtlk_wssa_drv_peer_stats_t       peer_stats;
};

struct driver_vap_info {
  mtlk_wssa_drv_tr181_wlan_stats_t vap_stats;
  u64 TransmittedOctetsInAMSDUCount;
  u64 ReceivedOctetsInAMSDUCount;
  u64 TransmittedOctetsInAMPDUCount;
  u64 ReceivedOctetsInAMPDUCount;
  u32 RTSSuccessCount;
  u32 RTSFailureCount;
  u32 TransmittedAMSDUCount;
  u32 FailedAMSDUCount;
  u32 AMSDUAckFailureCount;
  u32 ReceivedAMSDUCount;
  u32 TransmittedAMPDUCount;
  u32 TransmittedMPDUsInAMPDUCount;
  u32 AMPDUReceivedCount;
  u32 MPDUInReceivedAMPDUCount;
  u32 ImplicitBARFailureCount;
  u32 ExplicitBARFailureCount;
  u32 TwentyMHzFrameTransmittedCount;
  u32 FortyMHzFrameTransmittedCount;
  u32 SwitchChannel20To40;
  u32 SwitchChannel40To20;
  u32 FrameDuplicateCount;
};

struct driver_radio_info {
  mtlk_wssa_drv_tr181_hw_t tr181_hw;
  mtlk_wssa_drv_tr181_hw_stats_t tr181_hw_stats;
  u64 tsf_start_time; /* measurement start time */
  u8 load;
  u32 tx_pwr_cfg;
  u8 num_tx_antennas;
  u8 num_rx_antennas;
  u32 primary_center_freq; /* center frequency in MHz */
  u32 center_freq1;
  u32 center_freq2;
  u32 width; /* 20,40,80,... */
};

enum mtlk_sta_4addr_mode_e {
  STA_4ADDR_MODE_DYNAMIC    = -1,
  STA_4ADDR_MODE_OFF        = 0,
  STA_4ADDR_MODE_ON         = 1
};

#define MTLK_PACK_ON
#include "mtlkpack.h"

struct __MTLK_PACKED vendor_check_4addr_mode {
  enum mtlk_sta_4addr_mode_e sta_4addr_mode;
};

#define MTLK_PACK_OFF
#include "mtlkpack.h"

typedef struct {
  int    idx;
  uint8 *mac;
  struct station_info *stinfo;
} st_info_by_idx_data_t;

struct mgmt_tx_params
{
  const uint8 *buf;
  size_t len;
  int channum;
  uint64 *cookie;
  int no_cck;
  int dont_wait_for_ack;
  uint32 extra_processing;
  mtlk_nbuf_t *skb;
};


struct tasklet_limits
{
  uint32 data_txout_lim;
  uint32 data_rx_lim;
  uint32 bss_rx_lim;
  uint32 bss_cfm_lim;
  uint32 legacy_lim;
};

MTLK_DECLARE_CFG_START(mtlk_tasklet_limits_cfg_t)
  MTLK_CFG_ITEM(struct tasklet_limits, tl)
MTLK_DECLARE_CFG_END(mtlk_tasklet_limits_cfg_t)

MTLK_DECLARE_CFG_START(mtlk_wlan_counters_src_cfg_t)
  MTLK_CFG_ITEM(unsigned, src)
MTLK_DECLARE_CFG_END(mtlk_wlan_counters_src_cfg_t)

typedef struct mtlk_dfs_state_cfg {
  struct cfg80211_chan_def chandef;
  enum nl80211_dfs_state   dfs_state;
  enum nl80211_radar_event event;
  uint8                    rbm;
  BOOL                     cac_started;
  mtlk_dlist_entry_t       lentry;
} mtlk_dfs_state_cfg_t;

/* CCA threshold */
#define MTLK_CCA_TH_PARAMS_LEN  5   /* number of params */

typedef struct iwpriv_cca_th
{
  int8  values[MTLK_CCA_TH_PARAMS_LEN];
  int8  is_updated;
} iwpriv_cca_th_t;

MTLK_DECLARE_CFG_START(mtlk_cca_th_cfg_t)
  MTLK_CFG_ITEM(iwpriv_cca_th_t, cca_th_params);
MTLK_DECLARE_CFG_END(mtlk_cca_th_cfg_t)


/* adaptive CCA */

#define MTLK_CCA_ADAPT_PARAMS_LEN  7   /* number of params */

typedef struct iwpriv_cca_adapt
{
  u32  initial;
  u32  iterative;
  int  limit;
  u32  step_up;
  u32  step_down;
  u32  step_down_interval;
  u32  min_unblocked_time;
} iwpriv_cca_adapt_t;

struct mtlk_cca_adapt
{
  int8 cca_th_params[MTLK_CCA_TH_PARAMS_LEN];
  u32  step_down_coef;
  u32  last_unblocked_time;
  BOOL stepping_down;
  BOOL stepping_up;
  u32  interval;
  BOOL cwi_poll;
  BOOL cwi_drop_detected;
  u32  cwi_poll_ts;
};

MTLK_DECLARE_CFG_START(mtlk_cca_adapt_cfg_t)
  MTLK_CFG_ITEM(iwpriv_cca_adapt_t, cca_adapt_params);
MTLK_DECLARE_CFG_END(mtlk_cca_adapt_cfg_t)

/* Radar RSSI Threshold */
MTLK_DECLARE_CFG_START(mtlk_radar_rssi_th_cfg_t)
  MTLK_CFG_ITEM(int, radar_rssi_th);
MTLK_DECLARE_CFG_END(mtlk_radar_rssi_th_cfg_t)

/* IRE Control */
MTLK_DECLARE_CFG_START(mtlk_ire_ctrl_cfg_t)
  MTLK_CFG_ITEM(int, ire_ctrl_value);
MTLK_DECLARE_CFG_END(mtlk_ire_ctrl_cfg_t)

/* Multicast configuration structures */

typedef enum {
  MTLK_MCAST_ACTION_CLEANUP,
  MTLK_MCAST_ACTION_ADD,
  MTLK_MCAST_ACTION_DEL,
  MTLK_MCAST_ACTION_LAST
} mtlk_mcast_action_t;

typedef struct mtlk_mcast_range {
  mtlk_mcast_action_t action;
  mtlk_ip_netmask_t   netmask;
} mtlk_mcast_range_t;

MTLK_DECLARE_CFG_START(mtlk_mcast_range_cfg_t)
  MTLK_CFG_ITEM(struct mtlk_mcast_range, range)
MTLK_DECLARE_CFG_END(mtlk_mcast_range_cfg_t)

MTLK_DECLARE_CFG_START(mtlk_mcast_range_vect_cfg_t)
  MTLK_CFG_ITEM(mtlk_clpb_t *, range_vect)
MTLK_DECLARE_CFG_END(mtlk_mcast_range_vect_cfg_t)

MTLK_DECLARE_CFG_START(mtlk_wlan_rts_rate_cfg_t)
  MTLK_CFG_ITEM(unsigned, cutoff_point)
MTLK_DECLARE_CFG_END(mtlk_wlan_rts_rate_cfg_t)

/* PHY in band power */
typedef struct {
  int32  noise_estim[4]; /* per antenna */
  int32  system_gain[4]; /* per antenna */
  uint32 ant_mask;
} mtlk_phy_inband_power_data_t;

MTLK_DECLARE_CFG_START(mtlk_phy_inband_power_cfg_t)
  MTLK_CFG_ITEM(mtlk_phy_inband_power_data_t, power_data)
MTLK_DECLARE_CFG_END(mtlk_phy_inband_power_cfg_t)

/* The CW_ values must be 0, 1, 2, 3, X, where X >= 4. */
enum chanWidth
{
  CW_20 = 0,
  CW_40,
  CW_80,
  CW_160,
  CW_80_80,
  CW_DEFAULT     = CW_20,
  CW_MIN         = CW_20,
  CW_MAX_2G      = CW_40,
  CW_MAX_5G_GEN5 = CW_80,
  CW_MAX_5G_GEN6 = CW_160
};

static __INLINE BOOL
wave_chan_width_is_supported(enum chanWidth cw)
{
  return ((CW_MIN <= cw) && (cw <= CW_160));
}

/* mimic the linux struct ieee80211_channel with our own, skip the fields that we don't use yet */
struct mtlk_channel
{
  unsigned long dfs_state_entered;
  unsigned dfs_state; /* from enum mtlk_dfs_state */
  unsigned dfs_cac_ms;
  mtlk_hw_band_e band;
  u32 center_freq; /* center frequency in MHz */
  u32 flags; /* channel flags from enum mtlk_channel_flags */
  u32 orig_flags;
  int max_antenna_gain;
  int max_power;
  int max_reg_power;
};

enum mtlk_sb_dfs_bw {
  MTLK_SB_DFS_BW_NORMAL,
  MTLK_SB_DFS_BW_20,
  MTLK_SB_DFS_BW_40,
  MTLK_SB_DFS_BW_80,
};

typedef struct mtlk_sb_dfs_params
{
  u32 center_freq;
  u32 width; /* from enum chanWidth */
  u8 sb_dfs_bw; /* from enum mtlk_sb_dfs_bw */
} mtlk_sb_dfs_params_t;

/* mimic the linux struct cfg80211_channel with our own */
struct mtlk_chan_def
{
  /* mtlk_osal_spinlock_t chan_lock; */
  u32 center_freq1;
  u32 center_freq2;
  u32 width; /* from enum chanWidth */
  struct mtlk_channel chan; /* primary channel spec */
  BOOL is_noht;
  BOOL wait_for_beacon;
  mtlk_sb_dfs_params_t sb_dfs;
};

static __INLINE
BOOL __mtlk_is_sb_dfs_switch (enum mtlk_sb_dfs_bw bw)
{
  return MTLK_SB_DFS_BW_20 == bw ||
         MTLK_SB_DFS_BW_40 == bw ||
         MTLK_SB_DFS_BW_80 == bw;
}

/* Conversion between NL80211 band and MTLK band */
typedef enum nl80211_band  nl80211_band_e;
typedef enum mtlk_hw_band  mtlk_hw_band_e;

/* First two elements in nl80211_band and mtlk_hw_band contain 5/2Ghz,
   but in different order:

   In nl80211_band:
     NL80211_BAND_2GHZ - 0
     NL80211_BAND_5GHZ - 1

   In enum mtlk_hw_band:
     MTLK_HW_BAND_5_2_GHZ - 0
     MTLK_HW_BAND_2_4_GHZ - 1
*/

static __INLINE
mtlk_hw_band_e nlband2mtlkband(nl80211_band_e band)
{
  MTLK_ASSERT(band == NL80211_BAND_2GHZ || band == NL80211_BAND_5GHZ);
  return (band == NL80211_BAND_2GHZ) ?
    MTLK_HW_BAND_2_4_GHZ : MTLK_HW_BAND_5_2_GHZ;
}

static __INLINE
nl80211_band_e mtlkband2nlband(mtlk_hw_band_e band)
{
  MTLK_ASSERT(band == MTLK_HW_BAND_2_4_GHZ || band == MTLK_HW_BAND_5_2_GHZ);
  return (band == MTLK_HW_BAND_2_4_GHZ) ?
    NL80211_BAND_2GHZ : NL80211_BAND_5GHZ;
}

/* Conversion from NL80211 chan-width constants to our chan-width constants */
static __INLINE
unsigned nlcw2mtlkcw(unsigned cw)
{
  switch (cw)
  {
  case NL80211_CHAN_WIDTH_20_NOHT:
  case NL80211_CHAN_WIDTH_20:
    return CW_20;
  case NL80211_CHAN_WIDTH_40:
    return CW_40;
  case NL80211_CHAN_WIDTH_80:
    return CW_80;
  case NL80211_CHAN_WIDTH_80P80:
    return CW_80_80;
  case NL80211_CHAN_WIDTH_160:
    return CW_160;
  default:
    MTLK_ASSERT(FALSE);
  }

  return CW_20;
};

/* Conversion from our chan-width constants to NL80211 chan-width constants */
static __INLINE
unsigned mtlkcw2nlcw(unsigned cw, BOOL is_noht)
{
  switch (cw)
  {
  case CW_20:
    return (is_noht ? NL80211_CHAN_WIDTH_20_NOHT : NL80211_CHAN_WIDTH_20);
  case CW_40:
    return NL80211_CHAN_WIDTH_40;
  case CW_80:
    return NL80211_CHAN_WIDTH_80;
  case CW_80_80:
    return NL80211_CHAN_WIDTH_80P80;
  case CW_160:
    return NL80211_CHAN_WIDTH_160;
  default:
    MTLK_ASSERT(0);
  }

  return NL80211_CHAN_WIDTH_20;
};

/* Conversion from our chan-width constants to real chan width */
static __INLINE
unsigned mtlkcw2cw(unsigned cw)
{
  return cw == CW_80_80 ? 80 : (1 << cw) * 20;
}

/* Conversion from our chan-width constants to strings */
static __INLINE
char* mtlkcw2str(unsigned cw)
{
  switch (cw) {
  case CW_20:
    return "20";
  case CW_40:
    return "40";
  case CW_80:
    return "80";
  case CW_160:
    return "160";
  case CW_80_80:
    return "80+80";
  default:
    return "Unknown";
  }
}


/* Conversion from center freq and given chanwidth encoding (0 for 20, 1 for 40, etc.) to freq of lowest 20 MHz chan */
static __INLINE
u32 freq2lowfreq(u32 center_freq, unsigned cw)
{
  return center_freq - mtlkcw2cw(cw) / 2 + 10;
}

/* Conversion from center freq and given chanwidth encoding (0 for 20, 1 for 40, etc.) to freq of highest 20 MHz chan */
static __INLINE
u32 freq2highfreq(u32 center_freq, unsigned cw)
{
  return center_freq + mtlkcw2cw(cw) / 2 - 10;
}

/* Conversion from center freq and given chanwidth encoding to the lowest 20 MHz channel number */
static __INLINE
int freq2lowchannum(u32 center_freq, unsigned cw)
{
  return ieee80211_frequency_to_channel(freq2lowfreq(center_freq, cw));
}

struct set_chan_param_data
{
  struct mtlk_chan_def chandef;
  u32 cac_time; // whether to set the cac timer afterwards and for how long, in ms
  u16 chan_switch_time; // number of tbtt-s before the CSA-type switch * beacon interval in ms
  u8 switch_type; // ST_NORMAL, ST_SCAN or ST_CSA
  u8 block_tx_pre; // block TX except beacons and csa action frames before the CSA-type switch
  u8 block_tx_post; // block all TX including beacons after the CSA-type switch
  u8 bg_scan; // set if a SCAN_type switch is part of a background scan
  u8 radar_required; // radar detection required on the channel-range
  u16 sid; //station index: used only when switch_type==ST_RSSI/ST_NORMAL_AFTER_RSSI
  /* rssi per antenna used only when switch_type==ST_NORMAL_AFTER_RSSI */
  s8 rssi[PHY_STATISTICS_MAX_RX_ANT];
  struct net_device *ndev;
  u8 vap_id;
  /* noise per antenna used only when switch_type==ST_NORMAL_AFTER_RSSI */
  u8 noise[PHY_STATISTICS_MAX_RX_ANT];
  /* Rate of last packet received switch_type==ST_NORMAL_AFTER_RSSI */
  u32 rate;
  u8 rf_gain[PHY_STATISTICS_MAX_RX_ANT];
  BOOL dont_notify_kernel;
};

enum mtlk_hidden_ssid
{
  MTLK_HIDDEN_SSID_NOT_IN_USE,
  MTLK_HIDDEN_SSID_ZERO_LEN,
  MTLK_HIDDEN_SSID_ZERO_CONTENTS
};

struct mtlk_ap_settings
{
  int beacon_interval;
  int dtim_period;
  int hidden_ssid;
  unsigned essid_len;
  char essid[MIB_ESSID_LENGTH + 1];
};

#define MTLK_MAX_NUM_BASIC_RATES 32

/* mimic cfg80211.h's bss_parameters */
struct mtlk_bss_parameters
{
  unsigned vap_id;
  int use_cts_prot;
  int use_short_preamble;
  int use_short_slot_time;
  int ap_isolate;
  int ht_opmode;
  u8 basic_rates[MTLK_MAX_NUM_BASIC_RATES];
  u8 basic_rates_len;
  s8 p2p_ctwindow, p2p_opp_ps;
};

struct mtlk_sta_bss_change_parameters
{
  const char *vif_name;
  struct ieee80211_supported_band **bands;
  mtlk_core_t *core;
  struct ieee80211_bss_conf *info;
  u32 changed;
  u8 vap_index;
};

/* mimic cfg80211_match_set with ours */
struct mtlk_match_set
{
  char ssid[MIB_ESSID_LENGTH];
  s32 rssi_thold;
  u8 ssid_len;
};

#define MAX_SCAN_SSIDS      16
#define MAX_MATCH_SETS      16
#define MAX_SCAN_IE_LEN     2048
#define NUM_2GHZ_CHANS      14
#define NUM_5GHZ_CHANS      33
#define NUM_TOTAL_CHANS     (NUM_2GHZ_CHANS + NUM_5GHZ_CHANS)

#define NUM_5GHZ_CENTRAL_FREQS_BW40     10  /* number of central freqs in 40 MHz mode */
#define NUM_5GHZ_CENTRAL_FREQS_BW80      5  /* number of central freqs in 80 MHz mode */
#define NUM_5GHZ_CENTRAL_FREQS_BW160     2  /* number of central freqs in 160 MHz mode */
#define NUM_TOTAL_CHAN_FREQS            (NUM_TOTAL_CHANS + NUM_5GHZ_CENTRAL_FREQS_BW40 +\
                                         NUM_5GHZ_CENTRAL_FREQS_BW80 + NUM_5GHZ_CENTRAL_FREQS_BW160)

typedef enum {
  MTLK_SCAN_AP,
  MTLK_SCAN_STA,
  MTLK_SCAN_SCHED_AP,
  MTLK_SCAN_SCHED_STA,
} mtlk_scan_type_e;

/* mimic both cfg80211_scan_request and cfg80211_sched_scan_request */
struct mtlk_scan_request
{
  void *saved_request; /* the original cfg80211_scan_request* (or sched_scan_request*) saved */
  void *wiphy;
  mtlk_scan_type_e type;
  u8 requester_vap_index;
  struct mtlk_channel channels[NUM_TOTAL_CHANS];
  u32 n_channels;
  u32 flags;
  u32 rates[NUM_SUPPORTED_BANDS];
  u32 interval;
  struct mtlk_match_set match_sets[MAX_MATCH_SETS];
  int n_match_sets;
  s32 min_rssi_thold;
  char ssids[MAX_SCAN_SSIDS][MIB_ESSID_LENGTH + 1];
  int n_ssids;
  u8 ie[MAX_SCAN_IE_LEN];
  size_t ie_len;
  mtlk_country_code_t country_code;
};

enum UMI_AC {
  UMI_AC_BE,
  UMI_AC_BK,
  UMI_AC_VI,
  UMI_AC_VO,
  UMI_AC_NUM_ACS
};

/* Conversion from NL80211 AC queue numbers to ours */
static __INLINE
unsigned nlac2mtlkac(unsigned ac)
{
  switch (ac)
  {
  case NL80211_AC_VO:
    return UMI_AC_VO;
  case NL80211_AC_VI:
    return UMI_AC_VI;
  case NL80211_AC_BE:
    return UMI_AC_BE;
  case NL80211_AC_BK:
    return UMI_AC_BK;
  default:
    MTLK_ASSERT(0);
  }

  return 0;
};

/* mimic ieee80211_txq_params*/
struct mtlk_txq_params
{
  u16 txop;
  u16 cwmin;
  u16 cwmax;
  u8 aifs;
  u8 acm_flag;
  u8 ac;
};

struct mtlk_wmm_settings
{
  unsigned vap_id;
  struct mtlk_txq_params txq_params;
};

struct mtlk_channel_status
{
  struct mtlk_channel *channel;
  u16 current_primary_chan_freq;
  u8 noise;
  u8 load;
};

typedef struct _mtlk_beacon_data_t
{
  void const *head;
  void const *tail;
  void const *probe_resp;
  void *data;
  uint32 dma_addr;
  uint16 head_len;
  uint16 tail_len;
  uint16 probe_resp_len;
  uint8 vap_idx;
  void *bmgr_priv;
} mtlk_beacon_data_t;

typedef struct _mtlk_radio_ap_tbl_item_t {
  const uint32            *abilities;
  uint32                   num_elems;
} mtlk_ab_tbl_item_t;

typedef enum
{
  MTLK_CORE_UI_ASSERT_TYPE_NONE,          /* Reset with */
  MTLK_CORE_UI_ASSERT_TYPE_FW_UMIPS,      /* 1 */
  MTLK_CORE_UI_ASSERT_TYPE_FW_LMIPS0,     /* 2 0 */
  MTLK_CORE_UI_ASSERT_TYPE_FW_LMIPS1,     /* 2 1 */
  MTLK_CORE_UI_ASSERT_TYPE_ALL_MACS,      /* 3 */
  MTLK_CORE_UI_ASSERT_TYPE_DRV_DIV0,      /* 4 */
  MTLK_CORE_UI_ASSERT_TYPE_DRV_BLOOP,     /* 5 */
  MTLK_CORE_UI_ASSERT_TYPE_LAST
} mtlk_core_ui_dbg_assert_type_e; /* MTLK_CORE_REQ_SET_MAC_ASSERT */

typedef enum
{
  MTLK_CORE_UI_RCVRY_TYPE_NONE = RCVRY_TYPE_NONE,
  MTLK_CORE_UI_RCVRY_TYPE_FAST = RCVRY_TYPE_FAST,
  MTLK_CORE_UI_RCVRY_TYPE_FULL = RCVRY_TYPE_FULL,
  MTLK_CORE_UI_RCVRY_TYPE_UNRECOVARABLE_ERROR = RCVRY_TYPE_UNRECOVARABLE_ERROR,
  MTLK_CORE_UI_RCVRY_TYPE_DUT = RCVRY_TYPE_DUT,
  MTLK_CORE_UI_RCVRY_TYPE_UNDEF = RCVRY_TYPE_UNDEF,
  MTLK_CORE_UI_RCVRY_TYPE_DBG = RCVRY_TYPE_DBG,
  MTLK_CORE_UI_RCVRY_TYPE_LAST
} mtlk_core_ui_wave_rcvry_type_e;


/* Fixed Rate */
typedef enum {
    MTLK_FIXED_RATE_CFG_SID = 0,
    MTLK_FIXED_RATE_CFG_AUTO,
    MTLK_FIXED_RATE_CFG_BW,
    MTLK_FIXED_RATE_CFG_PHYM,
    MTLK_FIXED_RATE_CFG_NSS,
    MTLK_FIXED_RATE_CFG_MCS,
    MTLK_FIXED_RATE_CFG_SCP,
    MTLK_FIXED_RATE_CFG_DCM,
    MTLK_FIXED_RATE_CFG_HE_EXTPARTIALBWDATA,
    MTLK_FIXED_RATE_CFG_HE_EXTPARTIALBWMNG,
    MTLK_FIXED_RATE_CFG_CHANGETYPE,
    MTLK_FIXED_RATE_CFG_SIZE
} mtlk_fixed_rate_cfg_e;

MTLK_DECLARE_CFG_START(mtlk_fixed_rate_cfg_t)
  MTLK_CFG_ITEM_ARRAY(uint32, params, MTLK_FIXED_RATE_CFG_SIZE);
MTLK_DECLARE_CFG_END(mtlk_fixed_rate_cfg_t)

MTLK_DECLARE_CFG_START(mtlk_static_plan_cfg_t)
  MTLK_CFG_ITEM(UMI_STATIC_PLAN_CONFIG, config);
MTLK_DECLARE_CFG_END(mtlk_static_plan_cfg_t)


/* SSB Mode Configuration */
typedef enum {
    MTLK_SSB_MODE_CFG_EN = 0,
    MTLK_SSB_MODE_CFG_20MODE,
    MTLK_SSB_MODE_CFG_SIZE
} mtlk_ssb_mode_cfg_e;

MTLK_DECLARE_CFG_START(mtlk_ssb_mode_cfg_t)
  MTLK_CFG_ITEM_ARRAY(uint8, params, MTLK_SSB_MODE_CFG_SIZE);
MTLK_DECLARE_CFG_END(mtlk_ssb_mode_cfg_t)

MTLK_DECLARE_CFG_START(mtlk_coex_cfg_t)
  MTLK_CFG_ITEM(uint8, coex_mode);
  MTLK_CFG_ITEM(uint8, coex_enable);
MTLK_DECLARE_CFG_END(mtlk_coex_cfg_t)

/* Frequency Jump Mode Configuration */
#define MTLK_FREQ_JUMP_MODE_DEFAULT  0
MTLK_DECLARE_CFG_START(mtlk_freq_jump_mode_cfg_t)
  MTLK_CFG_ITEM(uint32, freq_jump_mode)
MTLK_DECLARE_CFG_END(mtlk_freq_jump_mode_cfg_t)

/* Previous active channel configuration */
#define MTLK_CHANNEL_PREV_CFG_PRI_NUM_DEFAULT   0xFFFF
MTLK_DECLARE_CFG_START(mtlk_channel_prev_cfg_t)
  MTLK_CFG_ITEM(uint16, pri_chan_num);
  MTLK_CFG_ITEM(uint16, sec_chan_freq);
MTLK_DECLARE_CFG_END(mtlk_channel_prev_cfg_t)

#define MTLK_BLOCK_TX_DEFAULT  0

/* Restricted AC Mode Configuration */
#define MTLK_PD_THRESH_CFG_SIZE                        3
#define MTLK_RESTRICTED_AC_MODE_CFG_SIZE               4
#define MTLK_RESTRICTED_AC_MODE_BITMAP_MASK            0xF
#define MTLK_RESTRICTED_AC_MODE_THRESH_EXIT_MAX        4096

MTLK_DECLARE_CFG_START(mtlk_restricted_ac_mode_cfg_t)
  MTLK_CFG_ITEM(UMI_SET_PD_THRESH, pd_thresh_params);
  MTLK_CFG_ITEM(UMI_SET_RESTRICTED_AC, ac_mode_params);
MTLK_DECLARE_CFG_END(mtlk_restricted_ac_mode_cfg_t)

/* Dynamic MU Type Configuration */
#define WAVE_DYNAMIC_MU_TYPE_CFG_SIZE 5
#define WAVE_DYNAMIC_MU_TYPE_DL_MU_TYPE_MIN 0
#define WAVE_DYNAMIC_MU_TYPE_DL_MU_TYPE_MAX 3
#define WAVE_DYNAMIC_MU_TYPE_DL_MU_TYPE_DEFAULT WAVE_DYNAMIC_MU_TYPE_DL_MU_TYPE_MIN /*OFDMA*/
#define WAVE_DYNAMIC_MU_TYPE_UL_MU_TYPE_MIN 0
#define WAVE_DYNAMIC_MU_TYPE_UL_MU_TYPE_MAX 3
#define WAVE_DYNAMIC_MU_TYPE_UL_MU_TYPE_DEFAULT WAVE_DYNAMIC_MU_TYPE_UL_MU_TYPE_MIN /*OFDMA*/
#define WAVE_DYNAMIC_MU_TYPE_MIN_STA_IN_GROUP_NUM_DEFAULT 8
#define WAVE_DYNAMIC_MU_TYPE_MAX_STA_IN_GROUP_NUM_DEFAULT 8
#define WAVE_DYNAMIC_MU_TYPE_CDB_CFG_DEFAULT 0 /* ??? */
/* HE MU Fixed Parameters Configuration */
#define WAVE_HE_MU_FIXED_PARAMTERS_CFG_SIZE 4
#define WAVE_HE_MU_FIXED_PARAMTERS_MU_SEQUENCE_MIN 0
#define WAVE_HE_MU_FIXED_PARAMTERS_MU_SEQUENCE_MAX 4
#define WAVE_HE_MU_FIXED_PARAMTERS_MU_SEQUENCE_DEFAULT 0xFF /* AUTO */
#define WAVE_HE_MU_FIXED_PARAMTERS_LTF_GI_MIN 0
#define WAVE_HE_MU_FIXED_PARAMTERS_LTF_GI_MAX 5
#define WAVE_HE_MU_FIXED_PARAMTERS_LTF_GI_DEFAULT 0xFF /* AUTO */
#define WAVE_HE_MU_FIXED_PARAMTERS_CODING_TYPE_MIN 0
#define WAVE_HE_MU_FIXED_PARAMTERS_CODING_TYPE_MAX 1
#define WAVE_HE_MU_FIXED_PARAMTERS_CODING_TYPE_DEFAULT 0xFF /* AUTO */
#define WAVE_HE_MU_FIXED_PARAMTERS_HE_RATE_DEFAULT 0 /* ??? */
/* HE MU Duration Configuration */
#define WAVE_HE_MU_DURATION_CFG_SIZE 4
#define WAVE_HE_MU_DURATION_PPDU_DURATION_DEFAULT 0 /* AUTO */
#define WAVE_HE_MU_DURATION_TXOP_DURATION_DEFAULT 0 /* AUTO */
#define WAVE_HE_MU_DURATION_TF_LENGTH_DEFAULT 0 /* AUTO */
#define WAVE_HE_MU_DURATION_NUM_REPETITIONS_DEFAULT 0xFF /* AUTO */

MTLK_DECLARE_CFG_START(wave_dynamic_mu_cfg_t)
  MTLK_CFG_ITEM(UMI_DYNAMIC_MU_TYPE, dynamic_mu_type_params);
  MTLK_CFG_ITEM(UMI_HE_MU_FIXED_PARAMTERS, he_mu_fixed_params);
  MTLK_CFG_ITEM(UMI_HE_MU_DURATION, he_mu_duration_params);
MTLK_DECLARE_CFG_END(wave_dynamic_mu_cfg_t)

#define   MTLK_IDEFS_OFF
#include "mtlkidefs.h"

void wave_radio_abilities_disable_vap_ops(mtlk_vap_handle_t vap_handle);
void wave_radio_abilities_enable_vap_ops(mtlk_vap_handle_t vap_handle);
void wave_radio_abilities_disable_11b_abilities(mtlk_vap_handle_t vap_handle);
void wave_radio_abilities_enable_11b_abilities(mtlk_vap_handle_t vap_handle);
int  mtlk_core_dp_get_subif_cb (mtlk_vap_handle_t vap_handle,  char *mac_addr, uint32_t *subif);
BOOL mtlk_core_mcast_module_is_available(mtlk_core_t* nic);
void mtlk_core_mcast_group_id_action_serialized(mtlk_core_t* nic, mc_action_t action, int grp_id, uint8 *mac_addr, mtlk_mc_addr_t *mc_addr);
uint32 mtlk_core_mcast_handle_grid(mtlk_core_t* nic, mtlk_mc_addr_t *mc_addr, mc_grid_action_t action, int grp_id);
int mtlk_core_mcast_notify_fw(mtlk_vap_handle_t vap_handle, int action, int sta_id, int grp_id);

void mtlk_core_on_bss_drop_tx_que_full(mtlk_core_t *core);

#undef LOG_LOCAL_GID
#undef LOG_LOCAL_FID

#endif /* !__MTLK_COREUI_H__ */
