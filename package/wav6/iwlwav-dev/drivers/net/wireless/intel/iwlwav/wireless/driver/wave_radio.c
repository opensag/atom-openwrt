/******************************************************************************

                               Copyright (c) 2016
                               Intel Corporation

  For licensing information, see the file 'LICENSE' in the root folder of
  this software module.

******************************************************************************/

/*
 * $Id$
 *
 *
 *
 * Radio module functionality
 *
 */

#include "mtlkinc.h"
#include <net/cfg80211.h>
#ifndef CPTCFG_IWLWAV_X86_HOST_PC
#include <../net/wireless/core.h>
#include <../net/wireless/nl80211.h>
#endif

#include "mtlk_coreui.h"
#include "mtlk_fast_mem.h"
#include "mtlk_mmb_drv.h"
#include "mtlkdfdefs.h"
#include "eeprom.h"
#include "mtlk_psdb.h"
#include "cfg80211.h"
#include "mac80211.h"
#include "hw_mmb.h"
#include "mtlkwlanirbdefs.h"
#include "intel_vendor_shared.h"
#include "mtlk_wssd.h"
#include "mtlk_wss_debug.h"
#include "mtlk_dfg.h"
#include "mtlk_df_user_priv.h"
#include "mtlk_df_priv.h"
#include "mtlk_df.h"
#include "mtlk_packets.h"
#include "mtlk_df_nbuf.h"
#include "mtlk_param_db.h"
#include "scan_support.h"
#include "wave_radio.h"
#include "mhi_umi.h"
#include "core_config.h"
#include "wave_80211ax.h"
#include "wave_hal_stats.h"

#define LOG_LOCAL_GID   GID_WAVE_RADIO
#define LOG_LOCAL_FID   0


#define STRING_PROGMODEL_SIZE   128

/* channels calibration definitions */

typedef enum {
  WAVE_RADIO_CHAN_G24_20,
  WAVE_RADIO_CHAN_G24_40,
  WAVE_RADIO_CHAN_G52_20,
  WAVE_RADIO_CHAN_G52_40,
  WAVE_RADIO_CHAN_G52_80,
  WAVE_RADIO_CHAN_G52_160
} wave_radio_chan_type_t;

#define G24_20_CH_NUM_MAX                           14
#define G24_40_CH_NUM_MAX                            9
#define G52_20_CH_NUM_MAX                           25
#define G52_40_CH_NUM_MAX       (G52_20_CH_NUM_MAX / 2)
#define G52_80_CH_NUM_MAX       (G52_40_CH_NUM_MAX / 2)
#define G52_160_CH_NUM_MAX  (G52_80_CH_NUM_MAX / 2 - 1)

typedef struct {
  uint8 num_of_chan;
  uint8 ch_width;
  uint8 channel[G24_20_CH_NUM_MAX];
} g24_20_ch_tab_t;

typedef struct {
  uint8 num_of_chan;
  uint8 ch_width;
  uint8 channel[G24_40_CH_NUM_MAX];
} g24_40_ch_tab_t;

typedef struct {
  uint8 num_of_chan;
  uint8 ch_width;
  uint8 channel[G52_20_CH_NUM_MAX];
} g52_20_ch_tab_t;

typedef struct {
  uint8 num_of_chan;
  uint8 ch_width;
  uint8 channel[G52_40_CH_NUM_MAX];
} g52_40_ch_tab_t;

typedef struct {
  uint8 num_of_chan;
  uint8 ch_width;
  uint8 channel[G52_80_CH_NUM_MAX];
} g52_80_ch_tab_t;

typedef struct {
  uint8 num_of_chan;
  uint8 ch_width;
  uint8 channel[G52_160_CH_NUM_MAX];
} g52_160_ch_tab_t;

static const uint8 g24_20_supported_ch[G24_20_CH_NUM_MAX] = {
  1,  2,  3,  4,  5,  6,  7,
  8,  9, 10, 11, 12, 13, 14
};

static const uint8 g24_40_supported_ch[G24_40_CH_NUM_MAX] = {
  1, 2, 3, 4, 5, 6, 7, 8, 9
};

static const uint8 g52_20_supported_ch[G52_20_CH_NUM_MAX] = {
   36,  40,  44,  48,  52,
   56,  60,  64, 100, 104,
  108, 112, 116, 120, 124,
  128, 132, 136, 140, 144,
  149, 153, 157, 161, 165
};

static const uint8 g52_40_supported_ch[G52_40_CH_NUM_MAX] = {
  36, 44, 52, 60, 100, 108, 116, 124, 132, 140, 149, 157
};

static const uint8 g52_80_supported_ch[G52_80_CH_NUM_MAX] = {
  36, 52, 100, 116, 132, 149
};

static const uint8 g52_160_supported_ch[G52_160_CH_NUM_MAX] = {
  36, 100
};

struct _wave_radio_radar_ctx_t {
  mtlk_osal_event_t radar_detect_end_evt; /* object for waiting of Radar Detection process end */
};

/**************************************************************
    WSS counters for HW Radio statistics
 */
static const uint32 _wave_radio_wss_id_map[] =
{
  MTLK_WWSS_WLAN_STAT_ID_BYTES_SENT_64,                  /* WAVE_RADIO_CNT_BYTES_SENT */
  MTLK_WWSS_WLAN_STAT_ID_BYTES_RECEIVED_64,              /* WAVE_RADIO_CNT_BYTES_RECEIVED */
  MTLK_WWSS_WLAN_STAT_ID_PACKETS_SENT_64,                /* WAVE_RADIO_CNT_PACKETS_SENT */
  MTLK_WWSS_WLAN_STAT_ID_PACKETS_RECEIVED_64,            /* WAVE_RADIO_CNT_PACKETS_RECEIVED */

  MTLK_WWSS_WLAN_STAT_ID_UNICAST_PACKETS_SENT,        /* WAVE_RADIO_CNT_UNICAST_PACKETS_SENT */
  MTLK_WWSS_WLAN_STAT_ID_MULTICAST_PACKETS_SENT,      /* WAVE_RADIO_CNT_MULTICAST_PACKETS_SENT */
  MTLK_WWSS_WLAN_STAT_ID_BROADCAST_PACKETS_SENT,      /* WAVE_RADIO_CNT_BROADCAST_PACKETS_SENT */
  MTLK_WWSS_WLAN_STAT_ID_UNICAST_BYTES_SENT,          /* WAVE_RADIO_CNT_UNICAST_BYTES_SENT */
  MTLK_WWSS_WLAN_STAT_ID_MULTICAST_BYTES_SENT,        /* WAVE_RADIO_CNT_MULTICAST_BYTES_SENT */
  MTLK_WWSS_WLAN_STAT_ID_BROADCAST_BYTES_SENT,        /* WAVE_RADIO_CNT_BROADCAST_BYTES_SENT */

  MTLK_WWSS_WLAN_STAT_ID_UNICAST_PACKETS_RECEIVED,    /* WAVE_RADIO_CNT_UNICAST_PACKETS_RECEIVED */
  MTLK_WWSS_WLAN_STAT_ID_MULTICAST_PACKETS_RECEIVED,  /* WAVE_RADIO_CNT_MULTICAST_PACKETS_RECEIVED */
  MTLK_WWSS_WLAN_STAT_ID_BROADCAST_PACKETS_RECEIVED,  /* WAVE_RADIO_CNT_BROADCAST_PACKETS_RECEIVED */
  MTLK_WWSS_WLAN_STAT_ID_UNICAST_BYTES_RECEIVED,      /* WAVE_RADIO_CNT_UNICAST_BYTES_RECEIVED */
  MTLK_WWSS_WLAN_STAT_ID_MULTICAST_BYTES_RECEIVED,    /* WAVE_RADIO_CNT_MULTICAST_BYTES_RECEIVED */
  MTLK_WWSS_WLAN_STAT_ID_BROADCAST_BYTES_RECEIVED,    /* WAVE_RADIO_CNT_BROADCAST_BYTES_RECEIVED */

  MTLK_WWSS_WLAN_STAT_ID_ERROR_PACKETS_SENT,          /* WAVE_RADIO_CNT_ERROR_PACKETS_SENT */
  MTLK_WWSS_WLAN_STAT_ID_ERROR_PACKETS_RECEIVED,      /* WAVE_RADIO_CNT_ERROR_PACKETS_RECEIVED */
  MTLK_WWSS_WLAN_STAT_ID_DISCARD_PACKETS_RECEIVED,    /* WAVE_RADIO_CNT_DISCARD_PACKETS_RECEIVED */

  MTLK_WWSS_WLAN_STAT_ID_RX_PACKETS_DISCARDED_DRV_TOO_OLD,  /* WAVE_RADIO_CNT_RX_PACKETS_DISCARDED_DRV_TOO_OLD */
  MTLK_WWSS_WLAN_STAT_ID_RX_PACKETS_DISCARDED_DRV_DUPLICATE,/* WAVE_RADIO_CNT_RX_PACKETS_DISCARDED_DRV_DUPLICATE */

  MTLK_WWSS_WLAN_STAT_ID_802_1X_PACKETS_RECEIVED,      /* WAVE_RADIO_CNT_802_1X_PACKETS_RECEIVED                                         */
  MTLK_WWSS_WLAN_STAT_ID_802_1X_PACKETS_SENT,          /* WAVE_RADIO_CNT_802_1X_PACKETS_SENT                                             */
  MTLK_WWSS_WLAN_STAT_ID_802_1X_PACKETS_DISCARDED,     /* WAVE_RADIO_CNT_802_1X_PACKETS_DISCARDED */
  MTLK_WWSS_WLAN_STAT_ID_PAIRWISE_MIC_FAILURE_PACKETS, /* WAVE_RADIO_CNT_PAIRWISE_MIC_FAILURE_PACKETS */
  MTLK_WWSS_WLAN_STAT_ID_GROUP_MIC_FAILURE_PACKETS,    /* WAVE_RADIO_CNT_GROUP_MIC_FAILURE_PACKETS */
  MTLK_WWSS_WLAN_STAT_ID_UNICAST_REPLAYED_PACKETS,     /* WAVE_RADIO_CNT_UNICAST_REPLAYED_PACKETS */
  MTLK_WWSS_WLAN_STAT_ID_MULTICAST_REPLAYED_PACKETS,   /* WAVE_RADIO_CNT_MULTICAST_REPLAYED_PACKETS */
  MTLK_WWSS_WLAN_STAT_ID_MANAGEMENT_REPLAYED_PACKETS,  /* WAVE_RADIO_CNT_MANAGEMENT_REPLAYED_PACKETS */
  MTLK_WWSS_WLAN_STAT_ID_FWD_RX_PACKETS,               /* WAVE_RADIO_CNT_FWD_RX_PACKETS */
  MTLK_WWSS_WLAN_STAT_ID_FWD_RX_BYTES,                 /* WAVE_RADIO_CNT_FWD_RX_BYTES */

  MTLK_WWSS_WLAN_STAT_ID_DAT_FRAMES_RECEIVED,          /* WAVE_RADIO_CNT_DAT_FRAMES_RECEIVED */
  MTLK_WWSS_WLAN_STAT_ID_CTL_FRAMES_RECEIVED,          /* WAVE_RADIO_CNT_CTL_FRAMES_RECEIVED */
  MTLK_WWSS_WLAN_STAT_ID_MAN_FRAMES_RES_QUEUE,         /* WAVE_RADIO_CNT_MAN_FRAMES_RES_QUEUE */
  MTLK_WWSS_WLAN_STAT_ID_MAN_FRAMES_SENT,              /* WAVE_RADIO_CNT_MAN_FRAMES_SENT */
  MTLK_WWSS_WLAN_STAT_ID_MAN_FRAMES_CONFIRMED,         /* WAVE_RADIO_CNT_MAN_FRAMES_CONFIRMED */
  MTLK_WWSS_WLAN_STAT_ID_MAN_FRAMES_RECEIVED,          /* WAVE_RADIO_CNT_MAN_FRAMES_RECEIVED */
  MTLK_WWSS_WLAN_STAT_ID_RX_MAN_FRAMES_RETRY_DROPPED,  /* WAVE_RADIO_CNT_RX_MAN_FRAMES_RETRY_DROPPED */
  MTLK_WWSS_WLAN_STAT_ID_MAN_FRAMES_CFG80211_FAILED,   /* WAVE_RADIO_CNT_MAN_FRAMES_CFG80211_FAILED */
  MTLK_WWSS_WLAN_STAT_ID_TX_PROBE_RESP_SENT,           /* WAVE_RADIO_CNT_TX_PROBE_RESP_SENT */
  MTLK_WWSS_WLAN_STAT_ID_TX_PROBE_RESP_DROPPED,        /* WAVE_RADIO_CNT_TX_PROBE_RESP_DROPPED */
  MTLK_WWSS_WLAN_STAT_ID_BSS_MGMT_TX_QUE_FULL,         /* WAVE_RADIO_CNT_BSS_MGMT_TX_QUE_FULL */
};

/**************************************************************/

struct _wave_radio_t {
  mtlk_eeprom_data_t   *ee_data; /* EEPROM parsed data */
  hw_psdb_t            *psdb;

  wave_ant_params_t    *ant_params;
  mtlk_atomic_t         cur_ant_mask;  /* Currently in use antennas mask. Currently same for RX/TX */

  char                  progmodel[STRING_PROGMODEL_SIZE];
  mtlk_vap_manager_t    *vap_manager;
  beacon_manager_priv_t bmgr_priv;
  wave_radio_limits_t   limits;

  wave_radio_phy_stat_t phy_status; /* Radio Phy RX Status */
  mtlk_osal_spinlock_t  phy_status_lock;

  uint32                total_traffic_delta; /* bytes */
  uint32                airtime_efficiency;  /* bytes/sec */

  wv_cfg80211_t         *cfg80211;
  wv_mac80211_t         *mac80211;
  mtlk_hw_api_t         *hw_api;
  mtlk_pdb_t            *param_db;
  struct _mtlk_abmgr_t  *abmgr;
  mtlk_coc_t            *coc_mgmt;

  UMI_HDK_CONFIG        current_hdkconfig; /* the currently in effect hdkconfig */
  mtlk_scan_support_t   scan_support;
  struct mtlk_chan_def  current_chandef; /* the channel that's set (even if just for scanning) */
  int                   chan_switch_type; /* ST_something */
  uint8                 last_pm_freq; /* frequency of the last loaded programming model */
  BOOL                  is_interfdet_enabled; /* Interference Detection */
  BOOL                  init_done;
  BOOL                  sta_vifs_exist;
  atomic_t              sta_cnt;
  BOOL                  progmodel_loaded;
  unsigned              idx;
  mtlk_hw_band_e        wave_band;           /* band in wave format */
  nl80211_band_e        ieee_band;           /* band in cfg80211/nl80211 format */
  /* CDB configuration */
  wave_cdb_cfg_e        cdb_config;

  mtlk_wss_t           *wss;
  mtlk_wss_cntr_handle_t  *wss_hcntrs[WAVE_RADIO_CNT_LAST];

  /* table of channels for the radio */
  g24_20_ch_tab_t       g24_20_ch_tab;
  g24_40_ch_tab_t       g24_40_ch_tab;
  g52_20_ch_tab_t       g52_20_ch_tab;
  g52_40_ch_tab_t       g52_40_ch_tab;
  g52_80_ch_tab_t       g52_80_ch_tab;
  g52_160_ch_tab_t      g52_160_ch_tab;
  uint8                 calib_done_mask[1 + NUM_TOTAL_CHANS]; /* 0th entry not used */
  BOOL                  is_sync_hostapd_done; /* indicates that STA DB is synchronous in driver and hostapd */
  struct _wave_radio_radar_ctx_t radar_ctx; /* Radar Detection context */
  MTLK_DECLARE_INIT_STATUS;
  MTLK_DECLARE_START_STATUS;
};

#define WAVE_RADIO_ANY_HW 0xff /* filler for don't care parameters*/

static const mtlk_ability_id_t _wave_radio_abilities[] = {
  WAVE_RADIO_REQ_SET_TEST,
  WAVE_RADIO_REQ_GET_TEST
};

/*** Init / deinit ***/
MTLK_INIT_STEPS_LIST_BEGIN(wave_radio)
  MTLK_INIT_STEPS_LIST_ENTRY(wave_radio, WAVE_RADIO_CFG80211_CREATE)
  MTLK_INIT_STEPS_LIST_ENTRY(wave_radio, WAVE_RADIO_VAP_MANAGER_CREATE)
  MTLK_INIT_STEPS_LIST_ENTRY(wave_radio, WAVE_RADIO_PHY_STATUS_LOCK)
MTLK_INIT_INNER_STEPS_BEGIN(wave_radio)
MTLK_INIT_STEPS_LIST_END(wave_radio);

MTLK_START_STEPS_LIST_BEGIN(wave_radio)
  MTLK_START_STEPS_LIST_ENTRY(wave_radio, WAVE_RADIO_PDB_INIT)
  MTLK_START_STEPS_LIST_ENTRY(wave_radio, WAVE_RADIO_CONTEXT_INIT)
  MTLK_START_STEPS_LIST_ENTRY(wave_radio, WAVE_RADIO_ABMGR_INIT)
  MTLK_START_STEPS_LIST_ENTRY(wave_radio, WAVE_RADIO_ABMGR_REGISTER)

  MTLK_START_STEPS_LIST_ENTRY(wave_radio, WAVE_RADIO_WSS_CREATE)

  MTLK_START_STEPS_LIST_ENTRY(wave_radio, WAVE_RADIO_CFG80211_INIT)
  MTLK_START_STEPS_LIST_ENTRY(wave_radio, WAVE_RADIO_CFG80211_REGISTER)
  MTLK_START_STEPS_LIST_ENTRY(wave_radio, WAVE_RADIO_BEACON_MAN_PRIV_INIT)
  MTLK_START_STEPS_LIST_ENTRY(wave_radio, WAVE_RADIO_VAP_CREATE)
  MTLK_START_STEPS_LIST_ENTRY(wave_radio, WAVE_RADIO_SCAN_INIT)
  MTLK_START_STEPS_LIST_ENTRY(wave_radio, WAVE_RADIO_CHANDEF_INIT)
  MTLK_START_STEPS_LIST_ENTRY(wave_radio, WAVE_RADIO_COC_INIT)
#if 0 /* moved into wave_radio_mac80211_init */
  MTLK_START_STEPS_LIST_ENTRY(wave_radio, WAVE_RADIO_IEEE80211_INIT)
  MTLK_START_STEPS_LIST_ENTRY(wave_radio, WAVE_RADIO_IEEE80211_REGISTER)
#endif
  MTLK_START_STEPS_LIST_ENTRY(wave_radio, WAVE_RADIO_RADAR_CONTEXT_INIT)
MTLK_START_INNER_STEPS_BEGIN(wave_radio)
MTLK_START_STEPS_LIST_END(wave_radio);


uint32 __MTLK_IFUNC mtk_mmb_drv_get_free_wlan_id (void);
uint32 __MTLK_IFUNC mtk_mmb_drv_free_wlan_id (void);


static void _wave_radio_destroy(wave_radio_t *radio)
{
  MTLK_CLEANUP_BEGIN(wave_radio, MTLK_OBJ_PTR(radio))
    MTLK_CLEANUP_STEP(wave_radio, WAVE_RADIO_PHY_STATUS_LOCK, MTLK_OBJ_PTR(radio),
                      mtlk_osal_lock_cleanup, (&radio->phy_status_lock));
    MTLK_CLEANUP_STEP(wave_radio, WAVE_RADIO_VAP_MANAGER_CREATE, MTLK_OBJ_PTR(radio),
                      mtlk_vap_manager_delete, (radio->vap_manager));
    MTLK_CLEANUP_STEP(wave_radio, WAVE_RADIO_CFG80211_CREATE, MTLK_OBJ_PTR(radio),
                      wv_cfg80211_destroy, (radio->cfg80211));
  MTLK_CLEANUP_END(wave_radio, MTLK_OBJ_PTR(radio));
}

void wave_radio_destroy(wave_radio_descr_t *radio_descr)
{
  wave_int i;

  for (i = 0; i < radio_descr->num_radios; i++)
    _wave_radio_destroy(&radio_descr->radio[i]);

  mtlk_fast_mem_free(radio_descr->radio);
  mtlk_fast_mem_free(radio_descr);
  radio_descr = NULL;
}

static int _wave_radio_create(wave_radio_t *radio, mtlk_work_mode_e work_mode)
{
  MTLK_INIT_TRY(wave_radio, MTLK_OBJ_PTR(radio));
    MTLK_INIT_STEP_EX(wave_radio, WAVE_RADIO_CFG80211_CREATE, MTLK_OBJ_PTR(radio),
                      wv_cfg80211_create, (),
                      radio->cfg80211, NULL != radio->cfg80211,
                      MTLK_ERR_UNKNOWN);
    MTLK_INIT_STEP_EX(wave_radio, WAVE_RADIO_VAP_MANAGER_CREATE, MTLK_OBJ_PTR(radio),
                      mtlk_vap_manager_create, (radio, radio->idx, radio->hw_api, work_mode),
                      radio->vap_manager, NULL != radio->vap_manager,
                      MTLK_ERR_UNKNOWN);
    MTLK_INIT_STEP(wave_radio, WAVE_RADIO_PHY_STATUS_LOCK, MTLK_OBJ_PTR(radio),
                      mtlk_osal_lock_init, (&radio->phy_status_lock));
  MTLK_INIT_FINALLY(wave_radio, MTLK_OBJ_PTR(radio));
  MTLK_INIT_RETURN(wave_radio, MTLK_OBJ_PTR(radio), _wave_radio_destroy, (radio));
}

wave_radio_descr_t* wave_radio_create (unsigned radio_num, mtlk_hw_api_t *hw_api,
                                       mtlk_work_mode_e work_mode)
{
  int res = MTLK_ERR_OK;
  wave_int i;
  wave_radio_t *radio = NULL;
  wave_radio_descr_t *radio_descr = NULL;

  ILOG1_D("Number of radios: %d", radio_num);

  if (!wave_card_num_of_radios_is_valid(radio_num)) {
    ELOG_D("Wrong number of radios: %u", radio_num);
    return NULL;
  }

  radio_descr = mtlk_fast_mem_alloc(MTLK_FM_USER_RADIO, sizeof(wave_radio_descr_t));
  if (NULL == radio_descr) {
    goto wave_radio_create_error;
  }

  radio = mtlk_fast_mem_alloc(MTLK_FM_USER_RADIO, sizeof(wave_radio_t) * radio_num);
  if (NULL == radio) {
    goto wave_radio_create_error;
  }

  memset(radio, 0, sizeof(wave_radio_t) * radio_num);
  memset(radio_descr, 0, sizeof(wave_radio_descr_t));

  radio_descr->radio = radio;
  radio_descr->num_radios = radio_num;

  for(i = 0; i < radio_num; i++) {
    radio_descr->radio[i].hw_api = hw_api;
    radio_descr->radio[i].idx = i;
    res = _wave_radio_create(&radio_descr->radio[i], work_mode);
    if (res != MTLK_ERR_OK) {
      /* delete radios that were created successfully */
      for(i--; i >= 0; i--)
        _wave_radio_destroy(&radio_descr->radio[i]);
      goto wave_radio_create_error;
    }
  }

  return radio_descr;

wave_radio_create_error:
  if (radio)
    mtlk_fast_mem_free(radio);
  if (radio_descr)
    mtlk_fast_mem_free(radio_descr);

  return NULL;
}

static int _wave_radio_vap_create(wave_radio_t *radio, mtlk_work_role_e role)
{
  int res = MTLK_ERR_OK;
  struct wiphy *_wiphy = (role == MTLK_ROLE_STA)?
      wv_mac80211_wiphy_get(radio->mac80211):
      wv_cfg80211_wiphy_get(radio->cfg80211);
  uint32 _vap_index;
  uint32 wlan_id;
  char ndev_name[IFNAMSIZ];

  /* re-use existing master VAP index if such exists */
  if (mtlk_vap_manager_get_master_vap_index(radio->vap_manager) != MTLK_VAP_INVALID_IDX)
    _vap_index = mtlk_vap_manager_get_master_vap_index(radio->vap_manager);
  else{
    res = mtlk_vap_manager_get_free_vap_index(radio->vap_manager, &_vap_index);
    if (MTLK_ERR_OK != res) {
      ELOG_V("No free slot for new VAP");
      return res;
    }
  }

  wlan_id = mtk_mmb_drv_get_free_wlan_id();
  snprintf(ndev_name, sizeof(ndev_name), MTLK_NDEV_NAME "%u", wlan_id);
  ILOG0_SD("ndev_name %s, VapID %u", ndev_name, _vap_index);

  rtnl_lock();
  res = mtlk_vap_manager_create_vap(radio->vap_manager,
                                    _vap_index,
                                    _wiphy,
                                    ndev_name,
                                    role,
                                    true,
                                    NULL); /* Master VAP is AP, ndev will be allocated when we call alloc_etherdev */
  rtnl_unlock();
  return res;
}

static int _wave_radio_scan_init(wave_radio_t *radio)
{
  mtlk_core_t* master_core = mtlk_vap_manager_get_master_core(radio->vap_manager);
  return scan_support_init(master_core, &radio->scan_support);
}

static int _wave_radio_chandef_init(wave_radio_t *radio)
{
  mtlk_core_t* master_core = mtlk_vap_manager_get_master_core(radio->vap_manager);
  return current_chandef_init(master_core, &radio->current_chandef);
}

static int _wave_radio_coc_init(wave_radio_t *radio)
{
  mtlk_core_t* master_core = mtlk_vap_manager_get_master_core(radio->vap_manager);
  mtlk_txmm_t *txmm = mtlk_vap_get_txmm(master_core->vap_handle);
  mtlk_coc_cfg_t  coc_cfg;

  coc_cfg.hw_antenna_cfg.num_tx_antennas = master_core->slow_ctx->tx_limits.num_tx_antennas;
  coc_cfg.hw_antenna_cfg.num_rx_antennas = master_core->slow_ctx->tx_limits.num_rx_antennas;
  coc_cfg.default_auto_cfg.interval_1x1 = 10; /* 1s */
  coc_cfg.default_auto_cfg.interval_2x2 = 50; /* 5s */
  coc_cfg.default_auto_cfg.interval_3x3 = 50; /* 5s */
  coc_cfg.default_auto_cfg.interval_4x4 = 50; /* 5s */
  coc_cfg.default_auto_cfg.high_limit_1x1 = 1000;  /* byte/sec */
  coc_cfg.default_auto_cfg.low_limit_2x2 =  5000;  /* byte/sec */
  coc_cfg.default_auto_cfg.high_limit_2x2 = 5000;  /* byte/sec */
  coc_cfg.default_auto_cfg.low_limit_3x3 =  5000;  /* byte/sec */
  coc_cfg.default_auto_cfg.high_limit_3x3 = 5000;  /* byte/sec */
  coc_cfg.default_auto_cfg.low_limit_4x4 =  5000;  /* byte/sec */
  coc_cfg.txmm = txmm;
  coc_cfg.vap_handle = master_core->vap_handle;
  radio->coc_mgmt = mtlk_coc_create(&coc_cfg);
  return (NULL != radio->coc_mgmt) ? MTLK_ERR_OK : MTLK_ERR_UNKNOWN;
}

static void _wave_radio_coc_delete(wave_radio_t *radio)
{
  return mtlk_coc_delete(radio->coc_mgmt);
}

static int _wave_radio_context_init(wave_radio_t *radio)
{
  iwpriv_cca_th_t cca_th;
  int res = MTLK_ERR_OK;

  ILOG2_D("Processing RadioID %u", radio->idx);

  radio->last_pm_freq = MTLK_HW_BAND_NONE;
  radio->progmodel_loaded = FALSE;
  radio->g24_20_ch_tab.ch_width  = CW_20;
  radio->g24_40_ch_tab.ch_width  = CW_40;
  radio->g52_20_ch_tab.ch_width  = CW_20;
  radio->g52_40_ch_tab.ch_width  = CW_40;
  radio->g52_80_ch_tab.ch_width  = CW_80;
  radio->g52_160_ch_tab.ch_width = CW_160;

  radio->is_sync_hostapd_done = TRUE;

  radio->ee_data = NULL;
  (void)mtlk_hw_get_prop(radio->hw_api, MTLK_HW_PROP_EEPROM_DATA, &radio->ee_data, sizeof(&radio->ee_data));
  MTLK_ASSERT(radio->ee_data != NULL);
  if (radio->ee_data == NULL) {
    ELOG_D("RadioID %u: Error get EEPROM", radio->idx);
    return MTLK_ERR_EEPROM;
  }

  radio->psdb = mtlk_hw_get_psdb(radio->hw_api->hw);
  if(NULL == radio->psdb) {
    ELOG_D("RadioID %u: PSDB is NULL", radio->idx);
    return MTLK_ERR_EEPROM;
  }

  radio->ant_params = wave_psdb_get_radio_ant_params(radio->psdb, radio->idx);
  MTLK_ASSERT(radio->ant_params != NULL);

  wave_radio_current_antenna_mask_reset(radio);

  radio->cdb_config = wave_psdb_get_radio_cdb_config(radio->psdb, radio->idx);
  ILOG1_DDS("RadioID %u: CBD config %d %s",
           radio->idx, radio->cdb_config, wave_cdb_cfg_to_string(radio->cdb_config));

  /* Update CCA Thresholds configuration if available in PSDB */
  if (MTLK_ERR_OK == wave_psdb_get_radio_cca_th_params(radio->psdb, radio->idx, &cca_th)) {
    res = wave_radio_cca_threshold_set(radio, &cca_th); /* Error already logged */
  }

  atomic_set(&radio->sta_cnt, 0);

  return res;
}

static void _wave_radio_vap_delete(wave_radio_t *radio)
{
  mtlk_vap_manager_deallocate_vaps(radio->vap_manager);
  mtk_mmb_drv_free_wlan_id();
}

static void _wave_radio_scan_cleanup(wave_radio_t *radio)
{
  scan_support_cleanup(&radio->scan_support);
}

static void _wave_radio_ab_register(wave_radio_t *radio)
{
  uint32 size = ARRAY_SIZE(_wave_radio_abilities);
  mtlk_abmgr_register_ability_set(radio->abmgr, _wave_radio_abilities, size);
  mtlk_abmgr_enable_ability_set(radio->abmgr, _wave_radio_abilities, size);
}

static void _wave_radio_ab_unregister(wave_radio_t *radio)
{
  uint32 size = ARRAY_SIZE(_wave_radio_abilities);
  mtlk_abmgr_disable_ability_set(radio->abmgr, _wave_radio_abilities, size);
  mtlk_abmgr_unregister_ability_set(radio->abmgr, _wave_radio_abilities, size);
}

static void
_wave_radio_mac80211_deinit (wave_radio_t *radio)
{
    /* Do not hold rtnl_lock for mac80211 interfaces, this is already done by mac80211 */
    /* Instructs mac80211 to free allocated resources
     *   and unregister netdevices from the networking subsystem */
    if (NULL != radio->mac80211) {
        wv_ieee80211_unregister(radio->mac80211);
        /*frees everything that was allocated for mac80211, including the private data for the driver.*/
        wv_ieee80211_free(radio->mac80211);
    }
}

void wave_radio_mac80211_deinit(wave_radio_descr_t *radio_descr)
{
  wave_int i;

  for (i = 0; i < radio_descr->num_radios; i++)
      _wave_radio_mac80211_deinit(&radio_descr->radio[i]);
}

static int _wave_radio_mac80211_init (wave_radio_t *radio, struct device *dev)
{
  int res = MTLK_ERR_UNKNOWN;

  radio->mac80211 = wv_ieee80211_init(dev, radio, HANDLE_T(radio->hw_api->hw));

  if (NULL == radio->mac80211) {
    ELOG_V("wv_ieee80211_init failure");
  }
  else {
    res = wv_ieee80211_setup_register(dev, radio->mac80211, HANDLE_T(radio->hw_api->hw));
    if (MTLK_ERR_OK != res)
      ELOG_D("wv_ieee80211_setup_register failure: %d", res);
  }
  return res;
}

int wave_radio_mac80211_init (wave_radio_descr_t *radio_descr, struct device *dev)
{
  wave_int i;
  int res = MTLK_ERR_OK;

  for(i = 0; i < radio_descr->num_radios; i++) {
    res = _wave_radio_mac80211_init(&radio_descr->radio[i], dev);
    if (MTLK_ERR_OK != res) {
      wave_radio_mac80211_deinit(radio_descr);
      break;
    }
  }
  return res;
}

static int _wave_radio_radar_ctx_init (wave_radio_t *radio)
{
  struct _wave_radio_radar_ctx_t *pradar_ctx = &radio->radar_ctx;

  mtlk_osal_event_init(&pradar_ctx->radar_detect_end_evt);
  mtlk_osal_event_set(&pradar_ctx->radar_detect_end_evt);   /* allow to execute Radar events */
  return MTLK_ERR_OK;
}

static void _wave_radio_radar_ctx_cleanup (wave_radio_t *radio)
{
  mtlk_osal_event_cleanup(&radio->radar_ctx.radar_detect_end_evt);
}

static void _wave_radio_radar_detect_end_reset (wave_radio_t *radio)
{
  /* postpone to execute Radar events */
  mtlk_osal_event_reset(&radio->radar_ctx.radar_detect_end_evt);
}

static void _wave_radio_radar_detect_end_set (wave_radio_t *radio)
{
  /* allow to execute Radar events */
  mtlk_osal_event_set(&radio->radar_ctx.radar_detect_end_evt);
}

void wave_radio_radar_detect_end_wait (wave_radio_t *radio)
{
  /* wait for Radar Detection end */
  int res = mtlk_osal_event_wait(&radio->radar_ctx.radar_detect_end_evt, MTLK_OSAL_EVENT_INFINITE);
  MTLK_ASSERT(MTLK_ERR_OK == res);
  MTLK_UNREFERENCED_PARAM(res);
}

void wave_radio_on_rcvry_isolate (wave_radio_t *radio)
{
  MTLK_ASSERT(radio);
  mtlk_osal_event_set(&radio->radar_ctx.radar_detect_end_evt);
}

static void _wave_radio_deinit(wave_radio_t *radio)
{
  MTLK_STOP_BEGIN(wave_radio, MTLK_OBJ_PTR(radio))
    MTLK_STOP_STEP(wave_radio, WAVE_RADIO_RADAR_CONTEXT_INIT, MTLK_OBJ_PTR(radio),
                   _wave_radio_radar_ctx_cleanup, (radio));
#if 0 /* moved into wave_radio_mac80211_init */
    /* Do not hold rtnl_lock for mac80211 interfaces, this is already done by mac80211 */
    /* Instructs mac80211 to free allocated resources
     *   and unregister netdevices from the networking subsystem */
    MTLK_STOP_STEP(wave_radio, WAVE_RADIO_IEEE80211_REGISTER, MTLK_OBJ_PTR(radio),
                      wv_ieee80211_unregister, (radio->mac80211));
    /*frees everything that was allocated for mac80211, including the private data for the driver.*/
    MTLK_STOP_STEP(wave_radio, WAVE_RADIO_IEEE80211_INIT, MTLK_OBJ_PTR(radio),
                      wv_ieee80211_free, (radio->mac80211));
#endif

    MTLK_STOP_STEP(wave_radio, WAVE_RADIO_COC_INIT, MTLK_OBJ_PTR(radio),
                  _wave_radio_coc_delete, (radio));
    MTLK_STOP_STEP(wave_radio, WAVE_RADIO_CHANDEF_INIT, MTLK_OBJ_PTR(radio),
                   MTLK_NOACTION, ());
    MTLK_STOP_STEP(wave_radio, WAVE_RADIO_SCAN_INIT, MTLK_OBJ_PTR(radio),
                  _wave_radio_scan_cleanup, (radio));
    MTLK_STOP_STEP(wave_radio, WAVE_RADIO_VAP_CREATE, MTLK_OBJ_PTR(radio),
                  _wave_radio_vap_delete, (radio));
    MTLK_STOP_STEP(wave_radio, WAVE_RADIO_BEACON_MAN_PRIV_INIT, MTLK_OBJ_PTR(radio),
                   wave_beacon_man_private_cleanup, (&radio->bmgr_priv, radio->hw_api->hw));
    MTLK_STOP_STEP(wave_radio, WAVE_RADIO_CFG80211_REGISTER, MTLK_OBJ_PTR(radio),
                   wv_cfg80211_unregister, (radio->cfg80211));
    MTLK_STOP_STEP(wave_radio, WAVE_RADIO_CFG80211_INIT, MTLK_OBJ_PTR(radio),
                   wv_cfg80211_cleanup, (radio->cfg80211));

    MTLK_STOP_STEP(wave_radio, WAVE_RADIO_WSS_CREATE, MTLK_OBJ_PTR(radio),
                      mtlk_wss_delete, (radio->wss));

    MTLK_STOP_STEP(wave_radio, WAVE_RADIO_ABMGR_REGISTER, MTLK_OBJ_PTR(radio),
                   _wave_radio_ab_unregister, (radio));
    MTLK_STOP_STEP(wave_radio, WAVE_RADIO_ABMGR_INIT, MTLK_OBJ_PTR(radio),
                   mtlk_abmgr_delete, (radio->abmgr));
    MTLK_STOP_STEP(wave_radio, WAVE_RADIO_CONTEXT_INIT, MTLK_OBJ_PTR(radio),
                   MTLK_NOACTION, ());
    MTLK_STOP_STEP(wave_radio, WAVE_RADIO_PDB_INIT, MTLK_OBJ_PTR(radio),
                   wave_pdb_delete, (radio->param_db));
  MTLK_STOP_END(wave_radio, MTLK_OBJ_PTR(radio));
}

