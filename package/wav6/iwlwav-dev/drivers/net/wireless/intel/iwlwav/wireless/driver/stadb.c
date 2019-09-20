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
* Written by: Dmitry Fleytman
*
*/
#include "mtlkinc.h"

#include "core.h"
#include "core_config.h"
#include "stadb.h"
#include "mtlk_osal.h"
#include "core.h"
#include "mtlk_coreui.h"
#include "mtlk_param_db.h"
#include "mtlkwlanirbdefs.h"
#include "mtlk_snprintf.h"
#include "mtlk_wssd.h"
#include "mtlk_df_nbuf.h"
#include "mtlk_df.h"
#include "mtlk_dfg.h"

#define LOG_LOCAL_GID   GID_STADB
#define LOG_LOCAL_FID   1

#define STADB_FLAGS_STOPPED      0x20000000


#define MTLK_STA_HTCAP_LDPC_SUPPORTED           MTLK_BFIELD_INFO(0, 1)
#define MTLK_STA_HTCAP_CB_SUPPORTED             MTLK_BFIELD_INFO(1, 1)
#define MTLK_STA_HTCAP_SGI20_SUPPORTED          MTLK_BFIELD_INFO(5, 1)
#define MTLK_STA_HTCAP_SGI40_SUPPORTED          MTLK_BFIELD_INFO(6, 1)
#define MTLK_STA_HTCAP_MIMO_CONFIG_TX           MTLK_BFIELD_INFO(7, 1)
#define MTLK_STA_HTCAP_MIMO_CONFIG_RX           MTLK_BFIELD_INFO(8, 2)
#define MTLK_STA_HTCAP_40MHZ_INTOLERANT         MTLK_BFIELD_INFO(14, 1)

#define MTLK_STA_AMPDU_PARAMS_MAX_LENGTH_EXP    MTLK_BFIELD_INFO(0, 2)
#define MTLK_STA_AMPDU_PARAMS_MIN_START_SPACING MTLK_BFIELD_INFO(2, 3)


/******************************************************************************************
 * GLOBAL STA DB API
 ******************************************************************************************/

static __INLINE mtlk_atomic_t *
__mtlk_global_stadb_get_cntr(void)
{
  global_stadb_t *db = mtlk_dfg_get_driver_stadb();
  return &db->sta_cnt;
}

BOOL __MTLK_IFUNC
mtlk_global_stadb_is_empty (void)
{
  return (mtlk_osal_atomic_get(__mtlk_global_stadb_get_cntr()) == 0);
}


/******************************************************************************************
 * STA API
 ******************************************************************************************/

static const uint32 _mtlk_sta_wss_id_map[] =
{
  MTLK_WWSS_WLAN_STAT_ID_DISCARD_PACKETS_RECEIVED,                      /* MTLK_STAI_CNT_DISCARD_PACKETS_RECEIVED */

#if MTLK_MTIDL_PEER_STAT_FULL
  MTLK_WWSS_WLAN_STAT_ID_RX_PACKETS_DISCARDED_DRV_TOO_OLD,             /* MTLK_STAI_CNT_RX_PACKETS_DISCARDED_DRV_TOO_OLD */
  MTLK_WWSS_WLAN_STAT_ID_RX_PACKETS_DISCARDED_DRV_DUPLICATE,           /* MTLK_STAI_CNT_RX_PACKETS_DISCARDED_DRV_DUPLICATE */
  MTLK_WWSS_WLAN_STAT_ID_TX_PACKETS_DISCARDED_DRV_NO_RESOURCES,        /* MTLK_STAI_CNT_TX_PACKETS_DISCARDED_DRV_NO_RESOURCES */
  MTLK_WWSS_WLAN_STAT_ID_TX_PACKETS_DISCARDED_DRV_SQ_OVERFLOW,         /* MTLK_STAI_CNT_TX_PACKETS_DISCARDED_SQ_OVERFLOW */
  MTLK_WWSS_WLAN_STAT_ID_TX_PACKETS_DISCARDED_DRV_EAPOL_FILTER,        /* MTLK_STAI_CNT_TX_PACKETS_DISCARDED_EAPOL_FILTER */
  MTLK_WWSS_WLAN_STAT_ID_TX_PACKETS_DISCARDED_DRV_DROP_ALL_FILTER,     /* MTLK_STAI_CNT_TX_PACKETS_DISCARDED_DROP_ALL_FILTER */
  MTLK_WWSS_WLAN_STAT_ID_TX_PACKETS_DISCARDED_DRV_TX_QUEUE_OVERFLOW,   /* MTLK_STAI_CNT_TX_PACKETS_DISCARDED_TX_QUEUE_OVERFLOW */

  MTLK_WWSS_WLAN_STAT_ID_802_1X_PACKETS_RECEIVED,                      /* MTLK_STAI_CNT_802_1X_PACKETS_RECEIVED                                         */
  MTLK_WWSS_WLAN_STAT_ID_802_1X_PACKETS_SENT,                          /* MTLK_STAI_CNT_802_1X_PACKETS_SENT                                             */
  MTLK_WWSS_WLAN_STAT_ID_802_1X_PACKETS_DISCARDED,                     /* MTLK_STAI_CNT_802_1X_PACKETS_DISCARDED                                        */

  MTLK_WWSS_WLAN_STAT_ID_FWD_RX_PACKETS,                               /* MTLK_STAI_CNT_FWD_RX_PACKETS */
  MTLK_WWSS_WLAN_STAT_ID_FWD_RX_BYTES,                                 /* MTLK_STAI_CNT_FWD_RX_BYTES */
  MTLK_WWSS_WLAN_STAT_ID_PS_MODE_ENTRANCES,                            /* MTLK_STAI_CNT_PS_MODE_ENTRANCES */
  MTLK_WWSS_WLAN_STAT_ID_TX_PACKETS_DISCARDED_DRV_ACM,                 /* MTLK_STAI_CNT_TX_PACKETS_DISCARDED_ACM */
  MTLK_WWSS_WLAN_STAT_ID_TX_PACKETS_DISCARDED_DRV_EAPOL_CLONED,        /* MTLK_STAI_CNT_TX_PACKETS_DISCARDED_EAPOL_CLONED */
#endif /* MTLK_MTIDL_PEER_STAT_FULL */
};

/* counters will be modified with checking ALLOWED option */
#define _mtlk_sta_inc_cnt(sta, id)         { if (id##_ALLOWED) __mtlk_sta_inc_cnt(sta, id); }
#define _mtlk_sta_add_cnt(sta, id, val)    { if (id##_ALLOWED) __mtlk_sta_add_cnt(sta, id, val); }

static __INLINE void
__mtlk_sta_inc_cnt (sta_entry        *sta,
                   sta_info_cnt_id_e cnt_id)
{
  MTLK_ASSERT(cnt_id >= 0 && cnt_id < MTLK_STAI_CNT_LAST);

  mtlk_wss_cntr_inc(sta->wss_hcntrs[cnt_id]);
}

static __INLINE void
__mtlk_sta_add_cnt (sta_entry        *sta,
                   sta_info_cnt_id_e cnt_id,
                   uint32 val)
{
  MTLK_ASSERT(cnt_id >= 0 && cnt_id < MTLK_STAI_CNT_LAST);

  mtlk_wss_cntr_add(sta->wss_hcntrs[cnt_id], val);
}

static __INLINE int
__mtlk_sta_get_short_term_rssi (const sta_entry *sta, int rssi_index)
{
    return sta->info.stats.data_rssi[rssi_index];
}

static __INLINE int8
__mtlk_sta_get_snr (const sta_entry *sta, int antenna_idx)
{
  return sta->info.stats.snr[antenna_idx];
}

static __INLINE int
__mtlk_sta_get_long_term_rssi (const sta_entry *sta, int rssi_index)
{
#if MTLK_WWSS_WLAN_STAT_ANALYZER_RX_LONG_RSSI_ALLOWED
    return mtlk_peer_analyzer_get_long_term_rssi(&sta->info.sta_analyzer, rssi_index);
#else
    return 0;
#endif
}

static __INLINE int
__mtlk_sta_get_last_mgmt_rssi (const sta_entry *sta, int rssi_index)
{
    return sta->info.stats.mgmt_rssi[rssi_index];
}

static __INLINE uint32
__mtlk_sta_rate_info_to_rate (const sta_entry *sta, const char *path, mtlk_bitrate_info_t rate_info)
{
  uint32    rx_rate;

  rx_rate = mtlk_bitrate_info_to_rate(rate_info);
  if (MTLK_BITRATE_INVALID == rx_rate) {
      ILOG2_YSD("STA:%Y %s rx_rate is not found for bitrate_info 0x%08X",
                mtlk_sta_get_addr(sta), path, (uint32)rate_info);
  }

  return rx_rate;
}

static uint32
_mtlk_sta_get_rx_data_rate (const sta_entry *sta)
{
  MTLK_ASSERT(sta);

  return __mtlk_sta_rate_info_to_rate(sta, "Data", sta->info.stats.rx_data_rate_info);
}

static uint32
_mtlk_sta_get_rx_mgmt_rate (const sta_entry *sta)
{
  MTLK_ASSERT(sta);

  return __mtlk_sta_rate_info_to_rate(sta, "Management", sta->info.stats.rx_mgmt_rate_info);
}

static uint32
_mtlk_sta_get_tx_data_rate (const sta_entry *sta)
{
  return mtlk_bitrate_params_to_rate(sta->info.stats.tx_data_rate_params);
}

static uint8
_mtlk_sta_airtime_usage_get (const sta_entry *sta)
{
  return sta->info.stats.airtime_stats.airtime_usage;
}

static uint32
_mtlk_sta_airtime_efficiency_get (const sta_entry *sta)
{
  return sta->info.stats.airtime_stats.airtime_efficiency;
}

static BOOL
_mtlk_sta_get_is_sta_auth (const sta_entry *sta)
{
  return((MTLK_PCKT_FLTR_ALLOW_ALL == sta->info.filter) ? TRUE : FALSE);
}

static __INLINE uint32
__mtlk_sta_get_tx_data_rate_kbps (const sta_entry *sta)
{
  return MTLK_BITRATE_TO_KBPS(_mtlk_sta_get_tx_data_rate(sta));
}

static __INLINE uint32
__mtlk_sta_get_rx_data_rate_kbps (const sta_entry *sta)
{
  return MTLK_BITRATE_TO_KBPS(_mtlk_sta_get_rx_data_rate(sta));
}

static __INLINE uint32
__mtlk_sta_get_tx_data_rate_mbps (const sta_entry *sta)
{
  return MTLK_BITRATE_TO_MBPS(_mtlk_sta_get_tx_data_rate(sta));
}

static __INLINE uint32
__mtlk_sta_get_rx_data_rate_mbps (const sta_entry *sta)
{
  return MTLK_BITRATE_TO_MBPS(_mtlk_sta_get_rx_data_rate(sta));
}

void __MTLK_IFUNC
mtlk_sta_update_rx_rate_rssi_on_man_frame (sta_entry *sta, const mtlk_phy_info_t *phy_info)
{
    int     i;

    MTLK_ASSERT(sta);
    MTLK_ASSERT(phy_info);

    sta->info.stats.rx_mgmt_rate_info = phy_info->bitrate_info;

    sta->info.stats.mgmt_max_rssi = phy_info->max_rssi;
    for (i = 0; i < ARRAY_SIZE(sta->info.stats.mgmt_rssi); i++) {
        sta->info.stats.mgmt_rssi[i] = phy_info->rssi[i];
    }
}

static uint32 __MTLK_IFUNC
_mtlk_sta_keepalive_tmr (mtlk_osal_timer_t *timer,
                         mtlk_handle_t      clb_usr_data)
{
  sta_entry *sta = HANDLE_T_PTR(sta_entry, clb_usr_data);
  mtlk_osal_msec_t timeout = sta->paramdb->keepalive_interval;

  sta->paramdb->cfg.api.on_sta_keepalive(sta->paramdb->cfg.api.usr_data, sta);
  return timeout; /* restart with same timeout */
}

static void
_mtlk_sta_reset_cnts (sta_entry *sta)
{
  int i = 0;
  for (; i < MTLK_STAI_CNT_LAST; i++) {
    mtlk_wss_reset_stat(sta->wss, i);
  }
}

static void
_mtlk_sta_set_packets_filter_default (sta_entry *sta)
{
  sta->info.filter = MTLK_PCKT_FLTR_DISCARD_ALL;
}


/* Returns number of TX spatial streams supported by STA.
   todo: currently supports only AX */
static uint8 _mtlk_sta_get_nss (const sta_entry *sta)
{
  const sta_info *info = &sta->info;

  if (MTLK_BFIELD_GET(info->flags, STA_FLAGS_11ax)) {
    uint16 tx_he_mcs_map;
    int ms0_bit;
    tx_he_mcs_map = le16_to_cpu(*(uint16*)(&info->he_cap.he_mcs_nss_supp));
    ms0_bit = locate_ms0bit_16(tx_he_mcs_map);
    if (ms0_bit >= 0)
      return ms0_bit / 2 + 1;
  }
  return 0;
}

/******************************************************************************************/

#ifdef MTLK_LEGACY_STATISTICS
void __MTLK_IFUNC
mtlk_sta_update_mhi_peers_stats(sta_entry *sta)
{
    uint32 value;
    mhi_sta_cntr_t *stat_cntrs = mtlk_sta_get_mhi_stat_array(sta);

    /* Currently only one wss counter should be updated */
    value = mtlk_sta_get_cnt(sta, MTLK_STAI_CNT_DISCARD_PACKETS_RECEIVED);  /* last */
    value = stat_cntrs[STAT_STA_CNTR_RX_SW_UPDATE_DROP] - value;            /* delta = new - last */
    _mtlk_sta_add_cnt(sta, MTLK_STAI_CNT_DISCARD_PACKETS_RECEIVED, value);  /* update with delta */
}


static __INLINE void
_mtlk_sta_get_peer_traffic_stats(const sta_entry* sta, mtlk_wssa_peer_traffic_stats_t* stats)
{
    mhi_sta_cntr_t *stat_cntrs = mtlk_sta_get_mhi_stat_array(sta);

    stats->PacketsSent      = stat_cntrs[STAT_STA_CNTR_TX_FRAMES];
    stats->PacketsReceived  = stat_cntrs[STAT_STA_CNTR_RX_FRAMES];
    stats->BytesSent        = stat_cntrs[STAT_STA_CNTR_TX_BYTES];
    stats->BytesReceived    = stat_cntrs[STAT_STA_CNTR_RX_BYTES];
}

