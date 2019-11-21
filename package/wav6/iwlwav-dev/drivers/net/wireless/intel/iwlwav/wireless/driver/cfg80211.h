/******************************************************************************

                               Copyright (c) 2012
                            Lantiq Deutschland GmbH

  For licensing information, see the file 'LICENSE' in the root folder of
  this software module.

******************************************************************************/
#ifndef _MTLK_CFG80211_H_
#define _MTLK_CFG80211_H_

#include <net/cfg80211.h>
#include "mtlk_coreui.h"
#include "wds.h"

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,10,0)
#error "minimum Linux kernel version supported by MTLK WLAN driver 3.10.0"
#endif

#define WV_WOW_MAX_PATTERNS       20
#define WV_WOW_MIN_PATTERN_LEN    1
#define WV_WOW_MAX_PATTERN_LEN    128

#define WV_SUPP_NUM_STS 3 /* a possible default to fall back on */

typedef struct _wv_cfg80211_t wv_cfg80211_t;
typedef struct _wave_radio_t wave_radio_t;

wave_radio_t * __MTLK_IFUNC wv_cfg80211_wave_radio_get (wv_cfg80211_t *cfg80211);

void            __MTLK_IFUNC wv_cfg80211_destroy(wv_cfg80211_t *cfg80211);
wv_cfg80211_t * __MTLK_IFUNC wv_cfg80211_create(void);
int             __MTLK_IFUNC wv_cfg80211_init(wv_cfg80211_t *cfg80211, struct device *dev, void *radio_ctx, mtlk_handle_t hw_handle);
void            __MTLK_IFUNC wv_cfg80211_unregister(wv_cfg80211_t *cfg80211);
void            __MTLK_IFUNC wv_cfg80211_cleanup(wv_cfg80211_t *cfg80211);
int             __MTLK_IFUNC wv_cfg80211_register(wv_cfg80211_t *cfg80211);
void *          __MTLK_IFUNC wv_cfg80211_wiphy_get(wv_cfg80211_t *cfg80211);
int             __MTLK_IFUNC wv_cfg80211_handle_eapol(mtlk_vap_handle_t vap_handle, void *data, int data_len);
int             __MTLK_IFUNC wv_cfg80211_handle_flush_stations(mtlk_vap_handle_t vap_handle, void *data, int data_len);
int             __MTLK_IFUNC wv_cfg80211_handle_get_unconnected_sta(struct wireless_dev *wdev, void *data,int data_len);
void            __MTLK_IFUNC wv_cfg80211_class3_error_notify (struct wireless_dev *wdev, const u8 *addr);
BOOL            __MTLK_IFUNC wv_cfg80211_mngmn_frame (struct wireless_dev *wdev, const u8 *data, int size, int freq, int sig_dbm, int snr_db, unsigned subtype);
void            __MTLK_IFUNC wv_cfg80211_obss_beacon_report (struct wireless_dev *wdev, uint8 *frame, size_t len, int channel, int sig_dbm);

void            __MTLK_IFUNC wv_cfg80211_inform_bss_frame(struct wireless_dev *wdev, struct mtlk_channel *chan,
                             u8 *buf, size_t len, s32 signal);

unsigned        __MTLK_IFUNC wv_cfg80211_regulatory_limit_get (struct wireless_dev *wdev,
                                                               unsigned lower_freq, unsigned upper_freq);

void            __MTLK_IFUNC wv_cfg80211_peer_connect (struct net_device *ndev, const IEEE_ADDR *mac_addr, wds_beacon_data_t *beacon_data);
void            __MTLK_IFUNC wv_cfg80211_peer_disconnect (struct net_device *ndev, const IEEE_ADDR *mac_addr);

bool wv_cfg80211_get_chans_dfs_required(struct wiphy *wiphy, u32 center_freq, u32 bandwidth);
bool wv_cfg80211_get_chans_dfs_bitmap_valid(struct wiphy *wiphy, u32 center_freq, u32 bandwidth, u8 rbm);
int wv_cfg80211_update_supported_bands(mtlk_handle_t hw_handle, unsigned radio_id, struct wiphy *wiphy, u32 num_sts);
void wv_cfg80211_radar_event(struct wireless_dev *wdev, struct cfg80211_chan_def *chandef,
                             uint32 cac_started, uint8 rbm);