void wave_radio_deinit(wave_radio_descr_t *radio_descr)
{
  wave_int i;

  for (i = 0; i < radio_descr->num_radios; i++)
      _wave_radio_deinit(&radio_descr->radio[i]);
}

static int _wave_radio_init(wave_radio_t *radio, struct device *dev)
{
  mtlk_card_type_t hw_type = MTLK_CARD_UNKNOWN;
  mtlk_card_type_info_t hw_type_info;
  int res;

  MTLK_START_TRY(wave_radio, MTLK_OBJ_PTR(radio));

  res = mtlk_hw_get_prop(radio->hw_api, MTLK_HW_PROP_CARD_TYPE, &hw_type, sizeof(&hw_type));
  if (res != MTLK_ERR_OK) {
    ELOG_D("RadioID %u: Can't determine HW type", radio->idx);
    return res;
  }

  res = mtlk_hw_get_prop(radio->hw_api, MTLK_HW_PROP_CARD_TYPE_INFO, &hw_type_info, sizeof(&hw_type_info));
  if (res != MTLK_ERR_OK) {
    ELOG_D("RadioID %u: Can't determine HW type info", radio->idx);
    return res;
  }

    MTLK_START_STEP_EX(wave_radio, WAVE_RADIO_PDB_INIT, MTLK_OBJ_PTR(radio),
                       wave_pdb_create, (PARAM_DB_MODULE_ID_RADIO, hw_type, hw_type_info),
                       radio->param_db,
                       radio->param_db != NULL,
                       MTLK_ERR_NO_MEM);

    MTLK_START_STEP(wave_radio, WAVE_RADIO_CONTEXT_INIT, MTLK_OBJ_PTR(radio),
                    _wave_radio_context_init, (radio));

    MTLK_START_STEP_EX(wave_radio, WAVE_RADIO_ABMGR_INIT, MTLK_OBJ_PTR(radio),
                       mtlk_abmgr_create, (),
                       radio->abmgr,
                       radio->abmgr != NULL,
                       MTLK_ERR_NO_MEM);
    MTLK_START_STEP_VOID(wave_radio, WAVE_RADIO_ABMGR_REGISTER, MTLK_OBJ_PTR(radio),
                         _wave_radio_ab_register, (radio));

    MTLK_START_STEP_EX(wave_radio, WAVE_RADIO_WSS_CREATE, MTLK_OBJ_PTR(radio),
                       mtlk_wss_create, (mtlk_dfg_get_driver_wss(),
                       _wave_radio_wss_id_map, ARRAY_SIZE(_wave_radio_wss_id_map)),
                       radio->wss, radio->wss != NULL, MTLK_ERR_NO_MEM);

    MTLK_START_STEP(wave_radio, WAVE_RADIO_CFG80211_INIT, MTLK_OBJ_PTR(radio),
                    wv_cfg80211_init,
                    (radio->cfg80211, dev, radio, HANDLE_T(radio->hw_api->hw)));
    MTLK_START_STEP(wave_radio, WAVE_RADIO_CFG80211_REGISTER, MTLK_OBJ_PTR(radio),
                    wv_cfg80211_register, (radio->cfg80211));
    MTLK_START_STEP(wave_radio, WAVE_RADIO_BEACON_MAN_PRIV_INIT, MTLK_OBJ_PTR(radio),
                    wave_beacon_man_private_init, (&radio->bmgr_priv));

    /* AP mode interface creation should be initiated here in order to create a wlan
     * device. As for station mode interface, mac80211 will create the first wlan
     * device for every radio when mac80211 is initialized and registered */
    MTLK_START_STEP(wave_radio, WAVE_RADIO_VAP_CREATE, MTLK_OBJ_PTR(radio),
                    _wave_radio_vap_create, (radio, MTLK_ROLE_AP));

    MTLK_START_STEP(wave_radio, WAVE_RADIO_SCAN_INIT, MTLK_OBJ_PTR(radio),
                    _wave_radio_scan_init, (radio));
    MTLK_START_STEP(wave_radio, WAVE_RADIO_CHANDEF_INIT, MTLK_OBJ_PTR(radio),
                    _wave_radio_chandef_init, (radio));
    MTLK_START_STEP(wave_radio, WAVE_RADIO_COC_INIT, MTLK_OBJ_PTR(radio),
                    _wave_radio_coc_init, (radio));
#if 0 /* moved into wave_radio_mac80211_init */
    MTLK_START_STEP_EX(wave_radio, WAVE_RADIO_IEEE80211_INIT, MTLK_OBJ_PTR(radio),
                       wv_ieee80211_init, (dev, radio, HANDLE_T(radio->hw_api->hw)),
                       radio->mac80211,
                       NULL != radio->mac80211,
                       MTLK_ERR_UNKNOWN);

    MTLK_START_STEP(wave_radio, WAVE_RADIO_IEEE80211_REGISTER, MTLK_OBJ_PTR(radio),
                    wv_ieee80211_setup_register, (dev, radio->mac80211, HANDLE_T(radio->hw_api->hw)));
#endif
    MTLK_START_STEP(wave_radio, WAVE_RADIO_RADAR_CONTEXT_INIT, MTLK_OBJ_PTR(radio),
                    _wave_radio_radar_ctx_init, (radio));
  MTLK_START_FINALLY(wave_radio, MTLK_OBJ_PTR(radio));
  MTLK_START_RETURN(wave_radio, MTLK_OBJ_PTR(radio), _wave_radio_deinit, (radio));
}

int wave_radio_init(wave_radio_descr_t *radio_descr, struct device *dev)
{
  int res = MTLK_ERR_OK;
  wave_int i;

  for(i = 0; i < radio_descr->num_radios; i++) {
    res = _wave_radio_init(&radio_descr->radio[i], dev);
    if (res != MTLK_ERR_OK) {
      /* cleanup radios that were initialized successfully */
      for(i--; i >= 0; i--)
        _wave_radio_deinit(&radio_descr->radio[i]);
      break;
    }
  }
  return res;
}

int    __MTLK_IFUNC mtlk_df_user_rtlog_register(uint32 hw_idx, mtlk_vap_handle_t vap_handle); /* FIXME */
void   __MTLK_IFUNC mtlk_df_user_rtlog_unregister(uint32 hw_idx); /* FIXME */

int wave_radio_init_finalize(wave_radio_descr_t *radio_descr)
{
  wave_radio_t* radio = &radio_descr->radio[0];
  mtlk_vap_handle_t vap_handle;
  mtlk_vap_info_internal_t *_info;
  wave_int i;
  int res = MTLK_ERR_NOT_IN_USE;

  for (i = 0; i < radio_descr->num_radios; i++) {
    res = mtlk_vap_manager_get_master_vap(radio->vap_manager, &vap_handle);
    if (res != MTLK_ERR_OK)
      return res;

    rtnl_lock();
    res = mtlk_vap_start(vap_handle);
    rtnl_unlock();
    if (MTLK_ERR_OK == res) {
      radio->init_done = TRUE;
    }
    else {
      _info = (mtlk_vap_info_internal_t *)vap_handle;
      ELOG_DDD("CID-%04x: Can't start VAP num=%d res=%d", mtlk_vap_get_oid(vap_handle), _info->id, res);
    }

    radio++;
  }

  if (res != MTLK_ERR_OK)
    return res; /* None radios */

  /* HW RT-Logger */
  /* WAVE600: TODO: move it out to per HW, not per Radio! */
  mtlk_df_user_rtlog_register(mtlk_vap_manager_get_wlan_index(radio_descr->radio[0].vap_manager), vap_handle);

  return MTLK_ERR_OK;
}

void wave_radio_deinit_finalize(wave_radio_descr_t *radio_descr)
{
  wave_radio_t* radio = &radio_descr->radio[0];
  wave_int i;

  for (i = 0; i < radio_descr->num_radios; i++) {
    if (radio->init_done) {
      mtlk_vap_manager_stop_all_vaps(radio->vap_manager);
      radio->init_done = FALSE;
    }

    radio++;
  }

  /* HW RT-Logger */
  /* WAVE600: TODO: move it out to per HW, not per Radio! */
  mtlk_df_user_rtlog_unregister(mtlk_vap_manager_get_wlan_index(radio_descr->radio[0].vap_manager));
}

void wave_radio_prepare_stop(wave_radio_descr_t *radio_descr)
{
  wave_radio_t* radio = &radio_descr->radio[0];
  wave_int i;

  for (i = 0; i < radio_descr->num_radios; i++) {
    mtlk_vap_manager_prepare_stop(radio->vap_manager);
    radio++;
  }
}

/*** Context accessors ***/

unsigned wave_radio_id_get (wave_radio_t *radio)
{
  MTLK_ASSERT(NULL != radio);
  return radio->idx;
}

void *wave_radio_beacon_man_private_get(wave_radio_t *radio)
{
  MTLK_ASSERT(NULL != radio);
  return &radio->bmgr_priv;
}

mtlk_wss_t *
wave_radio_wss_get (wave_radio_t *radio)
{
  MTLK_ASSERT(NULL != radio);
  return radio->wss;
}

mtlk_vap_manager_t *
wave_radio_vap_manager_get (wave_radio_t *radio)
{
  MTLK_ASSERT(NULL != radio);
  return radio->vap_manager;
}

mtlk_df_t *
wave_radio_df_get (wave_radio_t *radio)
{
  MTLK_ASSERT(NULL != radio);
  return mtlk_vap_manager_get_master_df(radio->vap_manager);
}

mtlk_core_t *
wave_radio_master_core_get (wave_radio_t *radio)
{
  MTLK_ASSERT(NULL != radio);
  return mtlk_vap_manager_get_master_core(radio->vap_manager);
}

mtlk_vap_manager_t *
wave_radio_descr_vap_manager_get (wave_radio_descr_t *radio_descr, unsigned radio_idx)
{
  MTLK_ASSERT(radio_idx < radio_descr->num_radios);
  return radio_idx < radio_descr->num_radios ? radio_descr->radio[radio_idx].vap_manager : NULL;
}

mtlk_hw_api_t *
wave_radio_descr_hw_api_get (wave_radio_descr_t *radio_descr, unsigned radio_idx)
{
  MTLK_ASSERT(radio_idx < radio_descr->num_radios);
  return radio_idx < radio_descr->num_radios ? radio_descr->radio[radio_idx].hw_api : NULL;
}

int
wave_radio_descr_master_vap_handle_get (wave_radio_descr_t *radio_descr, unsigned radio_idx, mtlk_vap_handle_t  *vap_handle)
{
  MTLK_ASSERT(radio_idx < radio_descr->num_radios);
  return mtlk_vap_manager_get_master_vap(radio_descr->radio[radio_idx].vap_manager, vap_handle);
}

static mtlk_txmm_t *
_wave_radio_txmm_get (wave_radio_t *radio)
{
  mtlk_txmm_t       *txmm = NULL;
  mtlk_vap_handle_t  vap_handle = NULL;

  MTLK_ASSERT(radio != NULL);

  if (radio) {
    mtlk_vap_manager_get_master_vap(radio->vap_manager, &vap_handle);
  }

  if (vap_handle) {
    txmm = mtlk_vap_get_txmm(vap_handle);
  }

  return txmm;
}

static wave_ant_params_t *
_wave_radio_get_ant_params (wave_radio_t *radio)
{
  return ((radio) ? radio->ant_params : NULL);
}

uint8 __MTLK_IFUNC
wave_radio_max_tx_antennas_get (wave_radio_t *radio)
{
  wave_ant_params_t *ant_params = _wave_radio_get_ant_params(radio);
  return ((ant_params) ? ant_params->tx_ant_num : 0);
}

uint8 __MTLK_IFUNC
wave_radio_max_rx_antennas_get (wave_radio_t *radio)
{
  wave_ant_params_t *ant_params = _wave_radio_get_ant_params(radio);
  return ((ant_params) ? ant_params->rx_ant_num : 0);
}

uint8 __MTLK_IFUNC
wave_radio_tx_antenna_mask_get (wave_radio_t *radio)
{
  wave_ant_params_t *ant_params = _wave_radio_get_ant_params(radio);
  return ((ant_params) ? ant_params->tx_ant_mask : 0);
}

uint8 __MTLK_IFUNC
wave_radio_rx_antenna_mask_get (wave_radio_t *radio)
{
  wave_ant_params_t *ant_params = _wave_radio_get_ant_params(radio);
  return ((ant_params) ? ant_params->rx_ant_mask : 0);
}

void __MTLK_IFUNC
wave_radio_ant_masks_num_sts_get (wave_radio_t *radio, u32 *tx_ant_mask, u32 *rx_ant_mask, u32 *num_sts)
{
  wave_ant_params_t *ant_params = _wave_radio_get_ant_params(radio);
  if (ant_params) {
    *tx_ant_mask  = ant_params->tx_ant_mask;
    *rx_ant_mask  = ant_params->rx_ant_mask;
    *num_sts      = wave_hw_get_num_sts_by_ant_mask (radio->hw_api->hw, *tx_ant_mask);
  }
}

uint8 __MTLK_IFUNC
wave_radio_current_antenna_mask_get (wave_radio_t *radio)
{
  MTLK_ASSERT(radio);
  return mtlk_osal_atomic_get(&radio->cur_ant_mask);
}

void __MTLK_IFUNC
wave_radio_current_antenna_mask_set (wave_radio_t *radio, uint8 mask)
{
  MTLK_ASSERT(radio);
  mtlk_osal_atomic_set(&radio->cur_ant_mask, mask);
}

void __MTLK_IFUNC
wave_radio_current_antenna_mask_reset (wave_radio_t *radio)
{
  MTLK_ASSERT(radio);
  mtlk_osal_atomic_set(&radio->cur_ant_mask, wave_radio_rx_antenna_mask_get(radio));
}

void __MTLK_IFUNC
wave_radio_antenna_cfg_update (wave_radio_t *radio, uint8 mask)
{
  wave_ant_params_t *ant_params = _wave_radio_get_ant_params(radio);
  uint8 ant_num = count_bits_set(mask);

  MTLK_ASSERT(radio);

  if (ant_params) {
    ant_params->tx_ant_num = ant_num;
    ant_params->rx_ant_num = ant_num;
    ant_params->tx_ant_mask = mask;
    ant_params->rx_ant_mask = mask;

    wave_radio_current_antenna_mask_set(radio, mask);
  }
}

/*** CFG80211 interface ***/

/* For channels whose nominal bandwidth falls completely or partly within the band
   5600 MHz to 5650 MHz, the Channel Availability Check Time shall be 10 minutes.  */
#define ETSI_LONG_CAC_TIME_MS   600000
#define ETSI_LONG_CAC_LOW_FREQ  5600
#define ETSI_LONG_CAC_HIGH_FREQ 5650

uint32 _wave_radio_chandef_width_get (const struct cfg80211_chan_def *c)
{
  uint32 width;

  switch (c->width) {
  case NL80211_CHAN_WIDTH_20:
  case NL80211_CHAN_WIDTH_20_NOHT:
    width = 20;
    break;
  case NL80211_CHAN_WIDTH_40:
    width = 40;
    break;
  case NL80211_CHAN_WIDTH_80P80:
  case NL80211_CHAN_WIDTH_80:
    width = 80;
    break;
  case NL80211_CHAN_WIDTH_160:
    width = 160;
    break;
  default:
    return 0;
  }
  return width;
}

uint32 wave_radio_chandef_width_get(const struct cfg80211_chan_def *c)
{
  return _wave_radio_chandef_width_get(c);
}

static char*
__iftype_to_string(
enum nl80211_iftype iftype)
{
  switch (iftype) {
  case NL80211_IFTYPE_UNSPECIFIED:
    return "UNSPECIFIED";
  case NL80211_IFTYPE_ADHOC:
    return "ADHOC";
  case NL80211_IFTYPE_STATION:
    return "STATION";
  case NL80211_IFTYPE_AP:
    return "AP";
  case NL80211_IFTYPE_AP_VLAN:
    return "AP_VLAN";
  case NL80211_IFTYPE_WDS:
    return "WDS";
  case NL80211_IFTYPE_MONITOR:
    return "MONITOR";
  case NL80211_IFTYPE_MESH_POINT:
    return "MESH_POINT";
  case NL80211_IFTYPE_P2P_CLIENT:
    return "P2P_CLIENT";
  case NL80211_IFTYPE_P2P_GO:
    return "P2P_GO";
  default:
    ILOG0_D("Unknown type 0x%04X", (uint32)iftype);
  }
  return "Unknown type";
};

static int
_wave_radio_abort_and_prevent_scan (wave_radio_t *radio)
{
  int res;
  mtlk_clpb_t *clpb = NULL;
  mtlk_alter_scan_t alter_scan;

  /* Abort in case a scan is running (initiated by the sta interface)
     and prevent future scans */
  memset(&alter_scan, 0, sizeof(alter_scan));
  alter_scan.abort_scan = TRUE;
  alter_scan.pause_or_prevent = TRUE;

  res = _mtlk_df_user_invoke_core(wave_radio_df_get(radio),
            WAVE_RADIO_REQ_ALTER_SCAN, &clpb, &alter_scan, sizeof(alter_scan));
  res = _mtlk_df_user_process_core_retval(res, clpb, WAVE_RADIO_REQ_ALTER_SCAN, TRUE);

  if (MTLK_ERR_OK != res)
    ELOG_V("Failed to abort or prevent scan");

  return res;
}

static int
_wave_radio_allow_or_resume_scan (wave_radio_t *radio)
{
  int res;
  mtlk_clpb_t *clpb = NULL;
  mtlk_alter_scan_t alter_scan;

  memset(&alter_scan, 0, sizeof(alter_scan));
  alter_scan.resume_or_allow = TRUE;

  res = _mtlk_df_user_invoke_core(wave_radio_df_get(radio),
            WAVE_RADIO_REQ_ALTER_SCAN, &clpb, &alter_scan, sizeof(alter_scan));
  res = _mtlk_df_user_process_core_retval(res, clpb, WAVE_RADIO_REQ_ALTER_SCAN, TRUE);

  if (MTLK_ERR_OK != res)
    ELOG_V("Failed to resume or allow scan");

  return res;
}

void *wave_radio_virtual_interface_add(
  wave_radio_t *radio,
  mtlk_mbss_cfg_t *mbss_cfg,
  enum nl80211_iftype iftype)
{
  int res = MTLK_ERR_OK;
  mtlk_clpb_t *clpb = NULL;
  void **clpb_data, *wdev;
  mtlk_df_user_t *df_user;
  char *s_iftype;

  MTLK_ASSERT(NULL != radio);
  df_user = mtlk_df_get_user(mtlk_vap_manager_get_master_df(radio->vap_manager));
  MTLK_ASSERT(NULL != df_user);

  s_iftype = __iftype_to_string(iftype);

  ILOG1_SSD("%s: Invoked from %s (%i)", mtlk_df_user_get_name(df_user), current->comm, current->pid);
  ILOG1_SS("name = %s, iftype = %s", mbss_cfg->added_vap_name, s_iftype);

  {
    int retry_counter = 0;
    do {
      res = _mtlk_df_user_invoke_core(mtlk_df_user_get_df(df_user), WAVE_RADIO_REQ_MBSS_ADD_VAP_NAME, &clpb, mbss_cfg, sizeof(*mbss_cfg));
      res = _mtlk_df_user_process_core_retval_void(res, clpb, WAVE_RADIO_REQ_MBSS_ADD_VAP_NAME, FALSE);

      if (MTLK_ERR_RETRY != res) break;
      mtlk_osal_msleep(50);
      retry_counter++;
    } while ((MTLK_ERR_RETRY == res) && (retry_counter < MAX_VAP_WAIT_RETRIES));

    if (retry_counter > 0)
      ILOG0_SD("%s: Scan waited, number of retries %d", mtlk_df_user_get_name(df_user), retry_counter);
  }

  if (MTLK_ERR_OK != res) {
    /* set an error which will be detected by cfg80211 */
    wdev = ERR_PTR(_mtlk_df_mtlk_to_linux_error_code(res));
  } else {
    clpb_data = mtlk_clpb_enum_get_next(clpb, NULL);
    wdev = clpb_data ? *clpb_data : ERR_PTR(-EFAULT);
    mtlk_clpb_delete(clpb);
  }

  return wdev;
}

int wave_radio_virtual_interface_del(
  wave_radio_t *radio,
  struct net_device *ndev)
{
  int res = MTLK_ERR_OK;
  mtlk_clpb_t *clpb = NULL;
  mtlk_df_user_t *df_user = mtlk_df_user_from_ndev(ndev);
  mtlk_mbss_cfg_t mbss_cfg;
  mtlk_vap_handle_t _vap_handle;
  mtlk_df_t *master_df;
  int scan_paused = 0;

  MTLK_ASSERT(NULL != radio);

  ILOG1_SSD("%s: Invoked from %s (%i)", ndev->name, current->comm, current->pid);
  MTLK_CHECK_DF_USER(df_user);

  master_df = mtlk_vap_manager_get_master_df(radio->vap_manager);
  MTLK_ASSERT(NULL != master_df);

  _vap_handle = mtlk_df_get_vap_handle(mtlk_df_user_get_df(df_user));

  mbss_cfg.vap_handle = _vap_handle;

  if (!mtlk_vap_is_master(_vap_handle)){
    if (wave_radio_get_sta_vifs_exist(radio)) {
      res = _wave_radio_abort_and_prevent_scan(radio);
      if (MTLK_ERR_OK != res)
        goto finish;

      scan_paused = 1;

      if (is_mac_scan_running(wave_radio_scan_support_get(radio))) {
        ELOG_V("ERROR, cannot remove VAP while MAC scan is running");
        res = MTLK_ERR_BUSY;
        goto finish;
      }
    }
  }

  res = _mtlk_df_user_invoke_core(master_df,
            WAVE_RADIO_REQ_MBSS_DEL_VAP_NAME,
            &clpb, &mbss_cfg, sizeof(mbss_cfg));
  res = _mtlk_df_user_process_core_retval(res, clpb, WAVE_RADIO_REQ_MBSS_DEL_VAP_NAME, TRUE);

finish:
  if (scan_paused == 1)
    res = _wave_radio_allow_or_resume_scan(radio);

  return _mtlk_df_mtlk_to_linux_error_code(res);
}

int wave_radio_virtual_interface_change(
  struct net_device *ndev,
  enum nl80211_iftype iftype)
{
  int res;
  mtlk_df_user_t *df_user = mtlk_df_user_from_ndev(ndev);

  ILOG1_SSD("%s: Invoked from %s (%i)", ndev->name, current->comm, current->pid);
  ILOG1_S("iftype = %s", __iftype_to_string(iftype));
  MTLK_CHECK_DF_USER(df_user);

  res = mtlk_df_user_set_iftype(df_user, iftype);
  return _mtlk_df_mtlk_to_linux_error_code(res);
}

static __INLINE void __wave_radio_beacon_data_copy(mtlk_beacon_data_t *mb, struct cfg80211_beacon_data *beacon)
{
  mb->head = beacon->head;
  mb->tail = beacon->tail;
  mb->head_len = (uint16)beacon->head_len;
  mb->tail_len = (uint16)beacon->tail_len;
  mb->probe_resp = beacon->probe_resp;
  mb->probe_resp_len = (uint16)beacon->probe_resp_len;
  mb->data = NULL;
  mb->dma_addr = 0;
}

int wave_radio_beacon_change(
  wave_radio_t *radio,
  struct net_device *ndev,
  struct cfg80211_beacon_data *beacon)
{
  int res = MTLK_ERR_OK;
  mtlk_df_user_t *df_user = mtlk_df_user_from_ndev(ndev);
  mtlk_vap_handle_t vap_handle;
  uint8 vap_idx;
  mtlk_beacon_data_t beacon_data;
  mtlk_clpb_t *clpb = NULL;

  MTLK_ASSERT(NULL != radio);
  MTLK_ASSERT(NULL != beacon);

  ILOG1_SSD("%s Invoked from %s (%i)", ndev->name, current->comm, current->pid);
  MTLK_CHECK_DF_USER(df_user);

  vap_handle = mtlk_df_get_vap_handle(mtlk_df_user_get_df(df_user));
  vap_idx = mtlk_vap_get_id(vap_handle);

  df_user = mtlk_df_get_user(mtlk_vap_manager_get_master_df(radio->vap_manager));

  /* When master VAP is used as a dummy VAP (in order to support reconf
   * of all VAPs without having to restart hostapd) we avoid setting beacon
   * so that the dummy VAPs will not send beacons */
  if (WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_DISABLE_MASTER_VAP) &&
      mtlk_vap_is_master(vap_handle)){
    ILOG0_V("Dummy (disabled) Master VAP, not setting beacon template in FW");
    return _mtlk_df_mtlk_to_linux_error_code(res);
  }

  __wave_radio_beacon_data_copy(&beacon_data, beacon);
  beacon_data.vap_idx = vap_idx;
  beacon_data.bmgr_priv = &radio->bmgr_priv;

  res = _mtlk_df_user_invoke_core(mtlk_df_user_get_df(df_user), WAVE_CORE_REQ_SET_BEACON, &clpb, &beacon_data, sizeof(beacon_data));
  res = _mtlk_df_user_process_core_retval(res, clpb, WAVE_CORE_REQ_SET_BEACON, TRUE);
  return _mtlk_df_mtlk_to_linux_error_code(res);
}

static __INLINE void
__wave_radio_chandef_copy (struct mtlk_chan_def *mcd, struct cfg80211_chan_def *chandef)
{
  struct mtlk_channel       *mc = &mcd->chan;
  struct ieee80211_channel  *ic = chandef->chan;

  mcd->center_freq1 = chandef->center_freq1;
  mcd->center_freq2 = chandef->center_freq2;
  mcd->width = nlcw2mtlkcw(chandef->width);
  mcd->is_noht = (chandef->width == NL80211_CHAN_WIDTH_20_NOHT);

  mc->dfs_state_entered = ic->dfs_state_entered;
  mc->dfs_state = ic->dfs_state;
  mc->band = nlband2mtlkband(ic->band);
  mc->center_freq = ic->center_freq;
  mc->flags = ic->flags;
  mc->orig_flags = ic->orig_flags;
  mc->max_antenna_gain = ic->max_antenna_gain;
  mc->max_power = ic->max_power;
  mc->max_reg_power = ic->max_reg_power;
  mc->dfs_cac_ms = ic->dfs_cac_ms;
}

void
wave_radio_chandef_copy (struct mtlk_chan_def *mcd, struct cfg80211_chan_def *chandef)
{
  return __wave_radio_chandef_copy(mcd, chandef);
}

void __MTLK_IFUNC
wave_radio_ch_switch_event (struct wiphy *wiphy, mtlk_core_t *core, struct mtlk_chan_def *mcd)
{
  mtlk_df_t *df;
  u32 freq, low_freq, high_freq;
  struct mtlk_chan_def mcd_single_chan;
  struct mtlk_chan_def *ccd = __wave_core_chandef_get(core);

  ILOG1_DDDD("switched from center_freq1=%u, center_freq2=%u, width=%u, chan.center_freq=%u",
    mcd->center_freq1, mcd->center_freq2, mcd->width, mcd->chan.center_freq);
  ILOG1_DDDD("to center_freq1=%u, center_freq2=%u, width=%u, chan.center_freq=%u",
    ccd->center_freq1,
    ccd->center_freq2,
    ccd->width,
    ccd->chan.center_freq);

  df = mtlk_vap_get_df(core->vap_handle);
  memset(&mcd_single_chan, 0, sizeof(mcd_single_chan));

  if (__mtlk_is_sb_dfs_switch(mcd->sb_dfs.sb_dfs_bw)) {
    low_freq = freq2lowfreq(mcd->sb_dfs.center_freq, mcd->sb_dfs.width);
    high_freq = freq2highfreq(mcd->sb_dfs.center_freq, mcd->sb_dfs.width);

    mcd_single_chan.width = CW_20;
    for(freq = low_freq; freq <= high_freq; freq += 20) {
      mcd_single_chan.center_freq1 = mcd_single_chan.chan.center_freq = freq;
      if (!core_cfg_channels_overlap(&mcd_single_chan, ccd))
        wv_cfg80211_nop_finished_send(wiphy, df, &mcd_single_chan);
    }
  }

  if (!is_channel_certain(mcd) ||
    core_cfg_equal_range(mcd, ccd))
    goto propagate_dfs_state;

  memset(&mcd_single_chan, 0, sizeof(mcd_single_chan));
  /* Report one by one, as some of them can be in unavailable state */
  if (!core_cfg_channels_overlap(mcd, ccd)) {
    low_freq = freq2lowfreq(mcd->center_freq1, mcd->width);
    high_freq = freq2highfreq(mcd->center_freq1, mcd->width);
    mcd_single_chan.width = CW_20;

    for(freq = low_freq; freq <= high_freq; freq += 20) {
      mcd_single_chan.center_freq1 = mcd_single_chan.chan.center_freq = freq;
      wv_cfg80211_nop_finished_send(wiphy, df, &mcd_single_chan);
    }
    goto propagate_dfs_state;
  }

  mcd_single_chan.width = CW_20;
  low_freq = freq2lowfreq(mcd->center_freq1, mcd->width);
  high_freq = freq2highfreq(mcd->center_freq1, mcd->width);
  for(freq = low_freq; freq <= high_freq; freq += 20) {
    mcd_single_chan.center_freq1 = mcd_single_chan.chan.center_freq = freq;
    if (!core_cfg_channels_overlap(&mcd_single_chan, ccd))
      wv_cfg80211_nop_finished_send(wiphy, df, &mcd_single_chan);
  }
  if (mcd->center_freq2 != 0) {
    low_freq = freq2lowfreq(mcd->center_freq2, mcd->width);
    high_freq = freq2highfreq(mcd->center_freq2, mcd->width);
    for(freq = low_freq; freq <= high_freq; freq += 20) {
      mcd_single_chan.center_freq1 = mcd_single_chan.chan.center_freq = freq;
      if (!core_cfg_channels_overlap(&mcd_single_chan, ccd))
        wv_cfg80211_nop_finished_send(wiphy, df, &mcd_single_chan);
    }
  }

propagate_dfs_state:
  mtlk_df_user_schedule_prop_dfs_wq(mtlk_df_get_user(df));
}

int wave_radio_ap_chanwidth_set(
  struct wiphy *wiphy,
  wave_radio_t *radio,
  struct net_device *ndev,
  struct cfg80211_chan_def *chandef)
{
  int res = MTLK_ERR_OK;
  mtlk_clpb_t *clpb = NULL;
  struct set_chan_param_data cpd;
  mtlk_df_t *master_df = mtlk_vap_manager_get_master_df(radio->vap_manager);
  struct mtlk_chan_def  *ccd = wave_radio_chandef_get(radio);
  struct mtlk_chan_def original_cd;
  mtlk_df_user_t *df_user = mtlk_df_user_from_ndev(ndev);
  mtlk_vap_handle_t vap_handle;

  ILOG1_SSD("%s: Invoked from %s (%i)", ndev->name, current->comm, current->pid);
  MTLK_CHECK_DF_USER(df_user);

  vap_handle = mtlk_df_get_vap_handle(mtlk_df_user_get_df(df_user));
  /*
   * Prevent AP role VAP to switch channels in case a station role interface exists
   * Just notify kernel the real channel we work on (if it's a certain one) instead
   *
  */
  if (wave_radio_get_sta_vifs_exist(radio)) {
    if (is_channel_certain(ccd)) {
      ILOG0_V("Cannot switch VAP channels, since there are station role interfaces active");
      wdev_lock(ndev->ieee80211_ptr);
      wv_cfg80211_ch_switch_notify(ndev, ccd, TRUE);
      wdev_unlock(ndev->ieee80211_ptr);
      res = MTLK_ERR_OK;
      return  _mtlk_df_mtlk_to_linux_error_code(res);
    }
  }

  original_cd = *ccd;

  ILOG1_DDDDD("center_freq1=%u, nl_width=%u, chan.nlband=%u, chan.center_freq=%hu, chan.flags=0x%08x, ",
    chandef->center_freq1, chandef->width, chandef->chan->band, chandef->chan->center_freq, chandef->chan->flags);

  memset(&cpd, 0, sizeof(cpd));
  wave_radio_chandef_copy(&cpd.chandef, chandef);
  cpd.ndev = ndev;
  cpd.vap_id = mtlk_vap_get_id(vap_handle);

  res = _mtlk_df_user_invoke_core(master_df, WAVE_RADIO_REQ_SET_CHAN, &clpb, &cpd, sizeof(cpd));
  res = _mtlk_df_user_process_core_retval(res, clpb, WAVE_RADIO_REQ_SET_CHAN, TRUE);

  if (res == MTLK_ERR_OK)
    wave_radio_ch_switch_event(wiphy, mtlk_vap_manager_get_master_core(radio->vap_manager), &original_cd);

  return _mtlk_df_mtlk_to_linux_error_code(res);
}

int wave_radio_ap_start(
  struct wiphy *wiphy,
  wave_radio_t *radio,
  struct net_device *ndev,
  struct cfg80211_ap_settings *info)
{
  int res = MTLK_ERR_PARAMS;
  mtlk_clpb_t *clpb = NULL;
  mtlk_df_user_t *df_user = mtlk_df_user_from_ndev(ndev);
  struct mtlk_ap_settings aps;
  struct mtlk_chan_def  *ccd;
  mtlk_core_t *master_core;
  int scan_paused = 0;

  MTLK_ASSERT(NULL != radio);

  ccd = wave_radio_chandef_get(radio);
  master_core = mtlk_vap_manager_get_master_core(radio->vap_manager);

  /* FIXCFG80211: add all necessary configuration */
  ILOG1_SSD("%s: Invoked from %s (%i)", ndev->name, current->comm, current->pid);
  MTLK_CHECK_DF_USER(df_user);

  if (wave_radio_get_sta_vifs_exist(radio)) {

    res = _wave_radio_abort_and_prevent_scan(radio);
    if (MTLK_ERR_OK != res)
      goto finish;

    scan_paused = 1;

    if (is_mac_scan_running(wave_radio_scan_support_get(radio))) {
      ELOG_V("ERROR, cannot add VAP while MAC scan is running");
      res = MTLK_ERR_BUSY;
      goto finish;
    }

    if (is_channel_certain(ccd)) {
      struct ieee80211_channel *ch = ieee80211_get_channel(wiphy, ccd->chan.center_freq);
      struct mtlk_chan_def *mcd = ccd;
      info->chandef.chan = ch;
      info->chandef.width = mtlkcw2nlcw(mcd->width, mcd->is_noht);
      info->chandef.center_freq1 = mcd->center_freq1;
      info->chandef.center_freq2 = mcd->center_freq2;
      ILOG0_D("Notify hostapd of current channel %u", ccd->center_freq1);
      wv_cfg80211_ch_switch_notify(ndev, ccd, TRUE);
    } else {
      ELOG_S("%s: Sta vifs exists but channel is uncertain", ndev->name);
    }
  } else {
    /* start by setting the required channel, unless it's already right, ignore errors */
    wave_radio_ap_chanwidth_set(wiphy, radio, ndev, &info->chandef);
  }

  if (NULL == info->ssid || MIB_ESSID_LENGTH < info->ssid_len)
    goto finish;

  aps.beacon_interval = info->beacon_interval;
  aps.dtim_period = info->dtim_period;
  aps.hidden_ssid = info->hidden_ssid;
  aps.essid_len = info->ssid_len;
  wave_strncopy(aps.essid, info->ssid, sizeof(aps.essid), aps.essid_len);

  if ((res = wave_radio_beacon_change(radio, ndev, &info->beacon)) != MTLK_ERR_OK)
    goto finish;

  /* Set security */
  {
    mtlk_core_ui_auth_cfg_t   auth_cfg;
    int i;

    ILOG1_D("Authentication type:0x%x", info->auth_type);
    auth_cfg.wep_enabled = 0;
    auth_cfg.rsn_enabled = 0;

    switch (info->auth_type) {
    case NL80211_AUTHTYPE_OPEN_SYSTEM:
      auth_cfg.authentication = MTLK_AUTHENTICATION_OPEN_SYSTEM;
      break;
    case NL80211_AUTHTYPE_SHARED_KEY:
      auth_cfg.authentication = MTLK_AUTHENTICATION_SHARED_KEY;
      break;
    case NL80211_AUTHTYPE_AUTOMATIC:
      auth_cfg.authentication = MTLK_AUTHENTICATION_AUTO;
      break;
    case NL80211_AUTHTYPE_NETWORK_EAP:
    default:
      ILOG1_D("Authentication type:0x%x is not supported", info->auth_type);
      res = MTLK_ERR_PARAMS;
      goto finish;
    }

    ILOG1_D("Number of akm suits:%d", info->crypto.n_akm_suites);
    for (i = 0; i < info->crypto.n_akm_suites; i++) {
      ILOG1_D("akm_suites:0x%x", info->crypto.akm_suites[i]);
      switch (info->crypto.akm_suites[i]) {
      case WLAN_AKM_SUITE_8021X:
        if (info->crypto.wpa_versions & NL80211_WPA_VERSION_1)
          auth_cfg.rsn_enabled = 1;
        if (info->crypto.wpa_versions & NL80211_WPA_VERSION_2)
          auth_cfg.rsn_enabled = 1;
        break;
      case WLAN_AKM_SUITE_PSK:
        if (info->crypto.wpa_versions & NL80211_WPA_VERSION_1)
          auth_cfg.rsn_enabled = 1;
        if (info->crypto.wpa_versions & NL80211_WPA_VERSION_2)
          auth_cfg.rsn_enabled = 1;
        break;
      }
    }

    ILOG1_D("Number of ciphers pairwise:%d", info->crypto.n_ciphers_pairwise);
    for (i = 0; i < info->crypto.n_ciphers_pairwise; i++) {
      ILOG1_D("ciphers pairwise:0x%x", info->crypto.ciphers_pairwise[i]);
      switch (info->crypto.ciphers_pairwise[i]) {
      case WLAN_CIPHER_SUITE_WEP40:
      case WLAN_CIPHER_SUITE_WEP104:
        auth_cfg.wep_enabled = 1;
        break;
      case WLAN_CIPHER_SUITE_TKIP:
      case WLAN_CIPHER_SUITE_CCMP:
        break;
      }
    }
    ILOG1_D("cipher group:0x%x", info->crypto.cipher_group);

    res = _mtlk_df_user_invoke_core(mtlk_df_user_get_df(df_user), WAVE_CORE_REQ_SET_AUTH_CFG, &clpb, (char*)&auth_cfg, sizeof(auth_cfg));
    res = _mtlk_df_user_process_core_retval_void(res, clpb, WAVE_CORE_REQ_SET_AUTH_CFG, TRUE);

    if (MTLK_ERR_OK != res) {
      goto finish;
    }
  }

