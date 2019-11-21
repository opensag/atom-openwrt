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

#ifndef _MTLK_STADB_H_
#define _MTLK_STADB_H_

#include "mtlklist.h"
#include "mtlkqos.h"
#include "mtlkhash_ieee_addr.h"
#include "mtlk_clipboard.h"
#include "mtlkirbd.h"
#include "mtlk_wss.h"
#include "mtlk_analyzer.h"
#include "mtlkwssa_drvinfo.h"
#include "bitrate.h"
#include "eeprom.h"
#include "mtlkhal.h"
#include <net/cfg80211.h>
#include "Statistics/Wave600B/Statistics_Descriptors.h"
#include "Statistics/Wave600/Statistics_Descriptors.h"
#include "wave_hal_stats.h"

#define   MTLK_IDEFS_ON
#include "mtlkidefs.h"

#define LOG_LOCAL_GID   GID_STADB
#define LOG_LOCAL_FID   0

/* Timeout change adaptation interval */
#define DEFAULT_KEEPALIVE_TIMEOUT   1000 /* ms */
#define HOST_INACTIVE_TIMEOUT       1800 /* sec */

#define DEFAULT_BEACON_INTERVAL (100)

#define WV_SUPP_RATES_MAX MAX_NUM_SUPPORTED_RATES
#define WV_HT_MCS_MASK_LEN 10 /* must match IEEE80211_HT_MCS_MASK_LEN from include/linux/ieee80211.h */
#define WV_VHT_MCS_INFO_NUM_FIELDS 4


#define STA_FLAGS_WDS                     MTLK_BFIELD_INFO(0, 1)
#define STA_FLAGS_WMM                     MTLK_BFIELD_INFO(1, 1)
#define STA_FLAGS_MFP                     MTLK_BFIELD_INFO(2, 1)
#define STA_FLAGS_11n                     MTLK_BFIELD_INFO(3, 1)
#define STA_FLAGS_IS_8021X_FILTER_OPEN    MTLK_BFIELD_INFO(4, 1)
#define STA_FLAGS_11ac                    MTLK_BFIELD_INFO(5, 1)
#define STA_FLAGS_PBAC                    MTLK_BFIELD_INFO(6, 1)
#define STA_FLAGS_OPMODE_NOTIF            MTLK_BFIELD_INFO(7, 1)
#define STA_FLAGS_WDS_WPA                 MTLK_BFIELD_INFO(8, 1)
#define STA_FLAGS_OMN_SUPPORTED           MTLK_BFIELD_INFO(9, 1)
#define STA_FLAGS_11ax                    MTLK_BFIELD_INFO(15, 1)

#define BSS_COEX_IE_40_MHZ_INTOLERANT               MTLK_BFIELD_INFO(1, 1)
#define BSS_COEX_IE_20_MHZ_BSS_WIDTH_REQUEST        MTLK_BFIELD_INFO(2, 1)
#define BSS_COEX_IE_OBSS_SCANNING_EXEMPTION_REQUEST MTLK_BFIELD_INFO(3, 1)
#define BSS_COEX_IE_OBSS_SCANNING_EXEMPTION_GRANT   MTLK_BFIELD_INFO(4, 1)

#define MIN_RSSI                         (-128)
#define MIN_NOISE                        (-128)

#define RX_HE_MCS_MAP_LESS_OR_EQUAL_80_OFFSET 0
#define TX_HE_MCS_MAP_LESS_OR_EQUAL_80_OFFSET 2
#define RX_HE_MCS_MAP_160_OFFSET              4
#define TX_HE_MCS_MAP_160_OFFSET              6
#define RX_HE_MCS_MAP_80_PLUS_80_OFFSET       8
#define TX_HE_MCS_MAP_80_PLUS_80_OFFSET       10

#define MAX_HE_MCS_1_SS                   MTLK_BFIELD_INFO(0, 2)
#define MAX_HE_MCS_2_SS                   MTLK_BFIELD_INFO(2, 2)
#define MAX_HE_MCS_3_SS                   MTLK_BFIELD_INFO(4, 2)
#define MAX_HE_MCS_4_SS                   MTLK_BFIELD_INFO(6, 2)
#define MAX_HE_MCS_5_SS                   MTLK_BFIELD_INFO(8, 2)
#define MAX_HE_MCS_6_SS                   MTLK_BFIELD_INFO(10, 2)
#define MAX_HE_MCS_7_SS                   MTLK_BFIELD_INFO(12, 2)
#define MAX_HE_MCS_8_SS                   MTLK_BFIELD_INFO(14, 2)