int wv_cfg80211_radar_event_if_sta_exist(struct wireless_dev *wdev, struct mtlk_chan_def *mcd);
void wv_cfg80211_cac_event(struct net_device *ndev, const struct mtlk_chan_def *mcd, int finished);
void wv_cfg80211_ch_switch_notify(struct net_device *ndev, const struct mtlk_chan_def *mcd,  bool sta_exist);
void wv_fill_ht_capab(struct mtlk_scan_request *request, mtlk_hw_band_e band, u8 *buf, size_t len);

void wv_cfg80211_set_chan_dfs_state(struct wiphy *wiphy, uint32 center_freq, uint32 bandwidth, enum nl80211_dfs_state dfs_state);

void wv_cfg80211_set_scan_expire_time(struct wireless_dev *wdev, uint32 time);
uint32 wv_cfg80211_get_scan_expire_time(struct wireless_dev *wdev);

int wave_cfg80211_vendor_hapd_send_eapol(struct wiphy *wiphy, struct wireless_dev *wdev, const void *data, int data_len);
int wave_cfg80211_vendor_hapd_get_radio_info(struct wiphy *wiphy, struct wireless_dev *wdev, const void *data, int data_len);
int wave_cfg80211_vendor_hapd_get_unconnected_sta(struct wiphy *wiphy, struct wireless_dev *wdev, const void *data, int data_len);
int __MTLK_IFUNC wave_cfg80211_vendor_cmd_reply(struct wiphy *wiphy, const void *data, int data_len);

BOOL _mtlk_is_rate_80211b(uint32 bitrate);
BOOL _mtlk_is_rate_80211ag(uint32 bitrate);
void __MTLK_IFUNC
wv_cfg80211_update_wiphy_ant_mask(struct wiphy *wiphy, uint8 available_ant_mask);

static __INLINE mtlk_nbuf_t *
wv_cfg80211_vendor_cmd_alloc_reply_skb(struct wiphy *wiphy, int approxlen)
{
  return cfg80211_vendor_cmd_alloc_reply_skb(wiphy, approxlen);
}

static __INLINE int
wv_cfg80211_vendor_cmd_reply(mtlk_nbuf_t *nbuf)
{
  return cfg80211_vendor_cmd_reply(nbuf);
}

static __INLINE mtlk_nbuf_t *
wv_cfg80211_vendor_event_alloc(struct wireless_dev *wdev, int approxlen, int event_idx)
{
  return cfg80211_vendor_event_alloc(wdev->wiphy, wdev, approxlen, event_idx, GFP_ATOMIC);
}

static __INLINE void
wv_cfg80211_vendor_event(mtlk_nbuf_t *nbuf)
{
  cfg80211_vendor_event(nbuf, GFP_ATOMIC);
}

static __INLINE
void wv_cfg80211_scan_done(struct mtlk_scan_request *request, bool aborted)
{
  struct cfg80211_scan_info info = {
                        .aborted = aborted,
  };
  struct cfg80211_scan_request *sr =
    (struct cfg80211_scan_request *)request->saved_request;

  /* request->saved_request is freed by the kernel, need to set driver pointer
     to NULL as well */
  request->saved_request = NULL;
  cfg80211_scan_done(sr, &info);
}

static __INLINE
void wv_cfg80211_sched_scan_results(struct mtlk_scan_request *request)
{
  struct cfg80211_sched_scan_request *ssr;

  ssr = (struct cfg80211_sched_scan_request *)request->saved_request;
  request->saved_request = NULL;
  cfg80211_sched_scan_results(ssr->wiphy, ssr->reqid);
}

#ifdef CPTCFG_IWLWAV_DBG_DRV_UTEST

void __MTLK_IFUNC wv_cfg80211_utest(struct net_device *ndev);

#endif /* CPTCFG_IWLWAV_DBG_DRV_UTEST */

void wv_cfg80211_regulatory_propagate_dfs_state(struct wireless_dev *wdev,
  struct cfg80211_chan_def *chandef,
  enum nl80211_dfs_state dfs_state,
  enum nl80211_radar_event event,
  uint8 rbm);
void wv_cfg80211_nop_finished_send (struct wiphy *wiphy, mtlk_df_t *df,
  struct mtlk_chan_def *mcd);
void wv_cfg80211_on_core_down (mtlk_core_t *core);

mtlk_core_t * _wave_cfg80211_get_maste_core (struct _wv_cfg80211_t *cfg80211);
void _wave_cfg80211_register_vendor_cmds(struct wiphy *wiphy);
void _wave_cfg80211_register_vendor_evts(struct wiphy *wiphy);

#endif /* _MTLK_CFG80211_H_ */