  ILOG2_V("Switching to serializer to activate the VAP and set the BSS");
  res = _mtlk_df_user_invoke_core(mtlk_df_user_get_df(df_user), WAVE_CORE_REQ_ACTIVATE_OPEN, &clpb, &aps, sizeof(aps));
  res = _mtlk_df_user_process_core_retval(res, clpb, WAVE_CORE_REQ_ACTIVATE_OPEN, TRUE);

#if 0
  /* Radio param DB test */
  {
    ELOG_V("*********** Radio param db test ***********");
    WAVE_RADIO_PDB_SET_INT(radio, PARAM_DB_RADIO_TEST_PARAM0, 0xf1f1f1f1);
    WAVE_RADIO_PDB_SET_INT(radio, PARAM_DB_RADIO_TEST_PARAM1, 0x2f2f2f2f);
    ELOG_D("PARAM_DB_RADIO_TEST_PARAM0 0x%x", WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_TEST_PARAM0));
    ELOG_D("PARAM_DB_RADIO_TEST_PARAM1 0x%x", WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_TEST_PARAM1));
  }
#endif

finish:
  if (scan_paused == 1)
    res = _wave_radio_allow_or_resume_scan(radio);

  return _mtlk_df_mtlk_to_linux_error_code(res);
}

int wave_radio_ap_stop(
  wave_radio_t *radio,
  struct net_device *ndev)
{
  int res = MTLK_ERR_OK;
  mtlk_clpb_t *clpb = NULL;
  mtlk_df_user_t *df_user = mtlk_df_user_from_ndev(ndev);
  mtlk_vap_handle_t _vap_handle;
  int scan_paused = 0;

  MTLK_ASSERT(NULL != radio);

  ILOG1_SSD("%s: Invoked from %s (%i)", ndev->name, current->comm, current->pid);
  MTLK_CHECK_DF_USER(df_user);

  _vap_handle = mtlk_df_get_vap_handle(mtlk_df_user_get_df(df_user));

  /* abort and prevent scan and VAP_DEACTIVATE are done in del_virtual_interface
   * for slave VAP's
   */
  if (mtlk_vap_is_slave_ap(_vap_handle))
    goto finish;

  if (wave_radio_get_sta_vifs_exist(radio)) {
    res = _wave_radio_abort_and_prevent_scan(radio);
    if (MTLK_ERR_OK != res)
      goto finish;

    scan_paused = 1;

    if (is_mac_scan_running(wave_radio_scan_support_get(radio))) {
      ELOG_V("ERROR, cannot add VAP while MAC scan is running");
      res = MTLK_ERR_BUSY;
      goto finish;
    }
  }
  do {
    res = _mtlk_df_user_invoke_core(mtlk_df_user_get_df(df_user), WAVE_CORE_REQ_DEACTIVATE, &clpb, NULL, 0);
    res = _mtlk_df_user_process_core_retval(res, clpb, WAVE_CORE_REQ_DEACTIVATE, TRUE);
    mtlk_osal_msleep(100);
  } while ((MTLK_ERR_OK != res) && (MTLK_ERR_FW != res));

finish:
  if (scan_paused == 1)
    res = _wave_radio_allow_or_resume_scan(radio);

  return _mtlk_df_mtlk_to_linux_error_code(MTLK_ERR_OK);
}

/* The next bitrate tables are filled according to values from hostapd */
static const u8 rateset_80211b[]  = {2, 4, 11, 22};                     /* 1, 2, 5.5, 11 Mbps */
static const u8 rateset_80211ag[] = {12, 18, 24, 36, 48, 72, 96, 108};  /* 6, 9, 12, 18, 24, 36, 48, 54 Mbps */

static BOOL
_wave_radio_rate_check(const u8 *rateset, int count, uint8 bitrate)
{
  do {
    if (*rateset++ == bitrate)
      return TRUE;
  } while (--count);
  return FALSE;
}

BOOL
wave_radio_is_rate_80211b(uint8 bitrate)
{
  return _wave_radio_rate_check(rateset_80211b, ARRAY_SIZE(rateset_80211b), bitrate);
}

BOOL
wave_radio_is_rate_80211ag(uint8 bitrate)
{
  return _wave_radio_rate_check(rateset_80211ag, ARRAY_SIZE(rateset_80211ag), bitrate);
}

#define MTLK_VHT_MCS_MAP_INVALID       0xFFFF
#define MTLK_EXT_CAP_OPMODE_NOTIF_MASK 0x40 /* 8th byte's 7th bit */

static __INLINE BOOL
__mtlk_is_valid_rx_tx_mcs_map (const struct ieee80211_vht_mcs_info *vht_mcs_info)
{
  return !(MTLK_VHT_MCS_MAP_INVALID == vht_mcs_info->rx_mcs_map ||
           MTLK_VHT_MCS_MAP_INVALID == vht_mcs_info->tx_mcs_map);
}

#define MBSSID_AID_OFFSET0 0
#define MBSSID_TX_VAP_AID_OFFSET64 64
#define MBSSID_NON_TX_VAP_AID_OFFSET64 64

static int _wave_radio_sta_param_process (
    struct net_device *ndev,
    const uint8 *mac,
    struct station_parameters *params,
    sta_info *info)
{
  const u8 mbssid_aid_offset[] = {MBSSID_AID_OFFSET0, MBSSID_TX_VAP_AID_OFFSET64, MBSSID_NON_TX_VAP_AID_OFFSET64};
  wv_ie80211_elems elems;
  uint32 flags_mask, flags_set, mbssid_aid_offset_idx;
  mtlk_df_user_t *df_user = mtlk_df_user_from_ndev(ndev);
  mtlk_df_t *df;
  mtlk_vap_handle_t vap_handle;
  mtlk_pdb_t *param_db_core;

  ILOG1_SSD("%s: Invoked from %s (%i)", ndev->name, current->comm, current->pid);
  MTLK_CHECK_DF_USER(df_user);

  df = mtlk_df_user_get_df(df_user);
  vap_handle = mtlk_df_get_vap_handle(df);
  param_db_core = mtlk_vap_get_param_db(vap_handle);

  memset(info, 0, sizeof(*info));
  memset(&elems, 0, sizeof(elems));

  if (mac == NULL) {
    ELOG_S("%s: MAC address is NULL", ndev->name);
    return MTLK_ERR_PARAMS;
  }

  if (!mtlk_osal_is_valid_ether_addr(mac)) {
    ELOG_SY("%s: Invalid MAC address: %Y", ndev->name, mac);
    return MTLK_ERR_PARAMS;
  }

  if (params->supported_rates_len > WV_SUPP_RATES_MAX) {
    ELOG_SDD("%s: Too big number of supported rates '%d' (supported %d)",
              ndev->name, params->supported_rates_len, WV_SUPP_RATES_MAX);
    return MTLK_ERR_PARAMS;
  }

  if (params->supported_rates) {
    struct wiphy *wiphy = ndev->ieee80211_ptr->wiphy;
    struct ieee80211_supported_band *band_2ghz = wiphy->bands[NL80211_BAND_2GHZ];
    struct ieee80211_supported_band *band_5ghz = wiphy->bands[NL80211_BAND_5GHZ];
    int i;

    for(i=0; i<params->supported_rates_len; i++) {
      uint8 bitrate = params->supported_rates[i] & SUPPORTED_BITRATE_MASK;  /* Clear basic rate flag */

      if (band_5ghz) {
        if (wave_radio_is_rate_80211ag(bitrate)) {
          MTLK_BIT_SET(info->sta_net_modes, MTLK_WSSA_11A_SUPPORTED, 1);
          break;
        }
      }

      if (band_2ghz) {
        if (wave_radio_is_rate_80211b(bitrate)) {
          MTLK_BIT_SET(info->sta_net_modes, MTLK_WSSA_11B_SUPPORTED, 1);
        }

        if (wave_radio_is_rate_80211ag(bitrate)) {
          MTLK_BIT_SET(info->sta_net_modes, MTLK_WSSA_11G_SUPPORTED, 1);
        }
      }
    }

    wave_memcpy(info->rates, sizeof(info->rates), params->supported_rates, params->supported_rates_len);
  }

  info->supported_rates_len = params->supported_rates_len;
  info->listen_interval = params->listen_interval;

  /* If code below is not working, check if "last association request" patch is in kernel and hostapd */
  if (params->resp) {
    int res = MTLK_ERR_OK;
    struct ieee80211_mgmt *p_frame= (struct ieee80211_mgmt *)params->resp;
    /* Association request by default */
    uint8 *ie_ptr = p_frame->u.assoc_req.variable;
    /* Or maybe we have Reassociation request? */
    if (MAN_TYPE_REASSOC_REQ == (WLAN_FC_GET_STYPE(mtlk_wlan_pkt_get_frame_ctl(params->resp)) >> FRAME_SUBTYPE_SHIFT))
      ie_ptr = p_frame->u.reassoc_req.variable;

    /* Adjust frame length before parsing IEs */
    res = mtlk_mgmt_parse_elems(ie_ptr, (params->resp_len-((unsigned long)ie_ptr-(unsigned long)p_frame)), &elems);
    if (MTLK_ERR_OK != res) {
      ELOG_S("%s: Error parsing IE elements", ndev->name);
      return MTLK_ERR_PARAMS;
    }

    if (elems.bss_coex_20_40) {
      info->bss_coex_20_40 = *elems.bss_coex_20_40;
    }

    if (elems.vendor_lantiq) {
      info->vendor = 1;
    }
    else if (elems.vendor_metalink) {
      info->vendor = 1;
    }
    else if (elems.vendor_w101) {
      info->vendor = 2;
    }

    if (wave_pdb_get_int(param_db_core, PARAM_DB_CORE_4ADDR_MODE) == MTLK_CORE_4ADDR_DYNAMIC) {
      if (elems.vendor_intel) {
        uint8 type = elems.vendor_intel[WLAN_EID_VENDOR_SPECIFIC_OUI_TYPE_OFFSET];
        const uint8 *attributes = elems.vendor_intel +
          WLAN_EID_INTEL_VENDOR_ATTRIBUTES_OFFSET;

        if ((type == WLAN_EID_INTEL_VENDOR_TYPE_4ADDR_MODE) &&
            (*attributes == WLAN_EID_INTEL_VENDOR_4ADDR_MODE_STA)) {
          /* Default value of WDS_client_type is set by memset to value 0 == PEER_AP */
          info->WDS_client_type = FOUR_ADDRESSES_STATION;
        }
      }
      if (elems.vendor_brcm) {
        uint8 type = elems.vendor_brcm[WLAN_EID_VENDOR_SPECIFIC_OUI_TYPE_OFFSET];

        if (WLAN_EID_BRCM_VENDOR_VERSION_2 == type) {
          uint8 flags1 = elems.vendor_brcm[WLAN_EID_BRCM_VENDOR_FLAGS1_OFFSET];
          if (flags1 & WLAN_EID_BRCM_VENDOR_DWDS_CAPABLE) {
            info->WDS_client_type = FOUR_ADDRESSES_STATION;
          }
        }
      }
      if (elems.vendor_wfa) {
        uint8 type = elems.vendor_wfa[WLAN_EID_VENDOR_SPECIFIC_OUI_TYPE_OFFSET];

        if (WLAN_OUI_TYPE_WFA_MULTI_AP == type) {
          /* Default value of WDS_client_type is set by memset to value 0 == PEER_AP */
          info->WDS_client_type = FOUR_ADDRESSES_STATION;
        }
      }
    }
    if (elems.opmode_notif) {
      info->opmode_notif = *elems.opmode_notif;
      MTLK_BFIELD_SET(info->flags, STA_FLAGS_OPMODE_NOTIF, 1);
    }
  }

  mbssid_aid_offset_idx = wave_pdb_get_int(param_db_core, PARAM_DB_CORE_MBSSID_VAP);

  ASSERT(ARRAY_SIZE(mbssid_aid_offset) > mbssid_aid_offset_idx);

  if (params->aid) {
    info->aid = params->aid;
    info->sid = params->aid - mbssid_aid_offset[mbssid_aid_offset_idx] - 1;
  }

  info->rssi_dbm = MBM_TO_DBM((int32)params->rssi);
  mtlk_stadb_stats_set_mgmt_rssi(info, info->rssi_dbm);
  ILOG2_DD("Get RSSI from HOSTAPD: params->rssi=%d, info->rssi_dbm=%d",   params->rssi, info->rssi_dbm);

  if (params->sta_modify_mask & STATION_PARAM_APPLY_UAPSD) {
    info->uapsd_queues = params->uapsd_queues;
    info->max_sp = params->max_sp;
  }

  flags_mask = params->sta_flags_mask;
  flags_set = params->sta_flags_set;

  if (flags_mask & BIT(NL80211_STA_FLAG_WME)) {
    MTLK_BFIELD_SET(info->flags, STA_FLAGS_WMM,
                    (flags_set & BIT(NL80211_STA_FLAG_WME) ? 1 : 0));
  }

  if (flags_mask & BIT(NL80211_STA_FLAG_MFP)) {
    MTLK_BFIELD_SET(info->flags, STA_FLAGS_MFP,
                    (flags_set & BIT(NL80211_STA_FLAG_MFP) ? 1 : 0));
    /* check for PBAC bit in RSNE capabilities */
    if (MTLK_BFIELD_GET(info->flags,STA_FLAGS_MFP) && (elems.rsne_capab)) {
      MTLK_BFIELD_SET(info->flags, STA_FLAGS_PBAC,
                    (*elems.rsne_capab & RSNE_CAPAB_PBAC ? 1 : 0));
    }
  }

  if (flags_mask & BIT(NL80211_STA_FLAG_AUTHORIZED)) {
    MTLK_BFIELD_SET(info->flags, STA_FLAGS_IS_8021X_FILTER_OPEN,
                    (flags_set & BIT(NL80211_STA_FLAG_AUTHORIZED)) ? 1 : 0);
  }

  mtlk_osal_copy_eth_addresses(info->addr.au8Addr, mac);

  if (params->ht_capa) {
    MTLK_ASSERT(sizeof(info->rx_mcs_bitmask) == sizeof(params->ht_capa->mcs.rx_mask));
    if(memcmp(info->rx_mcs_bitmask, params->ht_capa->mcs.rx_mask, sizeof(info->rx_mcs_bitmask)) != 0) {
      /* There are MCS rates -means sta supports HT */
      info->ht_cap_info = params->ht_capa->cap_info;
      info->ampdu_param = params->ht_capa->ampdu_params_info;
      info->tx_bf_cap_info = params->ht_capa->tx_BF_cap_info;
      MTLK_BFIELD_SET(info->flags, STA_FLAGS_11n, 1);
      MTLK_BIT_SET(info->sta_net_modes, MTLK_WSSA_11N_SUPPORTED, 1);
      wave_memcpy(info->rx_mcs_bitmask, sizeof(info->rx_mcs_bitmask),
          params->ht_capa->mcs.rx_mask, sizeof(params->ht_capa->mcs.rx_mask));
    }
  }

  if (params->vht_capa) {
    MTLK_ASSERT(sizeof(info->vht_mcs_info) == sizeof(params->vht_capa->supp_mcs));
    if(__mtlk_is_valid_rx_tx_mcs_map(&params->vht_capa->supp_mcs)) {
      info->vht_cap_info = params->vht_capa->vht_cap_info;
      MTLK_BFIELD_SET(info->flags, STA_FLAGS_11ac, 1);
      MTLK_BIT_SET(info->sta_net_modes, MTLK_WSSA_11AC_SUPPORTED, 1);
      wave_memcpy(&info->vht_mcs_info, sizeof(info->vht_mcs_info),
          &params->vht_capa->supp_mcs, sizeof(params->vht_capa->supp_mcs));
    }
  }

  if (params->he_capa_iwlwav) {
    wave_memcpy(&info->he_cap.he_cap_elem, sizeof(info->he_cap.he_cap_elem),
        &params->he_capa_iwlwav->he_capabilities[HE_MAC_PHY_OFFSET], sizeof(info->he_cap.he_cap_elem));

    /* Can't copy all capabilities due to AX mode 80+80 is not supported by FW */
    wave_memcpy(&info->he_cap.he_mcs_nss_supp, sizeof(info->he_cap.he_mcs_nss_supp),
        &params->he_capa_iwlwav->he_capabilities[HE_MCS_NSS_OFFSET], sizeof(info->he_cap.he_mcs_nss_supp));

    wave_memcpy(&info->he_cap.ppe_thres, sizeof(info->he_cap.ppe_thres),
        &params->he_capa_iwlwav->he_capabilities[HE_PPE_TH_8080_MHZ_OFFSET], sizeof(info->he_cap.ppe_thres));

    MTLK_BFIELD_SET(info->flags, STA_FLAGS_11ax, 1);
    MTLK_BIT_SET(info->sta_net_modes, MTLK_WSSA_11AX_SUPPORTED, 1);
  }

  if (params->he_oper_iwlwav) {
    info->he_operation_parameters = *params->he_oper_iwlwav;
  }

  if (params->ext_capab) {
    if ((params->ext_capab_len >= 8) &&
        (params->ext_capab[7] & MTLK_EXT_CAP_OPMODE_NOTIF_MASK)) {
      MTLK_BFIELD_SET(info->flags, STA_FLAGS_OMN_SUPPORTED, 1);
    }
  }

  return MTLK_ERR_OK;
}

int wave_radio_sta_add(
  wave_radio_t *radio,
  struct net_device *ndev,
  const uint8 *mac,
  struct station_parameters *params)
{
  int res = MTLK_ERR_OK;
  mtlk_clpb_t *clpb = NULL;
  sta_info info;
  mtlk_df_user_t *df_user = mtlk_df_user_from_ndev(ndev);

  MTLK_ASSERT(NULL != radio);

  ILOG1_SSD("%s: Invoked from %s (%i)", ndev->name, current->comm, current->pid);
  MTLK_CHECK_DF_USER(df_user);

  if (!netif_running(ndev)) {
    WLOG_S("%s: You should bring interface up first", ndev->name);
    res = MTLK_ERR_WRONG_CONTEXT;
    goto finish;
  }

  res = _wave_radio_sta_param_process(ndev, mac, params, &info);
  if (MTLK_ERR_OK != res) {
    ELOG_S("%s: Error parsing parameters", ndev->name);
    res = MTLK_ERR_PARAMS;
    goto finish;
  }

  res = _mtlk_df_user_invoke_core(mtlk_df_user_get_df(df_user), WAVE_CORE_REQ_AP_CONNECT_STA, &clpb, &info, sizeof(info));
  res = _mtlk_df_user_process_core_retval(res, clpb, WAVE_CORE_REQ_AP_CONNECT_STA, TRUE);

finish:
  return _mtlk_df_mtlk_to_linux_error_code(res);
}

int wave_radio_sta_change (
  wave_radio_t *radio,
  struct net_device *ndev,
  const uint8 *mac,
  struct station_parameters *params)
{
  int res = MTLK_ERR_PARAMS;
  mtlk_clpb_t *clpb = NULL;
  mtlk_df_user_t *df_user = mtlk_df_user_from_ndev(ndev);
  mtlk_core_ui_authorizing_cfg_t t_authorizing;

  MTLK_ASSERT(NULL != radio);

  memset(&t_authorizing, 0, sizeof(t_authorizing));

  ILOG1_SSD("%s: Invoked from %s (%i)", ndev->name, current->comm, current->pid);
  MTLK_CHECK_DF_USER(df_user);

  if (!netif_running(ndev)) {
    WLOG_S("%s: You should bring interface up first", ndev->name);
    goto finish;
  }

  if(mac == NULL){
    WLOG_S("%s: MAC addr is not set", ndev->name);
    goto finish;
  }

  /* Use this only for authorizing/unauthorizing a station */
  if (!(params->sta_flags_mask & BIT(NL80211_STA_FLAG_AUTHORIZED))){
    ILOG2_SD("%s: Only authorizing/unauthorizing is supporting, change_flags:0x%x", ndev->name, params->sta_flags_mask);
    res = MTLK_ERR_OK;
    goto finish;
  }

  MTLK_CFG_SET_ITEM_BY_FUNC_VOID(&t_authorizing, sta_addr, mtlk_osal_copy_eth_addresses, (t_authorizing.sta_addr.au8Addr, mac));
  MTLK_CFG_SET_ITEM(&t_authorizing, authorizing, !!(params->sta_flags_set & BIT(NL80211_STA_FLAG_AUTHORIZED)));
  ILOG2_YD("STA:%Y authorizing flag:%d", mac, t_authorizing.authorizing);

  res = _mtlk_df_user_invoke_core(mtlk_df_user_get_df(df_user), WAVE_CORE_REQ_AUTHORIZING_STA, &clpb, &t_authorizing, sizeof(t_authorizing));
  res = _mtlk_df_user_process_core_retval_void(res, clpb, WAVE_CORE_REQ_AUTHORIZING_STA, TRUE);

finish:
  return _mtlk_df_mtlk_to_linux_error_code(res);
}

static const uint8 _bcast_addr[ETH_ALEN] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

int wave_radio_sta_del (
  wave_radio_t *radio,
  struct net_device *ndev,
  const uint8 *mac)
{
  int res = MTLK_ERR_OK;
  mtlk_clpb_t *clpb = NULL;
  mtlk_df_user_t *df_user = mtlk_df_user_from_ndev(ndev);
  const uint8 *addr = mac ? mac : _bcast_addr;

  MTLK_ASSERT(NULL != radio);

  ILOG1_SSD("%s: Invoked from %s (%i)", ndev->name, current->comm, current->pid);
  ILOG1_Y("MAC = %Y", addr);
  MTLK_CHECK_DF_USER(df_user);

  if (!netif_running(ndev)) {
    WLOG_S("%s: You should bring interface up first", ndev->name);
    res = MTLK_ERR_WRONG_CONTEXT;
    goto finish;
  }

  if (mtlk_osal_eth_is_broadcast(addr)) {
    res = _mtlk_df_user_invoke_core(mtlk_df_user_get_df(df_user), WAVE_CORE_REQ_AP_DISCONNECT_ALL, &clpb, NULL, 0);
    res = _mtlk_df_user_process_core_retval_void(res, clpb, WAVE_CORE_REQ_AP_DISCONNECT_ALL, TRUE);
  }
  else {
    if (!mtlk_osal_is_valid_ether_addr(addr)) {
      ELOG_SY("%s: Invalid MAC address: %Y", ndev->name, addr);
      res = MTLK_ERR_PARAMS;
      goto finish;
    }

    res = _mtlk_df_user_invoke_core(mtlk_df_user_get_df(df_user), WAVE_CORE_REQ_AP_DISCONNECT_STA, &clpb, addr, ETH_ALEN);
    res = _mtlk_df_user_process_core_retval(res, clpb, WAVE_CORE_REQ_AP_DISCONNECT_STA, TRUE);
  }

finish:
  return _mtlk_df_mtlk_to_linux_error_code(res);
}

int wave_radio_sta_get (
  wave_radio_t *radio,
  struct net_device *ndev,
  const uint8 *mac,
  struct station_info *sinfo)
{
  int res = MTLK_ERR_OK;
  st_info_data_t info_data;
  mtlk_df_user_t *df_user = mtlk_df_user_from_ndev(ndev);
  mtlk_clpb_t *clpb = NULL;

  MTLK_ASSERT(NULL != sinfo);
  MTLK_ASSERT(NULL != mac);
  MTLK_ASSERT(NULL != radio);

  ILOG1_SSD("%s: Invoked from %s (%i)", ndev->name, current->comm, current->pid);
  ILOG1_Y("MAC = %Y", mac);
  MTLK_CHECK_DF_USER(df_user);

  if (!mtlk_osal_is_valid_ether_addr(mac)) {
    ELOG_SY("%s: Invalid MAC address: %Y", ndev->name, mac);
    res = MTLK_ERR_PARAMS;
    goto finish;
  }

  info_data.mac = mac;
  info_data.stinfo = sinfo;
  res = _mtlk_df_user_invoke_core(mtlk_df_user_get_df(df_user), WAVE_CORE_REQ_GET_STATION, &clpb, &info_data, sizeof(st_info_data_t));
  res = _mtlk_df_user_process_core_retval(res, clpb, WAVE_CORE_REQ_GET_STATION, TRUE);

finish:
  return _mtlk_df_mtlk_to_linux_error_code(res);
}

int wave_radio_bss_change (
  wave_radio_t *radio,
  struct net_device *ndev,
  struct bss_parameters *params)
{
  int res = MTLK_ERR_OK;
  mtlk_df_user_t *df_user = mtlk_df_user_from_ndev(ndev);
  mtlk_df_t *df;
  mtlk_clpb_t *clpb = NULL;
  struct mtlk_bss_parameters bp;
  mtlk_df_t *master_df;

  MTLK_ASSERT(NULL != radio);

  master_df = mtlk_vap_manager_get_master_df(radio->vap_manager);
  MTLK_ASSERT(NULL != master_df);

  ILOG1_SSD("%s: Invoked from %s (%i)", ndev->name, current->comm, current->pid);
  MTLK_CHECK_DF_USER(df_user);
  df = mtlk_df_user_get_df(df_user);

  memset(&bp, 0, sizeof(bp));
  bp.vap_id = mtlk_vap_get_id(mtlk_df_get_vap_handle(df));
  bp.use_cts_prot = params->use_cts_prot;
  bp.use_short_preamble = params->use_short_preamble;
  bp.use_short_slot_time = params->use_short_slot_time;
  bp.ap_isolate = params->ap_isolate;
  bp.ht_opmode = params->ht_opmode;
  bp.p2p_ctwindow = params->p2p_ctwindow;
  bp.p2p_opp_ps = params->p2p_opp_ps;
  bp.basic_rates_len = params->basic_rates_len;
  wave_memcpy(bp.basic_rates, sizeof(bp.basic_rates), params->basic_rates, bp.basic_rates_len);

  /* Keep trying in case scan is in progress for the current radio */
  {
    int retry_counter = 0;

    do {
      res = _mtlk_df_user_invoke_core(master_df, WAVE_CORE_REQ_CHANGE_BSS, &clpb, &bp, sizeof(bp));
      res = _mtlk_df_user_process_core_retval(res, clpb, WAVE_CORE_REQ_CHANGE_BSS, TRUE);

      if (MTLK_ERR_RETRY != res) break;
      mtlk_osal_msleep(50);
      retry_counter++;
    } while ((MTLK_ERR_RETRY == res) && (retry_counter < MAX_SCAN_WAIT_RETRIES));

    if (retry_counter > 0)
      ILOG0_SD("%s: Scan waited, number of retries %d", ndev->name, retry_counter);
  }

  return _mtlk_df_mtlk_to_linux_error_code(res);
}

int wave_radio_mgmt_tx (
  wave_radio_t *radio,
  struct net_device *ndev,
  struct ieee80211_channel *chan,
  bool offchan,
  unsigned int wait, /* duration for remaining on channel in case it gets switched */
  const uint8 *buf,
  size_t len,
  bool no_cck,
  bool dont_wait_for_ack,
  uint64 *cookie)
{
  mtlk_df_user_t *df_user = mtlk_df_user_from_ndev(ndev);
  int in_chan;
  struct mgmt_tx_params mtp;
  int res = MTLK_ERR_OK;
  mtlk_clpb_t *clpb = NULL;

  MTLK_CHECK_DF_USER(df_user);

  CPU_STAT_BEGIN_TRACK(CPU_STAT_ID_TX_BSS);
  MTLK_ASSERT(NULL != radio);

  if (!chan) {
    res = MTLK_ERR_UNKNOWN;
    goto finish;
  }

  in_chan = ieee80211_frequency_to_channel(chan->center_freq);

  if (!netif_running(ndev)) {
    ILOG0_V("You should bring interface up first");
    goto finish;
  }

  ILOG3_DDDPDDD("in_chan=%i, offchan=%u, wait=%u, "
             "buf=%p, len=%u, no_cck=%u, dont_wait_for_ack=%u",
             in_chan, offchan, wait, buf, len, no_cck, dont_wait_for_ack);

  memset(&mtp, 0, sizeof(mtp));
  mtp.buf = buf;
  mtp.len = len;
  mtp.channum = in_chan;
  mtp.cookie = cookie;
  mtp.no_cck = no_cck;
  mtp.dont_wait_for_ack = dont_wait_for_ack;
  mtp.extra_processing = PROCESS_MANAGEMENT;

  res = _mtlk_df_user_invoke_core(mtlk_df_user_get_df(df_user), WAVE_CORE_REQ_MGMT_TX, &clpb, &mtp, sizeof(mtp));
  res = _mtlk_df_user_process_core_retval(res, clpb, WAVE_CORE_REQ_MGMT_TX, TRUE);

finish:
  CPU_STAT_END_TRACK(CPU_STAT_ID_TX_BSS);
  return _mtlk_df_mtlk_to_linux_error_code(res);
}

static char* _wave_radio_mgmt_frametype_to_str (
  uint16 frame_type)
{
  switch (frame_type) {
    case IEEE80211_STYPE_ASSOC_REQ:
      return "ASSOC_REQ";
    case IEEE80211_STYPE_ASSOC_RESP:
      return "ASSOC_RESP";
    case IEEE80211_STYPE_REASSOC_REQ:
      return "REASSOC_REQ";
    case IEEE80211_STYPE_REASSOC_RESP:
      return "REASSOC_RESP";
    case IEEE80211_STYPE_PROBE_REQ:
      return "PROBE_REQ";
    case IEEE80211_STYPE_PROBE_RESP:
      return "PROBE_RESP";
    case IEEE80211_STYPE_BEACON:
      return "BEACON";
    case IEEE80211_STYPE_ATIM:
      return "ATIM";
    case IEEE80211_STYPE_DISASSOC:
      return "DISASSOC";
    case IEEE80211_STYPE_AUTH:
      return "AUTH";
    case IEEE80211_STYPE_DEAUTH:
      return "DEAUTH";
    case IEEE80211_STYPE_ACTION:
      return "ACTION";
    default:
      ILOG0_D("Unknown type 0x%04X", (uint32)frame_type);
  }
  return "Unknown type";
}

void wave_radio_mgmt_frame_register(
  wave_radio_t *radio,
  struct net_device *ndev,
  uint16 frame_type,
  bool reg)
{
  mtlk_df_user_t *df_user = mtlk_df_user_from_ndev(ndev);
  char *s_ftype;

  s_ftype = _wave_radio_mgmt_frametype_to_str(frame_type);

  MTLK_ASSERT(NULL != radio);

  ILOG1_SSD("%s: Invoked from %s (%i)", ndev->name, current->comm, current->pid);
  ILOG1_SD("frame_type = %s, reg = %d", s_ftype, (uint32)reg);
  MTLK_CHECK_DF_USER_NORES(df_user);
  mtlk_df_user_set_mngmnt_filter(df_user, (frame_type & FRAME_SUBTYPE_MASK) >> FRAME_SUBTYPE_SHIFT, reg);
}

int wave_radio_scan (
  wave_radio_t *radio,
  struct cfg80211_scan_request *request)
{
  mtlk_clpb_t *clpb = NULL;
  mtlk_df_t *master_df;
  struct mtlk_scan_request *sr;
  int res = MTLK_ERR_OK;
  int i;
  void *wiphy;
  mtlk_core_t *core;

  MTLK_ASSERT(NULL != radio);
  wiphy = wv_cfg80211_wiphy_get(radio->cfg80211);
  MTLK_ASSERT(NULL != wiphy);
  master_df = mtlk_vap_manager_get_master_df(radio->vap_manager);
  core = mtlk_vap_get_core(mtlk_df_get_vap_handle(master_df));

  ILOG1_SSDPP("%s: Invoked from %s (%i): wihpy=%p, request=%p",
              mtlk_df_user_get_name(mtlk_df_get_user(master_df)), current->comm, current->pid, wiphy, request);

  if (request->n_channels > NUM_TOTAL_CHANS
      || request->n_ssids > MAX_SCAN_SSIDS
      || request->ie_len > MAX_SCAN_IE_LEN)
  {
    ELOG_V("Maximum number of channels, ssids or extra IE length exceeded");
    return _mtlk_df_mtlk_to_linux_error_code(MTLK_ERR_VALUE);
  }

  /* scan request is too big to put on the stack */
  if (!(sr = mtlk_osal_mem_alloc(sizeof(*sr), MTLK_MEM_TAG_CFG80211)))
  {
    ELOG_V("cfg80211: can't allocate memory");
    return _mtlk_df_mtlk_to_linux_error_code(MTLK_ERR_NO_MEM);
  }

  memset(sr, 0, sizeof(*sr));
  sr->saved_request = request;
  sr->type = MTLK_SCAN_AP;
  sr->wiphy = wiphy;
  sr->n_channels = request->n_channels;
  core_cfg_country_code_get(core, &sr->country_code);

  for (i = 0; i < sr->n_channels; i++)
  {
    struct mtlk_channel *mc = &sr->channels[i];
    struct ieee80211_channel *ic = request->channels[i];

    mc->dfs_state_entered = ic->dfs_state_entered;
    mc->dfs_state = ic->dfs_state;
    mc->dfs_cac_ms = ic->dfs_cac_ms;
    mc->band = nlband2mtlkband(ic->band);
    mc->center_freq = ic->center_freq;
    mc->flags = ic->flags;
    mc->orig_flags = ic->orig_flags;
    mc->max_antenna_gain = ic->max_antenna_gain;
    mc->max_power = ic->max_power;
    mc->max_reg_power = ic->max_reg_power;
  }

  sr->flags = request->flags;

  for (i = 0; i < NUM_SUPPORTED_BANDS; i++)
    sr->rates[nlband2mtlkband(i)] = request->rates[i];

  sr->n_ssids = request->n_ssids;

  MTLK_STATIC_ASSERT(MIB_ESSID_LENGTH >= IEEE80211_MAX_SSID_LEN);

  for (i = 0; i < sr->n_ssids; i++)
  {
    /* in case SSID-s with '\0'-s in the middle are acceptable, we'll need to keep the len, too */
    wave_strncopy(sr->ssids[i], request->ssids[i].ssid, sizeof(sr->ssids[i]), request->ssids[i].ssid_len);
  }

  sr->ie_len = request->ie_len;
  /* request->ie and request->ie_len can be NULL */
  if (request->ie_len) {
    wave_memcpy(sr->ie, sizeof(sr->ie), request->ie, request->ie_len);
  }

  /* Keep trying in case scan is in progress for the current radio */
  {
    int retry_counter = 0;

    do {
      /* any checks for preexisting scans done later to minimize locking */
      res = _mtlk_df_user_invoke_core(master_df, WAVE_RADIO_REQ_DO_SCAN, &clpb, sr, sizeof(*sr));
      res = _mtlk_df_user_process_core_retval(res, clpb, WAVE_RADIO_REQ_DO_SCAN, TRUE);

      if (MTLK_ERR_RETRY != res) break;
      mtlk_osal_msleep(50);
      retry_counter++;
    } while ((MTLK_ERR_RETRY == res) && (retry_counter < MAX_SCAN_WAIT_RETRIES));

    if (retry_counter > 0)
      ILOG0_SD("%s: Scan waited, number of retries %d", mtlk_df_user_get_name(mtlk_df_get_user(master_df)), retry_counter);
  }

  /* no waiting for the end of the scan, just return and let the kernel report the scan as started */
  mtlk_osal_mem_free(sr);
  return _mtlk_df_mtlk_to_linux_error_code(res);
}

int wave_radio_sched_scan_start(
    wave_radio_t *radio,
    struct net_device *ndev,
    struct cfg80211_sched_scan_request *request)
{
  mtlk_df_user_t *df_user = mtlk_df_user_from_ndev(ndev);
  mtlk_clpb_t *clpb = NULL;
  struct mtlk_scan_request *sr;
  int res = MTLK_ERR_OK;
  int i;
  void *wiphy;

  MTLK_ASSERT(NULL != radio);
  wiphy = wv_cfg80211_wiphy_get(radio->cfg80211);
  MTLK_ASSERT(NULL != wiphy);

  ILOG1_SSD("%s: Invoked from %s (%i)", ndev->name, current->comm, current->pid);
  MTLK_CHECK_DF_USER(df_user);

