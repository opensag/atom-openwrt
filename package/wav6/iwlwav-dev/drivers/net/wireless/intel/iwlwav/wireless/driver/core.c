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
 * Core functionality
 *
 */
#include "mtlkinc.h"

#include "core_priv.h"
#include "mtlk_df.h"
#include "mtlk_df_user_priv.h"
#include "mtlk_df_priv.h"
#include "mtlk_coreui.h"
#include "core.h"
#include "core_config.h"
#include "core_stats.h"
#include "mtlkhal.h"
#include "drvver.h"
#include "mhi_mac_event.h"
#include "mtlk_packets.h"
#include "mtlkparams.h"
#include "nlmsgs.h"
#include "mtlk_snprintf.h"
#include "eeprom.h"
#include "bitrate.h"
#include "mtlk_fast_mem.h"
#include "mtlk_gpl_helper.h"
#include "mtlkaux.h"
#include "mtlk_param_db.h"
#include "mtlkwssa_drvinfo.h"
#ifdef MTLK_LEGACY_STATISTICS
#include "mtlk_wssd.h"
#endif /* MTLK_LEGACY_STATISTICS */
#include "wds.h"
#include "ta.h"
#include "core_common.h"
#include "mtlk_df_nbuf.h"
#include "bt_acs.h"
#include "mtlk_coc.h"
#include "vendor_cmds.h"

#ifdef CPTCFG_IWLWAV_PMCU_SUPPORT
  #include "mtlk_pcoc.h"
#endif /*CPTCFG_IWLWAV_PMCU_SUPPORT*/

#include "cfg80211.h"
#include "mac80211.h"
#include "wave_radio.h"
#include "fw_recovery.h"
#include "scan_support.h"
#include "mhi_umi.h"
#include "mcast.h"
#include "mtlk_hs20.h"
#include "eth_parser.h"
#include "core_pdb_def.h"

#define DEFAULT_NUM_RX_ANTENNAS (3)

#define LOG_LOCAL_GID   GID_CORE
#define LOG_LOCAL_FID   4

#define SLOG_DFLT_ORIGINATOR    0x11
#define SLOG_DFLT_RECEIVER      0x12

#define RCVRY_DEACTIVATE_VAPS_TIMEOUT       10000
#define RCVRY_PMCU_SYNC_TIMEOUT             3000
#define RCVRY_PMCU_COMPLETE_TIMEOUT         15000
#define RCVRY_PMCU_SYNC_CBK_TIMEOUT         500

#define MTLK_CORE_STA_LIST_HASH_NOF_BUCKETS          16
#define MTLK_CORE_WIDAN_BLACKLIST_HASH_NOF_BUCKETS   16
#define MTLK_CORE_4ADDR_STA_LIST_HASH_NOF_BUCKETS    16

#define MTLK_CORE_WIDAN_UNCONNECTED_STATION_RATE     140

static void
__log_set_mib_item(uint32 line, uint32 oid, uint32 mibid, uint32 val)
{
    ILOG2_DDDD("Line %4d: CID-%04x: Read Mib 0x%04x, Val 0x%02x", line, oid, mibid, val);
}

#define MTLK_CFG_SET_MIB_ITEM_BY_FUNC_VOID(obj,name,func,mibid,retdata,core) \
  MTLK_CFG_SET_ITEM_BY_FUNC_VOID(obj, name, func,(mtlk_vap_get_txmm(core->vap_handle), mibid,(retdata))); \
  __log_set_mib_item(__LINE__, mtlk_vap_get_oid(core->vap_handle), mibid, *(retdata))

#ifdef MTLK_LEGACY_STATISTICS
static int
_mtlk_core_on_peer_disconnect(mtlk_core_t *core,
                             sta_entry   *sta,
                             uint16       reason);
#endif /* MTLK_LEGACY_STATISTICS */

typedef enum __mtlk_core_async_priorities_t
{
  _MTLK_CORE_PRIORITY_MAINTENANCE = 0,
  _MTLK_CORE_PRIORITY_NETWORK,
  _MTLK_CORE_PRIORITY_INTERNAL,
  _MTLK_CORE_PRIORITY_USER,
  _MTLK_CORE_PRIORITY_EMERGENCY,
  _MTLK_CORE_NUM_PRIORITIES
} _mtlk_core_async_priorities_t;

#define SCAN_CACHE_AGEING (3600) /* 1 hour */

static const IEEE_ADDR EMPTY_MAC_ADDR = { {0x00, 0x00, 0x00, 0x00, 0x00, 0x00} };
static const IEEE_ADDR EMPTY_MAC_MASK = { {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF} };

#define TMP_BCAST_DEST_ADDR 0x3ffa

static const uint32 _mtlk_core_wss_id_map[] =
{
  MTLK_WWSS_WLAN_STAT_ID_BYTES_SENT_64,                                            /* MTLK_CORE_CNT_BYTES_SENT_64 */
  MTLK_WWSS_WLAN_STAT_ID_BYTES_RECEIVED_64,                                        /* MTLK_CORE_CNT_BYTES_RECEIVED_64 */
  MTLK_WWSS_WLAN_STAT_ID_PACKETS_SENT_64,                                          /* MTLK_CORE_CNT_PACKETS_SENT_64 */
  MTLK_WWSS_WLAN_STAT_ID_PACKETS_RECEIVED_64,                                      /* MTLK_CORE_CNT_PACKETS_RECEIVED_64 */

  MTLK_WWSS_WLAN_STAT_ID_UNICAST_PACKETS_SENT,                                     /* MTLK_CORE_CNT_UNICAST_PACKETS_SENT */
  MTLK_WWSS_WLAN_STAT_ID_MULTICAST_PACKETS_SENT,                                   /* MTLK_CORE_CNT_MULTICAST_PACKETS_SENT */
  MTLK_WWSS_WLAN_STAT_ID_BROADCAST_PACKETS_SENT,                                   /* MTLK_CORE_CNT_BROADCAST_PACKETS_SENT */
  MTLK_WWSS_WLAN_STAT_ID_UNICAST_BYTES_SENT,                                       /* MTLK_CORE_CNT_UNICAST_BYTES_SENT */
  MTLK_WWSS_WLAN_STAT_ID_MULTICAST_BYTES_SENT,                                     /* MTLK_CORE_CNT_MULTICAST_BYTES_SENT */
  MTLK_WWSS_WLAN_STAT_ID_BROADCAST_BYTES_SENT,                                     /* MTLK_CORE_CNT_BROADCAST_BYTES_SENT */

  MTLK_WWSS_WLAN_STAT_ID_UNICAST_PACKETS_RECEIVED,                                 /* MTLK_CORE_CNT_UNICAST_PACKETS_RECEIVED */
  MTLK_WWSS_WLAN_STAT_ID_MULTICAST_PACKETS_RECEIVED,                               /* MTLK_CORE_CNT_MULTICAST_PACKETS_RECEIVED */
  MTLK_WWSS_WLAN_STAT_ID_BROADCAST_PACKETS_RECEIVED,                               /* MTLK_CORE_CNT_BROADCAST_PACKETS_RECEIVED */
  MTLK_WWSS_WLAN_STAT_ID_UNICAST_BYTES_RECEIVED,                                   /* MTLK_CORE_CNT_UNICAST_BYTES_RECEIVED */
  MTLK_WWSS_WLAN_STAT_ID_MULTICAST_BYTES_RECEIVED,                                 /* MTLK_CORE_CNT_MULTICAST_BYTES_RECEIVED */
  MTLK_WWSS_WLAN_STAT_ID_BROADCAST_BYTES_RECEIVED,                                 /* MTLK_CORE_CNT_BROADCAST_BYTES_RECEIVED */

  MTLK_WWSS_WLAN_STAT_ID_ERROR_PACKETS_SENT,                                       /* MTLK_CORE_CNT_ERROR_PACKETS_SENT */
  MTLK_WWSS_WLAN_STAT_ID_ERROR_PACKETS_RECEIVED,                                   /* MTLK_CORE_CNT_ERROR_PACKETS_RECEIVED */
  MTLK_WWSS_WLAN_STAT_ID_DISCARD_PACKETS_RECEIVED,                                 /* MTLK_CORE_CNT_DISCARD_PACKETS_RECEIVED */

  MTLK_WWSS_WLAN_STAT_ID_TX_PROBE_RESP_SENT,                                       /* MTLK_CORE_CNT_TX_PROBE_RESP_SENT */
  MTLK_WWSS_WLAN_STAT_ID_TX_PROBE_RESP_DROPPED,                                    /* MTLK_CORE_CNT_TX_PROBE_RESP_DROPPED */
  MTLK_WWSS_WLAN_STAT_ID_BSS_MGMT_TX_QUE_FULL,                                     /* MTLK_CORE_CNT_BSS_MGMT_TX_QUE_FULL */

  MTLK_WWSS_WLAN_STAT_ID_MAN_FRAMES_RES_QUEUE,                                     /* MTLK_CORE_CNT_MAN_FRAMES_RES_QUEUE */
  MTLK_WWSS_WLAN_STAT_ID_MAN_FRAMES_SENT,                                          /* MTLK_CORE_CNT_MAN_FRAMES_SENT */
  MTLK_WWSS_WLAN_STAT_ID_MAN_FRAMES_CONFIRMED,                                     /* MTLK_CORE_CNT_MAN_FRAMES_CONFIRMED */

#if MTLK_MTIDL_WLAN_STAT_FULL
  MTLK_WWSS_WLAN_STAT_ID_RX_PACKETS_DISCARDED_DRV_TOO_OLD,                         /* MTLK_CORE_CNT_RX_PACKETS_DISCARDED_DRV_TOO_OLD */
  MTLK_WWSS_WLAN_STAT_ID_RX_PACKETS_DISCARDED_DRV_DUPLICATE,                       /* MTLK_CORE_CNT_RX_PACKETS_DISCARDED_DRV_DUPLICATE */
  MTLK_WWSS_WLAN_STAT_ID_TX_PACKETS_DISCARDED_DRV_NO_PEERS,                        /* MTLK_CORE_CNT_TX_PACKETS_DISCARDED_NO_PEERS                                   */
  MTLK_WWSS_WLAN_STAT_ID_TX_PACKETS_DISCARDED_DRV_ACM,                             /* MTLK_CORE_CNT_TX_PACKETS_DISCARDED_DRV_ACM                                    */
  MTLK_WWSS_WLAN_STAT_ID_TX_PACKETS_DISCARDED_DRV_EAPOL_CLONED,                    /* MTLK_CORE_CNT_TX_PACKETS_DISCARDED_EAPOL_CLONED                               */
  MTLK_WWSS_WLAN_STAT_ID_TX_PACKETS_DISCARDED_DRV_UNKNOWN_DESTINATION_DIRECTED,    /* MTLK_CORE_CNT_TX_PACKETS_DISCARDED_DRV_UNKNOWN_DESTINATION_DIRECTED           */
  MTLK_WWSS_WLAN_STAT_ID_TX_PACKETS_DISCARDED_DRV_UNKNOWN_DESTINATION_MCAST,       /* MTLK_CORE_CNT_TX_PACKETS_DISCARDED_DRV_UNKNOWN_DESTINATION_MCAST              */
  MTLK_WWSS_WLAN_STAT_ID_TX_PACKETS_DISCARDED_DRV_NO_RESOURCES,                    /* MTLK_CORE_CNT_TX_PACKETS_DISCARDED_DRV_NO_RESOURCES                           */
  MTLK_WWSS_WLAN_STAT_ID_TX_PACKETS_DISCARDED_DRV_SQ_OVERFLOW,                     /* MTLK_CORE_CNT_TX_PACKETS_DISCARDED_SQ_OVERFLOW                                */
  MTLK_WWSS_WLAN_STAT_ID_TX_PACKETS_DISCARDED_DRV_EAPOL_FILTER,                    /* MTLK_CORE_CNT_TX_PACKETS_DISCARDED_EAPOL_FILTER                               */
  MTLK_WWSS_WLAN_STAT_ID_TX_PACKETS_DISCARDED_DRV_DROP_ALL_FILTER,                 /* MTLK_CORE_CNT_TX_PACKETS_DISCARDED_DROP_ALL_FILTER                            */
  MTLK_WWSS_WLAN_STAT_ID_TX_PACKETS_DISCARDED_DRV_TX_QUEUE_OVERFLOW,               /* MTLK_CORE_CNT_TX_PACKETS_DISCARDED_TX_QUEUE_OVERFLOW                          */
  MTLK_WWSS_WLAN_STAT_ID_802_1X_PACKETS_RECEIVED,                                  /* MTLK_CORE_CNT_802_1X_PACKETS_RECEIVED                                         */
  MTLK_WWSS_WLAN_STAT_ID_802_1X_PACKETS_SENT,                                      /* MTLK_CORE_CNT_802_1X_PACKETS_SENT                                             */
  MTLK_WWSS_WLAN_STAT_ID_802_1X_PACKETS_DISCARDED,                                 /* MTLK_CORE_CNT_802_1X_PACKETS_DISCARDED                                        */
  MTLK_WWSS_WLAN_STAT_ID_PAIRWISE_MIC_FAILURE_PACKETS,                              /* MTLK_CORE_CNT_PAIRWISE_MIC_FAILURE_PACKETS */
  MTLK_WWSS_WLAN_STAT_ID_GROUP_MIC_FAILURE_PACKETS,                                 /* MTLK_CORE_CNT_GROUP_MIC_FAILURE_PACKETS */
  MTLK_WWSS_WLAN_STAT_ID_UNICAST_REPLAYED_PACKETS,                                  /* MTLK_CORE_CNT_UNICAST_REPLAYED_PACKETS */
  MTLK_WWSS_WLAN_STAT_ID_MULTICAST_REPLAYED_PACKETS,                                /* MTLK_CORE_CNT_MULTICAST_REPLAYED_PACKETS */
  MTLK_WWSS_WLAN_STAT_ID_MANAGEMENT_REPLAYED_PACKETS,                               /* MTLK_CORE_CNT_MANAGEMENT_REPLAYED_PACKETS */
  MTLK_WWSS_WLAN_STAT_ID_FWD_RX_PACKETS,                                            /* MTLK_CORE_CNT_FWD_RX_PACKETS */
  MTLK_WWSS_WLAN_STAT_ID_FWD_RX_BYTES,                                              /* MTLK_CORE_CNT_FWD_RX_BYTES */
  MTLK_WWSS_WLAN_STAT_ID_DAT_FRAMES_RECEIVED,                                       /* MTLK_CORE_CNT_DAT_FRAMES_RECEIVED */
  MTLK_WWSS_WLAN_STAT_ID_CTL_FRAMES_RECEIVED,                                       /* MTLK_CORE_CNT_CTL_FRAMES_RECEIVED */
  MTLK_WWSS_WLAN_STAT_ID_MAN_FRAMES_RECEIVED,                                       /* MTLK_CORE_CNT_MAN_FRAMES_RECEIVED */
  MTLK_WWSS_WLAN_STAT_ID_RX_MAN_FRAMES_RETRY_DROPPED,                               /* MTLK_CORE_CNT_RX_MAN_FRAMES_RETRY_DROPPED */
  MTLK_WWSS_WLAN_STAT_ID_MAN_FRAMES_CFG80211_FAILED,                                /* MTLK_CORE_CNT_MAN_FRAMES_CFG80211_FAILED */
  MTLK_WWSS_WLAN_STAT_ID_NOF_COEX_EL_RECEIVED,                                      /* MTLK_CORE_CNT_COEX_EL_RECEIVED */
  MTLK_WWSS_WLAN_STAT_ID_NOF_COEX_EL_SCAN_EXEMPTION_REQUESTED,                      /* MTLK_CORE_CNT_COEX_EL_SCAN_EXEMPTION_REQUESTED */
  MTLK_WWSS_WLAN_STAT_ID_NOF_COEX_EL_SCAN_EXEMPTION_GRANTED,                        /* MTLK_CORE_CNT_COEX_EL_SCAN_EXEMPTION_GRANTED */
  MTLK_WWSS_WLAN_STAT_ID_NOF_COEX_EL_SCAN_EXEMPTION_GRANT_CANCELLED,                /* MTLK_CORE_CNT_COEX_EL_SCAN_EXEMPTION_GRANT_CANCELLED */
  MTLK_WWSS_WLAN_STAT_ID_NOF_CHANNEL_SWITCH_20_TO_40,                               /* MTLK_CORE_CNT_CHANNEL_SWITCH_20_TO_40 */
  MTLK_WWSS_WLAN_STAT_ID_NOF_CHANNEL_SWITCH_40_TO_20,                               /* MTLK_CORE_CNT_CHANNEL_SWITCH_40_TO_20 */
  MTLK_WWSS_WLAN_STAT_ID_NOF_CHANNEL_SWITCH_40_TO_40,                               /* MTLK_CORE_CNT_CHANNEL_SWITCH_40_TO_40 */
  MTLK_WWSS_WLAN_STAT_TX_PACKETS_TO_UNICAST_DGAF_DISABLED,                          /* MTLK_CORE_CNT_TX_PACKETS_TO_UNICAST_DGAF_DISABLED */
  MTLK_WWSS_WLAN_STAT_TX_PACKETS_SKIPPED_DGAF_DISABLED,                             /* MTLK_CORE_CNT_TX_PACKETS_SKIPPED_DGAF_DISABLED */
#endif /* MTLK_MTIDL_WLAN_STAT_FULL */
};

/* API between Core and HW */
static int
_mtlk_core_start (mtlk_vap_handle_t vap_handle);
static int
_mtlk_core_handle_tx_data (mtlk_core_t* nic, mtlk_core_handle_tx_data_t *data, uint32 nbuf_flags) __MTLK_INT_HANDLER_SECTION;
static int
_mtlk_core_release_tx_data (mtlk_vap_handle_t vap_handle, mtlk_hw_data_req_mirror_t *data_req) __MTLK_INT_HANDLER_SECTION;
static int
_mtlk_core_handle_rx_data (mtlk_vap_handle_t vap_handle, mtlk_core_handle_rx_data_t *data) __MTLK_INT_HANDLER_SECTION;
static int
_mtlk_core_handle_rx_bss (mtlk_vap_handle_t vap_handle, mtlk_core_handle_rx_bss_t *data);
static void
_mtlk_core_handle_rx_ctrl (mtlk_vap_handle_t vap_handle, uint32 id, void *payload, uint32 payload_buffer_size);
static int
_mtlk_core_get_prop (mtlk_vap_handle_t vap_handle, mtlk_core_prop_e prop_id, void* buffer, uint32 size);
static int
_mtlk_core_set_prop (mtlk_vap_handle_t vap_handle, mtlk_core_prop_e prop_id, void *buffer, uint32 size);
static void
_mtlk_core_stop (mtlk_vap_handle_t vap_handle);
static void
_mtlk_core_prepare_stop (mtlk_vap_handle_t vap_handle);
static int _core_sync_done(mtlk_handle_t hcore, const void* data, uint32 data_size);
static BOOL
_mtlk_core_blacklist_frame_drop (mtlk_core_t *nic,
  const IEEE_ADDR *addr, unsigned subtype, int8 rx_snr_db, BOOL isbroadcast);

static mtlk_core_vft_t const core_vft = {
  _mtlk_core_start,
  _mtlk_core_handle_tx_data,
  _mtlk_core_release_tx_data,
  _mtlk_core_handle_rx_data,
  _mtlk_core_handle_rx_bss,
  _mtlk_core_handle_rx_ctrl,
  _mtlk_core_get_prop,
  _mtlk_core_set_prop,
  _mtlk_core_stop,
  _mtlk_core_prepare_stop
};

typedef struct {
  uint8    *frame;
  int      size;
  int      freq;
  int      sig_dbm;
  unsigned subtype;
  BOOL     probe_req_wps_ie;
  BOOL     probe_req_interworking_ie;
  BOOL     probe_req_vsie;
  BOOL     probe_req_he_ie;
  uint8    pmf_flags;
  mtlk_phy_info_t  phy_info; /* FIXME: could be a pointer to */
} mtlk_mngmnt_frame_t;

/* API between Core and DF UI */

static int
_mtlk_core_get_hw_limits(mtlk_handle_t hcore, const void* data, uint32 data_size);
static int
_mtlk_core_get_tx_rate_power(mtlk_handle_t hcore, const void* data, uint32 data_size);
static int
_mtlk_core_get_ee_caps(mtlk_handle_t hcore, const void* data, uint32 data_size);
static int
_mtlk_core_get_stadb_sta_list(mtlk_handle_t hcore, const void* data, uint32 data_size);
static int
_mtlk_core_get_stadb_sta_by_iter_id(mtlk_handle_t hcore, const void* data, uint32 data_size);
static int
_mtlk_core_set_mac_assert(mtlk_handle_t hcore, const void* data, uint32 data_size);
static int
_mtlk_core_get_mc_igmp_tbl(mtlk_handle_t hcore, const void* data, uint32 data_size);
static int
_mtlk_core_get_mc_hw_tbl(mtlk_handle_t hcore, const void* data, uint32 data_size);
static int
_mtlk_core_bcl_mac_data_get(mtlk_handle_t hcore, const void* data, uint32 data_size);
static int
_mtlk_core_bcl_mac_data_set(mtlk_handle_t hcore, const void* data, uint32 data_size);
static int
_mtlk_core_range_info_get (mtlk_handle_t hcore, const void* data, uint32 data_size);
static int
_mtlk_core_connect_sta(mtlk_handle_t hcore, const void* data, uint32 data_size);
#ifdef MTLK_LEGACY_STATISTICS
static int
handleDisconnectMe(mtlk_handle_t core_object, const void *payload, uint32 size);
#endif
static int
_mtlk_core_get_enc_ext_cfg(mtlk_handle_t hcore, const void* data, uint32 data_size);
static int
_mtlk_core_set_enc_ext_cfg(mtlk_handle_t hcore, const void* data, uint32 data_size);
static int
_mtlk_core_set_default_key_cfg(mtlk_handle_t hcore, const void* data, uint32 data_size);
static int
_mtlk_core_get_status(mtlk_handle_t hcore, const void* data, uint32 data_size);
static int
_mtlk_core_get_hs20_info(mtlk_handle_t hcore, const void* data, uint32 data_size);
static int
_mtlk_core_set_qamplus_mode(mtlk_handle_t hcore, const void* data, uint32 data_size);
static int
_mtlk_core_get_qamplus_mode(mtlk_handle_t hcore, const void* data, uint32 data_size);
static int
_mtlk_core_qamplus_mode_req(mtlk_core_t *master_core, const uint32 qamplus_mode);
static int
_mtlk_core_set_radio_mode(mtlk_handle_t hcore, const void* data, uint32 data_size);
static int
_mtlk_core_get_radio_mode(mtlk_handle_t hcore, const void* data, uint32 data_size);
static int
_mtlk_core_set_aggr_cfg(mtlk_handle_t hcore, const void* data, uint32 data_size);
static int
_mtlk_core_get_aggr_cfg(mtlk_handle_t hcore, const void* data, uint32 data_size);
static int
_mtlk_core_set_amsdu_num(mtlk_handle_t hcore, const void* data, uint32 data_size);
static int
_mtlk_core_get_amsdu_num(mtlk_handle_t hcore, const void* data, uint32 data_size);
static int _mtlk_core_set_blacklist_entry(mtlk_handle_t hcore, const void* data, uint32 data_size);
static int _mtlk_core_get_blacklist_entries(mtlk_handle_t hcore, const void* data, uint32 data_size);
static int _mtlk_core_get_station_measurements(mtlk_handle_t hcore, const void *data, uint32 data_size);
static int _mtlk_core_get_vap_measurements(mtlk_handle_t hcore, const void *data, uint32 data_size);
static int _mtlk_core_get_radio_info(mtlk_handle_t hcore, const void *data, uint32 data_size);
static int _mtlk_core_get_unconnected_station(mtlk_handle_t hcore, const void* data, uint32 data_size);
static int _mtlk_core_check_4addr_mode(mtlk_handle_t hcore, const void *data, uint32 data_size);
static int
_mtlk_core_set_softblocklist_entry(mtlk_handle_t hcore, const void* data, uint32 data_size);

/* Core utilities */
static void
mtlk_core_configuration_dump(mtlk_core_t *core);
static uint32
mtlk_core_get_available_bitrates (struct nic *nic);

int
mtlk_core_update_network_mode(mtlk_core_t* nic, uint8 net_mode);

static uint8 _mtlk_core_get_spectrum_mode(mtlk_core_t *core);
static int _mtlk_core_set_fw_interfdet_req(mtlk_core_t *core, BOOL is_spectrum_40);
int mtlk_core_init_defaults (mtlk_core_t *core);

static void _mtlk_core_get_traffic_wlan_stats(mtlk_core_t* core, mtlk_wssa_drv_traffic_stats_t* stats);
static void _mtlk_core_get_vap_info_stats(mtlk_core_t* core, struct driver_vap_info *vap_info);
static void _mtlk_core_get_tr181_hw(mtlk_core_t* core,  mtlk_wssa_drv_tr181_hw_t* tr181_hw);
#ifdef MTLK_LEGACY_STATISTICS
static void _mtlk_core_get_tr181_wlan_stats (mtlk_core_t* core, mtlk_wssa_drv_tr181_wlan_stats_t* stats);
static void _mtlk_core_get_wlan_stats (mtlk_core_t* core, mtlk_wssa_drv_wlan_stats_t* stats);
#if MTLK_MTIDL_WLAN_STAT_FULL
static void _mtlk_core_get_debug_wlan_stats(mtlk_core_t* core, mtlk_wssa_drv_debug_wlan_stats_t* stats);
#endif
#endif /* MTLK_LEGACY_STATISTICS */

#ifdef CPTCFG_IWLWAV_FILTER_BLACKLISTED_BSS
static BOOL _mtlk_core_blacklist_entry_exists (mtlk_core_t *nic, const IEEE_ADDR *addr);
#endif

int fill_channel_data(mtlk_core_t *core, struct intel_vendor_channel_data *ch_data);



/* with checking ALLOWED option */
#define _mtlk_core_get_cnt(core, id)        (TRUE == id##_ALLOWED) ? __mtlk_core_get_cnt(core, id) : 0
#define _mtlk_core_reset_cnt(core, id)      if (TRUE == id##_ALLOWED) __mtlk_core_reset_cnt(core, id)

static __INLINE uint32
__mtlk_core_get_cnt (mtlk_core_t       *core,
                    mtlk_core_wss_cnt_id_e cnt_id)
{
  MTLK_ASSERT(cnt_id >= 0 && cnt_id < MTLK_CORE_CNT_LAST);

  return mtlk_wss_get_stat(core->wss, cnt_id);
}

static __INLINE void
__mtlk_core_reset_cnt (mtlk_core_t *core, mtlk_core_wss_cnt_id_e cnt_id)
{
  MTLK_ASSERT(cnt_id >= 0 && cnt_id < MTLK_CORE_CNT_LAST);

  mtlk_wss_reset_stat(core->wss, cnt_id);
}

static __INLINE void
_mtlk_core_on_mic_failure (mtlk_core_t       *core,
                           mtlk_df_ui_mic_fail_type_t mic_fail_type)
{
  MTLK_ASSERT((MIC_FAIL_PAIRWISE == mic_fail_type) || (MIC_FAIL_GROUP== mic_fail_type));
  switch(mic_fail_type) {
  case MIC_FAIL_PAIRWISE:
    mtlk_core_inc_cnt(core, MTLK_CORE_CNT_PAIRWISE_MIC_FAILURE_PACKETS);
    break;
  case MIC_FAIL_GROUP:
    mtlk_core_inc_cnt(core, MTLK_CORE_CNT_GROUP_MIC_FAILURE_PACKETS);
    break;
  default:
    WLOG_D("CID-%04x: Wrong type of pairwise packet", mtlk_vap_get_oid(core->vap_handle));
    break;
  }
}

static __INLINE BOOL
_mtlk_core_has_connections(mtlk_core_t *core)
{
  return !mtlk_stadb_is_empty(&core->slow_ctx->stadb);
};

BOOL __MTLK_IFUNC
mtlk_core_has_connections(mtlk_core_t *core)
{
  return _mtlk_core_has_connections(core);
};

static __INLINE unsigned
_mtlk_core_get_rrsi_offs (mtlk_core_t *nic)
{
    return mtlk_hw_get_rrsi_offs(mtlk_vap_get_hw(nic->vap_handle));
}

/* ======================================================*/
/* Core internal wrapper for asynchronous execution.     */
/* Uses serializer, command can't be tracked/canceled,   */
/* allocated on heap and deleted by completion callback. */
static void
_mtlk_core_async_clb(mtlk_handle_t user_context)
{
  int res = MTLK_ERR_BUSY;
  _core_async_exec_t *ctx = (_core_async_exec_t *) user_context;

  if (ctx->cmd.is_cancelled) {
    mtlk_slid_t slid = ctx->cmd.issuer_slid;
    MTLK_UNREFERENCED_PARAM(slid);
    WLOG_DDDD("CID-%04x: Core request was cancelled (GID=%d, FID=%d, LID=%d)",
              mtlk_vap_get_oid(ctx->vap_handle),
              mtlk_slid_get_gid(slid),
              mtlk_slid_get_fid(slid),
              mtlk_slid_get_lid(slid));
    res = MTLK_ERR_CANCELED;
  }
  else if (_mtlk_abmgr_is_ability_enabled(mtlk_vap_get_abmgr(ctx->vap_handle),
                                          ctx->ability_id))
  {
    res = ctx->func(ctx->receiver, &ctx[1], ctx->data_size);
  }
  else
  {
    WLOG_DD("CID-%04x: Requested ability 0x%X is disabled or never was registered",
           mtlk_vap_get_oid(ctx->vap_handle), ctx->ability_id);
  }

  if(NULL != ctx->user_req)
    mtlk_df_ui_req_complete(ctx->user_req, res);
}

static void
_mtlk_core_async_compl_clb(serializer_result_t res,
                           mtlk_command_t* command,
                           mtlk_handle_t completion_ctx)
{
  _core_async_exec_t *ctx = (_core_async_exec_t *) completion_ctx;

  mtlk_command_cleanup(&ctx->cmd);
  mtlk_osal_mem_free(ctx);
}

static int
_mtlk_core_execute_async_ex (struct nic *nic, mtlk_ability_id_t ability_id, mtlk_handle_t receiver,
                             mtlk_core_task_func_t func, const void *data, size_t size,
                             _mtlk_core_async_priorities_t priority,
                             mtlk_user_request_t *req,
                             mtlk_slid_t issuer_slid)
{
  int res;
  _core_async_exec_t *ctx;

  MTLK_ASSERT(0 == sizeof(_core_async_exec_t) % sizeof(void*));

  ctx = mtlk_osal_mem_alloc(sizeof(_core_async_exec_t) + size,
                            MTLK_MEM_TAG_ASYNC_CTX);
  if(NULL == ctx)
  {
    ELOG_D("CID-%04x: Failed to allocate execution context object", mtlk_vap_get_oid(nic->vap_handle));
    return MTLK_ERR_NO_MEM;
  }

  ctx->receiver     = receiver;
  ctx->data_size    = size;
  ctx->func         = func;
  ctx->user_req     = req;
  ctx->vap_handle   = nic->vap_handle;
  ctx->ability_id   = ability_id;
  wave_memcpy(&ctx[1], size, data, size);

  res = mtlk_command_init(&ctx->cmd, _mtlk_core_async_clb, HANDLE_T(ctx), issuer_slid);
  if(MTLK_ERR_OK != res)
  {
    mtlk_osal_mem_free(ctx);
    ELOG_D("CID-%04x: Failed to initialize command object", mtlk_vap_get_oid(nic->vap_handle));
    return res;
  }

  res = mtlk_serializer_enqueue(&nic->slow_ctx->serializer, priority,
                                &ctx->cmd, _mtlk_core_async_compl_clb,
                                HANDLE_T(ctx));
  if(MTLK_ERR_OK != res)
  {
    mtlk_osal_mem_free(ctx);
    ELOG_DD("CID-%04x: Failed to enqueue command object (error: %d)", mtlk_vap_get_oid(nic->vap_handle), res);
    return res;
  }

  return res;
}

#define _mtlk_core_execute_async(nic, ability_id, receiver, func, data, size, priority, req) \
  _mtlk_core_execute_async_ex((nic), (ability_id), (receiver), (func), (data), (size), (priority), (req), MTLK_SLID)

int __MTLK_IFUNC mtlk_core_schedule_internal_task_ex (struct nic *nic,
                                                      mtlk_handle_t object,
                                                      mtlk_core_task_func_t func,
                                                      const void *data, size_t size,
                                                      mtlk_slid_t issuer_slid)
{
  return _mtlk_core_execute_async_ex(nic, MTLK_ABILITY_NONE, object, func, data, size,
                                     _MTLK_CORE_PRIORITY_INTERNAL, NULL, issuer_slid);
}

int __MTLK_IFUNC mtlk_core_schedule_user_task_ex (struct nic *nic,
                                                  mtlk_handle_t object,
                                                  mtlk_core_task_func_t func,
                                                  const void *data, size_t size,
                                                  mtlk_slid_t issuer_slid)
{
  return _mtlk_core_execute_async_ex(nic, MTLK_ABILITY_NONE, object, func, data, size,
                                     _MTLK_CORE_PRIORITY_USER, NULL, issuer_slid);
}

/*! Function for scheduling out of order (emergency) task.
    Sends message confirmation for message object specified

    \param   nic              Pointer to the core object
    \param   object           Handle of receiver object
    \param   func             Task callback
    \param   data             Pointer to the data buffer provided by caller
    \param   data_size        Size of data buffer provided by caller

*/
int __MTLK_IFUNC mtlk_core_schedule_emergency_task (struct nic *nic,
                                                    mtlk_handle_t object,
                                                    mtlk_core_task_func_t func,
                                                    const void *data, size_t size,
                                                    mtlk_slid_t issuer_slid)
{
  return _mtlk_core_execute_async_ex(nic, MTLK_ABILITY_NONE, object, func, data, size,
                                     _MTLK_CORE_PRIORITY_EMERGENCY, NULL, issuer_slid);
}

/*! Function for scheduling serialized task on demand of HW module activities
    Sends message confirmation for message object specified

    \param   nic              Pointer to the core object
    \param   object           Handle of receiver object
    \param   func             Task callback
    \param   data             Pointer to the data buffer provided by caller
    \param   data_size        Size of data buffer provided by caller

*/
int __MTLK_IFUNC mtlk_core_schedule_hw_task(struct nic *nic,
                                            mtlk_handle_t object,
                                            mtlk_core_task_func_t func,
                                            const void *data, size_t size,
                                            mtlk_slid_t issuer_slid)
{
  return _mtlk_core_execute_async_ex(nic, MTLK_ABILITY_NONE, object, func, data, size,
                                     _MTLK_CORE_PRIORITY_NETWORK, NULL, issuer_slid);
}
/* ======================================================*/

/* ======================================================*/
/* Function for processing HW tasks                      */

typedef enum __mtlk_hw_task_type_t
{
  SYNCHRONOUS,
  SERIALIZABLE
} _mtlk_core_task_type_t;

static void
_mtlk_process_hw_task_ex (mtlk_core_t* nic,
                          _mtlk_core_task_type_t task_type, mtlk_core_task_func_t task_func,
                          mtlk_handle_t object, const void* data, uint32 data_size, mtlk_slid_t issuer_slid)
{
    if(SYNCHRONOUS == task_type)
    {
      task_func(object, data, data_size);
    }
    else
    {
      if(MTLK_ERR_OK != mtlk_core_schedule_hw_task(nic, object,
                                                   task_func, data, data_size, issuer_slid))
      {
        ELOG_DP("CID-%04x: Hardware task schedule for callback 0x%p failed.", mtlk_vap_get_oid(nic->vap_handle), task_func);
      }
    }
}

#define _mtlk_process_hw_task(nic, task_type, task_func, object, data, data_size) \
  _mtlk_process_hw_task_ex((nic), (task_type), (task_func), (object), (data), (data_size), MTLK_SLID)

static void
_mtlk_process_user_task_ex (mtlk_core_t* nic, mtlk_user_request_t *req,
                            _mtlk_core_task_type_t task_type, mtlk_ability_id_t ability_id,
                            mtlk_core_task_func_t task_func,
                            mtlk_handle_t object, mtlk_clpb_t* data, mtlk_slid_t issuer_slid)
{
    if(SYNCHRONOUS == task_type)
    {
      int res = MTLK_ERR_BUSY;
      /* check is ability enabled for execution */
      if (_mtlk_abmgr_is_ability_enabled(mtlk_vap_get_abmgr(nic->vap_handle), ability_id)) {
        res = task_func(object, &data, sizeof(mtlk_clpb_t*));
      }
      else
      {
        WLOG_DD("CID-%04x: Requested ability 0x%X is disabled or never was registered",
                mtlk_vap_get_oid(nic->vap_handle), ability_id);
      }


      mtlk_df_ui_req_complete(req, res);
    }
    else
    {
      int result = _mtlk_core_execute_async_ex(nic, ability_id, object, task_func,
                                               &data, sizeof(data), _MTLK_CORE_PRIORITY_USER, req,
                                               issuer_slid);

      if(MTLK_ERR_OK != result)
      {
        ELOG_DPD("CID-%04x: User task schedule for callback 0x%p failed (error %d).",
                 mtlk_vap_get_oid(nic->vap_handle), task_func, result);
        mtlk_df_ui_req_complete(req, result);
      }
    }
}

#define _mtlk_process_user_task(nic, req, task_type, ability_id, task_func, object, data) \
  _mtlk_process_user_task_ex((nic), (req), (task_type), (ability_id), (task_func), (object), (data), MTLK_SLID)

static void
_mtlk_process_emergency_task_ex (mtlk_core_t* nic,
                                 mtlk_core_task_func_t task_func,
                                 mtlk_handle_t object, const void* data, uint32 data_size, mtlk_slid_t issuer_slid)
{
  if (MTLK_ERR_OK != mtlk_core_schedule_emergency_task(nic, object,
                                                      task_func, data, data_size, issuer_slid)) {
    ELOG_DP("CID-%04x: Emergency task schedule for callback 0x%p failed.", mtlk_vap_get_oid(nic->vap_handle), task_func);
  }
}

#define _mtlk_process_emergency_task(nic, task_func, object, data, data_size) \
  _mtlk_process_emergency_task_ex((nic), (task_func), (object), (data), (data_size), MTLK_SLID)

/* ======================================================*/
static void cleanup_on_disconnect(struct nic *nic);

mtlk_eeprom_data_t* __MTLK_IFUNC
mtlk_core_get_eeprom(mtlk_core_t* core)
{
  mtlk_eeprom_data_t *ee_data = NULL;

  (void)mtlk_hw_get_prop(mtlk_vap_get_hwapi(core->vap_handle), MTLK_HW_PROP_EEPROM_DATA, &ee_data, sizeof(&ee_data));

  return ee_data;
}

mtlk_scan_support_t* __MTLK_IFUNC
mtlk_core_get_scan_support(mtlk_core_t* core)
{
  return wave_radio_scan_support_get(wave_vap_radio_get(core->vap_handle));
}

const char *mtlk_net_state_to_string(uint32 state)
{
  switch (state) {
  case NET_STATE_HALTED:
    return "NET_STATE_HALTED";
  case NET_STATE_IDLE:
    return "NET_STATE_IDLE";
  case NET_STATE_READY:
    return "NET_STATE_READY";
  case NET_STATE_ACTIVATING:
    return "NET_STATE_ACTIVATING";
  case NET_STATE_CONNECTED:
    return "NET_STATE_CONNECTED";
  case NET_STATE_DEACTIVATING:
    return "NET_STATE_DEACTIVATING";
  default:
    break;
  }
  ILOG1_D("Unknown state 0x%04X", state);
  return "NET_STATE_UNKNOWN";
}


static __INLINE mtlk_hw_state_e
__mtlk_core_get_hw_state (mtlk_core_t *nic)
{
  mtlk_hw_state_e hw_state = MTLK_HW_STATE_LAST;

  mtlk_hw_get_prop(mtlk_vap_get_hwapi(nic->vap_handle), MTLK_HW_PROP_STATE, &hw_state, sizeof(hw_state));
  return hw_state;
}

mtlk_hw_state_e __MTLK_IFUNC
mtlk_core_get_hw_state (mtlk_core_t *nic)
{
  return __mtlk_core_get_hw_state(nic);
}

int __MTLK_IFUNC
mtlk_set_hw_state (mtlk_core_t *nic, mtlk_hw_state_e st)
{
#if IWLWAV_RTLOG_MAX_DLEVEL >= 1
  mtlk_hw_state_e ost;
  (void)mtlk_hw_get_prop(mtlk_vap_get_hwapi(nic->vap_handle), MTLK_HW_PROP_STATE, &ost, sizeof(ost));
  ILOG1_DD("%i -> %i", ost, st);
#endif
  return mtlk_hw_set_prop(mtlk_vap_get_hwapi(nic->vap_handle), MTLK_HW_PROP_STATE, &st, sizeof(st));
}

BOOL mtlk_core_is_hw_halted(mtlk_core_t *nic)
{
  mtlk_hw_state_e hw_state = mtlk_core_get_hw_state(nic);
  return mtlk_hw_is_halted(hw_state);
}

static int
mtlk_core_set_net_state(mtlk_core_t *core, uint32 new_state)
{
  uint32 allow_mask;
  mtlk_hw_state_e hw_state;
  int result = MTLK_ERR_OK;

  mtlk_osal_lock_acquire(&core->net_state_lock);
  if (new_state == NET_STATE_HALTED) {
    ILOG3_SSS("%s: Going from %s to %s", mtlk_df_get_name(mtlk_vap_get_df(core->vap_handle)),
              mtlk_net_state_to_string(core->net_state), "NET_STATE_HALTED");
    core->net_state = NET_STATE_HALTED;
    goto FINISH;
  }
  /* allow transition from NET_STATE_HALTED to NET_STATE_IDLE
     while in hw state MTLK_HW_STATE_READY */
  hw_state = __mtlk_core_get_hw_state(core);
  if ((hw_state != MTLK_HW_STATE_READY) && (hw_state != MTLK_HW_STATE_UNLOADING) &&
      (new_state != NET_STATE_IDLE)) {
    ELOG_DD("CID-%04x: Wrong hw_state=%d", mtlk_vap_get_oid(core->vap_handle), hw_state);
    result = MTLK_ERR_HW;
    goto FINISH;
  }
  allow_mask = 0;
  switch (new_state) {
  case NET_STATE_IDLE:
    allow_mask = NET_STATE_HALTED; /* on core_start */
    break;
  case NET_STATE_READY:
    allow_mask = NET_STATE_IDLE | NET_STATE_ACTIVATING | NET_STATE_DEACTIVATING;
    break;
  case NET_STATE_ACTIVATING:
    allow_mask = NET_STATE_READY | NET_STATE_DEACTIVATING; /* because activating/disconnecting may morph into one another */
    break;
  case NET_STATE_CONNECTED:
    allow_mask = NET_STATE_ACTIVATING;
    break;
  case NET_STATE_DEACTIVATING:
    allow_mask = NET_STATE_CONNECTED | NET_STATE_ACTIVATING; /* because activating/disconnecting may morph into one another */
    break;
  default:
    break;
  }
  /* check mask */
  if (core->net_state & allow_mask) {
    ILOG0_SSS("%s: Going from %s to %s", mtlk_df_get_name(mtlk_vap_get_df(core->vap_handle)),
              mtlk_net_state_to_string(core->net_state), mtlk_net_state_to_string(new_state));
    core->net_state = new_state;
  } else {
    ILOG0_SSS("%s: Failed to change state from %s to %s", mtlk_df_get_name(mtlk_vap_get_df(core->vap_handle)),
              mtlk_net_state_to_string(core->net_state), mtlk_net_state_to_string(new_state));
    result = MTLK_ERR_WRONG_CONTEXT;
  }
FINISH:
  mtlk_osal_lock_release(&core->net_state_lock);
  return result;
}

int __MTLK_IFUNC
mtlk_core_get_net_state (mtlk_core_t *core)
{
  mtlk_hw_state_e hw_state = __mtlk_core_get_hw_state(core);
  if (hw_state != MTLK_HW_STATE_READY && hw_state != MTLK_HW_STATE_UNLOADING) {
    return  NET_STATE_HALTED; /* FIXME? don't we need to do some cleanup, too to avoid resource leaks? */
  }
  return core->net_state;
}

static int
check_mac_watchdog (mtlk_handle_t core_object, const void *payload, uint32 size)
{
  struct nic *nic = HANDLE_T_PTR(struct nic, core_object);
  mtlk_txmm_msg_t man_msg;
  mtlk_txmm_data_t *man_entry;
  UMI_MAC_WATCHDOG *mac_watchdog;
  int res = MTLK_ERR_OK;
  wave_radio_t *radio = wave_vap_radio_get(nic->vap_handle);

  MTLK_ASSERT(0 == size);
  MTLK_UNREFERENCED_PARAM(payload);
  MTLK_UNREFERENCED_PARAM(size);
  MTLK_ASSERT(FALSE == mtlk_vap_is_slave_ap(nic->vap_handle));

  man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, mtlk_vap_get_txdm(nic->vap_handle), NULL);
  if (!man_entry) {
    res = MTLK_ERR_NO_RESOURCES;
    goto END;
  }

  man_entry->id = UM_DBG_MAC_WATCHDOG_REQ;
  man_entry->payload_size = sizeof(UMI_MAC_WATCHDOG);

  mac_watchdog = (UMI_MAC_WATCHDOG *)man_entry->payload;
  mac_watchdog->u16Timeout =
      HOST_TO_MAC16(WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_MAC_WATCHDOG_TIMER_TIMEOUT_MS));

  res = mtlk_txmm_msg_send_blocked(&man_msg,
          WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_MAC_WATCHDOG_TIMER_TIMEOUT_MS));
  if (res == MTLK_ERR_OK) {
    switch (mac_watchdog->u8Status) {
    case UMI_OK:
      break;
    case UMI_MC_BUSY:
      break;
    case UMI_TIMEOUT:
      res = MTLK_ERR_UMI;
      break;
    default:
      res = MTLK_ERR_UNKNOWN;
      break;
    }
  }
  mtlk_txmm_msg_cleanup(&man_msg);

END:
  if (res != MTLK_ERR_OK) {
    ELOG_DD("CID-%04x: MAC watchdog error %d", mtlk_vap_get_oid(nic->vap_handle), res);
    (void)mtlk_hw_set_prop(mtlk_vap_get_hwapi(nic->vap_handle), MTLK_HW_RESET, NULL, 0);
  } else {
    if (MTLK_ERR_OK !=
        mtlk_osal_timer_set(&nic->slow_ctx->mac_watchdog_timer,
                            WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_MAC_WATCHDOG_TIMER_PERIOD_MS))) {
      ELOG_D("CID-%04x: Cannot schedule MAC watchdog timer", mtlk_vap_get_oid(nic->vap_handle));
      (void)mtlk_hw_set_prop(mtlk_vap_get_hwapi(nic->vap_handle), MTLK_HW_RESET, NULL, 0);
    }
  }

  return MTLK_ERR_OK;
}

static uint32
mac_watchdog_timer_handler (mtlk_osal_timer_t *timer, mtlk_handle_t data)
{
  int err;
  struct nic *nic = (struct nic *)data;

  err = _mtlk_core_execute_async(nic, MTLK_ABILITY_NONE, HANDLE_T(nic), check_mac_watchdog,
                                 NULL, 0, _MTLK_CORE_PRIORITY_MAINTENANCE, NULL);

  if (err != MTLK_ERR_OK) {
    ELOG_DD("CID-%04x: Can't schedule MAC WATCHDOG task (err=%d)", mtlk_vap_get_oid(nic->vap_handle), err);
  }

  return 0;
}

static int __MTLK_IFUNC
send_cca_thresholds(mtlk_handle_t core_object, const void *payload, uint32 size)
{
  struct nic *nic = HANDLE_T_PTR(struct nic, core_object);
  iwpriv_cca_th_t *cca_th_params = (iwpriv_cca_th_t *)payload;
  int res = MTLK_ERR_OK;

  MTLK_ASSERT(sizeof(*cca_th_params) == size);
  MTLK_UNREFERENCED_PARAM(size);
  MTLK_ASSERT(FALSE == mtlk_vap_is_slave_ap(nic->vap_handle));

  res = mtlk_core_cfg_send_cca_threshold_req(nic, cca_th_params);
  if (res != MTLK_ERR_OK)
    ELOG_DD("CID-%04x: Can't send CCA thresholds (err=%d)", mtlk_vap_get_oid(nic->vap_handle), res);

  mtlk_osal_timer_set(&nic->slow_ctx->cca_step_down_timer, nic->slow_ctx->cca_adapt.interval * 1000);
  return MTLK_ERR_OK;
}

static BOOL
_mtlk_core_cca_is_above_configured (mtlk_core_t *core, iwpriv_cca_th_t *cca_th_params)
{
  int i;

  for (i = 0; i < MTLK_CCA_TH_PARAMS_LEN; i++)
    if (core->slow_ctx->cca_adapt.cca_th_params[i] > cca_th_params->values[i]) return TRUE;

  return FALSE;
}

static BOOL
_mtlk_core_cca_is_below_limit (mtlk_core_t *core, int limit)
{
  int i;

  for (i = 0; i < MTLK_CCA_TH_PARAMS_LEN; i++)
    if (core->slow_ctx->cca_adapt.cca_th_params[i] < limit) return TRUE;

  return FALSE;
}

static uint32
cca_step_down_timer_clb_func (mtlk_osal_timer_t *timer, mtlk_handle_t data)
{
  struct nic *nic = (struct nic *)data;
  wave_radio_t *radio = wave_vap_radio_get(nic->vap_handle);
  iwpriv_cca_adapt_t cca_adapt_params;
  mtlk_pdb_size_t    cca_adapt_size = sizeof(cca_adapt_params);
  iwpriv_cca_th_t    cca_th_params;

  if (MTLK_ERR_OK != WAVE_RADIO_PDB_GET_BINARY(radio, PARAM_DB_RADIO_CCA_ADAPT, &cca_adapt_params, &cca_adapt_size)) {
    ELOG_V("cannot read CCA_ADAPT data");
    return 0;
  }

  /* read user config */
  if (MTLK_ERR_OK != mtlk_core_cfg_read_cca_threshold(nic, &cca_th_params)) {
    return MTLK_ERR_UNKNOWN;
  }

  ILOG3_V("CCA adaptive: step down timer triggered");

  if (_mtlk_core_cca_is_above_configured(nic, &cca_th_params)) {
    uint32 interval = nic->slow_ctx->cca_adapt.step_down_coef * cca_adapt_params.step_down_interval;
    int i;

    ILOG2_D("CCA adaptive: step down, next interval %d", interval);
    for (i = 0; i < MTLK_CCA_TH_PARAMS_LEN; i++) {
      nic->slow_ctx->cca_adapt.cca_th_params[i] = MAX(cca_th_params.values[i],
                                                      nic->slow_ctx->cca_adapt.cca_th_params[i] - cca_adapt_params.step_down);
      cca_th_params.values[i] = nic->slow_ctx->cca_adapt.cca_th_params[i];
      ILOG2_DD("CCA adaptive: step down value %d: %d", i, cca_th_params.values[i]);
    }

    nic->slow_ctx->cca_adapt.interval = interval;
    _mtlk_process_hw_task(nic, SERIALIZABLE, send_cca_thresholds, HANDLE_T(nic), &cca_th_params, sizeof(cca_th_params));

  } else nic->slow_ctx->cca_adapt.stepping_down = 0;

  return 0;
}

static void __MTLK_IFUNC
clean_all_sta_on_disconnect_sta_clb (mtlk_handle_t    usr_ctx,
                                     sta_entry *sta)
{
  struct nic      *nic  = HANDLE_T_PTR(struct nic, usr_ctx);
  const IEEE_ADDR *addr = mtlk_sta_get_addr(sta);

  ILOG1_Y("Station %Y disconnected", addr->au8Addr);

  /* Notify Traffic analyzer about STA disconnect */
  mtlk_ta_on_disconnect(mtlk_vap_get_ta(nic->vap_handle), sta);

  if (BR_MODE_WDS == MTLK_CORE_HOT_PATH_PDB_GET_INT(nic, CORE_DB_CORE_BRIDGE_MODE) ||
    mtlk_sta_is_4addr(sta)) {
    /* Don't remove HOST from DB if in WDS mode or 4 address station */
    mtlk_hstdb_stop_all_by_sta(&nic->slow_ctx->hstdb, sta);
  }
  else {
    mtlk_hstdb_remove_all_by_sta(&nic->slow_ctx->hstdb, sta);
  }

  /* Remove all hosts MAC addr from switch MAC table */
  mtlk_hstdb_dcdp_remove_all_by_sta(&nic->slow_ctx->hstdb, sta);

  /* Remove sta MAC addr from DC DP MAC table */
  mtlk_df_user_dcdp_remove_mac_addr(mtlk_vap_get_df(nic->vap_handle), addr->au8Addr);
}

static void
_mtlk_core_clean_vap_tx_on_disconnect(struct nic *nic)
{
  MTLK_UNREFERENCED_PARAM(nic);
}

static void
clean_all_sta_on_disconnect (struct nic *nic)
{
  BOOL wait_all_packets_confirmed;

  wait_all_packets_confirmed = (mtlk_core_get_net_state(nic) != NET_STATE_HALTED);

  mtlk_stadb_disconnect_all(&nic->slow_ctx->stadb,
                            clean_all_sta_on_disconnect_sta_clb,
                            HANDLE_T(nic),
                            wait_all_packets_confirmed);

  _mtlk_core_clean_vap_tx_on_disconnect(nic);
}

static void
cleanup_on_disconnect (struct nic *nic)
{
  if (!mtlk_vap_is_ap(nic->vap_handle)) {
    /* rollback network mode */
    MTLK_CORE_PDB_SET_INT(nic, PARAM_DB_CORE_NET_MODE_CUR, core_cfg_get_network_mode_cfg(nic));
  }

  if (!_mtlk_core_has_connections(nic)) {
    if (mtlk_core_get_net_state(nic) == NET_STATE_DEACTIVATING) {
      MTLK_CORE_PDB_SET_MAC(nic, PARAM_DB_CORE_BSSID, mtlk_osal_eth_zero_addr);
    }
    _mtlk_core_clean_vap_tx_on_disconnect(nic);
  }

#ifdef CPTCFG_IWLWAV_SET_PM_QOS
  if (mtlk_global_stadb_is_empty()) {
    mtlk_mmb_update_cpu_dma_latency(mtlk_vap_get_hw(nic->vap_handle), MTLK_HW_PM_QOS_VALUE_NO_CLIENTS);
  }
#endif
}

#ifdef MTLK_LEGACY_STATISTICS
static int
_mtlk_core_ap_disconnect_sta_blocked(struct nic *nic, const IEEE_ADDR *addr,
                                     uint16 reason)
#else
static int
_mtlk_core_ap_disconnect_sta_blocked(struct nic *nic, const IEEE_ADDR *addr)
#endif
{
  int       res = MTLK_ERR_OK;
  int       net_state;
  mtlk_df_t *df;
  sta_entry *sta = NULL;

  MTLK_ASSERT(NULL != nic);
  MTLK_ASSERT(NULL != addr);

  df = mtlk_vap_get_df(nic->vap_handle);
  MTLK_ASSERT(NULL != df);

  /* Check is STA is a peer AP */
  if (BR_MODE_WDS == MTLK_CORE_HOT_PATH_PDB_GET_INT(nic, CORE_DB_CORE_BRIDGE_MODE)) {
    wds_peer_disconnect(&nic->slow_ctx->wds_mng, addr);
  }

  sta = mtlk_stadb_find_sta(&nic->slow_ctx->stadb, addr->au8Addr);
  if (sta == NULL) {
    ILOG1_DY("CID-%04x: Station %Y not found during disconnecting",
            mtlk_vap_get_oid(nic->vap_handle),
            addr);
    res = MTLK_ERR_UNKNOWN;
    goto finish;
  }

  net_state = mtlk_core_get_net_state(nic);
  if (net_state == NET_STATE_HALTED) {
    /* Do not send anything to halted MAC or if STA hasn't been connected */
    res = MTLK_ERR_UNKNOWN;
    goto sta_decref;
  }

  mtlk_sta_set_packets_filter(sta, MTLK_PCKT_FLTR_DISCARD_ALL);
  res = core_ap_stop_traffic(nic, &sta->info); /* Send Stop Traffic Request to FW */

  if (MTLK_ERR_OK != res)
    goto sta_decref;

  res = core_ap_remove_sta(nic, &sta->info);
  if (MTLK_ERR_OK != res) {
    res = MTLK_ERR_UNKNOWN;
    goto sta_decref;
  }

  if (BR_MODE_WDS == MTLK_CORE_HOT_PATH_PDB_GET_INT(nic, CORE_DB_CORE_BRIDGE_MODE) ||
    mtlk_sta_is_4addr(sta)) {
    /* Don't remove HOST from DB if in WDS mode or 4 address station */
    mtlk_hstdb_stop_all_by_sta(&nic->slow_ctx->hstdb, sta);
  }
  else {
    mtlk_hstdb_remove_all_by_sta(&nic->slow_ctx->hstdb, sta);
  }

  mtlk_stadb_remove_sta(&nic->slow_ctx->stadb, sta);

  /* Remove all hosts MAC addr from switch MAC table */
  mtlk_hstdb_dcdp_remove_all_by_sta(&nic->slow_ctx->hstdb, sta);
  /* Remove sta MAC addr from switch MAC table */
  mtlk_df_user_dcdp_remove_mac_addr(df, addr->au8Addr);

  /* Notify ARP proxy if enabled */
  if (MTLK_CORE_PDB_GET_INT(nic, PARAM_DB_CORE_ARP_PROXY))
  {
    mtlk_parp_notify_disconnect((char*) mtlk_df_get_name(df), (char *) addr->au8Addr);
  }

  /* Notify Traffic analyzer about STA disconnect */
  mtlk_ta_on_disconnect(mtlk_vap_get_ta(nic->vap_handle), sta);

#ifdef MTLK_LEGACY_STATISTICS
  /* Send indication if STA has been disconnected from AP */
  _mtlk_core_on_peer_disconnect(nic, sta, reason);
#endif /* MTLK_LEGACY_STATISTICS */

  cleanup_on_disconnect(nic);
  /* update disconnections statistics */
  nic->pstats.num_disconnects++;

sta_decref:
  mtlk_sta_decref(sta); /* De-reference of find */

finish:
  return res;
}

static int
reset_security_stuff(struct nic *nic)
{
  int res = MTLK_ERR_OK;

  memset(&nic->slow_ctx->keys, 0, sizeof(nic->slow_ctx->keys));
  memset(&nic->slow_ctx->group_rsc, 0, sizeof(nic->slow_ctx->group_rsc));
  nic->slow_ctx->default_key    = 0;
  nic->slow_ctx->default_mgmt_key    = 0;
  nic->slow_ctx->wep_enabled    = FALSE;
  nic->slow_ctx->rsn_enabled    = FALSE;
  nic->slow_ctx->group_cipher   = IW_ENCODE_ALG_NONE;

  /* 802.11W */
  memset(&nic->slow_ctx->igtk_key, 0, sizeof(nic->slow_ctx->igtk_key));
  nic->slow_ctx->igtk_cipher    = IW_ENCODE_ALG_NONE;
  nic->slow_ctx->igtk_key_len   = 0;

  return res;
}

BOOL
can_disconnect_now(struct nic *nic, BOOL is_scan_fast_rcvry)
{
  return mtlk_vap_is_slave_ap(nic->vap_handle) || !mtlk_core_scan_is_running(nic) || is_scan_fast_rcvry;
}

/* This interface can be used if we need to disconnect while in
 * atomic context (for example, when disconnecting from a timer).
 * Disconnect process requires blocking function calls, so we
 * have to schedule a work.
 */

struct mtlk_core_disconnect_sta_data
{
  IEEE_ADDR          addr;
  uint16             reason;
  uint16             reason_code;
  int               *res;
  mtlk_osal_event_t *done_evt;
};

#ifdef MTLK_LEGACY_STATISTICS
/***************************************************************************
 * Disconnecting routines for STA
 ***************************************************************************/
/* This interface can be used if we need to disconnect while in
 * atomic context (for example, when disconnecting from a timer).
 * Disconnect process requires blocking function calls, so we
 * have to schedule a work.
 */
static void
_mtlk_core_schedule_disconnect_me (struct nic *nic, uint16 reason, uint16 reason_code)
{
  int err;
  struct mtlk_core_disconnect_sta_data data;

  MTLK_ASSERT(nic != NULL);

  memset(&data, 0, sizeof(data));
  data.reason   = reason;
  data.reason_code = reason_code;

  err = _mtlk_core_execute_async(nic, MTLK_ABILITY_NONE, HANDLE_T(nic), handleDisconnectMe, &data, sizeof(data),
                                 _MTLK_CORE_PRIORITY_NETWORK, NULL);
  if (err != MTLK_ERR_OK) {
    ELOG_DD("CID-%04x: Can't schedule DISCONNECT (err=%d)", mtlk_vap_get_oid(nic->vap_handle), err);
  }
}

static int __MTLK_IFUNC
_mtlk_core_disconnect_sta(mtlk_core_t *nic, uint16 reason, uint16 reason_code)
{
  uint32 net_state;

  net_state = mtlk_core_get_net_state(nic);
  if ((net_state != NET_STATE_CONNECTED) &&
      (net_state != NET_STATE_HALTED)) { /* allow disconnect for clean up */
    ILOG0_DS("CID-%04x: disconnect in invalid state %s", mtlk_vap_get_oid(nic->vap_handle),
          mtlk_net_state_to_string(net_state));
    return MTLK_ERR_NOT_READY;
  }

  if (!can_disconnect_now(nic, FALSE)) {
    _mtlk_core_schedule_disconnect_me(nic, reason, reason_code);
    return MTLK_ERR_OK;
  }

  reset_security_stuff(nic);

  return MTLK_ERR_OK;
}

static int
_mtlk_core_hanle_disconnect_sta_req (mtlk_handle_t hcore, const void* data, uint32 data_size)
{
  mtlk_core_t *nic = HANDLE_T_PTR(mtlk_core_t, hcore);
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;
  mtlk_core_ui_mlme_cfg_t *mlme_cfg;
  uint32 size;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  mlme_cfg = mtlk_clpb_enum_get_next(clpb, &size);
  if ( (NULL == mlme_cfg) || (sizeof(*mlme_cfg) != size) ) {
    ELOG_D("CID-%04x: Failed to get MLME configuration parameters from CLPB", mtlk_vap_get_oid(nic->vap_handle));
    return MTLK_ERR_UNKNOWN;
  }

  return _mtlk_core_disconnect_sta(nic, FM_STATUSCODE_USER_REQUEST, mlme_cfg->reason_code);
}

static int
handleDisconnectMe(mtlk_handle_t core_object, const void *payload, uint32 size)
{
  struct nic *nic = HANDLE_T_PTR(struct nic, core_object);
  struct mtlk_core_disconnect_sta_data *data =
    (struct mtlk_core_disconnect_sta_data *)payload;

  MTLK_ASSERT(sizeof(uint16) == size);

  ILOG2_V("Handling disconnect request");
  _mtlk_core_disconnect_sta(nic, data->reason, data->reason_code);

  return MTLK_ERR_OK;
}
#endif /* MTLK_LEGACY_STATISTICS */

/* kernel doesn't have structure of the BOOTP/DHCP header
 * so here it is defined according to rfc2131
 */

struct dhcphdr {
  u8      op;
#define BOOTREQUEST   1
#define BOOTREPLY     2
  u8      htype;
  u8      hlen;
  u8      hops;
  u32     xid;
  u16     secs;
  u16     flags;
#define BOOTP_BRD_FLAG 0x8000
  u32     ciaddr;
  u32     yiaddr;
  u32     siaddr;
  u32     giaddr;
  u8      chaddr[16];
  u8      sname[64];
  u8      file[128];
  u32     magic; /* NB: actually magic is a part of options */
  u8      options[0];
} __attribute__((aligned(1), packed));

typedef struct _whole_dhcp_packet_t
{
  struct ethhdr  eth_hdr;
  struct iphdr   ip_hdr;
  struct udphdr  udp_hdr;
  struct dhcphdr dhcp_hdr;
} __attribute__((aligned(1), packed)) whole_dhcp_packet_t;

#define UDP_PORT_DHCP_SERVER (67)
#define UDP_PORT_DHCP_CLIENT (68)

static BOOL
_mtlk_core_convert_dhcp_bcast_to_ucast(mtlk_nbuf_t *nbuf)
{
  BOOL res = FALSE;
  whole_dhcp_packet_t *wdp = (whole_dhcp_packet_t*) nbuf->data;

  if (nbuf->len >= sizeof(*wdp) &&
      wdp->eth_hdr.h_proto == __constant_htons(ETH_P_IP) &&
      wdp->ip_hdr.protocol == IPPROTO_UDP &&
      (wdp->udp_hdr.source == __constant_htons(UDP_PORT_DHCP_SERVER) || wdp->udp_hdr.source == __constant_htons(UDP_PORT_DHCP_CLIENT)) &&
      (wdp->udp_hdr.dest == __constant_htons(UDP_PORT_DHCP_SERVER) || wdp->udp_hdr.dest == __constant_htons(UDP_PORT_DHCP_CLIENT)))
  {
    mtlk_osal_copy_eth_addresses(wdp->eth_hdr.h_dest, wdp->dhcp_hdr.chaddr);
    res = TRUE;
  }

  return res;
}

#define IP_ALEN 4
#define ARP_REQ_SEND_RATE_MS  300 /* 1 ARP request per 300ms */


mtlk_nbuf_t* subst_ip_to_arp_probe ( mtlk_nbuf_t            **nbuf_in_out,
                                     mtlk_osal_timestamp_t *last_arp_req)
{

  struct ethhdr *ether_header_in, *ether_header_arp;
  struct iphdr  *ip_header_in;
  struct arphdr *arp_hdr;
  uint8         *arp_data;
  mtlk_nbuf_t   *nbuf_arp, *nbuf_in;
  uint32         arp_pck_size;
  mtlk_osal_timestamp_diff_t diff;

  /* Limit ARP probe sending rate */
  diff = mtlk_osal_timestamp_diff(mtlk_osal_timestamp(), *last_arp_req);
  if (mtlk_osal_timestamp_to_ms(diff) < ARP_REQ_SEND_RATE_MS){
    goto subst_ip_to_arp_probe_err;
  }

  /* IP packet sanity check */
  nbuf_in = *nbuf_in_out;
  ether_header_in = (struct ethhdr *)nbuf_in->data;
  if( ntohs(ether_header_in->h_proto) != ETH_P_IP){
    goto subst_ip_to_arp_probe_err;
  }

  ip_header_in = (struct iphdr *)(nbuf_in->data + sizeof(struct ethhdr));
  if (!ip_header_in->saddr || !ip_header_in->daddr){
    goto subst_ip_to_arp_probe_err;
  }

  /* Allocate and Compose ARP probe packet */
  arp_pck_size = sizeof(struct ethhdr) + sizeof(struct arphdr) + (ETH_ALEN + IP_ALEN)*2;
  nbuf_arp = mtlk_df_nbuf_alloc(arp_pck_size);

  if (!nbuf_arp){
    WLOG_V("Unable to allocate buffer for ARP probe");
    goto subst_ip_to_arp_probe_err;
  }

  /* Init nbuf fields */
  nbuf_arp->dev = nbuf_in->dev;
  nbuf_arp->len = arp_pck_size;
  mtlk_df_nbuf_set_priority(nbuf_arp, MTLK_WMM_ACI_DEFAULT_CLASS);

  /* Fill up ETH header */
  ether_header_arp = (struct ethhdr *)nbuf_arp->data;
  mtlk_osal_copy_eth_addresses(ether_header_arp->h_source, ether_header_in->h_source);
  memset(ether_header_arp->h_dest, 0xFF, ETH_ALEN);
  ether_header_arp->h_proto = htons(ETH_P_ARP);

  /* Fill up ARP header */
  arp_hdr = (struct arphdr *)(ether_header_arp + 1);
  arp_hdr->ar_hrd = htons(ARPHRD_ETHER);
  arp_hdr->ar_pro = htons(ETH_P_IP);
  arp_hdr->ar_hln = ETH_ALEN;
  arp_hdr->ar_pln = IP_ALEN;
  arp_hdr->ar_op  = ARPOP_REQUEST;

  /* Fill up ARP body */
  arp_data = (uint8 *)(arp_hdr + 1);
  mtlk_osal_copy_eth_addresses(arp_data + 0,                             ether_header_in->h_source); /* Sender MAC - from IN pck */
  mtlk_osal_zero_ip4_address  (arp_data + ETH_ALEN                       );                          /* Sender IP  - all 0 */
  mtlk_osal_copy_eth_addresses(arp_data + ETH_ALEN + IP_ALEN,            ether_header_in->h_dest);   /* Target MAC - from IN pck */
  mtlk_osal_copy_ip4_addresses(arp_data + ETH_ALEN + IP_ALEN + ETH_ALEN, &ip_header_in->daddr);      /* Target IP  - from IN pck */

  /* Release original nbuf */
  mtlk_df_nbuf_free(nbuf_in);

  *nbuf_in_out = nbuf_arp;
  *last_arp_req = mtlk_osal_timestamp();

  ILOG2_Y("Sending ARP probe to %Y", &ether_header_in->h_dest);

  return nbuf_arp;

subst_ip_to_arp_probe_err:

  return NULL;
}


/*****************************************************************************
**
** NAME         mtlk_core_handle_tx_data
**
** PARAMETERS   vap_handle           Handle of VAP object
**              nbuf                 Skbuff to transmit
**
** DESCRIPTION  Entry point for TX buffers
**
******************************************************************************/
void __MTLK_IFUNC
mtlk_core_handle_tx_data (mtlk_vap_handle_t vap_handle, mtlk_nbuf_t *nbuf)
{
  mtlk_core_t   *nic = mtlk_vap_get_core(vap_handle);
  struct ethhdr *ether_header = (struct ethhdr *)nbuf->data;
  bridge_mode_t  bridge_mode;
  sta_entry     *sta = NULL; /* destination STA in AP mode or destination BSS in STA mode */
  uint32         nbuf_flags = 0;
  sta_db        *stadb;

  MTLK_ASSERT(NULL != nic);
  stadb = &nic->slow_ctx->stadb;

  CPU_STAT_BEGIN_TRACK(CPU_STAT_ID_TX_OS);

  /* Transmit only if connected to someone */
  if (__UNLIKELY(!_mtlk_core_has_connections(nic))) {
    mtlk_core_inc_cnt(nic, MTLK_CORE_CNT_TX_PACKETS_DISCARDED_NO_PEERS);
    goto tx_error;
  }

#if defined(CPTCFG_IWLWAV_PER_PACKET_STATS)
  {
    /* get private fields */
    mtlk_nbuf_priv_t *nbuf_priv = mtlk_nbuf_priv(nbuf);
    mtlk_nbuf_priv_stats_set(nbuf_priv, MTLK_NBUF_STATS_TS_SQ_IN, mtlk_hw_get_timestamp(vap_handle));
  }
#endif

  ILOG4_Y("802.3 tx DA: %Y", ether_header->h_dest);
  ILOG4_Y("802.3 tx SA: %Y", ether_header->h_source);
  mtlk_eth_parser(nbuf->data, nbuf->len, mtlk_df_get_name(mtlk_vap_get_df(vap_handle)), "TX");

  bridge_mode = MTLK_CORE_HOT_PATH_PDB_GET_INT(nic, CORE_DB_CORE_BRIDGE_MODE);

  if (mtlk_vap_is_ap(vap_handle)) {
     /* AP mode interface */
    if (MESH_MODE_BACKHAUL_AP == MTLK_CORE_HOT_PATH_PDB_GET_INT(nic, CORE_DB_CORE_MESH_MODE)) {
      /* MultiAP: backhaul AP always has ONLY one station and always transmits frames
       * to the connected STA as is, w/o additional processing. */
      nbuf_flags = MTLK_NBUFF_UNICAST;
      sta = mtlk_stadb_get_first_sta(stadb);
      if (!sta) {
        ILOG3_D("CID-%04x: Failed to get destination STA", mtlk_vap_get_oid(vap_handle));
        mtlk_core_inc_cnt(nic, MTLK_CORE_CNT_TX_PACKETS_DISCARDED_DRV_UNKNOWN_DESTINATION_DIRECTED);
        goto tx_error;
      }
      goto send_tx_data;
    }

    /* check frame destination */
    if (mtlk_osal_eth_is_multicast(ether_header->h_dest)) {
      if (mtlk_osal_eth_is_broadcast(ether_header->h_dest)) {
        if (nic->dgaf_disabled) {
          if (_mtlk_core_convert_dhcp_bcast_to_ucast(nbuf)) {
            ILOG3_V("DGAF disabled: Convert DHCP to Unicast");
            mtlk_core_inc_cnt(nic, MTLK_CORE_CNT_TX_PACKETS_TO_UNICAST_DGAF_DISABLED);
            goto unicast;
          }
          else {
            ILOG3_V("DGAF disabled: Skip Broadcast packet");
            mtlk_core_inc_cnt(nic, MTLK_CORE_CNT_TX_PACKETS_SKIPPED_DGAF_DISABLED);
            goto tx_error;
          }
        }
        nbuf_flags |= MTLK_NBUFF_BROADCAST;
      }
      else
        nbuf_flags |= MTLK_NBUFF_MULTICAST;

      goto send_tx_data;
    }

unicast:
    /* Process unicast packet */
    sta = mtlk_stadb_find_sta(stadb, ether_header->h_dest);
    if (!sta) { /* search even if not WDS - to support STA bridge mode */
      sta = mtlk_hstdb_find_sta(&nic->slow_ctx->hstdb, ether_header->h_dest);
    }

    if (sta) {
      nbuf_flags |= MTLK_NBUFF_UNICAST;
    } else {
      ILOG3_Y("Unknown destination (%Y)", ether_header->h_dest);
      mtlk_core_inc_cnt(nic, MTLK_CORE_CNT_TX_PACKETS_DISCARDED_DRV_UNKNOWN_DESTINATION_DIRECTED);
      if (bridge_mode == BR_MODE_WDS || mtlk_stadb_get_four_addr_sta_cnt(stadb)) {
        /* In bridge mode or if there are four address stations connected,
           in case of IP UNICAST packet to unknown destination,
           substitute original packet with ARP probe */
        if (NULL == subst_ip_to_arp_probe(&nbuf, &nic->slow_ctx->last_arp_probe)){
          goto tx_error; /* Fail to generate ARP packet */
        }
        nbuf_flags |= MTLK_NBUFF_BROADCAST;
      } else {
        goto tx_error;
      }
    }
  }
  else {
    /* Station mode interface */
    struct ieee80211_vif *vif = net_device_to_ieee80211_vif(nbuf->dev);
    u8 *bssid = wv_ieee80211_peer_ap_address(vif);
    if (!bssid) {
      ELOG_V("wv_ieee80211_peer_ap_address failed");
      goto tx_error;
    }
    sta = mtlk_stadb_find_sta(stadb, bssid);
    if (!sta) {
      ELOG_Y("MAC address %Y not found in sta mode", bssid);
      goto tx_error;
    }
    if (!mtlk_sta_is_4addr(sta) && MTLK_CORE_HOT_PATH_PDB_CMP_MAC(nic, CORE_DB_CORE_MAC_ADDR, ether_header->h_source)) {
      mtlk_dump(0, nbuf->data, MIN(nbuf->len, 64), "ERROR, not our source MAC address in sta mode");
      goto tx_error;
    }
    nbuf_flags |= MTLK_NBUFF_UNICAST;
  }

send_tx_data:
  {
    mtlk_core_handle_tx_data_t tx_data;
    /* initialize tx_data */
    memset(&tx_data, 0, sizeof(tx_data));
    tx_data.nbuf = nbuf;
    tx_data.dst_sta = sta;
    mtlk_mc_transmit(nic, &tx_data, nbuf_flags, bridge_mode, NULL);
  }
  CPU_STAT_END_TRACK(CPU_STAT_ID_TX_OS);
  return;

tx_error:
  mtlk_df_nbuf_free(nbuf);
  if (sta) mtlk_sta_decref(sta); /* De-reference of find or get_ap */
  CPU_STAT_END_TRACK(CPU_STAT_ID_TX_OS);
}


#define SEQUENCE_NUMBER_LIMIT                    (0x1000)
#define SEQ_DISTANCE(seq1, seq2) (((seq2) - (seq1) + SEQUENCE_NUMBER_LIMIT) \
                                    % SEQUENCE_NUMBER_LIMIT)


static __INLINE BOOL
__mtlk_core_is_looped_packet (mtlk_core_t* nic, mtlk_nbuf_t *nbuf)
{
  struct ethhdr *ether_header = (struct ethhdr *)nbuf->data;
  return (0 == MTLK_CORE_HOT_PATH_PDB_CMP_MAC(nic, CORE_DB_CORE_MAC_ADDR, ether_header->h_source));
}

BOOL __MTLK_IFUNC
mtlk_core_is_looped_packet (mtlk_core_t* nic, mtlk_nbuf_t *nbuf)
{
  return __mtlk_core_is_looped_packet(nic, nbuf);
}

/* Send Packet to the OS's protocol stack or forward */
void __MTLK_IFUNC
mtlk_core_analyze_and_send_up (mtlk_core_t* nic, mtlk_core_handle_tx_data_t *tx_data, sta_entry *src_sta)
{
  uint32 nbuf_flags = 0;
  mtlk_nbuf_t *nbuf = tx_data->nbuf;
  struct ethhdr *ether_header = (struct ethhdr *)nbuf->data;
  uint32 nbuf_len = mtlk_df_nbuf_get_data_length(nbuf);

  if (__UNLIKELY(__mtlk_core_is_looped_packet(nic, nbuf))) {
    ILOG3_Y("drop rx packet - the source address is the same as own address: %Y", ether_header->h_source);
    mtlk_df_nbuf_free(nbuf);
    return;
  }

  if (!mtlk_vap_is_ap(nic->vap_handle))
  {
    ILOG3_V("Client mode");
    nbuf_flags |= MTLK_NBUFF_UNICAST | MTLK_NBUFF_CONSUME;
  }
  else if (MESH_MODE_BACKHAUL_AP == MTLK_CORE_HOT_PATH_PDB_GET_INT(nic, CORE_DB_CORE_MESH_MODE)) {
    /* MultiAP: backhaul AP always has ONLY one connected station and always forwards frames
     * from the connected STA to Linux as is, without additional processing. */
    ILOG3_V("AP in Backhaul mode - short path");
    nbuf_flags |= MTLK_NBUFF_UNICAST | MTLK_NBUFF_CONSUME;
  }
  else {
    /* AP mode */
    mtlk_nbuf_t *nbuf_orig = nbuf;
    sta_entry *dst_sta = NULL;
    sta_db *stadb = &nic->slow_ctx->stadb;
    bridge_mode_t bridge_mode = MTLK_CORE_HOT_PATH_PDB_GET_INT(nic, CORE_DB_CORE_BRIDGE_MODE);

    if (mtlk_osal_eth_is_broadcast(ether_header->h_dest)) {
      /* Broadcast packet */
      nbuf_flags |= MTLK_NBUFF_BROADCAST | MTLK_NBUFF_CONSUME;
      if (MTLK_CORE_HOT_PATH_PDB_GET_INT(nic, CORE_DB_CORE_AP_FORWARDING))
        nbuf_flags |= MTLK_NBUFF_FORWARD;
    } else if (mtlk_osal_eth_is_multicast(ether_header->h_dest)) {
      /* Multicast packet */
      nbuf_flags |= MTLK_NBUFF_MULTICAST | MTLK_NBUFF_CONSUME;
      if (MTLK_CORE_HOT_PATH_PDB_GET_INT(nic, CORE_DB_CORE_AP_FORWARDING))
        nbuf_flags |= MTLK_NBUFF_FORWARD;
    } else {
      /* Unicast packet */
      nbuf_flags |= MTLK_NBUFF_UNICAST;
      if (MTLK_CORE_HOT_PATH_PDB_GET_INT(nic, CORE_DB_CORE_AP_FORWARDING)) {
        /* Search of DESTINATION MAC ADDRESS of RECEIVED packet */
        dst_sta = mtlk_stadb_find_sta(stadb, ether_header->h_dest);
        if ((dst_sta == NULL) && ((BR_MODE_WDS == bridge_mode) || mtlk_stadb_get_four_addr_sta_cnt(stadb))) {
          dst_sta = mtlk_hstdb_find_sta(&nic->slow_ctx->hstdb, ether_header->h_dest);
        }

        if (dst_sta != NULL) {
          if (dst_sta != src_sta) {
            nbuf_flags |= MTLK_NBUFF_FORWARD;
    #if MTLK_USE_DIRECTCONNECT_DP_API
            if (src_sta->vap_handle == dst_sta->vap_handle) {
              /* Allow Acceleration subsystem to learn about session when driver shortcuts
                 forwarding without going to stack. The flag can be set safely even if acceleration isn't available */
              nbuf_flags |= MTLK_NBUFF_SHORTCUT;
              #if (CPTCFG_IWLWAV_MAX_DLEVEL >= 2)
              if (mtlk_mmb_fastpath_available(mtlk_vap_get_hw(nic->vap_handle))) {
                /* Output log message only if acceleratoin is available */
                ILOG2_DYDY("Learn shortcut from STA_ID %d (MAC:%Y) to STA_ID %d (MAC %Y)",
                      mtlk_sta_get_sid(src_sta), ether_header->h_source, mtlk_sta_get_sid(dst_sta), ether_header->h_dest);
              }
              #endif
            }
    #endif
          } else {
            ILOG3_V("Loop detected ! Don't forward packet !");
          }
        } else nbuf_flags |= MTLK_NBUFF_CONSUME; /* let the system forward - dst STA not found */
      } else nbuf_flags |= MTLK_NBUFF_CONSUME; /* let the system forward - FWD disabled */
    }

    if (nbuf_flags & (MTLK_NBUFF_BROADCAST | MTLK_NBUFF_MULTICAST)) {
      mtlk_mc_parse(nic, nbuf, src_sta);
    }

    switch (nbuf_flags & (MTLK_NBUFF_FORWARD | MTLK_NBUFF_CONSUME))
    {
      case (MTLK_NBUFF_FORWARD | MTLK_NBUFF_CONSUME):
        nbuf = mtlk_df_nbuf_clone_no_priv(nbuf);
        break;

      case 0:
        mtlk_df_nbuf_free(nbuf);
        ILOG3_P("nbuf %p dropped - consumption is disabled", nbuf);
        return;
    }

    if (nbuf_flags & MTLK_NBUFF_FORWARD) {
      CPU_STAT_BEGIN_TRACK(CPU_STAT_ID_TX_FWD);
      if (__LIKELY(nbuf)) {
        /* update tx_data before transmitting */
        tx_data->nbuf = nbuf;
        tx_data->dst_sta = dst_sta;
        mtlk_mc_transmit(nic, tx_data, nbuf_flags, bridge_mode, src_sta);
      } else {
        ELOG_D("CID-%04x: Can't clone the packet for forwarding", mtlk_vap_get_oid(nic->vap_handle));
        nic->pstats.fwd_cannot_clone++;
        nic->pstats.fwd_dropped++;
      }

      nbuf = nbuf_orig;
      /* Count rxed data to be forwarded */
      mtlk_sta_on_rx_packet_forwarded(src_sta, nbuf);
      CPU_STAT_END_TRACK(CPU_STAT_ID_TX_FWD);
    }
  } /* AP mode */

  if (nbuf_flags & MTLK_NBUFF_CONSUME)
  {
    int net_state;
    ether_header = (struct ethhdr *)nbuf->data;

#if defined MTLK_DEBUG_IPERF_PAYLOAD_RX
    //check if it is an iperf's packet we use to debug
    mtlk_iperf_payload_t *iperf = debug_ooo_is_iperf_pkt((uint8*) ether_header);
    if (iperf != NULL) {
      iperf->ts.tag_tx_to_os = htonl(debug_iperf_priv.tag_tx_to_os);
      debug_iperf_priv.tag_tx_to_os++;
    }
#endif

    ILOG3_Y("802.3 rx DA: %Y", nbuf->data);
    ILOG3_Y("802.3 rx SA: %Y", nbuf->data+ETH_ALEN);
    ILOG3_D("packet protocol %04x", ntohs(ether_header->h_proto));

    net_state = mtlk_core_get_net_state(nic);
    if (net_state != NET_STATE_CONNECTED) {
      mtlk_df_nbuf_free(nbuf);
      if (net_state != NET_STATE_DEACTIVATING) {
        WLOG_DD("CID-%04x: Data rx in not connected state (current state is %d), dropped", mtlk_vap_get_oid(nic->vap_handle), net_state);
      }
      return;
    }


    if (ntohs(ether_header->h_proto) == ETH_P_IP) {
      struct iphdr *iph;
      uint16 ip_len;

      if (nbuf_len < (sizeof(struct ethhdr) + sizeof(struct iphdr))) {
        ILOG2_V("malformed IP packet due to uncomplete hdr, dropped\n");
        mtlk_df_nbuf_free(nbuf);
        return;
      }

      iph = (struct iphdr *)((mtlk_handle_t)ether_header + sizeof(struct ethhdr));
      ip_len = ntohs(iph->tot_len);

      if((iph->ihl < 5) || (ip_len < (iph->ihl * sizeof(uint32))) || (ip_len > (nbuf_len - sizeof(struct ethhdr)))) {
        ILOG2_V("malformed IP packet due to payload invalidity, dropped\n");
        mtlk_df_nbuf_free(nbuf);
        return;
      }
    }

#ifdef MTLK_DEBUG_CHARIOT_OOO
    /* check out-of-order */
    {
      int diff, seq_prev;

      seq_prev = nic->seq_prev_sent[nbuf_priv->seq_qos];
      diff = SEQ_DISTANCE(seq_prev, nbuf_priv->seq_num);
      if (diff > SEQUENCE_NUMBER_LIMIT / 2)
        ILOG2_DDD("ooo: qos %u prev = %u, cur %u\n", nbuf_priv->seq_qos, seq_prev, nbuf_priv->seq_num);
      nic->seq_prev_sent[nbuf_priv->seq_qos] = nbuf_priv->seq_num;
    }
#endif

    /* Count only packets sent to OS */
    mtlk_sta_on_packet_indicated(src_sta, nbuf, nbuf_flags);

    CPU_STAT_END_TRACK(CPU_STAT_ID_RX_HW);
    mtlk_df_ui_indicate_rx_data(mtlk_vap_get_df(nic->vap_handle), nbuf);
  }
}

void
mtlk_core_record_xmit_err(struct nic *nic, mtlk_nbuf_t *nbuf, uint32 nbuf_flags)
{
  if (nbuf_flags & MTLK_NBUFF_FORWARD) {
    nic->pstats.fwd_dropped++;
  }

  if (++nic->pstats.tx_cons_drop_cnt > nic->pstats.tx_max_cons_drop)
    nic->pstats.tx_max_cons_drop = nic->pstats.tx_cons_drop_cnt;
}



/*****************************************************************************
 * Helper functions for _mtlk_core_handle_tx_data():
 ******************************************************************************/
static __INLINE int
__mtlk_core_prepare_and_xmit (mtlk_core_t* nic, mtlk_core_handle_tx_data_t *tx_data, BOOL wds, uint16 sta_sid, int mc_index, uint32 nbuf_flags)
{
    int res = MTLK_ERR_PKT_DROPPED;
    mtlk_nbuf_t       *nbuf         = tx_data->nbuf;
    struct ethhdr     *ether_header = (struct ethhdr *)nbuf->data;
    mtlk_hw_api_t     *hw_api       = mtlk_vap_get_hwapi(nic->vap_handle);
    uint32             tid          = mtlk_df_nbuf_get_priority(nbuf);
    uint32             ntx_free     = 0;
    mtlk_hw_data_req_mirror_t *msg  = NULL;

    /* Sanity check */
    if (__UNLIKELY(tid >= NTS_TIDS_GEN6)) {
      ELOG_D("Invalid priority: %u", tid);
      goto tx_skip;
    }

    /* prepare hw message */
    msg = DATA_REQ_MIRROR_PTR(mtlk_hw_get_msg_to_send(hw_api, nic->vap_handle, &ntx_free));
    if (__UNLIKELY(!msg)) {
      ++nic->pstats.ac_dropped_counter[tid];
      nic->pstats.tx_overruns++;
      nic->pstats.txerr_swpath_overruns++;
      goto tx_skip;
    }

    ++nic->pstats.ac_used_counter[tid];
    mtlk_osal_atomic_inc(&nic->unconfirmed_data);
    ILOG4_P("got from hw_msg %p", msg);

    /* fill fields */
    msg->nbuf       = nbuf;
    msg->size       = mtlk_df_nbuf_get_data_length(nbuf);
    msg->sid        = sta_sid;
    msg->tid        = tid;
    msg->wds        = (uint8)wds;
    msg->mcf        = wds ? 0 : nbuf_flags & (MTLK_NBUFF_MULTICAST | MTLK_NBUFF_BROADCAST);
    msg->frame_type = (ntohs(ether_header->h_proto) >= ETH_P_802_3_MIN) ? FRAME_TYPE_ETHERNET : FRAME_TYPE_IPX_LLC_SNAP;
    msg->mc_index   = mc_index;
  #ifdef CPTCFG_IWLWAV_PER_PACKET_STATS
    {
        mtlk_nbuf_priv_t *nbuf_priv = mtlk_nbuf_priv(nbuf);
        mtlk_nbuf_priv_stats_set(nbuf_priv, MTLK_NBUF_STATS_DATA_SIZE, msg->size);

        #if defined(CPTCFG_IWLWAV_TSF_TIMER_ACCESS_ENABLED)
        mtlk_nbuf_priv_stats_set(nbuf_priv, MTLK_NBUF_STATS_TS_FW_IN, mtlk_hw_get_timestamp(nic->vap_handle));
	#endif
    }
  #endif

    /* send hw message */
    res = mtlk_hw_send_data(hw_api, nic->vap_handle, msg);
    if (__LIKELY(res == MTLK_ERR_OK))
        return res;

    ELOG_D("mtlk_hw_send_data ret (%d)", res);
    nic->pstats.txerr_swpath_hwsend++;

tx_skip:
    /* release hw message and record error's statistic */
    if (msg) {
      mtlk_hw_release_msg_to_send(hw_api, HW_MSG_PTR(msg));
    }

    return res;
}

#if MTLK_USE_DIRECTCONNECT_DP_API
static __INLINE int
__mtlk_core_dcdp_prepare_and_xmit (mtlk_core_t *nic, mtlk_core_handle_tx_data_t *tx_data, BOOL wds, uint16 sta_sid, int mc_index, uint32 nbuf_flags)
{
  int res = mtlk_df_ui_dp_prepare_and_xmit(nic->vap_handle, tx_data, wds, sta_sid, mc_index, nbuf_flags);
  if (__UNLIKELY(MTLK_ERR_OK != res)) {
    nic->pstats.txerr_dc_xmit++;
  }
  return res;
}
#endif

/*****************************************************************************
**
** NAME         _mtlk_core_handle_tx_data
**
** PARAMETERS   nbuf                Skbuff to transmit
**              dev                 Device context
**
** RETURNS      Skbuff transmission status
**
** DESCRIPTION  This function called to perform packet transmission.
**
******************************************************************************/
static int
_mtlk_core_handle_tx_data (mtlk_core_t *nic, mtlk_core_handle_tx_data_t *data, uint32 nbuf_flags)
{
  int res = MTLK_ERR_PKT_DROPPED;
  uint16 sta_sid = TMP_BCAST_DEST_ADDR;
  sta_entry *sta = data->dst_sta;
  struct ethhdr *ether_header = (struct ethhdr *)data->nbuf->data;
  int mc_index = MCAST_GROUP_BROADCAST;
  BOOL wds = 0;

  MTLK_UNUSED_VAR(ether_header);
  mtlk_dump(5, data->nbuf->data, data->nbuf->len, "nbuf->data received from OS");

  if (__LIKELY(sta)) {
    if (MTLK_PCKT_FLTR_DISCARD_ALL == mtlk_sta_get_packets_filter(sta)) {
      mtlk_sta_on_packet_dropped(sta, MTLK_TX_DISCARDED_DROP_ALL_FILTER);
      nic->pstats.txerr_drop_all_filter++;
      goto tx_skip;
    }
  }

  if (MESH_MODE_BACKHAUL_AP == MTLK_CORE_HOT_PATH_PDB_GET_INT(nic, CORE_DB_CORE_MESH_MODE)) {
    /* Multi-AP: AP works as backhaul AP. All frames should be sent as-is to the single 4-address STA*/
    MTLK_ASSERT(!sta);
    if (!sta) {
      /* In fact this code should not be executed, but added there for the safety */
      mtlk_core_inc_cnt(nic, MTLK_CORE_CNT_TX_PACKETS_DISCARDED_NO_PEERS);
      goto tx_skip;
    }
    sta_sid = mtlk_sta_get_sid(sta);
  }
  else
  {
    /* Determine multicast group index */
    if (nbuf_flags & MTLK_NBUFF_MULTICAST) {
      mc_index = mtlk_mc_get_grid(nic, data->nbuf);
      if (MCAST_GROUP_UNDEF == mc_index) {
        ILOG2_V("Drop due to undefined MC index.");
        nic->pstats.txerr_undef_mc_index++;
        goto tx_skip;
      }
    }
    ILOG3_DD("CID-%04x: multicast group: %d", mtlk_vap_get_oid(nic->vap_handle), mc_index);

    if (sta) {
      /* Use 4 address for peer APs and 4 address stations */
      if (sta->peer_ap || mtlk_sta_is_4addr(sta)) {
        wds = 1;
        ILOG4_DYY("WDS/four address STA packet: Peer %d, STA %Y, ETH Dest %Y",
          sta->peer_ap, mtlk_sta_get_addr(sta), ether_header->h_dest);
      }
      sta_sid = mtlk_sta_get_sid(sta);
    }
  }
  ILOG3_D("SID: %d", sta_sid);

#if MTLK_USE_DIRECTCONNECT_DP_API
  if (mtlk_mmb_dcdp_path_available(mtlk_vap_get_hw(nic->vap_handle)))
    res = __mtlk_core_dcdp_prepare_and_xmit(nic, data, wds, sta_sid, mc_index, nbuf_flags);
  else
#endif
    res = __mtlk_core_prepare_and_xmit(nic, data, wds, sta_sid, mc_index, nbuf_flags);

  if (__LIKELY(res == MTLK_ERR_OK)) {
    mtlk_df_ui_notify_tx_start(mtlk_vap_get_df(nic->vap_handle));
  }
  else {
    if(sta) {
      mtlk_sta_on_packet_dropped(sta, MTLK_TX_DISCARDED_TX_QUEUE_OVERFLOW);
    } else {
      mtlk_core_inc_cnt(nic, MTLK_CORE_CNT_TX_PACKETS_DISCARDED_TX_QUEUE_OVERFLOW);
    }
  }

tx_skip:
  return res;
}

#if MTLK_USE_DIRECTCONNECT_DP_API
int __MTLK_IFUNC
mtlk_core_get_dev_spec_info (mtlk_nbuf_t *nbuf, mtlk_core_dev_spec_info_t *dev_spec_info)
{
  struct ethhdr *ether_header = (struct ethhdr *)nbuf->data;
  sta_entry *dst_sta = NULL;
  mtlk_df_user_t *df_user;
  mtlk_vap_handle_t vap_handle;
  mtlk_core_t *core;

  df_user = mtlk_df_user_from_ndev(nbuf->dev);
  if (!df_user) {
    return MTLK_ERR_UNKNOWN;
  }

  vap_handle = mtlk_df_get_vap_handle(mtlk_df_user_get_df(df_user));
  core = mtlk_vap_get_core(vap_handle);

  dev_spec_info->vap_id  = mtlk_vap_get_id(vap_handle);
  dev_spec_info->mc_index = MCAST_GROUP_BROADCAST;
  dev_spec_info->sta_id = TMP_BCAST_DEST_ADDR;
  dev_spec_info->frame_type = (ntohs(ether_header->h_proto) >= ETH_P_802_3_MIN) ? FRAME_TYPE_ETHERNET : FRAME_TYPE_IPX_LLC_SNAP;

  if (mtlk_vap_is_ap(vap_handle)) {
    /* AP mode */
    if (mtlk_osal_eth_is_multicast(ether_header->h_dest)) {
      /* Multicast/broadcast*/
      if (!mtlk_osal_eth_is_broadcast(ether_header->h_dest)) {
          dev_spec_info->mc_index = mtlk_mc_get_grid(core, nbuf);
          if (dev_spec_info->mc_index == MCAST_GROUP_UNDEF)
            ILOG1_V("Undefined multicast group index");
      }
      dev_spec_info->mcf = 1;
    } else {
      /* Unicast */
      dst_sta = mtlk_stadb_find_sta(&core->slow_ctx->stadb, ether_header->h_dest);
      if (!dst_sta) /* search even if not WDS - to support STA bridge mode */
        dst_sta = mtlk_hstdb_find_sta(&core->slow_ctx->hstdb, ether_header->h_dest);

      if (dst_sta) {
        dev_spec_info->sta_id = mtlk_sta_get_sid(dst_sta);
        mtlk_sta_decref(dst_sta);
      }
      dev_spec_info->mcf = 0;
    }
  } else {
    /* STA mode */
    struct ieee80211_vif *vif = net_device_to_ieee80211_vif(nbuf->dev);
    u8 *bssid = wv_ieee80211_peer_ap_address(vif);
    if (bssid) {
      dst_sta = mtlk_stadb_find_sta(&core->slow_ctx->stadb, bssid);
      if (dst_sta) {
        dev_spec_info->sta_id = mtlk_sta_get_sid(dst_sta);
        mtlk_sta_decref(dst_sta);
      }
    }
    /* in STA mode we always send unicast packets*/
    dev_spec_info->mcf = 0;
  }
  return MTLK_ERR_OK;
}
#endif /* MTLK_USE_DIRECTCONNECT_DP_API */

#ifdef DEBUG_WPS
static char test_wps_ie0[] = {0xdd, 7, 0x00, 0x50, 0xf2, 0x04, 1, 2, 3};
static char test_wps_ie1[] = {0xdd, 7, 0x00, 0x50, 0xf2, 0x04, 4, 5, 6};
static char test_wps_ie2[] = {0xdd, 7, 0x00, 0x50, 0xf2, 0x04, 7, 8, 9};
#endif

static int bt_acs_send_interf_event(uint8 channel, int8 maximumValue)
{
  int rc;
  struct bt_acs_interf_event_t *interf_event;

  rc = MTLK_ERR_OK;

  /* allocate memory for notification event */
  interf_event = mtlk_osal_mem_alloc(sizeof(*interf_event), MTLK_MEM_TAG_CORE);
  if (!interf_event){
    return MTLK_ERR_NO_MEM;
  }

  interf_event->event_id = BT_ACS_EVNT_ID_INTERF_DET;
  interf_event->channel = channel;
  interf_event->floor_noise = maximumValue;

  mtlk_nl_bt_acs_send_brd_msg(interf_event, sizeof(*interf_event));

  mtlk_osal_mem_free(interf_event);

  return rc;
}

/*
+----+-------++-----------------++------------------- -----------------------------------------+
|     \      ||  Interf. Dis.   ||                   Inerf. Enabled                            |
|      \     ||--------+--------||--------+--------+----------+----------+----------+----------|
|       \    || No Meas| No Meas|| No Meas| No Meas| Measure  | Measure  | Measure  | Measure  |
|        \   || Scan20 | Scan40 || Scan20 | Scan40 | Scan20   | Scan20   | Scan40   | Scan40   |
|         \  || Regular| Regular|| Regular| Regular|  LONG    | SHORT    |  LONG    |  SHORT   |
|          \ || all       all   || all    | all    | unrestr  | restr    | unrestr  | restr    |
| Spectrum  \|| clr $$ | clr $$ || clr $$ | clr $$ |          |          |          |          |
+------------++--------+--------++--------+--------+----------+----------+----------+----------+
|   20       ||   1    |   .    ||    .   |   .    |    1     |    2     |    .     |    .     |
|   40       ||   2    |   1    ||    1   |   .    |    .     |    .     |    2     |    3     |
| 20/40,coex ||   2    |   1    ||    .   |   1    |    2     |    3     |    .     |    .     |
+------------++--------+--------++--------+--------+----------+----------+----------+----------+

1..3 - scan execution order
.    - scan skipped
*/
#define SCAN_TABLE_IDX_20     0
#define SCAN_TABLE_IDX_40     1
#define SCAN_TABLE_IDX_20_40  2
#define SCAN_TABLE_IDX_COEX   3
#define SCAN_TABLE_IDX_LAST   4

#define SCAN_TABLE_NO_MEAS_20_REGULAR_ALL   1
#define SCAN_TABLE_NO_MEAS_40_REGULAR_ALL   2
#define SCAN_TABLE_MEAS_20_LONG_UNRESTR     3
#define SCAN_TABLE_MEAS_20_SHORT_RESTR      4
#define SCAN_TABLE_MEAS_40_LONG_UNRESTR     5
#define SCAN_TABLE_MEAS_40_SHORT_RESTR      6
#define SCAN_TABLE_MEAS_LAST                7

#define SCAN_TABLE_ROW_LAST 4

static const uint8 scan_table_interf_en[SCAN_TABLE_IDX_LAST][SCAN_TABLE_ROW_LAST] = {
  /* SCAN_TABLE_IDX_20 */
  { SCAN_TABLE_MEAS_20_LONG_UNRESTR,   SCAN_TABLE_MEAS_20_SHORT_RESTR,  0, 0},

  /* SCAN_TABLE_IDX_40 */
  { SCAN_TABLE_NO_MEAS_20_REGULAR_ALL, SCAN_TABLE_MEAS_40_LONG_UNRESTR, SCAN_TABLE_MEAS_40_SHORT_RESTR, 0},

  /* SCAN_TABLE_IDX_20_40 */
  { SCAN_TABLE_NO_MEAS_40_REGULAR_ALL, SCAN_TABLE_MEAS_20_LONG_UNRESTR, SCAN_TABLE_MEAS_20_SHORT_RESTR, 0},

};

static const uint8 scan_table_interf_dis[SCAN_TABLE_IDX_LAST][SCAN_TABLE_ROW_LAST] = {
  /* SCAN_TABLE_IDX_20 */
  { SCAN_TABLE_NO_MEAS_20_REGULAR_ALL, 0, 0, 0},

  /* SCAN_TABLE_IDX_40 */
  { SCAN_TABLE_NO_MEAS_40_REGULAR_ALL, SCAN_TABLE_NO_MEAS_20_REGULAR_ALL, 0, 0},

  /* SCAN_TABLE_IDX_20_40 */
  { SCAN_TABLE_NO_MEAS_40_REGULAR_ALL, SCAN_TABLE_NO_MEAS_20_REGULAR_ALL, 0, 0},

};

static int
_mtlk_mbss_send_preactivate_req (struct nic *nic)
{
  mtlk_txmm_data_t          *man_entry = NULL;
  mtlk_txmm_msg_t           man_msg;
  int                       result = MTLK_ERR_OK;
  UMI_MBSS_PRE_ACTIVATE     *pPreActivate;
  UMI_MBSS_PRE_ACTIVATE_HDR *pPreActivateHeader;

  man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, mtlk_vap_get_txmm(nic->vap_handle), &result);
  if (man_entry == NULL) {
    ELOG_D("CID-%04x: Can't send PRE_ACTIVATE request to MAC due to the lack of MAN_MSG", mtlk_vap_get_oid(nic->vap_handle));
    goto FINISH;
  }

  man_entry->id           = UM_MAN_MBSS_PRE_ACTIVATE_REQ;
  man_entry->payload_size = mtlk_get_umi_mbss_pre_activate_size();

  memset(man_entry->payload, 0, man_entry->payload_size);
  pPreActivate = (UMI_MBSS_PRE_ACTIVATE *)(man_entry->payload);

  pPreActivateHeader = &pPreActivate->sHdr;
  pPreActivateHeader->u16Status = HOST_TO_MAC16(UMI_OK);

  wave_memcpy(pPreActivate->storedCalibrationChannelBitMap,
         sizeof(pPreActivate->storedCalibrationChannelBitMap),
         nic->storedCalibrationChannelBitMap,
         sizeof(nic->storedCalibrationChannelBitMap));

  ILOG1_D("CID-%04x: Sending UMI FW Preactivation", mtlk_vap_get_oid(nic->vap_handle));
  mtlk_dump(2, pPreActivate, sizeof(*pPreActivate), "dump of UMI_MBSS_PRE_ACTIVATE:");

  result = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);
  if (result != MTLK_ERR_OK) {
    ELOG_DD("CID-%04x: Can't send PRE_ACTIVATE request to MAC (err=%d)", mtlk_vap_get_oid(nic->vap_handle), result);
    goto FINISH;
  }

  if (MAC_TO_HOST16(pPreActivateHeader->u16Status) != UMI_OK) {
    ELOG_DD("CID-%04x: Error returned for PRE_ACTIVATE request to MAC (err=%d)", mtlk_vap_get_oid(nic->vap_handle), MAC_TO_HOST16(pPreActivateHeader->u16Status));
    result = MTLK_ERR_UMI;
    goto FINISH;
  }

FINISH:
  if (man_entry) {
    mtlk_txmm_msg_cleanup(&man_msg);
  }

  return result;
}

static int _core_set_preactivation_mibs (mtlk_core_t *core)
{
  wave_radio_t *radio = wave_vap_radio_get(core->vap_handle);

  MTLK_ASSERT(mtlk_vap_is_master_ap(core->vap_handle));

  if (MTLK_ERR_OK != mtlk_mib_set_pre_activate(core)) {
    return MTLK_ERR_UNKNOWN;
  }

  if (MTLK_ERR_OK != mtlk_set_mib_value_uint8(mtlk_vap_get_txmm(core->vap_handle), MIB_SPECTRUM_MODE,
                                              WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_PROG_MODEL_SPECTRUM_MODE))) {
    return MTLK_ERR_UNKNOWN;
  }

  return MTLK_ERR_OK;
}

/* FIXME soon to be removed */
static int
_mtlk_mbss_preactivate (struct nic *nic, BOOL rescan_exempted)
{
  int                     result = MTLK_ERR_OK;
  int                     channel;
  uint8                   ap_scan_band_cfg;
  int                     actual_spectrum_mode;
  int                     prog_model_spectrum_mode;
  wave_radio_t            *radio;

  MTLK_ASSERT(NULL != nic);

  radio = wave_vap_radio_get(nic->vap_handle);

  /* select and validate the channel and the spectrum mode*/
  channel = WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_CHANNEL_CFG);

  /************************************************************************/
  /* Add aocs initialization + loading of programming module                                                                     */
  /************************************************************************/

  /* now we have to perform an AP scan and update
   * the table after we have scan results. Do scan only in one band */
  ap_scan_band_cfg = core_cfg_get_freq_band_cfg(nic);
  ILOG1_DD("CID-%04x: ap_scan_band_cfg = %d", mtlk_vap_get_oid(nic->vap_handle), ap_scan_band_cfg);
  WAVE_RADIO_PDB_SET_INT(radio, PARAM_DB_RADIO_FREQ_BAND_CFG,
      ((ap_scan_band_cfg == MTLK_HW_BAND_2_4_GHZ) ? MTLK_HW_BAND_2_4_GHZ : MTLK_HW_BAND_5_2_GHZ) );

  /* restore all after AP scan */
  WAVE_RADIO_PDB_SET_INT(radio, PARAM_DB_RADIO_FREQ_BAND_CFG, ap_scan_band_cfg);

  /*
   * at this point spectrum & channel may be changed by AOCS
   */

  channel = WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_CHANNEL_CUR);
  actual_spectrum_mode = WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_SPECTRUM_MODE);
  prog_model_spectrum_mode = (actual_spectrum_mode == CW_20) ? CW_20 : CW_40;
  WAVE_RADIO_PDB_SET_INT(radio, PARAM_DB_RADIO_PROG_MODEL_SPECTRUM_MODE, prog_model_spectrum_mode);

  /* Progmodel loading was here, moving it out to mtlk_mbss_send_vap_activate() */

  /* now check AOCS result - here all state is already restored */
  if (result != MTLK_ERR_OK) {
    ELOG_D("CID-%04x: aocs did not find available channel", mtlk_vap_get_oid(nic->vap_handle));
    result = MTLK_ERR_AOCS_FAILED;
    goto FINISH;
  }

  /*
   * at this point spectrum & channel may be changed by COEX
   */

  /* Send LOG SIGNAL */
  SLOG0(SLOG_DFLT_ORIGINATOR, SLOG_DFLT_RECEIVER, mtlk_core_t, nic);

  _core_set_preactivation_mibs(nic);

  result = _mtlk_mbss_send_preactivate_req(nic);
  if (result != MTLK_ERR_OK) {
    goto FINISH;
  }

FINISH:
  if (result == MTLK_ERR_OK) {
    ILOG2_D("CID-%04x: _mtlk_mbss_preactivate returned successfully", mtlk_vap_get_oid(nic->vap_handle));
  }
  else {
    ELOG_D("CID-%04x: _mtlk_mbss_preactivate returned with error", mtlk_vap_get_oid(nic->vap_handle));
  }

  return result;
}

int mtlk_mbss_send_vap_db_add(struct nic *nic)
{
  mtlk_txmm_data_t        *man_entry = NULL;
  mtlk_txmm_msg_t         man_msg;
  int                     result = MTLK_ERR_OK;
  UMI_VAP_DB_OP           *pAddRequest;


  man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, mtlk_vap_get_txmm(nic->vap_handle), &result);
  if (man_entry == NULL) {
    ELOG_D("CID-%04x: Can't send VAP_DB ADD request to MAC due to the lack of MAN_MSG", mtlk_vap_get_oid(nic->vap_handle));
    goto FINISH;
  }

  man_entry->id           = UM_MAN_VAP_DB_REQ;
  man_entry->payload_size = sizeof (UMI_VAP_DB_OP);


  pAddRequest = (UMI_VAP_DB_OP *)(man_entry->payload);
  pAddRequest->u16Status = HOST_TO_MAC16(UMI_OK);
  pAddRequest->u8OperationCode = VAP_OPERATION_ADD;
  pAddRequest->u8VAPIdx = mtlk_vap_get_id(nic->vap_handle);

  result = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);
  if (result != MTLK_ERR_OK) {
    ELOG_DD("CID-%04x: Can't send VAP_DB ADD request to MAC (err=%d)", mtlk_vap_get_oid(nic->vap_handle), result);
    goto FINISH;
  }

  if (MAC_TO_HOST16(pAddRequest->u16Status) != UMI_OK) {
    ELOG_DD("CID-%04x: Error returned for VAP_DB ADD request to MAC (err=%d)", mtlk_vap_get_oid(nic->vap_handle), MAC_TO_HOST16(pAddRequest->u16Status));
    result = MTLK_ERR_UMI;
    goto FINISH;
  }

FINISH:
  if (man_entry) {
    mtlk_txmm_msg_cleanup(&man_msg);
  }

  return result;
}

/* FIXME soon to be removed */
static int
_mtlk_mbss_preactivate_if_needed (mtlk_core_t *core, BOOL rescan_exempted)
{
  int result = MTLK_ERR_OK;

  MTLK_ASSERT(NULL != core);
  if (0 == mtlk_vap_manager_get_active_vaps_number(mtlk_vap_get_manager(core->vap_handle))
      && mtlk_core_get_net_state(core) != NET_STATE_ACTIVATING) {
    result = _mtlk_mbss_preactivate(mtlk_core_get_master(core), rescan_exempted);
  }
  return result;
}

/* Get/set param DB value of MGMT_FRAMES_RATE */
static uint32 _core_management_frames_rate_get (mtlk_core_t *core)
{
  return (uint32)MTLK_CORE_PDB_GET_INT(core, PARAM_DB_CORE_MGMT_FRAMES_RATE);
}

void __MTLK_IFUNC wave_core_management_frames_rate_set (mtlk_core_t *core, uint32 value)
{
  MTLK_CORE_PDB_SET_INT(core, PARAM_DB_CORE_MGMT_FRAMES_RATE, value);
}

/* We can't to use the real configured value because we don't send 11g rates to FW in 11b mode */
static uint32 _core_management_frames_rate_select (mtlk_core_t *core)
{
  mtlk_core_t *mcore = mtlk_core_get_master(core);

  if ((!mcore) || ((UMI_PHY_TYPE_BAND_2_4_GHZ == __wave_core_chandef_get(mcore)->chan.band) &&
                   (MTLK_NETWORK_11B_ONLY == core_cfg_get_network_mode_cur(core)))) {
    WLOG_DD("CID-%04x: Set MCS VAP Management to default (%02x) value in FW due to 11B HW mode",
            mtlk_vap_get_oid(core->vap_handle), FIXED_MCS_VAP_MANAGEMENT_IS_NOT_VALID);
    return FIXED_MCS_VAP_MANAGEMENT_IS_NOT_VALID;
  }
  return _core_management_frames_rate_get(core);
}

#define WAVE_HW_11B_RATES_NUM  4
#define WAVE_HW_11G_RATES_NUM  8
#define WAVE_HW_11A_RATES_NUM  8

static uint32 _core_basic_rates_11g_set (mtlk_core_t *core, mtlk_hw_band_e band, uint8 *rates_buf, uint32 rates_buf_len, uint32 rates_fill_len)
{
  uint32 rates_len = rates_fill_len;
  uint8 rates[] = {
    (1 * MBIT_RATE_ENCODING_MUL), /* rates 11b */
    (2 * MBIT_RATE_ENCODING_MUL),
    (55 / HUNDRED_KBIT_RATE_ENCODING_DIV),
    (11 * MBIT_RATE_ENCODING_MUL),
    (6 * MBIT_RATE_ENCODING_MUL), /* rates 11g or 11a */
    (9 * MBIT_RATE_ENCODING_MUL),
    (12 * MBIT_RATE_ENCODING_MUL),
    (18 * MBIT_RATE_ENCODING_MUL),
    (24 * MBIT_RATE_ENCODING_MUL),
    (36 * MBIT_RATE_ENCODING_MUL),
    (48 * MBIT_RATE_ENCODING_MUL),
    (54 * MBIT_RATE_ENCODING_MUL)
  };

  if (UMI_PHY_TYPE_BAND_2_4_GHZ == band)
  {
    uint32 mode = core_cfg_get_network_mode_cur(core);

    if ((MTLK_NETWORK_11BG_MIXED == mode) ||
        (MTLK_NETWORK_11BGN_MIXED == mode) ||
        (MTLK_NETWORK_11BGNAX_MIXED == mode))
    {
      if (rates_buf_len >= sizeof(rates)) {
        wave_memcpy(rates_buf, rates_buf_len, rates, sizeof(rates));
        rates_len = sizeof(rates);
      }
    }
    else if (MTLK_NETWORK_11B_ONLY == mode)
    {
      if (rates_buf_len >= WAVE_HW_11B_RATES_NUM) {
        wave_memcpy(rates_buf, rates_buf_len, rates, WAVE_HW_11B_RATES_NUM);
        rates_len = WAVE_HW_11B_RATES_NUM;
      }
    }
  }
  else /* if (UMI_PHY_TYPE_BAND_5_2_GHZ == band) */
  {
    if (rates_buf_len >= WAVE_HW_11A_RATES_NUM) {
      wave_memcpy(rates_buf, rates_buf_len, &rates[WAVE_HW_11B_RATES_NUM], WAVE_HW_11A_RATES_NUM);
      rates_len = WAVE_HW_11A_RATES_NUM;
    }
  }
  return rates_len;
}

uint32 __MTLK_IFUNC wave_core_param_db_basic_rates_get (mtlk_core_t *core, mtlk_hw_band_e band, uint8 *rates_buf, uint32 rates_buf_len)
{
  mtlk_pdb_size_t rates_len = rates_buf_len;
  mtlk_pdb_t *param_db_core = mtlk_vap_get_param_db(core->vap_handle);
  mtlk_pdb_id_t param_id = (band == UMI_PHY_TYPE_BAND_2_4_GHZ ?
                            PARAM_DB_CORE_BASIC_RATES_2 : PARAM_DB_CORE_BASIC_RATES_5);

  if (MTLK_ERR_OK == wave_pdb_get_binary(param_db_core, param_id, rates_buf, &rates_len))
    rates_len = _core_basic_rates_11g_set(core, band, rates_buf, rates_buf_len, rates_len);
  else
    rates_len = 0;

  return rates_len;
}

/* band needed to load the right progmodels and for initial basic rate choosing */
int mtlk_mbss_send_vap_activate(struct nic *nic, mtlk_hw_band_e band)
{
  mtlk_txmm_data_t* man_entry=NULL;
  mtlk_txmm_msg_t activate_msg;
  UMI_ADD_VAP *req;
  int result = MTLK_ERR_OK;
  uint8 vap_id = mtlk_vap_get_id(nic->vap_handle);
  mtlk_pdb_t *param_db_core = mtlk_vap_get_param_db(nic->vap_handle);
  int net_state = mtlk_core_get_net_state(nic);
  mtlk_txmm_t  *txmm = mtlk_vap_get_txmm(nic->vap_handle);

  u8 mbssid_vap_mode = wave_pdb_get_int(param_db_core, PARAM_DB_CORE_MBSSID_VAP);

  /* If the VAP has already been added, skip all the work */
  if (net_state & (NET_STATE_ACTIVATING | NET_STATE_DEACTIVATING | NET_STATE_CONNECTED))
    goto end;

  /* FIXME this thing is supposed to get removed soon */
  if (MTLK_ERR_OK != (result = _mtlk_mbss_preactivate_if_needed(nic, FALSE)))
  {
    ELOG_D("CID-%04x: _mtlk_mbss_preactivate_if_needed returned with error", mtlk_vap_get_oid(nic->vap_handle));
    goto end;
  }

  /* we switch beforehand because the state switch does some checks; we'll correct the state if we fail */
  if (mtlk_core_set_net_state(nic, NET_STATE_ACTIVATING) != MTLK_ERR_OK) {
    ELOG_D("CID-%04x: Failed to switch core to state ACTIVATING", mtlk_vap_get_oid(nic->vap_handle));
    result = MTLK_ERR_NOT_READY;
    goto end;
  }

  ILOG1_D("CID-%04x: Start activation", mtlk_vap_get_oid(nic->vap_handle));

  mtlk_vap_set_ready(nic->vap_handle);

  man_entry = mtlk_txmm_msg_init_with_empty_data(&activate_msg, txmm, &result);
  if (man_entry == NULL)
  {
    ELOG_D("CID-%04x: Can't send ACTIVATE request to MAC due to the lack of MAN_MSG", mtlk_vap_get_oid(nic->vap_handle));
    goto FINISH;
  }

  man_entry->id           = UM_MAN_ADD_VAP_REQ;
  man_entry->payload_size = sizeof(*req);

  req = (UMI_ADD_VAP *) man_entry->payload;
  memset(req, 0, sizeof(*req));

  wave_pdb_get_mac(param_db_core, PARAM_DB_CORE_MAC_ADDR, &req->sBSSID);
  ILOG2_DY("Mac addr for VapID %u from param DB is %Y", vap_id, &req->sBSSID.au8Addr);
  req->vapId = vap_id;


  if (mtlk_vap_is_ap(nic->vap_handle)) {
    if(WAVE_RADIO_OPERATION_MODE_MBSSID_TRANSMIT_VAP == mbssid_vap_mode) {
      req->operationMode = OPERATION_MODE_MBSSID_TRANSMIT_VAP;
    } else if(WAVE_RADIO_OPERATION_MODE_MBSSID_NON_TRANSMIT_VAP == mbssid_vap_mode) {
      req->operationMode = OPERATION_MODE_MBSSID_NON_TRANSMIT_VAP;
    } else {
      req->operationMode = OPERATION_MODE_NORMAL;
    }
  } else {
    req->operationMode = OPERATION_MODE_VSTA;
  }

  ILOG2_D("mbssid_vap_mode: %d", mbssid_vap_mode);
  ILOG2_D("UMI_ADD_VAP->operationMode: %d", req->operationMode);

  req->u8Rates_Length = (uint8)wave_core_param_db_basic_rates_get(nic, band, req->u8Rates, sizeof(req->u8Rates));

  memset(&req->u8TX_MCS_Bitmask, 0, sizeof(req->u8TX_MCS_Bitmask));
  /* Each MCS Map subfield of u8VHT_Mcs_Nss/u8HE_Mcs_Nss is two bits wide,
   * 3 indicates that n spatial streams is not supported for HE PPDUs.
   * Filling it with 0xff - meaning all subfields are equal to 3 by default */
  memset(&req->u8VHT_Mcs_Nss, 0xff, sizeof(req->u8VHT_Mcs_Nss));
  memset(&req->u8HE_Mcs_Nss, 0xff, sizeof(req->u8HE_Mcs_Nss));

  nic->activation_status = FALSE;

  mtlk_osal_event_reset(&nic->slow_ctx->connect_event);

  mtlk_dump(2, req, sizeof(*req), "dump of UMI_ADD_VAP:");

  ILOG0_DY("CID-%04x: UMI_ADD_VAP, BSSID %Y", mtlk_vap_get_oid(nic->vap_handle), &req->sBSSID);

  result = mtlk_txmm_msg_send_blocked(&activate_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);
  if (result != MTLK_ERR_OK) {
    ELOG_DD("CID-%04x: Cannot send ADD_VAP request due to TXMM err#%d",
            mtlk_vap_get_oid(nic->vap_handle), result);
    goto FINISH;
  }

  nic->activation_status = TRUE;
  nic->is_stopped = FALSE;
  mtlk_vap_manager_notify_vap_activated(mtlk_vap_get_manager(nic->vap_handle));
  result = MTLK_ERR_OK;
  ILOG1_SDDDS("%s: Activated: is_stopped=%u, is_stopping=%u, is_iface_stopping=%u, net_state=%s",
              mtlk_df_get_name(mtlk_vap_get_df(nic->vap_handle)),
              nic->is_stopped, nic->is_stopping, nic->is_iface_stopping,
              mtlk_net_state_to_string(mtlk_core_get_net_state(nic)));

FINISH:
  if (result != MTLK_ERR_OK && mtlk_core_get_net_state(nic) != NET_STATE_READY)
     mtlk_core_set_net_state(nic, NET_STATE_READY);

  if (man_entry)
    mtlk_txmm_msg_cleanup(&activate_msg);

end:
  return result;
}


/* ===============================================================================
 * This function fills UMI_SET_BSS request with values from parameters db,
 * combined with bss_parameters.
 * =============================================================================== */
static void
_mtlk_core_fill_set_bss_request(mtlk_core_t *core, UMI_SET_BSS *req, struct mtlk_bss_parameters *params)
{
    mtlk_pdb_t *param_db_core = mtlk_vap_get_param_db(core->vap_handle);
    struct nic *master_core = mtlk_vap_manager_get_master_core(mtlk_vap_get_manager(core->vap_handle));
    struct mtlk_chan_def *current_chandef = __wave_core_chandef_get(master_core);
    wave_radio_t *radio = wave_vap_radio_get(core->vap_handle);
    uint16 dtim_int, beacon_int;

    MTLK_ASSERT(req);
    MTLK_ASSERT(params);
    memset(req, 0, sizeof(*req));

    /* Dealing w/ cts protection */
    if (params->use_cts_prot == -1)
        params->use_cts_prot = MTLK_CORE_PDB_GET_INT(core, PARAM_DB_CORE_HT_PROTECTION);
    req->protectionMode = params->use_cts_prot;

    /* Dealing w/ short slot time */
    if (params->use_short_slot_time == -1)
        params->use_short_slot_time = WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_SHORT_SLOT);
    req->slotTime = params->use_short_slot_time;

    /* Dealing w/ short preamble */
    if (params->use_short_preamble == -1)
        params->use_short_preamble = WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_SHORT_PREAMBLE);
    req->useShortPreamble = params->use_short_preamble;
    /* FIXME figure out how preamble affects the rates */

    /* DTIM and Beacon interval are set in start_ap and stored in param_db */
    beacon_int = (uint16) WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_BEACON_PERIOD);
    dtim_int = (uint16) MTLK_CORE_PDB_GET_INT(core, PARAM_DB_CORE_DTIM_PERIOD);
    req->beaconInterval = HOST_TO_MAC16(beacon_int);
    req->dtimInterval = HOST_TO_MAC16(dtim_int);
    ILOG1_DD("dtim_int: %d beacon_int: %d", dtim_int, beacon_int);

    /* Dealing w/ basic rates */
    if (!params->basic_rates_len) {
        /* copy existing parameters from params db */
        req->u8Rates_Length = (uint8)wave_core_param_db_basic_rates_get(core, current_chandef->chan.band,
                                                                        req->u8Rates, sizeof(req->u8Rates));
    }
    else {
        /* copy from input structure */
        wave_memcpy(req->u8Rates, sizeof(req->u8Rates), params->basic_rates, params->basic_rates_len);
        req->u8Rates_Length = (uint8)_core_basic_rates_11g_set(core, current_chandef->chan.band,
                                                               req->u8Rates, sizeof(req->u8Rates), params->basic_rates_len);
    }

    /* Dealing w/ HT/VHT MCS-s and flags */
    {
        mtlk_pdb_size_t mcs_len;
        mcs_len = sizeof(req->u8TX_MCS_Bitmask);
        wave_pdb_get_binary(param_db_core, PARAM_DB_CORE_RX_MCS_BITMASK, &req->u8TX_MCS_Bitmask, &mcs_len);

        mcs_len = sizeof(req->u8VHT_Mcs_Nss);
        wave_pdb_get_binary(param_db_core, PARAM_DB_CORE_VHT_MCS_NSS, &req->u8VHT_Mcs_Nss, &mcs_len);

        req->flags = wave_pdb_get_int(param_db_core, PARAM_DB_CORE_SET_BSS_FLAGS);

        if (wave_pdb_get_int(param_db_core, PARAM_DB_CORE_RELIABLE_MCAST)) {
            req->flags |= MTLK_BFIELD_VALUE(VAP_ADD_FLAGS_RELIABLE_MCAST, 1, uint8);
        }

        if (mtlk_df_user_get_hs20_status(mtlk_vap_get_df(core->vap_handle))) {
            req->flags |= MTLK_BFIELD_VALUE(VAP_ADD_FLAGS_HS20_ENABLE, 1, uint8);
        }
    }

    /* MultiBSS related */
    req->mbssIdNumOfVapsInGroup = (uint8)wave_pdb_get_int(param_db_core, PARAM_DB_CORE_MBSSID_NUM_VAPS_IN_GROUP);
    ILOG2_D("UMI_SET_BSS->mbssIdNumOfVapsInGroup:%d", req->mbssIdNumOfVapsInGroup);

    /* 802.11AX HE support */
    {
      mtlk_pdb_size_t mcs_len;
      mcs_len = sizeof(req->u8HE_Mcs_Nss);
      /* Each MCS Map subfield of u8VHT_Mcs_Nss/u8HE_Mcs_Nss is two bits wide,
       * 3 indicates that n spatial streams is not supported for HE PPDUs.
       * Filling it with 0xff - meaning all subfields are equal to 3 by default */
      memset(req->u8HE_Mcs_Nss, 0xff, sizeof(req->u8HE_Mcs_Nss));
      wave_pdb_get_binary(param_db_core, PARAM_DB_CORE_HE_MCS_NSS, &req->u8HE_Mcs_Nss, &mcs_len);
      mtlk_dump(1, &req->u8HE_Mcs_Nss, mcs_len, "dump of u8HE_Mcs_Nss:");
    }
}

/* ===============================================================================
 * This function updates values in parameters db using data in already filled
 * UMI_SET_BSS request, according to values in bss_parameters as update indicators
 * ===============================================================================
 */
static void
_mtlk_core_update_bss_params(mtlk_core_t *core, UMI_SET_BSS *req, struct mtlk_bss_parameters *params)
{
    mtlk_pdb_t *param_db_core = mtlk_vap_get_param_db(core->vap_handle);
    struct nic *master_core = mtlk_vap_manager_get_master_core(mtlk_vap_get_manager(core->vap_handle));
    struct mtlk_chan_def *current_chandef = __wave_core_chandef_get(master_core);
    wave_radio_t *radio = wave_vap_radio_get(core->vap_handle);

    /* Dealing w/ cts protection */
    if (params->use_cts_prot != -1)
      MTLK_CORE_PDB_SET_INT(core, PARAM_DB_CORE_HT_PROTECTION, req->protectionMode);

    /* Dealing w/ short slot time */
    if (params->use_short_slot_time != -1)
      WAVE_RADIO_PDB_SET_INT(radio, PARAM_DB_RADIO_SHORT_SLOT, req->slotTime);

    /* Dealing w/ short preamble */
    if (params->use_short_preamble != -1)
      WAVE_RADIO_PDB_SET_INT(radio, PARAM_DB_RADIO_SHORT_PREAMBLE, req->useShortPreamble);
    /* FIXME figure out how preamble affects the rates */

    /* Dealing w/ basic rates */
    if (params->basic_rates_len) {
        wave_pdb_set_binary(param_db_core,
                          (current_chandef->chan.band == UMI_PHY_TYPE_BAND_2_4_GHZ
                           ? PARAM_DB_CORE_BASIC_RATES_2
                           : PARAM_DB_CORE_BASIC_RATES_5),
                             req->u8Rates, params->basic_rates_len);
    }
}

/* ===============================================================================
 * This function processes UM_BSS_SET_BSS_REQ FW request
 * ===============================================================================
 * If we are in master serializer context we need to prevent set chan for the master core
 * (otherwize there will be a dead-lock) but still the actual FW SET_BSS message should be
 * sent for the relevant core.
 */
int __MTLK_IFUNC mtlk_core_set_bss(mtlk_core_t *core, mtlk_core_t *context_core, UMI_SET_BSS *fw_params)
{
    mtlk_txmm_msg_t man_msg;
    mtlk_txmm_data_t *man_entry;
    UMI_SET_BSS *req;
    int res = MTLK_ERR_OK;
    uint8 vap_id = mtlk_vap_get_id(core->vap_handle);
    mtlk_pdb_t *param_db_core = mtlk_vap_get_param_db(core->vap_handle);
    int twt_operation_mode = wave_pdb_get_int(param_db_core, PARAM_DB_CORE_TWT_OPERATION_MODE);
    uint8 mbssIdNumOfVapsInGroup = wave_pdb_get_int(param_db_core, PARAM_DB_CORE_MBSSID_NUM_VAPS_IN_GROUP);
    MTLK_ASSERT(fw_params);
    MTLK_ASSERT(TRUE >= twt_operation_mode);

    /* prepare msg for the FW */
    if (!(man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, mtlk_vap_get_txmm(core->vap_handle), &res))) {
      ELOG_DD("CID-%04x: UM_BSS_SET_BSS_REQ init failed, err=%i",
             mtlk_vap_get_oid(core->vap_handle), res);
      res = MTLK_ERR_NO_RESOURCES;
      goto end;
    }

    man_entry->id = UM_BSS_SET_BSS_REQ;
    man_entry->payload_size = sizeof(*req);
    req = (UMI_SET_BSS *) man_entry->payload;
    /* copy input structure and setup vapId */
    *req = *fw_params;
    req->vapId = vap_id;
    req->twtOpreationMode = twt_operation_mode;
    req->mbssIdNumOfVapsInGroup = mbssIdNumOfVapsInGroup;
    req->fixedMcsVapManagement = (uint8)_core_management_frames_rate_select(core);

    /* temporary hardcode bss color always to 4 */
    {
      req->u8HE_Bss_Color = 4u;
      ILOG0_D("UMI_SET_BSS->u8HE_Bss_Color:%d", req->u8HE_Bss_Color);
    }


    ILOG2_D("UMI_SET_BSS->mbssIdNumOfVapsInGroup:%d", req->mbssIdNumOfVapsInGroup);

    finish_and_prevent_fw_set_chan(context_core, TRUE); /* set_bss is a "process", so need this; core is the core on which we're running */

    mtlk_dump(2, req, sizeof(*req), "dump of UMI_SET_BSS:");

    /* send message to FW */
    res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);

    /* process result */
    if (res != MTLK_ERR_OK)
        ELOG_DD("CID-%04x: UM_BSS_SET_BSS_REQ send failed, err=%i",
                mtlk_vap_get_oid(core->vap_handle), res);
    else if (mtlk_core_get_net_state(core) != NET_STATE_CONNECTED
             && MTLK_ERR_OK != (res = mtlk_core_set_net_state(core, NET_STATE_CONNECTED))) {
      ELOG_D("CID-%04x: Failed to switch core to state CONNECTED", mtlk_vap_get_oid(core->vap_handle));
    }

    if (NULL != man_entry)
      mtlk_txmm_msg_cleanup(&man_msg);

  end:
    finish_and_prevent_fw_set_chan(context_core, FALSE);
    return res;
}

/* This function should be called from master serializer context */
static int
_core_change_bss_by_params(mtlk_core_t *master_core, struct mtlk_bss_parameters *params)
{
  mtlk_core_t *core = NULL;
  UMI_SET_BSS fw_request;
  int res = MTLK_ERR_OK;
  mtlk_pdb_t *param_db_core = NULL;
  mtlk_vap_handle_t vap_handle;
  wave_radio_t *radio;

  MTLK_ASSERT(master_core == mtlk_core_get_master(master_core));

  /* Prevent set bss while in block_tx */
  if (mtlk_core_is_block_tx_mode(master_core)) {
    res = MTLK_ERR_NOT_READY;
    goto end;
  }

  /* Prevent set bss while in scan */
  if (mtlk_core_is_in_scan_mode(master_core)) {
    res = MTLK_ERR_RETRY;
    goto end;
  }

  if (MTLK_ERR_OK != mtlk_vap_manager_get_vap_handle(mtlk_vap_get_manager(master_core->vap_handle), params->vap_id, &vap_handle)) {
    res = MTLK_ERR_VALUE;
    goto end;
  }
  core = mtlk_vap_get_core (vap_handle);
  param_db_core = mtlk_vap_get_param_db(core->vap_handle);
  radio = wave_vap_radio_get(core->vap_handle);

  if (core->net_state != NET_STATE_ACTIVATING &&
      core->net_state != NET_STATE_CONNECTED)
    goto end;

  if (mtlk_vap_is_ap(core->vap_handle)) {
    if (!wave_core_sync_hostapd_done_get(core)) {
      ILOG1_D("CID-%04x: HOSTAPD STA database is not synced", mtlk_vap_get_oid(core->vap_handle));
      res = MTLK_ERR_NOT_READY;
      goto end;
    }

    /* Update ap_isolate parameter */
    wave_pdb_set_int(param_db_core, PARAM_DB_CORE_AP_FORWARDING, !params->ap_isolate);

    /* Update ht_protection value if it was changed earlier by iwpriv */
    if (MTLK_CORE_PDB_GET_INT(core, PARAM_DB_CORE_IWPRIV_FORCED) & PARAM_DB_CORE_IWPRIV_FORCED_HT_PROTECTION_FLAG) {
        params->use_cts_prot = MTLK_CORE_PDB_GET_INT(core, PARAM_DB_CORE_HT_PROTECTION);
    }
  } else {
    wave_pdb_set_int(param_db_core, PARAM_DB_CORE_AP_FORWARDING, FALSE);
  }

  /* Fill UMI_SET_BSS FW request */
  _mtlk_core_fill_set_bss_request(core, &fw_request, params);

  /* Process UM_BSS_SET_BSS_REQ */
  res = mtlk_core_set_bss(core, master_core, &fw_request);

  /* Update params db */
  if (MTLK_ERR_OK == res)
      _mtlk_core_update_bss_params(core, &fw_request, params);

end:
  return res;
}

/* This function should be called from master serializer context */
static int
_core_change_bss (mtlk_handle_t hcore, const void* data, uint32 data_size)
{
  mtlk_core_t *master_core = HANDLE_T_PTR(mtlk_core_t, hcore);
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;
  struct mtlk_bss_parameters *params;
  unsigned params_size;
  int res = MTLK_ERR_OK;

  MTLK_ASSERT(sizeof(mtlk_clpb_t *) == data_size);

  params = mtlk_clpb_enum_get_next(clpb, &params_size);
  MTLK_CLPB_TRY(params, params_size)
    res = _core_change_bss_by_params(master_core, params);
  MTLK_CLPB_FINALLY(res)
    return mtlk_clpb_push_res(clpb, res);
  MTLK_CLPB_END;
}

static int _mtlk_core_send_stop_vap_traffic(struct nic *nic)
{
  mtlk_txmm_msg_t man_msg;
  mtlk_txmm_data_t *man_entry = NULL;
  UMI_STOP_VAP_TRAFFIC *req;
  int net_state = mtlk_core_get_net_state(nic);
  unsigned vap_id = mtlk_vap_get_id(nic->vap_handle);
  int result = MTLK_ERR_OK;

  ILOG0_DD("STOP_VAP_TRAFFIC VapID=%u, net_state=%i", vap_id, net_state);

  if (net_state == NET_STATE_HALTED) {
    /* Do not send anything to halted MAC or if STA hasn't been connected */
    clean_all_sta_on_disconnect(nic);
    return MTLK_ERR_OK;
  }

  /* we switch the states beforehand because this does some checks; we'll fix the state if we fail */
  if ((result = mtlk_core_set_net_state(nic, NET_STATE_DEACTIVATING)) != MTLK_ERR_OK) {
    goto FINISH;
  }

  man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, mtlk_vap_get_txmm(nic->vap_handle), &result);
  if (man_entry == NULL) {
    ELOG_D("CID-%04x: Can't init STOP_VAP_TRAFFIC request due to the lack of MAN_MSG",
           mtlk_vap_get_oid(nic->vap_handle));
    goto FINISH;
  }

  core_ap_disconnect_all(nic);

  man_entry->id = UM_MAN_STOP_VAP_TRAFFIC_REQ;
  man_entry->payload_size = sizeof(*req);
  req = (UMI_STOP_VAP_TRAFFIC *) man_entry->payload;

  req->u16Status = HOST_TO_MAC16(UMI_OK);
  req->vapId = vap_id;

  mtlk_dump(2, req, sizeof(*req), "dump of UMI_STOP_VAP_TRAFFIC:");

  result = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);
  if (result != MTLK_ERR_OK) {
    ELOG_DD("CID-%04x: Can't send STOP_VAP_TRAFFIC request to VAP (err=%d)",
            mtlk_vap_get_oid(nic->vap_handle), result);
    mtlk_core_set_net_state(nic, net_state); /* restore previous net_state if we can */
    goto FINISH;
  }

  if (MAC_TO_HOST16(req->u16Status) != UMI_OK) {
    WLOG_DD("CID-%04x: STOP_VAP_TRAFFIC failed in FW (status=%u)", mtlk_vap_get_oid(nic->vap_handle),
      MAC_TO_HOST16(req->u16Status));
    result = MTLK_ERR_MAC;
    mtlk_core_set_net_state(nic, net_state); /* restore previous net_state if we can */
    goto FINISH;
  }

FINISH:
  if (man_entry)
    mtlk_txmm_msg_cleanup(&man_msg);

  return result;
}

static int _mtlk_core_send_vap_remove(struct nic *nic)
{
  mtlk_txmm_msg_t man_msg;
  mtlk_txmm_data_t *man_entry = NULL;
  UMI_REMOVE_VAP *req;
  int net_state = mtlk_core_get_net_state(nic);
  int result = MTLK_ERR_OK;
  uint8 vap_id = mtlk_vap_get_id(nic->vap_handle);

  if (net_state == NET_STATE_HALTED) {
    /* Do not send anything to halted MAC or if STA hasn't been connected */
    clean_all_sta_on_disconnect(nic);
    return MTLK_ERR_OK;
  }

  man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, mtlk_vap_get_txmm(nic->vap_handle), &result);
  if (man_entry == NULL) {
    ELOG_D("CID-%04x: Can't send REMOVE_VAP request to MAC due to the lack of MAN_MSG",
           mtlk_vap_get_oid(nic->vap_handle));
    goto FINISH;
  }

  /* we switch the states beforehand because this does some checks; we'll fix the state if we fail */
  if ((result = mtlk_core_set_net_state(nic, NET_STATE_READY)) != MTLK_ERR_OK) {
    goto FINISH;
  }

  man_entry->id           = UM_MAN_REMOVE_VAP_REQ;
  /* the structs are identical so we could have not bothered with this... */
  man_entry->payload_size = sizeof(*req);
  req = (UMI_REMOVE_VAP *) man_entry->payload;

  req->u16Status = HOST_TO_MAC16(UMI_OK);
  req->vapId = vap_id;

  mtlk_dump(2, req, sizeof(*req), "dump of UMI_REMOVE_VAP:");

  result = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);
  if (result != MTLK_ERR_OK) {
    ELOG_DD("CID-%04x: Can't send REMOVE_VAP request to VAP (err=%d)",
            mtlk_vap_get_oid(nic->vap_handle), result);
    mtlk_core_set_net_state(nic, net_state); /* restore previous net_state if we can */
    goto FINISH;
  }

  if (MAC_TO_HOST16(req->u16Status) != UMI_OK) {
    WLOG_DD("CID-%04x: REMOVE_VAP failed in FW (status=%u)", mtlk_vap_get_oid(nic->vap_handle),
      MAC_TO_HOST16(req->u16Status));
    result = MTLK_ERR_MAC;
    mtlk_core_set_net_state(nic, net_state); /* restore previous net_state if we can */
    goto FINISH;
  }

  /* update disconnections statistics */
  nic->pstats.num_disconnects++;
#if IWLWAV_RTLOG_MAX_DLEVEL >= 1
  {
    IEEE_ADDR mac_addr;
    MTLK_CORE_PDB_GET_MAC(nic, PARAM_DB_CORE_MAC_ADDR, &mac_addr);
    ILOG1_DDYD("CID-%04x: VapID %u at %Y disconnected (status %u)", mtlk_vap_get_oid(nic->vap_handle),
              vap_id, mac_addr.au8Addr, MAC_TO_HOST16(req->u16Status));
  }
#endif
FINISH:
  if (man_entry)
    mtlk_txmm_msg_cleanup(&man_msg);

  return result;
}

static void _mtlk_core_flush_bcast_probe_resp_list (mtlk_core_t *nic);
static void _mtlk_core_flush_ucast_probe_resp_list (mtlk_core_t *nic);
static int  _mtlk_core_add_ucast_probe_resp_entry (mtlk_core_t* nic, const IEEE_ADDR *addr);
static int  _mtlk_core_del_bcast_probe_resp_entry (mtlk_core_t *nic, const IEEE_ADDR *addr);
static int  _mtlk_core_del_ucast_probe_resp_entry (mtlk_core_t *nic, const IEEE_ADDR *addr);
static BOOL _mtlk_core_ucast_probe_resp_entry_exists (mtlk_core_t *nic, const IEEE_ADDR *addr);

void __MTLK_IFUNC
mtlk_core_on_bss_tx (mtlk_core_t *nic,  uint32 subtype) {
  MTLK_ASSERT(nic);
  MTLK_UNREFERENCED_PARAM(subtype); /* can be used in the future */
  ILOG3_DD("CID-%04x: mgmt subtype %d", mtlk_vap_get_oid(nic->vap_handle), subtype);
  mtlk_core_inc_cnt(nic, MTLK_CORE_CNT_MAN_FRAMES_SENT);
}

void __MTLK_IFUNC
mtlk_core_on_bss_cfm (mtlk_core_t *nic, IEEE_ADDR *peer, uint32 extra_processing, uint32 subtype, BOOL is_broadcast) {
  MTLK_ASSERT(nic);
  mtlk_core_inc_cnt(nic, MTLK_CORE_CNT_MAN_FRAMES_CONFIRMED);

  ILOG3_DDDYD("CID-%04x: type %d, subtype %d, peer %Y, is_broadcast %d",
            mtlk_vap_get_oid(nic->vap_handle), extra_processing, subtype, peer->au8Addr, (int)is_broadcast);
  if (extra_processing == PROCESS_NULL_DATA_PACKET && nic->waiting_for_ndp_ack)
    mtlk_osal_event_set(&nic->ndp_acked);
  if (subtype == MAN_TYPE_PROBE_RES) {
    if (is_broadcast) {
        _mtlk_core_del_bcast_probe_resp_entry(nic, peer);
    } else {
        _mtlk_core_del_ucast_probe_resp_entry(nic, peer);
    }
  }
}

/* Update counters for BSS reserved queue */
void __MTLK_IFUNC
mtlk_core_on_bss_res_queued(mtlk_core_t *nic) {
  MTLK_ASSERT(nic);
  mtlk_core_inc_cnt(nic, MTLK_CORE_CNT_MAN_FRAMES_RES_QUEUE);
}

void __MTLK_IFUNC
mtlk_core_on_bss_res_freed(mtlk_core_t *nic) {
  MTLK_ASSERT(nic);
  mtlk_core_dec_cnt(nic, MTLK_CORE_CNT_MAN_FRAMES_RES_QUEUE);
}

int __MTLK_IFUNC
mtlk_wait_all_packets_confirmed(mtlk_core_t *nic)
{
#define SQ_WAIT_ALL_PACKETS_CFM_TIMEOUT_TOTAL  (2 * MSEC_PER_SEC)
#define SQ_WAIT_ALL_PACKETS_CFM_TIMEOUT_ONCE   10
  int wait_cnt = (SQ_WAIT_ALL_PACKETS_CFM_TIMEOUT_TOTAL / SQ_WAIT_ALL_PACKETS_CFM_TIMEOUT_ONCE);
  int res = MTLK_ERR_OK;
  uint32    dat_uncfm, bss_sent, bss_cfm, bss_uncfm, bss_res_queue;

  for (;;) {
    dat_uncfm = mtlk_osal_atomic_get(&nic->unconfirmed_data);
    bss_sent  = _mtlk_core_get_cnt(nic, MTLK_CORE_CNT_MAN_FRAMES_SENT);     /* main ring */
    bss_cfm   = _mtlk_core_get_cnt(nic, MTLK_CORE_CNT_MAN_FRAMES_CONFIRMED);
    bss_uncfm = bss_sent - bss_cfm;
    bss_res_queue = _mtlk_core_get_cnt(nic, MTLK_CORE_CNT_MAN_FRAMES_RES_QUEUE);  /* reserved queue */

    ILOG4_DDDDDDD("CID-%04x: wait_cnt %3d,  data:%d, bss:%d (sent:%d, confirmed:%d), bss_res_queue:%d",
                mtlk_vap_get_oid(nic->vap_handle),
                wait_cnt, dat_uncfm, bss_uncfm, bss_sent, bss_cfm, bss_res_queue);

    if (!(dat_uncfm | bss_uncfm | bss_res_queue)) break; /* all confirmed */

    if (wait_cnt == 0) {
      mtlk_hw_t *hw = mtlk_vap_get_hw(nic->vap_handle);

      ELOG_DDDDDD("CID-%04x: Unconfirmed data:%d, bss:%d (sent:%d, confirmed:%d), bss_res_queue:%d",
                mtlk_vap_get_oid(nic->vap_handle),
                dat_uncfm, bss_uncfm, bss_sent, bss_cfm, bss_res_queue);

      if (dat_uncfm) mtlk_mmb_print_tx_dat_ring_info(hw);
      if (bss_uncfm) mtlk_mmb_print_tx_bss_ring_info(hw);
      if (bss_res_queue) mtlk_mmb_print_tx_bss_res_queue(hw);

      res = MTLK_ERR_CANCELED;
      break;
    }

    mtlk_osal_msleep(SQ_WAIT_ALL_PACKETS_CFM_TIMEOUT_ONCE);
    wait_cnt--;
  }

  return res;
}

static int _mtlk_mbss_deactivate_vap(mtlk_core_t *running_core, mtlk_core_t *nic)
{
  int net_state = mtlk_core_get_net_state(nic);
  int res;

  ILOG2_DS("CID-%04x: net_state=%s", mtlk_vap_get_oid(nic->vap_handle), mtlk_net_state_to_string(nic->net_state));

  finish_and_prevent_fw_set_chan(running_core, TRUE); /* remove_vap is a "process", so need this. Not sure about stop_vap_traffic */

  if (net_state == NET_STATE_HALTED) {
    goto CORE_HALTED;
  }

  if (net_state == NET_STATE_CONNECTED) {
    if (nic->vap_in_fw_is_active) {
      res = _mtlk_core_send_stop_vap_traffic(nic);
      if (res != MTLK_ERR_OK) {
        goto FINISH;
      }
    } else {
      ILOG0_V("skip UM_MAN_STOP_VAP_TRAFFIC_REQ due vap is not active in fw");
      if ((res = mtlk_core_set_net_state(nic, NET_STATE_DEACTIVATING)) != MTLK_ERR_OK) {
        goto FINISH;
      }
    }
  }

  if (mtlk_wait_all_packets_confirmed(nic)) {
    WLOG_D("CID-%04x: wait time for all MSDUs confirmation expired", mtlk_vap_get_oid(nic->vap_handle));
    res = MTLK_ERR_FW;
    mtlk_hw_set_prop(mtlk_vap_get_hwapi(nic->vap_handle), MTLK_HW_RESET, NULL, 0);
    goto FINISH;
  }

CORE_HALTED:
  cleanup_on_disconnect(nic); /* Don't wait with this for complete removal */

  res = _mtlk_core_send_vap_remove(nic);

  mtlk_vap_set_stopped(nic->vap_handle);

FINISH:
  finish_and_prevent_fw_set_chan(running_core, FALSE);

  if (1 >= mtlk_vap_manager_get_active_vaps_number(mtlk_vap_get_manager(nic->vap_handle))) {
    struct mtlk_chan_def *current_chandef = __wave_core_chandef_get(nic);
    if (!mtlk_core_rcvry_is_running(nic))
      wv_cfg80211_on_core_down(mtlk_core_get_master(nic));

    current_chandef->center_freq1 = 0;
  }

  if (MTLK_ERR_OK != res) {
    ELOG_D("CID-%04x: Error during VAP deactivation", mtlk_vap_get_oid(nic->vap_handle));
    res = MTLK_ERR_FW;
/* FIXME Are we really supposed to halt the FW or just pretend we did?
    mtlk_core_set_net_state(nic, NET_STATE_HALTED);
*/
  }

  nic->vap_in_fw_is_active = FALSE;

  return res;
}

int mtlk_mbss_send_vap_db_del(struct nic *nic)
{
  mtlk_txmm_data_t        *man_entry = NULL;
  mtlk_txmm_msg_t         man_msg;
  int                     result = MTLK_ERR_OK;
  UMI_VAP_DB_OP           *pRemoveRequest;

  man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, mtlk_vap_get_txmm(nic->vap_handle), &result);
  if (man_entry == NULL) {
    ELOG_D("CID-%04x: Can't send VAP_DB DEL request to MAC due to the lack of MAN_MSG", mtlk_vap_get_oid(nic->vap_handle));
    goto FINISH;
  }

  man_entry->id           = UM_MAN_VAP_DB_REQ;
  man_entry->payload_size = sizeof (UMI_VAP_DB_OP);

  pRemoveRequest = (UMI_VAP_DB_OP *)(man_entry->payload);
  pRemoveRequest->u16Status = MTLK_ERR_OK;
  pRemoveRequest->u8OperationCode = VAP_OPERATION_DEL;
  pRemoveRequest->u8VAPIdx = mtlk_vap_get_id(nic->vap_handle);

  result = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);
  if (result != MTLK_ERR_OK) {
    ELOG_DD("CID-%04x: Can't send VAP_DB DEL request to MAC (err=%d)", mtlk_vap_get_oid(nic->vap_handle), result);
    goto FINISH;
  }

  if (pRemoveRequest->u16Status != MTLK_ERR_OK) {
    ELOG_DD("CID-%04x: Error returned for VAP DB DEL request to MAC (err=%d)", mtlk_vap_get_oid(nic->vap_handle), pRemoveRequest->u16Status);
    result = MTLK_ERR_UMI;
    goto FINISH;
  }

FINISH:
  if (man_entry) {
    mtlk_txmm_msg_cleanup(&man_msg);
  }

  return result;
}

/* FIXME this is a station only thing and has to be reworked with ADD_VAP, etc. */
static int
mtlk_send_activate (struct nic *nic)
{
  mtlk_txmm_data_t* man_entry = NULL;
  mtlk_txmm_msg_t activate_msg;
  int channel, bss_type;
  mtlk_pdb_size_t essid_len;
  UMI_ACTIVATE_HDR *areq;
  int result = MTLK_ERR_OK;
  wave_radio_t *radio = wave_vap_radio_get(nic->vap_handle);

  MTLK_ASSERT(!mtlk_vap_is_ap(nic->vap_handle));

  if (!mtlk_vap_is_ap(nic->vap_handle)) {
    bss_type = CFG_INFRA_STATION;
  } else {
    bss_type = CFG_ACCESS_POINT;
  }

  /* Start activation request */
  ILOG2_D("CID-%04x: Start activation", mtlk_vap_get_oid(nic->vap_handle));

  man_entry = mtlk_txmm_msg_init_with_empty_data(&activate_msg, mtlk_vap_get_txmm(nic->vap_handle), &result);
  if (man_entry == NULL) {
    ELOG_D("CID-%04x: Can't send ACTIVATE request to MAC due to the lack of MAN_MSG", mtlk_vap_get_oid(nic->vap_handle));
    return result;
  }

  man_entry->id           = UM_MAN_ACTIVATE_REQ;
  man_entry->payload_size = mtlk_get_umi_activate_size();

  areq = mtlk_get_umi_activate_hdr(man_entry->payload);
  memset(areq, 0, sizeof(UMI_ACTIVATE_HDR));

  channel = WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_CHANNEL_CUR);
  /* for AP channel 0 means "use AOCS", but for STA channel must be set
     implicitly - we cannot send 0 to MAC in activation request */
  if ((channel == 0) && !mtlk_vap_is_master_ap(nic->vap_handle)) {
    ELOG_D("CID-%04x: Channel must be specified for station or Virtual AP", mtlk_vap_get_oid(nic->vap_handle));
    result = MTLK_ERR_NOT_READY;
    goto FINISH;
  }

  essid_len = sizeof(areq->sSSID.acESSID);
  result = MTLK_CORE_PDB_GET_BINARY(nic, PARAM_DB_CORE_ESSID, areq->sSSID.acESSID, &essid_len);
  if (MTLK_ERR_OK != result) {
    ELOG_D("CID-%04x: ESSID parameter has wrong length", mtlk_vap_get_oid(nic->vap_handle));
    goto FINISH;
  }
  if (0 == essid_len) {
    ELOG_D("CID-%04x: ESSID is not set", mtlk_vap_get_oid(nic->vap_handle));
    result = MTLK_ERR_NOT_READY;
    goto FINISH;
  }

  /* Do not allow to activate if BSSID isn't set for the STA. Probably it
   * is worth to not allow this on AP as well? */
  if (!mtlk_vap_is_ap(nic->vap_handle) &&
     (0 == MTLK_CORE_HOT_PATH_PDB_CMP_MAC(nic, PARAM_DB_CORE_BSSID, mtlk_osal_eth_zero_addr))) {
    ELOG_D("CID-%04x: BSSID is not set", mtlk_vap_get_oid(nic->vap_handle));
    result = MTLK_ERR_NOT_READY;
    goto FINISH;
  }

  if (!mtlk_vap_is_ap(nic->vap_handle)) {
    core_cfg_sta_country_code_set_default_on_activate(nic);
  }

  wave_pdb_get_mac(mtlk_vap_get_param_db(nic->vap_handle), PARAM_DB_CORE_BSSID, areq->sBSSID.au8Addr);
  areq->sSSID.u8Length = essid_len;
  areq->u16RestrictedChannel = HOST_TO_MAC16(channel);
  areq->u16BSStype = HOST_TO_MAC16(bss_type);
  areq->isHiddenBssID = nic->slow_ctx->cfg.is_hidden_ssid;

  ILOG0_D("CID-%04x: Activation started with parameters:", mtlk_vap_get_oid(nic->vap_handle));
  mtlk_core_configuration_dump(nic);

  /* get data from Regulatory table:
     Availability Check Time,
     Scan Type
  */

  ILOG2_DD("CurrentSpectrumMode = %d\n"
           "RFSpectrumMode = %d",
           WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_SPECTRUM_MODE),
           WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_PROG_MODEL_SPECTRUM_MODE));

  /* TODO- add SmRequired to 11d table !! */
  /*
    ILOG1_SSDY("activating (mode:%s, essid:\"%s\", chan:%d, bssid %Y)...",
                bss_type_str, nic->slow_ctx->essid, channel, nic->slow_ctx->bssid);
  */

  /*********************** END NEW **********************************/

  if (mtlk_core_set_net_state(nic, NET_STATE_ACTIVATING) != MTLK_ERR_OK) {
    ELOG_D("CID-%04x: Failed to switch core to state ACTIVATING", mtlk_vap_get_oid(nic->vap_handle));
    result = MTLK_ERR_NOT_READY;
    goto FINISH;
  }

  nic->activation_status = FALSE;
  mtlk_osal_event_reset(&nic->slow_ctx->connect_event);

  mtlk_dump(3, areq, sizeof(UMI_ACTIVATE_HDR), "dump of UMI_ACTIVATE_HDR:");

  result = mtlk_txmm_msg_send_blocked(&activate_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);
  if (result != MTLK_ERR_OK) {
    ELOG_DD("CID-%04x: Cannot send activate request due to TXMM err#%d", mtlk_vap_get_oid(nic->vap_handle), result);
    goto FINISH;
  }

  if (areq->u16Status != UMI_OK && areq->u16Status != UMI_ALREADY_ENABLED) {
    WLOG_DD("CID-%04x: Activate VAP request failed with code %d", mtlk_vap_get_oid(nic->vap_handle), areq->u16Status);
    result = MTLK_ERR_UNKNOWN;
    goto FINISH;
  }

  /* now wait and handle connection event if any */
  ILOG4_V("Timestamp before network status wait...");

  result = mtlk_osal_event_wait(&nic->slow_ctx->connect_event, CONNECT_TIMEOUT);
  if (result == MTLK_ERR_TIMEOUT) {
    /* MAC is dead? Either fix MAC or increase timeout */
    ELOG_DS("CID-%04x: Timeout reached while waiting for %s event", mtlk_vap_get_oid(nic->vap_handle),
           mtlk_vap_is_ap(nic->vap_handle) ? "BSS creation" : "connection");
    (void)mtlk_hw_set_prop(mtlk_vap_get_hwapi(nic->vap_handle), MTLK_HW_RESET, NULL, 0);
    goto CLEANUP;
  } else if (nic->activation_status) {
    mtlk_core_set_net_state(nic, NET_STATE_CONNECTED);
    mtlk_vap_manager_notify_vap_activated(mtlk_vap_get_manager(nic->vap_handle));
    nic->is_stopped = FALSE;
    ILOG1_SDDDS("%s: Activated2: is_stopped=%u, is_stopping=%u, is_iface_stopping=%u, net_state=%s",
                mtlk_df_get_name(mtlk_vap_get_df(nic->vap_handle)),
                nic->is_stopped, nic->is_stopping, nic->is_iface_stopping,
                mtlk_net_state_to_string(mtlk_core_get_net_state(nic)));
  } else {
    ELOG_D("CID-%04x: Activate failed. Switch to NET_STATE_READY", mtlk_vap_get_oid(nic->vap_handle));
    mtlk_core_set_net_state(nic, NET_STATE_READY);
    goto CLEANUP;
  }

FINISH:
  if (result != MTLK_ERR_OK &&
      mtlk_core_get_net_state(nic) != NET_STATE_READY)
      mtlk_core_set_net_state(nic, NET_STATE_READY);

CLEANUP:
  mtlk_txmm_msg_cleanup(&activate_msg);

  return result;
}

/* FIXME this also seems to be a station only thing and has to be reworked */
static int
_mtlk_core_connect_sta (mtlk_handle_t hcore, const void* data, uint32 data_size)
{
  mtlk_core_t *nic = HANDLE_T_PTR(mtlk_core_t, hcore);
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;
  int res = MTLK_ERR_OK;
  bss_data_t bss_found;
  int freq;
  IEEE_ADDR *addr;
  uint32 addr_size;
  uint32 new_spectrum_mode;
  wave_radio_t *radio = wave_vap_radio_get(nic->vap_handle);

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  addr = mtlk_clpb_enum_get_next(clpb, &addr_size);
  MTLK_ASSERT(NULL != addr);
  MTLK_ASSERT(sizeof(*addr) == addr_size);
  if (!mtlk_clpb_check_data(addr, addr_size, sizeof(*addr), __FUNCTION__, __LINE__))
    return MTLK_ERR_UNKNOWN;

  if (mtlk_vap_is_ap(nic->vap_handle)) {
    res = MTLK_ERR_NOT_SUPPORTED;
    goto end_activation;
  }

  if (  (mtlk_core_get_net_state(nic) != NET_STATE_READY)
      || mtlk_core_scan_is_running(nic)
      || mtlk_core_is_stopping(nic)) {
    ILOG1_V("Can't connect to AP - unappropriated state");
    res = MTLK_ERR_NOT_READY;
    /* We shouldn't update current network mode in these cases */
    goto end;
  }

  if (mtlk_cache_find_bss_by_bssid(&nic->slow_ctx->cache, addr, &bss_found, NULL) == 0) {
    ILOG1_V("Can't connect to AP - unknown BSS");
    res = MTLK_ERR_PARAMS;
    goto end_activation;
  }

  /* store actual BSS data */
  nic->slow_ctx->bss_data = bss_found;

  /* update regulation limits for the BSS */
  if (core_cfg_get_dot11d(nic)) {
    core_cfg_sta_country_code_update_on_connect(nic, &bss_found.country_code);
  }

  WAVE_RADIO_PDB_SET_INT(radio, PARAM_DB_RADIO_CHANNEL_CUR, bss_found.channel);
  /* save ESSID */
  res = MTLK_CORE_PDB_SET_BINARY(nic, PARAM_DB_CORE_ESSID, bss_found.essid, strlen(bss_found.essid));
  if (MTLK_ERR_OK != res) {
    ELOG_DD("CID-%04x: Can't store ESSID (err=%d)", mtlk_vap_get_oid(nic->vap_handle), res);
    goto end_activation;
  }
  /* save BSSID so we can use it on activation */
  wave_pdb_set_mac(mtlk_vap_get_param_db(nic->vap_handle), PARAM_DB_CORE_BSSID, addr);
  /* set bonding according to the AP */
  WAVE_RADIO_PDB_SET_INT(radio, PARAM_DB_RADIO_BONDING_MODE, bss_found.upper_lower);

  /* set current frequency band */
  freq = channel_to_band(bss_found.channel);
  WAVE_RADIO_PDB_SET_INT(radio, PARAM_DB_RADIO_FREQ_BAND_CUR, freq);

  /* set current HT capabilities */
  if (BSS_IS_WEP_ENABLED(&bss_found) && nic->slow_ctx->wep_enabled) {
    /* no HT is allowed for WEP connections */
    MTLK_CORE_PDB_SET_INT(nic, PARAM_DB_CORE_IS_HT_CUR, FALSE);
  }
  else {
    MTLK_CORE_PDB_SET_INT(nic, PARAM_DB_CORE_IS_HT_CUR,
        (core_cfg_get_is_ht_cfg(nic) && bss_found.is_ht));
  }

  /* for STA spectrum mode should be set according to our own HT capabilities */
  if (core_cfg_get_is_ht_cur(nic) == 0) {
    /* e.g. if we connect to A/N AP, but STA is A then we should select 20MHz  */
    new_spectrum_mode = CW_20;
  } else {
    new_spectrum_mode = bss_found.spectrum;

    if (CW_40 == bss_found.spectrum) {
      uint32 sta_force_spectrum_mode =
          MTLK_CORE_PDB_GET_INT(nic, PARAM_DB_CORE_STA_FORCE_SPECTRUM_MODE);
      /* force set spectrum mode */
      if (CW_20 == sta_force_spectrum_mode) {
        new_spectrum_mode = CW_20;
      }
    }
  }
  WAVE_RADIO_PDB_SET_INT(radio, PARAM_DB_RADIO_SPECTRUM_MODE, new_spectrum_mode);
  ILOG1_DS("CID-%04x: Set SpectrumMode: %s MHz", mtlk_vap_get_oid(nic->vap_handle),
    mtlkcw2str(new_spectrum_mode));

  /* previously set network mode shouldn't be overridden,
   * but in case it "MTLK_HW_BAND_BOTH" it need to be recalculated, this value is not
   * acceptable for MAC! */
  if(MTLK_HW_BAND_BOTH == net_mode_to_band(core_cfg_get_network_mode_cur(nic))) {
     MTLK_CORE_PDB_SET_INT(nic, PARAM_DB_CORE_NET_MODE_CUR,
         get_net_mode(freq, core_cfg_get_is_ht_cur(nic)));
  }

  if (mtlk_send_activate(nic) != MTLK_ERR_OK) {
    res = MTLK_ERR_NOT_READY;
  }

end_activation:
  if (MTLK_ERR_OK != res) {
    /* rollback network mode */
    MTLK_CORE_PDB_SET_INT(nic, PARAM_DB_CORE_NET_MODE_CUR, core_cfg_get_network_mode_cfg(nic));
  }
end:
  return res;
}

static int _mtlk_sta_status_query(mtlk_handle_t hcore, const void* payload, uint32 size)
{
  int res = MTLK_ERR_OK;
  mtlk_core_t *core = HANDLE_T_PTR(mtlk_core_t, hcore);
  uint8 *mac_addr = (uint8 *) payload;
  sta_entry *sta = NULL;
  int status;

  MTLK_ASSERT(ETH_ALEN == size);

  if (NET_STATE_HALTED == mtlk_core_get_net_state(core)) { /* Do nothing if halted */
    return MTLK_ERR_OK;
  }

  sta = mtlk_stadb_find_sta(&core->slow_ctx->stadb, mac_addr);

  if (NULL == sta) {
    ILOG1_Y("STA not found during status query: %Y", mac_addr);
    return MTLK_ERR_NO_ENTRY;
  }

  res = poll_client_req(core->vap_handle, sta, &status);

  mtlk_sta_decref(sta); /* De-reference by STA DB hash */

  if (MTLK_ERR_OK != res)
  {
    ELOG_Y("Failed to poll STA %Y ", mac_addr);
  }

  return res;
}

static void __MTLK_IFUNC
_mtlk_core_on_sta_keepalive (mtlk_handle_t usr_data, sta_entry *sta)
{
  struct nic *nic = HANDLE_T_PTR(struct nic, usr_data);

  /* Skip scheduling because serializer is blocked during Recovery */
  if (!wave_rcvry_mac_fatal_pending_get((mtlk_vap_get_hw(nic->vap_handle))) &&
      !mtlk_core_rcvry_is_running(nic)) {
    _mtlk_process_hw_task(nic, SERIALIZABLE, _mtlk_sta_status_query, HANDLE_T(nic), &mtlk_sta_get_addr(sta)->au8Addr, ETH_ALEN);
  }
}

static int
_mtlk_core_get_ap_capabilities (mtlk_handle_t hcore,
                                const void* data, uint32 data_size)
{
  mtlk_core_t *nic = HANDLE_T_PTR(mtlk_core_t, hcore);
  mtlk_card_capabilities_t card_capabilities;
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;
  wave_radio_t *radio;

  MTLK_ASSERT(mtlk_vap_is_master_ap(nic->vap_handle));
  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  radio = wave_vap_radio_get(nic->vap_handle);

  MTLK_CFG_SET_ITEM(&card_capabilities, max_stas_supported, wave_radio_max_stas_get(radio));
  MTLK_CFG_SET_ITEM(&card_capabilities, max_vaps_supported, wave_radio_max_vaps_get(radio));

  return mtlk_clpb_push(clpb, &card_capabilities, sizeof(card_capabilities));
}

static int
_mtlk_core_cfg_init_mr_coex (mtlk_core_t *core)
{
  int res;
  uint8 enabled, mode;

  if (MTLK_ERR_OK != mtlk_core_is_band_supported(core, UMI_PHY_TYPE_BAND_2_4_GHZ))
    return MTLK_ERR_OK;

  res = mtlk_core_receive_coex_config(core, &enabled, &mode);

  if (MTLK_ERR_OK == res) {
    WAVE_RADIO_PDB_SET_INT(wave_vap_radio_get(core->vap_handle), PARAM_DB_RADIO_COEX_ENABLED, 0);
    WAVE_RADIO_PDB_SET_INT(wave_vap_radio_get(core->vap_handle), PARAM_DB_RADIO_COEX_MODE, 0);
  }

  return res;
}

static int
_mtlk_core_receive_tx_power_limit_offset (mtlk_core_t *core, uint8 *power_limit_offset)
{
  mtlk_txmm_msg_t         man_msg;
  mtlk_txmm_data_t        *man_entry;
  UMI_TX_POWER_LIMIT      *mac_msg;
  int                     res;

  man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, mtlk_vap_get_txmm(core->vap_handle), NULL);
  if (!man_entry) {
      ELOG_D("CID-%04x: Can not get TXMM slot", mtlk_vap_get_oid(core->vap_handle));
      return MTLK_ERR_NO_RESOURCES;
  }

  man_entry->id = UM_MAN_SET_POWER_LIMIT_REQ;
  man_entry->payload_size = sizeof(UMI_TX_POWER_LIMIT);
  mac_msg = (UMI_TX_POWER_LIMIT *)man_entry->payload;
  mac_msg->getSetOperation = API_GET_OPERATION;

  res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);

  if (MTLK_ERR_OK == res) {
    *power_limit_offset = mac_msg->powerLimitOffset;
  }
  else{
    ELOG_D("CID-%04x: Receive UM_MAN_SET_POWER_LIMIT_REQ failed", mtlk_vap_get_oid(core->vap_handle));
  }

  mtlk_txmm_msg_cleanup(&man_msg);
  return res;
}

static int
_mtlk_core_cfg_init_pw_lim_offset (mtlk_core_t *core)
{
  int res;
  uint8 power_limit_offset;

  res = _mtlk_core_receive_tx_power_limit_offset(core, &power_limit_offset);

  if (MTLK_ERR_OK == res)
    WAVE_RADIO_PDB_SET_INT(wave_vap_radio_get(core->vap_handle),
                           PARAM_DB_RADIO_TX_POWER_LIMIT_OFFSET, DBM_TO_POWER(power_limit_offset));

  return res;
}

static int
_mtlk_core_cfg_init_mcast (mtlk_core_t *core)
{
  int res;
  uint8 mcast_flag;

  res = mtlk_core_receive_reliable_multicast(core, &mcast_flag);

  if (MTLK_ERR_OK == res)
    MTLK_CORE_PDB_SET_INT(core, PARAM_DB_CORE_RELIABLE_MCAST, mcast_flag);

  return res;
}

int mtlk_core_init_defaults (mtlk_core_t *core)
{
  int res;

  _mtlk_core_cfg_init_mcast(core);

  if (mtlk_vap_is_master(core->vap_handle)) {
    mtlk_core_cfg_init_cca_adapt(core);
    mtlk_core_cfg_init_ire_ctrl(core);
    _mtlk_core_cfg_init_mr_coex(core);
    _mtlk_core_cfg_init_pw_lim_offset(core);
  }

  res = mtlk_core_send_iwpriv_config_to_fw(core);

  return res;
}

static void
_mtlk_core_reset_pstat_traffic_cntrs(mtlk_core_t *core)
{
    core->pstats.tx_bytes   = 0;
    core->pstats.rx_bytes   = 0;
    core->pstats.tx_packets = 0;
    core->pstats.rx_packets = 0;
    core->pstats.rx_multicast_packets = 0;
}

static void
_mtlk_core_poll_stat_stop(mtlk_core_t *core)
{
    mtlk_wss_poll_stat_stop(&core->poll_stat);
}

static void
_mtlk_core_poll_stat_init(mtlk_core_t *core)
{
    memset(&core->poll_stat_last, 0, sizeof(core->poll_stat_last));

    mtlk_wss_poll_stat_init(&core->poll_stat, MTLK_CORE_CNT_POLL_STAT_NUM,
                             &core->wss_hcntrs[MTLK_CORE_CNT_POLL_STAT_FIRST],
                             &core->poll_stat_last[0]);
}

static void
_mtlk_core_poll_stat_start(mtlk_core_t *core)
{
    _mtlk_core_reset_pstat_traffic_cntrs(core);
    mtlk_wss_poll_stat_start(&core->poll_stat);
}

static BOOL
_mtlk_core_poll_stat_is_started(mtlk_core_t *core)
{
    return mtlk_wss_poll_stat_is_started(&core->poll_stat);
}

static void
_mtlk_core_poll_stat_update (mtlk_core_t *core, uint32 *values, uint32 values_num)
{
    mtlk_poll_stat_update_cntrs(&core->poll_stat, values, values_num);
}

static int
_mtlk_core_activate (mtlk_handle_t hcore, const void* data, uint32 data_size)
{
  mtlk_core_t *nic = HANDLE_T_PTR(mtlk_core_t, hcore);
  mtlk_core_t *mcore = mtlk_vap_manager_get_master_core(mtlk_vap_get_manager(nic->vap_handle));
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;
  mtlk_pdb_t *param_db_core = mtlk_vap_get_param_db(nic->vap_handle);
  wave_radio_t *radio = wave_vap_radio_get(nic->vap_handle);
  struct mtlk_ap_settings *aps;
  unsigned aps_size;
  int res;
  sta_db_cfg_t sta_db_cfg;

  MTLK_ASSERT(sizeof(mtlk_clpb_t *) == data_size);

  aps = mtlk_clpb_enum_get_next(clpb, &aps_size);

  MTLK_CLPB_TRY(aps, aps_size)
  {
    if (mtlk_vap_is_ap(nic->vap_handle)) {
      /* save these away for later restores, etc. */
      MTLK_CORE_PDB_SET_BINARY(nic, PARAM_DB_CORE_ESSID, aps->essid, aps->essid_len);
      wave_pdb_set_int(param_db_core, PARAM_DB_CORE_HIDDEN_SSID, aps->hidden_ssid);
      MTLK_CORE_PDB_SET_INT(nic, PARAM_DB_CORE_DTIM_PERIOD, aps->dtim_period);
      /* This is "per-interface", not per-VAP in hostapd, but we stick it into this VAP's DB anyway for simplicity */
      WAVE_RADIO_PDB_SET_INT(radio, PARAM_DB_RADIO_BEACON_PERIOD, aps->beacon_interval);
    }

    /* We used to check that the VAP is not up here but now it might be due to scan or
     * initial channels switch. Yet it may not have the BSS, so we need to keep working on it */

    ILOG0_D("CID-%04x: open interface", mtlk_vap_get_oid(nic->vap_handle));

    if (MTLK_ERR_OK != mtlk_eeprom_is_valid(mtlk_core_get_eeprom(nic))) {
      WLOG_D("CID-%04x: Interface cannot UP after EEPROM failure", mtlk_vap_get_oid(nic->vap_handle));
        MTLK_CLPB_EXIT(MTLK_ERR_NOT_READY);
    }

    if (mtlk_vap_is_dut(nic->vap_handle)) {
      WLOG_D("CID-%04x: Interface cannot UP in DUT mode", mtlk_vap_get_oid(nic->vap_handle));
        MTLK_CLPB_EXIT(MTLK_ERR_NOT_SUPPORTED);
    }

    if (!(mtlk_core_get_net_state(nic) & (NET_STATE_READY | NET_STATE_ACTIVATING))
        || mtlk_core_scan_is_running(nic)
        || mtlk_core_is_stopping(nic)) {
      ELOG_D("CID-%04x: Failed to open - inappropriate state", mtlk_vap_get_oid(nic->vap_handle));
        MTLK_CLPB_EXIT(MTLK_ERR_NOT_READY);
    }

    _mtlk_core_poll_stat_start(nic);

      res = mtlk_mbss_send_vap_activate(nic, __wave_core_chandef_get(mcore)->chan.band);

    if (mtlk_vap_is_ap(nic->vap_handle)) {
      if (BR_MODE_WDS == MTLK_CORE_HOT_PATH_PDB_GET_INT(nic, CORE_DB_CORE_BRIDGE_MODE)){
        wds_on_if_up(&nic->slow_ctx->wds_mng);
      }
    }
    if (MTLK_ERR_OK != res)
    {
      ELOG_D("CID-%04x: Failed to activate the core", mtlk_vap_get_oid(nic->vap_handle));
        MTLK_CLPB_EXIT(res);
    }

    /* CoC configuration */
    if (1 >= mtlk_vap_manager_get_active_vaps_number(mtlk_vap_get_manager(nic->vap_handle))) {
      mtlk_coc_t *coc_mgmt = __wave_core_coc_mgmt_get(nic);
      mtlk_coc_reset_antenna_params(coc_mgmt);
      (void)mtlk_coc_set_power_mode(coc_mgmt, mtlk_coc_get_auto_mode_cfg(coc_mgmt));
    }

    /* interface is up - start timers */
    sta_db_cfg.api.usr_data          = HANDLE_T(nic);
    sta_db_cfg.api.on_sta_keepalive  = _mtlk_core_on_sta_keepalive;

    sta_db_cfg.max_nof_stas = wave_radio_max_stas_get(radio);
    sta_db_cfg.parent_wss   = nic->wss;

    mtlk_stadb_configure(&nic->slow_ctx->stadb, &sta_db_cfg);
    mtlk_stadb_start(&nic->slow_ctx->stadb);
  }
  MTLK_CLPB_FINALLY(res) {
    return mtlk_clpb_push_res(clpb, res);
  }
  MTLK_CLPB_END;
}

static int
__mtlk_core_deactivate(mtlk_core_t *running_core, mtlk_core_t *nic)
{
  mtlk_hw_t *hw;
  BOOL is_scan_rcvry;
  wave_rcvry_type_e rcvry_type;
  mtlk_scan_support_t *ss;
  int net_state = mtlk_core_get_net_state(nic);
  mtlk_vap_manager_t *vap_manager = mtlk_vap_get_manager(nic->vap_handle);
  int res = MTLK_ERR_OK;
  int deactivate_res  = MTLK_ERR_OK;

  ILOG1_SDDDS("%s: Deactivating: is_stopped=%u, is_iface_stopping=%u, is_slave_ap=%u, net_state=%s",
              mtlk_df_get_name(mtlk_vap_get_df(nic->vap_handle)), nic->is_stopped, nic->is_iface_stopping,
              mtlk_vap_is_slave_ap(nic->vap_handle), mtlk_net_state_to_string(net_state));

  if (nic->is_stopped) {
    /* Interface has already been stopped */
    res = MTLK_ERR_OK;
    goto FINISH;
  }

  MTLK_ASSERT(0 != mtlk_vap_manager_get_active_vaps_number(mtlk_vap_get_manager(nic->vap_handle)));

  nic->chan_state = ST_LAST;

  hw = mtlk_vap_get_hw(nic->vap_handle);
  rcvry_type = wave_rcvry_type_current_get(hw);
  is_scan_rcvry = (RCVRY_NO_SCAN != wave_rcvry_scan_type_current_get(hw, vap_manager));

  mtlk_osal_lock_acquire(&nic->net_state_lock);
  nic->is_iface_stopping = TRUE;
  mtlk_osal_lock_release(&nic->net_state_lock);

  _mtlk_core_poll_stat_stop(nic);

  ss = __wave_core_scan_support_get(running_core);

  if (mtlk_vap_is_master(nic->vap_handle))
  {
    mtlk_df_t *df = mtlk_vap_get_df(nic->vap_handle);
    mtlk_df_user_t *df_user = mtlk_df_get_user(df);
    struct mtlk_chan_def *ccd = __wave_core_chandef_get(nic);

    ccd->sb_dfs.sb_dfs_bw = MTLK_SB_DFS_BW_NORMAL;

    mtlk_core_cfg_set_block_tx(nic, 0);

    if (!mtlk_osal_timer_is_stopped(&nic->slow_ctx->cac_timer))
    {
      mtlk_osal_timer_cancel_sync(&nic->slow_ctx->cac_timer);

      if ((RCVRY_TYPE_FAST == rcvry_type) || (RCVRY_TYPE_FULL == rcvry_type)) {
        ILOG1_D("CID-%04x: Cancel CAC-timer due to Recovery w/o Kernel and hostapd notification",
                mtlk_vap_get_oid(nic->vap_handle));
        wave_rcvry_restart_cac_set(hw, vap_manager, TRUE);
      }
      else {
        struct net_device *ndev = mtlk_df_user_get_ndev(df_user);
        ILOG1_D("CID-%04x: Aborting the CAC", mtlk_vap_get_oid(nic->vap_handle));
        wv_cfg80211_cac_event(ndev, ccd, 0); /* 0 means it didn't finish, i.e., was aborted */
      }
    }

    if (ss->dfs_debug_params.debug_chan)
      ss->dfs_debug_params.switch_in_progress = FALSE;

    if (is_scan_rcvry) {
      pause_or_prevent_scan(nic);
      if (ss->set_chan_man_msg.data) {
        mtlk_txmm_msg_cleanup(&ss->set_chan_man_msg);
        ss->set_chan_man_msg.data = NULL;
      }
    }
  }

  if (!is_scan_rcvry) {
    mtlk_df_user_t *df_user   = mtlk_df_get_user(mtlk_vap_get_df(running_core->vap_handle));

    if(ss->req.saved_request != NULL && ((struct cfg80211_scan_request *)ss->req.saved_request)->wdev == mtlk_df_user_get_wdev(df_user))
      abort_scan_sync(mtlk_core_get_master(running_core)); /* this can be done many times, so no other checks */
  }

  /* Force disconnect of all WDS peers */
  if (mtlk_vap_is_ap(nic->vap_handle) &&
      BR_MODE_WDS == MTLK_CORE_HOT_PATH_PDB_GET_INT(nic, CORE_DB_CORE_BRIDGE_MODE)){
    wds_on_if_down(&nic->slow_ctx->wds_mng);
  }

  if (!can_disconnect_now(nic, is_scan_rcvry)) {
     res = MTLK_ERR_RETRY;
     goto FINISH;
  }

  /* for all of these states we can at least remove the VAP. For connected will also do stop_vap_traffic before that */
  if ((net_state & (NET_STATE_CONNECTED | NET_STATE_ACTIVATING | NET_STATE_DEACTIVATING))
      || (net_state == NET_STATE_HALTED)) /* for cleanup after exception */
  {
    deactivate_res = _mtlk_mbss_deactivate_vap(running_core, nic); /* this will set the net_state */

    /* For Fast and Full Recovery we will need to restore WEP or GTK keys.
       For Complete Recovery - hostapd will restore all keys */
    if ((RCVRY_TYPE_FAST != rcvry_type) && (RCVRY_TYPE_FULL != rcvry_type)) {
      ILOG1_D("CID-%04x: Reset security", mtlk_vap_get_oid(nic->vap_handle));
      reset_security_stuff(nic);
    } else {
      ILOG1_D("CID-%04x: Skip reset security", mtlk_vap_get_oid(nic->vap_handle));
    }
  }

  MTLK_ASSERT(0 == mtlk_stadb_get_four_addr_sta_cnt(&nic->slow_ctx->stadb));

  mtlk_stadb_stop(&nic->slow_ctx->stadb);
  /* Clearing cache */
  mtlk_cache_clear(&nic->slow_ctx->cache);

  mtlk_qos_dscp_table_init(nic->dscp_table);

  net_state = mtlk_core_get_net_state(nic); /* see what the new net_state is */

  mtlk_osal_lock_acquire(&nic->net_state_lock);
  nic->is_iface_stopping = FALSE;
  /* FIXME is_stopped should always be deduced from the net_state flags, a separate field is a nuisance */
  nic->is_stopped = !(net_state & (NET_STATE_CONNECTED | NET_STATE_ACTIVATING | NET_STATE_DEACTIVATING));
  mtlk_osal_lock_release(&nic->net_state_lock);

  if (nic->is_stopped)
  {
    mtlk_vap_manager_notify_vap_deactivated(vap_manager);
    ILOG1_D("CID-%04x: interface is stopped", mtlk_vap_get_oid(nic->vap_handle));
  }

  ILOG1_SDDDS("%s: Deactivated: is_stopped=%u, is_stopping=%u, is_iface_stopping=%u, net_state=%s",
              mtlk_df_get_name(mtlk_vap_get_df(nic->vap_handle)),
              nic->is_stopped, nic->is_stopping, nic->is_iface_stopping,
              mtlk_net_state_to_string(mtlk_core_get_net_state(nic)));

  if ((0 == mtlk_vap_manager_get_active_vaps_number(vap_manager)))
  {
    mtlk_coc_t *coc_mgmt = __wave_core_coc_mgmt_get(nic);
    mtlk_coc_auto_mode_disable(coc_mgmt);
  }

  if (mtlk_vap_is_master (nic->vap_handle)) { // re-enable in case we disabled during channel switch
    wave_radio_abilities_enable_vap_ops(nic->vap_handle);
  }

  if (0 == mtlk_vap_manager_get_active_vaps_number(vap_manager)) {
    __wave_core_chan_switch_type_set(nic, ST_NONE);
  }

FINISH:
  /*
    If deactivate_res indicates an error - we must make sure
    that the close function will not reiterate. Therefore, we return
    specific error code in this case.
  */
  if (MTLK_ERR_OK != deactivate_res)
    res = deactivate_res;
  return res;
}

int core_recovery_deactivate(mtlk_core_t *master_core, mtlk_core_t *nic)
{
  return __mtlk_core_deactivate(master_core, nic);
}

static int
_mtlk_core_deactivate (mtlk_handle_t hcore, const void* data, uint32 data_size)
{
  int res = MTLK_ERR_OK;
  mtlk_core_t *core = HANDLE_T_PTR(mtlk_core_t, hcore);
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  res = __mtlk_core_deactivate(core, core);

  return mtlk_clpb_push(clpb, &res, sizeof(res));
}

/* Set peer AP beacon interval (relevant only for station role interface)
 * Should be called before UMI ADD STA for the peer AP */
static int
_mtlk_beacon_man_set_beacon_interval_by_params(mtlk_core_t *core, mtlk_beacon_interval_t *mtlk_beacon_interval)
{
  mtlk_txmm_msg_t           man_msg;
  mtlk_txmm_data_t          *man_entry = NULL;
  UMI_BEACON_INTERVAL_t     *pBeaconInterval = NULL;
  mtlk_vap_handle_t         vap_handle = core->vap_handle;
  int res;

  MTLK_ASSERT(vap_handle);

  man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, mtlk_vap_get_txmm(vap_handle), NULL);
  if (NULL == man_entry) {
    ELOG_D("CID-%04x: No man entry available", mtlk_vap_get_oid(vap_handle));
    return MTLK_ERR_NO_RESOURCES;
  }

  man_entry->id = UM_MAN_SET_BEACON_INTERVAL_REQ;
  man_entry->payload_size = sizeof(UMI_BEACON_INTERVAL_t);

  pBeaconInterval = (UMI_BEACON_INTERVAL_t *)(man_entry->payload);
  pBeaconInterval->beaconInterval = HOST_TO_MAC32(mtlk_beacon_interval->beacon_interval);
  pBeaconInterval->vapID = HOST_TO_MAC32(mtlk_beacon_interval->vap_id);

  res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);
  if (MTLK_ERR_OK != res) {
    ELOG_DD("CID-%04x: beacon interval set failure (%i)", mtlk_vap_get_oid(vap_handle), res);
  }

  mtlk_txmm_msg_cleanup(&man_msg);
  return res;
}

static int
_mtlk_beacon_man_set_beacon_interval (mtlk_handle_t hcore, const void* data, uint32 data_size)
{
  mtlk_core_t *core = HANDLE_T_PTR(mtlk_core_t, hcore);
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;
  mtlk_beacon_interval_t *mtlk_beacon_interval;
  uint32 res = MTLK_ERR_UNKNOWN;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);
  mtlk_beacon_interval = mtlk_clpb_enum_get_next(clpb, NULL);
  MTLK_ASSERT(NULL != mtlk_beacon_interval);

  if (mtlk_beacon_interval) {
    res = _mtlk_beacon_man_set_beacon_interval_by_params(core, mtlk_beacon_interval);
  }

  return mtlk_clpb_push_res(clpb, res);
}

static int
_mtlk_core_mgmt_tx (mtlk_handle_t hcore, const void* data, uint32 data_size)
{
  uint32 res = MTLK_ERR_OK;
  mtlk_core_t *core = HANDLE_T_PTR(mtlk_core_t, hcore);
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;
  struct mgmt_tx_params *mtp;
  uint32 mtp_size;
  sta_entry *sta = NULL;
  const unsigned char *dst_addr;
  unsigned frame_ctrl, subtype;
  size_t frame_min_len = sizeof(frame_head_t);

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);
  mtp = mtlk_clpb_enum_get_next(clpb, &mtp_size);
  MTLK_CLPB_TRY(mtp, mtp_size)
  {
    if (PROCESS_EAPOL == mtp->extra_processing)
      frame_min_len = sizeof(struct ethhdr);

    if (mtp->len < frame_min_len) {
      ELOG_DDD("CID-%04x: Management Frame length %u is less than min %u",
        mtlk_vap_get_oid(core->vap_handle), mtp->len, frame_min_len);
      MTLK_CLPB_EXIT(MTLK_ERR_NOT_IN_USE);
    }

    dst_addr   = WLAN_GET_ADDR1(mtp->buf);
    frame_ctrl = mtlk_wlan_pkt_get_frame_ctl((uint8 *)mtp->buf);
    subtype    = (frame_ctrl & FRAME_SUBTYPE_MASK) >> FRAME_SUBTYPE_SHIFT;

    if (PROCESS_EAPOL == mtp->extra_processing) {
      struct ethhdr *ether_header = (struct ethhdr *) mtp->buf;
      sta = mtlk_stadb_find_sta(&core->slow_ctx->stadb, ether_header->h_dest);
      if (!sta) {
          MTLK_CLPB_EXIT(MTLK_ERR_NOT_HANDLED);
      }
      if (MTLK_PCKT_FLTR_DISCARD_ALL == mtlk_sta_get_packets_filter(sta)) {
        mtlk_sta_on_packet_dropped(sta,  MTLK_TX_DISCARDED_DROP_ALL_FILTER);
          MTLK_CLPB_EXIT(MTLK_ERR_NOT_HANDLED);
      }
    } else if (PROCESS_NULL_DATA_PACKET == mtp->extra_processing) {
      uint16 sid = mtlk_core_get_sid_by_addr(core, (char *)dst_addr);
      if (DB_UNKNOWN_SID == sid) {
        ELOG_Y("Unknown SID when sending null data packet, STA address %Y", dst_addr);
          MTLK_CLPB_EXIT(MTLK_ERR_NOT_HANDLED);
      }
    } else if (PROCESS_MANAGEMENT == mtp->extra_processing) {
      ILOG2_DDY("CID-%04x: mgmt subtype %d, peer %Y", mtlk_vap_get_oid(core->vap_handle), subtype, dst_addr);
      if (MAN_TYPE_PROBE_RES == subtype) { /* Filtering the Probe Responses */
        if (_mtlk_core_ucast_probe_resp_entry_exists(core, (IEEE_ADDR *)dst_addr)) {
          ILOG2_DY("CID-%04x: Don't send Probe Response to %Y", mtlk_vap_get_oid(core->vap_handle), dst_addr);
          MTLK_CLPB_EXIT(MTLK_ERR_OK);
        } else {
          /*
           * We are in serializer context. It is important to add entry to the list
           * prior to frame transmission is executed, as CFM may come nearly immediately
           * after HD is copied to the ring. The entry is removed from list in the
           * tasklet context, that might create a racing on entry removal.
           */
          _mtlk_core_add_ucast_probe_resp_entry(core, (IEEE_ADDR *)dst_addr);
        }
      }
    }

    res = mtlk_mmb_bss_mgmt_tx(core->vap_handle, mtp->buf, mtp->len, mtp->channum,
                                 mtp->no_cck, mtp->dont_wait_for_ack, FALSE, /* unicast */
                               mtp->cookie, mtp->extra_processing, mtp->skb,
                               FALSE, NTS_TID_USE_DEFAULT);

    if (res != MTLK_ERR_OK) {
      /* delete entry if TX failed */
      if ((PROCESS_MANAGEMENT == mtp->extra_processing) && (MAN_TYPE_PROBE_RES == subtype)) {
        _mtlk_core_del_ucast_probe_resp_entry(core, (IEEE_ADDR *)dst_addr);
      }
      ILOG1_DSDDS("CID-%04x: Send %s frame error: type=%d, res=%d (%s)",
               mtlk_vap_get_oid(core->vap_handle),
               (mtp->extra_processing == PROCESS_MANAGEMENT) ? "management" : (mtp->extra_processing == PROCESS_EAPOL) ? "EAPOL" : (mtp->extra_processing == PROCESS_NULL_DATA_PACKET) ? "null data" : "unknown",
               mtp->extra_processing, res, mtlk_get_error_text(res));
      if (sta)
        if (PROCESS_EAPOL == mtp->extra_processing)
          mtlk_sta_on_tx_packet_discarded_802_1x(sta);  /* Count 802_1x discarded TX packets */
    } else {
      if (sta)
        if (PROCESS_EAPOL == mtp->extra_processing)
          mtlk_sta_on_tx_packet_802_1x(sta);            /* Count 802_1x TX packets */
    }
  }
  MTLK_CLPB_FINALLY(res) {
    if (sta) mtlk_sta_decref(sta);                           /* De-reference of find */
    return mtlk_clpb_push_res(clpb, res);
  }
  MTLK_CLPB_END;
}

static int
_mtlk_core_mgmt_rx (mtlk_handle_t hcore, const void* data, uint32 data_size)
{
  uint32 res = MTLK_ERR_OK;
  mtlk_core_t *core = HANDLE_T_PTR(mtlk_core_t, hcore);
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;
  mtlk_core_handle_rx_bss_t *rx_bss;
  uint32 rx_bss_size;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  rx_bss = mtlk_clpb_enum_get_next(clpb, &rx_bss_size);
  MTLK_CLPB_TRY(rx_bss, rx_bss_size)
    res = _mtlk_core_handle_rx_bss(core->vap_handle, rx_bss);
  MTLK_CLPB_FINALLY(res)
    return mtlk_clpb_push_res(clpb, res);
  MTLK_CLPB_END;
}

int mtlk_handle_eapol(mtlk_vap_handle_t vap_handle, void *data, int data_len)
{
  mtlk_df_t *df               = mtlk_vap_get_df(vap_handle);
  mtlk_df_user_t *df_user     = mtlk_df_get_user(df);
  struct wireless_dev *wdev;
  struct ethhdr *ether_header = (struct ethhdr *)data;
  mtlk_core_t *nic            = mtlk_vap_get_core(vap_handle);
  sta_entry *sta              = mtlk_stadb_find_sta(&nic->slow_ctx->stadb, ether_header->h_source);
  mtlk_nbuf_t *evt_nbuf;
  uint8 *cp;

  if (mtlk_vap_is_ap(vap_handle))
    wdev = mtlk_df_user_get_wdev(df_user);
  else
    wdev = (struct wireless_dev *)mtlk_df_user_get_ndev(df_user)->ieee80211_ptr;

  MTLK_ASSERT(NULL != wdev);

  CAPWAP1(mtlk_hw_mmb_get_card_idx(mtlk_vap_get_hw(vap_handle)), data, data_len, 0, 0);

  if (sta)
  {
    /* If WDS WPS station sent EAPOL not to us, discard. */
    if (MTLK_BFIELD_GET(sta->info.flags, STA_FLAGS_WDS_WPA)) {
      if (MTLK_CORE_HOT_PATH_PDB_CMP_MAC(nic, CORE_DB_CORE_MAC_ADDR, ether_header->h_dest)) {
        mtlk_sta_decref(sta);               /* De-reference of find */
        return MTLK_ERR_OK;
      }
    }
    mtlk_sta_on_rx_packet_802_1x(sta);  /* Count 802_1x RX packets */
    mtlk_sta_decref(sta);               /* De-reference of find */
  }

  evt_nbuf = wv_cfg80211_vendor_event_alloc(wdev, data_len,
                                            LTQ_NL80211_VENDOR_EVENT_RX_EAPOL);
  if (!evt_nbuf)
  {
    return MTLK_ERR_NO_MEM;
  }

  cp = mtlk_df_nbuf_put(evt_nbuf, data_len);
  wave_memcpy(cp, data_len, data, data_len);

  ILOG3_D("CID-%04x: EAPOL RX", mtlk_vap_get_oid(vap_handle));
  mtlk_dump(3, evt_nbuf->data, evt_nbuf->len, "EAPOL RX vendor frame");
  wv_cfg80211_vendor_event(evt_nbuf);

  return MTLK_ERR_OK;

}

static int
_mtlk_core_set_mac_addr_wrapper (mtlk_handle_t hcore,
                                 const void* data, uint32 data_size)
{
  mtlk_core_t *nic = HANDLE_T_PTR(mtlk_core_t, hcore);
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;
  const char* mac_addr;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);
  mac_addr = mtlk_clpb_enum_get_next(clpb, NULL);
  MTLK_ASSERT(NULL != mac_addr);
  if (!mac_addr) return MTLK_ERR_UNKNOWN;

  return core_cfg_set_mac_addr(nic, mac_addr);
}

static int
_mtlk_core_get_mac_addr (mtlk_handle_t hcore,
                         const void* data, uint32 data_size)
{
  mtlk_core_t *core = HANDLE_T_PTR(mtlk_core_t, hcore);
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;
  IEEE_ADDR mac_addr;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  MTLK_CORE_PDB_GET_MAC(core, PARAM_DB_CORE_MAC_ADDR, &mac_addr);
  return mtlk_clpb_push(clpb, &mac_addr, sizeof(mac_addr));
}

static void
_mtlk_core_reset_stats_internal (mtlk_core_t *core)
{
  if (mtlk_vap_is_ap(core->vap_handle)) {
    mtlk_stadb_reset_cnts(&core->slow_ctx->stadb);
  }

  memset(&core->pstats, 0, sizeof(core->pstats));
}

static int
_mtlk_core_reset_stats (mtlk_handle_t hcore,
                        const void* data, uint32 data_size)
{
  int res = MTLK_ERR_OK;

  unsigned uint32_size;
  uint32 *reset_radar_cnt;
  mtlk_core_t *nic = HANDLE_T_PTR(mtlk_core_t, hcore);
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;
  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  reset_radar_cnt = mtlk_clpb_enum_get_next(clpb, &uint32_size);
  MTLK_CLPB_TRY(reset_radar_cnt, uint32_size)
  {
    if (*reset_radar_cnt) {
      mtlk_hw_reset_radar_cntr(mtlk_vap_get_hw(nic->vap_handle));
        MTLK_CLPB_EXIT(res);
    }

    if (mtlk_core_get_net_state(nic) != NET_STATE_HALTED)
    {
      ELOG_D("CID-%04x: Can not reset stats when core is active", mtlk_vap_get_oid(nic->vap_handle));
      res = MTLK_ERR_NOT_READY;
    }
    else
    {
      _mtlk_core_reset_stats_internal(nic);
    }
  }
  MTLK_CLPB_FINALLY(res)
    return mtlk_clpb_push_res(clpb, res);
  MTLK_CLPB_END;
}

static int
_mtlk_core_get_aocs_cfg (mtlk_handle_t hcore,
                         const void* data, uint32 data_size)
{
  uint32 res = MTLK_ERR_OK;

  ILOG3_V("Faking");

  return res;
}

static int
_mtlk_core_set_aocs_cfg (mtlk_handle_t hcore,
                         const void* data, uint32 data_size)
{
  uint32 res = MTLK_ERR_OK;
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  ILOG3_V("Faking");

  return mtlk_clpb_push(clpb, &res, sizeof(res));
}

int mtlk_get_dbg_channel_availability_check_time(mtlk_scan_support_t* obj)
{
  MTLK_ASSERT(NULL != obj);

  return obj->dfs_debug_params.cac_time;
}

int mtlk_get_dbg_channel_switch_count(mtlk_scan_support_t* obj)
{
  MTLK_ASSERT(NULL != obj);

  return obj->dfs_debug_params.beacon_count;
}

int mtlk_get_dbg_nop(mtlk_scan_support_t* obj)
{
  MTLK_ASSERT(NULL != obj);

  return obj->dfs_debug_params.nop;
}

static int
_mtlk_core_get_dot11h_ap_cfg (mtlk_handle_t hcore,
                              const void* data, uint32 data_size)
{
  uint32 res = MTLK_ERR_OK;
  mtlk_11h_ap_cfg_t dot11h_cfg;
  mtlk_core_t *core = (mtlk_core_t*)hcore;
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);
  MTLK_ASSERT(!mtlk_vap_is_slave_ap(core->vap_handle));

  memset(&dot11h_cfg, 0, sizeof(dot11h_cfg));

  MTLK_CFG_SET_ITEM(&dot11h_cfg, debugChannelSwitchCount,
                     mtlk_get_dbg_channel_switch_count(mtlk_core_get_scan_support(core)));

  MTLK_CFG_SET_ITEM(&dot11h_cfg, debugChannelAvailabilityCheckTime,
                     mtlk_get_dbg_channel_availability_check_time(mtlk_core_get_scan_support(core)));

  MTLK_CFG_SET_ITEM(&dot11h_cfg, debugNOP,
                     mtlk_get_dbg_nop(mtlk_core_get_scan_support(core)));

  res = mtlk_clpb_push(clpb, &res, sizeof(res));
  if (MTLK_ERR_OK == res) {
    res = mtlk_clpb_push(clpb, &dot11h_cfg, sizeof(dot11h_cfg));
  }

  return res;
}

void mtlk_set_dbg_channel_availability_check_time(mtlk_scan_support_t* obj, int channel_availability_check_time)
{
  /* wait for Radar Detection end */
  wave_radio_radar_detect_end_wait(wave_vap_radio_get(obj->master_core->vap_handle));

  obj->dfs_debug_params.cac_time = channel_availability_check_time;
}

void mtlk_set_dbg_channel_switch_count(mtlk_scan_support_t *obj, int channel_switch_count)
{
  obj->dfs_debug_params.beacon_count = channel_switch_count;
}

void mtlk_set_dbg_nop(mtlk_scan_support_t *obj, uint32 nop)
{
  obj->dfs_debug_params.nop = nop;
}

static int
_mtlk_core_set_dot11h_ap_cfg (mtlk_handle_t hcore,
                              const void* data, uint32 data_size)
{
  uint32 res = MTLK_ERR_OK;
  mtlk_core_t *core = (mtlk_core_t*)hcore;
  mtlk_11h_ap_cfg_t *dot11h_cfg = NULL;
  uint32 dot11h_cfg_size;
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);
  MTLK_ASSERT(!mtlk_vap_is_slave_ap(core->vap_handle));

  dot11h_cfg = mtlk_clpb_enum_get_next(clpb, &dot11h_cfg_size);
  MTLK_CLPB_TRY(dot11h_cfg, dot11h_cfg_size)
  {
    MTLK_CFG_START_CHEK_ITEM_AND_CALL()

      MTLK_CFG_CHECK_ITEM_AND_CALL_VOID(dot11h_cfg, debugChannelSwitchCount, mtlk_set_dbg_channel_switch_count,
                                        (mtlk_core_get_scan_support(core), dot11h_cfg->debugChannelSwitchCount));

      MTLK_CFG_CHECK_ITEM_AND_CALL_VOID(dot11h_cfg, debugChannelAvailabilityCheckTime, mtlk_set_dbg_channel_availability_check_time,
                                        (mtlk_core_get_scan_support(core), dot11h_cfg->debugChannelAvailabilityCheckTime));

      MTLK_CFG_CHECK_ITEM_AND_CALL_VOID(dot11h_cfg, debugNOP, mtlk_set_dbg_nop,
                                    (mtlk_core_get_scan_support(core), dot11h_cfg->debugNOP));

    MTLK_CFG_END_CHEK_ITEM_AND_CALL()
  }
  MTLK_CLPB_FINALLY(res)
    return mtlk_clpb_push_res(clpb, res);
  MTLK_CLPB_END;
}

static int
_mtlk_core_get_mibs_cfg (mtlk_handle_t hcore,
                         const void* data, uint32 data_size)
{
  uint32 res = MTLK_ERR_OK;
  mtlk_mibs_cfg_t mibs_cfg;
  mtlk_core_t *core = (mtlk_core_t*)hcore;
  wave_radio_t *radio = wave_vap_radio_get(core->vap_handle);
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  memset(&mibs_cfg, 0, sizeof(mibs_cfg));

  MTLK_CFG_SET_ITEM(&mibs_cfg, short_cyclic_prefix, WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_SHORT_CYCLIC_PREFIX));

  if (mtlk_core_scan_is_running(core)) {
    ILOG1_D("%CID-%04x: Request eliminated due to running scan", mtlk_vap_get_oid(core->vap_handle));
    res = MTLK_ERR_NOT_READY;
    goto err_get;
  } else {
    MTLK_CFG_SET_MIB_ITEM_BY_FUNC_VOID(&mibs_cfg, long_retry_limit, mtlk_get_mib_value_uint16,
                                       MIB_LONG_RETRY_LIMIT, &mibs_cfg.long_retry_limit, core);
  }

err_get:
  res = mtlk_clpb_push(clpb, &res, sizeof(res));
  if (MTLK_ERR_OK == res) {
    res = mtlk_clpb_push(clpb, &mibs_cfg, sizeof(mibs_cfg));
  }

  return res;
}

static int
_wave_core_get_phy_inband_power (mtlk_handle_t hcore,
                                 const void *data, uint32 data_size)
{
  int res = MTLK_ERR_OK;
  mtlk_phy_inband_power_cfg_t power_cfg;
  mtlk_core_t *core = (mtlk_core_t*)hcore;
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;
  wave_radio_t *radio;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  radio = wave_vap_radio_get(core->vap_handle);

  memset(&power_cfg, 0, sizeof(power_cfg));
  MTLK_CFG_SET_ITEM_BY_FUNC(&power_cfg, power_data, mtlk_ccr_read_phy_inband_power,
                            (mtlk_hw_mmb_get_ccr(mtlk_vap_get_hw(core->vap_handle)),
                             wave_radio_id_get(radio),
                             wave_radio_current_antenna_mask_get(radio),
                             power_cfg.power_data.noise_estim,
                             power_cfg.power_data.system_gain),
                             res);

  return mtlk_clpb_push_res_data(clpb, res, &power_cfg, sizeof(power_cfg));
}

static int
_mtlk_core_get_country_cfg (mtlk_handle_t hcore,
                            const void* data, uint32 data_size)
{
  uint32 res = MTLK_ERR_OK;
  mtlk_country_cfg_t country_cfg;
  mtlk_core_t *core = (mtlk_core_t*)hcore;
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  memset(&country_cfg, 0, sizeof(country_cfg));

  /* TODO: This check must be dropped in favor of abilities */
  if (mtlk_core_scan_is_running(core)) {
    ILOG1_D("%CID-%04x: Request eliminated due to running scan", mtlk_vap_get_oid(core->vap_handle));
    res = MTLK_ERR_NOT_READY;
    goto err_get;
  }

  MTLK_CFG_SET_ITEM_BY_FUNC_VOID(&country_cfg, country_code, core_cfg_country_code_get,
                                 (core, &country_cfg.country_code));

err_get:
  res = mtlk_clpb_push(clpb, &res, sizeof(res));
  if (MTLK_ERR_OK == res) {
    res = mtlk_clpb_push(clpb, &country_cfg, sizeof(country_cfg));
  }

  return res;
}

static int
_mtlk_core_set_bss_base_rate(mtlk_core_t *core, uint32 val)
{
  MTLK_ASSERT(mtlk_vap_is_ap(core->vap_handle));

  if (mtlk_core_get_net_state(core) != NET_STATE_READY) {
    return MTLK_ERR_NOT_READY;
  }

  if ((val != CFG_BASIC_RATE_SET_DEFAULT)
      && (val != CFG_BASIC_RATE_SET_EXTRA)
      && (val != CFG_BASIC_RATE_SET_LEGACY)) {
    return MTLK_ERR_PARAMS;
  }

  if ((val == CFG_BASIC_RATE_SET_LEGACY)
      && (MTLK_HW_BAND_2_4_GHZ != core_cfg_get_freq_band_cfg(core))) {
    return MTLK_ERR_PARAMS;
  }

  MTLK_CORE_PDB_SET_INT(core, PARAM_DB_CORE_BASIC_RATE_SET, val);
  return MTLK_ERR_OK;
}

static int
_mtlk_core_set_mibs_cfg (mtlk_handle_t hcore,
                         const void* data, uint32 data_size)
{
  uint32 res = MTLK_ERR_OK;
  mtlk_core_t *core = (mtlk_core_t*)hcore;
  wave_radio_t *radio = wave_vap_radio_get(core->vap_handle);
  mtlk_txmm_t *txmm = mtlk_vap_get_txmm(core->vap_handle);

  mtlk_mibs_cfg_t *mibs_cfg = NULL;
  uint32 mibs_cfg_size;
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  mibs_cfg = mtlk_clpb_enum_get_next(clpb, &mibs_cfg_size);

  MTLK_CLPB_TRY(mibs_cfg, mibs_cfg_size)
  {
    MTLK_CFG_START_CHEK_ITEM_AND_CALL()
      MTLK_CFG_CHECK_ITEM_AND_CALL_VOID(mibs_cfg, short_cyclic_prefix, WAVE_RADIO_PDB_SET_INT,
                                        (radio, PARAM_DB_RADIO_SHORT_CYCLIC_PREFIX, mibs_cfg->short_cyclic_prefix));

      if (mtlk_core_scan_is_running(core)) {
        ILOG1_D("CID-%04x: Request eliminated due to running scan", mtlk_vap_get_oid(core->vap_handle));
            MTLK_CLPB_EXIT(MTLK_ERR_NOT_READY);
      } else {
        MTLK_CFG_CHECK_ITEM_AND_CALL(mibs_cfg, long_retry_limit, mtlk_set_mib_value_uint16,
                                     (txmm, MIB_LONG_RETRY_LIMIT, mibs_cfg->long_retry_limit), res);

        MTLK_CFG_CHECK_ITEM_AND_CALL_VOID(mibs_cfg, long_retry_limit, WAVE_RADIO_PDB_SET_INT,
                                          (radio, PARAM_DB_RADIO_LONG_RETRY_LIMIT, mibs_cfg->long_retry_limit));

      }
    MTLK_CFG_END_CHEK_ITEM_AND_CALL()
  }
  MTLK_CLPB_FINALLY(res)
    return mtlk_clpb_push_res(clpb, res);
  MTLK_CLPB_END;
}

static int
_mtlk_core_set_country_cfg (mtlk_handle_t hcore,
                            const void* data, uint32 data_size)
{
  uint32 res = MTLK_ERR_OK;
  mtlk_core_t *core = (mtlk_core_t*)hcore;

  mtlk_country_cfg_t *country_cfg = NULL;
  uint32 country_cfg_size;
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  country_cfg = mtlk_clpb_enum_get_next(clpb, &country_cfg_size);
  MTLK_CLPB_TRY(country_cfg, country_cfg_size)
  {
    MTLK_CFG_START_CHEK_ITEM_AND_CALL()

      if (mtlk_core_scan_is_running(core)) {
        ILOG1_D("CID-%04x: Request eliminated due to running scan", mtlk_vap_get_oid(core->vap_handle));
        res = MTLK_ERR_NOT_READY;
        break;
      }

      MTLK_CFG_CHECK_ITEM_AND_CALL(country_cfg, country_code, core_cfg_set_country_from_ui,
                                   (core, &country_cfg->country_code), res);

    MTLK_CFG_END_CHEK_ITEM_AND_CALL()
  }
  MTLK_CLPB_FINALLY(res)
    return mtlk_clpb_push_res(clpb, res);
  MTLK_CLPB_END;
}

static int
_mtlk_core_set_wds_cfg (mtlk_handle_t hcore,
                        const void* data, uint32 data_size)
{
  uint32 res = MTLK_ERR_OK;
  mtlk_wds_cfg_t *wds_cfg;
  uint32  wds_cfg_size;
  mtlk_core_t *core = (mtlk_core_t*)hcore;
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  wds_cfg = mtlk_clpb_enum_get_next(clpb, &wds_cfg_size);
  MTLK_CLPB_TRY(wds_cfg, wds_cfg_size)
  {
    MTLK_CFG_START_CHEK_ITEM_AND_CALL()
      MTLK_CFG_CHECK_ITEM_AND_CALL(wds_cfg, peer_ap_addr_add, wds_usr_add_peer_ap,
                                   (&core->slow_ctx->wds_mng, &wds_cfg->peer_ap_addr_add), res);

      MTLK_CFG_CHECK_ITEM_AND_CALL(wds_cfg, peer_ap_addr_del, wds_usr_del_peer_ap,
                                   (&core->slow_ctx->wds_mng, &wds_cfg->peer_ap_addr_del), res);

      MTLK_CFG_GET_ITEM(wds_cfg, peer_ap_key_idx, core->slow_ctx->peerAPs_key_idx);

    MTLK_CFG_END_CHEK_ITEM_AND_CALL()
  }
  MTLK_CLPB_FINALLY(res)
    return mtlk_clpb_push_res(clpb, res);
  MTLK_CLPB_END;
}

static int
_mtlk_core_get_wds_cfg (mtlk_handle_t hcore,
                        const void* data, uint32 data_size)
{
  uint32 res = MTLK_ERR_OK;
  mtlk_wds_cfg_t wds_cfg;
  mtlk_core_t *core = (mtlk_core_t*)hcore;
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  memset(&wds_cfg, 0, sizeof(wds_cfg));

  MTLK_CFG_SET_ITEM(&wds_cfg, peer_ap_key_idx, core->slow_ctx->peerAPs_key_idx);

  res = mtlk_clpb_push(clpb, &res, sizeof(res));
  if (MTLK_ERR_OK == res) {
    res = mtlk_clpb_push(clpb, &wds_cfg, sizeof(wds_cfg));
  }

  return res;
}

static int
_mtlk_core_get_wds_peer_ap (mtlk_handle_t hcore,
                            const void* data, uint32 data_size)
{
  uint32 res = MTLK_ERR_OK;
  mtlk_wds_cfg_t wds_cfg;
  mtlk_core_t *core = (mtlk_core_t*)hcore;
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  memset(&wds_cfg, 0, sizeof(wds_cfg));

  MTLK_CFG_SET_ITEM_BY_FUNC(&wds_cfg, peer_ap_vect,
    mtlk_wds_get_peer_vect, (&core->slow_ctx->wds_mng, &wds_cfg.peer_ap_vect), res);

  res = mtlk_clpb_push(clpb, &res, sizeof(res));
  if (MTLK_ERR_OK == res) {
    res = mtlk_clpb_push(clpb, &wds_cfg, sizeof(wds_cfg));
  }

  return res;
}

static int
_core_cfg_get_dot11d_cfg (mtlk_handle_t hcore,
                          const void* data, uint32 data_size)
{
  uint32 res = MTLK_ERR_OK;
  mtlk_dot11d_cfg_t dot11d_cfg;

  mtlk_core_t *core = (mtlk_core_t*)hcore;

  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  memset(&dot11d_cfg, 0, sizeof(dot11d_cfg));

  MTLK_CFG_SET_ITEM(&dot11d_cfg, is_dot11d, core_cfg_get_dot11d(core));

  res = mtlk_clpb_push(clpb, &res, sizeof(res));
  if (MTLK_ERR_OK == res) {
    res = mtlk_clpb_push(clpb, &dot11d_cfg, sizeof(dot11d_cfg));
  }

  return res;
}

static int
_mtlk_core_set_dot11d_cfg (mtlk_handle_t hcore,
                           const void* data, uint32 data_size)
{
  uint32 res = MTLK_ERR_OK;
  mtlk_core_t *core = (mtlk_core_t*)hcore;

  mtlk_dot11d_cfg_t *dot11d_cfg = NULL;
  uint32 dot11d_cfg_size;
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  dot11d_cfg = mtlk_clpb_enum_get_next(clpb, &dot11d_cfg_size);
  MTLK_CLPB_TRY(dot11d_cfg, dot11d_cfg_size)
  {
    MTLK_CFG_START_CHEK_ITEM_AND_CALL()
       MTLK_CFG_CHECK_ITEM_AND_CALL(dot11d_cfg, is_dot11d, core_cfg_set_is_dot11d,
                                   (core, dot11d_cfg->is_dot11d), res);
    MTLK_CFG_END_CHEK_ITEM_AND_CALL()
  }
  MTLK_CLPB_FINALLY(res)
    return mtlk_clpb_push_res(clpb, res);
  MTLK_CLPB_END;
}

static int
_mtlk_core_get_mac_wdog_cfg (mtlk_handle_t hcore,
                             const void* data, uint32 data_size)
{
  uint32 res = MTLK_ERR_OK;
  mtlk_mac_wdog_cfg_t mac_wdog_cfg;
  mtlk_core_t *core = (mtlk_core_t*)hcore;
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;
  wave_radio_t *radio = wave_vap_radio_get(core->vap_handle);

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  memset(&mac_wdog_cfg, 0, sizeof(mac_wdog_cfg));

  MTLK_CFG_SET_ITEM(&mac_wdog_cfg, mac_watchdog_timeout_ms,
                    WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_MAC_WATCHDOG_TIMER_TIMEOUT_MS));
  MTLK_CFG_SET_ITEM(&mac_wdog_cfg, mac_watchdog_period_ms,
                    WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_MAC_WATCHDOG_TIMER_PERIOD_MS));

  res = mtlk_clpb_push(clpb, &res, sizeof(res));
  if (MTLK_ERR_OK == res) {
    res = mtlk_clpb_push(clpb, &mac_wdog_cfg, sizeof(mac_wdog_cfg));
  }

  return res;
}

static int
_mtlk_core_set_mac_wdog_timeout(mtlk_core_t *core, uint16 value)
{
  if (value < 1000) {
    return MTLK_ERR_PARAMS;
  }
  WAVE_RADIO_PDB_SET_INT(wave_vap_radio_get(core->vap_handle), PARAM_DB_RADIO_MAC_WATCHDOG_TIMER_TIMEOUT_MS, value);
  return MTLK_ERR_OK;
}

static int
_mtlk_core_set_mac_wdog_period(mtlk_core_t *core, uint32 value)
{
  if (0 == value) {
    return MTLK_ERR_PARAMS;
  }
  WAVE_RADIO_PDB_SET_INT(wave_vap_radio_get(core->vap_handle), PARAM_DB_RADIO_MAC_WATCHDOG_TIMER_PERIOD_MS, value);
  return MTLK_ERR_OK;
}

static int
_mtlk_core_set_mac_wdog_cfg (mtlk_handle_t hcore,
                             const void* data, uint32 data_size)
{
  uint32 res = MTLK_ERR_OK;
  mtlk_core_t *core = (mtlk_core_t*)hcore;

  mtlk_mac_wdog_cfg_t *mac_wdog_cfg = NULL;
  uint32 mac_wdog_cfg_size;
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  mac_wdog_cfg = mtlk_clpb_enum_get_next(clpb, &mac_wdog_cfg_size);
  MTLK_CLPB_TRY(mac_wdog_cfg, mac_wdog_cfg_size)
  {
    MTLK_CFG_START_CHEK_ITEM_AND_CALL()
       MTLK_CFG_CHECK_ITEM_AND_CALL(mac_wdog_cfg, mac_watchdog_timeout_ms, _mtlk_core_set_mac_wdog_timeout,
                                   (core, mac_wdog_cfg->mac_watchdog_timeout_ms), res);
       MTLK_CFG_CHECK_ITEM_AND_CALL(mac_wdog_cfg, mac_watchdog_period_ms, _mtlk_core_set_mac_wdog_period,
                                   (core, mac_wdog_cfg->mac_watchdog_period_ms), res);
    MTLK_CFG_END_CHEK_ITEM_AND_CALL()
  }
  MTLK_CLPB_FINALLY(res)
    return mtlk_clpb_push_res(clpb, res);
  MTLK_CLPB_END;
}

static int
_mtlk_core_get_stadb_cfg (mtlk_handle_t hcore,
                          const void* data, uint32 data_size)
{
  uint32 res = MTLK_ERR_OK;
  mtlk_stadb_cfg_t stadb_cfg;

  mtlk_core_t *core = (mtlk_core_t*)hcore;

  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  memset(&stadb_cfg, 0, sizeof(stadb_cfg));
  MTLK_CFG_SET_ITEM(&stadb_cfg, keepalive_interval, core->slow_ctx->stadb.keepalive_interval);

  res = mtlk_clpb_push(clpb, &res, sizeof(res));
  if (MTLK_ERR_OK == res) {
    res = mtlk_clpb_push(clpb, &stadb_cfg, sizeof(stadb_cfg));
  }

  return res;
}

static int
_mtlk_core_set_stadb_cfg (mtlk_handle_t hcore,
                          const void* data, uint32 data_size)
{
  uint32 res = MTLK_ERR_OK;
  mtlk_core_t *core = (mtlk_core_t*)hcore;

  mtlk_stadb_cfg_t *stadb_cfg = NULL;
  uint32 stadb_cfg_size;
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  stadb_cfg = mtlk_clpb_enum_get_next(clpb, &stadb_cfg_size);
  MTLK_CLPB_TRY(stadb_cfg, stadb_cfg_size)
  {
    if (stadb_cfg->keepalive_interval == 0) {
      stadb_cfg->keepalive_interval = DEFAULT_KEEPALIVE_TIMEOUT;
    }

    MTLK_CFG_GET_ITEM(stadb_cfg, keepalive_interval,
                      core->slow_ctx->stadb.keepalive_interval);
  }
  MTLK_CLPB_FINALLY(res)
    return mtlk_clpb_push_res(clpb, res);
  MTLK_CLPB_END;
}

static uint8
_mtlk_core_get_spectrum_mode(mtlk_core_t *core)
{
  wave_radio_t *radio = wave_vap_radio_get(core->vap_handle);
  if (mtlk_vap_is_ap(core->vap_handle)) {
    return WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_SPECTRUM_MODE);
  }

  return MTLK_CORE_PDB_GET_INT(core, PARAM_DB_CORE_STA_FORCE_SPECTRUM_MODE);
}

static int
_mtlk_core_get_channel (mtlk_core_t *core)
{
  wave_radio_t *radio;
  MTLK_ASSERT(NULL != core);

  radio = wave_vap_radio_get(core->vap_handle);

  /* Retrieve PARAM_DB_RADIO_CHANNEL_CUR channel in case if there are active VAPs
   * Master VAP can be in NET_STATE_READY, but Slave VAP can be in NET_STATE_CONNECTED,
   * therefore PARAM_DB_RADIO_CHANNEL_CUR channel, belonged to Master VAP has correct value */
  if ((NET_STATE_CONNECTED == mtlk_core_get_net_state(core)) || (0 != mtlk_vap_manager_get_active_vaps_number(mtlk_vap_get_manager(core->vap_handle))))
    return WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_CHANNEL_CUR);
  else
    return WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_CHANNEL_CFG);
}

int __MTLK_IFUNC
mtlk_core_get_channel (mtlk_core_t *core)
{
  return _mtlk_core_get_channel(core);
}

static int
_mtlk_core_set_up_rescan_exemption_time_sec (mtlk_core_t *core, uint32 value)
{
  if (value == MAX_UINT32) {
    ;
  }
  else if (value < MAX_UINT32/ MSEC_PER_SEC) {
    value *= MSEC_PER_SEC;
  }
  else {
    /* In this case, the TS (which is measured in ms) can wrap around. */
    return MTLK_ERR_PARAMS;
  }

  MTLK_CORE_PDB_SET_INT(core, PARAM_DB_CORE_UP_RESCAN_EXEMPTION_TIME, value);

  return MTLK_ERR_OK;
}

static uint32
_mtlk_core_get_up_rescan_exemption_time_sec (mtlk_core_t *core)
{
  uint32 res = MTLK_CORE_PDB_GET_INT(core, PARAM_DB_CORE_UP_RESCAN_EXEMPTION_TIME);

  if (res != MAX_UINT32) {
    res /= MSEC_PER_SEC;
  }

  return res;
}


static void
_mtlk_core_fill_channel_params (mtlk_core_t *core, mtlk_core_channel_def_t *ch_def)
{
  wave_radio_t *radio = wave_vap_radio_get(core->vap_handle);

  ch_def->channel = _mtlk_core_get_channel(core);
  ch_def->spectrum_mode = _mtlk_core_get_spectrum_mode(core);
  ch_def->bonding = WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_BONDING_MODE);
}

static void
_mtlk_master_core_get_core_cfg (mtlk_core_t *core,
                         mtlk_gen_core_cfg_t* pCore_Cfg)
{
  wave_radio_t *radio = wave_vap_radio_get(core->vap_handle);

  MTLK_CFG_CHECK_AND_SET_ITEM(pCore_Cfg, dbg_sw_wd_enable, WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_MAC_SOFT_RESET_ENABLE));
  MTLK_CFG_CHECK_AND_SET_ITEM(pCore_Cfg, up_rescan_exemption_time, _mtlk_core_get_up_rescan_exemption_time_sec(core));
  MTLK_CFG_CHECK_AND_SET_ITEM(pCore_Cfg, frequency_band_cur, core_cfg_get_freq_band_cur(core));
  MTLK_CFG_CHECK_AND_SET_ITEM_BY_FUNC_VOID(pCore_Cfg, channel_def, _mtlk_core_fill_channel_params, (core, &pCore_Cfg->channel_def));
}

static void
_mtlk_slave_core_get_core_cfg (mtlk_core_t *core,
                         mtlk_gen_core_cfg_t* pCore_Cfg)
{
  _mtlk_master_core_get_core_cfg(mtlk_core_get_master(core), pCore_Cfg);
}

static uint32 __MTLK_IFUNC
_core_get_network_mode_current(mtlk_core_t *core)
{
    return core_cfg_get_network_mode_cur(core);
}

static int
_mtlk_core_get_temperature_req(mtlk_core_t *core, uint32 *temperature, uint32 calibrate_mask)
{
  int                       res = MTLK_ERR_OK;
  mtlk_txmm_msg_t           man_msg;
  mtlk_txmm_data_t          *man_entry = NULL;
  UMI_HDK_USER_DEMAND       *pTemperature = NULL;
  int i;

  mtlk_df_t *df               = mtlk_vap_get_df(core->vap_handle);
  mtlk_df_user_t *df_user     = mtlk_df_get_user(df);
  struct wireless_dev *wdev = mtlk_df_user_get_wdev(df_user);
  BOOL cac_started = wdev->cac_started;

  mtlk_card_type_info_t  card_type_info;

  mtlk_hw_get_prop(mtlk_vap_get_hwapi(core->vap_handle), MTLK_HW_PROP_CARD_TYPE_INFO,
                   &card_type_info, sizeof(&card_type_info));

  if (!_mtlk_card_is_asic(card_type_info)) { /* non ASIC, i.e. FPGA/Emul */
    return MTLK_ERR_NOT_SUPPORTED;
  }

  if (!is_channel_certain(__wave_core_chandef_get(core))) {
    return MTLK_ERR_NOT_READY;
  }

  if (cac_started || core->chan_state != ST_NORMAL) {
    return MTLK_ERR_NOT_READY;
  }

  man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, mtlk_vap_get_txmm(core->vap_handle), NULL);
  if (NULL == man_entry) {
    ELOG_D("CID-%04x: No man entry available", mtlk_vap_get_oid(core->vap_handle));
    res = MTLK_ERR_NO_RESOURCES;
    goto FINISH;
  }

  res = mtlk_core_radio_enable_if_needed(core);
  if (MTLK_ERR_OK != res)
    goto FINISH;

  man_entry->id = UM_MAN_HDK_USER_DEMAND_REQ;
  man_entry->payload_size = sizeof(UMI_HDK_USER_DEMAND);

  pTemperature = (UMI_HDK_USER_DEMAND *)(man_entry->payload);
  memset(pTemperature, 0, sizeof(*pTemperature));

  pTemperature->calibrateMask = HOST_TO_MAC32(calibrate_mask);

  res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);
  if (MTLK_ERR_OK != res) {
    ELOG_DD("CID-%04x: Get temperature failed (%i)", mtlk_vap_get_oid(core->vap_handle), res);
    goto FINISH;
  }

  for (i=0; i<NUM_OF_ANTS_FOR_TEMPERATURE; i++) {
    temperature[i] = MAC_TO_HOST32(pTemperature->temperature[i]);
  }

  res = mtlk_core_radio_disable_if_needed(core);

FINISH:
  if (man_entry) {
    mtlk_txmm_msg_cleanup(&man_msg);
  }

  return res;
}

static int
_mtlk_core_set_calibrate_on_demand (mtlk_handle_t hcore, const void* data, uint32 data_size)
{
  uint32 res = MTLK_ERR_OK;
  mtlk_temperature_sensor_t *temperature_cfg = NULL;
  uint32 temperature_cfg_size;
  mtlk_core_t *core = (mtlk_core_t*)hcore;
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  temperature_cfg = mtlk_clpb_enum_get_next(clpb, &temperature_cfg_size);
  MTLK_CLPB_TRY(temperature_cfg, temperature_cfg_size)
    MTLK_CFG_SET_ITEM_BY_FUNC(temperature_cfg, temperature, _mtlk_core_get_temperature_req,
                              (core, temperature_cfg->temperature, temperature_cfg->calibrate_mask), res);
  MTLK_CLPB_FINALLY(res)
    return mtlk_clpb_push_res(clpb, res);
  MTLK_CLPB_END;
}

static int
_mtlk_core_get_temperature (mtlk_handle_t hcore, const void* data, uint32 data_size)
{
  uint32 res = MTLK_ERR_OK;
  mtlk_temperature_sensor_t temperature_cfg;
  mtlk_core_t *core = (mtlk_core_t*)hcore;
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  MTLK_CFG_SET_ITEM_BY_FUNC(&temperature_cfg, temperature, _mtlk_core_get_temperature_req,
                            (core, temperature_cfg.temperature, 0), res);

  /* push result and config to clipboard*/
  res = mtlk_clpb_push(clpb, &res, sizeof(res));
  if (MTLK_ERR_OK == res) {
    res = mtlk_clpb_push(clpb, &temperature_cfg, sizeof(temperature_cfg));
  }

  return res;
}

static int
_mtlk_core_get_essid (mtlk_core_t *core, mtlk_gen_core_cfg_t* pcore_cfg)
{
  int res = MTLK_ERR_OK;
  mtlk_pdb_size_t str_size = sizeof(pcore_cfg->essid);

  /* Don't report ESSID to iw/iwconfig if we are not beaconing */
  if (core_cfg_is_connected(core)) {
    res = MTLK_CORE_PDB_GET_BINARY(core, PARAM_DB_CORE_ESSID, pcore_cfg->essid, &str_size);
  }
  else {
    pcore_cfg->essid[0] = '\0';
  }
  return res;
}

static int
_mtlk_core_get_core_cfg (mtlk_handle_t hcore,
                         const void* data, uint32 data_size)
{
  int res = MTLK_ERR_OK;
  mtlk_gen_core_cfg_t* pcore_cfg;
  mtlk_core_t *core = (mtlk_core_t*)hcore;
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;
  uint32 str_size, core_cfg_size;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  pcore_cfg = mtlk_clpb_enum_get_next(clpb, &core_cfg_size);

  MTLK_CLPB_TRY(pcore_cfg, core_cfg_size)
    MTLK_CFG_CHECK_AND_SET_ITEM(pcore_cfg, bridge_mode,
                                MTLK_CORE_PDB_GET_INT(core, PARAM_DB_CORE_BRIDGE_MODE));
    MTLK_CFG_CHECK_AND_SET_ITEM(pcore_cfg, ap_forwarding,
                                MTLK_CORE_PDB_GET_INT(core, PARAM_DB_CORE_AP_FORWARDING));
    MTLK_CFG_CHECK_AND_SET_ITEM_BY_FUNC(pcore_cfg, reliable_multicast, mtlk_core_receive_reliable_multicast,
                                        (core, &pcore_cfg->reliable_multicast), res);

    if (NET_STATE_CONNECTED == mtlk_core_get_net_state(core)) {
      MTLK_CFG_CHECK_AND_SET_ITEM_BY_FUNC_VOID(pcore_cfg, bssid, MTLK_CORE_PDB_GET_MAC, (core,
                                               PARAM_DB_CORE_BSSID, &pcore_cfg->bssid));
    } else {
      MTLK_CFG_CHECK_AND_SET_ITEM_BY_FUNC_VOID(pcore_cfg, bssid, ieee_addr_zero, (&pcore_cfg->bssid));
    }

    MTLK_CFG_CHECK_AND_SET_ITEM(pcore_cfg, net_mode, _core_get_network_mode_current(core));
    MTLK_CFG_CHECK_AND_SET_ITEM(pcore_cfg, bss_rate,
                                MTLK_CORE_PDB_GET_INT(core, PARAM_DB_CORE_BASIC_RATE_SET));
    str_size = sizeof(pcore_cfg->nickname);
    MTLK_CFG_CHECK_AND_SET_ITEM_BY_FUNC(pcore_cfg, nickname, wave_pdb_get_string,
                                        (mtlk_vap_get_param_db(core->vap_handle),
                                        PARAM_DB_CORE_NICK_NAME,
                                        pcore_cfg->nickname, &str_size), res);
    MTLK_CFG_CHECK_AND_SET_ITEM_BY_FUNC(pcore_cfg, essid, _mtlk_core_get_essid, (core, pcore_cfg), res);
    MTLK_CFG_CHECK_AND_SET_ITEM(pcore_cfg, is_hidden_ssid, core->slow_ctx->cfg.is_hidden_ssid);
    MTLK_CFG_CHECK_AND_SET_ITEM(pcore_cfg, is_bss_load_enable, MTLK_CORE_PDB_GET_INT(core, PARAM_DB_CORE_IS_BSS_LOAD_ENABLE));
    MTLK_CFG_CHECK_AND_SET_ITEM_BY_FUNC(pcore_cfg, admission_capacity, mtlk_core_receive_admission_capacity,
                                        (core, &pcore_cfg->admission_capacity), res);
    if (mtlk_vap_is_slave_ap(core->vap_handle)) {
      _mtlk_slave_core_get_core_cfg(core, pcore_cfg);
    } else {
      _mtlk_master_core_get_core_cfg(core, pcore_cfg);
    }
  MTLK_CLPB_FINALLY(res)
    return mtlk_clpb_push_res_data(clpb, res, pcore_cfg, sizeof(*pcore_cfg));
  MTLK_CLPB_END
}

static int _wave_core_network_mode_get (mtlk_handle_t hcore, const void* data, uint32 data_size)
{
  int res = MTLK_ERR_OK;
  wave_core_network_mode_cfg_t network_mode_cfg;
  mtlk_core_t *core = (mtlk_core_t*)hcore;
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  /* read config from internal db */
  memset(&network_mode_cfg, 0, sizeof(network_mode_cfg));
  MTLK_CFG_SET_ITEM(&network_mode_cfg, net_mode, (uint8)_core_get_network_mode_current(core));

  /* push result and config to clipboard*/
  res = mtlk_clpb_push(clpb, &res, sizeof(res));
  if (MTLK_ERR_OK == res)
      res = mtlk_clpb_push(clpb, &network_mode_cfg, sizeof(network_mode_cfg));

  return res;
}

BOOL __MTLK_IFUNC   wave_hw_radio_band_cfg_is_single (mtlk_hw_t *hw);

static int _wave_core_cdb_cfg_get (mtlk_handle_t hcore, const void* data, uint32 data_size)
{
  int res = MTLK_ERR_OK;
  wave_core_cdb_cfg_t cdb;
  mtlk_core_t *core = (mtlk_core_t*)hcore;
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  /* read config from internal db */
  memset(&cdb, 0, sizeof(cdb));
  MTLK_CFG_SET_ITEM(&cdb, cdb_cfg, (uint8)!wave_hw_radio_band_cfg_is_single(mtlk_vap_get_hw(core->vap_handle)));

  /* push result and config to clipboard*/
  res = mtlk_clpb_push(clpb, &res, sizeof(res));
  if (MTLK_ERR_OK == res)
      res = mtlk_clpb_push(clpb, &cdb, sizeof(cdb));

  return res;
}

static int
_mtlk_core_receive_dynamic_mc_rate (mtlk_core_t *core, uint32 *dynamic_mc_rate)
{
  mtlk_txmm_msg_t     man_msg;
  mtlk_txmm_data_t   *man_entry = NULL;
  UmiDmrConfig_t     *dmr_cfg = NULL;
  mtlk_vap_handle_t   vap_handle = core->vap_handle;
  int                 res;

  man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, mtlk_vap_get_txmm(vap_handle), NULL);
  if (NULL == man_entry) {
    ELOG_D("CID-%04x: No man entry available", mtlk_vap_get_oid(vap_handle));
    return MTLK_ERR_NO_RESOURCES;
  }

  man_entry->id = UM_MAN_DMR_CONFIG_REQ;
  man_entry->payload_size = sizeof(UmiDmrConfig_t);

  dmr_cfg = (UmiDmrConfig_t *)(man_entry->payload);
  dmr_cfg->getSetOperation = API_GET_OPERATION;

  res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);

  if (MTLK_ERR_OK == res) {
    *dynamic_mc_rate = MAC_TO_HOST32(dmr_cfg->dmrMode);
  }
  else {
    ELOG_DD("CID-%04x: Receiving UM_MAN_DMR_CONFIG_REQ failed, res %d", mtlk_vap_get_oid(vap_handle), res);
  }

  mtlk_txmm_msg_cleanup(&man_msg);
  return res;
}

static int
_mtlk_core_get_master_specific_cfg (mtlk_handle_t hcore,
                                    const void* data, uint32 data_size)
{
  int res = MTLK_ERR_OK;
  mtlk_core_t *core = (mtlk_core_t*)hcore;
  wave_radio_t *radio = wave_vap_radio_get(core->vap_handle);
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;

  mtlk_master_core_cfg_t *master_cfg = NULL;
  uint32 master_cfg_size;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  master_cfg = mtlk_clpb_enum_get_next(clpb, &master_cfg_size);

  MTLK_CLPB_TRY(master_cfg, master_cfg_size)
    MTLK_CFG_CHECK_AND_SET_ITEM(master_cfg, acs_update_timeout,
      WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_ACS_UPDATE_TO));
  MTLK_CFG_CHECK_AND_SET_ITEM_BY_FUNC(master_cfg, slow_probing_mask, mtlk_core_receive_slow_probing_mask,
    (core, &master_cfg->slow_probing_mask), res);
  MTLK_CFG_CHECK_AND_SET_ITEM_BY_FUNC(master_cfg, mu_operation, mtlk_core_receive_mu_operation, (core, &master_cfg->mu_operation), res);
  MTLK_CFG_CHECK_AND_SET_ITEM(master_cfg, he_mu_operation, WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_HE_MU_OPERATION));
  MTLK_CFG_CHECK_AND_SET_ITEM(master_cfg, rts_mode_params.dynamic_bw, WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_RTS_DYNAMIC_BW));
  MTLK_CFG_CHECK_AND_SET_ITEM_BY_FUNC(master_cfg, rts_mode_params, mtlk_core_receive_rts_mode, (core, &master_cfg->rts_mode_params), res);
  MTLK_CFG_CHECK_AND_SET_ITEM(master_cfg, bf_mode, WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_BF_MODE));
  MTLK_CFG_CHECK_AND_SET_ITEM_BY_FUNC(master_cfg, txop_mode_params, mtlk_core_receive_txop_mode, (core, &master_cfg->txop_mode_params), res);
  MTLK_CFG_CHECK_AND_SET_ITEM(master_cfg, txop_mode_params.sid, WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_TXOP_SID));
  MTLK_CFG_CHECK_AND_SET_ITEM(master_cfg, active_ant_mask, WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_ACTIVE_ANT_MASK));
  MTLK_CFG_CHECK_AND_SET_ITEM_BY_FUNC_VOID(master_cfg, fixed_pwr_params, wave_radio_fixed_pwr_params_get, (radio, &master_cfg->fixed_pwr_params));
  MTLK_CFG_CHECK_AND_SET_ITEM(master_cfg, unconnected_sta_scan_time,
    WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_UNCONNECTED_STA_SCAN_TIME));
  MTLK_CFG_CHECK_AND_SET_ITEM_BY_FUNC(master_cfg, fast_drop, mtlk_core_receive_fast_drop, (core, &master_cfg->fast_drop), res);
  MTLK_CFG_CHECK_AND_SET_ITEM_BY_FUNC(master_cfg, dynamic_mc_rate, _mtlk_core_receive_dynamic_mc_rate, (core, &master_cfg->dynamic_mc_rate), res);
  MTLK_CLPB_FINALLY(res)
    return mtlk_clpb_push_res_data(clpb, res, master_cfg, sizeof(*master_cfg));
  MTLK_CLPB_END
}

static int
_mtlk_core_set_bridge_mode(mtlk_core_t *core, uint8 mode)
{
  uint8 mode_old;
  mtlk_vap_manager_t* vap_manager = mtlk_vap_get_manager(core->vap_handle);

  /* check for only allowed values */
  if (mode >= BR_MODE_LAST) {
    ELOG_DD("CID-%04x: Unsupported bridge mode value: %d.", mtlk_vap_get_oid(core->vap_handle), mode);
    return MTLK_ERR_PARAMS;
  }

  /* on AP only NONE and WDS allowed */
  if (mtlk_vap_is_ap(core->vap_handle) && mode != BR_MODE_NONE && mode != BR_MODE_WDS) {
    ELOG_DD("CID-%04x: Unsupported (on AP) bridge mode value: %d.", mtlk_vap_get_oid(core->vap_handle), mode);
    return MTLK_ERR_PARAMS;
  }

  mode_old = MTLK_CORE_PDB_GET_INT(core, PARAM_DB_CORE_BRIDGE_MODE);

  /* Nothing's changed */
  if (mode_old == mode)
    return MTLK_ERR_OK;

  MTLK_CORE_PDB_SET_INT(core, PARAM_DB_CORE_BRIDGE_MODE, mode);

  ILOG1_DD("CID-%04x: bridge_mode set to %u", mtlk_vap_get_oid(core->vap_handle), mode);

  if (mtlk_vap_is_ap(core->vap_handle)){
    if (mode == BR_MODE_WDS){
      /* Enable WDS abilities */
      wds_enable_abilities(&core->slow_ctx->wds_mng);
      mtlk_vap_manager_inc_wds_bridgemode(vap_manager);
    }else{
      /* Disable WDS abilities */
      wds_disable_abilities(&core->slow_ctx->wds_mng);
    }
    if ((mode != BR_MODE_WDS) && (mode_old == BR_MODE_WDS)) {
      wds_switch_off(&core->slow_ctx->wds_mng);
      mtlk_vap_manager_dec_wds_bridgemode(vap_manager);
    }
  }

  return MTLK_ERR_OK;
}

static int
_mtlk_core_set_bss_load_enable(mtlk_core_t *core, uint32 value)
{
  int res = MTLK_ERR_OK;
  mtlk_core_t *master_core;

  MTLK_CORE_PDB_SET_INT(core, PARAM_DB_CORE_IS_BSS_LOAD_ENABLE, value);

  master_core = mtlk_vap_manager_get_master_core(mtlk_vap_get_manager(core->vap_handle));
  MTLK_ASSERT(NULL != master_core);

  /* update BSS load on/off for the current VAP through the master VAP serializer */
  _mtlk_process_hw_task(master_core, SERIALIZABLE, wave_beacon_man_beacon_update,
                        HANDLE_T(core), &value, 0); /* no need for parameters */

  return res;
}

static int
_mtlk_core_recovery_reliable_multicast (mtlk_core_t *core)
{
  uint8 flag = MTLK_CORE_PDB_GET_INT(core, PARAM_DB_CORE_RELIABLE_MCAST);

  if (MTLK_PARAM_DB_VALUE_IS_INVALID(flag))
    return MTLK_ERR_OK;

  return mtlk_core_set_reliable_multicast(core, flag);
}

static int
_mtlk_core_update_network_mode(mtlk_core_t *core, uint8 mode)
{
  if(mtlk_core_scan_is_running(core)) {
    ELOG_D("CID-%04x: Cannot set network mode while scan is running", mtlk_vap_get_oid(core->vap_handle));
    return MTLK_ERR_BUSY;
  }

  return mtlk_core_update_network_mode(core, mode);
}

static __INLINE int
_mtlk_core_set_nickname_by_cfg(mtlk_core_t *core, mtlk_gen_core_cfg_t *core_cfg)
{
  int res = wave_pdb_set_string(mtlk_vap_get_param_db(core->vap_handle),
                                PARAM_DB_CORE_NICK_NAME,
                                core_cfg->nickname);
  if (MTLK_ERR_OK == res) {
    ILOG2_DS("CID-%04x: Set NICKNAME to \"%s\"", mtlk_vap_get_oid(core->vap_handle),
        core_cfg->nickname);
  }
  return res;
}

int mtlk_core_set_essid_by_cfg(mtlk_core_t *core, mtlk_gen_core_cfg_t *core_cfg)
{
  int res = MTLK_CORE_PDB_SET_BINARY(core, PARAM_DB_CORE_ESSID, core_cfg->essid, strlen(core_cfg->essid));
  if (MTLK_ERR_OK != res) {
    ELOG_DD("CID-%04x: Can't store ESSID (err=%d)", mtlk_vap_get_oid(core->vap_handle), res);
  } else {
    ILOG2_DS("CID-%04x: Set ESSID to \"%s\"", mtlk_vap_get_oid(core->vap_handle), core_cfg->essid);
  }

  return res;
}

static int
_mtlk_core_set_radio_mode_req (mtlk_core_t *core, uint32 enable_radio)
{
  mtlk_txmm_msg_t           man_msg;
  mtlk_txmm_data_t          *man_entry = NULL;
  UMI_ENABLE_RADIO          *pEnableRadio = NULL;
  mtlk_vap_handle_t         vap_handle = core->vap_handle;
  wave_radio_t              *radio = wave_vap_radio_get(vap_handle);
  int res;

  MTLK_ASSERT(vap_handle);

  ILOG0_DD("CID-%04x:EnableRadio FW request: Set %d mode", mtlk_vap_get_oid(vap_handle), enable_radio);

  man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, mtlk_vap_get_txmm(vap_handle), NULL);
  if (NULL == man_entry) {
    ELOG_D("CID-%04x: No man entry available", mtlk_vap_get_oid(vap_handle));
    return MTLK_ERR_NO_RESOURCES;
  }

  man_entry->id = UM_MAN_ENABLE_RADIO_REQ;
  man_entry->payload_size = sizeof(UMI_ENABLE_RADIO);

  pEnableRadio = (UMI_ENABLE_RADIO *)(man_entry->payload);
  pEnableRadio->u32RadioOn = HOST_TO_MAC32(enable_radio);

  res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);
  if (MTLK_ERR_OK != res) {
    ELOG_DD("CID-%04x: Set Radio Enable failure (%i)", mtlk_vap_get_oid(vap_handle), res);
  } else {
    WAVE_RADIO_PDB_SET_INT(radio, PARAM_DB_RADIO_MODE_CURRENT, enable_radio);
  }

  mtlk_txmm_msg_cleanup(&man_msg);
  return res;
}

static int
_mtlk_core_check_and_set_radio_mode (mtlk_core_t *core, uint32 radio_mode)
{
  int net_state;
  struct mtlk_chan_def *cd = __wave_core_chandef_get(core);
  wave_radio_t *radio = wave_vap_radio_get(core->vap_handle);

  if (mtlk_core_is_block_tx_mode(core)) {
    ILOG1_V("waiting for beacons");
    return MTLK_ERR_OK;
  }

  if (mtlk_core_scan_is_running(core) ||
      mtlk_core_is_in_scan_mode(core)) {
    ILOG1_V("scan is running");
    return MTLK_ERR_OK;
  }

  if (ST_RSSI == __wave_core_chan_switch_type_get(core)) {
    ILOG1_V("Is in ST_RSSI channel switch mode");
    return MTLK_ERR_OK;
  }

  net_state = mtlk_core_get_net_state(core);
  if (NET_STATE_ACTIVATING > net_state) {
    ILOG1_D("net state: %d", net_state);
    return MTLK_ERR_OK;
  }
  if (!is_channel_certain(cd)) {
    ILOG1_V("channel is not certain");
    return MTLK_ERR_OK;
  }

  if (radio_mode == WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_MODE_CURRENT)) {
    ILOG1_S("Radio already %s", radio_mode ? "ON" : "OFF");
    return MTLK_ERR_OK;
  }

  return _mtlk_core_set_radio_mode_req(core, radio_mode);
}

static int
_mtlk_core_set_core_cfg (mtlk_handle_t hcore,
                         const void* data, uint32 data_size)
{
  int res = MTLK_ERR_OK;
  mtlk_core_t *core = (mtlk_core_t*)hcore;
  mtlk_gen_core_cfg_t *core_cfg = NULL;
  uint32 core_cfg_size;
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  core_cfg = mtlk_clpb_enum_get_next(clpb, &core_cfg_size);
  MTLK_CLPB_TRY(core_cfg, core_cfg_size)
  {
    MTLK_CFG_START_CHEK_ITEM_AND_CALL()

      MTLK_CFG_CHECK_ITEM_AND_CALL(core_cfg, bridge_mode, _mtlk_core_set_bridge_mode,
                                  (core, core_cfg->bridge_mode), res);
      MTLK_CFG_CHECK_ITEM_AND_CALL(core_cfg, reliable_multicast, mtlk_core_set_reliable_multicast,
                                  (core, !!core_cfg->reliable_multicast), res);
      MTLK_CFG_CHECK_ITEM_AND_CALL(core_cfg, up_rescan_exemption_time, _mtlk_core_set_up_rescan_exemption_time_sec,
                                   (core, core_cfg->up_rescan_exemption_time), res);

      MTLK_CFG_CHECK_ITEM_AND_CALL_VOID(core_cfg, ap_forwarding, wave_pdb_set_int,
                                        (mtlk_vap_get_param_db(core->vap_handle),
                                        PARAM_DB_CORE_AP_FORWARDING,
                                        !!core_cfg->ap_forwarding));

      MTLK_CFG_CHECK_ITEM_AND_CALL(core_cfg, net_mode, _mtlk_core_update_network_mode,
                                  (core, core_cfg->net_mode), res);

      MTLK_CFG_CHECK_ITEM_AND_CALL(core_cfg, bss_rate, _mtlk_core_set_bss_base_rate,
                                  (core, core_cfg->bss_rate), res);

      MTLK_CFG_CHECK_ITEM_AND_CALL(core_cfg, nickname, _mtlk_core_set_nickname_by_cfg, (core, core_cfg), res);

      MTLK_CFG_GET_ITEM_BY_FUNC_VOID(core_cfg, essid, mtlk_core_set_essid_by_cfg, (core, core_cfg));

      MTLK_CFG_GET_ITEM(core_cfg, is_hidden_ssid, core->slow_ctx->cfg.is_hidden_ssid);

      MTLK_CFG_CHECK_ITEM_AND_CALL(core_cfg, is_bss_load_enable, _mtlk_core_set_bss_load_enable,
                                  (core, core_cfg->is_bss_load_enable), res);

      MTLK_CFG_CHECK_ITEM_AND_CALL(core_cfg, admission_capacity, mtlk_core_set_admission_capacity,
                                  (core, core_cfg->admission_capacity), res);

    MTLK_CFG_END_CHEK_ITEM_AND_CALL()
  }
  MTLK_CLPB_FINALLY(res)
    return mtlk_clpb_push_res(clpb, res);
  MTLK_CLPB_END;
}

static int __MTLK_IFUNC
_mtlk_core_set_he_mu_operation(mtlk_core_t *core, BOOL he_mu_operation)
{
  mtlk_txmm_msg_t                 man_msg;
  mtlk_txmm_data_t               *man_entry = NULL;
  UMI_HE_MU_OPERATION_CONFIG     *mu_operation_config = NULL;
  mtlk_vap_handle_t               vap_handle = core->vap_handle;
  wave_radio_t                   *radio = wave_vap_radio_get(vap_handle);
  int                             res;

  ILOG2_V("Sending UMI_HE_MU_OPERATION_CONFIG");

  man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, mtlk_vap_get_txmm(vap_handle), NULL);
  if (NULL == man_entry) {
    ELOG_D("CID-%04x: No man entry available", mtlk_vap_get_oid(vap_handle));
    return MTLK_ERR_NO_RESOURCES;
  }

  man_entry->id = UM_MAN_HE_MU_OPERATION_CONFIG_REQ;
  man_entry->payload_size = sizeof(UMI_HE_MU_OPERATION_CONFIG);

  mu_operation_config = (UMI_HE_MU_OPERATION_CONFIG *)(man_entry->payload);
  mu_operation_config->enableHeMuOperation = he_mu_operation;

  res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);

  if (MTLK_ERR_OK != res) {
    ELOG_DD("CID-%04x: Set UMI_HE_MU_OPERATION_CONFIG failed (res = %d)", mtlk_vap_get_oid(vap_handle), res);
  }
  else {
    WAVE_RADIO_PDB_SET_INT(radio, PARAM_DB_RADIO_HE_MU_OPERATION, he_mu_operation);
  }

  mtlk_txmm_msg_cleanup(&man_msg);
  return res;
}

static int __MTLK_IFUNC
_mtlk_core_send_bf_mode (mtlk_core_t *core, BeamformingMode_e bf_mode)
{
  mtlk_txmm_msg_t                 man_msg;
  mtlk_txmm_data_t               *man_entry = NULL;
  UMI_BF_MODE_CONFIG             *bf_mode_config = NULL;
  mtlk_vap_handle_t               vap_handle = core->vap_handle;
  wave_radio_t                   *radio = wave_vap_radio_get(vap_handle);
  int                             res;

  if ((BF_NUMBER_OF_MODES <= bf_mode) &&
      (BF_STATE_AUTO_MODE != bf_mode)) {
    ELOG_DDDD("Wrong Beamforming mode: %u, must be from %u to %u or %u",
           bf_mode, BF_FIRST_STATE, BF_LAST_STATE, BF_STATE_AUTO_MODE);
    return MTLK_ERR_PARAMS;
  }

  ILOG2_V("Sending UM_MAN_BF_MODE_CONFIG_REQ");

  man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, mtlk_vap_get_txmm(vap_handle), NULL);

  if (NULL == man_entry) {
    ELOG_D("CID-%04x: No man entry available", mtlk_vap_get_oid(vap_handle));
    return MTLK_ERR_NO_RESOURCES;
  }

  man_entry->id = UM_MAN_BF_MODE_CONFIG_REQ;
  man_entry->payload_size = sizeof(UMI_BF_MODE_CONFIG);

  bf_mode_config = (UMI_BF_MODE_CONFIG *)(man_entry->payload);
  bf_mode_config->bfMode = bf_mode;

  res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);

  if (MTLK_ERR_OK != res) {
    ELOG_DD("CID-%04x: Sending UM_MAN_BF_MODE_CONFIG_REQ failed (res = %d)", mtlk_vap_get_oid(vap_handle), res);
  }
  else {
    WAVE_RADIO_PDB_SET_INT(radio, PARAM_DB_RADIO_BF_MODE, bf_mode);
  }

  mtlk_txmm_msg_cleanup(&man_msg);
  return res;
}

static int
_mtlk_core_send_fixed_pwr_cfg (mtlk_core_t *core, FixedPower_t *fixed_pwr_params)
{
  mtlk_txmm_msg_t     man_msg;
  mtlk_txmm_data_t   *man_entry = NULL;
  FixedPower_t       *fixed_pwr_cfg = NULL;
  mtlk_vap_handle_t   vap_handle = core->vap_handle;
  int                 res;

  ILOG2_V("Sending UM_MAN_FIXED_POWER_REQ");

  man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, mtlk_vap_get_txmm(vap_handle), NULL);
  if (NULL == man_entry) {
    ELOG_D("CID-%04x: No man entry available", mtlk_vap_get_oid(vap_handle));
    return MTLK_ERR_NO_RESOURCES;
  }

  man_entry->id = UM_MAN_FIXED_POWER_REQ;
  man_entry->payload_size = sizeof(FixedPower_t);

  fixed_pwr_cfg = (FixedPower_t *)(man_entry->payload);
  *fixed_pwr_cfg = *fixed_pwr_params;

  res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);
  if (MTLK_ERR_OK != res) {
    ELOG_DD("CID-%04x: Sending UM_MAN_FIXED_POWER_REQ failed (res = %d)", mtlk_vap_get_oid(vap_handle), res);
  }

  mtlk_txmm_msg_cleanup(&man_msg);
  return res;
}

static int
_mtlk_core_store_and_send_fixed_pwr_cfg (mtlk_core_t *core, FixedPower_t *fixed_pwr_params)
{
  wave_radio_t *radio = wave_vap_radio_get(core->vap_handle);
  MTLK_ASSERT(NULL != radio);

  if (MTLK_ERR_OK != WAVE_RADIO_PDB_SET_BINARY(radio, PARAM_DB_RADIO_FIXED_PWR, fixed_pwr_params, sizeof(*fixed_pwr_params)))
    ELOG_V("Failed to store Fixed TX management power parameters");

  return _mtlk_core_send_fixed_pwr_cfg(core, fixed_pwr_params);
}

static int
_mtlk_core_send_dynamic_mc_rate (mtlk_core_t *core, uint32 dynamic_mc_rate)
{
  mtlk_txmm_msg_t     man_msg;
  mtlk_txmm_data_t   *man_entry = NULL;
  UmiDmrConfig_t     *dmr_cfg = NULL;
  mtlk_vap_handle_t   vap_handle = core->vap_handle;
  int                 res;

  ILOG2_V("Sending UM_MAN_DMR_CONFIG_REQ");

  man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, mtlk_vap_get_txmm(vap_handle), NULL);
  if (NULL == man_entry) {
    ELOG_D("CID-%04x: No man entry available", mtlk_vap_get_oid(vap_handle));
    return MTLK_ERR_NO_RESOURCES;
  }

  man_entry->id = UM_MAN_DMR_CONFIG_REQ;
  man_entry->payload_size = sizeof(UmiDmrConfig_t);

  dmr_cfg = (UmiDmrConfig_t *)(man_entry->payload);
  dmr_cfg->dmrMode = HOST_TO_MAC32(dynamic_mc_rate);

  res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);
  if (MTLK_ERR_OK != res) {
    ELOG_DD("CID-%04x: Sending UM_MAN_DMR_CONFIG_REQ failed (res = %d)", mtlk_vap_get_oid(vap_handle), res);
  }

  mtlk_txmm_msg_cleanup(&man_msg);
  return res;
}

static int
_mtlk_core_store_and_send_dynamic_mc_rate (mtlk_core_t *core, uint32 dynamic_mc_rate)
{
  int res = MTLK_ERR_PARAMS;

  MTLK_ASSERT(core);

  if ((UMI_DMR_DISABLED != dynamic_mc_rate) &&
      (UMI_DMR_SUPPORTED_RATES != dynamic_mc_rate)) {
    ELOG_DDDD("CID-%04x: Wrong Dynamic multicast range value given (%d), must be %d or %d",
             mtlk_vap_get_oid(core->vap_handle), dynamic_mc_rate, UMI_DMR_DISABLED, UMI_DMR_SUPPORTED_RATES);
    return res;
  }

  WAVE_RADIO_PDB_SET_INT(wave_vap_radio_get(core->vap_handle), PARAM_DB_RADIO_DYNAMIC_MC_RATE, dynamic_mc_rate);
  return _mtlk_core_send_dynamic_mc_rate(core, dynamic_mc_rate);
}

static int
_mtlk_core_set_master_specific_cfg (mtlk_handle_t hcore,
                                    const void* data, uint32 data_size)
{
  uint32 res = MTLK_ERR_OK;
  mtlk_core_t *core = (mtlk_core_t*)hcore;
  wave_radio_t *radio = wave_vap_radio_get(core->vap_handle);
  mtlk_master_core_cfg_t *master_cfg = NULL;
  uint32 master_cfg_size;
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  master_cfg = mtlk_clpb_enum_get_next(clpb, &master_cfg_size);
  MTLK_CLPB_TRY(master_cfg, master_cfg_size)
  {
    MTLK_CFG_START_CHEK_ITEM_AND_CALL()

      MTLK_CFG_CHECK_ITEM_AND_CALL_VOID(master_cfg, acs_update_timeout, WAVE_RADIO_PDB_SET_INT,
                                       (radio, PARAM_DB_RADIO_ACS_UPDATE_TO, master_cfg->acs_update_timeout));

      MTLK_CFG_CHECK_ITEM_AND_CALL_VOID(master_cfg, txop_mode_params, mtlk_core_send_txop_mode,
                                       (core, master_cfg->txop_mode_params.sid, master_cfg->txop_mode_params.mode));

      MTLK_CFG_CHECK_ITEM_AND_CALL(master_cfg, mu_operation, mtlk_core_set_mu_operation,
                                  (core, !!master_cfg->mu_operation), res);

      MTLK_CFG_CHECK_ITEM_AND_CALL(master_cfg, he_mu_operation, _mtlk_core_set_he_mu_operation,
                                  (core, !!master_cfg->he_mu_operation), res);

      MTLK_CFG_CHECK_ITEM_AND_CALL(master_cfg, rts_mode_params, mtlk_core_set_rts_mode,
                                  (core, !!master_cfg->rts_mode_params.dynamic_bw, !!master_cfg->rts_mode_params.static_bw), res);

      MTLK_CFG_CHECK_ITEM_AND_CALL(master_cfg, bf_mode, _mtlk_core_send_bf_mode,
                                  (core, master_cfg->bf_mode), res);

      MTLK_CFG_CHECK_ITEM_AND_CALL(master_cfg, active_ant_mask, mtlk_core_cfg_send_active_ant_mask,
                                  (core, master_cfg->active_ant_mask), res);

      MTLK_CFG_CHECK_ITEM_AND_CALL(master_cfg, slow_probing_mask, mtlk_core_cfg_send_slow_probing_mask,
                                  (core, master_cfg->slow_probing_mask), res);

      MTLK_CFG_CHECK_ITEM_AND_CALL(master_cfg, fixed_pwr_params, _mtlk_core_store_and_send_fixed_pwr_cfg,
                                  (core, &master_cfg->fixed_pwr_params), res);

      MTLK_CFG_CHECK_ITEM_AND_CALL_VOID(master_cfg, unconnected_sta_scan_time, WAVE_RADIO_PDB_SET_INT,
                                        (radio, PARAM_DB_RADIO_UNCONNECTED_STA_SCAN_TIME,
                                         master_cfg->unconnected_sta_scan_time));

      MTLK_CFG_CHECK_ITEM_AND_CALL(master_cfg, fast_drop, mtlk_core_cfg_set_fast_drop,
                                   (core, master_cfg->fast_drop), res);

      MTLK_CFG_CHECK_ITEM_AND_CALL(master_cfg, erp_cfg, mtlk_core_send_erp_cfg,
                                  (core, &master_cfg->erp_cfg), res);

      MTLK_CFG_CHECK_ITEM_AND_CALL(master_cfg, dynamic_mc_rate, _mtlk_core_store_and_send_dynamic_mc_rate,
                                  (core, master_cfg->dynamic_mc_rate), res);


    MTLK_CFG_END_CHEK_ITEM_AND_CALL()
  }
  MTLK_CLPB_FINALLY(res)
    return mtlk_clpb_push_res(clpb, res);
  MTLK_CLPB_END;
}


#ifdef MTLK_LEGACY_STATISTICS
/* MHI_STATISTICS */

static int
_mtlk_core_get_mhi_stats (mtlk_handle_t hcore,
                          const void* data, uint32 data_size)
{
  mtlk_mhi_stats_t *mhi_stats;
  mtlk_core_t      *core = (mtlk_core_t*)hcore;
  mtlk_clpb_t      *clpb = *(mtlk_clpb_t **) data;
  mtlk_hw_t        *hw;
  uint32            size;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  mhi_stats = mtlk_clpb_enum_get_next(clpb, &size);
  if ((NULL == mhi_stats) || (sizeof(*mhi_stats) != size)) {
    ELOG_D("CID-%04x: Failed to get parameters from CLPB", mtlk_vap_get_oid(core->vap_handle));
    return MTLK_ERR_UNKNOWN;
  }

  hw = mtlk_vap_get_hw(core->vap_handle);
  MTLK_ASSERT(hw);

  return mtlk_hw_mhi_copy_stats(hw, mhi_stats->mhi_stats_data, &mhi_stats->mhi_stats_size);
}
#endif /* MTLK_LEGACY_STATISTICS */

/*------ PHY_RX_STATUS -------*/

static int
_mtlk_core_get_phy_rx_status (mtlk_handle_t hcore, const void* data, uint32 data_size)
{
  mtlk_core_t      *core = HANDLE_T_PTR(mtlk_core_t, hcore);
  mtlk_clpb_t      *clpb = *(mtlk_clpb_t **) data;
  wave_bin_data_t  *phy_rx_status;
  uint32            size;
  int               res;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  phy_rx_status = mtlk_clpb_enum_get_next(clpb, &size);
  MTLK_CLPB_TRY(phy_rx_status, size)
    res = mtlk_hw_copy_phy_rx_status(mtlk_vap_get_hw(core->vap_handle),
                                     phy_rx_status->buff, &phy_rx_status->size);
  MTLK_CLPB_FINALLY(res)
    return res;
  MTLK_CLPB_END;
}

static int
_mtlk_core_get_eeprom_cfg (mtlk_handle_t hcore,
                           const void* data, uint32 data_size)
{
  uint32 res = MTLK_ERR_OK;
  mtlk_eeprom_cfg_t* eeprom_cfg = mtlk_osal_mem_alloc(sizeof(mtlk_eeprom_cfg_t), MTLK_MEM_TAG_EEPROM);
  mtlk_core_t *core = (mtlk_core_t*)hcore;
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  if(NULL == eeprom_cfg) {
    return MTLK_ERR_NO_MEM;
  }

  memset(eeprom_cfg, 0, sizeof(mtlk_eeprom_cfg_t));

  MTLK_CFG_SET_ITEM_BY_FUNC_VOID(eeprom_cfg, eeprom_data, mtlk_eeprom_get_cfg,
                                 (mtlk_core_get_eeprom(core), &eeprom_cfg->eeprom_data));

  MTLK_CFG_SET_ITEM_BY_FUNC_VOID(eeprom_cfg, eeprom_raw_data, mtlk_eeprom_get_raw_data,
                                 (mtlk_vap_get_hwapi(core->vap_handle), eeprom_cfg));

  res = mtlk_clpb_push(clpb, &res, sizeof(res));
  if (MTLK_ERR_OK == res) {
    res = mtlk_clpb_push(clpb, eeprom_cfg, sizeof(mtlk_eeprom_cfg_t));
  }

  mtlk_osal_mem_free(eeprom_cfg);

  return res;
}

static int
_mtlk_core_get_hstdb_cfg (mtlk_handle_t hcore,
                          const void* data, uint32 data_size)
{
  uint32 res = MTLK_ERR_OK;
  mtlk_hstdb_cfg_t hstdb_cfg;
  mtlk_core_t *core = (mtlk_core_t*)hcore;
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  memset(&hstdb_cfg, 0, sizeof(hstdb_cfg));

  MTLK_CFG_SET_ITEM(&hstdb_cfg, wds_host_timeout, core->slow_ctx->hstdb.wds_host_timeout);
  MTLK_CFG_SET_ITEM_BY_FUNC_VOID(&hstdb_cfg, address, mtlk_hstdb_get_local_mac,
                                 (&core->slow_ctx->hstdb, hstdb_cfg.address.au8Addr));

  res = mtlk_clpb_push(clpb, &res, sizeof(res));
  if (MTLK_ERR_OK == res) {
    res = mtlk_clpb_push(clpb, &hstdb_cfg, sizeof(hstdb_cfg));
  }

  return res;
}

static int
_mtlk_core_set_hstdb_cfg (mtlk_handle_t hcore,
                          const void* data, uint32 data_size)
{
  uint32 res = MTLK_ERR_OK;
  mtlk_core_t *core = (mtlk_core_t*)hcore;
  mtlk_hstdb_cfg_t *hstdb_cfg = NULL;
  uint32 hstdb_cfg_size;
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  hstdb_cfg = mtlk_clpb_enum_get_next(clpb, &hstdb_cfg_size);
  MTLK_CLPB_TRY(hstdb_cfg, hstdb_cfg_size)
  {
    MTLK_CFG_START_CHEK_ITEM_AND_CALL()
      MTLK_CFG_GET_ITEM(hstdb_cfg, wds_host_timeout, core->slow_ctx->hstdb.wds_host_timeout);
      MTLK_CFG_CHECK_ITEM_AND_CALL(hstdb_cfg, address, mtlk_hstdb_set_local_mac,
                                   (&core->slow_ctx->hstdb, hstdb_cfg->address.au8Addr), res);
    MTLK_CFG_END_CHEK_ITEM_AND_CALL()
  }
  MTLK_CLPB_FINALLY(res)
    return mtlk_clpb_push_res(clpb, res);
  MTLK_CLPB_END;
}

static int
_mtlk_core_simple_cli (mtlk_handle_t hcore, const void* data, uint32 data_size)
{
  mtlk_txmm_msg_t         man_msg;
  mtlk_txmm_data_t*       man_entry = NULL;
  UmiDbgCliReq_t          *mac_msg;
  mtlk_dbg_cli_cfg_t      *UmiDbgCliReq;
  int                     res = MTLK_ERR_OK;

  mtlk_core_t *core = HANDLE_T_PTR(mtlk_core_t, hcore);
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;
  uint32 clpb_data_size;
  mtlk_dbg_cli_cfg_t* clpb_data;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  clpb_data = mtlk_clpb_enum_get_next(clpb, &clpb_data_size);
  MTLK_CLPB_TRY(clpb_data, clpb_data_size)
    UmiDbgCliReq = clpb_data;

    ILOG2_DDDDD("Simple CLI: Action %d, data1 %d, data2 %d, data3 %d, numOfArgs %d",
      UmiDbgCliReq->DbgCliReq.action, UmiDbgCliReq->DbgCliReq.data1, UmiDbgCliReq->DbgCliReq.data2,
      UmiDbgCliReq->DbgCliReq.data3, UmiDbgCliReq->DbgCliReq.numOfArgumets);

    man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, mtlk_vap_get_txdm(core->vap_handle), NULL);
    if (!man_entry) {
      ELOG_D("CID-%04x: Can not get TXMM slot", mtlk_vap_get_oid(core->vap_handle));
      MTLK_CLPB_EXIT(MTLK_ERR_NO_RESOURCES);
    }

    man_entry->id = UM_DBG_CLI_REQ;
    man_entry->payload_size = sizeof(UmiDbgCliReq_t);
    mac_msg = (UmiDbgCliReq_t *)man_entry->payload;

    mac_msg->action         = HOST_TO_MAC32(UmiDbgCliReq->DbgCliReq.action);
    mac_msg->numOfArgumets = HOST_TO_MAC32(UmiDbgCliReq->DbgCliReq.numOfArgumets);
    mac_msg->data1          = HOST_TO_MAC32(UmiDbgCliReq->DbgCliReq.data1);
    mac_msg->data2          = HOST_TO_MAC32(UmiDbgCliReq->DbgCliReq.data2);
    mac_msg->data3          = HOST_TO_MAC32(UmiDbgCliReq->DbgCliReq.data3);

    res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);
    mtlk_txmm_msg_cleanup(&man_msg);

    if (MTLK_ERR_OK != res) {
      ELOG_DD("CID-%04x: DBG_CLI failed (res=%d)", mtlk_vap_get_oid(core->vap_handle), res);
    }
  MTLK_CLPB_FINALLY(res)
    return mtlk_clpb_push_res(clpb, res);
  MTLK_CLPB_END
}

int __MTLK_IFUNC
_mtlk_core_fw_assert (mtlk_core_t *core, UMI_FW_DBG_REQ *req_msg)
{
  mtlk_txmm_msg_t     man_msg;
  mtlk_txmm_data_t*   man_entry = NULL;
  UMI_FW_DBG_REQ      *mac_msg;
  int                 res = MTLK_ERR_OK;

  man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, mtlk_vap_get_txdm(core->vap_handle), NULL);
  if (!man_entry) {
    ELOG_D("CID-%04x: Can not get TXMM slot", mtlk_vap_get_oid(core->vap_handle));
    return MTLK_ERR_NO_RESOURCES;
  }

  man_entry->id = UM_DBG_FW_DBG_REQ;
  man_entry->payload_size = sizeof(UMI_FW_DBG_REQ);
  mac_msg = (UMI_FW_DBG_REQ *)man_entry->payload;

  MTLK_STATIC_ASSERT(sizeof(mac_msg->debugType) == sizeof(uint8));
  mac_msg->debugType = req_msg->debugType;

  res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);
  mtlk_txmm_msg_cleanup(&man_msg);

  if (MTLK_ERR_OK != res) {
    ELOG_DD("CID-%04x: FW Debug message failed (res=%d)",
            mtlk_vap_get_oid(core->vap_handle),
            res);
  }

  return res;
}

BOOL __MTLK_IFUNC
mtlk_core_rcvry_is_running (mtlk_core_t *core)
{
  return (RCVRY_TYPE_UNDEF != wave_rcvry_type_current_get(mtlk_vap_get_hw(core->vap_handle)));
}

static int
_mtlk_core_fw_debug (mtlk_handle_t hcore, const void* data, uint32 data_size)
{
  mtlk_fw_debug_cfg_t *UmiFWDebugReq;
  int                 res = MTLK_ERR_OK;

  mtlk_core_t *core = HANDLE_T_PTR(mtlk_core_t, hcore);
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;
  uint32 clpb_data_size;
  mtlk_fw_debug_cfg_t* clpb_data;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  clpb_data = mtlk_clpb_enum_get_next(clpb, &clpb_data_size);
  MTLK_CLPB_TRY(clpb_data, clpb_data_size)
  {
    UmiFWDebugReq = (mtlk_fw_debug_cfg_t*)clpb_data;

    ILOG2_DD("CID-%04x: FW debug type: %d",
             mtlk_vap_get_oid(core->vap_handle),
             UmiFWDebugReq->FWDebugReq.debugType);

    wave_rcvry_set_to_dbg_mode(core->vap_handle);
    res = _mtlk_core_fw_assert(core, &UmiFWDebugReq->FWDebugReq);
  }
  MTLK_CLPB_FINALLY(res)
    return mtlk_clpb_push_res(clpb, res);
  MTLK_CLPB_END;
}

static __INLINE int
_mtlk_core_set_radar_detect (mtlk_core_t *core, uint32 radar_detect)
{
  mtlk_txmm_msg_t                 man_msg;
  mtlk_txmm_data_t               *man_entry = NULL;
  UMI_ENABLE_RADAR_INDICATION *radar_ind_cfg = NULL;
  mtlk_vap_handle_t               vap_handle = core->vap_handle;
  int                             res;
  wave_radio_t                   *radio;

  MTLK_ASSERT(vap_handle);

  ILOG1_V("Sending UMI_ENABLE_RADAR_INDICATION");

  radio = wave_vap_radio_get(vap_handle);

  man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, mtlk_vap_get_txmm(vap_handle), NULL);
  if (NULL == man_entry) {
    ELOG_D("CID-%04x: No man entry available", mtlk_vap_get_oid(vap_handle));
    return MTLK_ERR_NO_RESOURCES;
  }

  man_entry->id = UM_MAN_ENABLE_RADAR_INDICATION_REQ;
  man_entry->payload_size = sizeof(UMI_ENABLE_RADAR_INDICATION);

  radar_ind_cfg = (UMI_ENABLE_RADAR_INDICATION *)(man_entry->payload);
  radar_ind_cfg->enableIndication = radar_detect;

  res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);
  if (MTLK_ERR_OK != res) {
    ELOG_DD("CID-%04x: Set UMI_ENABLE_RADAR_INDICATION failed (res = %i)", mtlk_vap_get_oid(vap_handle), res);
  } else {
    WAVE_RADIO_PDB_SET_INT(wave_vap_radio_get(vap_handle), PARAM_DB_RADIO_DFS_RADAR_DETECTION, radar_detect);
  }

  mtlk_txmm_msg_cleanup(&man_msg);
  return res;
}

static int
_mtlk_core_emulate_radar_event (mtlk_handle_t hcore, uint8 rbm)
{
  int res;
  mtlk_core_radar_emu_t radar_emu;
  mtlk_core_t *core = (mtlk_core_t*)hcore;
  struct mtlk_chan_def *ccd = __wave_core_chandef_get(core);
  int cur_channel = ieee80211_frequency_to_channel(ccd->chan.center_freq);

  if (!is_channel_certain(ccd)) {
    ELOG_V("Can not emulate radar, channel is not certain");
    return MTLK_ERR_UNKNOWN;
  }

  if (!cur_channel) {
    ELOG_D("Could not find channel for frequency %d", ccd->chan.center_freq);
    return MTLK_ERR_UNKNOWN;
  }

  memset(&radar_emu, 0, sizeof(radar_emu));

  radar_emu.radar_det.channel = cur_channel;
  radar_emu.radar_det.subBandBitmap = rbm;

  res = _handle_radar_event(hcore, &radar_emu, sizeof(radar_emu));

  return res;
}

static int
_mtlk_core_set_scan_and_calib_cfg (mtlk_handle_t hcore, const void* data, uint32 data_size)
{
  uint32 res = MTLK_ERR_OK;
  mtlk_core_t *core = (mtlk_core_t*)hcore;
  wave_radio_t *radio = wave_vap_radio_get(core->vap_handle);

  mtlk_scan_and_calib_cfg_t *scan_and_calib_cfg;
  uint32 clpb_data_size;
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  scan_and_calib_cfg = mtlk_clpb_enum_get_next(clpb, &clpb_data_size);
  MTLK_CLPB_TRY(scan_and_calib_cfg, clpb_data_size)
    MTLK_CFG_START_CHEK_ITEM_AND_CALL()
    MTLK_CFG_CHECK_ITEM_AND_CALL_VOID(scan_and_calib_cfg, scan_modifs, WAVE_RADIO_PDB_SET_INT,
                                      (radio, PARAM_DB_RADIO_SCAN_MODIFS, scan_and_calib_cfg->scan_modifs));
    MTLK_CFG_CHECK_ITEM_AND_CALL(scan_and_calib_cfg, scan_params, WAVE_RADIO_PDB_SET_BINARY,
                                 (radio, PARAM_DB_RADIO_SCAN_PARAMS, &scan_and_calib_cfg->scan_params,
                                   sizeof(iwpriv_scan_params_t)), res);
    MTLK_CFG_CHECK_ITEM_AND_CALL(scan_and_calib_cfg, scan_params_bg, WAVE_RADIO_PDB_SET_BINARY,
                                 (radio, PARAM_DB_RADIO_SCAN_PARAMS_BG, &scan_and_calib_cfg->scan_params_bg,
                                   sizeof(iwpriv_scan_params_bg_t)), res);
    MTLK_CFG_CHECK_ITEM_AND_CALL(scan_and_calib_cfg, calib_cw_masks, WAVE_RADIO_PDB_SET_BINARY,
                                 (radio, PARAM_DB_RADIO_CALIB_CW_MASKS, &scan_and_calib_cfg->calib_cw_masks,
                                   sizeof(uint32) * NUM_IWPRIV_CALIB_CW_MASKS), res);
      MTLK_CFG_CHECK_ITEM_AND_CALL(scan_and_calib_cfg, rbm, _mtlk_core_emulate_radar_event,
                                   (hcore, scan_and_calib_cfg->rbm), res);
      MTLK_CFG_CHECK_ITEM_AND_CALL(scan_and_calib_cfg, radar_detect, _mtlk_core_set_radar_detect,
                                   (core, !!scan_and_calib_cfg->radar_detect), res);
      MTLK_CFG_CHECK_ITEM_AND_CALL_VOID(scan_and_calib_cfg, scan_expire_time, wv_cfg80211_set_scan_expire_time,
                                        (mtlk_df_user_get_wdev(mtlk_df_get_user(mtlk_vap_get_df(core->vap_handle))),
                                        scan_and_calib_cfg->scan_expire_time));
    MTLK_CFG_END_CHEK_ITEM_AND_CALL()
  MTLK_CLPB_FINALLY(res)
    return mtlk_clpb_push_res(clpb, res);
  MTLK_CLPB_END
}

static int
_mtlk_core_get_scan_and_calib_cfg (mtlk_handle_t hcore, const void* data, uint32 data_size)
{
  uint32 res = MTLK_ERR_OK;
  mtlk_pdb_size_t scan_params_size = sizeof(iwpriv_scan_params_t);
  mtlk_pdb_size_t scan_params_bg_size = sizeof(iwpriv_scan_params_bg_t);
  mtlk_pdb_size_t calib_cw_masks_size = sizeof(uint32) * NUM_IWPRIV_CALIB_CW_MASKS;
  mtlk_core_t *core = (mtlk_core_t*)hcore;
  wave_radio_t *radio = wave_vap_radio_get(core->vap_handle);
  mtlk_scan_and_calib_cfg_t scan_and_calib_cfg;
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  memset(&scan_and_calib_cfg, 0, sizeof(scan_and_calib_cfg));

  MTLK_CFG_SET_ITEM(&scan_and_calib_cfg, scan_modifs, WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_SCAN_MODIFS));
  MTLK_CFG_SET_ITEM_BY_FUNC(&scan_and_calib_cfg, scan_params, WAVE_RADIO_PDB_GET_BINARY,
                            (radio, PARAM_DB_RADIO_SCAN_PARAMS, &scan_and_calib_cfg.scan_params,
                            &scan_params_size), res);
  MTLK_CFG_SET_ITEM_BY_FUNC(&scan_and_calib_cfg, scan_params_bg, WAVE_RADIO_PDB_GET_BINARY,
                            (radio, PARAM_DB_RADIO_SCAN_PARAMS_BG, &scan_and_calib_cfg.scan_params_bg,
                            &scan_params_bg_size), res);
  MTLK_CFG_SET_ITEM_BY_FUNC(&scan_and_calib_cfg, calib_cw_masks, WAVE_RADIO_PDB_GET_BINARY,
                            (radio, PARAM_DB_RADIO_CALIB_CW_MASKS, scan_and_calib_cfg.calib_cw_masks,
                            &calib_cw_masks_size), res);
  MTLK_CFG_SET_ITEM(&scan_and_calib_cfg, radar_detect, WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_DFS_RADAR_DETECTION));
  if (mtlk_vap_is_ap(core->vap_handle)){
    MTLK_CFG_SET_ITEM(&scan_and_calib_cfg, scan_expire_time,
                     wv_cfg80211_get_scan_expire_time(mtlk_df_user_get_wdev(mtlk_df_get_user(mtlk_vap_get_df(core->vap_handle)))));
  } /* TODO MAC80211: handle set/get bss scan expire time for sta mode interface - is this required? */
  res = mtlk_clpb_push(clpb, &res, sizeof(res));
  if (MTLK_ERR_OK == res)
    res = mtlk_clpb_push(clpb, &scan_and_calib_cfg, sizeof(scan_and_calib_cfg));

  return res;
}

static int
_mtlk_core_get_coc_antenna_params (mtlk_core_t *core, mtlk_coc_antenna_cfg_t *antenna_params)
{
  mtlk_coc_antenna_cfg_t *current_params;
  mtlk_coc_t *coc_mgmt = __wave_core_coc_mgmt_get(core);

  MTLK_ASSERT(core != NULL);
  MTLK_ASSERT(antenna_params != NULL);

  current_params = mtlk_coc_get_current_params(coc_mgmt);
  *antenna_params = *current_params;

  return MTLK_ERR_OK;
}

static unsigned
_mtlk_core_get_current_tx_antennas (mtlk_core_t *core)
{
  mtlk_coc_antenna_cfg_t *current_params;
  mtlk_coc_t *coc_mgmt = __wave_core_coc_mgmt_get(core);

  MTLK_ASSERT(core != NULL);

  current_params = mtlk_coc_get_current_params(coc_mgmt);

  return current_params->num_tx_antennas;
}

static int
_mtlk_core_get_coc_auto_params (mtlk_core_t *core, mtlk_coc_auto_cfg_t *auto_params)
{
  mtlk_coc_auto_cfg_t *configured_params;
  mtlk_coc_t *coc_mgmt = __wave_core_coc_mgmt_get(core);

  MTLK_ASSERT(core != NULL);
  MTLK_ASSERT(auto_params != NULL);

  configured_params = mtlk_coc_get_auto_params(coc_mgmt);
  *auto_params = *configured_params;

  return MTLK_ERR_OK;
}

static int
_mtlk_core_get_coc_cfg (mtlk_handle_t hcore, const void* data, uint32 data_size)
{
  uint32 res = MTLK_ERR_OK;
  mtlk_coc_mode_cfg_t coc_cfg;
  mtlk_core_t *core = (mtlk_core_t*)hcore;
  wave_radio_t *radio;
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;
  mtlk_coc_t *coc_mgmt = __wave_core_coc_mgmt_get(core);

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);
  MTLK_ASSERT(!mtlk_vap_is_slave_ap(core->vap_handle));

  radio = wave_vap_radio_get(core->vap_handle);
  MTLK_ASSERT(radio);

  memset(&coc_cfg, 0, sizeof(coc_cfg));

  MTLK_CFG_SET_ITEM(&coc_cfg, is_auto_mode, mtlk_coc_is_auto_mode(coc_mgmt));
  MTLK_CFG_SET_ITEM_BY_FUNC(&coc_cfg, antenna_params,
                            _mtlk_core_get_coc_antenna_params, (core, &coc_cfg.antenna_params), res);
  MTLK_CFG_SET_ITEM_BY_FUNC(&coc_cfg, auto_params,
                            _mtlk_core_get_coc_auto_params, (core, &coc_cfg.auto_params), res);
  MTLK_CFG_SET_ITEM(&coc_cfg, cur_ant_mask, wave_radio_current_antenna_mask_get(radio));
  MTLK_CFG_SET_ITEM(&coc_cfg, hw_ant_mask,  wave_radio_rx_antenna_mask_get(radio));

  res = mtlk_clpb_push(clpb, &res, sizeof(res));
  if (MTLK_ERR_OK == res) {
    res = mtlk_clpb_push(clpb, &coc_cfg, sizeof(coc_cfg));
  }

  return res;
}

static int
_mtlk_core_set_coc_power_mode (mtlk_core_t *core, BOOL is_auto_mode)
{
  int res = MTLK_ERR_OK;
  mtlk_coc_t *coc_mgmt = __wave_core_coc_mgmt_get(core);

  res = mtlk_coc_set_power_mode(coc_mgmt, is_auto_mode);

  return res;
}

int __MTLK_IFUNC
mtlk_core_set_coc_actual_power_mode(mtlk_core_t *core)
{
  mtlk_coc_t *coc_mgmt = __wave_core_coc_mgmt_get(core);
  return mtlk_coc_set_actual_power_mode(coc_mgmt);
}

static int
_mtlk_core_set_antenna_params (mtlk_core_t *core, mtlk_coc_antenna_cfg_t *antenna_params)
{
  int res = MTLK_ERR_OK;
  mtlk_coc_t *coc_mgmt = __wave_core_coc_mgmt_get(core);
  res = mtlk_coc_set_antenna_params(coc_mgmt, antenna_params);

  return res;
}

static int
_mtlk_core_set_auto_params (mtlk_core_t *core, mtlk_coc_auto_cfg_t *auto_params)
{
  int res = MTLK_ERR_OK;
  mtlk_coc_t *coc_mgmt = __wave_core_coc_mgmt_get(core);

  res = mtlk_coc_set_auto_params(coc_mgmt, auto_params);

  return res;
}

static int
_mtlk_core_set_coc_cfg (mtlk_handle_t hcore, const void* data, uint32 data_size)
{
  uint32 res = MTLK_ERR_OK;
  mtlk_core_t *core = (mtlk_core_t*)hcore;
  mtlk_coc_mode_cfg_t *coc_cfg = NULL;
  uint32 coc_cfg_size;
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  coc_cfg = mtlk_clpb_enum_get_next(clpb, &coc_cfg_size);
  MTLK_CLPB_TRY(coc_cfg, coc_cfg_size)
    MTLK_CFG_START_CHEK_ITEM_AND_CALL()
      MTLK_CFG_CHECK_ITEM_AND_CALL(coc_cfg, auto_params, _mtlk_core_set_auto_params,
                                   (core, &coc_cfg->auto_params), res);
      MTLK_CFG_CHECK_ITEM_AND_CALL(coc_cfg, antenna_params, _mtlk_core_set_antenna_params,
                                   (core, &coc_cfg->antenna_params), res);
      MTLK_CFG_CHECK_ITEM_AND_CALL(coc_cfg, is_auto_mode, _mtlk_core_set_coc_power_mode,
                                   (core, coc_cfg->is_auto_mode), res);
    MTLK_CFG_END_CHEK_ITEM_AND_CALL()
  MTLK_CLPB_FINALLY(res)
    return mtlk_clpb_push_res(clpb, res);
  MTLK_CLPB_END
}



static int
_mtlk_core_get_tasklet_limits (mtlk_handle_t hcore, const void* data, uint32 data_size)
{
  mtlk_core_t *core = (mtlk_core_t *) hcore;
  mtlk_hw_api_t *hw_api = mtlk_vap_get_hwapi(core->vap_handle);
  mtlk_tasklet_limits_cfg_t tl_cfg;
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;
  uint32 res = MTLK_ERR_OK;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);
  MTLK_ASSERT(!mtlk_vap_is_slave_ap(core->vap_handle));

  memset(&tl_cfg, 0, sizeof(tl_cfg));

  res |= mtlk_hw_get_prop(hw_api, MTLK_HW_DATA_TXOUT_LIM, &tl_cfg.tl.data_txout_lim, sizeof(tl_cfg.tl.data_txout_lim));
  res |= mtlk_hw_get_prop(hw_api, MTLK_HW_DATA_RX_LIM, &tl_cfg.tl.data_rx_lim, sizeof(tl_cfg.tl.data_rx_lim));
  res |= mtlk_hw_get_prop(hw_api, MTLK_HW_BSS_RX_LIM, &tl_cfg.tl.bss_rx_lim, sizeof(tl_cfg.tl.bss_rx_lim));
  res |= mtlk_hw_get_prop(hw_api, MTLK_HW_BSS_CFM_LIM, &tl_cfg.tl.bss_cfm_lim, sizeof(tl_cfg.tl.bss_cfm_lim));
  res |= mtlk_hw_get_prop(hw_api, MTLK_HW_LEGACY_LIM, &tl_cfg.tl.legacy_lim, sizeof(tl_cfg.tl.legacy_lim));

  if (MTLK_ERR_OK != res)
    ELOG_D("CID-%04x: Can't get tasklet_limits", mtlk_vap_get_oid(core->vap_handle));
  else
    tl_cfg.tl_filled = 1;

  res = mtlk_clpb_push(clpb, &res, sizeof(res));

  if (MTLK_ERR_OK == res)
    res = mtlk_clpb_push(clpb, &tl_cfg, sizeof(tl_cfg));

  return res;
}

static int
_mtlk_core_set_tasklet_limits (mtlk_handle_t hcore, const void* data, uint32 data_size)
{
  uint32 res = MTLK_ERR_OK;
  mtlk_core_t *core = (mtlk_core_t *) hcore;
  mtlk_hw_api_t *hw_api = mtlk_vap_get_hwapi(core->vap_handle);
  mtlk_tasklet_limits_cfg_t *tl_cfg = NULL;
  uint32 tl_cfg_size;
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  tl_cfg = mtlk_clpb_enum_get_next(clpb, &tl_cfg_size);
  MTLK_CLPB_TRY(tl_cfg, tl_cfg_size)
    if (!tl_cfg->tl_filled)
      MTLK_CLPB_EXIT(MTLK_ERR_UNKNOWN);

    res |= mtlk_hw_set_prop(hw_api, MTLK_HW_DATA_TXOUT_LIM, &tl_cfg->tl.data_txout_lim, sizeof(tl_cfg->tl.data_txout_lim));
    res |= mtlk_hw_set_prop(hw_api, MTLK_HW_DATA_RX_LIM, &tl_cfg->tl.data_rx_lim, sizeof(tl_cfg->tl.data_rx_lim));
    res |= mtlk_hw_set_prop(hw_api, MTLK_HW_BSS_RX_LIM, &tl_cfg->tl.bss_rx_lim, sizeof(tl_cfg->tl.bss_rx_lim));
    res |= mtlk_hw_set_prop(hw_api, MTLK_HW_BSS_CFM_LIM, &tl_cfg->tl.bss_cfm_lim, sizeof(tl_cfg->tl.bss_cfm_lim));
    res |= mtlk_hw_set_prop(hw_api, MTLK_HW_LEGACY_LIM, &tl_cfg->tl.legacy_lim, sizeof(tl_cfg->tl.legacy_lim));

    if (MTLK_ERR_OK != res)
      ELOG_D("CID-%04x: Can't set tasklet_limits", mtlk_vap_get_oid(core->vap_handle));
  MTLK_CLPB_FINALLY(res)
    return mtlk_clpb_push_res(clpb, res);
  MTLK_CLPB_END
}

/************* AGG rate limit *******************/

static int _mtlk_core_receive_agg_rate_limit (mtlk_core_t *core, mtlk_agg_rate_limit_req_cfg_t *arl_cfg)
{
  int                 res;
  mtlk_txmm_msg_t     man_msg;
  mtlk_txmm_data_t   *man_entry;
  UMI_AGG_RATE_LIMIT *mac_msg;

  man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, mtlk_vap_get_txmm(core->vap_handle), NULL);
  if (!man_entry) {
    ELOG_D("CID-%04x: Can not get TXMM slot", mtlk_vap_get_oid(core->vap_handle));
    return MTLK_ERR_NO_RESOURCES;
  }

  man_entry->id = UM_MAN_SET_AGG_RATE_LIMIT_REQ;
  man_entry->payload_size = sizeof(UMI_AGG_RATE_LIMIT);
  mac_msg = (UMI_AGG_RATE_LIMIT *)man_entry->payload;
  mac_msg->getSetOperation = API_GET_OPERATION;

  res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);

  if (MTLK_ERR_OK == res) {
    arl_cfg->mode    = mac_msg->mode;
    arl_cfg->maxRate = MAC_TO_HOST16(mac_msg->maxRate);
  }
  else {
    ELOG_D("CID-%04x: Failed to receive UM_MAN_SET_AGG_RATE_LIMIT_REQ", mtlk_vap_get_oid(core->vap_handle));
  }

  mtlk_txmm_msg_cleanup(&man_msg);
  return res;
}

static void
_mtlk_core_read_agg_rate_limit (mtlk_core_t *core, mtlk_agg_rate_limit_cfg_t *agg_rate_cfg)
{
    wave_radio_t *radio;
    MTLK_ASSERT(core != NULL);
    MTLK_ASSERT(agg_rate_cfg != NULL);
    radio = wave_vap_radio_get(core->vap_handle);

    agg_rate_cfg->agg_rate_limit.mode    = (uint8)  WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_AGG_RATE_LIMIT_MODE);
    agg_rate_cfg->agg_rate_limit.maxRate = (uint16) WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_AGG_RATE_LIMIT_MAXRATE);
}

static void
_mtlk_core_store_agg_rate_limit (mtlk_core_t *core, const mtlk_agg_rate_limit_cfg_t *agg_rate_cfg)
{
    wave_radio_t *radio;
    MTLK_ASSERT(core != NULL);
    MTLK_ASSERT(agg_rate_cfg != NULL);
    radio = wave_vap_radio_get(core->vap_handle);

    WAVE_RADIO_PDB_SET_INT(radio, PARAM_DB_RADIO_AGG_RATE_LIMIT_MODE, agg_rate_cfg->agg_rate_limit.mode);
    WAVE_RADIO_PDB_SET_INT(radio, PARAM_DB_RADIO_AGG_RATE_LIMIT_MAXRATE, agg_rate_cfg->agg_rate_limit.maxRate);
}


int __MTLK_IFUNC
mtlk_core_set_agg_rate_limit (mtlk_core_t *core, mtlk_agg_rate_limit_cfg_t *agg_rate_cfg)
{
    mtlk_txmm_msg_t         man_msg;
    mtlk_txmm_data_t        *man_entry;
    UMI_AGG_RATE_LIMIT      *mac_msg;
    int                     res;

    MTLK_ASSERT((0==agg_rate_cfg->agg_rate_limit.mode) || (1==agg_rate_cfg->agg_rate_limit.mode));

    ILOG2_DDD("CID-%04x: Set aggregation-rate limit. Mode: %u, maxRate: %u",
            mtlk_vap_get_oid(core->vap_handle), agg_rate_cfg->agg_rate_limit.mode, agg_rate_cfg->agg_rate_limit.maxRate);

    /* allocate a new message */
    man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, mtlk_vap_get_txmm(core->vap_handle), NULL);
    if (!man_entry)
    {
        ELOG_D("CID-%04x: Can not get TXMM slot", mtlk_vap_get_oid(core->vap_handle));
        return MTLK_ERR_NO_RESOURCES;
    }

    /* fill the message data */
    man_entry->id = UM_MAN_SET_AGG_RATE_LIMIT_REQ;
    man_entry->payload_size = sizeof(UMI_AGG_RATE_LIMIT);
    mac_msg = (UMI_AGG_RATE_LIMIT *)man_entry->payload;
    mac_msg->mode = agg_rate_cfg->agg_rate_limit.mode;
    mac_msg->maxRate = HOST_TO_MAC16(agg_rate_cfg->agg_rate_limit.maxRate);

    /* send the message to FW */
    res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);

    /* cleanup the message */
    mtlk_txmm_msg_cleanup(&man_msg);

    return res;
}

static int
_mtlk_core_set_agg_rate_limit (mtlk_handle_t hcore, const void* data, uint32 data_size)
{
    int res = MTLK_ERR_OK;
    mtlk_core_t *core = (mtlk_core_t *) hcore;

    mtlk_agg_rate_limit_cfg_t *agg_cfg = NULL;
    uint32 agg_cfg_size;
    mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;

    MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

    /* get configuration */
    agg_cfg = mtlk_clpb_enum_get_next(clpb, &agg_cfg_size);
    MTLK_CLPB_TRY(agg_cfg, agg_cfg_size)
      MTLK_CFG_START_CHEK_ITEM_AND_CALL()
        /* send new config to FW */
        MTLK_CFG_CHECK_ITEM_AND_CALL(agg_cfg, agg_rate_limit, mtlk_core_set_agg_rate_limit,
                                     (core, agg_cfg), res);
        /* store new config in internal db*/
        MTLK_CFG_CHECK_ITEM_AND_CALL_VOID(agg_cfg, agg_rate_limit, _mtlk_core_store_agg_rate_limit,
                                          (core, agg_cfg));
      MTLK_CFG_END_CHEK_ITEM_AND_CALL()
    MTLK_CLPB_FINALLY(res)
      /* push result into clipboard */
      return mtlk_clpb_push_res(clpb, res);
    MTLK_CLPB_END
}

static int
_mtlk_core_get_agg_rate_limit (mtlk_handle_t hcore, const void* data, uint32 data_size)
{
    int res = MTLK_ERR_OK;
    mtlk_agg_rate_limit_cfg_t agg_cfg;
    mtlk_core_t *core = (mtlk_core_t*)hcore;
    mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;

    MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

    /* read config from internal db */
    memset(&agg_cfg, 0, sizeof(agg_cfg));
    MTLK_CFG_SET_ITEM_BY_FUNC(&agg_cfg, agg_rate_limit, _mtlk_core_receive_agg_rate_limit,
                              (core, &agg_cfg.agg_rate_limit), res)

    /* push result and config to clipboard*/
    res = mtlk_clpb_push(clpb, &res, sizeof(res));
    if (MTLK_ERR_OK == res) {
        res = mtlk_clpb_push(clpb, &agg_cfg, sizeof(agg_cfg));
    }

    return res;
}

/************* TX Power Limit configuration ******************/
static int
_mtlk_core_send_tx_power_limit_offset (mtlk_core_t *core, const uint32 power_limit)
{
    mtlk_txmm_msg_t         man_msg;
    mtlk_txmm_data_t        *man_entry;
    UMI_TX_POWER_LIMIT      *mac_msg;
    wave_radio_t            *radio = wave_vap_radio_get(core->vap_handle);
    int                     res;

    MTLK_ASSERT(((power_limit == 0) ||
                 (power_limit == 3) ||
                 (power_limit == 6) ||
                 (power_limit == 9)));

    /* Power settings are determined by peer AP configuration. Therefore TX power cannot be set
     * in case there are station mode interfaces unless FW recovery is in progress */
    if (wave_radio_get_sta_vifs_exist(radio) && !mtlk_core_rcvry_is_running(core)) {
      WLOG_V("Setting TX power limit is disabled while sta vifs exists");
      return MTLK_ERR_NOT_SUPPORTED;
    }

    if (mtlk_vap_is_ap(core->vap_handle) && ((wave_radio_sta_cnt_get(radio) > 0) && !mtlk_core_rcvry_is_running(core))) {
      WLOG_V("Setting TX power limit is disabled while connected STA or peer AP exists");
      return MTLK_ERR_NOT_SUPPORTED;
    }

    ILOG2_DD("CID-%04x: Set TX Power Limit. powerLimitOffset: %u",
            mtlk_vap_get_oid(core->vap_handle), power_limit);

    /* allocate a new message */
    man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, mtlk_vap_get_txmm(core->vap_handle), NULL);
    if (!man_entry)
    {
        ELOG_D("CID-%04x: Can not get TXMM slot", mtlk_vap_get_oid(core->vap_handle));
        return MTLK_ERR_NO_RESOURCES;
    }

    /* fill the message data */
    man_entry->id = UM_MAN_SET_POWER_LIMIT_REQ;
    man_entry->payload_size = sizeof(UMI_TX_POWER_LIMIT);
    mac_msg = (UMI_TX_POWER_LIMIT *)man_entry->payload;
    memset(mac_msg, 0, sizeof(*mac_msg));
    mac_msg->powerLimitOffset = (uint8)power_limit;

    /* send the message to FW */
    res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);

    /* New structure does not contain field status */

    /* cleanup the message */
    mtlk_txmm_msg_cleanup(&man_msg);

    return res;
}

static int
_mtlk_core_set_tx_power_limit_offset (mtlk_handle_t hcore, const void* data, uint32 data_size)
{
    uint32 res = MTLK_ERR_OK;
    mtlk_core_t *core = (mtlk_core_t *) hcore;
    wave_radio_t *radio = wave_vap_radio_get(core->vap_handle);

    mtlk_tx_power_lim_cfg_t *tx_power_lim_cfg = NULL;
    uint32 tx_power_lim_cfg_size;
    mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;

    MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

    /* get configuration */
    tx_power_lim_cfg = mtlk_clpb_enum_get_next(clpb, &tx_power_lim_cfg_size);
    MTLK_CLPB_TRY(tx_power_lim_cfg, tx_power_lim_cfg_size)
      MTLK_CFG_START_CHEK_ITEM_AND_CALL()
        /* send new config to FW */
        MTLK_CFG_CHECK_ITEM_AND_CALL(tx_power_lim_cfg, powerLimitOffset, _mtlk_core_send_tx_power_limit_offset,
                                     (core, tx_power_lim_cfg->powerLimitOffset), res);
        /* store new config in internal db*/
        MTLK_CFG_CHECK_ITEM_AND_CALL_VOID(tx_power_lim_cfg, powerLimitOffset, WAVE_RADIO_PDB_SET_INT,
                                          (radio, PARAM_DB_RADIO_TX_POWER_LIMIT_OFFSET, tx_power_lim_cfg->powerLimitOffset));
      MTLK_CFG_END_CHEK_ITEM_AND_CALL()
    MTLK_CLPB_FINALLY(res)
      /* push result into clipboard */
      return mtlk_clpb_push_res(clpb, res);
    MTLK_CLPB_END
}

static int
_mtlk_core_get_tx_power_limit_offset (mtlk_handle_t hcore, const void* data, uint32 data_size)
{
    uint32 res = MTLK_ERR_OK;
    mtlk_tx_power_lim_cfg_t tx_power_lim_cfg;
    mtlk_core_t *core = (mtlk_core_t*)hcore;
    mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;

    MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

    MTLK_CFG_SET_ITEM_BY_FUNC(&tx_power_lim_cfg, powerLimitOffset, _mtlk_core_receive_tx_power_limit_offset,
                              (core, &tx_power_lim_cfg.powerLimitOffset), res);

    /* push result and config to clipboard*/
    res = mtlk_clpb_push(clpb, &res, sizeof(res));
    if (MTLK_ERR_OK == res) {
        res = mtlk_clpb_push(clpb, &tx_power_lim_cfg, sizeof(tx_power_lim_cfg));
    }

    return res;
}

/* Fixed Rate configuration */

struct mtlk_param_limits {
      uint32 min_limit;
      uint32 max_limit;
};

static BOOL
_mtlk_core_params_limits_valid (mtlk_core_t *core, const uint32 *params, struct mtlk_param_limits *limits, int size)
{
    uint32  value, v_min, v_max;
    int     i;

    for (i = 0; i < size; i++) {
      value = params[i];
      v_min = limits[i].min_limit;
      v_max = limits[i].max_limit;
      if (!((v_min <= value) && (value <= v_max))) {
          ELOG_DDDD("params[%d] = %u is not fit to range [%u ... %u]", i, value, v_min, v_max);
          return FALSE;
      }
    }
    return TRUE;
}

static int
_mtlk_core_check_fixed_rate (mtlk_core_t *core, const mtlk_fixed_rate_cfg_t *fixed_rate_cfg)
{
    struct mtlk_param_limits limits[MTLK_FIXED_RATE_CFG_SIZE] = {
      [MTLK_FIXED_RATE_CFG_SID]  = {            0,  ALL_SID_MAX }, /* stationIndex */
      [MTLK_FIXED_RATE_CFG_AUTO] = {            0,            1 }, /* isAutoRate   */
      [MTLK_FIXED_RATE_CFG_BW]   = {        CW_20,       CW_160 }, /* bw           */
      [MTLK_FIXED_RATE_CFG_PHYM] = { PHY_MODE_MIN, PHY_MODE_MAX }, /* phyMode      */
      [MTLK_FIXED_RATE_CFG_NSS]  = {            0,            4 }, /* nss          */
      [MTLK_FIXED_RATE_CFG_MCS]  = {            0,           32 }, /* mcs          */
      [MTLK_FIXED_RATE_CFG_SCP]  = {            0,            5 }, /* scp/cpMode   */
      [MTLK_FIXED_RATE_CFG_DCM]  = {            0,            1 }, /* dcm   */
      [MTLK_FIXED_RATE_CFG_HE_EXTPARTIALBWDATA] = {            0,            1 }, /* heExtPartialBwData   */
      [MTLK_FIXED_RATE_CFG_HE_EXTPARTIALBWMNG]  = {            0,            1 }, /* heExtPartialBwMng   */
      [MTLK_FIXED_RATE_CFG_CHANGETYPE]          = {            1,            3 }, /* changeType   */
    };

    if (/* All params are within limits */
        _mtlk_core_params_limits_valid(core, &fixed_rate_cfg->params[0], &limits[0], MTLK_FIXED_RATE_CFG_SIZE) &&
        /* StationID: valid OR for all */
        mtlk_hw_is_sid_valid_or_all_sta_sid(mtlk_vap_get_hw(core->vap_handle),
                                            fixed_rate_cfg->params[MTLK_FIXED_RATE_CFG_SID]) &&
        /* AutoRate OR correct bitrate params */
        ((0 != fixed_rate_cfg->params[MTLK_FIXED_RATE_CFG_AUTO]) ||
          mtlk_bitrate_params_supported(
            fixed_rate_cfg->params[MTLK_FIXED_RATE_CFG_PHYM],
            fixed_rate_cfg->params[MTLK_FIXED_RATE_CFG_BW],
            fixed_rate_cfg->params[MTLK_FIXED_RATE_CFG_SCP],
            fixed_rate_cfg->params[MTLK_FIXED_RATE_CFG_MCS],
            fixed_rate_cfg->params[MTLK_FIXED_RATE_CFG_NSS]))
        ) {
        return MTLK_ERR_OK;
    }

    return MTLK_ERR_PARAMS;
}

static int
_mtlk_core_send_fixed_rate (mtlk_core_t *core, const mtlk_fixed_rate_cfg_t *fixed_rate_cfg)
{
    mtlk_txmm_msg_t         man_msg;
    mtlk_txmm_data_t       *man_entry;
    UMI_FIXED_RATE_CONFIG  *mac_msg;
    int                     res;
    unsigned                oid;

    MTLK_ASSERT(core != NULL);
    oid = mtlk_vap_get_oid(core->vap_handle);

    ILOG2_D("CID-%04x: Set FIXED RATE", oid);

    /* allocate a new message */
    man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, mtlk_vap_get_txmm(core->vap_handle), NULL);
    if (!man_entry)
    {
      ELOG_D("CID-%04x: Can not get TXMM slot", oid);
      return MTLK_ERR_NO_RESOURCES;
    }

    /* fill the message data */
    man_entry->id = UM_MAN_FIXED_RATE_CONFIG_REQ;
    man_entry->payload_size = sizeof(UMI_FIXED_RATE_CONFIG);
    mac_msg = (UMI_FIXED_RATE_CONFIG *)man_entry->payload;
    memset(mac_msg, 0, sizeof(*mac_msg));

    MTLK_STATIC_ASSERT(sizeof(mac_msg->stationIndex) == sizeof(uint16));
    mac_msg->stationIndex  = HOST_TO_MAC16(fixed_rate_cfg->params[MTLK_FIXED_RATE_CFG_SID]);
    mac_msg->isAutoRate    = fixed_rate_cfg->params[MTLK_FIXED_RATE_CFG_AUTO];
    mac_msg->bw            = fixed_rate_cfg->params[MTLK_FIXED_RATE_CFG_BW];
    mac_msg->phyMode       = fixed_rate_cfg->params[MTLK_FIXED_RATE_CFG_PHYM];
    mac_msg->nss           = fixed_rate_cfg->params[MTLK_FIXED_RATE_CFG_NSS];
    mac_msg->mcs           = fixed_rate_cfg->params[MTLK_FIXED_RATE_CFG_MCS];
    mac_msg->cpMode        = fixed_rate_cfg->params[MTLK_FIXED_RATE_CFG_SCP];
    mac_msg->dcm           = fixed_rate_cfg->params[MTLK_FIXED_RATE_CFG_DCM];
    mac_msg->heExtPartialBwData
                           = fixed_rate_cfg->params[MTLK_FIXED_RATE_CFG_HE_EXTPARTIALBWDATA];
    mac_msg->heExtPartialBwMng
                           = fixed_rate_cfg->params[MTLK_FIXED_RATE_CFG_HE_EXTPARTIALBWMNG];
    mac_msg->changeType
                           = fixed_rate_cfg->params[MTLK_FIXED_RATE_CFG_CHANGETYPE];

    /* send the message to FW */
    res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);

    /* cleanup the message */
    mtlk_txmm_msg_cleanup(&man_msg);

    return res;
}

static int
_mtlk_core_check_and_send_fixed_rate (mtlk_core_t *core, const mtlk_fixed_rate_cfg_t *fixed_rate_cfg)
{
    int res = _mtlk_core_check_fixed_rate(core, fixed_rate_cfg);
    if (MTLK_ERR_OK == res) {
      res = _mtlk_core_send_fixed_rate(core, fixed_rate_cfg);
    }
    return res;
}

static int
_mtlk_core_set_fixed_rate (mtlk_handle_t hcore, const void *data, uint32 data_size)
{
    int res = MTLK_ERR_OK;
    mtlk_core_t *core = (mtlk_core_t *) hcore;
    mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;
    mtlk_fixed_rate_cfg_t *fixed_rate_cfg = NULL;
    uint32 cfg_size;

    MTLK_ASSERT(core != NULL);
    MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

    /* get configuration */
    fixed_rate_cfg = mtlk_clpb_enum_get_next(clpb, &cfg_size);
    MTLK_CLPB_TRY(fixed_rate_cfg, cfg_size)
      MTLK_CFG_START_CHEK_ITEM_AND_CALL()
        /* send configuration to FW */
        MTLK_CFG_CHECK_ITEM_AND_CALL(fixed_rate_cfg, params, _mtlk_core_check_and_send_fixed_rate,
                                     (core, fixed_rate_cfg), res);
      MTLK_CFG_END_CHEK_ITEM_AND_CALL()
    MTLK_CLPB_FINALLY(res)
      /* push result into clipboard */
      return mtlk_clpb_push_res(clpb, res);
    MTLK_CLPB_END
}

/************* Change HT protection method ******************/

static int
_mtlk_core_set_bss_internal(mtlk_core_t *core, struct mtlk_bss_parameters *params)
{
    UMI_SET_BSS fw_request;
    int res = MTLK_ERR_OK;

    ILOG2_DD("CID-%04x: vap_in_fw_is_active=%u",
            mtlk_vap_get_oid(core->vap_handle), core->vap_in_fw_is_active);

    /* FIXME: be sure that this flag as correct */
    if (!core->vap_in_fw_is_active) {
        /* Workaround: UM_BSS_SET_BSS_REQ still cannot be sent, but it is not a fatal error.
           Updated parameterts has to written to pdb; they will be set during a new call
           of UM_BSS_SET_BSS_REQ.
         */
        ILOG2_D("CID-%04x: UM_BSS_SET_BSS_REQ rejected, but settings will be updated in pdb",  mtlk_vap_get_oid(core->vap_handle));
        goto end;
    }

    /* Fill UMI_SET_BSS FW request */
    _mtlk_core_fill_set_bss_request(core, &fw_request, params);

    /* Process UM_BSS_SET_BSS_REQ */
    res = mtlk_core_set_bss(core, core, &fw_request);

end:
    return res;
}

static void
_mtlk_core_reset_bss_params_internal(struct mtlk_bss_parameters *bss_parameters)
{
    memset(bss_parameters, 0, sizeof(*bss_parameters));
    bss_parameters->use_cts_prot = -1;
    bss_parameters->use_short_preamble = -1;
    bss_parameters->use_short_slot_time = -1;
    bss_parameters->ht_opmode = -1;
    bss_parameters->p2p_ctwindow = -1;
    bss_parameters->p2p_opp_ps = -1;
}

static int
mtlk_core_set_ht_protection (mtlk_core_t *core, uint32 protection_mode)
{
    struct mtlk_bss_parameters bss_parameters;

    /* reset structure and set protection mode */
    _mtlk_core_reset_bss_params_internal(&bss_parameters);
    bss_parameters.use_cts_prot = protection_mode;

    /* set bss */
    return _mtlk_core_set_bss_internal(core, &bss_parameters);
}

static void
_mtlk_core_store_ht_protection (mtlk_core_t *core, const uint32 protection_mode)
{
    int flags;

    MTLK_ASSERT(core != NULL);

    /* store value */
    MTLK_CORE_PDB_SET_INT(core, PARAM_DB_CORE_HT_PROTECTION, protection_mode);
    /* update priority flag */
    flags = MTLK_CORE_PDB_GET_INT(core, PARAM_DB_CORE_IWPRIV_FORCED);
    MTLK_CORE_PDB_SET_INT(core, PARAM_DB_CORE_IWPRIV_FORCED, flags | PARAM_DB_CORE_IWPRIV_FORCED_HT_PROTECTION_FLAG);
}

static int
_mtlk_core_set_ht_protection (mtlk_handle_t hcore, const void* data, uint32 data_size)
{
    uint32 res = MTLK_ERR_OK;
    mtlk_core_t *core = (mtlk_core_t *) hcore;

    mtlk_ht_protection_cfg_t *protection_cfg = NULL;
    uint32 protection_cfg_size;
    mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;

    MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

    /* get configuration */
    protection_cfg = mtlk_clpb_enum_get_next(clpb, &protection_cfg_size);
    MTLK_CLPB_TRY(protection_cfg, protection_cfg_size)
      MTLK_CFG_START_CHEK_ITEM_AND_CALL()
        /* send new config to FW */
        MTLK_CFG_CHECK_ITEM_AND_CALL(protection_cfg, use_cts_prot, mtlk_core_set_ht_protection,
                                     (core, protection_cfg->use_cts_prot), res);
        /* store new config in internal db*/
        MTLK_CFG_CHECK_ITEM_AND_CALL_VOID(protection_cfg, use_cts_prot, _mtlk_core_store_ht_protection,
                                          (core, protection_cfg->use_cts_prot));
      MTLK_CFG_END_CHEK_ITEM_AND_CALL()
    MTLK_CLPB_FINALLY(res)
      /* push result into clipboard */
      return mtlk_clpb_push_res(clpb, res);
    MTLK_CLPB_END
}

static int
_mtlk_core_get_ht_protection (mtlk_handle_t hcore, const void* data, uint32 data_size)
{
    uint32 res = MTLK_ERR_OK;
    mtlk_ht_protection_cfg_t protection_cfg;
    mtlk_core_t *core = (mtlk_core_t*)hcore;
    mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;

    MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

    /* read config from internal db */
    MTLK_CFG_SET_ITEM(&protection_cfg, use_cts_prot, MTLK_CORE_PDB_GET_INT(core, PARAM_DB_CORE_HT_PROTECTION));

    /* push result and config to clipboard*/
    res = mtlk_clpb_push(clpb, &res, sizeof(res));
    if (MTLK_ERR_OK == res) {
        res = mtlk_clpb_push(clpb, &protection_cfg, sizeof(protection_cfg));
    }

    return res;
}

/********************************/

static int
_mtlk_core_get_bf_explicit_cap (mtlk_handle_t hcore, const void* data, uint32 data_size)
{
    uint32 res = MTLK_ERR_OK;
    mtlk_bf_explicit_cap_cfg_t bf_explicit_cap_cfg;
    mtlk_core_t *core = (mtlk_core_t*)hcore;
    mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;

    MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

    /* read capability */
    MTLK_CFG_SET_ITEM(&bf_explicit_cap_cfg, bf_explicit_cap, core_get_psdb_bf_explicit_cap(core));

    /* push result and config to clipboard*/
    res = mtlk_clpb_push(clpb, &res, sizeof(res));
    if (MTLK_ERR_OK == res) {
        res = mtlk_clpb_push(clpb, &bf_explicit_cap_cfg, sizeof(bf_explicit_cap_cfg));
    }

    return res;
}

/********************************/

static int
_mtlk_core_get_tx_power (mtlk_handle_t hcore, const void* data, uint32 data_size)
{
    int res = MTLK_ERR_OK;
    mtlk_tx_power_cfg_t tx_power_cfg;
    mtlk_core_t *core = (mtlk_core_t*)hcore;
    wave_radio_t *radio = wave_vap_radio_get(core->vap_handle);
    mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;

    MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

    /* read tx power and convert it from power units to dBm */
    MTLK_CFG_SET_ITEM(&tx_power_cfg, tx_power,
      POWER_TO_DBM(WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_TX_POWER_CFG) -
                   WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_TX_POWER_LIMIT_OFFSET)));

    /* push result and config to clipboard*/
    res = mtlk_clpb_push(clpb, &res, sizeof(res));
    if (MTLK_ERR_OK == res) {
        res = mtlk_clpb_push(clpb, &tx_power_cfg, sizeof(tx_power_cfg));
    }

    return res;
}

/********************************/

#define DSCP_LOWEST_PRIORITY 0
#define DSCP_UNUSED_RANGE 255

static int
_save_qos_map (mtlk_core_t *core, struct cfg80211_qos_map *qos_map)
{
  int res = MTLK_ERR_PARAMS;
  uint32 up_idx, dscp_idx, dscp_low, dscp_high;
  uint8 dscp_table[DSCP_NUM];

  /* Erase table. All frames with unknown DSCP must be transmitted with UP=0 (DSCP_LOWEST_PRIORITY) */
  memset(dscp_table, DSCP_LOWEST_PRIORITY, sizeof(dscp_table));

  /* Process common DSCP values */
  {
    for (up_idx = 0; up_idx < NTS_TIDS; up_idx++) {
      dscp_low = qos_map->up[up_idx].low;
      dscp_high = qos_map->up[up_idx].high;

      if ((dscp_low < DSCP_NUM) && (dscp_high < DSCP_NUM) && (dscp_low <= dscp_high)) {
        for (dscp_idx = dscp_low; dscp_idx <= dscp_high; dscp_idx++) {
          dscp_table[dscp_idx] = up_idx;
        }
      } else if ((dscp_low == DSCP_UNUSED_RANGE) && (dscp_high == DSCP_UNUSED_RANGE)){
        ILOG0_D("Skip pair for up_idx %d", up_idx);
      } else {
        ELOG_DDD("Incorrect value(s) for up_idx: %d (dscp_low: %d, dscp_high %d)", up_idx, dscp_low, dscp_high);
        goto end;
      }
    }
  }

  /* Process DSCP exceptions */
  {
    int num_des = qos_map->num_des;
    int i;

    if (num_des > IEEE80211_QOS_MAP_MAX_EX) {
      ELOG_DD("Too many DSCP exceptions: %d, maximum allowed: %d", num_des, IEEE80211_QOS_MAP_MAX_EX);
      goto end;
    }

    for (i = 0; i < num_des; i++) {
      dscp_idx = qos_map->dscp_exception[i].dscp;
      up_idx = qos_map->dscp_exception[i].up;
      if ((dscp_idx < DSCP_NUM) && (up_idx < NTS_TIDS)) {
        dscp_table[dscp_idx] = up_idx;
      } else {
        ELOG_DDD("Incorrect value(s) for exception %d (dscp_idx: %d, up_idx: %d)", i, dscp_idx, up_idx);
        goto end;
      }
    }
  }

  mtlk_dump(2, dscp_table, sizeof(dscp_table), "dump of QoS map");
  wave_memcpy(core->dscp_table, sizeof(core->dscp_table), dscp_table, sizeof(dscp_table));
  res = MTLK_ERR_OK;

#if MTLK_USE_DIRECTCONNECT_DP_API
  /* Update QoS for DirectConnect DP Driver */
  {
    mtlk_df_user_t *df_user = mtlk_df_get_user(mtlk_vap_get_df(core->vap_handle));
    if (MTLK_ERR_OK != mtlk_df_user_set_priority_to_qos(df_user, core->dscp_table)) {
      WLOG_S("%s: Unable set priority to WMM", mtlk_df_user_get_name(df_user));
    }
  }
#endif

end:
  return res;
}

static int
_mtlk_core_set_qos_map (mtlk_handle_t hcore, const void* data, uint32 data_size)
{
  int                       res = MTLK_ERR_NOT_SUPPORTED;
  mtlk_core_t               *core = (mtlk_core_t*)hcore;
  mtlk_clpb_t               *clpb = *(mtlk_clpb_t **) data;
  uint32                    qos_map_size;
  struct cfg80211_qos_map   *qos_map = NULL;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  qos_map = mtlk_clpb_enum_get_next(clpb, &qos_map_size);
  MTLK_CLPB_TRY(qos_map, qos_map_size)
    res = _save_qos_map(core, qos_map);
  MTLK_CLPB_FINALLY(res)
    return mtlk_clpb_push_res(clpb, res);
  MTLK_CLPB_END
}

#ifdef CPTCFG_IWLWAV_PMCU_SUPPORT
static int
_mtlk_core_get_pcoc_params (mtlk_core_t *core, mtlk_pcoc_params_t *params)
{
  mtlk_pcoc_params_t *configured_params;

  MTLK_ASSERT(core != NULL);
  MTLK_ASSERT(params != NULL);

  configured_params = wv_PMCU_Get_Params();
  *params = *configured_params;

  return MTLK_ERR_OK;
}

static int
_mtlk_core_get_pcoc_cfg (mtlk_handle_t hcore,
                         const void* data, uint32 data_size)
{
  uint32 res = MTLK_ERR_OK;
  mtlk_pcoc_mode_cfg_t pcoc_cfg;
  mtlk_core_t *core = (mtlk_core_t*)hcore;
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);
  MTLK_ASSERT(!mtlk_vap_is_slave_ap(core->vap_handle));

  memset(&pcoc_cfg, 0, sizeof(pcoc_cfg));

  MTLK_CFG_SET_ITEM(&pcoc_cfg, is_enabled,    wv_PMCU_is_enabled_adm());
  MTLK_CFG_SET_ITEM(&pcoc_cfg, is_active,     wv_PMCU_is_active());
  MTLK_CFG_SET_ITEM(&pcoc_cfg, traffic_state, wv_PMCU_get_traffic_state());
  MTLK_CFG_SET_ITEM_BY_FUNC(&pcoc_cfg, params,
                            _mtlk_core_get_pcoc_params, (core, &pcoc_cfg.params), res);

  res = mtlk_clpb_push(clpb, &res, sizeof(res));
  if (MTLK_ERR_OK == res) {
    res = mtlk_clpb_push(clpb, &pcoc_cfg, sizeof(pcoc_cfg));
  }

  return res;
}

static int
_mtlk_core_set_pcoc_enabled (mtlk_core_t *core, BOOL is_enabled)
{
  int res = MTLK_ERR_OK;

  res = wv_PMCU_set_enabled_adm(is_enabled);

  return res;
}

static int
_mtlk_core_set_pcoc_pmcu_debug (mtlk_core_t *core, uint32 pmcu_debug)
{
  int res = MTLK_ERR_OK;

  return res;
}

static int
_mtlk_core_set_pcoc_cfg (mtlk_handle_t hcore,
                         const void* data, uint32 data_size)
{
  uint32 res = MTLK_ERR_OK;
  mtlk_core_t *core = (mtlk_core_t*)hcore;
  mtlk_pcoc_mode_cfg_t *pcoc_cfg = NULL;
  uint32 pcoc_cfg_size;
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  pcoc_cfg = mtlk_clpb_enum_get_next(clpb, &pcoc_cfg_size);
  MTLK_CLPB_TRY(pcoc_cfg, pcoc_cfg_size)
  {
    MTLK_CFG_START_CHEK_ITEM_AND_CALL()
      MTLK_CFG_CHECK_ITEM_AND_CALL(pcoc_cfg, params, wv_PMCU_Set_Params,
                                   (&pcoc_cfg->params), res);
      MTLK_CFG_CHECK_ITEM_AND_CALL(pcoc_cfg, is_enabled, _mtlk_core_set_pcoc_enabled,
                                   (core, pcoc_cfg->is_enabled), res);
      MTLK_CFG_CHECK_ITEM_AND_CALL(pcoc_cfg, pmcu_debug, _mtlk_core_set_pcoc_pmcu_debug,
                                   (core, pcoc_cfg->pmcu_debug), res);
    MTLK_CFG_END_CHEK_ITEM_AND_CALL()
  }
  MTLK_CLPB_FINALLY(res)
    return mtlk_clpb_push_res(clpb, res);
  MTLK_CLPB_END;
}

#endif /*CPTCFG_IWLWAV_PMCU_SUPPORT*/

#ifdef CPTCFG_IWLWAV_SET_PM_QOS
static int
_mtlk_core_set_cpu_dma_latency (mtlk_handle_t hcore, const void* data, uint32 data_size)
{
    int res = MTLK_ERR_OK;
    mtlk_core_t *core = (mtlk_core_t *) hcore;
    mtlk_pm_qos_cfg_t *pm_qos_cfg = NULL;
    uint32 pm_qos_cfg_size;
    mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;

    MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);
    /* get configuration */
    pm_qos_cfg = mtlk_clpb_enum_get_next(clpb, &pm_qos_cfg_size);
    MTLK_CLPB_TRY(pm_qos_cfg, pm_qos_cfg_size)
    {
      MTLK_CFG_START_CHEK_ITEM_AND_CALL()
        /* change config */
        MTLK_CFG_CHECK_ITEM_AND_CALL(pm_qos_cfg, cpu_dma_latency, mtlk_mmb_update_cpu_dma_latency,
                                (mtlk_vap_get_hw(core->vap_handle), pm_qos_cfg->cpu_dma_latency), res);
      MTLK_CFG_END_CHEK_ITEM_AND_CALL()
    }
    MTLK_CLPB_FINALLY(res)
      /* push result into clipboard */
      return mtlk_clpb_push_res(clpb, res);
    MTLK_CLPB_END;
}

static int
_mtlk_core_get_cpu_dma_latency (mtlk_handle_t hcore, const void* data, uint32 data_size)
{
    int res = MTLK_ERR_OK;
    mtlk_pm_qos_cfg_t pm_qos_cfg;
    mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;

    MTLK_UNREFERENCED_PARAM(hcore);
    MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);
    MTLK_CFG_SET_ITEM(&pm_qos_cfg, cpu_dma_latency, cpu_dma_latency);

    /* push result and config to clipboard*/
    res = mtlk_clpb_push(clpb, &res, sizeof(res));
    if (MTLK_ERR_OK == res) {
        res = mtlk_clpb_push(clpb, &pm_qos_cfg, sizeof(pm_qos_cfg));
    }
    return res;
}
#endif

static int
_mtlk_core_mbss_add_vap_idx (mtlk_handle_t hcore, uint32 vap_index, mtlk_work_role_e role, BOOL is_master)
{
  uint32 res = MTLK_ERR_OK;
  mtlk_core_t *core = (mtlk_core_t*)hcore;
  char ndev_name_pattern[IFNAMSIZ];
  int nchars;

  ILOG2_D("CID-%04x: Got PRM_ID_VAP_ADD", mtlk_vap_get_oid(core->vap_handle));

  nchars = snprintf(ndev_name_pattern, sizeof(ndev_name_pattern), "%s.%d", MTLK_NDEV_NAME, (vap_index - 1));
  if (nchars >= sizeof(ndev_name_pattern)) {
    return MTLK_ERR_BUF_TOO_SMALL;
  }

  if ((res = mtlk_vap_manager_create_vap(mtlk_vap_get_manager(core->vap_handle),
                                         vap_index,
                                         NULL,
                                         ndev_name_pattern,
                                         role,
                                         is_master,
                                         NULL)) != MTLK_ERR_OK) {
    ELOG_DD("CID-%04x: Can't add VapID %u", mtlk_vap_get_oid(core->vap_handle), vap_index);
  }
  else {
    ILOG0_DD("CID-%04x: VapID %u added", mtlk_vap_get_oid(core->vap_handle), vap_index);
  }

  return res;
}

static int
_mtlk_core_mbss_del_vap_idx (mtlk_handle_t hcore, uint32 vap_index)
{
  uint32 res = MTLK_ERR_OK;
  mtlk_core_t *core = (mtlk_core_t*)hcore;
  mtlk_vap_handle_t vap_handle;
  int target_core_state;
  uint32 max_vaps_count;

  max_vaps_count = mtlk_vap_manager_get_max_vaps_count(mtlk_vap_get_manager(core->vap_handle));
  if (vap_index >= max_vaps_count) {
    ELOG_DD("CID-%04x: VapID %u invalid", mtlk_vap_get_oid(core->vap_handle), vap_index);
    res = MTLK_ERR_PARAMS;
    goto func_ret;
  }

  res = mtlk_vap_manager_get_vap_handle(mtlk_vap_get_manager(core->vap_handle), vap_index, &vap_handle);
  if (MTLK_ERR_OK != res ) {
    ELOG_DD("CID-%04x: VapID %u doesn't exist", mtlk_vap_get_oid(core->vap_handle), vap_index);
    res = MTLK_ERR_PARAMS;
    goto func_ret;
  }

  if (mtlk_vap_is_master(vap_handle)) {
    ELOG_D("CID-%04x: Can't remove Master VAP", mtlk_vap_get_oid(core->vap_handle));
    res = MTLK_ERR_PARAMS;
    goto func_ret;
  }

  target_core_state = mtlk_core_get_net_state(mtlk_vap_get_core(vap_handle));
  if ( 0 == ((NET_STATE_READY|NET_STATE_IDLE|NET_STATE_HALTED) & target_core_state) ) {
    ILOG1_D("CID-%04x:: Invalid card state - request rejected", mtlk_vap_get_oid(vap_handle));
    res = MTLK_ERR_NOT_READY;
    goto func_ret;
  }

  ILOG0_DD("CID-%04x: Deleting VapID %u", mtlk_vap_get_oid(core->vap_handle), vap_index);
  mtlk_vap_stop(vap_handle);
  mtlk_vap_delete(vap_handle);
  res = MTLK_ERR_OK;

func_ret:
  return res;
}

static int
_mtlk_core_add_vap_name (mtlk_handle_t hcore,
                         const void* data, uint32 data_size)
{
  int res = MTLK_ERR_NO_RESOURCES;
  mtlk_core_t *nic = HANDLE_T_PTR(mtlk_core_t, hcore);
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;
  mtlk_mbss_cfg_t *mbss_cfg;
  uint32 mbss_cfg_size = sizeof(mtlk_mbss_cfg_t);
  uint32 _vap_index;
  mtlk_vap_handle_t _vap_handle;
  void *ctx;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  mbss_cfg = mtlk_clpb_enum_get_next(clpb, &mbss_cfg_size);
  MTLK_CLPB_TRY(mbss_cfg, mbss_cfg_size)
  {
    if (mtlk_core_is_block_tx_mode(nic)) {
      ELOG_V("Cannot add vap, waiting for beacons");
        MTLK_CLPB_EXIT(MTLK_ERR_RETRY);
    }

    if (mtlk_core_is_in_scan_mode(nic)) {
      ELOG_V("Cannot add vap, scan is running");
        MTLK_CLPB_EXIT(MTLK_ERR_RETRY);
    }

    res = mtlk_vap_manager_get_free_vap_index(mtlk_vap_get_manager(nic->vap_handle), &_vap_index);
    if (MTLK_ERR_OK != res) {
      ELOG_V("No free slot for new VAP");
        MTLK_CLPB_EXIT(res);
    }

    res = mtlk_vap_manager_create_vap(mtlk_vap_get_manager(nic->vap_handle),
        _vap_index,
        mbss_cfg->wiphy,
        mbss_cfg->added_vap_name,
        mbss_cfg->role,
        mbss_cfg->is_master,
        mbss_cfg->ndev);
    if (MTLK_ERR_OK != res) {
      ELOG_V("Can't add VAP");
        MTLK_CLPB_EXIT(res);
    }

    res = mtlk_vap_manager_get_vap_handle(mtlk_vap_get_manager(nic->vap_handle), _vap_index, &_vap_handle);
    if (MTLK_ERR_OK != res) {
      ELOG_D("VapID %u doesn't exist", _vap_index);
        MTLK_CLPB_EXIT(res);
    }

    if (mbss_cfg->role == MTLK_ROLE_AP){
      ctx = mtlk_df_user_get_wdev(mtlk_df_get_user(mtlk_vap_get_df(_vap_handle)));

      res = mtlk_clpb_push(clpb, &ctx, sizeof(ctx));
      if (MTLK_ERR_OK != res) {
        mtlk_clpb_purge(clpb);
      }
    } else { /* Station mode interface */
      ctx = mtlk_df_get_user(mtlk_vap_get_df(_vap_handle));

      res = mtlk_clpb_push(clpb, &ctx, sizeof(ctx));
      if (MTLK_ERR_OK != res) {
        mtlk_clpb_purge(clpb);
      }
    }
  }
  MTLK_CLPB_FINALLY(res)
    return res;
  MTLK_CLPB_END;
}

static int
_mtlk_core_add_vap_idx (mtlk_handle_t hcore,
                        const void* data, uint32 data_size)
{
  uint32 res = MTLK_ERR_OK;
  mtlk_mbss_cfg_t *mbss_cfg;
  uint32 mbss_cfg_size = sizeof(mtlk_mbss_cfg_t);
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  mbss_cfg = mtlk_clpb_enum_get_next(clpb, &mbss_cfg_size);
  MTLK_CLPB_TRY(mbss_cfg, mbss_cfg_size)
    {
    MTLK_CFG_START_CHEK_ITEM_AND_CALL()
      MTLK_CFG_CHECK_ITEM_AND_CALL(mbss_cfg, added_vap_index, _mtlk_core_mbss_add_vap_idx,
                                   (hcore, mbss_cfg->added_vap_index, mbss_cfg->role, mbss_cfg->is_master), res);
    MTLK_CFG_END_CHEK_ITEM_AND_CALL()
    }
  MTLK_CLPB_FINALLY(res)
    return mtlk_clpb_push_res(clpb, res);
  MTLK_CLPB_END;
}

static int
_mtlk_core_del_vap_name (mtlk_handle_t hcore,
                         const void* data, uint32 data_size)
{
  int res = MTLK_ERR_OK;
  mtlk_core_t *core = (mtlk_core_t*)hcore;
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;
  int target_core_state;
  mtlk_mbss_cfg_t *mbss_cfg;
  uint32 mbss_cfg_size = sizeof(mtlk_mbss_cfg_t);

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  mbss_cfg = mtlk_clpb_enum_get_next(clpb, &mbss_cfg_size);
  MTLK_CLPB_TRY(mbss_cfg, mbss_cfg_size)
    if (mtlk_vap_is_master(mbss_cfg->vap_handle)) {
      ELOG_D("CID-%04x: Can't remove Master VAP", mtlk_vap_get_oid(core->vap_handle));
      MTLK_CLPB_EXIT(MTLK_ERR_PARAMS);
    }

    if (!mtlk_vap_get_core(mbss_cfg->vap_handle)->is_stopped) {
      res = __mtlk_core_deactivate(core, mtlk_vap_get_core(mbss_cfg->vap_handle));
      if ((MTLK_ERR_OK != res) && (MTLK_ERR_RETRY != res)) {
        ELOG_DD("CID-%04x: Core deactivation is failed err:%d", mtlk_vap_get_oid(mbss_cfg->vap_handle), res);
        MTLK_CLPB_EXIT(MTLK_ERR_PARAMS);
      }
      else if (MTLK_ERR_RETRY == res) {
        ILOG2_D("CID-%04x: Core deactivation is postponded", mtlk_vap_get_oid(mbss_cfg->vap_handle));
        MTLK_CLPB_EXIT(res);
      }
    }

    target_core_state = mtlk_core_get_net_state(mtlk_vap_get_core(mbss_cfg->vap_handle));
    if (0 == ((NET_STATE_READY|NET_STATE_IDLE|NET_STATE_HALTED) & target_core_state)) {
      WLOG_D("CID-%04x: Invalid card state - request rejected", mtlk_vap_get_oid(mbss_cfg->vap_handle));
      MTLK_CLPB_EXIT(MTLK_ERR_NOT_READY);
    }

    if (mtlk_core_is_block_tx_mode(core)) {
      ELOG_V("Cannot remove vap, waiting for beacons");
      MTLK_CLPB_EXIT(MTLK_ERR_RETRY);
    }

    if (mtlk_core_is_in_scan_mode(core)) {
      ELOG_V("Cannot remove vap, scan is running");
      MTLK_CLPB_EXIT(MTLK_ERR_RETRY);
    }

    mtlk_vap_stop(mbss_cfg->vap_handle);
    mtlk_vap_delete(mbss_cfg->vap_handle);
  MTLK_CLPB_FINALLY(res)
    return mtlk_clpb_push_res(clpb, res);
  MTLK_CLPB_END;
}

static int
_mtlk_core_del_vap_idx (mtlk_handle_t hcore,
                        const void* data, uint32 data_size)
{
  uint32 res = MTLK_ERR_OK;
  mtlk_mbss_cfg_t *mbss_cfg;
  uint32 mbss_cfg_size = sizeof(mtlk_mbss_cfg_t);
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  mbss_cfg = mtlk_clpb_enum_get_next(clpb, &mbss_cfg_size);
  MTLK_CLPB_TRY(mbss_cfg, mbss_cfg_size)
    MTLK_CFG_START_CHEK_ITEM_AND_CALL()
      MTLK_CFG_CHECK_ITEM_AND_CALL(mbss_cfg, deleted_vap_index, _mtlk_core_mbss_del_vap_idx,
                                   (hcore, mbss_cfg->deleted_vap_index), res);
    MTLK_CFG_END_CHEK_ITEM_AND_CALL()
  MTLK_CLPB_FINALLY(res)
    return mtlk_clpb_push_res(clpb, res);
  MTLK_CLPB_END;
}

typedef struct
{
  int          res;
  mtlk_clpb_t *clpb;
} mtlk_core_get_serializer_info_enum_ctx_t;

static BOOL
__mtlk_core_get_serializer_info_enum_clb (mtlk_serializer_t    *szr,
                                          const mtlk_command_t *command,
                                          BOOL                  is_current,
                                          mtlk_handle_t         enum_ctx)
{
  mtlk_core_get_serializer_info_enum_ctx_t *ctx =
    HANDLE_T_PTR(mtlk_core_get_serializer_info_enum_ctx_t, enum_ctx);
  mtlk_serializer_command_info_t cmd_info;

  MTLK_CFG_START_CHEK_ITEM_AND_CALL()
    MTLK_CFG_SET_ITEM(&cmd_info, is_current, is_current);
    MTLK_CFG_SET_ITEM(&cmd_info, priority, mtlk_command_get_priority(command));
    MTLK_CFG_SET_ITEM(&cmd_info, issuer_slid, mtlk_command_get_issuer_slid(command));
  MTLK_CFG_END_CHEK_ITEM_AND_CALL()

  ctx->res = mtlk_clpb_push(ctx->clpb, &cmd_info, sizeof(cmd_info));

  return (ctx->res == MTLK_ERR_OK)?TRUE:FALSE;
}

static int
_mtlk_core_get_serializer_info (mtlk_handle_t hcore,
                                const void* data, uint32 data_size)
{
  mtlk_core_t *nic = HANDLE_T_PTR(mtlk_core_t, hcore);
  mtlk_core_get_serializer_info_enum_ctx_t ctx;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  ctx.clpb = *(mtlk_clpb_t **) data;
  ctx.res  = MTLK_ERR_OK;

  mtlk_serializer_enum_commands(&nic->slow_ctx->serializer, __mtlk_core_get_serializer_info_enum_clb, HANDLE_T(&ctx));

  return ctx.res;
}

static int
_mtlk_core_set_interfdet_do_params (mtlk_core_t *core, wave_radio_t *radio)
{
  BOOL interf_det_enabled, is_spectrum_40;

  MTLK_ASSERT(core != NULL);
  MTLK_ASSERT(mtlk_vap_is_master_ap(core->vap_handle));

  interf_det_enabled = FALSE;
  interf_det_enabled |= (0 != WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_INTERFDET_20MHZ_DETECTION_THRESHOLD));
  interf_det_enabled |= (0 != WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_INTERFDET_20MHZ_NOTIFICATION_THRESHOLD));
  WAVE_RADIO_PDB_SET_INT(radio, PARAM_DB_RADIO_INTERFDET_MODE, interf_det_enabled);

  if (!core->is_stopped) {
    is_spectrum_40 = (CW_40 == _mtlk_core_get_spectrum_mode(core));

    if (MTLK_ERR_OK == _mtlk_core_set_fw_interfdet_req(core, is_spectrum_40)) {
      ILOG0_DS("CID-%04x: Interference detection is %s", mtlk_vap_get_oid(core->vap_handle),
        (interf_det_enabled ? "activated" : "deactivated"));
      wave_radio_interfdet_set(radio, interf_det_enabled);
    }
    else {
      ELOG_DS("CID-%04x: Interference detection cannot be %s", mtlk_vap_get_oid(core->vap_handle),
        (interf_det_enabled ? "activated" : "deactivated"));
      wave_radio_interfdet_set(radio, FALSE);
    }
  }

  return MTLK_ERR_OK;
}

static int
_mtlk_core_set_interfdet_cfg (mtlk_handle_t hcore, const void* data, uint32 data_size)
{
  uint32 res = MTLK_ERR_OK;
  mtlk_interfdet_cfg_t *interfdet_cfg;
  mtlk_core_t *core = HANDLE_T_PTR(mtlk_core_t, hcore);
  uint32 interfdet_cfg_size = sizeof(mtlk_interfdet_cfg_t);
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **)data;
  wave_radio_t *radio = wave_vap_radio_get(core->vap_handle);

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  interfdet_cfg = mtlk_clpb_enum_get_next(clpb, &interfdet_cfg_size);
  MTLK_CLPB_TRY(interfdet_cfg, interfdet_cfg_size)
    MTLK_CFG_START_CHEK_ITEM_AND_CALL()
    MTLK_CFG_CHECK_ITEM_AND_CALL_VOID(interfdet_cfg, req_timeouts, WAVE_RADIO_PDB_SET_INT,
                            (radio, PARAM_DB_RADIO_INTERFDET_ACTIVE_POLLING_TIMEOUT, interfdet_cfg->req_timeouts.active_polling_timeout));
    MTLK_CFG_CHECK_ITEM_AND_CALL_VOID(interfdet_cfg, req_timeouts, WAVE_RADIO_PDB_SET_INT,
                            (radio, PARAM_DB_RADIO_INTERFDET_SHORT_SCAN_POLLING_TIMEOUT, interfdet_cfg->req_timeouts.short_scan_polling_timeout));
    MTLK_CFG_CHECK_ITEM_AND_CALL_VOID(interfdet_cfg, req_timeouts, WAVE_RADIO_PDB_SET_INT,
                            (radio, PARAM_DB_RADIO_INTERFDET_LONG_SCAN_POLLING_TIMEOUT, interfdet_cfg->req_timeouts.long_scan_polling_timeout));
    MTLK_CFG_CHECK_ITEM_AND_CALL_VOID(interfdet_cfg, req_timeouts, WAVE_RADIO_PDB_SET_INT,
                            (radio, PARAM_DB_RADIO_INTERFDET_ACTIVE_NOTIFICATION_TIMEOUT, interfdet_cfg->req_timeouts.active_notification_timeout));
    MTLK_CFG_CHECK_ITEM_AND_CALL_VOID(interfdet_cfg, req_timeouts, WAVE_RADIO_PDB_SET_INT,
                            (radio, PARAM_DB_RADIO_INTERFDET_SHORT_SCAN_NOTIFICATION_TIMEOUT, interfdet_cfg->req_timeouts.short_scan_notification_timeout));
    MTLK_CFG_CHECK_ITEM_AND_CALL_VOID(interfdet_cfg, req_timeouts, WAVE_RADIO_PDB_SET_INT,
                            (radio, PARAM_DB_RADIO_INTERFDET_LONG_SCAN_NOTIFICATION_TIMEOUT, interfdet_cfg->req_timeouts.long_scan_notification_timeout));
    MTLK_CFG_CHECK_ITEM_AND_CALL_VOID(interfdet_cfg, req_thresh, WAVE_RADIO_PDB_SET_INT,
                            (radio, PARAM_DB_RADIO_INTERFDET_20MHZ_DETECTION_THRESHOLD, interfdet_cfg->req_thresh.detection_threshold_20mhz));
    MTLK_CFG_CHECK_ITEM_AND_CALL_VOID(interfdet_cfg, req_thresh, WAVE_RADIO_PDB_SET_INT,
                            (radio, PARAM_DB_RADIO_INTERFDET_20MHZ_NOTIFICATION_THRESHOLD, interfdet_cfg->req_thresh.notification_threshold_20mhz));
    MTLK_CFG_CHECK_ITEM_AND_CALL_VOID(interfdet_cfg, req_thresh, WAVE_RADIO_PDB_SET_INT,
                            (radio, PARAM_DB_RADIO_INTERFDET_40MHZ_DETECTION_THRESHOLD, interfdet_cfg->req_thresh.detection_threshold_40mhz));
    MTLK_CFG_CHECK_ITEM_AND_CALL_VOID(interfdet_cfg, req_thresh, WAVE_RADIO_PDB_SET_INT,
                            (radio, PARAM_DB_RADIO_INTERFDET_40MHZ_NOTIFICATION_THRESHOLD, interfdet_cfg->req_thresh.notification_threshold_40mhz));
    MTLK_CFG_CHECK_ITEM_AND_CALL_VOID(interfdet_cfg, req_thresh, WAVE_RADIO_PDB_SET_INT,
                            (radio, PARAM_DB_RADIO_INTERFDET_SCAN_NOISE_THRESHOLD, interfdet_cfg->req_thresh.scan_noise_threshold));
    MTLK_CFG_CHECK_ITEM_AND_CALL_VOID(interfdet_cfg, req_thresh, WAVE_RADIO_PDB_SET_INT,
                            (radio, PARAM_DB_RADIO_INTERFDET_SCAN_MINIMUM_NOISE, interfdet_cfg->req_thresh.scan_minimum_noise));

    _mtlk_core_set_interfdet_do_params(core, radio);
    MTLK_CFG_END_CHEK_ITEM_AND_CALL()
  MTLK_CLPB_FINALLY(res)
    return mtlk_clpb_push_res(clpb, res);
  MTLK_CLPB_END
}

static int
_mtlk_core_get_interfdet_mode_cfg (mtlk_handle_t hcore, const void* data, uint32 data_size)
{
  mtlk_interfdet_mode_cfg_t interfdet_mode_cfg;
  mtlk_core_t *core = HANDLE_T_PTR(mtlk_core_t, hcore);
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **)data;
  wave_radio_t *radio = wave_vap_radio_get(core->vap_handle);

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);
  MTLK_ASSERT(mtlk_vap_is_master_ap(core->vap_handle));
  memset(&interfdet_mode_cfg, 0, sizeof(interfdet_mode_cfg));

  MTLK_CFG_SET_ITEM(&interfdet_mode_cfg, interfdet_mode, wave_radio_interfdet_get(radio));

  return mtlk_clpb_push(clpb, &interfdet_mode_cfg, sizeof(interfdet_mode_cfg));
}

static int
_mtlk_core_set_fw_interfdet_req (mtlk_core_t *core, BOOL is_spectrum_40)
{
  int res = MTLK_ERR_UNKNOWN;
  mtlk_txmm_msg_t man_msg;
  mtlk_txmm_data_t *man_entry = NULL;
  UMI_INTERFERER_DETECTION_PARAMS *umi_interfdet = NULL;
  wave_radio_t *radio;

  MTLK_ASSERT(core != NULL);
  MTLK_ASSERT(mtlk_vap_is_master_ap(core->vap_handle));

  radio = wave_vap_radio_get(core->vap_handle);

  man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, mtlk_vap_get_txmm(core->vap_handle), NULL);
  if (!man_entry) {
    res = MTLK_ERR_NO_RESOURCES;
    goto END;
  }

  man_entry->id = UM_MAN_SET_INTERFERER_DETECTION_PARAMS_REQ;
  man_entry->payload_size = sizeof(*umi_interfdet);

  umi_interfdet = (UMI_INTERFERER_DETECTION_PARAMS *)man_entry->payload;

  if (is_spectrum_40){
    umi_interfdet->threshold = (int8)WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_INTERFDET_40MHZ_NOTIFICATION_THRESHOLD);
  }
  else {
    umi_interfdet->threshold = (int8)WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_INTERFDET_20MHZ_NOTIFICATION_THRESHOLD);
  }

  res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);
  if (res != MTLK_ERR_OK) {
    ELOG_DD("CID-%04x: Cannot send UM_MAN_SET_INTERFERER_DETECTION_PARAMS_REQ to the FW (err=%d)",
            mtlk_vap_get_oid(core->vap_handle), res);
    goto END;
  }

  ILOG1_DSD("CID-%04x: UMI_INTERFERER_DETECTION_PARAMS(%s): Threshold: %d",
            mtlk_vap_get_oid(core->vap_handle), is_spectrum_40 ? "40MHz":"20MHz", umi_interfdet->threshold);

END:
  if (man_entry) {
    mtlk_txmm_msg_cleanup(&man_msg);
  }

  return res;
}

static int
_mtlk_core_set_fw_log_severity (mtlk_handle_t hcore, const void* data, uint32 data_size)
{
  uint32 res = MTLK_ERR_UNKNOWN;
  mtlk_fw_log_severity_t  *fw_log_severity;
  mtlk_core_t  *core = HANDLE_T_PTR(mtlk_core_t, hcore);
  mtlk_clpb_t  *clpb = *(mtlk_clpb_t **) data;
  uint32        clpb_data_size;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  fw_log_severity = mtlk_clpb_enum_get_next(clpb, &clpb_data_size);
  MTLK_CLPB_TRY(fw_log_severity, clpb_data_size)
    MTLK_CFG_START_CHEK_ITEM_AND_CALL()
      /* need to check two parameters */
      /* 1st: check only */
      MTLK_CFG_CHECK_ITEM_VOID(fw_log_severity, newLevel);
      /* 2nd: check and call */
      MTLK_CFG_CHECK_ITEM_AND_CALL(fw_log_severity, targetCPU,
          _mtlk_mmb_send_fw_log_severity,
          (mtlk_vap_get_hw(core->vap_handle), fw_log_severity->newLevel, fw_log_severity->targetCPU), res);
    MTLK_CFG_END_CHEK_ITEM_AND_CALL()
  MTLK_CLPB_FINALLY(res)
    return mtlk_clpb_push_res(clpb, res);
  MTLK_CLPB_END
}

static int
_mtlk_core_send_11b_antsel (mtlk_core_t *core, const mtlk_11b_antsel_t *antsel)
{
#define _11B_ANTSEL_ROUNDROBIN    4 /* TODO: move this define to the mac shared files */

  int res = MTLK_ERR_UNKNOWN;
  mtlk_txmm_msg_t man_msg;
  mtlk_txmm_data_t *man_entry = NULL;
  UMI_ANT_SELECTION_11B *umi_11b_antsel = NULL;

  BOOL valid_roundrobin;
  BOOL valid_ant_sel;
  uint32 tx_hw_supported_antenna_mask;
  uint32 rx_hw_supported_antenna_mask;
  mtlk_hw_t *hw;

  MTLK_ASSERT(core != NULL);
  MTLK_ASSERT(antsel != NULL);
  MTLK_STATIC_ASSERT(MAX_NUM_TX_ANTENNAS <= _11B_ANTSEL_ROUNDROBIN);
  MTLK_STATIC_ASSERT(MAX_NUM_RX_ANTENNAS <= _11B_ANTSEL_ROUNDROBIN);

  hw = mtlk_vap_get_hw(core->vap_handle);
  tx_hw_supported_antenna_mask = hw_get_tx_antenna_mask(hw);
  rx_hw_supported_antenna_mask = hw_get_rx_antenna_mask(hw);

  /* rate field is set only when txAnt and rxAnt set to Round Robin mode,
     otherwise the rate value should be 0 */

  valid_roundrobin = (antsel->txAnt == _11B_ANTSEL_ROUNDROBIN) &&
                     (antsel->rxAnt == _11B_ANTSEL_ROUNDROBIN) &&
                     (antsel->rate != 0);

  valid_ant_sel = (antsel->rate == 0) &&
                  (antsel->txAnt < MAX_NUM_TX_ANTENNAS) &&
                  (antsel->rxAnt < MAX_NUM_RX_ANTENNAS);

  if (!valid_roundrobin && !valid_ant_sel) {
    ELOG_DDDD("CID-%04x: Incorrect configuration: txAnt=%d, rxAnt=%d, rate=%d", mtlk_vap_get_oid(core->vap_handle), antsel->txAnt, antsel->rxAnt, antsel->rate);
    res = MTLK_ERR_PARAMS;
    goto END;
  }

  if (valid_ant_sel &&
    !((tx_hw_supported_antenna_mask & (1 << antsel->txAnt)) &&
      (rx_hw_supported_antenna_mask & (1 << antsel->rxAnt)))) {
    ELOG_DDDDD("CID-%04x: Antenna selection is not supported by HW: txAnt=%d, rxAnt=%d, TX/RX mask: 0x%x/0x%x",
    mtlk_vap_get_oid(core->vap_handle), antsel->txAnt, antsel->rxAnt, tx_hw_supported_antenna_mask, rx_hw_supported_antenna_mask);
    res = MTLK_ERR_PARAMS;
    goto END;
  }


  man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, mtlk_vap_get_txmm(core->vap_handle), NULL);
  if (!man_entry) {
    res = MTLK_ERR_NO_RESOURCES;
    goto END;
  }

  man_entry->id = UM_MAN_SEND_11B_SET_ANT_REQ;
  man_entry->payload_size = sizeof(*umi_11b_antsel);

  umi_11b_antsel = (UMI_ANT_SELECTION_11B *)man_entry->payload;

  umi_11b_antsel->txAnt = antsel->txAnt;
  umi_11b_antsel->rxAnt = antsel->rxAnt;
  umi_11b_antsel->rate  = antsel->rate;

  res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);
  if (res != MTLK_ERR_OK) {
    ELOG_DD("CID-%04x: Cannot send UM_MAN_SEND_11B_SET_ANT_REQ to the FW (err=%d)", mtlk_vap_get_oid(core->vap_handle), res);
    goto END;
  }

END:
  if (man_entry) {
    mtlk_txmm_msg_cleanup(&man_msg);
  }

  return res;
}

static int
_mtlk_core_set_11b_cfg (mtlk_handle_t hcore, const void* data, uint32 data_size)
{
  uint32 res = MTLK_ERR_OK;
  mtlk_core_t *core = (mtlk_core_t*)hcore;
  wave_radio_t *radio;
  mtlk_11b_cfg_t *_11b_cfg = NULL;
  uint32 _11b_cfg_size;
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);
  MTLK_ASSERT(core != NULL);

  radio = wave_vap_radio_get(core->vap_handle);
  MTLK_ASSERT(radio != NULL);

  _11b_cfg = mtlk_clpb_enum_get_next(clpb, &_11b_cfg_size);
  MTLK_CLPB_TRY(_11b_cfg, _11b_cfg_size)
    MTLK_CFG_START_CHEK_ITEM_AND_CALL()
      MTLK_CFG_CHECK_ITEM_AND_CALL(_11b_cfg, antsel, _mtlk_core_send_11b_antsel,
                                   (core, &_11b_cfg->antsel), res);
      WAVE_RADIO_PDB_SET_INT(radio, PARAM_DB_RADIO_11B_ANTSEL_TXANT, _11b_cfg->antsel.txAnt);
      WAVE_RADIO_PDB_SET_INT(radio, PARAM_DB_RADIO_11B_ANTSEL_RXANT, _11b_cfg->antsel.rxAnt);
      WAVE_RADIO_PDB_SET_INT(radio, PARAM_DB_RADIO_11B_ANTSEL_RATE,  _11b_cfg->antsel.rate);
    MTLK_CFG_END_CHEK_ITEM_AND_CALL()

  MTLK_CLPB_FINALLY(res)
    return mtlk_clpb_push_res(clpb, res);
  MTLK_CLPB_END
}

static int
_mtlk_core_receive_11b_antsel (mtlk_core_t *core, mtlk_11b_antsel_t *antsel)
{
  int res;
  mtlk_txmm_msg_t man_msg;
  mtlk_txmm_data_t *man_entry = NULL;
  UMI_ANT_SELECTION_11B *umi_11b_antsel = NULL;

  man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, mtlk_vap_get_txmm(core->vap_handle), NULL);

  if (!man_entry)
    return MTLK_ERR_NO_RESOURCES;

  man_entry->id = UM_MAN_SEND_11B_SET_ANT_REQ;
  man_entry->payload_size = sizeof(*umi_11b_antsel);
  umi_11b_antsel = (UMI_ANT_SELECTION_11B *)man_entry->payload;
  umi_11b_antsel->getSetOperation = API_GET_OPERATION;

  res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);

  if (MTLK_ERR_OK == res) {
    antsel->txAnt = umi_11b_antsel->txAnt;
    antsel->rxAnt = umi_11b_antsel->rxAnt;
    antsel->rate  = umi_11b_antsel->rate;
  }
  else {
    ELOG_D("CID-%04x: Failed to receive UM_MAN_SEND_11B_SET_ANT_REQ", mtlk_vap_get_oid(core->vap_handle));
  }

  mtlk_txmm_msg_cleanup(&man_msg);
  return res;
}

static int
_mtlk_core_get_11b_cfg (mtlk_handle_t hcore, const void* data, uint32 data_size)
{
  uint32 res = MTLK_ERR_OK;
  mtlk_11b_cfg_t _11b_cfg;
  mtlk_core_t *core = (mtlk_core_t*)hcore;
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  memset(&_11b_cfg, 0, sizeof(_11b_cfg));

  MTLK_CFG_START_CHEK_ITEM_AND_CALL()
    MTLK_CFG_SET_ITEM_BY_FUNC(&_11b_cfg, antsel, _mtlk_core_receive_11b_antsel,
                              (core, &_11b_cfg.antsel), res);
  MTLK_CFG_END_CHEK_ITEM_AND_CALL()

  res = mtlk_clpb_push(clpb, &res, sizeof(res));
  if (MTLK_ERR_OK == res) {
    res = mtlk_clpb_push(clpb, &_11b_cfg, sizeof(_11b_cfg));
  }

  return res;
}

static int
_mtlk_core_set_recovery_cfg (mtlk_handle_t hcore, const void *data, uint32 data_size)
{
  uint32 res = MTLK_ERR_OK;
  mtlk_core_t *core = (mtlk_core_t*)hcore;
  mtlk_rcvry_cfg_t *_rcvry_cfg = NULL;
  uint32 _rcvry_cfg_size;
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **)data;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  _rcvry_cfg = mtlk_clpb_enum_get_next(clpb, &_rcvry_cfg_size);
  MTLK_CLPB_TRY(_rcvry_cfg, _rcvry_cfg_size)
    MTLK_CFG_START_CHEK_ITEM_AND_CALL()
      MTLK_CFG_CHECK_ITEM_AND_CALL(_rcvry_cfg, recovery_cfg, wave_rcvry_cfg_set,
                                   (mtlk_vap_get_hw(core->vap_handle),
                                    &_rcvry_cfg->recovery_cfg), res);
    MTLK_CFG_END_CHEK_ITEM_AND_CALL()
  MTLK_CLPB_FINALLY(res)
    return mtlk_clpb_push_res(clpb, res);
  MTLK_CLPB_END
}

static int
_mtlk_core_get_recovery_cfg (mtlk_handle_t hcore, const void *data, uint32 data_size)
{
  uint32 res = MTLK_ERR_OK;
  mtlk_rcvry_cfg_t _rcvry_cfg;
  mtlk_core_t *core = (mtlk_core_t*)hcore;
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  memset(&_rcvry_cfg, 0, sizeof(_rcvry_cfg));

  MTLK_CFG_START_CHEK_ITEM_AND_CALL()
    MTLK_CFG_SET_ITEM_BY_FUNC(&_rcvry_cfg, recovery_cfg, wave_rcvry_cfg_get,
                              (mtlk_vap_get_hw(core->vap_handle),
                               &_rcvry_cfg.recovery_cfg), res);
  MTLK_CFG_END_CHEK_ITEM_AND_CALL()

  res = mtlk_clpb_push(clpb, &res, sizeof(res));
  if (MTLK_ERR_OK == res) {
    res = mtlk_clpb_push(clpb, &_rcvry_cfg, sizeof(_rcvry_cfg));
  }
  return res;
}

static int
_mtlk_core_get_recovery_stats (mtlk_handle_t hcore, const void *data, uint32 data_size)
{
  int res = MTLK_ERR_OK;
  mtlk_rcvry_stats_t _rcvry_stats;
  mtlk_core_t *core = (mtlk_core_t*)hcore;
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  memset(&_rcvry_stats, 0, sizeof(_rcvry_stats));

  MTLK_CFG_START_CHEK_ITEM_AND_CALL()
    MTLK_CFG_SET_ITEM_BY_FUNC_VOID(&_rcvry_stats, recovery_stats, wave_hw_get_recovery_stats,
                              (mtlk_vap_get_hw(core->vap_handle), &_rcvry_stats.recovery_stats));
  MTLK_CFG_END_CHEK_ITEM_AND_CALL()

  return mtlk_clpb_push_res_data(clpb, res, &_rcvry_stats, sizeof(_rcvry_stats));
}

static int
_mtlk_core_stop_lm(mtlk_handle_t hcore, const void* data, uint32 data_size)
{
  int res = MTLK_ERR_OK;
  mtlk_core_t *core = HANDLE_T_PTR(mtlk_core_t, hcore);

  mtlk_txmm_msg_t   man_msg;
  mtlk_txmm_data_t *man_entry = NULL;

  man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, mtlk_vap_get_txmm(core->vap_handle), &res);
  if (!man_entry) {
    ELOG_D("CID-%04x: Can't stop lower MAC due to the lack of MAN_MSG", mtlk_vap_get_oid(core->vap_handle));
    res = MTLK_ERR_NO_MEM;
    goto end;
  }

  man_entry->id           = UM_LM_STOP_REQ;
  man_entry->payload_size = 0;

  res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);
  if (MTLK_ERR_OK != res) {
    ELOG_D("CID-%04x: Can't stop lower MAC, timed-out", mtlk_vap_get_oid(core->vap_handle));
  }

  mtlk_txmm_msg_cleanup(&man_msg);
end:
  return res;
}

static int
_mtlk_core_get_iw_generic (mtlk_handle_t hcore, const void* data, uint32 data_size)
{
  int res = MTLK_ERR_OK;

  UMI_GENERIC_MAC_REQUEST *req_df_cfg = NULL;
  UMI_GENERIC_MAC_REQUEST *pdata = NULL;
  uint32 req_df_cfg_size;
  mtlk_core_t *core = HANDLE_T_PTR(mtlk_core_t, hcore);
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;

  mtlk_txmm_msg_t   man_msg;
  mtlk_txmm_data_t *man_entry = NULL;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  req_df_cfg = mtlk_clpb_enum_get_next(clpb, &req_df_cfg_size);
  MTLK_CLPB_TRY(req_df_cfg, req_df_cfg_size)
    man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, mtlk_vap_get_txmm(core->vap_handle), &res);
    if (!man_entry) {
      ELOG_D("CID-%04x: Can't send request to MAC due to the lack of MAN_MSG", mtlk_vap_get_oid(core->vap_handle));
      MTLK_CLPB_EXIT(MTLK_ERR_NO_MEM);
    }

    pdata = (UMI_GENERIC_MAC_REQUEST*)man_entry->payload;
    man_entry->id           = UM_MAN_GENERIC_MAC_REQ;
    man_entry->payload_size = sizeof(*pdata);

    pdata->opcode=  cpu_to_le32(req_df_cfg->opcode);
    pdata->size=  cpu_to_le32(req_df_cfg->size);
    pdata->action=  cpu_to_le32(req_df_cfg->action);
    pdata->res0=  cpu_to_le32(req_df_cfg->res0);
    pdata->res1=  cpu_to_le32(req_df_cfg->res1);
    pdata->res2=  cpu_to_le32(req_df_cfg->res2);
    pdata->retStatus=  cpu_to_le32(req_df_cfg->retStatus);

    res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);
    if (MTLK_ERR_OK != res) {
      ELOG_D("CID-%04x: Can't send generic request to MAC, timed-out", mtlk_vap_get_oid(core->vap_handle));
      MTLK_CLPB_EXIT(res);
    }

    /* Send response to DF user */
    pdata->opcode=  cpu_to_le32(pdata->opcode);
    pdata->size=  cpu_to_le32(pdata->size);
    pdata->action=  cpu_to_le32(pdata->action);
    pdata->res0=  cpu_to_le32(pdata->res0);
    pdata->res1=  cpu_to_le32(pdata->res1);
    pdata->res2=  cpu_to_le32(pdata->res2);
    pdata->retStatus=  cpu_to_le32(pdata->retStatus);

    res = mtlk_clpb_push(clpb, pdata, sizeof(*pdata));
  MTLK_CLPB_FINALLY(res)
    if (man_entry) mtlk_txmm_msg_cleanup(&man_msg);

    /* Don't need to push result to clipboard */
    return res;
  MTLK_CLPB_END
}

static int
_mtlk_core_gen_dataex_get_connection_stats (mtlk_core_t *core,
                                            WE_GEN_DATAEX_REQUEST *preq,
                                            mtlk_clpb_t *clpb)
{
  int res = MTLK_ERR_OK;
  WE_GEN_DATAEX_CONNECTION_STATUS dataex_conn_status;
  WE_GEN_DATAEX_RESPONSE resp;
  int nof_connected;
  const sta_entry *sta = NULL;
  mtlk_stadb_iterator_t iter;

  memset(&resp, 0, sizeof(resp));
  memset(&dataex_conn_status, 0, sizeof(dataex_conn_status));

  resp.ver = WE_GEN_DATAEX_PROTO_VER;
  resp.status = WE_GEN_DATAEX_SUCCESS;
  resp.datalen = sizeof(WE_GEN_DATAEX_CONNECTION_STATUS);

  if (preq->datalen < resp.datalen) {
    return MTLK_ERR_NO_MEM;
  }

  memset(&dataex_conn_status, 0, sizeof(WE_GEN_DATAEX_CONNECTION_STATUS));

  nof_connected = 0;

  sta = mtlk_stadb_iterate_first(&core->slow_ctx->stadb, &iter);
  if (sta) {
    do {
      WE_GEN_DATAEX_DEVICE_STATUS dataex_dev_status;
      dataex_dev_status.u32RxCount    = mtlk_sta_get_stat_cntr_rx_frames(sta);
      dataex_dev_status.u32TxCount    = mtlk_sta_get_stat_cntr_tx_frames(sta);

      resp.datalen += sizeof(WE_GEN_DATAEX_DEVICE_STATUS);
      if (preq->datalen < resp.datalen) {
        res = MTLK_ERR_NO_MEM;
        break;
      }

      res = mtlk_clpb_push(clpb, &dataex_dev_status, sizeof(WE_GEN_DATAEX_DEVICE_STATUS));
      if (MTLK_ERR_OK != res) {
        break;
      }

      nof_connected++;

      sta = mtlk_stadb_iterate_next(&iter);
    } while (sta);
    mtlk_stadb_iterate_done(&iter);
  }

  if (MTLK_ERR_OK == res) {
    dataex_conn_status.u32NumOfConnections = nof_connected;
    res = mtlk_clpb_push(clpb, &dataex_conn_status, sizeof(WE_GEN_DATAEX_CONNECTION_STATUS));
  }

  if (MTLK_ERR_OK == res) {
    res = mtlk_clpb_push(clpb, &resp, sizeof(resp));
  }
  return res;
}

static int
_mtlk_core_gen_dataex_get_status (mtlk_core_t *core,
                                  WE_GEN_DATAEX_REQUEST *preq,
                                  mtlk_clpb_t *clpb)
{
  int res = MTLK_ERR_OK;
  const sta_entry *sta;
  WE_GEN_DATAEX_RESPONSE resp;
  WE_GEN_DATAEX_STATUS status;
  mtlk_stadb_iterator_t iter;

  memset(&resp, 0, sizeof(resp));
  memset(&status, 0, sizeof(status));

  if (preq->datalen < sizeof(status)) {
    resp.status = WE_GEN_DATAEX_DATABUF_TOO_SMALL;
    resp.datalen = sizeof(status);
    goto end;
  }

  resp.ver = WE_GEN_DATAEX_PROTO_VER;
  resp.status = WE_GEN_DATAEX_SUCCESS;
  resp.datalen = sizeof(status);

  memset(&status, 0, sizeof(status));

  status.security_on = 0;
  status.wep_enabled = 0;

  sta = mtlk_stadb_iterate_first(&core->slow_ctx->stadb, &iter);
  if (sta) {
    do {
      /* Check global WEP enabled flag only if some STA connected */
      if ((mtlk_sta_get_cipher(sta) != IW_ENCODE_ALG_NONE) || core->slow_ctx->wep_enabled) {
        status.security_on = 1;
        if (core->slow_ctx->wep_enabled) {
          status.wep_enabled = 1;
        }
        break;
      }
      sta = mtlk_stadb_iterate_next(&iter);
    } while (sta);
    mtlk_stadb_iterate_done(&iter);
  }

  status.scan_started = mtlk_core_scan_is_running(core);
  if (!mtlk_vap_is_slave_ap(core->vap_handle)) {
    status.frequency_band = core_cfg_get_freq_band_cur(core);
  }
  else {
    status.frequency_band = MTLK_HW_BAND_NONE;
  }
  status.link_up = (mtlk_core_get_net_state(core) == NET_STATE_CONNECTED) ? 1 : 0;

  res = mtlk_clpb_push(clpb, &status, sizeof(status));
  if (MTLK_ERR_OK == res) {
    res = mtlk_clpb_push(clpb, &resp, sizeof(resp));
  }

end:
  return res;
}

static int
_mtlk_core_gen_data_exchange (mtlk_handle_t hcore, const void* data, uint32 data_size)
{
  int res = MTLK_ERR_OK;

  mtlk_core_ui_gen_data_t *req = NULL;
  uint32 req_size;
  mtlk_core_t *core = HANDLE_T_PTR(mtlk_core_t, hcore);
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  req = mtlk_clpb_enum_get_next(clpb, &req_size);
  MTLK_CLPB_TRY(req, req_size)
    switch (req->request.cmd_id) {
      case WE_GEN_DATAEX_CMD_CONNECTION_STATS:
        res = _mtlk_core_gen_dataex_get_connection_stats(core, &req->request, clpb);
        break;
      case WE_GEN_DATAEX_CMD_STATUS:
        res = _mtlk_core_gen_dataex_get_status(core, &req->request, clpb);
        break;
      default:
        MTLK_ASSERT(0);
    }
  MTLK_CLPB_FINALLY(res)
    return res;
  MTLK_CLPB_END
}

uint32
mtlk_core_get_available_bitrates (struct nic *nic)
{
  uint8 net_mode;
  uint32 mask = 0;

  /* Get all needed MIBs */
  net_mode = core_cfg_get_network_mode(nic);
  mask = get_operate_rate_set(net_mode);
  ILOG3_D("Configuration mask: 0x%08x", mask);

  return mask;
}

int mtlk_core_is_chandef_identical(struct mtlk_chan_def *chan1, struct mtlk_chan_def *chan2)
{
  return (chan1->width == chan2->width
        && chan1->center_freq1 == chan2->center_freq1
        && chan1->center_freq2 == chan2->center_freq2
        && chan1->chan.center_freq == chan2->chan.center_freq);
}

int mtlk_core_is_band_supported(mtlk_core_t *nic, mtlk_hw_band_e band)
{
  mtlk_hw_t    *hw = mtlk_vap_get_hw(nic->vap_handle);
  wave_radio_t *radio = wave_vap_radio_get(nic->vap_handle);
  unsigned      radio_id = wave_radio_id_get(radio);
  int           res = MTLK_ERR_NOT_SUPPORTED;

  if ((band != MTLK_HW_BAND_BOTH) && /* VAP (station or AP role) can't be dual-band */
      (mtlk_is_band_supported(HANDLE_T(hw), radio_id, band)))
  {
    res = MTLK_ERR_OK;
  }

  return res;
}

int __MTLK_IFUNC
hw_mmb_set_rtlog_cfg(mtlk_hw_t *hw, void *buff, uint32 size);

static int
_mtlk_core_set_rtlog_cfg (mtlk_handle_t hcore,
                          const void* data, uint32 size)
{
  mtlk_core_t *core = (mtlk_core_t*)hcore;
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;
  uint32        cfg_size;
  void         *cfg_data; /* FIXME */

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == size);

  cfg_data = mtlk_clpb_enum_get_next(clpb, &cfg_size);
  MTLK_ASSERT(NULL != cfg_data);
  if (NULL == cfg_data) {
    ELOG_SD("%s(%d): failed to get data from clipboard", __FUNCTION__, __LINE__);
    return MTLK_ERR_UNKNOWN;
  }

  mtlk_dump(2, cfg_data, MIN(64, cfg_size), "cfg_data");

  /* Don't need to push result to clipboard */
  return hw_mmb_set_rtlog_cfg(mtlk_vap_get_hw(core->vap_handle), cfg_data, cfg_size);
}

static int
_mtlk_core_mcast_group_id_action (mtlk_handle_t core_object, const void *payload, uint32 size)
{
  struct nic *nic = HANDLE_T_PTR(struct nic, core_object);
  mtlk_core_ui_mc_action_t *req = (mtlk_core_ui_mc_action_t *)payload;
  sta_entry *sta = NULL;

  MTLK_ASSERT(size == sizeof(mtlk_core_ui_mc_action_t));

  ILOG1_DDSDY("CID-%04x: action=%d (%s), group=%d, sta addr=%Y", mtlk_vap_get_oid(nic->vap_handle), req->action,
    ((req->action == MTLK_MC_STA_JOIN_GROUP) ? "join group" : ((req->action == MTLK_MC_STA_LEAVE_GROUP) ? "leave group" : "unknown")),
    req->grp_id, &req->sta_mac_addr);
  if (MTLK_IPv4 == req->mc_addr.type)
    ILOG1_D("dst IPv4:%B", htonl(req->mc_addr.grp_ip.ip4_addr.s_addr));
  else
    ILOG1_K("dst IPv6:%K", req->mc_addr.grp_ip.ip6_addr.s6_addr);

  sta = mtlk_stadb_find_sta(&nic->slow_ctx->stadb, req->sta_mac_addr.au8Addr);
  if (NULL == sta) {
    sta = mtlk_hstdb_find_sta(&nic->slow_ctx->hstdb, req->sta_mac_addr.au8Addr);
    if (NULL == sta) {
      ILOG1_DY("CID-%04x: can't find sta:%Y", mtlk_vap_get_oid(nic->vap_handle), req->sta_mac_addr.au8Addr);
      return MTLK_ERR_OK;
    }
  }

  mtlk_mc_update_group_id_sta(nic, req->grp_id, req->action, &req->mc_addr, sta);
  mtlk_sta_decref(sta);

  return MTLK_ERR_OK;
}

int _mtlk_core_mcast_helper_group_id_action (mtlk_handle_t hcore, const void* data, uint32 data_size)
{
  mtlk_core_t *core = HANDLE_T_PTR(mtlk_core_t, hcore);
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;
  mtlk_core_ui_mc_update_sta_db_t *req;
  unsigned r_size;
  int res = MTLK_ERR_OK;

  ILOG1_V("mcast_helper group action invoked");

  MTLK_ASSERT(sizeof(mtlk_clpb_t *) == data_size);
  req = mtlk_clpb_enum_get_next(clpb, &r_size);
  MTLK_CLPB_TRY(req, r_size)
    mtlk_mc_update_stadb(core, req);
    mtlk_osal_mem_free(req->macs_list);
  MTLK_CLPB_FINALLY(res)
    return res;
  MTLK_CLPB_END
}

int
mtlk_core_update_network_mode(mtlk_core_t* nic, uint8 net_mode)
{
  mtlk_core_t *core = nic;
  uint8 band_new = net_mode_to_band(net_mode);
  wave_radio_t *radio;

  MTLK_ASSERT(NULL != nic);

  radio = wave_vap_radio_get(nic->vap_handle);
  if (mtlk_core_is_band_supported(core, band_new) != MTLK_ERR_OK) {
    if (band_new == MTLK_HW_BAND_BOTH) {
      /*
       * Just in case of single-band hardware
       * continue to use `default' frequency band,
       * which is de facto correct.
       */
      ELOG_D("CID-%04x: dualband isn't supported", mtlk_vap_get_oid(core->vap_handle));
      return MTLK_ERR_OK;
    } else {
      ELOG_DSD("CID-%04x: %s band isn't supported (%d)", mtlk_vap_get_oid(core->vap_handle),
              mtlk_eeprom_band_to_string(net_mode_to_band(net_mode)), net_mode);
      return MTLK_ERR_NOT_SUPPORTED;
    }
  }

  ILOG1_S("Set Network Mode to %s", net_mode_to_string(net_mode));

  MTLK_CORE_PDB_SET_INT(core, PARAM_DB_CORE_NET_MODE_CUR, net_mode);
  MTLK_CORE_PDB_SET_INT(core, PARAM_DB_CORE_NET_MODE_CFG, net_mode);

  MTLK_CORE_PDB_SET_INT(core, PARAM_DB_CORE_IS_HT_CUR, is_ht_net_mode(net_mode));
  MTLK_CORE_PDB_SET_INT(core, PARAM_DB_CORE_IS_HT_CFG, is_ht_net_mode(net_mode));

  WAVE_RADIO_PDB_SET_INT(radio, PARAM_DB_RADIO_FREQ_BAND_CFG, band_new);
  WAVE_RADIO_PDB_SET_INT(radio, PARAM_DB_RADIO_FREQ_BAND_CUR, band_new);

  /* The set of supported bands may be changed by this request.           */
  /* Scan cache to be cleared to throw out BSS from unsupported now bands */
  mtlk_cache_clear(&core->slow_ctx->cache);
  return MTLK_ERR_OK;
}

static int
_mtlk_core_get_ta_cfg (mtlk_handle_t hcore, const void* data, uint32 data_size)
{
  uint32 res = MTLK_ERR_OK;
  mtlk_ta_cfg_t *ta_cfg;
  mtlk_core_t *core = (mtlk_core_t*)hcore;
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  ta_cfg = mtlk_osal_mem_alloc(sizeof(*ta_cfg), MTLK_MEM_TAG_CLPB);
  if(ta_cfg == NULL) {
    ELOG_D("CID-%04x: Can't allocate clipboard data", mtlk_vap_get_oid(core->vap_handle));
    return MTLK_ERR_NO_MEM;
  }

  memset(ta_cfg, 0, sizeof(*ta_cfg));

  MTLK_CFG_SET_ITEM(ta_cfg, timer_resolution,
    mtlk_ta_get_timer_resolution_ticks(mtlk_vap_get_ta(core->vap_handle)));

  MTLK_CFG_SET_ITEM_BY_FUNC_VOID(ta_cfg, debug_info, mtlk_ta_get_debug_info,
                                 (mtlk_vap_get_ta(core->vap_handle), &ta_cfg->debug_info));

  res = mtlk_clpb_push(clpb, &res, sizeof(res));
  if (MTLK_ERR_OK == res) {
    res = mtlk_clpb_push_nocopy(clpb, ta_cfg, sizeof(*ta_cfg));
    if(MTLK_ERR_OK != res) {
      mtlk_osal_mem_free(ta_cfg);
    }
  }

  return res;
}

static int
_mtlk_core_set_ta_cfg (mtlk_handle_t hcore, const void* data, uint32 data_size)
{
  uint32 res = MTLK_ERR_OK;
  mtlk_ta_cfg_t *ta_cfg;
  uint32  ta_cfg_size;
  mtlk_core_t *core = (mtlk_core_t*)hcore;
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  ta_cfg = mtlk_clpb_enum_get_next(clpb, &ta_cfg_size);
  MTLK_CLPB_TRY(ta_cfg, ta_cfg_size)
    MTLK_CFG_START_CHEK_ITEM_AND_CALL()
      MTLK_CFG_CHECK_ITEM_AND_CALL(ta_cfg, timer_resolution, mtlk_ta_set_timer_resolution_ticks,
                                   (mtlk_vap_get_ta(core->vap_handle), ta_cfg->timer_resolution), res);
    MTLK_CFG_END_CHEK_ITEM_AND_CALL()
  MTLK_CLPB_FINALLY(res)
    return mtlk_clpb_push_res(clpb, res);
  MTLK_CLPB_END
}

/*
 * Definitions and macros below are used only for the packet's header transformation
 * For more information, please see following documents:
 *   - IEEE 802.1H standard
 *   - IETF RFC 1042
 *   - IEEE 802.11n standard draft 5 Annex M
 * */

#define _8021H_LLC_HI4BYTES             0xAAAA0300
#define _8021H_LLC_LO2BYTES_CONVERT     0x0000
#define RFC1042_LLC_LO2BYTES_TUNNEL     0x00F8

/* Default ISO/IEC conversion
 * we need to keep full LLC header and store packet length in the T/L subfield */
#define _8021H_CONVERT(ether_header, nbuf, data_offset) \
  data_offset -= sizeof(struct ethhdr); \
  ether_header = (struct ethhdr *)(nbuf->data + data_offset); \
  ether_header->h_proto = htons(nbuf->len - data_offset - sizeof(struct ethhdr))

/* 802.1H encapsulation
 * we need to remove LLC header except the 'type' field */
#define _8021H_DECAPSULATE(ether_header, nbuf, data_offset) \
  data_offset -= sizeof(struct ethhdr) - (sizeof(mtlk_snap_hdr_t) + sizeof(mtlk_llc_hdr_t)); \
  ether_header = (struct ethhdr *)(nbuf->data + data_offset)

static int
_handle_rx_ind (mtlk_core_t *nic, mtlk_nbuf_t *nbuf, mtlk_core_handle_rx_data_t *data)
{
  int res = MTLK_ERR_OK; /* Do not free nbuf */
  uint32 qos = 0;
  sta_entry *src_sta = NULL;
  struct ethhdr *ether_header = (struct ethhdr *)nbuf->data;

  ILOG4_V("Rx indication");
  mtlk_core_inc_cnt(nic, MTLK_CORE_CNT_DAT_FRAMES_RECEIVED);

  mtlk_eth_parser(nbuf->data, nbuf->len, mtlk_df_get_name(mtlk_vap_get_df(nic->vap_handle)), "RX");
  mtlk_dump(3, nbuf->data, MIN(64, nbuf->len), "dump of recvd 802.3 packet");

  /* Try to find source MAC of transmitter */
  src_sta = mtlk_stadb_find_sid(&nic->slow_ctx->stadb, data->sid);
  if (src_sta == NULL) {
    ILOG2_V("SOURCE of RX packet not found!");
    res = MTLK_ERR_NOT_IN_USE; /* Free nbuf */
    goto end;
  }

  ILOG5_YD("STA %Y found by SID %d", src_sta->info.addr.au8Addr, data->sid);

  qos = data->priority;
#ifdef MTLK_DEBUG_CHARIOT_OOO
  {
    /* Get pointer to private area */
    mtlk_nbuf_priv_t *nbuf_priv = mtlk_nbuf_priv(nbuf);
    nbuf_priv->seq_qos = qos;
  }
#endif

  if (__LIKELY(qos < NTS_TIDS_GEN6))
    nic->pstats.ac_rx_counter[qos]++;
  else
    ELOG_D("Invalid priority: %u", qos);
  nic->pstats.sta_session_rx_packets++;

  /* In Backhaul AP mode only one STA can be connected (IRE), so we do not need to update hostdb */
  if (MESH_MODE_BACKHAUL_AP != MTLK_CORE_HOT_PATH_PDB_GET_INT(nic, CORE_DB_CORE_MESH_MODE))
  {
    /* Check if packet received from WDS HOST or 4 address station */
    if (src_sta->peer_ap || mtlk_sta_is_4addr(src_sta)) {
      /* On AP we need to update HOST's entry in database of registered
       * HOSTs behind connected STAs */
      mtlk_hstdb_update_host(&nic->slow_ctx->hstdb, ether_header->h_source, src_sta);
    }
  }

  {
    mtlk_core_handle_tx_data_t tx_data;
    memset(&tx_data, 0, sizeof(tx_data));
    tx_data.nbuf = nbuf;
    mtlk_core_analyze_and_send_up(nic, &tx_data, src_sta);
  }

end:
  if (src_sta) mtlk_sta_decref(src_sta); /* De-reference of find */
  return res;
}

/* Funstions for STA DB hash */
__INLINE void
_mtlk_core_flush_ieee_addr_list (mtlk_core_t *nic,
                                 ieee_addr_list_t *list, char *name)
{
  mtlk_hash_enum_t              e;
  MTLK_HASH_ENTRY_T(ieee_addr) *h;
  ieee_addr_entry_t            *ieee_addr_entry;

  MTLK_UNREFERENCED_PARAM(name);
  ILOG2_DS("CID-%04x: %s list flush", mtlk_vap_get_oid(nic->vap_handle), name);

  mtlk_osal_lock_acquire(&list->ieee_addr_lock);
  h = mtlk_hash_enum_first_ieee_addr(&list->hash, &e);
  while (h) {
    ILOG2_Y("\t remove %Y", h->key.au8Addr);
    ieee_addr_entry = MTLK_CONTAINER_OF(h, ieee_addr_entry_t, hentry);
    mtlk_hash_remove_ieee_addr(&list->hash, &ieee_addr_entry->hentry);
    mtlk_osal_mem_free(ieee_addr_entry);
    h = mtlk_hash_enum_next_ieee_addr(&list->hash, &e);
  }
  mtlk_osal_lock_release(&list->ieee_addr_lock);
}

__INLINE BOOL
_mtlk_core_ieee_addr_entry_exists (mtlk_core_t *nic, const IEEE_ADDR *addr,
                                   ieee_addr_list_t *list, char *name)
{
  MTLK_HASH_ENTRY_T(ieee_addr) *h;

  MTLK_UNREFERENCED_PARAM(name);
  ILOG3_DSY("CID-%04x: looking for %s list entry %Y",
           mtlk_vap_get_oid(nic->vap_handle), name, addr->au8Addr);

  mtlk_osal_lock_acquire(&list->ieee_addr_lock);
  h = mtlk_hash_find_ieee_addr(&list->hash, addr);
  mtlk_osal_lock_release(&list->ieee_addr_lock);
  if (!h) {
    ILOG3_DSY("CID-%04x: %s list entry NOT found %Y",
             mtlk_vap_get_oid(nic->vap_handle), name, addr->au8Addr);
  }
  return (h != NULL);
}

__INLINE void*
__mtlk_core_add_ieee_addr_entry_internal (mtlk_core_t *nic, const IEEE_ADDR *addr,
                                   ieee_addr_list_t *list, char *name, size_t extra_size)
{
  ieee_addr_entry_t *ieee_addr_entry = NULL;
  MTLK_HASH_ENTRY_T(ieee_addr) *h;
  size_t alloc_size = sizeof(*ieee_addr_entry) + extra_size;

  ieee_addr_entry = mtlk_osal_mem_alloc(alloc_size, MTLK_MEM_TAG_IEEE_ADDR_LIST);
  if (!ieee_addr_entry) {
    WLOG_V("Can't alloc list entry");
    return NULL;
  }
  memset(ieee_addr_entry, 0, alloc_size);

  mtlk_osal_lock_acquire(&list->ieee_addr_lock);
  h = mtlk_hash_insert_ieee_addr(&list->hash, addr, &ieee_addr_entry->hentry);
  mtlk_osal_lock_release(&list->ieee_addr_lock);
  if (h) {
    mtlk_osal_mem_free(ieee_addr_entry);
    ieee_addr_entry = MTLK_CONTAINER_OF(h, ieee_addr_entry_t, hentry);
    WLOG_DYS("CID-%04x: %Y already in %s list", mtlk_vap_get_oid(nic->vap_handle), addr->au8Addr, name);
  } else {
    ILOG3_DSY("CID-%04x: %s list entry add %Y", mtlk_vap_get_oid(nic->vap_handle), name, addr->au8Addr);
  }

  return ieee_addr_entry;
}

__INLINE int
_mtlk_core_add_ieee_addr_entry (mtlk_core_t *nic, const IEEE_ADDR *addr,
                                   ieee_addr_list_t *list, char *name)
{
  return ((__mtlk_core_add_ieee_addr_entry_internal (nic, addr, list, name, 0)
      == NULL) ? MTLK_ERR_NO_MEM : MTLK_ERR_OK);
}

__INLINE int
_mtlk_core_del_ieee_addr_entry (mtlk_core_t* nic, const IEEE_ADDR *addr,
                                   ieee_addr_list_t *list, char *name,
                                   BOOL entry_expected)
{
  MTLK_HASH_ENTRY_T(ieee_addr) *h;

  ILOG3_DSY("CID-%04x: %s list entry del %Y",
            mtlk_vap_get_oid(nic->vap_handle), name, addr->au8Addr);

  mtlk_osal_lock_acquire(&list->ieee_addr_lock);
  h = mtlk_hash_find_ieee_addr(&list->hash, addr); /* find address list entry in address list */
  if (h) {
    ieee_addr_entry_t *ieee_addr_entry = MTLK_CONTAINER_OF(h, ieee_addr_entry_t, hentry);
    mtlk_hash_remove_ieee_addr(&list->hash, &ieee_addr_entry->hentry);
    mtlk_osal_mem_free(ieee_addr_entry);
    mtlk_osal_lock_release(&list->ieee_addr_lock);
  } else {
    mtlk_osal_lock_release(&list->ieee_addr_lock);
    if (entry_expected) {
      ILOG0_DSY("CID-%04x: %s list entry %Y doesn't exist",
               mtlk_vap_get_oid(nic->vap_handle), name, addr->au8Addr);
    } else {
      ILOG2_DSY("CID-%04x: %s list entry %Y doesn't exist",
               mtlk_vap_get_oid(nic->vap_handle), name, addr->au8Addr);
    }
  }
  return MTLK_ERR_OK;
}


__INLINE int
_mtlk_core_dump_ieee_addr_list (mtlk_core_t *nic,
                                ieee_addr_list_t *list, char *name,
                                const void* data, uint32 data_size)
{
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;
  mtlk_hash_enum_t             e;
  MTLK_HASH_ENTRY_T(ieee_addr) *h;
  int                          res = MTLK_ERR_UNKNOWN;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);
  MTLK_ASSERT(nic);
  MTLK_ASSERT(clpb);
  MTLK_UNREFERENCED_PARAM(name);
  ILOG2_DS("CID-%04x: %s list dump", mtlk_vap_get_oid(nic->vap_handle), name);

  mtlk_osal_lock_acquire(&list->ieee_addr_lock);
  h = mtlk_hash_enum_first_ieee_addr(&list->hash, &e);
  while (h) {
    ILOG2_Y("\t dump %Y", h->key.au8Addr);
    if (MTLK_ERR_OK != (res = mtlk_clpb_push(clpb, h, sizeof(*h)))) {
      mtlk_osal_lock_release(&list->ieee_addr_lock);
      goto err_push;
    }
    h = mtlk_hash_enum_next_ieee_addr(&list->hash, &e);
  }
  mtlk_osal_lock_release(&list->ieee_addr_lock);

  return MTLK_ERR_OK;

err_push:
  mtlk_clpb_purge(clpb);
  return res;
}

__INLINE int
__mtlk_core_add_blacklist_addr_entry (mtlk_core_t *nic, struct intel_vendor_blacklist_cfg *blacklist_cfg,
                                   ieee_addr_list_t *list, char *name)
{
  ieee_addr_entry_t *ieee_addr_entry = NULL;

  ieee_addr_entry = __mtlk_core_add_ieee_addr_entry_internal(nic, &blacklist_cfg->addr, list,
                        name, sizeof(blacklist_snr_info_t));

  if (ieee_addr_entry) {
    /* STA in blacklist.Update the params if any */
    ieee_addr_entry->data[0] = blacklist_cfg->snrProbeHWM;
    ieee_addr_entry->data[1] = blacklist_cfg->snrProbeLWM;
    return MTLK_ERR_OK;
  } else {
    return MTLK_ERR_NO_MEM;
  }
}

/* STA DB hash for filtering the Probe Responses
    1) on broadcast Probe Request (offline)     2) on unicast Probe Request */
static void _mtlk_core_flush_bcast_probe_resp_list (mtlk_core_t *nic)
{
  _mtlk_core_flush_ieee_addr_list(nic, &nic->broadcast_probe_resp_sta_list, "broadcast");
}

static BOOL _mtlk_core_bcast_probe_resp_entry_exists (mtlk_core_t *nic, const IEEE_ADDR *addr)
{
  return _mtlk_core_ieee_addr_entry_exists(nic, addr, &nic->broadcast_probe_resp_sta_list, "broadcast");
}

static int _mtlk_core_add_bcast_probe_resp_entry (mtlk_core_t* nic, const IEEE_ADDR *addr)
{
  return _mtlk_core_add_ieee_addr_entry(nic, addr, &nic->broadcast_probe_resp_sta_list, "broadcast");
}

static int _mtlk_core_del_bcast_probe_resp_entry (mtlk_core_t* nic, const IEEE_ADDR *addr)
{
  return _mtlk_core_del_ieee_addr_entry(nic, addr, &nic->broadcast_probe_resp_sta_list, "broadcast", TRUE);
}

static void _mtlk_core_flush_ucast_probe_resp_list (mtlk_core_t *nic)
{
  _mtlk_core_flush_ieee_addr_list(nic, &nic->unicast_probe_resp_sta_list, "unicast");
}

static BOOL _mtlk_core_ucast_probe_resp_entry_exists (mtlk_core_t *nic, const IEEE_ADDR *addr)
{
  return _mtlk_core_ieee_addr_entry_exists(nic, addr, &nic->unicast_probe_resp_sta_list, "unicast");
}

static int _mtlk_core_add_ucast_probe_resp_entry (mtlk_core_t* nic, const IEEE_ADDR *addr)
{
  return _mtlk_core_add_ieee_addr_entry(nic, addr, &nic->unicast_probe_resp_sta_list, "unicast");
}

static int _mtlk_core_del_ucast_probe_resp_entry (mtlk_core_t* nic, const IEEE_ADDR *addr)
{
  return _mtlk_core_del_ieee_addr_entry(nic, addr, &nic->unicast_probe_resp_sta_list, "unicast", TRUE);
}

/* This function must be called from Master VAP serializer context */
static __INLINE
void mtlk_core_finalize_and_send_probe_resp(mtlk_core_t *core, mtlk_mngmnt_frame_t *frame)
{
  uint64 cookie;
  frame_head_t *head_req, *head_resp;
  unsigned len;
  u8 *buf;

  head_req = (frame_head_t *) frame->frame;

  /* Need to remove HE IEs from probe response for clients that do not support HE */
  if (!frame->probe_req_he_ie) {
    buf = core->slow_ctx->probe_resp_templ_non_he;
    len = core->slow_ctx->probe_resp_templ_non_he_len;
  }
  else
  {
    buf = core->slow_ctx->probe_resp_templ;
    len = core->slow_ctx->probe_resp_templ_len;
  }

  head_resp = (frame_head_t *) buf;
  /* we mess up the dst_addr in the template, but it's OK, we're under a lock and it will get copied into one of the queues in mgmt_tx */
  head_resp->dst_addr = head_req->src_addr;

  ILOG3_DY("CID-%04x: Probe Response to %Y",
           mtlk_vap_get_oid(core->vap_handle), head_resp->dst_addr.au8Addr);

  /* check if have enough room in tx queue. Do not send probe response if not */
  if (mtlk_mmb_bss_mgmt_tx_check(core->vap_handle)) {

    /* Search STA in list. Don't send if found */
    if (_mtlk_core_bcast_probe_resp_entry_exists(core, &head_resp->dst_addr)) {
        ILOG2_DY("CID-%04x: Don't send Probe Response to %Y",
                 mtlk_vap_get_oid(core->vap_handle), head_resp->dst_addr.au8Addr);

      mtlk_core_inc_cnt(core, MTLK_CORE_CNT_TX_PROBE_RESP_DROPPED);
      return;
    }

    /*
     * We are in serializer context. It is important to add entry to the list
     * prior to frame transmission is executed, as CFM may come nearly immediately
     * after HD is copied to the ring. The entry is removed from list in the
     * tasklet context, that might create a racing on entry removal.
     */
    _mtlk_core_add_bcast_probe_resp_entry(core, &head_resp->dst_addr);

    if (MTLK_ERR_OK ==
      mtlk_mmb_bss_mgmt_tx(core->vap_handle, buf, len,
                           freq2lowchannum(frame->freq, CW_20),
                           FALSE, TRUE, TRUE /* broadcast */, &cookie, PROCESS_MANAGEMENT, NULL,
                           FALSE, NTS_TID_USE_DEFAULT)) {
      mtlk_core_inc_cnt(core, MTLK_CORE_CNT_TX_PROBE_RESP_SENT);
    } else {
      /* delete entry if TX failed */
      _mtlk_core_del_bcast_probe_resp_entry(core, &head_resp->dst_addr);
       mtlk_core_inc_cnt(core, MTLK_CORE_CNT_TX_PROBE_RESP_DROPPED);
    }
  } else {
    /* no room in tx queue, frame is dropped */
    mtlk_core_inc_cnt(core, MTLK_CORE_CNT_TX_PROBE_RESP_DROPPED);
  }
}

int __MTLK_IFUNC
mtlk_send_softblock_msg_drop (mtlk_vap_handle_t vap_handle, void *data, int data_len)
{
  mtlk_df_t *df               = mtlk_vap_get_df(vap_handle);
  mtlk_df_user_t *df_user     = mtlk_df_get_user(df);
  struct wireless_dev *wdev   = mtlk_df_user_get_wdev(df_user);
  mtlk_nbuf_t *evt_nbuf;
  uint8* cp;

  MTLK_ASSERT(NULL != wdev);

  evt_nbuf = wv_cfg80211_vendor_event_alloc(wdev, data_len,
                                            LTQ_NL80211_VENDOR_EVENT_SOFTBLOCK_DROP);
  if (!evt_nbuf)
  {
    ELOG_D("Malloc event fail. data_len = %d", data_len);
    return MTLK_ERR_NO_MEM;
  }

  cp = mtlk_df_nbuf_put(evt_nbuf, data_len);
  wave_memcpy(cp, data_len, data, data_len);
  wv_cfg80211_vendor_event(evt_nbuf);

  return MTLK_ERR_OK;
}

enum {
  DRV_SOFTBLOCK_ACCEPT = 0,
  DRV_SOFTBLOCK_DROP = 1,
  DRV_SOFTBLOCK_ALLOW = 2,
  DRV_MULTI_AP_BLACKLIST_FOUND = 3
};

static int
_mtlk_core_mngmnt_softblock_notify (mtlk_core_t *nic, const IEEE_ADDR *addr,
      ieee_addr_list_t *list, char *name, int8 rx_snr_db, BOOL isbroadcast,
      struct intel_vendor_event_msg_drop *prb_req_drop)
{
  MTLK_HASH_ENTRY_T(ieee_addr) *h;
  ieee_addr_entry_t *entry;
  int ret = DRV_SOFTBLOCK_ACCEPT;
  blacklist_snr_info_t *blacklist_snr_info = NULL;

  if (MTLK_CORE_PDB_GET_INT(nic, PARAM_DB_CORE_SOFTBLOCK_DISABLE))
    return ret;

  MTLK_UNREFERENCED_PARAM(name);
  ILOG3_DSY("CID-%04x: looking for %s SoftBlock entry %Y",
      mtlk_vap_get_oid(nic->vap_handle), name, addr->au8Addr);

  mtlk_osal_lock_acquire(&list->ieee_addr_lock);
  h = mtlk_hash_find_ieee_addr(&list->hash, addr); /* finding the mac addr only from the list in address list */
  mtlk_osal_lock_release(&list->ieee_addr_lock);

  if (h) {
    entry = MTLK_CONTAINER_OF(h, ieee_addr_entry_t, hentry);
    ret = DRV_MULTI_AP_BLACKLIST_FOUND;
    blacklist_snr_info = (blacklist_snr_info_t *)&entry->data[0];

    if ((blacklist_snr_info->snrProbeHWM != 0) && (blacklist_snr_info->snrProbeLWM != 0)) {
      /* The case when the notification to be sent to hostap since its configured */
      prb_req_drop->rx_snr = rx_snr_db;
      prb_req_drop->broadcast = isbroadcast;
      prb_req_drop->msgtype = MAN_TYPE_PROBE_REQ;
      prb_req_drop->reason = 0;
      prb_req_drop->rejected = 0;
      prb_req_drop->addr = *addr;
      prb_req_drop->vap_id = mtlk_vap_get_id(nic->vap_handle);

      if ((blacklist_snr_info->snrProbeHWM < rx_snr_db) ||
             (blacklist_snr_info->snrProbeLWM > rx_snr_db)) {
        /* Silently Ignore the message */
        ILOG1_DD("CID-%04x: mgmt::Probe Req Softblock dropped SNR: %d",
              mtlk_vap_get_oid(nic->vap_handle), rx_snr_db);
        /* Notify hostap its dropped */
        ret = DRV_SOFTBLOCK_DROP;
        prb_req_drop->blocked = TRUE;
        /* Trigger notification */
        mtlk_send_softblock_msg_drop(nic->vap_handle, prb_req_drop, sizeof(*prb_req_drop));
      }
      else {
        ILOG1_DD("CID-%04x: mgmt::Probe Req Softblock allowed SNR: %d",
              mtlk_vap_get_oid(nic->vap_handle), rx_snr_db);
        /* Notify hostap its allowed */
        prb_req_drop->blocked = FALSE;
        ret = DRV_SOFTBLOCK_ALLOW; /* Notification will be done when its sent after further checks */
      }
    }
    else {
        return ret;
    }
  }
  return ret;
}

static __INLINE void
__mtlk_core_mngmnt_frame_notify (mtlk_core_t *nic, const u8 *data, int size, int freq,
                                      unsigned subtype, mtlk_phy_info_t *phy_info, uint8 pmf_flags)
{
  BOOL is_broadcast = FALSE;

  if (mtlk_vap_is_ap(nic->vap_handle))
  {
    mtlk_df_t *df               = mtlk_vap_get_df(nic->vap_handle); /* this checks vap_handle and df, cannot return NULL */
    mtlk_df_user_t *df_user     = mtlk_df_get_user(df);
    struct wireless_dev *wdev   = mtlk_df_user_get_wdev(df_user);   /* this checks df_user */

    if (is_multicast_ether_addr((const u8 *)&(((frame_head_t*)data)->dst_addr))) {
      is_broadcast = TRUE;
    }

    /* don't notify about client that is in the blacklist */
    if (_mtlk_core_blacklist_frame_drop(nic, (IEEE_ADDR *)& (((frame_head_t*)data)->src_addr), subtype, phy_info->snr_db, is_broadcast))
      return;

    if (!wv_cfg80211_mngmn_frame(wdev, data, size, freq, phy_info->sig_dbm, phy_info->snr_db, subtype))
    {
      ILOG3_D("CID-%04x: CFG80211 can't pass the RX mgmt frame to the kernel",
              mtlk_vap_get_oid(nic->vap_handle));
              mtlk_core_inc_cnt(nic, MTLK_CORE_CNT_MAN_FRAMES_CFG80211_FAILED);
              mtlk_dump(3, data, MIN(size, 32), "RX BSS IND data (first 32 bytes):");
    }
  }
  else
  {
#if 1 /* FIXME: will be available in phy_info */
    phy_info->rate_idx = mtlk_bitrate_info_to_rate_idx(phy_info->bitrate_info);
#endif
    wv_ieee80211_mngmn_frame_rx(nic, data, size, freq, phy_info->sig_dbm, subtype,
                                phy_info->rate_idx, phy_info->phy_mode, pmf_flags);
  }
}

static __INLINE
ie_t *mtlk_core_find_essid(u8 *buf, unsigned len)
{
  u8 *end = buf + len;
  ie_t *essid = NULL;

  while (!essid && buf < end)
  {
    ie_t *ie = (ie_t *) buf;

    if (ie->id == IE_SSID)
      essid = ie;

    buf += sizeof(ie_t) + ie->length;
  }

  if (buf > end) /* the SSID element was incomplete */
    essid = NULL;

  return essid;
}

void
copy_mtlk_chandef(struct mtlk_chan_def *mcd, struct mtlk_chan_def *mcs)
{
  struct mtlk_channel *mc_dest = &mcd->chan;
  struct mtlk_channel *mc_src = &mcs->chan;

  mcd->center_freq1 = mcs->center_freq1;
  mcd->center_freq2 = mcs->center_freq2;
  mcd->width = mcs->width;
  mcd->is_noht = mcs->is_noht;

  mc_dest->dfs_state_entered = mc_src->dfs_state_entered;
  mc_dest->dfs_state = mc_src->dfs_state;
  mc_dest->band = mc_src->band;
  mc_dest->center_freq = mc_src->center_freq;
  mc_dest->flags = mc_src->flags;
  mc_dest->orig_flags = mc_src->orig_flags;
  mc_dest->max_antenna_gain = mc_src->max_antenna_gain;
  mc_dest->max_power = mc_src->max_power;
  mc_dest->max_reg_power = mc_src->max_reg_power;
  mc_dest->dfs_cac_ms = mc_src->dfs_cac_ms;
}

int __MTLK_IFUNC
_mtlk_core_switch_channel_normal(mtlk_core_t *master_core, struct mtlk_chan_def *chandef)
{
  mtlk_df_t *master_df = mtlk_vap_manager_get_master_df(mtlk_vap_get_manager(master_core->vap_handle));
  mtlk_vap_handle_t master_vap_handle = mtlk_df_get_vap_handle(master_df);
  struct set_chan_param_data cpd;

  ILOG0_V("Seen a beacon changing channel to type normal");

  memset(&cpd, 0, sizeof(cpd));
  cpd.vap_id = mtlk_vap_get_id(master_vap_handle);
  cpd.ndev = mtlk_df_user_get_ndev(mtlk_df_get_user(master_df));
  copy_mtlk_chandef(&cpd.chandef, chandef);

  cpd.switch_type = ST_NORMAL;
  cpd.block_tx_pre = FALSE;
  cpd.block_tx_post = FALSE; /* TRUE means waiting for radars */
  cpd.radar_required = FALSE; /* we have a station up so we don't need radar checks */

  _mtlk_df_user_invoke_core_async(master_df,
    WAVE_RADIO_REQ_SET_CHAN, &cpd, sizeof(cpd),
    NULL, 0);

  return MTLK_ERR_OK;
}

bool _mtlk_core_is_radar_chan(struct mtlk_channel *channel)
{
  return (channel->flags & MTLK_CHAN_RADAR);
}

static int
_mtlk_core_broadcast_mngmnt_frame_notify (mtlk_handle_t object, const void *data,  uint32 data_size)
{
  mtlk_mngmnt_frame_t *frame = (mtlk_mngmnt_frame_t *)data;
  struct nic          *nic = HANDLE_T_PTR(struct nic, object);
  mtlk_core_t         *cur_core;
  mtlk_vap_manager_t  *vap_mng;
  mtlk_vap_handle_t   vap_handle;
  uint8 *buf     = frame->frame;
  int    size    = frame->size;
  int    freq    = frame->freq;
  unsigned subtype = frame->subtype;
  unsigned i, max_vaps;
  sta_entry *sta;
  IEEE_ADDR *src_addr;
  int disable_master_vap;
  int softblockcheck;
  struct intel_vendor_event_msg_drop prb_req_drop;

  MTLK_ASSERT (data_size == sizeof(mtlk_mngmnt_frame_t));

  vap_mng  = mtlk_vap_get_manager(nic->vap_handle);
  max_vaps = wave_radio_max_vaps_get(wave_vap_radio_get(nic->vap_handle));

  src_addr = &((frame_head_t*)buf)->src_addr;
  for (i = 0; i < max_vaps; i++) {
    if (MTLK_ERR_OK != mtlk_vap_manager_get_vap_handle(vap_mng, i, &vap_handle)) {
      continue;   /* VAP does not exist */
    }
    cur_core = mtlk_vap_get_core(vap_handle);
    if (NET_STATE_CONNECTED != mtlk_core_get_net_state(cur_core)) {
      /* Core is not ready */
      continue;
    }
    disable_master_vap = WAVE_VAP_RADIO_PDB_GET_INT(vap_handle,
                         PARAM_DB_RADIO_DISABLE_MASTER_VAP);
    if (disable_master_vap && mtlk_vap_is_master(vap_handle)){
      /* Dummy master VAP (in order to support reconf for all VAPs) */
      continue;
    }

    sta = mtlk_stadb_find_sta(&cur_core->slow_ctx->stadb, src_addr->au8Addr);
    /* don't notify about unassociated client that is in the blacklist */
    if ((sta == NULL) &&
      _mtlk_core_blacklist_frame_drop(cur_core, src_addr, subtype, frame->phy_info.snr_db, TRUE))
      continue;
    if (sta)
      mtlk_sta_decref(sta);

    if (subtype == MAN_TYPE_PROBE_REQ)
    {
      if (mtlk_vap_is_ap(vap_handle)) {
        ie_t *essid = NULL;
        mtlk_pdb_t *param_db_core = mtlk_vap_get_param_db(vap_handle);
        int hidden = wave_pdb_get_int(param_db_core, PARAM_DB_CORE_HIDDEN_SSID);
        u8 our_ssid[MIB_ESSID_LENGTH];
        mtlk_pdb_size_t our_ssid_len = sizeof(our_ssid);

        if (size > sizeof(frame_head_t))
          essid = mtlk_core_find_essid(buf + sizeof(frame_head_t), size - sizeof(frame_head_t));

        if (MTLK_CORE_PDB_GET_BINARY(cur_core, PARAM_DB_CORE_ESSID, our_ssid, &our_ssid_len) != MTLK_ERR_OK)
        {
          ELOG_D("CID-%04x: Can't get the ESSID", mtlk_vap_get_oid(nic->vap_handle));
          continue;
        }

        /* if ESSID given and matches ours */
        if ((essid && essid->length == our_ssid_len && !memcmp((char *) essid + sizeof(ie_t), our_ssid, our_ssid_len))
            || ((!essid || essid->length == 0) && !hidden)) /* or no ESSID or broadcast ESSID but we're not hidden */
        {
          if (frame->probe_req_wps_ie) {
            ILOG3_V("Probe request with WPS IE received, notify hostapd");
            __mtlk_core_mngmnt_frame_notify(cur_core, buf, size, freq, subtype, &frame->phy_info, frame->pmf_flags);
            /* hostapd will generate and send probe response */
            continue;
          }
          if (frame->probe_req_interworking_ie) {
            ILOG3_V("Probe request with INTERWORKING IE received, notify hostapd");
            __mtlk_core_mngmnt_frame_notify(cur_core, buf, size, freq, subtype, &frame->phy_info, frame->pmf_flags);
            /* hostapd will generate and send probe response */
            continue;
          }
          if (frame->probe_req_vsie) {
            ILOG3_V("Probe request with VSIE received, notify hostapd");
            __mtlk_core_mngmnt_frame_notify(cur_core, buf, size, freq, subtype, &frame->phy_info, frame->pmf_flags);
            /* hostapd will generate and send probe response */
            continue;
          }

          /* case when wps, interworking, vs ies are not present */
          if (cur_core->slow_ctx->probe_resp_templ) {
             softblockcheck = _mtlk_core_mngmnt_softblock_notify(cur_core, src_addr, &cur_core->multi_ap_blacklist,
               "multi-AP black", frame->phy_info.snr_db, TRUE, &prb_req_drop);

             if ((softblockcheck == DRV_SOFTBLOCK_ACCEPT) || (softblockcheck == DRV_SOFTBLOCK_ALLOW)) {

                 /* All probe responses should be sent from master VAP in case of MBSSID */
                 if(MTLK_CORE_PDB_GET_INT(cur_core, PARAM_DB_CORE_MBSSID_VAP) > WAVE_RADIO_OPERATION_MODE_MBSSID_TRANSMIT_VAP)
                   mtlk_core_finalize_and_send_probe_resp(mtlk_core_get_master(cur_core), frame);
                 else
                   mtlk_core_finalize_and_send_probe_resp(cur_core, frame);

                /* Notify hostap */
                if (softblockcheck == DRV_SOFTBLOCK_ALLOW) {
                   /* Trigger notification */
                   mtlk_send_softblock_msg_drop(cur_core->vap_handle, &prb_req_drop, sizeof(prb_req_drop));
                }
             }
          }
        }
      }
    }
    else if (subtype == MAN_TYPE_BEACON)
    {
      if (!mtlk_vap_is_ap(vap_handle))
        __mtlk_core_mngmnt_frame_notify(cur_core, buf, size, freq, subtype, &frame->phy_info, frame->pmf_flags);
    }
    else
    {
      __mtlk_core_mngmnt_frame_notify(cur_core, buf, size, freq, subtype, &frame->phy_info, frame->pmf_flags);
    }
  } /* for (i = 0; i < max_vaps; i++) */

  mtlk_osal_mem_free(frame->frame);
  return MTLK_ERR_OK;
}

static BOOL _wave_core_cac_in_progress(mtlk_core_t *master_core)
{
  MTLK_ASSERT(NULL != master_core);
  return !mtlk_osal_timer_is_stopped(&master_core->slow_ctx->cac_timer);
}

static int
handle_rx_bss_ind(mtlk_core_t *nic, mtlk_core_handle_rx_bss_t *data)
{
  int res = MTLK_ERR_OK;
  int disable_master_vap;
  uint16 frame_ctl;
  unsigned frame_type;

  mtlk_dump(4, data->buf, MIN(data->size, 32), "RX BSS IND data (first 32 bytes):");

  if (data->size < sizeof(frame_head_t)) {
    ILOG1_D("CID-%04x: Management Frame length is wrong",
      mtlk_vap_get_oid(nic->vap_handle));
    return MTLK_ERR_NOT_IN_USE;
  }
  frame_ctl = mtlk_wlan_pkt_get_frame_ctl(data->buf);
  frame_type = WLAN_FC_GET_TYPE(frame_ctl);

/*
802.11n data frame from AP:

        |----------------------------------------------------------------|
 Bytes  |  2   |  2    |  6  |  6  |  6  |  2  | 6?  | 2?  | 0..2312 | 4 |
        |------|-------|-----|-----|-----|-----|-----|-----|---------|---|
 Descr. | Ctl  |Dur/ID |Addr1|Addr2|Addr3| Seq |Addr4| QoS |  Frame  |fcs|
        |      |       |     |     |     | Ctl |     | Ctl |  data   |   |
        |----------------------------------------------------------------|
Total: 28-2346 bytes

Existance of Addr4 in frame is optional and depends on To_DS From_DS flags.
Existance of QoS_Ctl is also optional and depends on Ctl flags.
(802.11n-D1.0 describes also HT Control (0 or 4 bytes) field after QoS_Ctl
but we don't support this for now.)

Interpretation of Addr1/2/3/4 depends on To_DS From_DS flags:

To DS From DS   Addr1   Addr2   Addr3   Addr4
---------------------------------------------
0       0       DA      SA      BSSID   N/A
0       1       DA      BSSID   SA      N/A
1       0       BSSID   SA      DA      N/A
1       1       RA      TA      DA      SA


frame data begins with 8 bytes of LLC/SNAP:

        |-----------------------------------|
 Bytes  |  1   |   1  |  1   |    3   |  2  |
        |-----------------------------------|
 Descr. |        LLC         |     SNAP     |
        |-----------------------------------+
        | DSAP | SSAP | Ctrl |   OUI  |  T  |
        |-----------------------------------|
        |  AA  |  AA  |  03  | 000000 |     |
        |-----------------------------------|

From 802.11 data frame that we receive from MAC we are making
Ethernet DIX (II) frame.

Ethernet DIX (II) frame format:

        |------------------------------------------------------|
 Bytes  |  6  |  6  | 2 |         46 - 1500               |  4 |
        |------------------------------------------------------|
 Descr. | DA  | SA  | T |          Data                   | FCS|
        |------------------------------------------------------|

So we overwrite 6 bytes of LLC/SNAP with SA.


  Excerpts from "IEEE P802.11e/D13.0, January 2005" p.p. 22-23
  Type          Subtype     Description
  -------------------------------------------------------------
  00 Management 0000        Association request
  00 Management 0001        Association response
  00 Management 0010        Reassociation request
  00 Management 0011        Reassociation response
  00 Management 0100        Probe request
  00 Management 0101        Probe response
  00 Management 0110-0111   Reserved
  00 Management 1000        Beacon
  00 Management 1001        Announcement traffic indication message (ATIM)
  00 Management 1010        Disassociation
  00 Management 1011        Authentication
  00 Management 1100        Deauthentication
  00 Management 1101        Action
  00 Management 1101-1111   Reserved
  01 Control    0000-0111   Reserved
  01 Control    1000        Block Acknowledgement Request (BlockAckReq)
  01 Control    1001        Block Acknowledgement (BlockAck)
  01 Control    1010        Power Save Poll (PS-Poll)
  01 Control    1011        Request To Send (RTS)
  01 Control    1100        Clear To Send (CTS)
  01 Control    1101        Acknowledgement (ACK)
  01 Control    1110        Contention-Free (CF)-End
  01 Control    1111        CF-End + CF-Ack
  10 Data       0000        Data
  10 Data       0001        Data + CF-Ack
  10 Data       0010        Data + CF-Poll
  10 Data       0011        Data + CF-Ack + CF-Poll
  10 Data       0100        Null function (no data)
  10 Data       0101        CF-Ack (no data)
  10 Data       0110        CF-Poll (no data)
  10 Data       0111        CF-Ack + CF-Poll (no data)
  10 Data       1000        QoS Data
  10 Data       1001        QoS Data + CF-Ack
  10 Data       1010        QoS Data + CF-Poll
  10 Data       1011        QoS Data + CF-Ack + CF-Poll
  10 Data       1100        QoS Null (no data)
  10 Data       1101        Reserved
  10 Data       1110        QoS CF-Poll (no data)
  10 Data       1111        QoS CF-Ack + CF-Poll (no data)
  11 Reserved   0000-1111   Reserved
  */

  /*
  If we are AP:
  - in CAC time filter out all management/control/action frames. Print AUTH_REQ for debug a specific case.
  - if NET_STATE != CONNECTED: pass only beacons and probe responces.
  */
  if (mtlk_vap_is_ap(nic->vap_handle)) {
    mtlk_core_t *master_core = mtlk_vap_manager_get_master_core(mtlk_vap_get_manager(nic->vap_handle));
    int frame_subtype = 0, net_state;

    /* are we in DFS CAC? */
    if (_wave_core_cac_in_progress(master_core)) {
      if (frame_type == IEEE80211_FTYPE_MGMT) {
        frame_subtype = (frame_ctl & FRAME_SUBTYPE_MASK) >> FRAME_SUBTYPE_SHIFT;
        if (frame_subtype == MAN_TYPE_AUTH) {
          ILOG0_Y("AUTH frame from %Y dropped during CAC", WLAN_GET_ADDR2(data->buf));
        }
      }
      return res;
    }

    /* are we in NET_STATE_CONNECTED ? */
    net_state = mtlk_core_get_net_state(master_core);
    if (NET_STATE_CONNECTED != net_state) {
      if (frame_type == IEEE80211_FTYPE_MGMT) {
        frame_subtype = (frame_ctl & FRAME_SUBTYPE_MASK) >> FRAME_SUBTYPE_SHIFT;
        if ((frame_subtype != MAN_TYPE_PROBE_RES) && (frame_subtype != MAN_TYPE_BEACON)) {
          ILOG2_DYD("Frame subtype %d from %Y dropped due to wrong NET_STATE %d", frame_subtype, WLAN_GET_ADDR2(data->buf), net_state);
          return res;
        }
      }
      else {
        /* not a management frame */
          ILOG2_DYD("Frame type %d from %Y dropped due to wrong NET_STATE %d", frame_type, WLAN_GET_ADDR2(data->buf), net_state);
          return res;
      }
    }
  }

  switch (frame_type)
  {
  case IEEE80211_FTYPE_MGMT:
    {
      mtlk_phy_info_t *phy_info = &data->phy_info;
      uint8 *src_addr = WLAN_GET_ADDR2(data->buf);
      sta_entry *sta = mtlk_stadb_find_sta(&(nic->slow_ctx->stadb), src_addr);
      mtlk_mgmt_frame_data_t fd;

      mtlk_df_t *df               = mtlk_vap_get_df(nic->vap_handle); /* this checks vap_handle and df, cannot return NULL */
      mtlk_df_user_t *df_user     = mtlk_df_get_user(df);
      struct wireless_dev *wdev   = mtlk_df_user_get_wdev(df_user);   /* this checks df_user */
      unsigned subtype = (frame_ctl & FRAME_SUBTYPE_MASK) >> FRAME_SUBTYPE_SHIFT;
      uint8 *orig_buf = data->buf;
      uint32 orig_size = data->size;

      MTLK_ASSERT(NULL != wdev);

      memset(&fd, 0, sizeof(fd));
      fd.phy_info = phy_info;
      fd.chan = NULL;

      res = mtlk_process_man_frame(HANDLE_T(nic), sta, data->buf, &data->size, &fd);

      /*
      if (subtype != MAN_TYPE_BEACON)
        ILOG2_DDDD("Res=%i, notify_hostapd=%i, report_bss=%i, obss_beacon_report=%i",
                   res, notify_hostapd, report_bss, obss_beacon_report);
      */

      if (res == MTLK_ERR_OK)
      {
        BOOL reported = FALSE;
        if (fd.notify_hapd_supplicant)
        {
          BOOL is_broadcast = FALSE;
          reported = TRUE;

          if (mgmt_frame_filter_allows(nic, &nic->slow_ctx->mgmt_frame_filter,
               data->buf, data->size, &is_broadcast))
          {
            if (is_broadcast) {
              /* Broad cast frames will be send throught serialzer - we need copy them for all vaps */
              mtlk_mngmnt_frame_t frame;
              frame.size = data->size;
              frame.freq = fd.chan->center_freq;
              frame.subtype = subtype;
              frame.phy_info = *fd.phy_info; /* including max_rssi */
              frame.probe_req_wps_ie = fd.probe_req_wps_ie;
              frame.probe_req_interworking_ie = fd.probe_req_interworking_ie;
              frame.probe_req_vsie = fd.probe_req_vsie;
              frame.probe_req_he_ie = fd.probe_req_he_ie;
              frame.pmf_flags = fd.pmf_flags;
              frame.frame = mtlk_osal_mem_alloc(data->size, MTLK_MEM_TAG_CFG80211);
              if (frame.frame == NULL) {
                ELOG_D("CID-%04x: Can't allocate memory for mngmnt frame", mtlk_vap_get_oid(nic->vap_handle));
              } else {
                wave_memcpy(frame.frame, data->size, data->buf, data->size);
                _mtlk_process_hw_task(mtlk_core_get_master(nic), SERIALIZABLE, _mtlk_core_broadcast_mngmnt_frame_notify, HANDLE_T(nic),
                                      &frame, sizeof(mtlk_mngmnt_frame_t));
              }
            } else {

              /* Sanity: drop unicast frames sent to a disabled mater VAP */
              disable_master_vap = WAVE_VAP_RADIO_PDB_GET_INT(nic->vap_handle,
                                   PARAM_DB_RADIO_DISABLE_MASTER_VAP);
              if (disable_master_vap && mtlk_vap_is_master(nic->vap_handle)){
                ILOG2_DY("Frame type %d from %Y dropped since master VAP is disabled",
                         frame_type, WLAN_GET_ADDR2(data->buf));
                return MTLK_ERR_OK;
              }

              /* unicast frames can be send directly */
              __mtlk_core_mngmnt_frame_notify(nic, data->buf, data->size, fd.chan->center_freq,
                                              subtype, fd.phy_info, fd.pmf_flags);
            }
          }
        }

        if (fd.report_bss)
        {
          reported = TRUE;
#ifdef CPTCFG_IWLWAV_FILTER_BLACKLISTED_BSS
          if (!_mtlk_core_blacklist_entry_exists(nic, (IEEE_ADDR*)src_addr))
#endif
            if (mtlk_vap_is_ap(nic->vap_handle))
              wv_cfg80211_inform_bss_frame(wdev, fd.chan, orig_buf, orig_size, phy_info->max_rssi);
        }

        if (fd.obss_beacon_report)
        {
          reported = TRUE;
#ifdef CPTCFG_IWLWAV_FILTER_BLACKLISTED_BSS
          if (!_mtlk_core_blacklist_entry_exists(nic, (IEEE_ADDR*)src_addr))
#endif
            if (mtlk_vap_is_ap(nic->vap_handle))
              wv_cfg80211_obss_beacon_report(wdev, data->buf, data->size,
                    WAVE_RADIO_PDB_GET_INT(wave_vap_radio_get(nic->vap_handle), PARAM_DB_RADIO_CHANNEL_CUR),
                    phy_info->max_rssi);

/*          mtlk_dump(2, data->buf, MIN(data->size, 16), "RX BSS frame obss-beacon-reported (first 16 bytes):"); */
        }

        if (!reported && subtype != MAN_TYPE_BEACON
            && (subtype != MAN_TYPE_PROBE_RES || !wave_radio_scan_is_ignorant(wave_vap_radio_get(nic->vap_handle))))
          mtlk_dump(2, data->buf, MIN(data->size, 32), "RX mgmt frame not reported in any way (first 32 bytes):");
      }

      mtlk_core_inc_cnt(nic, MTLK_CORE_CNT_MAN_FRAMES_RECEIVED);

      if (sta) {
        ILOG3_YDDDDDD("STA %Y frame_ctrl 0x%04X, rssi (%d, %d, %d, %d), max_rssi %d",
                    mtlk_sta_get_addr(sta), frame_ctl,
                    phy_info->rssi[0], phy_info->rssi[1], phy_info->rssi[2], phy_info->rssi[3],
                    phy_info->max_rssi);

        mtlk_sta_update_rx_rate_rssi_on_man_frame(sta, phy_info);

        mtlk_sta_decref(sta); /* De-reference of find */
      }
    }
    break;
  case IEEE80211_FTYPE_CTL:
    CPU_STAT_SPECIFY_TRACK(CPU_STAT_ID_RX_CTL);
    res = mtlk_process_ctl_frame(HANDLE_T(nic), data->buf, data->size);
    mtlk_core_inc_cnt(nic, MTLK_CORE_CNT_CTL_FRAMES_RECEIVED);
    break;
  default:
    mtlk_df_inc_corrupted_packets(mtlk_vap_get_df(nic->vap_handle));
    WLOG_DD("CID-%04x: Not Management or Control packet in BSS RX path, frame_ctl %04x", mtlk_vap_get_oid(nic->vap_handle), frame_ctl);
    mtlk_dump(0, data->buf, MIN(data->size, 128), "RX BSS IND data (first 128 bytes):");
    data->make_assert = TRUE;
    res = MTLK_ERR_CORRUPTED;
  }

  return res;
}

int _handle_radar_event(mtlk_handle_t core_object, const void *payload, uint32 size)
{
  BOOL emulating;
  uint8 rbm; /* Radar Bit Map */
  struct mtlk_chan_def ccd;
  UMI_RADAR_DETECTION *radar_det;
  mtlk_scan_support_t *ss;
  mtlk_df_t *df;
  mtlk_df_user_t *df_user;
  struct wireless_dev *wdev;
  struct net_device *ndev;
  int cur_channel;
  uint32 cac_started;
  BOOL radar_det_enabled;
  struct nic *nic = HANDLE_T_PTR(struct nic, core_object);
  wave_radio_t *radio = wave_vap_radio_get(nic->vap_handle);
  wv_cfg80211_t *cfg80211 = wave_radio_cfg80211_get(radio);
  struct wiphy *wiphy = wv_cfg80211_wiphy_get(cfg80211);

  MTLK_ASSERT(nic == mtlk_core_get_master(nic));

  /* wait for Radar Detection end */
  wave_radio_radar_detect_end_wait(radio);

  if (mtlk_vap_manager_vap_is_not_ready(mtlk_vap_get_manager(nic->vap_handle), mtlk_vap_get_id(nic->vap_handle))) {
    ELOG_V("Radar detection: interface already deactivated");
    return MTLK_ERR_UNKNOWN;
  }

  ss = mtlk_core_get_scan_support(nic);
  df = mtlk_vap_get_df(nic->vap_handle);
  df_user = mtlk_df_get_user(df);
  wdev = mtlk_df_user_get_wdev(df_user);
  ndev = mtlk_df_user_get_ndev(df_user);
  radar_det_enabled = WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_DFS_RADAR_DETECTION);

  ccd = *__wave_core_chandef_get(nic);
  cur_channel = ieee80211_frequency_to_channel(ccd.chan.center_freq); /* Primary channel */

  if (sizeof(UMI_RADAR_DETECTION) == size) {
    radar_det = (UMI_RADAR_DETECTION *)payload;
    emulating = FALSE;
    /* On gen6 Sub Band DFS PHY layer development has not started yet.
       rbm contains dummy data and should be ignored. */
    rbm = mtlk_hw_type_is_gen6(mtlk_vap_get_hw(nic->vap_handle)) ?
           0 : MAC_TO_HOST16(radar_det->subBandBitmap);
  }
  else if (sizeof(mtlk_core_radar_emu_t) == size) { /* Emulating */
    mtlk_core_radar_emu_t *radar_emu = (mtlk_core_radar_emu_t *)payload;
    radar_det = &radar_emu->radar_det;
    rbm = radar_det->subBandBitmap;
    emulating = TRUE;
  }
  else {
    ELOG_D("Wrong radar event data size %d", size);
    return MTLK_ERR_UNKNOWN;
  }

  ILOG0_DDDD("RADAR detected on channel: %u, radar_type: %u, radar bit map 0x%02x, emulated %d",
             radar_det->channel, radar_det->radarType, rbm, emulating);

  cac_started = !mtlk_osal_timer_is_stopped(&nic->slow_ctx->cac_timer);
  mtlk_hw_inc_radar_cntr(mtlk_vap_get_hw(nic->vap_handle));

  if (!radar_det_enabled) {
    if (!emulating) {
      ELOG_V("Radar detected while radar detection is disabled");
      return MTLK_ERR_OK; /* Ignore message */
    }
    else {
      ILOG0_V("Allowing radar emulation even radar detection is disabled");
    }
  }

  if (WAVE_RADIO_OFF == WAVE_RADIO_PDB_GET_INT(radio,
                                                    PARAM_DB_RADIO_MODE_CURRENT)) {
    ILOG1_V("Radar detected while RF is off, ignoring");
    return MTLK_ERR_OK; /* Ignore message */
  }

  if (cur_channel != radar_det->channel) {
    ILOG0_DD("Radar detected on different channel (%u) than current channel (%d)", radar_det->channel, cur_channel);
    return MTLK_ERR_OK; /* Ignore message */
  }

  if (!wv_cfg80211_get_chans_dfs_required(wiphy, ccd.center_freq1, mtlkcw2cw(ccd.width)) ||
      (rbm && !wv_cfg80211_get_chans_dfs_bitmap_valid(wiphy, ccd.center_freq1, mtlkcw2cw(ccd.width), rbm))) {
    ILOG0_D("Radar detected on non-DFS channel (%u), ignoring", cur_channel);
    return MTLK_ERR_OK; /* Ignore message */
  }

  /* Radar simulation debug switch may take some time,
   * so ignore new event if previous not completed yet */
  if (ss->dfs_debug_params.debug_chan) {
    if (ss->dfs_debug_params.switch_in_progress)
      return MTLK_ERR_OK; /* Ignore message */
    else
      ss->dfs_debug_params.switch_in_progress = TRUE;
  }

  MTLK_ASSERT(wdev);
  mtlk_osal_timer_cancel_sync(&nic->slow_ctx->cac_timer);
  mtlk_core_cfg_set_block_tx(nic, 0);

  if (wave_radio_get_sta_vifs_exist(radio)) {
    return wv_cfg80211_radar_event_if_sta_exist(wdev, &ccd);
  }

  if (cac_started)
    wv_cfg80211_cac_event(ndev, &ccd, 0); /* 0 means it didn't finish, i.e., was aborted */

  if (__mtlk_is_sb_dfs_switch(ccd.sb_dfs.sb_dfs_bw)) {
    ccd.center_freq1 = ccd.sb_dfs.center_freq;
    ccd.width = ccd.sb_dfs.width;
  }

  mtlk_df_user_handle_radar_event(df_user, wdev, &ccd, cac_started, rbm);

  return MTLK_ERR_OK;
}

static int
_mtlk_core_set_wep_key_blocked (struct nic      *nic,
                                sta_entry       *sta,
                                uint16           key_idx)
{
  int                   res = MTLK_ERR_UNKNOWN;
  mtlk_txmm_msg_t       man_msg;
  mtlk_txmm_data_t      *man_entry = NULL;
  UMI_SET_KEY           *umi_key;
  UMI_DEFAULT_KEY_INDEX *umi_default_key;
  uint16                key_len = 0;
  int                   i;
  uint16                sid;
  uint16                key_type;

  if(key_idx >= CORE_NUM_WEP_KEYS) {
    ELOG_DD("CID-%04x: Wrong default key index %d", mtlk_vap_get_oid(nic->vap_handle), key_idx);
    goto FINISH;
  }

  if(0 == nic->slow_ctx->wds_keys[key_idx].key_len) {
    ELOG_DD("CID-%04x: There is no key with default index %d", mtlk_vap_get_oid(nic->vap_handle), key_idx);
    goto FINISH;
  }

  man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, mtlk_vap_get_txmm(nic->vap_handle), &res);
  if (!man_entry) {
    ELOG_DD("CID-%04x: No man entry available (res = %d)", mtlk_vap_get_oid(nic->vap_handle), res);
    goto FINISH;
  }

  if (sta) {
    key_type = HOST_TO_MAC16(UMI_RSN_PAIRWISE_KEY);
    sid      = HOST_TO_MAC16(mtlk_sta_get_sid(sta));
  }
  else {
    key_type = HOST_TO_MAC16(UMI_RSN_GROUP_KEY);
    sid      = HOST_TO_MAC16(TMP_BCAST_DEST_ADDR);
  }

  umi_key = (UMI_SET_KEY*)man_entry->payload;
  memset(umi_key, 0, sizeof(*umi_key));

  man_entry->id           = UM_MAN_SET_KEY_REQ;
  man_entry->payload_size = sizeof(*umi_key);

  for (i = 0; i < CORE_NUM_WEP_KEYS; i++) {

    key_len = nic->slow_ctx->wds_keys[i].key_len;
    if(0 == key_len) { /* key is not set */
      break;
    }
    if ((nic->slow_ctx->wds_keys[i].cipher != UMI_RSN_CIPHER_SUITE_WEP40) && (nic->slow_ctx->wds_keys[i].cipher != UMI_RSN_CIPHER_SUITE_WEP104)) {
      ELOG_DD("CID-%04x: CipherSuite is not WEP %d", mtlk_vap_get_oid(nic->vap_handle), nic->slow_ctx->wds_keys[i].cipher);
      goto FINISH;
    }
    /* Prepeare the key */
    memset(umi_key, 0, sizeof(*umi_key));
    umi_key->u16Sid         = sid;
    umi_key->u16KeyType     = key_type;
    umi_key->u16KeyIndex    = HOST_TO_MAC16(i);
    umi_key->u16CipherSuite = HOST_TO_MAC16(nic->slow_ctx->wds_keys[i].cipher);
    wave_memcpy(umi_key->au8Tk, sizeof(umi_key->au8Tk), nic->slow_ctx->wds_keys[i].key, key_len);

    ILOG1_D("UMI_SET_KEY             SID:0x%x", MAC_TO_HOST16(umi_key->u16Sid));
    ILOG1_D("UMI_SET_KEY         KeyType:0x%x", MAC_TO_HOST16(umi_key->u16KeyType));
    ILOG1_D("UMI_SET_KEY  u16CipherSuite:0x%x", MAC_TO_HOST16(umi_key->u16CipherSuite));
    ILOG1_D("UMI_SET_KEY        KeyIndex:%d", MAC_TO_HOST16(umi_key->u16KeyIndex));
    mtlk_dump(1, umi_key->au8RxSeqNum, sizeof(umi_key->au8RxSeqNum), "RxSeqNum");
    mtlk_dump(1, umi_key->au8TxSeqNum, sizeof(umi_key->au8TxSeqNum), "TxSeqNum");
    mtlk_dump(1, umi_key->au8Tk, key_len, "KEY:");

    res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);
    if (res != MTLK_ERR_OK) {
      ELOG_DD("CID-%04x: mtlk_mm_send failed: %d", mtlk_vap_get_oid(nic->vap_handle), res);
      goto FINISH;
    }

    umi_key->u16Status = MAC_TO_HOST16(umi_key->u16Status);

    if (umi_key->u16Status != UMI_OK) {
      ELOG_DYD("CID-%04x: %Y: status is %d", mtlk_vap_get_oid(nic->vap_handle), mtlk_sta_get_addr(sta)->au8Addr, umi_key->u16Status);
      res = MTLK_ERR_MAC;
      goto FINISH;
    }
  }

  /* Send default key index */
  man_entry->id           = UM_MAN_SET_DEFAULT_KEY_INDEX_REQ;
  man_entry->payload_size = sizeof(*umi_key);
  umi_default_key = (UMI_DEFAULT_KEY_INDEX *)man_entry->payload;
  memset(umi_default_key, 0, sizeof(*umi_default_key));

  umi_default_key->u16SID     = sid;
  umi_default_key->u16KeyType = key_type;
  umi_default_key->u16KeyIndex = MAC_TO_HOST16(key_idx);

  ILOG1_D("UMI_SET_DEFAULT_KEY             SID:0x%x", MAC_TO_HOST16(umi_default_key->u16SID));
  ILOG1_D("UMI_SET_DEFAULT_KEY         KeyType:0x%x", MAC_TO_HOST16(umi_default_key->u16KeyType));
  ILOG1_D("UMI_SET_DEFAULT_KEY        KeyIndex:%d", MAC_TO_HOST16(umi_default_key->u16KeyIndex));

  res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);
  if (res != MTLK_ERR_OK) {
    ELOG_DD("CID-%04x: failed to send default key: %d", mtlk_vap_get_oid(nic->vap_handle), res);
    goto FINISH;
  }

FINISH:
  if (man_entry) {
    mtlk_txmm_msg_cleanup(&man_msg);
  }

  return res;
}

static int
_mtlk_core_ap_add_sta_req (struct nic *nic,
                           sta_info   *info)
{
  int               res = MTLK_ERR_OK;
  mtlk_txmm_msg_t   man_msg;
  mtlk_txmm_data_t *man_entry = NULL;
  UMI_STA_ADD      *psUmiStaAdd;
  uint8             u8Flags;
  uint8             u8FlagsExt;
  unsigned          rssi_offs;
  int               rssi;

  MTLK_ASSERT(NULL != nic);
  MTLK_ASSERT(NULL != info);

  if (info->supported_rates_len > MAX_NUM_SUPPORTED_RATES) {
    ELOG_DD("Rates length (%d) is longer than rates array size (%d).",
      info->supported_rates_len, MAX_NUM_SUPPORTED_RATES);
    res = MTLK_ERR_PARAMS;
    goto FINISH;
  }

  man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, mtlk_vap_get_txmm(nic->vap_handle), &res);
  if (!man_entry) {
    ELOG_D("CID-%04x: Can't send STA_ADD request to MAC due to the lack of MAN_MSG",
           mtlk_vap_get_oid(nic->vap_handle));
    res = MTLK_ERR_NO_RESOURCES;
    goto FINISH;
  }

  man_entry->id           = UM_MAN_STA_ADD_REQ;
  man_entry->payload_size = sizeof(UMI_STA_ADD);

  memset(man_entry->payload, 0, man_entry->payload_size);
  psUmiStaAdd = (UMI_STA_ADD *)man_entry->payload;

  rssi_offs = _mtlk_core_get_rrsi_offs(nic);
  rssi      = info->rssi_dbm + rssi_offs;
  ILOG2_DDD("rssi_dbm %d, offs %d, rssi %d", info->rssi_dbm, rssi_offs, rssi);
  MTLK_ASSERT((MIN_RSSI <= rssi) && (rssi <= (int8)MAX_INT8));

  psUmiStaAdd->u8Status         = UMI_OK;
  psUmiStaAdd->u16SID           = HOST_TO_MAC16(info->sid);
  psUmiStaAdd->u8VapIndex       = mtlk_vap_get_id(nic->vap_handle);
  psUmiStaAdd->u8BSS_Coex_20_40 = info->bss_coex_20_40;
  psUmiStaAdd->u8UAPSD_Queues   = info->uapsd_queues;
  psUmiStaAdd->u8Max_SP         = info->max_sp;
  psUmiStaAdd->u16AID           = HOST_TO_MAC16(info->aid);
  psUmiStaAdd->u16HT_Cap_Info   = info->ht_cap_info;  /* Do not convert to MAC format */
  psUmiStaAdd->u8AMPDU_Param    = info->ampdu_param;
  psUmiStaAdd->sAddr            = info->addr;
  psUmiStaAdd->u8Rates_Length   = info->supported_rates_len;
  psUmiStaAdd->u8ListenInterval = info->listen_interval;
  psUmiStaAdd->u32VHT_Cap_Info  = info->vht_cap_info;

  /* element ID is the first byte */
  psUmiStaAdd->u8HE_Mac_Phy_Cap_Info[0] = WLAN_EID_EXT_HE_CAPABILITY;

  wave_memcpy(psUmiStaAdd->u8HE_Mac_Phy_Cap_Info + 1, sizeof(psUmiStaAdd->u8HE_Mac_Phy_Cap_Info) - 1,
      &info->he_cap.he_cap_elem, sizeof(info->he_cap.he_cap_elem));

  psUmiStaAdd->rssi             = (uint8)rssi;

  psUmiStaAdd->transmitBfCapabilities = info->tx_bf_cap_info; /* Do not convert to MAC format */
  psUmiStaAdd->u8VHT_OperatingModeNotification = info->opmode_notif;
  psUmiStaAdd->u8WDS_client_type = info->WDS_client_type;

  u8Flags = 0;
  MTLK_BFIELD_SET(u8Flags, STA_ADD_FLAGS_WDS,
    MTLK_BFIELD_GET(info->flags, STA_FLAGS_WDS) || mtlk_sta_info_is_4addr(info));
  MTLK_BFIELD_SET(u8Flags, STA_ADD_FLAGS_IS_8021X_FILTER_OPEN, MTLK_BFIELD_GET(info->flags, STA_FLAGS_IS_8021X_FILTER_OPEN));
  MTLK_BFIELD_SET(u8Flags, STA_ADD_FLAGS_MFP, MTLK_BFIELD_GET(info->flags, STA_FLAGS_MFP));
  MTLK_BFIELD_SET(u8Flags, STA_ADD_FLAGS_IS_HT, MTLK_BFIELD_GET(info->flags, STA_FLAGS_11n));
  MTLK_BFIELD_SET(u8Flags, STA_ADD_FLAGS_IS_VHT, MTLK_BFIELD_GET(info->flags, STA_FLAGS_11ac));
  MTLK_BFIELD_SET(u8Flags, STA_ADD_FLAGS_OMN, MTLK_BFIELD_GET(info->flags, STA_FLAGS_OMN_SUPPORTED));
  MTLK_BFIELD_SET(u8Flags, STA_ADD_FLAGS_OPER_MODE_NOTIF_VALID, MTLK_BFIELD_GET(info->flags, STA_FLAGS_OPMODE_NOTIF));
  MTLK_BFIELD_SET(u8Flags, STA_ADD_FLAGS_WME, MTLK_BFIELD_GET(info->flags, STA_FLAGS_WMM) ||
                                              MTLK_BFIELD_GET(u8Flags, STA_ADD_FLAGS_IS_HT) ||
                                              MTLK_BFIELD_GET(u8Flags, STA_ADD_FLAGS_IS_VHT));
  psUmiStaAdd->u8Flags = u8Flags;

  u8FlagsExt = 0;
  MTLK_BFIELD_SET(u8FlagsExt, STA_ADD_FLAGS_EXT_IS_HE, MTLK_BFIELD_GET(info->flags, STA_FLAGS_11ax));
  MTLK_BFIELD_SET(u8FlagsExt, STA_ADD_FLAGS_EXT_PBAC, MTLK_BFIELD_GET(info->flags, STA_FLAGS_PBAC));
  psUmiStaAdd->u8FlagsExt = u8FlagsExt;

  wave_memcpy(psUmiStaAdd->u8Rates, sizeof(psUmiStaAdd->u8Rates),
      info->rates, psUmiStaAdd->u8Rates_Length);
  wave_memcpy(psUmiStaAdd->u8RX_MCS_Bitmask, sizeof(psUmiStaAdd->u8RX_MCS_Bitmask),
      info->rx_mcs_bitmask, sizeof(info->rx_mcs_bitmask));
  wave_memcpy(psUmiStaAdd->u8VHT_Mcs_Nss, sizeof(psUmiStaAdd->u8VHT_Mcs_Nss),
      &info->vht_mcs_info, sizeof(info->vht_mcs_info));

  wave_memcpy(psUmiStaAdd->u8HE_Mcs_Nss, sizeof(psUmiStaAdd->u8HE_Mcs_Nss),
      &info->he_cap.he_mcs_nss_supp,
      MIN(sizeof(psUmiStaAdd->u8HE_Mcs_Nss), sizeof(info->he_cap.he_mcs_nss_supp))); /* 80P80 not supported by FW */

  wave_memcpy(psUmiStaAdd->u8HE_Ppe_Th, sizeof(psUmiStaAdd->u8HE_Ppe_Th),
      &info->he_cap.ppe_thres,
      MIN(sizeof(psUmiStaAdd->u8HE_Ppe_Th), sizeof(info->he_cap.ppe_thres)));  /* 8 NSS not supported by FW */

  ILOG1_D("CID-%04x: UMI_STA_ADD", mtlk_vap_get_oid(nic->vap_handle));
  mtlk_dump(1, psUmiStaAdd, sizeof(UMI_STA_ADD), "dump of UMI_STA_ADD:");

  ILOG1_D("UMI_STA_ADD->u16SID:                %u", MAC_TO_HOST16(psUmiStaAdd->u16SID));
  ILOG1_D("UMI_STA_ADD->u8VapIndex:            %u", psUmiStaAdd->u8VapIndex);
  ILOG1_D("UMI_STA_ADD->u8Status:              %u", psUmiStaAdd->u8Status);
  ILOG1_D("UMI_STA_ADD->u8ListenInterval:      %u", psUmiStaAdd->u8ListenInterval);
  ILOG1_D("UMI_STA_ADD->u8BSS_Coex_20_40:      %u", psUmiStaAdd->u8BSS_Coex_20_40);
  ILOG1_D("UMI_STA_ADD->u8UAPSD_Queues:        %u", psUmiStaAdd->u8UAPSD_Queues);
  ILOG1_D("UMI_STA_ADD->u8Max_SP:              %u", psUmiStaAdd->u8Max_SP);
  ILOG1_D("UMI_STA_ADD->u16AID:                %u", MAC_TO_HOST16(psUmiStaAdd->u16AID));
  ILOG1_Y("UMI_STA_ADD->sAddr:                 %Y", &psUmiStaAdd->sAddr);
  mtlk_dump(1, psUmiStaAdd->u8Rates, sizeof(psUmiStaAdd->u8Rates), "dump of UMI_STA_ADD->u8Rates:");
  ILOG1_D("UMI_STA_ADD->u16HT_Cap_Info:        %04X", MAC_TO_HOST16(psUmiStaAdd->u16HT_Cap_Info));
  ILOG1_D("UMI_STA_ADD->u8AMPDU_Param:         %u", psUmiStaAdd->u8AMPDU_Param);
  mtlk_dump(1, psUmiStaAdd->u8RX_MCS_Bitmask, sizeof(psUmiStaAdd->u8RX_MCS_Bitmask), "dump of UMI_STA_ADD->u8RX_MCS_Bitmask:");
  ILOG1_D("UMI_STA_ADD->u8Rates_Length:        %u", psUmiStaAdd->u8Rates_Length);
  ILOG1_DD("UMI_STA_ADD->rssi:                 %02X(%d)", psUmiStaAdd->rssi, (int8)psUmiStaAdd->rssi);
  ILOG1_D("UMI_STA_ADD->u8Flags:               %02X", psUmiStaAdd->u8Flags);
  ILOG1_D("UMI_STA_ADD->u8FlagsExt:            %02X", psUmiStaAdd->u8FlagsExt);
  ILOG1_D("UMI_STA_ADD->u32VHT_Cap_Info:       %08X", MAC_TO_HOST32(psUmiStaAdd->u32VHT_Cap_Info));

  ILOG1_D("UMI_STA_ADD->transmitBfCapabilities:%08X", MAC_TO_HOST32(psUmiStaAdd->transmitBfCapabilities));
  ILOG1_D("UMI_STA_ADD->u8VHT_OperatingModeNotification:%02X", psUmiStaAdd->u8VHT_OperatingModeNotification);
  ILOG1_D("UMI_STA_ADD->u8HE_OperatingModeNotification:%02X", psUmiStaAdd->u8HE_OperatingModeNotification);
  ILOG1_D("UMI_STA_ADD->u8WDS_client_type:     %u", psUmiStaAdd->u8WDS_client_type);
  mtlk_dump(1, psUmiStaAdd->u8VHT_Mcs_Nss, sizeof(psUmiStaAdd->u8VHT_Mcs_Nss), "dump of UMI_STA_ADD->u8VHT_Mcs_Nss:");

  ILOG1_D("UMI_STA_ADD->b8WDS:                 %u", MTLK_BFIELD_GET(u8Flags, STA_ADD_FLAGS_WDS));
  ILOG1_D("UMI_STA_ADD->b8WME:                 %u", MTLK_BFIELD_GET(u8Flags, STA_ADD_FLAGS_WME));
  ILOG1_D("UMI_STA_ADD->b8Authorized:          %u", MTLK_BFIELD_GET(u8Flags, STA_ADD_FLAGS_IS_8021X_FILTER_OPEN));
  ILOG1_D("UMI_STA_ADD->b8MFP:                 %u", MTLK_BFIELD_GET(u8Flags, STA_ADD_FLAGS_MFP));
  ILOG1_D("UMI_STA_ADD->b8HT:                  %u", MTLK_BFIELD_GET(u8Flags, STA_ADD_FLAGS_IS_HT));
  ILOG1_D("UMI_STA_ADD->b8VHT:                 %u", MTLK_BFIELD_GET(u8Flags, STA_ADD_FLAGS_IS_VHT));
  ILOG1_D("UMI_STA_ADD->b8OMN_supported:       %u", MTLK_BFIELD_GET(u8Flags, STA_ADD_FLAGS_OMN));
  ILOG1_D("UMI_STA_ADD->b8OPER_MODE_NOTIF:     %u", MTLK_BFIELD_GET(u8Flags, STA_ADD_FLAGS_OPER_MODE_NOTIF_VALID));
  ILOG1_D("UMI_STA_ADD->b8PBAC:                %u", MTLK_BFIELD_GET(u8FlagsExt, STA_ADD_FLAGS_EXT_PBAC));
  ILOG1_D("UMI_STA_ADD->b8HE:                  %u", MTLK_BFIELD_GET(u8FlagsExt, STA_ADD_FLAGS_EXT_IS_HE));
  ILOG1_D("UMI_STA_ADD->heExtSingleUserDisable: %u", psUmiStaAdd->heExtSingleUserDisable);

  mtlk_dump(1, psUmiStaAdd->u8HE_Mac_Phy_Cap_Info, sizeof(psUmiStaAdd->u8HE_Mac_Phy_Cap_Info), "dump of UMI_STA_ADD->u8HE_Mac_Phy_Cap_Info:");
  mtlk_dump(1, psUmiStaAdd->u8HE_Mcs_Nss, sizeof(psUmiStaAdd->u8HE_Mcs_Nss), "dump of UMI_STA_ADD->u8HE_Mcs_Nss:");
  mtlk_dump(1, psUmiStaAdd->u8HE_Ppe_Th, sizeof(psUmiStaAdd->u8HE_Ppe_Th), "dump of UMI_STA_ADD->u8HE_Ppe_Th:");
  mtlk_dump(1, &info->he_operation_parameters, sizeof(info->he_operation_parameters), "dump of info->he_operation_parameters:");

  res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);
  if (res != MTLK_ERR_OK) {
    ELOG_DD("CID-%04x: Can't send UM_MAN_STA_ADD_REQ request to MAC (err=%d)",
            mtlk_vap_get_oid(nic->vap_handle), res);
    goto FINISH;
  }

  if (psUmiStaAdd->u8Status != UMI_OK) {
    WLOG_DYD("CID-%04x: Station %Y add failed in FW (status=%u)",
             mtlk_vap_get_oid(nic->vap_handle),
             &info->addr,
             psUmiStaAdd->u8Status);
    res = MTLK_ERR_MAC;
    goto FINISH;
  }

  info->sid = MAC_TO_HOST16(psUmiStaAdd->u16SID);

  ILOG1_DYD("CID-%04x: Station %Y connected (SID = %u)",
            mtlk_vap_get_oid(nic->vap_handle),
            &info->addr,
            info->sid);

FINISH:
  if (man_entry) {
    mtlk_txmm_msg_cleanup(&man_msg);
  }

  return res;
}

int _mtlk_send_filter_req(mtlk_core_t *core, sta_entry *sta, BOOL authorizing);

static sta_entry *
_mtlk_core_add_sta (mtlk_core_t *core, const IEEE_ADDR *mac, sta_info *info_cfg)
{
#ifdef CPTCFG_IWLWAV_SET_PM_QOS
  BOOL is_empty = mtlk_global_stadb_is_empty();
#endif
  sta_entry *sta = mtlk_stadb_add_sta(&core->slow_ctx->stadb, mac->au8Addr, info_cfg);
#ifdef CPTCFG_IWLWAV_SET_PM_QOS
  if (sta && is_empty) {
    /* change PM QOS latency, if added very first STA */
    mtlk_mmb_update_cpu_dma_latency(mtlk_vap_get_hw(core->vap_handle), MTLK_HW_PM_QOS_VALUE_ANY_CLIENT);
  }
#endif
  return sta;
}

static enum mtlk_sta_4addr_mode_e _mtlk_core_check_static_4addr_mode (mtlk_core_t *core, const IEEE_ADDR *addr)
{
  /* For WDS WPA 4-way Handshake must be in 4-addr mode, otherwise in 3-addr mode:
   *    FOUR_ADRESSES_STATION - EAPOL 4-way handshake will be in 3-addr mode
   *    WPA_WDS               - EAPOL 4-way handshake will be in 4-addr mode
   * For both cases data afterwards will be sent in 4-addr mode
   */
  int four_addr_mode = MTLK_CORE_PDB_GET_INT(core, PARAM_DB_CORE_4ADDR_MODE);
  if (MTLK_CORE_4ADDR_DYNAMIC == four_addr_mode)
    return STA_4ADDR_MODE_DYNAMIC;
  if ((MTLK_CORE_4ADDR_STATIC == four_addr_mode) ||
      (MTLK_CORE_4ADDR_LIST == four_addr_mode && core_cfg_four_addr_entry_exists(core, addr)))
    return STA_4ADDR_MODE_ON;
  if (mtlk_core_cfg_wds_wpa_entry_exists(core, addr))
    return STA_4ADDR_MODE_ON;

  return STA_4ADDR_MODE_OFF;
}

static int _mtlk_core_ap_connect_sta_by_info (mtlk_core_t *core, sta_info *info)
{
  uint32 res = MTLK_ERR_PARAMS;
  const IEEE_ADDR *addr = &info->addr;
  sta_entry *sta = NULL;
  BOOL is_ht_capable;
  uint32 four_addr_mode = MTLK_CORE_PDB_GET_INT(core, PARAM_DB_CORE_4ADDR_MODE);

  /* set WDS flag for peer AP */
  if (mtlk_vap_is_ap(core->vap_handle) &&
      BR_MODE_WDS == MTLK_CORE_HOT_PATH_PDB_GET_INT(core, CORE_DB_CORE_BRIDGE_MODE)){
    MTLK_BFIELD_SET(info->flags, STA_FLAGS_WDS, wds_sta_is_peer_ap(&core->slow_ctx->wds_mng, addr) ? 1 : 0);
  }

  MTLK_BFIELD_SET(info->flags, STA_FLAGS_WDS_WPA, mtlk_core_cfg_wds_wpa_entry_exists(core, addr));

  /* For WDS WPA 4-way Handshake must be in 4-addr mode, otherwise in 3-addr mode:
   *    FOUR_ADDRESSES_STATION - EAPOL 4-way handshake will be in 3-addr mode
   *    WPA_WDS               - EAPOL 4-way handshake will be in 4-addr mode
   * For both cases data afterwards will be sent in 4-addr mode
   */
  if ((MTLK_CORE_4ADDR_STATIC == four_addr_mode) ||
    (MTLK_CORE_4ADDR_LIST == four_addr_mode &&
      core_cfg_four_addr_entry_exists(core, addr)))
    info->WDS_client_type = FOUR_ADDRESSES_STATION;
  else if (MTLK_BFIELD_GET(info->flags, STA_FLAGS_WDS_WPA))
    info->WDS_client_type = WPA_WDS;

  /* Reset of authorized flag for secure connection. This flag is set later on in _change_sta() */
  if (MTLK_BFIELD_GET(info->flags, STA_FLAGS_WDS)) {
    if (core->slow_ctx->peerAPs_key_idx) { /* WDS with WEP - clear filter after setting key below */
      MTLK_BFIELD_SET(info->flags, STA_FLAGS_IS_8021X_FILTER_OPEN, 0);
    }
  } else {
    if ((core->slow_ctx->group_cipher == IW_ENCODE_ALG_TKIP)
     || (core->slow_ctx->group_cipher == IW_ENCODE_ALG_CCMP)
     || (core->slow_ctx->group_cipher == IW_ENCODE_ALG_AES_CMAC)) {
      if (0 != MTLK_BFIELD_GET(info->flags, STA_FLAGS_IS_8021X_FILTER_OPEN)) {
        ELOG_DY("CID-%04x: STA:%Y flag STA_FLAGS_IS_8021X_FILTER_OPEN must be 0", mtlk_vap_get_oid(core->vap_handle), info->addr.au8Addr);
        MTLK_BFIELD_SET(info->flags, STA_FLAGS_IS_8021X_FILTER_OPEN, 0);
      }
    }
  }

  sta = mtlk_stadb_find_sta(&core->slow_ctx->stadb, info->addr.au8Addr);
  if (sta != NULL) {
    ELOG_DY("CID-%04x: ERROR:Try to add sta which already exist:%Y", mtlk_vap_get_oid(core->vap_handle), info->addr.au8Addr);
    mtlk_sta_decref(sta);
    MTLK_ASSERT(FALSE);
  }

  res = _mtlk_core_ap_add_sta_req(core, info);
  if (MTLK_ERR_OK != res) {
    goto FINISH;
  }

  /* Remove sta MAC addr from DC DP MAC table */
  mtlk_df_user_dcdp_remove_mac_addr(mtlk_vap_get_df(core->vap_handle), addr->au8Addr);

  is_ht_capable = MTLK_BFIELD_GET(info->flags, STA_FLAGS_11n);
  sta = _mtlk_core_add_sta(core, addr, info);

  if (sta == NULL) {
    core_ap_remove_sta(core, info);
    res = MTLK_ERR_UNKNOWN;
    goto FINISH;
  }

  if (MTLK_BFIELD_GET(sta->info.flags, STA_FLAGS_WDS)) {
    /* WDS peer */
    if (core->slow_ctx->peerAPs_key_idx) {
      if (MTLK_ERR_OK != _mtlk_core_set_wep_key_blocked(core, sta, core->slow_ctx->peerAPs_key_idx - 1)) {

        wds_peer_disconnect (&core->slow_ctx->wds_mng, addr);

        core_ap_stop_traffic(core, &sta->info); /* Send Stop Traffic Request to FW */

        core_ap_remove_sta(core, info);
        mtlk_stadb_remove_sta(&core->slow_ctx->stadb, sta);
        res = MTLK_ERR_UNKNOWN;
        goto FINISH;
      }

      mtlk_sta_set_cipher(sta, IW_ENCODE_ALG_WEP);
      res = _mtlk_send_filter_req(core, sta, TRUE); /* open filter */
    }
  }
  else {
    if (core->slow_ctx->wep_enabled) {
      mtlk_sta_set_cipher(sta, IW_ENCODE_ALG_WEP);
    }
  }

  if (!MTLK_BFIELD_GET(info->flags, STA_FLAGS_IS_8021X_FILTER_OPEN) && !MTLK_BFIELD_GET(sta->info.flags, STA_FLAGS_WDS)) {
    /* In WPA/WPA security start ADDBA after key is set */
    mtlk_sta_set_packets_filter(sta, MTLK_PCKT_FLTR_ALLOW_802_1X);
    ILOG1_Y("%Y: turn on 802.1x filtering due to RSN", mtlk_sta_get_addr(sta));
  }
  else {
    mtlk_sta_set_packets_filter(sta, MTLK_PCKT_FLTR_ALLOW_ALL);
    mtlk_stadb_set_sta_auth_flag_in_irbd(sta);
    mtlk_df_mcast_notify_sta_connected(mtlk_vap_get_df(core->vap_handle));
  }

  if (BR_MODE_WDS == MTLK_CORE_HOT_PATH_PDB_GET_INT(core, CORE_DB_CORE_BRIDGE_MODE) ||
    mtlk_sta_is_4addr(sta)) {
    mtlk_hstdb_start_all_by_sta(&core->slow_ctx->hstdb, sta);
  }

  mtlk_hstdb_remove_host_by_addr(&core->slow_ctx->hstdb, addr);

  if (BR_MODE_WDS == MTLK_CORE_HOT_PATH_PDB_GET_INT(core, CORE_DB_CORE_BRIDGE_MODE)){
    wds_peer_connect(&core->slow_ctx->wds_mng, sta, addr,
                     MTLK_BFIELD_GET(info->flags, STA_FLAGS_11n),
                     MTLK_BFIELD_GET(info->flags, STA_FLAGS_11ac), MTLK_BFIELD_GET(info->flags, STA_FLAGS_11ax));
  }

  /* notify Traffic Analyzer about new STA */
  mtlk_ta_on_connect(mtlk_vap_get_ta(core->vap_handle), sta);

  if (mtlk_vap_is_ap(core->vap_handle) && core->dgaf_disabled)
  {
    mtlk_mc_add_sta(core, sta);
  }

  if (!mtlk_vap_is_ap(core->vap_handle)) {
    bss_data_t bss_found;
    if (mtlk_cache_find_bss_by_bssid(&core->slow_ctx->cache, addr, &bss_found, NULL) == 0) {
      ILOG2_V("unknown BSS");
      goto FINISH;
    }
    /* store actual BSS data */
    core->slow_ctx->bss_data = bss_found;
    MTLK_CORE_PDB_SET_INT(core, PARAM_DB_CORE_IS_HT_CUR,
           (core_cfg_get_is_ht_cfg(core) && bss_found.is_ht));
  }



FINISH:
  if (sta) {
    mtlk_sta_decref(sta); /* De-reference by creator */
  }

  return res;
}

#ifdef MTLK_LEGACY_STATISTICS
static int _mtlk_core_on_peer_connect(mtlk_core_t *core,
                                      sta_info    *info,
                                      uint16       status)
{
  mtlk_connection_event_t connect_event;

  MTLK_ASSERT(core != NULL);
  MTLK_ASSERT(info != NULL);
  MTLK_STATIC_ASSERT(sizeof(connect_event.mac_addr) >= sizeof(IEEE_ADDR));

  memset(&connect_event, 0, sizeof(connect_event));

  *((IEEE_ADDR *)&connect_event.mac_addr) = info->addr;
  connect_event.status = status;

  if (MTLK_BFIELD_GET(info->flags, STA_FLAGS_IS_8021X_FILTER_OPEN))
    connect_event.auth_type = UMI_BSS_AUTH_OPEN;
  else
    connect_event.auth_type = UMI_BSS_AUTH_SHARED_KEY;

  return mtlk_wssd_send_event(mtlk_vap_get_irbd(core->vap_handle),
                              MTLK_WSSA_DRV_EVENT_CONNECTION,
                              &connect_event,
                              sizeof(connect_event));
}
#endif /* MTLK_LEGACY_STATISTICS */

static int _mtlk_core_ap_connect_sta (mtlk_handle_t hcore, const void* data, uint32 data_size)
{
  int res = MTLK_ERR_OK;
  mtlk_core_t *core = HANDLE_T_PTR(mtlk_core_t, hcore);
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;
  sta_info *info = NULL;
  uint32 info_size;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  info = mtlk_clpb_enum_get_next(clpb, &info_size);
  MTLK_CLPB_TRY(info, info_size)
    if (mtlk_vap_is_ap(core->vap_handle) && !wave_core_sync_hostapd_done_get(core)) {
      ILOG1_D("CID-%04x: HOSTAPD STA database is not synced", mtlk_vap_get_oid(core->vap_handle));
      res = MTLK_ERR_NOT_READY;
    }
    else {
      res = _mtlk_core_ap_connect_sta_by_info(core, info);
    }
#ifdef MTLK_LEGACY_STATISTICS
    _mtlk_core_on_peer_connect(core, info, res ? FM_STATUSCODE_FAILURE : FM_STATUSCODE_SUCCESSFUL);
#endif
  MTLK_CLPB_FINALLY(res)
    return mtlk_clpb_push_res(clpb, res);
  MTLK_CLPB_END
}

int _mtlk_send_filter_req(mtlk_core_t *core, sta_entry *sta, BOOL authorizing)
{
  mtlk_txmm_msg_t           man_msg;
  mtlk_txmm_data_t          *man_entry = NULL;
  mtlk_txmm_t               *txmm = mtlk_vap_get_txmm(core->vap_handle);
  UMI_802_1X_FILTER         *umi_filter;
  uint32 res = MTLK_ERR_PARAMS;

  /* Prepare UMI message */
  man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, txmm, NULL);
  if (!man_entry) {
    ELOG_D("CID-%04x: No man entry available", mtlk_vap_get_oid(core->vap_handle));
    return MTLK_ERR_NO_RESOURCES;
  }

  man_entry->id           = UM_MAN_SET_802_1X_FILTER_REQ;
  man_entry->payload_size = sizeof(UMI_802_1X_FILTER);
  umi_filter = (UMI_802_1X_FILTER *)man_entry->payload;
  memset(umi_filter, 0, sizeof(*umi_filter));

  umi_filter->u16SID         = HOST_TO_MAC16(mtlk_sta_get_sid(sta));
  umi_filter->u8IsFilterOpen = authorizing;

  ILOG1_DD("CID-%04x: UM_MAN_SET_802_1X_FILTER_REQ            SID:0x%x", mtlk_vap_get_oid(core->vap_handle),
           MAC_TO_HOST16(umi_filter->u16SID));
  ILOG1_DD("CID-%04x: UM_MAN_SET_802_1X_FILTER_REQ u8IsFilterOpen:%d", mtlk_vap_get_oid(core->vap_handle),
           umi_filter->u8IsFilterOpen);

  res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);
  if (MTLK_ERR_OK != res) {
    ELOG_DD("CID-%04x: mtlk_mm_send_blocked failed: %i", mtlk_vap_get_oid(core->vap_handle), res);
  }

  if (NULL != man_entry) {
    mtlk_txmm_msg_cleanup(&man_msg);
  }

  return res;
}

static int
_mtlk_core_ap_authorizing_sta (mtlk_handle_t hcore, const void* data, uint32 data_size)
{
  uint32 res = MTLK_ERR_PARAMS;
  mtlk_core_t *core = HANDLE_T_PTR(mtlk_core_t, hcore);
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;
  mtlk_core_ui_authorizing_cfg_t *t_authorizing;
  uint32       size;
  IEEE_ADDR    sStationID;
  BOOL         authorizing = FALSE;
  sta_entry   *sta = NULL;
  mtlk_pckt_filter_e       sta_filter_stored;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  if (mtlk_vap_is_ap(core->vap_handle) && !wave_core_sync_hostapd_done_get(core)) {
    ILOG1_D("CID-%04x: HOSTAPD STA database is not synced", mtlk_vap_get_oid(core->vap_handle));
    res = MTLK_ERR_NOT_READY;
    goto FINISH;
  }

  if ((mtlk_core_get_net_state(core) & (NET_STATE_READY | NET_STATE_CONNECTED)) == 0) {
    ILOG1_D("CID-%04x: Invalid card state - request rejected", mtlk_vap_get_oid(core->vap_handle));
    res = MTLK_ERR_NOT_READY;
    goto FINISH;
  }

  if (mtlk_core_is_stopping(core)) {
    ILOG1_D("CID-%04x: Can't set authorizing configuration - core is stopping", mtlk_vap_get_oid(core->vap_handle));
    res = MTLK_ERR_NOT_READY;
    goto FINISH;
  }

  t_authorizing = mtlk_clpb_enum_get_next(clpb, &size);
  if ( (NULL == t_authorizing) || (sizeof(*t_authorizing) != size) ) {
    ELOG_D("CID-%04x: Failed to set authorizing configuration parameters from CLPB", mtlk_vap_get_oid(core->vap_handle));
    res = MTLK_ERR_UNKNOWN;
    goto FINISH;
  }

  MTLK_CFG_START_CHEK_ITEM_AND_CALL()
    MTLK_CFG_GET_ITEM(t_authorizing, sta_addr, sStationID);
    MTLK_CFG_GET_ITEM(t_authorizing, authorizing, authorizing);
  MTLK_CFG_END_CHEK_ITEM_AND_CALL()

  sta = mtlk_stadb_find_sta(&core->slow_ctx->stadb, sStationID.au8Addr);
  if (sta == NULL) {
    ILOG1_DY("CID-%04x: Station %Y not found during authorizing",
             mtlk_vap_get_oid(core->vap_handle), sStationID.au8Addr);
    res = MTLK_ERR_OK;
    goto FINISH;
  }

  /*check if we already have the same filter set */
  sta_filter_stored = mtlk_sta_get_packets_filter(sta);
  if ((MTLK_PCKT_FLTR_ALLOW_ALL == sta_filter_stored) && (authorizing)) {
    ILOG2_DY("CID-%04x: Station %Y has already filter set",
             mtlk_vap_get_oid(core->vap_handle), sStationID.au8Addr);

    res = MTLK_ERR_OK;
    goto FINISH;

  } else if ((MTLK_PCKT_FLTR_ALLOW_802_1X == sta_filter_stored) && (!authorizing)) {
    ILOG2_DY("CID-%04x: Station %Y has already filter set",
             mtlk_vap_get_oid(core->vap_handle), sStationID.au8Addr);

    res = MTLK_ERR_OK;
    goto FINISH;
  }

  res = _mtlk_send_filter_req(core, sta, authorizing);

  if(authorizing){
    mtlk_sta_set_packets_filter(sta, MTLK_PCKT_FLTR_ALLOW_ALL);
    mtlk_stadb_set_sta_auth_flag_in_irbd(sta);
    mtlk_df_mcast_notify_sta_connected(mtlk_vap_get_df(core->vap_handle));
  }
  else{
    mtlk_sta_set_packets_filter(sta, MTLK_PCKT_FLTR_ALLOW_802_1X);
  }

FINISH:
  if (NULL != sta) {
    mtlk_sta_decref(sta); /* De-reference of find */
  }

  return res;
}

static int
_mtlk_core_ap_disconnect_sta (mtlk_handle_t hcore, const void* data, uint32 data_size)
{
  uint32 res = MTLK_ERR_PARAMS;
  mtlk_core_t *core = HANDLE_T_PTR(mtlk_core_t, hcore);
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;
  IEEE_ADDR *addr;
  uint32 size;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  addr = mtlk_clpb_enum_get_next(clpb, &size);
  MTLK_CLPB_TRY(addr, size)
#ifdef MTLK_LEGACY_STATISTICS
    res = _mtlk_core_ap_disconnect_sta_blocked(core,
                                               addr,
                                               FM_STATUSCODE_USER_REQUEST);
#else
    res = _mtlk_core_ap_disconnect_sta_blocked(core, addr);
#endif
    if (MTLK_ERR_OK != res) {
      ILOG1_DYD("CID-%04x: Station %Y disconnection failed (%d)",
                mtlk_vap_get_oid(core->vap_handle),
                addr, res);
    }
  MTLK_CLPB_FINALLY(res)
    return mtlk_clpb_push_res(clpb, res);
  MTLK_CLPB_END
}

int __MTLK_IFUNC
core_ap_disconnect_all (mtlk_core_t *core)
{
  uint32               res = MTLK_ERR_OK;
  const sta_entry      *sta = NULL;
  /* int                   net_state = mtlk_core_get_net_state(nic); */
  mtlk_stadb_iterator_t iter;

  /* FIXCFG80211: Check if this should be removed or leave as is.
     Note: NET_STATE_CONNECTED is only after core activation,
           this function can be called before start_ap by hostapd.
  */
  /*
  if (mtlk_core_get_net_state(nic) != NET_STATE_CONNECTED) {
    WLOG_V("AP is down - request rejected");
    res = MTLK_ERR_NOT_READY;
    goto FINISH;
  }
  */

  sta = mtlk_stadb_iterate_first(&core->slow_ctx->stadb, &iter);
  if (sta) {
    do {
      /*uint16 sid = mtlk_sta_get_sid(sta);*/
#ifdef MTLK_LEGACY_STATISTICS
      res = _mtlk_core_ap_disconnect_sta_blocked(core,
                                                 mtlk_sta_get_addr(sta),
                                                 FM_STATUSCODE_USER_REQUEST);
#else
      res = _mtlk_core_ap_disconnect_sta_blocked(core, mtlk_sta_get_addr(sta));
#endif
      if (res != MTLK_ERR_OK) {
        ELOG_DYD("CID-%04x: Station %Y disconnection failed (%d)",
                 mtlk_vap_get_oid(core->vap_handle),
                 mtlk_sta_get_addr(sta), res);
        break;
      }

      /* all SIDs will be removed by commands from hostapd */
      /*core_remove_sid(core, sid);*/
      sta = mtlk_stadb_iterate_next(&iter);
    } while (sta);
    mtlk_stadb_iterate_done(&iter);
  }

  return res;
}

#ifdef MTLK_LEGACY_STATISTICS
static int _mtlk_core_on_peer_disconnect(mtlk_core_t *core,
                                        sta_entry   *sta,
                                        uint16       reason)
{
  mtlk_disconnection_event_t disconnect_event;

  MTLK_ASSERT(core != NULL);
  MTLK_ASSERT(sta != NULL);
  MTLK_STATIC_ASSERT(sizeof(disconnect_event.mac_addr) >= sizeof(IEEE_ADDR));

  memset(&disconnect_event, 0, sizeof(disconnect_event));
  *((IEEE_ADDR *)&disconnect_event.mac_addr) = *mtlk_sta_get_addr(sta);
  disconnect_event.reason = reason;

  if((FM_STATUSCODE_AGED_OUT            == reason)   ||
     (FM_STATUSCODE_INACTIVITY          == reason)   ||
     (FM_STATUSCODE_USER_REQUEST        == reason)   ||
     (FM_STATUSCODE_PEER_PARAMS_CHANGED == reason))
  {
    disconnect_event.initiator = MTLK_DI_THIS_SIDE;
  }
  else disconnect_event.initiator = MTLK_DI_OTHER_SIDE;

  mtlk_sta_get_peer_stats(sta, &disconnect_event.peer_stats);
  mtlk_sta_get_peer_capabilities(sta, &disconnect_event.peer_capabilities);
  return mtlk_wssd_send_event(mtlk_vap_get_irbd(core->vap_handle),
                              MTLK_WSSA_DRV_EVENT_DISCONNECTION,
                              &disconnect_event,
                              sizeof(disconnect_event));
}
#endif /* MTLK_LEGACY_STATISTICS */

static int
_handle_security_alert_ind(mtlk_handle_t object, const void *data,  uint32 data_size)
{
  mtlk_core_t *core = HANDLE_T_PTR(mtlk_core_t, object);
  UMI_TKIP_MIC_FAILURE *usa = (UMI_TKIP_MIC_FAILURE *)data;
  mtlk_df_ui_mic_fail_type_t mic_fail_type;
  sta_entry *sta = NULL;
  mtlk_core_t         *cur_core;
  mtlk_vap_manager_t  *vap_mng;
  mtlk_vap_handle_t   vap_handle;
  unsigned            i, j, max_vaps;
  uint16_t            stationId;

  MTLK_ASSERT(sizeof(UMI_TKIP_MIC_FAILURE) == data_size);

  if (MAX_NUMBER_TKIP_MIC_FAILURE_ENTRIES_IN_MESSAGE < usa->u8numOfValidEntries) {
    ELOG_D("Number of valid entries too big: %d", usa->u8numOfValidEntries);
    mtlk_dump(0, usa, sizeof(*usa), "Dump of UMI_TKIP_MIC_FAILURE");
    return MTLK_ERR_OK;
  }

  vap_mng  = mtlk_vap_get_manager(core->vap_handle);
  max_vaps = mtlk_vap_manager_get_max_vaps_count(vap_mng);

  for (i = 0; i < usa->u8numOfValidEntries; i++) {
    stationId = MAC_TO_HOST16(usa->micFailureEntry[i].stationId);
    for (j = 0; j < max_vaps; j++) {
      if (MTLK_ERR_OK != mtlk_vap_manager_get_vap_handle(vap_mng, j, &vap_handle)) {
        continue;   /* VAP does not exist */
      }
      cur_core = mtlk_vap_get_core(vap_handle);
      if (NET_STATE_CONNECTED != mtlk_core_get_net_state(cur_core)) {
        /* Core is not ready */
        continue;
      }
      sta = mtlk_stadb_find_sid(&cur_core->slow_ctx->stadb, stationId);

      if (sta) {
        mic_fail_type = usa->micFailureEntry[i].isGroupKey ? MIC_FAIL_GROUP : MIC_FAIL_PAIRWISE;
        /* todo: temporarily disabled till fixed in FW */
#if 0
        ELOG_DDYS("CID-%04x: MIC failure STA_ID:%d STA:%Y type:%s", mtlk_vap_get_oid(vap_handle),
          stationId, mtlk_sta_get_addr(sta)->au8Addr,
          (mic_fail_type == MIC_FAIL_GROUP) ? "GROUP" : "PAIRWISE");
#endif
        mtlk_df_ui_notify_mic_failure(mtlk_vap_get_df(vap_handle),
                                   mtlk_sta_get_addr(sta)->au8Addr, mic_fail_type);
        _mtlk_core_on_mic_failure(cur_core, mic_fail_type);
         mtlk_sta_decref(sta); /* De-reference of find */
        break;
      }
    }
    /* todo: temporarily disabled till fixed in FW */
#if 0
    if (!sta) {
      ELOG_DD("CID-%04x: MIC failure for unknown STA_ID:%d", mtlk_vap_get_oid(core->vap_handle), stationId);
    }
#endif
  }

  return MTLK_ERR_OK;
}

uint32 hw_get_max_tx_antennas (mtlk_hw_t *hw);
uint32 hw_get_max_rx_antennas (mtlk_hw_t *hw);

uint32 mtlk_core_get_max_tx_antennas (struct nic* nic)
{
  return wave_radio_max_tx_antennas_get(wave_vap_radio_get(nic->vap_handle));
}

uint32 mtlk_core_get_max_rx_antennas (struct nic* nic)
{
  return wave_radio_max_rx_antennas_get(wave_vap_radio_get(nic->vap_handle));
}

/* To keep this valid forever Master VAP slow ctx should only get deleted on card/driver removal */
int current_chandef_init(mtlk_core_t *core, struct mtlk_chan_def *ccd)
{
  UMI_HDK_CONFIG *chc = __wave_core_hdkconfig_get(core);
  uint32 dma_addr;
  mtlk_eeprom_data_t *eeprom = mtlk_core_get_eeprom(core);

  if (MTLK_ERR_OK != mtlk_eeprom_is_valid(eeprom))
  {
    ELOG_D("CID-%04x: EEPROM/calibration file info is invalid", mtlk_vap_get_oid(core->vap_handle));
    return MTLK_ERR_UNKNOWN;
  }

  memset(ccd, 0, sizeof(*ccd));
  /* mtlk_osal_lock_init(&ccd->chan_lock); */
  __wave_core_chan_switch_type_set(core, ST_NONE);

  memset(chc, 0, sizeof(*chc));
  mtlk_hw_get_prop(mtlk_vap_get_hwapi(core->vap_handle), MTLK_HW_CALIB_BUF_DMA_ADDR, &dma_addr, sizeof(dma_addr));
  chc->calibrationBufferBaseAddress = HOST_TO_MAC32(dma_addr);

  chc->hdkConf.numTxAnts = mtlk_core_get_max_tx_antennas(core);
  chc->hdkConf.numRxAnts = mtlk_core_get_max_rx_antennas(core);
  chc->hdkConf.eepromInfo.u16EEPROMVersion = HOST_TO_MAC16(mtlk_eeprom_get_version(eeprom));

#if 1   /* FIXME: to be removed because is not used by FW, i.e. zero values can be used */
        /* TODO:  remove also the functions mtlk_eeprom_get_numpointsXX and all related data */
  chc->hdkConf.eepromInfo.u8NumberOfPoints5GHz = mtlk_eeprom_get_numpoints52(eeprom);
  chc->hdkConf.eepromInfo.u8NumberOfPoints2GHz = mtlk_eeprom_get_numpoints24(eeprom);
#endif

  return MTLK_ERR_OK;
}

/* steps for init and cleanup */
MTLK_INIT_STEPS_LIST_BEGIN(core_slow_ctx)
#ifdef EEPROM_DATA_ACCESS
  MTLK_INIT_STEPS_LIST_ENTRY(core_slow_ctx, EEPROM)
#endif /* EEPROM_DATA_ACCESS */
  MTLK_INIT_STEPS_LIST_ENTRY(core_slow_ctx, SERIALIZER)
  MTLK_INIT_STEPS_LIST_ENTRY(core_slow_ctx, SET_NIC_CFG)
  MTLK_INIT_STEPS_LIST_ENTRY(core_slow_ctx, WATCHDOG_TIMER_INIT)
  MTLK_INIT_STEPS_LIST_ENTRY(core_slow_ctx, CONNECT_EVENT_INIT)
  MTLK_INIT_STEPS_LIST_ENTRY(core_slow_ctx, STADB_INIT)
  MTLK_INIT_STEPS_LIST_ENTRY(core_slow_ctx, HSTDB_INIT)
  MTLK_INIT_STEPS_LIST_ENTRY(core_slow_ctx, BEACON_DATA)
  MTLK_INIT_STEPS_LIST_ENTRY(core_slow_ctx, MGMT_FRAME_FILTER)
  MTLK_INIT_STEPS_LIST_ENTRY(core_slow_ctx, SCHED_SCAN_TIMER_INIT)
  MTLK_INIT_STEPS_LIST_ENTRY(core_slow_ctx, CAC_TIMER_INIT)
  MTLK_INIT_STEPS_LIST_ENTRY(core_slow_ctx, CCA_TIMER_INIT)
  MTLK_INIT_STEPS_LIST_ENTRY(core_slow_ctx, SCHED_SCAN_REQ_INIT)
  MTLK_INIT_STEPS_LIST_ENTRY(core_slow_ctx, PROBE_RESP_TEMPL)
MTLK_INIT_INNER_STEPS_BEGIN(core_slow_ctx)
MTLK_INIT_STEPS_LIST_END(core_slow_ctx);

static __INLINE
void mem_free_nullsafe(void **ptrptr)
{
  if (*ptrptr)
  {
    mtlk_osal_mem_free(*ptrptr);
    *ptrptr = NULL;
  }
}

static __INLINE void __probe_resp_templ_mem_free (struct nic_slow_ctx *slow_ctx)
{
  if (slow_ctx->probe_resp_templ)
    mtlk_osal_mem_free(slow_ctx->probe_resp_templ);

  if (slow_ctx->probe_resp_templ_non_he &&
      (slow_ctx->probe_resp_templ_non_he != slow_ctx->probe_resp_templ))
    mtlk_osal_mem_free(slow_ctx->probe_resp_templ_non_he);

  slow_ctx->probe_resp_templ = NULL;
  slow_ctx->probe_resp_templ_non_he = NULL;
}

static void __MTLK_IFUNC
_mtlk_slow_ctx_cleanup(struct nic_slow_ctx *slow_ctx, struct nic* nic)
{
  MTLK_ASSERT(NULL != slow_ctx);
  MTLK_ASSERT(NULL != nic);

  MTLK_CLEANUP_BEGIN(core_slow_ctx, MTLK_OBJ_PTR(slow_ctx))

    MTLK_CLEANUP_STEP(core_slow_ctx, PROBE_RESP_TEMPL, MTLK_OBJ_PTR(slow_ctx),
                      __probe_resp_templ_mem_free, (slow_ctx));
    MTLK_CLEANUP_STEP(core_slow_ctx, SCHED_SCAN_REQ_INIT, MTLK_OBJ_PTR(slow_ctx),
                      mem_free_nullsafe, ((void **) &slow_ctx->sched_scan_req));
    MTLK_CLEANUP_STEP(core_slow_ctx, CCA_TIMER_INIT, MTLK_OBJ_PTR(slow_ctx),
                      mtlk_osal_timer_cleanup, (&slow_ctx->cca_step_down_timer));
    MTLK_CLEANUP_STEP(core_slow_ctx, CAC_TIMER_INIT, MTLK_OBJ_PTR(slow_ctx),
                      mtlk_osal_timer_cleanup, (&slow_ctx->cac_timer));
    MTLK_CLEANUP_STEP(core_slow_ctx, SCHED_SCAN_TIMER_INIT, MTLK_OBJ_PTR(slow_ctx),
                      mtlk_osal_timer_cleanup, (&slow_ctx->sched_scan_timer));
    MTLK_CLEANUP_STEP(core_slow_ctx, MGMT_FRAME_FILTER, MTLK_OBJ_PTR(slow_ctx),
                      mgmt_frame_filter_cleanup, (&slow_ctx->mgmt_frame_filter));
    MTLK_CLEANUP_STEP(core_slow_ctx, BEACON_DATA, MTLK_OBJ_PTR(slow_ctx),
                      wave_beacon_man_cleanup, (&slow_ctx->beacon_man_data, nic->vap_handle));

    MTLK_CLEANUP_STEP(core_slow_ctx, HSTDB_INIT, MTLK_OBJ_PTR(slow_ctx),
                      mtlk_hstdb_cleanup, (&slow_ctx->hstdb));

    MTLK_CLEANUP_STEP(core_slow_ctx, STADB_INIT, MTLK_OBJ_PTR(slow_ctx),
                      mtlk_stadb_cleanup, (&slow_ctx->stadb));

    MTLK_CLEANUP_STEP(core_slow_ctx, CONNECT_EVENT_INIT, MTLK_OBJ_PTR(slow_ctx),
                      mtlk_osal_event_cleanup, (&slow_ctx->connect_event));

    MTLK_CLEANUP_STEP(core_slow_ctx, WATCHDOG_TIMER_INIT, MTLK_OBJ_PTR(slow_ctx),
                      mtlk_osal_timer_cleanup, (&slow_ctx->mac_watchdog_timer));

    MTLK_CLEANUP_STEP(core_slow_ctx, SET_NIC_CFG, MTLK_OBJ_PTR(slow_ctx),
                      MTLK_NOACTION, ());

    MTLK_CLEANUP_STEP(core_slow_ctx, SERIALIZER, MTLK_OBJ_PTR(slow_ctx),
                      mtlk_serializer_cleanup, (&slow_ctx->serializer));

#ifdef EEPROM_DATA_ACCESS
    MTLK_CLEANUP_STEP(core_slow_ctx, EEPROM, MTLK_OBJ_PTR(slow_ctx),
                      mtlk_eeprom_access_cleanup, (nic->vap_handle));
#endif /* EEPROM_DATA_ACCESS */

  MTLK_CLEANUP_END(core_slow_ctx, MTLK_OBJ_PTR(slow_ctx));
}

static int __MTLK_IFUNC
_mtlk_slow_ctx_init(struct nic_slow_ctx *slow_ctx, struct nic* nic)
{
  char ser_name[sizeof(slow_ctx->serializer.name)];

  MTLK_ASSERT(NULL != slow_ctx);
  MTLK_ASSERT(NULL != nic);

  memset(slow_ctx, 0, sizeof(struct nic_slow_ctx));
  slow_ctx->nic = nic;

  snprintf(ser_name, sizeof(ser_name), "mtlk_%s", mtlk_df_get_name(mtlk_vap_get_df(nic->vap_handle)));

  MTLK_INIT_TRY(core_slow_ctx, MTLK_OBJ_PTR(slow_ctx))

#ifdef EEPROM_DATA_ACCESS
    MTLK_INIT_STEP_IF(!mtlk_vap_is_slave_ap(nic->vap_handle), core_slow_ctx, EEPROM, MTLK_OBJ_PTR(slow_ctx),
                      mtlk_eeprom_access_init, (nic->vap_handle));
#endif /* EEPROM_DATA_ACCESS */

    MTLK_INIT_STEP(core_slow_ctx, SERIALIZER, MTLK_OBJ_PTR(slow_ctx),
                   mtlk_serializer_init, (&slow_ctx->serializer, ser_name, _MTLK_CORE_NUM_PRIORITIES));

    MTLK_INIT_STEP_VOID(core_slow_ctx, SET_NIC_CFG, MTLK_OBJ_PTR(slow_ctx),
                        mtlk_mib_set_nic_cfg, (nic));

    MTLK_INIT_STEP_IF(!mtlk_vap_is_slave_ap(nic->vap_handle), core_slow_ctx, WATCHDOG_TIMER_INIT, MTLK_OBJ_PTR(slow_ctx),
                      mtlk_osal_timer_init, (&slow_ctx->mac_watchdog_timer,
                                             mac_watchdog_timer_handler,
                                             HANDLE_T(nic)));

    MTLK_INIT_STEP(core_slow_ctx, CONNECT_EVENT_INIT, MTLK_OBJ_PTR(slow_ctx),
                   mtlk_osal_event_init, (&slow_ctx->connect_event));

    /* TODO MAC80211: is this required for station mode interfaces? */
    MTLK_INIT_STEP(core_slow_ctx, STADB_INIT, MTLK_OBJ_PTR(slow_ctx),
                   mtlk_stadb_init, (&slow_ctx->stadb, nic->vap_handle));

    MTLK_INIT_STEP(core_slow_ctx, HSTDB_INIT, MTLK_OBJ_PTR(slow_ctx),
                   mtlk_hstdb_init, (&slow_ctx->hstdb, nic->vap_handle));

    /* TODO MAC80211: is this required for station mode interfaces? */
    MTLK_INIT_STEP(core_slow_ctx, BEACON_DATA, MTLK_OBJ_PTR(slow_ctx),
                   wave_beacon_man_init, (&slow_ctx->beacon_man_data, nic->vap_handle));

    MTLK_INIT_STEP(core_slow_ctx, MGMT_FRAME_FILTER, MTLK_OBJ_PTR(slow_ctx),
                   mgmt_frame_filter_init, (&slow_ctx->mgmt_frame_filter));

    MTLK_INIT_STEP(core_slow_ctx, SCHED_SCAN_TIMER_INIT, MTLK_OBJ_PTR(slow_ctx),
                   mtlk_osal_timer_init, (&slow_ctx->sched_scan_timer, sched_scan_timer_clb_func, HANDLE_T(nic)));

    MTLK_INIT_STEP(core_slow_ctx, CAC_TIMER_INIT, MTLK_OBJ_PTR(slow_ctx),
                   mtlk_osal_timer_init, (&slow_ctx->cac_timer, cac_timer_clb_func, HANDLE_T(nic)));

    MTLK_INIT_STEP(core_slow_ctx, CCA_TIMER_INIT, MTLK_OBJ_PTR(slow_ctx),
                   mtlk_osal_timer_init, (&slow_ctx->cca_step_down_timer, cca_step_down_timer_clb_func, HANDLE_T(nic)));

    /* everything in slow_ctx was memset to 0, so we don't need to set sched_scan_req to NULL explicitly */
    MTLK_INIT_STEP_VOID(core_slow_ctx, SCHED_SCAN_REQ_INIT, MTLK_OBJ_PTR(slow_ctx),
                        MTLK_NOACTION, ());

    /* everything in slow_ctx was memset to 0, so we don't need to set probe_resp_templ to NULL explicitly */
    /* MAC80211 TODO: no need to do that for sta */
    MTLK_INIT_STEP_VOID(core_slow_ctx, PROBE_RESP_TEMPL, MTLK_OBJ_PTR(slow_ctx),
                        MTLK_NOACTION, ());

    nic->slow_ctx->tx_limits.num_tx_antennas = mtlk_core_get_max_tx_antennas(nic);
    nic->slow_ctx->tx_limits.num_rx_antennas = mtlk_core_get_max_rx_antennas(nic);

  MTLK_INIT_FINALLY(core_slow_ctx, MTLK_OBJ_PTR(slow_ctx))
  MTLK_INIT_RETURN(core_slow_ctx, MTLK_OBJ_PTR(slow_ctx), _mtlk_slow_ctx_cleanup, (nic->slow_ctx, nic))
}

static int
mtlk_core_crypto_cleanup (mtlk_core_t *core)
{
struct nic_slow_ctx *slow_ctx = core->slow_ctx;

  if(slow_ctx->wep_rx){
    crypto_free_cipher(slow_ctx->wep_rx);
  }

  if(slow_ctx->wep_tx){
    crypto_free_cipher(slow_ctx->wep_tx);
  }

  return MTLK_ERR_OK;
}

static int
mtlk_core_crypto_init  (mtlk_core_t *core)
{
struct nic_slow_ctx *slow_ctx = core->slow_ctx;

  /* start WEP IV from a random value */
  get_random_bytes(&slow_ctx->wep_iv, IEEE80211_WEP_IV_LEN);

  slow_ctx->wep_rx = crypto_alloc_cipher("arc4", 0, CRYPTO_ALG_ASYNC);
  if(!slow_ctx->wep_rx){
    ELOG_D("CID-%04x: Can't allocate wep cipher RX", mtlk_vap_get_oid(core->vap_handle));
    return MTLK_ERR_UNKNOWN;
  }

  slow_ctx->wep_tx = crypto_alloc_cipher("arc4", 0, CRYPTO_ALG_ASYNC);
  if(!slow_ctx->wep_tx){
    ELOG_D("CID-%04x: Can't allocate wep cipher TX", mtlk_vap_get_oid(core->vap_handle));
    crypto_free_cipher(slow_ctx->wep_rx);
    return MTLK_ERR_UNKNOWN;
  }

  return MTLK_ERR_OK;
}

BOOL __MTLK_IFUNC
mtlk_core_crypto_decrypt(mtlk_core_t *core, uint32 key_idx, uint8 *fbuf, uint8 *data, int32 data_len)
{
struct nic_slow_ctx *slow_ctx = core->slow_ctx;
uint8 rc4key[3 + MIB_WEP_KEY_WEP2_LENGTH];
uint32 key_len;
uint32 crc;
int i;

  /* check the cipher */
  if ((slow_ctx->keys[key_idx].key_len == 0) || ((slow_ctx->keys[key_idx].cipher != UMI_RSN_CIPHER_SUITE_WEP104) &&
       (slow_ctx->keys[key_idx].cipher != UMI_RSN_CIPHER_SUITE_WEP40))) {
    ILOG1_DDDD("CID-%04x: KEY:%d is not set or cipher is wrong key_len:%d cipher:%d", mtlk_vap_get_oid(core->vap_handle), key_idx, slow_ctx->keys[key_idx].key_len,
            slow_ctx->keys[key_idx].cipher);
    return FALSE;
  }

  key_len =  IEEE80211_WEP_IV_WO_IDX_LEN + slow_ctx->keys[key_idx].key_len;

  /* Prepend 24-bit IV to RC4 key */
  wave_memcpy(rc4key, sizeof(rc4key), fbuf, IEEE80211_WEP_IV_WO_IDX_LEN);

  /* Copy rest of the WEP key (the secret part) */
  wave_memcpy(rc4key + IEEE80211_WEP_IV_WO_IDX_LEN, sizeof(rc4key) - IEEE80211_WEP_IV_WO_IDX_LEN,
      slow_ctx->keys[key_idx].key, slow_ctx->keys[key_idx].key_len);

  crypto_cipher_setkey(slow_ctx->wep_rx, rc4key, key_len);
  for (i = 0; i < data_len + IEEE80211_WEP_ICV_LEN; i++) {
    crypto_cipher_decrypt_one(slow_ctx->wep_rx, data + i, data + i);
  }

  crc = cpu_to_le32(~crc32_le(~0, data, data_len));
  if (memcmp(&crc, data + data_len, IEEE80211_WEP_ICV_LEN) != 0) {
    /* ICV mismatch */
    ILOG1_D("CID-%04x: ICV mismatch", mtlk_vap_get_oid(core->vap_handle));
    return FALSE;
  }

  return TRUE;
}

/* steps for init and cleanup */
MTLK_INIT_STEPS_LIST_BEGIN(core)
  MTLK_INIT_STEPS_LIST_ENTRY(core, CORE_PDB_INIT)
  MTLK_INIT_STEPS_LIST_ENTRY(core, SLOW_CTX_ALLOC)
  MTLK_INIT_STEPS_LIST_ENTRY(core, SLOW_CTX_INIT)
  MTLK_INIT_STEPS_LIST_ENTRY(core, WSS_CREATE)
  MTLK_INIT_STEPS_LIST_ENTRY(core, WSS_HCTNRs)
  MTLK_INIT_STEPS_LIST_ENTRY(core, NET_STATE_LOCK_INIT)
  MTLK_INIT_STEPS_LIST_ENTRY(core, TXMM_EEPROM_ASYNC_MSGS_INIT)
  MTLK_INIT_STEPS_LIST_ENTRY(core, CRYPTO_INIT)
  MTLK_INIT_STEPS_LIST_ENTRY(core, BLACKLIST_INIT)
  MTLK_INIT_STEPS_LIST_ENTRY(core, BLACKLIST_LOCK_INIT)
  MTLK_INIT_STEPS_LIST_ENTRY(core, MULTI_AP_BLACKLIST_INIT)
  MTLK_INIT_STEPS_LIST_ENTRY(core, MULTI_AP_BLACKLIST_LOCK_INIT)
  MTLK_INIT_STEPS_LIST_ENTRY(core, 4ADDR_STA_LIST_INIT)
  MTLK_INIT_STEPS_LIST_ENTRY(core, 4ADDR_STA_LIST_LOCK_INIT)
  MTLK_INIT_STEPS_LIST_ENTRY(core, BC_PROBE_REQ_STA_LIST_INIT)
  MTLK_INIT_STEPS_LIST_ENTRY(core, BC_PROBE_REQ_STA_LIST_LOCK_INIT)
  MTLK_INIT_STEPS_LIST_ENTRY(core, UC_PROBE_REQ_STA_LIST_INIT)
  MTLK_INIT_STEPS_LIST_ENTRY(core, UC_PROBE_REQ_STA_LIST_LOCK_INIT)
  MTLK_INIT_STEPS_LIST_ENTRY(core, NDP_WDS_WPA_STA_LIST_INIT)
  MTLK_INIT_STEPS_LIST_ENTRY(core, NDP_WDS_WPA_STA_LIST_LOCK_INIT)
  MTLK_INIT_STEPS_LIST_ENTRY(core, NDP_ACKED)
MTLK_INIT_INNER_STEPS_BEGIN(core)
MTLK_INIT_STEPS_LIST_END(core);

static void __MTLK_IFUNC
_mtlk_core_cleanup(struct nic* nic)
{
  int i;

  MTLK_ASSERT(NULL != nic);

  /* delete all four address list entries */
  core_cfg_flush_four_addr_list(nic);
  /* delete all blacklist entries */
  _mtlk_core_flush_ieee_addr_list(nic, &nic->widan_blacklist, "widan black");
  _mtlk_core_flush_ieee_addr_list(nic, &nic->multi_ap_blacklist, "multi-AP black");
  _mtlk_core_flush_bcast_probe_resp_list(nic);
  _mtlk_core_flush_ucast_probe_resp_list(nic);
  mtlk_core_cfg_flush_wds_wpa_list(nic);
  mtlk_osal_event_terminate(&nic->ndp_acked);

  if (BR_MODE_WDS == MTLK_CORE_PDB_GET_INT(nic, PARAM_DB_CORE_BRIDGE_MODE)) {
    mtlk_vap_manager_dec_wds_bridgemode(mtlk_vap_get_manager(nic->vap_handle));
  }

  MTLK_CLEANUP_BEGIN(core, MTLK_OBJ_PTR(nic))

    MTLK_CLEANUP_STEP(core, NDP_ACKED, MTLK_OBJ_PTR(nic),
      mtlk_osal_event_cleanup, (&nic->ndp_acked));

    MTLK_CLEANUP_STEP(core, NDP_WDS_WPA_STA_LIST_LOCK_INIT, MTLK_OBJ_PTR(nic),
      mtlk_osal_lock_cleanup, (&nic->wds_wpa_sta_list.ieee_addr_lock));

    MTLK_CLEANUP_STEP(core, NDP_WDS_WPA_STA_LIST_INIT, MTLK_OBJ_PTR(nic),
      mtlk_hash_cleanup_ieee_addr, (&nic->wds_wpa_sta_list.hash));

    MTLK_CLEANUP_STEP(core, UC_PROBE_REQ_STA_LIST_LOCK_INIT, MTLK_OBJ_PTR(nic),
      mtlk_osal_lock_cleanup, (&nic->unicast_probe_resp_sta_list.ieee_addr_lock));

    MTLK_CLEANUP_STEP(core, UC_PROBE_REQ_STA_LIST_INIT, MTLK_OBJ_PTR(nic),
      mtlk_hash_cleanup_ieee_addr, (&nic->unicast_probe_resp_sta_list.hash));

    MTLK_CLEANUP_STEP(core, BC_PROBE_REQ_STA_LIST_LOCK_INIT, MTLK_OBJ_PTR(nic),
      mtlk_osal_lock_cleanup, (&nic->broadcast_probe_resp_sta_list.ieee_addr_lock));

    MTLK_CLEANUP_STEP(core, BC_PROBE_REQ_STA_LIST_INIT, MTLK_OBJ_PTR(nic),
      mtlk_hash_cleanup_ieee_addr, (&nic->broadcast_probe_resp_sta_list.hash));

    MTLK_CLEANUP_STEP(core, 4ADDR_STA_LIST_LOCK_INIT, MTLK_OBJ_PTR(nic),
      mtlk_osal_lock_cleanup, (&nic->four_addr_sta_list.ieee_addr_lock));

    MTLK_CLEANUP_STEP(core, 4ADDR_STA_LIST_INIT, MTLK_OBJ_PTR(nic),
      mtlk_hash_cleanup_ieee_addr, (&nic->four_addr_sta_list.hash));

    MTLK_CLEANUP_STEP(core, MULTI_AP_BLACKLIST_LOCK_INIT, MTLK_OBJ_PTR(nic),
      mtlk_osal_lock_cleanup, (&nic->multi_ap_blacklist.ieee_addr_lock));

    MTLK_CLEANUP_STEP(core, MULTI_AP_BLACKLIST_INIT, MTLK_OBJ_PTR(nic),
      mtlk_hash_cleanup_ieee_addr, (&nic->multi_ap_blacklist.hash));

    MTLK_CLEANUP_STEP(core, BLACKLIST_LOCK_INIT, MTLK_OBJ_PTR(nic),
      mtlk_osal_lock_cleanup, (&nic->widan_blacklist.ieee_addr_lock));

    MTLK_CLEANUP_STEP(core, BLACKLIST_INIT, MTLK_OBJ_PTR(nic),
      mtlk_hash_cleanup_ieee_addr, (&nic->widan_blacklist.hash));

    MTLK_CLEANUP_STEP(core, CRYPTO_INIT, MTLK_OBJ_PTR(nic), mtlk_core_crypto_cleanup, (nic));

    for (i = 0; i < ARRAY_SIZE(nic->txmm_async_eeprom_msgs); i++) {
      MTLK_CLEANUP_STEP_LOOP(core, TXMM_EEPROM_ASYNC_MSGS_INIT, MTLK_OBJ_PTR(nic),
                             mtlk_txmm_msg_cleanup, (&nic->txmm_async_eeprom_msgs[i]));
    }

    MTLK_CLEANUP_STEP(core, NET_STATE_LOCK_INIT, MTLK_OBJ_PTR(nic),
                      mtlk_osal_lock_cleanup, (&nic->net_state_lock));

    MTLK_CLEANUP_STEP(core, WSS_HCTNRs, MTLK_OBJ_PTR(nic),
                      mtlk_wss_cntrs_close, (nic->wss, nic->wss_hcntrs, ARRAY_SIZE(nic->wss_hcntrs)))

    MTLK_CLEANUP_STEP(core, WSS_CREATE, MTLK_OBJ_PTR(nic),
                      mtlk_wss_delete, (nic->wss));

    MTLK_CLEANUP_STEP(core, SLOW_CTX_INIT, MTLK_OBJ_PTR(nic),
                      _mtlk_slow_ctx_cleanup, (nic->slow_ctx, nic));

    MTLK_CLEANUP_STEP(core, SLOW_CTX_ALLOC, MTLK_OBJ_PTR(nic),
                      kfree_tag, (nic->slow_ctx));

    MTLK_CLEANUP_STEP(core, CORE_PDB_INIT, MTLK_OBJ_PTR(nic),
        mtlk_core_pdb_fast_handles_close, (nic->pdb_hot_path_handles));

  MTLK_CLEANUP_END(core, MTLK_OBJ_PTR(nic));
}

static int __MTLK_IFUNC
_mtlk_core_init(struct nic* nic, mtlk_vap_handle_t vap_handle, mtlk_df_t*   df)
{
  int           txem_cnt = 0;

  MTLK_ASSERT(NULL != nic);

  MTLK_STATIC_ASSERT(MTLK_CORE_CNT_LAST == ARRAY_SIZE(nic->wss_hcntrs));
  MTLK_STATIC_ASSERT(MTLK_CORE_CNT_LAST == ARRAY_SIZE(_mtlk_core_wss_id_map));

  MTLK_INIT_TRY(core, MTLK_OBJ_PTR(nic))
    /* set initial net state */
    nic->net_state   = NET_STATE_HALTED;
    nic->vap_handle  = vap_handle;
    nic->chan_state  = ST_LAST;

    nic->radio_wss = wave_radio_wss_get(wave_vap_radio_get(nic->vap_handle)); /* Get parent WSS from Radio */
    MTLK_ASSERT(NULL != nic->radio_wss);

    MTLK_INIT_STEP(core, CORE_PDB_INIT, MTLK_OBJ_PTR(nic),
        mtlk_core_pdb_fast_handles_open, (mtlk_vap_get_param_db(nic->vap_handle), nic->pdb_hot_path_handles));

    MTLK_INIT_STEP_EX(core, SLOW_CTX_ALLOC, MTLK_OBJ_PTR(nic),
                      kmalloc_tag, (sizeof(struct nic_slow_ctx), GFP_KERNEL, MTLK_MEM_TAG_CORE),
                      nic->slow_ctx, NULL != nic->slow_ctx, MTLK_ERR_NO_MEM);

    MTLK_INIT_STEP(core, SLOW_CTX_INIT, MTLK_OBJ_PTR(nic),
                   _mtlk_slow_ctx_init, (nic->slow_ctx, nic));

    MTLK_INIT_STEP_EX(core, WSS_CREATE, MTLK_OBJ_PTR(nic),
                      mtlk_wss_create, (nic->radio_wss, _mtlk_core_wss_id_map, ARRAY_SIZE(_mtlk_core_wss_id_map)),
                      nic->wss, nic->wss != NULL, MTLK_ERR_NO_MEM);

    MTLK_INIT_STEP(core, WSS_HCTNRs, MTLK_OBJ_PTR(nic),
                   mtlk_wss_cntrs_open, (nic->wss, _mtlk_core_wss_id_map, nic->wss_hcntrs, MTLK_CORE_CNT_LAST));

    MTLK_INIT_STEP(core, NET_STATE_LOCK_INIT, MTLK_OBJ_PTR(nic),
                   mtlk_osal_lock_init, (&nic->net_state_lock));

    for (txem_cnt = 0; txem_cnt < ARRAY_SIZE(nic->txmm_async_eeprom_msgs); txem_cnt++) {
      MTLK_INIT_STEP_LOOP(core, TXMM_EEPROM_ASYNC_MSGS_INIT, MTLK_OBJ_PTR(nic),
                          mtlk_txmm_msg_init, (&nic->txmm_async_eeprom_msgs[txem_cnt]));
    }


    MTLK_INIT_STEP(core, CRYPTO_INIT, MTLK_OBJ_PTR(nic), mtlk_core_crypto_init, (nic));

    MTLK_INIT_STEP_VOID(core, BLACKLIST_INIT, MTLK_OBJ_PTR(nic),
      mtlk_hash_init_ieee_addr, (&nic->widan_blacklist.hash,
        MTLK_CORE_WIDAN_BLACKLIST_HASH_NOF_BUCKETS));

    MTLK_INIT_STEP(core, BLACKLIST_LOCK_INIT, MTLK_OBJ_PTR(nic),
      mtlk_osal_lock_init, (&nic->widan_blacklist.ieee_addr_lock));

    MTLK_INIT_STEP_VOID(core, MULTI_AP_BLACKLIST_INIT, MTLK_OBJ_PTR(nic),
      mtlk_hash_init_ieee_addr, (&nic->multi_ap_blacklist.hash,
        MTLK_CORE_STA_LIST_HASH_NOF_BUCKETS));

    MTLK_INIT_STEP(core, MULTI_AP_BLACKLIST_LOCK_INIT, MTLK_OBJ_PTR(nic),
      mtlk_osal_lock_init, (&nic->multi_ap_blacklist.ieee_addr_lock));

    MTLK_INIT_STEP_VOID(core, 4ADDR_STA_LIST_INIT, MTLK_OBJ_PTR(nic),
      mtlk_hash_init_ieee_addr, (&nic->four_addr_sta_list.hash,
        MTLK_CORE_4ADDR_STA_LIST_HASH_NOF_BUCKETS));

    MTLK_INIT_STEP(core, 4ADDR_STA_LIST_LOCK_INIT, MTLK_OBJ_PTR(nic),
      mtlk_osal_lock_init, (&nic->four_addr_sta_list.ieee_addr_lock));

    MTLK_INIT_STEP_VOID(core, BC_PROBE_REQ_STA_LIST_INIT, MTLK_OBJ_PTR(nic),
      mtlk_hash_init_ieee_addr, (&nic->broadcast_probe_resp_sta_list.hash,
        MTLK_CORE_STA_LIST_HASH_NOF_BUCKETS));

    MTLK_INIT_STEP(core, BC_PROBE_REQ_STA_LIST_LOCK_INIT, MTLK_OBJ_PTR(nic),
      mtlk_osal_lock_init, (&nic->broadcast_probe_resp_sta_list.ieee_addr_lock));

    MTLK_INIT_STEP_VOID(core, UC_PROBE_REQ_STA_LIST_INIT, MTLK_OBJ_PTR(nic),
      mtlk_hash_init_ieee_addr, (&nic->unicast_probe_resp_sta_list.hash,
        MTLK_CORE_STA_LIST_HASH_NOF_BUCKETS));

    MTLK_INIT_STEP(core, UC_PROBE_REQ_STA_LIST_LOCK_INIT, MTLK_OBJ_PTR(nic),
      mtlk_osal_lock_init, (&nic->unicast_probe_resp_sta_list.ieee_addr_lock));

    MTLK_INIT_STEP_VOID(core, NDP_WDS_WPA_STA_LIST_INIT, MTLK_OBJ_PTR(nic),
      mtlk_hash_init_ieee_addr, (&nic->wds_wpa_sta_list.hash,
        MTLK_CORE_4ADDR_STA_LIST_HASH_NOF_BUCKETS));

    MTLK_INIT_STEP(core, NDP_WDS_WPA_STA_LIST_LOCK_INIT, MTLK_OBJ_PTR(nic),
      mtlk_osal_lock_init, (&nic->wds_wpa_sta_list.ieee_addr_lock));

    MTLK_INIT_STEP(core, NDP_ACKED, MTLK_OBJ_PTR(nic),
      mtlk_osal_event_init, (&nic->ndp_acked));

    nic->is_stopped = TRUE;
    ILOG1_SDDDS("%s: Inited: is_stopped=%u, is_stopping=%u, is_iface_stopping=%u, net_state=%s",
                mtlk_df_get_name(mtlk_vap_get_df(nic->vap_handle)),
                nic->is_stopped, nic->is_stopping, nic->is_iface_stopping,
                mtlk_net_state_to_string(mtlk_core_get_net_state(nic)));

  MTLK_INIT_FINALLY(core, MTLK_OBJ_PTR(nic))
  MTLK_INIT_RETURN(core, MTLK_OBJ_PTR(nic), _mtlk_core_cleanup, (nic))
}

int __MTLK_IFUNC
mtlk_core_api_init (mtlk_vap_handle_t vap_handle, mtlk_core_api_t *core_api, mtlk_df_t* df)
{
  int res;
  mtlk_core_t *core;

  MTLK_ASSERT(NULL != core_api);

  /* initialize function table */
  core_api->vft = &core_vft;

  core = mtlk_fast_mem_alloc(MTLK_FM_USER_CORE, sizeof(mtlk_core_t));
  if(NULL == core) {
    return MTLK_ERR_NO_MEM;
  }

  memset(core, 0, sizeof(mtlk_core_t));

  res = _mtlk_core_init(core, vap_handle, df);
  if (MTLK_ERR_OK != res) {
    mtlk_fast_mem_free(core);
    return res;
  }

  core_api->core_handle = HANDLE_T(core);

  return MTLK_ERR_OK;
}

static int
mtlk_core_master_set_default_band(struct nic *nic)
{
  uint8 freq_band_cfg = MTLK_HW_BAND_NONE;
  wave_radio_t *radio = wave_vap_radio_get(nic->vap_handle);

  MTLK_ASSERT(!mtlk_vap_is_slave_ap(nic->vap_handle));

  if (mtlk_core_is_band_supported(nic, MTLK_HW_BAND_BOTH) == MTLK_ERR_OK) {
    freq_band_cfg = MTLK_HW_BAND_BOTH;
  } else if (mtlk_core_is_band_supported(nic, MTLK_HW_BAND_5_2_GHZ) == MTLK_ERR_OK) {
    freq_band_cfg = MTLK_HW_BAND_5_2_GHZ;
  } else if (mtlk_core_is_band_supported(nic, MTLK_HW_BAND_2_4_GHZ) == MTLK_ERR_OK) {
    freq_band_cfg = MTLK_HW_BAND_2_4_GHZ;
  } else {
    ELOG_D("CID-%04x: None of the bands is supported", mtlk_vap_get_oid(nic->vap_handle));
    return MTLK_ERR_UNKNOWN;
  }

  WAVE_RADIO_PDB_SET_INT(radio, PARAM_DB_RADIO_FREQ_BAND_CFG, freq_band_cfg);
  WAVE_RADIO_PDB_SET_INT(radio, PARAM_DB_RADIO_FREQ_BAND_CUR, freq_band_cfg);

  return MTLK_ERR_OK;
}

static int
mtlk_core_set_default_net_mode(struct nic *nic)
{
  uint8 freq_band_cfg = MTLK_HW_BAND_NONE;
  uint8 net_mode = MTLK_NETWORK_NONE;
  mtlk_core_t *core_master = NULL;
  uint32 bss_rate = CFG_BASIC_RATE_SET_DEFAULT;

  MTLK_ASSERT(nic != NULL);

  if(mtlk_vap_is_master(nic->vap_handle))
  {
    /* Set default mode based on band
     * for Master AP or STA */
    freq_band_cfg = core_cfg_get_freq_band_cfg(nic);
    net_mode = get_net_mode(freq_band_cfg, TRUE);
  }
  else
  {
    /* Copy default mode from Master VAP.
     * This is important while scripts
     * are not ready for network mode per VAP. */
    core_master = mtlk_core_get_master(nic);
    MTLK_ASSERT(core_master != NULL);
    net_mode = core_cfg_get_network_mode_cfg(core_master);

    /* Copy basic rate set too */
    bss_rate = MTLK_CORE_PDB_GET_INT(core_master, PARAM_DB_CORE_BASIC_RATE_SET);
    MTLK_CORE_PDB_SET_INT(nic, PARAM_DB_CORE_BASIC_RATE_SET, bss_rate);
  }

  MTLK_CORE_PDB_SET_INT(nic, PARAM_DB_CORE_NET_MODE_CUR, net_mode);
  MTLK_CORE_PDB_SET_INT(nic, PARAM_DB_CORE_NET_MODE_CFG, net_mode);

  MTLK_CORE_PDB_SET_INT(nic, PARAM_DB_CORE_IS_HT_CUR, is_ht_net_mode(net_mode));
  MTLK_CORE_PDB_SET_INT(nic, PARAM_DB_CORE_IS_HT_CFG, is_ht_net_mode(net_mode));

  return MTLK_ERR_OK;
}

MTLK_START_STEPS_LIST_BEGIN(core_slow_ctx)
  MTLK_START_STEPS_LIST_ENTRY(core_slow_ctx, SERIALIZER_START)
  MTLK_START_STEPS_LIST_ENTRY(core_slow_ctx, SET_MAC_MAC_ADDR)
  MTLK_START_STEPS_LIST_ENTRY(core_slow_ctx, PARSE_EE_DATA)
  MTLK_START_STEPS_LIST_ENTRY(core_slow_ctx, CACHE_INIT)
  MTLK_START_STEPS_LIST_ENTRY(core_slow_ctx, PROCESS_ANTENNA_CFG)
#ifdef CPTCFG_IWLWAV_PMCU_SUPPORT
  MTLK_START_STEPS_LIST_ENTRY(core_slow_ctx, PROCESS_PCOC)
#endif /*CPTCFG_IWLWAV_PMCU_SUPPORT*/
  MTLK_START_STEPS_LIST_ENTRY(core_slow_ctx, WDS_INIT)
  MTLK_START_STEPS_LIST_ENTRY(core_slow_ctx, SET_DEFAULT_BAND)
  MTLK_START_STEPS_LIST_ENTRY(core_slow_ctx, SET_DEFAULT_NET_MODE)
  MTLK_START_STEPS_LIST_ENTRY(core_slow_ctx, WATCHDOG_TIMER_START)
  MTLK_START_STEPS_LIST_ENTRY(core_slow_ctx, SERIALIZER_ACTIVATE)
#ifdef MTLK_LEGACY_STATISTICS
  MTLK_START_STEPS_LIST_ENTRY(core_slow_ctx, CORE_STAT_REQ_HANDLER)
#endif
MTLK_START_INNER_STEPS_BEGIN(core_slow_ctx)
MTLK_START_STEPS_LIST_END(core_slow_ctx);

#ifdef MTLK_LEGACY_STATISTICS
static void __MTLK_IFUNC
_mtlk_core_stat_handle_request(mtlk_irbd_t       *irbd,
                               mtlk_handle_t      context,
                               const mtlk_guid_t *evt,
                               void              *buffer,
                               uint32            *size)
{
  struct nic_slow_ctx  *slow_ctx = HANDLE_T_PTR(struct nic_slow_ctx, context);
  mtlk_wssa_info_hdr_t *hdr = (mtlk_wssa_info_hdr_t *) buffer;

  MTLK_UNREFERENCED_PARAM(evt);

  if(sizeof(mtlk_wssa_info_hdr_t) > *size)
    return;

  if(MTIDL_SRC_DRV == hdr->info_source)
  {
    switch(hdr->info_id)
    {
    case MTLK_WSSA_DRV_TR181_WLAN:
      {
        if(sizeof(mtlk_wssa_drv_tr181_wlan_stats_t) + sizeof(mtlk_wssa_info_hdr_t) > *size)
        {
          hdr->processing_result = MTLK_ERR_BUF_TOO_SMALL;
        }
        else
        {
          _mtlk_core_get_tr181_wlan_stats(slow_ctx->nic, (mtlk_wssa_drv_tr181_wlan_stats_t*) &hdr[1]);
          hdr->processing_result = MTLK_ERR_OK;
          *size = sizeof(mtlk_wssa_drv_tr181_wlan_stats_t) + sizeof(mtlk_wssa_info_hdr_t);
        }
      }
      break;
    case MTLK_WSSA_DRV_STATUS_WLAN:
      {
        if(sizeof(mtlk_wssa_drv_wlan_stats_t) + sizeof(mtlk_wssa_info_hdr_t) > *size)
        {
          hdr->processing_result = MTLK_ERR_BUF_TOO_SMALL;
        }
        else
        {
          _mtlk_core_get_wlan_stats(slow_ctx->nic, (mtlk_wssa_drv_wlan_stats_t*) &hdr[1]);
          hdr->processing_result = MTLK_ERR_OK;
          *size = sizeof(mtlk_wssa_drv_wlan_stats_t) + sizeof(mtlk_wssa_info_hdr_t);
        }
      }
      break;
    case MTLK_WSSA_DRV_TR181_HW:
      {
        if(sizeof(mtlk_wssa_drv_tr181_hw_t) + sizeof(mtlk_wssa_info_hdr_t) > *size)
        {
          hdr->processing_result = MTLK_ERR_BUF_TOO_SMALL;
        }
        else
        {
          _mtlk_core_get_tr181_hw(slow_ctx->nic, (mtlk_wssa_drv_tr181_hw_t*) &hdr[1]);
          hdr->processing_result = MTLK_ERR_OK;
          *size = sizeof(mtlk_wssa_drv_tr181_hw_t) + sizeof(mtlk_wssa_info_hdr_t);
        }
      }
      break;
#if MTLK_MTIDL_WLAN_STAT_FULL
    case MTLK_WSSA_DRV_DEBUG_STATUS_WLAN:
      {
        if(sizeof(mtlk_wssa_drv_debug_wlan_stats_t) + sizeof(mtlk_wssa_info_hdr_t) > *size)
        {
          hdr->processing_result = MTLK_ERR_BUF_TOO_SMALL;
        }
        else
        {
          _mtlk_core_get_debug_wlan_stats(slow_ctx->nic, (mtlk_wssa_drv_debug_wlan_stats_t*) &hdr[1]);
          hdr->processing_result = MTLK_ERR_OK;
          *size = sizeof(mtlk_wssa_drv_debug_wlan_stats_t) + sizeof(mtlk_wssa_info_hdr_t);
        }
      }
      break;
#endif /* MTLK_MTIDL_WLAN_STAT_FULL */
    default:
      {
        /* Try to handle HW/Radio request */
        wave_radio_t *radio = wave_vap_radio_get(slow_ctx->nic->vap_handle);
        return wave_radio_stat_handle_request(irbd, HANDLE_T(radio), evt, buffer, size);
      }
    }
  }
  else
  {
    hdr->processing_result = MTLK_ERR_NO_ENTRY;
    *size = sizeof(mtlk_wssa_info_hdr_t);
  }
}
#endif /* MTLK_LEGACY_STATISTICS */

#ifdef CPTCFG_IWLWAV_PMCU_SUPPORT
static const mtlk_ability_id_t _core_pmcu_abilities[] = {
  WAVE_RADIO_REQ_GET_PCOC_CFG,
  WAVE_RADIO_REQ_SET_PCOC_CFG
};

void __MTLK_IFUNC mtlk_core_pmcu_register_vap(mtlk_vap_handle_t vap_handle)
{
  mtlk_abmgr_register_ability_set(mtlk_vap_get_abmgr(vap_handle),
                    _core_pmcu_abilities, ARRAY_SIZE(_core_pmcu_abilities));
  mtlk_abmgr_enable_ability_set(mtlk_vap_get_abmgr(vap_handle),
                    _core_pmcu_abilities, ARRAY_SIZE(_core_pmcu_abilities));
}

void __MTLK_IFUNC mtlk_core_pmcu_unregister_vap(mtlk_vap_handle_t vap_handle)
{
  mtlk_abmgr_disable_ability_set(mtlk_vap_get_abmgr(vap_handle),
                    _core_pmcu_abilities, ARRAY_SIZE(_core_pmcu_abilities));
  mtlk_abmgr_unregister_ability_set(mtlk_vap_get_abmgr(vap_handle),
                    _core_pmcu_abilities, ARRAY_SIZE(_core_pmcu_abilities));
}
#endif /*CPTCFG_IWLWAV_PMCU_SUPPORT*/

static void __MTLK_IFUNC
_mtlk_slow_ctx_stop(struct nic_slow_ctx *slow_ctx, struct nic* nic)
{
  MTLK_ASSERT(NULL != slow_ctx);
  MTLK_ASSERT(NULL != nic);

  MTLK_STOP_BEGIN(core_slow_ctx, MTLK_OBJ_PTR(slow_ctx))
#ifdef MTLK_LEGACY_STATISTICS
    MTLK_STOP_STEP(core_slow_ctx, CORE_STAT_REQ_HANDLER, MTLK_OBJ_PTR(slow_ctx),
                   mtlk_wssd_unregister_request_handler, (mtlk_vap_get_irbd(nic->vap_handle), slow_ctx->stat_irb_handle));
#endif

    MTLK_STOP_STEP(core_slow_ctx, SERIALIZER_ACTIVATE, MTLK_OBJ_PTR(slow_ctx),
                   mtlk_serializer_stop, (&slow_ctx->serializer))

    MTLK_STOP_STEP(core_slow_ctx, WATCHDOG_TIMER_START, MTLK_OBJ_PTR(slow_ctx),
                   mtlk_osal_timer_cancel_sync, (&slow_ctx->mac_watchdog_timer))

    MTLK_STOP_STEP(core_slow_ctx, SET_DEFAULT_NET_MODE, MTLK_OBJ_PTR(slow_ctx),
                   MTLK_NOACTION, ())

    MTLK_STOP_STEP(core_slow_ctx, SET_DEFAULT_BAND, MTLK_OBJ_PTR(slow_ctx),
                   MTLK_NOACTION, ())

    MTLK_STOP_STEP(core_slow_ctx, WDS_INIT, MTLK_OBJ_PTR(slow_ctx),
                   wds_cleanup, (&slow_ctx->wds_mng))
#ifdef CPTCFG_IWLWAV_PMCU_SUPPORT
    MTLK_STOP_STEP(core_slow_ctx, PROCESS_PCOC, MTLK_OBJ_PTR(slow_ctx),
                               mtlk_core_pmcu_unregister_vap, (nic->vap_handle))
#endif /*CPTCFG_IWLWAV_PMCU_SUPPORT*/


    MTLK_STOP_STEP(core_slow_ctx, PROCESS_ANTENNA_CFG, MTLK_OBJ_PTR(slow_ctx),
                   MTLK_NOACTION, ())

    MTLK_STOP_STEP(core_slow_ctx, CACHE_INIT, MTLK_OBJ_PTR(slow_ctx),
                   mtlk_cache_cleanup, (&slow_ctx->cache))

    MTLK_STOP_STEP(core_slow_ctx, PARSE_EE_DATA, MTLK_OBJ_PTR(slow_ctx),
                   MTLK_NOACTION, ())

    MTLK_STOP_STEP(core_slow_ctx, SET_MAC_MAC_ADDR, MTLK_OBJ_PTR(slow_ctx),
                   MTLK_NOACTION, ())

    MTLK_STOP_STEP(core_slow_ctx, SERIALIZER_START, MTLK_OBJ_PTR(slow_ctx),
                   MTLK_NOACTION, ())
  MTLK_STOP_END(core_slow_ctx, MTLK_OBJ_PTR(slow_ctx))
}

static int __MTLK_IFUNC
_mtlk_slow_ctx_start(struct nic_slow_ctx *slow_ctx, struct nic* nic)
{
  int cache_param;
  struct mtlk_scan_config scan_cfg;
  mtlk_eeprom_data_t *eeprom_data;
  mtlk_txmm_t *txmm;
  BOOL is_dut;
  BOOL is_master;
  int res = MTLK_ERR_OK;

  MTLK_ASSERT(NULL != slow_ctx);
  MTLK_ASSERT(NULL != nic);

  MTLK_UNREFERENCED_PARAM(res);

  txmm = mtlk_vap_get_txmm(nic->vap_handle);
  is_dut = mtlk_vap_is_dut(nic->vap_handle);
  is_master = mtlk_vap_is_master (nic->vap_handle);

  MTLK_START_TRY(core_slow_ctx, MTLK_OBJ_PTR(slow_ctx))
    MTLK_START_STEP(core_slow_ctx, SERIALIZER_START, MTLK_OBJ_PTR(slow_ctx),
                    mtlk_serializer_start, (&slow_ctx->serializer))

    eeprom_data = mtlk_core_get_eeprom(nic);

    MTLK_START_STEP_IF(!is_dut && is_master, core_slow_ctx, SET_MAC_MAC_ADDR, MTLK_OBJ_PTR(slow_ctx),
                       core_cfg_set_mac_addr, (nic, (char *)mtlk_eeprom_get_nic_mac_addr(eeprom_data)))

    MTLK_START_STEP_IF(!is_dut && is_master, core_slow_ctx, PARSE_EE_DATA, MTLK_OBJ_PTR(slow_ctx),
                       mtlk_eeprom_check_ee_data, (eeprom_data, txmm, mtlk_vap_is_ap(nic->vap_handle)))

    core_cfg_country_code_set_default(nic);

    if (mtlk_vap_is_ap(nic->vap_handle)) {
      cache_param = 0;
    } else {
      cache_param = SCAN_CACHE_AGEING;
    }

    MTLK_START_STEP(core_slow_ctx, CACHE_INIT, MTLK_OBJ_PTR(slow_ctx),
                    mtlk_cache_init, (&slow_ctx->cache, cache_param))

    MTLK_START_STEP_VOID(core_slow_ctx, PROCESS_ANTENNA_CFG, MTLK_OBJ_PTR(slow_ctx),
                         MTLK_NOACTION, ())

    SLOG0(SLOG_DFLT_ORIGINATOR, SLOG_DFLT_RECEIVER, tx_limit_t, &nic->slow_ctx->tx_limits);

#ifdef CPTCFG_IWLWAV_PMCU_SUPPORT

    MTLK_START_STEP_VOID_IF((is_master), core_slow_ctx, PROCESS_PCOC,
        MTLK_OBJ_PTR(slow_ctx), mtlk_core_pmcu_register_vap, (nic->vap_handle));
#endif /*CPTCFG_IWLWAV_PMCU_SUPPORT*/


    scan_cfg.txmm = txmm;
    scan_cfg.bss_cache = &slow_ctx->cache;
    scan_cfg.bss_data  = &slow_ctx->bss_data;

    MTLK_START_STEP_IF(mtlk_vap_is_ap(nic->vap_handle), core_slow_ctx, WDS_INIT, MTLK_OBJ_PTR(slow_ctx),
                       wds_init, (&slow_ctx->wds_mng, nic->vap_handle))

    MTLK_START_STEP_IF(!is_dut && is_master, core_slow_ctx, SET_DEFAULT_BAND, MTLK_OBJ_PTR(slow_ctx),
                       mtlk_core_master_set_default_band, (nic))

    MTLK_START_STEP(core_slow_ctx, SET_DEFAULT_NET_MODE, MTLK_OBJ_PTR(slow_ctx),
                    mtlk_core_set_default_net_mode, (nic))

    MTLK_START_STEP_IF(is_master, core_slow_ctx, WATCHDOG_TIMER_START, MTLK_OBJ_PTR(slow_ctx),
                       mtlk_osal_timer_set,
                       (&slow_ctx->mac_watchdog_timer,
                       WAVE_RADIO_PDB_GET_INT(wave_vap_radio_get(nic->vap_handle), PARAM_DB_RADIO_MAC_WATCHDOG_TIMER_PERIOD_MS)))

    MTLK_START_STEP_VOID(core_slow_ctx, SERIALIZER_ACTIVATE, MTLK_OBJ_PTR(slow_ctx),
                         MTLK_NOACTION, ())

#ifdef MTLK_LEGACY_STATISTICS
    MTLK_START_STEP_EX(core_slow_ctx, CORE_STAT_REQ_HANDLER, MTLK_OBJ_PTR(slow_ctx),
                       mtlk_wssd_register_request_handler, (mtlk_vap_get_irbd(nic->vap_handle),
                                                            _mtlk_core_stat_handle_request, HANDLE_T(slow_ctx)),
                       slow_ctx->stat_irb_handle, slow_ctx->stat_irb_handle != NULL, MTLK_ERR_UNKNOWN);
#endif

  MTLK_START_FINALLY(core_slow_ctx, MTLK_OBJ_PTR(slow_ctx))
  MTLK_START_RETURN(core_slow_ctx, MTLK_OBJ_PTR(slow_ctx), _mtlk_slow_ctx_stop, (slow_ctx, nic))
}

MTLK_START_STEPS_LIST_BEGIN(core)
  MTLK_START_STEPS_LIST_ENTRY(core, SET_NET_STATE_IDLE)
  MTLK_START_STEPS_LIST_ENTRY(core, SLOW_CTX_START)
  MTLK_START_STEPS_LIST_ENTRY(core, DF_USER_SET_MAC_ADDR)
  MTLK_START_STEPS_LIST_ENTRY(core, RESET_STATS)
  MTLK_START_STEPS_LIST_ENTRY(core, MC_INIT)
  MTLK_START_STEPS_LIST_ENTRY(core, DUT_REGISTER)
  MTLK_START_STEPS_LIST_ENTRY(core, VAP_DB)
  MTLK_START_STEPS_LIST_ENTRY(core, CORE_ABILITIES_INIT)
  MTLK_START_STEPS_LIST_ENTRY(core, RADIO_ABILITIES_INIT)
  MTLK_START_STEPS_LIST_ENTRY(core, SET_NET_STATE_READY)
  MTLK_START_STEPS_LIST_ENTRY(core, INIT_DEFAULTS)
  MTLK_START_STEPS_LIST_ENTRY(core, SET_VAP_MIBS)
MTLK_START_INNER_STEPS_BEGIN(core)
MTLK_START_STEPS_LIST_END(core);

static void
_mtlk_core_stop (mtlk_vap_handle_t vap_handle)
{
  mtlk_core_t *nic = mtlk_vap_get_core (vap_handle);
  int i;

  ILOG0_D("CID-%04x: stop", mtlk_vap_get_oid(vap_handle));

  /*send RMMOD event to application*/
  if (!mtlk_core_is_hw_halted(nic) && mtlk_vap_is_ap(vap_handle)) {
    ILOG4_V("RMMOD send event");
    mtlk_df_ui_notify_notify_rmmod(mtlk_df_get_name(mtlk_vap_get_df(vap_handle)));
  }

  MTLK_STOP_BEGIN(core, MTLK_OBJ_PTR(nic))
    MTLK_STOP_STEP(core, SET_VAP_MIBS, MTLK_OBJ_PTR(nic),
                   MTLK_NOACTION, ())

    MTLK_STOP_STEP(core, INIT_DEFAULTS, MTLK_OBJ_PTR(nic),
                   MTLK_NOACTION, ())

    MTLK_STOP_STEP(core, SET_NET_STATE_READY, MTLK_OBJ_PTR(nic),
                   MTLK_NOACTION, ())

    MTLK_STOP_STEP(core, RADIO_ABILITIES_INIT, MTLK_OBJ_PTR(nic),
                   wave_radio_abilities_unregister, (nic))

    MTLK_STOP_STEP(core, CORE_ABILITIES_INIT, MTLK_OBJ_PTR(nic),
                   wave_core_abilities_unregister, (nic))

    MTLK_STOP_STEP(core, VAP_DB, MTLK_OBJ_PTR(nic),
                   mtlk_mbss_send_vap_db_del, (nic))

    MTLK_STOP_STEP(core, DUT_REGISTER, MTLK_OBJ_PTR(nic),
                   mtlk_dut_core_unregister, (nic));

    MTLK_STOP_STEP(core, MC_INIT, MTLK_OBJ_PTR(nic),
                   mtlk_mc_uninit, (nic))

    MTLK_STOP_STEP(core, RESET_STATS, MTLK_OBJ_PTR(nic),
                   MTLK_NOACTION, ())

    MTLK_STOP_STEP(core, DF_USER_SET_MAC_ADDR, MTLK_OBJ_PTR(nic),
                   MTLK_NOACTION, ())

    MTLK_STOP_STEP(core, SLOW_CTX_START, MTLK_OBJ_PTR(nic),
                   _mtlk_slow_ctx_stop, (nic->slow_ctx, nic))

    MTLK_STOP_STEP(core, SET_NET_STATE_IDLE, MTLK_OBJ_PTR(nic),
                   MTLK_NOACTION, ())

  MTLK_STOP_END(core, MTLK_OBJ_PTR(nic))

  for (i = 0; i < ARRAY_SIZE(nic->txmm_async_eeprom_msgs); i++) {
    mtlk_txmm_msg_cancel(&nic->txmm_async_eeprom_msgs[i]);
  }

  ILOG1_SDDDS("%s: Stopped: is_stopped=%u, is_stopping=%u, is_iface_stopping=%u, net_state=%s",
              mtlk_df_get_name(mtlk_vap_get_df(nic->vap_handle)),
              nic->is_stopped, nic->is_stopping, nic->is_iface_stopping,
              mtlk_net_state_to_string(mtlk_core_get_net_state(nic)));
}

static int
_mtlk_core_start (mtlk_vap_handle_t vap_handle)
{
  mtlk_core_t       *nic = mtlk_vap_get_core (vap_handle);
  uint8              mac_addr[ETH_ALEN];

  MTLK_START_TRY(core, MTLK_OBJ_PTR(nic))
    MTLK_START_STEP(core, SET_NET_STATE_IDLE, MTLK_OBJ_PTR(nic),
                    mtlk_core_set_net_state, (nic, NET_STATE_IDLE))

    _mtlk_core_poll_stat_init(nic);

    MTLK_START_STEP(core, SLOW_CTX_START, MTLK_OBJ_PTR(nic),
                    _mtlk_slow_ctx_start, (nic->slow_ctx, nic))

    wave_pdb_get_mac(
        mtlk_vap_get_param_db(vap_handle), PARAM_DB_CORE_MAC_ADDR, &mac_addr);

    /* Setting mac address in net_device is done by mac80211 framework for station role interfaces */
    MTLK_START_STEP_VOID_IF((mtlk_vap_is_ap(vap_handle)), core, DF_USER_SET_MAC_ADDR, MTLK_OBJ_PTR(nic),
                           mtlk_df_ui_set_mac_addr, (mtlk_vap_get_df(vap_handle), mac_addr))

    MTLK_START_STEP_VOID(core, RESET_STATS, MTLK_OBJ_PTR(nic),
                         _mtlk_core_reset_stats_internal, (nic))

    MTLK_START_STEP_IF((mtlk_vap_is_ap(vap_handle)), core, MC_INIT, MTLK_OBJ_PTR(nic),
                       mtlk_mc_init, (nic))

    MTLK_START_STEP(core, DUT_REGISTER, MTLK_OBJ_PTR(nic),
                    mtlk_dut_core_register, (nic));

    MTLK_START_STEP_IF(mtlk_vap_is_ap(vap_handle),
                       core, VAP_DB, MTLK_OBJ_PTR(nic),
                       mtlk_mbss_send_vap_db_add, (nic))

    MTLK_START_STEP(core, CORE_ABILITIES_INIT, MTLK_OBJ_PTR(nic),
                    wave_core_abilities_register, (nic))

    MTLK_START_STEP_IF(mtlk_vap_is_master_ap(vap_handle),
                    core, RADIO_ABILITIES_INIT, MTLK_OBJ_PTR(nic),
                    wave_radio_abilities_register, (nic))

    MTLK_START_STEP(core, SET_NET_STATE_READY, MTLK_OBJ_PTR(nic),
                    mtlk_core_set_net_state, (nic, NET_STATE_READY))

    MTLK_START_STEP(core, INIT_DEFAULTS, MTLK_OBJ_PTR(nic),
                    mtlk_core_init_defaults, (nic))

    mtlk_qos_dscp_table_init(nic->dscp_table);
    if (mtlk_vap_is_ap(vap_handle))
      nic->dgaf_disabled = FALSE;
    else
      nic->dgaf_disabled = TRUE;
    nic->osen_enabled = FALSE;

    MTLK_START_STEP_IF(mtlk_vap_is_ap(vap_handle),
                       core, SET_VAP_MIBS, MTLK_OBJ_PTR(nic),
                       mtlk_set_vap_mibs, (nic))

  MTLK_START_FINALLY(core, MTLK_OBJ_PTR(nic))
  MTLK_START_RETURN(core, MTLK_OBJ_PTR(nic), _mtlk_core_stop, (vap_handle))
}

static int
_mtlk_core_release_tx_data (mtlk_vap_handle_t vap_handle, mtlk_hw_data_req_mirror_t *data_req)
{
  int res = MTLK_ERR_UNKNOWN;
  mtlk_core_t *nic = mtlk_vap_get_core (vap_handle);
  mtlk_nbuf_t *nbuf = data_req->nbuf;
  unsigned short qos = 0;
#if defined(CPTCFG_IWLWAV_PER_PACKET_STATS)
  mtlk_nbuf_priv_t *nbuf_priv = mtlk_nbuf_priv(nbuf);
#endif

#if defined(CPTCFG_IWLWAV_PER_PACKET_STATS)
  mtlk_nbuf_priv_stats_set(nbuf_priv, MTLK_NBUF_STATS_TS_FW_OUT, mtlk_hw_get_timestamp(vap_handle));
#endif

  // check if NULL packet confirmed
  if (data_req->size == 0) {
    ILOG9_V("Confirmation for NULL nbuf");
    goto FINISH;
  }

  qos = data_req->tid;

  if (nic->pstats.ac_used_counter[qos] > 0)
    --nic->pstats.ac_used_counter[qos];

  mtlk_osal_atomic_dec(&nic->unconfirmed_data);

  res = MTLK_ERR_OK;

FINISH:

#if defined(CPTCFG_IWLWAV_PRINT_PER_PACKET_STATS)
  mtlk_nbuf_priv_stats_dump(nbuf_priv);
#endif

  /* Release net buffer
   * WARNING: we can't do it before since we use STA referenced by this packet on FINISH.
   */
  mtlk_df_nbuf_free(nbuf);

  return res;
}

static int
_mtlk_core_handle_rx_data (mtlk_vap_handle_t vap_handle, mtlk_core_handle_rx_data_t *data)
{
  mtlk_nbuf_t *nbuf = data->nbuf;
  MTLK_ASSERT(nbuf != NULL);
  return _handle_rx_ind(mtlk_vap_get_core(vap_handle), nbuf, data);
}

static int
_mtlk_core_handle_rx_bss (mtlk_vap_handle_t vap_handle, mtlk_core_handle_rx_bss_t *data)
{
  MTLK_ASSERT(data != NULL);
  return handle_rx_bss_ind(mtlk_vap_get_core(vap_handle), data);
}

static int __MTLK_IFUNC
_handle_fw_debug_trace_event(mtlk_handle_t object, const void *data, uint32 data_size)
{
  UmiDbgTraceInd_t *UmiDbgTraceInd = (UmiDbgTraceInd_t *) data;
  MTLK_UNREFERENCED_PARAM(object);
  MTLK_ASSERT(sizeof(UmiDbgTraceInd_t) >= data_size);
  MTLK_ASSERT(MAX_DBG_TRACE_DATA >= MAC_TO_HOST32(UmiDbgTraceInd->length));

  UmiDbgTraceInd->au8Data[MAX_DBG_TRACE_DATA - 1] = 0; // make sure it is NULL-terminated (although it should be without this)
  ILOG0_SDDD("DBG TRACE: %s, val1:0x%08X val2:0x%08X val3:0x%08X",
             UmiDbgTraceInd->au8Data,
             MAC_TO_HOST32(UmiDbgTraceInd->val1),
             MAC_TO_HOST32(UmiDbgTraceInd->val2),
             MAC_TO_HOST32(UmiDbgTraceInd->val3));

  return MTLK_ERR_OK;
}

static int
_wave_core_rcvry_reset (mtlk_handle_t hcore, const void* data, uint32 data_size){
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **)data;
  wave_rcvry_task_ctx_t ** rcvry_handle = NULL;
  int res = MTLK_ERR_OK;
  uint32 size = sizeof(wave_rcvry_task_ctx_t *);

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);
  ILOG1_V("resetting recovery config");

  rcvry_handle = mtlk_clpb_enum_get_next(clpb, &size);
  MTLK_CLPB_TRY(rcvry_handle, size)
    wave_rcvry_reset(rcvry_handle);
  MTLK_CLPB_FINALLY(res)
    return mtlk_clpb_push_res(clpb, res);
  MTLK_CLPB_END;
}

static int __MTLK_IFUNC
wave_dbg_static_plan_mu_group_stats_ind_handle(mtlk_handle_t object, const void *data, uint32 data_size)
{
  int i;
  int group_id;

  mtlk_core_t *master_core;
  wave_radio_t *radio;


  UMI_DBG_HE_MU_GROUP_STATS *UmiDbgMuGroup = (UMI_DBG_HE_MU_GROUP_STATS *) data;
  UMI_DBG_HE_MU_GROUP_STATS HeMuGroups[HE_MU_MAX_NUM_OF_GROUPS];
  mtlk_pdb_size_t UmiDbgMuGroupStatsSize = sizeof(HeMuGroups);

  MTLK_UNREFERENCED_PARAM(object);
  MTLK_ASSERT(sizeof(*UmiDbgMuGroup) == data_size);

  group_id = UmiDbgMuGroup->groupId;

  if(group_id >= HE_MU_MAX_NUM_OF_GROUPS) {
    ELOG_D("Wrong HE MU Group ID (%d) !", group_id);
    return MTLK_ERR_PARAMS;
  }

  master_core = HANDLE_T_PTR(mtlk_core_t, object);
  radio = wave_vap_radio_get(master_core->vap_handle);

  mtlk_dump(1, UmiDbgMuGroup, sizeof(*UmiDbgMuGroup), "dump of the UMI_MAN_HE_MU_DBG_IND");

  /* Read current MU group stats */
  if (MTLK_ERR_OK != WAVE_RADIO_PDB_GET_BINARY(radio, PARAM_DB_RADIO_PLAN_MU_GROUP_STATS, &HeMuGroups[0], &UmiDbgMuGroupStatsSize)) {
    ELOG_D("CID-%04x: Can't read PLAN_MU_GROUP_STATS from PDB", mtlk_vap_get_oid(master_core->vap_handle));
    return MTLK_ERR_PARAMS;
  }

  /* Update the stats for particular group */
  HeMuGroups[group_id].groupId  = group_id;
  HeMuGroups[group_id].planType = UmiDbgMuGroup->planType;
  HeMuGroups[group_id].setReset = UmiDbgMuGroup->setReset;
  for(i = 0; i < HE_MU_MAX_NUM_OF_USERS_PER_GROUP; i++) {
    HeMuGroups[group_id].stationId[i] = MAC_TO_HOST16(UmiDbgMuGroup->stationId[i]);
  }

  if (MTLK_ERR_OK != WAVE_RADIO_PDB_SET_BINARY(radio, PARAM_DB_RADIO_PLAN_MU_GROUP_STATS, &HeMuGroups[0], UmiDbgMuGroupStatsSize)) {
    ELOG_D("CID-%04x: Can't set PLAN_MU_GROUP_STATS to PDB", mtlk_vap_get_oid(master_core->vap_handle));
    return MTLK_ERR_PARAMS;
  }

  return MTLK_ERR_OK;
}

static int __MTLK_IFUNC
_handle_core_class3_error(mtlk_handle_t core_object, const void *data, uint32 data_size)
{
mtlk_core_t                 *master_core = HANDLE_T_PTR(mtlk_core_t, core_object);
mtlk_core_t                 *core;
UMI_FRAME_CLASS_ERROR       *frame_error = (UMI_FRAME_CLASS_ERROR *)data;
UMI_FRAME_CLASS_ERROR_ENTRY *class3_error = NULL;
mtlk_vap_manager_t          *vap_mng;
mtlk_vap_handle_t            vap_handle;
uint32                       i;

  MTLK_ASSERT(sizeof(UMI_FRAME_CLASS_ERROR) == data_size);
  MTLK_ASSERT(frame_error->u8numOfValidEntries <= MAX_NUMBER_FRAME_CLASS_ERROR_ENTRIES_IN_MESSAGE);
  MTLK_ASSERT(master_core != NULL);

  vap_mng = mtlk_vap_get_manager(master_core->vap_handle);

  for(i = 0; i < frame_error->u8numOfValidEntries; i++) {

    class3_error = &frame_error->frameClassErrorEntries[i];
    MTLK_ASSERT(class3_error != NULL);

    if (MTLK_ERR_OK != mtlk_vap_manager_get_vap_handle(vap_mng, class3_error->u8vapIndex, &vap_handle)){
      ILOG2_D("VapID %u doesn't exist. Ignore class3 error", class3_error->u8vapIndex);
      continue;   /* VAP not exist */
    }

    core = mtlk_vap_get_core(vap_handle);
    MTLK_ASSERT(core != NULL);

    if (mtlk_core_get_net_state(core) != NET_STATE_CONNECTED){
      ILOG2_D("VapID %u not active. Ignore class3 error", class3_error->u8vapIndex);
      continue;
    }

    /* if WDS is ON, find peer AP in DB and filter out Class 3 errors */
    if (mtlk_vap_is_ap(vap_handle) &&
       (BR_MODE_WDS == MTLK_CORE_HOT_PATH_PDB_GET_INT(core, CORE_DB_CORE_BRIDGE_MODE)) &&
      (core_wds_frame_drop(core, &class3_error->sAddr))) {
      ILOG2_Y("Ignore class3 error for WDS peers:%Y", class3_error->sAddr.au8Addr);
      continue;
    }

    ILOG2_DDY("CID-%04x: Class3 Error VapID:%u MAC:%Y",
              mtlk_vap_get_oid(core->vap_handle), class3_error->u8vapIndex, class3_error->sAddr.au8Addr);

    if (mtlk_vap_is_ap(vap_handle))
      wv_cfg80211_class3_error_notify(mtlk_df_user_get_wdev(mtlk_df_get_user(mtlk_vap_get_df(core->vap_handle))),
                                     class3_error->sAddr.au8Addr);
  }

  return MTLK_ERR_OK;
}

static int __MTLK_IFUNC
_handle_class3_error_ind_handle (mtlk_handle_t core_object, const void *data, uint32 data_size)
{
  mtlk_core_t *core = HANDLE_T_PTR(mtlk_core_t, core_object);

  ILOG5_V("Handling class 3 error");

  _mtlk_process_hw_task(core, SERIALIZABLE, _handle_core_class3_error,
                         HANDLE_T(core), data, sizeof(UMI_FRAME_CLASS_ERROR));

  return MTLK_ERR_OK;
}

static int __MTLK_IFUNC
_handle_fw_interference_ind (mtlk_handle_t core_object, const void *data, uint32 data_size)
{
  int8 det_threshold;
  mtlk_core_t* core = HANDLE_T_PTR(mtlk_core_t, core_object);
  wave_radio_t *radio = wave_vap_radio_get(core->vap_handle);
  const UMI_CONTINUOUS_INTERFERER *interferer_ind = (const UMI_CONTINUOUS_INTERFERER *)data;

  MTLK_ASSERT(sizeof(UMI_CONTINUOUS_INTERFERER) == data_size);

  ILOG3_DD("UMI_CONTINUOUS_INTERFERER event: Channel: %u, maximumValue: %d",
           interferer_ind->channel, interferer_ind->maximumValue);

  if (mtlk_vap_is_master_ap(core->vap_handle)) {
    if (core->is_stopped) {
      ILOG2_V("UMI_CONTINUOUS_INTERFERER event while core is down");
      return MTLK_ERR_OK; /* do not process */
    }

    if (!wave_radio_interfdet_get(radio)) {
      ILOG3_V("UMI_CONTINUOUS_INTERFERER event while interference detection is deactivated");
      return MTLK_ERR_OK; /* do not process */
    }

    if (mtlk_core_scan_is_running(core)) {
      ILOG2_V("UMI_CONTINUOUS_INTERFERER event while scan is running");
      return MTLK_ERR_OK; /* do not process */
    }

    if (WAVE_RADIO_OFF == WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_MODE_CURRENT)) {
      ILOG2_V("UMI_CONTINUOUS_INTERFERER event while RF is off");
      return MTLK_ERR_OK; /* do not process */
    }

    if (CW_40 == _mtlk_core_get_spectrum_mode(core)) {
      /* Use 40MHz thresold if user choose 40MHz explicitly */
      det_threshold = WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_INTERFDET_40MHZ_DETECTION_THRESHOLD);
    } else {
      /* Use 20MHZ threshold for 20MHz and 20/40 Auto and coexistance */
      det_threshold = WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_INTERFDET_20MHZ_DETECTION_THRESHOLD);
    }

    if (interferer_ind->maximumValue > det_threshold) {
      struct intel_vendor_channel_data ch_data;
      int res;
      iwpriv_cca_adapt_t cca_adapt_params;
      iwpriv_cca_th_t    cca_th_params;
      mtlk_pdb_size_t    cca_adapt_size = sizeof(cca_adapt_params);

      if (MTLK_ERR_OK == WAVE_RADIO_PDB_GET_BINARY(radio, PARAM_DB_RADIO_CCA_ADAPT, &cca_adapt_params, &cca_adapt_size)) {
        if (cca_adapt_params.initial) {
          mtlk_osal_timer_cancel_sync(&core->slow_ctx->cca_step_down_timer);
          core->slow_ctx->cca_adapt.cwi_poll = TRUE;
          core->slow_ctx->cca_adapt.cwi_drop_detected = FALSE;
          core->slow_ctx->cca_adapt.stepping_down = FALSE;
          core->slow_ctx->cca_adapt.stepping_up = FALSE;
          core->slow_ctx->cca_adapt.step_down_coef = 1;

          /* read user config */
          if (MTLK_ERR_OK != mtlk_core_cfg_read_cca_threshold(core, &cca_th_params)) {
            return MTLK_ERR_UNKNOWN;
          }

          ILOG3_V("CCA adaptive: stop adaptation");

          if (_mtlk_core_cca_is_above_configured(core, &cca_th_params))
          {
            int i;

            ILOG2_V("CCA adaptive: restore original values");

            for (i = 0; i < MTLK_CCA_TH_PARAMS_LEN; i++) {
              core->slow_ctx->cca_adapt.cca_th_params[i] = cca_th_params.values[i];
            }
            res = mtlk_core_cfg_send_cca_threshold_req(core, &cca_th_params);
            if (res != MTLK_ERR_OK)
              ELOG_DD("CID-%04x: Can't send CCA thresholds (err=%d)", mtlk_vap_get_oid(core->vap_handle), res);
          }
        }
      }

      if (is_interf_det_switch_needed(interferer_ind->channel)) {
        ILOG0_DDD("CID-%04x: Interference is detected on channel %d with Metric %d", mtlk_vap_get_oid(core->vap_handle),
                   interferer_ind->channel, interferer_ind->maximumValue);

        bt_acs_send_interf_event(interferer_ind->channel, interferer_ind->maximumValue);

        res = fill_channel_data(core, &ch_data);
        if (res == MTLK_ERR_OK) mtlk_send_chan_data(core->vap_handle, &ch_data, sizeof(ch_data));
      }
    }
  }

  return MTLK_ERR_OK;
}

void mtlk_cca_step_up_if_allowed (mtlk_core_t* core, int cwi_noise, int limit, int step_up)
{
  wave_radio_t *radio = wave_vap_radio_get(core->vap_handle);
  int det_threshold;
  int i;

  if (CW_40 == _mtlk_core_get_spectrum_mode(core)) {
    /* Use 40MHz thresold if user choose 40MHz explicitly */
    det_threshold = WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_INTERFDET_40MHZ_DETECTION_THRESHOLD);
  }
  else { /* Use 20MHZ threshold for 20MHz and 20/40 Auto and coexistance */
    det_threshold = WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_INTERFDET_20MHZ_DETECTION_THRESHOLD);
  }

  ILOG1_DDD("noise = %d, det_thresh = %d, limit = %d", cwi_noise, det_threshold, limit);
  ILOG1_DDDDD("current adaptive CCA thresholds: %d %d %d %d %d",
    core->slow_ctx->cca_adapt.cca_th_params[0],
    core->slow_ctx->cca_adapt.cca_th_params[1],
    core->slow_ctx->cca_adapt.cca_th_params[2],
    core->slow_ctx->cca_adapt.cca_th_params[3],
    core->slow_ctx->cca_adapt.cca_th_params[4]);

  if (cwi_noise < det_threshold) {

    if (core->slow_ctx->cca_adapt.cwi_poll) {
      if (core->slow_ctx->cca_adapt.cwi_drop_detected) {
        core->slow_ctx->cca_adapt.cwi_poll = FALSE;
        core->slow_ctx->cca_adapt.cwi_drop_detected = FALSE;
      } else {
        core->slow_ctx->cca_adapt.cwi_drop_detected = TRUE;
        return; /* act next time */
      }
    }

    if (_mtlk_core_cca_is_below_limit(core, limit))
    { /* increase threshold, send */
      iwpriv_cca_th_t cca_th_params;

      for (i = 0; i < MTLK_CCA_TH_PARAMS_LEN; i++) {
        core->slow_ctx->cca_adapt.cca_th_params[i] = MIN(limit, core->slow_ctx->cca_adapt.cca_th_params[i] + step_up);
        cca_th_params.values[i] = core->slow_ctx->cca_adapt.cca_th_params[i];
        ILOG1_DD("CCA adaptive: step up value %d: %d", i, cca_th_params.values[i]);
      }
      core->slow_ctx->cca_adapt.stepping_up = TRUE;
      mtlk_core_cfg_send_cca_threshold_req(core, &cca_th_params);
    }
  } else {
    core->slow_ctx->cca_adapt.cwi_poll = TRUE;
  }
}

static int __MTLK_IFUNC
_handle_beacon_blocked_ind(mtlk_handle_t core_object, const void *data, uint32 data_size)
{
  iwpriv_cca_adapt_t cca_adapt_params;
  mtlk_pdb_size_t cca_adapt_size = sizeof(cca_adapt_params);
  mtlk_core_t* core = HANDLE_T_PTR(mtlk_core_t, core_object);
  const UMI_Beacon_Block_t *beacon_block_ind = (const UMI_Beacon_Block_t *)data;
  mtlk_osal_msec_t cur_time = mtlk_osal_timestamp_to_ms(mtlk_osal_timestamp());
  wave_radio_t *radio = wave_vap_radio_get(core->vap_handle);
  iwpriv_cca_th_t cca_th_params;

  MTLK_ASSERT(sizeof(UMI_Beacon_Block_t) == data_size);

  ILOG1_D("BEACON_BLOCKED event: blocked: %d", beacon_block_ind->beaconBlock);

  if (mtlk_vap_is_master_ap(core->vap_handle)) {
    if (core->is_stopped) {
      ILOG1_V("BEACON_BLOCKED event while core is down");
      return MTLK_ERR_OK; /* do not process */
    }

    if (!wave_radio_interfdet_get(radio)) {
      ILOG1_V("BEACON_BLOCKED event while interference detection is deactivated");
      return MTLK_ERR_OK; /* do not process */
    }

    if (MTLK_ERR_OK == WAVE_RADIO_PDB_GET_BINARY(radio, PARAM_DB_RADIO_CCA_ADAPT, &cca_adapt_params, &cca_adapt_size)) {
      if (!cca_adapt_params.initial) {
        ILOG1_V("BEACON_BLOCKED event while CCA adaptation is deactivated");
        return MTLK_ERR_OK; /* do not process */
      }
    }

    /* read default user config */
    if (MTLK_ERR_OK != mtlk_core_cfg_read_cca_threshold(core, &cca_th_params)) {
      return MTLK_ERR_UNKNOWN;
    }

    if (beacon_block_ind->beaconBlock) { /* blocked */
      struct intel_vendor_channel_data ch_data;
      int res;

      if (core->slow_ctx->cca_adapt.stepping_down)
      { /* cancel step_down_timer */
        mtlk_osal_timer_cancel_sync(&core->slow_ctx->cca_step_down_timer);
        core->slow_ctx->cca_adapt.stepping_down = FALSE;
        if ((cur_time - core->slow_ctx->cca_adapt.last_unblocked_time) < (cca_adapt_params.min_unblocked_time * 1000)) {
          core->slow_ctx->cca_adapt.step_down_coef <<= 1;
        }
      }
      else {
        if (!core->slow_ctx->cca_adapt.stepping_up) {
          core->slow_ctx->cca_adapt.step_down_coef = 1; /* reset if adaptation is (re)started */
        }
      }

      memset(&ch_data, 0, sizeof(ch_data));
      res = scan_get_aocs_info(core, &ch_data, NULL);

      if (MTLK_ERR_OK == res)
        mtlk_cca_step_up_if_allowed(core, ch_data.cwi_noise, cca_adapt_params.limit, cca_adapt_params.step_up);
    }
    else { /* unblocked */
      core->slow_ctx->cca_adapt.stepping_up = FALSE;
      core->slow_ctx->cca_adapt.cwi_poll = FALSE;

      if (!core->slow_ctx->cca_adapt.stepping_down) {
        core->slow_ctx->cca_adapt.last_unblocked_time = cur_time;
        ILOG2_D("CCA adaptive: set last unblocked %d", core->slow_ctx->cca_adapt.last_unblocked_time);

        if (_mtlk_core_cca_is_above_configured(core, &cca_th_params)) {
          ILOG1_D("CCA adaptive: Schedule step down timer, interval %d", core->slow_ctx->cca_adapt.step_down_coef * cca_adapt_params.step_down_interval);
          ILOG1_DDDDD("user config  th: %d %d %d %d %d", cca_th_params.values[0], cca_th_params.values[1], cca_th_params.values[2], cca_th_params.values[3], cca_th_params.values[4]);
          ILOG1_DDDDD("current core th: %d %d %d %d %d",
            core->slow_ctx->cca_adapt.cca_th_params[0],
            core->slow_ctx->cca_adapt.cca_th_params[1],
            core->slow_ctx->cca_adapt.cca_th_params[2],
            core->slow_ctx->cca_adapt.cca_th_params[3],
            core->slow_ctx->cca_adapt.cca_th_params[4]);

          core->slow_ctx->cca_adapt.stepping_down = TRUE;
          mtlk_osal_timer_set(&core->slow_ctx->cca_step_down_timer,
            core->slow_ctx->cca_adapt.step_down_coef * cca_adapt_params.step_down_interval * 1000);
        }
      }
    }
  }

  return MTLK_ERR_OK;
}

static int
_mtlk_core_sta_req_bss_change (mtlk_core_t *master_core, struct mtlk_sta_bss_change_parameters *bss_change_params)
{
  struct cfg80211_chan_def *chandef = &bss_change_params->info->chandef;
  struct ieee80211_channel *channel = chandef->chan;
  struct mtlk_bss_parameters bp;
  unsigned long basic_rates = bss_change_params->info->basic_rates;
  int i, res;

  memset(&bp, 0, sizeof(bp));
  bp.vap_id = bss_change_params->vap_index;
  bp.use_cts_prot = bss_change_params->info->use_cts_prot;
  bp.use_short_preamble = bss_change_params->info->use_short_preamble;
  bp.use_short_slot_time = bss_change_params->info->use_short_slot;
  bp.ap_isolate = 0;
  bp.ht_opmode = bss_change_params->info->ht_operation_mode;
  bp.p2p_ctwindow = bss_change_params->info->p2p_noa_attr.oppps_ctwindow;
  bp.p2p_opp_ps = 0;

  for_each_set_bit(i, &basic_rates, BITS_PER_LONG) {
    bp.basic_rates[bp.basic_rates_len++] =
        bss_change_params->bands[channel->band]->bitrates[i].bitrate / 5;
  }

  res = _core_change_bss_by_params(master_core, &bp);

  return res;
}

/* This function should be called from master serializer context */
static int
_mtlk_core_sta_change_bss (mtlk_handle_t hcore, const void* data, uint32 data_size)
{
  uint32 res = MTLK_ERR_OK;
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;
  struct mtlk_sta_bss_change_parameters *bss_change_params = NULL;
  uint32 params_size;
  mtlk_core_t   *master_core = HANDLE_T_PTR(mtlk_core_t, hcore);
  wave_radio_t  *radio = wave_vap_radio_get(master_core->vap_handle);
  wv_mac80211_t *mac80211 = wave_radio_mac80211_get(radio);

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);
  MTLK_ASSERT(master_core == mtlk_core_get_master(master_core));

  bss_change_params = mtlk_clpb_enum_get_next(clpb, &params_size);
  MTLK_CLPB_TRY(bss_change_params, params_size)
    /* Prevent setting WMM while in scan */
    if (mtlk_core_is_in_scan_mode(master_core)) {
      MTLK_CLPB_EXIT(MTLK_ERR_RETRY);
    }

    if ((bss_change_params->changed & BSS_CHANGED_BASIC_RATES) ||
        (bss_change_params->changed & BSS_CHANGED_ERP_PREAMBLE) ||
        (bss_change_params->changed & BSS_CHANGED_ERP_SLOT) ||
        (bss_change_params->changed & BSS_CHANGED_ERP_CTS_PROT)) {
      res = _mtlk_core_sta_req_bss_change(master_core, bss_change_params);
      if (res) {
        ELOG_V("_mtlk_core_sta_change_bss failed");
        MTLK_CLPB_EXIT(res);
      }
      wv_mac80211_iface_set_initialized(mac80211, bss_change_params->vap_index);
    }

    if (!wv_mac80211_iface_get_is_initialized(mac80211, bss_change_params->vap_index)) {
      res = _mtlk_core_sta_req_bss_change(master_core, bss_change_params);
      if (res) {
        ELOG_V("_mtlk_core_sta_change_bss failed");
        MTLK_CLPB_EXIT(res);
      }
      wv_mac80211_iface_set_initialized(mac80211, bss_change_params->vap_index);
    }

    if (bss_change_params->changed & BSS_CHANGED_BEACON_INT) {
      mtlk_beacon_interval_t mtlk_beacon_interval;

      /*
       * FW should be notified about peer AP beacon interval so that FW ages out packets correctly.
       * Ager timeout for STA is set according to VAP beacon interval * STA listen interval.
       * If we only have one VSTA VAP FW may drop packets unnecessarily as ageing period will be minimum
       * Note: even though we do not enter PS we still need to do some periodic ageing on STA queues
       * in case we have low rate, many retries, lack of PDs, etc...
       */

      ILOG1_DD("Peer AP Beacon interval (Sta idx %d) has updated to: %d", bss_change_params->vap_index, bss_change_params->info->beacon_int);
      /* store interval to be used when we get CSA from peer AP*/
      wv_mac80211_iface_set_beacon_interval(mac80211, bss_change_params->vap_index, bss_change_params->info->beacon_int);

      mtlk_beacon_interval.beacon_interval = bss_change_params->info->beacon_int;
      mtlk_beacon_interval.vap_id = bss_change_params->vap_index;

      res = _mtlk_beacon_man_set_beacon_interval_by_params(bss_change_params->core, &mtlk_beacon_interval);
      if (res != MTLK_ERR_OK)
        ELOG_DD("Error setting peer AP beacon interval %u for VapID %u ", bss_change_params->info->beacon_int, bss_change_params->vap_index);
    }

    /* this flag indicates that txq params for all ac's was set by wv_ieee80211_conf_tx*/
    if (bss_change_params->changed & BSS_CHANGED_QOS) {
      struct mtlk_wmm_settings wmm_settings;

      wmm_settings.vap_id = bss_change_params->vap_index;
      /* For Sta mode txq params values were already set in param_db by wv_ieee80211_conf_tx,
       * so these are just dummy values */
      wmm_settings.txq_params.ac = 0;
      wmm_settings.txq_params.aifs = 0;
      wmm_settings.txq_params.cwmax = 0;
      wmm_settings.txq_params.cwmin = 0;
      wmm_settings.txq_params.txop = 0;
      wmm_settings.txq_params.acm_flag = 0;

      res = core_cfg_wmm_param_set_by_params(master_core, &wmm_settings);

      if (res != MTLK_ERR_OK)
        ELOG_V("WAVE_CORE_REQ_SET_WMM_PARAM failed");
    }

    if (bss_change_params->changed & BSS_CHANGED_TXPOWER) {
      ILOG0_SD("%s: TODO: tx power changed tx_power=%d", bss_change_params->vif_name, bss_change_params->info->txpower);
    }
  MTLK_CLPB_FINALLY(res)
    return mtlk_clpb_push_res(clpb, res);
  MTLK_CLPB_END
}

static int __MTLK_IFUNC
_mtlk_handle_eeprom_failure_sync(mtlk_handle_t object, const void *data,  uint32 data_size)
{
  mtlk_core_t *nic = HANDLE_T_PTR(mtlk_core_t, object);
  MTLK_ASSERT(sizeof(EEPROM_FAILURE_EVENT) == data_size);

  mtlk_cc_handle_eeprom_failure(nic->vap_handle, (const EEPROM_FAILURE_EVENT*) data);

  return MTLK_ERR_OK;
}

static int __MTLK_IFUNC
_mtlk_handle_generic_event(mtlk_handle_t object, const void *data,  uint32 data_size)
{
  MTLK_ASSERT(sizeof(GENERIC_EVENT) == data_size);
  mtlk_cc_handle_generic_event(HANDLE_T_PTR(mtlk_core_t, object)->vap_handle, (GENERIC_EVENT*) data);
  return MTLK_ERR_OK;
}

static int __MTLK_IFUNC
_mtlk_handle_algo_failure(mtlk_handle_t object, const void *data,  uint32 data_size)
{
  mtlk_core_t *nic = HANDLE_T_PTR(mtlk_core_t, object);

  MTLK_ASSERT(sizeof(CALIBR_ALGO_EVENT) == data_size);

  mtlk_cc_handle_algo_calibration_failure(nic->vap_handle, (const CALIBR_ALGO_EVENT*)data);

  return MTLK_ERR_OK;
}

static int __MTLK_IFUNC
_mtlk_handle_dummy_event(mtlk_handle_t object, const void *data,  uint32 data_size)
{
  MTLK_ASSERT(sizeof(DUMMY_EVENT) == data_size);
  mtlk_cc_handle_dummy_event(HANDLE_T_PTR(mtlk_core_t, object)->vap_handle, (const DUMMY_EVENT*) data);
  return MTLK_ERR_OK;
}

static int __MTLK_IFUNC
_mtlk_handle_unknown_event(mtlk_handle_t object, const void *data,  uint32 data_size)
{
  MTLK_ASSERT(sizeof(uint32) == data_size);
  mtlk_cc_handle_unknown_event(HANDLE_T_PTR(mtlk_core_t, object)->vap_handle, *(uint32*)data);
  return MTLK_ERR_OK;
}

static void __MTLK_IFUNC
_mtlk_handle_mac_event(mtlk_core_t         *nic,
                       MAC_EVENT           *event)
{
  uint32 event_id = MAC_TO_HOST32(event->u32EventID) & 0xff;

  switch(event_id)
  {
  case EVENT_EEPROM_FAILURE:
    _mtlk_process_hw_task(nic, SYNCHRONOUS, _mtlk_handle_eeprom_failure_sync,
                          HANDLE_T(nic), &event->u.sEepromEvent, sizeof(EEPROM_FAILURE_EVENT));
    break;
  case EVENT_GENERIC_EVENT:
    _mtlk_process_hw_task(nic, SERIALIZABLE, _mtlk_handle_generic_event,
                          HANDLE_T(nic), &event->u.sGenericData, sizeof(GENERIC_EVENT));
    break;
  case EVENT_CALIBR_ALGO_FAILURE:
    _mtlk_process_hw_task(nic, SERIALIZABLE, _mtlk_handle_algo_failure,
                          HANDLE_T(nic), &event->u.sCalibrationEvent, sizeof(CALIBR_ALGO_EVENT));
    break;
  case EVENT_DUMMY:
    _mtlk_process_hw_task(nic, SERIALIZABLE, _mtlk_handle_dummy_event,
                          HANDLE_T(nic), &event->u.sDummyEvent, sizeof(DUMMY_EVENT));
    break;
  default:
    _mtlk_process_hw_task(nic, SERIALIZABLE, _mtlk_handle_unknown_event,
                          HANDLE_T(nic), &event_id, sizeof(uint32));
    break;
  }
}

static int __MTLK_IFUNC
_mtlk_handle_unknown_ind_type(mtlk_handle_t object, const void *data,  uint32 data_size)
{
  MTLK_ASSERT(sizeof(uint32) == data_size);
  ILOG0_DD("CID-%04x: Unknown MAC indication type %u",
           mtlk_vap_get_oid(HANDLE_T_PTR(mtlk_core_t, object)->vap_handle), *(uint32*)data);
  return MTLK_ERR_OK;
}

static void
_mtlk_core_handle_rx_ctrl (mtlk_vap_handle_t   vap_handle,
                          uint32               id,
                          void                *payload,
                          uint32               payload_buffer_size)
{
  mtlk_core_t *nic = mtlk_vap_get_core (vap_handle);
  mtlk_core_t *master_nic = NULL;

  MTLK_ASSERT(NULL != nic);

  master_nic = mtlk_vap_manager_get_master_core(mtlk_vap_get_manager(nic->vap_handle));
  MTLK_ASSERT(NULL != master_nic);

  switch(id)
  {
  case MC_MAN_MAC_EVENT_IND:
    _mtlk_handle_mac_event(nic, (MAC_EVENT*)payload);
    break;
  case MC_MAN_TKIP_MIC_FAILURE_IND:
    _mtlk_process_hw_task(master_nic, SERIALIZABLE, _handle_security_alert_ind,
                           HANDLE_T(nic), payload, sizeof(UMI_TKIP_MIC_FAILURE));
    break;
  case MC_MAN_TRACE_IND:
    _mtlk_process_hw_task(nic, SERIALIZABLE, _handle_fw_debug_trace_event,
                          HANDLE_T(nic), payload, sizeof(UmiDbgTraceInd_t));
    break;
  case MC_MAN_CONTINUOUS_INTERFERER_IND:
    _mtlk_process_hw_task(nic, SERIALIZABLE, _handle_fw_interference_ind,
                          HANDLE_T(nic), payload, sizeof(UMI_CONTINUOUS_INTERFERER));
    break;
  case MC_MAN_BEACON_BLOCKED_IND:
    _mtlk_process_hw_task(nic, SERIALIZABLE, _handle_beacon_blocked_ind,
      HANDLE_T(nic), payload, sizeof(UMI_Beacon_Block_t));
    break;
  case MC_MAN_RADAR_IND:
    _mtlk_process_hw_task(master_nic, SERIALIZABLE, _handle_radar_event,
                          HANDLE_T(master_nic), payload, sizeof(UMI_RADAR_DETECTION));
    break;
  case MC_MAN_BEACON_TEMPLATE_WAS_SET_IND:
    /* This has to be run in master VAP context */
    if (RCVRY_TYPE_UNDEF != wave_rcvry_type_current_get(mtlk_vap_get_hw(nic->vap_handle))) {
      /* recovery case */
      wave_beacon_man_rcvry_template_ind_handle(master_nic);
    } else {
      /* operational case */
      _mtlk_process_hw_task(master_nic, SERIALIZABLE, wave_beacon_man_template_ind_handle,
                            HANDLE_T(master_nic), payload, sizeof(UMI_BEACON_SET));
    }
    break;
  case MC_MAN_CLASS3_ERROR_IND:
    _mtlk_process_hw_task(nic, SYNCHRONOUS, _handle_class3_error_ind_handle,
                           HANDLE_T(nic), payload, sizeof(UMI_FRAME_CLASS_ERROR_ENTRY));
    break;
  case MC_MAN_HE_MU_DBG_IND: {
    _mtlk_process_hw_task(nic, SERIALIZABLE, wave_dbg_static_plan_mu_group_stats_ind_handle,
                          HANDLE_T(nic), payload, sizeof(UMI_DBG_HE_MU_GROUP_STATS));
    break;
  }
  default:
    _mtlk_process_hw_task(nic, SERIALIZABLE, _mtlk_handle_unknown_ind_type,
                           HANDLE_T(nic), &id, sizeof(uint32));
    break;
  }
}


static BOOL
mtlk_core_ready_to_process_task (mtlk_core_t *nic, uint32 req_id)
{
  wave_rcvry_type_e rcvry_type;
  mtlk_hw_t *hw = mtlk_vap_get_hw(nic->vap_handle);

  /* if error is occurred in _pci_probe then serializer is absent */
  if (wave_rcvry_pci_probe_error_get(wave_hw_mmb_get_mmb_base(hw)))
    return FALSE;

  /* if NONE Recovery was executed then handle BCL commands only */
  rcvry_type = wave_rcvry_type_current_get(hw);
  if (((RCVRY_TYPE_DUT == rcvry_type) ||
       (RCVRY_TYPE_IGNORE == rcvry_type)) &&          /* just in case, if user executes 2nd NONE Recovery */
      (WAVE_CORE_REQ_GET_NETWORK_MODE != req_id) &&   /* required for BclSockServer */
      (WAVE_RADIO_REQ_GET_BCL_MAC_DATA != req_id) &&
      (WAVE_RADIO_REQ_SET_BCL_MAC_DATA != req_id)) {
    return FALSE;
  }

  if ((mtlk_core_rcvry_is_running(nic) ||
       mtlk_core_is_hw_halted(nic) ||
       wave_rcvry_mac_fatal_pending_get(hw)) &&
      (WAVE_CORE_REQ_SET_BEACON    == req_id ||
       WAVE_CORE_REQ_ACTIVATE_OPEN == req_id ||
       WAVE_CORE_REQ_GET_STATION   == req_id ||
       WAVE_CORE_REQ_CHANGE_BSS    == req_id ||
       WAVE_CORE_REQ_SET_WMM_PARAM == req_id ||
       WAVE_RADIO_REQ_SET_CHAN      == req_id ||
       WAVE_RADIO_REQ_DO_SCAN       == req_id ||
       WAVE_RADIO_REQ_SCAN_TIMEOUT  == req_id)) {
    return FALSE;
  }

  return TRUE;
}

void __MTLK_IFUNC
mtlk_core_handle_tx_ctrl (mtlk_vap_handle_t    vap_handle,
                          mtlk_user_request_t *req,
                          uint32               id,
                          mtlk_clpb_t         *data)
{
#define _WAVE_CORE_REQ_MAP_START(req_id)                                                \
  switch (req_id) {

#define _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(req_id, func)                                \
  case (req_id):                                                                        \
    _mtlk_process_user_task(nic, req, SERIALIZABLE, req_id, func, HANDLE_T(nic), data); \
    break;

#define _MTLK_CORE_HANDLE_REQ_SYNCHRONOUS(req_id, func)                                 \
  case (req_id):                                                                        \
    _mtlk_process_user_task(nic, req, SYNCHRONOUS, req_id, func, HANDLE_T(nic), data);  \
    break;
#define _MTLK_CORE_HANDLE_REQ_DUMPABLE(req_id, func)                                    \
  case (req_id):                                                                        \
  if (wave_rcvry_fw_dump_in_progress_get(mtlk_vap_get_hw(nic->vap_handle))) {           \
    _mtlk_process_user_task(nic, req, SYNCHRONOUS, req_id, func, HANDLE_T(nic), data);  \
  }                                                                                     \
  else {                                                                                \
    _mtlk_process_user_task(nic, req, SERIALIZABLE, req_id, func, HANDLE_T(nic), data); \
  }                                                                                     \
  break;
#define _WAVE_CORE_REQ_MAP_END(_id_)                                                    \
    default:                                                                            \
      ELOG_D("Request 0x%08x not mapped by core", _id_);                                \
      MTLK_ASSERT(FALSE);                                                               \
  }

  mtlk_core_t *nic = mtlk_vap_get_core(vap_handle);

  MTLK_ASSERT(NULL != nic);
  MTLK_ASSERT(NULL != req);
  MTLK_ASSERT(NULL != data);

  if (!mtlk_core_ready_to_process_task (nic, id)) {
    mtlk_df_ui_req_complete(req, MTLK_ERR_NOT_READY);
    return;
  }

  _WAVE_CORE_REQ_MAP_START(id)
    /* Radio requests */
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_CORE_REQ_ACTIVATE_OPEN,             _mtlk_core_activate);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_CORE_REQ_SET_BEACON,                wave_beacon_man_beacon_set);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_CORE_REQ_SET_BEACON_INTERVAL,       _mtlk_beacon_man_set_beacon_interval);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_CORE_REQ_REQUEST_SID,               core_cfg_request_sid);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_CORE_REQ_REMOVE_SID,                core_cfg_remove_sid);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_CORE_REQ_SYNC_DONE,                 _core_sync_done)
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_CORE_REQ_CHANGE_BSS,                _core_change_bss)
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_CORE_REQ_GET_STATION,               core_cfg_get_station);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_CORE_REQ_SET_WMM_PARAM,             core_cfg_wmm_param_set);
    _MTLK_CORE_HANDLE_REQ_SYNCHRONOUS(WAVE_CORE_REQ_MGMT_TX,                    _mtlk_core_mgmt_tx);
    _MTLK_CORE_HANDLE_REQ_SYNCHRONOUS(WAVE_CORE_REQ_MGMT_RX,                    _mtlk_core_mgmt_rx);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_CORE_REQ_CONNECT_STA,               _mtlk_core_connect_sta);
#ifdef MTLK_LEGACY_STATISTICS
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_CORE_REQ_DISCONNECT_STA,            _mtlk_core_hanle_disconnect_sta_req);
#endif
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_CORE_REQ_AP_CONNECT_STA,            _mtlk_core_ap_connect_sta);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_CORE_REQ_AP_DISCONNECT_STA,         _mtlk_core_ap_disconnect_sta);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_CORE_REQ_AP_DISCONNECT_ALL,         core_cfg_ap_disconnect_all);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_CORE_REQ_AUTHORIZING_STA,           _mtlk_core_ap_authorizing_sta);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_CORE_REQ_DEACTIVATE,                _mtlk_core_deactivate);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_CORE_REQ_SET_MAC_ADDR,              _mtlk_core_set_mac_addr_wrapper);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_CORE_REQ_GET_MAC_ADDR,              _mtlk_core_get_mac_addr);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_CORE_REQ_GET_STATUS,                _mtlk_core_get_status);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_CORE_REQ_RESET_STATS,               _mtlk_core_reset_stats);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_CORE_REQ_SET_WDS_CFG,               _mtlk_core_set_wds_cfg);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_CORE_REQ_GET_WDS_CFG,               _mtlk_core_get_wds_cfg);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_CORE_REQ_GET_WDS_PEERAP,            _mtlk_core_get_wds_peer_ap);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_CORE_REQ_GET_STADB_CFG,             _mtlk_core_get_stadb_cfg);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_CORE_REQ_SET_STADB_CFG,             _mtlk_core_set_stadb_cfg);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_CORE_REQ_GET_CORE_CFG,              _mtlk_core_get_core_cfg);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_CORE_REQ_SET_CORE_CFG,              _mtlk_core_set_core_cfg);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_CORE_REQ_GET_HSTDB_CFG,             _mtlk_core_get_hstdb_cfg);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_CORE_REQ_SET_HSTDB_CFG,             _mtlk_core_set_hstdb_cfg);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_CORE_REQ_GEN_DATA_EXCHANGE,         _mtlk_core_gen_data_exchange);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_CORE_REQ_GET_EE_CAPS,               _mtlk_core_get_ee_caps);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_CORE_REQ_GET_MC_IGMP_TBL,           _mtlk_core_get_mc_igmp_tbl);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_CORE_REQ_GET_MC_HW_TBL,             _mtlk_core_get_mc_hw_tbl);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_CORE_REQ_GET_RANGE_INFO,            _mtlk_core_range_info_get);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_CORE_REQ_GET_STADB_STATUS,          _mtlk_core_get_stadb_sta_list);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_CORE_REQ_GET_STADB_STA_BY_ITER_ID,  _mtlk_core_get_stadb_sta_by_iter_id);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_CORE_REQ_GET_HS20_INFO,             _mtlk_core_get_hs20_info);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_CORE_REQ_SET_WDS_WEP_ENC_CFG,       mtlk_core_cfg_set_wds_wep_enc_cfg);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_CORE_REQ_GET_WDS_WEP_ENC_CFG,       mtlk_core_cfg_get_wds_wep_enc_cfg);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_CORE_REQ_SET_AUTH_CFG,              mtlk_core_cfg_set_auth_cfg);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_CORE_REQ_GET_AUTH_CFG,              mtlk_core_cfg_get_auth_cfg);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_CORE_REQ_GET_ENCEXT_CFG,            _mtlk_core_get_enc_ext_cfg);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_CORE_REQ_SET_ENCEXT_CFG,            _mtlk_core_set_enc_ext_cfg);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_CORE_REQ_SET_DEFAULT_KEY_CFG,       _mtlk_core_set_default_key_cfg);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_CORE_REQ_GET_IW_GENERIC,            _mtlk_core_get_iw_generic);
    _MTLK_CORE_HANDLE_REQ_SYNCHRONOUS(WAVE_CORE_REQ_GET_SERIALIZER_INFO,        _mtlk_core_get_serializer_info);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_CORE_REQ_SET_RTLOG_CFG,             _mtlk_core_set_rtlog_cfg);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(MTLK_CORE_REQ_MCAST_HELPER_GROUP_ID_ACTION, _mtlk_core_mcast_helper_group_id_action);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_CORE_REQ_GET_RX_TH,                 mtlk_core_cfg_get_rx_threshold);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_CORE_REQ_SET_RX_TH,                 mtlk_core_cfg_set_rx_threshold);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_CORE_REQ_SET_QOS_MAP,               _mtlk_core_set_qos_map);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_CORE_REQ_SET_AGGR_CONFIG,           _mtlk_core_set_aggr_cfg);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_CORE_REQ_GET_AGGR_CONFIG,           _mtlk_core_get_aggr_cfg);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_CORE_REQ_SET_SOFTBLOCKLIST_ENTRY,   _mtlk_core_set_softblocklist_entry);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_CORE_REQ_SET_BLACKLIST_ENTRY,       _mtlk_core_set_blacklist_entry);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_CORE_REQ_GET_BLACKLIST_ENTRIES,     _mtlk_core_get_blacklist_entries);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_CORE_REQ_GET_MULTI_AP_BL_ENTRIES,   mtlk_core_cfg_get_multi_ap_blacklist_entries);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_CORE_REQ_GET_ASSOCIATED_DEV_RATE_INFO_RX_STATS,  mtlk_core_get_associated_dev_rate_info_rx_stats);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_CORE_REQ_GET_ASSOCIATED_DEV_RATE_INFO_TX_STATS,  mtlk_core_get_associated_dev_rate_info_tx_stats);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_CORE_REQ_GET_STATION_MEASUREMENTS,  _mtlk_core_get_station_measurements);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_CORE_REQ_GET_VAP_MEASUREMENTS,      _mtlk_core_get_vap_measurements);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_CORE_REQ_GET_UNCONNECTED_STATION,   _mtlk_core_get_unconnected_station);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_CORE_REQ_SET_FOUR_ADDR_CFG,         core_cfg_set_four_addr_cfg);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_CORE_REQ_GET_FOUR_ADDR_CFG,         core_cfg_get_four_addr_cfg);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_CORE_REQ_GET_FOUR_ADDR_STA_LIST,    core_cfg_get_four_addr_sta_list);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_CORE_REQ_SET_ATF_QUOTAS,            mtlk_core_cfg_set_atf_quotas);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_CORE_REQ_SET_MCAST_RANGE,           mtlk_core_cfg_set_mcast_range);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_CORE_REQ_GET_MCAST_RANGE_IPV4,      mtlk_core_cfg_get_mcast_range_list_ipv4);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_CORE_REQ_GET_MCAST_RANGE_IPV6,      mtlk_core_cfg_get_mcast_range_list_ipv6);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_CORE_REQ_SET_OPERATING_MODE,        mtlk_core_cfg_set_operating_mode);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_CORE_REQ_SET_WDS_WPA_LIST_ENTRY,    mtlk_core_cfg_set_wds_wpa_entry);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_CORE_REQ_GET_WDS_WPA_LIST_ENTRIES,  mtlk_core_cfg_get_wds_wpa_entry);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_CORE_REQ_SET_DGAF_DISABLED,         mtlk_core_cfg_set_dgaf_disabled);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_CORE_REQ_GET_TX_POWER,             _mtlk_core_get_tx_power);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_CORE_REQ_CHECK_4ADDR_MODE,          _mtlk_core_check_4addr_mode);

    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_RADIO_REQ_GET_RADIO_INFO,           _mtlk_core_get_radio_info);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_RADIO_REQ_SET_CHAN,                 core_cfg_set_chan_clpb);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_RADIO_REQ_DO_SCAN,                  scan_do_scan);
    _MTLK_CORE_HANDLE_REQ_SYNCHRONOUS(WAVE_RADIO_REQ_START_SCHED_SCAN,          scan_start_sched_scan);
    _MTLK_CORE_HANDLE_REQ_SYNCHRONOUS(WAVE_RADIO_REQ_STOP_SCHED_SCAN,           scan_stop_sched_scan);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_RADIO_REQ_SCAN_TIMEOUT,             scan_timeout_async_func);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_RADIO_REQ_ALTER_SCAN,               mtlk_alter_scan)
    _MTLK_CORE_HANDLE_REQ_SYNCHRONOUS(WAVE_RADIO_REQ_DUMP_SURVEY,               scan_dump_survey);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_RADIO_REQ_FIN_PREV_FW_SC,           finish_and_prevent_fw_set_chan_clpb);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_RADIO_REQ_GET_AOCS_CFG,             _mtlk_core_get_aocs_cfg);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_RADIO_REQ_SET_AOCS_CFG,             _mtlk_core_set_aocs_cfg);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_RADIO_REQ_GET_DOT11H_AP_CFG,        _mtlk_core_get_dot11h_ap_cfg);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_RADIO_REQ_SET_DOT11H_AP_CFG,        _mtlk_core_set_dot11h_ap_cfg);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_RADIO_REQ_GET_MIBS_CFG,             _mtlk_core_get_mibs_cfg);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_RADIO_REQ_SET_MIBS_CFG,             _mtlk_core_set_mibs_cfg);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_RADIO_REQ_GET_COUNTRY_CFG,          _mtlk_core_get_country_cfg);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_RADIO_REQ_SET_COUNTRY_CFG,          _mtlk_core_set_country_cfg);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_RADIO_REQ_GET_DOT11D_CFG,           _core_cfg_get_dot11d_cfg);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_RADIO_REQ_SET_DOT11D_CFG,           _mtlk_core_set_dot11d_cfg);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_RADIO_REQ_GET_MAC_WATCHDOG_CFG,     _mtlk_core_get_mac_wdog_cfg);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_RADIO_REQ_SET_MAC_WATCHDOG_CFG,     _mtlk_core_set_mac_wdog_cfg);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_RADIO_REQ_GET_MASTER_CFG,           _mtlk_core_get_master_specific_cfg);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_RADIO_REQ_SET_MASTER_CFG,           _mtlk_core_set_master_specific_cfg);
#ifdef MTLK_LEGACY_STATISTICS
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_RADIO_REQ_GET_MHI_STATS,            _mtlk_core_get_mhi_stats);
#endif
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_RADIO_REQ_GET_PHY_RX_STATUS,        _mtlk_core_get_phy_rx_status);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_RADIO_REQ_GET_HW_LIMITS,            _mtlk_core_get_hw_limits);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_RADIO_REQ_GET_TX_RATE_POWER,        _mtlk_core_get_tx_rate_power);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_RADIO_REQ_GET_TPC_CFG,              mtlk_core_get_tpc_cfg);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_RADIO_REQ_SET_TPC_CFG,              mtlk_core_set_tpc_cfg);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_RADIO_REQ_GET_COC_CFG,              _mtlk_core_get_coc_cfg);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_RADIO_REQ_SET_COC_CFG,              _mtlk_core_set_coc_cfg);
#ifdef CPTCFG_IWLWAV_PMCU_SUPPORT
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_RADIO_REQ_GET_PCOC_CFG,             _mtlk_core_get_pcoc_cfg);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_RADIO_REQ_SET_PCOC_CFG,             _mtlk_core_set_pcoc_cfg);
#endif /*CPTCFG_IWLWAV_PMCU_SUPPORT*/
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_RADIO_REQ_STOP_LM,                  _mtlk_core_stop_lm);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_RADIO_REQ_SET_MAC_ASSERT,           _mtlk_core_set_mac_assert);
    _MTLK_CORE_HANDLE_REQ_DUMPABLE(WAVE_RADIO_REQ_GET_BCL_MAC_DATA,             _mtlk_core_bcl_mac_data_get);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_RADIO_REQ_SET_BCL_MAC_DATA,         _mtlk_core_bcl_mac_data_set);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_RADIO_REQ_MBSS_ADD_VAP_IDX,         _mtlk_core_add_vap_idx);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_RADIO_REQ_MBSS_ADD_VAP_NAME,        _mtlk_core_add_vap_name);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_RADIO_REQ_MBSS_DEL_VAP_IDX,         _mtlk_core_del_vap_idx);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_RADIO_REQ_MBSS_DEL_VAP_NAME,        _mtlk_core_del_vap_name);
    /* Interference Detection */
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_RADIO_REQ_SET_INTERFDET_PARAMS_CFG, _mtlk_core_set_interfdet_cfg);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_RADIO_REQ_GET_INTERFDET_MODE_CFG,   _mtlk_core_get_interfdet_mode_cfg);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_RADIO_REQ_SET_DBG_CLI,              _mtlk_core_simple_cli);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_RADIO_REQ_SET_FW_DEBUG,             _mtlk_core_fw_debug);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_RADIO_REQ_SET_FW_LOG_SEVERITY,      _mtlk_core_set_fw_log_severity);
    /* Traffic Analyzer */
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_RADIO_REQ_SET_TA_CFG,               _mtlk_core_set_ta_cfg);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_RADIO_REQ_GET_TA_CFG,               _mtlk_core_get_ta_cfg);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_RADIO_REQ_SET_11B_CFG,              _mtlk_core_set_11b_cfg);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_RADIO_REQ_GET_11B_CFG,              _mtlk_core_get_11b_cfg);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_RADIO_REQ_SET_RECOVERY_CFG,         _mtlk_core_set_recovery_cfg);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_RADIO_REQ_GET_RECOVERY_CFG,         _mtlk_core_get_recovery_cfg);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_RADIO_REQ_GET_RECOVERY_STATS,       _mtlk_core_get_recovery_stats);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_RADIO_REQ_SET_SCAN_AND_CALIB_CFG,   _mtlk_core_set_scan_and_calib_cfg);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_RADIO_REQ_GET_SCAN_AND_CALIB_CFG,   _mtlk_core_get_scan_and_calib_cfg);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_RADIO_REQ_GET_AGG_RATE_LIMIT,       _mtlk_core_get_agg_rate_limit);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_RADIO_REQ_SET_AGG_RATE_LIMIT,       _mtlk_core_set_agg_rate_limit);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_RADIO_REQ_GET_RX_DUTY_CYCLE,        mtlk_core_cfg_get_rx_duty_cycle);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_RADIO_REQ_SET_RX_DUTY_CYCLE,        mtlk_core_cfg_set_rx_duty_cycle);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_RADIO_REQ_GET_TX_POWER_LIMIT_OFFSET,  _mtlk_core_get_tx_power_limit_offset);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_RADIO_REQ_SET_TX_POWER_LIMIT_OFFSET,  _mtlk_core_set_tx_power_limit_offset);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_CORE_REQ_GET_HT_PROTECTION,         _mtlk_core_get_ht_protection)
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_CORE_REQ_SET_HT_PROTECTION,         _mtlk_core_set_ht_protection)
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_RADIO_REQ_SET_QAMPLUS_MODE,         _mtlk_core_set_qamplus_mode);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_RADIO_REQ_GET_QAMPLUS_MODE,         _mtlk_core_get_qamplus_mode);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_RADIO_REQ_SET_RADIO_MODE,           _mtlk_core_set_radio_mode);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_RADIO_REQ_GET_RADIO_MODE,           _mtlk_core_get_radio_mode);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_RADIO_REQ_SET_AMSDU_NUM,            _mtlk_core_set_amsdu_num);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_RADIO_REQ_GET_AMSDU_NUM,            _mtlk_core_get_amsdu_num);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_RADIO_REQ_SET_CCA_THRESHOLD,        mtlk_core_cfg_set_cca_threshold);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_RADIO_REQ_GET_CCA_THRESHOLD,        mtlk_core_cfg_get_cca_threshold);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_RADIO_REQ_SET_CCA_ADAPTIVE,         mtlk_core_cfg_set_cca_intervals);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_RADIO_REQ_GET_CCA_ADAPTIVE,         mtlk_core_cfg_get_cca_intervals);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_RADIO_REQ_SET_RADAR_RSSI_TH,        mtlk_core_cfg_set_radar_rssi_threshold);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_RADIO_REQ_GET_RADAR_RSSI_TH,        mtlk_core_cfg_get_radar_rssi_threshold);
#ifdef CPTCFG_IWLWAV_SET_PM_QOS
    _MTLK_CORE_HANDLE_REQ_SYNCHRONOUS (WAVE_RADIO_REQ_GET_CPU_DMA_LATENCY,      _mtlk_core_get_cpu_dma_latency);
    _MTLK_CORE_HANDLE_REQ_SYNCHRONOUS (WAVE_RADIO_REQ_SET_CPU_DMA_LATENCY,      _mtlk_core_set_cpu_dma_latency);
#endif
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_RADIO_REQ_SET_MAX_MPDU_LENGTH,      mtlk_core_cfg_set_max_mpdu_length);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_RADIO_REQ_GET_MAX_MPDU_LENGTH,      mtlk_core_cfg_get_max_mpdu_length);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_RADIO_REQ_SET_IRE_CTRL_B,           mtlk_core_cfg_set_ire_ctrl_b);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_RADIO_REQ_GET_IRE_CTRL_B,           mtlk_core_cfg_get_ire_ctrl_b);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_RADIO_REQ_SET_FIXED_RATE,           _mtlk_core_set_fixed_rate);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_RADIO_SET_STATIC_PLAN,              mtlk_core_cfg_set_static_plan);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_RADIO_REQ_SET_SSB_MODE,             mtlk_core_set_ssb_mode);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_RADIO_REQ_GET_SSB_MODE,             mtlk_core_get_ssb_mode);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_RADIO_REQ_SET_COEX_CFG,             mtlk_core_set_coex_cfg);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_RADIO_REQ_GET_COEX_CFG,             mtlk_core_get_coex_cfg);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_RADIO_REQ_SET_FREQ_JUMP_MODE,       mtlk_core_cfg_set_freq_jump_mode);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_RADIO_REQ_SET_RESTRICTED_AC_MODE,    mtlk_core_cfg_set_restricted_ac_mode_cfg);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_RADIO_REQ_GET_RESTRICTED_AC_MODE,    mtlk_core_cfg_get_restricted_ac_mode_cfg);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_RADIO_REQ_STA_CHANGE_BSS,            _mtlk_core_sta_change_bss);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_RADIO_REQ_SET_FIXED_LTF_AND_GI,      mtlk_core_set_fixed_ltf_and_gi);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_RADIO_REQ_GET_FIXED_LTF_AND_GI,      mtlk_core_get_fixed_ltf_and_gi);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_RADIO_REQ_CAC_TIMEOUT,               cac_timer_serialized_func);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_RADIO_REQ_SET_CALIBRATION_MASK,      core_cfg_set_calibration_mask);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_RADIO_REQ_GET_CALIBRATION_MASK,      core_cfg_get_calibration_mask);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_RADIO_REQ_SET_NFRP_CFG,              wave_core_set_nfrp_cfg);

    /* HW requests */
#ifdef EEPROM_DATA_ACCESS
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_HW_REQ_GET_EEPROM_CFG,              _mtlk_core_get_eeprom_cfg);
#endif /* EEPROM_DATA_ACCESS */
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_HW_REQ_GET_AP_CAPABILITIES,         _mtlk_core_get_ap_capabilities)
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_HW_REQ_GET_BF_EXPLICIT_CAP,         _mtlk_core_get_bf_explicit_cap);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_HW_REQ_SET_TEMPERATURE_SENSOR,      _mtlk_core_set_calibrate_on_demand);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_HW_REQ_GET_TEMPERATURE_SENSOR,      _mtlk_core_get_temperature);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_HW_REQ_GET_TASKLET_LIMITS,          _mtlk_core_get_tasklet_limits);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_HW_REQ_SET_TASKLET_LIMITS,          _mtlk_core_set_tasklet_limits);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(MTLK_HW_REQ_GET_COUNTERS_SRC,            mtlk_core_cfg_get_counters_src);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(MTLK_HW_REQ_SET_COUNTERS_SRC,            mtlk_core_cfg_set_counters_src);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_HW_REQ_GET_PVT_SENSOR,              wave_core_get_pvt_sensor);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_HW_REQ_SET_TEST_BUS,                wave_core_set_test_bus_mode);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_RCVRY_RESET,                        _wave_core_rcvry_reset);

    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_CORE_REQ_RCVRY_MSG_TX,              wave_core_cfg_send_rcvry_msg);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_CORE_REQ_GET_NETWORK_MODE,          _wave_core_network_mode_get);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_RADIO_REQ_GET_RTS_RATE,             wave_core_cfg_get_rts_rate);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_RADIO_REQ_GET_CDB_CFG,              _wave_core_cdb_cfg_get);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_RADIO_REQ_SET_RTS_RATE,             wave_core_cfg_set_rts_rate);

    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_RADIO_REQ_GET_PHY_INBAND_POWER,     _wave_core_get_phy_inband_power);

    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_CORE_REQ_GET_ASSOCIATED_DEV_STATS,    mtlk_core_get_associated_dev_stats);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_CORE_REQ_GET_CHANNEL_STATS,          mtlk_core_get_channel_stats);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_CORE_REQ_GET_ASSOCIATED_DEV_TID_STATS,  mtlk_core_get_associated_dev_tid_stats);

    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_CORE_REQ_GET_STATS_POLL_PERIOD,      mtlk_core_stats_poll_period_get);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_CORE_REQ_SET_STATS_POLL_PERIOD,      mtlk_core_stats_poll_period_set);

#ifndef MTLK_LEGACY_STATISTICS
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_CORE_REQ_GET_PEER_LIST,             mtlk_core_get_peer_list);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_CORE_REQ_GET_PEER_FLOW_STATUS,      mtlk_core_get_peer_flow_status);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_CORE_REQ_GET_PEER_CAPABILITIES,     mtlk_core_get_peer_capabilities);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_CORE_REQ_GET_PEER_RATE_INFO,        mtlk_core_get_peer_rate_info);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_CORE_REQ_GET_RECOVERY_STATS,        mtlk_core_get_recovery_stats);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_CORE_REQ_GET_HW_FLOW_STATUS,        mtlk_core_get_hw_flow_status);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_CORE_REQ_GET_TR181_WLAN_STATS,      mtlk_core_get_tr181_wlan_statistics);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_CORE_REQ_GET_TR181_HW_STATS,        mtlk_core_get_tr181_hw_statistics);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_CORE_REQ_GET_TR181_PEER_STATS,      mtlk_core_get_tr181_peer_statistics);
#endif

    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_HW_REQ_SET_DYNAMIC_MU_CFG,          wave_core_cfg_set_dynamic_mu_cfg);
    _MTLK_CORE_HANDLE_REQ_SERIALIZABLE(WAVE_HW_REQ_GET_DYNAMIC_MU_CFG,          wave_core_cfg_get_dynamic_mu_cfg);

  _WAVE_CORE_REQ_MAP_END(id)

#undef _WAVE_CORE_REQ_MAP_START
#undef _MTLK_CORE_HANDLE_REQ_SERIALIZABLE
#undef _WAVE_CORE_REQ_MAP_END
}

static int
_mtlk_core_get_prop (mtlk_vap_handle_t vap_handle, mtlk_core_prop_e prop_id, void* buffer, uint32 size)
{
  int res = MTLK_ERR_NOT_SUPPORTED;
  switch (prop_id) {
  case MTLK_CORE_PROP_MAC_SW_RESET_ENABLED:
    if (buffer && size == sizeof(uint32))
    {
      uint32 *mac_sw_reset_enabled = (uint32 *)buffer;

      *mac_sw_reset_enabled = WAVE_RADIO_PDB_GET_INT(wave_vap_radio_get(vap_handle), PARAM_DB_RADIO_MAC_SOFT_RESET_ENABLE);
      res = MTLK_ERR_OK;
    }
    break;
  case MTLK_CORE_PROP_IS_DUT:
    if (buffer && size == sizeof(BOOL))
    {
      BOOL *val = (BOOL *)buffer;
      *val = mtlk_is_dut_core_active(mtlk_vap_get_core(vap_handle));
      res = MTLK_ERR_OK;
    }
    break;
  case MTLK_CORE_PROP_IS_MAC_FATAL_PENDING:
    if (buffer && size == sizeof(BOOL))
    {
      BOOL *val = (BOOL *)buffer;
      mtlk_hw_t *hw = mtlk_vap_get_hw(vap_handle);
      *val = (wave_rcvry_mac_fatal_pending_get(hw) | wave_rcvry_fw_dump_in_progress_get(hw));
      res = MTLK_ERR_OK;
    }
    break;
  default:
    break;
  }
  return res;
}

static int
_mtlk_core_set_prop (mtlk_vap_handle_t vap_handle,
                    mtlk_core_prop_e  prop_id,
                    void             *buffer,
                    uint32            size)
{
  int res = MTLK_ERR_NOT_SUPPORTED;
  mtlk_core_t *nic = mtlk_vap_get_core (vap_handle);
  switch (prop_id)
  {
  case MTLK_CORE_PROP_MAC_STUCK_DETECTED:
    if (buffer && size == sizeof(uint32))
    {
      uint32 *cpu_no = (uint32 *)buffer;
      nic->slow_ctx->mac_stuck_detected_by_sw = 1;
      mtlk_set_hw_state(nic, MTLK_HW_STATE_APPFATAL);
      (void)mtlk_hw_set_prop(mtlk_vap_get_hwapi(nic->vap_handle), MTLK_HW_RESET, NULL, 0);
      mtlk_df_ui_notify_notify_fw_hang(mtlk_vap_get_df(nic->vap_handle), *cpu_no, MTLK_HW_STATE_APPFATAL);
    }
    break;
  default:
    break;
  }

  return res;
}

void __MTLK_IFUNC
mtlk_core_api_cleanup (mtlk_core_api_t *core_api)
{
  mtlk_core_t* core = HANDLE_T_PTR(mtlk_core_t, core_api->core_handle);
  _mtlk_core_cleanup(core);
  mtlk_fast_mem_free(core);
}

static int
_mtlk_core_get_hw_limits (mtlk_handle_t hcore, const void *data, uint32 data_size)
{
  mtlk_core_t *nic = HANDLE_T_PTR(mtlk_core_t, hcore);
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;
  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  return mtlk_psdb_get_hw_limits_list(nic, clpb);
}

void __MTLK_IFUNC
mtlk_core_get_tx_power_data (mtlk_core_t *core, mtlk_tx_power_data_t *tx_power_data)
{
  wave_radio_t *radio;
  int i;
  uint16 tx_power_offs; /* user set tx power offset in to reduce total tx power */
  mtlk_pdb_size_t pw_limits_size = sizeof(psdb_pw_limits_t);
  unsigned radio_idx;

  MTLK_ASSERT(core);
  MTLK_ASSERT(tx_power_data);

  radio = wave_vap_radio_get(core->vap_handle);
  MTLK_ASSERT(NULL != radio);

  radio_idx = wave_radio_id_get(radio);

  tx_power_offs = WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_TX_POWER_LIMIT_OFFSET);

  mtlk_hw_mhi_get_tx_power(mtlk_vap_get_hw(core->vap_handle), &tx_power_data->power_hw, radio_idx);

  /* All TX power values are in power units */
  tx_power_data->power_usr_offs = tx_power_offs;
  tx_power_data->power_reg = (uint16)WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_TX_POWER_REG_LIM); /* power units */
  tx_power_data->power_psd = (uint16)WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_TX_POWER_PSD); /* power units */
  tx_power_data->power_cfg = (uint16)WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_TX_POWER_CFG) - tx_power_offs; /* power units */

  pw_limits_size = sizeof(psdb_pw_limits_t);
  if (MTLK_ERR_OK !=  WAVE_RADIO_PDB_GET_BINARY(radio, PARAM_DB_RADIO_TPC_PW_LIMITS_PSD, &(tx_power_data->power_tpc_psd), &pw_limits_size)) {
    ELOG_V("Failed to get TPC Power limits PSD array");
  }

  pw_limits_size = sizeof(psdb_pw_limits_t);
  if (MTLK_ERR_OK !=  WAVE_RADIO_PDB_GET_BINARY(radio, PARAM_DB_RADIO_TPC_PW_LIMITS_CFG, &(tx_power_data->power_tpc_cfg), &pw_limits_size)) {
    ELOG_V("Failed to get TPC Power limits Configured array");
  }

  for (i = PSDB_PHY_CW_11B; i <= PSDB_PHY_CW_OFDM_160; i++) {
    tx_power_data->power_tpc_cfg.pw_limits[i] -= tx_power_offs; /* substruct power backoffs for 11b till bw160 */
  }

  tx_power_data->open_loop = (TPC_OPEN_LOOP == WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_TPC_LOOP_TYPE));

  core_cfg_country_code_get(core, &tx_power_data->cur_country_code);
  tx_power_data->cur_band = core_cfg_get_freq_band_cfg(core);
  tx_power_data->cur_chan = _mtlk_core_get_channel(core);

  tx_power_data->cur_cbw = _mtlk_core_get_spectrum_mode(core);
  tx_power_data->max_cbw = (MTLK_HW_BAND_2_4_GHZ == tx_power_data->cur_band) ? CW_MAX_2G :
                            mtlk_hw_type_is_gen5(mtlk_vap_get_hw(core->vap_handle))
                              ? CW_MAX_5G_GEN5 : CW_MAX_5G_GEN6;

  tx_power_data->max_antennas = core->slow_ctx->tx_limits.num_tx_antennas;
  tx_power_data->cur_antennas = _mtlk_core_get_current_tx_antennas(core);

  tx_power_data->max_ant_gain = mtlk_antennas_factor(tx_power_data->max_antennas);
  tx_power_data->cur_ant_gain = mtlk_antennas_factor(tx_power_data->cur_antennas);

  for (i = 0; i < tx_power_data->power_hw.pw_size; i++) {
    tx_power_data->pw_min_brd[i] = tx_power_data->cur_ant_gain + tx_power_data->power_hw.pw_min_ant[i];
    tx_power_data->pw_max_brd[i] = tx_power_data->cur_ant_gain + tx_power_data->power_hw.pw_max_ant[i];
    tx_power_data->pw_targets[i] = hw_mmb_get_tx_power_target(tx_power_data->power_cfg, (uint16)(tx_power_data->cur_ant_gain + tx_power_data->power_hw.pw_max_ant[i]));
    ILOG3_DD("pw_min_ant[%d]=%d", i, tx_power_data->power_hw.pw_min_ant[i]);
    ILOG3_DD("pw_max_ant[%d]=%d", i, tx_power_data->power_hw.pw_max_ant[i]);
  }
}

static int
_mtlk_core_get_tx_rate_power (mtlk_handle_t hcore, const void *data, uint32 data_size)
{
  mtlk_core_t *core = HANDLE_T_PTR(mtlk_core_t, hcore);
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;
  mtlk_tx_power_data_t  tx_power_data;
  int res;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  /* 1. Tx power */
  mtlk_core_get_tx_power_data(core, &tx_power_data);
  if (MTLK_ERR_OK != (res = mtlk_clpb_push(clpb, &tx_power_data, sizeof(tx_power_data)))) {
    goto err_push;
  }

  /* 2. PSDB rate power list */
  return mtlk_psdb_get_rate_power_list(core, clpb);

err_push:
    mtlk_clpb_purge(clpb);
    return res;
}

static int
_mtlk_core_set_mac_assert (mtlk_handle_t hcore, const void* data, uint32 data_size)
{
  int res = MTLK_ERR_NOT_SUPPORTED;
  mtlk_core_t *nic = HANDLE_T_PTR(mtlk_core_t, hcore);
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;
  uint32 clpb_data_size;
  uint32* clpb_data;
  uint32 assert_type;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  clpb_data = mtlk_clpb_enum_get_next(clpb, &clpb_data_size);
  MTLK_CLPB_TRY(clpb_data, clpb_data_size)
    assert_type = *clpb_data;

    WLOG_DD("CID-%04x: Rise MAC assert (assert type=%d)", mtlk_vap_get_oid(nic->vap_handle), assert_type);

    switch (assert_type) {
      case MTLK_CORE_UI_ASSERT_TYPE_ALL_MACS:
        wave_rcvry_type_forced_set(mtlk_vap_get_hw(nic->vap_handle), MTLK_CORE_UI_RCVRY_TYPE_NONE);
        (void)mtlk_hw_set_prop(mtlk_vap_get_hwapi(nic->vap_handle), MTLK_HW_DBG_ASSERT_ALL_MACS, NULL, 0);
        res = MTLK_ERR_OK;
        break;
      case MTLK_CORE_UI_ASSERT_TYPE_FW_LMIPS0:
      case MTLK_CORE_UI_ASSERT_TYPE_FW_LMIPS1:
      case MTLK_CORE_UI_ASSERT_TYPE_FW_UMIPS:
      {
        int mips_no;

        mips_no = hw_assert_type_to_core_nr(mtlk_vap_get_hw(nic->vap_handle), assert_type);
        if (mips_no == -1) {
          ELOG_DD("CID-%04x: Invalid assert type %d", mtlk_vap_get_oid(nic->vap_handle), assert_type);
          return res;
        }
        wave_rcvry_type_forced_set(mtlk_vap_get_hw(nic->vap_handle), MTLK_CORE_UI_RCVRY_TYPE_NONE);
        res = mtlk_hw_set_prop(mtlk_vap_get_hwapi(nic->vap_handle), MTLK_HW_DBG_ASSERT_FW, &mips_no, sizeof(mips_no));
        if (res != MTLK_ERR_OK) {
          ELOG_DDD("CID-%04x: Can't assert FW MIPS#%d (res=%d)", mtlk_vap_get_oid(nic->vap_handle), mips_no, res);
        }
      }
      break;

      case MTLK_CORE_UI_ASSERT_TYPE_DRV_DIV0:
      {
      #ifdef __KLOCWORK__
        abort(1); /* Special case for correct analysis by Klocwork  */
      #else
        volatile int do_bug = 0;
        do_bug = 1/do_bug;
        ILOG0_D("do_bug = %d", do_bug); /* To avoid compilation optimization */
      #endif
        res = MTLK_ERR_OK;
      }
      break;

      case MTLK_CORE_UI_ASSERT_TYPE_DRV_BLOOP:
      #ifdef __KLOCWORK__
        abort(1); /* Special case for correct analysis by Klocwork  */
      #else
        while (1) {;}
      #endif
        break;

      case MTLK_CORE_UI_ASSERT_TYPE_NONE:
      case MTLK_CORE_UI_ASSERT_TYPE_LAST:
      default:
        WLOG_DD("CID-%04x: Unsupported assert type: %d", mtlk_vap_get_oid(nic->vap_handle), assert_type);
        res = MTLK_ERR_NOT_SUPPORTED;
        break;
    }
  MTLK_CLPB_FINALLY(res)
    return res;
  MTLK_CLPB_END
}

static int
_mtlk_core_get_mc_igmp_tbl(mtlk_handle_t hcore, const void* data, uint32 data_size)
{
  int res = MTLK_ERR_NOT_SUPPORTED;
  mtlk_core_t *nic = HANDLE_T_PTR(mtlk_core_t, hcore);
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  res = mtlk_mc_dump_groups(nic, clpb);

  return res;
}

static int
_mtlk_core_get_mc_hw_tbl (mtlk_handle_t hcore, const void* data, uint32 data_size)
{
  int res = MTLK_ERR_NOT_SUPPORTED;
  mtlk_core_t *nic = HANDLE_T_PTR(mtlk_core_t, hcore);
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;
  mtlk_core_ui_mc_grid_action_t req;
  int i;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  for (i = MC_MIN_GROUP; i < MC_GROUPS; i++) {
    req.grp_id = i;
    mtlk_hw_get_prop(mtlk_vap_get_hwapi(nic->vap_handle), MTLK_HW_MC_GROUP_ID, &req, sizeof(req));
    res = mtlk_clpb_push(clpb, &req, sizeof(req));
    if (MTLK_ERR_OK != res) {
      goto err_push;
    }
  }

  goto finish;

err_push:
   mtlk_clpb_purge(clpb);
finish:
  return res;
}

static int
_mtlk_core_get_stadb_sta_by_iter_id (mtlk_handle_t hcore, const void* data, uint32 data_size)
{
  int res = MTLK_ERR_OK;
  mtlk_core_t *nic = HANDLE_T_PTR(mtlk_core_t, hcore);
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;
  st_info_by_idx_data_t *p_info_data;
  uint32 size;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  p_info_data = mtlk_clpb_enum_get_next(clpb, &size);
  MTLK_CLPB_TRY(p_info_data, size)
    if (!wave_core_sync_hostapd_done_get(nic)) {
      ILOG1_D("CID-%04x: HOSTAPD STA database is not synced", mtlk_vap_get_oid(nic->vap_handle));
      MTLK_CLPB_EXIT(MTLK_ERR_NOT_READY);
    }
    /* No need to check res, MAC address will be set to 0 if entry not found */
    mtlk_stadb_get_sta_by_iter_id(&nic->slow_ctx->stadb, p_info_data->idx,
                                  p_info_data->mac, p_info_data->stinfo);
  MTLK_CLPB_FINALLY(res)
    return mtlk_clpb_push_res(clpb, res);
  MTLK_CLPB_END
}

static int
_mtlk_core_get_stadb_sta_list (mtlk_handle_t hcore, const void* data, uint32 data_size)
{
  mtlk_core_t *nic = HANDLE_T_PTR(mtlk_core_t, hcore);
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;
  mtlk_core_ui_get_stadb_status_req_t *get_stadb_status_req;
  uint32 size;
  hst_db *hstdb = NULL;
  uint8 group_cipher = FALSE;
  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  if ( 0 == (mtlk_core_get_net_state(nic) & (NET_STATE_HALTED | NET_STATE_CONNECTED)) ) {
    mtlk_clpb_purge(clpb);
    return MTLK_ERR_OK;
  }

  get_stadb_status_req = mtlk_clpb_enum_get_next(clpb, &size);
  if ( (NULL == get_stadb_status_req) || (sizeof(*get_stadb_status_req) != size) ) {
    ELOG_SD("Failed to get data from clipboard in function %s, line %d", __FUNCTION__, __LINE__);
    return MTLK_ERR_UNKNOWN;
  }

  if (get_stadb_status_req->get_hostdb) {
    hstdb = &nic->slow_ctx->hstdb;
  }

  if (get_stadb_status_req->use_cipher) {
    group_cipher = nic->slow_ctx->group_cipher;
  }

  mtlk_clpb_purge(clpb);
  return mtlk_stadb_get_stat(&nic->slow_ctx->stadb, hstdb, clpb, group_cipher);
}

static int
_mtlk_core_get_ee_caps(mtlk_handle_t hcore, const void* data, uint32 data_size)
{
  int res = MTLK_ERR_OK;
  mtlk_core_t *nic = HANDLE_T_PTR(mtlk_core_t, hcore);
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;
  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  res = mtlk_eeprom_get_caps(mtlk_core_get_eeprom(nic), clpb);

  return res;
}

mtlk_core_t * __MTLK_IFUNC
mtlk_core_get_master (mtlk_core_t *core)
{
  MTLK_ASSERT(core != NULL);

  return mtlk_vap_manager_get_master_core(mtlk_vap_get_manager(core->vap_handle));
}

uint8 __MTLK_IFUNC mtlk_core_is_device_busy(mtlk_handle_t context)
{
    return FALSE;
}

tx_limit_t* __MTLK_IFUNC
mtlk_core_get_tx_limits(mtlk_core_t *core)
{
  return &core->slow_ctx->tx_limits;
}

void
mtlk_core_configuration_dump(mtlk_core_t *core)
{
  mtlk_country_code_t   country_code;
  wave_radio_t *radio = wave_vap_radio_get(core->vap_handle);

  MTLK_UNREFERENCED_PARAM(radio);

  core_cfg_country_code_get(core, &country_code);

  ILOG0_DS("CID-%04x: Country             : %s", mtlk_vap_get_oid(core->vap_handle), country_code.country);
  ILOG0_DD("CID-%04x: Domain              : %u", mtlk_vap_get_oid(core->vap_handle), mtlk_psdb_country_to_regd_code(country_code.country));
  ILOG0_DS("CID-%04x: Network mode        : %s", mtlk_vap_get_oid(core->vap_handle), net_mode_to_string(core_cfg_get_network_mode_cfg(core)));
  ILOG0_DS("CID-%04x: Band                : %s", mtlk_vap_get_oid(core->vap_handle), mtlk_eeprom_band_to_string(net_mode_to_band(core_cfg_get_network_mode_cfg(core))));
  ILOG0_DS("CID-%04x: Prog Model Spectrum : %s MHz", mtlk_vap_get_oid(core->vap_handle), mtlkcw2str(WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_PROG_MODEL_SPECTRUM_MODE)));
  ILOG0_DS("CID-%04x: Spectrum            : %s MHz", mtlk_vap_get_oid(core->vap_handle), mtlkcw2str(WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_SPECTRUM_MODE)));
  ILOG0_DS("CID-%04x: Bonding             : %s", mtlk_vap_get_oid(core->vap_handle),  WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_BONDING_MODE) == ALTERNATE_UPPER ? "upper" : (WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_BONDING_MODE) == ALTERNATE_LOWER ? "lower" : "none"));
  ILOG0_DS("CID-%04x: HT mode             : %s", mtlk_vap_get_oid(core->vap_handle),  core_cfg_get_is_ht_cur(core) ? "enabled" : "disabled");
  ILOG0_DS("CID-%04x: SM enabled          : %s", mtlk_vap_get_oid(core->vap_handle),
      mtlk_eeprom_get_disable_sm_channels(mtlk_core_get_eeprom(core)) ? "disabled" : "enabled");
}

static void _mtlk_core_prepare_stop(mtlk_vap_handle_t vap_handle)
{
  mtlk_core_t *nic = mtlk_vap_get_core (vap_handle);

  if (!nic->is_stopped) {
    if (MTLK_ERR_OK != __mtlk_core_deactivate(nic, nic)) {
      ELOG_D("CID-%04x: Core deactivation is failed", mtlk_vap_get_oid(vap_handle));
    }
  }

  ILOG1_V("Core prepare stopping....");
  mtlk_osal_lock_acquire(&nic->net_state_lock);
  nic->is_stopping = TRUE;
  mtlk_osal_lock_release(&nic->net_state_lock);

  if (mtlk_vap_is_slave_ap(vap_handle)) {
    return;
  }
}

BOOL __MTLK_IFUNC
mtlk_core_is_stopping(mtlk_core_t *core)
{
  return (core->is_stopping || core->is_iface_stopping);
}

void
mtlk_core_bswap_bcl_request (UMI_BCL_REQUEST *req, BOOL hdr_only)
{
  int i;

  req->Size    = cpu_to_le32(req->Size);
  req->Address = cpu_to_le32(req->Address);
  req->Unit    = cpu_to_le32(req->Unit);

  if (!hdr_only) {
    for (i = 0; i < ARRAY_SIZE(req->Data); i++) {
      req->Data[i] = cpu_to_le32(req->Data[i]);
    }
  }
}

static int
_mtlk_core_bcl_mac_data_get (mtlk_handle_t hcore, const void* data, uint32 data_size)
{
  int res = MTLK_ERR_OK;
  mtlk_core_t *core = HANDLE_T_PTR(mtlk_core_t, hcore);
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;
  mtlk_txmm_msg_t   man_msg;
  mtlk_txmm_data_t* man_entry = NULL;
  int exception;
  UMI_BCL_REQUEST* preq;
  BOOL f_bswap_data = TRUE;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  /* Get BCL request from CLPB */
  preq = mtlk_clpb_enum_get_next(clpb, NULL);
  if (NULL == preq) {
    ELOG_SD("Failed to get data from clipboard in function %s, line %d", __FUNCTION__, __LINE__);
    return MTLK_ERR_UNKNOWN;
  }

  /* Check MAC state */
  exception = (mtlk_core_is_hw_halted(core) &&
               !core->slow_ctx->mac_stuck_detected_by_sw);

  /* if Core got here preq->Unit field wiath value greater or equal to BCL_UNIT_MAX -
   * the Core should not convert result data words in host format. */
  if (preq->Unit >= BCL_UNIT_MAX) {
    preq->Unit -= BCL_UNIT_MAX; /*Restore original field value*/
    f_bswap_data = FALSE;
  }

  ILOG4_SDDDD("Getting BCL over %s unit(%d) address(0x%x) size(%u) (%x)",
       exception ? "io" : "txmm",
       (int)preq->Unit,
       (unsigned int)preq->Address,
       (unsigned int)preq->Size,
       (unsigned int)preq->Data[0]);

  if (exception)
  {
    /* MAC is halted - send BCL request through IO */
    mtlk_core_bswap_bcl_request(preq, TRUE);

    res = mtlk_hw_get_prop(mtlk_vap_get_hwapi(core->vap_handle), MTLK_HW_BCL_ON_EXCEPTION, preq, sizeof(*preq));

    if (MTLK_ERR_OK != res) {
      ELOG_D("CID-%04x: Can't get BCL", mtlk_vap_get_oid(core->vap_handle));
      goto err_push;
    }
  }
  else
  {
    /* MAC is in normal state - send BCL request through TXMM */
    man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, mtlk_vap_get_txmm(core->vap_handle), NULL);
    if (!man_entry) {
      ELOG_D("CID-%04x: Can't send Get BCL request to MAC due to the lack of MAN_MSG", mtlk_vap_get_oid(core->vap_handle));
      res = MTLK_ERR_NO_RESOURCES;
      goto err_push;
    }

    mtlk_core_bswap_bcl_request(preq, TRUE);

    *((UMI_BCL_REQUEST*)man_entry->payload) = *preq;
    man_entry->id           = UM_MAN_QUERY_BCL_VALUE;
    man_entry->payload_size = sizeof(*preq);

    res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);

    if (MTLK_ERR_OK != res) {
      ELOG_D("CID-%04x: Can't send Get BCL request to MAC, timed-out", mtlk_vap_get_oid(core->vap_handle));
      mtlk_txmm_msg_cleanup(&man_msg);
      goto err_push;
    }

    /* Copy back results */
    *preq = *((UMI_BCL_REQUEST*)man_entry->payload);
    mtlk_txmm_msg_cleanup(&man_msg);
  }

  /* Send back results */
  mtlk_core_bswap_bcl_request(preq, !f_bswap_data);

  mtlk_dump(3, preq, sizeof(*preq), "dump of the UM_MAN_QUERY_BCL_VALUE");

  res = mtlk_clpb_push(clpb, preq, sizeof(*preq));
  if (MTLK_ERR_OK == res) {
    return res;
  }

err_push:
  mtlk_clpb_purge(clpb);
  return res;
}

static int
_mtlk_core_bcl_mac_data_set (mtlk_handle_t hcore, const void* data, uint32 data_size)
{
  int res = MTLK_ERR_OK;
  mtlk_core_t *core = HANDLE_T_PTR(mtlk_core_t, hcore);
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;
  mtlk_txmm_msg_t   man_msg;
  mtlk_txmm_data_t* man_entry = NULL;
  int exception;
  uint32 req_size;
  UMI_BCL_REQUEST* preq = NULL;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  /* Read Set BCL request from CLPB */
  preq = mtlk_clpb_enum_get_next(clpb, &req_size);
  MTLK_CLPB_TRY(preq, req_size)
    /* Check MAC state */
    exception = (mtlk_core_is_hw_halted(core) &&
                 !core->slow_ctx->mac_stuck_detected_by_sw);

    ILOG2_SDDDD("Setting BCL over %s unit(%d) address(0x%x) size(%u) (%x)",
         exception ? "io" : "txmm",
         (int)preq->Unit,
         (unsigned int)preq->Address,
         (unsigned int)preq->Size,
         (unsigned int)preq->Data[0]);

    mtlk_dump(3, preq, sizeof(*preq), "dump of the UM_MAN_SET_BCL_VALUE");

    if (exception)
    {
      /* MAC is halted - send BCL request through IO */
      mtlk_core_bswap_bcl_request(preq, FALSE);

      res = mtlk_hw_set_prop(mtlk_vap_get_hwapi(core->vap_handle), MTLK_HW_BCL_ON_EXCEPTION, preq, sizeof(*preq));
      if (MTLK_ERR_OK != res) {
        ELOG_D("CID-%04x: Can't set BCL", mtlk_vap_get_oid(core->vap_handle));
        MTLK_CLPB_EXIT(res);
      }
    }
    else
    {
      /* MAC is in normal state - send BCL request through TXMM */
       man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, mtlk_vap_get_txmm(core->vap_handle), NULL);
       if (!man_entry) {
         ELOG_D("CID-%04x: Can't send Set BCL request to MAC due to the lack of MAN_MSG", mtlk_vap_get_oid(core->vap_handle));
         MTLK_CLPB_EXIT(MTLK_ERR_NO_RESOURCES);
       }

       mtlk_core_bswap_bcl_request(preq, FALSE);

       *((UMI_BCL_REQUEST*)man_entry->payload) = *preq;
       man_entry->id           = UM_MAN_SET_BCL_VALUE;
       man_entry->payload_size = sizeof(*preq);

       res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);

       if (MTLK_ERR_OK != res) {
         ELOG_D("CID-%04x: Can't send Set BCL request to MAC, timed-out", mtlk_vap_get_oid(core->vap_handle));
       }

       mtlk_txmm_msg_cleanup(&man_msg);
    }
  MTLK_CLPB_FINALLY(res)
    return res;
  MTLK_CLPB_END
}

static int
_mtlk_general_core_range_info_get (mtlk_core_t* nic, const void* data, uint32 data_size)
{
  int res = MTLK_ERR_OK;
  mtlk_core_ui_range_info_t range_info;
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;
  wave_radio_t *radio = wave_vap_radio_get(nic->vap_handle);

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);
  MTLK_ASSERT(!mtlk_vap_is_slave_ap(nic->vap_handle));

  /* Get supported bitrates */
  {
    int avail = mtlk_core_get_available_bitrates(nic);
    int32 short_cyclic_prefix = WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_SHORT_CYCLIC_PREFIX);
    int num_bitrates; /* Index in table returned to userspace */
    int value; /* Bitrate's value */
    int i; /* Bitrate index */
    int k, l; /* Counters, used for sorting */
    int sm = WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_SPECTRUM_MODE);

    /* Array of bitrates is sorted and consist of only unique elements */
    num_bitrates = 0;
    for (i = BITRATE_FIRST; i <= BITRATE_LAST; i++) {
      if ((1 << i) & avail) {
        value = mtlk_bitrate_get_value(i, sm, short_cyclic_prefix);
        if (MTLK_BITRATE_INVALID == value) {
            ILOG1_DDD("Rate is not supported: index %d, spectrum mode %d, scp %d", i, sm, short_cyclic_prefix);
            continue;   /* is not supported  (e.g in AC mode) */
        }

        range_info.bitrates[num_bitrates] = value;
        k = num_bitrates;
        while (k && (range_info.bitrates[k-1] >= value)) k--; /* Position found */
        if ((k == num_bitrates) || (range_info.bitrates[k] != value)) {
          for (l = num_bitrates; l > k; l--)
            range_info.bitrates[l] = range_info.bitrates[l-1];
          range_info.bitrates[k] = value;
          num_bitrates++;
        }
      }
    }

    range_info.num_bitrates = num_bitrates;
  }

  res = mtlk_clpb_push(clpb, &range_info, sizeof(range_info));
  if (MTLK_ERR_OK != res) {
    mtlk_clpb_purge(clpb);
  }

  return res;
}

static int
_mtlk_slave_core_range_info_get (mtlk_core_t* nic, const void* data, uint32 data_size)
{
  return (_mtlk_general_core_range_info_get (mtlk_core_get_master (nic), data, data_size));
}

static int
_mtlk_core_range_info_get (mtlk_handle_t hcore, const void* data, uint32 data_size)
{
  mtlk_core_t *core = HANDLE_T_PTR(mtlk_core_t, hcore);
  if (!mtlk_vap_is_slave_ap (core->vap_handle)) {
    return _mtlk_general_core_range_info_get (core, data, data_size);
  }
  else
    return _mtlk_slave_core_range_info_get (core, data, data_size);
}

static int
_mtlk_core_get_enc_ext_cfg (mtlk_handle_t hcore, const void* data, uint32 data_size)
{
  int                       res = MTLK_ERR_OK;
  mtlk_core_t               *nic = HANDLE_T_PTR(mtlk_core_t, hcore);
  mtlk_clpb_t               *clpb = *(mtlk_clpb_t **) data;
  uint32                    size;
  mtlk_txmm_t               *txmm = mtlk_vap_get_txmm(nic->vap_handle);
  mtlk_txmm_msg_t           man_msg;
  mtlk_txmm_data_t          *man_entry = NULL;
  mtlk_core_ui_group_pn_t   rsc_result;
  mtlk_core_ui_group_pn_t   *prsc_requset;
  UMI_GROUP_PN              *umi_gpn;
  uint32                    key_index;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  if (!wave_core_sync_hostapd_done_get(nic)) {
    ILOG1_D("CID-%04x: HOSTAPD STA database is not synced", mtlk_vap_get_oid(nic->vap_handle));
    res = MTLK_ERR_NOT_READY;
    goto FINISH;
  }

  if (0 != (mtlk_core_get_net_state(nic) & (NET_STATE_HALTED | NET_STATE_IDLE))) {
    ILOG1_D("CID-%04x: Invalid card state - request rejected", mtlk_vap_get_oid(nic->vap_handle));
    res = MTLK_ERR_NOT_READY;
    goto FINISH;
  }

  if (mtlk_core_scan_is_running(nic)) {
    ILOG1_D("CID-%04x: Can't get WEP configuration - scan in progress", mtlk_vap_get_oid(nic->vap_handle));
    res = MTLK_ERR_NOT_READY;
    goto FINISH;
  }

  man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, txmm, NULL);
  if (NULL == man_entry) {
    ELOG_D("CID-%04x: No man entry available", mtlk_vap_get_oid(nic->vap_handle));
    res = MTLK_ERR_NO_RESOURCES;
    goto FINISH;
  }

  prsc_requset = mtlk_clpb_enum_get_next(clpb, &size);
  if ( (NULL == prsc_requset) || (sizeof(mtlk_core_ui_group_pn_t) != size) ) {
    ELOG_D("CID-%04x: Failed to set authorizing configuration parameters from CLPB", mtlk_vap_get_oid(nic->vap_handle));
    res = MTLK_ERR_UNKNOWN;
    goto FINISH;
  }

  umi_gpn = (UMI_GROUP_PN*)man_entry->payload;
  memset(umi_gpn, 0, sizeof(UMI_GROUP_PN));

  man_entry->id           = UM_MAN_GET_GROUP_PN_REQ;
  man_entry->payload_size = sizeof(UMI_GROUP_PN);

  res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);
  if (MTLK_ERR_OK != res) {
    ELOG_D("CID-%04x: Timeout expired while waiting for CFM from MAC", mtlk_vap_get_oid(nic->vap_handle));
    goto FINISH;
  }

  umi_gpn->u16Status = le16_to_cpu(umi_gpn->u16Status);
  if (UMI_OK != umi_gpn->u16Status) {
    ELOG_DD("CID-%04x: GET_GROUP_PN_REQ failed: %u", mtlk_vap_get_oid(nic->vap_handle), umi_gpn->u16Status);
    res = MTLK_ERR_NOT_READY;
    goto FINISH;
  }

  mtlk_dump(3, umi_gpn->au8TxSeqNum, UMI_RSN_SEQ_NUM_LEN, "GROUP RSC");

  memset(&rsc_result, 0, sizeof(mtlk_core_ui_group_pn_t));
  wave_memcpy(rsc_result.seq, sizeof(rsc_result.seq), umi_gpn->au8TxSeqNum, UMI_RSN_SEQ_NUM_LEN);
  rsc_result.seq_len = CORE_KEY_SEQ_LEN;
  key_index = prsc_requset->key_idx;
  wave_memcpy(rsc_result.key, sizeof(rsc_result.key),
      nic->slow_ctx->keys[key_index].key, nic->slow_ctx->keys[key_index].key_len);
  rsc_result.key_len =  nic->slow_ctx->keys[key_index].key_len ;

  res = mtlk_clpb_push(clpb, &rsc_result, sizeof(rsc_result));
  if (MTLK_ERR_OK != res) {
    mtlk_clpb_purge(clpb);
  }

FINISH:
  if (man_entry) {
    mtlk_txmm_msg_cleanup(&man_msg);
  }
  return res;
}

static void
_mtlk_core_set_rx_seq(struct nic *nic, uint16 idx, uint8* rx_seq)
{
  nic->slow_ctx->group_rsc[idx][0] = rx_seq[5];
  nic->slow_ctx->group_rsc[idx][1] = rx_seq[4];
  nic->slow_ctx->group_rsc[idx][2] = rx_seq[3];
  nic->slow_ctx->group_rsc[idx][3] = rx_seq[2];
  nic->slow_ctx->group_rsc[idx][4] = rx_seq[1];
  nic->slow_ctx->group_rsc[idx][5] = rx_seq[0];
}

static int
_mtlk_core_set_enc_ext_cfg (mtlk_handle_t hcore, const void* data, uint32 data_size)
{
  int                       res = MTLK_ERR_OK;
  mtlk_core_ui_encext_cfg_t *encext_cfg;
  uint16                    key_indx;
  mtlk_core_t               *core = HANDLE_T_PTR(mtlk_core_t, hcore);
  mtlk_clpb_t               *clpb = *(mtlk_clpb_t **) data;
  uint32                    size;
  mtlk_txmm_msg_t           man_msg;
  mtlk_txmm_data_t          *man_entry = NULL;
  mtlk_txmm_t               *txmm = mtlk_vap_get_txmm(core->vap_handle);

  UMI_SET_KEY               *umi_key;
  uint16                    alg_type = IW_ENCODE_ALG_NONE;
  uint16                    key_len = 0;
  sta_entry                 *sta = NULL;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  if ((mtlk_core_get_net_state(core) & (NET_STATE_READY | NET_STATE_ACTIVATING | NET_STATE_CONNECTED)) == 0) {
    ILOG1_D("CID-%04x: Invalid card state - request rejected", mtlk_vap_get_oid(core->vap_handle));
    res = MTLK_ERR_NOT_READY;
    goto FINISH;
  }

  if (mtlk_core_is_stopping(core)) {
    ILOG1_D("CID-%04x: Can't set ENC_EXT configuration - core is stopping", mtlk_vap_get_oid(core->vap_handle));
    res = MTLK_ERR_NOT_READY;
    goto FINISH;
  }

  encext_cfg = mtlk_clpb_enum_get_next(clpb, &size);
  if ( (NULL == encext_cfg) || (sizeof(*encext_cfg) != size) ) {
    ELOG_D("CID-%04x: Failed to get ENC_EXT configuration parameters from CLPB", mtlk_vap_get_oid(core->vap_handle));
    res = MTLK_ERR_UNKNOWN;
    goto FINISH;
  }

  /* Prepare UMI message */
  man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, txmm, NULL);
  if (!man_entry) {
    ELOG_D("CID-%04x: No man entry available", mtlk_vap_get_oid(core->vap_handle));
    res = MTLK_ERR_NO_RESOURCES;
    goto FINISH;
  }

  umi_key = (UMI_SET_KEY*)man_entry->payload;
  memset(umi_key, 0, sizeof(*umi_key));

  man_entry->id           = UM_MAN_SET_KEY_REQ;
  man_entry->payload_size = sizeof(*umi_key);

  key_len = encext_cfg->key_len;
  if(0 == key_len) {
    ELOG_D("CID-%04x: No key is set", mtlk_vap_get_oid(core->vap_handle));
    res = MTLK_ERR_PARAMS;
    goto FINISH;
  }

  key_indx = encext_cfg->key_idx;

  /* Set Ciper Suite */
  alg_type = encext_cfg->alg_type;
  switch (alg_type) {
    case IW_ENCODE_ALG_WEP:
      if(MIB_WEP_KEY_WEP2_LENGTH == key_len) { /* 104 bit */
        umi_key->u16CipherSuite = HOST_TO_MAC16(UMI_RSN_CIPHER_SUITE_WEP104);
      }
      else if (MIB_WEP_KEY_WEP1_LENGTH == key_len) { /* 40 bit */
        umi_key->u16CipherSuite = HOST_TO_MAC16(UMI_RSN_CIPHER_SUITE_WEP40);
      }
      else {
        ELOG_DD("CID-%04x: Wrong WEP key lenght %d", mtlk_vap_get_oid(core->vap_handle), key_len);
        res = MTLK_ERR_PARAMS;
        goto FINISH;
      }
      break;
    case IW_ENCODE_ALG_TKIP:
      umi_key->u16CipherSuite = HOST_TO_MAC16(UMI_RSN_CIPHER_SUITE_TKIP);
      break;
    case IW_ENCODE_ALG_CCMP:
      umi_key->u16CipherSuite = HOST_TO_MAC16(UMI_RSN_CIPHER_SUITE_CCMP);
      break;
    case IW_ENCODE_ALG_AES_CMAC:
      umi_key->u16CipherSuite = HOST_TO_MAC16(UMI_RSN_CIPHER_SUITE_BIP);
      break;
    case IW_ENCODE_ALG_GCMP:
      umi_key->u16CipherSuite = HOST_TO_MAC16(UMI_RSN_CIPHER_SUITE_GCMP128);
      break;
    case IW_ENCODE_ALG_GCMP_256:
      umi_key->u16CipherSuite = HOST_TO_MAC16(UMI_RSN_CIPHER_SUITE_GCMP256);
      break;
    default:
      ELOG_D("CID-%04x: Unknown CiperSuite", mtlk_vap_get_oid(core->vap_handle));
      res = MTLK_ERR_PARAMS;
      goto FINISH;
  }

  /* Set key type */
  if (mtlk_osal_eth_is_broadcast(encext_cfg->sta_addr.au8Addr)) {
    umi_key->u16KeyType = HOST_TO_MAC16(UMI_RSN_GROUP_KEY);
    core->slow_ctx->group_cipher = alg_type;

    /* update group replay counter */
    _mtlk_core_set_rx_seq(core, key_indx, encext_cfg->rx_seq);
    umi_key->u16Sid = HOST_TO_MAC16(TMP_BCAST_DEST_ADDR);
  }
  else {
    /* Check STA availability */
    sta = mtlk_stadb_find_sta(&core->slow_ctx->stadb, encext_cfg->sta_addr.au8Addr);
    if (NULL == sta) {
      ILOG1_Y("There is no connection with %Y", encext_cfg->sta_addr.au8Addr);
      res = MTLK_ERR_PARAMS;
      goto FINISH;
    }

    umi_key->u16Sid = HOST_TO_MAC16(mtlk_sta_get_sid(sta));
    umi_key->u16KeyType = HOST_TO_MAC16(UMI_RSN_PAIRWISE_KEY);
  }

  /* The key has been copied into au8Tk1 array with UMI_RSN_TK1_LEN size.
   * But key can have UMI_RSN_TK1_LEN+UMI_RSN_TK2_LEN size - so
   * actually second part of key is copied into au8Tk2 array */
  wave_memcpy(umi_key->au8Tk, sizeof(umi_key->au8Tk), encext_cfg->key, key_len);

  if(sta){
    mtlk_sta_set_cipher(sta, alg_type);
  }

  /* set TX sequence number */
  umi_key->au8TxSeqNum[0] = 1;
  umi_key->u16KeyIndex = HOST_TO_MAC16(key_indx);

  wave_memcpy(umi_key->au8RxSeqNum, sizeof(umi_key->au8RxSeqNum), encext_cfg->rx_seq, sizeof(encext_cfg->rx_seq));

  ILOG1_D("UMI_SET_KEY             SID:0x%x", MAC_TO_HOST16(umi_key->u16Sid));
  ILOG1_D("UMI_SET_KEY         KeyType:0x%x", MAC_TO_HOST16(umi_key->u16KeyType));
  ILOG1_D("UMI_SET_KEY  u16CipherSuite:0x%x", MAC_TO_HOST16(umi_key->u16CipherSuite));
  ILOG1_D("UMI_SET_KEY        KeyIndex:%d", MAC_TO_HOST16(umi_key->u16KeyIndex));
  mtlk_dump(1, umi_key->au8RxSeqNum, sizeof(umi_key->au8RxSeqNum), "RxSeqNum");
  mtlk_dump(1, umi_key->au8TxSeqNum, sizeof(umi_key->au8TxSeqNum), "TxSeqNum");
  mtlk_dump(1, umi_key->au8Tk, key_len, "KEY:");

  res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);
  if (MTLK_ERR_OK != res) {
    ELOG_DD("CID-%04x: mtlk_mm_send_blocked failed: %i", mtlk_vap_get_oid(core->vap_handle), res);
    goto FINISH;
  }

  /* Store the key */
  wave_memcpy(core->slow_ctx->keys[key_indx].key, sizeof(core->slow_ctx->keys[key_indx].key),
      encext_cfg->key, key_len);
  core->slow_ctx->keys[key_indx].key_len = key_len;
  memset(core->slow_ctx->keys[key_indx].seq, 0, CORE_KEY_SEQ_LEN);
  wave_memcpy(core->slow_ctx->keys[key_indx].seq, sizeof(core->slow_ctx->keys[key_indx].seq),
      encext_cfg->rx_seq, sizeof(encext_cfg->rx_seq));
  core->slow_ctx->keys[key_indx].seq_len =  CORE_KEY_SEQ_LEN;
  core->slow_ctx->keys[key_indx].cipher =  MAC_TO_HOST16(umi_key->u16CipherSuite);

FINISH:
  if (NULL != sta) {
    mtlk_sta_decref(sta); /* De-reference of find */
  }

  if (NULL != man_entry) {
    mtlk_txmm_msg_cleanup(&man_msg);
  }

  return res;
}

static int
_mtlk_core_set_default_key_cfg (mtlk_handle_t hcore, const void* data, uint32 data_size)
{
  int                       res = MTLK_ERR_OK;
  mtlk_core_ui_default_key_cfg_t *default_key_cfg;
  mtlk_core_t               *core = HANDLE_T_PTR(mtlk_core_t, hcore);
  mtlk_clpb_t               *clpb = *(mtlk_clpb_t **) data;
  uint32                    size;
  mtlk_txmm_msg_t           man_msg;
  mtlk_txmm_data_t          *man_entry = NULL;
  mtlk_txmm_t               *txmm = mtlk_vap_get_txmm(core->vap_handle);
  UMI_DEFAULT_KEY_INDEX     *umi_default_key;
  sta_entry                 *sta = NULL;
  uint16                    key_indx;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  if (!wave_core_sync_hostapd_done_get(core)) {
    ILOG1_D("CID-%04x: HOSTAPD STA database is not synced", mtlk_vap_get_oid(core->vap_handle));
    res = MTLK_ERR_NOT_READY;
    goto FINISH;
  }

  default_key_cfg = mtlk_clpb_enum_get_next(clpb, &size);
  if ( (NULL == default_key_cfg) || (sizeof(*default_key_cfg) != size) ) {
    ELOG_D("CID-%04x: Failed to get ENC_EXT configuration parameters from CLPB", mtlk_vap_get_oid(core->vap_handle));
    res = MTLK_ERR_UNKNOWN;
    goto FINISH;
  }

  /* Prepare UMI message */
  man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, txmm, NULL);
  if (!man_entry) {
    ELOG_D("CID-%04x: No man entry available", mtlk_vap_get_oid(core->vap_handle));
    res = MTLK_ERR_NO_RESOURCES;
    goto FINISH;
  }

  man_entry->id           = UM_MAN_SET_DEFAULT_KEY_INDEX_REQ;
  man_entry->payload_size = sizeof(UMI_DEFAULT_KEY_INDEX);
  umi_default_key = (UMI_DEFAULT_KEY_INDEX *)man_entry->payload;
  memset(umi_default_key, 0, sizeof(*umi_default_key));

  if (mtlk_osal_eth_is_broadcast(default_key_cfg->sta_addr.au8Addr)) {
    umi_default_key->u16SID     = HOST_TO_MAC16(TMP_BCAST_DEST_ADDR);
    if (default_key_cfg->is_mgmt_key)
      umi_default_key->u16KeyType = HOST_TO_MAC16(UMI_RSN_MGMT_GROUP_KEY);
    else
      umi_default_key->u16KeyType = HOST_TO_MAC16(UMI_RSN_GROUP_KEY);
  }
  else {
    /* Check STA availability */
    sta = mtlk_stadb_find_sta(&core->slow_ctx->stadb, default_key_cfg->sta_addr.au8Addr);
    if (NULL == sta) {
      ILOG1_Y("There is no connection with %Y", default_key_cfg->sta_addr.au8Addr);
      res = MTLK_ERR_PARAMS;
      goto FINISH;
    }

    umi_default_key->u16SID     = HOST_TO_MAC16(mtlk_sta_get_sid(sta));
    umi_default_key->u16KeyType = HOST_TO_MAC16(UMI_RSN_PAIRWISE_KEY);
  }

  key_indx = default_key_cfg->key_idx;
  umi_default_key->u16KeyIndex = HOST_TO_MAC16(key_indx);

  ILOG2_D("UMI_SET_DEFAULT_KEY             SID:0x%x", MAC_TO_HOST16(umi_default_key->u16SID));
  ILOG2_D("UMI_SET_DEFAULT_KEY         KeyType:0x%x", MAC_TO_HOST16(umi_default_key->u16KeyType));
  ILOG2_D("UMI_SET_DEFAULT_KEY        KeyIndex:%d", MAC_TO_HOST16(umi_default_key->u16KeyIndex));

  res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);
  if (MTLK_ERR_OK != res) {
    ELOG_DD("CID-%04x: mtlk_mm_send_blocked failed: %i", mtlk_vap_get_oid(core->vap_handle), res);
    goto FINISH;
  }

  /* Store default key */
  if (default_key_cfg->is_mgmt_key) {
    core->slow_ctx->default_mgmt_key = key_indx;
  }
  else {
    core->slow_ctx->default_key = key_indx;
  }

FINISH:
  if (NULL != sta) {
    mtlk_sta_decref(sta); /* De-reference of find */
  }

  if (NULL != man_entry) {
    mtlk_txmm_msg_cleanup(&man_msg);
  }

  return res;
}

static int
core_recovery_default_key(mtlk_core_t *core, BOOL is_mgmt_key)
{
  int res;
  mtlk_txmm_msg_t           man_msg;
  mtlk_txmm_data_t          *man_entry = NULL;
  mtlk_txmm_t               *txmm = mtlk_vap_get_txmm(core->vap_handle);
  UMI_DEFAULT_KEY_INDEX     *umi_default_key;
  uint16                    alg_type;

  alg_type = core->slow_ctx->group_cipher;
  if (alg_type == IW_ENCODE_ALG_NONE) {
    ILOG2_D("CID-%04x: ENCODE ALG is NONE, ignore set default key index.", mtlk_vap_get_oid(core->vap_handle));
    res = MTLK_ERR_OK;
    goto FINISH;
  }

  /* Prepare UMI message */
  man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, txmm, NULL);
  if (!man_entry) {
    ELOG_D("CID-%04x: No man entry available", mtlk_vap_get_oid(core->vap_handle));
    res = MTLK_ERR_NO_RESOURCES;
    goto FINISH;
  }

  man_entry->id           = UM_MAN_SET_DEFAULT_KEY_INDEX_REQ;
  man_entry->payload_size = sizeof(UMI_DEFAULT_KEY_INDEX);
  umi_default_key = (UMI_DEFAULT_KEY_INDEX *)man_entry->payload;
  memset(umi_default_key, 0, sizeof(*umi_default_key));

  umi_default_key->u16SID     = HOST_TO_MAC16(TMP_BCAST_DEST_ADDR);
  if (is_mgmt_key) {
    umi_default_key->u16KeyType = HOST_TO_MAC16(UMI_RSN_MGMT_GROUP_KEY);
    umi_default_key->u16KeyIndex = HOST_TO_MAC16(core->slow_ctx->default_mgmt_key);
  }
  else {
    umi_default_key->u16KeyType = HOST_TO_MAC16(UMI_RSN_GROUP_KEY);
    umi_default_key->u16KeyIndex = HOST_TO_MAC16(core->slow_ctx->default_key);
  }


  ILOG2_D("UMI_SET_DEFAULT_KEY             SID:0x%x", MAC_TO_HOST16(umi_default_key->u16SID));
  ILOG2_D("UMI_SET_DEFAULT_KEY         KeyType:0x%x", MAC_TO_HOST16(umi_default_key->u16KeyType));
  ILOG2_D("UMI_SET_DEFAULT_KEY        KeyIndex:%d", MAC_TO_HOST16(umi_default_key->u16KeyIndex));

  res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);
  if (MTLK_ERR_OK != res) {
    ELOG_DD("CID-%04x: mtlk_mm_send_blocked failed: %i", mtlk_vap_get_oid(core->vap_handle), res);
    goto FINISH;
  }

FINISH:
  if (NULL != man_entry) {
    mtlk_txmm_msg_cleanup(&man_msg);
  }

  return res;
}

/* Aggregation configuration */
static int
_mtlk_core_receive_agg_rate_cfg (mtlk_core_t *core,  mtlk_core_aggr_cfg_t *aggr_cfg)
{
  int res;
  mtlk_txmm_msg_t       man_msg;
  mtlk_txmm_data_t     *man_entry = NULL;
  UMI_TS_VAP_CONFIGURE *pAggrConfig = NULL;
  mtlk_vap_handle_t     vap_handle = core->vap_handle;

  man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, mtlk_vap_get_txmm(vap_handle), NULL);
  if (NULL == man_entry) {
    ELOG_D("CID-%04x: No man entry available", mtlk_vap_get_oid(vap_handle));
    return MTLK_ERR_NO_RESOURCES;
  }

  man_entry->id = UM_MAN_TS_VAP_CONFIGURE_REQ;
  man_entry->payload_size = sizeof(UMI_TS_VAP_CONFIGURE);

  pAggrConfig = (UMI_TS_VAP_CONFIGURE *)(man_entry->payload);
  pAggrConfig->vapId = (uint8)mtlk_vap_get_id(vap_handle);
  pAggrConfig->getSetOperation = API_GET_OPERATION;

  res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);
  if (MTLK_ERR_OK == res) {
    aggr_cfg->ba_mode    = pAggrConfig->enableBa;
    aggr_cfg->amsdu_mode = pAggrConfig->amsduSupport;
    aggr_cfg->windowSize = MAC_TO_HOST32(pAggrConfig->windowSize);
  }
  else {
    ELOG_D("CID-%04x: Failed to receive UM_MAN_TS_VAP_CONFIGURE_REQ", mtlk_vap_get_oid(vap_handle));
  }

  mtlk_txmm_msg_cleanup(&man_msg);
  return res;
}

static int _mtlk_core_get_aggr_cfg (mtlk_handle_t hcore, const void* data, uint32 data_size)
{
  int                    res = MTLK_ERR_OK;
  mtlk_aggr_cfg_t        aggr_cfg;
  mtlk_core_t            *core = HANDLE_T_PTR(mtlk_core_t, hcore);
  mtlk_clpb_t            *clpb = *(mtlk_clpb_t **)data;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  /* read config from internal db */
  memset(&aggr_cfg, 0, sizeof(aggr_cfg));

  MTLK_CFG_SET_ITEM_BY_FUNC(&aggr_cfg, cfg, _mtlk_core_receive_agg_rate_cfg,
                            (core, &aggr_cfg.cfg), res)

  /* push result and config to clipboard*/
  res = mtlk_clpb_push(clpb, &res, sizeof(res));
  if (MTLK_ERR_OK == res) {
    res = mtlk_clpb_push(clpb, &aggr_cfg, sizeof(aggr_cfg));
  }

  return res;
}

static int
_mtlk_core_set_aggr_cfg_req (mtlk_core_t *core, uint8 enable_amsdu, uint8 enable_ba, uint32 windowSize)
{
  int res;
  mtlk_txmm_msg_t           man_msg;
  mtlk_txmm_data_t          *man_entry = NULL;
  UMI_TS_VAP_CONFIGURE      *pAggrConfig = NULL;
  mtlk_vap_handle_t         vap_handle = core->vap_handle;

  MTLK_ASSERT(vap_handle);

  if ((enable_amsdu != 0 && enable_amsdu != 1) ||
      (enable_ba != 0 && enable_ba != 1)) {
    ELOG_V("Wrong parameter value given, must be 0 or 1");
    return MTLK_ERR_PARAMS;
  }

  ILOG1_DDDD("CID-%04x: Send Aggregation config to FW: enable AMSDU %u, enable BA %u, Window size %u",
            mtlk_vap_get_oid(vap_handle), enable_amsdu, enable_ba, windowSize);

  man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, mtlk_vap_get_txmm(vap_handle), NULL);
  if (NULL == man_entry) {
    ELOG_D("CID-%04x: No man entry available", mtlk_vap_get_oid(vap_handle));
    return MTLK_ERR_NO_RESOURCES;
  }

  man_entry->id = UM_MAN_TS_VAP_CONFIGURE_REQ;
  man_entry->payload_size = sizeof(UMI_TS_VAP_CONFIGURE);

  pAggrConfig = (UMI_TS_VAP_CONFIGURE *)(man_entry->payload);
  pAggrConfig->vapId = (uint8) mtlk_vap_get_id(vap_handle);
  pAggrConfig->enableBa = (uint8)enable_ba;
  pAggrConfig->amsduSupport = (uint8)enable_amsdu;
  pAggrConfig->windowSize = HOST_TO_MAC32(windowSize);

  res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);
  if (MTLK_ERR_OK != res) {
    ELOG_DD("CID-%04x: Set Aggregation config failure (%i)", mtlk_vap_get_oid(vap_handle), res);
  }

  mtlk_txmm_msg_cleanup(&man_msg);
  return res;
}

static void _mtlk_core_store_aggr_config (mtlk_core_t *core, uint32 amsdu_mode, uint32 ba_mode, uint32 windowSize)
{
  MTLK_CORE_PDB_SET_INT(core, PARAM_DB_CORE_AMSDU_MODE, amsdu_mode);
  MTLK_CORE_PDB_SET_INT(core, PARAM_DB_CORE_BA_MODE, ba_mode);
  MTLK_CORE_PDB_SET_INT(core, PARAM_DB_CORE_WINDOW_SIZE, windowSize);
}

static int _mtlk_core_set_aggr_cfg (mtlk_handle_t hcore, const void* data, uint32 data_size)
{
  int                    res = MTLK_ERR_OK;
  mtlk_core_t            *core = HANDLE_T_PTR(mtlk_core_t, hcore);
  mtlk_aggr_cfg_t        *aggr_cfg = NULL;
  uint32                 aggr_cfg_size;
  mtlk_clpb_t            *clpb = *(mtlk_clpb_t **)data;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  /* get configuration */
  aggr_cfg = mtlk_clpb_enum_get_next(clpb, &aggr_cfg_size);
  MTLK_CLPB_TRY(aggr_cfg, aggr_cfg_size)
    MTLK_CFG_START_CHEK_ITEM_AND_CALL()
      /* send new config to FW */
      MTLK_CFG_CHECK_ITEM_AND_CALL(aggr_cfg, cfg, _mtlk_core_set_aggr_cfg_req,
                                   (core, aggr_cfg->cfg.amsdu_mode, aggr_cfg->cfg.ba_mode, aggr_cfg->cfg.windowSize), res);
      /* store new config in internal db*/
      MTLK_CFG_CHECK_ITEM_AND_CALL_VOID(aggr_cfg, cfg, _mtlk_core_store_aggr_config,
                                        (core, aggr_cfg->cfg.amsdu_mode, aggr_cfg->cfg.ba_mode, aggr_cfg->cfg.windowSize));
    MTLK_CFG_END_CHEK_ITEM_AND_CALL()
  MTLK_CLPB_FINALLY(res)
    /* push result into clipboard */
    return mtlk_clpb_push_res(clpb, res);
  MTLK_CLPB_END
}

#ifdef MTLK_LEGACY_STATISTICS
/* MHI_STATISTICS */

static __INLINE uint32
_mtlk_core_get_vap_stat(mtlk_core_t *nic, mtlk_mhi_stat_vap_e id)
{
  return nic->mhi_vap_stat.stat[id];
}

uint32 __MTLK_IFUNC
mtlk_core_get_vap_stat(mtlk_core_t *nic, mtlk_mhi_stat_vap_e id)
{
  return _mtlk_core_get_vap_stat(nic, id);
}

static __INLINE uint64
_mtlk_core_get_vap_stat64(mtlk_core_t *nic, mtlk_mhi_stat64_vap_e id)
{
  return nic->mhi_vap_stat.stat64[id];
}

uint64 __MTLK_IFUNC
mtlk_core_get_vap_stat64(mtlk_core_t *nic, mtlk_mhi_stat64_vap_e id)
{
  return _mtlk_core_get_vap_stat64(nic, id);
}
#endif /* MTLK_LEGACY_STATISTICS */

/* Calculate summ of 3 counters, update summ of them, return delta.
 */
static __INLINE uint32
_update_summ_of_3_cntrs(uint64 *summ, uint32 cntr1, uint32 cntr2, uint32 cntr3)
{
    uint32 sum123, value;

    sum123 = cntr1 + cntr2 + cntr3; /* new */
    value = (uint32)*summ;          /* prev */
    value = sum123 - value;         /* delta = new - prev */
    *summ += (uint64)value;         /* update with delta */

    return value;
}

#ifdef MTLK_LEGACY_STATISTICS
static int
_mtlk_core_mac_get_mhi_stats (mtlk_core_t *nic, mtlk_hw_t *hw)
{
  if (NET_STATE_HALTED == mtlk_core_get_net_state(nic)) { /* Do nothing if halted */
    return MTLK_ERR_OK;
  }

  return mtlk_hw_mhi_get_stats(hw);
}
#else
static int
_mtlk_core_get_statistics (mtlk_core_t *nic, mtlk_hw_t *hw)
{
  if (NET_STATE_CONNECTED == mtlk_core_get_net_state(nic)) {
    mtlk_hw_set_stats_available(hw, TRUE);
    return _mtlk_hw_get_statistics(hw);
  }
    return MTLK_ERR_OK;

}
#endif

#ifdef MTLK_LEGACY_STATISTICS
static int
_mtlk_core_update_vaps_mhi_stats (mtlk_core_t *nic, wave_radio_t *radio, mtlk_hw_t *hw)
{
  mtlk_vap_manager_t    *vap_mgr;
  mtlk_core_t           *cur_nic;
  mtlk_mhi_stats_vap_t  *mhi_vap_stat;
  mtlk_vap_handle_t      vap_handle;
  int                    vaps_count, vap_index;
  uint64                 total_traffic = 0;
  uint32                 delta, total_traffic_delta = 0;
  int                    res = MTLK_ERR_OK;

/* Update statistics for all VAPs and calculate total_traffic */
  vap_mgr = mtlk_vap_get_manager(nic->vap_handle);
  vaps_count = mtlk_vap_manager_get_max_vaps_count(vap_mgr);
  for (vap_index = vaps_count - 1; vap_index >= 0; vap_index--) {
    res = mtlk_vap_manager_get_vap_handle(vap_mgr, vap_index, &vap_handle);
    if (MTLK_ERR_OK == res) {
      cur_nic = mtlk_vap_get_core(vap_handle);

      mhi_vap_stat = &cur_nic->mhi_vap_stat;
      mtlk_hw_mhi_get_vap_stats(hw, mhi_vap_stat, mtlk_vap_get_id_fw(cur_nic->vap_handle)); /* Vap ID in FW */

      if (_mtlk_core_poll_stat_is_started(cur_nic)) {
        /* - Update all 32-bit statistics from FW;
           - 64-bit statistics is updated manually */
        MTLK_STATIC_ASSERT(4 == STAT64_VAP_TOTAL);

        _mtlk_core_poll_stat_update(cur_nic, mhi_vap_stat->stat, ARRAY_SIZE(mhi_vap_stat->stat));

        /* Take into account the traffic since last update */
        delta = _update_summ_of_3_cntrs(&cur_nic->pstats.tx_packets,
            mhi_vap_stat->stat[STAT_VAP_TX_UNICAST_FRAMES],
            mhi_vap_stat->stat[STAT_VAP_TX_BROADCAST_FRAMES],
            mhi_vap_stat->stat[STAT_VAP_TX_MULTICAST_FRAMES]);
        mhi_vap_stat->stat64[STAT64_VAP_TX_FRAMES] = cur_nic->pstats.tx_packets;

        mtlk_core_add64_cnt(cur_nic, MTLK_CORE_CNT_PACKETS_SENT_64, delta);

        delta = _update_summ_of_3_cntrs(&cur_nic->pstats.rx_packets,
            mhi_vap_stat->stat[STAT_VAP_RX_UNICAST_FRAMES],
            mhi_vap_stat->stat[STAT_VAP_RX_BROADCAST_FRAMES],
            mhi_vap_stat->stat[STAT_VAP_RX_MULTICAST_FRAMES]);
        mhi_vap_stat->stat64[STAT64_VAP_RX_FRAMES] = cur_nic->pstats.rx_packets;

        mtlk_core_add64_cnt(cur_nic, MTLK_CORE_CNT_PACKETS_RECEIVED_64, delta);

        delta = _update_summ_of_3_cntrs(&cur_nic->pstats.tx_bytes,
            mhi_vap_stat->stat[STAT_VAP_TX_UNICAST_BYTES],
            mhi_vap_stat->stat[STAT_VAP_TX_BROADCAST_BYTES],
            mhi_vap_stat->stat[STAT_VAP_TX_MULTICAST_BYTES]);
        mhi_vap_stat->stat64[STAT64_VAP_TX_BYTES] = cur_nic->pstats.tx_bytes;

        mtlk_core_add64_cnt(cur_nic, MTLK_CORE_CNT_BYTES_SENT_64, delta);
        total_traffic_delta += delta;

        delta = _update_summ_of_3_cntrs(&cur_nic->pstats.rx_bytes,
            mhi_vap_stat->stat[STAT_VAP_RX_UNICAST_BYTES],
            mhi_vap_stat->stat[STAT_VAP_RX_BROADCAST_BYTES],
            mhi_vap_stat->stat[STAT_VAP_RX_MULTICAST_BYTES]);
        mhi_vap_stat->stat64[STAT64_VAP_RX_BYTES] = cur_nic->pstats.rx_bytes;

        mtlk_core_add64_cnt(cur_nic, MTLK_CORE_CNT_BYTES_RECEIVED_64, delta);
        total_traffic_delta += delta;

        /* Received multicast packets */
        _update_summ_of_3_cntrs(&cur_nic->pstats.rx_multicast_packets,
            0, 0, mhi_vap_stat->stat[STAT_VAP_RX_MULTICAST_FRAMES]);
      }

      total_traffic += cur_nic->pstats.tx_bytes;
      total_traffic += cur_nic->pstats.rx_bytes;
    }
  }

#ifdef CPTCFG_IWLWAV_PMCU_SUPPORT
  wv_PMCU_Traffic_Report(hw, total_traffic);
#endif

  wave_radio_total_traffic_delta_set(radio, total_traffic_delta);

  return res;
}
#else /* MTLK_LEGACY_STATISTICS */
static int
_mtlk_core_update_vaps_mhi_stats (mtlk_core_t *nic, wave_radio_t *radio, mtlk_hw_t *hw)
{
  mtlk_vap_manager_t    *vap_mgr;
  mtlk_core_t           *cur_nic;
  mtlk_mhi_stats_vap_t  *mhi_vap_stat;
  mtlk_vap_handle_t      vap_handle;
  int                    vaps_count, vap_index;
  uint64                 total_traffic = 0;
  uint32                 delta, total_traffic_delta = 0;
  int                    res = MTLK_ERR_OK;
  uint32                 *stats;

  stats = mtlk_osal_mem_alloc(sizeof(mhi_vap_stat->stats), MTLK_MEM_TAG_EXTENSION);
  if(NULL == stats)
  {
    ELOG_D("CID-%04x: Failed to allocate stats", mtlk_vap_get_oid(nic->vap_handle));
    return MTLK_ERR_NO_MEM;
  }

/* Update statistics for all VAPs and calculate total_traffic */
  vap_mgr = mtlk_vap_get_manager(nic->vap_handle);
  vaps_count = mtlk_vap_manager_get_max_vaps_count(vap_mgr);
  for (vap_index = vaps_count - 1; vap_index >= 0; vap_index--) {
    res = mtlk_vap_manager_get_vap_handle(vap_mgr, vap_index, &vap_handle);
    if (MTLK_ERR_OK == res) {
      cur_nic = mtlk_vap_get_core(vap_handle);

      mhi_vap_stat = &cur_nic->mhi_vap_stat;
      mtlk_hw_mhi_get_vap_stats(hw, mhi_vap_stat, mtlk_vap_get_id_fw(cur_nic->vap_handle)); /* Vap ID in FW */

      if (_mtlk_core_poll_stat_is_started(cur_nic)) {
        /* - Update all 32-bit statistics from FW;
           - 64-bit statistics is updated manually */

        wave_memcpy(stats, sizeof(mhi_vap_stat->stats), &mhi_vap_stat->stats, sizeof(mhi_vap_stat->stats));
        _mtlk_core_poll_stat_update(cur_nic, (uint32 *)stats, sizeof(mhi_vap_stat->stats));

        /* Take into account the traffic since last update */
        delta = _update_summ_of_3_cntrs(&cur_nic->pstats.tx_packets,
                                  mhi_vap_stat->stats.txInUnicastHd,
                                  mhi_vap_stat->stats.txInBroadcastHd,
                                  mhi_vap_stat->stats.txInMulticastHd);
        mhi_vap_stat->stats64.txFrames = cur_nic->pstats.tx_packets;

        mtlk_core_add64_cnt(cur_nic, MTLK_CORE_CNT_PACKETS_SENT_64, delta);

        delta = _update_summ_of_3_cntrs(&cur_nic->pstats.rx_packets,
                                  mhi_vap_stat->stats.rxOutUnicastHd,
                                  mhi_vap_stat->stats.rxOutBroadcastHd,
                                  mhi_vap_stat->stats.rxOutMulticastHd);
        mhi_vap_stat->stats64.rxFrames = cur_nic->pstats.rx_packets;

        mtlk_core_add64_cnt(cur_nic, MTLK_CORE_CNT_PACKETS_RECEIVED_64, delta);

        delta = _update_summ_of_3_cntrs(&cur_nic->pstats.tx_bytes,
                                  mhi_vap_stat->stats.txInUnicastNumOfBytes,
                                  mhi_vap_stat->stats.txInBroadcastNumOfBytes,
                                  mhi_vap_stat->stats.txInMulticastNumOfBytes);
        mhi_vap_stat->stats64.txBytes = cur_nic->pstats.tx_bytes;

        mtlk_core_add64_cnt(cur_nic, MTLK_CORE_CNT_BYTES_SENT_64, delta);
        total_traffic_delta += delta;

        delta = _update_summ_of_3_cntrs(&cur_nic->pstats.rx_bytes,
                                  mhi_vap_stat->stats.rxOutUnicastNumOfBytes,
                                  mhi_vap_stat->stats.rxOutBroadcastNumOfBytes,
                                  mhi_vap_stat->stats.rxOutMulticastNumOfBytes);
        mhi_vap_stat->stats64.rxBytes = cur_nic->pstats.rx_bytes;

        mtlk_core_add64_cnt(cur_nic, MTLK_CORE_CNT_BYTES_RECEIVED_64, delta);
        total_traffic_delta += delta;

        /* Received multicast packets */
        _update_summ_of_3_cntrs(&cur_nic->pstats.rx_multicast_packets,
            0, 0, mhi_vap_stat->stats.rxOutMulticastHd);
      }

      total_traffic += cur_nic->pstats.tx_bytes;
      total_traffic += cur_nic->pstats.rx_bytes;
    }
  }

  if( NULL != stats){
    mtlk_osal_mem_free(stats);
  }
#ifdef CPTCFG_IWLWAV_PMCU_SUPPORT
  wv_PMCU_Traffic_Report(hw, total_traffic);
#endif

  wave_radio_total_traffic_delta_set(radio, total_traffic_delta);

  return res;
}
#endif /* MTLK_LEGACY_STATISTICS */

/* PHY_RX_STATUS */
void hw_mac_update_peers_stats (mtlk_hw_t *hw, mtlk_core_t *core);

static int
_mtlk_core_mac_get_phy_status(mtlk_core_t *nic, mtlk_hw_t *hw, mtlk_core_general_stats_t *general_stats)
{
  MTLK_ASSERT(nic != NULL);
  MTLK_ASSERT(general_stats != NULL);

  if (NET_STATE_HALTED == mtlk_core_get_net_state(nic)) { /* Do nothing if halted */
    return MTLK_ERR_OK;
  }

  return hw_phy_rx_status_get(hw, nic);
}

static void
_mtlk_core_mac_update_peers_stats (mtlk_core_t *nic)
{
  hw_mac_update_peers_stats(mtlk_vap_get_hw(nic->vap_handle), nic);
}

extern void __MTLK_IFUNC mtlk_log_tsf_sync_msg(int hw_idx, uint32 drv_ts, int32 fw_offset);

extern void __MTLK_IFUNC mtlk_log_tsf_sync_msg(int hw_idx, uint32 drv_ts, int32 fw_offset);

/* Sync Log Timestamp with FW TSF */
static void
_mtlk_core_sync_log_timestamp (mtlk_vap_handle_t vap_handle)
{
#ifdef CPTCFG_IWLWAV_TSF_TIMER_ACCESS_ENABLED
    uint32  t_log, t_tsf;
    int32   t_ofs;

    mtlk_hw_get_log_fw_timestamps(vap_handle, &t_log, &t_tsf);

    t_ofs = t_tsf - t_log;          /* signed time shift */
#if 0
    if (-1 > t_ofs || t_ofs > 1)    /*  +/- 1 usec is allowed for jitter */
    {
        mtlk_log_timestamp_offset += t_ofs; /* additional shift */
        ILOG4_DDD("LogTimestamp shift: TSF %010u, t_log %010u, shift %+d", t_tsf, t_log, t_ofs);
    }
#endif

    mtlk_log_tsf_sync_msg(mtlk_vap_get_hw_idx(vap_handle), t_log, t_ofs);

#endif /* CPTCFG_IWLWAV_TSF_TIMER_ACCESS_ENABLED */
}

static int
_mtlk_core_get_status (mtlk_handle_t hcore, const void* data, uint32 data_size)
{
  int                        res = MTLK_ERR_OK;
  mtlk_core_t               *nic = HANDLE_T_PTR(mtlk_core_t, hcore);
  wave_radio_t              *radio = wave_vap_radio_get(nic->vap_handle);
  mtlk_hw_api_t             *hw_api = mtlk_vap_get_hwapi(nic->vap_handle);
  mtlk_clpb_t               *clpb = *(mtlk_clpb_t **) data;
  mtlk_core_general_stats_t *general_stats;
  mtlk_txmm_stats_t          txm_stats;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  general_stats = mtlk_osal_mem_alloc(sizeof(*general_stats), MTLK_MEM_TAG_CLPB);
  if(general_stats == NULL) {
    ELOG_D("CID-%04x: Can't allocate clipboard data", mtlk_vap_get_oid(nic->vap_handle));
    return MTLK_ERR_NO_MEM;
  }
  memset(general_stats, 0, sizeof(*general_stats));

  /* Fill Core private statistic fields*/
  general_stats->core_priv_stats = nic->pstats; /* struct copy */

  general_stats->tx_packets = nic->pstats.tx_packets;
  general_stats->tx_bytes   = nic->pstats.tx_bytes;
  general_stats->rx_packets = nic->pstats.rx_packets;
  general_stats->rx_bytes   = nic->pstats.rx_bytes;
  general_stats->rx_multicast_packets = nic->pstats.rx_multicast_packets;

  general_stats->unicast_replayed_packets = _mtlk_core_get_cnt(nic, MTLK_CORE_CNT_UNICAST_REPLAYED_PACKETS);
  general_stats->multicast_replayed_packets = _mtlk_core_get_cnt(nic, MTLK_CORE_CNT_MULTICAST_REPLAYED_PACKETS);
  general_stats->management_replayed_packets = _mtlk_core_get_cnt(nic, MTLK_CORE_CNT_MANAGEMENT_REPLAYED_PACKETS);
  general_stats->fwd_rx_packets = _mtlk_core_get_cnt(nic, MTLK_CORE_CNT_FWD_RX_PACKETS);
  general_stats->fwd_rx_bytes = _mtlk_core_get_cnt(nic, MTLK_CORE_CNT_FWD_RX_BYTES);
  general_stats->rx_dat_frames = _mtlk_core_get_cnt(nic, MTLK_CORE_CNT_DAT_FRAMES_RECEIVED);
  general_stats->rx_ctl_frames = _mtlk_core_get_cnt(nic, MTLK_CORE_CNT_CTL_FRAMES_RECEIVED);

  general_stats->tx_man_frames_res_queue = _mtlk_core_get_cnt(nic, MTLK_CORE_CNT_MAN_FRAMES_RES_QUEUE);
  general_stats->tx_man_frames_sent      = _mtlk_core_get_cnt(nic, MTLK_CORE_CNT_MAN_FRAMES_SENT);
  general_stats->tx_man_frames_confirmed = _mtlk_core_get_cnt(nic, MTLK_CORE_CNT_MAN_FRAMES_CONFIRMED);

  general_stats->rx_man_frames = _mtlk_core_get_cnt(nic, MTLK_CORE_CNT_MAN_FRAMES_RECEIVED);
  general_stats->rx_man_frames_retry_dropped = _mtlk_core_get_cnt(nic, MTLK_CORE_CNT_RX_MAN_FRAMES_RETRY_DROPPED);
  general_stats->rx_man_frames_cfg80211_fail = _mtlk_core_get_cnt(nic, MTLK_CORE_CNT_MAN_FRAMES_CFG80211_FAILED);

  general_stats->dgaf_disabled_tx_pck_dropped = _mtlk_core_get_cnt(nic, MTLK_CORE_CNT_TX_PACKETS_TO_UNICAST_DGAF_DISABLED);
  general_stats->dgaf_disabled_tx_pck_converted = _mtlk_core_get_cnt(nic, MTLK_CORE_CNT_TX_PACKETS_SKIPPED_DGAF_DISABLED);

  /* bss mgmt statistics */
  general_stats->tx_probe_resp_sent_cnt    = _mtlk_core_get_cnt(nic, MTLK_CORE_CNT_TX_PROBE_RESP_SENT);
  general_stats->tx_probe_resp_dropped_cnt = _mtlk_core_get_cnt(nic, MTLK_CORE_CNT_TX_PROBE_RESP_DROPPED);
  general_stats->bss_mgmt_tx_que_full_cnt = _mtlk_core_get_cnt(nic, MTLK_CORE_CNT_BSS_MGMT_TX_QUE_FULL);

  /* Radio status */
  general_stats->tx_probe_resp_sent_cnt_per_radio    = mtlk_wss_get_stat(nic->radio_wss, WAVE_RADIO_CNT_TX_PROBE_RESP_SENT);
  general_stats->tx_probe_resp_dropped_cnt_per_radio = mtlk_wss_get_stat(nic->radio_wss, WAVE_RADIO_CNT_TX_PROBE_RESP_DROPPED);
  general_stats->bss_mgmt_tx_que_full_cnt_per_radio  = mtlk_wss_get_stat(nic->radio_wss, WAVE_RADIO_CNT_BSS_MGMT_TX_QUE_FULL);

  /* HW status */
  (void)mtlk_hw_get_prop(hw_api, MTLK_HW_BSS_MGMT_MAX_MSGS, &general_stats->bss_mgmt_bds_max_num, sizeof(general_stats->bss_mgmt_bds_max_num));
  (void)mtlk_hw_get_prop(hw_api, MTLK_HW_BSS_MGMT_FREE_MSGS, &general_stats->bss_mgmt_bds_free_num, sizeof(general_stats->bss_mgmt_bds_free_num));
  (void)mtlk_hw_get_prop(hw_api, MTLK_HW_BSS_MGMT_MSGS_USED_PEAK, &general_stats->bss_mgmt_bds_usage_peak, sizeof(general_stats->bss_mgmt_bds_usage_peak));
  (void)mtlk_hw_get_prop(hw_api, MTLK_HW_BSS_MGMT_MAX_RES_MSGS, &general_stats->bss_mgmt_bds_max_num_res, sizeof(general_stats->bss_mgmt_bds_max_num_res));
  (void)mtlk_hw_get_prop(hw_api, MTLK_HW_BSS_MGMT_FREE_RES_MSGS, &general_stats->bss_mgmt_bds_free_num_res, sizeof(general_stats->bss_mgmt_bds_free_num_res));
  (void)mtlk_hw_get_prop(hw_api, MTLK_HW_BSS_MGMT_MSGS_RES_USED_PEAK, &general_stats->bss_mgmt_bds_usage_peak_res, sizeof(general_stats->bss_mgmt_bds_usage_peak_res));

  (void)mtlk_hw_get_prop(hw_api, MTLK_HW_FREE_TX_MSGS, &general_stats->tx_msdus_free, sizeof(general_stats->tx_msdus_free));
  (void)mtlk_hw_get_prop(hw_api, MTLK_HW_TX_MSGS_USED_PEAK, &general_stats->tx_msdus_usage_peak, sizeof(general_stats->tx_msdus_usage_peak));
  (void)mtlk_hw_get_prop(hw_api, MTLK_HW_BIST, &general_stats->bist_check_passed, sizeof(general_stats->bist_check_passed));
  (void)mtlk_hw_get_prop(hw_api, MTLK_HW_FW_BUFFERS_PROCESSED, &general_stats->fw_logger_packets_processed, sizeof(general_stats->fw_logger_packets_processed));
  (void)mtlk_hw_get_prop(hw_api, MTLK_HW_FW_BUFFERS_DROPPED, &general_stats->fw_logger_packets_dropped, sizeof(general_stats->fw_logger_packets_dropped));
  (void)mtlk_hw_get_prop(hw_api, MTLK_HW_RADARS_DETECTED, &general_stats->radars_detected, sizeof(general_stats->radars_detected));
#ifdef CPTCFG_IWLWAV_LEGACY_INT_RECOVERY_MON
  (void)mtlk_hw_get_prop(hw_api, MTLK_HW_ISR_LOST_SUSPECT, &general_stats->isr_lost_suspect, sizeof(general_stats->isr_lost_suspect));
  (void)mtlk_hw_get_prop(hw_api, MTLK_HW_ISR_LOST_RECOVERED, &general_stats->isr_recovered, sizeof(general_stats->isr_recovered));
#endif

#ifdef MTLK_DEBUG
  (void)mtlk_hw_get_prop(hw_api, MTLK_HW_FW_CFM_IN_TASKLET,         &general_stats->tx_max_cfm_in_tasklet,      sizeof(general_stats->tx_max_cfm_in_tasklet));
  (void)mtlk_hw_get_prop(hw_api, MTLK_HW_FW_TX_TIME_INT_TO_TASKLET, &general_stats->tx_max_time_int_to_tasklet, sizeof(general_stats->tx_max_time_int_to_tasklet));
  (void)mtlk_hw_get_prop(hw_api, MTLK_HW_FW_TX_TIME_INT_TO_CFM,     &general_stats->tx_max_time_int_to_pck,     sizeof(general_stats->tx_max_time_int_to_pck));
  (void)mtlk_hw_get_prop(hw_api, MTLK_HW_FW_DATA_IN_TASKLET,        &general_stats->rx_max_pck_in_tasklet,      sizeof(general_stats->rx_max_pck_in_tasklet));
  (void)mtlk_hw_get_prop(hw_api, MTLK_HW_FW_RX_TIME_INT_TO_TASKLET, &general_stats->rx_max_time_int_to_tasklet, sizeof(general_stats->rx_max_time_int_to_tasklet));
  (void)mtlk_hw_get_prop(hw_api, MTLK_HW_FW_RX_TIME_INT_TO_PCK,     &general_stats->rx_max_time_int_to_pck,     sizeof(general_stats->rx_max_time_int_to_pck));
#endif

  mtlk_txmm_get_stats(mtlk_vap_get_txmm(nic->vap_handle), &txm_stats);
  general_stats->txmm_sent = txm_stats.nof_sent;
  general_stats->txmm_cfmd = txm_stats.nof_cfmed;
  general_stats->txmm_peak = txm_stats.used_peak;

  mtlk_txmm_get_stats(mtlk_vap_get_txdm(nic->vap_handle), &txm_stats);
  general_stats->txdm_sent = txm_stats.nof_sent;
  general_stats->txdm_cfmd = txm_stats.nof_cfmed;
  general_stats->txdm_peak = txm_stats.used_peak;

  /* For master VAP only */
  if(mtlk_vap_is_master(nic->vap_handle)) {
    mtlk_vap_handle_t   vap_handle = nic->vap_handle;
    mtlk_hw_t          *hw = mtlk_vap_get_hw(vap_handle);
    wave_radio_t       *radio = wave_vap_radio_get(vap_handle);

    /* Sync Log Timestamp with FW TSF */
    _mtlk_core_sync_log_timestamp(vap_handle);

    /* Check HW ring queues */
    if (wave_hw_mmb_all_rings_queue_check(hw)) {
      ELOG_V("Ring failure -> Reset FW");
      (void)mtlk_hw_set_prop(mtlk_vap_get_hwapi(nic->vap_handle), MTLK_HW_RESET, NULL, 0);
      res = MTLK_ERR_UNKNOWN;
      goto FINISH;
    }

#ifdef MTLK_LEGACY_STATISTICS
    /* Get MHI Statistics which is per HW card */
    if (wave_radio_is_first(radio))
      _mtlk_core_mac_get_mhi_stats(nic, hw);
#else
    /* if any core on this card is scanning then _mtlk_core_get_statistics is
     * invoked from scan context and  not from stats context, this
     * prevents both scan and stats context accessing the common stats buffer */
    if (wave_radio_is_first(radio) && (!mtlk_hw_scan_is_running(hw)))
      _mtlk_core_get_statistics(nic, hw);

    if ( TRUE == mtlk_hw_get_stats_available(hw)){
#endif

    /* Update all VAPs Statistics */
    if (MTLK_ERR_OK == res) {
      _mtlk_core_update_vaps_mhi_stats(nic, radio, hw);
    }

#ifdef MTLK_LEGACY_STATISTICS
    /* Get PHY Status */
    if (!mtlk_core_scan_is_running(nic)) {
#endif
    {
      struct intel_vendor_channel_data ch_data;
      mtlk_osal_msec_t cur_time = mtlk_osal_timestamp_to_ms(mtlk_osal_timestamp());
      iwpriv_cca_adapt_t cca_adapt_params;
      mtlk_pdb_size_t cca_adapt_size = sizeof(cca_adapt_params);

      _mtlk_core_mac_get_phy_status(nic, hw, general_stats);

      /* if beacons were blocked due to CCA, need to poll CWI */
      if (nic->slow_ctx->cca_adapt.cwi_poll) {
        if (MTLK_ERR_OK != WAVE_RADIO_PDB_GET_BINARY(radio, PARAM_DB_RADIO_CCA_ADAPT, &cca_adapt_params, &cca_adapt_size))
          goto FINISH;

        if ((cur_time - nic->slow_ctx->cca_adapt.cwi_poll_ts) > (cca_adapt_params.iterative * DEFAULT_BEACON_INTERVAL)) {
          res = scan_get_aocs_info(nic, &ch_data, NULL);
          if (MTLK_ERR_OK == res) {
            mtlk_cca_step_up_if_allowed(nic, ch_data.cwi_noise, cca_adapt_params.limit, cca_adapt_params.step_up);
          }
          nic->slow_ctx->cca_adapt.cwi_poll_ts = cur_time;
        }
      }

      /* for AP channel 0 means "use AOCS" */
      if (!WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_CHANNEL_CFG) &&
          WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_ACS_UPDATE_TO) &&
          (cur_time - nic->acs_updated_ts) > WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_ACS_UPDATE_TO))
      {
        res = fill_channel_data(nic, &ch_data);
        if (MTLK_ERR_OK == res) res = mtlk_send_chan_data(nic->vap_handle, &ch_data, sizeof(ch_data));
        nic->acs_updated_ts = cur_time;
      }
    }
    }

    if (MTLK_ERR_OK != res) {
      goto FINISH;
    }

    /* Fill core status fields */
    general_stats->net_state = mtlk_core_get_net_state(nic);

    MTLK_CORE_PDB_GET_MAC(nic, PARAM_DB_CORE_BSSID, &general_stats->bssid);

    if (!mtlk_vap_is_ap(nic->vap_handle) && (mtlk_core_get_net_state(nic) == NET_STATE_CONNECTED)) {
      sta_entry *sta = mtlk_stadb_get_ap(&nic->slow_ctx->stadb);

      if (NULL != sta) {
        general_stats->max_rssi = mtlk_sta_get_max_rssi(sta);

        mtlk_sta_decref(sta);  /* De-reference of get_ap */
      }
    }
  }

  /* Radio Phy Status */
  {
    wave_radio_phy_stat_t phy_stat;

    wave_radio_phy_status_get(radio, &phy_stat);

    general_stats->noise              = phy_stat.noise;
    general_stats->channel_util       = phy_stat.ch_util;
    general_stats->airtime            = phy_stat.airtime;
    general_stats->airtime_efficiency = phy_stat.airtime_efficiency;;
  }

  /* Get PHY Status */
  _mtlk_core_mac_update_peers_stats(nic);

  /* Return Core status & statistic data */
  res = mtlk_clpb_push_nocopy(clpb, general_stats, sizeof(*general_stats));
  if (MTLK_ERR_OK != res) {
    mtlk_clpb_purge(clpb);
    goto FINISH;
  }
  return MTLK_ERR_OK;

FINISH:
  mtlk_osal_mem_free(general_stats);
  return res;
}

int
_mtlk_core_get_hs20_info(mtlk_handle_t hcore, const void* data, uint32 data_size)
{
  int                       res = MTLK_ERR_OK;
  mtlk_core_t               *nic = HANDLE_T_PTR(mtlk_core_t, hcore);
  mtlk_clpb_t               *clpb = *(mtlk_clpb_t **) data;
  mtlk_core_hs20_info_t     *hs20_info;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  hs20_info = mtlk_osal_mem_alloc(sizeof(*hs20_info), MTLK_MEM_TAG_CLPB);
  if(hs20_info == NULL) {
    ELOG_V("Can't allocate clipboard data");
    res = MTLK_ERR_NO_MEM;
    goto err_finish;
  }
  memset(hs20_info, 0, sizeof(*hs20_info));

  hs20_info->hs20_enabled = mtlk_df_user_get_hs20_status(mtlk_vap_get_df(nic->vap_handle));
  hs20_info->arp_proxy = MTLK_CORE_PDB_GET_INT(nic, PARAM_DB_CORE_ARP_PROXY);
  hs20_info->dgaf_disabled = nic->dgaf_disabled;
  hs20_info->osen_enabled = nic->osen_enabled;

  wave_memcpy(hs20_info->dscp_table, sizeof(hs20_info->dscp_table), nic->dscp_table, sizeof(nic->dscp_table));

  res = mtlk_clpb_push_nocopy(clpb, hs20_info, sizeof(*hs20_info));
  if (MTLK_ERR_OK != res) {
    mtlk_clpb_purge(clpb);
    goto err_finish;
  }

  return MTLK_ERR_OK;

err_finish:
  if (hs20_info) {
    mtlk_osal_mem_free(hs20_info);
  }
  return res;
}

void __MTLK_IFUNC core_wds_beacon_process(mtlk_core_t *master_core, wds_beacon_data_t *beacon_data)
{
  wds_t           *master_wds = &master_core->slow_ctx->wds_mng;
  wds_on_beacon_t *on_beacon;

  ILOG5_V("Processing a WDS beacon");

  on_beacon = wds_on_beacon_find_remove(master_wds, &beacon_data->addr, FALSE);
  if (on_beacon) {
    beacon_data->vap_idx = on_beacon->vap_id;
    if (NULL == on_beacon->ie_data) {
      on_beacon->ie_data = mtlk_osal_mem_alloc(beacon_data->ie_data_len, LQLA_MEM_TAG_WDS);
      if (on_beacon->ie_data) {
        wave_memcpy(on_beacon->ie_data, beacon_data->ie_data_len,
                    beacon_data->ie_data, beacon_data->ie_data_len);
        on_beacon->ie_data_len = beacon_data->ie_data_len;
        _mtlk_process_hw_task(master_core, SERIALIZABLE, wds_beacon_process, HANDLE_T(master_wds), beacon_data, sizeof(wds_beacon_data_t));
      }
    }
  }
}

BOOL __MTLK_IFUNC core_wds_frame_drop(mtlk_core_t *core, IEEE_ADDR *addr)
{
  wds_t               *wds_ctx = &core->slow_ctx->wds_mng;

  return wds_sta_is_peer_ap(wds_ctx, addr);
}

uint32 __MTLK_IFUNC
mtlk_core_ta_on_timer (mtlk_osal_timer_t *timer, mtlk_handle_t ta_handle)
{
  /* Note: This function is called from timer context
           Do not modify TA structure from here
  */
  mtlk_core_t     *core;

  mtlk_vap_handle_t vap_handle = mtlk_ta_get_vap_handle(ta_handle);
  if (vap_handle) {
    core = mtlk_vap_get_core(vap_handle);
    mtlk_core_schedule_internal_task(core, ta_handle, ta_timer, NULL, 0);
    return mtlk_ta_get_timer_resolution_ms(ta_handle);
  }
  else {
    ELOG_V("Can't to schedule TA task: VAP-handler not found");
    return 0;
  }
}

int __MTLK_IFUNC
mtlk_core_stop_lm (struct nic *core)
{
  return (_mtlk_core_stop_lm(HANDLE_T(core), NULL, 0));
}

int __MTLK_IFUNC core_on_rcvry_isol (mtlk_core_t *core, uint32 rcvry_type)
{
  int res;

#ifdef MTLK_LEGACY_STATISTICS
  mtlk_wssd_unregister_request_handler(mtlk_vap_get_irbd(core->vap_handle), core->slow_ctx->stat_irb_handle);
  core->slow_ctx->stat_irb_handle = NULL;
#endif

  if (mtlk_vap_is_master_ap(core->vap_handle)) {
    mtlk_coc_t *coc_mgmt = __wave_core_coc_mgmt_get(core);
    mtlk_ta_on_rcvry_isol(mtlk_vap_get_ta(core->vap_handle));

    /* Disable MAC WatchDog */
    mtlk_osal_timer_cancel_sync(&core->slow_ctx->mac_watchdog_timer);

    /* CoC isolation */
    mtlk_coc_on_rcvry_isol(coc_mgmt);

    if (mtlk_core_scan_is_running(core)) {
      res = pause_or_prevent_scan(core);
      if (MTLK_ERR_OK != res) {
        return res;
      }
    }
  }

  clean_all_sta_on_disconnect(core);

  if (mtlk_vap_is_ap(core->vap_handle)) {
    wds_on_if_down(&core->slow_ctx->wds_mng);
  }

  mtlk_stadb_stop(&core->slow_ctx->stadb);

  res = wave_beacon_man_rcvry_reset(core);
  if (MTLK_ERR_OK != res) {
    return res;
  }

  res = mtlk_core_set_net_state(core, NET_STATE_HALTED);
  if (res != MTLK_ERR_OK) {
    return res;
  }

  return MTLK_ERR_OK;
}

int __MTLK_IFUNC
_core_set_umi_key(mtlk_core_t *core, int key_index)
{
  int                       res;
  mtlk_txmm_msg_t           man_msg;
  mtlk_txmm_data_t          *man_entry = NULL;
  mtlk_txmm_t               *txmm = mtlk_vap_get_txmm(core->vap_handle);

  UMI_SET_KEY               *umi_key;
  uint16                    alg_type;
  uint16                    key_len;

  if ((mtlk_core_get_net_state(core) & (NET_STATE_READY | NET_STATE_ACTIVATING | NET_STATE_CONNECTED)) == 0) {
    ILOG1_D("CID-%04x: Invalid card state - request rejected", mtlk_vap_get_oid(core->vap_handle));
    res = MTLK_ERR_NOT_READY;
    goto FINISH;
  }

  if (mtlk_core_is_stopping(core)) {
    ILOG1_D("CID-%04x: Can't set ENC_EXT configuration - core is stopping", mtlk_vap_get_oid(core->vap_handle));
    res = MTLK_ERR_NOT_READY;
    goto FINISH;
  }

  alg_type = core->slow_ctx->group_cipher;
  if ((key_index == 0) && (alg_type != IW_ENCODE_ALG_WEP)) {
    ILOG2_DDD("CID-%04x: Ignore Key %d, if alg_type=%d is not WEP",
        mtlk_vap_get_oid(core->vap_handle), key_index, alg_type);
    res = MTLK_ERR_OK;
    goto FINISH;
  }

  /* Prepare UMI message */
  man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, txmm, NULL);
  if (!man_entry) {
    ELOG_D("CID-%04x: No man entry available", mtlk_vap_get_oid(core->vap_handle));
    res = MTLK_ERR_NO_RESOURCES;
    goto FINISH;
  }

  umi_key = (UMI_SET_KEY*)man_entry->payload;
  memset(umi_key, 0, sizeof(*umi_key));

  man_entry->id           = UM_MAN_SET_KEY_REQ;
  man_entry->payload_size = sizeof(*umi_key);

  key_len = core->slow_ctx->keys[key_index].key_len;
  if( 0 == key_len) {
    ILOG2_DD("CID-%04x: Key %d is not set", mtlk_vap_get_oid(core->vap_handle), key_index);
    res = MTLK_ERR_OK;
    goto FINISH;
  }

  /* Set Ciper Suite */
  switch (alg_type) {
    case IW_ENCODE_ALG_WEP:
      if(MIB_WEP_KEY_WEP2_LENGTH == key_len) { /* 104 bit */
        umi_key->u16CipherSuite = HOST_TO_MAC16(UMI_RSN_CIPHER_SUITE_WEP104);
      }
      else if (MIB_WEP_KEY_WEP1_LENGTH == key_len) { /* 40 bit */
        umi_key->u16CipherSuite = HOST_TO_MAC16(UMI_RSN_CIPHER_SUITE_WEP40);
      }
      else {
        ELOG_DD("CID-%04x: Wrong WEP key length %d", mtlk_vap_get_oid(core->vap_handle), key_len);
        res = MTLK_ERR_PARAMS;
        goto FINISH;
      }
      break;
    case IW_ENCODE_ALG_TKIP:
      umi_key->u16CipherSuite = HOST_TO_MAC16(UMI_RSN_CIPHER_SUITE_TKIP);
      break;
    case IW_ENCODE_ALG_CCMP:
      umi_key->u16CipherSuite = HOST_TO_MAC16(UMI_RSN_CIPHER_SUITE_CCMP);
      break;
    case IW_ENCODE_ALG_AES_CMAC:
      umi_key->u16CipherSuite = HOST_TO_MAC16(UMI_RSN_CIPHER_SUITE_BIP);
      break;
    case IW_ENCODE_ALG_GCMP:
      umi_key->u16CipherSuite = HOST_TO_MAC16(UMI_RSN_CIPHER_SUITE_GCMP128);
      break;
    case IW_ENCODE_ALG_GCMP_256:
      umi_key->u16CipherSuite = HOST_TO_MAC16(UMI_RSN_CIPHER_SUITE_GCMP256);
      break;
    default:
      ELOG_D("CID-%04x: Unknown CiperSuite", mtlk_vap_get_oid(core->vap_handle));
      res = MTLK_ERR_PARAMS;
      goto FINISH;
  }

  /* Recovery only group key */
  umi_key->u16KeyType = HOST_TO_MAC16(UMI_RSN_GROUP_KEY);

  /* update group replay counter */
  _mtlk_core_set_rx_seq(core, key_index, core->slow_ctx->keys[key_index].seq);
  umi_key->u16Sid = HOST_TO_MAC16(TMP_BCAST_DEST_ADDR);

  /* The key has been copied into au8Tk1 array with UMI_RSN_TK1_LEN size.
   * But key can have UMI_RSN_TK1_LEN+UMI_RSN_TK2_LEN size - so
   * actually second part of key is copied into au8Tk2 array */
  wave_memcpy(umi_key->au8Tk, sizeof(umi_key->au8Tk),
         core->slow_ctx->keys[key_index].key,
         core->slow_ctx->keys[key_index].key_len);

  /* set TX sequence number */
  umi_key->au8TxSeqNum[0] = 1;
  umi_key->u16KeyIndex = HOST_TO_MAC16(key_index);

  ILOG1_D("UMI_SET_KEY             SID:0x%x", MAC_TO_HOST16(umi_key->u16Sid));
  ILOG1_D("UMI_SET_KEY         KeyType:0x%x", MAC_TO_HOST16(umi_key->u16KeyType));
  ILOG1_D("UMI_SET_KEY  u16CipherSuite:0x%x", MAC_TO_HOST16(umi_key->u16CipherSuite));
  ILOG1_D("UMI_SET_KEY        KeyIndex:%d", MAC_TO_HOST16(umi_key->u16KeyIndex));
  mtlk_dump(1, umi_key->au8RxSeqNum, sizeof(umi_key->au8RxSeqNum), "RxSeqNum");
  mtlk_dump(1, umi_key->au8TxSeqNum, sizeof(umi_key->au8TxSeqNum), "TxSeqNum");
  mtlk_dump(1, umi_key->au8Tk, key_len, "KEY:");

  res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);
  if (MTLK_ERR_OK != res) {
    ELOG_DD("CID-%04x: mtlk_mm_send_blocked failed: %i", mtlk_vap_get_oid(core->vap_handle), res);
    goto FINISH;
  }

FINISH:
  if (NULL != man_entry) {
    mtlk_txmm_msg_cleanup(&man_msg);
  }

  return res;
}

static int
_mtlk_core_set_amsdu_num_req (mtlk_core_t *core, uint32 htMsduInAmsdu,
  uint32 vhtMsduInAmsdu, uint32 heMsduInAmsdu)
{
  int res;
  mtlk_txmm_msg_t           man_msg;
  mtlk_txmm_data_t          *man_entry = NULL;
  UMI_MSDU_IN_AMSDU_CONFIG  *pAMSDUNum = NULL;
  mtlk_vap_handle_t         vap_handle = core->vap_handle;

  MTLK_ASSERT(vap_handle);

  ILOG1_DDD("CID-%04x:AMSDUNum FW request: Set %d %d", mtlk_vap_get_oid(vap_handle), htMsduInAmsdu, vhtMsduInAmsdu);

  man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, mtlk_vap_get_txmm(vap_handle), NULL);
  if (NULL == man_entry) {
    ELOG_D("CID-%04x: No man entry available", mtlk_vap_get_oid(vap_handle));
    return MTLK_ERR_NO_RESOURCES;
  }

  man_entry->id = UM_MAN_MSDU_IN_AMSDU_CONFIG_REQ;
  man_entry->payload_size = sizeof(UMI_MSDU_IN_AMSDU_CONFIG);

  pAMSDUNum = (UMI_MSDU_IN_AMSDU_CONFIG *)(man_entry->payload);
  pAMSDUNum->htMsduInAmsdu = (uint8)htMsduInAmsdu;
  pAMSDUNum->vhtMsduInAmsdu = (uint8)vhtMsduInAmsdu;
  pAMSDUNum->heMsduInAmsdu = (uint8)heMsduInAmsdu;

  res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);
  if (MTLK_ERR_OK != res) {
    ELOG_DD("CID-%04x: Set AMSDU Enable failure (%i)", mtlk_vap_get_oid(vap_handle), res);
  }

  mtlk_txmm_msg_cleanup(&man_msg);
  return res;
}

void __MTLK_IFUNC
core_on_rcvry_error (mtlk_core_t *core)
{
  if (mtlk_vap_is_master(core->vap_handle)) {
    wave_core_sync_hostapd_done_set(core, TRUE);
    ILOG1_D("CID-%04x: Recovery: Set HOSTAPD STA database synced to TRUE", mtlk_vap_get_oid(core->vap_handle));
  }
}

static void
mtlk_core_on_rcvry_reset_counters(mtlk_core_t *core)
{
  /* Reset data packet confirmation counter */
  mtlk_osal_atomic_set(&core->unconfirmed_data, 0);

  /* Reset management packet sent/confirmed counters */
  _mtlk_core_reset_cnt(core, MTLK_CORE_CNT_MAN_FRAMES_SENT);
  _mtlk_core_reset_cnt(core, MTLK_CORE_CNT_MAN_FRAMES_CONFIRMED);

  /* Reserved queue is cleaned up in mtlk_hw_mmb_restore, so just reset counter */
  _mtlk_core_reset_cnt(core, MTLK_CORE_CNT_MAN_FRAMES_RES_QUEUE);
}

int __MTLK_IFUNC
core_on_rcvry_restore (mtlk_core_t *core, uint32 rcvry_type)
{
  /* Restore CORE's data & variables
     not related with current configuration
  */
  int res = MTLK_ERR_OK;

  res = mtlk_core_set_net_state(core, NET_STATE_IDLE);
  if (res != MTLK_ERR_OK) {
    return res;
  }

  if (mtlk_vap_is_master(core->vap_handle)) {
    wave_radio_t *radio = wave_vap_radio_get(core->vap_handle);

    /* Restore WATCHDOG timer */
    mtlk_osal_timer_set(&core->slow_ctx->mac_watchdog_timer,
                        WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_MAC_WATCHDOG_TIMER_PERIOD_MS));

    wave_radio_progmodel_loaded_set(radio, FALSE);
    wave_radio_last_pm_freq_set(radio, MTLK_HW_BAND_NONE);

    /* Reset Radio Calibration context */
    if (RCVRY_TYPE_FULL == rcvry_type) {
      struct mtlk_chan_def  *ccd = wave_radio_chandef_get(radio);
      struct scan_support   *ss  = wave_radio_scan_support_get(radio);

      /* Make channel uncertain */
      ccd->center_freq1 = 0;

      /* Reset previously calibrated channels list */
      memset(ss->css, 0, sizeof(ss->css));
    }
  }

  /* Reset required counters */
  mtlk_core_on_rcvry_reset_counters (core);

#ifdef MTLK_LEGACY_STATISTICS
  /* register back irb handler */
  core->slow_ctx->stat_irb_handle =
    mtlk_wssd_register_request_handler(mtlk_vap_get_irbd(core->vap_handle),
                                       _mtlk_core_stat_handle_request,
                                       HANDLE_T(core->slow_ctx));
  if (core->slow_ctx->stat_irb_handle == NULL) {
    return res;
  }
#endif

  res = mtlk_core_set_net_state(core, NET_STATE_READY);
  if (res != MTLK_ERR_OK) {
    ELOG_D("Set net state failed. res=%d", res);
    return res;
  }

  return MTLK_ERR_OK;
}

static int core_on_rcvry_security(mtlk_core_t *core)
{
  int res;
  int i;

  for (i = 0; i < MIB_WEP_N_DEFAULT_KEYS; i++) {
    res = _core_set_umi_key(core, i);
    if (res != MTLK_ERR_OK) {
      ELOG_DD("CID-%04x: Recovery failed. res=%d", mtlk_vap_get_oid(core->vap_handle), res);
      return res;
    }
  }

  res = core_recovery_default_key(core, FALSE);
  if (res != MTLK_ERR_OK) {
    ELOG_DD("CID-%04x: Recovery failed. res=%d", mtlk_vap_get_oid(core->vap_handle), res);
    return res;
  }

  return res;
}

static int core_recovery_cfg_change_bss(mtlk_core_t *core)
{
  int res;
  mtlk_txmm_msg_t man_msg;
  mtlk_txmm_data_t *man_entry;
  UMI_SET_BSS *req;
  uint16 beacon_int, dtim_int;
  uint8 vap_id = mtlk_vap_get_id(core->vap_handle);
  mtlk_pdb_t *param_db_core = mtlk_vap_get_param_db(core->vap_handle);
  struct nic *master_core = mtlk_vap_manager_get_master_core(mtlk_vap_get_manager(core->vap_handle));
  struct mtlk_chan_def *current_chandef = __wave_core_chandef_get(master_core);
  wave_radio_t *radio = wave_vap_radio_get(core->vap_handle);

  /* prepare msg for the FW */
  if (!(man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, mtlk_vap_get_txmm(core->vap_handle), &res)))
  {
    ELOG_DD("CID-%04x: UM_BSS_SET_BSS_REQ init failed, err=%i",
           mtlk_vap_get_oid(core->vap_handle), res);
    return MTLK_ERR_NO_RESOURCES;
  }

  man_entry->id = UM_BSS_SET_BSS_REQ;
  man_entry->payload_size = sizeof(*req);
  req = (UMI_SET_BSS *) man_entry->payload;
  memset(req, 0, sizeof(*req));
  req->vapId = vap_id;

  ILOG4_V("Dealing w/ protection");
  req->protectionMode = MTLK_CORE_PDB_GET_INT(core, PARAM_DB_CORE_HT_PROTECTION);

  ILOG4_V("Dealing w/ short slot");
  req->slotTime = WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_SHORT_SLOT);
  req->useShortPreamble = WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_SHORT_PREAMBLE);

  /* DTIM and Beacon interval are set in start_ap and stored in param_db */
  ILOG4_V("Dealing with DTIM and Beacon intervals");
  beacon_int = (uint16) WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_BEACON_PERIOD);
  dtim_int = (uint16) MTLK_CORE_PDB_GET_INT(core, PARAM_DB_CORE_DTIM_PERIOD);
  req->beaconInterval = HOST_TO_MAC16(beacon_int);
  req->dtimInterval = HOST_TO_MAC16(dtim_int);

  ILOG4_V("Dealing w/ basic rates");
  req->u8Rates_Length = (uint8)wave_core_param_db_basic_rates_get(core, current_chandef->chan.band,
                                                                  req->u8Rates, sizeof(req->u8Rates));

  ILOG4_V("Dealing w/ Fixed MCS VAP management");
  req->fixedMcsVapManagement = (uint8)_core_management_frames_rate_select(core);

  ILOG4_V("Dealing w/ HT/VHT MCS-s and flags");
  {
    mtlk_pdb_size_t mcs_len = sizeof(req->u8TX_MCS_Bitmask);
    wave_pdb_get_binary(param_db_core, PARAM_DB_CORE_RX_MCS_BITMASK, &req->u8TX_MCS_Bitmask, &mcs_len);
    mcs_len = sizeof(req->u8VHT_Mcs_Nss);
    wave_pdb_get_binary(param_db_core, PARAM_DB_CORE_VHT_MCS_NSS, &req->u8VHT_Mcs_Nss, &mcs_len);
    req->flags = wave_pdb_get_int(param_db_core, PARAM_DB_CORE_SET_BSS_FLAGS);

    if (wave_pdb_get_int(param_db_core, PARAM_DB_CORE_RELIABLE_MCAST)) {
      req->flags |= MTLK_BFIELD_VALUE(VAP_ADD_FLAGS_RELIABLE_MCAST, 1, uint8);
    }

    if (mtlk_df_user_get_hs20_status(mtlk_vap_get_df(core->vap_handle))) {
      req->flags |= MTLK_BFIELD_VALUE(VAP_ADD_FLAGS_HS20_ENABLE, 1, uint8);
    }
  }

  mtlk_dump(2, req, sizeof(*req), "dump of UMI_SET_BSS:");

  res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);

  if (res != MTLK_ERR_OK) {
      ELOG_DD("CID-%04x: UM_BSS_SET_BSS_REQ send failed, err=%i",
              mtlk_vap_get_oid(core->vap_handle), res);
  } else {
      if (mtlk_core_get_net_state(core) != NET_STATE_CONNECTED) {
        res = mtlk_core_set_net_state(core, NET_STATE_CONNECTED);
      }
  }

  mtlk_txmm_msg_cleanup(&man_msg);

  return res;
}

static __INLINE BOOL
__mtlk_core_11b_antsel_configured (mtlk_11b_antsel_t *antsel)
{
  MTLK_ASSERT(antsel != NULL);

  return antsel->txAnt != MTLK_PARAM_DB_INVALID_UINT8 &&
         antsel->rxAnt != MTLK_PARAM_DB_INVALID_UINT8 &&
         antsel->rate  != MTLK_PARAM_DB_INVALID_UINT8;
}

static int
_mtlk_core_set_11b_antsel (mtlk_core_t *core)
{
  wave_radio_t *radio = wave_vap_radio_get(core->vap_handle);
  mtlk_11b_antsel_t antsel;

  memset(&antsel, 0, sizeof(antsel));
  antsel.txAnt = WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_11B_ANTSEL_TXANT);
  antsel.rxAnt = WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_11B_ANTSEL_RXANT);
  antsel.rate =  WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_11B_ANTSEL_RATE);

  if (!__mtlk_core_11b_antsel_configured(&antsel))
    return MTLK_ERR_OK;

  return _mtlk_core_send_11b_antsel(core, &antsel);
}

static int
mtlk_core_rcvry_set_channel (mtlk_core_t *core)
{
  mtlk_handle_t ccd_handle;
  struct mtlk_chan_def *ccd;

  wave_rcvry_chandef_current_get(mtlk_vap_get_hw(core->vap_handle),
                                 mtlk_vap_get_manager(core->vap_handle),
                                 &ccd_handle);
  ccd = (struct mtlk_chan_def *)ccd_handle;
  if (NULL == ccd)
    return MTLK_ERR_NOT_SUPPORTED;

  /* In case FW crashed for example during SET_CHAN_REQ */
  if (!is_channel_certain(ccd)) {
    ccd = __wave_core_chandef_get(core);
  }
  return core_cfg_set_chan(core, ccd, NULL);
}

static int
mtlk_core_cfg_recovery_set_coex (mtlk_core_t *core)
{
  wave_radio_t *radio = wave_vap_radio_get(core->vap_handle);
  uint8 coex_enabled = (uint8)WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_COEX_ENABLED);
  uint8 coex_mode    = (uint8)WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_COEX_MODE);

  if (MTLK_ERR_OK != mtlk_core_is_band_supported(core, UMI_PHY_TYPE_BAND_2_4_GHZ))
    return MTLK_ERR_OK;

  return mtlk_core_send_coex_config_req(core, coex_mode, coex_enabled);
}

static int _mtlk_core_rcvry_set_freq_jump_mode (mtlk_core_t *core)
{
  int res = MTLK_ERR_OK;
  uint32 freq_jump_mode = WAVE_RADIO_PDB_GET_INT(wave_vap_radio_get(core->vap_handle),
                                                          PARAM_DB_RADIO_FREQ_JUMP_MODE);

  /* First message to FW must not contain default value */
  if (MTLK_FREQ_JUMP_MODE_DEFAULT != freq_jump_mode) {
    res = mtlk_core_cfg_send_freq_jump_mode(core, freq_jump_mode);
  }
  return res;
}

static int _mtlk_core_rcvry_set_pd_threshold (mtlk_core_t *core)
{
  UMI_SET_PD_THRESH pd_thresh;

  mtlk_core_cfg_get_pd_threshold(core, &pd_thresh);

  if (QOS_PD_THRESHOLD_NUM_MODES == pd_thresh.mode)
    return MTLK_ERR_OK;

  return mtlk_core_cfg_send_pd_threshold(core, &pd_thresh);
}

static int _mtlk_core_rcvry_set_resticted_ac_mode (mtlk_core_t *core)
{
  UMI_SET_RESTRICTED_AC restricted_ac_mode;

  mtlk_core_cfg_get_restricted_ac_mode(core, &restricted_ac_mode);

  if (MTLK_PARAM_DB_VALUE_IS_INVALID(restricted_ac_mode.restrictedAcModeEnable))
    return MTLK_ERR_OK;

  return mtlk_core_cfg_send_restricted_ac_mode(core, &restricted_ac_mode);
}

static int _mtlk_core_recover_aggr_cfg (mtlk_core_t *core)
{
  uint8 amsdu_mode   = MTLK_CORE_PDB_GET_INT(core, PARAM_DB_CORE_AMSDU_MODE);
  uint8 ba_mode      = MTLK_CORE_PDB_GET_INT(core, PARAM_DB_CORE_BA_MODE);
  uint32 window_size = MTLK_CORE_PDB_GET_INT(core, PARAM_DB_CORE_WINDOW_SIZE);


  if (MTLK_PARAM_DB_VALUE_IS_INVALID(amsdu_mode) ||
      MTLK_PARAM_DB_VALUE_IS_INVALID(ba_mode) ||
      MTLK_PARAM_DB_VALUE_IS_INVALID(window_size))
    return MTLK_ERR_OK;

  return _mtlk_core_set_aggr_cfg_req(core, amsdu_mode, ba_mode, window_size);
}

static int
_mtlk_core_recover_agg_rate_limit (mtlk_core_t *core)
{
  mtlk_agg_rate_limit_cfg_t agg_rate_cfg;

  _mtlk_core_read_agg_rate_limit(core, &agg_rate_cfg);

  if (MTLK_PARAM_DB_VALUE_IS_INVALID(agg_rate_cfg.agg_rate_limit.mode))
      return MTLK_ERR_OK;

  return mtlk_core_set_agg_rate_limit(core, &agg_rate_cfg);
}

static int
_mtlk_core_recover_max_mpdu_length (mtlk_core_t *core)
{
  uint32 length = mtlk_core_cfg_read_max_mpdu_length(core);

  if (MTLK_PARAM_DB_VALUE_IS_INVALID(length))
    return MTLK_ERR_OK;

  return mtlk_core_cfg_send_max_mpdu_length(core, length);
}

static int
_mtlk_core_recover_mu_operation (mtlk_core_t *core)
{
  uint8 mu_operation = WAVE_RADIO_PDB_GET_INT(wave_vap_radio_get(core->vap_handle), PARAM_DB_RADIO_MU_OPERATION);

  if (MTLK_PARAM_DB_VALUE_IS_INVALID(mu_operation))
    return MTLK_ERR_OK;

  return mtlk_core_set_mu_operation(core, mu_operation);
}

static int _core_recover_he_mu_operation (mtlk_core_t *core)
{
  int res = MTLK_ERR_OK;

  if (!mtlk_hw_type_is_gen5(mtlk_vap_get_hw(core->vap_handle))) {
    res = _mtlk_core_set_he_mu_operation(core,
                                         WAVE_RADIO_PDB_GET_INT(wave_vap_radio_get(core->vap_handle),
                                         PARAM_DB_RADIO_HE_MU_OPERATION));
  }
  return res;
}

static int _mtlk_core_recover_amsdu_cfg (mtlk_core_t *core)
{
  wave_radio_t *radio = wave_vap_radio_get(core->vap_handle);
  uint32 amsdu_num  = WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_AMSDU_NUM);
  uint32 amsdu_vnum = WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_AMSDU_VNUM);
  uint32 amsdu_henum = WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_AMSDU_HENUM);

  if (MTLK_PARAM_DB_VALUE_IS_INVALID(amsdu_num) ||
      MTLK_PARAM_DB_VALUE_IS_INVALID(amsdu_vnum) ||
      MTLK_PARAM_DB_VALUE_IS_INVALID(amsdu_henum))
      return MTLK_ERR_OK;

  return _mtlk_core_set_amsdu_num_req(core, amsdu_num, amsdu_vnum, amsdu_henum);
}

static int _mtlk_core_recover_qamplus_mode (mtlk_core_t *core)
{
  uint8 qam_plus_mode = WAVE_RADIO_PDB_GET_INT(wave_vap_radio_get(core->vap_handle), PARAM_DB_RADIO_QAMPLUS_MODE);

  if (MTLK_PARAM_DB_VALUE_IS_INVALID(qam_plus_mode))
    return MTLK_ERR_OK;

  return _mtlk_core_qamplus_mode_req(core, qam_plus_mode);
}

static int _mtlk_recover_rts_mode (mtlk_core_t *core)
{
  uint8 dynamic_bw, static_bw;
  wave_radio_t *radio = wave_vap_radio_get(core->vap_handle);

  dynamic_bw = WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_RTS_DYNAMIC_BW);
  static_bw  = WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_RTS_STATIC_BW);

  if (MTLK_PARAM_DB_VALUE_IS_INVALID(dynamic_bw) ||
      MTLK_PARAM_DB_VALUE_IS_INVALID(static_bw))
    return MTLK_ERR_OK;

  return mtlk_core_set_rts_mode(core, dynamic_bw, static_bw);
}

static int _mtlk_core_recover_rx_duty_cycle (mtlk_core_t *core)
{
  uint32 on_time, off_time;
  mtlk_rx_duty_cycle_cfg_t duty_cycle_cfg;
  wave_radio_t *radio = wave_vap_radio_get(core->vap_handle);

  memset(&duty_cycle_cfg, 0, sizeof(duty_cycle_cfg));

  on_time  = WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_RX_DUTY_CYCLE_ON_TIME);
  off_time = WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_RX_DUTY_CYCLE_OFF_TIME);

  if (MTLK_PARAM_DB_VALUE_IS_INVALID(on_time) ||
      MTLK_PARAM_DB_VALUE_IS_INVALID(off_time))
    return MTLK_ERR_OK;

  duty_cycle_cfg.duty_cycle.onTime  = on_time;
  duty_cycle_cfg.duty_cycle.offTime = off_time;

  return mtlk_core_set_rx_duty_cycle(core, &duty_cycle_cfg);
}

static int _mtlk_core_recover_rx_threshold (mtlk_core_t *core)
{
  uint32 rx_thr = MTLK_CORE_PDB_GET_INT(core, PARAM_DB_CORE_RX_THRESHOLD);
  mtlk_rx_th_cfg_t rx_th_cfg;

  if (MTLK_PARAM_DB_VALUE_IS_INVALID(rx_thr))
    return MTLK_ERR_OK;

  /* Validated on iwpriv setter */
  rx_th_cfg.rx_threshold = (int8)rx_thr;

  return mtlk_core_set_rx_threshold(core, &rx_th_cfg);
}

static int _mtlk_core_recover_slow_probing_mask (mtlk_core_t *core)
{
  uint32 mask = WAVE_RADIO_PDB_GET_INT(wave_vap_radio_get(core->vap_handle), PARAM_DB_RADIO_SLOW_PROBING_MASK);

  if (MTLK_PARAM_DB_VALUE_IS_INVALID(mask))
    return MTLK_ERR_OK;

  return mtlk_core_cfg_send_slow_probing_mask(core, mask);
}

static int _mtlk_core_recover_ssb_mode (mtlk_core_t *core)
{
  mtlk_ssb_mode_cfg_t ssb_mode_cfg;
  uint32              ssb_mode_cfg_data_size = MTLK_SSB_MODE_CFG_SIZE;

  mtlk_core_read_ssb_mode(core, &ssb_mode_cfg, &ssb_mode_cfg_data_size);

  if (MTLK_PARAM_DB_VALUE_IS_INVALID(ssb_mode_cfg.params[MTLK_SSB_MODE_CFG_EN]) ||
      MTLK_PARAM_DB_VALUE_IS_INVALID(ssb_mode_cfg.params[MTLK_SSB_MODE_CFG_20MODE]))
    return MTLK_ERR_OK;

  return mtlk_core_send_ssb_mode(core, &ssb_mode_cfg);
}

static int _mtlk_core_recover_txop_mode (mtlk_core_t *core)
{
  wave_radio_t *radio = wave_vap_radio_get(core->vap_handle);
  uint8 mode = WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_TXOP_MODE);
  uint32 sid = WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_TXOP_SID);

  if (MTLK_PARAM_DB_VALUE_IS_INVALID(mode))
    return MTLK_ERR_OK;

  return mtlk_core_send_txop_mode(core, sid, mode);
}

static int _mtlk_core_recover_admission_capacity (mtlk_core_t *core)
{
  uint32 admission_capacity = MTLK_CORE_PDB_GET_INT(core, PARAM_DB_CORE_ADMISSION_CAPACITY);

  if (MTLK_PARAM_DB_VALUE_IS_INVALID(admission_capacity))
    return MTLK_ERR_OK;

  return mtlk_core_set_admission_capacity(core, admission_capacity);
}

static int _mtlk_core_recover_fast_drop (mtlk_core_t *core)
{
  uint8 fast_drop =  WAVE_RADIO_PDB_GET_INT(wave_vap_radio_get(core->vap_handle), PARAM_DB_RADIO_FAST_DROP);

  if (MTLK_PARAM_DB_VALUE_IS_INVALID(fast_drop))
    return MTLK_ERR_OK;

  return mtlk_core_cfg_send_fast_drop(core, fast_drop);
}

static int _mtlk_recover_bf_mode (mtlk_core_t *core)
{
  uint8 bf_mode = WAVE_RADIO_PDB_GET_INT(wave_vap_radio_get(core->vap_handle), PARAM_DB_RADIO_BF_MODE);

  return _mtlk_core_send_bf_mode(core, bf_mode);
}

static int _mtlk_core_recover_interfdet_threshold (mtlk_core_t *core)
{
  BOOL is_spectrum_40 = (CW_40 == _mtlk_core_get_spectrum_mode(core));
  wave_radio_t *radio = wave_vap_radio_get(core->vap_handle);
  int8 thr;

  if (is_spectrum_40)
    thr = (int8)WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_INTERFDET_40MHZ_NOTIFICATION_THRESHOLD);
  else
    thr = (int8)WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_INTERFDET_20MHZ_NOTIFICATION_THRESHOLD);

  if (MTLK_INTERFDET_THRESHOLD_INVALID == thr)
    return MTLK_ERR_OK;

  return _mtlk_core_set_fw_interfdet_req(core, is_spectrum_40);
}

static int _mtlk_core_recover_dynamic_mc_rate (mtlk_core_t *core)
{
  uint32 dynamic_mc_rate = WAVE_RADIO_PDB_GET_INT(wave_vap_radio_get(core->vap_handle), PARAM_DB_RADIO_DYNAMIC_MC_RATE);

  if (MTLK_PARAM_DB_VALUE_IS_INVALID(dynamic_mc_rate))
    return MTLK_ERR_OK;

  return _mtlk_core_send_dynamic_mc_rate(core, dynamic_mc_rate);
}

static void
_mtlk_core_rcvry_chan_switch_notify (mtlk_core_t *core)
{
  mtlk_df_t         *df       = mtlk_vap_get_df(core->vap_handle);
  mtlk_df_user_t    *df_user  = mtlk_df_get_user(df);
  struct net_device *ndev     = mtlk_df_user_get_ndev(df_user);

  wv_cfg80211_ch_switch_notify(ndev, __wave_core_chandef_get(core), FALSE);
}

#define RECOVERY_INFO(text, oid)    do { ILOG1_DS("CID-%04x: Recovery %s", oid, text); } while(0)
#define RECOVERY_WARN(text, oid)    do { WLOG_DS("CID-%04x: Recovery %s", oid, text);  } while(0)
static int _core_on_rcvry_configure (mtlk_core_t *core, uint32 target_net_state)
{
  int res = MTLK_ERR_OK;
  uint32                    db_value;
  uint32                    active_ant_mask;
  mtlk_hw_band_e            band;
  uint16 core_oid                       = mtlk_vap_get_oid(core->vap_handle);
  mtlk_core_t *master_core              = mtlk_core_get_master(core);
  struct mtlk_chan_def *current_chandef = __wave_core_chandef_get(master_core);
  mtlk_scan_support_t *ss               = mtlk_core_get_scan_support(core);
  mtlk_coc_t *coc_mgmt                  = __wave_core_coc_mgmt_get(core);
  wave_radio_t *radio                   = wave_vap_radio_get(core->vap_handle);
  mtlk_hw_t *hw = mtlk_vap_get_hw(core->vap_handle);
  mtlk_vap_manager_t *vap_manager = mtlk_vap_get_manager(core->vap_handle);

  MTLK_ASSERT(master_core != NULL);
  MTLK_ASSERT(current_chandef != NULL);

  MTLK_UNUSED_VAR(core_oid);
  ILOG1_DS("CID-%04x: Recovery: target_net_state: %s", core_oid, mtlk_net_state_to_string(target_net_state));

  /* Set all MIBs, beside of security & preactivation MIBs
   * all these MIBS are sent on core creation*/
  RECOVERY_INFO("set vap mibs", core_oid);
  res = mtlk_set_vap_mibs(core);
  if (res != MTLK_ERR_OK) {goto ERR_END;}

  /* Set before Activating VAP */

  RECOVERY_INFO("Aggregation config", core_oid);
  res = _mtlk_core_recover_aggr_cfg(core);
  if (res != MTLK_ERR_OK) {goto ERR_END;}

  RECOVERY_INFO("Admission Capacity", core_oid);
  res = _mtlk_core_recover_admission_capacity(core);
  if (res != MTLK_ERR_OK) {goto ERR_END;}

  if (mtlk_vap_is_master(core->vap_handle)) {
    /* Gen6 specific */
    if (wave_radio_is_gen6(radio)) {
      /* HW card abilities are for the first radio */
      if (wave_radio_is_first(radio)) {
        /* Test Bus */
        db_value = WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_TEST_BUS_MODE);
        if (!MTLK_PARAM_DB_VALUE_IS_INVALID(db_value)) {
        RECOVERY_INFO("Enable Test Bus", core_oid);
          res = wave_core_send_test_bus_mode(core, db_value);
          if (res != MTLK_ERR_OK) {goto ERR_END;}
        }
      }
    }

    RECOVERY_INFO("Enable radar detection", core_oid);
    res = _mtlk_core_set_radar_detect(core, WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_DFS_RADAR_DETECTION));
    if (res != MTLK_ERR_OK) {goto ERR_END;}

    RECOVERY_INFO("Set aggr limits", core_oid);
    res = _mtlk_core_recover_agg_rate_limit(core);
    if (res != MTLK_ERR_OK) {goto ERR_END;}

    RECOVERY_INFO("MAX MPDU length", core_oid);
    res = _mtlk_core_recover_max_mpdu_length(core);
    if (res != MTLK_ERR_OK) {goto ERR_END;}

    RECOVERY_INFO("AMSDU config", core_oid);
    res = _mtlk_core_recover_amsdu_cfg(core);
    if (res != MTLK_ERR_OK) {goto ERR_END;}

    RECOVERY_INFO("MU operation", core_oid);
    res = _mtlk_core_recover_mu_operation(core);
    if (res != MTLK_ERR_OK) {goto ERR_END;}

    RECOVERY_INFO("HE MU operation", core_oid);
    res = _core_recover_he_mu_operation(core);
    if (res != MTLK_ERR_OK) {goto ERR_END;}

    RECOVERY_INFO("RTS mode", core_oid);
    res = _mtlk_recover_rts_mode(core);
    if (res != MTLK_ERR_OK) { goto ERR_END; }

    RECOVERY_INFO("11b antsel", core_oid);
    res = _mtlk_core_set_11b_antsel(core);
    if (res != MTLK_ERR_OK) { goto ERR_END; }

    RECOVERY_INFO("BF Mode", core_oid);
    res = _mtlk_recover_bf_mode(core);
    if (res != MTLK_ERR_OK) {goto ERR_END;}

    RECOVERY_INFO("SSB Mode", core_oid);
    res = _mtlk_core_recover_ssb_mode(core);
    if (res != MTLK_ERR_OK) {goto ERR_END;}

    RECOVERY_INFO("Frequency Jump Mode", core_oid);
    res = _mtlk_core_rcvry_set_freq_jump_mode(core);
    if (res != MTLK_ERR_OK) {goto ERR_END;}

    RECOVERY_INFO("QOS PD Threshold", core_oid);
    res = _mtlk_core_rcvry_set_pd_threshold(core);
    if (res != MTLK_ERR_OK) {goto ERR_END;}

    RECOVERY_INFO("Restricted AC Mode", core_oid);
    res = _mtlk_core_rcvry_set_resticted_ac_mode(core);
    if (res != MTLK_ERR_OK) {goto ERR_END;}

    RECOVERY_INFO("Fast Drop", core_oid);
    res = _mtlk_core_recover_fast_drop(core);
    if (res != MTLK_ERR_OK) {goto ERR_END;}

    /* CCA Threshold */
    RECOVERY_INFO("set CCA Threshold", core_oid);
    res = mtlk_core_cfg_recovery_cca_threshold(core);
    if (res != MTLK_ERR_OK) {goto ERR_END;}

    /* adaptive CCA Threshold */
    RECOVERY_INFO("set CCA Adaptive Intervals", core_oid);
    res = mtlk_core_cfg_recovery_cca_adapt(core);
    if (res != MTLK_ERR_OK) { goto ERR_END; }

    /* 2.4 GHz Coexistance */
    RECOVERY_INFO("Set Coexistance configuration", core_oid);
    res = mtlk_core_cfg_recovery_set_coex(core);
    if (res != MTLK_ERR_OK) {goto ERR_END;}

    /* TX Power limit configuration */
    RECOVERY_INFO("set power limits", core_oid);
    res = _mtlk_core_send_tx_power_limit_offset(core,
            POWER_TO_DBM(WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_TX_POWER_LIMIT_OFFSET)));
    if (res != MTLK_ERR_OK) {goto ERR_END;}

    /* QAMplus mode configuration */
    RECOVERY_INFO("QAMplus mode", core_oid);
    res = _mtlk_core_recover_qamplus_mode(core);
    if (res != MTLK_ERR_OK) {goto ERR_END;}

    /* Reception Duty Cycle configuration */
    RECOVERY_INFO("set duty cycle", core_oid);
    res = _mtlk_core_recover_rx_duty_cycle(core);
    if (res != MTLK_ERR_OK) {goto ERR_END;}

    /* High Reception Threshold configuration */
    RECOVERY_INFO("set rx_thresold", core_oid);
    res = _mtlk_core_recover_rx_threshold(core);
    if (res != MTLK_ERR_OK) {goto ERR_END;}

    /* Slow Probing Mask */
    RECOVERY_INFO("Slow Probing Mask", core_oid);
    res = _mtlk_core_recover_slow_probing_mask(core);
    if (res != MTLK_ERR_OK) { goto ERR_END; }

    /* TXOP mode */
    RECOVERY_INFO("set TXOP mode", core_oid);
    res = _mtlk_core_recover_txop_mode(core);
    if (res != MTLK_ERR_OK) { goto ERR_END; }

    RECOVERY_INFO("Multicast", core_oid);
    res = _mtlk_core_recovery_reliable_multicast(core);
    if (res != MTLK_ERR_OK) {goto ERR_END;}

    /* Radar Detection Threshold */
    RECOVERY_INFO("set Radar Detection Threshold", core_oid);
    res = mtlk_core_cfg_recovery_radar_rssi_threshold(core);
    if (res != MTLK_ERR_OK) {goto ERR_END;}

    /* Dynamic Multicast Rate */
    RECOVERY_INFO("set Dynamic Multicast Rate", core_oid);
    res = _mtlk_core_recover_dynamic_mc_rate(core);
    if (res != MTLK_ERR_OK) {goto ERR_END;}

    /* MAC Fatal occurred during scan, but before VAP was activated (NET_STATE_READY) */
    if (RCVRY_FG_SCAN == wave_rcvry_scan_type_current_get(hw, vap_manager) &&
        (target_net_state & NET_STATE_READY)) {
      RECOVERY_INFO("Resuming foreground scan", core_oid);
      res = mtlk_scan_recovery_continue_scan(core, MTLK_SCAN_SUPPORT_NO_BG_SCAN);
      if (res != MTLK_ERR_OK) {goto ERR_END;}
    }

    RECOVERY_INFO("RTS protection cutoff point", core_oid);
    res = wave_core_cfg_recover_cutoff_point(core);
    if (res != MTLK_ERR_OK) {goto ERR_END;}
  }

  /* VAP was not previously activated, nothing to configure */
  if (!(target_net_state & (NET_STATE_ACTIVATING | NET_STATE_CONNECTED))) {
    goto ERR_OK;
  }

  RECOVERY_INFO("vap activate", core_oid);
  band = wave_radio_band_get(radio);

  if (!is_channel_certain(current_chandef) &&
      RCVRY_FG_SCAN == wave_rcvry_scan_type_current_get(hw, vap_manager)) {
    band = ss->req.channels[0].band; /* Band wasn't set yet, so get from original scan request */
  }

  res = mtlk_mbss_send_vap_activate(core, band);
  if (res != MTLK_ERR_OK) {goto ERR_END;}
  /* net state is ACTIVATING */

  RECOVERY_INFO("stadb start", core_oid);
  mtlk_stadb_start(&core->slow_ctx->stadb);
  if (res != MTLK_ERR_OK) {goto ERR_END;}

  RECOVERY_INFO("restore security", core_oid);
  res = core_on_rcvry_security(core);
  if (res != MTLK_ERR_OK) {goto ERR_END;}

  if (mtlk_vap_is_master(core->vap_handle)) {
    /* WDS */
    if (BR_MODE_WDS == MTLK_CORE_HOT_PATH_PDB_GET_INT(core, CORE_DB_CORE_BRIDGE_MODE)) {
      RECOVERY_INFO("restore WDS", core_oid);
      wds_on_if_up(&core->slow_ctx->wds_mng);
    }

    /* IRE Control */
    RECOVERY_INFO("set IRE Control", core_oid);
    res = mtlk_core_cfg_recovery_ire_ctrl(core);
    if (res != MTLK_ERR_OK) {goto ERR_END;}

    /* Continue foreground scan */
    if (RCVRY_FG_SCAN == wave_rcvry_scan_type_current_get(hw, vap_manager)) {
      RECOVERY_INFO("Resuming foreground scan", core_oid);
      res = mtlk_scan_recovery_continue_scan(core, MTLK_SCAN_SUPPORT_NO_BG_SCAN);
      if (res != MTLK_ERR_OK) { goto ERR_END; }
    }

    /* Restart CAC-procedure */
    if (wave_rcvry_restart_cac_get(hw, vap_manager)) {
      struct set_chan_param_data *pcpd = (struct set_chan_param_data *)wave_rcvry_chanparam_get(hw, vap_manager);
      wave_rcvry_restart_cac_set(hw, vap_manager, FALSE);
      if (NULL != pcpd) {
        RECOVERY_INFO("restart CAC-procedure", core_oid);
        /* CAC-timer will be restarted by this function */
        res = core_cfg_set_chan(core, &pcpd->chandef, pcpd);
        if (res != MTLK_ERR_OK) {goto ERR_END;}
      }
    }
  }

  /* Continue only if NET_STATE_CONNECTED */
  if (!(target_net_state & NET_STATE_CONNECTED)) {
    goto ERR_OK;
  }

  if (mtlk_vap_is_master(core->vap_handle)) {
    RECOVERY_INFO("set channel", core_oid);
    res = mtlk_core_rcvry_set_channel(core);
    if (res != MTLK_ERR_OK) {goto ERR_END;}
  }

  if (mtlk_vap_is_ap(core->vap_handle)) {
    /* When master VAP is used as a dummy VAP (in order to support reconf
     * of all VAPs without having to restart hostapd) we avoid setting beacon
     * so that the dummy VAPs will not send beacons */
    if (WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_DISABLE_MASTER_VAP) &&
        mtlk_vap_is_master(core->vap_handle)){
      RECOVERY_INFO("Dummy (disabled) Master VAP, not setting beacon template in FW",
                    core_oid);
    } else {
      RECOVERY_INFO("set beacon template", core_oid);
      res = wave_beacon_man_rcvry_beacon_set(core);
      if (res != MTLK_ERR_OK) {goto ERR_END;}
    }
  }

  RECOVERY_INFO("set bss", core_oid);
  res = core_recovery_cfg_change_bss(core);
  if (res != MTLK_ERR_OK) {goto ERR_END;}

  /* net state is CONNECTED */

  RECOVERY_INFO("set wmm", core_oid);
  /*Set after state CONNECTED */
  res = core_recovery_cfg_wmm_param_set(core);
  if (res != MTLK_ERR_OK) {goto ERR_END;}

  if (mtlk_vap_is_master(core->vap_handle)) {
    RECOVERY_INFO("Enable radio", core_oid);
    res = _mtlk_core_set_radio_mode_req(core, WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_MODE_REQUESTED));
    if (res != MTLK_ERR_OK) {goto ERR_END;}

    /* CoC configuration */
    RECOVERY_INFO("COC start", core_oid);
    res = mtlk_coc_on_rcvry_configure(coc_mgmt);
    if (res != MTLK_ERR_OK) {goto ERR_END;}

    active_ant_mask = WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_ACTIVE_ANT_MASK);
    if (active_ant_mask) {
      RECOVERY_INFO("Active ant mask", core_oid);
      res = mtlk_core_cfg_send_active_ant_mask(core, active_ant_mask);
      if (res != MTLK_ERR_OK) {goto ERR_END;}
    }

    RECOVERY_INFO("Interference detection", core_oid);
    res = _mtlk_core_recover_interfdet_threshold(core);
    if (res != MTLK_ERR_OK) {goto ERR_END;}

    RECOVERY_INFO("Sync HOSTAPD STA database", core_oid);
    wave_core_sync_hostapd_done_set(core, FALSE);
    res = wv_cfg80211_handle_flush_stations(core->vap_handle, NULL, 0);
    if (res != MTLK_ERR_OK) {goto ERR_END;}
  }

  /* If FW crashed e.g. during SET_CHAN procedure, recovery could end up
   * with misaligned channel, beacons, etc. Notify hostapd to make sure
   * we have aligned beacons to the recovered channel */
  if (is_channel_certain(current_chandef)) {
    _mtlk_core_rcvry_chan_switch_notify(core);
  }
  else {
    RECOVERY_WARN("Channel switch notification not sent because channel is uncertain", core_oid);
  }

ERR_OK:
  return res;

ERR_END:
  ELOG_DD("CID-%04x: Recovery configuration fail. res=%d", core_oid, res);
  return res;
}

/* Restore CORE's and its sub modules configuration.
 * This function is intended for recovery configurable
 * parameters only. Non configurable parameters and
 * variables must be set in RESTORE function.
 */
int __MTLK_IFUNC
core_on_rcvry_configure (mtlk_core_t *core, uint32 was_connected)
{
  int res;
  uint16 oid = mtlk_vap_get_oid(core->vap_handle);

  MTLK_UNREFERENCED_PARAM(oid);

  ILOG1_DDS("CID-%04x: Net State before Recovery = %d (%s)", oid, was_connected,
            mtlk_net_state_to_string(was_connected));

  res = mtlk_mbss_send_vap_db_add(core);
  if (res != MTLK_ERR_OK) {
    ELOG_DD("CID-%04x: Recovery VAP_DB ADD failed. res=%d", oid, res);
    return res;
  }

  res = _core_on_rcvry_configure(core, was_connected);
  if (res != MTLK_ERR_OK) {
    ELOG_D("CID-%04x: Recovery configuration failed", oid);
  }
  return res;
}

void __MTLK_IFUNC
core_schedule_recovery_task (mtlk_vap_handle_t vap_handle, mtlk_core_task_func_t task,
            mtlk_handle_t rcvry_handle, const void* data, uint32 data_size)
{
    /* Just wrapper to put recovery task to the serializer */
    mtlk_core_t *core = mtlk_vap_get_core(vap_handle);

    _mtlk_process_emergency_task(core, task, rcvry_handle, data, data_size);

    return;
}

void __MTLK_IFUNC
mtlk_core_store_calibration_channel_bit_map (mtlk_core_t *core, uint32 *storedCalibrationChannelBitMap)
{
  wave_memcpy(core->storedCalibrationChannelBitMap,
         sizeof(core->storedCalibrationChannelBitMap),
         storedCalibrationChannelBitMap,
         sizeof(core->storedCalibrationChannelBitMap));
}

static int _core_sync_done (mtlk_handle_t hcore, const void* data, uint32 data_size)
{
  int res;
  mtlk_core_t *core = HANDLE_T_PTR(mtlk_core_t, hcore);
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  if (!mtlk_vap_is_master_ap(core->vap_handle)) {
    ELOG_S("%s: vap not master ap", __FUNCTION__);
    res = MTLK_ERR_NOT_SUPPORTED;
    goto Exit;
  }

  wave_core_sync_hostapd_done_set(core, TRUE);
  ILOG1_D("CID-%04x: Recovery: Set HOSTAPD STA database synced to TRUE", mtlk_vap_get_oid(core->vap_handle));
  res = MTLK_ERR_OK;

Exit:
  return mtlk_clpb_push_res(clpb, res);
}

int __MTLK_IFUNC
core_remove_sid(mtlk_core_t *core, uint16 sid)
{
  int res = MTLK_ERR_OK;
  mtlk_vap_handle_t vap_handle = core->vap_handle;
  mtlk_txmm_msg_t man_msg;
  mtlk_txmm_data_t *man_entry;
  UMI_REMOVE_SID *umi_rem_sid;

  /* prepare the msg to the FW */
  man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, mtlk_vap_get_txmm(vap_handle), &res);

  if (!man_entry) {
    ELOG_DD("CID-%04x: UM_MAN_REMOVE_SID_REQ init failed, err=%i",
            mtlk_vap_get_oid(vap_handle), res);
    return MTLK_ERR_NO_MEM;
  }

  man_entry->id = UM_MAN_REMOVE_SID_REQ;
  man_entry->payload_size = sizeof(UMI_REMOVE_SID);
  umi_rem_sid = (UMI_REMOVE_SID *) man_entry->payload;
  memset(umi_rem_sid, 0, sizeof(UMI_REMOVE_SID));
  umi_rem_sid->u16SID = HOST_TO_MAC16(sid);

  mtlk_dump(2, umi_rem_sid, sizeof(UMI_REMOVE_SID), "dump of UMI_REMOVE_SID before submitting to FW:");
  res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);
  mtlk_dump(2, umi_rem_sid, sizeof(UMI_REMOVE_SID), "dump of UMI_REMOVE_SID after submitting to FW:");

  if (res != MTLK_ERR_OK) {
    ELOG_DD("CID-%04x: UM_MAN_REMOVE_SID_REQ send failed, err=%i",
            mtlk_vap_get_oid(vap_handle), res);
  }
  else if (umi_rem_sid->u8Status) {
    ELOG_DD("CID-%04x: UM_MAN_REMOVE_SID_REQ execution failed, status=%hhu",
            mtlk_vap_get_oid(vap_handle), umi_rem_sid->u8Status);
    res = MTLK_ERR_MAC;
  }

  mtlk_txmm_msg_cleanup(&man_msg);

  return res;
}

uint32 __MTLK_IFUNC
mtlk_core_get_sid_by_addr(mtlk_core_t *nic, uint8 *addr)
{
  uint32 sid = DB_UNKNOWN_SID;
  sta_entry *sta = NULL;

  if (mtlk_osal_eth_is_multicast(addr)) /* Broadcast is a kind of multicast too */
  {
    sid = TMP_BCAST_DEST_ADDR;
  }
  else
  {
    sta = mtlk_stadb_find_sta(&nic->slow_ctx->stadb, addr);

    if (sta)
    {
      sid = mtlk_sta_get_sid(sta);
      mtlk_sta_decref(sta);
    }
  }

  ILOG3_DY("SID is=:%d STA:%Y", sid, addr);

  return sid;
}

int core_ap_stop_traffic(struct nic *nic, sta_info *info)
{
  int               res = MTLK_ERR_OK;
  mtlk_txmm_msg_t   man_msg;
  mtlk_txmm_data_t *man_entry = NULL;
  UMI_STOP_TRAFFIC *psUmiStopTraffic;

  man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, mtlk_vap_get_txmm(nic->vap_handle), &res);

  if (!man_entry) {
    ELOG_D("CID-%04x: Can't send STOP_TRAFFIC request to MAC: no MAN_MSG", mtlk_vap_get_oid(nic->vap_handle));
    return MTLK_ERR_NO_MEM;
  }

  man_entry->id           = UM_MAN_STOP_TRAFFIC_REQ;
  man_entry->payload_size = sizeof(UMI_STOP_TRAFFIC);
  memset(man_entry->payload, 0, man_entry->payload_size);
  psUmiStopTraffic = (UMI_STOP_TRAFFIC *) man_entry->payload;
  psUmiStopTraffic->u8Status = UMI_OK;
  psUmiStopTraffic->u16SID = HOST_TO_MAC16(info->sid);

  mtlk_dump(2, psUmiStopTraffic, sizeof(UMI_STA_REMOVE), "dump of UMI_STOP_TRAFFIC:");

  res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);
  if (res != MTLK_ERR_OK) {
    ELOG_DD("CID-%04x: Can't send UM_MAN_STOP_TRAFFIC_REQ request to MAC (err=%d)",
            mtlk_vap_get_oid(nic->vap_handle), res);
    goto finish;
  }
  if (psUmiStopTraffic->u8Status != UMI_OK) {
    WLOG_DYD("CID-%04x: Station %Y remove failed in FW (status=%u)",
             mtlk_vap_get_oid(nic->vap_handle), &info->addr, psUmiStopTraffic->u8Status);
    res = MTLK_ERR_MAC;
    goto finish;
  }

  ILOG1_DYD("CID-%04x: Station %Y traffic stopped (SID = %u)",
            mtlk_vap_get_oid(nic->vap_handle), &info->addr, info->sid);

finish:
  mtlk_txmm_msg_cleanup(&man_msg);
  return res;
}

int core_ap_remove_sta(struct nic *nic, sta_info *info)
{
  int               res = MTLK_ERR_OK;
  mtlk_vap_handle_t vap_handle = nic->vap_handle;
  mtlk_txmm_msg_t   man_msg;
  mtlk_txmm_data_t *man_entry = NULL;
  UMI_STA_REMOVE   *psUmiStaRemove;

  man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, mtlk_vap_get_txmm(vap_handle), &res);
  if (man_entry == NULL) {
    ELOG_D("CID-%04x: Can't send STA_REMOVE request to MAC: no MAN_MSG", mtlk_vap_get_oid(vap_handle));
    return MTLK_ERR_NO_MEM;
  }

  man_entry->id           = UM_MAN_STA_REMOVE_REQ;
  man_entry->payload_size = sizeof(UMI_STA_REMOVE);

  memset(man_entry->payload, 0, man_entry->payload_size);
  psUmiStaRemove = (UMI_STA_REMOVE *)man_entry->payload;

  psUmiStaRemove->u8Status = UMI_OK;
  psUmiStaRemove->u16SID = HOST_TO_MAC16(info->sid);

  mtlk_dump(2, psUmiStaRemove, sizeof(UMI_STA_REMOVE), "dump of UMI_STA_REMOVE:");

  ILOG1_DY("UMI_STA_REMOVE->u16SID: %u, %Y", MAC_TO_HOST16(psUmiStaRemove->u16SID), &info->addr);
  res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);
  if (res != MTLK_ERR_OK) {
    ELOG_DD("CID-%04x: Can't send UM_MAN_STA_REMOVE_REQ request to MAC (err=%d)",
            mtlk_vap_get_oid(vap_handle), res);
    goto finish;
  }
  if (psUmiStaRemove->u8Status != UMI_OK) {
    WLOG_DYD("CID-%04x: Station %Y remove failed in FW (status=%u)",
             mtlk_vap_get_oid(vap_handle), &info->addr, psUmiStaRemove->u8Status);
    res = MTLK_ERR_MAC;
    goto finish;
  }

  ILOG1_DYD("CID-%04x: Station %Y disconnected (SID = %u)",
            mtlk_vap_get_oid(vap_handle), &info->addr, info->sid);

finish:
  mtlk_txmm_msg_cleanup(&man_msg);
  return res;
}

#if MTLK_USE_DIRECTCONNECT_DP_API

/* Get station ID callback */
int mtlk_core_dp_get_subif_cb (mtlk_vap_handle_t vap_handle,  char *mac_addr, uint32_t *subif) {
  uint16    sta_id = 0;
  sta_entry   *sta = NULL;
  mtlk_core_t *nic = mtlk_vap_get_core(vap_handle);
  uint8    vap_id  = mtlk_vap_get_id(vap_handle);

  MTLK_ASSERT(NULL != nic);
  MTLK_ASSERT(NULL != nic->slow_ctx);

  *subif = 0;
  MTLK_BFIELD_SET(*subif, TX_DATA_INFO_VAPID, vap_id);

  if (mac_addr) {
    if (MESH_MODE_BACKHAUL_AP == MTLK_CORE_HOT_PATH_PDB_GET_INT(nic, CORE_DB_CORE_MESH_MODE)) {
      /* MultiAP: backhaul AP.
       * Need to return a first station from the STA_DB regardless from the MAC */
      sta = mtlk_stadb_get_first_sta(&nic->slow_ctx->stadb);
      if (sta) {
        sta_id = mtlk_sta_get_sid(sta);
      } else {
        ILOG2_D("CID-%04x: backhaul AP, cannot return STA_ID", mtlk_vap_get_oid(vap_handle));
        return MTLK_ERR_UNKNOWN;
      }
      ILOG2_DD("CID-%04x: backhaul AP, returned IRE STA_ID = %d", mtlk_vap_get_oid(vap_handle), sta_id);
    }
    else {
      /* Normal processing */
      sta = mtlk_stadb_find_sta(&nic->slow_ctx->stadb, mac_addr);
      if (sta) {
        sta_id = mtlk_sta_get_sid(sta);
      } else {
        /* try searching in hostdb */
        sta = mtlk_hstdb_find_sta(&nic->slow_ctx->hstdb, mac_addr);
        if (sta) sta_id = mtlk_sta_get_sid(sta);
        else {
          ILOG2_DY("CID-%04x: Cannot find STA_ID for MAC %Y", mtlk_vap_get_oid(vap_handle), mac_addr);
          return MTLK_ERR_UNKNOWN;
        }
      }
      ILOG2_DYD("CID-%04x: Requested MAC = %Y, returned STA_ID = %d", mtlk_vap_get_oid(vap_handle), mac_addr, sta_id);
    }
    MTLK_BFIELD_SET(*subif, TX_DATA_INFO_STAID, sta_id);
    mtlk_sta_decref(sta); /* De-reference of find or get_ap */
  }

  return MTLK_ERR_OK;
}
#endif

BOOL __MTLK_IFUNC
mtlk_core_mcast_module_is_available (mtlk_core_t* nic)
{
  return mtlk_df_ui_mcast_is_registered(mtlk_vap_get_df(nic->vap_handle));
}

uint32 __MTLK_IFUNC
mtlk_core_mcast_handle_grid (mtlk_core_t* nic, mtlk_mc_addr_t *mc_addr, mc_grid_action_t action, int grp_id)
{
  mtlk_core_ui_mc_grid_action_t req;

  req.mc_addr = *mc_addr;
  req.action = action;
  req.grp_id = grp_id;

  if (MTLK_ERR_OK == mtlk_hw_set_prop(mtlk_vap_get_hwapi(nic->vap_handle), MTLK_HW_MC_GROUP_ID, &req, sizeof(req))) {
    if ((MCAST_GROUP_UNDEF == req.grp_id) && (MTLK_GRID_ADD == action)) {
      ELOG_D("CID-%04x: No free group ID", mtlk_vap_get_oid(nic->vap_handle));
    }
  }
  return req.grp_id;
}

void __MTLK_IFUNC
mtlk_core_mcast_group_id_action_serialized (mtlk_core_t* nic, mc_action_t action, int grp_id, uint8 *mac_addr, mtlk_mc_addr_t *mc_addr)
{
  mtlk_core_ui_mc_action_t req;

  if (MCAST_GROUP_UNDEF == grp_id) {
    ILOG1_V("Group ID is not defined yet");
    return;
  }

  req.action = action;
  req.grp_id = grp_id;
  ieee_addr_set(&req.sta_mac_addr, mac_addr);
  req.mc_addr = *mc_addr;
  _mtlk_process_hw_task(nic, SERIALIZABLE, _mtlk_core_mcast_group_id_action, HANDLE_T(nic), &req, sizeof(mtlk_core_ui_mc_action_t));
}

int __MTLK_IFUNC
mtlk_core_mcast_notify_fw (mtlk_vap_handle_t vap_handle, int action, int sta_id, int grp_id)
{
  int                       res = MTLK_ERR_OK;
  mtlk_txmm_msg_t           man_msg;
  mtlk_txmm_data_t          *man_entry = NULL;
  UMI_MULTICAST_ACTION      *pMulticastAction = NULL;

  man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, mtlk_vap_get_txmm(vap_handle), &res);

  if (NULL == man_entry) {
    ELOG_D("CID-%04x: No man entry available", mtlk_vap_get_oid(vap_handle));
    res = MTLK_ERR_NO_RESOURCES;
    goto FINISH;
  }

  man_entry->id = UM_MAN_MULTICAST_ACTION_REQ;
  man_entry->payload_size = sizeof(UMI_MULTICAST_ACTION);

  pMulticastAction = (UMI_MULTICAST_ACTION *) man_entry->payload;
  pMulticastAction->u8Action = (action == MTLK_MC_STA_JOIN_GROUP) ? ADD_STA_TO_MULTICAST_GROUP : REMOVE_STA_FROM_MULTICAST_GROUP;
  pMulticastAction->u16StaID = HOST_TO_MAC16((uint16)sta_id);
  pMulticastAction->u8GroupID = grp_id;

  ILOG1_DSD("Multicast FW action request: STA (SID=%d) %s group %d", sta_id, (action == MTLK_MC_STA_JOIN_GROUP) ? "joined" : "leaving", grp_id);

  mtlk_dump(1, pMulticastAction, sizeof(UMI_MULTICAST_ACTION), "dump of UMI_MULTICAST_ACTION:");

  res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);
  if (MTLK_ERR_OK != res) {
    ELOG_DD("CID-%04x: Multicast Action sending failure (%i)", mtlk_vap_get_oid(vap_handle), res);
    goto FINISH;
  }

FINISH:
  if (man_entry) {
    mtlk_txmm_msg_cleanup(&man_msg);
  }

  return res;
}

void
mtlk_core_on_bss_drop_tx_que_full(mtlk_core_t *core)
{
    MTLK_ASSERT(core);
    mtlk_core_inc_cnt(core, MTLK_CORE_CNT_BSS_MGMT_TX_QUE_FULL);
}

/* QAMplus configuration */

static void _core_store_qamplus_mode (wave_radio_t *radio, const uint32 qamplus_mode)
{
  MTLK_ASSERT(radio != NULL);
  WAVE_RADIO_PDB_SET_INT(radio, PARAM_DB_RADIO_QAMPLUS_MODE, qamplus_mode);
}

static int
_mtlk_core_receive_qamplus_mode (mtlk_core_t *master_core, uint8 *qamplus_mode)
{
  mtlk_txmm_msg_t         man_msg;
  mtlk_txmm_data_t        *man_entry;
  UMI_QAMPLUS_ACTIVATE    *mac_msg;
  int                     res = MTLK_ERR_OK;

  man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, mtlk_vap_get_txmm(master_core->vap_handle), NULL);
  if (!man_entry) {
    ELOG_D("CID-%04x: Can not get TXMM slot", mtlk_vap_get_oid(master_core->vap_handle));
    return MTLK_ERR_NO_RESOURCES;
  }

  man_entry->id = UM_MAN_QAMPLUS_ACTIVATE_REQ;
  man_entry->payload_size = sizeof(UMI_QAMPLUS_ACTIVATE);
  mac_msg = (UMI_QAMPLUS_ACTIVATE *)man_entry->payload;
  memset(mac_msg, 0, sizeof(*mac_msg));
  mac_msg->getSetOperation = API_GET_OPERATION;

  res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);

  if (MTLK_ERR_OK == res) {
    *qamplus_mode = mac_msg->enableQAMplus;
  }
  else {
    ELOG_D("CID-%04x: Receive UM_MAN_QAMPLUS_ACTIVATE_REQ failed", mtlk_vap_get_oid(master_core->vap_handle));
  }

  mtlk_txmm_msg_cleanup(&man_msg);
  return res;
}

static int _mtlk_core_qamplus_mode_req (mtlk_core_t *master_core, const uint32 qamplus_mode)
{
  mtlk_txmm_msg_t         man_msg;
  mtlk_txmm_data_t        *man_entry;
  UMI_QAMPLUS_ACTIVATE    *mac_msg;
  int                     res = MTLK_ERR_OK;

  ILOG2_DD("CID-%04x: QAMplus mode: %u",
    mtlk_vap_get_oid(master_core->vap_handle), qamplus_mode);

  /* allocate a new message */
  man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, mtlk_vap_get_txmm(master_core->vap_handle), NULL);
  if (!man_entry) {
    ELOG_D("CID-%04x: Can not get TXMM slot", mtlk_vap_get_oid(master_core->vap_handle));
    return MTLK_ERR_NO_RESOURCES;
  }

  /* fill the message data */
  man_entry->id = UM_MAN_QAMPLUS_ACTIVATE_REQ;
  man_entry->payload_size = sizeof(UMI_QAMPLUS_ACTIVATE);
  mac_msg = (UMI_QAMPLUS_ACTIVATE *)man_entry->payload;
  memset(mac_msg, 0, sizeof(*mac_msg));
  mac_msg->enableQAMplus = (uint8)qamplus_mode;

  /* send the message to FW */
  res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);

  /* New structure does not contain field status */

  /* cleanup the message */
  mtlk_txmm_msg_cleanup(&man_msg);

  return res;
}

static int _mtlk_core_set_qamplus_mode (mtlk_handle_t hcore, const void* data, uint32 data_size)
{
  int res = MTLK_ERR_OK;
  mtlk_core_t *core = mtlk_core_get_master((mtlk_core_t*)hcore);
  wave_radio_t *radio = wave_vap_radio_get(core->vap_handle);
  mtlk_qamplus_mode_cfg_t *qamplus_mode_cfg = NULL;
  uint32 qamplus_mode_cfg_size;
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **)data;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  /* get configuration */
  qamplus_mode_cfg = mtlk_clpb_enum_get_next(clpb, &qamplus_mode_cfg_size);
  MTLK_CLPB_TRY(qamplus_mode_cfg, qamplus_mode_cfg_size)
    MTLK_CFG_START_CHEK_ITEM_AND_CALL()
      /* send new config to FW */
      MTLK_CFG_CHECK_ITEM_AND_CALL(qamplus_mode_cfg, qamplus_mode, _mtlk_core_qamplus_mode_req,
                                   (core, qamplus_mode_cfg->qamplus_mode), res);
      /* store new config in internal db*/
      MTLK_CFG_CHECK_ITEM_AND_CALL_VOID(qamplus_mode_cfg, qamplus_mode, _core_store_qamplus_mode,
                                        (radio, qamplus_mode_cfg->qamplus_mode));
    MTLK_CFG_END_CHEK_ITEM_AND_CALL()
  MTLK_CLPB_FINALLY(res)
    /* push result into clipboard */
    return mtlk_clpb_push_res(clpb, res);
  MTLK_CLPB_END
}

static int _mtlk_core_get_qamplus_mode (mtlk_handle_t hcore, const void* data, uint32 data_size)
{
  int res = MTLK_ERR_OK;
  mtlk_qamplus_mode_cfg_t qamplus_mode_cfg;
  mtlk_core_t *master_core = mtlk_core_get_master((mtlk_core_t*)hcore);
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **)data;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  MTLK_CFG_SET_ITEM_BY_FUNC(&qamplus_mode_cfg, qamplus_mode, _mtlk_core_receive_qamplus_mode,
                            (master_core, &qamplus_mode_cfg.qamplus_mode), res);

  /* push result and config to clipboard*/
  res = mtlk_clpb_push(clpb, &res, sizeof(res));
  if (MTLK_ERR_OK == res) {
    res = mtlk_clpb_push(clpb, &qamplus_mode_cfg, sizeof(qamplus_mode_cfg));
  }

  return res;
}

/* Radio configuration */
static int _mtlk_core_get_radio_mode(mtlk_handle_t hcore, const void* data, uint32 data_size)
{
  int res = MTLK_ERR_OK;
  mtlk_radio_mode_cfg_t radio_mode_cfg;
  mtlk_core_t *master_core = mtlk_core_get_master((mtlk_core_t*)hcore);
  wave_radio_t *radio = wave_vap_radio_get(master_core->vap_handle);
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **)data;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  /* read config from internal db */
  MTLK_CFG_SET_ITEM(&radio_mode_cfg, radio_mode, wave_radio_mode_get(radio))

  /* push result and config to clipboard*/
  res = mtlk_clpb_push(clpb, &res, sizeof(res));
  if (MTLK_ERR_OK == res) {
    res = mtlk_clpb_push(clpb, &radio_mode_cfg, sizeof(radio_mode_cfg));
  }

  return res;
}

static int _mtlk_core_set_radio_mode (mtlk_handle_t hcore, const void* data, uint32 data_size)
{
  int res = MTLK_ERR_OK;
  mtlk_core_t *core = mtlk_core_get_master((mtlk_core_t*)hcore);
  wave_radio_t *radio = wave_vap_radio_get(core->vap_handle);
  mtlk_radio_mode_cfg_t *radio_mode_cfg = NULL;
  uint32 radio_mode_cfg_size;
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **)data;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  /* get configuration */
  radio_mode_cfg = mtlk_clpb_enum_get_next(clpb, &radio_mode_cfg_size);
  MTLK_CLPB_TRY(radio_mode_cfg, radio_mode_cfg_size)
    MTLK_CFG_START_CHEK_ITEM_AND_CALL()
      /* store new config in internal db*/
      MTLK_CFG_CHECK_ITEM_AND_CALL_VOID(radio_mode_cfg, radio_mode, wave_radio_mode_set,
                                        (radio, radio_mode_cfg->radio_mode));
      /* send new config to FW if it is ready, if not, send it later after set bss */
      MTLK_CFG_CHECK_ITEM_AND_CALL(radio_mode_cfg, radio_mode, _mtlk_core_check_and_set_radio_mode,
                                   (core, radio_mode_cfg->radio_mode), res);
    MTLK_CFG_END_CHEK_ITEM_AND_CALL()
  MTLK_CLPB_FINALLY(res)
    /* push result into clipboard */
    return mtlk_clpb_push_res(clpb, res);
  MTLK_CLPB_END
}

/* AMSDU NUM configuration */
inline static void __core_store_amsdu_num (mtlk_core_t *core,
  const uint32 htMsduInAmsdu, const uint32 vhtMsduInAmsdu,
  const uint32 heMsduInAmsdu)
{
  wave_radio_t *radio = wave_vap_radio_get(core->vap_handle);
  MTLK_ASSERT(radio != NULL);

  WAVE_RADIO_PDB_SET_INT(radio, PARAM_DB_RADIO_AMSDU_NUM, htMsduInAmsdu);
  WAVE_RADIO_PDB_SET_INT(radio, PARAM_DB_RADIO_AMSDU_VNUM, vhtMsduInAmsdu);
  WAVE_RADIO_PDB_SET_INT(radio, PARAM_DB_RADIO_AMSDU_HENUM, heMsduInAmsdu);
}

static int
_mtlk_core_receive_amsdu_num (mtlk_core_t *core, uint32 *htMsduInAmsdu,
  uint32 *vhtMsduInAmsdu, uint32 *heMsduInAmsdu)
{
  int res;
  mtlk_txmm_msg_t           man_msg;
  mtlk_txmm_data_t          *man_entry = NULL;
  UMI_MSDU_IN_AMSDU_CONFIG  *pAMSDUNum = NULL;
  mtlk_vap_handle_t         vap_handle = core->vap_handle;

  man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, mtlk_vap_get_txmm(vap_handle), NULL);
  if (NULL == man_entry) {
    ELOG_D("CID-%04x: No man entry available", mtlk_vap_get_oid(vap_handle));
    return MTLK_ERR_NO_RESOURCES;
  }

  man_entry->id = UM_MAN_MSDU_IN_AMSDU_CONFIG_REQ;
  man_entry->payload_size = sizeof(UMI_MSDU_IN_AMSDU_CONFIG);

  pAMSDUNum = (UMI_MSDU_IN_AMSDU_CONFIG *)(man_entry->payload);
  pAMSDUNum->getSetOperation = API_GET_OPERATION;

  res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);
  if (MTLK_ERR_OK == res) {
    *htMsduInAmsdu  = pAMSDUNum->htMsduInAmsdu;
    *vhtMsduInAmsdu = pAMSDUNum->vhtMsduInAmsdu;
    *heMsduInAmsdu = pAMSDUNum->heMsduInAmsdu;
  }
  else {
    ELOG_D("CID-%04x: Receive UM_MAN_MSDU_IN_AMSDU_CONFIG_REQ failed", mtlk_vap_get_oid(vap_handle));
  }

  mtlk_txmm_msg_cleanup(&man_msg);
  return res;
}

static int _mtlk_core_get_amsdu_num (mtlk_handle_t hcore, const void* data, uint32 data_size)
{
  int                      res = MTLK_ERR_OK;
  mtlk_amsdu_num_cfg_t     amsdu_num_cfg;
  uint32                   amsdu_num, amsdu_vnum, amsdu_henum;
  mtlk_core_t              *core = mtlk_core_get_master((mtlk_core_t*)hcore);
  mtlk_clpb_t              *clpb = *(mtlk_clpb_t **)data;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  res = _mtlk_core_receive_amsdu_num(core, &amsdu_num, &amsdu_vnum, &amsdu_henum);
  if (MTLK_ERR_OK == res) {
    MTLK_CFG_SET_ITEM(&amsdu_num_cfg, amsdu_num, amsdu_num);
    MTLK_CFG_SET_ITEM(&amsdu_num_cfg, amsdu_vnum, amsdu_vnum);
    MTLK_CFG_SET_ITEM(&amsdu_num_cfg, amsdu_henum, amsdu_henum);
  }

  /* push result and config to clipboard*/
  return mtlk_clpb_push_res_data(clpb, res, &amsdu_num_cfg, sizeof(amsdu_num_cfg));
}

static int
_mtlk_core_check_and_set_amsdu_num (mtlk_core_t *core, uint32 htMsduInAmsdu,
  uint32 vhtMsduInAmsdu, uint32 heMsduInAmsdu)
{
  if (htMsduInAmsdu > MAX_AMSDU_NUM || htMsduInAmsdu < MIN_AMSDU_NUM) {
    ELOG_D("Invalid htMsduInAmsdu: %d", htMsduInAmsdu);
    return MTLK_ERR_PARAMS;
  }

  if (vhtMsduInAmsdu > MAX_AMSDU_NUM || vhtMsduInAmsdu < MIN_AMSDU_NUM) {
    ELOG_D("Invalid vhtMsduInAmsdu: %d", vhtMsduInAmsdu);
    return MTLK_ERR_PARAMS;
  }

  if (heMsduInAmsdu > MAX_AMSDU_NUM || heMsduInAmsdu < MIN_AMSDU_NUM) {
    ELOG_D("Invalid heMsduInAmsdu: %d", heMsduInAmsdu);
    return MTLK_ERR_PARAMS;
  }

  return _mtlk_core_set_amsdu_num_req(core, htMsduInAmsdu, vhtMsduInAmsdu,
    heMsduInAmsdu);
}

static int _mtlk_core_set_amsdu_num (mtlk_handle_t hcore, const void* data, uint32 data_size)
{
  int                    res = MTLK_ERR_OK;
  mtlk_core_t            *core = mtlk_core_get_master((mtlk_core_t*)hcore);
  mtlk_amsdu_num_cfg_t   *amsdu_num_cfg = NULL;
  uint32                 amsdu_num_cfg_size;
  mtlk_clpb_t            *clpb = *(mtlk_clpb_t **)data;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  /* get configuration */
  amsdu_num_cfg = mtlk_clpb_enum_get_next(clpb, &amsdu_num_cfg_size);
  MTLK_CLPB_TRY(amsdu_num_cfg, amsdu_num_cfg_size)
    MTLK_CFG_START_CHEK_ITEM_AND_CALL()
      /* send new config to FW */
      MTLK_CFG_CHECK_ITEM_AND_CALL(amsdu_num_cfg, amsdu_num, _mtlk_core_check_and_set_amsdu_num,
                                   (core, amsdu_num_cfg->amsdu_num, amsdu_num_cfg->amsdu_vnum, amsdu_num_cfg->amsdu_henum), res);
      /* store new config in internal db*/
      MTLK_CFG_CHECK_ITEM_AND_CALL_VOID(amsdu_num_cfg, amsdu_num, __core_store_amsdu_num,
                                        (core, amsdu_num_cfg->amsdu_num, amsdu_num_cfg->amsdu_vnum, amsdu_num_cfg->amsdu_henum));
    MTLK_CFG_END_CHEK_ITEM_AND_CALL()
  MTLK_CLPB_FINALLY(res)
    /* push result into clipboard */
    return mtlk_clpb_push_res(clpb, res);
  MTLK_CLPB_END
}

#ifdef CPTCFG_IWLWAV_FILTER_BLACKLISTED_BSS
static BOOL _mtlk_core_blacklist_entry_exists (mtlk_core_t *nic,
  const IEEE_ADDR *addr)
{
  return _mtlk_core_ieee_addr_entry_exists(nic, addr, &nic->widan_blacklist,
    "widan black") ||
    _mtlk_core_ieee_addr_entry_exists(nic, addr, &nic->multi_ap_blacklist,
        "multi-AP black");
}
#endif

static BOOL
_mtlk_core_blacklist_frame_drop (mtlk_core_t *nic,
  const IEEE_ADDR *addr, unsigned subtype, int8 rx_snr_db, BOOL isbroadcast)
{
  struct intel_vendor_event_msg_drop prb_req_drop;
  ieee_addr_entry_t *entry;
  MTLK_HASH_ENTRY_T(ieee_addr) *h;
  blacklist_snr_info_t *blacklist_snr_info = NULL;

  /* drop all frames to STA in list without reject code */
  if (_mtlk_core_ieee_addr_entry_exists(nic, addr, &nic->widan_blacklist,
    "widan black")) /* Widanblacklist=1, MultiAP blacklist=0 */
    return TRUE;
  /* Check if STA is in multi AP blacklist */
  mtlk_osal_lock_acquire(&nic->multi_ap_blacklist.ieee_addr_lock);
  h = mtlk_hash_find_ieee_addr(&nic->multi_ap_blacklist.hash, addr);
  mtlk_osal_lock_release(&nic->multi_ap_blacklist.ieee_addr_lock);

  if (h) {
    entry = MTLK_CONTAINER_OF(h, ieee_addr_entry_t, hentry);
    blacklist_snr_info = (blacklist_snr_info_t *)&entry->data[0];
    if ((blacklist_snr_info->snrProbeHWM == 0) && (blacklist_snr_info->snrProbeLWM == 0)) {
      /* case when the client is not added by softblock */
      if (subtype == MAN_TYPE_PROBE_REQ)
        return TRUE;
      return FALSE;  /* report all auth, assoc requests and other */
    }
  }
  else {
    return FALSE;
  }

  /* W=0, A=1, Prb req */
  if (subtype == MAN_TYPE_PROBE_REQ) {
    /* In the path of unicast/bcast probe req. Check for softblock */
    if ((_mtlk_core_mngmnt_softblock_notify(nic, addr, &nic->multi_ap_blacklist,
        "multi-AP black", rx_snr_db, isbroadcast, &prb_req_drop)) == DRV_SOFTBLOCK_ALLOW) {
      /* This is the case when the STA is in multi ap blacklist due to SoftBlock
          and thresholds allow to be processed */
      return FALSE;
    } else {
      return TRUE;
    }
  }
  /* report all auth, assoc requests and other */

  /* A=1, other msgs */
  return FALSE;
}

static int
_mtlk_core_set_softblocklist_entry (mtlk_handle_t hcore, const void* data, uint32 data_size)
{
  mtlk_core_t *nic = HANDLE_T_PTR(mtlk_core_t, hcore);
  int res = MTLK_ERR_OK;
  struct intel_vendor_blacklist_cfg *softblocklist_cfg = NULL;
  uint32 softblocklist_cfg_size;
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **)data;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  /* get configuration */
  softblocklist_cfg = mtlk_clpb_enum_get_next(clpb, &softblocklist_cfg_size);
  MTLK_CLPB_TRY(softblocklist_cfg, softblocklist_cfg_size)
  if (softblocklist_cfg->remove) {
    _mtlk_core_del_ieee_addr_entry(nic, &softblocklist_cfg->addr,
      &nic->multi_ap_blacklist, "multi-AP black", FALSE);
  } else {
    res = __mtlk_core_add_blacklist_addr_entry(nic, softblocklist_cfg,
            &nic->multi_ap_blacklist, "multi-AP black");
  }
  MTLK_CLPB_FINALLY(res)
    /* push result into clipboard */
    return mtlk_clpb_push_res(clpb, res);
  MTLK_CLPB_END
}

static int _mtlk_core_set_blacklist_entry (mtlk_handle_t hcore, const void* data,
  uint32 data_size)
{
  mtlk_core_t *nic = HANDLE_T_PTR(mtlk_core_t, hcore);
  int res = MTLK_ERR_OK;
  struct intel_vendor_blacklist_cfg *blacklist_cfg = NULL;
  uint32 blacklist_cfg_size;
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **)data;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  /* get configuration */
  blacklist_cfg = mtlk_clpb_enum_get_next(clpb, &blacklist_cfg_size);
  MTLK_CLPB_TRY(blacklist_cfg, blacklist_cfg_size)
  if (blacklist_cfg->remove) {
    if (!is_zero_ether_addr(blacklist_cfg->addr.au8Addr)) {
      _mtlk_core_del_ieee_addr_entry(nic, &blacklist_cfg->addr,
        &nic->widan_blacklist, "widan black", FALSE);
      _mtlk_core_del_ieee_addr_entry(nic, &blacklist_cfg->addr,
        &nic->multi_ap_blacklist, "multi-AP black", FALSE);
    } else {
      _mtlk_core_flush_ieee_addr_list(nic, &nic->widan_blacklist, "widan black");
      _mtlk_core_flush_ieee_addr_list(nic, &nic->multi_ap_blacklist, "multi-AP black");
    }
  } else {
    if (blacklist_cfg->status == 0) {
      _mtlk_core_del_ieee_addr_entry(nic, &blacklist_cfg->addr,
        &nic->multi_ap_blacklist, "multi-AP black", FALSE);
      res = _mtlk_core_add_ieee_addr_entry(nic, &blacklist_cfg->addr,
        &nic->widan_blacklist, "widan black");
    }
    else {
      _mtlk_core_del_ieee_addr_entry(nic, &blacklist_cfg->addr,
        &nic->widan_blacklist, "widan black", FALSE);
      res = __mtlk_core_add_blacklist_addr_entry(nic, blacklist_cfg,
        &nic->multi_ap_blacklist, "multi-AP black");
    }
  }
  MTLK_CLPB_FINALLY(res)
    /* push result into clipboard */
    return mtlk_clpb_push_res(clpb, res);
  MTLK_CLPB_END
}

static int _mtlk_core_get_blacklist_entries (mtlk_handle_t hcore, const void* data, uint32 data_size)
{
  mtlk_core_t *core = HANDLE_T_PTR(mtlk_core_t, hcore);

  return _mtlk_core_dump_ieee_addr_list (core, &core->widan_blacklist,
    "widan black", data, data_size);
}

static int
_mtlk_core_get_station_measurements (mtlk_handle_t hcore, const void *data, uint32 data_size)
{
  mtlk_core_t *core = HANDLE_T_PTR(mtlk_core_t, hcore);
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;
  int res = MTLK_ERR_OK;
  IEEE_ADDR *addr;
  uint32 addr_size;
  sta_entry *sta = NULL;
  struct driver_sta_info sta_info;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);
  addr = mtlk_clpb_enum_get_next(clpb, &addr_size);
  MTLK_CLPB_TRY(addr, addr_size)
    if (!wave_core_sync_hostapd_done_get(core)) {
      ILOG1_D("CID-%04x: HOSTAPD STA database is not synced", mtlk_vap_get_oid(core->vap_handle));
      MTLK_CLPB_EXIT(MTLK_ERR_NOT_READY);
    }

    /* find station in stadb */
    sta = mtlk_stadb_find_sta(&core->slow_ctx->stadb, addr->au8Addr);
    if (NULL == sta) {
      WLOG_DY("CID-%04x: station %Y not found",
        mtlk_vap_get_oid(core->vap_handle), addr);
      MTLK_CLPB_EXIT(MTLK_ERR_UNKNOWN);
    }

    mtlk_core_get_driver_sta_info(sta, &sta_info);

  MTLK_CLPB_FINALLY(res)
    if (sta)
      mtlk_sta_decref(sta);
    return mtlk_clpb_push_res_data(clpb, res, &sta_info,
      sizeof(sta_info));
  MTLK_CLPB_END;
}

static int
_mtlk_core_get_vap_measurements (mtlk_handle_t hcore, const void *data, uint32 data_size)
{
  mtlk_core_t *core = HANDLE_T_PTR(mtlk_core_t, hcore);
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;
  struct driver_vap_info vap_info;

  _mtlk_core_get_vap_info_stats(core, &vap_info);
  return mtlk_clpb_push_res_data(clpb, MTLK_ERR_OK, &vap_info,
    sizeof(vap_info));
}

static int
_mtlk_core_check_4addr_mode (mtlk_handle_t hcore, const void *data, uint32 data_size)
{
  mtlk_core_t *core = HANDLE_T_PTR(mtlk_core_t, hcore);
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;
  struct vendor_check_4addr_mode check_4addr_mode;
  IEEE_ADDR *addr;
  uint32 addr_size;
  int res = MTLK_ERR_OK;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);
  addr = mtlk_clpb_enum_get_next(clpb, &addr_size);
  MTLK_CLPB_TRY(addr, addr_size)
    check_4addr_mode.sta_4addr_mode = _mtlk_core_check_static_4addr_mode(core, addr);
    ILOG2_DD("CID-%04x: sta_4addr_mode=%d", mtlk_vap_get_oid(core->vap_handle), check_4addr_mode.sta_4addr_mode);
  MTLK_CLPB_FINALLY(res)
    return mtlk_clpb_push_res_data(clpb, MTLK_ERR_OK, &check_4addr_mode, sizeof(check_4addr_mode));
  MTLK_CLPB_END;
}

static int
_mtlk_core_get_unconnected_station (mtlk_handle_t hcore, const void* data, uint32 data_size)
{
  mtlk_core_t *core = HANDLE_T_PTR(mtlk_core_t, hcore);
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;
  IEEE_ADDR *addr;
  sta_entry *sta = NULL;
  struct vendor_unconnected_sta_req_data_internal *sta_req_data;
  struct intel_vendor_unconnected_sta sta_res_data;
  unsigned sta_req_data_size;
  int res = MTLK_ERR_OK, res2, i;
  uint16 sid;
  sta_info info;
  struct mtlk_chan_def origianl_ccd;
  struct mtlk_chan_def cd;
  struct set_chan_param_data cpd;
  struct ieee80211_channel *c;
  BOOL paused_scan = FALSE;
  mtlk_vap_manager_t *vap_mgr = mtlk_vap_get_manager(core->vap_handle);
  wave_radio_t       *radio = wave_vap_radio_get(core->vap_handle);
  wv_mac80211_t      *mac80211 = wave_radio_mac80211_get(radio);
  mtlk_nbuf_t *nbuf_ndp = NULL;
  frame_head_t *wifi_header;
  mtlk_df_user_t *df_user;
  mtlk_vap_handle_t vap_handle;
  mtlk_core_t *caller_core, *sta_core;
  uint32 scan_flags;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  sta_req_data = mtlk_clpb_enum_get_next(clpb, &sta_req_data_size);
  MTLK_CLPB_TRY(sta_req_data, sta_req_data_size)
    df_user = mtlk_df_user_from_wdev(sta_req_data->wdev);
    if (NULL == df_user) {
      res = MTLK_ERR_UNKNOWN;
      goto end;
    }

    vap_handle = mtlk_df_get_vap_handle(mtlk_df_user_get_df(df_user));
    caller_core = mtlk_vap_get_core(vap_handle);

    memset(&cd, 0, sizeof(cd));
    memset(&sta_res_data, 0, sizeof(sta_res_data));
    sta_res_data.addr = sta_req_data->addr;
    for (i = 0; i < ARRAY_SIZE(sta_res_data.rssi); i++)
      sta_res_data.rssi[i] = MIN_RSSI;

    c = ieee80211_get_channel(sta_req_data->wdev->wiphy, sta_req_data->center_freq);
    if (!c) {
      ELOG_DD("CID-%04x: Getting channel structure for frequency %d MHz failed",
              mtlk_vap_get_oid(core->vap_handle), sta_req_data->center_freq);
      res = MTLK_ERR_PARAMS;
      goto end;
    }
    cd.chan.band = nlband2mtlkband(c->band);
    if (mtlk_core_is_band_supported(core, cd.chan.band) != MTLK_ERR_OK) {
      ELOG_DD("CID-%04x: HW does not support band %i",
              mtlk_vap_get_oid(core->vap_handle), cd.chan.band);
      res = MTLK_ERR_NOT_SUPPORTED;
      goto end;
    }

    scan_flags = wave_radio_scan_flags_get(radio);
    if (wave_radio_scan_is_running(radio)) {
      if (!(scan_flags & SDF_BACKGROUND)) {
        ELOG_V("Cannot change channels in the middle of a non-BG scan");
        res = MTLK_ERR_BUSY;
        goto end;
      }
      else if (!(scan_flags & SDF_BG_BREAK)) /* background scan and not on a break at the moment */
      {
        ILOG0_V("Background scan encountered, so pausing it first");

        if ((res = pause_or_prevent_scan(core)) != MTLK_ERR_OK)
        {
          ELOG_V("Scan is already paused/prevented, canceling the channel switch");
          goto end;
        }
        paused_scan = TRUE;
      }
      else
        ILOG0_V("Background scan during its break encountered, so changing the channel");
    }

    /* find station in stadb of any VAP */
    addr = &sta_req_data->addr;
    sta = mtlk_vap_manager_find_sta(core, &sta_core, addr);
    if (NULL != sta) {
      WLOG_DYD("CID-%04x: station %Y already connected to CID-%04x",
        mtlk_vap_get_oid(caller_core->vap_handle), addr->au8Addr,
        mtlk_vap_get_oid(sta_core->vap_handle));
        res = MTLK_ERR_ALREADY_EXISTS;
        goto end;
    }

    if (wv_mac80211_has_sta_connections(mac80211)) {
      nbuf_ndp = mtlk_df_nbuf_alloc(sizeof(frame_head_t));
      if (!nbuf_ndp)
      {
        ELOG_D("CID-%04x: Unable to allocate buffer for Null Data Packet",
               mtlk_vap_get_oid(caller_core->vap_handle));
        res = MTLK_ERR_UNKNOWN;
        goto end;
      }

      wifi_header = (frame_head_t *) nbuf_ndp->data;
      memset(wifi_header, 0, sizeof(*wifi_header));
      wifi_header->frame_control = HOST_TO_WLAN16(IEEE80211_STYPE_NULLFUNC | IEEE80211_FTYPE_DATA);

      res = wv_mac80211_NDP_send_to_all_APs(mac80211, nbuf_ndp, TRUE, TRUE);
      if (res != MTLK_ERR_OK) {
        ELOG_DDS("CID-%04x: sending NDP failed with error %d (%s)",
                 mtlk_vap_get_oid(caller_core->vap_handle), res,
                 mtlk_get_error_text(res));
        if (res == MTLK_ERR_TIMEOUT)
          wv_mac80211_NDP_send_to_all_APs(mac80211, nbuf_ndp, FALSE, FALSE);
        goto end;
      }
    }

    /* get SID */
    res = core_cfg_internal_request_sid(caller_core, addr, &sid);
    if (res != MTLK_ERR_OK)
      goto end;

    /* add station */
    memset(&info, 0, sizeof(info));
    info.sid = sid;
    info.aid = sid + 1;
    info.addr = *addr;
    info.rates[0] = MTLK_CORE_WIDAN_UNCONNECTED_STATION_RATE;
    info.supported_rates_len = sizeof(info.rates[0]);
    MTLK_BFIELD_SET(info.flags, STA_FLAGS_WMM, 1);
    MTLK_BFIELD_SET(info.flags, STA_FLAGS_IS_8021X_FILTER_OPEN, 1);
    /* RSSI in ADD STA should be set to MIN value */
    info.rssi_dbm = MIN_RSSI - _mtlk_core_get_rrsi_offs(caller_core);
    res = _mtlk_core_ap_add_sta_req(caller_core, &info);
    if (res != MTLK_ERR_OK)
      goto remove_sid;

    /* add station to stadb */
    sta = _mtlk_core_add_sta(caller_core, addr, &info);
    if (sta == NULL) {
      res = MTLK_ERR_UNKNOWN;
      goto remove_sta;
    }

    /* may not do set_chan unless there has been a VAP activated */
    if (0 == mtlk_vap_manager_get_active_vaps_number(vap_mgr)
        && ((res = mtlk_mbss_send_vap_activate(core, cd.chan.band)) != MTLK_ERR_OK))
    {
      ELOG_D("Could not activate the master VAP, err=%i", res);
      goto remove_sta;
    }

    /* save original channel definition for restoring it afterwards */
    origianl_ccd = *__wave_core_chandef_get(core);
    cd.center_freq1 = sta_req_data->cf1;
    cd.center_freq2 = sta_req_data->cf2;
    cd.width = nlcw2mtlkcw(sta_req_data->chan_width);
    cd.chan.center_freq = sta_req_data->center_freq;
    memset(&cpd, 0, sizeof(cpd));
    cpd.switch_type = ST_RSSI;
    cpd.bg_scan = core_cfg_channels_overlap(&origianl_ccd, &cd) ? 0 : SDF_RUNNING;
    cpd.sid = sid;
    res = core_cfg_send_set_chan(core, &cd, &cpd);
    if (res != MTLK_ERR_OK)
      goto remove_sta;

    mtlk_osal_msleep(WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_UNCONNECTED_STA_SCAN_TIME));

    memset(&cpd, 0, sizeof(cpd));
    cpd.switch_type = ST_NORMAL_AFTER_RSSI;
    cpd.sid = sid;
    res = core_cfg_send_set_chan(core, &origianl_ccd, &cpd);
    if (res != MTLK_ERR_OK)
      goto remove_sta;

    if (nbuf_ndp)
      wv_mac80211_NDP_send_to_all_APs(mac80211, nbuf_ndp, FALSE, FALSE);

    for(i = 0; i < ARRAY_SIZE(cpd.rssi); i++) {
      cpd.rssi[i] = mtlk_stadb_apply_rssi_offset(cpd.rssi[i], _mtlk_core_get_rrsi_offs(core));
    }

    /* Get MHI Statistics */
#ifdef MTLK_LEGACY_STATISTICS
    _mtlk_core_mac_get_mhi_stats(caller_core, mtlk_vap_get_hw(caller_core->vap_handle));
#else
    _mtlk_core_get_statistics(caller_core, mtlk_vap_get_hw(caller_core->vap_handle));
#endif
    _mtlk_core_mac_update_peers_stats(caller_core);

    sta_res_data.rx_bytes = mtlk_sta_get_stat_cntr_rx_bytes(sta);
    sta_res_data.rx_packets = mtlk_sta_get_stat_cntr_rx_frames(sta);
    wave_memcpy(sta_res_data.rssi, sizeof(sta_res_data.rssi), cpd.rssi, sizeof(cpd.rssi));
    sta_res_data.rate = MTLK_BITRATE_TO_MBPS(cpd.rate);

    MTLK_STATIC_ASSERT(ARRAY_SIZE(cpd.noise) == ARRAY_SIZE(cpd.rf_gain));
    MTLK_STATIC_ASSERT(ARRAY_SIZE(cpd.rf_gain) == ARRAY_SIZE(sta_res_data.noise));

    if ((ARRAY_SIZE(cpd.noise) != ARRAY_SIZE(cpd.rf_gain)) ||
        (ARRAY_SIZE(cpd.rf_gain) != ARRAY_SIZE(sta_res_data.noise))) {
      ELOG_V("Noise and RF Gain arrays not equal size");
      goto remove_sta;
    }

    for (i = 0; i < ARRAY_SIZE(cpd.noise); i++) {
      sta_res_data.noise[i] = mtlk_hw_noise_phy_to_noise_dbm(mtlk_vap_get_hw(core->vap_handle),
                                                             cpd.noise[i], cpd.rf_gain[i]);
    }

remove_sta:
    /* Send Stop Traffic Request to FW */
    if (sta)
      core_ap_stop_traffic(caller_core, &sta->info);
    core_ap_remove_sta(caller_core, &info);
    if (sta)
      mtlk_stadb_remove_sta(&caller_core->slow_ctx->stadb, sta);
remove_sid:
    core_remove_sid(caller_core, sid);
end:
    if (paused_scan) {
      ILOG0_V("Resuming the paused scan");
      resume_or_allow_scan(core);
    }
    if (nbuf_ndp)
      mtlk_df_nbuf_free(nbuf_ndp);
    if (sta)
      mtlk_sta_decref(sta); /* De-reference by creator */
    res2 = wv_cfg80211_handle_get_unconnected_sta(sta_req_data->wdev,
      &sta_res_data, sizeof(sta_res_data));
  MTLK_CLPB_FINALLY(res)
    return res != MTLK_ERR_OK ? res : res2;
  MTLK_CLPB_END
}

/* Radio has to be ON for set channel, calibrate request, CoC and temperature */
int __MTLK_IFUNC mtlk_core_radio_enable_if_needed(mtlk_core_t *core)
{
  int res = MTLK_ERR_OK;
  mtlk_core_t* master_core = mtlk_core_get_master(core);
  wave_radio_t *radio = wave_vap_radio_get(master_core->vap_handle);

  if (WAVE_RADIO_OFF == WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_MODE_CURRENT)) {
    res = _mtlk_core_set_radio_mode_req(master_core, WAVE_RADIO_ON);

    if (MTLK_ERR_OK != res) {
      ELOG_V("Failed to enable radio");
    }
  }

  return res;
}

int __MTLK_IFUNC mtlk_core_radio_disable_if_needed(mtlk_core_t *core)
{
  int res = MTLK_ERR_OK;
  mtlk_core_t* master_core = mtlk_core_get_master(core);
  wave_radio_t *radio = wave_vap_radio_get(master_core->vap_handle);

  if (mtlk_core_is_block_tx_mode(master_core)) {
    ILOG2_V("Waiting for beacons");
    return MTLK_ERR_OK;
  }

  /* will be disabled after scan */
  if (mtlk_core_scan_is_running(master_core) ||
      mtlk_core_is_in_scan_mode(master_core)) {
    ILOG2_V("Is in scan mode");
    return MTLK_ERR_OK;
  }

  if (ST_RSSI == __wave_core_chan_switch_type_get(core)) {
    ILOG2_V("Is in ST_RSSI channel switch mode");
    return MTLK_ERR_OK;
  }

  if ((WAVE_RADIO_ON  == WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_MODE_CURRENT)) &&
      (WAVE_RADIO_OFF == WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_MODE_REQUESTED))) {
    res = _mtlk_core_set_radio_mode_req(master_core, WAVE_RADIO_OFF);

    if (MTLK_ERR_OK != res) {
      ELOG_V("Failed to disable radio");
    }
  }

  return res;
}

static int
_mtlk_core_get_radio_info (mtlk_handle_t hcore, const void *data, uint32 data_size)
{
  mtlk_core_t   *core = mtlk_core_get_master((mtlk_core_t*)hcore);
  wave_radio_t  *radio = wave_vap_radio_get(core->vap_handle);
  mtlk_clpb_t   *clpb = *(mtlk_clpb_t **) data;
  int res = MTLK_ERR_OK;
  struct driver_radio_info radio_info;
  mtlk_tx_power_data_t tx_pw_data;
  mtlk_coc_antenna_cfg_t *current_params;
  mtlk_coc_t *coc_mgmt = __wave_core_coc_mgmt_get(core);
  struct mtlk_chan_def cd;

  memset(&radio_info, 0, sizeof(radio_info));
#if defined(CPTCFG_IWLWAV_TSF_TIMER_ACCESS_ENABLED)
  radio_info.tsf_start_time = mtlk_hw_get_timestamp(core->vap_handle);
#endif
  _mtlk_core_get_tr181_hw(core, &radio_info.tr181_hw);
  wave_radio_get_tr181_hw_stats(radio, &radio_info.tr181_hw_stats);

  radio_info.load = wave_radio_channel_load_get(radio);

  mtlk_core_get_tx_power_data(core, &tx_pw_data);
  radio_info.tx_pwr_cfg = POWER_TO_MBM(tx_pw_data.pw_targets[tx_pw_data.cur_cbw]);

  current_params = mtlk_coc_get_current_params(coc_mgmt);
  radio_info.num_tx_antennas = current_params->num_tx_antennas;
  radio_info.num_rx_antennas = current_params->num_rx_antennas;

  /* Note: this info could have been changing while we copied it and
   * we won't necessarily catch it with the is_channel_certain() trick.
   */
  cd = *__wave_core_chandef_get(core);

  if (is_channel_certain(&cd)) {
    radio_info.primary_center_freq = cd.chan.center_freq;
    radio_info.center_freq1 = cd.center_freq1;
    radio_info.center_freq2 = cd.center_freq2;
    radio_info.width = MHZS_PER_20MHZ << cd.width;
  }

  return mtlk_clpb_push_res_data(clpb, res, &radio_info,
    sizeof(radio_info));
}

int fill_channel_data (mtlk_core_t *core, struct intel_vendor_channel_data *ch_data)
{
  mtlk_core_t* master_core = mtlk_core_get_master(core);
  const struct mtlk_chan_def *cd = __wave_core_chandef_get(master_core);
  uint8 sec_offset;
  int   res;
  wave_radio_t *radio = wave_vap_radio_get(core->vap_handle);


  memset(ch_data, 0, sizeof(*ch_data));
  ch_data->channel = WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_CHANNEL_CUR);

  switch (WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_SPECTRUM_MODE)) {
  case CW_20: ch_data->BW = 20; break;
  case CW_40: ch_data->BW = 40; break;
  case CW_80: ch_data->BW = 80; break;
  case CW_160:ch_data->BW = 160; break;
  case CW_80_80: ch_data->BW = -160; break;
  default: MTLK_ASSERT(0);
  }
  ch_data->primary = ch_data->channel;

  switch (WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_BONDING_MODE)) {
  case ALTERNATE_UPPER: sec_offset = UMI_CHANNEL_SW_MODE_SCA; break;
  case ALTERNATE_LOWER: sec_offset = UMI_CHANNEL_SW_MODE_SCB; break;
  case ALTERNATE_NONE:  sec_offset = UMI_CHANNEL_SW_MODE_SCN; break;
  default: sec_offset = UMI_CHANNEL_SW_MODE_SCN; break;
  }

  ch_data->secondary = mtlk_channels_get_secondary_channel_no_by_offset(ch_data->primary, sec_offset);
  ch_data->freq = channel_to_frequency(freq2lowchannum(cd->center_freq1, cd->width));

  res = scan_get_aocs_info(core, ch_data, NULL);

  ch_data->total_time = 255;
  ch_data->filled_mask |= CHDATA_TOTAL_TIME;

  return res;
}

#ifdef MTLK_LEGACY_STATISTICS
static void
_mtlk_core_get_traffic_wlan_stats(mtlk_core_t* core, mtlk_wssa_drv_traffic_stats_t* stats)
{
  stats->UnicastPacketsSent = _mtlk_core_get_vap_stat(core, STAT_VAP_TX_UNICAST_FRAMES);
  stats->UnicastPacketsReceived = _mtlk_core_get_vap_stat(core, STAT_VAP_RX_UNICAST_FRAMES);
  stats->MulticastPacketsSent = _mtlk_core_get_vap_stat(core, STAT_VAP_TX_MULTICAST_FRAMES);
  stats->MulticastPacketsReceived = _mtlk_core_get_vap_stat(core, STAT_VAP_RX_MULTICAST_FRAMES);
  stats->BroadcastPacketsSent = _mtlk_core_get_vap_stat(core, STAT_VAP_TX_BROADCAST_FRAMES);
  stats->BroadcastPacketsReceived = _mtlk_core_get_vap_stat(core, STAT_VAP_RX_BROADCAST_FRAMES);

#if 1
  /* 64-bit accumulated values */
  stats->PacketsSent = _mtlk_core_get_vap_stat64(core, STAT64_VAP_TX_FRAMES);
  stats->PacketsReceived = _mtlk_core_get_vap_stat64(core, STAT64_VAP_RX_FRAMES);
  stats->BytesSent = _mtlk_core_get_vap_stat64(core, STAT64_VAP_TX_BYTES);
  stats->BytesReceived = _mtlk_core_get_vap_stat64(core, STAT64_VAP_RX_BYTES);
#else
  /* 32-bit values*/
  stats->PacketsSent = stats->UnicastPacketsSent
    + stats->MulticastPacketsSent
    + stats->BroadcastPacketsSent;

  stats->PacketsReceived = stats->UnicastPacketsReceived
    + stats->MulticastPacketsReceived
    + stats->BroadcastPacketsReceived;

  stats->BytesSent = _mtlk_core_get_vap_stat(core, STAT_VAP_TX_UNICAST_BYTES)
    + _mtlk_core_get_vap_stat(core, STAT_VAP_TX_MULTICAST_BYTES)
    + _mtlk_core_get_vap_stat(core, STAT_VAP_TX_BROADCAST_BYTES);

  stats->BytesReceived = _mtlk_core_get_vap_stat(core, STAT_VAP_RX_UNICAST_BYTES)
    + _mtlk_core_get_vap_stat(core, STAT_VAP_RX_MULTICAST_BYTES)
    + _mtlk_core_get_vap_stat(core, STAT_VAP_RX_BROADCAST_BYTES);
#endif
}

static void
_mtlk_core_get_mgmt_wlan_stats(mtlk_core_t *core, mtlk_wssa_drv_mgmt_stats_t *stats)
{
  stats->MANFramesResQueue = _mtlk_core_get_cnt(core, MTLK_CORE_CNT_MAN_FRAMES_RES_QUEUE);
  stats->MANFramesSent = _mtlk_core_get_cnt(core, MTLK_CORE_CNT_MAN_FRAMES_SENT);
  stats->MANFramesConfirmed = _mtlk_core_get_cnt(core, MTLK_CORE_CNT_MAN_FRAMES_CONFIRMED);
  stats->MANFramesReceived = _mtlk_core_get_cnt(core, MTLK_CORE_CNT_MAN_FRAMES_RECEIVED);
  stats->MANFramesRetryDropped = _mtlk_core_get_cnt(core, MTLK_CORE_CNT_RX_MAN_FRAMES_RETRY_DROPPED);

  stats->ProbeRespSent = _mtlk_core_get_cnt(core, MTLK_CORE_CNT_TX_PROBE_RESP_SENT);
  stats->ProbeRespDropped = _mtlk_core_get_cnt(core, MTLK_CORE_CNT_TX_PROBE_RESP_DROPPED);
  stats->BssMgmtTxQueFull = _mtlk_core_get_cnt(core, MTLK_CORE_CNT_BSS_MGMT_TX_QUE_FULL);
}

static void
_mtlk_core_get_wlan_stats(mtlk_core_t* core, mtlk_wssa_drv_wlan_stats_t* stats)
{
  /* minimal statistic */
  _mtlk_core_get_tr181_wlan_stats(core, &stats->tr181_stats);
  _mtlk_core_get_mgmt_wlan_stats(core, &stats->mgmt_stats);
}


#if MTLK_MTIDL_WLAN_STAT_FULL
static void
_mtlk_core_get_debug_wlan_stats(mtlk_core_t* core, mtlk_wssa_drv_debug_wlan_stats_t* stats)
{
  /* minimal statistic */
  stats->UnicastPacketsSent = _mtlk_core_get_vap_stat(core, STAT_VAP_TX_UNICAST_FRAMES);
  stats->UnicastPacketsReceived = _mtlk_core_get_vap_stat(core, STAT_VAP_RX_UNICAST_FRAMES);
  stats->MulticastPacketsSent = _mtlk_core_get_vap_stat(core, STAT_VAP_TX_MULTICAST_FRAMES);
  stats->MulticastPacketsReceived = _mtlk_core_get_vap_stat(core, STAT_VAP_RX_MULTICAST_FRAMES);
  stats->BroadcastPacketsSent = _mtlk_core_get_vap_stat(core, STAT_VAP_TX_BROADCAST_FRAMES);
  stats->BroadcastPacketsReceived = _mtlk_core_get_vap_stat(core, STAT_VAP_RX_BROADCAST_FRAMES);

#if 1
  /* 64-bit accumulated values */
  stats->TxPacketsSucceeded = _mtlk_core_get_vap_stat64(core, STAT64_VAP_TX_FRAMES);
  stats->RxPacketsSucceeded = _mtlk_core_get_vap_stat64(core, STAT64_VAP_RX_FRAMES);
  stats->TxBytesSucceeded = _mtlk_core_get_vap_stat64(core, STAT64_VAP_TX_BYTES);
  stats->RxBytesSucceeded = _mtlk_core_get_vap_stat64(core, STAT64_VAP_RX_BYTES);
#else
  /* 32-bit values */
  stats->TxPacketsSucceeded = stats->UnicastPacketsSent
    + stats->MulticastPacketsSent
    + stats->BroadcastPacketsSent;

  stats->RxPacketsSucceeded = stats->UnicastPacketsReceived
    + stats->MulticastPacketsReceived
    + stats->BroadcastPacketsReceived;

  stats->TxBytesSucceeded = _mtlk_core_get_vap_stat(core, STAT_VAP_TX_UNICAST_BYTES)
    + _mtlk_core_get_vap_stat(core, STAT_VAP_TX_MULTICAST_BYTES)
    + _mtlk_core_get_vap_stat(core, STAT_VAP_TX_BROADCAST_BYTES);

  stats->RxBytesSucceeded = _mtlk_core_get_vap_stat(core, STAT_VAP_RX_UNICAST_BYTES)
    + _mtlk_core_get_vap_stat(core, STAT_VAP_RX_MULTICAST_BYTES)
    + _mtlk_core_get_vap_stat(core, STAT_VAP_RX_BROADCAST_BYTES);
#endif

  /* full statistic */
  stats->MulticastBytesSent = _mtlk_core_get_cnt(core, MTLK_CORE_CNT_MULTICAST_BYTES_SENT);
  stats->MulticastBytesReceived = _mtlk_core_get_cnt(core, MTLK_CORE_CNT_MULTICAST_BYTES_RECEIVED);
  stats->BroadcastBytesSent = _mtlk_core_get_cnt(core, MTLK_CORE_CNT_BROADCAST_BYTES_SENT);
  stats->BroadcastBytesReceived = _mtlk_core_get_cnt(core, MTLK_CORE_CNT_BROADCAST_BYTES_RECEIVED);

  stats->RxPacketsDiscardedDrvTooOld = _mtlk_core_get_cnt(core, MTLK_CORE_CNT_RX_PACKETS_DISCARDED_DRV_TOO_OLD);
  stats->RxPacketsDiscardedDrvDuplicate = _mtlk_core_get_cnt(core, MTLK_CORE_CNT_RX_PACKETS_DISCARDED_DRV_DUPLICATE);
  stats->TxPacketsDiscardedDrvNoPeers = _mtlk_core_get_cnt(core, MTLK_CORE_CNT_TX_PACKETS_DISCARDED_NO_PEERS);
  stats->TxPacketsDiscardedDrvACM = _mtlk_core_get_cnt(core, MTLK_CORE_CNT_TX_PACKETS_DISCARDED_DRV_ACM);
  stats->TxPacketsDiscardedEapolCloned = _mtlk_core_get_cnt(core, MTLK_CORE_CNT_TX_PACKETS_DISCARDED_EAPOL_CLONED);
  stats->TxPacketsDiscardedDrvUnknownDestinationDirected = _mtlk_core_get_cnt(core, MTLK_CORE_CNT_TX_PACKETS_DISCARDED_DRV_UNKNOWN_DESTINATION_DIRECTED);
  stats->TxPacketsDiscardedDrvUnknownDestinationMcast = _mtlk_core_get_cnt(core, MTLK_CORE_CNT_TX_PACKETS_DISCARDED_DRV_UNKNOWN_DESTINATION_MCAST);
  stats->TxPacketsDiscardedDrvNoResources = _mtlk_core_get_cnt(core, MTLK_CORE_CNT_TX_PACKETS_DISCARDED_DRV_NO_RESOURCES);
  stats->TxPacketsDiscardedDrvSQOverflow = _mtlk_core_get_cnt(core, MTLK_CORE_CNT_TX_PACKETS_DISCARDED_SQ_OVERFLOW);
  stats->TxPacketsDiscardedDrvEAPOLFilter = _mtlk_core_get_cnt(core, MTLK_CORE_CNT_TX_PACKETS_DISCARDED_EAPOL_FILTER);
  stats->TxPacketsDiscardedDrvDropAllFilter = _mtlk_core_get_cnt(core, MTLK_CORE_CNT_TX_PACKETS_DISCARDED_DROP_ALL_FILTER);
  stats->TxPacketsDiscardedDrvTXQueueOverflow = _mtlk_core_get_cnt(core, MTLK_CORE_CNT_TX_PACKETS_DISCARDED_TX_QUEUE_OVERFLOW);
  stats->RxPackets802_1x = _mtlk_core_get_cnt(core, MTLK_CORE_CNT_802_1X_PACKETS_RECEIVED);
  stats->TxPackets802_1x = _mtlk_core_get_cnt(core, MTLK_CORE_CNT_802_1X_PACKETS_SENT);
  stats->TxDiscardedPackets802_1x = _mtlk_core_get_cnt(core, MTLK_CORE_CNT_802_1X_PACKETS_DISCARDED);
  stats->PairwiseMICFailurePackets = _mtlk_core_get_cnt(core, MTLK_CORE_CNT_PAIRWISE_MIC_FAILURE_PACKETS);
  stats->GroupMICFailurePackets = _mtlk_core_get_cnt(core, MTLK_CORE_CNT_GROUP_MIC_FAILURE_PACKETS);
  stats->UnicastReplayedPackets = _mtlk_core_get_cnt(core, MTLK_CORE_CNT_UNICAST_REPLAYED_PACKETS);
  stats->MulticastReplayedPackets = _mtlk_core_get_cnt(core, MTLK_CORE_CNT_MULTICAST_REPLAYED_PACKETS);
  stats->ManagementReplayedPackets = _mtlk_core_get_cnt(core, MTLK_CORE_CNT_MANAGEMENT_REPLAYED_PACKETS);
  stats->FwdRxPackets = _mtlk_core_get_cnt(core, MTLK_CORE_CNT_FWD_RX_PACKETS);
  stats->FwdRxBytes = _mtlk_core_get_cnt(core, MTLK_CORE_CNT_FWD_RX_BYTES);
  stats->DATFramesReceived = _mtlk_core_get_cnt(core, MTLK_CORE_CNT_DAT_FRAMES_RECEIVED);
  stats->CTLFramesReceived = _mtlk_core_get_cnt(core, MTLK_CORE_CNT_CTL_FRAMES_RECEIVED);

  stats->MANFramesResQueue = _mtlk_core_get_cnt(core, MTLK_CORE_CNT_MAN_FRAMES_RES_QUEUE);
  stats->MANFramesSent = _mtlk_core_get_cnt(core, MTLK_CORE_CNT_MAN_FRAMES_SENT);
  stats->MANFramesConfirmed = _mtlk_core_get_cnt(core, MTLK_CORE_CNT_MAN_FRAMES_CONFIRMED);
  stats->MANFramesReceived = _mtlk_core_get_cnt(core, MTLK_CORE_CNT_MAN_FRAMES_RECEIVED);
  stats->MANFramesRetryDropped = _mtlk_core_get_cnt(core, MTLK_CORE_CNT_RX_MAN_FRAMES_RETRY_DROPPED);
  stats->MANFramesRejectedCfg80211 = _mtlk_core_get_cnt(core, MTLK_CORE_CNT_MAN_FRAMES_CFG80211_FAILED);

  stats->ProbeRespSent = _mtlk_core_get_cnt(core, MTLK_CORE_CNT_TX_PROBE_RESP_SENT);
  stats->ProbeRespDropped = _mtlk_core_get_cnt(core, MTLK_CORE_CNT_TX_PROBE_RESP_DROPPED);
  stats->BssMgmtTxQueFull = _mtlk_core_get_cnt(core, MTLK_CORE_CNT_BSS_MGMT_TX_QUE_FULL);

  stats->CoexElReceived = _mtlk_core_get_cnt(mtlk_core_get_master(core), MTLK_CORE_CNT_COEX_EL_RECEIVED);
  stats->ScanExRequested = _mtlk_core_get_cnt(mtlk_core_get_master(core), MTLK_CORE_CNT_COEX_EL_SCAN_EXEMPTION_REQUESTED);
  stats->ScanExGranted = _mtlk_core_get_cnt(mtlk_core_get_master(core), MTLK_CORE_CNT_COEX_EL_SCAN_EXEMPTION_GRANTED);
  stats->ScanExGrantCancelled = _mtlk_core_get_cnt(mtlk_core_get_master(core), MTLK_CORE_CNT_COEX_EL_SCAN_EXEMPTION_GRANT_CANCELLED);
  stats->SwitchChannel20To40 = _mtlk_core_get_cnt(mtlk_core_get_master(core), MTLK_CORE_CNT_CHANNEL_SWITCH_20_TO_40);
  stats->SwitchChannel40To20 = _mtlk_core_get_cnt(mtlk_core_get_master(core), MTLK_CORE_CNT_CHANNEL_SWITCH_40_TO_20);
  stats->SwitchChannel40To40 = _mtlk_core_get_cnt(mtlk_core_get_master(core), MTLK_CORE_CNT_CHANNEL_SWITCH_40_TO_40);
  stats->TxPacketsToUnicastNoDGAF = _mtlk_core_get_cnt(mtlk_core_get_master(core), MTLK_CORE_CNT_TX_PACKETS_TO_UNICAST_DGAF_DISABLED);
  stats->TxPacketsSkippedNoDGAF = _mtlk_core_get_cnt(mtlk_core_get_master(core), MTLK_CORE_CNT_TX_PACKETS_SKIPPED_DGAF_DISABLED);
}
#endif /* MTLK_MTIDL_WLAN_STAT_FULL */

static void
_mtlk_core_get_tr181_error_stats (mtlk_core_t* core, mtlk_wssa_drv_tr181_error_stats_t* errors)
{
  errors->ErrorsSent = _mtlk_core_get_vap_stat(core, STAT_VAP_TX_AGER_COUNT);
  errors->ErrorsReceived = _mtlk_core_get_vap_stat(core, STAT_VAP_RX_DROP_MPDU);
  errors->DiscardPacketsReceived = _mtlk_core_get_cnt(core, MTLK_CORE_CNT_DISCARD_PACKETS_RECEIVED);
  errors->DiscardPacketsSent = errors->ErrorsSent;
}

static void
_mtlk_core_get_tr181_retrans_stats (mtlk_core_t* core, mtlk_wssa_retrans_stats_t* retrans)
{
  retrans->Retransmissions    = 0; /* Not available in FW so far */
  retrans->FailedRetransCount = 0; /* Never drop packets due to retry limit */
  retrans->RetransCount       = _mtlk_core_get_vap_stat(core, STAT_VAP_BAA_TRANSMIT_STREAM_RPRT_MULTIPLE_RETRY_COUNT);
  retrans->RetryCount         = _mtlk_core_get_vap_stat(core, STAT_VAP_BAA_RETRY_AMSDU);
  retrans->MultipleRetryCount = _mtlk_core_get_vap_stat(core, STAT_VAP_BAA_MULTIPLE_RETRY_AMSDU);
}

static void
_mtlk_core_get_tr181_wlan_stats (mtlk_core_t* core, mtlk_wssa_drv_tr181_wlan_stats_t* stats)
{
  _mtlk_core_get_traffic_wlan_stats(core, &stats->traffic_stats);
  _mtlk_core_get_tr181_error_stats(core, &stats->error_stats);
  _mtlk_core_get_tr181_retrans_stats(core, &stats->retrans_stats);

  stats->UnknownProtoPacketsReceived = _mtlk_core_get_vap_stat(core, STAT_VAP_RX_MPDU_TYPE_NOTSUPPORTED);
  stats->ACKFailureCount = _mtlk_core_get_vap_stat(core, STAT_VAP_BAA_ACK_FAILURE);
  stats->AggregatedPacketCount = _mtlk_core_get_vap_stat(core, STAT_VAP_BAA_TRANSMITTED_AMSDU);
}

static void
_mtlk_core_get_vap_info_stats(mtlk_core_t* core, struct driver_vap_info *vap_info)
{
  _mtlk_core_get_tr181_wlan_stats(core, &vap_info->vap_stats);

  /* todo: These 4 counters are 64-bit, but actual values are 32-bit. Check
   * if 64 bit values are needed and implement. */
  vap_info->TransmittedOctetsInAMSDUCount = _mtlk_core_get_vap_stat(core, STAT_VAP_BAA_TRANSMITTED_OCTETS_IN_AMSDU);
  vap_info->ReceivedOctetsInAMSDUCount = _mtlk_core_get_vap_stat(core, STAT_VAP_RX_AMSDU_BYTES);
  vap_info->TransmittedOctetsInAMPDUCount = _mtlk_core_get_vap_stat(core, STAT_VAP_BAA_TRANSMITTED_OCTETS_IN_AMPDU);
  vap_info->ReceivedOctetsInAMPDUCount = _mtlk_core_get_vap_stat(core, STAT_VAP_RX_OCTETS_IN_AMPDU);

  vap_info->RTSSuccessCount = _mtlk_core_get_vap_stat(core, STAT_VAP_BAA_RTS_SUCCESS_COUNT);
  vap_info->RTSFailureCount = _mtlk_core_get_vap_stat(core, STAT_VAP_BAA_RTS_FAILURE);
  vap_info->TransmittedAMSDUCount = _mtlk_core_get_vap_stat(core, STAT_VAP_BAA_TRANSMITTED_AMSDU);
  vap_info->FailedAMSDUCount = _mtlk_core_get_vap_stat(core, STAT_VAP_BAA_FAILED_AMSDU);
  vap_info->AMSDUAckFailureCount = _mtlk_core_get_vap_stat(core, STAT_VAP_BAA_AMSDU_ACK_FAILURE);
  vap_info->ReceivedAMSDUCount = _mtlk_core_get_vap_stat(core, STAT_VAP_RX_AMSDU);
  vap_info->TransmittedAMPDUCount = _mtlk_core_get_vap_stat(core, STAT_VAP_BAA_TRANSMITTED_AMPDU);
  vap_info->TransmittedMPDUsInAMPDUCount = _mtlk_core_get_vap_stat(core, STAT_VAP_BAA_TRANSMITTED_MPDU_IN_AMPDU);
  vap_info->AMPDUReceivedCount = _mtlk_core_get_vap_stat(core, STAT_VAP_RX_AMPDU);
  vap_info->MPDUInReceivedAMPDUCount = _mtlk_core_get_vap_stat(core, STAT_VAP_RX_MPDUIN_AMPDU);
  vap_info->ImplicitBARFailureCount = _mtlk_core_get_vap_stat(core, STAT_VAP_BAA_IMPLICIT_BAR_FAILURE);
  vap_info->ExplicitBARFailureCount = _mtlk_core_get_vap_stat(core, STAT_VAP_BAA_EXPLICIT_BAR_FAILURE);
  vap_info->TwentyMHzFrameTransmittedCount = _mtlk_core_get_vap_stat(core, STAT_VAP_BAA_TRANSMIT_BW20);
  vap_info->FortyMHzFrameTransmittedCount = _mtlk_core_get_vap_stat(core, STAT_VAP_BAA_TRANSMIT_BW40);
  vap_info->SwitchChannel20To40 = _mtlk_core_get_cnt(mtlk_core_get_master(core), MTLK_CORE_CNT_CHANNEL_SWITCH_20_TO_40);
  vap_info->SwitchChannel40To20 = _mtlk_core_get_cnt(mtlk_core_get_master(core), MTLK_CORE_CNT_CHANNEL_SWITCH_40_TO_20);
  vap_info->FrameDuplicateCount = _mtlk_core_get_cnt(core, MTLK_CORE_CNT_RX_PACKETS_DISCARDED_DRV_DUPLICATE);
}

#else /* MTLK_LEGACY_STATISTICS */

static void
_mtlk_core_get_traffic_wlan_stats (mtlk_core_t* core, mtlk_wssa_drv_traffic_stats_t* stats)
{
  mtlk_mhi_stats_vap_t *mhi_vap_stats;
  MTLK_ASSERT((core) || (stats));

  mhi_vap_stats = &core->mhi_vap_stat;
  /* 64-bit accumulated values */
  stats->PacketsSent = mhi_vap_stats->stats64.txFrames;
  stats->PacketsReceived = mhi_vap_stats->stats64.rxFrames;
  stats->BytesSent = mhi_vap_stats->stats64.txBytes;
  stats->BytesReceived = mhi_vap_stats->stats64.rxBytes;

  stats->UnicastPacketsSent = mhi_vap_stats->stats.txInUnicastHd;
  stats->UnicastPacketsReceived = mhi_vap_stats->stats.rxOutUnicastHd;
  stats->MulticastPacketsSent = mhi_vap_stats->stats.txInMulticastHd;
  stats->MulticastPacketsReceived = mhi_vap_stats->stats.rxOutMulticastHd;
  stats->BroadcastPacketsSent = mhi_vap_stats->stats.txInBroadcastHd;
  stats->BroadcastPacketsReceived = mhi_vap_stats->stats.rxOutBroadcastHd;

}

static void
_mtlk_core_get_tr181_hw (mtlk_core_t* core, mtlk_wssa_drv_tr181_hw_t* tr181_hw)
{
  wave_radio_t *radio;
  MTLK_ASSERT((core) || (tr181_hw));

  radio = wave_vap_radio_get(core->vap_handle);
  tr181_hw->Enable = wave_radio_mode_get(radio);
  tr181_hw->Channel = _mtlk_core_get_channel(core);
}

static void
_mtlk_core_get_tr181_error_stats (mtlk_core_t* core, mtlk_wssa_drv_tr181_error_stats_t* errors)
{
  mtlk_mhi_stats_vap_t *mhi_vap_stats;
  MTLK_ASSERT((core) || (errors));

  mhi_vap_stats = &core->mhi_vap_stat;

  errors->ErrorsSent = mhi_vap_stats->stats.agerCount;
  errors->ErrorsReceived = mhi_vap_stats->stats.dropMpdu;
  errors->DiscardPacketsReceived = _mtlk_core_get_cnt(core, MTLK_CORE_CNT_DISCARD_PACKETS_RECEIVED);
  errors->DiscardPacketsSent = errors->ErrorsSent;
}

static void
_mtlk_core_get_tr181_retrans_stats (mtlk_core_t* core, mtlk_wssa_retrans_stats_t* retrans)
{
  mtlk_mhi_stats_vap_t *mhi_vap_stats;
  MTLK_ASSERT((core) || (retrans));

  mhi_vap_stats = &core->mhi_vap_stat;

  retrans->Retransmissions    = 0; /* Not available in FW so far */
  retrans->FailedRetransCount = 0; /* Never drop packets due to retry limit */
  retrans->RetransCount       = mhi_vap_stats->stats.transmitStreamRprtMultipleRetryCount;
  retrans->RetryCount         = mhi_vap_stats->stats.retryAmsdu;
  retrans->MultipleRetryCount = mhi_vap_stats->stats.multipleRetryAmsdu;
}

void __MTLK_IFUNC
mtlk_core_get_tr181_wlan_stats (mtlk_core_t* core, mtlk_wssa_drv_tr181_wlan_stats_t* stats)
{
  mtlk_mhi_stats_vap_t *mhi_vap_stats;
  MTLK_ASSERT((core) || (stats));

  mhi_vap_stats = &core->mhi_vap_stat;

  _mtlk_core_get_traffic_wlan_stats(core, &stats->traffic_stats);
  _mtlk_core_get_tr181_error_stats(core, &stats->error_stats);
  _mtlk_core_get_tr181_retrans_stats(core, &stats->retrans_stats);

  stats->UnknownProtoPacketsReceived = mhi_vap_stats->stats.mpduTypeNotSupported;
  stats->ACKFailureCount = mhi_vap_stats->stats.ackFailure;
  stats->AggregatedPacketCount = mhi_vap_stats->stats.transmittedAmsdu;
}

static void
_mtlk_core_get_vap_info_stats (mtlk_core_t* core, struct driver_vap_info *vap_info)
{
  mtlk_mhi_stats_vap_t *mhi_vap_stats;
  MTLK_ASSERT((core) || (vap_info));

  mhi_vap_stats = &core->mhi_vap_stat;

  mtlk_core_get_tr181_wlan_stats(core, &vap_info->vap_stats);

  /* todo: These 4 counters are 64-bit, but actual values are 32-bit. Check
   * if 64 bit values are needed and implement. */
  vap_info->TransmittedOctetsInAMSDUCount = mhi_vap_stats->stats.transmittedOctetsInAmsdu;
  vap_info->ReceivedOctetsInAMSDUCount = mhi_vap_stats->stats.amsduBytes;
  vap_info->TransmittedOctetsInAMPDUCount = mhi_vap_stats->stats.transmittedOctetsInAmpdu;
  vap_info->ReceivedOctetsInAMPDUCount = mhi_vap_stats->stats.octetsInAmpdu;

  vap_info->RTSSuccessCount = mhi_vap_stats->stats.rtsSuccessCount;
  vap_info->RTSFailureCount = mhi_vap_stats->stats.rtsFailure;
  vap_info->TransmittedAMSDUCount = mhi_vap_stats->stats.transmittedAmsdu;
  vap_info->FailedAMSDUCount = mhi_vap_stats->stats.failedAmsdu;
  vap_info->AMSDUAckFailureCount = mhi_vap_stats->stats.amsduAckFailure;
  vap_info->ReceivedAMSDUCount = mhi_vap_stats->stats.amsdu;
  vap_info->TransmittedAMPDUCount = mhi_vap_stats->stats.transmittedAmpdu;
  vap_info->TransmittedMPDUsInAMPDUCount = mhi_vap_stats->stats.transmittedMpduInAmpdu;
  vap_info->AMPDUReceivedCount = mhi_vap_stats->stats.ampdu;
  vap_info->MPDUInReceivedAMPDUCount = mhi_vap_stats->stats.mpduInAmpdu;
  vap_info->ImplicitBARFailureCount = mhi_vap_stats->stats.implicitBarFailure;
  vap_info->ExplicitBARFailureCount = mhi_vap_stats->stats.explicitBarFailure;
  vap_info->TwentyMHzFrameTransmittedCount = mhi_vap_stats->stats.transmitBw20;
  vap_info->FortyMHzFrameTransmittedCount = mhi_vap_stats->stats.transmitBw40;
  vap_info->SwitchChannel20To40 = _mtlk_core_get_cnt(mtlk_core_get_master(core), MTLK_CORE_CNT_CHANNEL_SWITCH_20_TO_40);
  vap_info->SwitchChannel40To20 = _mtlk_core_get_cnt(mtlk_core_get_master(core), MTLK_CORE_CNT_CHANNEL_SWITCH_40_TO_20);
  vap_info->FrameDuplicateCount = _mtlk_core_get_cnt(core, MTLK_CORE_CNT_RX_PACKETS_DISCARDED_DRV_DUPLICATE);
}
#endif /* MTLK_LEGACY_STATISTICS */

int __MTLK_IFUNC
mtlk_core_send_iwpriv_config_to_fw (mtlk_core_t *core)
{
  int res = MTLK_ERR_OK;
  wave_radio_t *radio = wave_vap_radio_get(core->vap_handle);

  if (mtlk_vap_is_master(core->vap_handle)) {
    res = _mtlk_core_set_radar_detect(core, WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_DFS_RADAR_DETECTION));
    if (MTLK_ERR_OK != res) goto end;

    res = mtlk_core_cfg_init_cca_threshold(core);
  }

end:
  return res;
}
