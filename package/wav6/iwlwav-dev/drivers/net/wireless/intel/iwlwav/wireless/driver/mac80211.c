/******************************************************************************

                               Copyright (c) 2012
                            Lantiq Deutschland GmbH

  For licensing information, see the file 'LICENSE' in the root folder of
  this software module.

******************************************************************************/
#include "mtlkinc.h"
#include <net/iw_handler.h>
#include <net/cfg80211.h>
#include <net/mac80211.h>
#ifndef CPTCFG_IWLWAV_X86_HOST_PC
#include <../net/wireless/core.h>
#endif
#include <linux/netdevice.h>

#include "mtlk_clipboard.h"
#include "mtlk_vap_manager.h"
#include "mtlk_coreui.h"
#include "mtlkdfdefs.h"
#include "mtlk_df_user_priv.h"
#include "mtlk_df_priv.h"
#include "mtlk_df.h"
#include "cfg80211.h"
#include "mac80211.h"
#include "mtlkaux.h"
#include "frame.h"
#include "mtlk_packets.h"
#include "wds.h"
#include "mtlk_df_nbuf.h"
#include "mtlk_param_db.h"
#include "mtlk_hs20_api.h"
#include "core_config.h"
#include "vendor_cmds.h"
#include "wave_radio.h"

#define LOG_LOCAL_GID             GID_MAC80211
#define LOG_LOCAL_FID             0

/* There is only a single combination (of STA's only) with a single limitation.
   AP mode VAPs have a separate cfg80211 PHY */
#define MAC80211_STA_MAX_IF_COMB  1
#define MAC80211_STA_MAX_IF_LIMIT 1
#define MAC80211_HW_TX_QUEUES     4

/* Let FW send CSAs to STAs associated to AP mode VAPs */
#define DEFAULT_CSA_COUNT         5

#define LINUX_ERR_OK              0

#define TX_STATUS_SUCCESS         1

#define API_WRAPPER(api_function) api_function ## _recovery_retry_wrapper

#define MAC80211_WIDAN_NDP_TID    7
#define MAC80211_WIDAN_NDP_TIMEOUT  10
#define MAC80211_CONNECTION_IDLE_TIME  (30 * HZ)

#define RCVRY_RETRY_TIMEOUT_MS    120000

extern struct iw_handler_def wave_linux_iw_radio_handler_def;
extern struct iw_handler_def wave_linux_iw_core_handler_def;

static struct ieee80211_iface_combination *create_mac80211_if_comb (wave_radio_t *radio);
static void wv_ieee80211_ch_switch_notify(wv_mac80211_t *mac80211, struct cfg80211_chan_def *chandef);
static void wv_ieee80211_csa_received(struct wiphy *wiphy, void *data,
  int data_len);

struct _wv_mac80211_t
{
  BOOL registered;
  /* mtlk_vap_manager_t *vap_manager; */
  wave_radio_t        *radio;
  struct ieee80211_hw *mac80211_hw;
  struct ieee80211_vif **vif_array;
};

/* Per interface driver private date allocated by mac80211 upon interface creation*/
struct wv_vif_priv {
  struct ieee80211_vif *vif;
  mtlk_df_user_t *df_user;
  u8 vap_index;
  mtlk_vap_handle_t vap_handle;
  u16 vif_sid;
  enum ieee80211_sta_state current_state;
  BOOL is_initialized;
  BOOL is_set_sid;
  u8 peer_ap_addr[ETH_ALEN];
  const struct net_device_ops *mac80211_netdev_ops;
  u16 beacon_int;
  unsigned long latest_rx_and_tx_packets;
  int ndp_counter;
};

static const uint32 _cipher_suites[] = {
  WLAN_CIPHER_SUITE_USE_GROUP,
  WLAN_CIPHER_SUITE_WEP40,
  WLAN_CIPHER_SUITE_TKIP,
  WLAN_CIPHER_SUITE_CCMP,
  WLAN_CIPHER_SUITE_WEP104,
  WLAN_CIPHER_SUITE_AES_CMAC,
  WLAN_CIPHER_SUITE_SMS4
};

static const uint32 _cipher_suites_gcmp[] = {
  WLAN_CIPHER_SUITE_USE_GROUP,
  WLAN_CIPHER_SUITE_WEP40,
  WLAN_CIPHER_SUITE_TKIP,
  WLAN_CIPHER_SUITE_CCMP,
  WLAN_CIPHER_SUITE_WEP104,
  WLAN_CIPHER_SUITE_AES_CMAC,
  WLAN_CIPHER_SUITE_SMS4,
  WLAN_CIPHER_SUITE_GCMP,
  WLAN_CIPHER_SUITE_GCMP_256
};

#define WV_STYPE_ALL              0xFFFF
#define WV_STYPE_STA_RX           (BIT(IEEE80211_STYPE_ACTION >> 4) |       \
                                   BIT(IEEE80211_STYPE_ASSOC_RESP >> 4) |  \
                                   BIT(IEEE80211_STYPE_REASSOC_RESP >> 4) |  \
                                   BIT(IEEE80211_STYPE_PROBE_RESP >> 4) |  \
                                   BIT(IEEE80211_STYPE_BEACON >> 4) |  \
                                   BIT(IEEE80211_STYPE_AUTH >> 4) |  \
                                   BIT(IEEE80211_STYPE_DISASSOC >> 4))

static const struct ieee80211_txrx_stypes
wv_mgmt_stypes[NUM_NL80211_IFTYPES] = {
  [NL80211_IFTYPE_STATION] = {
    .tx = WV_STYPE_ALL,
    .rx = WV_STYPE_STA_RX
  }
};

#define RATE(_rate, _rateid) {  \
  .bitrate        = (_rate),    \
  .hw_value       = (_rateid)   \
}

#define CHAN2G(_channel, _freq) {       \
  .band           = NL80211_BAND_2GHZ,  \
  .hw_value       = (_channel),         \
  .center_freq    = (_freq)             \
}

#define CHAN5G(_channel, _freq) {       \
  .band           = NL80211_BAND_5GHZ,  \
  .hw_value       = (_channel),         \
  .center_freq    = (_freq)             \
}

static const IEEE_ADDR _bcast_addr =
  { {0xff, 0xff, 0xff, 0xff, 0xff, 0xff} };
static const unsigned char eapol_header[] =
  { 0xaa, 0xaa, 0x03, 0x00, 0x00, 0x00, 0x88, 0x8e };

/*****************************************************************************/