#define HE_MCS_N_SS(he_mcs_map, n)                \
  MTLK_BFIELD_GET(he_mcs_map, MAX_HE_MCS_##n_SS)

typedef enum
{
/* minimal statistic */

  MTLK_STAI_CNT_DISCARD_PACKETS_RECEIVED,

#if (!(MTLK_MTIDL_PEER_STAT_FULL))
  MTLK_STAI_CNT_LAST,
#endif /* MTLK_MTIDL_PEER_STAT_FULL */

  MTLK_STAI_CNT_RX_PACKETS_DISCARDED_DRV_TOO_OLD,
  MTLK_STAI_CNT_RX_PACKETS_DISCARDED_DRV_DUPLICATE,
  MTLK_STAI_CNT_TX_PACKETS_DISCARDED_DRV_NO_RESOURCES,
  MTLK_STAI_CNT_TX_PACKETS_DISCARDED_SQ_OVERFLOW,
  MTLK_STAI_CNT_TX_PACKETS_DISCARDED_EAPOL_FILTER,
  MTLK_STAI_CNT_TX_PACKETS_DISCARDED_DROP_ALL_FILTER,
  MTLK_STAI_CNT_TX_PACKETS_DISCARDED_TX_QUEUE_OVERFLOW,

  MTLK_STAI_CNT_802_1X_PACKETS_RECEIVED,                /* number of 802_1x packets received */
  MTLK_STAI_CNT_802_1X_PACKETS_SENT,                    /* number of 802_1x packets transmitted */
  MTLK_STAI_CNT_802_1X_PACKETS_DISCARDED,               /* number of 802_1x packets discarded */

  MTLK_STAI_CNT_FWD_RX_PACKETS,
  MTLK_STAI_CNT_FWD_RX_BYTES,
  MTLK_STAI_CNT_PS_MODE_ENTRANCES,
  MTLK_STAI_CNT_TX_PACKETS_DISCARDED_ACM,
  MTLK_STAI_CNT_TX_PACKETS_DISCARDED_EAPOL_CLONED,

/* DEBUG statistic */
#if MTLK_MTIDL_PEER_STAT_FULL
  MTLK_STAI_CNT_LAST
#endif /* MTLK_MTIDL_PEER_STAT_FULL */

} sta_info_cnt_id_e;

/* Statistic ALLOWED flags */
#define MTLK_STAI_CNT_DISCARD_PACKETS_RECEIVED_ALLOWED                  MTLK_WWSS_WLAN_STAT_ID_DISCARD_PACKETS_RECEIVED_ALLOWED

#define MTLK_STAI_CNT_RX_PACKETS_DISCARDED_DRV_TOO_OLD_ALLOWED          MTLK_WWSS_WLAN_STAT_ID_RX_PACKETS_DISCARDED_DRV_TOO_OLD_ALLOWED
#define MTLK_STAI_CNT_RX_PACKETS_DISCARDED_DRV_DUPLICATE_ALLOWED        MTLK_WWSS_WLAN_STAT_ID_RX_PACKETS_DISCARDED_DRV_DUPLICATE_ALLOWED
#define MTLK_STAI_CNT_TX_PACKETS_DISCARDED_DRV_NO_RESOURCES_ALLOWED     MTLK_WWSS_WLAN_STAT_ID_TX_PACKETS_DISCARDED_DRV_NO_RESOURCES_ALLOWED
#define MTLK_STAI_CNT_TX_PACKETS_DISCARDED_SQ_OVERFLOW_ALLOWED          MTLK_WWSS_WLAN_STAT_ID_TX_PACKETS_DISCARDED_DRV_SQ_OVERFLOW_ALLOWED
#define MTLK_STAI_CNT_TX_PACKETS_DISCARDED_EAPOL_FILTER_ALLOWED         MTLK_WWSS_WLAN_STAT_ID_TX_PACKETS_DISCARDED_DRV_EAPOL_FILTER_ALLOWED
#define MTLK_STAI_CNT_TX_PACKETS_DISCARDED_DROP_ALL_FILTER_ALLOWED      MTLK_WWSS_WLAN_STAT_ID_TX_PACKETS_DISCARDED_DRV_DROP_ALL_FILTER_ALLOWED
#define MTLK_STAI_CNT_TX_PACKETS_DISCARDED_TX_QUEUE_OVERFLOW_ALLOWED    MTLK_WWSS_WLAN_STAT_ID_TX_PACKETS_DISCARDED_DRV_TX_QUEUE_OVERFLOW_ALLOWED
#define MTLK_STAI_CNT_802_1X_PACKETS_RECEIVED_ALLOWED                   MTLK_WWSS_WLAN_STAT_ID_802_1X_PACKETS_RECEIVED_ALLOWED
#define MTLK_STAI_CNT_802_1X_PACKETS_SENT_ALLOWED                       MTLK_WWSS_WLAN_STAT_ID_802_1X_PACKETS_SENT_ALLOWED
#define MTLK_STAI_CNT_802_1X_PACKETS_DISCARDED_ALLOWED                  MTLK_WWSS_WLAN_STAT_ID_802_1X_PACKETS_DISCARDED_ALLOWED
#define MTLK_STAI_CNT_FWD_RX_PACKETS_ALLOWED                            MTLK_WWSS_WLAN_STAT_ID_FWD_RX_PACKETS_ALLOWED
#define MTLK_STAI_CNT_FWD_RX_BYTES_ALLOWED                              MTLK_WWSS_WLAN_STAT_ID_FWD_RX_BYTES_ALLOWED
#define MTLK_STAI_CNT_PS_MODE_ENTRANCES_ALLOWED                         MTLK_WWSS_WLAN_STAT_ID_PS_MODE_ENTRANCES_ALLOWED
#define MTLK_STAI_CNT_TX_PACKETS_DISCARDED_ACM_ALLOWED                  MTLK_WWSS_WLAN_STAT_ID_TX_PACKETS_DISCARDED_DRV_ACM_ALLOWED
#define MTLK_STAI_CNT_TX_PACKETS_DISCARDED_EAPOL_CLONED_ALLOWED         MTLK_WWSS_WLAN_STAT_ID_TX_PACKETS_DISCARDED_DRV_EAPOL_CLONED_ALLOWED

typedef enum {
  MTLK_PCKT_FLTR_ALLOW_ALL,
  MTLK_PCKT_FLTR_ALLOW_802_1X,
  MTLK_PCKT_FLTR_DISCARD_ALL
} mtlk_pckt_filter_e;

#ifndef MTLK_LEGACY_STATISTICS
typedef struct _rssiSnapshot_t
{
  unsigned char    rssi[4];      /* Last 4 RSSI frames received */
  unsigned char    time_s[4];    /* Time of when last 4 RSSI were received */
  unsigned short   count;        /* Sequence numer of received managemant (bcn, ack) frames */
} rssiSnapshot_t;
typedef struct devicePhyRxStatusDb
{
  unsigned char    noise[PHY_STATISTICS_MAX_RX_ANT];
  unsigned char    rf_gain[PHY_STATISTICS_MAX_RX_ANT];
  unsigned long    irad;
  unsigned long    tsf;
  unsigned char    channel_load;
  unsigned char    totalChannelUtilization;    /* Total Channel Utilization is an indication of how congested the medium is (all APs) */
  char             chNon80211Noise;
  char             CWIvalue;
  unsigned long    extStaRx;
  signed short int txPower;
  unsigned char    channelNum;
  unsigned char    reserved;

}devicePhyRxStatusDb_t;

typedef struct stationPhyRxStatusDb
{
  char             rssi[PHY_STATISTICS_MAX_RX_ANT];
  unsigned long    phyRate;    /* 17 bits [20:4] */
  unsigned long    irad;
  unsigned long    lastTsf;
  unsigned long    perClientRxtimeUsage;
  uint8            noise[PHY_STATISTICS_MAX_RX_ANT];
  uint8            gain[PHY_STATISTICS_MAX_RX_ANT];
  rssiSnapshot_t   rssiAckAvarage; /* AP_MODE: RSSI from last 4 ack received */
  unsigned char    rssiArray[PHY_STATISTICS_MAX_RX_ANT][NUM_OF_EXTENTION_RSSI];   /* <! 4=antennas, 4=20+20+40+80 extension rssi */

  /* Per antenna RSSI (above noise floor) for all widths (primary,secondary)
  -----------------------------------------------
  | chain_num |  20MHz [pri20                   ]
  |           |  40MHZ [pri20,sec20             ]
  |           |  80MHz [pri20,sec20,sec40,      ]
  |           | 160MHz [pri20,sec20,sec40,sec80 ]
  -----------------------------------------------
  |  1        |  rssi  [pri20,sec20,sec40,sec80 ]
  |  ...      |  ...
  |  8        |  rssi  [pri20,sec20,sec40,sec80 ]
  ----------------------------------------------- */
  unsigned char    reserved[2];

  /* Last possible word num is 14 (total of 15 words that can be configured to pac Extrap for 4 bits parameter */
}stationPhyRxStatusDb_t;

typedef struct wholePhyRxStatusDb
{
  devicePhyRxStatusDb_t     devicePhyRxStatus[GEN6_NUM_OF_BANDS];
  stationPhyRxStatusDb_t     staPhyRxStatus[HW_NUM_OF_STATIONS];
}wholePhyRxStatusDb_t;
#endif

#ifdef MTLK_LEGACY_STATISTICS
typedef enum {
    STAT_STA_TX_RATE_CBW = 0,
    STAT_STA_TX_RATE_MODE,
    STAT_STA_TX_RATE_SCP,
    STAT_STA_TX_RATE_MCS,
    STAT_STA_TX_RATE_NSS,
    STAT_STA_TX_BF_MODE,
    STAT_STA_TX_STBC_MODE,
    STAT_STA_TX_POWER_DATA,
    STAT_STA_TX_POWER_MGMT,
    STAT_STA_TX_PHYMODE_MGMT,
    STAT_STA_TX_RATE_TOTAL
} mtlk_mhi_stat_sta_tx_rate_e;

typedef struct _mtlk_mhi_stats_sta_tx_rate {
    uint8       stat[STAT_STA_TX_RATE_TOTAL];
} mtlk_mhi_stats_sta_tx_rate_t;

#else /* MTLK_LEGACY_STATISTICS */

typedef struct mtlk_mhi_stats_sta_tx_rate{
    uint8 dataBwLimit;
    uint8 DataPhyMode;
    uint8 scpData;
    uint8 mcsData;
    uint8 nssData;
    uint8 bfModeData;
    uint8 stbcModeData;
    uint8 powerData;
    uint8 powerManagement;
    uint8 ManagementPhyMode;
}mtlk_mhi_stats_sta_tx_rate_t;

#endif /* MTLK_LEGACY_STATISTICS */

#ifdef MTLK_LEGACY_STATISTICS
typedef enum {
    STAT_STA_CNTR_MPDU_FIRST_RETRANSMISSION = 0,
    STAT_STA_CNTR_TX_FRAMES,
    STAT_STA_CNTR_TX_BYTES,
    STAT_STA_CNTR_RX_FRAMES,
    STAT_STA_CNTR_RX_BYTES,
    STAT_STA_CNTR_RX_SW_UPDATE_DROP,
    STAT_STA_CNTR_PCT_RETRY,
    STAT_STA_CNTR_PCT_RETRY_EXHAUSTED,
    STAT_STA_CNTR_TOTAL
} mtlk_mhi_stat_sta_cntr_e;

typedef uint32      mhi_sta_cntr_t;
#define MHI_STATS_STA_CNTR_TO_HOST(x)  (MAC_TO_HOST32(x))

typedef struct _mtlk_mhi_stats_sta_cntr {
    mhi_sta_cntr_t      stat[STAT_STA_CNTR_TOTAL];
} mtlk_mhi_stats_sta_cntr_t;

#else /* MTLK_LEGACY_STATISTICS */

typedef struct mtlk_mhi_sta_stats{
    uint32 mpduFirstRetransmission;
    uint32 mpduTransmitted;
    uint32 mpduByteTransmitted;
    uint32 rdCount;
    uint32 rxOutStaNumOfBytes;
    uint32 swUpdateDrop;
    uint32 mpduRetransmission;
    uint32 exhaustedCount;
    uint32 successCount;
    uint32 rdDuplicateDrop;
    uint32 missingSn;
    uint32 mpduInAmpdu;
    uint32 ampdu;
    uint32 mpduRetryCount;
    uint32 transmittedAmpdu;
    uint32 tx_errors;
}mtlk_mhi_stats_sta_cntr_t;

typedef struct mtlk_mhi_sta_stats64{
    uint64 mpduFirstRetransmission;
    uint64 mpduTransmitted;
    uint64 mpduByteTransmitted;
    uint64 rdCount;
    uint64 rxOutStaNumOfBytes;
    uint64 swUpdateDrop;
    uint64 mpduRetransmission;
    uint64 exhaustedCount;
    uint64 successCount;
    uint64 rdDuplicateDrop;
    uint64 missingSn;
    uint64 mpduInAmpdu;
    uint64 ampdu;
    uint64 mpduRetryCount;
    uint64 transmittedAmpdu;
    uint64 tx_errors;
}mtlk_mhi_stats64_sta_cntr_t;

#endif /* MTLK_LEGACY_STATISTICS */

typedef struct airtime_stats
{
  uint32           tx_time;
  uint32           rx_time;
  uint32           airtime_efficiency;
  unsigned long    update_time;
  uint8            airtime_usage;
} airtime_stats_t;

typedef struct _sta_stats
{
  mtlk_mhi_stats_sta_tx_rate_t  mhi_tx_stats; /* Table of TX statistics */

  airtime_stats_t               airtime_stats;

                                /* TX: Downlink - from AP to STA */
  mtlk_bitrate_params_t tx_data_rate_params; /* always rate parameters */

                                /* RX: Uplink - from STA to AP */
  mtlk_bitrate_info_t   rx_data_rate_info; /* phy_rate OR rate_info*/
  mtlk_bitrate_info_t   rx_mgmt_rate_info; /* phy_rate OR rate_info */
  int8                  max_rssi;
  int8                  data_rssi[MAX_NUM_RX_ANTENNAS];
  int8                  mgmt_max_rssi;
  int8                  mgmt_rssi[MAX_NUM_RX_ANTENNAS];
  int8                  snr[MAX_NUM_RX_ANTENNAS];
} __MTLK_IDATA sta_stats;

typedef struct _sta_info
{
  uint16              sid;
  uint16              aid;
  uint16              flags;
  uint8               uapsd_queues;
  uint8               max_sp;
  uint8               rates[WV_SUPP_RATES_MAX];
  uint16              ht_cap_info;
  uint8               bss_coex_20_40;
  uint8               ampdu_param;
  uint8               rx_mcs_bitmask[WV_HT_MCS_MASK_LEN];
  uint8               vendor;
  uint8               cipher;   /* TKIP or CCMP */
  uint8               supported_rates_len;
  uint32              listen_interval;
  IEEE_ADDR           addr;
  sta_stats           stats;
  mtlk_pckt_filter_e  filter;      /* flag to filter packets */
  mtlk_peer_analyzer_t sta_analyzer;
  uint32              vht_cap_info;
  uint16              vht_mcs_info[WV_VHT_MCS_INFO_NUM_FIELDS];
  uint32              sid_order;
  int32               rssi_dbm;
  uint32              tx_bf_cap_info;
  uint8               opmode_notif;
  uint8               sta_net_modes;
  uint8               WDS_client_type;
  struct ieee80211_sta_he_cap   he_cap;
  struct ieee80211_he_operation he_operation_parameters;
} __MTLK_IDATA sta_info;

struct _sta_db; /* TODO: replace it with Param DB/callbacks with opaque pointers */

static __INLINE uint32
_mtlk_hash_sid_hashval (const u16 *key, uint32 nof_buckets)
{
  return (*key & (nof_buckets - 1));
}

static __INLINE int
_mtlk_hash_sid_keycmp (const u16 *key1, const u16 *key2)
{
  return (*key1 != *key2);
}

MTLK_HASH_DECLARE_ENTRY_T(sid, u16);

MTLK_HASH_DECLARE_INLINE(sid, u16);

MTLK_HASH_DEFINE_INLINE(sid, u16,
                        _mtlk_hash_sid_hashval,
                        _mtlk_hash_sid_keycmp);

struct _sta_entry {
  MTLK_HASH_ENTRY_T(ieee_addr) hentry;
  MTLK_HASH_ENTRY_T(sid)       hentry_sid;
  mtlk_atomic_t                ref_cnt;
  sta_info                     info;
  mtlk_osal_spinlock_t         lock;
  mtlk_osal_timestamp_t        activity_timestamp;
  mtlk_osal_timestamp_t        connection_timestamp;
  mtlk_osal_timer_t            keepalive_timer;      // Timer for polling station
  uint8                        peer_ap;              // Is peer AP?
  /* Description of AP */
  uint16                       beacon_interval;      // AP's beacon interval
  mtlk_osal_timestamp_t        beacon_timestamp;     // AP's last beacon timestamp
  mtlk_vap_handle_t            vap_handle;
  struct _sta_db              *paramdb;
  mtlk_irbd_t                 *irbd;
#ifdef MTLK_LEGACY_STATISTICS
  mtlk_irbd_handle_t          *stat_irb_handle;
  mtlk_mhi_stats_sta_cntr_t   mhi_stat_cntrs; /* Table of statistic counters */
#else
  mtlk_mhi_stats_sta_cntr_t   sta_stats_cntrs; /* Table of statistic counters */
  mtlk_mhi_stats64_sta_cntr_t   sta_stats64_cntrs; /* Table of statistic counters */
#endif
  mtlk_irbd_t                 *irbd_flags;
  mtlk_wss_t                  *wss;
  mtlk_wss_cntr_handle_t      *wss_hcntrs[MTLK_STAI_CNT_LAST];
  MTLK_DECLARE_INIT_STATUS;
  MTLK_DECLARE_START_STATUS;
  MTLK_DECLARE_START_LOOP(ROD_QUEs);
} __MTLK_IDATA;


static __INLINE int
mtlk_stadb_apply_rssi_offset(int phy_rssi, unsigned rssi_offs)
{
    int rssi = phy_rssi - rssi_offs;
    return MAX(MIN_RSSI, rssi);
}

static __INLINE
int8 mtlk_calculate_snr (int8 rssi, int8 noise)
{
  int snr;

  /* Unavailable noise */
  if (!noise)
    return 0;

  snr = rssi - noise;
  if (snr < (int8)MIN_INT8)
    return MIN_INT8;
  else if (snr > MAX_INT8)
    return MAX_INT8;

  return snr;
}

#ifdef MTLK_LEGACY_STATISTICS
static __INLINE mhi_sta_cntr_t *
mtlk_sta_get_mhi_stat_array(const sta_entry  *sta)
{
    return (mhi_sta_cntr_t *)&sta->mhi_stat_cntrs;
}

static __INLINE mhi_sta_cntr_t
mtlk_sta_get_mhi_stat_cntr(const sta_entry  *sta, mtlk_mhi_stat_sta_cntr_e id)
{
    MTLK_ASSERT(sta);
    MTLK_ASSERT(id < STAT_STA_CNTR_TOTAL);

    if (id < STAT_STA_CNTR_TOTAL) {
        mhi_sta_cntr_t  *cntrs = mtlk_sta_get_mhi_stat_array(sta);
        return cntrs[id];
    } else {
        return 0;
    }
}

static __INLINE mhi_sta_cntr_t
mtlk_sta_get_stat_cntr_tx_frames(const sta_entry  *sta)
{
    return mtlk_sta_get_mhi_stat_cntr(sta, STAT_STA_CNTR_TX_FRAMES);
}

static __INLINE mhi_sta_cntr_t
mtlk_sta_get_stat_cntr_rx_frames(const sta_entry  *sta)
{
    return mtlk_sta_get_mhi_stat_cntr(sta, STAT_STA_CNTR_RX_FRAMES);
}

static __INLINE mhi_sta_cntr_t
mtlk_sta_get_stat_cntr_tx_bytes(const sta_entry  *sta)
{
    return mtlk_sta_get_mhi_stat_cntr(sta, STAT_STA_CNTR_TX_BYTES);
}

static __INLINE mhi_sta_cntr_t
mtlk_sta_get_stat_cntr_rx_bytes(const sta_entry  *sta)
{
    return mtlk_sta_get_mhi_stat_cntr(sta, STAT_STA_CNTR_RX_BYTES);
}

static __INLINE mhi_sta_cntr_t
mtlk_sta_get_stat_cntr_pct_retry (const sta_entry  *sta)
{
  return mtlk_sta_get_mhi_stat_cntr(sta, STAT_STA_CNTR_PCT_RETRY);
}

static __INLINE mhi_sta_cntr_t
mtlk_sta_get_stat_cntr_pct_retry_exhausted (const sta_entry  *sta)
{
  return mtlk_sta_get_mhi_stat_cntr(sta, STAT_STA_CNTR_PCT_RETRY_EXHAUSTED);
}
#else /* MTLK_LEGACY_STATISTICS */

static __INLINE mtlk_mhi_stats64_sta_cntr_t *
mtlk_sta_get_mhi_stat64_array (const sta_entry  *sta)
{
  MTLK_ASSERT(sta);
  return (mtlk_mhi_stats64_sta_cntr_t *)&sta->sta_stats64_cntrs;
}

static __INLINE mtlk_mhi_stats_sta_cntr_t *
mtlk_sta_get_mhi_stat_array (const sta_entry  *sta)
{
  MTLK_ASSERT(sta);
  return (mtlk_mhi_stats_sta_cntr_t *)&sta->sta_stats_cntrs;
}

static __INLINE uint32
mtlk_sta_get_stat_cntr_tx_frames (const sta_entry  *sta)
{
  MTLK_ASSERT(sta);
  return sta->sta_stats_cntrs.mpduTransmitted;
}

static __INLINE uint32
mtlk_sta_get_stat_cntr_rx_frames (const sta_entry  *sta)
{
  MTLK_ASSERT(sta);
  return sta->sta_stats_cntrs.rdCount;
}

static __INLINE uint32
mtlk_sta_get_stat_cntr_tx_bytes (const sta_entry  *sta)
{
  MTLK_ASSERT(sta);
  return sta->sta_stats_cntrs.mpduByteTransmitted;
}

static __INLINE uint32
mtlk_sta_get_stat_cntr_rx_bytes (const sta_entry  *sta)
{
  MTLK_ASSERT(sta);
  return sta->sta_stats_cntrs.rxOutStaNumOfBytes;
}

static __INLINE uint32
mtlk_sta_get_stat_cntr_pct_retry (const sta_entry  *sta)
{
  MTLK_ASSERT(sta);
  return sta->sta_stats_cntrs.mpduRetransmission;
}

static __INLINE uint32
mtlk_sta_get_stat_cntr_pct_retry_exhausted (const sta_entry  *sta)
{
  MTLK_ASSERT(sta);
  return sta->sta_stats_cntrs.exhaustedCount;
}
#endif /* MTLK_LEGACY_STATISTICS */

/* MHI STA TX statistic */

static __INLINE mtlk_mhi_stats_sta_tx_rate_t *
mtlk_sta_get_mhi_tx_stats (const sta_entry *sta)
{
  return &((sta_entry *)sta)->info.stats.mhi_tx_stats; /* discard const qualifief */
}

#ifdef MTLK_LEGACY_STATISTICS
static __INLINE uint8
mtlk_sta_get_mhi_tx_stat_value (const sta_entry *sta, mtlk_mhi_stat_sta_tx_rate_e id)
{
    MTLK_ASSERT(sta);
    MTLK_ASSERT(id < STAT_STA_TX_RATE_TOTAL);

    if (id < STAT_STA_TX_RATE_TOTAL) {
        mtlk_mhi_stats_sta_tx_rate_t *tx_stats = mtlk_sta_get_mhi_tx_stats(sta);
        return (tx_stats->stat[id]);
    } else {
        return 0;
    }
}

static __INLINE uint8
mtlk_sta_get_mhi_tx_stat_bf_mode (const sta_entry  *sta)
{
    return mtlk_sta_get_mhi_tx_stat_value(sta, STAT_STA_TX_BF_MODE);
}

static __INLINE uint8
mtlk_sta_get_mhi_tx_stat_stbc_mode (const sta_entry  *sta)
{
    return mtlk_sta_get_mhi_tx_stat_value(sta, STAT_STA_TX_STBC_MODE);
}

static __INLINE uint8
mtlk_sta_get_mhi_tx_stat_power_data (const sta_entry  *sta)
{
    return mtlk_sta_get_mhi_tx_stat_value(sta, STAT_STA_TX_POWER_DATA);
}

static __INLINE uint8
mtlk_sta_get_mhi_tx_stat_power_mgmt (const sta_entry  *sta)
{
    return mtlk_sta_get_mhi_tx_stat_value(sta, STAT_STA_TX_POWER_MGMT);
}

static __INLINE uint8
mtlk_sta_get_mhi_tx_stat_phymode_mgmt (const sta_entry  *sta)
{
    return mtlk_sta_get_mhi_tx_stat_value(sta, STAT_STA_TX_PHYMODE_MGMT);
}
#else /* MTLK_LEGACY_STATISTICS */

static __INLINE uint8
mtlk_sta_get_mhi_tx_stat_bf_mode (const sta_entry  *sta)
{
  MTLK_ASSERT(sta);
  return ((sta_entry *)sta)->info.stats.mhi_tx_stats.bfModeData;
}

static __INLINE uint8
mtlk_sta_get_mhi_tx_stat_stbc_mode (const sta_entry  *sta)
{
  MTLK_ASSERT(sta);
  return ((sta_entry *)sta)->info.stats.mhi_tx_stats.stbcModeData;
}

static __INLINE uint8
mtlk_sta_get_mhi_tx_stat_power_data (const sta_entry  *sta)
{
  MTLK_ASSERT(sta);
  return ((sta_entry *)sta)->info.stats.mhi_tx_stats.powerData;
}

static __INLINE uint8
mtlk_sta_get_mhi_tx_stat_power_mgmt (const sta_entry  *sta)
{
  MTLK_ASSERT(sta);
  return ((sta_entry *)sta)->info.stats.mhi_tx_stats.powerManagement;
}

static __INLINE uint8
mtlk_sta_get_mhi_tx_stat_phymode_mgmt (const sta_entry  *sta)
{
  MTLK_ASSERT(sta);
  return ((sta_entry *)sta)->info.stats.mhi_tx_stats.ManagementPhyMode;
}
#endif /* MTLK_LEGACY_STATISTICS */

/* with checking ALLOWED option */
#define mtlk_sta_get_cnt(sta, id)         (TRUE == id##_ALLOWED) ? __mtlk_sta_get_cnt(sta, id) : 0

static __INLINE uint32
__mtlk_sta_get_cnt (const sta_entry  *sta,
                  sta_info_cnt_id_e cnt_id)
{
  MTLK_ASSERT(cnt_id >= 0 && cnt_id < MTLK_STAI_CNT_LAST);

  return mtlk_wss_get_stat(sta->wss, cnt_id);
}

#if MTLK_WWSS_WLAN_STAT_ANALYZER_TX_RATE_ALLOWED
static __INLINE uint32
mtlk_sta_get_short_term_tx(const sta_entry  *sta)
{
  return mtlk_peer_analyzer_get_short_term_tx(&sta->info.sta_analyzer);
}

static __INLINE uint32
mtlk_sta_get_long_term_tx(const sta_entry  *sta)
{
  return mtlk_peer_analyzer_get_long_term_tx(&sta->info.sta_analyzer);
}
#endif /* MTLK_WWSS_WLAN_STAT_ANALYZER_TX_RATE_ALLOWED */

#if MTLK_WWSS_WLAN_STAT_ANALYZER_RX_RATE_ALLOWED
static __INLINE uint32
mtlk_sta_get_short_term_rx(const sta_entry  *sta)
{
  return mtlk_peer_analyzer_get_short_term_rx(&sta->info.sta_analyzer);
}

static __INLINE uint32
mtlk_sta_get_long_term_rx(const sta_entry  *sta)
{
  return mtlk_peer_analyzer_get_long_term_rx(&sta->info.sta_analyzer);
}
#endif /* MTLK_WWSS_WLAN_STAT_ANALYZER_RX_RATE_ALLOWED */

static __INLINE uint8
mtlk_sta_get_cipher (const sta_entry *sta)
{
  return sta->info.cipher;
}

static __INLINE mtlk_pckt_filter_e
mtlk_sta_get_packets_filter (const sta_entry *sta)
{
  return sta->info.filter;
}

static __INLINE BOOL
mtlk_sta_is_dot11n (const sta_entry *sta)
{
  return MTLK_BFIELD_GET(sta->info.flags, STA_FLAGS_11n);
}

static __INLINE BOOL
mtlk_sta_is_mfp (const sta_entry *sta)
{
  return MTLK_BFIELD_GET(sta->info.flags, STA_FLAGS_MFP);
}

static __INLINE BOOL
mtlk_sta_is_wds (const sta_entry *sta)
{
  return MTLK_BFIELD_GET(sta->info.flags, STA_FLAGS_WDS);
}

static __INLINE BOOL
mtlk_sta_info_is_4addr (const sta_info *info)
{
  return FOUR_ADDRESSES_STATION == info->WDS_client_type ||
         WPA_WDS == info->WDS_client_type;
}


static __INLINE BOOL
mtlk_sta_is_4addr (const sta_entry *sta)
{
  return mtlk_sta_info_is_4addr(&sta->info);
}


/* static __INLINE uint8 */
/* mtlk_sta_get_net_mode (const sta_entry *sta) */
/* { */
/*   return sta->info.net_mode; */
/* } */

/* Get time passed since station connected in seconds */
static __INLINE uint32
mtlk_sta_get_age (const sta_entry *sta)
{
  mtlk_osal_timestamp_diff_t diff = mtlk_osal_timestamp_diff(mtlk_osal_timestamp(), sta->connection_timestamp);
  return mtlk_osal_timestamp_to_ms(diff) / 1000; /* Converted from milliseconds to seconds */
}

/* Get inactive passed since last activity in milliseconds */
static __INLINE uint32
mtlk_sta_get_inactive_time (const sta_entry *sta)
{
  mtlk_osal_timestamp_diff_t diff = mtlk_osal_timestamp_diff(mtlk_osal_timestamp(), sta->activity_timestamp);
  return mtlk_osal_timestamp_to_ms(diff);
}

static __INLINE void
mtlk_sta_get_rssi (const sta_entry *sta, int8 *rssi, uint32 size)
{
  MTLK_ASSERT(rssi != NULL);
  MTLK_ASSERT(size == ARRAY_SIZE(sta->info.stats.data_rssi));

  wave_memcpy(rssi, size, sta->info.stats.data_rssi, ARRAY_SIZE(sta->info.stats.data_rssi));
}

static __INLINE int8
mtlk_sta_get_max_rssi (const sta_entry *sta)
{
  return sta->info.stats.max_rssi;
}

static __INLINE int8
mtlk_sta_get_mgmt_max_rssi (const sta_entry *sta)
{
  return sta->info.stats.mgmt_max_rssi;
}

static __INLINE const IEEE_ADDR *
mtlk_sta_get_addr (const sta_entry *sta)
{
  return MTLK_HASH_VALUE_GET_KEY(sta, hentry);
}

static __INLINE uint16
mtlk_sta_get_sid (const sta_entry *sta)
{
  return sta->info.sid;
}

static __INLINE uint16
mtlk_sta_get_aid (const sta_entry *sta)
{
  return sta->info.aid;
}

void __MTLK_IFUNC
mtlk_sta_get_peer_stats(const sta_entry* sta, mtlk_wssa_drv_peer_stats_t* stats);

void __MTLK_IFUNC
mtlk_sta_get_peer_capabilities(const sta_entry* sta, mtlk_wssa_drv_peer_capabilities_t* capabilities);

/********************************************************
 * WARNING: __mtlk_sta_on_unref_private is private API! *
 *          No one is allowed to use it except the      *
 *          mtlk_sta_decref.                            *
 ********************************************************/
void __MTLK_IFUNC
__mtlk_sta_on_unref_private(sta_entry *sta);
/********************************************************/

//#define STA_REF_DBG

#ifndef STA_REF_DBG
static __INLINE void
mtlk_sta_incref (sta_entry  *sta)
{
  mtlk_osal_atomic_inc(&sta->ref_cnt);
}

static __INLINE void
mtlk_sta_decref (sta_entry *sta)
{
  uint32 ref_cnt = mtlk_osal_atomic_dec(&sta->ref_cnt);

  if (ref_cnt == 0) {
    __mtlk_sta_on_unref_private(sta);
  }
}
#else
#define mtlk_sta_incref(sta) __mtlk_sta_incref_dbg(__FUNCTION__, __LINE__, (sta))

static __INLINE void
__mtlk_sta_incref_dbg (const char *f, int l, sta_entry *sta)
{
  uint32 ref_cnt = mtlk_osal_atomic_inc(&sta->ref_cnt);

  ILOG0_YSDD("+++ %Y in %s on %d . ref = %d",
            mtlk_sta_get_addr(sta),
            f,
            l,
            ref_cnt);
}

#define mtlk_sta_decref(sta) __mtlk_sta_decref_dbg(__FUNCTION__, __LINE__, (sta))

static __INLINE void
__mtlk_sta_decref_dbg (const char *f, int l, sta_entry *sta)
{
  uint32 ref_cnt = mtlk_osal_atomic_dec(&sta->ref_cnt);

  ILOG0_YSDD("-- %Y in %s on %d . ref = %d",
            mtlk_sta_get_addr(sta),
            f,
            l,
            ref_cnt);

  if (ref_cnt == 0) {
    __mtlk_sta_on_unref_private(sta);
  }
}
#endif /* STA_REF_DBG */

void __MTLK_IFUNC
mtlk_sta_on_packet_sent(sta_entry *sta, uint32 nbuf_len, uint32 nbuf_flags) __MTLK_INT_HANDLER_SECTION;

void __MTLK_IFUNC
mtlk_sta_on_man_frame_arrived(sta_entry *sta, const int8 rssi[]);

/* with DEBUG statistic */
#if MTLK_MTIDL_PEER_STAT_FULL
typedef enum
{
  MTLK_TX_DISCARDED_DRV_NO_RESOURCES  = MTLK_STAI_CNT_TX_PACKETS_DISCARDED_DRV_NO_RESOURCES,
  MTLK_TX_DISCARDED_SQ_OVERFLOW       = MTLK_STAI_CNT_TX_PACKETS_DISCARDED_SQ_OVERFLOW,
  MTLK_TX_DISCARDED_EAPOL_FILTER      = MTLK_STAI_CNT_TX_PACKETS_DISCARDED_EAPOL_FILTER,
  MTLK_TX_DISCARDED_DROP_ALL_FILTER   = MTLK_STAI_CNT_TX_PACKETS_DISCARDED_DROP_ALL_FILTER,
  MTLK_TX_DISCARDED_TX_QUEUE_OVERFLOW = MTLK_STAI_CNT_TX_PACKETS_DISCARDED_TX_QUEUE_OVERFLOW,
  MTLK_TX_DISCARDED_DRV_ACM           = MTLK_STAI_CNT_TX_PACKETS_DISCARDED_ACM,
  MTLK_TX_DISCARDED_EAPOL_CLONED      = MTLK_STAI_CNT_TX_PACKETS_DISCARDED_EAPOL_CLONED
} mtlk_tx_drop_reasons_e;

#define MTLK_TX_DISCARDED_DRV_NO_RESOURCES_ALLOWED  MTLK_STAI_CNT_TX_PACKETS_DISCARDED_DRV_NO_RESOURCES_ALLOWED
#define MTLK_TX_DISCARDED_SQ_OVERFLOW_ALLOWED       MTLK_STAI_CNT_TX_PACKETS_DISCARDED_SQ_OVERFLOW_ALLOWED
#define MTLK_TX_DISCARDED_EAPOL_FILTER_ALLOWED      MTLK_STAI_CNT_TX_PACKETS_DISCARDED_EAPOL_FILTER_ALLOWED
#define MTLK_TX_DISCARDED_DROP_ALL_FILTER_ALLOWED   MTLK_STAI_CNT_TX_PACKETS_DISCARDED_DROP_ALL_FILTER_ALLOWED
#define MTLK_TX_DISCARDED_TX_QUEUE_OVERFLOW_ALLOWED MTLK_STAI_CNT_TX_PACKETS_DISCARDED_TX_QUEUE_OVERFLOW_ALLOWED
#define MTLK_TX_DISCARDED_DRV_ACM_ALLOWED           MTLK_STAI_CNT_TX_PACKETS_DISCARDED_ACM_ALLOWED
#define MTLK_TX_DISCARDED_EAPOL_CLONED_ALLOWED      MTLK_STAI_CNT_TX_PACKETS_DISCARDED_EAPOL_CLONED_ALLOWED

void __MTLK_IFUNC
mtlk_sta_on_packet_dropped(sta_entry *sta, mtlk_tx_drop_reasons_e reason);

#else /* MTLK_MTIDL_PEER_STAT_FULL */

#define mtlk_sta_on_packet_dropped(sta, id)     { /* empty */ }

#endif /* MTLK_MTIDL_PEER_STAT_FULL */

void __MTLK_IFUNC
mtlk_sta_on_packet_indicated(sta_entry *sta, mtlk_nbuf_t *nbuf, uint32 nbuf_flags) __MTLK_INT_HANDLER_SECTION;

void __MTLK_IFUNC
mtlk_sta_on_rx_packet_802_1x(sta_entry *sta);

void __MTLK_IFUNC
mtlk_sta_on_tx_packet_802_1x(sta_entry *sta);

void __MTLK_IFUNC
mtlk_sta_on_tx_packet_discarded_802_1x(sta_entry *sta);

void __MTLK_IFUNC
mtlk_sta_on_rx_packet_forwarded(sta_entry *sta, mtlk_nbuf_t *nbuf) __MTLK_INT_HANDLER_SECTION;

int __MTLK_IFUNC
mtlk_sta_update_beacon_interval(sta_entry *sta, uint16 beacon_interval);

void __MTLK_IFUNC
mtlk_sta_set_pm_enabled(sta_entry *sta, BOOL enabled);

void __MTLK_IFUNC
mtlk_sta_update_phy_info (sta_entry *sta, mtlk_hw_t *hw, stationPhyRxStatusDb_t *sta_status, BOOL is_gen6);

void __MTLK_IFUNC
mtlk_sta_update_rx_rate_rssi_on_man_frame (sta_entry *sta, const mtlk_phy_info_t *phy_info);

static __INLINE void
mtlk_sta_set_cipher (sta_entry *sta,
                     uint8      cipher)
{
  sta->info.cipher = cipher;
}

void __MTLK_IFUNC
mtlk_sta_set_packets_filter(sta_entry         *sta,
                            mtlk_pckt_filter_e filter_type);

struct nic;

typedef struct
{
  mtlk_handle_t   usr_data;
  void            (__MTLK_IFUNC *on_sta_keepalive)(mtlk_handle_t usr_data, sta_entry *sta);
} __MTLK_IDATA sta_db_wrap_api_t;

typedef struct _sta_db_cfg_t
{
  sta_db_wrap_api_t api;
  uint32            max_nof_stas;
  mtlk_wss_t       *parent_wss;
} __MTLK_IDATA sta_db_cfg_t;

typedef struct _sta_db
{
  mtlk_osal_timer_t    iter_addba_timer;
  mtlk_hash_t          hash;
  mtlk_hash_t          sid_hash;
  mtlk_osal_spinlock_t lock;
  uint32               hash_cnt;
  uint32               sid_hash_cnt;
  uint32               sid_order;
  uint32               keepalive_interval;
  sta_db_cfg_t         cfg;
  mtlk_vap_handle_t    vap_handle;
  uint32               flags;
  mtlk_atomic_t        four_addr_sta_cnt;
  mtlk_wss_t          *wss;
  MTLK_DECLARE_INIT_STATUS;
  MTLK_DECLARE_START_STATUS;
} __MTLK_IDATA sta_db;

typedef struct _global_stadb_t
{
  mtlk_atomic_t sta_cnt;
} __MTLK_IDATA global_stadb_t;


static __INLINE int
mtlk_stadb_get_sta_cnt (sta_db *sta_db)
{
  return sta_db->hash_cnt;
}

static __INLINE int
mtlk_stadb_get_sid_order (sta_db *sta_db)
{
  return sta_db->sid_order;
}

int __MTLK_IFUNC
mtlk_stadb_init(sta_db *stadb, mtlk_vap_handle_t vap_handle);

void __MTLK_IFUNC
mtlk_stadb_configure(sta_db *stadb, const sta_db_cfg_t *cfg);

int __MTLK_IFUNC
mtlk_stadb_start(sta_db *stadb);

void __MTLK_IFUNC
mtlk_stadb_stop(sta_db *stadb);

void __MTLK_IFUNC
mtlk_stadb_cleanup(sta_db *stadb);

sta_entry * __MTLK_IFUNC
mtlk_stadb_add_sta(sta_db *stadb, const unsigned char *mac, sta_info *info_cfg);

void __MTLK_IFUNC
mtlk_stadb_remove_sta(sta_db *stadb, sta_entry *sta);

void __MTLK_IFUNC
mtlk_stadb_set_sta_auth_flag_in_irbd(sta_entry *sta);

static __INLINE sta_entry *
mtlk_stadb_find_sid (sta_db *stadb, u16 sid)
{
  sta_entry                    *sta = NULL;
  MTLK_HASH_ENTRY_T(sid)       *h;

  mtlk_osal_lock_acquire(&stadb->lock);
  h = mtlk_hash_find_sid(&stadb->sid_hash, (u16 *)&sid);
  if (h) {
    sta = MTLK_CONTAINER_OF(h, sta_entry, hentry_sid);
    mtlk_sta_incref(sta); /* Reference by caller */
  }
  mtlk_osal_lock_release(&stadb->lock);

  return sta;
}

#ifndef STA_REF_DBG
static __INLINE sta_entry *
mtlk_stadb_find_sta (sta_db *stadb, const unsigned char *mac)
{
  sta_entry                    *sta = NULL;
  MTLK_HASH_ENTRY_T(ieee_addr) *h;

  mtlk_osal_lock_acquire(&stadb->lock);
  h = mtlk_hash_find_ieee_addr(&stadb->hash, (IEEE_ADDR *)mac);
  if (h) {
    sta = MTLK_CONTAINER_OF(h, sta_entry, hentry);
    mtlk_sta_incref(sta); /* Reference by caller */
  }
  mtlk_osal_lock_release(&stadb->lock);

  return sta;
}
#else
#define mtlk_stadb_find_sta(stadb, mac) \
  __mtlk_stadb_find_sta_dbg(__FUNCTION__, __LINE__, (stadb), (mac))

static __INLINE sta_entry *
__mtlk_stadb_find_sta_dbg (const char *f, int l, sta_db *stadb, const unsigned char *mac)
{
  sta_entry                    *sta = NULL;
  MTLK_HASH_ENTRY_T(ieee_addr) *h;

  mtlk_osal_lock_acquire(&stadb->lock);
  h = mtlk_hash_find_ieee_addr(&stadb->hash, (IEEE_ADDR *)mac);
  if (h) {
    sta = MTLK_CONTAINER_OF(h, sta_entry, hentry);
    __mtlk_sta_incref_dbg(f, l, sta); /* Reference by caller */
  }
  mtlk_osal_lock_release(&stadb->lock);

  return sta;
}
#endif

static __INLINE sta_entry *
mtlk_stadb_get_ap (sta_db *stadb)
{
  sta_entry                    *sta = NULL;
  mtlk_hash_enum_t              e;
  MTLK_HASH_ENTRY_T(ieee_addr) *h;

  mtlk_osal_lock_acquire(&stadb->lock);
  h = mtlk_hash_enum_first_ieee_addr(&stadb->hash, &e);
  if (h) {
    sta = MTLK_CONTAINER_OF(h, sta_entry, hentry);
    mtlk_sta_incref(sta); /* Reference by caller */
  }
  mtlk_osal_lock_release(&stadb->lock);

  return sta;
}

static __INLINE uint32
mtlk_stadb_get_four_addr_sta_cnt (sta_db *stadb)
{
  return mtlk_osal_atomic_get(&stadb->four_addr_sta_cnt);
}

typedef struct
{
  sta_entry **arr;
  uint32      size;
  uint32      idx;
  sta_db     *stadb;
} __MTLK_IDATA mtlk_stadb_iterator_t;

struct _hst_db; /* TODO: replace it with Param DB/callbacks with opaque pointers */

typedef enum {
  STAT_ID_STADB = 1,
  STAT_ID_HSTDB
} __MTLK_IDATA mtlk_stadb_stat_id_e;

typedef struct {
  IEEE_ADDR addr;
  uint8     sta_sid;
  uint32    sta_rx_dropped;
  uint8     network_mode;
  uint16    tx_rate;
  uint32    aid;
  uint32    vap;
  BOOL      is_sta_auth;
  BOOL      is_four_addr;
  mtlk_wssa_drv_peer_stats_t  peer_stats;
  uint8     nss;
} __MTLK_IDATA mtlk_stadb_general_stat_t;

typedef struct {
  IEEE_ADDR addr;
} __MTLK_IDATA mtlk_hstdb_stat_t;

typedef struct {
  mtlk_stadb_stat_id_e    type;
  union {
    mtlk_stadb_general_stat_t general_stat;
    mtlk_hstdb_stat_t hstdb_stat;
  } u;
}__MTLK_IDATA mtlk_stadb_stat_t;

#ifndef MTLK_LEGACY_STATISTICS
int __MTLK_IFUNC
mtlk_stadb_get_peer_list(sta_db *stadb, mtlk_clpb_t *clpb);

void __MTLK_IFUNC
mtlk_sta_get_associated_dev_stats(const sta_entry* sta, peerFlowStats* peer_stats);
#endif
int __MTLK_IFUNC
mtlk_stadb_get_stat(sta_db *stadb, struct _hst_db *hstdb, mtlk_clpb_t *clpb, uint8 group_cipher);
int __MTLK_IFUNC
mtlk_stadb_get_sta_by_iter_id(sta_db *stadb, int iter_id, uint8 *mac, struct station_info* stinfo);

const sta_entry * __MTLK_IFUNC
mtlk_stadb_iterate_first(sta_db *stadb, mtlk_stadb_iterator_t *iter);
const sta_entry * __MTLK_IFUNC
mtlk_stadb_iterate_next(mtlk_stadb_iterator_t *iter);
void __MTLK_IFUNC
mtlk_stadb_iterate_done(mtlk_stadb_iterator_t *iter);

void __MTLK_IFUNC
mtlk_stadb_reset_cnts(sta_db *stadb);

typedef void (__MTLK_IFUNC * mtlk_stadb_disconnect_sta_clb_f)(mtlk_handle_t usr_ctx, sta_entry *sta);

void __MTLK_IFUNC
mtlk_stadb_disconnect_all(sta_db *stadb,
    mtlk_stadb_disconnect_sta_clb_f clb,
    mtlk_handle_t usr_ctx,
    BOOL wait_all_packets_confirmed);

BOOL __MTLK_IFUNC
mtlk_stadb_is_empty(sta_db *stadb);

uint32 __MTLK_IFUNC
mtlk_stadb_stas_num(sta_db *stadb);

sta_entry * __MTLK_IFUNC
mtlk_stadb_get_first_sta (sta_db *stadb);

BOOL __MTLK_IFUNC
mtlk_global_stadb_is_empty(void);

typedef struct _hst_db
{
  mtlk_hash_t          hash;
  mtlk_osal_spinlock_t lock;
  uint32               hash_cnt;
  uint32               wds_host_timeout;
  mtlk_vap_handle_t    vap_handle;
  /* default host related */
  IEEE_ADDR            default_host;
  IEEE_ADDR            local_mac;
  MTLK_DECLARE_INIT_STATUS;
} __MTLK_IDATA hst_db;

typedef struct _host_entry {
  MTLK_HASH_ENTRY_T(ieee_addr) hentry;
  mtlk_osal_timer_t            idle_timer;
  mtlk_osal_timestamp_t        timestamp;
  sta_entry                   *sta;
  IEEE_ADDR                    sta_addr;
  struct _hst_db              *paramdb;
  MTLK_DECLARE_INIT_STATUS;
  MTLK_DECLARE_START_STATUS;
} host_entry;

static __INLINE const IEEE_ADDR *
_mtlk_hst_get_addr (const host_entry *hst)
{
  return MTLK_HASH_VALUE_GET_KEY(hst, hentry);
}

int __MTLK_IFUNC
mtlk_hstdb_init(hst_db *hstdb, mtlk_vap_handle_t vap_handle);

void __MTLK_IFUNC
mtlk_hstdb_cleanup(hst_db *hstdb);

sta_entry * __MTLK_IFUNC
mtlk_hstdb_find_sta(hst_db* hstdb, const unsigned char *mac);

void __MTLK_IFUNC
mtlk_hstdb_update_host(hst_db *hstdb, const unsigned char *mac,
                       sta_entry *sta);

void __MTLK_IFUNC
mtlk_hstdb_remove_host_by_addr(hst_db *hstdb, const IEEE_ADDR *mac);

void __MTLK_IFUNC
mtlk_hstdb_update_default_host(hst_db* hstdb, const unsigned char *mac);

int __MTLK_IFUNC
mtlk_hstdb_remove_all_by_sta(hst_db *hstdb, const sta_entry *sta);

int __MTLK_IFUNC
mtlk_hstdb_stop_all_by_sta(hst_db *hstdb, const sta_entry *sta);

int __MTLK_IFUNC
mtlk_hstdb_start_all_by_sta(hst_db *hstdb, sta_entry *sta);

int __MTLK_IFUNC
mtlk_hstdb_dcdp_remove_all_by_sta(hst_db *hstdb, const sta_entry *sta);

mtlk_vap_handle_t __MTLK_IFUNC
mtlk_host_get_vap_handle(mtlk_handle_t host_handle);

static __INLINE IEEE_ADDR *
mtlk_hstdb_get_default_host (hst_db* hstdb)
{
  if (ieee_addr_is_valid(&hstdb->default_host))
    return &hstdb->default_host;
  return NULL;
}

int __MTLK_IFUNC mtlk_hstdb_set_local_mac(hst_db              *hstdb,
                                          const unsigned char *mac);
void __MTLK_IFUNC mtlk_hstdb_get_local_mac(hst_db        *hstdb,
                                           unsigned char *mac);


typedef struct
{
  IEEE_ADDR *addr;
  uint32     size;
  uint32     idx;
} __MTLK_IDATA mtlk_hstdb_iterator_t;

const IEEE_ADDR * __MTLK_IFUNC
mtlk_hstdb_iterate_first(hst_db *hstdb, const sta_entry *sta, mtlk_hstdb_iterator_t *iter);
const IEEE_ADDR * __MTLK_IFUNC
mtlk_hstdb_iterate_next(mtlk_hstdb_iterator_t *iter);
void __MTLK_IFUNC
mtlk_hstdb_iterate_done(mtlk_hstdb_iterator_t *iter);

int __MTLK_IFUNC
mtlk_stadb_addba_status(sta_db *stadb, mtlk_clpb_t *clpb);

void __MTLK_IFUNC
mtlk_sta_update_mhi_peers_stats(sta_entry *sta);

void
mtlk_sta_get_tr181_peer_stats(const sta_entry* sta, mtlk_wssa_drv_tr181_peer_stats_t *stats);

struct driver_sta_info;

void
mtlk_core_get_driver_sta_info (const sta_entry* sta, struct driver_sta_info *stats);

void
_mtlk_sta_get_peer_rates_info(const sta_entry *sta, mtlk_wssa_drv_peer_rates_info_t *rates_info);

static __INLINE void mtlk_stadb_stats_set_mgmt_rssi(sta_info  *info, int32 rssi)
{
  int i;

  info->stats.mgmt_max_rssi = MAX(rssi, MIN_RSSI);
  for (i = 0; i < ARRAY_SIZE(info->stats.mgmt_rssi); i++) {
    info->stats.mgmt_rssi[i] = info->stats.mgmt_max_rssi;
  }
}

/* Locates MSB that is not set */
static __INLINE int locate_ms0bit_16 (uint16 i_x)
{
  int i;
  for (i = 15; i >= 0; i--)
    if (MTLK_BIT_GET(i_x, i) == 0)
      return i;
  return -1;
}

#define   MTLK_IDEFS_OFF
#include "mtlkidefs.h"

#undef LOG_LOCAL_GID
#undef LOG_LOCAL_FID

#endif /* !_MTLK_STADB_H_ */