  if (request->n_channels > NUM_TOTAL_CHANS
      || request->n_ssids > MAX_SCAN_SSIDS
      || request->ie_len > MAX_SCAN_IE_LEN)
  {
    ELOG_V("Maximum number of channels, ssids or extra IE length exceeded");
    return _mtlk_df_mtlk_to_linux_error_code(MTLK_ERR_VALUE);
  }

  /* scan request is too big to put on the stack */
  if (!(sr = mtlk_osal_mem_alloc(sizeof(*sr), MTLK_MEM_TAG_CFG80211)))
  {
    ELOG_V("cfg80211: can't allocate memory");
    return _mtlk_df_mtlk_to_linux_error_code(MTLK_ERR_NO_MEM);
  }

  memset(sr, 0, sizeof(*sr));
  sr->saved_request = request;
  sr->type = MTLK_SCAN_SCHED_AP;
  sr->wiphy = wiphy;
  sr->n_channels = request->n_channels;

  for (i = 0; i < sr->n_channels; i++)
  {
    struct mtlk_channel *mc = &sr->channels[i];
    struct ieee80211_channel *ic = request->channels[i];

    mc->dfs_state_entered = ic->dfs_state_entered;
    mc->dfs_state = ic->dfs_state;
    mc->dfs_cac_ms = ic->dfs_cac_ms;
    mc->band = nlband2mtlkband(ic->band);
    mc->center_freq = ic->center_freq;
    mc->flags = ic->flags;
    mc->orig_flags = ic->orig_flags;
    mc->max_antenna_gain = ic->max_antenna_gain;
    mc->max_power = ic->max_power;
    mc->max_reg_power = ic->max_reg_power;
  }

  sr->flags = request->flags;
  sr->interval = request->scan_plans->interval; /* TODO: There can be multiple scan_plans */
  sr->min_rssi_thold = request->min_rssi_thold;
  sr->n_match_sets = request->n_match_sets;

  MTLK_STATIC_ASSERT(MIB_ESSID_LENGTH >= IEEE80211_MAX_SSID_LEN);

  for (i = 0; i < sr->n_match_sets; i++)
  {
    size_t len = MIN(request->match_sets[i].ssid.ssid_len, IEEE80211_MAX_SSID_LEN);
    wave_memcpy(sr->match_sets[i].ssid, sizeof(sr->match_sets[i].ssid), request->match_sets[i].ssid.ssid, len);
    sr->match_sets[i].ssid_len = len;
    sr->match_sets[i].rssi_thold = request->match_sets[i].rssi_thold;
  }

  sr->n_ssids = request->n_ssids;

  for (i = 0; i < sr->n_ssids; i++)
  {
    /* in case SSID-s with '\0'-s in the middle are acceptable, we'll need to keep the len, too */
    wave_strncopy(sr->ssids[i], request->ssids[i].ssid, sizeof(sr->ssids[i]), request->ssids[i].ssid_len);
  }

  sr->ie_len = request->ie_len;
  wave_memcpy(sr->ie, sizeof(sr->ie), request->ie, sr->ie_len);

  /* any checks for preexisting scans done later to minimize locking */
  res = _mtlk_df_user_invoke_core(mtlk_df_user_get_df(df_user), WAVE_RADIO_REQ_START_SCHED_SCAN, &clpb, &sr, sizeof(sr)); /* synchronous, pass the ptr!!! */
  res = _mtlk_df_user_process_core_retval(res, clpb, WAVE_RADIO_REQ_START_SCHED_SCAN, TRUE);

  /* don't free sr if successfully started; it will get freed when sched scan gets stopped or VAP removed */
  if (res != MTLK_ERR_OK)
    mtlk_osal_mem_free(sr);

  return _mtlk_df_mtlk_to_linux_error_code(res);
}

int wave_radio_sched_scan_stop(
  wave_radio_t *radio,
  struct net_device *ndev,
  u64 reqid)
{
  mtlk_df_user_t *df_user = mtlk_df_user_from_ndev(ndev);
  mtlk_clpb_t *clpb = NULL;
  int res = MTLK_ERR_OK;
  void *wiphy;

  MTLK_ASSERT(NULL != radio);
  wiphy = wv_cfg80211_wiphy_get(radio->cfg80211);
  MTLK_ASSERT(NULL != wiphy);

  ILOG1_SSD("%s: Invoked from %s (%i)", ndev->name, current->comm, current->pid);
  MTLK_CHECK_DF_USER(df_user);

  res = _mtlk_df_user_invoke_core(mtlk_df_user_get_df(df_user), WAVE_RADIO_REQ_STOP_SCHED_SCAN, &clpb, NULL, 0); /* synchronous, pass the ptr!!! */
  res = _mtlk_df_user_process_core_retval(res, clpb, WAVE_RADIO_REQ_STOP_SCHED_SCAN, TRUE);

  cfg80211_sched_scan_stopped(wiphy, reqid);

  return _mtlk_df_mtlk_to_linux_error_code(res);
}

int wave_radio_wiphy_params_set (
  struct wiphy *wiphy,
  wave_radio_t *radio,
  uint32 changed)
{
  int res = MTLK_ERR_OK;
  mtlk_df_user_t *df_user;

  MTLK_ASSERT(NULL != radio);

  df_user = mtlk_df_get_user(mtlk_vap_manager_get_master_df(radio->vap_manager));
  MTLK_CHECK_DF_USER(df_user);

  ILOG1_SSD("%s: Invoked from %s (%i)", mtlk_df_user_get_name(df_user), current->comm, current->pid);
  ILOG1_D("changed = 0x%X", changed);

  /* ignore WIPHY_PARAM_RTS_THRESHOLD because it is hardcoded in FW */
  /* ignore WIPHY_PARAM_FRAG_THRESHOLD because it is hardcoded in FW */
  /* ignore WIPHY_PARAM_RETRY_SHORT because it is hardcoded in FW */

  if (changed & WIPHY_PARAM_RETRY_LONG) {
    uint32 long_retry_limit = wiphy->retry_long;
    res = mtlk_df_ui_set_cfg(df_user, MIB_LONG_RETRY_LIMIT, (char *)&long_retry_limit, MTLK_IW_PRIW_PARAM(INT_ITEM,1));
  }

  return _mtlk_df_mtlk_to_linux_error_code(res);
}

int wave_radio_client_probe (
  wave_radio_t *radio,
  struct net_device *ndev,
  const uint8 *peer,
  uint64 *cookie)
{
  mtlk_df_user_t *df_user = mtlk_df_user_from_ndev(ndev);
  mtlk_vap_handle_t vap_handle;
  mtlk_clpb_t *clpb = NULL;
  int res = MTLK_ERR_OK;
  struct mgmt_tx_params mtp;
  mtlk_nbuf_t *nbuf_ndp;
  frame_head_t *wifi_header;

  MTLK_ASSERT(radio != NULL);

  ILOG1_SSD("%s: Invoked from %s (%i)", ndev->name, current->comm, current->pid);
  MTLK_CHECK_DF_USER(df_user);

  nbuf_ndp = mtlk_df_nbuf_alloc(sizeof(frame_head_t));
  if (!nbuf_ndp)
  {
    WLOG_S("%s: Unable to allocate buffer for Null Data Packet", ndev->name);
    res = MTLK_ERR_UNKNOWN;
    goto FINISH;
  }

  vap_handle = mtlk_df_get_vap_handle(mtlk_df_user_get_df(df_user));

  wifi_header = (frame_head_t *) nbuf_ndp->data;
  wifi_header->frame_control = HOST_TO_WLAN16(IEEE80211_STYPE_NULLFUNC | IEEE80211_FTYPE_DATA);
  wifi_header->duration = 0;
  ieee_addr_set(&wifi_header->dst_addr, peer);
  wave_pdb_get_mac(mtlk_vap_get_param_db(vap_handle), PARAM_DB_CORE_BSSID, &wifi_header->src_addr);
  wave_pdb_get_mac(mtlk_vap_get_param_db(vap_handle), PARAM_DB_CORE_BSSID, &wifi_header->bssid);
  wifi_header->seq_control = 0;

  memset(&mtp, 0, sizeof(mtp));
  mtp.buf = nbuf_ndp->data;
  mtp.len = sizeof(frame_head_t);
  mtp.cookie = cookie;
  mtp.dont_wait_for_ack = FALSE;
  mtp.extra_processing = PROCESS_NULL_DATA_PACKET;

  res = _mtlk_df_user_invoke_core(mtlk_df_user_get_df(df_user), WAVE_CORE_REQ_MGMT_TX, &clpb, &mtp, sizeof(mtp));
  res = _mtlk_df_user_process_core_retval(res, clpb, WAVE_CORE_REQ_MGMT_TX, TRUE);

  mtlk_df_nbuf_free(nbuf_ndp);

FINISH:
  return _mtlk_df_mtlk_to_linux_error_code(res);
}

int wave_radio_key_add (
  wave_radio_t *radio,
  struct net_device *ndev,
  uint8 key_index,
  bool pairwise,
  const uint8 *mac_addr,
  struct key_params *params)
{
  int res = MTLK_ERR_PARAMS;
  mtlk_df_user_t *df_user = mtlk_df_user_from_ndev(ndev);
  mtlk_clpb_t *clpb = NULL;
  mtlk_core_ui_encext_cfg_t encext_cfg;

  MTLK_ASSERT(radio != NULL);

  ILOG1_SSD("%s: Invoked from %s (%i)", ndev->name, current->comm, current->pid);
  MTLK_CHECK_DF_USER(df_user);

  if (key_index >= MIB_WEP_N_DEFAULT_KEYS) {
    ILOG2_D("Invalid key index %d ignored", key_index);
    res = MTLK_ERR_OK;
    goto finish;
  }

  memset(&encext_cfg, 0, sizeof(encext_cfg));

  if (params != NULL) {
    ILOG1_DDDDD("pairwise= %d key_length = %d key_index = %d seq_len = %d cipher = %08x", pairwise, params->key_len,
                 key_index, params->seq_len, params->cipher);
  }
  else {
    WLOG_S("%s: params are not defined", ndev->name);
    res = MTLK_ERR_PARAMS;
    goto finish;
  }

  /* Validate pairwise flag */
  if(!mac_addr)
  {
    if(pairwise)
    {
      WLOG_S("%s: MAC_ADDR is not defined and pairwise is set", ndev->name);
      res = MTLK_ERR_PARAMS;
      goto finish;
    }

    mac_addr = _bcast_addr;
  }
  ILOG2_Y("mac_addr = %Y", mac_addr);
  ieee_addr_set(&encext_cfg.sta_addr, mac_addr);

  switch (params->cipher){
    case WLAN_CIPHER_SUITE_WEP40:
    case WLAN_CIPHER_SUITE_WEP104:
      /* Validate WEP key */
      if (mtlk_df_ui_validate_wep_key(params->key, params->key_len) != MTLK_ERR_OK) {
        ELOG_S("%s: Invalid WEP key", ndev->name);
        res = MTLK_ERR_PARAMS;
        goto finish;
      }

      /* Validate key index */
      if (key_index >= MIB_WEP_N_DEFAULT_KEYS) {
        ELOG_D("Invalid WEP key index %d", key_index);
        res = MTLK_ERR_PARAMS;
        goto finish;
      }

      encext_cfg.alg_type = IW_ENCODE_ALG_WEP;
      break;

    case WLAN_CIPHER_SUITE_TKIP:
      /* Validate key index */
      if ((key_index > 2) || (pairwise & (key_index !=0))) {
        ELOG_D("Invalid TKIP key index %d", key_index);
        res = MTLK_ERR_PARAMS;
        goto finish;
      }

      encext_cfg.alg_type = IW_ENCODE_ALG_TKIP;
      break;

    case WLAN_CIPHER_SUITE_CCMP:
      /* Validate key index */
      if ((key_index > 2) || (pairwise & (key_index !=0))) {
        ELOG_D("Invalid CCMP key index %d", key_index);
        res = MTLK_ERR_PARAMS;
        goto finish;
      }

      encext_cfg.alg_type = IW_ENCODE_ALG_CCMP;
      break;

    case WLAN_CIPHER_SUITE_AES_CMAC:
      /* Validate key index */
      if ((key_index > 2) || (pairwise & (key_index !=0))) {
        ELOG_D("Invalid AES_CMAC key index %d", key_index);
        res = MTLK_ERR_PARAMS;
        goto finish;
      }

      encext_cfg.alg_type = IW_ENCODE_ALG_AES_CMAC;
      break;

    case WLAN_CIPHER_SUITE_GCMP:
      /* Validate platform */
      if (!hw_get_gcmp_support(HANDLE_T(radio->hw_api->hw))) {
        ELOG_D("Cipher 0x%08X is not supported on this platform" , params->cipher);
        res = MTLK_ERR_NOT_SUPPORTED;
        goto finish;
      }
      /* Validate key index */
      else if ((key_index > 2) || (pairwise & (key_index !=0))) {
        ELOG_D("Invalid GCMP key index %d", key_index);
        res = MTLK_ERR_PARAMS;
        goto finish;
      }

      encext_cfg.alg_type = IW_ENCODE_ALG_GCMP;
      break;

    case WLAN_CIPHER_SUITE_GCMP_256:
      /* Validate platform */
      if (!hw_get_gcmp_support(HANDLE_T(radio->hw_api->hw))) {
        ELOG_D("Cipher 0x%08X is not supported on this platform" , params->cipher);
        res = MTLK_ERR_NOT_SUPPORTED;
        goto finish;
      }
      /* Validate key index */
      else if ((key_index > 2) || (pairwise & (key_index !=0))) {
        ELOG_D("Invalid GCMP256 key index %d", key_index);
        res = MTLK_ERR_PARAMS;
        goto finish;
      }

      encext_cfg.alg_type = IW_ENCODE_ALG_GCMP_256;
      break;

    default:
      res = MTLK_ERR_PARAMS;
      goto finish;
  }

  /* Validate and set the key lenght */
  if (((UMI_RSN_TK1_LEN + UMI_RSN_TK2_LEN) < params->key_len) || (params->key_len == 0)) {
    WLOG_SD("%s: Invalid key length %u", ndev->name, params->key_len);
    res = MTLK_ERR_PARAMS;
    goto finish;
  }
  encext_cfg.key_len = params->key_len;
  ILOG2_D("key: key_len (%u)", params->key_len);

  /* Set key index */
  encext_cfg.key_idx = key_index;
  ILOG2_D("key: key_idx (%u)", key_index);

  /* Save the key */
  wave_memcpy(encext_cfg.key, sizeof(encext_cfg.key), params->key, params->key_len);

  /* Determine and validate the RX seq */
  if(params->seq_len) {
    if (params->seq_len != MTLK_ARRAY_SIZE(encext_cfg.rx_seq)) {
      WLOG_SD("%s: Wrong seq_len:%d", ndev->name, params->seq_len);
      res = MTLK_ERR_PARAMS;
      goto finish;
    }
    wave_memcpy(encext_cfg.rx_seq, sizeof(encext_cfg.rx_seq), params->seq, sizeof(encext_cfg.rx_seq));
    mtlk_dump(2, params->seq, sizeof(encext_cfg.rx_seq), "group RSC");
  }

  res = _mtlk_df_user_invoke_core(mtlk_df_user_get_df(df_user), WAVE_CORE_REQ_SET_ENCEXT_CFG, &clpb, (char*)&encext_cfg, sizeof(encext_cfg));
  res = _mtlk_df_user_process_core_retval_void(res, clpb, WAVE_CORE_REQ_SET_ENCEXT_CFG, TRUE);

finish:
  return _mtlk_df_mtlk_to_linux_error_code(res);
}

int wave_radio_key_get(
  wave_radio_t *radio,
  struct net_device *ndev,
  uint8 key_index,
  bool pairwise,
  const uint8 *mac_addr,
  void *cookie,
  void(*callback)(void *cookie, struct key_params*))
{
  int                        res = MTLK_ERR_PARAMS;
  mtlk_df_user_t            *df_user = mtlk_df_user_from_ndev(ndev);
  mtlk_clpb_t               *clpb = NULL;
  uint32                     size;
  mtlk_core_ui_group_pn_t    ui_gpn_tx;
  mtlk_core_ui_group_pn_t    *ui_gpn;
  struct key_params          params;

  MTLK_ASSERT(radio != NULL);

  ILOG1_SSD("%s: Invoked from %s (%i)", ndev->name, current->comm, current->pid);
  MTLK_CHECK_DF_USER(df_user);

  ILOG2_DD("pairwise = %d key_index = %d ", pairwise, key_index);
  mac_addr = mac_addr ? mac_addr : _bcast_addr;
  ILOG2_Y(" mac_addr = %Y", mac_addr);

  memset(&params, 0, sizeof(params));
  memset(&ui_gpn_tx, 0, sizeof(mtlk_core_ui_group_pn_t));

  if (pairwise){
    WLOG_SD("%s: pairwise not supported:%d", ndev->name, key_index);
    res = MTLK_ERR_PARAMS;
    goto finish;
  }

  if (key_index >= MIB_WEP_N_DEFAULT_KEYS) {
    /* use ILOG instead of WLOG, because this error is expected. We are not
       supporting BIP */
    ILOG2_SD("%s: KEY index is out of range:%d", ndev->name, key_index);
    res = MTLK_ERR_PARAMS;
    goto finish;
  }

  ui_gpn_tx.key_idx = key_index;

  res = _mtlk_df_user_invoke_core(mtlk_df_user_get_df(df_user), WAVE_CORE_REQ_GET_ENCEXT_CFG, &clpb, (char*)&ui_gpn_tx, sizeof(mtlk_core_ui_group_pn_t));
  res = _mtlk_df_user_process_core_retval_void(res, clpb, WAVE_CORE_REQ_GET_ENCEXT_CFG, FALSE);
  if (MTLK_ERR_OK != res) {
    goto finish;
  }

  ui_gpn = mtlk_clpb_enum_get_next(clpb, &size);
  if ((NULL == ui_gpn) || (sizeof(*ui_gpn) != size)) {
    res = MTLK_ERR_UNKNOWN;
    goto delete_clpb;
  }

  mtlk_dump(2, ui_gpn->seq, ui_gpn->seq_len, "RSC:");
  mtlk_dump(2, ui_gpn->key, ui_gpn->key_len, "KEY:");

  params.seq = ui_gpn->seq;
  params.seq_len = ui_gpn->seq_len;
  params.key = ui_gpn->key;
  params.key_len = ui_gpn->key_len;

  callback(cookie, &params);

delete_clpb:
  mtlk_clpb_delete(clpb);
finish:
  return _mtlk_df_mtlk_to_linux_error_code(res);
}

int wave_radio_default_key_set (
  wave_radio_t *radio,
  struct net_device *ndev,
  uint8 key_index,
  bool unicast,
  bool multicast)
{
  int res = MTLK_ERR_PARAMS;
  mtlk_df_user_t *df_user = mtlk_df_user_from_ndev(ndev);
  mtlk_clpb_t *clpb = NULL;
  mtlk_core_ui_default_key_cfg_t default_key_cfg;

  MTLK_ASSERT(radio != NULL);

  ILOG1_SSD("%s: Invoked from %s (%i)", ndev->name, current->comm, current->pid);
  MTLK_CHECK_DF_USER(df_user);
  ILOG1_DDD("key index:%d unicast:%d multicast:%d", key_index, unicast, multicast);

  if (!multicast) {
    ELOG_S("%s:Wrong param only multicast is supported", ndev->name);
    res = MTLK_ERR_PARAMS;
    goto finish;
  }

  /*check default key index */
  if (key_index >= MIB_WEP_N_DEFAULT_KEYS) {
    ELOG_D("Invalid WEP key index %d", key_index);
    res = MTLK_ERR_PARAMS;
    goto finish;
  }

  memset(&default_key_cfg, 0, sizeof(default_key_cfg));
  ieee_addr_set(&default_key_cfg.sta_addr, _bcast_addr);
  default_key_cfg.key_idx = key_index;

  res = _mtlk_df_user_invoke_core(mtlk_df_user_get_df(df_user), WAVE_CORE_REQ_SET_DEFAULT_KEY_CFG, &clpb, (char*)&default_key_cfg, sizeof(default_key_cfg));
  res = _mtlk_df_user_process_core_retval_void(res, clpb, WAVE_CORE_REQ_SET_DEFAULT_KEY_CFG, TRUE);

finish:
  return _mtlk_df_mtlk_to_linux_error_code(res);
}

int wave_radio_default_mgmt_key_set (
    wave_radio_t *radio,
    struct net_device *netdev,
    u8 key_index)
{
  int                            res;
  mtlk_core_ui_default_key_cfg_t default_key_cfg;
  mtlk_df_user_t                 *df_user = mtlk_df_user_from_ndev(netdev);
  mtlk_clpb_t                    *clpb = NULL;

  MTLK_ASSERT(radio != NULL);

  ILOG1_SSD("%s: Invoked from %s (%i)", netdev->name, current->comm, current->pid);
  MTLK_CHECK_DF_USER(df_user);
  ILOG1_D("key index:%d", key_index);

  if (key_index >= MIB_WEP_N_DEFAULT_KEYS) {
    ILOG2_D("Invalid mgmt key index %d ignored", key_index);
    res = MTLK_ERR_OK;
    goto finish;
  }

  memset(&default_key_cfg, 0, sizeof(default_key_cfg));
  ieee_addr_set(&default_key_cfg.sta_addr, _bcast_addr);
  default_key_cfg.key_idx = key_index;
  default_key_cfg.is_mgmt_key = TRUE;

  res = _mtlk_df_user_invoke_core(mtlk_df_user_get_df(df_user), WAVE_CORE_REQ_SET_DEFAULT_KEY_CFG, &clpb, (char*)&default_key_cfg, sizeof(default_key_cfg));
  res = _mtlk_df_user_process_core_retval_void(res, clpb, WAVE_CORE_REQ_SET_DEFAULT_KEY_CFG, TRUE);

finish:
  return _mtlk_df_mtlk_to_linux_error_code(res);
}

int wave_radio_txq_params_set (
  wave_radio_t *radio,
  struct net_device *ndev,
  struct ieee80211_txq_params *params)
{
  mtlk_df_t *master_df;
  mtlk_clpb_t *clpb = NULL;
  struct mtlk_wmm_settings wmm_settings;
  int res = MTLK_ERR_OK;

  MTLK_ASSERT(radio != NULL);
  master_df = mtlk_vap_manager_get_master_df(radio->vap_manager);
  MTLK_ASSERT(master_df != NULL);

  ILOG1_SSD("%s: Invoked from %s (%i)",
            mtlk_df_user_get_name(mtlk_df_get_user(master_df)), current->comm, current->pid);

  wmm_settings.vap_id = mtlk_vap_get_id(mtlk_df_get_vap_handle(master_df));
  wmm_settings.txq_params.txop = params->txop;
  wmm_settings.txq_params.cwmin = params->cwmin;
  wmm_settings.txq_params.cwmax = params->cwmax;
  wmm_settings.txq_params.aifs = params->aifs;
  wmm_settings.txq_params.acm_flag = 0;/* ACM field in FW is relevant for station mode interface only */
  wmm_settings.txq_params.ac = nlac2mtlkac(params->ac);

  /* Keep trying in case scan is in progress for the current radio */
  {
    int retry_counter = 0;

    do {
      /* we could have saved these params away directly and just call the serializer on the 4th,
       * but we don't care and call it for each invocation */
      res = _mtlk_df_user_invoke_core(master_df, WAVE_CORE_REQ_SET_WMM_PARAM, &clpb, &wmm_settings, sizeof(wmm_settings));
      res = _mtlk_df_user_process_core_retval(res, clpb, WAVE_CORE_REQ_SET_WMM_PARAM, TRUE);

      if (MTLK_ERR_RETRY != res) break;
      mtlk_osal_msleep(50);
      retry_counter++;
    } while ((MTLK_ERR_RETRY == res) && (retry_counter < MAX_SCAN_WAIT_RETRIES));

    if (retry_counter > 0)
      ILOG0_SD("%s: Scan waited, number of retries %d", mtlk_df_user_get_name(mtlk_df_get_user(master_df)), retry_counter);
  }

  return _mtlk_df_mtlk_to_linux_error_code(res);
}

int wave_radio_survey_dump(
    struct wiphy *wiphy,
    wave_radio_t *radio,
    struct net_device *ndev,
    int idx,
    struct survey_info *info)
{
  mtlk_df_t *master_df;
  mtlk_clpb_t *clpb = NULL;
  struct mtlk_channel_status cs;
  int res = MTLK_ERR_OK;

  MTLK_ASSERT(radio != NULL);
  master_df = mtlk_vap_manager_get_master_df(radio->vap_manager);
  MTLK_ASSERT(master_df != NULL);

  ILOG1_SSDD("%s: Invoked from %s (%i), idx=%i",
             mtlk_df_user_get_name(mtlk_df_get_user(master_df)), current->comm, current->pid, idx);
  memset(&cs, 0, sizeof(cs));

  /* this is a synchronous call, no serializer involved */
  res = _mtlk_df_user_invoke_core(master_df, WAVE_RADIO_REQ_DUMP_SURVEY, &clpb, &idx, sizeof(idx));
  res = _mtlk_df_user_process_core_retval_void(res, clpb, WAVE_RADIO_REQ_DUMP_SURVEY, FALSE);

  if (MTLK_ERR_OK == res)
  {
    struct mtlk_channel_status *cs;
    uint32 cs_size;

    cs = mtlk_clpb_enum_get_next(clpb, &cs_size);

    MTLK_ASSERT(NULL != cs);
    MTLK_ASSERT(sizeof(*cs) == cs_size);

    if (!cs || !cs->channel)
    {
      mtlk_clpb_delete(clpb); /* already deleted in error cases */
      return -ENOENT;
    }

    info->channel = ieee80211_get_channel(wiphy, cs->channel->center_freq);
    if (!info->channel) {
    ELOG_D("Cannot to find channel for freq %u", cs->channel->center_freq);
      res = MTLK_ERR_UNKNOWN;
      goto fail;
    }

    info->noise = cs->noise;
    info->time = 255; /* we only have a u8 load value l available, so we claim that it's been loaded l/255 ms */
    info->time_busy = cs->load;
    info->filled = SURVEY_INFO_NOISE_DBM | SURVEY_INFO_TIME | SURVEY_INFO_TIME_BUSY;

    if (info->channel->center_freq == cs->current_primary_chan_freq)
      info->filled |= SURVEY_INFO_IN_USE;

fail:
    mtlk_clpb_delete(clpb); /* already deleted in error cases */
  }

  return _mtlk_df_mtlk_to_linux_error_code(res);
}

static BOOL _wave_radio_etsi_chan_in_long_cac_range(uint32 center_freq, int width)
{
  uint32 freq;
  uint32 start_freq = center_freq - width/2 + 10;
  uint32 end_freq = center_freq + width/2 - 10;

  for (freq = start_freq; freq <= end_freq; freq += 20) {
    if (freq >= ETSI_LONG_CAC_LOW_FREQ && freq <= ETSI_LONG_CAC_HIGH_FREQ)
      return TRUE;
  }

  return FALSE;
}

static uint32 _wave_radio_cac_time_etsi_get(const struct cfg80211_chan_def *chandef)
{
  uint32 width = _wave_radio_chandef_width_get(chandef);

  if (!width) {
    ELOG_DD("Could not get width for center_freq1 %u, enum width %u",
            chandef->center_freq1, chandef->width);
    return IEEE80211_DFS_MIN_CAC_TIME_MS;
  }

  if (_wave_radio_etsi_chan_in_long_cac_range(chandef->center_freq1, width))
    return ETSI_LONG_CAC_TIME_MS;

  if (chandef->center_freq2 &&
    _wave_radio_etsi_chan_in_long_cac_range(chandef->center_freq2, width))
    return ETSI_LONG_CAC_TIME_MS;

  return IEEE80211_DFS_MIN_CAC_TIME_MS;
}

int wave_radio_radar_detection_start(struct wiphy *wiphy,
                                     wave_radio_t *radio,
                                     struct net_device *ndev,
                                     struct cfg80211_chan_def *chandef,
                                     u32 cac_time_ms)
{
  int res = MTLK_ERR_OK;
  mtlk_clpb_t *clpb;
  struct set_chan_param_data cpd;
  mtlk_df_t *master_df;
  mtlk_core_t *core;
  mtlk_scan_support_t *scan_support;
  mtlk_df_user_t *df_user;
  mtlk_df_t *df;
  mtlk_vap_handle_t vap_handle;
  struct mtlk_chan_def original_cd;
  struct mtlk_chan_def *ccd;
  u32 cac_time;
  struct wireless_dev *wdev;
  uint32 regd_code;

  /* postpone to execute Radar events */
  _wave_radio_radar_detect_end_reset(radio);

  clpb = NULL;
  master_df = mtlk_vap_manager_get_master_df(radio->vap_manager);
  core = mtlk_vap_get_core(mtlk_df_get_vap_handle(master_df));
  scan_support = mtlk_core_get_scan_support(core);
  df_user = mtlk_df_user_from_ndev(ndev);
  MTLK_CHECK_DF_USER(df_user);

  df = mtlk_df_user_get_df(df_user);
  vap_handle = mtlk_df_get_vap_handle(df);
  ccd = wave_radio_chandef_get(radio);
  cac_time = IEEE80211_DFS_MIN_CAC_TIME_MS;
  wdev = ndev->ieee80211_ptr;

  wdev->preset_chandef = *chandef;

  regd_code = core_cfg_get_regd_code(core);
  switch (regd_code) {
    case REGD_CODE_ETSI:
      cac_time = _wave_radio_cac_time_etsi_get(chandef);
    break;
    default:
    break;
  }

  if (scan_support->dfs_debug_params.cac_time != -1) {
    ILOG1_D("Using debug CAC time: %d seconds", scan_support->dfs_debug_params.cac_time);
    cac_time = scan_support->dfs_debug_params.cac_time * MTLK_OSAL_MSEC_IN_SEC;
  }

  original_cd = *ccd;

  ILOG1_SSD("%s: Invoked from %s (%i)", ndev->name, current->comm, current->pid);
  ILOG1_DDDDDD("center_freq1=%u, nl_width=%u, chan.nlband=%u, chan.center_freq=%hu, chan.flags=0x%08x, cac_time=%i",
               chandef->center_freq1, chandef->width, chandef->chan->band, chandef->chan->center_freq,
               chandef->chan->flags, cac_time);

  memset(&cpd, 0, sizeof(cpd));
  wave_radio_chandef_copy(&cpd.chandef, chandef);
  cpd.cac_time = cac_time;
  cpd.radar_required = TRUE;
  cpd.ndev = ndev;
  cpd.vap_id = mtlk_vap_get_id(vap_handle);

  res = _mtlk_df_user_invoke_core(master_df, WAVE_RADIO_REQ_SET_CHAN, &clpb, &cpd, sizeof(cpd));
  res = _mtlk_df_user_process_core_retval(res, clpb, WAVE_RADIO_REQ_SET_CHAN, TRUE);

  if (res == MTLK_ERR_OK)
    wave_radio_ch_switch_event(wiphy, mtlk_vap_manager_get_master_core(radio->vap_manager), &original_cd);

  res = _mtlk_df_mtlk_to_linux_error_code(res);

  /* allow to execute Radar events */
  _wave_radio_radar_detect_end_set(radio);
  return res;
}

static int
_mtlk_nl_to_mtlk_sbdfs_bw (enum nl80211_sb_dfs_bw bw)
{
  switch (bw) {
  case NL80211_SB_DFS_BW_20:
    return MTLK_SB_DFS_BW_20;
  case NL80211_SB_DFS_BW_40:
    return MTLK_SB_DFS_BW_40;
  case NL80211_SB_DFS_BW_80:
    return MTLK_SB_DFS_BW_80;
  default:
    return MTLK_SB_DFS_BW_NORMAL;
  }
}

/* HostAPD will call this for every VAP. Every time we'll do a set_channel (type CSA) and set_beacon.
 * However, set_channel will go through only the first time, as it's done for all VAPs simultaneously.
 */
int wave_radio_channel_switch(
    wave_radio_t *radio,
    struct net_device *ndev,
    struct cfg80211_csa_settings *csas)
{
  int res = MTLK_ERR_OK;
  mtlk_df_user_t *df_user = mtlk_df_user_from_ndev(ndev);
  mtlk_vap_handle_t vap_handle;
  mtlk_clpb_t *clpb = NULL;
  struct set_chan_param_data cpd;
  mtlk_df_t *master_df;
  mtlk_scan_support_t *scan_support;
  int beacon_period;
  void *wiphy;
  struct mtlk_chan_def *ccd;
  struct mtlk_chan_def original_cd;
  BOOL sb_dfs_switch = FALSE;
  mtlk_sb_dfs_params_t *sb_dfs;

  MTLK_ASSERT(NULL != radio);
  master_df = mtlk_vap_manager_get_master_df(radio->vap_manager);
  MTLK_ASSERT(NULL != master_df);
  wiphy = wv_cfg80211_wiphy_get(radio->cfg80211);
  MTLK_ASSERT(NULL != wiphy);

  ccd = wave_radio_chandef_get(radio);

  ILOG1_SSD("%s: Invoked from %s (%i)", ndev->name, current->comm, current->pid);
  ILOG1_DDDDDD("center_freq1=%u, nl_width=%u, chan.nlband=%u, chan.center_freq=%hu, chan.flags=0x%08x, count=%hhu",
               csas->chandef.center_freq1, csas->chandef.width, csas->chandef.chan->band, csas->chandef.chan->center_freq,
               csas->chandef.chan->flags, csas->count);

  MTLK_CHECK_DF_USER(df_user);
  vap_handle = mtlk_df_get_vap_handle(mtlk_df_user_get_df(df_user));
  scan_support = mtlk_core_get_scan_support(mtlk_vap_get_core(vap_handle));

  /* Prevent AP role VAP to switch channels in case a station role interface exists */
  if (wave_radio_get_sta_vifs_exist(radio)) {
    ILOG0_V("Cannot switch VAP channels, since there are station role interfaces active");
    return  -EBUSY;
  }

  beacon_period = WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_BEACON_PERIOD);

  original_cd = *ccd;
  memset(&cpd, 0, sizeof(cpd));
  sb_dfs = &cpd.chandef.sb_dfs;
  wave_radio_chandef_copy(&cpd.chandef, &csas->chandef);

  cpd.ndev = ndev;
  cpd.vap_id = mtlk_vap_get_id(vap_handle);

  if (scan_support->dfs_debug_params.beacon_count > 0) {
    cpd.chan_switch_time = scan_support->dfs_debug_params.beacon_count * beacon_period;
    ILOG1_D("Using debug CSA Beacon count: %d", scan_support->dfs_debug_params.beacon_count);
  } else {
    cpd.chan_switch_time = csas->count * beacon_period;
  }

  cpd.block_tx_pre = csas->block_tx; /* hostapd always sets this to 1, BTW */
  cpd.block_tx_post = FALSE; /* TRUE is meant for waiting for radars, but hostapd has ensured it's switching to a safe range */
  cpd.radar_required = csas->radar_required;
  sb_dfs->sb_dfs_bw = _mtlk_nl_to_mtlk_sbdfs_bw(csas->sb_dfs_bw);

  if (__mtlk_is_sb_dfs_switch(sb_dfs->sb_dfs_bw)) {
    ILOG1_D("Sub band DFS channel switch, new bandwidth %d", sb_dfs->sb_dfs_bw);
    sb_dfs_switch = TRUE;

    if (!__mtlk_is_sb_dfs_switch(original_cd.sb_dfs.sb_dfs_bw)) {
      sb_dfs->center_freq = original_cd.center_freq1;
      sb_dfs->width = original_cd.width;
    }
    else
    {
      sb_dfs->center_freq = original_cd.sb_dfs.center_freq;
      sb_dfs->width = original_cd.sb_dfs.width;
    }
  }

  /* If using debug radar simulation, we will start CAC after CSA, so block_tx_post should be set */
  if (scan_support->dfs_debug_params.debug_chan) {
    uint32 cur_freq;
    struct ieee80211_channel *c;
    uint32 low_freq = freq2lowfreq(csas->chandef.center_freq1, nlcw2mtlkcw(csas->chandef.width));
    uint32 high_freq = freq2highfreq(csas->chandef.center_freq1, nlcw2mtlkcw(csas->chandef.width));

    for (cur_freq = low_freq; cur_freq <= high_freq; cur_freq += 20) {
      c = ieee80211_get_channel(wiphy, cur_freq);

      if (!c || !(c->flags & IEEE80211_CHAN_RADAR))
        continue;

      cpd.block_tx_post = TRUE;
    }
  }

  res = _mtlk_df_user_invoke_core(master_df, WAVE_RADIO_REQ_SET_CHAN, &clpb, &cpd, sizeof(cpd));
  res = _mtlk_df_user_process_core_retval(res, clpb, WAVE_RADIO_REQ_SET_CHAN, TRUE);

  if (res == MTLK_ERR_OK && !sb_dfs_switch)
    wave_radio_ch_switch_event(wiphy, mtlk_vap_manager_get_master_core(radio->vap_manager), &original_cd);

  return _mtlk_df_mtlk_to_linux_error_code(res);
}