static __INLINE void
_mtlk_sta_get_peer_retrans_stats (const sta_entry* sta, mtlk_wssa_retrans_stats_t* retrans)
{
  mhi_sta_cntr_t *stat_cntrs = mtlk_sta_get_mhi_stat_array(sta);

  retrans->Retransmissions    = 0; /* Not available from FW */
  retrans->RetransCount       = stat_cntrs[STAT_STA_CNTR_MPDU_FIRST_RETRANSMISSION];
  retrans->RetryCount         = mtlk_sta_get_stat_cntr_pct_retry(sta);
  retrans->FailedRetransCount = mtlk_sta_get_stat_cntr_pct_retry_exhausted(sta);
  retrans->MultipleRetryCount = 0; /* Not available from FW */
}
#else /* MTLK_LEGACY_STATISTICS */


void __MTLK_IFUNC
mtlk_sta_update_mhi_peers_stats (sta_entry *sta)
{
  uint32 value;
  mtlk_mhi_stats_sta_cntr_t *stat_cntrs;

  MTLK_ASSERT(sta);
  stat_cntrs = mtlk_sta_get_mhi_stat_array(sta);
  /* Currently only one wss counter should be updated */
  value = mtlk_sta_get_cnt(sta, MTLK_STAI_CNT_DISCARD_PACKETS_RECEIVED);  /* last */
  value = stat_cntrs->swUpdateDrop - value;            /* delta = new - last */
  _mtlk_sta_add_cnt(sta, MTLK_STAI_CNT_DISCARD_PACKETS_RECEIVED, value);  /* update with delta */
}


static __INLINE void
_mtlk_sta_get_peer_traffic_stats (const sta_entry* sta, mtlk_wssa_peer_traffic_stats_t* stats)
{
  mtlk_mhi_stats_sta_cntr_t *stat_cntrs;
  MTLK_ASSERT((sta) || (stats));

  stat_cntrs = mtlk_sta_get_mhi_stat_array(sta);
  stats->PacketsSent      = stat_cntrs->mpduTransmitted;
  stats->PacketsReceived  = stat_cntrs->rdCount;
  stats->BytesSent        = stat_cntrs->mpduByteTransmitted;
  stats->BytesReceived    = stat_cntrs->rxOutStaNumOfBytes;
}

static __INLINE void
_mtlk_sta_get_peer_retrans_stats (const sta_entry* sta, mtlk_wssa_retrans_stats_t* retrans)
{
  mtlk_mhi_stats_sta_cntr_t *stat_cntrs;
  MTLK_ASSERT((sta) || (retrans));

  stat_cntrs = mtlk_sta_get_mhi_stat_array(sta);

  retrans->Retransmissions    = 0; /* Not available from FW */
  retrans->RetransCount       = stat_cntrs->mpduFirstRetransmission;
  retrans->RetryCount         = mtlk_sta_get_stat_cntr_pct_retry(sta);
  retrans->FailedRetransCount = mtlk_sta_get_stat_cntr_pct_retry_exhausted(sta);
  retrans->MultipleRetryCount = 0; /* Not available from FW */
}

void __MTLK_IFUNC
mtlk_sta_get_associated_dev_stats (const sta_entry* sta, peerFlowStats* peer_stats)
{
  MTLK_ASSERT(sta);
  MTLK_ASSERT(peer_stats);

  peer_stats->cli_rx_bytes = sta->sta_stats64_cntrs.rxOutStaNumOfBytes;
  peer_stats->cli_tx_bytes= sta->sta_stats64_cntrs.mpduByteTransmitted;
  peer_stats->cli_rx_frames = sta->sta_stats64_cntrs.rdCount;
  peer_stats->cli_tx_frames = sta->sta_stats64_cntrs.mpduTransmitted;
  peer_stats->cli_rx_retries = sta->sta_stats64_cntrs.mpduRetryCount;
  peer_stats->cli_tx_retries = sta->sta_stats64_cntrs.mpduRetransmission;
  peer_stats->cli_rx_errors = sta->sta_stats64_cntrs.swUpdateDrop + sta->sta_stats64_cntrs.rdDuplicateDrop + sta->sta_stats64_cntrs.missingSn;
  peer_stats->cli_tx_errors = sta->sta_stats64_cntrs.tx_errors;
  peer_stats->cli_rx_rate = __mtlk_sta_get_rx_data_rate_kbps(sta);
  peer_stats->cli_tx_rate = __mtlk_sta_get_tx_data_rate_kbps(sta);
}
#endif /* MTLK_LEGACY_STATISTICS */

static void
_mtlk_sta_get_peer_stats (const sta_entry* sta, mtlk_wssa_drv_peer_stats_t* stats)
{
  int i;

  MTLK_ASSERT(sta);

  mtlk_sta_get_tr181_peer_stats(sta, &stats->tr181_stats);

  for (i = 0; i < NUMBER_OF_RX_ANTENNAS; ++i) {
    stats->ShortTermRSSIAverage[i] = __mtlk_sta_get_short_term_rssi(sta, i);
    stats->snr[i]                  = __mtlk_sta_get_snr(sta, i);
  }

  stats->AirtimeUsage      = _mtlk_sta_airtime_usage_get(sta);
  stats->AirtimeEfficiency = _mtlk_sta_airtime_efficiency_get(sta);
}

void
mtlk_sta_get_tr181_peer_stats (const sta_entry* sta, mtlk_wssa_drv_tr181_peer_stats_t *stats)
{
  MTLK_ASSERT(sta);

  stats->StationId          = mtlk_sta_get_sid(sta);
  stats->NetModesSupported  = sta->info.sta_net_modes;

  _mtlk_sta_get_peer_traffic_stats(sta, &stats->traffic_stats);
  _mtlk_sta_get_peer_retrans_stats(sta, &stats->retrans_stats);
  stats->ErrorsSent = 0; /* Not available in FW so far */

  stats->LastDataDownlinkRate = __mtlk_sta_get_tx_data_rate_kbps(sta);
  stats->LastDataUplinkRate   = __mtlk_sta_get_rx_data_rate_kbps(sta);

  stats->SignalStrength = sta->info.stats.max_rssi;
}

void
mtlk_core_get_driver_sta_info (const sta_entry* sta, struct driver_sta_info *stats)
{
  MTLK_ASSERT(sta);

  _mtlk_sta_get_peer_stats(sta, &stats->peer_stats);
  _mtlk_sta_get_peer_rates_info(sta, &stats->rates_info);
}

#ifdef MTLK_LEGACY_STATISTICS
#if MTLK_MTIDL_PEER_STAT_FULL
static void
_mtlk_sta_get_debug_peer_stats(const sta_entry* sta, mtlk_wssa_drv_debug_peer_stats_t* stats)
{
  int i;
  mhi_sta_cntr_t *stat_cntrs = mtlk_sta_get_mhi_stat_array(sta);

  /* minimal statistic level */
  stats->StationId                            = mtlk_sta_get_sid(sta);

  stats->HwTxPacketsSucceeded                 = stat_cntrs[STAT_STA_CNTR_TX_FRAMES];
  stats->HwTxBytesSucceeded                   = stat_cntrs[STAT_STA_CNTR_TX_BYTES];
  stats->HwRxPacketsSucceeded                 = stat_cntrs[STAT_STA_CNTR_RX_FRAMES];
  stats->HwRxBytesSucceeded                   = stat_cntrs[STAT_STA_CNTR_RX_BYTES];

  stats->LastDataDownlinkRate                 = _mtlk_sta_get_tx_data_rate(sta);
  stats->LastDataUplinkRate                   = _mtlk_sta_get_rx_data_rate(sta);
  stats->LastMgmtUplinkRate                   = _mtlk_sta_get_rx_mgmt_rate(sta);
  stats->AirtimeUsage                         = _mtlk_sta_airtime_usage_get(sta);
  stats->AirtimeEfficiency                    = _mtlk_sta_airtime_efficiency_get(sta);

  for (i = 0; i < NUMBER_OF_RX_ANTENNAS; ++i) {
    stats->ShortTermRSSIAverage[i] = __mtlk_sta_get_short_term_rssi(sta, i);
    stats->LongTermRSSIAverage[i]  = __mtlk_sta_get_long_term_rssi(sta, i);
    stats->LastMgmtRSSI[i]         = __mtlk_sta_get_last_mgmt_rssi(sta, i);
    stats->snr[i]                  = __mtlk_sta_get_snr(sta, i);
  }

  /* add FULL statistic level */
  stats->RxPackets802_1x                      = mtlk_sta_get_cnt(sta, MTLK_STAI_CNT_802_1X_PACKETS_RECEIVED);
  stats->TxPackets802_1x                      = mtlk_sta_get_cnt(sta, MTLK_STAI_CNT_802_1X_PACKETS_SENT);
  stats->TxDiscardedPackets802_1x             = mtlk_sta_get_cnt(sta, MTLK_STAI_CNT_802_1X_PACKETS_DISCARDED);
  stats->TxPacketsDiscardedACM                = mtlk_sta_get_cnt(sta, MTLK_STAI_CNT_TX_PACKETS_DISCARDED_ACM);
  stats->TxPacketsDiscardedEAPOLCloned        = mtlk_sta_get_cnt(sta, MTLK_STAI_CNT_TX_PACKETS_DISCARDED_EAPOL_CLONED);
  stats->RxPacketsDiscardedDrvTooOld          = mtlk_sta_get_cnt(sta, MTLK_STAI_CNT_RX_PACKETS_DISCARDED_DRV_TOO_OLD);
  stats->RxPacketsDiscardedDrvDuplicate       = mtlk_sta_get_cnt(sta, MTLK_STAI_CNT_RX_PACKETS_DISCARDED_DRV_DUPLICATE);
  stats->TxPacketsDiscardedDrvNoResources     = mtlk_sta_get_cnt(sta, MTLK_STAI_CNT_TX_PACKETS_DISCARDED_DRV_NO_RESOURCES);
  stats->TxPacketsDiscardedDrvSQOverflow      = mtlk_sta_get_cnt(sta, MTLK_STAI_CNT_TX_PACKETS_DISCARDED_SQ_OVERFLOW);
  stats->TxPacketsDiscardedDrvEAPOLFilter     = mtlk_sta_get_cnt(sta, MTLK_STAI_CNT_TX_PACKETS_DISCARDED_EAPOL_FILTER);
  stats->TxPacketsDiscardedDrvDropAllFilter   = mtlk_sta_get_cnt(sta, MTLK_STAI_CNT_TX_PACKETS_DISCARDED_DROP_ALL_FILTER);
  stats->TxPacketsDiscardedDrvTXQueueOverflow = mtlk_sta_get_cnt(sta, MTLK_STAI_CNT_TX_PACKETS_DISCARDED_TX_QUEUE_OVERFLOW);
  stats->FwdRxPackets                         = mtlk_sta_get_cnt(sta, MTLK_STAI_CNT_FWD_RX_PACKETS);
  stats->FwdRxBytes                           = mtlk_sta_get_cnt(sta, MTLK_STAI_CNT_FWD_RX_BYTES);
  stats->PSModeEntrances                      = mtlk_sta_get_cnt(sta, MTLK_STAI_CNT_PS_MODE_ENTRANCES);

  stats->LastActivityTimestamp                = mtlk_osal_time_passed_ms(sta->activity_timestamp);

/* FIXME: currently mtidl_ini.pl just ignore any C preprocessor "#" lines */
/* So these items (below) are always present in data struct */

#if MTLK_WWSS_WLAN_STAT_ANALYZER_TX_RATE_ALLOWED
  stats->ShortTermTXAverage                   = mtlk_sta_get_short_term_tx(sta);
  stats->LongTermTXAverage                    = mtlk_sta_get_long_term_tx(sta);
#else
  stats->ShortTermTXAverage                   = 0;
  stats->LongTermTXAverage                    = 0;
#endif
#if MTLK_WWSS_WLAN_STAT_ANALYZER_RX_RATE_ALLOWED
  stats->ShortTermRXAverage                   = mtlk_sta_get_short_term_rx(sta);
  stats->LongTermRXAverage                    = mtlk_sta_get_long_term_rx(sta);
#else
  stats->ShortTermRXAverage                   = 0;
  stats->LongTermRXAverage                    = 0;
#endif
}
#endif /* MTLK_MTIDL_PEER_STAT_FULL */
#endif /* MTLK_LEGACY_STATISTICS */

static void
_mtlk_sta_get_peer_capabilities(const sta_entry* sta, mtlk_wssa_drv_peer_capabilities_t* capabilities)
{
  uint16 ht_capabilities_info;
  memset(capabilities, 0, sizeof(*capabilities));

  capabilities->NetModesSupported     = sta->info.sta_net_modes;
  capabilities->WMMSupported          = MTLK_BFIELD_GET(sta->info.flags, STA_FLAGS_WMM);
  capabilities->Vendor                = sta->info.vendor;

  /* Parse u16HTCapabilityInfo according to
   * IEEE Std 802.11-2012 -- 8.4.2.58.2 HT Capabilities Info field */
  ht_capabilities_info                = MAC_TO_HOST16(sta->info.ht_cap_info);
  capabilities->LDPCSupported         = MTLK_BFIELD_GET(ht_capabilities_info, MTLK_STA_HTCAP_LDPC_SUPPORTED);
  capabilities->CBSupported           = MTLK_BFIELD_GET(ht_capabilities_info, MTLK_STA_HTCAP_CB_SUPPORTED);
  capabilities->SGI20Supported        = MTLK_BFIELD_GET(ht_capabilities_info, MTLK_STA_HTCAP_SGI20_SUPPORTED);
  capabilities->SGI40Supported        = MTLK_BFIELD_GET(ht_capabilities_info, MTLK_STA_HTCAP_SGI40_SUPPORTED);
  capabilities->Intolerant_40MHz      = MTLK_BFIELD_GET(ht_capabilities_info, MTLK_STA_HTCAP_40MHZ_INTOLERANT);
  capabilities->MIMOConfigTX          = MTLK_BFIELD_GET(ht_capabilities_info, MTLK_STA_HTCAP_MIMO_CONFIG_TX);
  capabilities->MIMOConfigRX          = MTLK_BFIELD_GET(ht_capabilities_info, MTLK_STA_HTCAP_MIMO_CONFIG_RX);
  capabilities->STBCSupported         = (capabilities->MIMOConfigTX || capabilities->MIMOConfigRX);

  /* Parse AMPDU_Parameters according to
   * IEEE Std 802.11-2012 -- 8.4.2.58.3 A-MPDU Parameters field */
  capabilities->AMPDUMaxLengthExp     = (uint8)MTLK_BFIELD_GET(sta->info.ampdu_param, MTLK_STA_AMPDU_PARAMS_MAX_LENGTH_EXP);
  capabilities->AMPDUMinStartSpacing  = (uint8)MTLK_BFIELD_GET(sta->info.ampdu_param, MTLK_STA_AMPDU_PARAMS_MIN_START_SPACING);

  /* Parse tx_bf_capabilities according to
   * IEEE Std 802.11-2012 -- 8.4.2.58.6 Transmit Beamforming Capabilities */
  /* We assume that explicit beamforming is supported in case at least one BF capability is supported */
  capabilities->BFSupported           = !!(MAC_TO_HOST32(sta->info.tx_bf_cap_info));

  capabilities->AssociationTimestamp  = mtlk_osal_time_passed_ms(sta->connection_timestamp);
}

