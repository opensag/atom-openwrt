/******************************************************************************

                               Copyright (c) 2012
                            Lantiq Deutschland GmbH

  For licensing information, see the file 'LICENSE' in the root folder of
  this software module.

******************************************************************************/
#ifndef _MTLK_MAC80211_H_
#define _MTLK_MAC80211_H_

#include <net/cfg80211.h>
#include <net/mac80211.h>
#include <linux/netdevice.h>
#include "mtlk_df_user_priv.h"
#include "mtlk_coreui.h"


typedef struct _wv_mac80211_t wv_mac80211_t;

wv_mac80211_t *       __MTLK_IFUNC wv_ieee80211_init (struct device *dev, void *radio_ctx, mtlk_handle_t hw_handle);
int                   __MTLK_IFUNC wv_ieee80211_setup_register(struct device *dev, wv_mac80211_t *mac80211, mtlk_handle_t hw_handle);
void                  __MTLK_IFUNC wv_ieee80211_free (wv_mac80211_t *mac80211);
void                  __MTLK_IFUNC wv_ieee80211_unregister (wv_mac80211_t *mac80211);
void                  __MTLK_IFUNC wv_ieee80211_cleanup(wv_mac80211_t *mac80211);
void                  __MTLK_IFUNC wv_ieee80211_mngmn_frame_rx(mtlk_core_t *nic, const u8 *data, int size, int freq, int sig_dbm,
                                                               unsigned subtype, uint16 rate_idx, u8 phy_mode, uint8 pmf_flags);
void                  __MTLK_IFUNC wv_ieee80211_tx_status(struct ieee80211_hw *hw, struct sk_buff *skb, u8 status);
struct ieee80211_hw * __MTLK_IFUNC wv_ieee80211_hw_get(wv_mac80211_t *mac80211);

void *                __MTLK_IFUNC wv_mac80211_wiphy_get(wv_mac80211_t *mac80211);

wave_radio_t *        __MTLK_IFUNC wv_ieee80211_hw_radio_get (struct ieee80211_hw *hw);

BOOL                  __MTLK_IFUNC wv_mac80211_has_sta_connections(wv_mac80211_t *mac80211);
int                   __MTLK_IFUNC wv_mac80211_NDP_send_to_all_APs(wv_mac80211_t *mac80211, mtlk_nbuf_t *nbuf_ndp, BOOL power_mgmt_on, BOOL wait_for_ack);

void                  __MTLK_IFUNC wv_ieee80211_scan_completed(struct ieee80211_hw *hw, BOOL aborted);
void                  __MTLK_IFUNC wv_ieee80211_sched_scan_results(struct ieee80211_hw *hw);
mtlk_df_user_t*       __MTLK_IFUNC wv_ieee80211_ndev_to_dfuser(struct net_device *ndev);
uint8*                __MTLK_IFUNC wv_ieee80211_peer_ap_address(struct ieee80211_vif *vif);

void                  __MTLK_IFUNC wv_mac80211_recover_sta_vifs(wv_mac80211_t *mac80211);

struct ieee80211_hw * __MTLK_IFUNC wave_vap_ieee80211_hw_get (mtlk_vap_handle_t vap_handle);
void                  __MTLK_IFUNC wave_vap_increment_ndp_counter(mtlk_vap_handle_t vap_handle);

void                  __MTLK_IFUNC wv_mac80211_iface_set_beacon_interval(wv_mac80211_t *mac80211, u8 vap_index, u16 beacon_interval);
void                  __MTLK_IFUNC wv_mac80211_iface_set_initialized (wv_mac80211_t *mac80211, u8 vap_index);
BOOL                  __MTLK_IFUNC wv_mac80211_iface_get_is_initialized (wv_mac80211_t *mac80211, u8 vap_index);

static __INLINE mtlk_nbuf_t *
wv_ieee80211_vendor_event_alloc(struct wiphy *wiphy, int approxlen, int event_idx)
{
  return cfg80211_vendor_event_alloc(wiphy, NULL, approxlen, event_idx, GFP_ATOMIC);
}
#endif /* _MTLK_MAC80211_H_ */