int wave_radio_qos_map_set(wave_radio_t *radio, struct net_device *ndev, struct cfg80211_qos_map *qos_map)
{
  int res = MTLK_ERR_OK;
  mtlk_df_user_t *df_user = mtlk_df_user_from_ndev(ndev);
  mtlk_clpb_t *clpb = NULL;

  MTLK_ASSERT(NULL != radio);
  ILOG1_SSD("%s: Invoked from %s (%i)", ndev->name, current->comm, current->pid);
  MTLK_CHECK_DF_USER(df_user);

  if (NULL == qos_map) {
    ILOG2_V("qos_map is NULL");
    return _mtlk_df_mtlk_to_linux_error_code(res);
  }

  res = _mtlk_df_user_invoke_core(mtlk_df_user_get_df(df_user), WAVE_CORE_REQ_SET_QOS_MAP, &clpb, qos_map, sizeof(struct cfg80211_qos_map));
  res = _mtlk_df_user_process_core_retval(res, clpb, WAVE_CORE_REQ_SET_QOS_MAP, TRUE);

  return _mtlk_df_mtlk_to_linux_error_code(res);
}

static void _wave_radio_antenna_params_set(uint32 tx_ant, uint32 rx_ant,
  mtlk_coc_antenna_cfg_t* antenna_params)
{
  MTLK_ASSERT(NULL != antenna_params);

  antenna_params->num_tx_antennas = tx_ant;
  antenna_params->num_rx_antennas = rx_ant;
}

int wave_radio_antenna_set(wave_radio_t *radio, u32 tx_ant, u32 rx_ant, u32 avail_ant_tx, u32 avail_ant_rx)
{
  int res = MTLK_ERR_OK;
  mtlk_clpb_t *clpb = NULL;
  mtlk_df_user_t *df_user;
  mtlk_coc_mode_cfg_t coc_cfg;

  MTLK_ASSERT(NULL != radio);

  df_user = mtlk_df_get_user(wave_radio_df_get(radio));
  MTLK_ASSERT(NULL != df_user);

  ILOG1_SSD("%s: Invoked from %s (%i)", mtlk_df_user_get_name(df_user),
    current->comm, current->pid);

  memset(&coc_cfg, 0, sizeof(coc_cfg));
  if (tx_ant & ~avail_ant_tx) {
    ELOG_DD("Invalid TX antenna mask: 0x%08X. Available TX antenna mask: 0x%08X",
      tx_ant, avail_ant_tx);
    return -EINVAL;
  }
  if (rx_ant & ~avail_ant_rx) {
    ELOG_DD("Invalid RX antenna mask: 0x%08X. Available RX antenna mask: 0x%08X",
      rx_ant, avail_ant_rx);
    return -EINVAL;
  }
  MTLK_CFG_SET_ITEM_BY_FUNC_VOID(&coc_cfg, antenna_params,
    _wave_radio_antenna_params_set, (count_bits_set(tx_ant), count_bits_set(rx_ant),
      &coc_cfg.antenna_params));
  MTLK_CFG_SET_ITEM(&coc_cfg, is_auto_mode, FALSE);
  res = _mtlk_df_user_invoke_core(mtlk_df_user_get_df(df_user),
    WAVE_RADIO_REQ_SET_COC_CFG, &clpb, &coc_cfg, sizeof(coc_cfg));
  res = _mtlk_df_user_process_core_retval(res, clpb, WAVE_RADIO_REQ_SET_COC_CFG,
    TRUE);

  return _mtlk_df_mtlk_to_linux_error_code(res);
}

int wave_radio_antenna_get(wave_radio_t *radio, u32 *tx_ant, u32 *rx_ant, u32 avail_ant_tx, u32 avail_ant_rx)
{
  int res = MTLK_ERR_OK;
  mtlk_clpb_t *clpb = NULL;
  mtlk_df_user_t *df_user;
  mtlk_df_t *master_df;
  mtlk_coc_mode_cfg_t *coc_cfg;
  uint32 coc_cfg_size;

  MTLK_ASSERT(NULL != radio);

  master_df = wave_radio_df_get(radio);
    /* get_antenna can be called before master_vap is created */
  if (NULL == master_df) {
    return _mtlk_df_mtlk_to_linux_error_code(MTLK_ERR_NOT_READY);
  }

  df_user = mtlk_df_get_user(master_df);
  MTLK_CHECK_DF_USER(df_user);

  ILOG1_SSD("%s: Invoked from %s (%i)", mtlk_df_user_get_name(df_user),
    current->comm, current->pid);

  res = _mtlk_df_user_invoke_core(mtlk_df_user_get_df(df_user), WAVE_RADIO_REQ_GET_COC_CFG, &clpb, NULL, 0);
  res = _mtlk_df_user_process_core_retval(res, clpb, WAVE_RADIO_REQ_GET_COC_CFG, FALSE);

  if (res != MTLK_ERR_OK)
    goto end;

  coc_cfg = mtlk_clpb_enum_get_next(clpb, &coc_cfg_size);
  MTLK_CLPB_TRY(coc_cfg, coc_cfg_size) {
    *tx_ant = coc_cfg->cur_ant_mask;
    *rx_ant = coc_cfg->cur_ant_mask;
  }
  MTLK_CLPB_FINALLY(res) {
    mtlk_clpb_delete(clpb);
  }
  MTLK_CLPB_END;

end:
  return _mtlk_df_mtlk_to_linux_error_code(res);
}

int wave_radio_aid_get(
  wave_radio_t *radio,
  struct net_device *ndev,
  const uint8 *mac_addr,
  u16 *aid)
{
  mtlk_df_user_t *df_user = mtlk_df_user_from_ndev(ndev);
  int res = MTLK_ERR_OK;
  mtlk_clpb_t *clpb = NULL;

  MTLK_ASSERT(NULL != radio);

  mac_addr = mac_addr ? mac_addr : _bcast_addr;

  ILOG1_SSD("%s: Invoked from %s (%i)", ndev->name, current->comm, current->pid);
  ILOG1_Y("mac_addr = %Y", mac_addr);
  MTLK_CHECK_DF_USER(df_user);

  if (!mtlk_osal_is_valid_ether_addr(mac_addr)) {
    ELOG_SY("%s: Invalid MAC address: %Y", ndev->name, mac_addr);
    res = MTLK_ERR_PARAMS;
    goto finish;
  }

  if (!netif_running(ndev)) {
    WLOG_S("%s: You should bring interface up first", ndev->name);
    res = MTLK_ERR_WRONG_CONTEXT;
    goto finish;
  }

  res = _mtlk_df_user_invoke_core(mtlk_df_user_get_df(df_user), WAVE_CORE_REQ_REQUEST_SID, &clpb, mac_addr, IEEE_ADDR_LEN);
  res = _mtlk_df_user_process_core_retval(res, clpb, WAVE_CORE_REQ_REQUEST_SID, FALSE);

  if (MTLK_ERR_OK == res) {
    u16 *sid;
    uint32 sid_size;

    sid = mtlk_clpb_enum_get_next(clpb, &sid_size);
    MTLK_CLPB_TRY(sid, sid_size)
      *aid = *sid + 1;
    MTLK_CLPB_FINALLY(res)
      mtlk_clpb_delete(clpb); /* already deleted in error cases */
    MTLK_CLPB_END;
  }

finish:
  return _mtlk_df_mtlk_to_linux_error_code(res);
}

int wave_radio_aid_free(wave_radio_t *radio, struct net_device *ndev, u16 aid)
{
  mtlk_df_user_t *df_user = mtlk_df_user_from_ndev(ndev);
  u16 sid = aid - 1;
  int res = MTLK_ERR_OK;
  mtlk_clpb_t *clpb = NULL;

  MTLK_ASSERT(NULL != radio);

  ILOG1_SSD("%s: Invoked from %s (%i)", ndev->name, current->comm, current->pid);
  ILOG1_DD("AID=%hi, SID=%hi", aid, sid);
  MTLK_CHECK_DF_USER(df_user);

  res = _mtlk_df_user_invoke_core(mtlk_df_user_get_df(df_user), WAVE_CORE_REQ_REMOVE_SID, &clpb, &sid, sizeof(sid));
  res = _mtlk_df_user_process_core_retval(res, clpb, WAVE_CORE_REQ_REMOVE_SID, TRUE);

  return _mtlk_df_mtlk_to_linux_error_code(res);
}

int wave_radio_sync_done(wave_radio_t *radio, struct net_device *ndev)
{
  mtlk_df_user_t *df_user = mtlk_df_user_from_ndev(ndev);
  int res = MTLK_ERR_OK;
  mtlk_clpb_t *clpb = NULL;

  MTLK_ASSERT(NULL != radio);

  ILOG1_SSD("%s: Invoked from %s (%i)", ndev->name, current->comm, current->pid);
  MTLK_CHECK_DF_USER(df_user);

  if (!netif_running(ndev)) {
    WLOG_S("%s: You should bring interface up first", ndev->name);
    res = MTLK_ERR_WRONG_CONTEXT;
    goto finish;
  }

  res = _mtlk_df_user_invoke_core(mtlk_df_user_get_df(df_user), WAVE_CORE_REQ_SYNC_DONE, &clpb, NULL, 0);
  res = _mtlk_df_user_process_core_retval(res, clpb, WAVE_CORE_REQ_SYNC_DONE, TRUE);

finish:
  return _mtlk_df_mtlk_to_linux_error_code(res);
}

int wave_radio_station_dump(
  wave_radio_t *radio,
  struct net_device *ndev,
  int idx,
  u8 *mac,
  struct station_info *st_info)
{
  int res;
  mtlk_df_user_t *df_user = mtlk_df_user_from_ndev(ndev);
  mtlk_clpb_t *clpb = NULL;
  st_info_by_idx_data_t info_data;

  MTLK_ASSERT(NULL != radio);
  MTLK_ASSERT(NULL != st_info);
  MTLK_ASSERT(NULL != mac);

  ILOG1_SSD("%s: Invoked from %s (%i)", ndev->name, current->comm, current->pid);
  MTLK_CHECK_DF_USER(df_user);

  info_data.idx = idx;
  info_data.mac = mac;
  info_data.stinfo = st_info;
  res = _mtlk_df_user_invoke_core(mtlk_df_user_get_df(df_user), WAVE_CORE_REQ_GET_STADB_STA_BY_ITER_ID, &clpb, &info_data, sizeof(info_data));
  res = _mtlk_df_user_process_core_retval(res, clpb, WAVE_CORE_REQ_GET_STADB_STA_BY_ITER_ID, TRUE);

  if (res == MTLK_ERR_OK && !is_zero_ether_addr(info_data.mac)) {
    ILOG2_DY("idx = %d, MAC = %Y", idx, mac);
    return _mtlk_df_mtlk_to_linux_error_code(res);
  }
  else {
    ILOG2_D("idx = %d, not found.", idx);
    return -ENOENT;
  }
}

int wave_radio_eapol_send(
  wave_radio_t *radio,
  struct wireless_dev *wdev,
  const void *data, int data_len)
{
  struct mgmt_tx_params mtp;
  uint64 cookie;
  mtlk_df_user_t *df_user = mtlk_df_user_from_wdev(wdev);
  int res = MTLK_ERR_OK;
  mtlk_clpb_t *clpb = NULL;

  MTLK_ASSERT(NULL != radio);
  MTLK_ASSERT(NULL != df_user);

  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  MTLK_CHECK_DF_USER(df_user);

  memset(&mtp, 0, sizeof(mtp));
  mtp.buf = data;
  mtp.len = data_len;
  mtp.cookie = &cookie;
  mtp.dont_wait_for_ack = TRUE;
  mtp.extra_processing = PROCESS_EAPOL;

  res = _mtlk_df_user_invoke_core(mtlk_df_user_get_df(df_user), WAVE_CORE_REQ_MGMT_TX, &clpb, &mtp, sizeof(mtp));
  res = _mtlk_df_user_process_core_retval(res, clpb, WAVE_CORE_REQ_MGMT_TX, TRUE);

  return _mtlk_df_mtlk_to_linux_error_code(res);
}

/* Get/set param DB value of 11N_ACAX_COMPAT */
static void __MTLK_IFUNC
_wave_radio_set_11n_acax_compat (wave_radio_t *radio, int value)
{
  WAVE_RADIO_PDB_SET_INT(radio, PARAM_DB_RADIO_11N_ACAX_COMPAT, value);
}

int __MTLK_IFUNC
wave_radio_get_11n_acax_compat (wave_radio_t *radio)
{
  return WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_11N_ACAX_COMPAT);
}

int wave_radio_initial_data_send(
    wave_radio_t *radio,
    struct net_device *ndev,
    const void *data,
    int data_len)
{
  int res = MTLK_ERR_OK;
  struct intel_vendor_initial_data_cfg *initial_data;
  struct nic *master_core;

  MTLK_ASSERT(NULL != radio);
  MTLK_ASSERT(NULL != ndev);

  master_core = mtlk_vap_manager_get_master_core(radio->vap_manager);

  ILOG1_SSD("%s: Invoked from %s (%i)", ndev->name, current->comm, current->pid);

  if (data_len != sizeof(struct intel_vendor_initial_data_cfg)) {
    ELOG_D("Wrong hostapd initial data len: %d", data_len);
    res = MTLK_ERR_PARAMS;
    goto end;
  }

  initial_data = (struct intel_vendor_initial_data_cfg *)data;
  if ('\0' == initial_data->alpha2[0]) {
    ELOG_V("Country code is not set");
    res = MTLK_ERR_PARAMS;
    goto end;
  }

  _wave_radio_set_11n_acax_compat (radio, initial_data->ieee80211n_acax_compat);

  res = core_cfg_set_hostapd_initial_data(master_core, initial_data);
  if (MTLK_ERR_OK != res) {
    ELOG_D("Can't set hostapd initial data, res: %d", res);
    goto end;
  }

end:
  return _mtlk_df_mtlk_to_linux_error_code(res);
}

int wave_radio_send_dfs_debug_radar_required_chan(
    wave_radio_t *radio,
    struct net_device *ndev,
    const void *data,
    int data_len)
{
  int res = MTLK_ERR_OK;
  uint8 *dfs_debug = (uint8 *)data;
  struct nic *master_core;
  mtlk_scan_support_t *scan_support;

  MTLK_ASSERT(NULL != radio);
  MTLK_ASSERT(NULL != ndev);
  master_core = mtlk_vap_manager_get_master_core(radio->vap_manager);
  scan_support = mtlk_core_get_scan_support(master_core);

  ILOG1_SSD("%s: Invoked from %s (%i)", ndev->name, current->comm, current->pid);

  if (data_len != sizeof(*dfs_debug)) {
    ELOG_D("Wrong DFS debug channel data length: %d", data_len);
    res = MTLK_ERR_PARAMS;
    goto end;
  }

  if (NULL == data) {
    ELOG_V("DFS debug channel data is NULL");
    res = MTLK_ERR_PARAMS;
    goto end;
  }

  scan_support->dfs_debug_params.debug_chan = *dfs_debug;

  ILOG1_S("DFS debug channel requires radar detection: %s", *dfs_debug ? "YES" : "NO");
end:
  return _mtlk_df_mtlk_to_linux_error_code(res);
}

int wave_radio_dfs_flags_change(
    wave_radio_t *radio,
    struct net_device *ndev,
    const void *data,
    int data_len)
{
  /* Never call this function! */
  return _mtlk_df_mtlk_to_linux_error_code(MTLK_ERR_OK);
}

int wave_radio_deny_mac_set(
  wave_radio_t *radio,
  struct net_device *ndev,
  const void *data,
  int data_len)
{
  int res = MTLK_ERR_OK;
  mtlk_df_user_t *df_user;
  mtlk_clpb_t *clpb = NULL;
  struct intel_vendor_blacklist_cfg blacklist_cfg;

  MTLK_ASSERT(NULL != radio);
  MTLK_ASSERT(NULL != ndev);
  df_user = mtlk_df_user_from_ndev(ndev);

  ILOG1_SSD("%s: Invoked from %s (%i)", ndev->name, current->comm, current->pid);
  MTLK_CHECK_DF_USER(df_user);

  if (data_len != sizeof(blacklist_cfg)) {
    ELOG_D("Wrong blacklist data length: %d", data_len);
    res = MTLK_ERR_PARAMS;
    goto end;
  }

  if (NULL == data) {
    ELOG_V("Blacklist data is NULL");
    res = MTLK_ERR_PARAMS;
    goto end;
  }
  memset(&blacklist_cfg, 0, sizeof(blacklist_cfg));
  wave_memcpy(&blacklist_cfg, sizeof(blacklist_cfg), data, sizeof(blacklist_cfg));
  if (mtlk_osal_eth_is_broadcast(blacklist_cfg.addr.au8Addr)) {
    ELOG_V("Cannot add broadcast address to blacklist");
    res = MTLK_ERR_PARAMS;
    goto end;
  }
  if (!ieee_addr_is_valid(&blacklist_cfg.addr) &&
    (!ieee_addr_is_zero(&blacklist_cfg.addr) || !blacklist_cfg.remove))
  {
    ELOG_Y("Invalid MAC address %Y", blacklist_cfg.addr.au8Addr);
    res = MTLK_ERR_PARAMS;
    goto end;
  }

  res = _mtlk_df_user_invoke_core(mtlk_df_user_get_df(df_user),
    WAVE_CORE_REQ_SET_BLACKLIST_ENTRY, &clpb, &blacklist_cfg,
    sizeof(blacklist_cfg));
  res = _mtlk_df_user_process_core_retval(res, clpb,
    WAVE_CORE_REQ_SET_BLACKLIST_ENTRY, TRUE);

end:
  return _mtlk_df_mtlk_to_linux_error_code(res);
}

int wave_radio_sta_steer(
  wave_radio_t *radio,
  struct net_device *ndev,
  const void *data,
  int data_len)
{
  int res = MTLK_ERR_OK;
  mtlk_df_user_t *df_user;
  mtlk_vap_handle_t vap_handle;
  mtlk_vap_manager_t *vap_mgr;
  int vaps_count, vap_index;
  mtlk_clpb_t *clpb = NULL;
  struct intel_vendor_blacklist_cfg blacklist_cfg;
  struct intel_vendor_steer_cfg *steer_cfg;

  MTLK_ASSERT(NULL != radio);
  MTLK_ASSERT(NULL != ndev);
  df_user = mtlk_df_user_from_ndev(ndev);

  ILOG1_SSD("%s: Invoked from %s (%i)", ndev->name, current->comm, current->pid);
  MTLK_CHECK_DF_USER(df_user);

  vap_handle = mtlk_df_get_vap_handle(mtlk_df_user_get_df(df_user));
  /* station address and BSSID steer to */
  if (data_len != sizeof(*steer_cfg)) {
    ELOG_D("Wrong station steer data length: %d", data_len);
    res = MTLK_ERR_PARAMS;
    goto end;
  }

  if (NULL == data) {
    ELOG_V("Station steer data is NULL");
    res = MTLK_ERR_PARAMS;
    goto end;
  }
  steer_cfg = (struct intel_vendor_steer_cfg *)data;
  if (mtlk_osal_eth_is_broadcast(steer_cfg->addr.au8Addr)) {
    ELOG_V("Cannot steer broadcast address");
    res = MTLK_ERR_PARAMS;
    goto end;
  }
  if (!ieee_addr_is_valid(&steer_cfg->addr)) {
    ELOG_Y("Invalid MAC address %Y", steer_cfg->addr.au8Addr);
    res = MTLK_ERR_PARAMS;
    goto end;
  }

  memset(&blacklist_cfg, 0, sizeof(blacklist_cfg));
  blacklist_cfg.addr = steer_cfg->addr;
  blacklist_cfg.status = steer_cfg->status;

  ILOG2_DYY("CID-%04x: steer station %Y to BSSID %Y",
      mtlk_vap_get_oid(vap_handle), blacklist_cfg.addr.au8Addr,
      steer_cfg->bssid.au8Addr);

  vap_mgr = mtlk_vap_get_manager(vap_handle);
  vaps_count = mtlk_vap_manager_get_max_vaps_count(vap_mgr);
  for (vap_index = vaps_count - 1; vap_index >= 0; vap_index--) {
    res = mtlk_vap_manager_get_vap_handle(vap_mgr, vap_index, &vap_handle);
    if (MTLK_ERR_OK == res) {
      if (mtlk_pdb_cmp_mac(mtlk_vap_get_param_db(vap_handle), PARAM_DB_CORE_MAC_ADDR,
          steer_cfg->bssid.au8Addr))
        /* steer station to another VAP, add it to blacklist on this one */
        blacklist_cfg.remove = 0;
      else
        /* steer station to this VAP, remove it from blacklist */
        blacklist_cfg.remove = 1;

      res = _mtlk_df_user_invoke_core(mtlk_vap_get_df(vap_handle),
        WAVE_CORE_REQ_SET_BLACKLIST_ENTRY, &clpb, &blacklist_cfg,
        sizeof(blacklist_cfg));
      res = _mtlk_df_user_process_core_retval(res, clpb,
        WAVE_CORE_REQ_SET_BLACKLIST_ENTRY, TRUE);
      if (res != MTLK_ERR_OK)
        break;
    }
  }

end:
  return _mtlk_df_mtlk_to_linux_error_code(res);
}

static void _wave_radio_fill_sta_info(struct intel_vendor_sta_info* vendor_sta_info,
  struct driver_sta_info* sta_info)
{
#define ASSIGN_STA_INFO_PEER_RATES_INFO_PARAM(name)                     \
  do {                                                                  \
    vendor_sta_info->name = sta_info->rates_info.name;                  \
  } while (0)

#define ASSIGN_STA_INFO_PEER_STATS_PARAM(name)                          \
  do {                                                                  \
    vendor_sta_info->name = sta_info->peer_stats.name;                  \
  } while (0)

#define ASSIGN_STA_INFO_PEER_TR181_STATS_PARAM(name)                    \
  do {                                                                  \
    vendor_sta_info->name = sta_info->peer_stats.tr181_stats.name;      \
  } while (0)

#define ASSIGN_STA_INFO_PEER_TRAFFIC_STATS_PARAM(name)                  \
  do {                                                                  \
    vendor_sta_info->name =                                             \
      sta_info->peer_stats.tr181_stats.traffic_stats.name;              \
  } while (0)

#define ASSIGN_STA_INFO_PEER_RETRANS_STATS_PARAM(name)                  \
  do {                                                                  \
    vendor_sta_info->name =                                             \
      sta_info->peer_stats.tr181_stats.retrans_stats.name;              \
  } while (0)

  int i;

  MTLK_ASSERT(vendor_sta_info != NULL);
  MTLK_ASSERT(sta_info != NULL);

  memset(vendor_sta_info, 0, sizeof(*vendor_sta_info));

  ASSIGN_STA_INFO_PEER_TR181_STATS_PARAM(StationId);
  ASSIGN_STA_INFO_PEER_TR181_STATS_PARAM(NetModesSupported);

  ASSIGN_STA_INFO_PEER_TRAFFIC_STATS_PARAM(BytesSent);
  ASSIGN_STA_INFO_PEER_TRAFFIC_STATS_PARAM(BytesReceived);
  ASSIGN_STA_INFO_PEER_TRAFFIC_STATS_PARAM(PacketsSent);
  ASSIGN_STA_INFO_PEER_TRAFFIC_STATS_PARAM(PacketsReceived);

  /* Excluding unavailable Retransmissions and MultipleRetryCount */
  ASSIGN_STA_INFO_PEER_RETRANS_STATS_PARAM(RetransCount);
  ASSIGN_STA_INFO_PEER_RETRANS_STATS_PARAM(FailedRetransCount);
  ASSIGN_STA_INFO_PEER_RETRANS_STATS_PARAM(RetryCount);

  ASSIGN_STA_INFO_PEER_TR181_STATS_PARAM(LastDataUplinkRate);
  ASSIGN_STA_INFO_PEER_TR181_STATS_PARAM(LastDataDownlinkRate);
  ASSIGN_STA_INFO_PEER_TR181_STATS_PARAM(SignalStrength);

  ASSIGN_STA_INFO_PEER_RATES_INFO_PARAM(TxMgmtPwr);
  ASSIGN_STA_INFO_PEER_RATES_INFO_PARAM(TxStbcMode);

  for (i = 0; i < NUMBER_OF_RX_ANTENNAS; ++i) {
    ASSIGN_STA_INFO_PEER_STATS_PARAM(ShortTermRSSIAverage[i]);
    ASSIGN_STA_INFO_PEER_STATS_PARAM(snr[i]);
  }

  /* Fill Phy Data Rate info if it is available from TX/RX rates info */
  {
    mtlk_wssa_drv_peer_rate_info1_t   *rate_info = NULL;

    if (sta_info->rates_info.tx_data_rate_info.InfoFlag) {
      rate_info = &sta_info->rates_info.tx_data_rate_info;
    } else if (sta_info->rates_info.rx_data_rate_info.InfoFlag) {
      rate_info = &sta_info->rates_info.rx_data_rate_info;
    }

    if (rate_info) { /* available */
      vendor_sta_info->RateInfoFlag = 1;
      vendor_sta_info->RatePhyMode  = rate_info->PhyMode;
      vendor_sta_info->RateCbwMHz   = rate_info->CbwMHz;
      vendor_sta_info->RateMcs      = rate_info->Mcs;
      vendor_sta_info->RateNss      = rate_info->Nss;
    }
  }

#undef ASSIGN_STA_INFO_PEER_RATES_INFO_PARAM
#undef ASSIGN_STA_INFO_PEER_STATS_PARAM
#undef ASSIGN_STA_INFO_PEER_TR181_STATS_PARAM
#undef ASSIGN_STA_INFO_PEER_TRAFFIC_STATS_PARAM
#undef ASSIGN_STA_INFO_PEER_RETRANS_STATS_PARAM
}

int wave_radio_sta_measurements_get(
  wave_radio_t *radio,
  struct net_device *ndev,
  const void *data,
  int data_len)
{
  u8 *addr = (u8*)data;
  mtlk_df_user_t *df_user;
  mtlk_clpb_t *clpb = NULL;
  mtlk_nbuf_t *rsp_nbuf;
  int res = MTLK_ERR_OK;
  struct driver_sta_info *sta_info;
  struct intel_vendor_sta_info vendor_sta_info;
  uint32 sta_info_size;
  void *wiphy;

  MTLK_ASSERT(NULL != radio);
  MTLK_ASSERT(NULL != ndev);
  df_user = mtlk_df_user_from_ndev(ndev);

  ILOG1_SSD("%s: Invoked from %s (%i)", ndev->name, current->comm, current->pid);
  MTLK_CHECK_DF_USER(df_user);

  wiphy = wv_cfg80211_wiphy_get(radio->cfg80211);
  MTLK_ASSERT(NULL != wiphy);

  /* address */
  if (data_len != sizeof(IEEE_ADDR)) {
    ELOG_D("Wrong station measurements data length: %d", data_len);
    res = MTLK_ERR_PARAMS;
    goto end;
  }

  if (NULL == data) {
    ELOG_V("Station measurements data is NULL");
    res = MTLK_ERR_PARAMS;
    goto end;
  }
  if (mtlk_osal_eth_is_broadcast((u8*)data)) {
    ELOG_V("Station address can't be broadcast address");
    res = MTLK_ERR_PARAMS;
    goto end;
  }
  if (!mtlk_osal_is_valid_ether_addr((u8*)data)) {
    ELOG_Y("Invalid MAC address %Y", data);
    res = MTLK_ERR_PARAMS;
    goto end;
  }

  res = _mtlk_df_user_invoke_core(mtlk_df_user_get_df(df_user),
    WAVE_CORE_REQ_GET_STATION_MEASUREMENTS, &clpb, addr, IEEE_ADDR_LEN);
  res = _mtlk_df_user_process_core_retval(res, clpb,
    WAVE_CORE_REQ_GET_STATION_MEASUREMENTS, FALSE);

  if (res != MTLK_ERR_OK)
    goto end;

  sta_info = mtlk_clpb_enum_get_next(clpb, &sta_info_size);
  MTLK_CLPB_TRY(sta_info, sta_info_size)
    rsp_nbuf = wv_cfg80211_vendor_cmd_alloc_reply_skb(wiphy, sizeof(vendor_sta_info));
    if (!rsp_nbuf) {
      MTLK_CLPB_EXIT(MTLK_ERR_NO_MEM);
    }
    _wave_radio_fill_sta_info(&vendor_sta_info, sta_info);
    nla_put(rsp_nbuf, NL80211_ATTR_VENDOR_DATA, sizeof(vendor_sta_info),
      &vendor_sta_info);
  MTLK_CLPB_FINALLY(res)
    mtlk_clpb_delete(clpb);
  MTLK_CLPB_END

  if (MTLK_ERR_OK == res) {
    return wv_cfg80211_vendor_cmd_reply(rsp_nbuf);
  }

end:
  return _mtlk_df_mtlk_to_linux_error_code(res);
}

static void _wave_radio_fill_vap_info(struct intel_vendor_vap_info* vendor_vap_info,
  struct driver_vap_info* vap_info)
{
#define ASSIGN_VAP_INFO_PARAM(name)                                 \
  do {                                                              \
    vendor_vap_info->name = vap_info->name;                         \
  } while (0)
#define ASSIGN_VAP_INFO_VAP_STATS_PARAM(name)                       \
  do {                                                              \
    vendor_vap_info->name =                                         \
      vap_info->vap_stats.name;                                     \
  } while (0)
#define ASSIGN_VAP_INFO_TRAFFIC_STATS_PARAM(name)                   \
  do {                                                              \
    vendor_vap_info->traffic_stats.name =                           \
      vap_info->vap_stats.traffic_stats.name;                       \
  } while (0)
#define ASSIGN_VAP_INFO_ERROR_STATS_PARAM(name)                     \
  do {                                                              \
    vendor_vap_info->error_stats.name =                             \
      vap_info->vap_stats.error_stats.name;                         \
  } while (0)

#define ASSIGN_VAP_INFO_RETRANS_STATS_PARAM(name)                   \
  do {                                                              \
    vendor_vap_info->name =                                         \
      vap_info->vap_stats.retrans_stats.name;                       \
  } while (0)

  MTLK_ASSERT(vendor_vap_info != NULL);
  MTLK_ASSERT(vap_info != NULL);

  memset(vendor_vap_info, 0, sizeof(*vendor_vap_info));
  ASSIGN_VAP_INFO_TRAFFIC_STATS_PARAM(BytesSent);
  ASSIGN_VAP_INFO_TRAFFIC_STATS_PARAM(BytesReceived);
  ASSIGN_VAP_INFO_TRAFFIC_STATS_PARAM(PacketsSent);
  ASSIGN_VAP_INFO_TRAFFIC_STATS_PARAM(PacketsReceived);
  ASSIGN_VAP_INFO_TRAFFIC_STATS_PARAM(UnicastPacketsSent);
  ASSIGN_VAP_INFO_TRAFFIC_STATS_PARAM(UnicastPacketsReceived);
  ASSIGN_VAP_INFO_TRAFFIC_STATS_PARAM(MulticastPacketsSent);
  ASSIGN_VAP_INFO_TRAFFIC_STATS_PARAM(MulticastPacketsReceived);
  ASSIGN_VAP_INFO_TRAFFIC_STATS_PARAM(BroadcastPacketsSent);
  ASSIGN_VAP_INFO_TRAFFIC_STATS_PARAM(BroadcastPacketsReceived);

  ASSIGN_VAP_INFO_ERROR_STATS_PARAM(ErrorsSent);
  ASSIGN_VAP_INFO_ERROR_STATS_PARAM(ErrorsReceived);
  ASSIGN_VAP_INFO_ERROR_STATS_PARAM(DiscardPacketsSent);
  ASSIGN_VAP_INFO_ERROR_STATS_PARAM(DiscardPacketsReceived);

  /* Excluding unavailable Retransmissions */
  ASSIGN_VAP_INFO_RETRANS_STATS_PARAM(RetransCount);
  ASSIGN_VAP_INFO_RETRANS_STATS_PARAM(FailedRetransCount);
  ASSIGN_VAP_INFO_RETRANS_STATS_PARAM(RetryCount);
  ASSIGN_VAP_INFO_RETRANS_STATS_PARAM(MultipleRetryCount);

  ASSIGN_VAP_INFO_VAP_STATS_PARAM(ACKFailureCount);
  ASSIGN_VAP_INFO_VAP_STATS_PARAM(AggregatedPacketCount);
  ASSIGN_VAP_INFO_VAP_STATS_PARAM(UnknownProtoPacketsReceived);

  ASSIGN_VAP_INFO_PARAM(TransmittedOctetsInAMSDUCount);
  ASSIGN_VAP_INFO_PARAM(ReceivedOctetsInAMSDUCount);
  ASSIGN_VAP_INFO_PARAM(TransmittedOctetsInAMPDUCount);
  ASSIGN_VAP_INFO_PARAM(ReceivedOctetsInAMPDUCount);
  ASSIGN_VAP_INFO_PARAM(RTSSuccessCount);
  ASSIGN_VAP_INFO_PARAM(RTSFailureCount);
  ASSIGN_VAP_INFO_PARAM(TransmittedAMSDUCount);
  ASSIGN_VAP_INFO_PARAM(FailedAMSDUCount);
  ASSIGN_VAP_INFO_PARAM(AMSDUAckFailureCount);
  ASSIGN_VAP_INFO_PARAM(ReceivedAMSDUCount);
  ASSIGN_VAP_INFO_PARAM(TransmittedAMPDUCount);
  ASSIGN_VAP_INFO_PARAM(TransmittedMPDUsInAMPDUCount);
  ASSIGN_VAP_INFO_PARAM(AMPDUReceivedCount);
  ASSIGN_VAP_INFO_PARAM(MPDUInReceivedAMPDUCount);
  ASSIGN_VAP_INFO_PARAM(ImplicitBARFailureCount);
  ASSIGN_VAP_INFO_PARAM(ExplicitBARFailureCount);
  ASSIGN_VAP_INFO_PARAM(TwentyMHzFrameTransmittedCount);
  ASSIGN_VAP_INFO_PARAM(FortyMHzFrameTransmittedCount);
  ASSIGN_VAP_INFO_PARAM(SwitchChannel20To40);
  ASSIGN_VAP_INFO_PARAM(SwitchChannel40To20);
  ASSIGN_VAP_INFO_PARAM(FrameDuplicateCount);

#undef ASSIGN_VAP_INFO_PARAM
#undef ASSIGN_VAP_INFO_VAP_STATS_PARAM
#undef ASSIGN_VAP_INFO_TRAFFIC_STATS_PARAM
#undef ASSIGN_VAP_INFO_ERROR_STATS_PARAM
#undef ASSIGN_VAP_INFO_RETRANS_STATS_PARAM
}

int wave_radio_vap_measurements_get(
  wave_radio_t *radio,
  struct net_device *ndev,
  const void *data,
  int data_len)
{
  void *wiphy;
  mtlk_df_user_t *df_user;
  mtlk_clpb_t *clpb = NULL;
  mtlk_nbuf_t *rsp_nbuf;
  int res = MTLK_ERR_OK;
  struct driver_vap_info *vap_info;
  struct intel_vendor_vap_info vendor_vap_info;
  uint32 vap_info_size;

  MTLK_ASSERT(NULL != radio);
  wiphy = wv_cfg80211_wiphy_get(radio->cfg80211);
  MTLK_ASSERT(NULL != wiphy);

  df_user = mtlk_df_user_from_ndev(ndev);

  ILOG2_SSD("%s: Invoked from %s (%i)", ndev->name, current->comm, current->pid);
  MTLK_CHECK_DF_USER(df_user);

  res = _mtlk_df_user_invoke_core(mtlk_df_user_get_df(df_user),
    WAVE_CORE_REQ_GET_VAP_MEASUREMENTS, &clpb, NULL, 0);
  res = _mtlk_df_user_process_core_retval(res, clpb,
    WAVE_CORE_REQ_GET_VAP_MEASUREMENTS, FALSE);

  if (res != MTLK_ERR_OK)
    goto end;

  vap_info = mtlk_clpb_enum_get_next(clpb, &vap_info_size);
  MTLK_CLPB_TRY(vap_info, vap_info_size)
    rsp_nbuf = wv_cfg80211_vendor_cmd_alloc_reply_skb(wiphy, sizeof(vendor_vap_info));
    if (!rsp_nbuf) {
      MTLK_CLPB_EXIT(MTLK_ERR_NO_MEM);
    }
    _wave_radio_fill_vap_info(&vendor_vap_info, vap_info);
    nla_put(rsp_nbuf, NL80211_ATTR_VENDOR_DATA, sizeof(vendor_vap_info),
      &vendor_vap_info);
  MTLK_CLPB_FINALLY(res)
    mtlk_clpb_delete(clpb);
  MTLK_CLPB_END

  if (MTLK_ERR_OK == res) {
    return wv_cfg80211_vendor_cmd_reply(rsp_nbuf);
  }

end:
  return _mtlk_df_mtlk_to_linux_error_code(res);
}

static void _wave_radio_fill_radio_info(struct intel_vendor_radio_info* vendor_radio_info,
  struct driver_radio_info* radio_info)
{
#define ASSIGN_RADIO_INFO_PARAM(name)                                   \
  do {                                                                  \
    vendor_radio_info->name = radio_info->name;                         \
  } while (0)
#define ASSIGN_RADIO_INFO_HW_PARAM(name)                                \
  do {                                                                  \
    vendor_radio_info->name = radio_info->tr181_hw.name;             \
  } while (0)
#define ASSIGN_RADIO_INFO_HW_STATS_PARAM(name)                          \
  do {                                                                  \
    vendor_radio_info->name = radio_info->tr181_hw_stats.name; \
  } while (0)
#define ASSIGN_RADIO_INFO_TRAFFIC_STATS_PARAM(name)                     \
  do {                                                                  \
    vendor_radio_info->traffic_stats.name =                    \
      radio_info->tr181_hw_stats.traffic_stats.name;                    \
  } while (0)
#define ASSIGN_RADIO_INFO_ERROR_STATS_PARAM(name)                       \
  do {                                                                  \
    vendor_radio_info->error_stats.name =                      \
      radio_info->tr181_hw_stats.error_stats.name;                      \
  } while (0)