static void
_mtlk_sta_fill_rate_info_by_invalid (mtlk_wssa_drv_peer_rate_info1_t *info)
{
  info->InfoFlag = FALSE;
  info->PhyMode = -1;
  info->Scp     = -1;
  info->Mcs     = -1;
  info->Nss     =  0;
  info->CbwIdx  =  0;
  info->CbwMHz  =  0;
}

static uint32
_mtlk_sta_rate_cbw_to_cbw_in_mhz (uint32 cbw)
{
  uint32 cbw_in_mhz[] = { 20, 40, 80, 160 };

  if (cbw >= ARRAY_SIZE(cbw_in_mhz)) {
    WLOG_DD("Wrong CBW value %u (max %d expected)", cbw, ARRAY_SIZE(cbw_in_mhz));
    cbw = 0;
  }
  return cbw_in_mhz[cbw];
}

static void
_mtlk_sta_fill_rate_info_by_params (mtlk_wssa_drv_peer_rate_info1_t *info, mtlk_bitrate_params_t params)
{
  info->InfoFlag = TRUE;
  mtlk_bitrate_params_get_hw_params(params,
                                    &info->PhyMode, &info->CbwIdx, &info->Scp, &info->Mcs, &info->Nss);
  info->CbwMHz  = _mtlk_sta_rate_cbw_to_cbw_in_mhz(info->CbwIdx);
}

/* BOOL flag: TRUE - rate parameters, FALSE - stat rate_info */
static void
_mtlk_sta_fill_rate_info_by_info (mtlk_wssa_drv_peer_rate_info1_t *info, mtlk_bitrate_info_t rate_info)
{
  info->InfoFlag = mtlk_bitrate_info_get_flag(rate_info); /* TRUE for bitfields */
  if(info->InfoFlag) {
    info->PhyMode =  mtlk_bitrate_info_get_mode(rate_info);
    info->Scp     =  mtlk_bitrate_info_get_scp(rate_info);
    info->Mcs     =  mtlk_bitrate_info_get_mcs(rate_info);
    info->Nss     =  mtlk_bitrate_info_get_nss(rate_info);
    info->CbwIdx  =  mtlk_bitrate_info_get_cbw(rate_info);
    info->CbwMHz  = _mtlk_sta_rate_cbw_to_cbw_in_mhz(info->CbwIdx);
  } else { /* unknown */
    _mtlk_sta_fill_rate_info_by_invalid(info);
  }
}

void __MTLK_IFUNC mtlk_core_get_tx_power_data(mtlk_core_t *core, mtlk_tx_power_data_t *tx_power_data);

void
_mtlk_sta_get_peer_rates_info(const sta_entry *sta, mtlk_wssa_drv_peer_rates_info_t *rates_info)
{
  mtlk_tx_power_data_t  tx_pw_data;
  mtlk_core_t *mcore;
  wave_radio_t *radio;
  unsigned      cbw;
  unsigned      radio_idx;

  MTLK_ASSERT(NULL != sta);
  MTLK_ASSERT(NULL != rates_info);

  memset(rates_info, 0, sizeof(*rates_info));
  mcore = mtlk_vap_manager_get_master_core(mtlk_vap_get_manager(sta->vap_handle));
  MTLK_ASSERT(NULL != mcore);

  radio = wave_vap_radio_get(mcore->vap_handle);
  radio_idx = wave_radio_id_get(radio);

  mtlk_core_get_tx_power_data(mcore, &tx_pw_data);

  /* TX: downlink info */
  rates_info->TxDataRate  = _mtlk_sta_get_tx_data_rate(sta);
  if (MTLK_BITRATE_INVALID != rates_info->TxDataRate) {
    _mtlk_sta_fill_rate_info_by_params(&rates_info->tx_data_rate_info, sta->info.stats.tx_data_rate_params);
  } else {
    _mtlk_sta_fill_rate_info_by_invalid(&rates_info->tx_data_rate_info);
  }

  cbw = rates_info->tx_data_rate_info.CbwIdx;

  /* Current power is for current antennas number */
  rates_info->TxPwrCur    = POWER_TO_MBM(tx_pw_data.cur_ant_gain +
                                mtlk_hw_sta_stat_to_power(mtlk_vap_get_hw(sta->vap_handle), radio_idx,
                                    mtlk_sta_get_mhi_tx_stat_power_data(sta),
                                    cbw));

  /* Don't apply ant_gain in 11B mode */
  rates_info->TxMgmtPwr   = POWER_TO_MBM(
                            ((PHY_MODE_B == mtlk_sta_get_mhi_tx_stat_phymode_mgmt(sta)) ?
                                0 : tx_pw_data.cur_ant_gain) +
                            mtlk_hw_sta_stat_to_power(mtlk_vap_get_hw(sta->vap_handle), radio_idx,
                            mtlk_sta_get_mhi_tx_stat_power_mgmt(sta), 0));

  rates_info->TxBfMode    = mtlk_sta_get_mhi_tx_stat_bf_mode(sta);
  rates_info->TxStbcMode  = mtlk_sta_get_mhi_tx_stat_stbc_mode(sta);

  /* RX: uplink info */
  rates_info->RxMgmtRate = _mtlk_sta_get_rx_mgmt_rate(sta);
  _mtlk_sta_fill_rate_info_by_info(&rates_info->rx_mgmt_rate_info, sta->info.stats.rx_mgmt_rate_info);

  rates_info->RxDataRate = _mtlk_sta_get_rx_data_rate(sta);
  _mtlk_sta_fill_rate_info_by_info(&rates_info->rx_data_rate_info, sta->info.stats.rx_data_rate_info);
}