#define WAVE_WV_CHECK_PTR_VOID(__ptr) \
do { \
  if (!(__ptr)) { \
    mtlk_assert_log_ss(MTLK_SLID, "Station mode interface:", #__ptr); \
      return; \
  } \
} while (0)

#define WAVE_WV_CHECK_PTR_RES(__ptr) \
do { \
  if (!(__ptr)) { \
    mtlk_assert_log_ss(MTLK_SLID, "Station mode interface:", #__ptr); \
      return -EINVAL; \
  } \
} while (0)

#define WAVE_WV_CHECK_PTR_GOTO(__ptr, __label) \
do { \
  if (!(__ptr)) { \
    mtlk_assert_log_ss(MTLK_SLID, "Station mode interface:", #__ptr); \
      goto __label; \
  } \
} while (0)

/*****************************************************************************/

static __INLINE struct _wv_mac80211_t *
__wv_ieee80211_hw_get_mac80211 (struct ieee80211_hw *mac80211_hw)
{
  return (wv_mac80211_t *)mac80211_hw->priv;
}

void* __MTLK_IFUNC
wv_mac80211_wiphy_get (
  wv_mac80211_t *mac80211)
{
  MTLK_ASSERT(mac80211 != NULL);

  return (void *)mac80211->mac80211_hw->wiphy;
}

static __INLINE struct scan_support *
__wv_mac80211_scan_support_get (wv_mac80211_t *mac80211)
{
  struct scan_support *ss = NULL;

  MTLK_ASSERT(NULL != mac80211);
  MTLK_ASSERT(NULL != mac80211->radio);
  if ((NULL != mac80211) && (NULL != mac80211->radio)) {
    ss = wave_radio_scan_support_get(mac80211->radio);
  }

  return ss;
}

static __INLINE struct mtlk_chan_def *
__wv_mac80211_chandef_get (wv_mac80211_t *mac80211)
{
  struct mtlk_chan_def *ccd = NULL;

  MTLK_ASSERT(NULL != mac80211);
  MTLK_ASSERT(NULL != mac80211->radio);
  if ((NULL != mac80211) && (NULL != mac80211->radio)) {
    ccd = wave_radio_chandef_get(mac80211->radio);
  }

  return ccd;
}

static __INLINE unsigned
__wv_mac80211_max_vaps_count_get (wv_mac80211_t *mac80211)
{
  MTLK_ASSERT(NULL != mac80211);
  MTLK_ASSERT(NULL != mac80211->radio);

  return mtlk_vap_manager_get_max_vaps_count(wave_radio_vap_manager_get(mac80211->radio));
}

static struct ieee80211_vif *
_wv_mac80211_get_vif (wv_mac80211_t *mac80211, u8 vap_index)
{
  struct ieee80211_vif *ieee80211_vif = NULL;

  MTLK_ASSERT(NULL != mac80211->vif_array);
  if (NULL != mac80211->vif_array) {
    ieee80211_vif = mac80211->vif_array[vap_index];
  }
  return ieee80211_vif;
}

static struct wv_vif_priv *
_wv_mac80211_get_vif_priv (wv_mac80211_t *mac80211, u8 vap_index)
{
  struct ieee80211_vif *ieee80211_vif;

  ieee80211_vif = _wv_mac80211_get_vif (mac80211, vap_index);
  if (NULL == ieee80211_vif)
    return NULL;

  return (struct wv_vif_priv *)ieee80211_vif->drv_priv;
}

static __INLINE BOOL
__wv_mac80211_get_sta_vifs_exist (wv_mac80211_t *mac80211)
{
  MTLK_ASSERT(NULL != mac80211);
  MTLK_ASSERT(NULL != mac80211->radio);

  return wave_radio_get_sta_vifs_exist(mac80211->radio);
}

static mtlk_df_t *
_wv_mac80211_master_df_get (wv_mac80211_t *mac80211)
{
  if (mac80211 == NULL) {
    ELOG_V("Station role Vif configuration, invalid mac80211 == NULL");
    return NULL;
  }

  if (mac80211->radio == NULL) {
    ELOG_V("Station role Vif configuration, mac80211->radio == NULL");
    return NULL;
  }

  return wave_radio_df_get(mac80211->radio);
}

static __INLINE wave_radio_t *
__wv_mac80211_radio_get (wv_mac80211_t *mac80211)
{
  MTLK_ASSERT(NULL != mac80211);
  return mac80211->radio;
}

wave_radio_t * __MTLK_IFUNC
wv_ieee80211_hw_radio_get (struct ieee80211_hw *hw)
{
  return __wv_mac80211_radio_get(__wv_ieee80211_hw_get_mac80211(hw));
}

static __INLINE mtlk_df_t *
__wv_ieee80211_hw_master_df_get (struct ieee80211_hw *hw)
{
  return _wv_mac80211_master_df_get(__wv_ieee80211_hw_get_mac80211(hw));
}

#if 0 /* FIXME: for the future */
static mtlk_vap_handle_t
_wv_ieee80211_hw_master_vap_handle_get (struct ieee80211_hw *hw)
{
  mtlk_df_t         *master_df;

  master_df = __wv_ieee80211_hw_master_df_get(hw);
  if (master_df == NULL) {
    return NULL;
  } else {
    return mtlk_df_get_vap_handle(master_df);
  }
}
#endif

static int
wait_rcvry_completion_if_needed (mtlk_core_t *core)
{
  int wait_iter_in_ms = 10;
  int no_of_retries = RCVRY_RETRY_TIMEOUT_MS / wait_iter_in_ms;
  int retry_counter = no_of_retries;

  while (retry_counter && (mtlk_core_rcvry_is_running(core) ||
         wave_rcvry_mac_fatal_pending_get(mtlk_vap_get_hw(core->vap_handle)))) {
    if (retry_counter == no_of_retries)
      ILOG0_V("Recovery is running, waiting for process to complete...");
    mtlk_osal_msleep(wait_iter_in_ms);
    retry_counter--;
  }

  if (retry_counter != no_of_retries)
    ILOG0_D("Waited %d ms for recovery procedure to complete", (no_of_retries - retry_counter) * wait_iter_in_ms);

  if (retry_counter)
    return MTLK_ERR_OK;
  else
    return MTLK_ERR_NOT_READY;
}

static bool wv_ieee80211_is_radar_chan(struct ieee80211_channel *chan)
{
  return (chan->flags & IEEE80211_CHAN_RADAR);
}

static struct ieee80211_hw *
_wave_vap_ieee80211_hw_get (mtlk_vap_handle_t vap_handle)
{
  wave_radio_t *radio = wave_vap_radio_get(vap_handle);
  return wave_radio_ieee80211_hw_get(radio);
}

struct ieee80211_hw * __MTLK_IFUNC
wave_vap_ieee80211_hw_get (mtlk_vap_handle_t vap_handle)
{
  return _wave_vap_ieee80211_hw_get (vap_handle);
}

static struct _wv_mac80211_t *
_wave_vap_get_mac80211 (mtlk_vap_handle_t vap_handle)
{
  struct ieee80211_hw   *mac80211_hw = wave_vap_ieee80211_hw_get(vap_handle);
  struct _wv_mac80211_t *wv_mac80211 = __wv_ieee80211_hw_get_mac80211(mac80211_hw);

  return wv_mac80211;
}

static struct wv_vif_priv *
_wave_vap_get_vif_priv (mtlk_vap_handle_t vap_handle)
{
  struct _wv_mac80211_t *wv_mac80211  = _wave_vap_get_mac80211(vap_handle);
  return _wv_mac80211_get_vif_priv(wv_mac80211, mtlk_vap_get_id(vap_handle));
}

static bool wv_ieee80211_supported_cipher_suite(struct wiphy *wiphy, u32 cipher)
{
  int i;
  for (i = 0; i < wiphy->n_cipher_suites; i++)
    if (cipher == wiphy->cipher_suites[i])
      return TRUE;

  return FALSE;
}

void __MTLK_IFUNC
wv_mac80211_iface_set_beacon_interval (wv_mac80211_t *mac80211, u8 vap_index, u16 beacon_interval)
{
  struct wv_vif_priv *wv_iface_inf = _wv_mac80211_get_vif_priv(mac80211, vap_index);

  if (__UNLIKELY(NULL == wv_iface_inf)) {
    return;
  }
  wv_iface_inf->beacon_int = beacon_interval;
}

void __MTLK_IFUNC
wv_mac80211_iface_set_initialized (wv_mac80211_t *mac80211, u8 vap_index)
{
  struct wv_vif_priv *wv_iface_inf = _wv_mac80211_get_vif_priv(mac80211, vap_index);

  if (__UNLIKELY(NULL == wv_iface_inf)) {
    return;
  }
  wv_iface_inf->is_initialized = TRUE;
}

BOOL __MTLK_IFUNC
wv_mac80211_iface_get_is_initialized (wv_mac80211_t *mac80211, u8 vap_index)
{
  struct wv_vif_priv *wv_iface_inf = _wv_mac80211_get_vif_priv(mac80211, vap_index);

  if (__UNLIKELY(NULL == wv_iface_inf)) {
    return FALSE;
  }
  return wv_iface_inf->is_initialized;
}

BOOL __MTLK_IFUNC
wv_mac80211_has_sta_connections (wv_mac80211_t *mac80211)
{
  int max_vaps_count = 0;
  unsigned vap_index;

  MTLK_ASSERT(NULL != mac80211);

  if (!__wv_mac80211_get_sta_vifs_exist(mac80211))
    return FALSE;

  max_vaps_count = __wv_mac80211_max_vaps_count_get(mac80211);
  for (vap_index = 0; vap_index < max_vaps_count; vap_index++) {
    struct wv_vif_priv *curr_vif_priv = _wv_mac80211_get_vif_priv(mac80211, vap_index);
    mtlk_vap_handle_t curr_vap_handle;
    mtlk_core_t *curr_core;

    if (curr_vif_priv == NULL) {
      continue;
    }

    curr_vap_handle = mtlk_df_get_vap_handle(mtlk_df_user_get_df(curr_vif_priv->df_user));
    curr_core = mtlk_vap_get_core(curr_vap_handle);

    if (NET_STATE_CONNECTED != mtlk_core_get_net_state(curr_core)) {
      /* Core is not ready */
      continue;
    }

    if (IEEE80211_STA_AUTHORIZED == curr_vif_priv->current_state) {
      /* found sta connected to peer ap */
      return TRUE;
    }
  }

  return FALSE;
}

static void _wv_ieee80211_op_tx (struct ieee80211_hw *hw,
  struct ieee80211_tx_control *control, struct sk_buff *skb)
{
  struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
  struct ieee80211_vif *vif = info->control.vif;
  struct ieee80211_hdr *hdr = (void *)skb->data;
  struct ieee80211_conf *conf = &hw->conf;
  struct cfg80211_chan_def *chandef = &conf->chandef;
  struct ieee80211_channel *channel = chandef->chan;
  struct wv_vif_priv *wv_iface_inf;
  mtlk_df_user_t *df_user;
  struct mgmt_tx_params mtp;
  u64 cookie;
  int in_chan;

  if (!netif_running(skb->dev)) {
    ELOG_V("You should bring interface up first");
    goto drop;
  }

  if (!vif) {
    ELOG_V("DROP skb with no vif");
    goto drop;
  }

  if (unlikely(ieee80211_is_assoc_resp(hdr->frame_control) ||
               ieee80211_is_reassoc_resp(hdr->frame_control) ||
               ieee80211_is_probe_resp(hdr->frame_control) ||
               ieee80211_is_beacon(hdr->frame_control))) {
    WLOG_V("STA can't send assoc resp, probe resp or beacon");
    goto drop;
  }

  wv_iface_inf = (struct wv_vif_priv *)vif->drv_priv;
  df_user = wv_iface_inf->df_user;
  if (df_user == NULL) {
    ELOG_V("df_user is NULL");
    goto drop;
  }
  in_chan = ieee80211_frequency_to_channel(channel->center_freq);
  memset(&mtp, 0, sizeof(mtp));

  if (ieee80211_is_nullfunc(hdr->frame_control) || ieee80211_is_qos_nullfunc(hdr->frame_control))
    mtp.extra_processing = PROCESS_NULL_DATA_PACKET;
  else
    mtp.extra_processing = PROCESS_MANAGEMENT;

  mtp.buf = skb->data;
  mtp.len = skb->len;
  mtp.channum = in_chan;
  mtp.cookie = &cookie;
  mtp.no_cck = info->flags & IEEE80211_TX_CTL_NO_CCK_RATE;
  mtp.dont_wait_for_ack = info->flags & IEEE80211_TX_CTL_NO_ACK;
  mtp.skb = skb;

  _mtlk_df_user_invoke_core_async(mtlk_df_user_get_df(df_user),
       WAVE_CORE_REQ_MGMT_TX, &mtp, sizeof(mtp), NULL, 0);
  return;

drop:
  ieee80211_free_txskb(hw, skb);
}

static void _wv_ieee80211_op_stop (struct ieee80211_hw *hw)
{
  wv_mac80211_t *mac80211;

  ILOG0_V("Last station role interface is stopped");
  mac80211 = __wv_ieee80211_hw_get_mac80211(hw);
  WAVE_WV_CHECK_PTR_VOID(mac80211);

  wave_radio_set_sta_vifs_exist(mac80211->radio, FALSE);
  if (mac80211->vif_array){
    mtlk_osal_mem_free(mac80211->vif_array);
    mac80211->vif_array = NULL;
  }
}

static int
_wv_ieee80211_op_config (struct ieee80211_hw *hw, u32 changed)
{
  struct ieee80211_conf     *conf = &hw->conf;
  struct cfg80211_chan_def  *chandef = &conf->chandef;
  struct ieee80211_channel  *channel = chandef->chan;
  mtlk_clpb_t *clpb = NULL;
  struct set_chan_param_data cpd;
  wv_mac80211_t     *mac80211 = __wv_ieee80211_hw_get_mac80211(hw);
  mtlk_vap_handle_t  master_vap_handle;
  mtlk_df_t         *master_df;
  mtlk_core_t       *master_core;
  int res = 0;
  mtlk_scan_support_t *scan_support;
  int beacon_period;
  struct mtlk_chan_def original_cd;

  WAVE_WV_CHECK_PTR_RES(mac80211);

  master_df = _wv_mac80211_master_df_get(mac80211);
  WAVE_WV_CHECK_PTR_RES(master_df);

  master_vap_handle = mtlk_df_get_vap_handle(master_df);
  master_core = mtlk_vap_get_core(master_vap_handle);

  if (wait_rcvry_completion_if_needed(master_core) != MTLK_ERR_OK){
    ELOG_V("Recovery is running, can't change configuration");
    return -EBUSY;
  }

  if (changed & IEEE80211_CONF_CHANGE_POWER) {
    /* MAC80211 TODO: set TX power limit according to conf->power_level*/
    ILOG0_D("Sta role interface, power level change, new level=%d",
        conf->power_level);
  }
  if (changed & IEEE80211_CONF_CHANGE_IDLE) {
    ILOG0_V("Sta role interface, interface idle state has changed");
  }

  if (changed & IEEE80211_CONF_CHANGE_CHANNEL) {
    memset(&cpd, 0, sizeof(cpd));
    wave_radio_chandef_copy(&cpd.chandef, chandef);
    /* master ndev ptr will be used to notify hostapd of AP VAP channel switch via cfg80211 API */
    cpd.ndev = mtlk_df_user_get_ndev(mtlk_df_get_user(master_df));
    cpd.vap_id = mtlk_vap_get_id(master_vap_handle);


    beacon_period = WAVE_RADIO_PDB_GET_INT(wave_vap_radio_get(master_core->vap_handle), PARAM_DB_RADIO_BEACON_PERIOD);
    scan_support =
        mtlk_core_get_scan_support(mtlk_vap_get_core(master_vap_handle));

    /* In case AP master VAP is active allow time for AP mode VAP(s)
     * to send CSAs to associated stations */
    if (is_mac_scan_running(scan_support)) {
      if (mtlk_core_is_chandef_identical(&cpd.chandef, &scan_support->orig_chandef)) {
        cpd.switch_type = ST_NORMAL;
        ILOG1_V("While mac scan - back to original");
      } else {
        cpd.switch_type = ST_SCAN;
        ILOG1_V("While mac scan - setting SCAN switch type");
      }
    } else if (mtlk_core_get_net_state(mtlk_vap_get_core(master_vap_handle)) == NET_STATE_CONNECTED) {
      cpd.switch_type = ST_CSA;
      cpd.chan_switch_time = DEFAULT_CSA_COUNT * beacon_period;
      ILOG1_D("Setting CSA switch type, switch time: %dms",
          DEFAULT_CSA_COUNT * beacon_period);
    } else {
      cpd.switch_type = ST_NORMAL;
      ILOG1_V("Not using CSA in channel switch");
    }

    ILOG1_SDDDDDD("Sta role interface, channel config change. band=%s, freq=%d, "
                "center_freq1=%d, center_freq2=%d, is_scan_running=%i, orig_channel=%i, "
                "switch_type=%i",
                (enum nl80211_band)channel->band == NL80211_BAND_2GHZ ? "2Ghz" : "5Ghz",
                channel->center_freq,
                chandef->center_freq1,
                chandef->center_freq2,
                is_scan_running(scan_support),
                mtlk_core_is_chandef_identical(&cpd.chandef, &scan_support->orig_chandef),
                cpd.switch_type);

    cpd.block_tx_pre = TRUE;
    cpd.block_tx_post = FALSE; /* TRUE means waiting for radars */
    cpd.radar_required = channel->flags & IEEE80211_CHAN_RADAR;

    original_cd = *__wv_mac80211_chandef_get(mac80211);
    wdev_lock(cpd.ndev->ieee80211_ptr);
    res = _mtlk_df_user_invoke_core(master_df, WAVE_RADIO_REQ_SET_CHAN, &clpb, &cpd, sizeof(cpd));
    res = _mtlk_df_user_process_core_retval(res, clpb, WAVE_RADIO_REQ_SET_CHAN, TRUE);
    wdev_unlock(cpd.ndev->ieee80211_ptr);
    if (res != MTLK_ERR_OK)
      ELOG_V("wv_ieee80211_config - error setting channel");
    else {
      if (cpd.switch_type != ST_SCAN)
        wv_ieee80211_ch_switch_notify(mac80211, chandef);
      wave_radio_ch_switch_event(hw->wiphy, master_core, &original_cd);
    }
  }

  return LINUX_ERR_OK;
}

/* Channel switch operation when CSAs were received from peer AP */
void _wv_ieee80211_op_channel_switch (struct ieee80211_hw *hw,
    struct ieee80211_vif *vif,
    struct ieee80211_channel_switch *ch_switch)
{
  wv_mac80211_t            *mac80211 = __wv_ieee80211_hw_get_mac80211(hw);
  struct cfg80211_chan_def *chandef = &ch_switch->chandef;
  struct ieee80211_channel *channel = chandef->chan;
  struct mtlk_chan_def     *ccd;
  mtlk_clpb_t *clpb = NULL;
  struct set_chan_param_data cpd;
  mtlk_df_t *master_df;
  mtlk_vap_handle_t master_vap_handle;
  mtlk_scan_support_t *scan_support;
  int beacon_period;
  int vap_index = 0;
  int max_vaps_count = 0;
  mtlk_core_t *master_core;
  struct mtlk_chan_def original_cd;
  struct intel_vendor_csa_received csa;
  int res = MTLK_ERR_PARAMS;

  ILOG0_D("Sta mode ch switch following CSA from peer AP in %d TBTTs",
      ch_switch->count);
  ILOG0_SDDD("Ch switch settings: band=%s, freq=%d, "
          "center_freq1=%d, center_freq2=%d",
          (enum nl80211_band)channel->band == NL80211_BAND_2GHZ ? "2Ghz" : "5Ghz",
          channel->center_freq,
          chandef->center_freq1,
          chandef->center_freq2);

  MTLK_ASSERT(hw->wiphy != NULL);
  memset(&csa, 0, sizeof(csa));
  csa.freq = ch_switch->chandef.chan->center_freq;
  csa.bandwidth = ch_switch->chandef.width;
  csa.center_freq1 = ch_switch->chandef.center_freq1;
  csa.center_freq2 = ch_switch->chandef.center_freq2;
  csa.count = ch_switch->count;
  wv_ieee80211_csa_received(hw->wiphy, &csa, sizeof(csa));

  /* Can't to continue if mac80211 is NULL */
  /* All other errors have to be handled */
  WAVE_WV_CHECK_PTR_VOID(mac80211);

  master_df = _wv_mac80211_master_df_get(mac80211);
  WAVE_WV_CHECK_PTR_GOTO(master_df, end);

  ccd = __wv_mac80211_chandef_get(mac80211);
  WAVE_WV_CHECK_PTR_GOTO(ccd, end);

  master_vap_handle = mtlk_df_get_vap_handle(master_df);
  master_core = mtlk_vap_get_core(master_vap_handle);
  WAVE_WV_CHECK_PTR_GOTO(master_core, end);

  memset(&cpd, 0, sizeof(cpd));
  wave_radio_chandef_copy(&cpd.chandef, chandef);
  /* master ndev ptr will be used to notify hostapd of AP VAP channel switch via cfg80211 API */
  cpd.ndev = mtlk_df_user_get_ndev(mtlk_df_get_user(master_df));
  cpd.vap_id = mtlk_vap_get_id(master_vap_handle);

  /* Update beacon period */
  beacon_period = DEFAULT_BEACON_INTERVAL;

  /* TODO: when multiple sta role vifs exist need to check which vif got the CSA
   * and set up a general policy regarding master sta role interface */
  max_vaps_count = __wv_mac80211_max_vaps_count_get(mac80211);
  for (vap_index = 0; vap_index < max_vaps_count; vap_index++) {
    struct wv_vif_priv *curr_vif_priv = _wv_mac80211_get_vif_priv(mac80211, vap_index);
    mtlk_vap_handle_t curr_vap_handle;
    mtlk_core_t *curr_core;

    if (curr_vif_priv == NULL) {
      continue;
    }

    if (curr_vif_priv->df_user == NULL) {
      ELOG_V("df_user is NULL");
      continue;
    }
    curr_vap_handle = mtlk_df_get_vap_handle(mtlk_df_user_get_df(curr_vif_priv->df_user));
    curr_core = mtlk_vap_get_core(curr_vap_handle);

    if (NET_STATE_CONNECTED != mtlk_core_get_net_state(curr_core)) {
      continue;
    }

    if (IEEE80211_STA_AUTHORIZED == curr_vif_priv->current_state) {
      /* found sta connected to peer ap */
      beacon_period = curr_vif_priv->beacon_int;
      break;
    }
  }

  scan_support =
      mtlk_core_get_scan_support(mtlk_vap_get_core(master_vap_handle));

  if (scan_support->dfs_debug_params.beacon_count > 0) {
    cpd.switch_type = ST_CSA;
    cpd.chan_switch_time =
        scan_support->dfs_debug_params.beacon_count * beacon_period;
    ILOG1_D("Using debug CSA Beacon count: %d",
        scan_support->dfs_debug_params.beacon_count);
  } else {
    if (ch_switch->count != 0){
      cpd.switch_type = ST_CSA;
      cpd.chan_switch_time = ch_switch->count * beacon_period;
    }
  }

  cpd.block_tx_pre = ch_switch->block_tx;
  cpd.radar_required = FALSE;

  /* check if we are moving to DFS channel, if we are we are stoping all TX after changing channel untill we will see a beacon */
  if (wv_ieee80211_is_radar_chan(channel)) {
    cpd.switch_type = ST_CSA;
    cpd.block_tx_post = TRUE;
    ILOG0_V("Received CSA from far AP to a DFS channel, blocking TX");
    ccd->wait_for_beacon = FALSE;
    master_core->slow_ctx->is_block_tx = TRUE;
  } else {
    cpd.block_tx_post = FALSE; /* TRUE means waiting for radars */
  }

  original_cd = *__wv_mac80211_chandef_get(mac80211);
  wdev_lock(cpd.ndev->ieee80211_ptr);
  res = _mtlk_df_user_invoke_core(master_df, WAVE_RADIO_REQ_SET_CHAN, &clpb, &cpd, sizeof(cpd));
  res = _mtlk_df_user_process_core_retval(res, clpb, WAVE_RADIO_REQ_SET_CHAN, TRUE);
  wdev_unlock(cpd.ndev->ieee80211_ptr);
  if (res != MTLK_ERR_OK){
    if (ccd->wait_for_beacon == FALSE &&
        master_core->slow_ctx->is_block_tx == TRUE) {
      master_core->slow_ctx->is_block_tx = FALSE;
    }
    ELOG_V("Error setting channel");

  } else {
    if (cpd.switch_type != ST_SCAN)
      wv_ieee80211_ch_switch_notify(mac80211, chandef);

    if (wv_ieee80211_is_radar_chan(channel)) {
      ILOG0_V("Channel switched to DFS channel, blocking tx until a beacon is found");
      master_core->slow_ctx->is_block_tx = TRUE;
      ccd->wait_for_beacon = TRUE;
    }
    wave_radio_ch_switch_event(hw->wiphy, master_core, &original_cd);
  }

end:
  /* Notify mac80211 that peer AP initiated ch switch (via CSA) is done */
  for (vap_index = max_vaps_count - 1; vap_index >= 0; vap_index--) {
    /*MAC80211 TODO: disable removing an interface while we call chswitch_done for this interface*/
    struct wv_vif_priv *curr_vif_priv = _wv_mac80211_get_vif_priv(mac80211, vap_index);
    if (curr_vif_priv == NULL) {
      continue;
    }

    ieee80211_chswitch_done(curr_vif_priv->vif, res == MTLK_ERR_OK ? TRUE : FALSE);
  }
}

static int _wv_ieee80211_op_start (struct ieee80211_hw *hw)
{
  wv_mac80211_t *mac80211;
  mtlk_df_t *master_df;
  int max_vaps_count = 0;

  ILOG0_V("First station role interface is started");
  mac80211 = __wv_ieee80211_hw_get_mac80211(hw);
  WAVE_WV_CHECK_PTR_RES(mac80211);

  master_df = _wv_mac80211_master_df_get(mac80211);
  WAVE_WV_CHECK_PTR_RES(master_df);

  /* from this point on, and until ieee80211_stop AP VAPs should not control channel settings */
  wave_radio_set_sta_vifs_exist(mac80211->radio, TRUE);

  max_vaps_count = __wv_mac80211_max_vaps_count_get(mac80211);
  mac80211->vif_array = (struct ieee80211_vif **)mtlk_osal_mem_alloc((sizeof(struct ieee80211_vif *) * max_vaps_count), MTLK_MEM_TAG_MAC80211);
  if (NULL == mac80211->vif_array) {
    return MTLK_ERR_NO_MEM;
  }
  memset(mac80211->vif_array, 0, sizeof(struct ieee80211_vif *) * max_vaps_count);

  return LINUX_ERR_OK;
}


static int wv_request_sid (mtlk_df_user_t *df_user, u16 *vif_sid, const uint8 *mac_addr)
{
  int res = MTLK_ERR_OK;
  mtlk_clpb_t *clpb = NULL;

  res = _mtlk_df_user_invoke_core(mtlk_df_user_get_df(df_user),
      WAVE_CORE_REQ_REQUEST_SID, &clpb, mac_addr, IEEE_ADDR_LEN);
  res = _mtlk_df_user_process_core_retval(res, clpb,
      WAVE_CORE_REQ_REQUEST_SID, FALSE);

  if (MTLK_ERR_OK == res) {
    u16 *sid;
    uint32 sid_size;

    sid = mtlk_clpb_enum_get_next(clpb, &sid_size);
    MTLK_CLPB_TRY(sid, sid_size)
      *vif_sid = *sid;
    MTLK_CLPB_FINALLY(res)
      mtlk_clpb_delete(clpb); /* already deleted in error cases */
    MTLK_CLPB_END
  }

  return res;
}

static int wv_release_sid(mtlk_df_user_t *df_user, u16 vif_sid)
{
  int res = MTLK_ERR_OK;
  mtlk_clpb_t *clpb = NULL;

  res = _mtlk_df_user_invoke_core(mtlk_df_user_get_df(df_user),
      WAVE_CORE_REQ_REMOVE_SID, &clpb, &vif_sid, sizeof(vif_sid));
  res = _mtlk_df_user_process_core_retval(res, clpb,
      WAVE_CORE_REQ_REMOVE_SID, TRUE);

  return res;
}

static int wv_request_sid_if_needed(struct ieee80211_vif *vif,
    struct ieee80211_sta *sta)
{
  struct wv_vif_priv *wv_iface_inf = (struct wv_vif_priv *)vif->drv_priv;
  mtlk_df_user_t *df_user = wv_iface_inf->df_user;
  int ret = 0;

  if (wv_iface_inf->is_set_sid) {
    if(mtlk_osal_compare_eth_addresses(wv_iface_inf->peer_ap_addr, sta->addr) != 0) {
      ILOG2_V("change vif_sid");
      ret = wv_release_sid(df_user, wv_iface_inf->vif_sid);
      if (ret) {
        ELOG_V("wv_release_sid failed");
        return  ret;
      }
      ret = wv_request_sid(df_user, &wv_iface_inf->vif_sid, sta->addr);
      if (ret) {
        ELOG_V("wv_request_sid failed");
        return ret;
      }
      mtlk_osal_copy_eth_addresses(wv_iface_inf->peer_ap_addr, sta->addr);
    }
  } else {
    ILOG2_V("new vif_sid");
    ret = wv_request_sid(df_user, &wv_iface_inf->vif_sid, sta->addr);
    if (ret) {
      ELOG_V("wv_request_sid failed");
      return ret;
    }
    mtlk_osal_copy_eth_addresses(wv_iface_inf->peer_ap_addr, sta->addr);
    wv_iface_inf->is_set_sid = TRUE;
  }

  return ret;
}

uint8* __MTLK_IFUNC
wv_ieee80211_peer_ap_address(struct ieee80211_vif *vif)
{
  struct wv_vif_priv *wv_iface_inf = (struct wv_vif_priv*)vif->drv_priv;
  if (!wv_iface_inf) {
    ELOG_V("Error getting peer_ap_address");
    return NULL;
  }

  return wv_iface_inf->peer_ap_addr;
}

static int wv_activate_vif(mtlk_df_user_t *df_user)
{
  struct mtlk_ap_settings aps;
  int res;

  mtlk_clpb_t *clpb = NULL;
  aps.beacon_interval = 0;
  aps.dtim_period = 0;
  aps.hidden_ssid = 0;
  aps.essid_len = 0;
  aps.essid[aps.essid_len] = '\0';

  /* Set security is currently not done, using open mode for now */

  ILOG2_V("Switching to serializer to activate the Station mode Vif");
  res = _mtlk_df_user_invoke_core(mtlk_df_user_get_df(df_user),
      WAVE_CORE_REQ_ACTIVATE_OPEN, &clpb, &aps, sizeof(aps));
  res = _mtlk_df_user_process_core_retval(res, clpb,
      WAVE_CORE_REQ_ACTIVATE_OPEN, TRUE);
  return _mtlk_df_mtlk_to_linux_error_code(res);
}

/* Saves the mac addr from vif into pdb
 * Must be called after vap init & before vap activate
 */
static int wv_set_mac_addr_pdb(mtlk_df_user_t *df_user, const char *mac_adr)
{
  mtlk_clpb_t *clpb = NULL;
  int res = MTLK_ERR_BUSY;

  MTLK_ASSERT(NULL != df_user);
  res = _mtlk_df_user_invoke_core(mtlk_df_user_get_df(df_user),
      WAVE_CORE_REQ_SET_MAC_ADDR, &clpb, mac_adr, ETH_ALEN);
  res = _mtlk_df_user_process_core_retval_void(res, clpb,
      WAVE_CORE_REQ_SET_MAC_ADDR, TRUE);

  return res;
}

mtlk_df_user_t *wv_ieee80211_ndev_to_dfuser (struct net_device *ndev)
{
  struct ieee80211_vif *vif;
  struct wv_vif_priv *wv_iface_inf;

  if (__UNLIKELY(!ndev))
    return NULL;

  vif = net_device_to_ieee80211_vif(ndev);
  wv_iface_inf = (struct wv_vif_priv*)vif->drv_priv;
  MTLK_ASSERT(wv_iface_inf != NULL);
  if (__UNLIKELY(wv_iface_inf == NULL)) {
    ELOG_V("Error getting df_user");
    return NULL;
  }
  return wv_iface_inf->df_user;
}

static int _wv_ieee80211_op_add_interface (struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{
  struct wv_vif_priv* wv_iface_inf;
  int res = 0;
  mtlk_clpb_t *clpb = NULL;
  mtlk_mbss_cfg_t mbss_cfg;
  mtlk_df_user_t **clpb_data;
  uint32 clpb_data_size;
  wv_mac80211_t *mac80211;
  mtlk_vap_info_internal_t *_info;
  mtlk_df_user_t *df_user;
  mtlk_df_t      *master_df;
  struct net_device_ops *ops;
  mtlk_core_t *master_core;
  struct net_device *netdev_p = NULL;

  ILOG1_SSD("%s: Invoked from %s (%i)", ieee80211_vif_to_name(vif), current->comm, current->pid);
  ILOG0_SY("Adding sta interface %s, mac addr: %Y. ",
      ieee80211_vif_to_name(vif), vif->addr);

  /* should not happen */
  if (vif->type !=  NL80211_IFTYPE_STATION) {
    ELOG_V("Error, virtual interface creation using mac80211 framework - interface type must be station.");
    return -ENOTSUPP;
  }

  wv_iface_inf = (struct wv_vif_priv*)vif->drv_priv;
  WAVE_WV_CHECK_PTR_RES(wv_iface_inf);
  MTLK_ASSERT(wv_iface_inf != NULL);

  mac80211 = __wv_ieee80211_hw_get_mac80211(hw);
  WAVE_WV_CHECK_PTR_RES(mac80211);

  master_df = _wv_mac80211_master_df_get(mac80211);
  WAVE_WV_CHECK_PTR_RES(master_df);

  master_core = mtlk_vap_get_core(mtlk_df_get_vap_handle(master_df));
  if (wait_rcvry_completion_if_needed(master_core) != MTLK_ERR_OK){
    ELOG_V("Recovery is running, can't change configuration");
    return -EBUSY;
  }

  mbss_cfg.added_vap_name = ieee80211_vif_to_name(vif);
  mbss_cfg.wiphy = hw->wiphy;
  mbss_cfg.role = MTLK_ROLE_STA;
  mbss_cfg.is_master = FALSE; /* AP mode VAP is master */

  /* Basically mac80211 handles net device. wlan driver that works with mac80211 framework
   * should not interact with wdev or netdev directly. However, we do need netdev for DCDP
   * and other features */
  netdev_p = dev_get_by_name(&init_net, mbss_cfg.added_vap_name);
  MTLK_ASSERT(netdev_p != NULL);
  if (!netdev_p) {
    ELOG_S("Net device for interface %s was not found",
        mbss_cfg.added_vap_name);
    return -EINVAL;
  }
  mbss_cfg.ndev = netdev_p;
  netdev_p->priv_flags &= ~IFF_DONT_BRIDGE;

  /* Keep trying in case scan is in progress for the current radio */
  {
    int retry_counter = 0;
    do {
      res = _mtlk_df_user_invoke_core(master_df,
                WAVE_RADIO_REQ_MBSS_ADD_VAP_NAME, &clpb, &mbss_cfg, sizeof(mbss_cfg));
      res = _mtlk_df_user_process_core_retval_void(res, clpb,
                WAVE_RADIO_REQ_MBSS_ADD_VAP_NAME, FALSE);

      if (MTLK_ERR_RETRY != res) break;
      mtlk_osal_msleep(50);
      retry_counter++;
    } while ((MTLK_ERR_RETRY == res) && (retry_counter < MAX_VAP_WAIT_RETRIES));

    if (retry_counter > 0)
      ILOG0_SD("%s: Scan waited, number of retries %d",
               mtlk_df_user_get_name(mtlk_df_get_user(master_df)), retry_counter);
  }

  if (MTLK_ERR_OK != res)
    goto end;

  clpb_data = mtlk_clpb_enum_get_next(clpb, &clpb_data_size);
  MTLK_CLPB_TRY(clpb_data, clpb_data_size)
    wv_iface_inf->df_user = *clpb_data;

    _info = (mtlk_vap_info_internal_t *)mtlk_df_get_vap_handle(mtlk_df_user_get_df(wv_iface_inf->df_user));
    wv_iface_inf->vap_index = _info->id;
    wv_iface_inf->latest_rx_and_tx_packets = 0;
    wv_iface_inf->ndp_counter = 0;

    /* Replace mac80211 data tx netdev op in order to support DCDP and fastpath */
    df_user = wv_iface_inf->df_user;
    ops = mtlk_df_user_get_ndev_ops(df_user);
    *ops = *netdev_p->netdev_ops;
    ops->ndo_start_xmit = _mtlk_df_user_linux_start_tx;
    wv_iface_inf->mac80211_netdev_ops = netdev_p->netdev_ops;
    netdev_p->netdev_ops = ops;

    /* set device MAC address */
    wv_set_mac_addr_pdb(wv_iface_inf->df_user, vif->addr);

    ASSERT(mac80211->vif_array != NULL);
    wv_iface_inf->vif = vif;
    mac80211->vif_array[wv_iface_inf->vap_index] = vif;

    /* Activate vif in FW */
    res = wv_activate_vif(wv_iface_inf->df_user);
  MTLK_CLPB_FINALLY(res)
    mtlk_clpb_delete(clpb);
  MTLK_CLPB_END;
end:
  /* Decrement ref-counted netdev */
  dev_put(netdev_p);
  return _mtlk_df_mtlk_to_linux_error_code(res);
}

static void _wv_ieee80211_op_remove_interface (struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{
  struct wv_vif_priv* wv_iface_inf;
  int res = 0;
  mtlk_clpb_t *clpb = NULL;
  mtlk_mbss_cfg_t mbss_cfg;
  wv_mac80211_t *mac80211;
  mtlk_df_t *master_df;
  mtlk_vap_handle_t _vap_to_be_removed;
  struct net_device *netdev_p = NULL;
  mtlk_core_t *core;

  ILOG0_SY("Removing sta interface %s, mac addr: %Y. ",
      ieee80211_vif_to_name(vif), vif->addr);

  mac80211 = __wv_ieee80211_hw_get_mac80211(hw);
  WAVE_WV_CHECK_PTR_VOID(mac80211);

  master_df = _wv_mac80211_master_df_get(mac80211);
  WAVE_WV_CHECK_PTR_VOID(master_df);

  wv_iface_inf = (struct wv_vif_priv*)vif->drv_priv;
  WAVE_WV_CHECK_PTR_VOID(wv_iface_inf);
  WAVE_WV_CHECK_PTR_VOID(wv_iface_inf->df_user);

  _vap_to_be_removed =
      mtlk_df_get_vap_handle(mtlk_df_user_get_df(wv_iface_inf->df_user));
  MTLK_ASSERT(_vap_to_be_removed != NULL);
  if (_vap_to_be_removed == NULL) {
    ELOG_V("Station mode interface removal error, _vap_handle == NULL");
    return;
  }
  core = mtlk_vap_get_core(_vap_to_be_removed);
  if (wait_rcvry_completion_if_needed(core) != MTLK_ERR_OK){
    ELOG_V("Recovery is running, can't remove interface!");
    return;
  }

  /* Just before we remove VAP set original mac80211 net_device operations back
   * since even when device is down it may still exist and ops may still be called */
  netdev_p = dev_get_by_name(&init_net,  ieee80211_vif_to_name(vif));
  netdev_p->netdev_ops = wv_iface_inf->mac80211_netdev_ops;

  /* Release FW allocated SID */
  if (wv_iface_inf->is_set_sid) {
    res = wv_release_sid(wv_iface_inf->df_user, wv_iface_inf->vif_sid);
    if (res) {
      ELOG_D("wv_release_sid failed, error code: %d", res);
    }
    wv_iface_inf->is_set_sid = FALSE;
  }

  wv_iface_inf->is_initialized = FALSE;
  wv_iface_inf->latest_rx_and_tx_packets = 0;
  wv_iface_inf->ndp_counter = 0;

  /* Remove VAP */
  mbss_cfg.vap_handle = _vap_to_be_removed;

  wv_iface_inf->df_user = NULL;

  /* Keep trying in case scan is in progress for the current radio */
  {
    int retry_counter = 0;
    do {

      res = _mtlk_df_user_invoke_core(master_df,
                WAVE_RADIO_REQ_MBSS_DEL_VAP_NAME, &clpb, &mbss_cfg, sizeof(mbss_cfg));
      res = _mtlk_df_user_process_core_retval(res, clpb,
                WAVE_RADIO_REQ_MBSS_DEL_VAP_NAME, TRUE);

      if (MTLK_ERR_RETRY != res) break;
      mtlk_osal_msleep(50);
      retry_counter++;
    } while ((MTLK_ERR_RETRY == res) && (retry_counter < MAX_VAP_WAIT_RETRIES));

    if (retry_counter > 0)
      ILOG0_SD("%s: Scan waited, number of retries %d",
               mtlk_df_user_get_name(mtlk_df_get_user(master_df)), retry_counter);
  }

  if (res != MTLK_ERR_OK)
    ELOG_D("Station mode interface removal error %d", res);

  ASSERT (mac80211->vif_array != NULL);
  mac80211->vif_array[wv_iface_inf->vap_index] = NULL;

  /* Decrement ref-counted netdev aquired in this function */
  dev_put(netdev_p);
  return;
}

static void _wv_ieee80211_op_configure_filter (struct ieee80211_hw *hw,
    unsigned int changed_flags,
    unsigned int *total_flags,
    u64 multicast)
{
  ILOG0_V("Sta interface filter configuration, RX filter is not supported.");
  /*
  * Receiving all multicast frames is always enabled since we currently
  * do not support programming multicast filters into the device.
  * FIF_CONTROL should not be set, since our HW does not send control
  * frames up the stack
  */
  *total_flags &= FIF_OTHER_BSS | FIF_ALLMULTI;
}

static int wv_request_sta_connect_ap(mtlk_df_user_t *df_user,
    struct ieee80211_hw *hw, struct ieee80211_vif *vif,
    struct ieee80211_sta *sta, struct station_info *sinfo)
{
  struct wv_vif_priv *wv_iface_inf = (struct wv_vif_priv *)vif->drv_priv;
  sta_info sta_info;
  mtlk_clpb_t *clpb = NULL;
  int i = 0, band = 0, res = 0;

  ILOG2_V("wv_request_sta_connect_ap");

  memset(&sta_info, 0, sizeof(sta_info));

  for (band = 0; band < NUM_SUPPORTED_BANDS; band++) {
    unsigned long basic_rates = sta->supp_rates[band];
    for_each_set_bit(i, &basic_rates, BITS_PER_LONG) {
      int bitrate = hw->wiphy->bands[band]->bitrates[i].bitrate / 5;
      sta_info.rates[sta_info.supported_rates_len++] = bitrate;
      if (band == NL80211_BAND_5GHZ && wave_radio_is_rate_80211ag(bitrate)) {
        MTLK_BIT_SET(sta_info.sta_net_modes, MTLK_WSSA_11A_SUPPORTED, 1);
      } else if (band == NL80211_BAND_2GHZ) {
        if (wave_radio_is_rate_80211ag(bitrate))
          MTLK_BIT_SET(sta_info.sta_net_modes, MTLK_WSSA_11G_SUPPORTED, 1);
        if (wave_radio_is_rate_80211b(bitrate)) {
          MTLK_BIT_SET(sta_info.sta_net_modes, MTLK_WSSA_11B_SUPPORTED, 1);
        }
      }
    }
  }

  wv_iface_inf->latest_rx_and_tx_packets = 0;
  wv_iface_inf->ndp_counter = 0;
  sta_info.sid = wv_iface_inf->vif_sid;
  sta_info.listen_interval = hw->max_listen_interval;
  sta_info.uapsd_queues = sta->uapsd_queues;
  sta_info.max_sp = sta->max_sp;
  sta_info.aid = sta->aid;
  if (sinfo  && (sinfo->filled & BIT(NL80211_STA_INFO_SIGNAL))) {
    sta_info.rssi_dbm = sinfo->signal;
    mtlk_stadb_stats_set_mgmt_rssi(&sta_info, sta_info.rssi_dbm);
  } else
    mtlk_stadb_stats_set_mgmt_rssi(&sta_info, MIN_RSSI);
  mtlk_osal_copy_eth_addresses(sta_info.addr.au8Addr, sta->addr);
  MTLK_BFIELD_SET(sta_info.flags, STA_FLAGS_MFP, sta->mfp ? 1 : 0);
  MTLK_BFIELD_SET(sta_info.flags, STA_FLAGS_WMM, sta->wme ? 1 : 0);

  if (sta->ht_cap.ht_supported) {
    if(memcmp(sta_info.rx_mcs_bitmask, sta->ht_cap.mcs.rx_mask,
        sizeof(sta_info.rx_mcs_bitmask)) != 0) {
      sta_info.ht_cap_info = MAC_TO_HOST16(sta->ht_cap.cap);
      sta_info.ampdu_param =
          sta->ht_cap.ampdu_density << 2 | sta->ht_cap.ampdu_factor;
      MTLK_BFIELD_SET(sta_info.flags, STA_FLAGS_11n, 1);
      MTLK_BIT_SET(sta_info.sta_net_modes, MTLK_WSSA_11N_SUPPORTED, 1);
      wave_memcpy(sta_info.rx_mcs_bitmask, sizeof(sta_info.rx_mcs_bitmask),
                  sta->ht_cap.mcs.rx_mask, sizeof(sta->ht_cap.mcs.rx_mask));
    }
  }

  if (sta->vht_cap.vht_supported) {
    if(memcmp(&sta_info.vht_mcs_info, &sta->vht_cap.vht_mcs,
        sizeof(sta_info.vht_mcs_info)) != 0) {
      sta_info.vht_cap_info = MAC_TO_HOST32(sta->vht_cap.cap);
      MTLK_BFIELD_SET(sta_info.flags, STA_FLAGS_11ac, 1);
      MTLK_BIT_SET(sta_info.sta_net_modes, MTLK_WSSA_11AC_SUPPORTED, 1);
      wave_memcpy(&sta_info.vht_mcs_info, sizeof(sta_info.vht_mcs_info),
                  &sta->vht_cap.vht_mcs,  sizeof(sta->vht_cap.vht_mcs));
    }
  }

  if (sta->vendor_wds)
    sta_info.WDS_client_type = FOUR_ADDRESSES_STATION;

  res = _mtlk_df_user_invoke_core(mtlk_df_user_get_df(df_user),
      WAVE_CORE_REQ_AP_CONNECT_STA, &clpb, &sta_info, sizeof(sta_info));
  res = _mtlk_df_user_process_core_retval(res, clpb,
      WAVE_CORE_REQ_AP_CONNECT_STA, TRUE);

  return res;
}

static int wv_request_sta_disconnect_ap(mtlk_df_user_t *df_user,
    struct ieee80211_sta *sta)
{
  mtlk_clpb_t *clpb = NULL;
  int res = 0;

  ILOG2_V("wv_request_sta_disconnect_ap");

  res = _mtlk_df_user_invoke_core(mtlk_df_user_get_df(df_user),
      WAVE_CORE_REQ_AP_DISCONNECT_STA, &clpb, sta->addr, ETH_ALEN);
  res = _mtlk_df_user_process_core_retval(res, clpb,
      WAVE_CORE_REQ_AP_DISCONNECT_STA, TRUE);

  return res;
}

static int wv_request_change_station(mtlk_df_user_t *df_user,
    struct ieee80211_sta *sta, BOOL is_authorizing)
{
  int res = MTLK_ERR_PARAMS;
  mtlk_clpb_t *clpb = NULL;
  mtlk_core_ui_authorizing_cfg_t t_authorizing;

  memset(&t_authorizing, 0, sizeof(t_authorizing));

  MTLK_CFG_SET_ITEM_BY_FUNC_VOID(&t_authorizing, sta_addr, ieee_addr_set, (&t_authorizing.sta_addr, sta->addr));
  MTLK_CFG_SET_ITEM(&t_authorizing, authorizing, is_authorizing);

  res = _mtlk_df_user_invoke_core(mtlk_df_user_get_df(df_user),
      WAVE_CORE_REQ_AUTHORIZING_STA, &clpb, &t_authorizing,
      sizeof(t_authorizing));
  res = _mtlk_df_user_process_core_retval_void(res, clpb,
      WAVE_CORE_REQ_AUTHORIZING_STA, TRUE);

  return res;
}

static int wv_request_get_station_info(mtlk_df_user_t *df_user,
    struct ieee80211_sta *sta, struct station_info *sinfo)
{
  mtlk_clpb_t *clpb = NULL;
  int res = 0;
  st_info_data_t info_data;

  ILOG2_V("wv_request_get_station_info");

  MTLK_ASSERT(NULL != sta);
  MTLK_ASSERT(NULL != df_user);

  ILOG2_Y("MAC = %Y", sta->addr);

  if (!mtlk_osal_is_valid_ether_addr(sta->addr)) {
    ELOG_Y("Invalid MAC address: %Y", sta->addr);
    res = MTLK_ERR_PARAMS;
    goto finish;
  }

  info_data.mac = sta->addr;
  info_data.stinfo = sinfo;
  res = _mtlk_df_user_invoke_core(mtlk_df_user_get_df(df_user), WAVE_CORE_REQ_GET_STATION, &clpb, &info_data, sizeof(st_info_data_t));
  res = _mtlk_df_user_process_core_retval(res, clpb, WAVE_CORE_REQ_GET_STATION, TRUE);

finish:
  return res;
}

static int _wv_ieee80211_op_sta_state (struct ieee80211_hw *hw,
    struct ieee80211_vif *vif,
    struct ieee80211_sta *sta,
    enum ieee80211_sta_state old_state,
    enum ieee80211_sta_state new_state)
{
  struct wv_vif_priv *wv_iface_inf = (struct wv_vif_priv *)vif->drv_priv;
  mtlk_df_user_t *df_user = wv_iface_inf->df_user;
  int ret = 0;
  mtlk_vap_handle_t vap_handle;
  mtlk_core_t *core;

  ILOG0_YDD("station %Y state change %d->%d", sta->addr,
      old_state, new_state);
  if (df_user == NULL) {
    ELOG_V("df_user is NULL");
    return -EPERM;
  }
  vap_handle = mtlk_df_get_vap_handle(mtlk_df_user_get_df(df_user));
  core = mtlk_vap_get_core(vap_handle);

  /* In case sta state has changed as a result of connection loss caused by FW reset
   * AP the sta is connected to is no longer in sta DB and there is no need to update
   * FW about station status changes */
  if (mtlk_core_rcvry_is_running(core) ||
      wave_rcvry_mac_fatal_pending_get(mtlk_vap_get_hw(core->vap_handle)) ||
      wv_iface_inf->is_initialized == FALSE) {

    wv_iface_inf->current_state = new_state;

    if (new_state < old_state){
      ILOG0_V("Station change while in recovery, no need to update FW");
      return _mtlk_df_mtlk_to_linux_error_code(MTLK_ERR_OK);
    } else {
      ELOG_V("Station change while in recovery");
      return -EBUSY;
    }
  }

  if (old_state == IEEE80211_STA_NOTEXIST && new_state == IEEE80211_STA_NONE) {
    ret = wv_request_sid_if_needed(vif, sta);
    if (ret) {
      ELOG_V("wv_request_sid_if_needed failed");
      goto out;
    }

  } else if (old_state == IEEE80211_STA_NONE && new_state == IEEE80211_STA_AUTH) {
  } else if (old_state == IEEE80211_STA_AUTH && new_state == IEEE80211_STA_ASSOC) {
    /* struct station_info is bigger than 800 bytes in kernel 4.x, avoid allocating it on stack */
    struct station_info *sinfo_ptr = (struct station_info *)mtlk_osal_mem_alloc(sizeof(struct station_info), MTLK_MEM_TAG_MAC80211);
    if (sinfo_ptr) {
      ret = wv_request_get_station_info(df_user, sta, sinfo_ptr);
      ret = wv_request_sta_connect_ap(df_user, hw, vif, sta, (ret == MTLK_ERR_OK) ? sinfo_ptr : NULL);
      mtlk_osal_mem_free(sinfo_ptr);
    }
    else
      return -ENOMEM;

    if (ret) {
      ELOG_V("wv_request_sta_connect_ap failed");
      goto out;
    }
  } else if (old_state == IEEE80211_STA_ASSOC && new_state == IEEE80211_STA_AUTHORIZED) {
    ret = wv_request_change_station(df_user, sta, TRUE);
    if (ret) {
      ELOG_V("wv_request_change_station failed");
      goto out;
    }
  } else if (old_state == IEEE80211_STA_AUTHORIZED && new_state == IEEE80211_STA_ASSOC) {
    ret = wv_request_change_station(df_user, sta, FALSE);
    if (ret) {
      ELOG_V("wv_request_change_station failed");
      goto out;
    }
  } else if (old_state == IEEE80211_STA_ASSOC && new_state == IEEE80211_STA_AUTH) {
    ret = wv_request_sta_disconnect_ap(df_user, sta);
    if (ret) {
      ELOG_V("wv_request_sta_disconnect_ap failed");
      goto out;
    }
  } else if (old_state == IEEE80211_STA_AUTH && new_state == IEEE80211_STA_NONE) {
  } else if (old_state == IEEE80211_STA_NONE && new_state == IEEE80211_STA_NOTEXIST) {
  } else {
    return -EIO;
  }

  wv_iface_inf->current_state = new_state;

out:
  return _mtlk_df_mtlk_to_linux_error_code(ret);
}

static int _wv_ieee80211_op_set_key (struct ieee80211_hw *hw,
    enum set_key_cmd cmd,
    struct ieee80211_vif *vif,
    struct ieee80211_sta *sta,
    struct ieee80211_key_conf *key)
{
  struct wv_vif_priv *wv_iface_inf = (struct wv_vif_priv *)vif->drv_priv;
  mtlk_df_user_t *df_user = wv_iface_inf->df_user;
  BOOL pairwise = key->flags & IEEE80211_KEY_FLAG_PAIRWISE;
  mtlk_clpb_t *clpb = NULL;
  mtlk_core_ui_encext_cfg_t encext_cfg;
  int ret = 0;
  mtlk_vap_handle_t vap_handle;
  mtlk_core_t *core;
  struct ieee80211_key_seq seq;
  u32 iv32;
  u16 iv16;

  if (df_user == NULL) {
    ELOG_V("df_user is NULL");
    return -EPERM;
  }
  vap_handle = mtlk_df_get_vap_handle(mtlk_df_user_get_df(df_user));
  core = mtlk_vap_get_core(vap_handle);
  if (mtlk_core_rcvry_is_running(core) ||
      wave_rcvry_mac_fatal_pending_get(mtlk_vap_get_hw(core->vap_handle))) {
    ELOG_V("Recovery is running, key should not be set in FW");
    return _mtlk_df_mtlk_to_linux_error_code(MTLK_ERR_OK);
  }
  ieee80211_get_key_rx_seq(key, 0, &seq);

  ieee80211_get_key_rx_seq(key, 0, &seq);

  switch (cmd) {
  case SET_KEY:
    ILOG0_V("SET_KEY");
    if (key->keyidx >= MIB_WEP_N_DEFAULT_KEYS) {
      ILOG2_D("Invalid key index %d ignored", key->keyidx);
      ret = MTLK_ERR_OK;
      goto out;
    }

    memset(&encext_cfg, 0, sizeof(encext_cfg));
    key->hw_key_idx = key->keyidx;

    ILOG2_DDDDDDD("cipher = %d, icv_len = %d, iv_len = %d, hw_key_idx = %d, flags = %d, keyidx = %d, keylen = %d",
        key->cipher, key->icv_len, key->iv_len, key->hw_key_idx,
        key->flags, key->keyidx, key->keylen);

    /* Validate pairwise flag */
    if (!sta) {
      if (pairwise) {
        WLOG_V("MAC_ADDR is not defined and pairwise is set");
        ret = MTLK_ERR_PARAMS;
        goto out;
      }

      ILOG2_Y("mac_addr = %Y", _bcast_addr.au8Addr);
      encext_cfg.sta_addr = _bcast_addr;
    } else {
      ILOG2_Y("mac_addr = %Y", sta->addr);
      mtlk_osal_copy_eth_addresses(encext_cfg.sta_addr.au8Addr, sta->addr);
    }

    if (!wv_ieee80211_supported_cipher_suite(hw->wiphy, key->cipher)) {
      ELOG_D("Cipher 0x%08X is not supported on this platform", key->cipher);
      ret = MTLK_ERR_NOT_SUPPORTED;
      goto out;
    }

    switch (key->cipher) {
    case WLAN_CIPHER_SUITE_WEP40:
    case WLAN_CIPHER_SUITE_WEP104:
      /* Validate WEP key */
      if (mtlk_df_ui_validate_wep_key(key->key, key->keylen)
          != MTLK_ERR_OK) {
        ELOG_V("Invalid WEP key");
        ret = MTLK_ERR_PARAMS;
        goto out;
      }

      /* Validate key index */
      if (key->keyidx >= MIB_WEP_N_DEFAULT_KEYS) {
        ELOG_D("Invalid WEP key index %d", key->keyidx);
        ret = MTLK_ERR_PARAMS;
        goto out;
      }

      encext_cfg.alg_type = IW_ENCODE_ALG_WEP;
      break;

    case WLAN_CIPHER_SUITE_TKIP:
      /* Validate key index */
      if ((key->keyidx > 2) || (pairwise & (key->keyidx != 0))) {
        ELOG_D("Invalid TKIP key index %d", key->keyidx);
        ret = MTLK_ERR_PARAMS;
        goto out;
      }
      iv16 = seq.tkip.iv16;
      iv32 = seq.tkip.iv32;
      encext_cfg.rx_seq[0] = iv16 & 0xff;
      encext_cfg.rx_seq[1] = (iv16 >> 8) & 0xff;
      encext_cfg.rx_seq[2] = iv32 & 0xff;
      encext_cfg.rx_seq[3] = (iv32 >> 8) & 0xff;
      encext_cfg.rx_seq[4] = (iv32 >> 16) & 0xff;
      encext_cfg.rx_seq[5] = (iv32 >> 24) & 0xff;

      iv16 = seq.tkip.iv16;
      iv32 = seq.tkip.iv32;
      encext_cfg.rx_seq[0] = iv16 & 0xff;
      encext_cfg.rx_seq[1] = (iv16 >> 8) & 0xff;
      encext_cfg.rx_seq[2] = iv32 & 0xff;
      encext_cfg.rx_seq[3] = (iv32 >> 8) & 0xff;
      encext_cfg.rx_seq[4] = (iv32 >> 16) & 0xff;
      encext_cfg.rx_seq[5] = (iv32 >> 24) & 0xff;

      encext_cfg.alg_type = IW_ENCODE_ALG_TKIP;
      break;

    case WLAN_CIPHER_SUITE_CCMP:
      /* Validate key index */
      if ((key->keyidx > 2) || (pairwise & (key->keyidx != 0))) {
        ELOG_D("Invalid CCMP key index %d", key->keyidx);
        ret = MTLK_ERR_PARAMS;
        goto out;
      }
      encext_cfg.rx_seq[0] = seq.ccmp.pn[5];
      encext_cfg.rx_seq[1] = seq.ccmp.pn[4];
      encext_cfg.rx_seq[2] = seq.ccmp.pn[3];
      encext_cfg.rx_seq[3] = seq.ccmp.pn[2];
      encext_cfg.rx_seq[4] = seq.ccmp.pn[1];
      encext_cfg.rx_seq[5] = seq.ccmp.pn[0];

      encext_cfg.rx_seq[0] = seq.ccmp.pn[5];
      encext_cfg.rx_seq[1] = seq.ccmp.pn[4];
      encext_cfg.rx_seq[2] = seq.ccmp.pn[3];
      encext_cfg.rx_seq[3] = seq.ccmp.pn[2];
      encext_cfg.rx_seq[4] = seq.ccmp.pn[1];
      encext_cfg.rx_seq[5] = seq.ccmp.pn[0];

      encext_cfg.alg_type = IW_ENCODE_ALG_CCMP;
      break;

    case WLAN_CIPHER_SUITE_AES_CMAC:
      /* Validate key index */
      if ((key->keyidx > 2) || (pairwise & (key->keyidx != 0))) {
        ELOG_D("Invalid AES_CMAC key index %d", key->keyidx);
        ret = MTLK_ERR_PARAMS;
        goto out;
      }
      encext_cfg.rx_seq[0] = seq.aes_cmac.pn[5];
      encext_cfg.rx_seq[1] = seq.aes_cmac.pn[4];
      encext_cfg.rx_seq[2] = seq.aes_cmac.pn[3];
      encext_cfg.rx_seq[3] = seq.aes_cmac.pn[2];
      encext_cfg.rx_seq[4] = seq.aes_cmac.pn[1];
      encext_cfg.rx_seq[5] = seq.aes_cmac.pn[0];

      encext_cfg.rx_seq[0] = seq.aes_cmac.pn[5];
      encext_cfg.rx_seq[1] = seq.aes_cmac.pn[4];
      encext_cfg.rx_seq[2] = seq.aes_cmac.pn[3];
      encext_cfg.rx_seq[3] = seq.aes_cmac.pn[2];
      encext_cfg.rx_seq[4] = seq.aes_cmac.pn[1];
      encext_cfg.rx_seq[5] = seq.aes_cmac.pn[0];

      encext_cfg.alg_type = IW_ENCODE_ALG_AES_CMAC;
      break;

    case WLAN_CIPHER_SUITE_GCMP:
      /* Validate key index */
      if ((key->keyidx > 2) || (pairwise & (key->keyidx !=0))) {
        ELOG_D("Invalid GCMP key index %d", key->keyidx);
        ret = MTLK_ERR_PARAMS;
        goto out;
      }
      encext_cfg.rx_seq[0] = seq.gcmp.pn[5];
      encext_cfg.rx_seq[1] = seq.gcmp.pn[4];
      encext_cfg.rx_seq[2] = seq.gcmp.pn[3];
      encext_cfg.rx_seq[3] = seq.gcmp.pn[2];
      encext_cfg.rx_seq[4] = seq.gcmp.pn[1];
      encext_cfg.rx_seq[5] = seq.gcmp.pn[0];

      encext_cfg.alg_type = IW_ENCODE_ALG_GCMP;
      break;

    case WLAN_CIPHER_SUITE_GCMP_256:
      /* Validate key index */
      if ((key->keyidx > 2) || (pairwise & (key->keyidx !=0))) {
        ELOG_D("Invalid GCMP key index %d", key->keyidx);
        ret = MTLK_ERR_PARAMS;
        goto out;
      }
      encext_cfg.rx_seq[0] = seq.gcmp.pn[5];
      encext_cfg.rx_seq[1] = seq.gcmp.pn[4];
      encext_cfg.rx_seq[2] = seq.gcmp.pn[3];
      encext_cfg.rx_seq[3] = seq.gcmp.pn[2];
      encext_cfg.rx_seq[4] = seq.gcmp.pn[1];
      encext_cfg.rx_seq[5] = seq.gcmp.pn[0];

      encext_cfg.alg_type = IW_ENCODE_ALG_GCMP_256;
      break;

    default:
      ret = MTLK_ERR_PARAMS;
      goto out;
    }

    /* Validate and set the key length */
    if (((UMI_RSN_TK1_LEN + UMI_RSN_TK2_LEN) < key->keylen)
        || (key->keylen == 0)) {
      WLOG_D("Invalid key length %u", key->keylen);
      ret = MTLK_ERR_PARAMS;
      goto out;
    }

    encext_cfg.key_len = key->keylen;
    encext_cfg.key_idx = key->keyidx;
    wave_memcpy(encext_cfg.key, sizeof(encext_cfg.key), key->key, key->keylen);

    ret = _mtlk_df_user_invoke_core(mtlk_df_user_get_df(df_user),
        WAVE_CORE_REQ_SET_ENCEXT_CFG, &clpb, (char*) &encext_cfg,
        sizeof(encext_cfg));
    ret = _mtlk_df_user_process_core_retval_void(ret, clpb,
        WAVE_CORE_REQ_SET_ENCEXT_CFG, TRUE);

    /* when using wep this callback is called only once
     * because it's the same key for unicast and broadcast.
     * but we need to notify it twice to the FW, with broadcast mac address
     * and the peer ap mac address.
     */
    if ((ret == MTLK_ERR_OK) && (encext_cfg.alg_type == IW_ENCODE_ALG_WEP)) {
      ieee_addr_set(&encext_cfg.sta_addr, wv_iface_inf->peer_ap_addr);
      ret = _mtlk_df_user_invoke_core(mtlk_df_user_get_df(df_user),
          WAVE_CORE_REQ_SET_ENCEXT_CFG, &clpb, (char*) &encext_cfg,
          sizeof(encext_cfg));
      ret = _mtlk_df_user_process_core_retval_void(ret, clpb,
          WAVE_CORE_REQ_SET_ENCEXT_CFG, TRUE);
    }

    break;

  case DISABLE_KEY:
    /* FW doesn't support delete key */
    ILOG2_V("DISABLE_KEY");
    break;

  default:
    ELOG_D("Unsupported key cmd %d", cmd);
    ret = MTLK_ERR_PARAMS;
    goto out;
  }

out:
  return ret;
}

static void _wv_ieee80211_op_set_default_unicast_key (struct ieee80211_hw *hw, struct ieee80211_vif *vif, int idx)
{
  int res = MTLK_ERR_PARAMS;
  struct wv_vif_priv *wv_iface_inf = (struct wv_vif_priv *)vif->drv_priv;
  mtlk_df_user_t *df_user = wv_iface_inf->df_user;
  mtlk_clpb_t *clpb = NULL;
  mtlk_core_ui_default_key_cfg_t default_key_cfg;
  mtlk_vap_handle_t vap_handle;
  mtlk_core_t *core;

  ILOG1_D("_wv_ieee80211_op_set_default_unicast_key, key index:%d", idx);
  if (df_user == NULL) {
    ELOG_V("df_user is NULL");
    return;
  }
  vap_handle = mtlk_df_get_vap_handle(mtlk_df_user_get_df(df_user));
  core = mtlk_vap_get_core(vap_handle);

  if (wait_rcvry_completion_if_needed(core) != MTLK_ERR_OK){
    ELOG_V("Recovery is running, can't set default unicast key");
    return;
  }

  /*check default key index */
  if (idx >= MIB_WEP_N_DEFAULT_KEYS) {
    ELOG_D("Invalid WEP key index %d", idx);
    return;
  }

  memset(&default_key_cfg, 0, sizeof(default_key_cfg));
  default_key_cfg.sta_addr = _bcast_addr;
  default_key_cfg.key_idx = idx;

  res = _mtlk_df_user_invoke_core(mtlk_df_user_get_df(df_user),
      WAVE_CORE_REQ_SET_DEFAULT_KEY_CFG, &clpb,
      (char*)&default_key_cfg, sizeof(default_key_cfg));
  res = _mtlk_df_user_process_core_retval_void(res, clpb,
      WAVE_CORE_REQ_SET_DEFAULT_KEY_CFG, TRUE);
}


static void _wv_ieee80211_op_get_key_seq (struct ieee80211_hw *hw,
    struct ieee80211_key_conf *key,
    struct ieee80211_key_seq *seq)
{
  ILOG0_V("TODO: mac80211 callback _wv_ieee80211_op_get_key_seq");
}

static int _wv_ieee80211_op_set_frag_threshold (struct ieee80211_hw *hw, u32 value)
{
  ILOG0_V("TODO: mac80211 callback _wv_ieee80211_op_set_frag_threshold");
  return LINUX_ERR_OK;
}

static int _wv_ieee80211_op_conf_tx (struct ieee80211_hw *hw,
    struct ieee80211_vif *vif, u16 ac,
    const struct ieee80211_tx_queue_params *params)
{
  struct wv_vif_priv *wv_iface_inf = (struct wv_vif_priv *)vif->drv_priv;
  int res = MTLK_ERR_OK;
  mtlk_df_user_t *df_user = wv_iface_inf->df_user;
  mtlk_vap_handle_t vap_handle;
  mtlk_pdb_t *param_db_core;
  struct mtlk_txq_params tp;

  if (df_user == NULL) {
    ELOG_V("df_user is NULL");
    return -EPERM;
  }
  vap_handle = mtlk_df_get_vap_handle(mtlk_df_user_get_df(df_user));
  param_db_core = mtlk_vap_get_param_db(vap_handle);
  tp.txop = params->txop;
  tp.cwmin = params->cw_min;
  tp.cwmax = params->cw_max;
  tp.aifs = params->aifs;
  tp.acm_flag = params->acm;
  tp.ac = nlac2mtlkac(ac);

  ILOG2_DDD("CID-%04x: Saving WMM params for VapID %u queue %u",
            mtlk_vap_get_oid(vap_handle), wv_iface_inf->vap_index, tp.ac);
  res = wave_pdb_set_binary(param_db_core, PARAM_DB_CORE_WMM_PARAMS_BE + tp.ac, &tp, sizeof(tp));

  return _mtlk_df_mtlk_to_linux_error_code(res);
}

static int _wv_ieee80211_get_connection_alive(struct ieee80211_hw *hw,
    struct ieee80211_vif *vif)
{
  struct wv_vif_priv *wv_iface_inf = (struct wv_vif_priv *)vif->drv_priv;
  mtlk_df_user_t *df_user = wv_iface_inf->df_user;
  mtlk_vap_handle_t vap_handle;
  mtlk_core_t *core;
  int res = FALSE;
  unsigned long total_rx_and_tx_uni_packets;
  mtlk_mhi_stats_vap_t *mhi_vap_stats;

  ILOG2_V("wv_ieee80211_get_connection_alive");
  ILOG2_Y("MAC = %Y", wv_iface_inf->peer_ap_addr);

  if (!mtlk_osal_is_valid_ether_addr(wv_iface_inf->peer_ap_addr)) {
    ELOG_Y("Invalid MAC address: %Y", wv_iface_inf->peer_ap_addr);
    return FALSE;
  }

  if (df_user == NULL) {
    ELOG_V("df_user is NULL");
    return FALSE;
  }
  vap_handle = mtlk_df_get_vap_handle(mtlk_df_user_get_df(df_user));
  core = mtlk_vap_get_core(vap_handle);
  mhi_vap_stats = &core->mhi_vap_stat;

  total_rx_and_tx_uni_packets = 0;
#ifdef MTLK_LEGACY_STATISTICS
  total_rx_and_tx_uni_packets += mtlk_core_get_vap_stat(core, STAT_VAP_TX_UNICAST_FRAMES);
  total_rx_and_tx_uni_packets += mtlk_core_get_vap_stat(core, STAT_VAP_RX_UNICAST_FRAMES);
#else
  total_rx_and_tx_uni_packets += mhi_vap_stats->stats.txInUnicastHd;
  total_rx_and_tx_uni_packets += mhi_vap_stats->stats.rxOutUnicastHd;
#endif

  if (total_rx_and_tx_uni_packets - wv_iface_inf->ndp_counter > wv_iface_inf->latest_rx_and_tx_packets) {
    res = TRUE;
  }

  ILOG1_DDDDD("VapID=%u, total_rx_and_tx_unicast_packets=%u, latest_rx_and_tx_packets=%u, ndp_counter=%d, res=%d",
              wv_iface_inf->vap_index, total_rx_and_tx_uni_packets, wv_iface_inf->latest_rx_and_tx_packets, wv_iface_inf->ndp_counter, res);
  wv_iface_inf->latest_rx_and_tx_packets = total_rx_and_tx_uni_packets;
  wv_iface_inf->ndp_counter = 0;

  return res;
}

static bool wv_ieee80211_is_all_iface_idle(struct ieee80211_hw *hw)
{
  wv_mac80211_t *mac80211 = __wv_ieee80211_hw_get_mac80211(hw);

  if (mtlk_vap_manager_has_active_ap(wave_radio_vap_manager_get(__wv_mac80211_radio_get(mac80211))))
    return FALSE;

  return TRUE;
}

static void _wv_ieee80211_op_bss_info_changed (struct ieee80211_hw *hw,
    struct ieee80211_vif *vif, struct ieee80211_bss_conf *info, u32 changed)
{
  struct wv_vif_priv *wv_iface_inf = (struct wv_vif_priv *)vif->drv_priv;
  mtlk_df_user_t *df_user = wv_iface_inf->df_user;
  int res;
  mtlk_vap_handle_t vap_handle;
  mtlk_core_t *core;
  wv_mac80211_t *mac80211;
  mtlk_df_t *master_df;
  struct mtlk_sta_bss_change_parameters bss_change_params;
  mtlk_clpb_t *clpb = NULL;

  ILOG0_SD("Setting BSS info for %s. changed = 0x%x",
      ieee80211_vif_to_name(vif), changed);

  mac80211 = __wv_ieee80211_hw_get_mac80211(hw);
  WAVE_WV_CHECK_PTR_VOID(mac80211);

  master_df = _wv_mac80211_master_df_get(mac80211);
  WAVE_WV_CHECK_PTR_VOID(master_df);

  vap_handle = mtlk_df_get_vap_handle(mtlk_df_user_get_df(df_user));
  core = mtlk_vap_get_core(vap_handle);
  if (wait_rcvry_completion_if_needed(core) != MTLK_ERR_OK){
    ELOG_V("Recovery is running, can't change bss info");
    return;
  }

  memset(&bss_change_params, 0, sizeof(bss_change_params));
  bss_change_params.vif_name = ieee80211_vif_to_name(vif);
  bss_change_params.bands = hw->wiphy->bands;
  bss_change_params.core = core;
  bss_change_params.info = info;
  bss_change_params.changed = changed;
  bss_change_params.vap_index = wv_iface_inf->vap_index;

  /* Keep trying in case scan is in progress for the current radio */
  {
    int retry_counter = 0;

    do {
      res = _mtlk_df_user_invoke_core(master_df,
                WAVE_RADIO_REQ_STA_CHANGE_BSS, &clpb, &bss_change_params, sizeof(bss_change_params));
      res = _mtlk_df_user_process_core_retval(res, clpb,
                WAVE_RADIO_REQ_STA_CHANGE_BSS, TRUE);

      if (MTLK_ERR_RETRY != res) break;
      mtlk_osal_msleep(50);
      retry_counter++;
    } while ((MTLK_ERR_RETRY == res) && (retry_counter < MAX_SCAN_WAIT_RETRIES));

    if (retry_counter > 0)
      ILOG0_SD("%s: Scan waited, number of retries %d", ieee80211_vif_to_name(vif), retry_counter);
  }
}

static void _wv_ieee80211_op_sw_scan_complete(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{
  wv_mac80211_t         *mac80211;
  struct scan_support   *ss;
  struct mtlk_chan_def  *ccd;
  mtlk_df_t             *master_df;
  mtlk_core_t           *master_core;
  mtlk_clpb_t           *clpb = NULL;
  struct set_chan_param_data cpd;
  struct cfg80211_chan_def chandef;
  struct mtlk_chan_def original_cd;
  int res;

  ILOG0_V("Software scan complete");

  mac80211 = __wv_ieee80211_hw_get_mac80211(hw);
  WAVE_WV_CHECK_PTR_VOID(mac80211);

  ccd = __wv_mac80211_chandef_get(mac80211);
  WAVE_WV_CHECK_PTR_VOID(ccd);

  ss  = __wv_mac80211_scan_support_get(mac80211);
  WAVE_WV_CHECK_PTR_VOID(ss);


  ss->flags &= ~SDF_MAC_RUNNING;
  mtlk_osal_event_set(&ss->scan_end_event);

  master_df = _wv_mac80211_master_df_get(mac80211);
  WAVE_WV_CHECK_PTR_VOID(master_df);

  master_core = mtlk_vap_get_core(mtlk_df_get_vap_handle(master_df));
  if (wait_rcvry_completion_if_needed(master_core) != MTLK_ERR_OK){
    ELOG_V("Recovery is running");
    return;
  }

  /* Set channel with type normal to finish scan on fw */
  ILOG0_V("Set channel to finish scan on fw");
  memset(&cpd, 0, sizeof(cpd));
  cpd.chandef = *ccd; /* struct copy */
  cpd.ndev = mtlk_df_user_get_ndev(mtlk_df_get_user(master_df));;
  cpd.vap_id = mtlk_vap_get_id(mtlk_df_get_vap_handle(master_df));

  original_cd = *ccd;
  wdev_lock(cpd.ndev->ieee80211_ptr);
  res = _mtlk_df_user_invoke_core(master_df,
      WAVE_RADIO_REQ_SET_CHAN, &clpb, &cpd, sizeof(cpd));
  res = _mtlk_df_user_process_core_retval(res, clpb,
      WAVE_RADIO_REQ_SET_CHAN, TRUE);
  wdev_unlock(cpd.ndev->ieee80211_ptr);
  if (res != MTLK_ERR_OK)
    ELOG_V("Error setting channel");
  else if (cpd.switch_type != ST_SCAN) {
    chandef.chan = ieee80211_get_channel(hw->wiphy, cpd.chandef.chan.center_freq);
    chandef.width = mtlkcw2nlcw(cpd.chandef.width, cpd.chandef.is_noht);
    chandef.center_freq1 = cpd.chandef.center_freq1;
    chandef.center_freq2 = cpd.chandef.center_freq2;
    wv_ieee80211_ch_switch_notify(mac80211, &chandef);
    wave_radio_ch_switch_event(hw->wiphy, master_core, &original_cd);
  }
}

static int _wv_ieee80211_op_hw_scan (struct ieee80211_hw *hw, struct ieee80211_vif *vif,
    struct ieee80211_scan_request *scan_req)
{
  mtlk_clpb_t *clpb = NULL;
  wv_mac80211_t *mac80211;
  struct wv_vif_priv *wv_iface_inf;
  struct cfg80211_scan_request *req = &scan_req->req;
  mtlk_vap_handle_t  master_vap_handle;
  mtlk_core_t       *master_core;
  mtlk_df_t         *master_df;
  struct mtlk_scan_request *sr;
  int res = MTLK_ERR_OK;
  int i;

  ILOG1_SSDPPP("%s: Invoked from %s (%i): hw=%p, vif=%p, req=%p",
      ieee80211_vif_to_name(vif), current->comm, current->pid, hw, vif, req);

  wv_iface_inf = (struct wv_vif_priv*)vif->drv_priv;
  WAVE_WV_CHECK_PTR_RES(wv_iface_inf);

  mac80211 = __wv_ieee80211_hw_get_mac80211(hw);
  WAVE_WV_CHECK_PTR_RES(mac80211);

  master_df = _wv_mac80211_master_df_get(mac80211);
  WAVE_WV_CHECK_PTR_RES(master_df);

  master_vap_handle = mtlk_df_get_vap_handle(master_df);
  master_core = mtlk_vap_get_core(master_vap_handle);

  if (wait_rcvry_completion_if_needed(master_core) != MTLK_ERR_OK){
    ELOG_V("Recovery is running, can't perform HW scan");
    return -EBUSY;
  }

  if (req->n_channels > NUM_TOTAL_CHANS
      || req->n_ssids > MAX_SCAN_SSIDS
      || req->ie_len > MAX_SCAN_IE_LEN) {
    ELOG_V("Maximum number of channels, ssids or extra IE length exceeded");
    return _mtlk_df_mtlk_to_linux_error_code(MTLK_ERR_VALUE);
  }

  /* scan request is too big to put on the stack */
  if (!(sr = mtlk_osal_mem_alloc(sizeof(*sr), MTLK_MEM_TAG_CFG80211))) {
    ELOG_V("_wv_ieee80211_op_hw_scan: can't allocate memory");
    return _mtlk_df_mtlk_to_linux_error_code(MTLK_ERR_NO_MEM);
  }

  memset(sr, 0, sizeof(*sr));
  sr->saved_request = req;
  sr->type = MTLK_SCAN_STA;
  sr->requester_vap_index = wv_iface_inf->vap_index;
  sr->wiphy = hw->wiphy;
  sr->n_channels = req->n_channels;
  core_cfg_country_code_get(master_core, &sr->country_code);

  for (i = 0; i < sr->n_channels; i++) {
    struct mtlk_channel *mc = &sr->channels[i];
    struct ieee80211_channel *ic = req->channels[i];

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

  sr->flags = req->flags;

  for (i = 0; i < NUM_SUPPORTED_BANDS; i++)
    sr->rates[nlband2mtlkband(i)] = req->rates[i];

  sr->n_ssids = req->n_ssids;

  MTLK_STATIC_ASSERT(MIB_ESSID_LENGTH >= IEEE80211_MAX_SSID_LEN);

  for (i = 0; i < sr->n_ssids; i++) {
    /* in case SSID-s with '\0'-s in the middle are acceptable, we'll need to keep the len, too */
    wave_strncopy(sr->ssids[i], req->ssids[i].ssid, sizeof(sr->ssids[i]), req->ssids[i].ssid_len);
  }

  sr->ie_len = req->ie_len;
  wave_memcpy(sr->ie, sizeof(sr->ie), req->ie, sr->ie_len);

  /* Keep trying in case scan is in progress for the current radio */
  {
    int retry_counter = 0;

    do {
      /* any checks for preexisting scans done later to minimize locking */
      res = _mtlk_df_user_invoke_core(master_df,
          WAVE_RADIO_REQ_DO_SCAN, &clpb, sr, sizeof(*sr));
      res = _mtlk_df_user_process_core_retval(res, clpb,
          WAVE_RADIO_REQ_DO_SCAN, TRUE);

      if (MTLK_ERR_RETRY != res) break;
      mtlk_osal_msleep(50);
      retry_counter++;
    } while ((MTLK_ERR_RETRY == res) && (retry_counter < MAX_SCAN_WAIT_RETRIES));

    if (retry_counter > 0)
      ILOG0_SD("%s: Scan waited, number of retries %d", ieee80211_vif_to_name(vif), retry_counter);

  }

  /* no waiting for the end of the scan, just return and let the kernel report the scan as started */
  mtlk_osal_mem_free(sr);

  ILOG2_SD("%s: return with %d",
      ieee80211_vif_to_name(vif), _mtlk_df_mtlk_to_linux_error_code(res));
  return _mtlk_df_mtlk_to_linux_error_code(res);
}

/* Setting up passive scan flag on radar channels */
static void wv_ieee80211_mark_ch_for_passive_scan(struct wiphy *wiphy)
{
  struct ieee80211_supported_band *sband;
  struct ieee80211_channel *ch;
  unsigned int i;

  if (!wiphy->bands[NL80211_BAND_5GHZ])
    return;

  sband = wiphy->bands[NL80211_BAND_5GHZ];

  for (i = 0; i < sband->n_channels; ++i) {
    ch = &sband->channels[i];
    if (!wv_ieee80211_is_radar_chan(ch) ||
        (ch->flags & IEEE80211_CHAN_DISABLED))
      continue;

    ch->flags |= IEEE80211_CHAN_NO_IR;
  }
}


static void wv_ieee80211_reg_notifier(struct wiphy *wiphy, struct regulatory_request *request)
{
  struct ieee80211_hw *hw = wiphy_to_ieee80211_hw(wiphy);
  mtlk_df_t           *master_df = __wv_ieee80211_hw_master_df_get(hw);
  mtlk_core_t         *master_core;

  WAVE_WV_CHECK_PTR_VOID(master_df);
  master_core = mtlk_vap_get_core(mtlk_df_get_vap_handle(master_df));

  /* set new country code by chars array (NOT NUL-terminated) */
  core_cfg_country_code_set_by_str(master_core, request->alpha2, sizeof(request->alpha2));
  /* Result already logged */

  /* Setting up passive scan on radar channels */
  wv_ieee80211_mark_ch_for_passive_scan(wiphy);
}

void __MTLK_IFUNC
wv_ieee80211_unregister (wv_mac80211_t *mac80211)
{
  MTLK_ASSERT(mac80211 != NULL);
  if (mac80211 == NULL)
    return;

  if (mac80211->registered) {
    ILOG0_V("Unregistering mac80211 HW");
    ieee80211_unregister_hw(mac80211->mac80211_hw);
  }
  mac80211->registered = FALSE;
}

#define VENDOR_CMD(cmd, func) { \
  .info  = {OUI_LTQ, (cmd)},    \
  .flags = (WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV | WIPHY_VENDOR_CMD_NEED_RUNNING), \
  .doit  = (func)               \
}

static struct
wiphy_vendor_command vendor_commands[] = {
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_TX_EAPOL,            wave_cfg80211_vendor_hapd_send_eapol),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_RADIO_INFO,      wave_cfg80211_vendor_hapd_get_radio_info),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_UNCONNECTED_STA, wave_cfg80211_vendor_hapd_get_unconnected_sta),
};

static void
register_vendor_commands(struct wiphy *wiphy)
{
  wiphy->vendor_commands = vendor_commands;
  wiphy->n_vendor_commands = ARRAY_SIZE(vendor_commands);
}

#define VENDOR_EVT(cmd) {OUI_LTQ, (cmd)}

/* There is a check in kernel that event index is less than number of events,
 * therefore we need to add unused events here as well. */
static struct
nl80211_vendor_cmd_info vendor_events[] = {
  VENDOR_EVT(LTQ_NL80211_VENDOR_EVENT_RX_EAPOL),
  VENDOR_EVT(LTQ_NL80211_VENDOR_EVENT_FLUSH_STATIONS),
  VENDOR_EVT(LTQ_NL80211_VENDOR_EVENT_CHAN_DATA),
  VENDOR_EVT(LTQ_NL80211_VENDOR_EVENT_UNCONNECTED_STA),
  VENDOR_EVT(LTQ_NL80211_VENDOR_EVENT_WDS_CONNECT),
  VENDOR_EVT(LTQ_NL80211_VENDOR_EVENT_WDS_DISCONNECT),
  VENDOR_EVT(LTQ_NL80211_VENDOR_EVENT_CSA_RECEIVED),
  VENDOR_EVT(LTQ_NL80211_VENDOR_EVENT_SOFTBLOCK_DROP)
};

static void
register_vendor_events(struct wiphy *wiphy)
{
  wiphy->vendor_events = vendor_events;
  wiphy->n_vendor_events = ARRAY_SIZE(vendor_events);
}


int __MTLK_IFUNC
wv_ieee80211_setup_register (struct device *dev, wv_mac80211_t *mac80211, mtlk_handle_t hw_handle)
{
  int ret;
  struct ieee80211_hw *hw = mac80211->mac80211_hw;
  u32 num_sts = WV_SUPP_NUM_STS; /* should get overwritten */

  if (mac80211->registered)
    return MTLK_ERR_OK;

  ILOG0_V("Registering mac80211 HW");

  /* Tell mac80211 our characteristics */
  ieee80211_hw_set(hw, SIGNAL_DBM);
  ieee80211_hw_set(hw, SPECTRUM_MGMT);
  ieee80211_hw_set(hw, REPORTS_TX_ACK_STATUS);
  ieee80211_hw_set(hw, MFP_CAPABLE);
  ieee80211_hw_set(hw, AMPDU_AGGREGATION);
  ieee80211_hw_set(hw, HAS_RATE_CONTROL);

  /* Driver's private station information */
  hw->sta_data_size = 0;

  /* driver's private per-interface information */
  hw->vif_data_size = sizeof (struct wv_vif_priv);

  hw->chanctx_data_size = 0; /* relevant for multichannel HW */

  hw->wiphy->interface_modes = BIT(NL80211_IFTYPE_STATION);

  hw->wiphy->mgmt_stypes = wv_mgmt_stypes;

  /* set WIPHY flags */
  hw->wiphy->flags |= (WIPHY_FLAG_HAS_CHANNEL_SWITCH);

  if (mtlk_mmb_drv_get_disable_11d_hint_param()) {
    hw->wiphy->flags |= WIPHY_FLAG_DISABLE_11D_HINT;
    ILOG0_V("11d hints are disabled");
  }

  /* set WIPHY features */
  hw->wiphy->features = (NL80211_FEATURE_SK_TX_STATUS |
                         NL80211_FEATURE_SCAN_FLUSH |
                         NL80211_FEATURE_SAE);

  hw->wiphy->max_scan_ssids = MAX_SCAN_SSIDS;
  hw->wiphy->max_sched_scan_ssids = MAX_SCAN_SSIDS;
  hw->wiphy->max_match_sets = MAX_MATCH_SETS;
  hw->wiphy->max_scan_ie_len = MAX_SCAN_IE_LEN;
  hw->wiphy->max_sched_scan_ie_len = MAX_SCAN_IE_LEN;
  hw->queues = MAC80211_HW_TX_QUEUES; /*number of available hardware transmit queues for data packets.*/

  hw->wiphy->iface_combinations = create_mac80211_if_comb(mac80211->radio);
  if(NULL == hw->wiphy->iface_combinations) {
    ELOG_V("Error creating mac80211 phy interface combinations");
    return MTLK_ERR_NO_MEM;
  }
  hw->wiphy->n_iface_combinations = MAC80211_STA_MAX_IF_COMB;


  hw_get_fw_version(hw_handle, hw->wiphy->fw_version,
      sizeof(hw->wiphy->fw_version));

  hw_get_hw_version(hw_handle, &hw->wiphy->hw_version);

  /* TX/RX available antennas: bitmap of antennas which are available to be configured as TX/RX antenna */
  wave_radio_ant_masks_num_sts_get(mac80211->radio,
                                   &hw->wiphy->available_antennas_tx, &hw->wiphy->available_antennas_rx, &num_sts);

  hw->wiphy->bands[NL80211_BAND_2GHZ] = NULL;
  hw->wiphy->bands[NL80211_BAND_5GHZ] = NULL;

  ret = wv_cfg80211_update_supported_bands(hw_handle, wave_radio_id_get(mac80211->radio),
                                           hw->wiphy, num_sts);
  if (ret != MTLK_ERR_OK)
    return ret;

  /* Setting up passive scan on radar channels */
  wv_ieee80211_mark_ch_for_passive_scan(hw->wiphy);

  hw->wiphy->signal_type = CFG80211_SIGNAL_TYPE_MBM;

  if(hw_get_gcmp_support(hw_handle)) {
    hw->wiphy->cipher_suites = _cipher_suites_gcmp;
    hw->wiphy->n_cipher_suites = ARRAY_SIZE(_cipher_suites_gcmp);
  } else {
    hw->wiphy->cipher_suites = _cipher_suites;
    hw->wiphy->n_cipher_suites = ARRAY_SIZE(_cipher_suites);
  }
  hw->wiphy->reg_notifier = wv_ieee80211_reg_notifier;

  hw->max_listen_interval = 0; /* Sta role interface does not enter Power Save mode */

  register_vendor_commands(hw->wiphy);
  register_vendor_events(hw->wiphy);

  /* Register hw with mac80211 kernel framework and add one default STA interface */
  ret = ieee80211_register_hw(hw);
  if (ret) {
    ELOG_D("ieee80211 HW registration failed. Error code: %d", ret);
    return MTLK_ERR_PARAMS;
  }

  mac80211->registered = TRUE;

  return MTLK_ERR_OK;
}

static void mac80211_if_limit_cleanup(struct ieee80211_iface_limit *if_limit)
{
  if (NULL != if_limit)
    mtlk_osal_mem_free(if_limit);
}

static void mac80211_if_comb_cleanup(
    struct ieee80211_iface_combination *if_comb)
{

  if (NULL != if_comb) {
    struct ieee80211_iface_limit *if_limit;
    if_limit = (struct ieee80211_iface_limit *) if_comb->limits;
    mac80211_if_limit_cleanup(if_limit);

    mtlk_osal_mem_free(if_comb);
  }
}

void wv_ieee80211_cleanup(wv_mac80211_t *mac80211)
{
  struct ieee80211_supported_band **bands = mac80211->mac80211_hw->wiphy->bands;
  struct ieee80211_iface_combination *if_comb;
  if_comb = (struct ieee80211_iface_combination*) mac80211->mac80211_hw->wiphy->iface_combinations;

  mac80211_if_comb_cleanup(if_comb);

  if (bands[NL80211_BAND_5GHZ]) {
    if (bands[NL80211_BAND_5GHZ]->channels)
      mtlk_osal_mem_free(bands[NL80211_BAND_5GHZ]->channels);

    mtlk_osal_mem_free(bands[NL80211_BAND_5GHZ]);
  }

  if (bands[NL80211_BAND_2GHZ]) {
    if (bands[NL80211_BAND_2GHZ]->channels)
      mtlk_osal_mem_free(bands[NL80211_BAND_2GHZ]->channels);

    mtlk_osal_mem_free(bands[NL80211_BAND_2GHZ]);
  }
}

static struct ieee80211_iface_limit *create_mac80211_if_limits (wave_radio_t *radio)
{
  struct ieee80211_iface_limit *if_limits;
  int size;

  size = MAC80211_STA_MAX_IF_LIMIT * sizeof(struct ieee80211_iface_limit);

  if_limits = mtlk_osal_mem_alloc(size, MTLK_MEM_TAG_CFG80211);
  if (NULL == if_limits) {
    ELOG_V("create_mac80211_if_limits: can't allocate memory");
    return NULL;
  }

  memset(if_limits, 0, size);

  /* Limitation on station mode VAPs */
  if_limits->max = wave_radio_max_vaps_get(radio);
  if_limits->types = BIT(NL80211_IFTYPE_STATION);

  return if_limits;
}

static struct ieee80211_iface_combination *create_mac80211_if_comb (wave_radio_t *radio)
{
  struct ieee80211_iface_combination *if_comb;
  struct ieee80211_iface_limit *if_limits;

  int size =
      MAC80211_STA_MAX_IF_COMB * sizeof(struct ieee80211_iface_combination);

  if_comb = mtlk_osal_mem_alloc(size, MTLK_MEM_TAG_CFG80211);
  if (NULL == if_comb) {
    ELOG_V("ieee80211_iface_combination: can't allocate memory");
    return NULL;
  }
  memset(if_comb, 0, size);

  if_limits = create_mac80211_if_limits(radio);
  if (NULL == if_limits) {
    mac80211_if_comb_cleanup(if_comb);
    return NULL;
  }

  if_comb->limits = if_limits;
  if_comb->n_limits = MAC80211_STA_MAX_IF_LIMIT; /*stations only*/
  if_comb->max_interfaces = wave_radio_max_vaps_get(radio);
  if_comb->num_different_channels = 1;
  if_comb->beacon_int_infra_match = 0;
  if_comb->radar_detect_widths = (1 << NL80211_CHAN_WIDTH_20_NOHT)
                               | (1 << NL80211_CHAN_WIDTH_20)
                               | (1 << NL80211_CHAN_WIDTH_40)
                               | (1 << NL80211_CHAN_WIDTH_80);

  return if_comb;
}

void __MTLK_IFUNC
wv_ieee80211_free (wv_mac80211_t *mac80211)
{
  MTLK_ASSERT(mac80211 != NULL);
  if (mac80211 == NULL) {
    ELOG_V("wv_ieee80211_free, mac80211 is NULL");
    return;
  }

  wv_ieee80211_cleanup(mac80211);

  if (!mac80211->registered) {
    ILOG0_V("Freeing mac80211 HW");
    ieee80211_free_hw(mac80211->mac80211_hw);
  }
}

static void
_wv_ieee80211_hw_rx_status (struct ieee80211_hw *mac80211_hw, struct ieee80211_rx_status *rx_status,
    int freq, int sig_dbm, uint16 rate_idx, u8 phy_mode, uint8 mtlk_pmf_flags)
{
  struct _wv_mac80211_t *mac80211 = __wv_ieee80211_hw_get_mac80211(mac80211_hw);
  struct mtlk_chan_def  *ccd;

  WAVE_WV_CHECK_PTR_VOID(mac80211);

  ccd = __wv_mac80211_chandef_get(mac80211);
  WAVE_WV_CHECK_PTR_VOID(ccd);

  if (freq <= 2484)
    rx_status->band = NL80211_BAND_2GHZ;
  else if (freq >= 4910 && freq <= 5835)
    rx_status->band = NL80211_BAND_5GHZ;
  else
    rx_status->band = NL80211_BAND_60GHZ;

  if (mtlk_pmf_flags & MTLK_MGMT_FRAME_DECRYPTED)
    rx_status->flag |= RX_FLAG_DECRYPTED;

  if (mtlk_pmf_flags & MTLK_MGMT_FRAME_IV_STRIPPED)
    rx_status->flag |= RX_FLAG_IV_STRIPPED;

  rx_status->signal = sig_dbm;
  rx_status->freq = freq;
  rx_status->rate_idx = rate_idx;

  switch (ccd->width) {
  case CW_20:
    break;
  case CW_40:
    rx_status->bw |= RATE_INFO_BW_40;
    break;
  case CW_80:
    rx_status->bw |= RATE_INFO_BW_80;
    break;
  case CW_160:
  case CW_80_80: /* 80+80 MHz rate removed from Linux starting from */
                 /* kernel 4.0, it is  treated the same as 160 MHz */
    rx_status->bw |= RATE_INFO_BW_160;
    break;
  }

  /* TODO IWLWAV: Needed? */
  switch(phy_mode) {
  case PHY_MODE_N:
    rx_status->encoding |= RX_ENC_HT;
    break;

  case PHY_MODE_AC:
    rx_status->encoding |= RX_ENC_VHT;
    break;

  default:
    break;
  };
}

void __MTLK_IFUNC
wv_ieee80211_mngmn_frame_rx(mtlk_core_t *nic, const u8 *data, int size,
    int freq, int sig_dbm, unsigned subtype, uint16 rate_idx, u8 phy_mode, uint8 pmf_flags)
{
  struct ieee80211_hw *mac80211_hw = wave_vap_ieee80211_hw_get(nic->vap_handle);
  struct ieee80211_rx_status *rx_status;
  struct sk_buff *skb;
  u8 *buf;

  skb = dev_alloc_skb(size);
  if (!skb) {
    ELOG_V("Couldn't allocate RX skb frame");
    return;
  }

  buf = skb_put(skb, size);
  wave_memcpy(buf, size, data, size);
  rx_status = IEEE80211_SKB_RXCB(skb);
  memset(rx_status, 0, sizeof(struct ieee80211_rx_status));
  _wv_ieee80211_hw_rx_status(mac80211_hw, rx_status, freq, sig_dbm, rate_idx, phy_mode, pmf_flags);

  ieee80211_rx_ni(mac80211_hw, skb);
}

void __MTLK_IFUNC
wv_ieee80211_tx_status(struct ieee80211_hw *hw,
    struct sk_buff *skb, uint8 status)
{
  struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);

  if ((status == TX_STATUS_SUCCESS) && !(info->flags & IEEE80211_TX_CTL_NO_ACK))
    info->flags |= IEEE80211_TX_STAT_ACK;

  ieee80211_tx_status(hw, skb);
}

void __MTLK_IFUNC
wave_vap_increment_ndp_counter (mtlk_vap_handle_t vap_handle)
{
  struct wv_vif_priv    *wv_iface_inf = _wave_vap_get_vif_priv(vap_handle);

  if (NULL != wv_iface_inf)
    wv_iface_inf->ndp_counter++;
}

struct ieee80211_hw * __MTLK_IFUNC
wv_ieee80211_hw_get (wv_mac80211_t *mac80211)
{
  MTLK_ASSERT(mac80211 != NULL);

  return mac80211->mac80211_hw;
}

static void _wv_ieee80211_op_sta_rc_update(struct ieee80211_hw *hw,
    struct ieee80211_vif *vif, struct ieee80211_sta *sta, u32 changed)
{
  struct wv_vif_priv *wv_iface_inf = (struct wv_vif_priv *)vif->drv_priv;
  mtlk_df_user_t *df_user = wv_iface_inf->df_user;
  mtlk_operating_mode_t operating_mode;

  ILOG0_SD("wv_ieee80211_sta_rc_update for %s. changed = 0x%x",
      ieee80211_vif_to_name(vif), changed);
  if (df_user == NULL) {
    ELOG_V("df_user is NULL");
    return;
  }

  if (changed & IEEE80211_RC_BW_CHANGED)
    ILOG0_D("IEEE80211_RC_BW_CHANGED, new BW = %d", sta->bandwidth);

  if (changed & IEEE80211_RC_NSS_CHANGED)
    ILOG0_D("IEEE80211_RC_NSS_CHANGED, new RX NSS value = %d", sta->rx_nss);

  operating_mode.station_id = wv_iface_inf->vif_sid;
  operating_mode.channel_width = sta->bandwidth;
  operating_mode.rx_nss = sta->rx_nss;

  _mtlk_df_user_invoke_core_async(mtlk_df_user_get_df(df_user),
        WAVE_CORE_REQ_SET_OPERATING_MODE, (char*) &operating_mode,
        sizeof(operating_mode), NULL, 0);

  return;

}

void __MTLK_IFUNC
wv_ieee80211_scan_completed(struct ieee80211_hw *hw, BOOL aborted)
{
  struct cfg80211_scan_info info = {
    .aborted = aborted,
  };
  MTLK_ASSERT(hw != NULL);
  ieee80211_scan_completed(hw, &info);
}

/* FW recovery - reset vif values to reflect updated FW status */
void __MTLK_IFUNC
wv_mac80211_recover_sta_vifs(wv_mac80211_t *mac80211)
{
  MTLK_ASSERT(NULL != mac80211);
  MTLK_ASSERT(NULL != mac80211->radio);

  if (__wv_mac80211_get_sta_vifs_exist(mac80211)) {
    unsigned vap_index, max_vaps_count;

    max_vaps_count = __wv_mac80211_max_vaps_count_get(mac80211);

    for (vap_index = 0; vap_index < max_vaps_count; vap_index++) {
      struct wv_vif_priv *curr_vif_priv = _wv_mac80211_get_vif_priv(mac80211, vap_index);

      if (curr_vif_priv != NULL){
        curr_vif_priv->is_set_sid = FALSE;
        curr_vif_priv->is_initialized = FALSE;
      }
    }
  }

  return;
}

void __MTLK_IFUNC
wv_ieee80211_sched_scan_results(struct ieee80211_hw *hw)
{
  MTLK_ASSERT(hw != NULL);
  ieee80211_sched_scan_results(hw);
}

static int wv_ieee80211_set_rts_threshold(struct ieee80211_hw *hw, u32 value)
{
  MTLK_ASSERT(hw != NULL);

  ILOG1_D("RTS Threshold set to %d. Ignoring, value is hard coded in FW", value);

  return 0;
}

const struct ieee80211_ops wv_ieee80211_ops = {
  .tx                       = _wv_ieee80211_op_tx,
  .start                    = _wv_ieee80211_op_start,
  .stop                     = _wv_ieee80211_op_stop,
  .config                   = _wv_ieee80211_op_config,
  .add_interface            = _wv_ieee80211_op_add_interface,
  .remove_interface         = _wv_ieee80211_op_remove_interface,
  .configure_filter         = _wv_ieee80211_op_configure_filter,
  .sta_state                = _wv_ieee80211_op_sta_state,
  .set_key                  = _wv_ieee80211_op_set_key,
  .set_default_unicast_key  = _wv_ieee80211_op_set_default_unicast_key,
  .get_key_seq = _wv_ieee80211_op_get_key_seq,
  .set_frag_threshold       = _wv_ieee80211_op_set_frag_threshold,
  .bss_info_changed         = _wv_ieee80211_op_bss_info_changed,
  .sta_rc_update            = _wv_ieee80211_op_sta_rc_update,
  .conf_tx                  = _wv_ieee80211_op_conf_tx,
  .channel_switch           = _wv_ieee80211_op_channel_switch,
  .hw_scan                  = _wv_ieee80211_op_hw_scan,
  .sw_scan_complete         = _wv_ieee80211_op_sw_scan_complete,
  .get_connection_alive     = _wv_ieee80211_get_connection_alive,
  .is_all_iface_idle        = wv_ieee80211_is_all_iface_idle,
  .set_rts_threshold = wv_ieee80211_set_rts_threshold,
};

wv_mac80211_t * __MTLK_IFUNC
wv_ieee80211_init (struct device *dev, void *radio_ctx, mtlk_handle_t hw_handle)
{
  struct ieee80211_hw *hw;
  wv_mac80211_t *mac80211 = NULL;
  wave_radio_t *radio = (wave_radio_t *)radio_ctx;

  /* Actual MAC address will be set in mtlk_mbss_send_vap_activate according
   * to the value in param_db for the specific VAP
   * Currently we do not know the VAP index.
   * This will only happen on wv_ieee80211_start
   */
  u8 addr[ETH_ALEN] = { 0 };

  MTLK_ASSERT(NULL != radio);
  if (radio == NULL) {
    return NULL;
  }

  hw = ieee80211_alloc_hw(sizeof(wv_mac80211_t), &wv_ieee80211_ops);
  if (!hw){
    ELOG_V("ieee80211_alloc_hw failed");
    return NULL;
  }

  /* retrieve mac80211 handle */
  mac80211 = hw->priv;

  MTLK_ASSERT(mac80211 != NULL);
  if (mac80211 == NULL) {
    ELOG_V("mac80211 phy initialization failed, error retrieving mac80211 from hw->priv");
    return NULL;
  }

  /* link mac80211 with radio module */
  mac80211->radio = radio;
  mac80211->mac80211_hw = hw;

  wave_radio_set_sta_vifs_exist(mac80211->radio, FALSE);

  SET_IEEE80211_DEV(hw, dev);
  SET_IEEE80211_PERM_ADDR (hw, addr);

  return mac80211;
}

int wv_mac80211_NDP_send_to_all_APs(wv_mac80211_t *mac80211,
  mtlk_nbuf_t *nbuf_ndp, BOOL power_mgmt_on, BOOL wait_for_ack)
{
  int res = MTLK_ERR_OK;
  unsigned max_vaps_count = 0;
  unsigned vap_index;
  frame_head_t *wifi_header;
  uint64 cookie;

  MTLK_ASSERT(NULL != mac80211);

  if (!__wv_mac80211_get_sta_vifs_exist(mac80211))
    return res;

  wifi_header = (frame_head_t *) nbuf_ndp->data;
  max_vaps_count = __wv_mac80211_max_vaps_count_get(mac80211);
  for (vap_index = 0; vap_index < max_vaps_count; vap_index++) {
    struct wv_vif_priv *curr_vif_priv = _wv_mac80211_get_vif_priv(mac80211, vap_index);
    mtlk_vap_handle_t curr_vap_handle;
    mtlk_core_t *curr_core;

    if (curr_vif_priv == NULL) {
      continue;
    }

    curr_vap_handle = mtlk_df_get_vap_handle(mtlk_df_user_get_df(curr_vif_priv->df_user));
    curr_core = mtlk_vap_get_core(curr_vap_handle);

    if (NET_STATE_CONNECTED != mtlk_core_get_net_state(curr_core)) {
      /* Core is not ready */
      continue;
    }

    if (IEEE80211_STA_AUTHORIZED != curr_vif_priv->current_state) {
      /* VSTA not connected to AP */
      continue;
    }

    ieee_addr_set(&wifi_header->dst_addr, curr_vif_priv->peer_ap_addr);
    ieee_addr_set(&wifi_header->src_addr, curr_vif_priv->vif->addr);
    ieee_addr_set(&wifi_header->bssid, curr_vif_priv->peer_ap_addr);

    if (wait_for_ack) {
      mtlk_osal_event_reset(&curr_core->ndp_acked);
      curr_core->waiting_for_ndp_ack = TRUE;
    }

    ILOG2_DY("CID-%04x: Sending NDP to %Y", mtlk_vap_get_oid(curr_vap_handle),
      wifi_header->dst_addr.au8Addr);
    res = mtlk_mmb_bss_mgmt_tx(curr_vap_handle, nbuf_ndp->data, sizeof(frame_head_t),
      0, FALSE, !wait_for_ack, FALSE /* unicast */ ,
      &cookie, PROCESS_NULL_DATA_PACKET, NULL,
      power_mgmt_on, MAC80211_WIDAN_NDP_TID);
    if (res != MTLK_ERR_OK) {
      ILOG1_DDS("CID-%04x: Send null data frame error: res=%d (%s)",
               mtlk_vap_get_oid(curr_vap_handle),
               res, mtlk_get_error_text(res));
      curr_core->waiting_for_ndp_ack = FALSE;
      return res;
    }
    if (wait_for_ack) {
      res = mtlk_osal_event_wait(&curr_core->ndp_acked,
        MAC80211_WIDAN_NDP_TIMEOUT);
      curr_core->waiting_for_ndp_ack = FALSE;
      if (res != MTLK_ERR_OK)
        break;
    }
  }
  return res;
}

static void wv_ieee80211_ch_switch_notify(wv_mac80211_t *mac80211, struct cfg80211_chan_def *chandef)
{
  unsigned max_vaps_count = 0;
  unsigned vap_index;
  struct net_device *ndev;
  int got_lock;

  if (!__wv_mac80211_get_sta_vifs_exist(mac80211))
    return;

  max_vaps_count = __wv_mac80211_max_vaps_count_get(mac80211);
  for (vap_index = 0; vap_index < max_vaps_count; vap_index++) {
    struct wv_vif_priv *wv_iface_inf = _wv_mac80211_get_vif_priv(mac80211, vap_index);

    if (wv_iface_inf == NULL) {
      continue;
    }

    ndev = mtlk_df_user_get_ndev(wv_iface_inf->df_user);
    /* Usually we shouldn't use the protected structure after taking mutex with
     * mutex_trylock, because of possible racing. In this case we don't
     * expect this scenario, because there are two cases how this function can
     * be invoked:
     * a) when processing netlink message,
     * b) from ieee80211_do_open.
     * In case 'a' netlink holds the lock during the whole processing of
     * message, so we are safe. In case 'b' no lock is taken, so we are taking
     * the lock in this function. We are not expecting that somebody else will
     * be modifying wdev while we are working with it, because open hasn't been
     * done yet.
     */
    got_lock = mutex_trylock(&ndev->ieee80211_ptr->mtx);
    cfg80211_ch_switch_notify(ndev, chandef);
    if (got_lock)
      wdev_unlock(ndev->ieee80211_ptr);
  }
}

static void wv_ieee80211_csa_received(struct wiphy *wiphy, void *data,
  int data_len)
{
  mtlk_nbuf_t *evt_nbuf;
  uint8 *cp;

  MTLK_ASSERT(NULL != wiphy);

  evt_nbuf = wv_ieee80211_vendor_event_alloc(wiphy, data_len,
    LTQ_NL80211_VENDOR_EVENT_CSA_RECEIVED);
  if (!evt_nbuf)
  {
    ELOG_D("Malloc event fail. data_len = %d", data_len);
    return;
  }

  cp = mtlk_df_nbuf_put(evt_nbuf, data_len);
  wave_memcpy(cp, data_len, data, data_len);
  wv_cfg80211_vendor_event(evt_nbuf);
}