  MTLK_ASSERT(vendor_radio_info != NULL);
  MTLK_ASSERT(radio_info != NULL);

  memset(vendor_radio_info, 0, sizeof(*vendor_radio_info));
  ASSIGN_RADIO_INFO_HW_PARAM(Enable);
  ASSIGN_RADIO_INFO_HW_PARAM(Channel);
  ASSIGN_RADIO_INFO_TRAFFIC_STATS_PARAM(BytesSent);
  ASSIGN_RADIO_INFO_TRAFFIC_STATS_PARAM(BytesReceived);
  ASSIGN_RADIO_INFO_TRAFFIC_STATS_PARAM(PacketsSent);
  ASSIGN_RADIO_INFO_TRAFFIC_STATS_PARAM(PacketsReceived);
  ASSIGN_RADIO_INFO_TRAFFIC_STATS_PARAM(UnicastPacketsSent);
  ASSIGN_RADIO_INFO_TRAFFIC_STATS_PARAM(UnicastPacketsReceived);
  ASSIGN_RADIO_INFO_TRAFFIC_STATS_PARAM(MulticastPacketsSent);
  ASSIGN_RADIO_INFO_TRAFFIC_STATS_PARAM(MulticastPacketsReceived);
  ASSIGN_RADIO_INFO_TRAFFIC_STATS_PARAM(BroadcastPacketsSent);
  ASSIGN_RADIO_INFO_TRAFFIC_STATS_PARAM(BroadcastPacketsReceived);
  ASSIGN_RADIO_INFO_ERROR_STATS_PARAM(ErrorsSent);
  ASSIGN_RADIO_INFO_ERROR_STATS_PARAM(ErrorsReceived);
  ASSIGN_RADIO_INFO_ERROR_STATS_PARAM(DiscardPacketsSent);
  ASSIGN_RADIO_INFO_ERROR_STATS_PARAM(DiscardPacketsReceived);
  ASSIGN_RADIO_INFO_HW_STATS_PARAM(FCSErrorCount);
  ASSIGN_RADIO_INFO_HW_STATS_PARAM(Noise);
  ASSIGN_RADIO_INFO_PARAM(tsf_start_time);
  ASSIGN_RADIO_INFO_PARAM(load);
  ASSIGN_RADIO_INFO_PARAM(tx_pwr_cfg);
  ASSIGN_RADIO_INFO_PARAM(num_tx_antennas);
  ASSIGN_RADIO_INFO_PARAM(num_rx_antennas);
  ASSIGN_RADIO_INFO_PARAM(primary_center_freq);
  ASSIGN_RADIO_INFO_PARAM(center_freq1);
  ASSIGN_RADIO_INFO_PARAM(center_freq2);
  ASSIGN_RADIO_INFO_PARAM(width);

#undef ASSIGN_RADIO_INFO_PARAM
#undef ASSIGN_RADIO_INFO_HW_PARAM
#undef ASSIGN_RADIO_INFO_HW_STATS_PARAM
#undef ASSIGN_RADIO_INFO_TRAFFIC_STATS_PARAM
#undef ASSIGN_RADIO_INFO_ERROR_STATS_PARAM
}

int wave_radio_radio_info_get(
  struct wiphy *wiphy,
  struct wireless_dev *wdev,
  const void *data,
  int data_len)
{
  struct net_device *ndev;
  mtlk_df_user_t *df_user = mtlk_df_user_from_wdev(wdev);
  mtlk_clpb_t *clpb = NULL;
  mtlk_nbuf_t *rsp_nbuf;
  int res = MTLK_ERR_OK;
  struct driver_radio_info *radio_info;
  struct intel_vendor_radio_info vendor_radio_info;
  uint32 radio_info_size;

  MTLK_ASSERT(NULL != wiphy);
  MTLK_ASSERT(NULL != wdev);

  ndev = wdev->netdev;
  MTLK_ASSERT(NULL != ndev);

  ILOG2_SSD("%s: Invoked from %s (%i)", ndev->name, current->comm, current->pid);
  MTLK_CHECK_DF_USER(df_user);

  res = _mtlk_df_user_invoke_core(mtlk_df_user_get_master_df(df_user),
            WAVE_RADIO_REQ_GET_RADIO_INFO, &clpb, NULL, 0);
  res = _mtlk_df_user_process_core_retval(res, clpb,
            WAVE_RADIO_REQ_GET_RADIO_INFO, FALSE);

  if (res != MTLK_ERR_OK)
    goto end;

  radio_info = mtlk_clpb_enum_get_next(clpb, &radio_info_size);
  MTLK_CLPB_TRY(radio_info, radio_info_size)
    rsp_nbuf = wv_cfg80211_vendor_cmd_alloc_reply_skb(wiphy, sizeof(vendor_radio_info));
    if (!rsp_nbuf) {
      MTLK_CLPB_EXIT(MTLK_ERR_NO_MEM);
    }
    _wave_radio_fill_radio_info(&vendor_radio_info, radio_info);
    nla_put(rsp_nbuf, NL80211_ATTR_VENDOR_DATA, sizeof(vendor_radio_info),
      &vendor_radio_info);
  MTLK_CLPB_FINALLY(res)
    mtlk_clpb_delete(clpb);
  MTLK_CLPB_END

  if (MTLK_ERR_OK == res) {
    return wv_cfg80211_vendor_cmd_reply(rsp_nbuf);
  }

end:
  return _mtlk_df_mtlk_to_linux_error_code(res);
}

int wave_radio_unconnected_sta_get(
  wave_radio_t *radio,
  struct wireless_dev *wdev,
  const void *data,
  int data_len)
{
  int res = MTLK_ERR_OK;
  mtlk_df_t *master_df;
  mtlk_vap_handle_t vap_handle;
  struct vendor_unconnected_sta_req_data_internal sta_req_data_internal;
  struct intel_vendor_unconnected_sta_req_cfg *sta_req_data =
    (struct intel_vendor_unconnected_sta_req_cfg *)data;

  MTLK_ASSERT(NULL != radio);
  MTLK_ASSERT(NULL != wdev);

  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);

  /*
   * Note: This is not supported in Gen6 FW yet.
   *       Executing SET_CHANNEL with switch_type == ST_RSSI leads to the FW crash,
   *       so this is prevented by the check and return in case of Gen6.
   */

  if (mtlk_hw_type_is_gen6(radio->hw_api->hw)) {
    ELOG_V("Not supported in Gen6 yet");
    res = MTLK_ERR_NOT_SUPPORTED;
    goto end;
  }

  /* station address */
  if (data_len != sizeof(*sta_req_data)) {
    ELOG_D("Wrong unconnected station data length: %d", data_len);
    res = MTLK_ERR_PARAMS;
    goto end;
  }

  if (NULL == data) {
    ELOG_V("Unconnected station data is NULL");
    res = MTLK_ERR_PARAMS;
    goto end;
  }
  if (mtlk_osal_eth_is_broadcast(sta_req_data->addr.au8Addr)) {
    ELOG_V("Cannot get unconnected station of broadcast address");
    res = MTLK_ERR_PARAMS;
    goto end;
  }
  if (!ieee_addr_is_valid(&sta_req_data->addr)) {
    ELOG_Y("Invalid MAC address %Y", data);
    res = MTLK_ERR_PARAMS;
    goto end;
  }

  master_df = wave_radio_df_get(radio);
  MTLK_ASSERT(master_df != NULL);

  vap_handle = mtlk_df_get_vap_handle(master_df);
  ILOG2_DY("CID-%04x: get unconnected station %Y",
    mtlk_vap_get_oid(vap_handle), data);

  memset(&sta_req_data_internal, 0, sizeof(sta_req_data_internal));
  sta_req_data_internal.addr = sta_req_data->addr;
  sta_req_data_internal.center_freq = sta_req_data->freq;
  sta_req_data_internal.cf1 = sta_req_data->center_freq1;
  sta_req_data_internal.cf2 = sta_req_data->center_freq2;
  sta_req_data_internal.chan_width = sta_req_data->bandwidth;
  sta_req_data_internal.wdev = wdev;
  _mtlk_df_user_invoke_core_async(master_df,
    WAVE_CORE_REQ_GET_UNCONNECTED_STATION, &sta_req_data_internal, sizeof(sta_req_data_internal),
    NULL, 0);

end:
  return _mtlk_df_mtlk_to_linux_error_code(res);
}

int wave_radio_set_atf_quotas (
  wave_radio_t *radio,
  struct net_device *ndev,
  const void *data,
  int total_len)
{
  void *wiphy;
  mtlk_df_user_t *df_user;
  mtlk_clpb_t *clpb = NULL;
  mtlk_nbuf_t *rsp_nbuf;
  uint8 *set_atf_result;
  uint32 ret_code_size;
  int res = MTLK_ERR_OK;
  int expected_len;

  MTLK_ASSERT(NULL != radio);
  df_user = mtlk_df_user_from_ndev(ndev);
  wiphy = wv_cfg80211_wiphy_get(radio->cfg80211);

  ILOG1_SSDD("%s: SET_ATF_QUOTAS invoked from %s (%i), msg_len = %d", ndev->name,
    current->comm, current->pid, total_len);
  MTLK_CHECK_DF_USER(df_user);

  if (NULL == data) {
    ELOG_V("SET_ATF_QUOTAS: is NULL");
    res = MTLK_ERR_PARAMS;
    goto end;
  }

  expected_len = sizeof(struct intel_vendor_atf_quotas) + ((struct intel_vendor_atf_quotas*)data)->data_len;
  if (total_len != expected_len) {
    ELOG_DD("SET_ATF_QUOTAS: invalid message length: expected: %d, got %d",
             expected_len, total_len);
    res = MTLK_ERR_PARAMS;
    goto end;
  }

  /* Invoke core to process the message further */
  res = _mtlk_df_user_invoke_core(mtlk_df_user_get_df(df_user),
          WAVE_CORE_REQ_SET_ATF_QUOTAS, &clpb, data, total_len);
  res = _mtlk_df_user_process_core_retval(res, clpb,
          WAVE_CORE_REQ_SET_ATF_QUOTAS, FALSE);
  if(res != MTLK_ERR_OK)
    goto end;

  set_atf_result = mtlk_clpb_enum_get_next(clpb, &ret_code_size);
  if ( (NULL == set_atf_result) || (sizeof(*set_atf_result) != ret_code_size) ) {
    mtlk_clpb_delete(clpb);
    res = MTLK_ERR_NO_MEM;
    goto end;
  }

  rsp_nbuf = wv_cfg80211_vendor_cmd_alloc_reply_skb(wiphy, sizeof(*set_atf_result));
  if (!rsp_nbuf) {
    mtlk_clpb_delete(clpb);
    res = MTLK_ERR_NO_MEM;
    goto end;
  }

  nla_put(rsp_nbuf, NL80211_ATTR_VENDOR_DATA, sizeof(*set_atf_result), set_atf_result);

  mtlk_clpb_delete(clpb);
  return wv_cfg80211_vendor_cmd_reply(rsp_nbuf);

end:
  return _mtlk_df_mtlk_to_linux_error_code(res);
}

int wave_radio_set_wds_wpa_sta (
  wave_radio_t *radio,
  struct net_device *ndev,
  const void *data,
  int data_len)
{
  int res = MTLK_ERR_OK;
  mtlk_df_user_t *df_user;
  mtlk_clpb_t *clpb = NULL;
  struct intel_vendor_mac_addr_list_cfg addrlist_cfg;

  MTLK_ASSERT(NULL != radio);
  df_user = mtlk_df_user_from_ndev(ndev);

  ILOG1_SSD("%s: Invoked from %s (%i)", ndev->name, current->comm, current->pid);
  MTLK_CHECK_DF_USER(df_user);

  if (data_len != sizeof(addrlist_cfg)) {
    ELOG_D("Wrong WDS WPA station list data length: %d", data_len);
    res = MTLK_ERR_PARAMS;
    goto end;
  }

  if (NULL == data) {
    ELOG_V("WDS WPA station list data is NULL");
    res = MTLK_ERR_PARAMS;
    goto end;
  }
  memset(&addrlist_cfg, 0, sizeof(addrlist_cfg));
  wave_memcpy(&addrlist_cfg, sizeof(addrlist_cfg), data, sizeof(addrlist_cfg));
  if (!ieee_addr_is_valid(&addrlist_cfg.addr)) {
    ELOG_Y("Invalid MAC address %Y", addrlist_cfg.addr.au8Addr);
    res = MTLK_ERR_PARAMS;
    goto end;
  }

  res = _mtlk_df_user_invoke_core(mtlk_df_user_get_df(df_user),
    WAVE_CORE_REQ_SET_WDS_WPA_LIST_ENTRY, &clpb, &addrlist_cfg,
    sizeof(addrlist_cfg));
  res = _mtlk_df_user_process_core_retval(res, clpb,
    WAVE_CORE_REQ_SET_WDS_WPA_LIST_ENTRY, TRUE);

end:
  return _mtlk_df_mtlk_to_linux_error_code(res);
}

int wave_radio_set_dgaf_disabled (
  wave_radio_t *radio,
  struct net_device *ndev,
  const void *data,
  int data_len)
{
  int res = MTLK_ERR_OK;
  mtlk_df_user_t *df_user;
  mtlk_clpb_t *clpb = NULL;
  uint32 dgaf_disabled;

  MTLK_ASSERT(NULL != radio);
  df_user = mtlk_df_user_from_ndev(ndev);

  ILOG1_SSD("%s: Invoked from %s (%i)", ndev->name, current->comm, current->pid);
  MTLK_CHECK_DF_USER(df_user);

  if (data_len != sizeof(dgaf_disabled)) {
    ELOG_D("Wrong dgaf_disabled data length: %d", data_len);
    res = MTLK_ERR_PARAMS;
    goto end;
  }

  if (NULL == data) {
    ELOG_V("dgaf_disabled data is NULL");
    res = MTLK_ERR_PARAMS;
    goto end;
  }
  wave_memcpy(&dgaf_disabled, sizeof(dgaf_disabled), data, sizeof(dgaf_disabled));

  res = _mtlk_df_user_invoke_core(mtlk_df_user_get_df(df_user),
          WAVE_CORE_REQ_SET_DGAF_DISABLED, &clpb, &dgaf_disabled, sizeof(dgaf_disabled));
  res = _mtlk_df_user_process_core_retval(res, clpb,
          WAVE_CORE_REQ_SET_DGAF_DISABLED, TRUE);

end:
  return _mtlk_df_mtlk_to_linux_error_code(res);
}

uint8 wave_radio_last_pm_freq_get(wave_radio_t *radio)
{
  MTLK_ASSERT(NULL != radio);
  return radio->last_pm_freq;
}

void wave_radio_last_pm_freq_set(wave_radio_t *radio, uint8 last_pm_freq)
{
  MTLK_ASSERT(NULL != radio);
  radio->last_pm_freq = last_pm_freq;
}

mtlk_pdb_t *wave_radio_param_db_get(wave_radio_t *radio)
{
  MTLK_ASSERT(NULL != radio);
  return radio->param_db;
}

unsigned wave_radio_scan_is_running(wave_radio_t *radio)
{
  MTLK_ASSERT(NULL != radio);
  return is_scan_running(&radio->scan_support);
}

unsigned wave_radio_scan_is_ignorant(wave_radio_t *radio)
{
  MTLK_ASSERT(NULL != radio);
  return is_scan_ignorant(&radio->scan_support);
}

uint32 wave_radio_scan_flags_get(wave_radio_t *radio)
{
  MTLK_ASSERT(NULL != radio);
  return radio->scan_support.flags;
}

mtlk_scan_support_t* wave_radio_scan_support_get(wave_radio_t *radio)
{
  MTLK_ASSERT(NULL != radio);
  return &radio->scan_support;
}

struct mtlk_chan_def* wave_radio_chandef_get(wave_radio_t *radio)
{
  MTLK_ASSERT(NULL != radio);
  return &radio->current_chandef;
}

int wave_radio_chan_switch_type_get(wave_radio_t *radio)
{
  MTLK_ASSERT(NULL != radio);
  return radio->chan_switch_type;
}

void wave_radio_chan_switch_type_set(wave_radio_t *radio, int value)
{
  MTLK_ASSERT(NULL != radio);
  radio->chan_switch_type = value;
}

UMI_HDK_CONFIG *wave_radio_hdkconfig_get(wave_radio_t *radio)
{
  MTLK_ASSERT(NULL != radio);
  return &radio->current_hdkconfig;
}

mtlk_coc_t *wave_radio_coc_mgmt_get(wave_radio_t *radio)
{
  MTLK_ASSERT(NULL != radio);
  return radio->coc_mgmt;
}

void wave_radio_interfdet_set(wave_radio_t *radio, BOOL enable_flag)
{
  MTLK_ASSERT(NULL != radio);
  radio->is_interfdet_enabled = enable_flag;
}

BOOL wave_radio_interfdet_get(wave_radio_t *radio)
{
  MTLK_ASSERT(NULL != radio);
  return radio->is_interfdet_enabled;
}

void __MTLK_IFUNC
wave_radio_set_sta_vifs_exist(wave_radio_t *radio, BOOL sta_exists)
{
  MTLK_ASSERT(radio != NULL);
  radio->sta_vifs_exist = sta_exists;
}

BOOL __MTLK_IFUNC
wave_radio_get_sta_vifs_exist(wave_radio_t *radio)
{
  MTLK_ASSERT(radio != NULL);
  return radio->sta_vifs_exist;
}

void wave_radio_sta_cnt_inc (wave_radio_t *radio)
{
  MTLK_ASSERT(radio != NULL);
  atomic_inc(&radio->sta_cnt);
}

void wave_radio_sta_cnt_dec (wave_radio_t *radio)
{
  int res;

  MTLK_ASSERT(radio != NULL);
  res = atomic_dec_return(&radio->sta_cnt);

  MTLK_ASSERT(res >= 0);
  MTLK_UNREFERENCED_PARAM(res);
}

int wave_radio_sta_cnt_get (wave_radio_t *radio)
{
  MTLK_ASSERT(radio != NULL);
  return atomic_read(&radio->sta_cnt);
}

/* radio mode configuration */
uint32 wave_radio_mode_get(wave_radio_t *radio)
{
  MTLK_ASSERT(radio != NULL);
  return WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_MODE_CURRENT);
}

void wave_radio_mode_set(wave_radio_t *radio, const uint32 radio_mode)
{
  MTLK_ASSERT(radio != NULL);
  WAVE_RADIO_PDB_SET_INT(radio, PARAM_DB_RADIO_MODE_REQUESTED, radio_mode);
}

wv_mac80211_t * __MTLK_IFUNC
wave_radio_mac80211_get (wave_radio_t *radio)
{
  MTLK_ASSERT(NULL != radio);
  return radio->mac80211;
}

void __MTLK_IFUNC
wave_radio_recover_sta_vifs (wave_radio_t *radio)
{
  return wv_mac80211_recover_sta_vifs(radio->mac80211);
}

struct ieee80211_hw * __MTLK_IFUNC
wave_radio_ieee80211_hw_get (wave_radio_t *radio)
{
  MTLK_ASSERT(NULL != radio);

  return wv_ieee80211_hw_get(radio->mac80211);
}

wv_cfg80211_t * __MTLK_IFUNC
wave_radio_cfg80211_get (wave_radio_t *radio)
{
  MTLK_ASSERT(radio != NULL);
  return radio->cfg80211;
}

struct  _wave_radio_t * __MTLK_IFUNC
wave_radio_descr_wave_radio_get (wave_radio_descr_t *radio_descr, unsigned idx)
{
  MTLK_ASSERT(idx < radio_descr->num_radios);
  if (idx < radio_descr->num_radios) {
    return &radio_descr->radio[idx];
  } else {
    return NULL;
  }
}

void __MTLK_IFUNC
wave_radio_fixed_pwr_params_get (wave_radio_t *radio, FixedPower_t *fixed_pwr_params)
{
  mtlk_pdb_size_t fixed_pwr_cfg_size = sizeof(*fixed_pwr_params);

  MTLK_ASSERT(NULL != radio);
  if (MTLK_ERR_OK != WAVE_RADIO_PDB_GET_BINARY(radio, PARAM_DB_RADIO_FIXED_PWR, fixed_pwr_params, &fixed_pwr_cfg_size)) {
    ELOG_V("Failed to get Fixed TX management power parameters");
  }
}

int __MTLK_IFUNC
wave_radio_cca_threshold_set (wave_radio_t *radio, iwpriv_cca_th_t *cca_th)
{
  int res;

  MTLK_ASSERT(radio);
  MTLK_ASSERT(cca_th);

  if((!radio) || (!cca_th)) {
    return MTLK_ERR_NOT_IN_USE;
  }

  cca_th->is_updated = 1;

  res = WAVE_RADIO_PDB_SET_BINARY(radio, PARAM_DB_RADIO_CCA_THRESHOLD, cca_th, sizeof(*cca_th));
  if (MTLK_ERR_OK != res) {
    ELOG_V("Can't store CCA_TH in PDB");
  }

  return res;
}

int __MTLK_IFUNC
wave_radio_cca_threshold_get (wave_radio_t *radio, iwpriv_cca_th_t *cca_th)
{
  mtlk_pdb_size_t size = sizeof(*cca_th);

  MTLK_ASSERT(radio);
  MTLK_ASSERT(cca_th);

  if((!radio) || (!cca_th)) {
    return MTLK_ERR_NOT_IN_USE;
  }

  memset(cca_th, 0, size);
  if (MTLK_ERR_OK != WAVE_RADIO_PDB_GET_BINARY(radio,
                        PARAM_DB_RADIO_CCA_THRESHOLD, cca_th, &size)) {
    ELOG_V("Failed to get CCA_TH from PDB");
  } else {
    MTLK_ASSERT(sizeof(*cca_th) == size);
  }

  return MTLK_ERR_OK;
}

BOOL __MTLK_IFUNC
wave_radio_progmodel_loaded_get (struct _wave_radio_t *radio)
{
  MTLK_ASSERT(NULL != radio);

  return radio->progmodel_loaded;
}

void __MTLK_IFUNC
wave_radio_progmodel_loaded_set (struct _wave_radio_t *radio, BOOL is_loaded)
{
  MTLK_ASSERT(NULL != radio);

  radio->progmodel_loaded = is_loaded;
}

void wave_radio_limits_set(wave_radio_descr_t *radio_descr, wave_radio_limits_t *radio_limits)
{
  wave_radio_t *radio;
  unsigned      i;

  MTLK_ASSERT(radio_descr != NULL);
  MTLK_ASSERT(radio_limits != NULL);

  radio = &radio_descr->radio[0];

  for (i = 0; i < radio_descr->num_radios; i++, radio++) {
    radio->limits.max_stas = radio_limits[i].max_stas;
    radio->limits.max_vaps = radio_limits[i].max_vaps;
    /* FIXME: set a non-zero max_vaps (temp solution) */
    if (0 == radio->limits.max_vaps) {
      radio->limits.max_vaps = 1;
    }
    ILOG0_DDD("RadioID %u supports max VAPs %u max STAs %u", i, radio->limits.max_vaps, radio->limits.max_stas);
  }
}

unsigned wave_radio_max_stas_get(wave_radio_t *radio)
{
  MTLK_ASSERT(radio != NULL);
  ILOG3_DD("RadioID %u max STAs %u", radio->idx, radio->limits.max_stas);
  return radio->limits.max_stas;
}

unsigned wave_radio_max_vaps_get(wave_radio_t *radio)
{
  MTLK_ASSERT(radio != NULL);
  ILOG3_DD("RadioID %u max VAPs %u", radio->idx, radio->limits.max_vaps);
  return radio->limits.max_vaps;
}

BOOL __MTLK_IFUNC
wave_radio_is_phy_dummy (wave_radio_t *radio)
{
  MTLK_ASSERT(radio != NULL);
  return wave_hw_mmb_card_is_phy_dummy(radio->hw_api->hw);
}

#define ADD_CH_TO_TAB(ch_tab_, max_size_, id_str_,channel_) \
  if(radio->ch_tab_.num_of_chan >= max_size_) { \
    ELOG_S("No more entrie in table %s", id_str_); \
    return MTLK_ERR_PARAMS; \
  } \
  radio->ch_tab_.channel[radio->ch_tab_.num_of_chan++] = channel_;

static int _wave_radio_supported_ch_add(wave_radio_t *radio, uint8 channel, wave_radio_chan_type_t ch_type)
{
  if (ch_type < WAVE_RADIO_CHAN_G24_20 || ch_type > WAVE_RADIO_CHAN_G52_160) {
    ELOG_D("Wrong chan_type %d", ch_type);
    return MTLK_ERR_PARAMS;
  }

  switch (ch_type) {
    case WAVE_RADIO_CHAN_G24_20:
      ADD_CH_TO_TAB(g24_20_ch_tab, G24_20_CH_NUM_MAX, "g24_20_ch_tab", channel);
      break;
    case WAVE_RADIO_CHAN_G24_40:
      ADD_CH_TO_TAB(g24_40_ch_tab, G24_40_CH_NUM_MAX, "g24_40_ch_tab", channel);
      break;
    case WAVE_RADIO_CHAN_G52_20:
      ADD_CH_TO_TAB(g52_20_ch_tab, G52_20_CH_NUM_MAX, "g52_20_ch_tab", channel);
      break;
    case WAVE_RADIO_CHAN_G52_40:
      ADD_CH_TO_TAB(g52_40_ch_tab, G52_40_CH_NUM_MAX, "g52_40_ch_tab", channel);
      break;
    case WAVE_RADIO_CHAN_G52_80:
      ADD_CH_TO_TAB(g52_80_ch_tab, G52_80_CH_NUM_MAX, "g52_80_ch_tab", channel);
      break;
    case WAVE_RADIO_CHAN_G52_160:
      ADD_CH_TO_TAB(g52_160_ch_tab, G52_160_CH_NUM_MAX, "g52_160_ch_tab", channel);
      break;
  }

  return MTLK_ERR_OK;
}

#define SKIP_CHANNEL(ch_, search_tab_, idx_, max_size_) \
  if(idx_ <= max_size_) { \
    if(ch_ == search_tab_[idx_]) { \
      idx_++; \
    } \
  }

/* If enabled - calibrate only regulatory allowed channels */
#define WAVE_REGULATORY_CHANNEL_CHECK 0

int wave_radio_channel_table_build_2ghz(wave_radio_t *radio, struct ieee80211_channel *channels, int n_channels)
{
  int res = MTLK_ERR_OK;
  uint8 ch_num;
  wave_int i, ch20_idx, ch40_idx;

  MTLK_ASSERT(NULL != radio);
  MTLK_ASSERT(NULL != channels);
  if (0 == n_channels) {
    ELOG_V("No channels");
    return MTLK_ERR_PARAMS;
  }

  ch20_idx = ch40_idx = 0;

  for (i = 0; i < n_channels; i++, channels++) {
    /* freq to channel number */
    ch_num = ieee80211_frequency_to_channel(channels->center_freq);
    if (0 == ch_num) {
      ELOG_D("ch_num is 0 for center_freq %d", channels->center_freq);
      res = MTLK_ERR_PARAMS;
      break;
    }
    /* skip disabled channel */
    if (channels->flags & IEEE80211_CHAN_DISABLED) {
      ILOG0_D("channel %d disabled by regulatory", ch_num);
#if WAVE_REGULATORY_CHANNEL_CHECK
      /* rewind search tables in case of match */
      SKIP_CHANNEL(ch_num, g24_20_supported_ch, ch20_idx, G24_20_CH_NUM_MAX);
      SKIP_CHANNEL(ch_num, g24_40_supported_ch, ch40_idx, G24_40_CH_NUM_MAX);
      continue;
#endif
    }

    /* add 20MHz channel */
    if ((ch20_idx < G24_20_CH_NUM_MAX) && (ch_num == g24_20_supported_ch[ch20_idx])) {
      ch20_idx++;
      res = _wave_radio_supported_ch_add(radio, ch_num, WAVE_RADIO_CHAN_G24_20);
      if (res != MTLK_ERR_OK)
        break;
    }
    /* add 40MHz*/
    if ((ch40_idx < G24_40_CH_NUM_MAX) && (ch_num == g24_40_supported_ch[ch40_idx])) {
      ch40_idx++;
#if WAVE_REGULATORY_CHANNEL_CHECK
      if (IEEE80211_CHAN_NO_HT40 != (channels->flags & IEEE80211_CHAN_NO_HT40)) {
        res = _wave_radio_supported_ch_add(radio, ch_num, WAVE_RADIO_CHAN_G24_40);
        if (res != MTLK_ERR_OK)
          break;
      }
#else
      res = _wave_radio_supported_ch_add(radio, ch_num, WAVE_RADIO_CHAN_G24_40);
      if (res != MTLK_ERR_OK)
        break;
#endif
    }
  }

  return res;
}

int wave_radio_channel_table_build_5ghz(wave_radio_t *radio, struct ieee80211_channel *channels, int n_channels)
{
  int res = MTLK_ERR_OK;
  uint8 ch_num;
  wave_int i, ch20_idx, ch40_idx, ch80_idx, ch160_idx;

  MTLK_ASSERT(NULL != radio);
  MTLK_ASSERT(NULL != channels);
  if (0 == n_channels) {
    ELOG_V("No channels");
    return MTLK_ERR_PARAMS;
  }

  ch20_idx = ch40_idx = ch80_idx = ch160_idx = 0;

  for (i = 0; i < n_channels; i++, channels++) {
    /* freq to channel number */
    ch_num = ieee80211_frequency_to_channel(channels->center_freq);
    if (0 == ch_num) {
      ELOG_D("ch_num is 0 for center_freq %d", channels->center_freq);
      res = MTLK_ERR_PARAMS;
      break;
    }
    /* skip disabled channel */
    if (channels->flags & IEEE80211_CHAN_DISABLED) {
      ILOG0_D("channel %d disabled by regulatory", ch_num);
#if WAVE_REGULATORY_CHANNEL_CHECK
      /* rewind search tables in case of match */
      SKIP_CHANNEL(ch_num,  g52_20_supported_ch,  ch20_idx,  G52_20_CH_NUM_MAX);
      SKIP_CHANNEL(ch_num,  g52_40_supported_ch,  ch40_idx,  G52_40_CH_NUM_MAX);
      SKIP_CHANNEL(ch_num,  g52_80_supported_ch,  ch80_idx,  G52_80_CH_NUM_MAX);
      SKIP_CHANNEL(ch_num, g52_160_supported_ch, ch160_idx, G52_160_CH_NUM_MAX);
#endif
      continue;
    }

    /* add 20MHz channel */
    if ((ch20_idx < G52_20_CH_NUM_MAX) && (ch_num == g52_20_supported_ch[ch20_idx])) {
      ch20_idx++;
      res = _wave_radio_supported_ch_add(radio, ch_num, WAVE_RADIO_CHAN_G52_20);
      if (res != MTLK_ERR_OK)
        break;
    }
    /* add 40MHz*/
    if ((ch40_idx < G52_40_CH_NUM_MAX) && (ch_num == g52_40_supported_ch[ch40_idx])) {
      ch40_idx++;
#if WAVE_REGULATORY_CHANNEL_CHECK
      if (IEEE80211_CHAN_NO_HT40 != (channels->flags & IEEE80211_CHAN_NO_HT40)) {
        res = _wave_radio_supported_ch_add(radio, ch_num, WAVE_RADIO_CHAN_G52_40);
        if (res != MTLK_ERR_OK)
          break;
      }
#else
      res = _wave_radio_supported_ch_add(radio, ch_num, WAVE_RADIO_CHAN_G52_40);
      if (res != MTLK_ERR_OK)
        break;
#endif
    }
    /* add 80MHz*/
    if ((ch80_idx < G52_80_CH_NUM_MAX) && (ch_num == g52_80_supported_ch[ch80_idx])) {
      ch80_idx++;
#if WAVE_REGULATORY_CHANNEL_CHECK
      if (0 == (channels->flags & IEEE80211_CHAN_NO_80MHZ)) {
        res = _wave_radio_supported_ch_add(radio, ch_num, WAVE_RADIO_CHAN_G52_80);
        if (res != MTLK_ERR_OK)
          break;
      }
#else
      res = _wave_radio_supported_ch_add(radio, ch_num, WAVE_RADIO_CHAN_G52_80);
      if (res != MTLK_ERR_OK)
        break;
#endif
    }
    /* add 160MHz only on Gen6 */
    if (!mtlk_hw_type_is_gen6(radio->hw_api->hw))
        continue; /* skip */

    if ((ch160_idx < G52_160_CH_NUM_MAX) && (ch_num == g52_160_supported_ch[ch160_idx])) {
      ch160_idx++;
#if WAVE_REGULATORY_CHANNEL_CHECK
      if (0 == (channels->flags & IEEE80211_CHAN_NO_160MHZ)) {
        res = _wave_radio_supported_ch_add(radio, ch_num, WAVE_RADIO_CHAN_G52_160);
        if (res != MTLK_ERR_OK)
          break;
      }
#else
      res = _wave_radio_supported_ch_add(radio, ch_num, WAVE_RADIO_CHAN_G52_160);
      if (res != MTLK_ERR_OK)
        break;
#endif
    }
  }

  return res;
}

static void
_wave_radio_channel_table_print (const char *name, uint8 *channels, uint8 num_of_chan)
{
  if (num_of_chan) {
    ILOG1_SD("%s: %d", name, num_of_chan);
    mtlk_dump(2, channels, num_of_chan, "channels");
  }
}

void wave_radio_channel_table_print(wave_radio_t *radio)
{
  MTLK_ASSERT(radio != NULL);

  ILOG1_D("RadioID:%u", radio->idx);

#define _PRINT_CHANNEL_TABLE_(name, table) \
          _wave_radio_channel_table_print(name, table.channel, table.num_of_chan)

  _PRINT_CHANNEL_TABLE_("2GHz channels supported 20MHz",  radio->g24_20_ch_tab);
  _PRINT_CHANNEL_TABLE_("2GHz channels supported 40MHz",  radio->g24_40_ch_tab);
  _PRINT_CHANNEL_TABLE_("5GHz channels supported 20MHz",  radio->g52_20_ch_tab);
  _PRINT_CHANNEL_TABLE_("5GHz channels supported 40MHz",  radio->g52_40_ch_tab);
  _PRINT_CHANNEL_TABLE_("5GHz channels supported 80MHz",  radio->g52_80_ch_tab);
  _PRINT_CHANNEL_TABLE_("5GHz channels supported 160MHz", radio->g52_160_ch_tab);

#undef _PRINT_CHANNEL_TABLE_
}

/* FIXME: prototype */
int __MTLK_IFUNC    mtlk_efuse_eeprom_load_to_hw (mtlk_hw_t *hw, mtlk_txmm_t *txmm, mtlk_hw_band_e band);
int __MTLK_IFUNC    mtlk_psdb_load_to_hw(mtlk_hw_t *hw, mtlk_txmm_t *txmm);
int __MTLK_IFUNC    mtlk_hw_send_hdk_antennas_config(mtlk_hw_t *hw);

static int
_wave_radio_hdk_config_send(wave_radio_t *radio, mtlk_txmm_t *txmm, uint32 offline_mask, uint32 online_mask)
{
  UMI_HDK_CONFIG   *current_hdkconfig = &radio->current_hdkconfig;
  UMI_HDK_CONFIG    updated_hdkconfig;
  UMI_HDK_CONFIG   *req;
  size_t            size = sizeof(UMI_HDK_CONFIG);
  mtlk_txmm_msg_t   man_msg;
  mtlk_txmm_data_t *man_entry;
  wave_int          band;
  int res = MTLK_ERR_OK;

  band = radio->wave_band;
  ILOG0_DDD("band=%u, offline_algo_mask=0x%08x, online_algo_mask=0x%08x",
            band, offline_mask, online_mask);

  /* prepare msg for the FW */
  if (!(man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, txmm, &res)))
  {
    ELOG_D("UM_MAN_HDK_CONFIG_REQ init failed, err=%i", res);
    res = MTLK_ERR_NO_RESOURCES;
    goto end;
  }

  man_entry->id = UM_MAN_HDK_CONFIG_REQ;
  man_entry->payload_size = size;
  req = (UMI_HDK_CONFIG *)man_entry->payload;

  /* copy everything from our saved copy */
  *req = *current_hdkconfig; /* struct copy */

  /* change the things that can dynamically change */
  req->hdkConf.band = band;
  req->hdkConf.calibrationMasks.offlineCalMask = HOST_TO_MAC32(offline_mask);
  req->hdkConf.calibrationMasks.onlineCalMask  = HOST_TO_MAC32(online_mask);

  /* copy updated config before sending */
  updated_hdkconfig = *req; /* struct copy */

  SLOG0(0, 0, UMI_HDK_CONFIG, req);
  mtlk_dump(0, req, size, "dump of HDK_CONFIG_REQ");

  res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);
  if (res != MTLK_ERR_OK)
    ELOG_D("UM_MAN_HDK_CONFIG_REQ send failed, err=%i", res);
  else {
    /* we succeeded, so just save our new settings */
    *current_hdkconfig = updated_hdkconfig; /* struct copy */
    WAVE_RADIO_PDB_SET_INT(radio, PARAM_DB_RADIO_CALIBRATION_ALGO_MASK, offline_mask);
    WAVE_RADIO_PDB_SET_INT(radio, PARAM_DB_RADIO_ONLINE_ACM, online_mask);
  }

  mtlk_txmm_msg_cleanup(&man_msg);