#ifdef MTLK_LEGACY_STATISTICS
static void __MTLK_IFUNC
_mtlk_sta_stat_handle_request(mtlk_irbd_t       *irbd,
                              mtlk_handle_t      context,
                              const mtlk_guid_t *evt,
                              void              *buffer,
                              uint32            *size)
{
  sta_entry            *sta  = HANDLE_T_PTR(sta_entry, context);
  mtlk_wssa_info_hdr_t *hdr = (mtlk_wssa_info_hdr_t *) buffer;

  MTLK_UNREFERENCED_PARAM(evt);

  if(sizeof(mtlk_wssa_info_hdr_t) > *size)
    return;

  if(MTIDL_SRC_DRV == hdr->info_source)
  {
    switch(hdr->info_id)
    {
    case MTLK_WSSA_DRV_TR181_PEER:
      {
        if(sizeof(mtlk_wssa_drv_tr181_peer_stats_t) + sizeof(mtlk_wssa_info_hdr_t) > *size)
        {
          hdr->processing_result = MTLK_ERR_BUF_TOO_SMALL;
        }
        else
        {
          mtlk_sta_get_tr181_peer_stats(sta, (mtlk_wssa_drv_tr181_peer_stats_t*) &hdr[1]);
          hdr->processing_result = MTLK_ERR_OK;
          *size = sizeof(mtlk_wssa_drv_tr181_peer_stats_t) + sizeof(mtlk_wssa_info_hdr_t);
        }
      }
      break;
    case MTLK_WSSA_DRV_STATUS_PEER:
      {
        if(sizeof(mtlk_wssa_drv_peer_stats_t) + sizeof(mtlk_wssa_info_hdr_t) > *size)
        {
          hdr->processing_result = MTLK_ERR_BUF_TOO_SMALL;
        }
        else
        {
          _mtlk_sta_get_peer_stats(sta, (mtlk_wssa_drv_peer_stats_t*) &hdr[1]);
          hdr->processing_result = MTLK_ERR_OK;
          *size = sizeof(mtlk_wssa_drv_peer_stats_t) + sizeof(mtlk_wssa_info_hdr_t);
        }
      }
      break;
#if MTLK_MTIDL_PEER_STAT_FULL
    case MTLK_WSSA_DRV_DEBUG_STATUS_PEER:
      {
        if(sizeof(mtlk_wssa_drv_debug_peer_stats_t) + sizeof(mtlk_wssa_info_hdr_t) > *size)
        {
          hdr->processing_result = MTLK_ERR_BUF_TOO_SMALL;
        }
        else
        {
          _mtlk_sta_get_debug_peer_stats(sta, (mtlk_wssa_drv_debug_peer_stats_t*) &hdr[1]);
          hdr->processing_result = MTLK_ERR_OK;
          *size = sizeof(mtlk_wssa_drv_debug_peer_stats_t) + sizeof(mtlk_wssa_info_hdr_t);
        }
      }
      break;
#endif /* MTLK_MTIDL_PEER_STAT_FULL */
    case MTLK_WSSA_DRV_CAPABILITIES_PEER:
      {
        if(sizeof(mtlk_wssa_drv_peer_capabilities_t) + sizeof(mtlk_wssa_info_hdr_t) > *size)
        {
          hdr->processing_result = MTLK_ERR_BUF_TOO_SMALL;
        }
        else
        {
          _mtlk_sta_get_peer_capabilities(sta, (mtlk_wssa_drv_peer_capabilities_t*) &hdr[1]);
          hdr->processing_result = MTLK_ERR_OK;
          *size = sizeof(mtlk_wssa_drv_peer_capabilities_t) + sizeof(mtlk_wssa_info_hdr_t);
        }
      }
      break;
    case MTLK_WSSA_DRV_PEER_RATES_INFO:
      {
        if (sizeof(mtlk_wssa_drv_peer_rates_info_t) + sizeof(mtlk_wssa_info_hdr_t) > *size)
        {
          hdr->processing_result = MTLK_ERR_BUF_TOO_SMALL;
        }
        else
        {
          _mtlk_sta_get_peer_rates_info(sta, (mtlk_wssa_drv_peer_rates_info_t*) &hdr[1]);
          hdr->processing_result = MTLK_ERR_OK;
          *size = sizeof(mtlk_wssa_drv_peer_rates_info_t) + sizeof(mtlk_wssa_info_hdr_t);
        }
      }
      break;

    default:
      {
        hdr->processing_result = MTLK_ERR_NO_ENTRY;
        *size = sizeof(mtlk_wssa_info_hdr_t);
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

MTLK_INIT_STEPS_LIST_BEGIN(sta_entry)
  MTLK_INIT_STEPS_LIST_ENTRY(sta_entry, STA_LOCK)
  MTLK_INIT_STEPS_LIST_ENTRY(sta_entry, KALV_TMR)
  MTLK_INIT_STEPS_LIST_ENTRY(sta_entry, IRBD_NODE)
  MTLK_INIT_STEPS_LIST_ENTRY(sta_entry, IRBD_FLAGS_NODE)
  MTLK_INIT_STEPS_LIST_ENTRY(sta_entry, WSS_NODE)
  MTLK_INIT_STEPS_LIST_ENTRY(sta_entry, WSS_HCNTRs)
  MTLK_INIT_STEPS_LIST_ENTRY(sta_entry, PEER_ANALYZER)
MTLK_INIT_INNER_STEPS_BEGIN(sta_entry)
MTLK_INIT_STEPS_LIST_END(sta_entry);

MTLK_START_STEPS_LIST_BEGIN(sta_entry)
  MTLK_START_STEPS_LIST_ENTRY(sta_entry, KALV_TMR)
  MTLK_START_STEPS_LIST_ENTRY(sta_entry, IRBD_NODE)
  MTLK_START_STEPS_LIST_ENTRY(sta_entry, IRBD_FLAGS_NODE)
#ifdef MTLK_LEGACY_STATISTICS
  MTLK_START_STEPS_LIST_ENTRY(sta_entry, STA_STAT_REQ_HANDLER)
#endif
  MTLK_START_STEPS_LIST_ENTRY(sta_entry, ENABLE_FILTER)
MTLK_START_INNER_STEPS_BEGIN(sta_entry)
MTLK_START_STEPS_LIST_END(sta_entry);

static void
_mtlk_sta_cleanup (sta_entry *sta)
{
  MTLK_CLEANUP_BEGIN(sta_entry, MTLK_OBJ_PTR(sta))
    MTLK_CLEANUP_STEP(sta_entry, PEER_ANALYZER, MTLK_OBJ_PTR(sta),
                      mtlk_peer_analyzer_cleanup, (&sta->info.sta_analyzer));
    MTLK_CLEANUP_STEP(sta_entry, WSS_HCNTRs, MTLK_OBJ_PTR(sta),
                      mtlk_wss_cntrs_close, (sta->wss, sta->wss_hcntrs, ARRAY_SIZE(sta->wss_hcntrs)));
    MTLK_CLEANUP_STEP(sta_entry, WSS_NODE, MTLK_OBJ_PTR(sta),
                      mtlk_wss_delete, (sta->wss));
    MTLK_CLEANUP_STEP(sta_entry, IRBD_FLAGS_NODE, MTLK_OBJ_PTR(sta),
                      mtlk_irbd_free, (sta->irbd_flags));
    MTLK_CLEANUP_STEP(sta_entry, IRBD_NODE, MTLK_OBJ_PTR(sta),
                      mtlk_irbd_free, (sta->irbd));
    MTLK_CLEANUP_STEP(sta_entry, KALV_TMR, MTLK_OBJ_PTR(sta),
                      mtlk_osal_timer_cleanup, (&sta->keepalive_timer))
    MTLK_CLEANUP_STEP(sta_entry, STA_LOCK, MTLK_OBJ_PTR(sta),
                      mtlk_osal_lock_cleanup, (&sta->lock))
  MTLK_CLEANUP_END(sta_entry, MTLK_OBJ_PTR(sta))
}

static int
_mtlk_sta_init (sta_entry *sta,
                sta_info* info_cfg,
                mtlk_vap_handle_t vap_handle,
                sta_db *paramdb)
{
  MTLK_STATIC_ASSERT(ARRAY_SIZE(_mtlk_sta_wss_id_map)  == MTLK_STAI_CNT_LAST);
  MTLK_STATIC_ASSERT(ARRAY_SIZE(sta->wss_hcntrs) == MTLK_STAI_CNT_LAST);

  memset(sta, 0, sizeof(*sta));
  sta->info = *info_cfg;
  sta->info.sid_order = paramdb->sid_order;
  sta->paramdb = paramdb;
  sta->vap_handle = vap_handle;

  /* Invalidate all rate parameters */
  sta->info.stats.tx_data_rate_params = MTLK_BITRATE_PARAMS_INVALID;
  sta->info.stats.rx_data_rate_info   = MTLK_BITRATE_INFO_INVALID;
  sta->info.stats.rx_mgmt_rate_info   = MTLK_BITRATE_INFO_INVALID;

  mtlk_osal_atomic_set(&sta->ref_cnt, 0);

  MTLK_INIT_TRY(sta_entry, MTLK_OBJ_PTR(sta))
    MTLK_INIT_STEP(sta_entry, STA_LOCK, MTLK_OBJ_PTR(sta),
                   mtlk_osal_lock_init, (&sta->lock));
    MTLK_INIT_STEP(sta_entry, KALV_TMR, MTLK_OBJ_PTR(sta),
                   mtlk_osal_timer_init, (&sta->keepalive_timer,
                                          _mtlk_sta_keepalive_tmr,
                                          HANDLE_T(sta)));
    MTLK_INIT_STEP_EX(sta_entry, IRBD_NODE, MTLK_OBJ_PTR(sta),
                      mtlk_irbd_alloc, (),
                      sta->irbd, sta->irbd != NULL, MTLK_ERR_NO_MEM);
    MTLK_INIT_STEP_EX(sta_entry, IRBD_FLAGS_NODE, MTLK_OBJ_PTR(sta),
                      mtlk_irbd_alloc, (),
                      sta->irbd_flags, sta->irbd_flags != NULL, MTLK_ERR_NO_MEM);
    MTLK_INIT_STEP_EX(sta_entry, WSS_NODE, MTLK_OBJ_PTR(sta),
                      mtlk_wss_create, (paramdb->wss, _mtlk_sta_wss_id_map, ARRAY_SIZE(_mtlk_sta_wss_id_map)),
                      sta->wss, sta->wss != NULL, MTLK_ERR_NO_MEM);
    MTLK_INIT_STEP(sta_entry, WSS_HCNTRs, MTLK_OBJ_PTR(sta),
                   mtlk_wss_cntrs_open, (sta->wss, _mtlk_sta_wss_id_map, sta->wss_hcntrs, MTLK_STAI_CNT_LAST));
    MTLK_INIT_STEP(sta_entry, PEER_ANALYZER, MTLK_OBJ_PTR(sta),
                   mtlk_peer_analyzer_init, (&sta->info.sta_analyzer));
  MTLK_INIT_FINALLY(sta_entry, MTLK_OBJ_PTR(sta))
  MTLK_INIT_RETURN(sta_entry, MTLK_OBJ_PTR(sta), _mtlk_sta_cleanup, (sta))
}

/* WARNING: the function _mtlk_sta_stop() cannot be called from a spinlock context
 * due to mtlk_irbd_cleanup() calls wait functions !!! */
static __INLINE void
_mtlk_sta_stop (sta_entry *sta)
{
  MTLK_STOP_BEGIN(sta_entry, MTLK_OBJ_PTR(sta))
    MTLK_STOP_STEP(sta_entry, ENABLE_FILTER, MTLK_OBJ_PTR(sta),
                   MTLK_NOACTION, ());
#ifdef MTLK_LEGACY_STATISTICS
    MTLK_STOP_STEP(sta_entry, STA_STAT_REQ_HANDLER, MTLK_OBJ_PTR(sta),
                   mtlk_wssd_unregister_request_handler, (sta->irbd, sta->stat_irb_handle));
#endif
    MTLK_STOP_STEP(sta_entry, IRBD_FLAGS_NODE, MTLK_OBJ_PTR(sta),
                   mtlk_irbd_cleanup, (sta->irbd_flags));
    MTLK_STOP_STEP(sta_entry, IRBD_NODE, MTLK_OBJ_PTR(sta),
                   mtlk_irbd_cleanup, (sta->irbd));
    MTLK_STOP_STEP(sta_entry, KALV_TMR, MTLK_OBJ_PTR(sta),
                   mtlk_osal_timer_cancel_sync, (&sta->keepalive_timer));
  MTLK_STOP_END(sta_entry, MTLK_OBJ_PTR(sta))
}

static __INLINE int
_mtlk_sta_start (sta_entry *sta, const IEEE_ADDR *addr, BOOL dot11n_mode)
{
  char irbd_name[sizeof(MTLK_IRB_STA_NAME) + sizeof("XX:XX:XX:XX:XX:XX")];
  char sta_auth_flag;
  char irbd_flags[sizeof(MTLK_IRB_STA_FLAGS) + sizeof(sta_auth_flag)];

  /* This call cannot fail since the MAC addr string size is constant */
  mtlk_snprintf(irbd_name, sizeof(irbd_name), MTLK_IRB_STA_NAME "%Y", addr->au8Addr);
  /* STA is not authorized yet */
  sta_auth_flag = (char)FALSE;
  mtlk_snprintf(irbd_flags, sizeof(irbd_flags), MTLK_IRB_STA_FLAGS "%hhd", sta_auth_flag);

  MTLK_START_TRY(sta_entry, MTLK_OBJ_PTR(sta))

    sta->connection_timestamp = mtlk_osal_timestamp();
    sta->activity_timestamp   = sta->connection_timestamp;

    sta->info.cipher       = IW_ENCODE_ALG_NONE;

    sta->beacon_interval   = DEFAULT_BEACON_INTERVAL;
    sta->beacon_timestamp  = 0;

    MTLK_START_STEP(sta_entry, KALV_TMR, MTLK_OBJ_PTR(sta),
                    mtlk_osal_timer_set, (&sta->keepalive_timer,
                                           sta->paramdb->keepalive_interval ?
                                           sta->paramdb->keepalive_interval :
                                           DEFAULT_KEEPALIVE_TIMEOUT));
    MTLK_START_STEP(sta_entry, IRBD_NODE, MTLK_OBJ_PTR(sta),
                    mtlk_irbd_init, (sta->irbd, mtlk_vap_get_irbd(sta->vap_handle), irbd_name));
    MTLK_START_STEP(sta_entry, IRBD_FLAGS_NODE, MTLK_OBJ_PTR(sta),
                    mtlk_irbd_init, (sta->irbd_flags, sta->irbd, irbd_flags));
#ifdef MTLK_LEGACY_STATISTICS
    MTLK_START_STEP_EX(sta_entry, STA_STAT_REQ_HANDLER, MTLK_OBJ_PTR(sta),
                       mtlk_wssd_register_request_handler, (sta->irbd, _mtlk_sta_stat_handle_request, HANDLE_T(sta)),
                       sta->stat_irb_handle, sta->stat_irb_handle != NULL, MTLK_ERR_UNKNOWN);
#endif
    MTLK_START_STEP_VOID(sta_entry, ENABLE_FILTER, MTLK_OBJ_PTR(sta),
                         _mtlk_sta_set_packets_filter_default, (sta));
  MTLK_START_FINALLY(sta_entry, MTLK_OBJ_PTR(sta))
  MTLK_START_RETURN(sta_entry, MTLK_OBJ_PTR(sta), _mtlk_sta_stop, (sta))
}

void __MTLK_IFUNC
mtlk_sta_on_packet_sent (sta_entry *sta, uint32 nbuf_len, uint32 nbuf_flags)
{
  ASSERT(sta != NULL);

  if (0 != nbuf_len) { /*skip null packets */
#if MTLK_WWSS_WLAN_STAT_ANALYZER_TX_RATE_ALLOWED
    mtlk_peer_analyzer_process_tx_packet(&sta->info.sta_analyzer, nbuf_len);
#endif
  }
}

/* with DEBUG statistic */
#if MTLK_MTIDL_PEER_STAT_FULL

void __MTLK_IFUNC
mtlk_sta_on_packet_dropped(sta_entry *sta, mtlk_tx_drop_reasons_e reason)
{
  __mtlk_sta_inc_cnt(sta, reason);
}

#endif /* MTLK_MTIDL_PEER_STAT_FULL */

void __MTLK_IFUNC
mtlk_sta_on_packet_indicated(sta_entry *sta, mtlk_nbuf_t *nbuf, uint32 nbuf_flags)
{
#if MTLK_WWSS_WLAN_STAT_ANALYZER_RX_RATE_ALLOWED
  uint32 data_length;
  ASSERT(sta != NULL);
  data_length = mtlk_df_nbuf_get_data_length(nbuf);

  mtlk_peer_analyzer_process_rx_packet(&sta->info.sta_analyzer, data_length);
#endif
}

void __MTLK_IFUNC
mtlk_sta_on_rx_packet_802_1x(sta_entry *sta)
{
  ASSERT(sta != NULL);
  _mtlk_sta_inc_cnt(sta, MTLK_STAI_CNT_802_1X_PACKETS_RECEIVED);
}

void __MTLK_IFUNC
mtlk_sta_on_tx_packet_802_1x(sta_entry *sta)
{
  ASSERT(sta != NULL);
  _mtlk_sta_inc_cnt(sta, MTLK_STAI_CNT_802_1X_PACKETS_SENT);
}

void __MTLK_IFUNC
mtlk_sta_on_tx_packet_discarded_802_1x(sta_entry *sta)
{
  ASSERT(sta != NULL);
  _mtlk_sta_inc_cnt(sta, MTLK_STAI_CNT_802_1X_PACKETS_DISCARDED);
}

void __MTLK_IFUNC
mtlk_sta_on_rx_packet_forwarded(sta_entry *sta, mtlk_nbuf_t *nbuf)
{
  ASSERT(sta != NULL);
  _mtlk_sta_inc_cnt(sta, MTLK_STAI_CNT_FWD_RX_PACKETS);
  _mtlk_sta_add_cnt(sta, MTLK_STAI_CNT_FWD_RX_BYTES, mtlk_df_nbuf_get_data_length(nbuf));
}

int __MTLK_IFUNC
mtlk_sta_update_beacon_interval (sta_entry *sta, uint16 beacon_interval)
{
  int lost_beacons;

  if (beacon_interval) {
    sta->beacon_interval = beacon_interval;
  }

  /* if sta->beacon_timestamp is zero - we receive first beacon from AP */
  if (sta->beacon_timestamp) {
    mtlk_osal_timestamp_diff_t diff;
    diff = mtlk_osal_timestamp_diff(mtlk_osal_timestamp(), sta->beacon_timestamp);
    lost_beacons = mtlk_osal_timestamp_to_ms(diff) / sta->beacon_interval;
  } else {
    lost_beacons = 0;
  }

  sta->beacon_timestamp = mtlk_osal_timestamp();

  ILOG3_YDD("Update AP %Y - %lu (beacon interval %d)",
    mtlk_sta_get_addr(sta), sta->activity_timestamp, beacon_interval);

  return lost_beacons;
}

void __MTLK_IFUNC
mtlk_sta_update_phy_info (sta_entry *sta, mtlk_hw_t *hw, stationPhyRxStatusDb_t *sta_status, BOOL is_gen6)
{
    mtlk_mhi_stats_sta_tx_rate_t  *mhi_tx_stats;
    uint32 word;
    int8   noise;
    int    i;

    MTLK_STATIC_ASSERT(sizeof(sta->info.stats.data_rssi) == sizeof(sta_status->rssi));
    MTLK_STATIC_ASSERT(PHY_STATISTICS_MAX_RX_ANT == sizeof(sta_status->rssi));
    MTLK_STATIC_ASSERT(PHY_STATISTICS_MAX_RX_ANT == sizeof(sta_status->noise));
    MTLK_STATIC_ASSERT(PHY_STATISTICS_MAX_RX_ANT == sizeof(sta->info.stats.data_rssi));
    MTLK_STATIC_ASSERT(PHY_STATISTICS_MAX_RX_ANT == sizeof(sta->info.stats.snr));

    mtlk_osal_lock_acquire(&sta->lock);

    /* Calculate RSSI [dBm] per antenna and MAX RSSI by PHY RSSI of STA status */
    sta->info.stats.max_rssi = mtlk_hw_get_rssi_max_by_rx_phy_rssi(hw,
                                sta_status->rssi, sta->info.stats.data_rssi);

    ILOG3_DDDDDD("SID %02X Updated rssi: %d, %d, %d, %d, max_rssi %d",
                mtlk_sta_get_sid(sta),
                sta->info.stats.data_rssi[0], sta->info.stats.data_rssi[1],
                sta->info.stats.data_rssi[2], sta->info.stats.data_rssi[3],
                sta->info.stats.max_rssi);

#if MTLK_WWSS_WLAN_STAT_ANALYZER_RX_LONG_RSSI_ALLOWED
    mtlk_peer_analyzer_process_rssi_sample(&sta->info.sta_analyzer, sta->info.stats.data_rssi);
#endif

    for (i = 0; i < ARRAY_SIZE(sta_status->noise); i++) {
      /* Gen5: Noise already in dBm from FW */
      /* Gen6: Convert PHY Noise to Noise in dBm */
      noise = sta_status->noise[i];
      if (is_gen6) {
        noise = mtlk_hw_noise_phy_to_noise_dbm(hw, noise, sta_status->gain[i]);
      }
      /* Calculate SNR [dB] using RSSI [dBm] and Noise [dBm] */
      sta->info.stats.snr[i] = mtlk_calculate_snr(sta->info.stats.data_rssi[i], noise);
      ILOG3_DDDDDD("[%d] RSSI %d, gain %u, noise %u -> %d, SNR %d",
                   i, sta->info.stats.data_rssi[i], sta_status->gain[i],
                   sta_status->noise[i], noise, sta->info.stats.snr[i]);
    }

    mhi_tx_stats = mtlk_sta_get_mhi_tx_stats(sta);

#ifdef MTLK_LEGACY_STATISTICS
    ILOG3_DDDDD("mode %d, cbw %d, scp %d, mcs %2d, nss %d",
        mhi_tx_stats->stat[STAT_STA_TX_RATE_MODE],
        mhi_tx_stats->stat[STAT_STA_TX_RATE_CBW],
        mhi_tx_stats->stat[STAT_STA_TX_RATE_SCP],
        mhi_tx_stats->stat[STAT_STA_TX_RATE_MCS],
        mhi_tx_stats->stat[STAT_STA_TX_RATE_NSS]);

    sta->info.stats.tx_data_rate_params = MTLK_BITRATE_PARAMS_BY_RATE_PARAMS(
                                            mhi_tx_stats->stat[STAT_STA_TX_RATE_MODE],
                                            mhi_tx_stats->stat[STAT_STA_TX_RATE_CBW],
                                            mhi_tx_stats->stat[STAT_STA_TX_RATE_SCP],
                                            mhi_tx_stats->stat[STAT_STA_TX_RATE_MCS],
                                            mhi_tx_stats->stat[STAT_STA_TX_RATE_NSS]);
#else
    ILOG3_DDDDD("mode %d, cbw %d, scp %d, mcs %2d, nss %d",
                                 mhi_tx_stats->DataPhyMode,
                                 mhi_tx_stats->dataBwLimit,
                                 mhi_tx_stats->scpData,
                                 mhi_tx_stats->mcsData,
                                 mhi_tx_stats->nssData);

    sta->info.stats.tx_data_rate_params = MTLK_BITRATE_PARAMS_BY_RATE_PARAMS(
                                                   mhi_tx_stats->DataPhyMode,
                                                   mhi_tx_stats->dataBwLimit,
                                                   mhi_tx_stats->scpData,
                                                   mhi_tx_stats->mcsData,
                                                   mhi_tx_stats->nssData);
#endif

    word = MAC_TO_HOST32(sta_status->phyRate);
    sta->info.stats.rx_data_rate_info =
        MTLK_BITRATE_INFO_BY_PHY_RATE(MTLK_BFIELD_GET(word, PHY_RX_STATUS_PHY_RATE_VALUE));

    mtlk_osal_lock_release(&sta->lock);
}

void __MTLK_IFUNC
mtlk_sta_set_packets_filter (sta_entry         *sta,
                             mtlk_pckt_filter_e filter_type)
{
  if (sta->info.filter != filter_type) {
    ILOG1_YDD("STA (%Y) filter: %d => %d", mtlk_sta_get_addr(sta), sta->info.filter, filter_type);
    sta->info.filter = filter_type;
  }
}

void __MTLK_IFUNC
mtlk_sta_get_peer_stats (const sta_entry* sta, mtlk_wssa_drv_peer_stats_t* stats)
{
  MTLK_ASSERT(sta != NULL);
  MTLK_ASSERT(stats != NULL);

  _mtlk_sta_get_peer_stats(sta, stats);
}

void __MTLK_IFUNC
mtlk_sta_get_peer_capabilities (const sta_entry* sta, mtlk_wssa_drv_peer_capabilities_t* capabilities)
{
  MTLK_ASSERT(sta != NULL);
  MTLK_ASSERT(capabilities != NULL);

  _mtlk_sta_get_peer_capabilities(sta, capabilities);
}


/******************************************************************************************/

/******************************************************************************************
 * STA DB API
 ******************************************************************************************/
#define MTLK_STADB_ITER_ADDBA_PERIOD     1000 /* msec */

#define MTLK_STADB_HASH_NOF_BUCKETS      16
#define MTLK_SID_HASH_NOF_BUCKETS        128

void __MTLK_IFUNC
__mtlk_sta_on_unref_private (sta_entry *sta)
{
  MTLK_ASSERT(sta != NULL);
  MTLK_ASSERT(sta->paramdb != NULL);

  ILOG2_Y("No more references of STA (%Y)!",
          mtlk_sta_get_addr(sta));

  _mtlk_sta_cleanup(sta);
  mtlk_osal_mem_free(sta);
}


sta_entry * __MTLK_IFUNC
mtlk_stadb_add_sta (sta_db *stadb, const unsigned char *mac, sta_info *info_cfg)
{
  int                           res = MTLK_ERR_UNKNOWN;
  sta_entry                    *sta = NULL;
  MTLK_HASH_ENTRY_T(ieee_addr) *h;
  BOOL dot11n_mode;

  sta = mtlk_osal_mem_alloc(sizeof(*sta), MTLK_MEM_TAG_STADB_ITER);
  if (!sta) {
    res = MTLK_ERR_NO_MEM;
    WLOG_V("Can't alloc STA");
    goto err_alloc;
  }

  res = _mtlk_sta_init(sta, info_cfg, stadb->vap_handle, stadb);
  if (res != MTLK_ERR_OK) {
    ELOG_D("Can't init STA (err#%d)", res);
    goto err_init;
  }

  mtlk_sta_incref(sta); /* Reference by a caller */

  dot11n_mode = MTLK_BFIELD_GET(info_cfg->flags, STA_FLAGS_11n);
  res = _mtlk_sta_start(sta, (IEEE_ADDR *)mac, dot11n_mode);
  if (res != MTLK_ERR_OK) {
    ELOG_D("Can't start STA (err#%d)", res);
    goto err_start;
  }

  mtlk_osal_lock_acquire(&stadb->lock);
  h = mtlk_hash_insert_ieee_addr(&stadb->hash, (IEEE_ADDR *)mac, &sta->hentry);
  if (h == NULL) {
    ++stadb->hash_cnt;
    mtlk_sta_incref(sta); /* Reference by STA DB hash */

    mtlk_hash_insert_sid(&stadb->sid_hash, &info_cfg->sid, &sta->hentry_sid);
    ++stadb->sid_hash_cnt;
    ++stadb->sid_order;
    mtlk_sta_incref(sta); /* Reference by STA DB SID hash */
  }
  mtlk_osal_lock_release(&stadb->lock);

  /* Increase global counter */
  if (h == NULL) {
    mtlk_osal_atomic_inc(__mtlk_global_stadb_get_cntr());
    wave_radio_sta_cnt_inc(wave_vap_radio_get(stadb->vap_handle));
  }

  if (h != NULL) {
    res = MTLK_ERR_PROHIB;
    WLOG_Y("%Y already connected", mac);
    goto err_insert;
  }

  if (mtlk_sta_info_is_4addr(info_cfg))
    mtlk_osal_atomic_inc(&stadb->four_addr_sta_cnt);

  ILOG3_YP("PEER %Y added (%p)", mac, sta);

  return sta;

err_insert:
  _mtlk_sta_stop(sta);
err_start:
  _mtlk_sta_cleanup(sta);
err_init:
  mtlk_osal_mem_free(sta);
err_alloc:
  return NULL;
}

void __MTLK_IFUNC
mtlk_stadb_remove_sta (sta_db* stadb, sta_entry *sta)
{
  /* Decrease global and per radio counters */
  mtlk_osal_atomic_dec(__mtlk_global_stadb_get_cntr());
  wave_radio_sta_cnt_dec(wave_vap_radio_get(stadb->vap_handle));

  /* Stop STA (_mtlk_sta_stop() cannot be called under spinlock) */
  _mtlk_sta_stop(sta);

  /* Remove STA from STADB */
  mtlk_osal_lock_acquire(&stadb->lock);

  --stadb->hash_cnt;
  mtlk_hash_remove_ieee_addr(&stadb->hash, &sta->hentry);

  --stadb->sid_hash_cnt;
  mtlk_hash_remove_sid(&stadb->sid_hash, &sta->hentry_sid);

  if (mtlk_sta_is_4addr(sta))
    mtlk_osal_atomic_dec(&stadb->four_addr_sta_cnt);

  mtlk_osal_lock_release(&stadb->lock);

  mtlk_mc_drop_sta(mtlk_vap_get_core(stadb->vap_handle), sta);

  ILOG3_Y("PEER %Y removed", mtlk_sta_get_addr(sta)->au8Addr);

  mtlk_sta_decref(sta); /* De-reference by STA DB hash */
  mtlk_sta_decref(sta); /* De-reference by STA DB SID hash */
}

void __MTLK_IFUNC
mtlk_stadb_set_sta_auth_flag_in_irbd (sta_entry *sta)
{
  char sta_auth_flag;
  char irbd_flags[sizeof(MTLK_IRB_STA_FLAGS) + sizeof(sta_auth_flag)];

  /* STA must be already authorized */
  sta_auth_flag = (char)TRUE;
  mtlk_snprintf(irbd_flags, sizeof(irbd_flags), MTLK_IRB_STA_FLAGS "%hhd", sta_auth_flag);

  if (MTLK_ERR_OK == mtlk_irbd_change_unique_desc(sta->irbd_flags, irbd_flags)) {
    ILOG1_Y("AUTH-flag is set in IRBD entry of STA (%Y).", mtlk_sta_get_addr(sta));
  }
  else {
    ELOG_Y("Failed to set AUTH-flag in IRBD entry of STA (%Y).", mtlk_sta_get_addr(sta));
  }
}

MTLK_INIT_STEPS_LIST_BEGIN(sta_db)
  MTLK_INIT_STEPS_LIST_ENTRY(sta_db, STADB_HASH)
  MTLK_INIT_STEPS_LIST_ENTRY(sta_db, SID_HASH)
  MTLK_INIT_STEPS_LIST_ENTRY(sta_db, STADB_LOCK)
  MTLK_INIT_STEPS_LIST_ENTRY(sta_db, REG_ABILITIES)
  MTLK_INIT_STEPS_LIST_ENTRY(sta_db, EN_ABILITIES)
MTLK_INIT_INNER_STEPS_BEGIN(sta_db)
MTLK_INIT_STEPS_LIST_END(sta_db);

MTLK_START_STEPS_LIST_BEGIN(sta_db)
  MTLK_START_STEPS_LIST_ENTRY(sta_db, STADB_WSS_CREATE)
MTLK_START_INNER_STEPS_BEGIN(sta_db)
MTLK_START_STEPS_LIST_END(sta_db);

static const mtlk_ability_id_t _stadb_abilities[] = {
  WAVE_CORE_REQ_GET_STADB_CFG,
  WAVE_CORE_REQ_SET_STADB_CFG,
  WAVE_CORE_REQ_GET_STADB_STATUS,
  WAVE_CORE_REQ_GET_STADB_STA_BY_ITER_ID
};

int __MTLK_IFUNC
mtlk_stadb_init (sta_db *stadb, mtlk_vap_handle_t vap_handle)
{
  MTLK_ASSERT(stadb != NULL);

  stadb->vap_handle = vap_handle;

  // Set default configuration
  stadb->keepalive_interval    = DEFAULT_KEEPALIVE_TIMEOUT;
  stadb->hash_cnt              = 0;
  stadb->sid_hash_cnt          = 0;
  stadb->sid_order             = 0;
  stadb->flags                |= STADB_FLAGS_STOPPED;

  MTLK_INIT_TRY(sta_db, MTLK_OBJ_PTR(stadb))
    MTLK_INIT_STEP(sta_db, STADB_HASH, MTLK_OBJ_PTR(stadb),
                   mtlk_hash_init_ieee_addr, (&stadb->hash, MTLK_STADB_HASH_NOF_BUCKETS))
    MTLK_INIT_STEP(sta_db, SID_HASH, MTLK_OBJ_PTR(stadb),
                   mtlk_hash_init_sid, (&stadb->sid_hash, MTLK_SID_HASH_NOF_BUCKETS))
    MTLK_INIT_STEP(sta_db, STADB_LOCK, MTLK_OBJ_PTR(stadb),
                   mtlk_osal_lock_init, (&stadb->lock));
    MTLK_INIT_STEP(sta_db, REG_ABILITIES, MTLK_OBJ_PTR(stadb),
                    mtlk_abmgr_register_ability_set,
                    (mtlk_vap_get_abmgr(stadb->vap_handle), _stadb_abilities, ARRAY_SIZE(_stadb_abilities)));
    MTLK_INIT_STEP_VOID(sta_db, EN_ABILITIES, MTLK_OBJ_PTR(stadb),
                        mtlk_abmgr_enable_ability_set,
                        (mtlk_vap_get_abmgr(stadb->vap_handle), _stadb_abilities, ARRAY_SIZE(_stadb_abilities)));
  MTLK_INIT_FINALLY(sta_db, MTLK_OBJ_PTR(stadb))
  MTLK_INIT_RETURN(sta_db, MTLK_OBJ_PTR(stadb), mtlk_stadb_cleanup, (stadb))
}

void __MTLK_IFUNC
mtlk_stadb_configure (sta_db *stadb, const sta_db_cfg_t *cfg)
{
  MTLK_ASSERT(stadb != NULL);
  MTLK_ASSERT(cfg != NULL);
  MTLK_ASSERT(cfg->max_nof_stas != 0);
  MTLK_ASSERT(cfg->parent_wss != NULL);
  MTLK_ASSERT(cfg->api.on_sta_keepalive != NULL);

  stadb->cfg = *cfg;
}

int __MTLK_IFUNC
mtlk_stadb_start (sta_db *stadb)
{
  MTLK_ASSERT(stadb != NULL);

  if (!(stadb->flags & STADB_FLAGS_STOPPED))
    return MTLK_ERR_OK;

  if (!(stadb->flags & STADB_FLAGS_STOPPED)) {
    return MTLK_ERR_OK;
  }

  MTLK_START_TRY(sta_db, MTLK_OBJ_PTR(stadb))
    MTLK_START_STEP_EX(sta_db, STADB_WSS_CREATE, MTLK_OBJ_PTR(stadb),
                       mtlk_wss_create, (stadb->cfg.parent_wss, NULL, 0),
                       stadb->wss, stadb->wss != NULL, MTLK_ERR_OK);
    stadb->flags &= ~STADB_FLAGS_STOPPED;
  MTLK_START_FINALLY(sta_db, MTLK_OBJ_PTR(stadb))
  MTLK_START_RETURN(sta_db, MTLK_OBJ_PTR(stadb), mtlk_stadb_stop, (stadb))
}

void __MTLK_IFUNC
mtlk_stadb_stop (sta_db *stadb)
{
  if (stadb->flags & STADB_FLAGS_STOPPED) {
    return;
  }

  MTLK_STOP_BEGIN(sta_db, MTLK_OBJ_PTR(stadb))
    MTLK_STOP_STEP(sta_db, STADB_WSS_CREATE, MTLK_OBJ_PTR(stadb),
                   mtlk_wss_delete, (stadb->wss));
    stadb->flags |= STADB_FLAGS_STOPPED;
  MTLK_STOP_END(sta_db, MTLK_OBJ_PTR(stadb))
}

void __MTLK_IFUNC
mtlk_stadb_cleanup (sta_db *stadb)
{
  MTLK_CLEANUP_BEGIN(sta_db, MTLK_OBJ_PTR(stadb))
    MTLK_CLEANUP_STEP(sta_db, EN_ABILITIES, MTLK_OBJ_PTR(stadb),
                      mtlk_abmgr_disable_ability_set,
                      (mtlk_vap_get_abmgr(stadb->vap_handle), _stadb_abilities, ARRAY_SIZE(_stadb_abilities)));
    MTLK_CLEANUP_STEP(sta_db, REG_ABILITIES, MTLK_OBJ_PTR(stadb),
                      mtlk_abmgr_unregister_ability_set,
                      (mtlk_vap_get_abmgr(stadb->vap_handle), _stadb_abilities, ARRAY_SIZE(_stadb_abilities)));
    MTLK_CLEANUP_STEP(sta_db, STADB_LOCK, MTLK_OBJ_PTR(stadb),
                      mtlk_osal_lock_cleanup, (&stadb->lock));
    MTLK_CLEANUP_STEP(sta_db, SID_HASH, MTLK_OBJ_PTR(stadb),
                      mtlk_hash_cleanup_sid, (&stadb->sid_hash));
    MTLK_CLEANUP_STEP(sta_db, STADB_HASH, MTLK_OBJ_PTR(stadb),
                      mtlk_hash_cleanup_ieee_addr, (&stadb->hash));
  MTLK_CLEANUP_END(sta_db, MTLK_OBJ_PTR(stadb))
}

const sta_entry * __MTLK_IFUNC
mtlk_stadb_iterate_first (sta_db *stadb, mtlk_stadb_iterator_t *iter)
{
  int                           err = MTLK_ERR_UNKNOWN;
  const sta_entry *             res = NULL;
  uint32                        idx = 0;
  mtlk_hash_enum_t              e;
  MTLK_HASH_ENTRY_T(ieee_addr) *h;

  MTLK_ASSERT(stadb != NULL);
  MTLK_ASSERT(iter != NULL);

  mtlk_osal_lock_acquire(&stadb->lock);

  iter->size = stadb->hash_cnt;

  if (!iter->size) {
    err = MTLK_ERR_NOT_IN_USE;
    goto end;
  }

  iter->arr =
    (sta_entry**)mtlk_osal_mem_alloc(iter->size * sizeof(sta_entry *),
                                     MTLK_MEM_TAG_STADB_ITER);
  if (!iter->arr) {
    ELOG_D("Can't allocate iteration array of %d entries", iter->size);
    err = MTLK_ERR_NO_MEM;
    goto end;
  }

  memset(iter->arr, 0, iter->size * sizeof(sta_entry *));

  h = mtlk_hash_enum_first_ieee_addr(&stadb->hash, &e);
  while (idx < iter->size && h) {
    sta_entry *sta = MTLK_CONTAINER_OF(h, sta_entry, hentry);
    iter->arr[idx] = sta;
    mtlk_sta_incref(sta); /* Reference by iterator */
    ++idx;
    h = mtlk_hash_enum_next_ieee_addr(&stadb->hash, &e);
  }

  err = MTLK_ERR_OK;

end:
  mtlk_osal_lock_release(&stadb->lock);

  if (err == MTLK_ERR_OK) {
    iter->stadb = stadb;
    iter->idx   = 0;

    res = mtlk_stadb_iterate_next(iter);
    if (!res) {
      mtlk_stadb_iterate_done(iter);
    }
  }

  return res;
}

const sta_entry * __MTLK_IFUNC
mtlk_stadb_iterate_next (mtlk_stadb_iterator_t *iter)
{
  const sta_entry *sta = NULL;

  MTLK_ASSERT(iter != NULL);
  MTLK_ASSERT(iter->stadb != NULL);
  MTLK_ASSERT(iter->arr != NULL);

  if (iter->idx < iter->size) {
    sta = iter->arr[iter->idx];
    ++iter->idx;
  }

  return sta;
}

void __MTLK_IFUNC
mtlk_stadb_iterate_done (mtlk_stadb_iterator_t *iter)
{
  uint32 idx = 0;

  MTLK_ASSERT(iter != NULL);
  MTLK_ASSERT(iter->stadb != NULL);
  MTLK_ASSERT(iter->arr != NULL);

  for (idx = 0; idx < iter->size; idx++) {
    mtlk_sta_decref(iter->arr[idx]); /* De-reference by iterator */
  }
  mtlk_osal_mem_free(iter->arr);
  memset(iter, 0, sizeof(*iter));
}

void __MTLK_IFUNC
mtlk_stadb_reset_cnts (sta_db *stadb)
{
  mtlk_hash_enum_t              e;
  MTLK_HASH_ENTRY_T(ieee_addr) *h;

  MTLK_ASSERT(stadb != NULL);

  mtlk_osal_lock_acquire(&stadb->lock);
  h = mtlk_hash_enum_first_ieee_addr(&stadb->hash, &e);
  while (h) {
    sta_entry *sta = MTLK_CONTAINER_OF(h, sta_entry, hentry);
    _mtlk_sta_reset_cnts(sta);
    h = mtlk_hash_enum_next_ieee_addr(&stadb->hash, &e);
  }
  mtlk_osal_lock_release(&stadb->lock);
}

void __MTLK_IFUNC
mtlk_stadb_disconnect_all (sta_db *stadb,
    mtlk_stadb_disconnect_sta_clb_f clb,
    mtlk_handle_t usr_ctx,
    BOOL wait_all_packets_confirmed)
{
  mtlk_hash_enum_t              e;
  MTLK_HASH_ENTRY_T(ieee_addr) *h;
  sta_entry                    *sta;
  mtlk_stadb_iterator_t       iter;

  MTLK_ASSERT(stadb != NULL);
  MTLK_ASSERT(clb != NULL);

  /* Function _mtlk_sta_stop() cannot be called from a spinlock context, so following algo is used:
   * 1) enable data traffic filtering for all STA's under spinlock;
   * 2) create a temporal copy of STADB (internal list of STA's) under spinlock;
   * 3) call _mtlk_sta_stop() for each STA outside of spinlock;
   * 4) delete temporal STADB copy;
   * 5) remove all STA's from STADB under spinlock'.
   */

  mtlk_osal_lock_acquire(&stadb->lock);
  h = mtlk_hash_enum_first_ieee_addr(&stadb->hash, &e);
  while (h) {
    sta = MTLK_CONTAINER_OF(h, sta_entry, hentry);
    mtlk_sta_set_packets_filter(sta, MTLK_PCKT_FLTR_DISCARD_ALL);
    h = mtlk_hash_enum_next_ieee_addr(&stadb->hash, &e);
  }
  mtlk_osal_lock_release(&stadb->lock);

  MTLK_UNREFERENCED_PARAM(wait_all_packets_confirmed);

  /* Make a copy of STADB and stop all STA's outside of spinlock context */
  sta = (sta_entry*)mtlk_stadb_iterate_first(stadb, &iter);
  if (sta) {
    do {
      clb(usr_ctx, sta);
      mtlk_stadb_remove_sta(stadb, sta);

      sta = (sta_entry*)mtlk_stadb_iterate_next(&iter);
    } while (sta);
    mtlk_stadb_iterate_done(&iter);
  }

  if (!mtlk_stadb_is_empty(stadb)) {
    ELOG_V("STA DB must be empty but actually not");
    MTLK_ASSERT(0);
  }
}

int __MTLK_IFUNC
mtlk_stadb_get_peer_list (sta_db *stadb, mtlk_clpb_t *clpb)
{
  int                   res = MTLK_ERR_OK;
  const sta_entry       *sta;
  mtlk_stadb_iterator_t iter;
  mtlk_wssa_peer_list_t peer_list;
  int                   sta_cnt =  mtlk_stadb_get_sta_cnt(stadb);

  res = mtlk_clpb_push(clpb, &sta_cnt, sizeof(int));
  if (MTLK_ERR_OK != res) {
    return res;
  }

  sta = mtlk_stadb_iterate_first(stadb, &iter);
  if (sta) {
    do {
      peer_list.addr = *(mtlk_sta_get_addr(sta));
      peer_list.is_sta_auth = _mtlk_sta_get_is_sta_auth(sta);

      res = mtlk_clpb_push(clpb, &peer_list, sizeof(mtlk_wssa_peer_list_t));
      if (MTLK_ERR_OK != res) {
        goto err_push_sta;
      }

      sta = mtlk_stadb_iterate_next(&iter);
    } while (sta);
    mtlk_stadb_iterate_done(&iter);
  }

  return res;

err_push_sta:
  mtlk_stadb_iterate_done(&iter);
  mtlk_clpb_purge(clpb);
  return res;
}

int __MTLK_IFUNC
mtlk_stadb_get_stat(sta_db *stadb, hst_db *hstdb, mtlk_clpb_t *clpb, uint8 group_cipher)
{
  int                   res = MTLK_ERR_OK;
  const sta_entry       *sta;
  mtlk_stadb_iterator_t iter;
  mtlk_stadb_stat_t     stadb_stat;
  mtlk_hstdb_iterator_t hiter;
  int bridge_mode;

  bridge_mode = wave_pdb_get_int(mtlk_vap_get_param_db(stadb->vap_handle), PARAM_DB_CORE_BRIDGE_MODE);

  sta = mtlk_stadb_iterate_first(stadb, &iter);
  if (sta) {
    do {

      /* Omit Open-mode STAs if security enabled */
      if ((!group_cipher) ||
          (group_cipher == IW_ENCODE_ALG_WEP) || (mtlk_sta_get_cipher(sta)) ||
          MTLK_BFIELD_GET(sta->info.flags, STA_FLAGS_WDS)) {

        /* Get general STA statistic */
        memset(&stadb_stat, 0, sizeof(stadb_stat));

        stadb_stat.type = STAT_ID_STADB;
        stadb_stat.u.general_stat.addr = *(mtlk_sta_get_addr(sta));
        stadb_stat.u.general_stat.sta_sid = mtlk_sta_get_sid(sta);
        stadb_stat.u.general_stat.aid = mtlk_sta_get_sid(sta) + 1;
        stadb_stat.u.general_stat.sta_rx_dropped = 0;
        /* stadb_stat.u.general_stat.network_mode = mtlk_sta_get_net_mode(sta); */
        stadb_stat.u.general_stat.tx_rate = _mtlk_sta_get_tx_data_rate(sta);
        stadb_stat.u.general_stat.is_sta_auth = _mtlk_sta_get_is_sta_auth(sta);
        stadb_stat.u.general_stat.is_four_addr = mtlk_sta_is_4addr(sta);
        stadb_stat.u.general_stat.nss = _mtlk_sta_get_nss(sta);

        mtlk_sta_get_peer_stats(sta, &stadb_stat.u.general_stat.peer_stats);

        /* push stadb statistic to clipboard */
        res = mtlk_clpb_push(clpb, &stadb_stat, sizeof(stadb_stat));
        if (MTLK_ERR_OK != res) {
          goto err_push_sta;
        }

        /* Get HostDB statistic related to current STA */
        if ((NULL != hstdb) && ((BR_MODE_WDS == bridge_mode) ||
          mtlk_stadb_get_four_addr_sta_cnt(stadb))) {

          const IEEE_ADDR *host_addr = mtlk_hstdb_iterate_first(hstdb, sta, &hiter);
          if (host_addr) {
            do {
              memset(&stadb_stat, 0, sizeof(stadb_stat));
              stadb_stat.type = STAT_ID_HSTDB;
              stadb_stat.u.hstdb_stat.addr = *host_addr;

              /* push to clipboard*/
              res = mtlk_clpb_push(clpb, &stadb_stat, sizeof(stadb_stat));
              if (MTLK_ERR_OK != res) {
                goto err_push_host;
              }

              host_addr = mtlk_hstdb_iterate_next(&hiter);
            } while (host_addr);
            mtlk_hstdb_iterate_done(&hiter);
          }
        }
      }

      sta = mtlk_stadb_iterate_next(&iter);
    } while (sta);
    mtlk_stadb_iterate_done(&iter);
  }

  return res;

err_push_host:
  mtlk_hstdb_iterate_done(&hiter);
err_push_sta:
  mtlk_stadb_iterate_done(&iter);
  mtlk_clpb_purge(clpb);
  return res;
}

int __MTLK_IFUNC mtlk_stadb_get_sta_by_iter_id(sta_db *stadb, int iter_id, uint8 *mac, struct station_info* stinfo)
{
  int                   res = MTLK_ERR_PARAMS;
  const sta_entry       *sta;
  mtlk_stadb_iterator_t iter;

  memset(mac, 0, ETH_ALEN);
  sta = mtlk_stadb_iterate_first(stadb, &iter);
  if (sta) {
    do {
      if (iter_id == 0) {
        ieee_addr_get(mac, mtlk_sta_get_addr(sta));

        if (stinfo) {
          mtlk_wssa_drv_tr181_peer_stats_t peer_stats;
          memset(&peer_stats, 0, sizeof(peer_stats));
          mtlk_sta_get_tr181_peer_stats(sta, &peer_stats);

          /* Signal Strength */
          stinfo->signal = peer_stats.SignalStrength;
          stinfo->filled |= BIT_ULL(NL80211_STA_INFO_SIGNAL);

          /* TX bitrate (Mbit/s) */
          stinfo->txrate.legacy = MTLK_KBPS_TO_MBPS(peer_stats.LastDataDownlinkRate);
          stinfo->filled |= BIT_ULL(NL80211_STA_INFO_TX_BITRATE);

          /* RX bitrate (Mbit/s) */
          stinfo->rxrate.legacy = MTLK_KBPS_TO_MBPS(peer_stats.LastDataUplinkRate);
          stinfo->filled |= BIT_ULL(NL80211_STA_INFO_RX_BITRATE);

          /* TX bytes */
          stinfo->tx_bytes = peer_stats.traffic_stats.BytesSent;
          stinfo->filled |= BIT_ULL(NL80211_STA_INFO_TX_BYTES);

          /* RX bytes */
          stinfo->rx_bytes = peer_stats.traffic_stats.BytesReceived;
          stinfo->filled |= BIT_ULL(NL80211_STA_INFO_RX_BYTES);

          /* TX packets */
          stinfo->tx_packets = peer_stats.traffic_stats.PacketsSent;
          stinfo->filled |= BIT_ULL(NL80211_STA_INFO_TX_PACKETS);

          /* RX packets */
          stinfo->rx_packets = peer_stats.traffic_stats.PacketsReceived;
          stinfo->filled |= BIT_ULL(NL80211_STA_INFO_RX_PACKETS);

          /* Connected time --> Doesn't show in the default iw */
          stinfo->connected_time = mtlk_sta_get_age(sta);
          stinfo->filled |= BIT_ULL(NL80211_STA_INFO_CONNECTED_TIME);

          /* Invactive time */
          stinfo->inactive_time = mtlk_sta_get_inactive_time(sta);
          stinfo->filled |= BIT_ULL(NL80211_STA_INFO_INACTIVE_TIME);
        }

        res = MTLK_ERR_OK;
        break;
      }
      sta = mtlk_stadb_iterate_next(&iter);
      iter_id--;
    } while (sta);
    mtlk_stadb_iterate_done(&iter);
  }

  return res;
}

BOOL __MTLK_IFUNC
mtlk_stadb_is_empty(sta_db *stadb)
{
  BOOL res;

  mtlk_osal_lock_acquire(&stadb->lock);
  res = (stadb->hash_cnt == 0);
  mtlk_osal_lock_release(&stadb->lock);

  return res;
}

uint32 __MTLK_IFUNC
mtlk_stadb_stas_num(sta_db *stadb)
{
  /* FIXME: lock needed */
  return stadb->hash_cnt;
}

sta_entry * __MTLK_IFUNC
mtlk_stadb_get_first_sta (sta_db *stadb)
{
  mtlk_hash_enum_t              e;
  MTLK_HASH_ENTRY_T(ieee_addr) *h;
  sta_entry                  *sta = NULL;

  MTLK_ASSERT(stadb != NULL);

  mtlk_osal_lock_acquire(&stadb->lock);
  h = mtlk_hash_enum_first_ieee_addr(&stadb->hash, &e);
  if (h) {
    sta = MTLK_CONTAINER_OF(h, sta_entry, hentry);
    mtlk_sta_incref(sta); /* Reference by caller */
  }
  mtlk_osal_lock_release(&stadb->lock);

  return sta;
}

/******************************************************************************************
 * HST API
 ******************************************************************************************/

static __INLINE BOOL
_mtlk_is_hst_behind_sta (const host_entry *host, const sta_entry *sta)
{
  return (host->sta == sta);
}

MTLK_INIT_STEPS_LIST_BEGIN(host_entry)
MTLK_INIT_INNER_STEPS_BEGIN(host_entry)
MTLK_INIT_STEPS_LIST_END(host_entry);
MTLK_START_STEPS_LIST_BEGIN(host_entry)
  MTLK_START_STEPS_LIST_ENTRY(host_entry, STA_REF)
MTLK_START_INNER_STEPS_BEGIN(host_entry)
MTLK_START_STEPS_LIST_END(host_entry);

static void
_mtlk_hst_cleanup (host_entry *host)
{
  MTLK_CLEANUP_BEGIN(host_entry, MTLK_OBJ_PTR(host))
  MTLK_CLEANUP_END(host_entry, MTLK_OBJ_PTR(host))
}

static int
_mtlk_hst_init (host_entry *host, hst_db *paramdb)
{
  memset(host, 0, sizeof(host_entry));

  host->paramdb = paramdb;

  MTLK_INIT_TRY(host_entry, MTLK_OBJ_PTR(host))
  MTLK_INIT_FINALLY(host_entry, MTLK_OBJ_PTR(host))
  MTLK_INIT_RETURN(host_entry, MTLK_OBJ_PTR(host), _mtlk_hst_cleanup, (host))
}

static __INLINE void
_mtlk_hst_stop (host_entry *host)
{

  MTLK_ASSERT(host->sta);

  MTLK_STOP_BEGIN(host_entry, MTLK_OBJ_PTR(host))
    MTLK_STOP_STEP(host_entry, STA_REF, MTLK_OBJ_PTR(host),
                   mtlk_sta_decref, (host->sta)); /* De-reference by HST */
  MTLK_STOP_END(host_entry, MTLK_OBJ_PTR(host))
  host->sta = NULL;
}

static __INLINE int
_mtlk_hst_start (host_entry *host, sta_entry *sta)
{
  host->timestamp = mtlk_osal_timestamp();
  host->sta       = sta;
  host->sta_addr  = *mtlk_sta_get_addr(sta);

  MTLK_START_TRY(host_entry, MTLK_OBJ_PTR(host))
    MTLK_START_STEP_VOID(host_entry, STA_REF, MTLK_OBJ_PTR(host),
                         mtlk_sta_incref, (host->sta)); /* Reference by HST */
  MTLK_START_FINALLY(host_entry, MTLK_OBJ_PTR(host))
  MTLK_START_RETURN(host_entry, MTLK_OBJ_PTR(host), _mtlk_hst_stop, (host))
}

static __INLINE void
_mtlk_hst_update (host_entry *host, sta_entry *sta)
{

  ILOG3_YD("Update HOST %Y - %lu", _mtlk_hst_get_addr(host), host->timestamp);

  if (!_mtlk_is_hst_behind_sta(host, sta)){

    if (host->sta){
      /* Host was associated previously with another STA */
      ILOG2_YY("HOST %Y is stopped with STA %Y on UPDATE", _mtlk_hst_get_addr(host)->au8Addr, host->sta_addr.au8Addr);
      _mtlk_hst_stop(host);
    }

    ILOG2_YY("HOST %Y is started with STA %Y on UPDATE", _mtlk_hst_get_addr(host)->au8Addr, mtlk_sta_get_addr(sta)->au8Addr);
    _mtlk_hst_start(host, sta);
  }
  else{
    host->timestamp = mtlk_osal_timestamp();
  }

}
/******************************************************************************************/

/******************************************************************************************
 * HST DB API
 ******************************************************************************************/
#define MTLK_HSTDB_HASH_NOF_BUCKETS 16

static void
_mtlk_hstdb_recalc_default_host_unsafe (hst_db *hstdb)
{
  mtlk_hash_enum_t              e;
  MTLK_HASH_ENTRY_T(ieee_addr) *h;
  host_entry                   *newest_hst = NULL;
  uint32                        smallest_diff = (uint32)-1;

  h = mtlk_hash_enum_first_ieee_addr(&hstdb->hash, &e);
  while (h)
  {
    host_entry *hst  = MTLK_CONTAINER_OF(h, host_entry, hentry);
    uint32      diff = mtlk_osal_timestamp_diff(mtlk_osal_timestamp(), hst->timestamp);

    if (hst->sta != NULL){
      if (diff < smallest_diff) {
        smallest_diff = diff;
        newest_hst    = hst;
      }
    }

    h = mtlk_hash_enum_next_ieee_addr(&hstdb->hash, &e);
  }

  if (newest_hst == NULL) {
    ieee_addr_zero(&hstdb->default_host);
  }
  else {
    hstdb->default_host = *_mtlk_hst_get_addr(newest_hst);
  }

  ILOG3_Y("default_host %Y", hstdb->default_host.au8Addr);
}

static void
_mtlk_hstdb_add_host (hst_db* hstdb, const unsigned char *mac,
                      sta_entry *sta)
{
  int         res = MTLK_ERR_UNKNOWN;
  host_entry *hst = NULL;
  MTLK_HASH_ENTRY_T(ieee_addr) *h;

  hst = mtlk_osal_mem_alloc(sizeof(*hst), MTLK_MEM_TAG_STADB_ITER);
  if (!hst) {
    ELOG_V("Can't allocate host!");
    res = MTLK_ERR_NO_MEM;
    goto err_alloc;
  }

  res = _mtlk_hst_init(hst, hstdb);
  if (res != MTLK_ERR_OK) {
    ELOG_D("Can't start Host (err#%d)", res);
    goto err_init;
  }

  ILOG2_YY("HOST %Y started with STA %Y on CREATE", ((IEEE_ADDR *)mac)->au8Addr, mtlk_sta_get_addr(sta)->au8Addr);
  res = _mtlk_hst_start(hst, sta);
  if (res != MTLK_ERR_OK) {
    ELOG_D("Can't start Host (err#%d)", res);
    goto err_start;
  }

  mtlk_osal_lock_acquire(&hstdb->lock);
  h = mtlk_hash_insert_ieee_addr(&hstdb->hash, (IEEE_ADDR *)mac, &hst->hentry);
  if (h == NULL) {
    ++hstdb->hash_cnt;
  }
  mtlk_osal_lock_release(&hstdb->lock);

  if (h != NULL) {
    res = MTLK_ERR_PROHIB;
    WLOG_V("Already registered");
    goto err_insert;
  }

  mtlk_hstdb_update_default_host(hstdb, mac);

  ILOG2_YPY("HOST %Y added (%p), belongs to STA %Y", mac, hst, hst->sta_addr.au8Addr);

  return;

err_insert:
  _mtlk_hst_stop(hst);
err_start:
  _mtlk_hst_cleanup(hst);
err_init:
  mtlk_osal_mem_free(hst);
err_alloc:
  return;
}

void __MTLK_IFUNC
mtlk_hstdb_update_default_host (hst_db* hstdb, const unsigned char *mac)
{
  if (!ieee_addr_is_valid(&hstdb->default_host) ||
       0 == ieee_addr_compare(&hstdb->default_host, &hstdb->local_mac)) {
    ieee_addr_set(&hstdb->default_host, mac);
    ILOG3_Y("default_host %Y", hstdb->default_host.au8Addr);
  }
}

void __MTLK_IFUNC
mtlk_hstdb_update_host (hst_db* hstdb, const unsigned char *mac, sta_entry *sta)
{
  MTLK_HASH_ENTRY_T(ieee_addr) *h;

  mtlk_osal_lock_acquire(&hstdb->lock);
  h = mtlk_hash_find_ieee_addr(&hstdb->hash, (IEEE_ADDR *)mac);
  mtlk_osal_lock_release(&hstdb->lock);
  if (h) {
    host_entry *host = MTLK_CONTAINER_OF(h, host_entry, hentry);
    /* Station already associated with HOST - just update timestamp */
    _mtlk_hst_update(host, sta);
  }
  else {

    ILOG2_V("Can't find peer (HOST), adding...");
    _mtlk_hstdb_add_host(hstdb, mac, sta);
  }
}

static void
_mtlk_hstdb_remove_host_unsafe (hst_db *hstdb, host_entry *host, const IEEE_ADDR *mac)
{
  MTLK_ASSERT(NULL != hstdb);
  MTLK_ASSERT(NULL != host);
  MTLK_ASSERT(NULL != mac);

  if (host->sta){
    ILOG2_YY("HOST %Y stopped on STA %Y on REMOVE", _mtlk_hst_get_addr(host)->au8Addr, host->sta_addr.au8Addr);
    _mtlk_hst_stop(host);
  }

  _mtlk_hst_cleanup(host);
  mtlk_osal_mem_free(host);

  ILOG2_Y("HOST %Y removed", mac->au8Addr);
}

/* Function should be used with HSTDB lock acquired by caller */
static void
_mtlk_hstdb_hash_remove_ieee_addr_internal(hst_db *hstdb, host_entry *host)
{
  MTLK_ASSERT(NULL != hstdb);
  MTLK_ASSERT(NULL != host);

  --hstdb->hash_cnt;
  mtlk_hash_remove_ieee_addr(&hstdb->hash, &host->hentry);
}

int __MTLK_IFUNC
mtlk_hstdb_remove_all_by_sta (hst_db *hstdb, const sta_entry *sta)
{
  /* For L2NAT only! not applicable for WDS */

  mtlk_hash_enum_t              e;
  MTLK_HASH_ENTRY_T(ieee_addr) *h;
  BOOL                          recalc_def_host = FALSE;

  mtlk_osal_lock_acquire(&hstdb->lock);
  h = mtlk_hash_enum_first_ieee_addr(&hstdb->hash, &e);
  while (h)
  {
    host_entry *hst  = MTLK_CONTAINER_OF(h, host_entry, hentry);
    if (_mtlk_is_hst_behind_sta(hst, sta)) {
      /* WARNING: We store the MAC address here since it may
      *           become corrupted after mtlk_hash_remove_hstdb()
      */
      const IEEE_ADDR mac = *_mtlk_hst_get_addr(hst);

      /* Call "internal" functions to remove host, they don't use locks */
      _mtlk_hstdb_hash_remove_ieee_addr_internal(hstdb, hst);
      _mtlk_hstdb_remove_host_unsafe(hstdb, hst, &mac);

      /* are we deleting default host? */
      if (FALSE == recalc_def_host && 0 == ieee_addr_compare(&hstdb->default_host, &mac)) {
        recalc_def_host = TRUE;
      }

    }
    h = mtlk_hash_enum_next_ieee_addr(&hstdb->hash, &e);
  }

  if (recalc_def_host) {
    _mtlk_hstdb_recalc_default_host_unsafe(hstdb);
  }
  mtlk_osal_lock_release(&hstdb->lock);

  return MTLK_ERR_OK;
}

void __MTLK_IFUNC
mtlk_hstdb_remove_host_by_addr (hst_db *hstdb, const IEEE_ADDR *mac)
{
  MTLK_HASH_ENTRY_T(ieee_addr) *h;

  mtlk_osal_lock_acquire(&hstdb->lock);
  h = mtlk_hash_find_ieee_addr(&hstdb->hash, mac);
  if (h) {
    host_entry *host = MTLK_CONTAINER_OF(h, host_entry, hentry);
    _mtlk_hstdb_hash_remove_ieee_addr_internal(hstdb, host);
    mtlk_osal_lock_release(&hstdb->lock);

    /* The host isn't in the DB anymore, so nobody can access it =>
     * we can remove it with no locking
     */
    _mtlk_hstdb_remove_host_unsafe(hstdb, host, mac);
  }
  else {
    mtlk_osal_lock_release(&hstdb->lock);
  }
}

static int
_mtlk_hstdb_cleanup_all_hst_unsafe (hst_db *hstdb)
{
  /* Remove all existing entries from DB. Called on slow context cleanup only.
     All entries must be already stopped at this point */

  mtlk_hash_enum_t              e;
  MTLK_HASH_ENTRY_T(ieee_addr) *h;

  h = mtlk_hash_enum_first_ieee_addr(&hstdb->hash, &e);
  while (h)
  {
    host_entry *hst  = MTLK_CONTAINER_OF(h, host_entry, hentry);
    MTLK_ASSERT(hst->sta == NULL);
    ILOG2_Y("WDS HOST Clean up on DESTROY %Y ", _mtlk_hst_get_addr(hst)->au8Addr);
    _mtlk_hstdb_hash_remove_ieee_addr_internal(hstdb, hst);
    _mtlk_hst_cleanup(hst);
    mtlk_osal_mem_free(hst);

    h = mtlk_hash_enum_next_ieee_addr(&hstdb->hash, &e);
  }

  return MTLK_ERR_OK;
}

int __MTLK_IFUNC
mtlk_hstdb_stop_all_by_sta (hst_db *hstdb, const sta_entry *sta)
{
  /* For WDS only! not applicable for L2NAT */

  mtlk_hash_enum_t              e;
  MTLK_HASH_ENTRY_T(ieee_addr) *h;

  mtlk_osal_lock_acquire(&hstdb->lock);
  h = mtlk_hash_enum_first_ieee_addr(&hstdb->hash, &e);
  while (h)
  {
    host_entry *hst  = MTLK_CONTAINER_OF(h, host_entry, hentry);
    if (_mtlk_is_hst_behind_sta(hst, sta)){
      ILOG2_YY("HOST %Y on STA %Y is stopped", _mtlk_hst_get_addr(hst)->au8Addr, mtlk_sta_get_addr(sta)->au8Addr);
      _mtlk_hst_stop(hst);
    }

    h = mtlk_hash_enum_next_ieee_addr(&hstdb->hash, &e);

  }

  mtlk_osal_lock_release(&hstdb->lock);

  return MTLK_ERR_OK;
}

int __MTLK_IFUNC
mtlk_hstdb_start_all_by_sta (hst_db *hstdb, sta_entry *sta)
{
  /* For WDS only! not applicable for L2NAT.
     Associate existing HOST entries with just connected STA
     if previous STA address is the same */

  mtlk_hash_enum_t              e;
  MTLK_HASH_ENTRY_T(ieee_addr) *h;
  const IEEE_ADDR              *sta_addr;

  sta_addr = mtlk_sta_get_addr(sta);
  mtlk_osal_lock_acquire(&hstdb->lock);
  h = mtlk_hash_enum_first_ieee_addr(&hstdb->hash, &e);
  while (h)
  {
    host_entry *hst  = MTLK_CONTAINER_OF(h, host_entry, hentry);

    if (0 == ieee_addr_compare(&hst->sta_addr, sta_addr)){
      ILOG2_YY("HOST Entry reassociated %Y to STA %Y on CONNECT", _mtlk_hst_get_addr(hst)->au8Addr, hst->sta_addr.au8Addr);
      _mtlk_hst_start(hst, sta);
    }

    h = mtlk_hash_enum_next_ieee_addr(&hstdb->hash, &e);

  }
  mtlk_osal_lock_release(&hstdb->lock);

  return MTLK_ERR_OK;
}

int __MTLK_IFUNC
mtlk_hstdb_dcdp_remove_all_by_sta(hst_db *hstdb, const sta_entry *sta)
{
#if MTLK_USE_DIRECTCONNECT_DP_API
  mtlk_hstdb_iterator_t  hiter;
  const IEEE_ADDR       *host_addr = NULL;

  ILOG3_Y("Remove all HOST addresses from switch table on STA %Y", mtlk_sta_get_addr(sta)->au8Addr);

  memset(&hiter, 0, sizeof(hiter));
  host_addr = mtlk_hstdb_iterate_first(hstdb, sta, &hiter);
  if (host_addr) {
    do {
      /* Remove host MAC addr from switch MAC table */
      mtlk_df_user_dcdp_remove_mac_addr(mtlk_vap_get_df(hstdb->vap_handle), host_addr->au8Addr);
      host_addr = mtlk_hstdb_iterate_next(&hiter);
    } while (host_addr);
    mtlk_hstdb_iterate_done(&hiter);
  }
#endif
  return MTLK_ERR_OK;
}

mtlk_vap_handle_t __MTLK_IFUNC
mtlk_host_get_vap_handle (mtlk_handle_t host_handle)
{
  host_entry  *host;

  MTLK_ASSERT(host_handle);

  host = HANDLE_T_PTR(host_entry, host_handle);
  return (host->paramdb->vap_handle);
}


MTLK_INIT_STEPS_LIST_BEGIN(hst_db)
  MTLK_INIT_STEPS_LIST_ENTRY(hst_db, HSTDB_HASH)
  MTLK_INIT_STEPS_LIST_ENTRY(hst_db, HSTDB_LOCK)
  MTLK_INIT_STEPS_LIST_ENTRY(hst_db, REG_ABILITIES)
  MTLK_INIT_STEPS_LIST_ENTRY(hst_db, EN_ABILITIES)
MTLK_INIT_INNER_STEPS_BEGIN(hst_db)
MTLK_INIT_STEPS_LIST_END(hst_db);

static const mtlk_ability_id_t _hstdb_abilities[] = {
  WAVE_CORE_REQ_GET_HSTDB_CFG,
  WAVE_CORE_REQ_SET_HSTDB_CFG
};

int __MTLK_IFUNC
mtlk_hstdb_init (hst_db *hstdb, mtlk_vap_handle_t vap_handle)
{
  hstdb->wds_host_timeout = 0;

  hstdb->vap_handle = vap_handle;

  MTLK_INIT_TRY(hst_db, MTLK_OBJ_PTR(hstdb))
    MTLK_INIT_STEP(hst_db, HSTDB_HASH, MTLK_OBJ_PTR(hstdb),
                   mtlk_hash_init_ieee_addr, (&hstdb->hash, MTLK_HSTDB_HASH_NOF_BUCKETS));
    MTLK_INIT_STEP(hst_db, HSTDB_LOCK, MTLK_OBJ_PTR(hstdb),
                   mtlk_osal_lock_init, (&hstdb->lock));
    MTLK_INIT_STEP(hst_db, REG_ABILITIES, MTLK_OBJ_PTR(hstdb),
                   mtlk_abmgr_register_ability_set,
                   (mtlk_vap_get_abmgr(hstdb->vap_handle), _hstdb_abilities, ARRAY_SIZE(_hstdb_abilities)));
    MTLK_INIT_STEP_VOID(hst_db, EN_ABILITIES, MTLK_OBJ_PTR(hstdb),
                        mtlk_abmgr_enable_ability_set,
                        (mtlk_vap_get_abmgr(hstdb->vap_handle), _hstdb_abilities, ARRAY_SIZE(_hstdb_abilities)));
  MTLK_INIT_FINALLY(hst_db, MTLK_OBJ_PTR(hstdb))
  MTLK_INIT_RETURN(hst_db, MTLK_OBJ_PTR(hstdb), mtlk_hstdb_cleanup, (hstdb))
}

void __MTLK_IFUNC
mtlk_hstdb_cleanup (hst_db *hstdb)
{

  _mtlk_hstdb_cleanup_all_hst_unsafe(hstdb);

  MTLK_CLEANUP_BEGIN(hst_db, MTLK_OBJ_PTR(hstdb))
    MTLK_CLEANUP_STEP(hst_db, EN_ABILITIES, MTLK_OBJ_PTR(hstdb),
                       mtlk_abmgr_disable_ability_set,
                       (mtlk_vap_get_abmgr(hstdb->vap_handle), _hstdb_abilities, ARRAY_SIZE(_hstdb_abilities)));
    MTLK_CLEANUP_STEP(hst_db, REG_ABILITIES, MTLK_OBJ_PTR(hstdb),
                       mtlk_abmgr_unregister_ability_set,
                       (mtlk_vap_get_abmgr(hstdb->vap_handle), _hstdb_abilities, ARRAY_SIZE(_hstdb_abilities)));
    MTLK_CLEANUP_STEP(hst_db, HSTDB_LOCK, MTLK_OBJ_PTR(hstdb),
                      mtlk_osal_lock_cleanup, (&hstdb->lock));
    MTLK_CLEANUP_STEP(hst_db, HSTDB_HASH, MTLK_OBJ_PTR(hstdb),
                      mtlk_hash_cleanup_ieee_addr, (&hstdb->hash));
  MTLK_CLEANUP_END(hst_db, MTLK_OBJ_PTR(hstdb))
}

sta_entry * __MTLK_IFUNC
mtlk_hstdb_find_sta (hst_db* hstdb, const unsigned char *mac)
{
  sta_entry                    *sta = NULL;
  MTLK_HASH_ENTRY_T(ieee_addr) *h;

  mtlk_osal_lock_acquire(&hstdb->lock);
  h = mtlk_hash_find_ieee_addr(&hstdb->hash, (IEEE_ADDR *)mac);
  if (h) {
    host_entry *host = MTLK_CONTAINER_OF(h, host_entry, hentry);

    if (host->sta){
      ILOG3_YPP("Found host %Y, STA %p (%p)",
        _mtlk_hst_get_addr(host), host->sta, host);
      sta = host->sta;
      mtlk_sta_incref(sta); /* Reference by caller */
    }
  }
  mtlk_osal_lock_release(&hstdb->lock);

  return sta;
}

int __MTLK_IFUNC mtlk_hstdb_set_local_mac (hst_db              *hstdb,
                                           const unsigned char *mac)
{
  int res = MTLK_ERR_OK;

  if (mtlk_osal_is_valid_ether_addr(mac))
    ieee_addr_set(&hstdb->local_mac, mac);
  else
    res = MTLK_ERR_PARAMS;
  return res;
}

void __MTLK_IFUNC mtlk_hstdb_get_local_mac (hst_db        *hstdb,
                                            unsigned char *mac)
{
  ieee_addr_get(mac, &hstdb->local_mac);
}

const IEEE_ADDR * __MTLK_IFUNC
mtlk_hstdb_iterate_first (hst_db *hstdb, const sta_entry *sta, mtlk_hstdb_iterator_t *iter)
{
  int                           err  = MTLK_ERR_OK;
  const IEEE_ADDR              *addr = NULL;
  uint32                        idx  = 0;
  mtlk_hash_enum_t              e;
  MTLK_HASH_ENTRY_T(ieee_addr) *h;

  MTLK_ASSERT(hstdb != NULL);
  MTLK_ASSERT(sta != NULL);
  MTLK_ASSERT(iter != NULL);

  mtlk_osal_lock_acquire(&hstdb->lock);

  iter->size = hstdb->hash_cnt;

  if (!iter->size) {
    err = MTLK_ERR_NOT_IN_USE;
    goto end;
  }

  iter->addr = (IEEE_ADDR *)mtlk_osal_mem_alloc(iter->size * sizeof(IEEE_ADDR), MTLK_MEM_TAG_STADB_ITER);
  if (!iter->addr) {
    ELOG_D("Can't allocate iteration array of %d entries", iter->size);
    err = MTLK_ERR_NO_MEM;
    goto end;
  }

  memset(iter->addr, 0, iter->size * sizeof(IEEE_ADDR));

  h = mtlk_hash_enum_first_ieee_addr(&hstdb->hash, &e);
  while (idx < iter->size && h) {
    const host_entry *host = MTLK_CONTAINER_OF(h, host_entry, hentry);
    if (_mtlk_is_hst_behind_sta(host, sta)) {
      iter->addr[idx] = *_mtlk_hst_get_addr(host);
      ++idx;
    }
    h = mtlk_hash_enum_next_ieee_addr(&hstdb->hash, &e);
  }

  err = MTLK_ERR_OK;

end:
  mtlk_osal_lock_release(&hstdb->lock);

  if (err == MTLK_ERR_OK) {
    iter->idx   = 0;

    addr = mtlk_hstdb_iterate_next(iter);
    if (!addr) {
      mtlk_osal_mem_free(iter->addr);
    }
  }

  return addr;
}

const IEEE_ADDR * __MTLK_IFUNC
mtlk_hstdb_iterate_next (mtlk_hstdb_iterator_t *iter)
{
  const IEEE_ADDR *addr = NULL;

  MTLK_ASSERT(iter != NULL);
  MTLK_ASSERT(iter->addr != NULL);

  if (iter->idx < iter->size &&
      !ieee_addr_is_zero(&iter->addr[iter->idx])) {
    addr = &iter->addr[iter->idx];
    ++iter->idx;
  }

  return addr;
}

void __MTLK_IFUNC
mtlk_hstdb_iterate_done (mtlk_hstdb_iterator_t *iter)
{
  MTLK_ASSERT(iter != NULL);
  MTLK_ASSERT(iter->addr != NULL);

  mtlk_osal_mem_free(iter->addr);
  memset(iter, 0, sizeof(*iter));
}
/******************************************************************************************/

int __MTLK_IFUNC
mtlk_stadb_addba_status(sta_db *stadb, mtlk_clpb_t *clpb)
{
  int res = MTLK_ERR_OK;
  MTLK_UNREFERENCED_PARAM(stadb);
  MTLK_UNREFERENCED_PARAM(clpb);

  /* function to be removed */

  return res;
}