end:
  return res;
}

int __MTLK_IFUNC
wave_radio_send_hdk_config(wave_radio_t *radio, uint32 offline_mask, uint32 online_mask)
{
  MTLK_ASSERT(radio);

  if (radio == NULL) {
    return MTLK_ERR_PARAMS;
  }

  return _wave_radio_hdk_config_send(radio, _wave_radio_txmm_get(radio), offline_mask, online_mask);
}


int __MTLK_IFUNC
wave_radio_read_hdk_config(wave_radio_t *radio, uint32 *offline_mask, uint32 *online_mask)
{
  mtlk_txmm_t      *txmm;
  UMI_HDK_CONFIG   *req;
  mtlk_txmm_msg_t   man_msg;
  mtlk_txmm_data_t *man_entry;
  size_t            size = sizeof(UMI_HDK_CONFIG);
  int               res;

  MTLK_ASSERT(radio);

  if (radio == NULL) {
    return MTLK_ERR_PARAMS;
  }

  txmm = _wave_radio_txmm_get(radio);
  MTLK_ASSERT(txmm);
  if (!txmm) {
    return MTLK_ERR_PARAMS;
  }

  /* prepare msg for the FW */
  if (!(man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, txmm, &res)))
  {
    ELOG_D("UM_MAN_HDK_CONFIG_REQ init failed, err=%i", res);
    return MTLK_ERR_NO_RESOURCES;
  }

  man_entry->id = UM_MAN_HDK_CONFIG_REQ;
  man_entry->payload_size = size;
  req = (UMI_HDK_CONFIG *)man_entry->payload;
  req->hdkConf.band = radio->wave_band;
  req->getSetOperation = API_GET_OPERATION;

  res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);
  if (res != MTLK_ERR_OK)
    ELOG_D("UM_MAN_HDK_CONFIG_REQ send failed, err=%i", res);
  else {
    *offline_mask = MAC_TO_HOST32(req->hdkConf.calibrationMasks.offlineCalMask);
    *online_mask  = MAC_TO_HOST32(req->hdkConf.calibrationMasks.onlineCalMask);
    ILOG2_DDD("RadioID %u: online mask 0x%x offline mask 0x%x", radio->idx, *online_mask, *offline_mask);
  }
  mtlk_txmm_msg_cleanup(&man_msg);

  return res;
}

static __INLINE BOOL
__wave_radio_is_first (wave_radio_t *radio)
{
  return wave_radio_id_is_first(radio->idx);
}

BOOL __MTLK_IFUNC
wave_radio_is_first (wave_radio_t *radio)
{
  MTLK_ASSERT(radio != NULL);
  return __wave_radio_is_first(radio);
}

static __INLINE BOOL
__wave_radio_is_gen6 (wave_radio_t *radio)
{
  return mtlk_hw_type_is_gen6(radio->hw_api->hw);
}


BOOL __MTLK_IFUNC
wave_radio_is_gen6 (wave_radio_t *radio)
{
  MTLK_ASSERT(radio != NULL);
  return __wave_radio_is_gen6(radio);
}


static int _wave_radio_dummy_vap_add (wave_radio_t *radio, mtlk_txmm_t *txmm)
{
  int res = MTLK_ERR_OK;
  mtlk_txmm_data_t* man_entry = NULL;
  mtlk_txmm_msg_t man_msg;
  UMI_ADD_VAP *req;

  ILOG2_V("Adding VAP");
  man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, txmm, &res);
  if (man_entry == NULL)
  {
    ELOG_V("Can't send UMI_ADD_VAP request to MAC due to the lack of MAN_MSG");
    return res;
  }

  man_entry->id = UM_MAN_ADD_VAP_REQ;
  man_entry->payload_size = sizeof(*req);

  req = (UMI_ADD_VAP *)man_entry->payload;
  memset(req, 0, sizeof(*req));

  ieee_addr_set(&req->sBSSID, mtlk_eeprom_get_nic_mac_addr(radio->ee_data));
  req->sBSSID.au8Addr[ETH_ALEN - 1] += radio->idx; /* unique for every radio */
  ILOG0_Y("Mac addr for calibration VAP is %Y", &req->sBSSID);

  req->vapId          = 0;
  /*
   * At this stage no need for:
   *  u8Rates
   *  u8Rates_Length
   */
  req->operationMode  = OPERATION_MODE_NORMAL;

  mtlk_dump(2, req, sizeof(*req), "dump of UMI_ADD_VAP");

  res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);
  if (res != MTLK_ERR_OK) {
    ELOG_D("Cannot send ADD_VAP request due to TXMM err#%d", res);
    goto FINISH;
  }

  ILOG2_V("VAP added");

FINISH:
  if (man_entry)
    mtlk_txmm_msg_cleanup(&man_msg);

  return res;
}

static int _wave_radio_dummy_vap_del(wave_radio_t *radio, mtlk_txmm_t *txmm)
{
  mtlk_txmm_msg_t man_msg;
  mtlk_txmm_data_t *man_entry = NULL;
  UMI_REMOVE_VAP *req;
  int result = MTLK_ERR_OK;

  man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, txmm, &result);
  if (man_entry == NULL) {
    ELOG_V("Can't send UM_MAN_REMOVE_VAP_REQ due to the lack of MAN_MSG");
    goto FINISH;
  }

  man_entry->id           = UM_MAN_REMOVE_VAP_REQ;
  man_entry->payload_size = sizeof(*req);
  req = (UMI_REMOVE_VAP *) man_entry->payload;

  req->u16Status = HOST_TO_MAC16(UMI_OK);
  req->vapId = 0;

  mtlk_dump(2, req, sizeof(*req), "dump of UMI_REMOVE_VAP:");

  result = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);
  if (result != MTLK_ERR_OK) {
    ELOG_D("Can't send UM_MAN_REMOVE_VAP_REQ (err=%d)", result);
    goto FINISH;
  }

  if (MAC_TO_HOST16(req->u16Status) != UMI_OK) {
    WLOG_D("UM_MAN_REMOVE_VAP_REQ failed in FW (status=%u)", MAC_TO_HOST16(req->u16Status));
    result = MTLK_ERR_MAC;
    goto FINISH;
  }

  ILOG0_DD("VAP is removed (status %u), result %d", MAC_TO_HOST16(req->u16Status), result);

FINISH:
  if (man_entry)
    mtlk_txmm_msg_cleanup(&man_msg);

  return result;
}

static int
_wave_radio_calibrate_prepare (wave_radio_t *radio, mtlk_txmm_t *txmm, mtlk_hw_band_e band,
                               uint32 param_algomask, uint32 param_oalgomask)
{
  mtlk_hw_t *hw;
  int res;

  MTLK_ASSERT(radio != NULL);
  MTLK_ASSERT(txmm != NULL);

  hw = radio->hw_api->hw;

  /* download progmodels */
  if (MTLK_ERR_OK != (res = wave_progmodel_load(txmm, radio, radio->wave_band)))
  {
    ELOG_V("Error while downloading progmodel files");
    return res;
  }

  /* CCA thresholds */
  res = mtlk_core_cfg_send_actual_cca_threshold(
            mtlk_vap_manager_get_master_core(radio->vap_manager));
  if (MTLK_ERR_OK != res) {
    return res; /* Error already logged */
  }

  /* EFUSE/EEPROM specific */
  if (MTLK_ERR_OK != (res = mtlk_efuse_eeprom_load_to_hw(hw, txmm, radio->wave_band))) {
    return res; /* Error already logged */
  }

  /* send HDK config */
  if (MTLK_ERR_OK != (res = _wave_radio_hdk_config_send(radio, txmm, param_algomask, param_oalgomask)))
  {
    /* Error already logged */
    return res;
  }

  /* Set HDK antennas configuration message */
  /* should be send once - when first band goes up */
  if (__wave_radio_is_first(radio)) {
    if (MTLK_ERR_OK != (res = mtlk_hw_send_hdk_antennas_config(hw))) {
      /* Error already logged */
      return res;
    }
  }

  /* Send Platform Specific */
  if (MTLK_ERR_OK != (res = mtlk_psdb_load_to_hw(hw, txmm))) {
    /* Error already logged */
    return res;
  }

  return MTLK_ERR_OK;
}

/* FIXME: HW dependent timeout */
#define MTLK_MM_BLOCKED_CALIBRATION_TIMEOUT_GEN5  20000 /* ms */
#define MTLK_MM_BLOCKED_CALIBRATION_TIMEOUT_GEN6 300000 /* ms */

static int
_wave_radio_calibrate_send (wave_radio_t *radio, mtlk_txmm_t *txmm,
                            mtlk_hw_band_e band, unsigned cw,
                            uint8 *chans, unsigned num_chans)
{
  mtlk_txmm_msg_t man_msg;
  mtlk_txmm_data_t *man_entry;
  UMI_CALIBRATE_PARAMS *req;
  int res = MTLK_ERR_OK;

  MTLK_ASSERT(MAX_CALIB_CHANS >= num_chans);

  /* limit number of channels to calibrate */
  if (num_chans > MAX_CALIB_CHANS) {
    WLOG_DD("num_chans(%d) > MAX_CALIB_CHANS(%d)", num_chans, MAX_CALIB_CHANS);
    num_chans = MAX_CALIB_CHANS;
  }

  /* prepare msg for the FW */
  if (!(man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, txmm, &res)))
  {
    ELOG_D("UM_MAN_CALIBRATE_REQ init failed, err=%d", res);
    res = MTLK_ERR_NO_RESOURCES;
    goto end;
  }

  man_entry->id = UM_MAN_CALIBRATE_REQ;
  man_entry->payload_size = sizeof(*req);
  req = (UMI_CALIBRATE_PARAMS *) man_entry->payload;
  memset(req, 0, sizeof(*req));

  wave_memcpy(req->chan_nums, sizeof(req->chan_nums), chans, num_chans);
  req->num_chans = (uint8)num_chans;
  req->chan_width = (uint8)cw;

  mtlk_dump(1, req, sizeof(*req), "dump of UMI_CALIBRATE_PARAMS:");

  res = mtlk_txmm_msg_send_blocked(&man_msg,
                                   mtlk_hw_type_is_gen5(radio->hw_api->hw) ?
                                   MTLK_MM_BLOCKED_CALIBRATION_TIMEOUT_GEN5 :
                                   MTLK_MM_BLOCKED_CALIBRATION_TIMEOUT_GEN6);

  if (res != MTLK_ERR_OK)
  {
      ELOG_D("UM_MAN_CALIBRATE_REQ send failed, err=%i", res);
  }

  mtlk_txmm_msg_cleanup(&man_msg);

end:
  return res;
}

static void _wave_radio_calibration_status_set(wave_radio_t *radio,
                                               uint8 *channels,
                                               uint8 nof_chan,
                                               uint8 ch_width)
{
  unsigned cw_bit = 1 << ch_width;
  int i;

  for (i = 0; i < nof_chan; i++) {
    unsigned idx = (unsigned)channum2cssidx(channels[i]);
    if (idx < ARRAY_SIZE(radio->calib_done_mask)) {
      radio->calib_done_mask[idx] |= cw_bit;
    } else {
      ELOG_D("idx %d outside of array size", idx);
    }
  }
}

static int _wave_radio_2G_calibrate(wave_radio_t *radio, mtlk_txmm_t *txmm)
{
  int res = MTLK_ERR_OK;

  /* 2.4 Ghz 20 Mhz channels*/
  if (radio->g24_20_ch_tab.num_of_chan) {
    ILOG0_V("20Mhz channels");
    res = _wave_radio_calibrate_send(radio, txmm, radio->wave_band,
                                     radio->g24_20_ch_tab.ch_width,
                                     radio->g24_20_ch_tab.channel,
                                     radio->g24_20_ch_tab.num_of_chan);
    if (MTLK_ERR_OK != res)
      return res;

    _wave_radio_calibration_status_set(radio,
                                       radio->g24_20_ch_tab.channel,
                                       radio->g24_20_ch_tab.num_of_chan,
                                       CW_20);
  }

  /* 2.4 Ghz 40 Mhz channels*/
  if (radio->g24_40_ch_tab.num_of_chan) {
    ILOG0_V("40Mhz channels");
    res = _wave_radio_calibrate_send(radio, txmm, radio->wave_band,
                                     radio->g24_40_ch_tab.ch_width,
                                     radio->g24_40_ch_tab.channel,
                                     radio->g24_40_ch_tab.num_of_chan);
    if (MTLK_ERR_OK != res)
      return res;

    _wave_radio_calibration_status_set(radio,
                                       radio->g24_40_ch_tab.channel,
                                       radio->g24_40_ch_tab.num_of_chan,
                                       CW_40);
  }

  return res;
}

static int _wave_radio_5G_calibrate(wave_radio_t *radio, mtlk_txmm_t *txmm)
{
  int res = MTLK_ERR_OK;

  /* 5.2 Ghz 20 Mhz channels*/
  if (radio->g52_20_ch_tab.num_of_chan) {
    ILOG0_V("20Mhz channels");
    res = _wave_radio_calibrate_send(radio, txmm, radio->wave_band,
                                     radio->g52_20_ch_tab.ch_width,
                                     radio->g52_20_ch_tab.channel,
                                     radio->g52_20_ch_tab.num_of_chan);
    if (MTLK_ERR_OK != res)
      return res;

    _wave_radio_calibration_status_set(radio,
                                       radio->g52_20_ch_tab.channel,
                                       radio->g52_20_ch_tab.num_of_chan,
                                       CW_20);
  }

  /* 5.2 Ghz 40 Mhz channels*/
  if (radio->g52_40_ch_tab.num_of_chan) {
    ILOG0_V("40Mhz channels");
    res = _wave_radio_calibrate_send(radio, txmm, radio->wave_band,
                                     radio->g52_40_ch_tab.ch_width,
                                     radio->g52_40_ch_tab.channel,
                                     radio->g52_40_ch_tab.num_of_chan);
    if (MTLK_ERR_OK != res)
      return res;

    _wave_radio_calibration_status_set(radio,
                                       radio->g52_40_ch_tab.channel,
                                       radio->g52_40_ch_tab.num_of_chan,
                                       CW_40);
  }

  /* 5.2 Ghz 80 Mhz channels*/
  if (radio->g52_80_ch_tab.num_of_chan) {
    ILOG0_V("80Mhz channels");
    res = _wave_radio_calibrate_send(radio, txmm, radio->wave_band,
                                     radio->g52_80_ch_tab.ch_width,
                                     radio->g52_80_ch_tab.channel,
                                     radio->g52_80_ch_tab.num_of_chan);
    if (MTLK_ERR_OK != res)
      return res;

    _wave_radio_calibration_status_set(radio,
                                       radio->g52_80_ch_tab.channel,
                                       radio->g52_80_ch_tab.num_of_chan,
                                       CW_80);
  }

  /* 5.2 Ghz 160 Mhz channels*/
  if (radio->g52_160_ch_tab.num_of_chan) {
    ILOG0_V("160Mhz channels");
    res = _wave_radio_calibrate_send(radio, txmm, radio->wave_band,
                                     radio->g52_160_ch_tab.ch_width,
                                     radio->g52_160_ch_tab.channel,
                                     radio->g52_160_ch_tab.num_of_chan);
    if (MTLK_ERR_OK != res)
      return res;

    _wave_radio_calibration_status_set(radio,
                                       radio->g52_160_ch_tab.channel,
                                       radio->g52_160_ch_tab.num_of_chan,
                                       CW_160);
  }

  return res;
}

static int _wave_radio_calibrate (wave_radio_t *radio, mtlk_txmm_t *txmm)
{
  int res;

  if (MTLK_ERR_OK != (res = _wave_radio_dummy_vap_add(radio, txmm))) {
    return res;
  }

  if (radio->ieee_band == NL80211_BAND_2GHZ) {
    if (MTLK_ERR_OK != (res = _wave_radio_2G_calibrate(radio, txmm))) {
      return res;
    }
  }

  if (radio->ieee_band == NL80211_BAND_5GHZ) {
    if (MTLK_ERR_OK != (res = _wave_radio_5G_calibrate(radio, txmm))) {
      return res;
    }
  }

  if (MTLK_ERR_OK != (res = _wave_radio_dummy_vap_del(radio, txmm))) {
    return res;
  }

  /* Make sure radio is OFF */
  WAVE_RADIO_PDB_SET_INT(radio, PARAM_DB_RADIO_MODE_CURRENT, WAVE_RADIO_OFF);

  return MTLK_ERR_OK;
}

int wave_radio_calibrate(wave_radio_descr_t *radio_descr, BOOL is_recovery)
{
  wave_radio_t *radio;
  mtlk_txmm_t  *txmm = NULL;
  int           res = MTLK_ERR_OK;
  wave_int      i;

  /* Pre-calibration for all radios */
  for (i = 0; i < radio_descr->num_radios; i++) {
    ILOG0_D("Processing RadioID %u", i);
    radio = &radio_descr->radio[i];
    MTLK_ASSERT(radio != NULL);

    txmm = _wave_radio_txmm_get(radio);
    MTLK_ASSERT(txmm != NULL);

    res = _wave_radio_calibrate_prepare(
            radio, txmm, radio->wave_band,
            WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_CALIBRATION_ALGO_MASK),
            WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_ONLINE_ACM));

    ILOG0_DS("Pre-calibration for RadioID %u %s",
             i, (MTLK_ERR_OK == res) ? "succeeded" : "failed");

    if (MTLK_ERR_OK != res) {
      return res;
    }
  }

  /* Calibration for all radios is not performed on Fast Recovery and for Production Mode */
  radio = &radio_descr->radio[0];
  if ((WAVE_RADIO_FAST_RCVRY == is_recovery) ||
      wave_hw_mmb_eeprom_is_production_mode(radio->hw_api->hw)) {
    return res;
  }

  for (i = 0; i < radio_descr->num_radios; i++) {
    radio = &radio_descr->radio[i];
    txmm = _wave_radio_txmm_get(radio);
    res = _wave_radio_calibrate(radio, txmm);
    ILOG0_DS("Calibration for RadioID %u %s",
             i, (MTLK_ERR_OK == res) ? "succeeded" : "failed");

    if (res != MTLK_ERR_OK) {
      return res;
    }
  }

  return res;
}

void wave_radio_band_set(wave_radio_t *radio, nl80211_band_e ieee_band)
{
  MTLK_ASSERT(radio != NULL);
  radio->ieee_band = ieee_band;
  radio->wave_band = nlband2mtlkband(ieee_band);
}

mtlk_hw_band_e wave_radio_band_get(wave_radio_t *radio)
{
  MTLK_ASSERT(radio != NULL);
  return radio->wave_band;
}

void wave_radio_calibration_status_get(wave_radio_t *radio,
                                       uint8 idx,
                                       uint8 *calib_done_mask,
                                       uint8 *calib_failed_mask)
{
  MTLK_ASSERT(radio != NULL);
  *calib_done_mask   = radio->calib_done_mask[idx];
  /* All channels in UM_MAN_CALIBRATE_REQ are calibrated */
  *calib_failed_mask = 0;
}

/**************************************************************
    Radio statistics handlers
 */

uint32 __MTLK_IFUNC
wave_radio_wss_cntr_get (wave_radio_t *radio, wave_radio_cnt_id_e id)
{
  MTLK_ASSERT(radio);
  return mtlk_wss_get_stat(radio->wss, id);
}

void __MTLK_IFUNC
wave_radio_total_traffic_delta_set (wave_radio_t *radio, uint32 total_traffic_delta)
{
  radio->total_traffic_delta = total_traffic_delta;
}

uint32 __MTLK_IFUNC
wave_radio_airtime_efficiency_get (wave_radio_t *radio)
{
  return radio->airtime_efficiency;
}

static uint32
_wave_radio_calculate_airtime_efficiency (wave_radio_t *radio, uint8 airtime)
{
  uint32 airtime_ms, efficiency = 0;
  mtlk_hw_t *hw;

  if (airtime) {
    hw = radio->hw_api->hw;
    airtime_ms = MTLK_PERCENT_TO_VALUE(airtime, wave_hw_mmb_get_stats_poll_period(hw));
    efficiency = wave_hw_calculate_airtime_efficiency(
                    (uint64)radio->total_traffic_delta * MTLK_OSAL_MSEC_IN_SEC, airtime_ms);
  }

  return efficiency;
}

/* Update Radio Phy Status by data from devicePhyRxStatus */
void __MTLK_IFUNC
wave_radio_phy_status_update (wave_radio_t *radio, wave_radio_phy_stat_t *params)
{
  wave_radio_phy_stat_t  *radio_status = &radio->phy_status;
  int       airtime;
  uint32    efficiency;


  /* Calculate air time and efficiency */
  airtime = (int)params->ch_util - (int)params->ch_load;
  if (airtime < 0) {
    ELOG_DDDD("RadioID %u: Invalid (negative) Total Airtime (%d) = Utilization (%u) - Load (%u)",
              radio->idx, airtime, params->ch_util, params->ch_load);

    airtime = 0;
  }

  efficiency = _wave_radio_calculate_airtime_efficiency(radio, airtime);

  mtlk_osal_lock_acquire(&radio->phy_status_lock);
  radio_status->noise   = params->noise;
  radio_status->ch_load = params->ch_load;
  radio_status->ch_util = params->ch_util;
  radio_status->airtime = (uint8)airtime;
  radio_status->airtime_efficiency = efficiency;
  mtlk_osal_lock_release(&radio->phy_status_lock);
}

void __MTLK_IFUNC
wave_radio_phy_status_get (wave_radio_t *radio, wave_radio_phy_stat_t *params)
{
  wave_radio_phy_stat_t  *radio_stat = &radio->phy_status;

  mtlk_osal_lock_acquire(&radio->phy_status_lock);
  *params = *radio_stat; /* struct copy */
  mtlk_osal_lock_release(&radio->phy_status_lock);
}

uint8 __MTLK_IFUNC
wave_radio_channel_load_get (wave_radio_t *radio)
{
  return radio->phy_status.ch_load;
}

static void
_wave_radio_get_traffic_stats (wave_radio_t *radio, mtlk_wssa_drv_traffic_stats_t* stats)
{
  mtlk_wss_t *wss = radio->wss;

  stats->UnicastPacketsSent       = mtlk_wss_get_stat(wss, WAVE_RADIO_CNT_UNICAST_PACKETS_SENT);
  stats->UnicastPacketsReceived   = mtlk_wss_get_stat(wss, WAVE_RADIO_CNT_UNICAST_PACKETS_RECEIVED);
  stats->MulticastPacketsSent     = mtlk_wss_get_stat(wss, WAVE_RADIO_CNT_MULTICAST_PACKETS_SENT);
  stats->MulticastPacketsReceived = mtlk_wss_get_stat(wss, WAVE_RADIO_CNT_MULTICAST_PACKETS_RECEIVED);
  stats->BroadcastPacketsSent     = mtlk_wss_get_stat(wss, WAVE_RADIO_CNT_BROADCAST_PACKETS_SENT);
  stats->BroadcastPacketsReceived = mtlk_wss_get_stat(wss, WAVE_RADIO_CNT_BROADCAST_PACKETS_RECEIVED);

  stats->PacketsSent              = mtlk_wss_get_stat_64(wss, WAVE_RADIO_CNT_PACKETS_SENT);
  stats->PacketsReceived          = mtlk_wss_get_stat_64(wss, WAVE_RADIO_CNT_PACKETS_RECEIVED);
  stats->BytesSent                = mtlk_wss_get_stat_64(wss, WAVE_RADIO_CNT_BYTES_SENT);
  stats->BytesReceived            = mtlk_wss_get_stat_64(wss, WAVE_RADIO_CNT_BYTES_RECEIVED);
}

static void
_wave_radio_get_mgmt_stats (wave_radio_t *radio, mtlk_wssa_drv_mgmt_stats_t *stats)
{
  mtlk_wss_t *wss = radio->wss;

  stats->MANFramesResQueue        = mtlk_wss_get_stat(wss, WAVE_RADIO_CNT_MAN_FRAMES_RES_QUEUE);
  stats->MANFramesSent            = mtlk_wss_get_stat(wss, WAVE_RADIO_CNT_MAN_FRAMES_SENT);
  stats->MANFramesConfirmed       = mtlk_wss_get_stat(wss, WAVE_RADIO_CNT_MAN_FRAMES_CONFIRMED);
  stats->MANFramesReceived        = mtlk_wss_get_stat(wss, WAVE_RADIO_CNT_MAN_FRAMES_RECEIVED);
  stats->MANFramesRetryDropped    = mtlk_wss_get_stat(wss, WAVE_RADIO_CNT_RX_MAN_FRAMES_RETRY_DROPPED);

  stats->ProbeRespSent            = mtlk_wss_get_stat(wss, WAVE_RADIO_CNT_TX_PROBE_RESP_SENT);
  stats->ProbeRespDropped         = mtlk_wss_get_stat(wss, WAVE_RADIO_CNT_TX_PROBE_RESP_DROPPED);
  stats->BssMgmtTxQueFull         = mtlk_wss_get_stat(wss, WAVE_RADIO_CNT_BSS_MGMT_TX_QUE_FULL);
}

static void
_wave_radio_get_tr181_error_stats (wave_radio_t *radio, mtlk_wssa_drv_tr181_error_stats_t *errors)
{
  mtlk_wss_t *wss = radio->wss;
  errors->ErrorsSent             = mtlk_wss_get_stat(wss, WAVE_RADIO_CNT_ERROR_PACKETS_SENT);
  errors->ErrorsReceived         = mtlk_wss_get_stat(wss, WAVE_RADIO_CNT_ERROR_PACKETS_RECEIVED);
  errors->DiscardPacketsReceived = mtlk_wss_get_stat(wss, WAVE_RADIO_CNT_DISCARD_PACKETS_RECEIVED);
  errors->DiscardPacketsSent     = errors->ErrorsSent;
}

void __MTLK_IFUNC
wave_radio_get_tr181_hw_stats (wave_radio_t *radio, mtlk_wssa_drv_tr181_hw_stats_t *stats)
{
  _wave_radio_get_traffic_stats(radio, &stats->traffic_stats);
  _wave_radio_get_tr181_error_stats(radio, &stats->error_stats);

  stats->Noise = radio->phy_status.noise;
  stats->FCSErrorCount = 0; /* FIXME: TBD */
}

void __MTLK_IFUNC
wave_radio_get_hw_stats (wave_radio_t *radio, mtlk_wssa_drv_hw_stats_t *stats)
{
  _wave_radio_get_traffic_stats(radio, &stats->traffic_stats);
  _wave_radio_get_mgmt_stats(radio, &stats->mgmt_stats);

  mtlk_osal_lock_acquire(&radio->phy_status_lock);
  stats->ChannelLoad        = radio->phy_status.ch_load;
  stats->ChannelUtil        = radio->phy_status.ch_util;
  stats->Airtime            = radio->phy_status.airtime;
  stats->AirtimeEfficiency  = radio->phy_status.airtime_efficiency;
  mtlk_osal_lock_release(&radio->phy_status_lock);

  /* HW statisticss */
  wave_hw_get_hw_stats(radio->hw_api->hw, stats);
}

#ifdef MTLK_LEGACY_STATISTICS
#if MTLK_MTIDL_HW_STAT_FULL
static void
_wave_radio_get_debug_hw_stats (wave_radio_t *radio, mtlk_wssa_drv_debug_hw_stats_t *stats)
{
  mtlk_wss_t *wss = radio->wss;

  /* Non debug HW/Radio statistics */
  wave_radio_get_hw_stats(radio, &stats->hw_stats);

  /* Debug Radio info */
  stats->RxPacketsDiscardedDrvTooOld = mtlk_wss_get_stat(wss, WAVE_RADIO_CNT_RX_PACKETS_DISCARDED_DRV_TOO_OLD);
  stats->RxPacketsDiscardedDrvDuplicate = mtlk_wss_get_stat(wss, WAVE_RADIO_CNT_RX_PACKETS_DISCARDED_DRV_DUPLICATE);

  stats->RxPackets802_1x = mtlk_wss_get_stat(wss, WAVE_RADIO_CNT_802_1X_PACKETS_RECEIVED);
  stats->TxPackets802_1x = mtlk_wss_get_stat(wss, WAVE_RADIO_CNT_802_1X_PACKETS_SENT);
  stats->TxDiscardedPackets802_1x = mtlk_wss_get_stat(wss, WAVE_RADIO_CNT_802_1X_PACKETS_DISCARDED);

  stats->PairwiseMICFailurePackets = mtlk_wss_get_stat(wss, WAVE_RADIO_CNT_PAIRWISE_MIC_FAILURE_PACKETS);
  stats->GroupMICFailurePackets = mtlk_wss_get_stat(wss, WAVE_RADIO_CNT_GROUP_MIC_FAILURE_PACKETS);
  stats->UnicastReplayedPackets = mtlk_wss_get_stat(wss, WAVE_RADIO_CNT_UNICAST_REPLAYED_PACKETS);
  stats->MulticastReplayedPackets = mtlk_wss_get_stat(wss, WAVE_RADIO_CNT_MULTICAST_REPLAYED_PACKETS);
  stats->ManagementReplayedPackets = mtlk_wss_get_stat(wss, WAVE_RADIO_CNT_MANAGEMENT_REPLAYED_PACKETS);
  stats->FwdRxPackets = mtlk_wss_get_stat(wss, WAVE_RADIO_CNT_FWD_RX_PACKETS);
  stats->FwdRxBytes = mtlk_wss_get_stat(wss, WAVE_RADIO_CNT_FWD_RX_BYTES);
  stats->MulticastBytesSent = mtlk_wss_get_stat(wss, WAVE_RADIO_CNT_MULTICAST_BYTES_SENT);
  stats->MulticastBytesReceived = mtlk_wss_get_stat(wss, WAVE_RADIO_CNT_MULTICAST_BYTES_RECEIVED);
  stats->BroadcastBytesSent = mtlk_wss_get_stat(wss, WAVE_RADIO_CNT_BROADCAST_BYTES_SENT);
  stats->BroadcastBytesReceived = mtlk_wss_get_stat(wss, WAVE_RADIO_CNT_BROADCAST_BYTES_RECEIVED);

  stats->DATFramesReceived = mtlk_wss_get_stat(wss, WAVE_RADIO_CNT_DAT_FRAMES_RECEIVED);
  stats->CTLFramesReceived = mtlk_wss_get_stat(wss, WAVE_RADIO_CNT_CTL_FRAMES_RECEIVED);

  stats->MANFramesResQueue  = mtlk_wss_get_stat(wss, WAVE_RADIO_CNT_MAN_FRAMES_RES_QUEUE);
  stats->MANFramesSent      = mtlk_wss_get_stat(wss, WAVE_RADIO_CNT_MAN_FRAMES_SENT);
  stats->MANFramesConfirmed = mtlk_wss_get_stat(wss, WAVE_RADIO_CNT_MAN_FRAMES_CONFIRMED);

  stats->MANFramesReceived = mtlk_wss_get_stat(wss, WAVE_RADIO_CNT_MAN_FRAMES_RECEIVED);
  stats->MANFramesRetryDropped     = mtlk_wss_get_stat(wss, WAVE_RADIO_CNT_RX_MAN_FRAMES_RETRY_DROPPED);
  stats->MANFramesRejectedCfg80211 = mtlk_wss_get_stat(wss, WAVE_RADIO_CNT_MAN_FRAMES_CFG80211_FAILED);

  /* Debug HW info */
  wave_hw_get_debug_hw_stats(radio->hw_api->hw, stats);
}
#endif /* MTLK_MTIDL_HW_STAT_FULL */

void __MTLK_IFUNC
wave_radio_stat_handle_request (mtlk_irbd_t *irbd,
                          mtlk_handle_t      radio_ctx,
                          const mtlk_guid_t *evt,
                          void              *buffer,
                          uint32            *size)
{
  wave_radio_t         *radio  = HANDLE_T_PTR(wave_radio_t, radio_ctx);
  mtlk_wssa_info_hdr_t *hdr = (mtlk_wssa_info_hdr_t *) buffer;

  MTLK_UNREFERENCED_PARAM(evt);

  MTLK_ASSERT(radio);

  if(sizeof(mtlk_wssa_info_hdr_t) > *size)
    return;

  if(MTIDL_SRC_DRV == hdr->info_source)
  {
    switch(hdr->info_id)
    {
    case MTLK_WSSA_DRV_TR181_HW_STATS:
      if(sizeof(mtlk_wssa_drv_tr181_hw_stats_t) + sizeof(mtlk_wssa_info_hdr_t) > *size) {
        hdr->processing_result = MTLK_ERR_BUF_TOO_SMALL;
      } else {
        wave_radio_get_tr181_hw_stats(radio, (mtlk_wssa_drv_tr181_hw_stats_t*) &hdr[1]);
        hdr->processing_result = MTLK_ERR_OK;
        *size = sizeof(mtlk_wssa_drv_tr181_hw_stats_t) + sizeof(mtlk_wssa_info_hdr_t);
      }
      break;
    case MTLK_WSSA_DRV_STATUS_HW:
      if(sizeof(mtlk_wssa_drv_hw_stats_t) + sizeof(mtlk_wssa_info_hdr_t) > *size)
      {
        hdr->processing_result = MTLK_ERR_BUF_TOO_SMALL;
      } else {
        wave_radio_get_hw_stats(radio, (mtlk_wssa_drv_hw_stats_t*) &hdr[1]);
        hdr->processing_result = MTLK_ERR_OK;
        *size = sizeof(mtlk_wssa_drv_hw_stats_t) + sizeof(mtlk_wssa_info_hdr_t);
      }
      break;
#if MTLK_MTIDL_HW_STAT_FULL
    case MTLK_WSSA_DRV_DEBUG_STATUS_HW:
      if(sizeof(mtlk_wssa_drv_debug_hw_stats_t) + sizeof(mtlk_wssa_info_hdr_t) > *size)
      {
        hdr->processing_result = MTLK_ERR_BUF_TOO_SMALL;
      } else {
        _wave_radio_get_debug_hw_stats(radio, (mtlk_wssa_drv_debug_hw_stats_t*) &hdr[1]);
        hdr->processing_result = MTLK_ERR_OK;
        *size = sizeof(mtlk_wssa_drv_debug_hw_stats_t) + sizeof(mtlk_wssa_info_hdr_t);
      }
      break;
#endif /* MTLK_MTIDL_HW_STAT_FULL */
    case MTLK_WSSA_DRV_RECOVERY:
      if (sizeof(mtlk_wssa_drv_recovery_stats_t) + sizeof(mtlk_wssa_info_hdr_t) > *size)
      {
        hdr->processing_result = MTLK_ERR_BUF_TOO_SMALL;
      } else {
        wave_hw_get_recovery_stats(radio->hw_api->hw, (mtlk_wssa_drv_recovery_stats_t*) &hdr[1]);
        hdr->processing_result = MTLK_ERR_OK;
        *size = sizeof(mtlk_wssa_drv_recovery_stats_t) + sizeof(mtlk_wssa_info_hdr_t);
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

void __MTLK_IFUNC wave_radio_sync_hostapd_done_set (wave_radio_t *radio, BOOL value)
{
  MTLK_ASSERT(NULL != radio);
  radio->is_sync_hostapd_done = value;
}

BOOL __MTLK_IFUNC wave_radio_is_sync_hostapd_done (wave_radio_t *radio)
{
  MTLK_ASSERT(NULL != radio);
  return radio->is_sync_hostapd_done;
}

int wave_radio_get_associated_dev_stats (
  wave_radio_t *radio,
  struct net_device *ndev,
  const void *data,
  int data_len)
{
  void *wiphy;
  u8 *addr = (u8*)data;
  mtlk_df_user_t *df_user;
  mtlk_clpb_t *clpb = NULL;
  mtlk_nbuf_t *rsp_nbuf;
  int res = MTLK_ERR_OK;
  peerFlowStats *peer_stats;
  uint32 stats_size;

  wiphy = wv_cfg80211_wiphy_get(radio->cfg80211);
  MTLK_ASSERT(NULL != wiphy);
  if (NULL == wiphy) {
    res = MTLK_ERR_UNKNOWN;
    goto end;
  }

  MTLK_ASSERT(NULL != radio);
  df_user = mtlk_df_user_from_ndev(ndev);

  ILOG2_SSD("%s: Invoked from %s (%i)", ndev->name, current->comm, current->pid);
  MTLK_CHECK_DF_USER(df_user);

  if (data_len != sizeof(IEEE_ADDR)) {
    ELOG_D("Wrong associated dev stats data length: %d", data_len);
    res = MTLK_ERR_PARAMS;
    goto end;
  }

  if (NULL == data) {
    ELOG_V("Associated dev stats data is NULL");
    res = MTLK_ERR_PARAMS;
    goto end;
  }
  if (mtlk_osal_eth_is_broadcast((u8*)data)) {
    ELOG_V("Station address can't be broadcast address");
    res = MTLK_ERR_PARAMS;
    goto end;
  }
  if (!mtlk_osal_is_valid_ether_addr((u8*)data)) {
    ELOG_Y("Invalid MAC address %Y", data);
    res = MTLK_ERR_PARAMS;
    goto end;
  }

  res = _mtlk_df_user_invoke_core(mtlk_df_user_get_df(df_user),
    WAVE_CORE_REQ_GET_ASSOCIATED_DEV_STATS, &clpb, addr, IEEE_ADDR_LEN);
  res = _mtlk_df_user_process_core_retval(res, clpb,
    WAVE_CORE_REQ_GET_ASSOCIATED_DEV_STATS, FALSE);

  if (res != MTLK_ERR_OK)
    goto end;

  peer_stats = mtlk_clpb_enum_get_next(clpb, &stats_size);

  MTLK_CLPB_TRY(peer_stats, stats_size)
    rsp_nbuf = wv_cfg80211_vendor_cmd_alloc_reply_skb(wiphy,
      sizeof(*peer_stats));
    if (!rsp_nbuf) {
      MTLK_CLPB_EXIT(MTLK_ERR_NO_MEM);
    }

    nla_put(rsp_nbuf, NL80211_ATTR_VENDOR_DATA, sizeof(*peer_stats),
      peer_stats);

    mtlk_clpb_delete(clpb);
    return wv_cfg80211_vendor_cmd_reply(rsp_nbuf);
  MTLK_CLPB_FINALLY(res)
    mtlk_clpb_delete(clpb);
  MTLK_CLPB_END

end:
  return _mtlk_df_mtlk_to_linux_error_code(res);
}

int wave_radio_get_associated_dev_tid_stats (
  wave_radio_t *radio,
  struct net_device *ndev,
  const void *data,
  int data_len)
{
  void *wiphy;
  u8 *addr = (u8*)data;
  mtlk_df_user_t *df_user;
  mtlk_clpb_t *clpb = NULL;
  mtlk_nbuf_t *rsp_nbuf;
  int res = MTLK_ERR_OK;
  peerTidStats *tid_stats;
  uint32 stats_size;

  wiphy = wv_cfg80211_wiphy_get(radio->cfg80211);
  MTLK_ASSERT(NULL != wiphy);
  if (NULL == wiphy) {
    res = MTLK_ERR_UNKNOWN;
    goto end;
  }

  MTLK_ASSERT(NULL != radio);
  df_user = mtlk_df_user_from_ndev(ndev);

  ILOG2_SSD("%s: Invoked from %s (%i)", ndev->name, current->comm, current->pid);
  MTLK_CHECK_DF_USER(df_user);

  if (data_len != sizeof(IEEE_ADDR)) {
    ELOG_D("Wrong associated dev stats data length: %d", data_len);
    res = MTLK_ERR_PARAMS;
    goto end;
  }

  if (NULL == data) {
    ELOG_V("Associated dev stats data is NULL");
    res = MTLK_ERR_PARAMS;
    goto end;
  }
  if (mtlk_osal_eth_is_broadcast((u8*)data)) {
    ELOG_V("Station address can't be broadcast address");
    res = MTLK_ERR_PARAMS;
    goto end;
  }
  if (!mtlk_osal_is_valid_ether_addr((u8*)data)) {
    ELOG_Y("Invalid MAC address %Y", data);
    res = MTLK_ERR_PARAMS;
    goto end;
  }

  res = _mtlk_df_user_invoke_core(mtlk_df_user_get_df(df_user),
    WAVE_CORE_REQ_GET_ASSOCIATED_DEV_TID_STATS, &clpb, addr, IEEE_ADDR_LEN);
  res = _mtlk_df_user_process_core_retval(res, clpb,
    WAVE_CORE_REQ_GET_ASSOCIATED_DEV_TID_STATS, FALSE);

  if (res != MTLK_ERR_OK)
    goto end;

  tid_stats = mtlk_clpb_enum_get_next(clpb, &stats_size);

  MTLK_CLPB_TRY(tid_stats, stats_size)
    rsp_nbuf = wv_cfg80211_vendor_cmd_alloc_reply_skb(wiphy,
      sizeof(*tid_stats));
    if (!rsp_nbuf) {
      MTLK_CLPB_EXIT(MTLK_ERR_NO_MEM);
    }

    nla_put(rsp_nbuf, NL80211_ATTR_VENDOR_DATA, sizeof(*tid_stats),
      tid_stats);

    mtlk_clpb_delete(clpb);
    return wv_cfg80211_vendor_cmd_reply(rsp_nbuf);
  MTLK_CLPB_FINALLY(res)
    mtlk_clpb_delete(clpb);
  MTLK_CLPB_END

end:
  return _mtlk_df_mtlk_to_linux_error_code(res);
}

int __MTLK_IFUNC
wave_radio_get_channel_stats (
  wave_radio_t *radio,
  struct net_device *ndev,
  const void *data,
  int data_len)
{
  void *wiphy;
  mtlk_core_t *core = NULL;
  mtlk_df_user_t *df_user;
  mtlk_clpb_t *clpb = NULL;
  int res = MTLK_ERR_OK;
  mtlk_channel_stats *chan_stats = NULL;
  int num_of_channels = 0;
  uint32 stats_size;
  mtlk_nbuf_t *rsp_nbuf;
  mtlk_hw_band_e hw_band;

  core = wave_radio_master_core_get(radio);
  MTLK_ASSERT(NULL != core);
  if (NULL == core) {
    res = MTLK_ERR_UNKNOWN;
    goto end;
  }

  wiphy = wv_cfg80211_wiphy_get(radio->cfg80211);
  MTLK_ASSERT(NULL != wiphy);
  if (NULL == wiphy) {
    res = MTLK_ERR_UNKNOWN;
    goto end;
  }

  df_user = mtlk_df_user_from_ndev(ndev);

  ILOG2_SSD("%s: Invoked from %s (%i)", ndev->name, current->comm, current->pid);
  MTLK_CHECK_DF_USER(df_user);

  res = _mtlk_df_user_invoke_core(mtlk_df_user_get_df(df_user),
    WAVE_CORE_REQ_GET_CHANNEL_STATS, &clpb, NULL, 0);
  res = _mtlk_df_user_process_core_retval(res, clpb,
    WAVE_CORE_REQ_GET_CHANNEL_STATS, FALSE);

  if (res != MTLK_ERR_OK)
    goto end;
  
  hw_band = core_cfg_get_freq_band_cur(core);
  num_of_channels = wave_core_cfg_get_nof_channels(hw_band);
  if (num_of_channels == 0) {
    res =  MTLK_ERR_UNKNOWN;
    goto end;
  }

  chan_stats = mtlk_clpb_enum_get_next(clpb, &stats_size);

  MTLK_CLPB_TRY_EX(chan_stats, sizeof(mtlk_channel_stats) * num_of_channels, stats_size)
    rsp_nbuf = wv_cfg80211_vendor_cmd_alloc_reply_skb(wiphy,stats_size);
    if (!rsp_nbuf) {
      MTLK_CLPB_EXIT(MTLK_ERR_NO_MEM);
    }

    nla_put(rsp_nbuf, NL80211_ATTR_VENDOR_DATA, stats_size, chan_stats);

    mtlk_clpb_delete(clpb);
    return wv_cfg80211_vendor_cmd_reply(rsp_nbuf);
  MTLK_CLPB_FINALLY(res)
    mtlk_clpb_delete(clpb);
  MTLK_CLPB_END

end:
  return _mtlk_df_mtlk_to_linux_error_code(res);
}

int wave_radio_get_associated_dev_rate_info_rx_stats (
  wave_radio_t *radio,
  struct net_device *ndev,
  const void *data,
  int data_len)
{
  void *wiphy;
  u8 *addr = (u8*)data;
  mtlk_df_user_t *df_user;
  mtlk_clpb_t *clpb = NULL;
  mtlk_nbuf_t *rsp_nbuf;
  int res = MTLK_ERR_OK;
  peerRateInfoRxStats *peer_rx_counters;
  uint32 stat_size;

  wiphy = wv_cfg80211_wiphy_get(radio->cfg80211);
  MTLK_ASSERT(NULL != wiphy);
  if (NULL == wiphy) {
    res = MTLK_ERR_UNKNOWN;
    goto end;
  }

  MTLK_ASSERT(NULL != radio);
  MTLK_ASSERT(NULL != ndev);
  df_user = mtlk_df_user_from_ndev(ndev);
  MTLK_CHECK_DF_USER(df_user);

  if (data_len != sizeof(IEEE_ADDR)) {
    ELOG_D("Wrong associated dev stats data length: %d", data_len);
    res = MTLK_ERR_PARAMS;
    goto end;
  }

  if (NULL == data) {
    ELOG_V("Associated dev stats data is NULL");
    res = MTLK_ERR_PARAMS;
    goto end;
  }
  if (mtlk_osal_eth_is_broadcast((u8*)data)) {
    ELOG_V("Station address can't be broadcast address");
    res = MTLK_ERR_PARAMS;
    goto end;
  }
  if (!mtlk_osal_is_valid_ether_addr((u8*)data)) {
    ELOG_Y("Invalid MAC address %Y", data);
    res = MTLK_ERR_PARAMS;
    goto end;
  }

  res = _mtlk_df_user_invoke_core(mtlk_df_user_get_df(df_user),
    WAVE_CORE_REQ_GET_ASSOCIATED_DEV_RATE_INFO_RX_STATS, &clpb, addr, IEEE_ADDR_LEN);
  res = _mtlk_df_user_process_core_retval(res, clpb,
    WAVE_CORE_REQ_GET_ASSOCIATED_DEV_RATE_INFO_RX_STATS, FALSE);

  if (res != MTLK_ERR_OK)
    goto end;

  peer_rx_counters = mtlk_clpb_enum_get_next(clpb, &stat_size);

  MTLK_CLPB_TRY(peer_rx_counters, stat_size)
    rsp_nbuf = wv_cfg80211_vendor_cmd_alloc_reply_skb(wiphy,
      sizeof(*peer_rx_counters));
    if (!rsp_nbuf) {
      MTLK_CLPB_EXIT(MTLK_ERR_NO_MEM);
    }

    nla_put(rsp_nbuf, NL80211_ATTR_VENDOR_DATA, sizeof(*peer_rx_counters),
      peer_rx_counters);

    mtlk_clpb_delete(clpb);
    return wv_cfg80211_vendor_cmd_reply(rsp_nbuf);
  MTLK_CLPB_FINALLY(res)
    mtlk_clpb_delete(clpb);
  MTLK_CLPB_END

end:
  return _mtlk_df_mtlk_to_linux_error_code(res);
}

int wave_radio_get_associated_dev_rate_info_tx_stats (
  wave_radio_t *radio,
  struct net_device *ndev,
  const void *data,
  int data_len)
{
  void *wiphy;
  u8 *addr = (u8*)data;
  mtlk_df_user_t *df_user;
  mtlk_clpb_t *clpb = NULL;
  mtlk_nbuf_t *rsp_nbuf;
  int res = MTLK_ERR_OK;
  peerRateInfoTxStats *peer_tx_counters;
  uint32 stat_size;

  wiphy = wv_cfg80211_wiphy_get(radio->cfg80211);
  MTLK_ASSERT(NULL != wiphy);
  if (NULL == wiphy) {
    res = MTLK_ERR_UNKNOWN;
    goto end;
  }

  MTLK_ASSERT(NULL != radio);
  MTLK_ASSERT(NULL != ndev);
  df_user = mtlk_df_user_from_ndev(ndev);
  MTLK_CHECK_DF_USER(df_user);

  if (NULL == data) {
    ELOG_V("Associated dev stats data is NULL");
    res = MTLK_ERR_PARAMS;
    goto end;
  }
  if (mtlk_osal_eth_is_broadcast((u8*)data)) {
    ELOG_V("Station address can't be broadcast address");
    res = MTLK_ERR_PARAMS;
    goto end;
  }
  if (!mtlk_osal_is_valid_ether_addr((u8*)data)) {
    ELOG_Y("Invalid MAC address %Y", data);
    res = MTLK_ERR_PARAMS;
    goto end;
  }

  res = _mtlk_df_user_invoke_core(mtlk_df_user_get_df(df_user),
    WAVE_CORE_REQ_GET_ASSOCIATED_DEV_RATE_INFO_TX_STATS, &clpb, addr, IEEE_ADDR_LEN);
  res = _mtlk_df_user_process_core_retval(res, clpb,
    WAVE_CORE_REQ_GET_ASSOCIATED_DEV_RATE_INFO_TX_STATS, FALSE);

  if (res != MTLK_ERR_OK)
    goto end;

  peer_tx_counters = mtlk_clpb_enum_get_next(clpb, &stat_size);

  MTLK_CLPB_TRY(peer_tx_counters, stat_size)
    rsp_nbuf = wv_cfg80211_vendor_cmd_alloc_reply_skb(wiphy,
      sizeof(*peer_tx_counters));
    if (!rsp_nbuf) {
      MTLK_CLPB_EXIT(MTLK_ERR_NO_MEM);
    }

    nla_put(rsp_nbuf, NL80211_ATTR_VENDOR_DATA, sizeof(*peer_tx_counters),
      peer_tx_counters);

    mtlk_clpb_delete(clpb);
    return wv_cfg80211_vendor_cmd_reply(rsp_nbuf);
  MTLK_CLPB_FINALLY(res)
    mtlk_clpb_delete(clpb);
  MTLK_CLPB_END

end:
  return _mtlk_df_mtlk_to_linux_error_code(res);
}

#ifndef MTLK_LEGACY_STATISTICS
int wave_radio_get_peer_list (
  wave_radio_t *radio,
  struct net_device *ndev,
  const void *data,
  int data_len)
{
  void *wiphy;
  mtlk_df_user_t *df_user;
  mtlk_clpb_t *clpb = NULL;
  mtlk_nbuf_t *rsp_nbuf;
  int res = MTLK_ERR_OK;
  mtlk_wssa_peer_list_t *peer_list = NULL, *tmp_peer_list = NULL, *tmp_clpb = NULL;
  uint32 size, *no_of_stas, sta_cnt;

  wiphy = wv_cfg80211_wiphy_get(radio->cfg80211);
  MTLK_ASSERT(NULL != wiphy);
  if (NULL == wiphy) {
    res = MTLK_ERR_UNKNOWN;
    goto end;
  }

  MTLK_ASSERT(NULL != radio);
  df_user = mtlk_df_user_from_ndev(ndev);

  ILOG2_SSD("%s: Invoked from %s (%i)", ndev->name, current->comm, current->pid);
  MTLK_CHECK_DF_USER(df_user);

  res = _mtlk_df_user_invoke_core(mtlk_df_user_get_df(df_user),
    WAVE_CORE_REQ_GET_PEER_LIST, &clpb, NULL, 0);
  res = _mtlk_df_user_process_core_retval_void(res, clpb,
    WAVE_CORE_REQ_GET_PEER_LIST, FALSE);

  if (res != MTLK_ERR_OK)
    goto end;

  no_of_stas = mtlk_clpb_enum_get_next(clpb, &size);
  if ((sizeof(uint32) != size) || (no_of_stas == NULL)) {
    goto end;
  }

  if (0 != *no_of_stas) {
    if (NULL == (peer_list = mtlk_osal_mem_alloc(sizeof(mtlk_wssa_peer_list_t) * (*no_of_stas),
                                                MTLK_MEM_TAG_EXTENSION))) {
      ELOG_V("Can't allocate memory");
      res = MTLK_ERR_NO_MEM;
      goto end;
    }

    sta_cnt = *no_of_stas;
    tmp_peer_list = peer_list;
    /* enumerate sta entries */
    while ((NULL != (tmp_clpb = mtlk_clpb_enum_get_next(clpb, &size))) && ((*no_of_stas)--)) {
      if (sizeof(*tmp_clpb) != size) {
        res = MTLK_ERR_UNKNOWN;
        goto delete_clpb;
      }
      wave_memcpy( tmp_peer_list++, size, tmp_clpb, size);
    }

    MTLK_ASSERT(NULL != peer_list);

    rsp_nbuf = wv_cfg80211_vendor_cmd_alloc_reply_skb(wiphy, sizeof(*peer_list) * sta_cnt);
    if (!rsp_nbuf) {
      goto delete_clpb;
    }

    nla_put(rsp_nbuf, NL80211_ATTR_VENDOR_DATA, sizeof(*peer_list) * sta_cnt, peer_list);

    mtlk_clpb_delete(clpb);
    mtlk_osal_mem_free(peer_list);
    return wv_cfg80211_vendor_cmd_reply(rsp_nbuf);
  }
  else
  {
    rsp_nbuf = wv_cfg80211_vendor_cmd_alloc_reply_skb(wiphy, 0);
    if (!rsp_nbuf) {
      goto end;
    }

    nla_put(rsp_nbuf, NL80211_ATTR_VENDOR_DATA, 0, NULL);

    mtlk_clpb_delete(clpb);
    return wv_cfg80211_vendor_cmd_reply(rsp_nbuf);
  }
  return res;
delete_clpb:
  mtlk_osal_mem_free(peer_list);

end:
  if(clpb)
    mtlk_clpb_delete(clpb);
  return _mtlk_df_mtlk_to_linux_error_code(res);
}

int wave_radio_get_peer_flow_status (
  wave_radio_t *radio,
  struct net_device *ndev,
  const void *data,
  int data_len)
{
  void *wiphy;
  u8 *addr = (u8*)data;
  mtlk_df_user_t *df_user;
  mtlk_clpb_t *clpb = NULL;
  mtlk_nbuf_t *rsp_nbuf;
  int res = MTLK_ERR_OK;
  mtlk_wssa_drv_peer_stats_t *peer_flow_stats;
  uint32 stats_size;

  wiphy = wv_cfg80211_wiphy_get(radio->cfg80211);
  MTLK_ASSERT(NULL != wiphy);
  if (NULL == wiphy) {
    res = MTLK_ERR_UNKNOWN;
    goto end;
  }

  MTLK_ASSERT(NULL != radio);
  df_user = mtlk_df_user_from_ndev(ndev);

  ILOG2_SSD("%s: Invoked from %s (%i)", ndev->name, current->comm, current->pid);
  MTLK_CHECK_DF_USER(df_user);

  if (data_len != sizeof(IEEE_ADDR)) {
    ELOG_D("Wrong peer flow status data length: %d", data_len);
    res = MTLK_ERR_PARAMS;
    goto end;
  }

  if (NULL == data) {
    ELOG_V("Peer flow status data is NULL");
    res = MTLK_ERR_PARAMS;
    goto end;
  }
  if (mtlk_osal_eth_is_broadcast((u8*)data)) {
    ELOG_V("Station address can't be broadcast address");
    res = MTLK_ERR_PARAMS;
    goto end;
  }
  if (!mtlk_osal_is_valid_ether_addr((u8*)data)) {
    ELOG_Y("Invalid MAC address %Y", data);
    res = MTLK_ERR_PARAMS;
    goto end;
  }

  res = _mtlk_df_user_invoke_core(mtlk_df_user_get_df(df_user),
    WAVE_CORE_REQ_GET_PEER_FLOW_STATUS, &clpb, addr, IEEE_ADDR_LEN);
  res = _mtlk_df_user_process_core_retval(res, clpb,
    WAVE_CORE_REQ_GET_PEER_FLOW_STATUS, FALSE);

  if (res != MTLK_ERR_OK)
    goto end;

  peer_flow_stats = mtlk_clpb_enum_get_next(clpb, &stats_size);

  MTLK_CLPB_TRY(peer_flow_stats, stats_size)
    rsp_nbuf = wv_cfg80211_vendor_cmd_alloc_reply_skb(wiphy,
      sizeof(*peer_flow_stats));
    if (!rsp_nbuf) {
      MTLK_CLPB_EXIT(MTLK_ERR_NO_MEM);
    }

    nla_put(rsp_nbuf, NL80211_ATTR_VENDOR_DATA, sizeof(*peer_flow_stats),
      peer_flow_stats);

    mtlk_clpb_delete(clpb);
    return wv_cfg80211_vendor_cmd_reply(rsp_nbuf);
  MTLK_CLPB_FINALLY(res)
    mtlk_clpb_delete(clpb);
  MTLK_CLPB_END

end:
  return _mtlk_df_mtlk_to_linux_error_code(res);
}

int wave_radio_get_peer_capabilities (
  wave_radio_t *radio,
  struct net_device *ndev,
  const void *data,
  int data_len)
{
  void *wiphy;
  u8 *addr = (u8*)data;
  mtlk_df_user_t *df_user;
  mtlk_clpb_t *clpb = NULL;
  mtlk_nbuf_t *rsp_nbuf;
  int res = MTLK_ERR_OK;
  mtlk_wssa_drv_peer_capabilities_t *peer_capabilities;
  uint32 stats_size;

  wiphy = wv_cfg80211_wiphy_get(radio->cfg80211);
  MTLK_ASSERT(NULL != wiphy);
  if (NULL == wiphy) {
    res = MTLK_ERR_UNKNOWN;
    goto end;
  }

  MTLK_ASSERT(NULL != radio);
  df_user = mtlk_df_user_from_ndev(ndev);

  ILOG2_SSD("%s: Invoked from %s (%i)", ndev->name, current->comm, current->pid);
  MTLK_CHECK_DF_USER(df_user);

  if (data_len != sizeof(IEEE_ADDR)) {
    ELOG_D("Wrong peer capabilities data length: %d", data_len);
    res = MTLK_ERR_PARAMS;
    goto end;
  }

  if (NULL == data) {
    ELOG_V("Peer capabilities data is NULL");
    res = MTLK_ERR_PARAMS;
    goto end;
  }
  if (mtlk_osal_eth_is_broadcast((u8*)data)) {
    ELOG_V("Station address can't be broadcast address");
    res = MTLK_ERR_PARAMS;
    goto end;
  }
  if (!mtlk_osal_is_valid_ether_addr((u8*)data)) {
    ELOG_Y("Invalid MAC address %Y", data);
    res = MTLK_ERR_PARAMS;
    goto end;
  }

  res = _mtlk_df_user_invoke_core(mtlk_df_user_get_df(df_user),
    WAVE_CORE_REQ_GET_PEER_CAPABILITIES, &clpb, addr, IEEE_ADDR_LEN);
  res = _mtlk_df_user_process_core_retval(res, clpb,
    WAVE_CORE_REQ_GET_PEER_CAPABILITIES, FALSE);

  if (res != MTLK_ERR_OK)
    goto end;

  peer_capabilities = mtlk_clpb_enum_get_next(clpb, &stats_size);

  MTLK_CLPB_TRY(peer_capabilities, stats_size)

    rsp_nbuf = wv_cfg80211_vendor_cmd_alloc_reply_skb(wiphy,
      sizeof(*peer_capabilities));
    if (!rsp_nbuf) {
      MTLK_CLPB_EXIT(MTLK_ERR_NO_MEM);
    }

    nla_put(rsp_nbuf, NL80211_ATTR_VENDOR_DATA, sizeof(*peer_capabilities),
      peer_capabilities);

    mtlk_clpb_delete(clpb);
    return wv_cfg80211_vendor_cmd_reply(rsp_nbuf);
  MTLK_CLPB_FINALLY(res)
    mtlk_clpb_delete(clpb);
  MTLK_CLPB_END
end:
  return _mtlk_df_mtlk_to_linux_error_code(res);
}

int wave_radio_get_peer_rate_info (
  wave_radio_t *radio,
  struct net_device *ndev,
  const void *data,
  int data_len)
{
  void *wiphy;
  u8 *addr = (u8*)data;
  mtlk_df_user_t *df_user;
  mtlk_clpb_t *clpb = NULL;
  mtlk_nbuf_t *rsp_nbuf;
  int res = MTLK_ERR_OK;
  mtlk_wssa_drv_peer_rates_info_t *peer_rate_info;
  uint32 stats_size;

  wiphy = wv_cfg80211_wiphy_get(radio->cfg80211);
  MTLK_ASSERT(NULL != wiphy);
  if (NULL == wiphy) {
    res = MTLK_ERR_UNKNOWN;
    goto end;
  }

  MTLK_ASSERT(NULL != radio);
  df_user = mtlk_df_user_from_ndev(ndev);

  ILOG2_SSD("%s: Invoked from %s (%i)", ndev->name, current->comm, current->pid);
  MTLK_CHECK_DF_USER(df_user);

  if (data_len != sizeof(IEEE_ADDR)) {
    ELOG_D("Wrong peer rate info data length: %d", data_len);
    res = MTLK_ERR_PARAMS;
    goto end;
  }

  if (NULL == data) {
    ELOG_V("Peer rate info data is NULL");
    res = MTLK_ERR_PARAMS;
    goto end;
  }
  if (mtlk_osal_eth_is_broadcast((u8*)data)) {
    ELOG_V("Station address can't be broadcast address");
    res = MTLK_ERR_PARAMS;
    goto end;
  }
  if (!mtlk_osal_is_valid_ether_addr((u8*)data)) {
    ELOG_Y("Invalid MAC address %Y", data);
    res = MTLK_ERR_PARAMS;
    goto end;
  }

  res = _mtlk_df_user_invoke_core(mtlk_df_user_get_df(df_user),
    WAVE_CORE_REQ_GET_PEER_RATE_INFO, &clpb, addr, IEEE_ADDR_LEN);
  res = _mtlk_df_user_process_core_retval(res, clpb,
    WAVE_CORE_REQ_GET_PEER_RATE_INFO, FALSE);

  if (res != MTLK_ERR_OK)
    goto end;

  peer_rate_info = mtlk_clpb_enum_get_next(clpb, &stats_size);

  MTLK_CLPB_TRY(peer_rate_info, stats_size)

    rsp_nbuf = wv_cfg80211_vendor_cmd_alloc_reply_skb(wiphy,
      sizeof(*peer_rate_info));
    if (!rsp_nbuf) {
      MTLK_CLPB_EXIT(MTLK_ERR_NO_MEM);
    }

    nla_put(rsp_nbuf, NL80211_ATTR_VENDOR_DATA, sizeof(*peer_rate_info),
      peer_rate_info);

    mtlk_clpb_delete(clpb);
    return wv_cfg80211_vendor_cmd_reply(rsp_nbuf);
  MTLK_CLPB_FINALLY(res)
    mtlk_clpb_delete(clpb);
  MTLK_CLPB_END

end:
  return _mtlk_df_mtlk_to_linux_error_code(res);
}

int wave_radio_get_tr181_peer_statistics (
  wave_radio_t *radio,
  struct net_device *ndev,
  const void *data,
  int data_len)
{
  void *wiphy;
  u8 *addr = (u8*)data;
  mtlk_df_user_t *df_user;
  mtlk_clpb_t *clpb = NULL;
  mtlk_nbuf_t *rsp_nbuf;
  int res = MTLK_ERR_OK;
  mtlk_wssa_drv_tr181_peer_stats_t *tr181_stats;
  uint32 stats_size;

  wiphy = wv_cfg80211_wiphy_get(radio->cfg80211);
  MTLK_ASSERT(NULL != wiphy);
  if (NULL == wiphy) {
    res = MTLK_ERR_UNKNOWN;
    goto end;
  }

  MTLK_ASSERT(NULL != radio);
  df_user = mtlk_df_user_from_ndev(ndev);

  ILOG2_SSD("%s: Invoked from %s (%i)", ndev->name, current->comm, current->pid);
  MTLK_CHECK_DF_USER(df_user);

  if (data_len != sizeof(IEEE_ADDR)) {
    ELOG_D("Wrong tr181 peer stats data length: %d", data_len);
    res = MTLK_ERR_PARAMS;
    goto end;
  }

  if (NULL == data) {
    ELOG_V("tr181 peer stats data is NULL");
    res = MTLK_ERR_PARAMS;
    goto end;
  }
  if (mtlk_osal_eth_is_broadcast((u8*)data)) {
    ELOG_V("Station address can't be broadcast address");
    res = MTLK_ERR_PARAMS;
    goto end;
  }
  if (!mtlk_osal_is_valid_ether_addr((u8*)data)) {
    ELOG_Y("Invalid MAC address %Y", data);
    res = MTLK_ERR_PARAMS;
    goto end;
  }

  res = _mtlk_df_user_invoke_core(mtlk_df_user_get_df(df_user),
    WAVE_CORE_REQ_GET_TR181_PEER_STATS, &clpb, addr, IEEE_ADDR_LEN);
  res = _mtlk_df_user_process_core_retval(res, clpb,
    WAVE_CORE_REQ_GET_TR181_PEER_STATS, FALSE);

  if (res != MTLK_ERR_OK)
    goto end;

  tr181_stats = mtlk_clpb_enum_get_next(clpb, &stats_size);
  MTLK_CLPB_TRY(tr181_stats, stats_size)

    rsp_nbuf = wv_cfg80211_vendor_cmd_alloc_reply_skb(wiphy,
      sizeof(*tr181_stats));
    if (!rsp_nbuf) {
      MTLK_CLPB_EXIT(MTLK_ERR_NO_MEM);
    }

    nla_put(rsp_nbuf, NL80211_ATTR_VENDOR_DATA, sizeof(*tr181_stats),
      tr181_stats);

    mtlk_clpb_delete(clpb);
    return wv_cfg80211_vendor_cmd_reply(rsp_nbuf);
  MTLK_CLPB_FINALLY(res)
    mtlk_clpb_delete(clpb);
  MTLK_CLPB_END

end:
  return _mtlk_df_mtlk_to_linux_error_code(res);
}

int wave_radio_get_tr181_wlan_statistics (
  wave_radio_t *radio,
  struct net_device *ndev,
  const void *data,
  int data_len)
{
  void *wiphy;
  mtlk_df_user_t *df_user;
  mtlk_clpb_t *clpb = NULL;
  mtlk_nbuf_t *rsp_nbuf;
  int res = MTLK_ERR_OK;
  mtlk_wssa_drv_tr181_wlan_stats_t *tr181_stats;
  uint32 stats_size;

  wiphy = wv_cfg80211_wiphy_get(radio->cfg80211);
  MTLK_ASSERT(NULL != wiphy);
  if (NULL == wiphy) {
    res = MTLK_ERR_UNKNOWN;
    goto end;
  }

  MTLK_ASSERT(NULL != radio);
  df_user = mtlk_df_user_from_ndev(ndev);

  ILOG2_SSD("%s: Invoked from %s (%i)", ndev->name, current->comm, current->pid);
  MTLK_CHECK_DF_USER(df_user);

  res = _mtlk_df_user_invoke_core(mtlk_df_user_get_df(df_user),
    WAVE_CORE_REQ_GET_TR181_WLAN_STATS, &clpb, NULL, 0);
  res = _mtlk_df_user_process_core_retval(res, clpb,
    WAVE_CORE_REQ_GET_TR181_WLAN_STATS, FALSE);

  if (res != MTLK_ERR_OK)
    goto end;

  tr181_stats = mtlk_clpb_enum_get_next(clpb, &stats_size);

  MTLK_CLPB_TRY(tr181_stats, stats_size)

    rsp_nbuf = wv_cfg80211_vendor_cmd_alloc_reply_skb(wiphy,stats_size);
    if (!rsp_nbuf) {
      MTLK_CLPB_EXIT(MTLK_ERR_NO_MEM);
    }

    nla_put(rsp_nbuf, NL80211_ATTR_VENDOR_DATA, stats_size, tr181_stats);

    mtlk_clpb_delete(clpb);
    return wv_cfg80211_vendor_cmd_reply(rsp_nbuf);
  MTLK_CLPB_FINALLY(res)
    mtlk_clpb_delete(clpb);
  MTLK_CLPB_END

end:
  return _mtlk_df_mtlk_to_linux_error_code(res);
}

int wave_radio_get_tr181_hw_statistics (
  wave_radio_t *radio,
  struct net_device *ndev,
  const void *data,
  int data_len)
{
  void *wiphy;
  mtlk_df_user_t *df_user;
  mtlk_clpb_t *clpb = NULL;
  mtlk_nbuf_t *rsp_nbuf;
  int res = MTLK_ERR_OK;
  mtlk_wssa_drv_tr181_hw_stats_t *tr181_stats;
  uint32 stats_size;

  wiphy = wv_cfg80211_wiphy_get(radio->cfg80211);
  MTLK_ASSERT(NULL != wiphy);
  if (NULL == wiphy) {
    res = MTLK_ERR_UNKNOWN;
    goto end;
  }

  MTLK_ASSERT(NULL != radio);
  df_user = mtlk_df_user_from_ndev(ndev);

  ILOG2_SSD("%s: Invoked from %s (%i)", ndev->name, current->comm, current->pid);
  MTLK_CHECK_DF_USER(df_user);

  res = _mtlk_df_user_invoke_core(mtlk_df_user_get_df(df_user),
    WAVE_CORE_REQ_GET_TR181_HW_STATS, &clpb, NULL, 0);
  res = _mtlk_df_user_process_core_retval(res, clpb,
    WAVE_CORE_REQ_GET_TR181_HW_STATS, FALSE);

  if (res != MTLK_ERR_OK)
    goto end;

  tr181_stats = mtlk_clpb_enum_get_next(clpb, &stats_size);

  MTLK_CLPB_TRY(tr181_stats, stats_size)
    rsp_nbuf = wv_cfg80211_vendor_cmd_alloc_reply_skb(wiphy,stats_size);
    if (!rsp_nbuf) {
      MTLK_CLPB_EXIT(MTLK_ERR_NO_MEM);
    }

    nla_put(rsp_nbuf, NL80211_ATTR_VENDOR_DATA, stats_size, tr181_stats);

    mtlk_clpb_delete(clpb);
    return wv_cfg80211_vendor_cmd_reply(rsp_nbuf);
  MTLK_CLPB_FINALLY(res)
    mtlk_clpb_delete(clpb);
  MTLK_CLPB_END

end:
  return _mtlk_df_mtlk_to_linux_error_code(res);
}

int wave_radio_get_recovery_statistics (
  wave_radio_t *radio,
  struct net_device *ndev,
  const void *data,
  int data_len)
{
  void *wiphy;
  mtlk_df_user_t *df_user;
  mtlk_clpb_t *clpb = NULL;
  mtlk_nbuf_t *rsp_nbuf;
  int res = MTLK_ERR_OK;
  mtlk_wssa_drv_recovery_stats_t *recovery_stats;
  uint32 stats_size;

  wiphy = wv_cfg80211_wiphy_get(radio->cfg80211);
  MTLK_ASSERT(NULL != wiphy);
  if (NULL == wiphy) {
    res = MTLK_ERR_UNKNOWN;
    goto end;
  }

  MTLK_ASSERT(NULL != radio);
  df_user = mtlk_df_user_from_ndev(ndev);

  ILOG2_SSD("%s: Invoked from %s (%i)", ndev->name, current->comm, current->pid);
  MTLK_CHECK_DF_USER(df_user);

  res = _mtlk_df_user_invoke_core(mtlk_df_user_get_df(df_user),
    WAVE_CORE_REQ_GET_RECOVERY_STATS, &clpb, NULL, 0);
  res = _mtlk_df_user_process_core_retval(res, clpb,
    WAVE_CORE_REQ_GET_RECOVERY_STATS, FALSE);

  if (res != MTLK_ERR_OK)
    goto end;

  recovery_stats = mtlk_clpb_enum_get_next(clpb, &stats_size);

  MTLK_CLPB_TRY(recovery_stats, stats_size)
    rsp_nbuf = wv_cfg80211_vendor_cmd_alloc_reply_skb(wiphy,stats_size);
    if (!rsp_nbuf) {
      MTLK_CLPB_EXIT(MTLK_ERR_NO_MEM);
    }

    nla_put(rsp_nbuf, NL80211_ATTR_VENDOR_DATA, stats_size, recovery_stats);

    mtlk_clpb_delete(clpb);
    return wv_cfg80211_vendor_cmd_reply(rsp_nbuf);
  MTLK_CLPB_FINALLY(res)
    mtlk_clpb_delete(clpb);
  MTLK_CLPB_END

end:
  return _mtlk_df_mtlk_to_linux_error_code(res);
}

int wave_radio_get_hw_flow_status (
  wave_radio_t *radio,
  struct net_device *ndev,
  const void *data,
  int data_len)
{
  void *wiphy;
  mtlk_df_user_t *df_user;
  mtlk_clpb_t *clpb = NULL;
  mtlk_nbuf_t *rsp_nbuf;
  int res = MTLK_ERR_OK;
  mtlk_wssa_drv_hw_stats_t *hw_flow_status;
  uint32 stats_size;

  wiphy = wv_cfg80211_wiphy_get(radio->cfg80211);
  MTLK_ASSERT(NULL != wiphy);
  if (NULL == wiphy) {
    res = MTLK_ERR_UNKNOWN;
    goto end;
  }

  MTLK_ASSERT(NULL != radio);
  df_user = mtlk_df_user_from_ndev(ndev);

  ILOG2_SSD("%s: Invoked from %s (%i)", ndev->name, current->comm, current->pid);
  MTLK_CHECK_DF_USER(df_user);

  res = _mtlk_df_user_invoke_core(mtlk_df_user_get_df(df_user),
    WAVE_CORE_REQ_GET_HW_FLOW_STATUS, &clpb, NULL, 0);
  res = _mtlk_df_user_process_core_retval(res, clpb,
    WAVE_CORE_REQ_GET_HW_FLOW_STATUS, FALSE);

  if (res != MTLK_ERR_OK)
    goto end;

  hw_flow_status = mtlk_clpb_enum_get_next(clpb, &stats_size);

  MTLK_CLPB_TRY(hw_flow_status, stats_size)
    rsp_nbuf = wv_cfg80211_vendor_cmd_alloc_reply_skb(wiphy,stats_size);
    if (!rsp_nbuf) {
      MTLK_CLPB_EXIT(MTLK_ERR_NO_MEM);
    }

    nla_put(rsp_nbuf, NL80211_ATTR_VENDOR_DATA, stats_size, hw_flow_status);

    mtlk_clpb_delete(clpb);
    return wv_cfg80211_vendor_cmd_reply(rsp_nbuf);
  MTLK_CLPB_FINALLY(res)
    mtlk_clpb_delete(clpb);
  MTLK_CLPB_END

end:
  return _mtlk_df_mtlk_to_linux_error_code(res);
}

#endif /* MTLK_LEGACY_STATISTICS */
