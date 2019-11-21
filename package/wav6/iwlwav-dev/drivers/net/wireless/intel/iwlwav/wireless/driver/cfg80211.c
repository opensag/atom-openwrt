/******************************************************************************

                               Copyright (c) 2012
                            Lantiq Deutschland GmbH

  For licensing information, see the file 'LICENSE' in the root folder of
  this software module.

******************************************************************************/
#include "mtlkinc.h"
#include <net/iw_handler.h>
#include <net/cfg80211.h>
#ifndef CPTCFG_IWLWAV_X86_HOST_PC
#include <../net/wireless/core.h>
#include <../net/wireless/nl80211.h>
#include <../net/wireless/reg.h>
#endif

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
#include "hw_mmb.h"
#include "wds.h"
#include "mtlk_df_nbuf.h"
#include "mtlk_param_db.h"
#include "core_config.h"
#include "wave_radio.h"
#include "vendor_cmds.h"

#define LOG_LOCAL_GID   GID_CFG80211
#define LOG_LOCAL_FID   0

/* There is only a single combination (of AP's only) with a single limitation.
   Station mode interfaces have a separate mac80211 PHY */
#define CFG80211_AP_MAX_IF_COMB 1
#define CFG80211_AP_MAX_IF_LIMIT 1

#define MAX_RETRY_TIMES 1
#define LINUX_ERR_OK    0

#define API_WRAPPER(api_function) api_function ## _recovery_retry_wrapper

extern struct iw_handler_def wave_linux_iw_radio_handler_def;
extern struct iw_handler_def wave_linux_iw_core_handler_def;
extern const struct ieee80211_regdomain *get_cfg80211_regdom(void);
extern const struct ieee80211_regdomain *get_wiphy_regdom(struct wiphy *wiphy);

static struct ieee80211_iface_combination *create_cfg80211_if_comb(mtlk_handle_t hw_handle, wave_radio_t *radio);

struct workqueue_struct *cfg80211_get_cfg80211_wq(void);
void cfg80211_set_scan_expire_time(struct wiphy *wiphy, uint32 time);
uint32 cfg80211_get_scan_expire_time(struct wiphy *wiphy);

struct _wv_cfg80211_t
{
  BOOL registered;
  /* mtlk_vap_manager_t *vap_manager;*/
  wave_radio_t *radio;
  struct wiphy *wiphy;
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
#define WV_STYPE_AP_RX            (BIT(IEEE80211_STYPE_ASSOC_REQ >> 4) |    \
                                   BIT(IEEE80211_STYPE_REASSOC_REQ >> 4) |  \
                                   BIT(IEEE80211_STYPE_PROBE_REQ >> 4) |    \
                                   BIT(IEEE80211_STYPE_DISASSOC >> 4) |     \
                                   BIT(IEEE80211_STYPE_AUTH >> 4) |         \
                                   BIT(IEEE80211_STYPE_DEAUTH >> 4) |       \
                                   BIT(IEEE80211_STYPE_ACTION >> 4))

static const struct ieee80211_txrx_stypes
wv_mgmt_stypes[NUM_NL80211_IFTYPES] = {
  [NL80211_IFTYPE_AP] = {
    .tx = WV_STYPE_ALL,
    .rx = WV_STYPE_AP_RX
  },
  [NL80211_IFTYPE_AP_VLAN] = {
    /* Same as AP */
    .tx = WV_STYPE_ALL,
    .rx = WV_STYPE_AP_RX
  }
};

#define RATE(_rate, _rateid, _flags) {  \
  .bitrate        = (_rate),    \
  .hw_value       = (_rateid),   \
  .flags          = (_flags)    \
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

/* FIXCFG80211: set rates according to hardware */
static struct ieee80211_rate _cfg80211_2ghz_rates[] = {
  RATE(10, 11, 0),
  RATE(20, 12, IEEE80211_RATE_SHORT_PREAMBLE),
  RATE(55, 13, IEEE80211_RATE_SHORT_PREAMBLE),
  RATE(110, 10, IEEE80211_RATE_SHORT_PREAMBLE),
  RATE(60, 0, 0),
  RATE(90, 1, 0),
  RATE(120, 2, 0),
  RATE(180, 3, 0),
  RATE(240, 4, 0),
  RATE(360, 5, 0),
  RATE(480, 6, 0),
  RATE(540, 7, 0)
};

/* FIXCFG80211: set rates according to hardware */
static struct ieee80211_rate _cfg80211_5ghz_rates[] = {
  RATE(60, 0, 0),
  RATE(90, 1, 0),
  RATE(120, 2, 0),
  RATE(180, 3, 0),
  RATE(240, 4, 0),
  RATE(360, 5, 0),
  RATE(480, 6, 0),
  RATE(540, 7, 0)
};

/* template for 2.4 GHz channels, we'll take a copy before wiphy_register */
static struct ieee80211_channel _cfg80211_2ghz_channels[] = {
  CHAN2G(1, 2412),
  CHAN2G(2, 2417),
  CHAN2G(3, 2422),
  CHAN2G(4, 2427),
  CHAN2G(5, 2432),
  CHAN2G(6, 2437),
  CHAN2G(7, 2442),
  CHAN2G(8, 2447),
  CHAN2G(9, 2452),
  CHAN2G(10, 2457),
  CHAN2G(11, 2462),
  CHAN2G(12, 2467),
  CHAN2G(13, 2472),
  CHAN2G(14, 2484)
};

/* template for 5 GHz channels, we'll take a copy before wiphy_register */
static struct ieee80211_channel _cfg80211_5ghz_channels[] = {
  CHAN5G(36, 5180),
  CHAN5G(40, 5200),
  CHAN5G(44, 5220),
  CHAN5G(48, 5240),
  CHAN5G(52, 5260),
  CHAN5G(56, 5280),
  CHAN5G(60, 5300),
  CHAN5G(64, 5320),
  CHAN5G(100, 5500),
  CHAN5G(104, 5520),
  CHAN5G(108, 5540),
  CHAN5G(112, 5560),
  CHAN5G(116, 5580),
  CHAN5G(120, 5600),
  CHAN5G(124, 5620),
  CHAN5G(128, 5640),
  CHAN5G(132, 5660),
  CHAN5G(136, 5680),
  CHAN5G(140, 5700),
  CHAN5G(144, 5720),
  CHAN5G(149, 5745),
  CHAN5G(153, 5765),
  CHAN5G(157, 5785),
  CHAN5G(161, 5805),
  CHAN5G(165, 5825)
};

#define WV_SUPP_RX_STBC 1
#define WV_SUPP_AMPDU_MAXLEN_EXP 20


#define WV_ENDIAN16_SHIFT_CHANGE(x) ((1 - (x) / 8) * 16 - 8 + (x))

#define MTLK_5GHZ_NUM_BEAMFORMEE_STS 4
#define MTLK_5GHZ_NUM_SOUNDING_DIMENSIONS MTLK_5GHZ_NUM_BEAMFORMEE_STS

#ifdef __LITTLE_ENDIAN
  #define WV_MCS_MAP_SS_SHIFT(x) (((x) - 1) * 2)
#endif
#ifdef __BIG_ENDIAN
  #define WV_MCS_MAP_SS_SHIFT(x) WV_ENDIAN16_SHIFT_CHANGE(((x) - 1) * 2)
#endif

#define IEEE80211_HT_MCS_LIMIT    4
#define IEEE80211_VHT_MCS_LIMIT   8

#define VHT_CAP_FLAGS \
  (IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_3895 /* this is a 0 */                                              \
/* | IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_7991 */ /* this is a 1; value 3 in the first 2 bits is illegal */ \
   | IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_11454 /* this is a 2 */                                           \
/* | IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_160MHZ */                                                         \
/* | IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_160_80PLUS80MHZ */                                                \
/* | IEEE80211_VHT_CAP_RXLDPC */                                                                         \
   | IEEE80211_VHT_CAP_SHORT_GI_80                                                                       \
/* | IEEE80211_VHT_CAP_SHORT_GI_160 */                                                                   \
   | IEEE80211_VHT_CAP_TXSTBC         /* Static configuration for all chips. Always ON. */               \
/* | IEEE80211_VHT_CAP_RXSTBC_1 set runtime depending on the chip */                                     \
/* | IEEE80211_VHT_CAP_RXSTBC_2 */                                                                       \
/* | IEEE80211_VHT_CAP_RXSTBC_3 */                                                                       \
/* | IEEE80211_VHT_CAP_RXSTBC_4 */                                                                       \
   | IEEE80211_VHT_CAP_SU_BEAMFORMER_CAPABLE                                                             \
   | IEEE80211_VHT_CAP_SU_BEAMFORMEE_CAPABLE                                                             \
/* avoid setting things dependent on STS-es now, leave them for later */                                 \
/* | ((WV_SUPP_NUM_STS - 1) << IEEE80211_VHT_CAP_BEAMFORMEE_STS_SHIFT) */                                \
/* | ((WV_SUPP_NUM_STS - 1) << IEEE80211_VHT_CAP_SOUNDING_DIMENSIONS_SHIFT) */                           \
   | IEEE80211_VHT_CAP_MU_BEAMFORMER_CAPABLE                                                             \
   | IEEE80211_VHT_CAP_VHT_TXOP_PS                                                                       \
/* | IEEE80211_VHT_CAP_HTC_VHT */                                                                        \
/* for VHT A-MPDU factor is 7<<23, which means MAX */                                                    \
   | ((WV_SUPP_AMPDU_MAXLEN_EXP - IEEE80211_HT_MAX_AMPDU_FACTOR)                                         \
      << IEEE80211_VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_SHIFT)                                             \
/* | IEEE80211_VHT_CAP_VHT_LINK_ADAPTATION_VHT_UNSOL_MFB */                                              \
/* | IEEE80211_VHT_CAP_VHT_LINK_ADAPTATION_VHT_MRQ_MFB */                                                \
/* | IEEE80211_VHT_CAP_RX_ANTENNA_PATTERN */                                                             \
/* | IEEE80211_VHT_CAP_TX_ANTENNA_PATTERN */ )


/* 2.4 GHz supported band template, we'll take a fresh copy for each wiphy we register */
static struct ieee80211_supported_band _cfg80211_band_2ghz = {
  .n_channels = 0,  /* to be filled later */
  .channels = NULL, /* to be filled later */
  .n_bitrates = ARRAY_SIZE(_cfg80211_2ghz_rates),
  .bitrates = _cfg80211_2ghz_rates,
  /* FIXCFG80211: setup according to hardware */
  .ht_cap.cap = (/* IEEE80211_HT_CAP_LDPC_CODING     | Set runtime depending on the chip */
                 IEEE80211_HT_CAP_SUP_WIDTH_20_40 | /* Static configuration for all chips. Always ON. */
                 IEEE80211_HT_CAP_SGI_20          |
                 IEEE80211_HT_CAP_SGI_40          |
                 IEEE80211_HT_CAP_DSSSCCK40       |
                 IEEE80211_HT_CAP_MAX_AMSDU       |
                 IEEE80211_HT_CAP_SGI_20          |
                 IEEE80211_HT_CAP_SGI_40          |
                 IEEE80211_HT_CAP_TX_STBC         /* | Set runtime depending on the chip */
                 /* (WV_SUPP_RX_STBC << IEEE80211_HT_CAP_RX_STBC_SHIFT)*/),
  /* thexe are minimal rates for just 1 STS, to be adjusted below once we know the number of STS-es */
  .ht_cap.mcs.rx_mask[0] = 0xff,
  .ht_cap.mcs.rx_mask[4] = 0x01, /* "Bit 32 always on */
  /* .ht_cap.ampdu_factor   = IEEE80211_HT_MAX_AMPDU_64K, - Set runtime depending on the chip */
  /* .ht_cap.ampdu_density  = IEEE80211_HT_MPDU_DENSITY_16, */
  /* .ht_cap.mcs.tx_params = IEEE80211_HT_MCS_TX_DEFINED, - Tx MCS set is not defined */
  .ht_cap.ht_supported = TRUE,
  .vht_cap.vht_supported = TRUE,
  .vht_cap.cap = VHT_CAP_FLAGS,
  /* MCS 0--9 supported for 1 spatial stream, no support for more; adjusted below when we know more */
  .vht_cap.vht_mcs.rx_mcs_map = ((IEEE80211_VHT_MCS_SUPPORT_0_9 << WV_MCS_MAP_SS_SHIFT(1))
                                 | (IEEE80211_VHT_MCS_NOT_SUPPORTED << WV_MCS_MAP_SS_SHIFT(2))
                                 | (IEEE80211_VHT_MCS_NOT_SUPPORTED << WV_MCS_MAP_SS_SHIFT(3))
                                 | (IEEE80211_VHT_MCS_NOT_SUPPORTED << WV_MCS_MAP_SS_SHIFT(4))
                                 | (IEEE80211_VHT_MCS_NOT_SUPPORTED << WV_MCS_MAP_SS_SHIFT(5))
                                 | (IEEE80211_VHT_MCS_NOT_SUPPORTED << WV_MCS_MAP_SS_SHIFT(6))
                                 | (IEEE80211_VHT_MCS_NOT_SUPPORTED << WV_MCS_MAP_SS_SHIFT(7))
                                 | (IEEE80211_VHT_MCS_NOT_SUPPORTED << WV_MCS_MAP_SS_SHIFT(8))),
  /* MCS 0--9 supported for 1 spatial stream, no support for more; adjusted below when we know more */
  .vht_cap.vht_mcs.tx_mcs_map = ((IEEE80211_VHT_MCS_SUPPORT_0_9 << WV_MCS_MAP_SS_SHIFT(1))
                                 | (IEEE80211_VHT_MCS_NOT_SUPPORTED << WV_MCS_MAP_SS_SHIFT(2))
                                 | (IEEE80211_VHT_MCS_NOT_SUPPORTED << WV_MCS_MAP_SS_SHIFT(3))
                                 | (IEEE80211_VHT_MCS_NOT_SUPPORTED << WV_MCS_MAP_SS_SHIFT(4))
                                 | (IEEE80211_VHT_MCS_NOT_SUPPORTED << WV_MCS_MAP_SS_SHIFT(5))
                                 | (IEEE80211_VHT_MCS_NOT_SUPPORTED << WV_MCS_MAP_SS_SHIFT(6))
                                 | (IEEE80211_VHT_MCS_NOT_SUPPORTED << WV_MCS_MAP_SS_SHIFT(7))
                                 | (IEEE80211_VHT_MCS_NOT_SUPPORTED << WV_MCS_MAP_SS_SHIFT(8)))
};

/* 5 GHz supported band template, we'll take a fresh copy for each wiphy we register */
static struct ieee80211_supported_band _cfg80211_band_5ghz = {
  .n_channels = 0,  /* to be filled later */
  .channels = NULL, /* to be filled later */
  .n_bitrates = ARRAY_SIZE(_cfg80211_5ghz_rates),
  .bitrates = _cfg80211_5ghz_rates,
  /* FIXCFG80211: setup according to hardware */
  .ht_cap.cap = (/* IEEE80211_HT_CAP_LDPC_CODING     | - set runtime depending on the chip */
                 IEEE80211_HT_CAP_SUP_WIDTH_20_40 | /* Static configuration for all chips. Always ON. */
                 IEEE80211_HT_CAP_SGI_20          |
                 IEEE80211_HT_CAP_SGI_40          |
                 IEEE80211_HT_CAP_DSSSCCK40       |
                 IEEE80211_HT_CAP_MAX_AMSDU       |
                 IEEE80211_HT_CAP_SGI_20          |
                 IEEE80211_HT_CAP_SGI_40          |
                 IEEE80211_HT_CAP_TX_STBC         /* | Set runtime depending on the chip
                 (WV_SUPP_RX_STBC << IEEE80211_HT_CAP_RX_STBC_SHIFT)*/),
  /* thexe are minimal rates for just 1 STS, to be adjusted below once we know the number of STS-es */
  .ht_cap.mcs.rx_mask[0] = 0xff,
  .ht_cap.mcs.rx_mask[4] = 0x01, /* "Bit 32 always on */
  /* .ht_cap.ampdu_factor   = IEEE80211_HT_MAX_AMPDU_64K, - Set runtime depending on the chip */
  /* .ht_cap.ampdu_density  = IEEE80211_HT_MPDU_DENSITY_16, */
  /* .ht_cap.mcs.tx_params = IEEE80211_HT_MCS_TX_DEFINED, - Tx MCS set is not defined */
  .ht_cap.ht_supported = TRUE,
  .vht_cap.vht_supported = TRUE,
  .vht_cap.cap = VHT_CAP_FLAGS,
  /* MCS 0--9 supported for 1 spatial stream, no support for more; adjusted below when we know more */
  .vht_cap.vht_mcs.rx_mcs_map = ((IEEE80211_VHT_MCS_SUPPORT_0_9 << WV_MCS_MAP_SS_SHIFT(1))
                                 | (IEEE80211_VHT_MCS_NOT_SUPPORTED << WV_MCS_MAP_SS_SHIFT(2))
                                 | (IEEE80211_VHT_MCS_NOT_SUPPORTED << WV_MCS_MAP_SS_SHIFT(3))
                                 | (IEEE80211_VHT_MCS_NOT_SUPPORTED << WV_MCS_MAP_SS_SHIFT(4))
                                 | (IEEE80211_VHT_MCS_NOT_SUPPORTED << WV_MCS_MAP_SS_SHIFT(5))
                                 | (IEEE80211_VHT_MCS_NOT_SUPPORTED << WV_MCS_MAP_SS_SHIFT(6))
                                 | (IEEE80211_VHT_MCS_NOT_SUPPORTED << WV_MCS_MAP_SS_SHIFT(7))
                                 | (IEEE80211_VHT_MCS_NOT_SUPPORTED << WV_MCS_MAP_SS_SHIFT(8))),
  /* MCS 0--9 supported for 1 spatial stream, no support for more; adjusted below when we know more */
  .vht_cap.vht_mcs.tx_mcs_map = ((IEEE80211_VHT_MCS_SUPPORT_0_9 << WV_MCS_MAP_SS_SHIFT(1))
                                 | (IEEE80211_VHT_MCS_NOT_SUPPORTED << WV_MCS_MAP_SS_SHIFT(2))
                                 | (IEEE80211_VHT_MCS_NOT_SUPPORTED << WV_MCS_MAP_SS_SHIFT(3))
                                 | (IEEE80211_VHT_MCS_NOT_SUPPORTED << WV_MCS_MAP_SS_SHIFT(4))
                                 | (IEEE80211_VHT_MCS_NOT_SUPPORTED << WV_MCS_MAP_SS_SHIFT(5))
                                 | (IEEE80211_VHT_MCS_NOT_SUPPORTED << WV_MCS_MAP_SS_SHIFT(6))
                                 | (IEEE80211_VHT_MCS_NOT_SUPPORTED << WV_MCS_MAP_SS_SHIFT(7))
                                 | (IEEE80211_VHT_MCS_NOT_SUPPORTED << WV_MCS_MAP_SS_SHIFT(8)))
};

/* Common HE MAC PHY Capabilities for all bands and STS */
#define HE_MAC_CAP0       (IEEE80211_HE_MAC_CAP0_HTC_HE | IEEE80211_HE_MAC_CAP0_TWT_RES)
#define HE_MAC_CAP1       (IEEE80211_HE_MAC_CAP1_MULTI_TID_AGG_RX_QOS_8)
#define HE_MAC_CAP2       (IEEE80211_HE_MAC_CAP2_ACK_EN)
#define HE_MAC_CAP3       (IEEE80211_HE_MAC_CAP3_OMI_CONTROL | IEEE80211_HE_MAC_CAP3_MAX_AMPDU_LEN_EXP_VHT_2)
#define HE_MAC_CAP4       (IEEE80211_HE_MAC_CAP4_AMDSU_IN_AMPDU | \
                           IEEE80211_HE_MAC_CAP4_MULTI_TID_AGG_TX_QOS_B39)
#define HE_MAC_CAP5       (IEEE80211_HE_MAC_CAP5_MULTI_TID_AGG_TX_QOS_B40 | \
                           IEEE80211_HE_MAC_CAP5_MULTI_TID_AGG_TX_QOS_B41 | \
                           IEEE80211_HE_MAC_CAP5_OM_CTRL_UL_MU_DATA_DIS_RX)
#define HE_PHY_CAP1       (IEEE80211_HE_PHY_CAP1_DEVICE_CLASS_A | IEEE80211_HE_PHY_CAP1_LDPC_CODING_IN_PAYLOAD)
#define HE_PHY_CAP2       (IEEE80211_HE_PHY_CAP2_STBC_TX_UNDER_80MHZ)
#define HE_PHY_CAP3       (IEEE80211_HE_PHY_CAP3_SU_BEAMFORMER)
#define HE_PHY_CAP4       (IEEE80211_HE_PHY_CAP4_BEAMFORMEE_MAX_STS_UNDER_80MHZ_4 | \
                           IEEE80211_HE_PHY_CAP4_BEAMFORMEE_MAX_STS_ABOVE_80MHZ_4 | \
                           IEEE80211_HE_PHY_CAP4_SU_BEAMFORMEE | IEEE80211_HE_PHY_CAP4_MU_BEAMFORMER)
#define HE_PHY_CAP6       (IEEE80211_HE_PHY_CAP6_TRIG_SU_BEAMFORMER_FB | IEEE80211_HE_PHY_CAP6_TRIG_CQI_FB | \
                           IEEE80211_HE_PHY_CAP6_PPE_THRESHOLD_PRESENT)
#define HE_PHY_CAP7       (IEEE80211_HE_PHY_CAP7_HE_SU_MU_PPDU_4XLTF_AND_08_US_GI)
#define HE_PPE_THRES1     0x1c /*PPET16_NSTSn_RUx|PPET8_NSTSn_RUx..*/
#define HE_PPE_THRES2     0xc7
#define HE_PPE_THRES3     0x71
#define HE_PPE_THRES4     0xf1 /*..PPET16_NSTSn_RUx|PPET8_NSTSn_RUx */
#define HE_PPE_THRES_NOT_SUPPORTED 0xff

/* Common for 2GHz band */
#define HE_2G_PHY_CAP0    (IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_IN_2G)

/* Common for 5GHz band */
#define HE_5G_PHY_CAP0    (IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_80MHZ_IN_5G | \
                          IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_160MHZ_IN_5G)
#define HE_MCS_NSS_NOT_SUPPORTED cpu_to_le16(0xffff)

/* Common for 2 STS */
#define HE_2STS_PHY_CAP5  (IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_UNDER_80MHZ_2 | \
                          IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_ABOVE_80MHZ_2)
#define HE_2STS_PHY_CAP7  (HE_PHY_CAP7 | IEEE80211_HE_PHY_CAP7_MAX_NC_1)
#define HE_MCS_2SS_80     cpu_to_le16(0xfffa)
#define HE_MCS_2SS_160    HE_MCS_2SS_80
#define HE_PPE_THRES_2NSS  (IEEE80211_PPE_THRES0_NSTS_1 | IEEE80211_PPE_THRES_RU_INDEX_BITMASK_MASK)

/* Common for 4 STS */
#define HE_4STS_PHY_CAP5  (IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_UNDER_80MHZ_4 | \
                          IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_ABOVE_80MHZ_4)
#define HE_4STS_PHY_CAP7  (HE_PHY_CAP7 | IEEE80211_HE_PHY_CAP7_MAX_NC_3)
#define HE_MCS_4SS_80     cpu_to_le16(0xffaa)
#define HE_MCS_4SS_160    HE_MCS_4SS_80
#define HE_PPE_THRES_4NSS (IEEE80211_PPE_THRES0_NSTS_3 | IEEE80211_PPE_THRES_RU_INDEX_BITMASK_MASK)

static const struct ieee80211_sband_iftype_data _cfg80211_he_capa_2ghz[] =
 {
  {
    .types_mask = BIT(NL80211_IFTYPE_AP),
    .he_cap = {
      .has_he = true,
      .he_cap_elem = {
        .mac_cap_info[0] = HE_MAC_CAP0,
        .mac_cap_info[1] = HE_MAC_CAP1,
        .mac_cap_info[2] = HE_MAC_CAP2,
        .mac_cap_info[3] = HE_MAC_CAP3,
        .mac_cap_info[4] = HE_MAC_CAP4,
        .mac_cap_info[5] = HE_MAC_CAP5,

        .phy_cap_info[0] = HE_2G_PHY_CAP0,
        .phy_cap_info[1] = HE_PHY_CAP1,
        .phy_cap_info[2] = HE_PHY_CAP2,
        .phy_cap_info[3] = HE_PHY_CAP3,
        .phy_cap_info[4] = HE_PHY_CAP4,
        .phy_cap_info[5] = HE_2STS_PHY_CAP5,
        .phy_cap_info[6] = HE_PHY_CAP6,
        .phy_cap_info[7] = HE_2STS_PHY_CAP7
        },
      .he_mcs_nss_supp = {
        .rx_mcs_80 =    HE_MCS_2SS_80,
        .tx_mcs_80 =    HE_MCS_2SS_80,
        .rx_mcs_160 =   HE_MCS_NSS_NOT_SUPPORTED,
        .tx_mcs_160 =   HE_MCS_NSS_NOT_SUPPORTED,
        .rx_mcs_80p80 = HE_MCS_NSS_NOT_SUPPORTED,
        .tx_mcs_80p80 = HE_MCS_NSS_NOT_SUPPORTED,
        },
        .ppe_thres[0]  = HE_PPE_THRES_2NSS,
        .ppe_thres[1]  = HE_PPE_THRES1,
        .ppe_thres[2]  = HE_PPE_THRES2,
        .ppe_thres[3]  = HE_PPE_THRES3,
        .ppe_thres[4]  = HE_PPE_THRES1,
        .ppe_thres[5]  = HE_PPE_THRES2,
        .ppe_thres[6]  = HE_PPE_THRES4,
        .ppe_thres[7]  = HE_PPE_THRES_NOT_SUPPORTED,
        .ppe_thres[8]  = HE_PPE_THRES_NOT_SUPPORTED,
        .ppe_thres[9]  = HE_PPE_THRES_NOT_SUPPORTED,
        .ppe_thres[10] = HE_PPE_THRES_NOT_SUPPORTED,
        .ppe_thres[11] = HE_PPE_THRES_NOT_SUPPORTED,
        .ppe_thres[12] = HE_PPE_THRES_NOT_SUPPORTED
      }
  },
  {
    .types_mask = BIT(NL80211_IFTYPE_AP),
    .he_cap = {
      .has_he = true,
      .he_cap_elem = {
        .mac_cap_info[0] = HE_MAC_CAP0,
        .mac_cap_info[1] = HE_MAC_CAP1,
        .mac_cap_info[2] = HE_MAC_CAP2,
        .mac_cap_info[3] = HE_MAC_CAP3,
        .mac_cap_info[4] = HE_MAC_CAP4,
        .mac_cap_info[5] = HE_MAC_CAP5,

        .phy_cap_info[0] = HE_2G_PHY_CAP0,
        .phy_cap_info[1] = HE_PHY_CAP1,
        .phy_cap_info[2] = HE_PHY_CAP2,
        .phy_cap_info[3] = HE_PHY_CAP3,
        .phy_cap_info[4] = HE_PHY_CAP4,
        .phy_cap_info[5] = HE_4STS_PHY_CAP5,
        .phy_cap_info[6] = HE_PHY_CAP6,
        .phy_cap_info[7] = HE_4STS_PHY_CAP7
      },
      .he_mcs_nss_supp = {
        .rx_mcs_80 =    HE_MCS_4SS_80,
        .tx_mcs_80 =    HE_MCS_4SS_80,
        .rx_mcs_160 =   HE_MCS_NSS_NOT_SUPPORTED,
        .tx_mcs_160 =   HE_MCS_NSS_NOT_SUPPORTED,
        .rx_mcs_80p80 = HE_MCS_NSS_NOT_SUPPORTED,
        .tx_mcs_80p80 = HE_MCS_NSS_NOT_SUPPORTED,
        },
        .ppe_thres[0]  = HE_PPE_THRES_4NSS,
        .ppe_thres[1]  = HE_PPE_THRES1,
        .ppe_thres[2]  = HE_PPE_THRES2,
        .ppe_thres[3]  = HE_PPE_THRES3,
        .ppe_thres[4]  = HE_PPE_THRES1,
        .ppe_thres[5]  = HE_PPE_THRES2,
        .ppe_thres[6]  = HE_PPE_THRES3,
        .ppe_thres[7]  = HE_PPE_THRES1,
        .ppe_thres[8]  = HE_PPE_THRES2,
        .ppe_thres[9]  = HE_PPE_THRES3,
        .ppe_thres[10] = HE_PPE_THRES1,
        .ppe_thres[11] = HE_PPE_THRES2,
        .ppe_thres[12] = HE_PPE_THRES3
      }
  }
};

static const struct ieee80211_sband_iftype_data _cfg80211_he_capa_5ghz[] =
 {
  {
    .types_mask = BIT(NL80211_IFTYPE_AP),
    .he_cap = {
      .has_he = true,
      .he_cap_elem = {
        .mac_cap_info[0] = HE_MAC_CAP0,
        .mac_cap_info[1] = HE_MAC_CAP1,
        .mac_cap_info[2] = HE_MAC_CAP2,
        .mac_cap_info[3] = HE_MAC_CAP3,
        .mac_cap_info[4] = HE_MAC_CAP4,
        .mac_cap_info[5] = HE_MAC_CAP5,

        .phy_cap_info[0] = HE_5G_PHY_CAP0,
        .phy_cap_info[1] = HE_PHY_CAP1,
        .phy_cap_info[2] = HE_PHY_CAP2,
        .phy_cap_info[3] = HE_PHY_CAP3,
        .phy_cap_info[4] = HE_PHY_CAP4,
        .phy_cap_info[5] = HE_2STS_PHY_CAP5,
        .phy_cap_info[6] = HE_PHY_CAP6,
        .phy_cap_info[7] = HE_2STS_PHY_CAP7
      },
      .he_mcs_nss_supp = {
        .rx_mcs_80 =    HE_MCS_2SS_80,
        .tx_mcs_80 =    HE_MCS_2SS_80,
        .rx_mcs_160 =   HE_MCS_2SS_160,
        .tx_mcs_160 =   HE_MCS_2SS_160,
        .rx_mcs_80p80 = HE_MCS_NSS_NOT_SUPPORTED,
        .tx_mcs_80p80 = HE_MCS_NSS_NOT_SUPPORTED,
       },
        .ppe_thres[0]  = HE_PPE_THRES_2NSS,
        .ppe_thres[1]  = HE_PPE_THRES1,
        .ppe_thres[2]  = HE_PPE_THRES2,
        .ppe_thres[3]  = HE_PPE_THRES3,
        .ppe_thres[4]  = HE_PPE_THRES1,
        .ppe_thres[5]  = HE_PPE_THRES2,
        .ppe_thres[6]  = HE_PPE_THRES4,
        .ppe_thres[7]  = HE_PPE_THRES_NOT_SUPPORTED,
        .ppe_thres[8]  = HE_PPE_THRES_NOT_SUPPORTED,
        .ppe_thres[9]  = HE_PPE_THRES_NOT_SUPPORTED,
        .ppe_thres[10] = HE_PPE_THRES_NOT_SUPPORTED,
        .ppe_thres[11] = HE_PPE_THRES_NOT_SUPPORTED,
        .ppe_thres[12] = HE_PPE_THRES_NOT_SUPPORTED
      }
  },
  {
    .types_mask = BIT(NL80211_IFTYPE_AP),
    .he_cap = {
    .has_he = true,
    .he_cap_elem = {
      .mac_cap_info[0] = HE_MAC_CAP0,
      .mac_cap_info[1] = HE_MAC_CAP1,
      .mac_cap_info[2] = HE_MAC_CAP2,
      .mac_cap_info[3] = HE_MAC_CAP3,
      .mac_cap_info[4] = HE_MAC_CAP4,
      .mac_cap_info[5] = HE_MAC_CAP5,

      .phy_cap_info[0] = HE_5G_PHY_CAP0,
      .phy_cap_info[1] = HE_PHY_CAP1,
      .phy_cap_info[2] = HE_PHY_CAP2,
      .phy_cap_info[3] = HE_PHY_CAP3,
      .phy_cap_info[4] = HE_PHY_CAP4,
      .phy_cap_info[5] = HE_4STS_PHY_CAP5,
      .phy_cap_info[6] = HE_PHY_CAP6,
      .phy_cap_info[7] = HE_4STS_PHY_CAP7
    },
      .he_mcs_nss_supp = {
        .rx_mcs_80 =    HE_MCS_4SS_80,
        .tx_mcs_80 =    HE_MCS_4SS_80,
        .rx_mcs_160 =   HE_MCS_4SS_160,
        .tx_mcs_160 =   HE_MCS_4SS_160,
        .rx_mcs_80p80 = HE_MCS_NSS_NOT_SUPPORTED,
        .tx_mcs_80p80 = HE_MCS_NSS_NOT_SUPPORTED,
      },
        .ppe_thres[0]  = HE_PPE_THRES_4NSS,
        .ppe_thres[1]  = HE_PPE_THRES1,
        .ppe_thres[2]  = HE_PPE_THRES2,
        .ppe_thres[3]  = HE_PPE_THRES3,
        .ppe_thres[4]  = HE_PPE_THRES1,
        .ppe_thres[5]  = HE_PPE_THRES2,
        .ppe_thres[6]  = HE_PPE_THRES3,
        .ppe_thres[7]  = HE_PPE_THRES1,
        .ppe_thres[8]  = HE_PPE_THRES2,
        .ppe_thres[9]  = HE_PPE_THRES3,
        .ppe_thres[10] = HE_PPE_THRES1,
        .ppe_thres[11] = HE_PPE_THRES2,
        .ppe_thres[12] = HE_PPE_THRES3
      }
  }
};

static void * _wiphy_privid = &_wiphy_privid;

/*
 * cfg80211 backend API
 */

mtlk_core_t *
_wave_cfg80211_get_maste_core (struct _wv_cfg80211_t *cfg80211)
{
  return wave_radio_master_core_get(cfg80211->radio);
}

mtlk_df_t *
wave_cfg80211_master_df_get (wv_cfg80211_t *cfg80211)
{
  mtlk_df_t     *master_df = NULL;

  MTLK_ASSERT(cfg80211 != NULL);
  MTLK_ASSERT(cfg80211->radio != NULL);

  master_df = wave_radio_df_get(cfg80211->radio);

  return master_df;
}


static struct wireless_dev * _wave_cfg80211_op_add_virtual_iface (
  struct wiphy *wiphy,
  const char *name,
  enum nl80211_iftype iftype,
  struct vif_params *params)
{
  mtlk_mbss_cfg_t mbss_cfg;
  wv_cfg80211_t *cfg80211 = wiphy_priv(wiphy);

  MTLK_ASSERT(NULL != cfg80211);
  mbss_cfg.added_vap_name = name;
  mbss_cfg.wiphy = wiphy;
  mbss_cfg.role = MTLK_ROLE_AP;
  mbss_cfg.is_master = FALSE;
  /* Will be allocated by our driver using alloc_etherdev (unlike station role vif where mac80211 already
   * allocated net_device and we only need to set it in df_user ) */
  mbss_cfg.ndev = NULL;

  return (struct wireless_dev *)wave_radio_virtual_interface_add(cfg80211->radio, &mbss_cfg, iftype);
}

static struct wireless_dev * API_WRAPPER(_wave_cfg80211_op_add_virtual_iface) (
  struct wiphy *wiphy,
  const char *name,
  unsigned char name_assign_type,
  enum nl80211_iftype iftype,
  struct vif_params *params)
{
  int retry_times = 0;
  struct wireless_dev *p_wdev;

  do {
    p_wdev = _wave_cfg80211_op_add_virtual_iface(wiphy, name, iftype, params);

    retry_times++;
  } while ((p_wdev == NULL) && (retry_times < MAX_RETRY_TIMES));

  return p_wdev;
}

static int _wave_cfg80211_op_del_virtual_iface (
  struct wiphy *wiphy,
  struct wireless_dev *wdev)
{
  wv_cfg80211_t *cfg80211 = wiphy_priv(wiphy);
  MTLK_ASSERT(NULL != cfg80211);
  return wave_radio_virtual_interface_del(cfg80211->radio, wdev->netdev);
}

static int API_WRAPPER(_wave_cfg80211_op_del_virtual_iface) (
  struct wiphy *wiphy,
  struct wireless_dev *wdev)
{
  int res;
  int retry_times = 0;

  do {
    res = _wave_cfg80211_op_del_virtual_iface(wiphy, wdev);

    retry_times++;
  } while ((res != LINUX_ERR_OK) && (retry_times < MAX_RETRY_TIMES));

  return res;
}

static int _wave_cfg80211_op_change_virtual_intf (
  struct wiphy *wiphy,
  struct net_device *ndev,
  enum nl80211_iftype iftype,
  struct vif_params *params)
{
  return wave_radio_virtual_interface_change(ndev, iftype);
}

static int API_WRAPPER(_wave_cfg80211_op_change_virtual_intf) (
  struct wiphy *wiphy,
  struct net_device *ndev,
  enum nl80211_iftype iftype,
  struct vif_params *params)
{
  int res;
  int retry_times = 0;

  do {
    res = _wave_cfg80211_op_change_virtual_intf(wiphy, ndev, iftype, params);

    retry_times++;
  } while ((res != LINUX_ERR_OK) && (retry_times < MAX_RETRY_TIMES));

  return res;
}

static int _wave_cfg80211_op_change_beacon (
  struct wiphy *wiphy,
  struct net_device *ndev,
  struct cfg80211_beacon_data *beacon)
{
  wv_cfg80211_t *cfg80211 = wiphy_priv(wiphy);
  MTLK_ASSERT(NULL != cfg80211);
  return wave_radio_beacon_change(cfg80211->radio, ndev, beacon);
}

static int API_WRAPPER(_wave_cfg80211_op_change_beacon) (
  struct wiphy *wiphy,
  struct net_device *ndev,
  struct cfg80211_beacon_data *beacon)
{
  int res;
  int retry_times = 0;

  do {
    res = _wave_cfg80211_op_change_beacon(wiphy, ndev, beacon);

    retry_times++;
  } while ((res != LINUX_ERR_OK) && (retry_times < MAX_RETRY_TIMES));

  return res;
}

static int _wave_cfg80211_op_set_ap_chanwidth(
    struct wiphy *wiphy,
    struct net_device *ndev,
    struct cfg80211_chan_def *chandef)
{
  wv_cfg80211_t *cfg80211 = wiphy_priv(wiphy);
  MTLK_ASSERT(NULL != cfg80211);
  return wave_radio_ap_chanwidth_set(wiphy, cfg80211->radio, ndev, chandef);
}

static int API_WRAPPER(_wave_cfg80211_op_set_ap_chanwidth) (
    struct wiphy *wiphy,
    struct net_device *ndev,
    struct cfg80211_chan_def *chandef)
{
  int res;
  int retry_times = 0;

  do {
    res = _wave_cfg80211_op_set_ap_chanwidth(wiphy, ndev, chandef);

    retry_times++;
  } while ((res != LINUX_ERR_OK) && (retry_times < MAX_RETRY_TIMES));

  return res;
}

static int _wave_cfg80211_op_start_ap (
  struct wiphy *wiphy,
  struct net_device *ndev,
  struct cfg80211_ap_settings *info)
{
  wv_cfg80211_t *cfg80211 = wiphy_priv(wiphy);
  MTLK_ASSERT(NULL != cfg80211);
  return wave_radio_ap_start(wiphy, cfg80211->radio, ndev, info);
}

static int API_WRAPPER(_wave_cfg80211_op_start_ap) (
  struct wiphy *wiphy,
  struct net_device *ndev,
  struct cfg80211_ap_settings *info)
{
  int res;
  int retry_times = 0;

  do {
    res = _wave_cfg80211_op_start_ap(wiphy, ndev, info);

    retry_times++;
  } while ((res != LINUX_ERR_OK) && (retry_times < MAX_RETRY_TIMES));

  return res;
}

static int _wave_cfg80211_op_stop_ap (
  struct wiphy *wiphy,
  struct net_device *ndev)
{
  wv_cfg80211_t *cfg80211 = wiphy_priv(wiphy);
  MTLK_ASSERT(NULL != cfg80211);
  return wave_radio_ap_stop(cfg80211->radio, ndev);
}

static int API_WRAPPER(_wave_cfg80211_op_stop_ap) (
  struct wiphy *wiphy,
  struct net_device *ndev)
{
  int res;
  int retry_times = 0;

  do {
    res = _wave_cfg80211_op_stop_ap(wiphy, ndev);

    retry_times++;
  } while ((res != LINUX_ERR_OK) && (retry_times < MAX_RETRY_TIMES));

  return res;
}

static int _wave_cfg80211_op_add_station (
  struct wiphy *wiphy,
  struct net_device *ndev,
  const uint8 *mac,
  struct station_parameters *params)
{
  wv_cfg80211_t *cfg80211 = wiphy_priv(wiphy);
  MTLK_ASSERT(NULL != cfg80211);
  return wave_radio_sta_add(cfg80211->radio, ndev, mac, params);
}

static int API_WRAPPER(_wave_cfg80211_op_add_station) (
  struct wiphy *wiphy,
  struct net_device *ndev,
  const uint8 *mac,
  struct station_parameters *params)
{
  int res;
  int retry_times = 0;

  do {
    res = _wave_cfg80211_op_add_station(wiphy, ndev, mac, params);

    retry_times++;
  } while ((res != LINUX_ERR_OK) && (retry_times < MAX_RETRY_TIMES));

  return res;
}

static int _wave_cfg80211_op_change_station (
  struct wiphy *wiphy,
  struct net_device *ndev,
  const uint8 *mac,
  struct station_parameters *params)
{
  wv_cfg80211_t *cfg80211 = wiphy_priv(wiphy);
  MTLK_ASSERT(NULL != cfg80211);
  return wave_radio_sta_change(cfg80211->radio, ndev, mac, params);
}

static int API_WRAPPER(_wave_cfg80211_op_change_station) (
  struct wiphy *wiphy,
  struct net_device *ndev,
  const uint8 *mac,
  struct station_parameters *params)
{
  int res;
  int retry_times = 0;

  do {
    res = _wave_cfg80211_op_change_station(wiphy, ndev, mac, params);

    retry_times++;
  } while ((res != LINUX_ERR_OK) && (retry_times < MAX_RETRY_TIMES));

  return res;
}

static const uint8 _bcast_addr[ETH_ALEN] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

static int _wave_cfg80211_op_del_station(
  struct wiphy *wiphy,
  struct net_device *ndev,
  const uint8 *mac)
{
  wv_cfg80211_t *cfg80211 = wiphy_priv(wiphy);
  MTLK_ASSERT(NULL != cfg80211);
  return wave_radio_sta_del(cfg80211->radio, ndev, mac);
}

static int API_WRAPPER(_wave_cfg80211_op_del_station) (
  struct wiphy *wiphy,
  struct net_device *ndev,
  struct station_del_parameters *params)
{
  int res;
  int retry_times = 0;

  do {
    res = _wave_cfg80211_op_del_station(wiphy, ndev, params->mac);

    retry_times++;
  } while ((res != LINUX_ERR_OK) && (retry_times < MAX_RETRY_TIMES));

  return res;
}

static int _wave_cfg80211_op_get_station (
  struct wiphy *wiphy,
  struct net_device *ndev,
  const uint8 *mac,
  struct station_info *sinfo)
{
  wv_cfg80211_t *cfg80211 = wiphy_priv(wiphy);
  MTLK_ASSERT(NULL != cfg80211);
  return wave_radio_sta_get(cfg80211->radio, ndev, mac, sinfo);
}

static int API_WRAPPER(_wave_cfg80211_op_get_station) (
  struct wiphy *wiphy,
  struct net_device *ndev,
  const uint8 *mac,
  struct station_info *sinfo)
{
  int res;
  int retry_times = 0;

  do {
    res = _wave_cfg80211_op_get_station(wiphy, ndev, mac, sinfo);

    retry_times++;
  } while ((res != LINUX_ERR_OK) && (retry_times < MAX_RETRY_TIMES));

  return res;
}

static int _wave_cfg80211_op_change_bss (
  struct wiphy *wiphy,
  struct net_device *ndev,
  struct bss_parameters *params)
{
  wv_cfg80211_t *cfg80211 = wiphy_priv(wiphy);
  MTLK_ASSERT(NULL != cfg80211);
  return wave_radio_bss_change(cfg80211->radio, ndev, params);
}

static int API_WRAPPER(_wave_cfg80211_op_change_bss) (
  struct wiphy *wiphy,
  struct net_device *ndev,
  struct bss_parameters *params)
{
  int res;
  int retry_times = 0;

  do {
    res = _wave_cfg80211_op_change_bss(wiphy, ndev, params);

    retry_times++;
  } while ((res != LINUX_ERR_OK) && (retry_times < MAX_RETRY_TIMES));

  return res;
}

static int _wave_cfg80211_op_mgmt_tx (
  struct wiphy *wiphy,
  struct wireless_dev *wdev,
  struct ieee80211_channel *chan,
  bool offchan,
  unsigned int wait, /* duration for remaining on channel in case it gets switched */
  const uint8 *buf,
  size_t len,
  bool no_cck,
  bool dont_wait_for_ack,
  uint64 *cookie)
{
  wv_cfg80211_t *cfg80211 = wiphy_priv(wiphy);
  MTLK_ASSERT(NULL != cfg80211);
  return wave_radio_mgmt_tx(cfg80211->radio, wdev->netdev, chan, offchan, wait, buf, len, no_cck, dont_wait_for_ack, cookie);
}

static int API_WRAPPER(_wave_cfg80211_op_mgmt_tx) (
  struct wiphy *wiphy,
  struct wireless_dev *wdev,
  struct cfg80211_mgmt_tx_params *params,
  uint64 *cookie)
{
  int res;
  int retry_times = 0;

  do {
    res = _wave_cfg80211_op_mgmt_tx(wiphy, wdev, params->chan, params->offchan, params->wait, params->buf,
                                    params->len, params->no_cck, params->dont_wait_for_ack, cookie);

    retry_times++;
  } while ((res != LINUX_ERR_OK) && (retry_times < MAX_RETRY_TIMES));

  return res;
}



static void
_wave_cfg80211_op_mgmt_frame_register (
  struct wiphy *wiphy,
  struct wireless_dev *wdev,
  uint16 frame_type,
  bool reg)
{
  wv_cfg80211_t *cfg80211 = wiphy_priv(wiphy);
  MTLK_ASSERT(NULL != cfg80211);
  wave_radio_mgmt_frame_register(cfg80211->radio, wdev->netdev, frame_type, reg);
}

static void API_WRAPPER(_wave_cfg80211_op_mgmt_frame_register) (
  struct wiphy *wiphy,
  struct wireless_dev *wdev,
  uint16 frame_type,
  bool reg)
{
  _wave_cfg80211_op_mgmt_frame_register(wiphy, wdev, frame_type, reg);

  return;
}

static int _wave_cfg80211_op_scan(struct wiphy *wiphy, struct cfg80211_scan_request *request)
{
  wv_cfg80211_t *cfg80211 = wiphy_priv(wiphy);
  MTLK_ASSERT(NULL != cfg80211);
  return wave_radio_scan(cfg80211->radio, request);
}

static int API_WRAPPER(_wave_cfg80211_op_scan) (
    struct wiphy *wiphy,
    struct cfg80211_scan_request *request)
{
  int res;
  int retry_times = 0;

  do {
    res = _wave_cfg80211_op_scan(wiphy, request);

    retry_times++;
  } while ((res != LINUX_ERR_OK) && (retry_times < MAX_RETRY_TIMES));

  return res;
}

static int _wave_cfg80211_op_sched_scan_start(
    struct wiphy *wiphy,
    struct net_device *ndev,
    struct cfg80211_sched_scan_request *request)
{
  wv_cfg80211_t *cfg80211 = wiphy_priv(wiphy);
  MTLK_ASSERT(NULL != cfg80211);
  return wave_radio_sched_scan_start(cfg80211->radio, ndev, request);
}

static int API_WRAPPER(_wave_cfg80211_op_sched_scan_start) (
    struct wiphy *wiphy,
    struct net_device *ndev,
    struct cfg80211_sched_scan_request *request)
{
  int res;
  int retry_times = 0;

  do {
    res = _wave_cfg80211_op_sched_scan_start(wiphy, ndev, request);

    retry_times++;
  } while ((res != LINUX_ERR_OK) && (retry_times < MAX_RETRY_TIMES));

  return res;
}

static int _wave_cfg80211_op_sched_scan_stop(
    struct wiphy *wiphy,
    struct net_device *ndev,
    u64 reqid)
{
  wv_cfg80211_t *cfg80211 = wiphy_priv(wiphy);
  MTLK_ASSERT(NULL != cfg80211);
  return wave_radio_sched_scan_stop(cfg80211->radio, ndev, reqid);
}

static int API_WRAPPER(_wave_cfg80211_op_sched_scan_stop) (
    struct wiphy *wiphy,
    struct net_device *ndev,
    u64 reqid)
{
  int res;
  int retry_times = 0;

  do {
    res = _wave_cfg80211_op_sched_scan_stop(wiphy, ndev, reqid);

    retry_times++;
  } while ((res != LINUX_ERR_OK) && (retry_times < MAX_RETRY_TIMES));

  return res;
}

static int _wave_cfg80211_op_auth (
  struct wiphy *wiphy,
  struct net_device *ndev,
  struct cfg80211_auth_request *req)
{
  /* FIXCFG80211: */
  ILOG0_V("TODO: AUTH");
  return _mtlk_df_mtlk_to_linux_error_code(MTLK_ERR_OK);
}

static int API_WRAPPER(_wave_cfg80211_op_auth) (
  struct wiphy *wiphy,
  struct net_device *ndev,
  struct cfg80211_auth_request *req)
{
  int res;
  int retry_times = 0;

  do {
    res = _wave_cfg80211_op_auth(wiphy, ndev, req);

    retry_times++;
  } while ((res != LINUX_ERR_OK) && (retry_times < MAX_RETRY_TIMES));

  return res;
}

static int _wave_cfg80211_op_deauth (
  struct wiphy *wiphy,
  struct net_device *ndev,
  struct cfg80211_deauth_request *req)
{
  /* FIXCFG80211: */
  ILOG0_V("TODO: DEAUTH");
  return _mtlk_df_mtlk_to_linux_error_code(MTLK_ERR_OK);
}

static int API_WRAPPER(_wave_cfg80211_op_deauth) (
  struct wiphy *wiphy,
  struct net_device *ndev,
  struct cfg80211_deauth_request *req)
{
  int res;
  int retry_times = 0;

  do {
    res = _wave_cfg80211_op_deauth(wiphy, ndev, req);

    retry_times++;
  } while ((res != LINUX_ERR_OK) && (retry_times < MAX_RETRY_TIMES));

  return res;
}

static int _wave_cfg80211_op_assoc (
  struct wiphy *wiphy,
  struct net_device *ndev,
  struct cfg80211_assoc_request *req)
{
  /* FIXCFG80211: */
  ILOG0_V("TODO: ASSOC");
  return _mtlk_df_mtlk_to_linux_error_code(MTLK_ERR_OK);
}

static int API_WRAPPER(_wave_cfg80211_op_assoc) (
  struct wiphy *wiphy,
  struct net_device *ndev,
  struct cfg80211_assoc_request *req)
{
  int res;
  int retry_times = 0;

  do {
    res = _wave_cfg80211_op_assoc(wiphy, ndev, req);

    retry_times++;
  } while ((res != LINUX_ERR_OK) && (retry_times < MAX_RETRY_TIMES));

  return res;
}

static int _wave_cfg80211_op_disassoc (
  struct wiphy *wiphy,
  struct net_device *ndev,
  struct cfg80211_disassoc_request *req)
{
  /* FIXCFG80211: */
  ILOG0_V("TODO: DISASSOC");
  return _mtlk_df_mtlk_to_linux_error_code(MTLK_ERR_OK);
}

static int API_WRAPPER(_wave_cfg80211_op_disassoc) (
  struct wiphy *wiphy,
  struct net_device *ndev,
  struct cfg80211_disassoc_request *req)
{
  int res;
  int retry_times = 0;

  do {
    res = _wave_cfg80211_op_disassoc(wiphy, ndev, req);

    retry_times++;
  } while ((res != LINUX_ERR_OK) && (retry_times < MAX_RETRY_TIMES));

  return res;
}

static int _wave_cfg80211_op_set_wiphy_params (
  struct wiphy *wiphy,
  uint32 changed)
{
  wv_cfg80211_t *cfg80211 = wiphy_priv(wiphy);
  MTLK_ASSERT(NULL != cfg80211);
  return wave_radio_wiphy_params_set(wiphy, cfg80211->radio, changed);
}

static int API_WRAPPER(_wave_cfg80211_op_set_wiphy_params) (
  struct wiphy *wiphy,
  uint32 changed)
{
  int res;
  int retry_times = 0;

  do {
    res = _wave_cfg80211_op_set_wiphy_params(wiphy, changed);

    retry_times++;
  } while ((res != LINUX_ERR_OK) && (retry_times < MAX_RETRY_TIMES));

  return res;
}

static int _wave_cfg80211_op_probe_client (
  struct wiphy *wiphy,
  struct net_device *ndev,
  const uint8 *peer,
  uint64 *cookie)
{
  wv_cfg80211_t *cfg80211 = wiphy_priv(wiphy);
  MTLK_ASSERT(NULL != cfg80211);
  return wave_radio_client_probe(cfg80211->radio, ndev, peer, cookie);
}

static int API_WRAPPER(_wave_cfg80211_op_probe_client) (
  struct wiphy *wiphy,
  struct net_device *ndev,
  const uint8 *peer,
  uint64 *cookie)
{
  int res;
  int retry_times = 0;

  do {
    res = _wave_cfg80211_op_probe_client(wiphy, ndev, peer, cookie);

    retry_times++;
  } while ((res != LINUX_ERR_OK) && (retry_times < MAX_RETRY_TIMES));

  return res;
}

static int _wave_cfg80211_op_add_key (
  struct wiphy *wiphy,
  struct net_device *ndev,
  uint8 key_index,
  bool pairwise,
  const uint8 *mac_addr,
  struct key_params *params)
{
  wv_cfg80211_t *cfg80211 = wiphy_priv(wiphy);
  MTLK_ASSERT(NULL != cfg80211);
  return wave_radio_key_add(cfg80211->radio, ndev, key_index, pairwise, mac_addr, params);
}

static int API_WRAPPER(_wave_cfg80211_op_add_key) (
  struct wiphy *wiphy,
  struct net_device *ndev,
  uint8 key_index,
  bool pairwise,
  const uint8 *mac_addr,
  struct key_params *params)
{
  int res;
  int retry_times = 0;

  do {
    res = _wave_cfg80211_op_add_key(wiphy, ndev, key_index, pairwise, mac_addr, params);

    retry_times++;
  } while ((res != LINUX_ERR_OK) && (retry_times < MAX_RETRY_TIMES));

  return res;
}

static int _wave_cfg80211_op_del_key (
struct wiphy *wiphy,
struct net_device *ndev,
  uint8 key_index,
  bool pairwise,
  const uint8 *mac_addr)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", ndev->name, current->comm, current->pid);
  ILOG2_DD("pairwise = %d key_index = %d ", pairwise, key_index);
  mac_addr = mac_addr ? mac_addr : _bcast_addr ;
  ILOG2_Y(" mac_addr = %Y", mac_addr);

  /* FW doesn't support delete key */
  return _mtlk_df_mtlk_to_linux_error_code(MTLK_ERR_OK);
}

static int API_WRAPPER( _wave_cfg80211_op_del_key) (
struct wiphy *wiphy,
struct net_device *ndev,
  uint8 key_index,
  bool pairwise,
  const uint8 *mac_addr)
{
  int res;
  int retry_times = 0;

  do {
    res = _wave_cfg80211_op_del_key(wiphy, ndev, key_index, pairwise, mac_addr);

    retry_times++;
  } while ((res != LINUX_ERR_OK) && (retry_times < MAX_RETRY_TIMES));

  return res;
}

static int _wave_cfg80211_op_get_key (
  struct wiphy *wiphy,
  struct net_device *ndev,
  uint8 key_index,
  bool pairwise,
  const uint8 *mac_addr,
  void *cookie,
  void (*callback)(void *cookie, struct key_params*))
{
  wv_cfg80211_t *cfg80211 = wiphy_priv(wiphy);
  MTLK_ASSERT(NULL != cfg80211);
  return wave_radio_key_get(cfg80211->radio, ndev, key_index, pairwise, mac_addr, cookie, callback);
}

static int API_WRAPPER(_wave_cfg80211_op_get_key) (
  struct wiphy *wiphy,
  struct net_device *ndev,
  uint8 key_index,
  bool pairwise,
  const uint8 *mac_addr,
  void *cookie,
  void (*callback)(void *cookie, struct key_params*))
{
  int res;
  int retry_times = 0;

  do {
    res = _wave_cfg80211_op_get_key(wiphy, ndev, key_index, pairwise, mac_addr, cookie, callback);

    retry_times++;
  } while ((res != LINUX_ERR_OK) && (retry_times < MAX_RETRY_TIMES));

  return res;
}

static int _wave_cfg80211_op_set_default_key (
  struct wiphy *wiphy,
  struct net_device *ndev,
  uint8 key_index,
  bool unicast,
  bool multicast)
{
  wv_cfg80211_t *cfg80211 = wiphy_priv(wiphy);
  MTLK_ASSERT(NULL != cfg80211);
  return wave_radio_default_key_set(cfg80211->radio, ndev, key_index, unicast, multicast);
}

static int API_WRAPPER(_wave_cfg80211_op_set_default_key) (
  struct wiphy *wiphy,
  struct net_device *ndev,
  uint8 key_index,
  bool unicast,
  bool multicast)
{
  int res;
  int retry_times = 0;

  do {
    res = _wave_cfg80211_op_set_default_key(wiphy, ndev, key_index, unicast, multicast);

    retry_times++;
  } while ((res != LINUX_ERR_OK) && (retry_times < MAX_RETRY_TIMES));

  return res;
}

static int _wave_cfg80211_op_set_txq_params (
  struct wiphy *wiphy,
  struct net_device *ndev,
  struct ieee80211_txq_params *params)
{
  wv_cfg80211_t *cfg80211 = wiphy_priv(wiphy);
  MTLK_ASSERT(NULL != cfg80211);
  return wave_radio_txq_params_set(cfg80211->radio, ndev, params);
}

static int API_WRAPPER(_wave_cfg80211_op_set_txq_params) (
  struct wiphy *wiphy,
  struct net_device *ndev,
  struct ieee80211_txq_params *params)
{
  int res;
  int retry_times = 0;

  do {
    res = _wave_cfg80211_op_set_txq_params(wiphy, ndev, params);

    retry_times++;
  } while ((res != LINUX_ERR_OK) && (retry_times < MAX_RETRY_TIMES));

  return res;
}

static int _wave_cfg80211_op_dump_survey(
    struct wiphy *wiphy,
    struct net_device *ndev,
    int idx,
    struct survey_info *info)
{
  wv_cfg80211_t *cfg80211 = wiphy_priv(wiphy);
  MTLK_ASSERT(NULL != cfg80211);
  return wave_radio_survey_dump(wiphy, cfg80211->radio, ndev, idx, info);
}

static int API_WRAPPER(_wave_cfg80211_op_dump_survey) (
    struct wiphy *wiphy,
    struct net_device *ndev,
    int idx,
    struct survey_info *info)
{
  int res;
  int retry_times = 0;

  do {
    res = _wave_cfg80211_op_dump_survey(wiphy, ndev, idx, info);

    retry_times++;
  } while ((res != LINUX_ERR_OK) && (retry_times < MAX_RETRY_TIMES));

  return res;
}

static int _wave_cfg80211_op_start_radar_detection(struct wiphy *wiphy,
                                  struct net_device *ndev,
                                  struct cfg80211_chan_def *chandef,
                                  u32 cac_time_ms)
{
  wv_cfg80211_t *cfg80211 = wiphy_priv(wiphy);
  MTLK_ASSERT(NULL != cfg80211);
  return wave_radio_radar_detection_start(wiphy, cfg80211->radio, ndev, chandef, cac_time_ms);
}

static int API_WRAPPER(_wave_cfg80211_op_start_radar_detection)(
    struct wiphy *wiphy,
    struct net_device *ndev,
    struct cfg80211_chan_def *chandef,
    u32 cac_time_ms)
{
  int res;
  int retry_times = 0;

  do {
    res = _wave_cfg80211_op_start_radar_detection(wiphy, ndev, chandef, cac_time_ms);

    retry_times++;
  } while ((res != LINUX_ERR_OK) && (retry_times < MAX_RETRY_TIMES));

  return res;
}

/* HostAPD will call this for every VAP. Every time we'll do a set_channel (type CSA) and set_beacon.
 * However, set_channel will go through only the first time, as it's done for all VAPs simultaneously.
 */
static int _wave_cfg80211_op_channel_switch(
    struct wiphy *wiphy,
    struct net_device *ndev,
    struct cfg80211_csa_settings *csas)
{
  wv_cfg80211_t *cfg80211 = wiphy_priv(wiphy);
  MTLK_ASSERT(NULL != cfg80211);
  return wave_radio_channel_switch(cfg80211->radio, ndev, csas);
}

static int API_WRAPPER(_wave_cfg80211_op_channel_switch) (
    struct wiphy *wiphy,
    struct net_device *ndev,
    struct cfg80211_csa_settings *csas)
{
  int res;
  int retry_times = 0;

  do {
    res = _wave_cfg80211_op_channel_switch(wiphy, ndev, csas);

    retry_times++;
  } while ((res != LINUX_ERR_OK) && (retry_times < MAX_RETRY_TIMES));

  return res;
}

static int _wave_cfg80211_op_set_qos_map(struct wiphy *wiphy, struct net_device *ndev, struct cfg80211_qos_map *qos_map)
{
  wv_cfg80211_t *cfg80211 = wiphy_priv(wiphy);
  MTLK_ASSERT(NULL != cfg80211);
  return wave_radio_qos_map_set(cfg80211->radio, ndev, qos_map);
}

static int API_WRAPPER(_wave_cfg80211_op_set_qos_map)(
    struct wiphy *wiphy,
    struct net_device *ndev,
    struct cfg80211_qos_map *qos_map)
{
  int res;
  int retry_times = 0;

  do {
    res = _wave_cfg80211_op_set_qos_map(wiphy, ndev, qos_map);

    retry_times++;
  } while ((res != LINUX_ERR_OK) && (retry_times < MAX_RETRY_TIMES));

  return res;
}

static int _wave_cfg80211_op_set_default_mgmt_key (
    struct wiphy *wiphy,
    struct net_device *netdev,
    u8 key_index)
{
  wv_cfg80211_t *cfg80211 = wiphy_priv(wiphy);
  MTLK_ASSERT(NULL != cfg80211);
  return wave_radio_default_mgmt_key_set(cfg80211->radio, netdev, key_index);
}

static int API_WRAPPER(_wave_cfg80211_op_set_default_mgmt_key) (
    struct wiphy *wiphy,
    struct net_device *ndev,
    u8 key_index)
{
  int res;
  int retry_times = 0;

  do {
    res = _wave_cfg80211_op_set_default_mgmt_key(wiphy, ndev, key_index);

    retry_times++;
  } while ((res != LINUX_ERR_OK) && (retry_times < MAX_RETRY_TIMES));

  return res;
}

static int _wave_cfg80211_op_dump_station(
    struct wiphy *wiphy,
    struct net_device *ndev,
    int idx,
    u8 *mac,
    struct station_info *st_info)
{
  wv_cfg80211_t *cfg80211 = wiphy_priv(wiphy);
  MTLK_ASSERT(NULL != cfg80211);
  return wave_radio_station_dump(cfg80211->radio, ndev, idx, mac, st_info);
}

static int API_WRAPPER(_wave_cfg80211_op_dump_station) (
    struct wiphy *wiphy,
    struct net_device *ndev,
    int idx,
    u8 *mac,
    struct station_info *sinfo)
{
  int res;
  int retry_times = 0;

  do {
    res = _wave_cfg80211_op_dump_station(wiphy, ndev, idx, mac, sinfo);

    retry_times++;
  } while ((res != LINUX_ERR_OK) && (retry_times < MAX_RETRY_TIMES));

  return res;
}

static int _wave_cfg80211_op_set_antenna(struct wiphy *wiphy, u32 tx_ant, u32 rx_ant)
{
  wv_cfg80211_t *cfg80211 = wiphy_priv(wiphy);
  MTLK_ASSERT(NULL != cfg80211);
  return wave_radio_antenna_set(cfg80211->radio, tx_ant, rx_ant,
                                wiphy->available_antennas_tx, wiphy->available_antennas_rx);
}

static int API_WRAPPER(_wave_cfg80211_op_set_antenna)(
    struct wiphy *wiphy,
    u32 tx_ant,
    u32 rx_ant)
{
  int res;
  int retry_times = 0;

  do {
    res = _wave_cfg80211_op_set_antenna(wiphy, tx_ant, rx_ant);

    retry_times++;
  } while ((res != LINUX_ERR_OK) && (retry_times < MAX_RETRY_TIMES));

  return res;
}

static int _wave_cfg80211_op_get_antenna(struct wiphy *wiphy, u32 *tx_ant, u32 *rx_ant)
{
  wv_cfg80211_t *cfg80211 = wiphy_priv(wiphy);
  MTLK_ASSERT(NULL != cfg80211);
  return wave_radio_antenna_get(cfg80211->radio, tx_ant, rx_ant,
                                wiphy->available_antennas_tx, wiphy->available_antennas_rx);
}

static int API_WRAPPER(_wave_cfg80211_op_get_antenna)(
    struct wiphy *wiphy,
    u32 *tx_ant,
    u32 *rx_ant)
{
  int res;
  int retry_times = 0;

  do {
    res = _wave_cfg80211_op_get_antenna(wiphy, tx_ant, rx_ant);

    retry_times++;
  } while ((res != LINUX_ERR_OK) && (retry_times < MAX_RETRY_TIMES));

  return res;
}

static struct cfg80211_ops wv_cfg80211_ops = {
  .add_virtual_intf = API_WRAPPER(_wave_cfg80211_op_add_virtual_iface),
  .del_virtual_intf = API_WRAPPER(_wave_cfg80211_op_del_virtual_iface),
  .change_virtual_intf = API_WRAPPER(_wave_cfg80211_op_change_virtual_intf),
  .start_ap = API_WRAPPER(_wave_cfg80211_op_start_ap),
  .change_beacon = API_WRAPPER(_wave_cfg80211_op_change_beacon),
  .stop_ap = API_WRAPPER(_wave_cfg80211_op_stop_ap),
  .add_station = API_WRAPPER(_wave_cfg80211_op_add_station),
  .del_station = API_WRAPPER(_wave_cfg80211_op_del_station),
  .change_station = API_WRAPPER(_wave_cfg80211_op_change_station),
  .get_station = API_WRAPPER(_wave_cfg80211_op_get_station),
  .dump_station = API_WRAPPER(_wave_cfg80211_op_dump_station),
  .change_bss = API_WRAPPER(_wave_cfg80211_op_change_bss),
  .scan = API_WRAPPER(_wave_cfg80211_op_scan),
  .auth = API_WRAPPER(_wave_cfg80211_op_auth),
  .assoc = API_WRAPPER(_wave_cfg80211_op_assoc),
  .deauth = API_WRAPPER(_wave_cfg80211_op_deauth),
  .disassoc = API_WRAPPER(_wave_cfg80211_op_disassoc),
  .set_wiphy_params = API_WRAPPER(_wave_cfg80211_op_set_wiphy_params),
  .mgmt_tx = API_WRAPPER(_wave_cfg80211_op_mgmt_tx),
  .mgmt_frame_register = API_WRAPPER(_wave_cfg80211_op_mgmt_frame_register),
  .sched_scan_start = API_WRAPPER(_wave_cfg80211_op_sched_scan_start),
  .sched_scan_stop = API_WRAPPER(_wave_cfg80211_op_sched_scan_stop),
  .probe_client = API_WRAPPER(_wave_cfg80211_op_probe_client),
  .add_key = API_WRAPPER(_wave_cfg80211_op_add_key),
  .del_key = API_WRAPPER(_wave_cfg80211_op_del_key),
  .get_key = API_WRAPPER(_wave_cfg80211_op_get_key),
  .set_default_key = API_WRAPPER(_wave_cfg80211_op_set_default_key),
  .set_default_mgmt_key = API_WRAPPER(_wave_cfg80211_op_set_default_mgmt_key),
  .set_txq_params = API_WRAPPER(_wave_cfg80211_op_set_txq_params),
  .set_ap_chanwidth = API_WRAPPER(_wave_cfg80211_op_set_ap_chanwidth),
  .dump_survey = API_WRAPPER(_wave_cfg80211_op_dump_survey),
  .start_radar_detection = API_WRAPPER(_wave_cfg80211_op_start_radar_detection),
  .channel_switch = API_WRAPPER(_wave_cfg80211_op_channel_switch),
  .set_qos_map = API_WRAPPER(_wave_cfg80211_op_set_qos_map),
  .set_antenna = API_WRAPPER(_wave_cfg80211_op_set_antenna),
  .get_antenna = API_WRAPPER(_wave_cfg80211_op_get_antenna),
};

/* Send flush stations request to Hostapd. */
int __MTLK_IFUNC
wv_cfg80211_handle_flush_stations(
  mtlk_vap_handle_t vap_handle,
  void *data, int data_len)
{
  mtlk_df_t *df               = mtlk_vap_get_df(vap_handle);
  mtlk_df_user_t *df_user     = mtlk_df_get_user(df);
  struct wireless_dev *wdev   = mtlk_df_user_get_wdev(df_user);
  mtlk_nbuf_t *evt_nbuf;

  MTLK_ASSERT(NULL != wdev);

  evt_nbuf = wv_cfg80211_vendor_event_alloc(wdev, data_len,
                                            LTQ_NL80211_VENDOR_EVENT_FLUSH_STATIONS);
  if (!evt_nbuf)
  {
    ELOG_D("Malloc event fail. data_len = %d", data_len);
    return MTLK_ERR_NO_MEM;
  }

  if (data != NULL && data_len > 0) {
    uint8 *cp = mtlk_df_nbuf_put(evt_nbuf, data_len);
    wave_memcpy(cp, data_len, data, data_len);
  }
  wv_cfg80211_vendor_event(evt_nbuf);

  return MTLK_ERR_OK;
}

int __MTLK_IFUNC
wv_cfg80211_handle_get_unconnected_sta (struct wireless_dev *wdev, void *data, int data_len)
{
  mtlk_nbuf_t *evt_nbuf;
  uint8 *cp;

  MTLK_ASSERT(NULL != wdev);

  evt_nbuf = wv_cfg80211_vendor_event_alloc(wdev, data_len,
                                            LTQ_NL80211_VENDOR_EVENT_UNCONNECTED_STA);
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

/**
  FIXCFG80211: description
*/
void __MTLK_IFUNC
wv_cfg80211_destroy (
  wv_cfg80211_t *cfg80211)
{
  MTLK_ASSERT(cfg80211 != NULL);
  if (cfg80211 == NULL) {
    return;
  }

  if (cfg80211->registered) {
    wv_cfg80211_unregister(cfg80211);
  }

  wiphy_free(cfg80211->wiphy);
}

/**
  FIXCFG80211: description
*/
wv_cfg80211_t* __MTLK_IFUNC
wv_cfg80211_create (void)
{
  wv_cfg80211_t *cfg80211;
  struct wiphy *wiphy;

  /* allocate Linux Wireless Hardware with cfg80211 in private section */
  wiphy = wiphy_new(&wv_cfg80211_ops, sizeof(wv_cfg80211_t));
  if (!wiphy) {
    ELOG_V("Couldn't allocate WIPHY device");
    return NULL;
  }

  _wave_cfg80211_register_vendor_cmds(wiphy);
  _wave_cfg80211_register_vendor_evts(wiphy);

#ifdef CPTCFG_CFG80211_WEXT
  wiphy->wext = (struct iw_handler_def *)&wave_linux_iw_radio_handler_def;
#endif /* CPTCFG_CFG80211_WEXT */

  /* retrieve cfg80211 handle */
  cfg80211 = wiphy_priv(wiphy);

  memset(cfg80211, 0, sizeof(*cfg80211));

  /* link cfg80211 with WIPHY */
  cfg80211->wiphy = wiphy;

  return cfg80211;
}

static void set_ldpc_cap(mtlk_handle_t hw_handle, struct ieee80211_supported_band *cfg80211_band, char *band_str)
{
  BOOL ldpc_support = hw_get_ldpc_support(hw_handle);
  ILOG1_DS("LDPC support is %d for %s band", (int)ldpc_support, band_str);
  if (ldpc_support) {
    cfg80211_band->ht_cap.cap |= IEEE80211_HT_CAP_LDPC_CODING;
    cfg80211_band->vht_cap.cap |= IEEE80211_VHT_CAP_RXLDPC;
  } else {
    cfg80211_band->ht_cap.cap &= ~IEEE80211_HT_CAP_LDPC_CODING;
    cfg80211_band->vht_cap.cap &= ~IEEE80211_VHT_CAP_RXLDPC;
  }
}

static void set_ampdu_factor(mtlk_handle_t hw_handle, struct ieee80211_supported_band *cfg80211_band, char *band_str)
{
  BOOL ampdu_64k = hw_get_ampdu_64k_support(hw_handle);
  ILOG1_SS("AMPDU factor %s is supported for %s band", ampdu_64k ? "64k" : "32k", band_str);
  if (ampdu_64k)
    cfg80211_band->ht_cap.ampdu_factor = IEEE80211_HT_MAX_AMPDU_64K;
  else
    cfg80211_band->ht_cap.ampdu_factor = IEEE80211_HT_MAX_AMPDU_32K;
}

static void set_ampdu_density(mtlk_handle_t hw_handle, struct ieee80211_supported_band *cfg80211_band, char *band_str)
{
  BOOL ampdu_density = hw_get_ampdu_density_restriction(hw_handle);
  ILOG1_SS("AMPDU density has %s restriction for %s band", ampdu_density ? "16 usec" : "no", band_str);
  cfg80211_band->ht_cap.ampdu_density = ampdu_density ? IEEE80211_HT_MPDU_DENSITY_16 : IEEE80211_HT_MPDU_DENSITY_NONE;
}

static void set_rx_stbc(mtlk_handle_t hw_handle, struct ieee80211_supported_band *cfg80211_band, char *band_str)
{
  BOOL rx_stbc = hw_get_rx_stbc_support(hw_handle);
  ILOG1_SS("RX STBC %s supported for %s band", rx_stbc ? "is" : "not", band_str);
  cfg80211_band->ht_cap.cap &= ~IEEE80211_HT_CAP_RX_STBC;
  cfg80211_band->vht_cap.cap &= ~IEEE80211_VHT_CAP_RXSTBC_MASK;
  if (rx_stbc) {
    cfg80211_band->ht_cap.cap |= (WV_SUPP_RX_STBC << IEEE80211_HT_CAP_RX_STBC_SHIFT);
    cfg80211_band->vht_cap.cap |= IEEE80211_VHT_CAP_RXSTBC_1;
  }
}

static void set_vht_support(mtlk_handle_t hw_handle, struct ieee80211_supported_band *cfg80211_band, char *band_str)
{
  BOOL is_supported = hw_get_vht_support(hw_handle);

  ILOG1_SS("vht %s supported for %s band", is_supported ? "is" : "not", band_str);

  cfg80211_band->vht_cap.vht_supported = is_supported;
}

static void set_vht_160mhz_short_gi (mtlk_handle_t hw_handle, struct ieee80211_supported_band *cfg80211_band, char *band_str)
{
  BOOL vht_160mhz_short_gi_support = hw_get_160mhz_short_gi_support(hw_handle);

  ILOG1_SS("160Mhz Short GI %s supported for %s band", vht_160mhz_short_gi_support ? "is" : "not", band_str);
  if (vht_160mhz_short_gi_support) {
    cfg80211_band->vht_cap.cap |= IEEE80211_VHT_CAP_SHORT_GI_160;
  } else {
    cfg80211_band->vht_cap.cap &= ~IEEE80211_VHT_CAP_SHORT_GI_160;
  }
}

static void set_vht_cap_160mhz (mtlk_handle_t hw_handle, struct ieee80211_supported_band *cfg80211_band, char *band_str)
{
  BOOL vht_160_mhz = hw_get_vht_160mhz_support(hw_handle);
  ILOG1_SS("160Mhz %s supported for %s band", vht_160_mhz ? "is" : "not", band_str);
  if (vht_160_mhz) {
    cfg80211_band->vht_cap.cap |= IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_160MHZ;
  } else {
    cfg80211_band->vht_cap.cap &= ~IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_160MHZ;
  }
}

static void set_he_support(mtlk_handle_t hw_handle, struct ieee80211_supported_band *cfg80211_band, char *band_str)
{
  BOOL is_supported = hw_get_he_support(hw_handle);

  ILOG1_SS("he %s supported for %s band", is_supported ? "is" : "not", band_str);

  cfg80211_band->he_cap.he_supported = is_supported;
}

static void
_wv_cfg80211_disable_hw_unsupported_channels (mtlk_hw_t *hw, struct ieee80211_supported_band *cfg80211_band)
{
  int i;
  struct ieee80211_channel *chan;
  MTLK_ASSERT(cfg80211_band);

  for (i = 0; i < cfg80211_band->n_channels; i++) {
    chan = &cfg80211_band->channels[i];
    if (!wave_hw_is_channel_supported(hw, chan->hw_value)) {
      ILOG1_D("Unsupported chan %u", chan->hw_value);
      chan->flags |= IEEE80211_CHAN_DISABLED;
    }
  }
}

static void
wv_cfg80211_update_he_capa(struct ieee80211_supported_band **sband, enum nl80211_band band, u32 num_sts)
{
  u32 idx;

  if(num_sts > 4) {
    ELOG_D("Unsupported STS:%d", num_sts);
    num_sts = 2;
  }

  ILOG2_D("Using number of STS:%d", num_sts);

  /* idx 0 for STS 1 and 2, idx 1 for STS 3 and 4 */
  idx = (num_sts - 1) >> 1;

  MTLK_ASSERT(idx < ARRAY_SIZE(_cfg80211_he_capa_2ghz));
  MTLK_ASSERT(idx < ARRAY_SIZE(_cfg80211_he_capa_5ghz));

  if (band == NL80211_BAND_2GHZ)
    (*sband)->iftype_data = &_cfg80211_he_capa_2ghz[idx];
  else if (band == NL80211_BAND_5GHZ)
    (*sband)->iftype_data = &_cfg80211_he_capa_5ghz[idx];
  else {
    ELOG_D("Not supported band:%d", band);
    (*sband)->n_iftype_data = 0;
    return;
  }

  (*sband)->n_iftype_data = 1;
}

/**
 * Helper for wv_cfg80211_init: initializes a band (2.4, 2.4_VHT or 5 GHz)
 */
static int wv_cfg80211_init_band(
  mtlk_handle_t hw_handle,
  struct        ieee80211_supported_band** cfg80211_band,
  struct        ieee80211_supported_band* cfg80211_band_template,
  struct        ieee80211_channel* cfg80211_channel_template,
  int           n_channels, /* Number of channels in channel template */
  u32           num_sts,    /* Number of spatial streams */
  char*         band_str)   /* To give context in log messages */
{
  int i;
  mtlk_hw_t *hw = HANDLE_T_PTR(mtlk_hw_t, hw_handle);
  size_t channels_size = sizeof(struct ieee80211_channel) * n_channels;

  MTLK_ASSERT(hw);

  /* Allocate memory for struct ieee80211_supported_band and copy initial data from a template */
  *cfg80211_band = mtlk_osal_mem_alloc(sizeof(struct ieee80211_supported_band), MTLK_MEM_TAG_CFG80211);
  if (*cfg80211_band == NULL)
    return MTLK_ERR_NO_MEM;
  **cfg80211_band = *cfg80211_band_template;

  set_ldpc_cap            (hw_handle, *cfg80211_band, band_str);
  set_ampdu_factor        (hw_handle, *cfg80211_band, band_str);
  set_ampdu_density       (hw_handle, *cfg80211_band, band_str);
  set_rx_stbc             (hw_handle, *cfg80211_band, band_str);
  set_vht_support         (hw_handle, *cfg80211_band, band_str);
  set_vht_cap_160mhz      (hw_handle, *cfg80211_band, band_str);
  set_vht_160mhz_short_gi (hw_handle, *cfg80211_band, band_str);
  set_he_support          (hw_handle, *cfg80211_band, band_str);

  /* Initialize the list of supported channels */
  (*cfg80211_band)->channels = mtlk_osal_mem_alloc(channels_size, MTLK_MEM_TAG_CFG80211);
  if ((*cfg80211_band)->channels == NULL)
    return MTLK_ERR_NO_MEM;
  wave_memcpy((*cfg80211_band)->channels, channels_size, cfg80211_channel_template, channels_size);
  (*cfg80211_band)->n_channels = n_channels;

  /* Disable HW unsupported channels */
  _wv_cfg80211_disable_hw_unsupported_channels(hw, *cfg80211_band);

#ifdef MTLK_DEBUG
  for (i = 0; i < (*cfg80211_band)->n_channels; i++) {
    ILOG2_DD("Ant mask per channel: Channel %d enabled %d", (*cfg80211_band)->channels[i].hw_value,
             !((*cfg80211_band)->channels[i].flags & IEEE80211_CHAN_DISABLED));
  }
#endif

  /* Set the rates achievable with more than 1 STS (1 was already set above, statically) */
  for (i = 2; i <= MIN(num_sts, IEEE80211_HT_MCS_LIMIT); i++) {
    (*cfg80211_band)->ht_cap.mcs.rx_mask[i-1] = 0xff;
  }

  for (i = 2; i <= MIN(num_sts, IEEE80211_VHT_MCS_LIMIT); i++) {
    (*cfg80211_band)->vht_cap.vht_mcs.rx_mcs_map &= ~(IEEE80211_VHT_MCS_NOT_SUPPORTED << WV_MCS_MAP_SS_SHIFT(i));
    (*cfg80211_band)->vht_cap.vht_mcs.rx_mcs_map |=  (IEEE80211_VHT_MCS_SUPPORT_0_9   << WV_MCS_MAP_SS_SHIFT(i));
    (*cfg80211_band)->vht_cap.vht_mcs.tx_mcs_map &= ~(IEEE80211_VHT_MCS_NOT_SUPPORTED << WV_MCS_MAP_SS_SHIFT(i));
    (*cfg80211_band)->vht_cap.vht_mcs.tx_mcs_map |=  (IEEE80211_VHT_MCS_SUPPORT_0_9   << WV_MCS_MAP_SS_SHIFT(i));
  }

  (*cfg80211_band)->vht_cap.cap |= ((num_sts - 1) << IEEE80211_VHT_CAP_BEAMFORMEE_STS_SHIFT);
  (*cfg80211_band)->vht_cap.cap |= ((num_sts - 1) << IEEE80211_VHT_CAP_SOUNDING_DIMENSIONS_SHIFT);

  ILOG0_SD("%s band supported, up to %i spatial streams", band_str, num_sts);
  return MTLK_ERR_OK;
}


int wv_cfg80211_update_supported_bands (mtlk_handle_t hw_handle, unsigned radio_id, struct wiphy *wiphy, u32 num_sts)
{
  if (mtlk_is_band_supported(hw_handle, radio_id, nlband2mtlkband(NL80211_BAND_2GHZ))) {
    int ret_code = wv_cfg80211_init_band(hw_handle, &wiphy->bands[NL80211_BAND_2GHZ], &_cfg80211_band_2ghz,
                   _cfg80211_2ghz_channels, ARRAY_SIZE(_cfg80211_2ghz_channels), num_sts, "2.4 GHz");

    if (ret_code != MTLK_ERR_OK)
      return ret_code;

    wv_cfg80211_update_he_capa(&wiphy->bands[NL80211_BAND_2GHZ], NL80211_BAND_2GHZ, num_sts);
  }

  if (mtlk_is_band_supported(hw_handle, radio_id, nlband2mtlkband(NL80211_BAND_5GHZ))) {
    int ret_code = wv_cfg80211_init_band(hw_handle, &wiphy->bands[NL80211_BAND_5GHZ], &_cfg80211_band_5ghz,
                   _cfg80211_5ghz_channels, ARRAY_SIZE(_cfg80211_5ghz_channels), num_sts, "5 GHz");

    if (ret_code != MTLK_ERR_OK)
      return ret_code;

    wv_cfg80211_update_he_capa(&wiphy->bands[NL80211_BAND_5GHZ], NL80211_BAND_5GHZ, num_sts);
  }

  if (!wiphy->bands[NL80211_BAND_2GHZ] && !wiphy->bands[NL80211_BAND_5GHZ])
  {
    ELOG_V("No bands supported, please check your EEPROM/calibration file");
    return MTLK_ERR_PARAMS;
  }

  return MTLK_ERR_OK;
}

/**
  FIXCFG80211: description
*/
int __MTLK_IFUNC
wv_cfg80211_init (
  wv_cfg80211_t *cfg80211,
  struct device *dev,
  void *radio_ctx,
  mtlk_handle_t hw_handle)
{
  struct wiphy *wiphy;
  u32 num_sts = WV_SUPP_NUM_STS; /* should get overwritten */
  wave_radio_t *radio = (wave_radio_t *)radio_ctx;
  unsigned radio_id;
  int res;

  MTLK_ASSERT(radio != NULL);
  if (radio == NULL) {
    return MTLK_ERR_PARAMS;
  }

  MTLK_ASSERT(cfg80211 != NULL);
  if (cfg80211 == NULL) {
    return MTLK_ERR_PARAMS;
  }

  wiphy = cfg80211->wiphy;
  MTLK_ASSERT(wiphy != NULL);
  if (wiphy == NULL) {
    return MTLK_ERR_PARAMS;
  }

  /* link cfg80211 with radio module */
  cfg80211->radio = radio;
  radio_id = wave_radio_id_get(radio);

  /* initialize WIPHY */
  /********************/
  wiphy->mgmt_stypes = wv_mgmt_stypes;

  /* set device pointer for WIPHY */
  set_wiphy_dev(wiphy, dev);

  wiphy->interface_modes = BIT(NL80211_IFTYPE_AP);

  hw_get_fw_version(hw_handle, wiphy->fw_version, sizeof(wiphy->fw_version));

  hw_get_hw_version(hw_handle, &wiphy->hw_version);

  /* TX/RX available antennas: bitmap of antennas which are available to be configured as TX/RX antenna */
  wave_radio_ant_masks_num_sts_get(radio, &wiphy->available_antennas_tx, &wiphy->available_antennas_rx, &num_sts);

  wiphy->bands[NL80211_BAND_2GHZ] = NULL;
  wiphy->bands[NL80211_BAND_5GHZ] = NULL;

  /* Bands and channels (not sure about the rates) must be duplicated for each wiphy
   * because upon regdomain changes the channel flags get updated by the kernel.
   * This leads to strange problems if multiple wiphies use the same set of channels.
   */
  res = wv_cfg80211_update_supported_bands(hw_handle, radio_id, wiphy, num_sts);
  if (res != MTLK_ERR_OK)
    return res;

  /* prepare radio bands tables only in case of AP */
  if (wiphy->bands[NL80211_BAND_2GHZ]) {
    wave_radio_band_set(radio, NL80211_BAND_2GHZ);
  }
  if (wiphy->bands[NL80211_BAND_5GHZ]) {
    wave_radio_band_set(radio, NL80211_BAND_5GHZ);
  }

  wiphy->signal_type = CFG80211_SIGNAL_TYPE_MBM;

  if(hw_get_gcmp_support(hw_handle)) {
    wiphy->cipher_suites = _cipher_suites_gcmp;
    wiphy->n_cipher_suites = ARRAY_SIZE(_cipher_suites_gcmp);
  } else {
    wiphy->cipher_suites = _cipher_suites;
    wiphy->n_cipher_suites = ARRAY_SIZE(_cipher_suites);
  }

  wiphy->iface_combinations = create_cfg80211_if_comb(hw_handle, radio);
  if(NULL == wiphy->iface_combinations)
  {
    return MTLK_ERR_NO_MEM;
  }
  wiphy->n_iface_combinations = CFG80211_AP_MAX_IF_COMB;

#if 0 /* Currently we do not support Wake-On-Wirless lan */
#ifdef CONFIG_PM
  /* FIXCFG80211: Does this feature supported by WAVE devices ? */
  wiphy->wowlan.flags = WIPHY_WOWLAN_MAGIC_PKT |
                        WIPHY_WOWLAN_DISCONNECT |
                        WIPHY_WOWLAN_GTK_REKEY_FAILURE |
                        WIPHY_WOWLAN_SUPPORTS_GTK_REKEY |
                        WIPHY_WOWLAN_EAP_IDENTITY_REQ |
                        WIPHY_WOWLAN_4WAY_HANDSHAKE;
  wiphy->wowlan.n_patterns = WV_WOW_MAX_PATTERNS;
  wiphy->wowlan.pattern_min_len = WV_WOW_MIN_PATTERN_LEN;
  wiphy->wowlan.pattern_max_len = WV_WOW_MAX_PATTERN_LEN;
#endif /* CONFIG_PM */
#endif

  /* set WIPHY flags */
  wiphy->flags |= (WIPHY_FLAG_REPORTS_OBSS
                   | WIPHY_FLAG_AP_PROBE_RESP_OFFLOAD
                   | WIPHY_FLAG_AP_UAPSD
                   | WIPHY_FLAG_HAS_CHANNEL_SWITCH);

  /* unset WIPHY flags */
  wiphy->flags &= ~(WIPHY_FLAG_HAVE_AP_SME |
                    WIPHY_FLAG_OFFCHAN_TX |
                    WIPHY_FLAG_HAS_REMAIN_ON_CHANNEL |
                    WIPHY_FLAG_SUPPORTS_TDLS |
                    WIPHY_FLAG_TDLS_EXTERNAL_SETUP |
                    WIPHY_FLAG_SUPPORTS_TDLS |
                    WIPHY_FLAG_SUPPORTS_FW_ROAM |
                    WIPHY_FLAG_MESH_AUTH |
                    WIPHY_FLAG_IBSS_RSN |
                    WIPHY_FLAG_4ADDR_AP |
                    WIPHY_FLAG_4ADDR_STATION
    );

  wiphy->regulatory_flags &= ~(REGULATORY_DISABLE_BEACON_HINTS |
	                           REGULATORY_CUSTOM_REG |
	                           REGULATORY_STRICT_REG);

  /* FIXCFG80211: Unset TMP or TBD */
  wiphy->flags &= ~(WIPHY_FLAG_CONTROL_PORT_PROTOCOL |
                    WIPHY_FLAG_NETNS_OK |
                    WIPHY_FLAG_PS_ON_BY_DEFAULT);

  /* set WIPHY features */
  wiphy->features = (NL80211_FEATURE_SK_TX_STATUS |
                     NL80211_FEATURE_LOW_PRIORITY_SCAN |
                     NL80211_FEATURE_AP_MODE_CHAN_WIDTH_CHANGE |
                     NL80211_FEATURE_STATIC_SMPS |
                     NL80211_FEATURE_SAE
    );
  /* unset WIPHY features */
  wiphy->features &= ~(NL80211_FEATURE_HT_IBSS);
  /* FIXCFG80211: Unset TMP or TBD */
  wiphy->features &= ~NL80211_FEATURE_INACTIVITY_TIMER;

  wiphy->max_sched_scan_reqs = 1;
  wiphy->max_scan_ssids = MAX_SCAN_SSIDS;
  wiphy->max_sched_scan_ssids = MAX_SCAN_SSIDS;
  wiphy->max_match_sets = MAX_MATCH_SETS;
  wiphy->max_scan_ie_len = MAX_SCAN_IE_LEN;
  wiphy->max_sched_scan_ie_len = MAX_SCAN_IE_LEN;

  wiphy->probe_resp_offload = (NL80211_PROBE_RESP_OFFLOAD_SUPPORT_WPS |
                               NL80211_PROBE_RESP_OFFLOAD_SUPPORT_WPS2);

  wiphy->privid = _wiphy_privid;

  wiphy->max_num_csa_counters = 2;

  return MTLK_ERR_OK;
}

/**
  FIXCFG80211: description
*/
void __MTLK_IFUNC
wv_cfg80211_unregister (
  wv_cfg80211_t *cfg80211)
{
  MTLK_ASSERT(cfg80211 != NULL);
  if (cfg80211 == NULL) {
    return;
  }

  if (!cfg80211->registered) {
    return;
  }

  /* unregister hardware */
  wiphy_unregister(cfg80211->wiphy);

  cfg80211->registered = FALSE;
}

/**
  FIXCFG80211: description
*/
int __MTLK_IFUNC
wv_cfg80211_register (
  wv_cfg80211_t *cfg80211)
{
  MTLK_ASSERT(cfg80211 != NULL);
  if (cfg80211 == NULL) {
    return MTLK_ERR_PARAMS;
  }

  if (cfg80211->registered) {
    return MTLK_ERR_OK;
  }

  /* register hardware */
  SLOG0(0, 0, wiphy, cfg80211->wiphy);
  if (0 != wiphy_register(cfg80211->wiphy)) {
    ELOG_V("Couldn't register WIPHY device");
    return MTLK_ERR_PARAMS;
  }

  if (cfg80211->wiphy->bands[NL80211_BAND_2GHZ]) {
    struct ieee80211_supported_band *band = cfg80211->wiphy->bands[NL80211_BAND_2GHZ];
    if(MTLK_ERR_OK != wave_radio_channel_table_build_2ghz(cfg80211->radio, band->channels, band->n_channels))
      return MTLK_ERR_PARAMS;
  }
  if (cfg80211->wiphy->bands[NL80211_BAND_5GHZ]) {
    struct ieee80211_supported_band *band = cfg80211->wiphy->bands[NL80211_BAND_5GHZ];
    if(MTLK_ERR_OK != wave_radio_channel_table_build_5ghz(cfg80211->radio, band->channels, band->n_channels))
      return MTLK_ERR_PARAMS;
  }

  wave_radio_channel_table_print(cfg80211->radio);

  cfg80211->registered = TRUE;

  return MTLK_ERR_OK;
}

/**
  FIXCFG80211: description
*/
void* __MTLK_IFUNC
wv_cfg80211_wiphy_get (
  wv_cfg80211_t *cfg80211)
{
  MTLK_ASSERT(cfg80211 != NULL);

  return (void *)cfg80211->wiphy;
}

wave_radio_t * __MTLK_IFUNC
wv_cfg80211_wave_radio_get (
       wv_cfg80211_t *cfg80211)
{
  MTLK_ASSERT(cfg80211 != NULL);

  return cfg80211->radio;
}


void __MTLK_IFUNC
wv_cfg80211_class3_error_notify (struct wireless_dev *wdev, const u8 *addr)
{
  cfg80211_rx_spurious_frame(wdev->netdev, addr, GFP_ATOMIC);
}

BOOL __MTLK_IFUNC
wv_cfg80211_mngmn_frame (struct wireless_dev *wdev, const u8 *data, int size, int freq, int sig_dbm, int snr_db, unsigned subtype)
{
  mtlk_df_user_t *df_user = mtlk_df_user_from_wdev(wdev);
  if (!df_user) return FALSE;

  if (mtlk_df_user_get_mngmnt_filter(df_user, subtype)) {
    return cfg80211_rx_mgmt(wdev, freq, DBM_TO_MBM(sig_dbm), snr_db, data, size, GFP_ATOMIC);
  } else {
    return TRUE;
  }
}

static void cfg80211_if_limit_cleanup(struct ieee80211_iface_limit *if_limit)
{
  if (NULL != if_limit)
  {
    mtlk_osal_mem_free(if_limit);
  }
}

static void cfg80211_if_comb_cleanup(struct ieee80211_iface_combination *if_comb)
{
  if (NULL != if_comb)
  {
    struct ieee80211_iface_limit *if_limit;
    if_limit = (struct ieee80211_iface_limit *) if_comb->limits;

    cfg80211_if_limit_cleanup(if_limit);
    mtlk_osal_mem_free(if_comb);
  }
}

void __MTLK_IFUNC
wv_cfg80211_cleanup(wv_cfg80211_t *cfg80211)
{
  struct ieee80211_supported_band **bands = cfg80211->wiphy->bands;
  struct ieee80211_iface_combination *if_comb;
  if_comb = (struct ieee80211_iface_combination*) cfg80211->wiphy->iface_combinations;

  cfg80211_if_comb_cleanup(if_comb);

  if (bands[NL80211_BAND_5GHZ])
  {
    if (bands[NL80211_BAND_5GHZ]->channels)
      mtlk_osal_mem_free(bands[NL80211_BAND_5GHZ]->channels);

    mtlk_osal_mem_free(bands[NL80211_BAND_5GHZ]);
  }

  if (bands[NL80211_BAND_2GHZ])
  {
    if (bands[NL80211_BAND_2GHZ]->channels)
      mtlk_osal_mem_free(bands[NL80211_BAND_2GHZ]->channels);

    mtlk_osal_mem_free(bands[NL80211_BAND_2GHZ]);
  }
}

static struct ieee80211_iface_limit *create_cfg80211_if_limits(wave_radio_t *radio)
{
  struct ieee80211_iface_limit *if_limits;
  int size = CFG80211_AP_MAX_IF_LIMIT * sizeof(struct ieee80211_iface_limit);

  if_limits = mtlk_osal_mem_alloc(size, MTLK_MEM_TAG_CFG80211);
  if (NULL == if_limits)
  {
    ELOG_V("create_cfg80211_if_limits: can't allocate memory");
    return NULL;
  }

  memset(if_limits, 0, size);

  /* Limitation on AP mode VAPs */
  if_limits->max = wave_radio_max_vaps_get(radio);
  if_limits->types = BIT(NL80211_IFTYPE_AP);

  return if_limits;
}

static struct ieee80211_iface_combination *create_cfg80211_if_comb(mtlk_handle_t hw_handle, wave_radio_t *radio)
{
  struct ieee80211_iface_combination *if_comb;
  struct ieee80211_iface_limit *if_limits;
  BOOL vht_160_mhz = hw_get_vht_160mhz_support(hw_handle);

  int size = CFG80211_AP_MAX_IF_COMB * sizeof(struct ieee80211_iface_combination);

  if_limits = create_cfg80211_if_limits(radio);
  if (NULL == if_limits)
  {
    return NULL;
  }

  if_comb = mtlk_osal_mem_alloc(size, MTLK_MEM_TAG_CFG80211);
  if (NULL == if_comb)
  {
    ELOG_V("create_cfg80211_if_comb: can't allocate memory");
    cfg80211_if_limit_cleanup(if_limits);
    return NULL;
  }
  memset(if_comb, 0, size);

  if_comb->limits = if_limits;
  if_comb->n_limits = CFG80211_AP_MAX_IF_LIMIT; /*APs only*/
  if_comb->max_interfaces = wave_radio_max_vaps_get(radio);
  /* During CSA with multiple VAPs, VAPs are switched one by one,
   * therefore during switch time they appear to be on two different channels */
  if_comb->num_different_channels = 2;
  if_comb->beacon_int_infra_match = 0;
  if_comb->radar_detect_widths = (1 << NL80211_CHAN_WIDTH_20_NOHT) | (1 << NL80211_CHAN_WIDTH_20)
                                 | (1 << NL80211_CHAN_WIDTH_40) | (1 << NL80211_CHAN_WIDTH_80)
                                 | ((vht_160_mhz ? 1 : 0) << NL80211_CHAN_WIDTH_160);

  return if_comb;
}

static int
wv_cfg80211_send_peer_connect (struct wireless_dev *wdev, void *data, int data_len)
{
  uint8 *cp;
  mtlk_nbuf_t *evt_nbuf;
#if IWLWAV_RTLOG_MAX_DLEVEL >= 3
  mtlk_df_user_t *df_user;
  mtlk_vap_handle_t vap_handle;
#endif

  MTLK_ASSERT(NULL != wdev);

  evt_nbuf = wv_cfg80211_vendor_event_alloc(wdev, data_len,
                                            LTQ_NL80211_VENDOR_EVENT_WDS_CONNECT);
  if (!evt_nbuf)
  {
    return MTLK_ERR_NO_MEM;
  }

  cp = mtlk_df_nbuf_put(evt_nbuf, data_len);
  wave_memcpy(cp, data_len, data, data_len);

#if IWLWAV_RTLOG_MAX_DLEVEL >= 3
  df_user = mtlk_df_user_from_wdev(wdev);
  if (df_user) {
    vap_handle = mtlk_df_get_vap_handle(mtlk_df_user_get_df(df_user));
    ILOG3_D("CID-%04x: WDS peer connect", mtlk_vap_get_oid(vap_handle));
  }
#endif

  mtlk_dump(3, evt_nbuf->data, evt_nbuf->len, "WDS peer connect vendor frame");
  wv_cfg80211_vendor_event(evt_nbuf);

  return MTLK_ERR_OK;
}

static int
wv_cfg80211_send_peer_disconnect (struct wireless_dev *wdev, void *data, int data_len)
{
  uint8 *cp;
  mtlk_nbuf_t *evt_nbuf;
#if IWLWAV_RTLOG_MAX_DLEVEL >= 3
  mtlk_df_user_t *df_user;
  mtlk_vap_handle_t vap_handle;
#endif

  MTLK_ASSERT(NULL != wdev);

  evt_nbuf = wv_cfg80211_vendor_event_alloc(wdev, data_len,
                                            LTQ_NL80211_VENDOR_EVENT_WDS_DISCONNECT);
  if (!evt_nbuf)
  {
    return MTLK_ERR_NO_MEM;
  }

  cp = mtlk_df_nbuf_put(evt_nbuf, data_len);
  wave_memcpy(cp, data_len, data, data_len);

#if IWLWAV_RTLOG_MAX_DLEVEL >= 3
  df_user = mtlk_df_user_from_wdev(wdev);
  if (df_user) {
    vap_handle = mtlk_df_get_vap_handle(mtlk_df_user_get_df(df_user));
    ILOG3_D("CID-%04x: WDS peer disconnect", mtlk_vap_get_oid(vap_handle));
  }
#endif

  mtlk_dump(3, evt_nbuf->data, evt_nbuf->len, "WDS peer disconnect vendor frame");
  wv_cfg80211_vendor_event(evt_nbuf);

  return MTLK_ERR_OK;
}

void __MTLK_IFUNC
wv_cfg80211_peer_connect (struct net_device *ndev, const IEEE_ADDR *mac_addr, wds_beacon_data_t *beacon_data)
{
  struct intel_vendor_wds_sta_info *st_info;
  int st_info_size = sizeof(struct intel_vendor_wds_sta_info) + beacon_data->ie_data_len;
  st_info = (struct intel_vendor_wds_sta_info *)mtlk_osal_mem_alloc(st_info_size, MTLK_MEM_TAG_CFG80211);

  if (NULL == st_info) {
    ELOG_V("Cannot allocate memory for WDS peer connect");
    return;
  }

  memset(st_info, 0, st_info_size);

  st_info->assoc_req_ies_len = beacon_data->ie_data_len;
  st_info->dtim_period       = beacon_data->dtim_period;
  st_info->beacon_interval   = beacon_data->beacon_interval;
  st_info->protection        = beacon_data->protection;
  st_info->short_preamble    = beacon_data->short_preamble;
  st_info->short_slot_time   = beacon_data->short_slot_time;
  st_info->max_rssi          = DBM_TO_MBM(beacon_data->max_rssi);
  st_info->sta_flags_mask    = BIT(NL80211_STA_FLAG_AUTHORIZED)     |
                               BIT(NL80211_STA_FLAG_SHORT_PREAMBLE) |
                               BIT(NL80211_STA_FLAG_AUTHENTICATED)  |
                               BIT(NL80211_STA_FLAG_ASSOCIATED);
  st_info->sta_flags_set     = (1 << NL80211_STA_FLAG_AUTHORIZED)                                         |
                               ((beacon_data->short_preamble ? 1 : 0) << NL80211_STA_FLAG_SHORT_PREAMBLE) |
                               (1 << NL80211_STA_FLAG_AUTHENTICATED)                                      |
                               (1 << NL80211_STA_FLAG_ASSOCIATED);
  st_info->mac_addr = *mac_addr;
  wave_memcpy(&st_info->assoc_req_ies, beacon_data->ie_data_len, beacon_data->ie_data, beacon_data->ie_data_len);
  wave_strcopy(st_info->ifname, ndev->name, sizeof(st_info->ifname));

  wv_cfg80211_send_peer_connect(ndev->ieee80211_ptr, st_info, st_info_size);
  mtlk_osal_mem_free(st_info);
}

void __MTLK_IFUNC  wv_cfg80211_peer_disconnect (struct net_device *ndev, const IEEE_ADDR *mac_addr)
{
  struct intel_vendor_wds_sta_info st_info;
  memset(&st_info, 0, sizeof(st_info));

  ILOG1_Y("Peer AP %Y", mac_addr);

  st_info.mac_addr = *mac_addr;
  wave_strcopy(st_info.ifname, ndev->name, sizeof(st_info.ifname));

  wv_cfg80211_send_peer_disconnect(ndev->ieee80211_ptr, &st_info, sizeof(st_info));
}

void __MTLK_IFUNC wv_cfg80211_obss_beacon_report (
  struct wireless_dev *wdev,
  uint8 *frame,
  size_t len,
  int channel,
  int sig_dbm)
{
  int freq = channel_to_frequency(channel);
  cfg80211_report_obss_beacon(wdev->wiphy, frame, len, freq, sig_dbm);
  ILOG3_DD("OBSS beacon report: chan=%d, dbm=%d", channel, sig_dbm);
}

void __MTLK_IFUNC wv_cfg80211_inform_bss_frame(struct wireless_dev *wdev, struct mtlk_channel *chan,
                                               u8 *buf, size_t len, s32 signal)
{
  struct cfg80211_bss *bss;

  MTLK_ASSERT(chan);

  bss = cfg80211_inform_bss_frame(wdev->wiphy,
      ieee80211_get_channel(wdev->wiphy, chan->center_freq),
      (struct ieee80211_mgmt *) buf, len, DBM_TO_MBM(signal), GFP_ATOMIC);

  if (!bss)
    ELOG_V("wv_cfg80211_inform_bss_frame() failed");
  else
  {
    /* ILOG3_S("SSID %s reported to the kernel", bss_data->essid); */
    cfg80211_put_bss(wdev->wiphy, bss);
  }
}

#define WAVE_CHAN_IS_5G(freq) (((freq) / 5000u) != 0)
#define WAVE_REGULATORY_POWER_MAX_DBM 0xFF

/*
 * Get regulatory power limit with a support of
 * regulatory rules conjuncted with AUTO_BW flag
 */
unsigned __MTLK_IFUNC
wv_cfg80211_regulatory_limit_get (struct wireless_dev *wdev,
                                  unsigned lower_freq, unsigned upper_freq)
{
  unsigned max_power = WAVE_REGULATORY_POWER_MAX_DBM;
  unsigned freq, freq_step = 5; /* frequency step between used channels for 2.4 GHz */

  /* adjust frequency step for 5.2 GHz */
  if (WAVE_CHAN_IS_5G(lower_freq))
    freq_step = 20;

  ILOG2_DDD("lower_freq %u upper_freq %u freq_step %u", lower_freq, upper_freq, freq_step);

  for (freq = lower_freq; freq <= upper_freq; freq += freq_step) {
    struct ieee80211_channel *channel = ieee80211_get_channel(wdev->wiphy, freq);

    if (NULL == channel) {
      WLOG_D("No ieee80211_channel for freq %d", freq);
      continue;
    }

    if (channel->flags & IEEE80211_CHAN_DISABLED) {
      ILOG2_D("Disabled channel %d", freq);
      continue;
    }

    ILOG2_DD("freq %d max_reg_power %d", channel->center_freq, channel->max_reg_power);

    /* find minimal allowed by regulatory */
    if (channel->max_reg_power < max_power)
      max_power = channel->max_reg_power;
  }

  /* check if nothing was found */
  if (max_power == WAVE_REGULATORY_POWER_MAX_DBM)
    max_power = 0;

  ILOG1_D("max_power %d dBm", max_power);

  return DBM_TO_POWER(max_power);
}

#define HT_CAPAB_LEN 26

void wv_fill_ht_capab(struct mtlk_scan_request *request, mtlk_hw_band_e band, u8 *buf, size_t len)
{
  struct wiphy *wiphy = (struct wiphy *) request->wiphy;
  int nlband = mtlkband2nlband(band);
  int i = 0;
  struct ieee80211_sta_ht_cap *ht_cap;
  uint16 cap_info;

  MTLK_ASSERT(buf && len >= HT_CAPAB_LEN && wiphy->bands[nlband]);

  ht_cap = &wiphy->bands[nlband]->ht_cap;

  cap_info = HOST_TO_WLAN16(ht_cap->cap);
  wave_memcpy(buf + i, len - i, &cap_info, sizeof(cap_info));
  i += sizeof(cap_info);

  buf[i++] = ht_cap->ampdu_factor | (ht_cap->ampdu_density << IEEE80211_HT_AMPDU_PARM_DENSITY_SHIFT);

  wave_memcpy(buf + i, len - i, &ht_cap->mcs, sizeof(ht_cap->mcs));
  i += sizeof(ht_cap->mcs); /* 16 */

  memset(buf + i, 0, HT_CAPAB_LEN - i); /* 7: extended capabilities, transmit beam forming, antenna selection */
}

static bool wv_cfg80211_reg_dfs_domain_same(struct wiphy *wiphy1, struct wiphy *wiphy2)
{
 const struct ieee80211_regdomain *wiphy1_regd = NULL;
 const struct ieee80211_regdomain *wiphy2_regd = NULL;
 const struct ieee80211_regdomain *cfg80211_regd = NULL;

 cfg80211_regd = get_cfg80211_regdom();
 if (cfg80211_regd == NULL)
   return FALSE;
 wiphy1_regd = get_wiphy_regdom(wiphy1);
 if (!wiphy1_regd)
   wiphy1_regd = cfg80211_regd;

 wiphy2_regd = get_wiphy_regdom(wiphy2);
 if (!wiphy2_regd)
   wiphy2_regd = cfg80211_regd;

 return wiphy1_regd->dfs_region == wiphy2_regd->dfs_region;
}

bool wv_cfg80211_get_chans_dfs_required(struct wiphy *wiphy,
                                            u32 center_freq,
                                            u32 bandwidth)
{
  struct ieee80211_channel *c;
  u32 freq;

  for (freq = center_freq - bandwidth/2 + 10;
    freq <= center_freq + bandwidth/2 - 10;
    freq += 20) {
    c = ieee80211_get_channel(wiphy, freq);
    if (!c)
      continue;

    if (c->flags & IEEE80211_CHAN_RADAR)
      return TRUE;
  }
  return FALSE;
}

/* In case of RBM bits set for non-DFS channels return FALSE */
bool wv_cfg80211_get_chans_dfs_bitmap_valid(struct wiphy *wiphy, u32 center_freq, u32 bandwidth, u8 rbm)
{
  struct ieee80211_channel *c;
  u32 freq, mask;
  bool chan_found = FALSE;

  for (freq = center_freq - bandwidth / 2 + 10, mask = 1;
       freq <= center_freq + bandwidth / 2 - 10;
       freq += 20, mask <<= 1)
  {
    if (rbm & mask) {
      c = ieee80211_get_channel(wiphy, freq);
      if (!c) continue;

      if (!(c->flags & IEEE80211_CHAN_RADAR))
        return FALSE;

      chan_found = TRUE;
    }
  }

  return chan_found;
}

/* In case of invalid chandef, return FALSE */
bool wv_cfg80211_chandef_dfs_required(struct wiphy *wiphy,
                                  const struct cfg80211_chan_def *chandef)
{
  int width;
  int r;

  if (!cfg80211_chandef_valid(chandef)) {
    ELOG_V("Invalid chandef");
    return FALSE;
  }

  width = _wave_radio_chandef_width_get(chandef);
  if (width < 0)
    return FALSE;

  r = wv_cfg80211_get_chans_dfs_required(wiphy, chandef->center_freq1,
    width);
  if (r)
    return r;

  if (!chandef->center_freq2)
    return FALSE;

  return wv_cfg80211_get_chans_dfs_required(wiphy, chandef->center_freq2,
    width);
}


/* Propagate DFS state of our interface to other interfaces. Debug features like
 * setting CAC time and Non Occupancy Period are not supported, because we don't
 * know weather we are propagating event to device handled by mtlk driver. */
void wv_cfg80211_regulatory_propagate_dfs_state(struct wireless_dev *wdev,
           struct cfg80211_chan_def *chandef,
           enum nl80211_dfs_state dfs_state,
           enum nl80211_radar_event event,
           uint8 rbm)
{
  struct cfg80211_registered_device *rdev;
  struct wireless_dev *wdev2;
  uint32 width;
  BOOL devices_exist = FALSE;
  BOOL update_dfs_wq;

  ASSERT_RTNL();

  if (!cfg80211_chandef_valid(chandef)) {
    ELOG_S("%s: Invalid chandef", wdev->netdev->name);
    return;
  }
  if (!wv_cfg80211_chandef_dfs_required(wdev->wiphy, chandef)) {
    ILOG1_S("%s: No DFS channels", wdev->netdev->name);
    return;
  }

  list_for_each_entry(rdev, &cfg80211_rdev_list, list) {
    update_dfs_wq = FALSE;
    if (wdev->wiphy == &rdev->wiphy)
      continue;

    if (!wv_cfg80211_reg_dfs_domain_same(wdev->wiphy, &rdev->wiphy))
      continue;

    if (!ieee80211_get_channel(&rdev->wiphy,
      chandef->chan->center_freq))
      continue;

    width = wave_radio_chandef_width_get(chandef);
    list_for_each_entry(wdev2, &rdev->wiphy.wdev_list, list) {
      struct ieee80211_channel *wdev2_chan = wdev2->chandef.chan;

      if (wdev2->iftype != NL80211_IFTYPE_AP || wdev2_chan == NULL)
        continue;

      devices_exist = update_dfs_wq = TRUE;

      /* If radar detected on our channel */
      if ((event == NL80211_RADAR_DETECTED) &&
          (chandef->center_freq1 - width / 2 < wdev2_chan->center_freq &&
           chandef->center_freq1 + width / 2 > wdev2_chan->center_freq))
        wdev2->cac_started = false;
    }

    if (update_dfs_wq) {
      cancel_delayed_work(&rdev->dfs_update_channels_wk);
      queue_delayed_work(cfg80211_get_cfg80211_wq(), &rdev->dfs_update_channels_wk, 0);
    }

    /* no state change on CAC aborted */
    if (update_dfs_wq && (event != NL80211_RADAR_CAC_ABORTED)) {
      if (rbm)
        cfg80211_set_dfs_state_bit_map(&rdev->wiphy, chandef, rbm, dfs_state);
      else
        cfg80211_set_dfs_state(&rdev->wiphy, chandef, dfs_state);
    }
  }
  /* Only CAC finished event needs to be propagated, the other events are
   * already global. */
  if (devices_exist && event == NL80211_RADAR_CAC_FINISHED)
    nl80211_radar_notify(wiphy_to_rdev(wdev->wiphy), chandef, event, NULL, 0, GFP_KERNEL);
}

void wv_cfg80211_radar_event(struct wireless_dev *wdev, struct cfg80211_chan_def *chandef,
                             uint32 cac_started, uint8 rbm)
{
  wv_cfg80211_t *cfg80211 = wiphy_priv(wdev->wiphy);
  mtlk_scan_support_t *scan_support = wave_radio_scan_support_get(cfg80211->radio);

  ASSERT_RTNL();

  cfg80211_radar_event(wdev->wiphy, chandef, rbm, GFP_ATOMIC);

#ifndef CPTCFG_IWLWAV_X86_HOST_PC
  /* Use debug non occupancy period */
  if (scan_support->dfs_debug_params.nop) {
    struct ieee80211_channel *c;
    uint32 low_freq, high_freq, cur_freq;
    struct cfg80211_registered_device *rdev = wiphy_to_rdev(wdev->wiphy);
    uint32 non_ocup_prd_ms = scan_support->dfs_debug_params.nop * MTLK_OSAL_MSEC_IN_MIN;
    low_freq = freq2lowfreq(chandef->center_freq1, nlcw2mtlkcw(chandef->width));
    high_freq = freq2highfreq(chandef->center_freq1, nlcw2mtlkcw(chandef->width));

    ILOG1_D("Setting debug non occupancy period for %d seconds",
            scan_support->dfs_debug_params.nop * MTLK_OSAL_SEC_IN_MIN);

    for (cur_freq = low_freq; cur_freq <= high_freq; cur_freq += 20) {
      c = ieee80211_get_channel(wdev->wiphy, cur_freq);

      if (!c || !(c->flags & IEEE80211_CHAN_RADAR) || (c->dfs_state != NL80211_DFS_UNAVAILABLE))
        continue;

      if (non_ocup_prd_ms < IEEE80211_DFS_MIN_NOP_TIME_MS)
        c->dfs_state_entered -= msecs_to_jiffies(IEEE80211_DFS_MIN_NOP_TIME_MS - non_ocup_prd_ms);
      else
        c->dfs_state_entered += msecs_to_jiffies(non_ocup_prd_ms - IEEE80211_DFS_MIN_NOP_TIME_MS);
    }

    cancel_delayed_work(&rdev->dfs_update_channels_wk);
    queue_delayed_work(cfg80211_get_cfg80211_wq(), &rdev->dfs_update_channels_wk, 0);
  }
#endif
}

static int
wv_cfg80211_handle_radar_event_if_sta_exist(struct wireless_dev *wdev, void *data,
  int data_len)
{
  mtlk_nbuf_t *evt_nbuf;
  uint8 *cp;

  MTLK_ASSERT(NULL != wdev);

  evt_nbuf = wv_cfg80211_vendor_event_alloc(wdev, data_len,
    LTQ_NL80211_VENDOR_EVENT_RADAR_DETECTED);
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

int wv_cfg80211_radar_event_if_sta_exist(struct wireless_dev *wdev,
  struct mtlk_chan_def *mcd)
{
  struct cfg80211_chan_def chandef;
  struct cfg80211_registered_device *rdev = wiphy_to_rdev(wdev->wiphy);
  unsigned long timeout;
  struct intel_vendor_radar radar;

  chandef.chan = ieee80211_get_channel(wdev->wiphy, mcd->chan.center_freq);
  chandef.width = mtlkcw2nlcw(mcd->width, mcd->is_noht);
  chandef.center_freq1 = mcd->center_freq1;
  chandef.center_freq2 = mcd->center_freq2;

  /* mark channel as UNAVAILABLE */
  cfg80211_set_dfs_state(wdev->wiphy, &chandef, NL80211_DFS_UNAVAILABLE);
  /* schedule workqueue */
  timeout = msecs_to_jiffies(IEEE80211_DFS_MIN_NOP_TIME_MS);
  queue_delayed_work(cfg80211_get_cfg80211_wq(), &rdev->dfs_update_channels_wk,
    timeout);

  /* notify hostapd with vendor event */
  memset(&radar, 0, sizeof(radar));
  radar.center_freq = mcd->chan.center_freq;
  radar.width = chandef.width;
  radar.center_freq1 = mcd->center_freq1;
  radar.center_freq2 = mcd->center_freq2;
  radar.radar_bit_map = 0xFF;
  return wv_cfg80211_handle_radar_event_if_sta_exist(wdev, &radar,
    sizeof(radar));
}

void wv_cfg80211_cac_event(struct net_device *ndev, const struct mtlk_chan_def *mcd, int finished)
{
  struct cfg80211_chan_def chandef;
  struct wireless_dev *wdev = ndev->ieee80211_ptr;
  wv_cfg80211_t *cfg80211 = wiphy_priv(wdev->wiphy);
  mtlk_scan_support_t *scan_support;
  mtlk_df_user_t *df_user = mtlk_df_user_from_ndev(ndev);

  MTLK_ASSERT(NULL != wdev);
  MTLK_ASSERT(NULL != cfg80211);

  /* df_user shouldn't be NULL in a normal situation */
  MTLK_CHECK_DF_USER_NORES(df_user);

  scan_support = wave_radio_scan_support_get(cfg80211->radio);
  chandef.chan = ieee80211_get_channel(wdev->wiphy, mcd->chan.center_freq);
  chandef.width = mtlkcw2nlcw(mcd->width, mcd->is_noht);
  chandef.center_freq1 = mcd->center_freq1;
  chandef.center_freq2 = mcd->center_freq2;

  if (finished) {
    unsigned long timeout, cur_jiffies;

    /* Kernel doesn't allow CAC time less than 60 seconds.
     * This changes the wdev->cac_start_time to allow
     * CAC time less than than 60 seconds*/
    if (scan_support->dfs_debug_params.cac_time != -1) {
      uint32 cac_time_ms = scan_support->dfs_debug_params.cac_time * MTLK_OSAL_MSEC_IN_SEC;
      if (cac_time_ms < IEEE80211_DFS_MIN_CAC_TIME_MS)
        wdev->cac_start_time -= msecs_to_jiffies(IEEE80211_DFS_MIN_CAC_TIME_MS);
    }

    timeout = wdev->cac_start_time + msecs_to_jiffies(IEEE80211_DFS_MIN_CAC_TIME_MS);
    cur_jiffies = jiffies;

    if (!time_after_eq(cur_jiffies, timeout)) {
      WLOG_DDDDD("CAC time was less than minimal %u seconds, jiffies %u, cac start time %u + cac time in jiffies %u = timeout %u",
                 IEEE80211_DFS_MIN_CAC_TIME_MS / MTLK_OSAL_MSEC_IN_SEC, cur_jiffies, wdev->cac_start_time,
                 msecs_to_jiffies(IEEE80211_DFS_MIN_CAC_TIME_MS), timeout);
      /* To eliminate possible kernel warning */
      wdev->cac_start_time -= msecs_to_jiffies(IEEE80211_DFS_MIN_CAC_TIME_MS);
    }
  }

  cfg80211_cac_event(ndev, &chandef, finished ? NL80211_RADAR_CAC_FINISHED : NL80211_RADAR_CAC_ABORTED, GFP_ATOMIC);

  if (finished) {
    mtlk_df_user_add_propagate_event(df_user, &chandef, NL80211_DFS_AVAILABLE, NL80211_RADAR_CAC_FINISHED, 0, 0);
    mtlk_df_user_schedule_prop_dfs_wq(df_user);
  }
}

void wv_cfg80211_nop_finished_send(struct wiphy *wiphy, mtlk_df_t *df,
  struct mtlk_chan_def *mcd)
{
  struct cfg80211_chan_def chandef;
  struct cfg80211_registered_device *rdev;

  chandef.chan = ieee80211_get_channel(wiphy, mcd->chan.center_freq);
  if (!chandef.chan) {
    ILOG1_V("Cannot get channel info, exit");
    return;
  }
  if (!(chandef.chan->flags & IEEE80211_CHAN_RADAR)) {
    ILOG1_V("Not DFS channel, exit");
    return;
  }
  if (chandef.chan->dfs_state == NL80211_DFS_UNAVAILABLE) {
    ILOG1_V("DFS channel in unavailable state, exit");
    return;
  }

  chandef.width = mtlkcw2nlcw(mcd->width, mcd->is_noht);
  chandef.center_freq1 = mcd->center_freq1;
  chandef.center_freq2 = mcd->center_freq2;

  ILOG1_DDDD("center_freq1=%u, center_freq2=%u, width=%u, chan.center_freq=%u",
    chandef.center_freq1, chandef.center_freq2, chandef.width,
    chandef.chan->center_freq);

  cfg80211_set_dfs_state(wiphy, &chandef, NL80211_DFS_USABLE);
  rdev = wiphy_to_rdev(wiphy);
  nl80211_radar_notify(rdev, &chandef, NL80211_RADAR_NOP_FINISHED, NULL, 0, GFP_KERNEL);

  mtlk_df_user_add_propagate_event(mtlk_df_get_user(df), &chandef, NL80211_DFS_USABLE, NL80211_RADAR_NOP_FINISHED, 0, 0);
}

/* Make channel uncertain and set state to USABLE if we were on DFS channels */
void wv_cfg80211_on_core_down (mtlk_core_t *core)
{
  u32 freq, low_freq, high_freq;
  struct mtlk_chan_def *ccd = __wave_core_chandef_get(core);
  struct mtlk_chan_def mcd_single_chan;
  mtlk_df_t *df = mtlk_vap_get_df(core->vap_handle);
  struct wireless_dev *wdev = mtlk_df_user_get_wdev(mtlk_df_get_user(df));
  memset(&mcd_single_chan, 0, sizeof(mcd_single_chan));
  mcd_single_chan.width = CW_20;

  if (!is_channel_certain(ccd))
    return;

  if (__mtlk_is_sb_dfs_switch(ccd->sb_dfs.sb_dfs_bw)) {
    low_freq = freq2lowfreq(ccd->sb_dfs.center_freq, ccd->sb_dfs.width);
    high_freq = freq2highfreq(ccd->sb_dfs.center_freq, ccd->sb_dfs.width);

    for(freq = low_freq; freq <= high_freq; freq += 20) {
      mcd_single_chan.center_freq1 = mcd_single_chan.chan.center_freq = freq;
      wv_cfg80211_nop_finished_send(wdev->wiphy, df, &mcd_single_chan);
    }
  }

  low_freq = freq2lowfreq(ccd->center_freq1, ccd->width);
  high_freq = freq2highfreq(ccd->center_freq1, ccd->width);
  for(freq = low_freq; freq <= high_freq; freq += 20) {
      mcd_single_chan.center_freq1 = mcd_single_chan.chan.center_freq = freq;
      wv_cfg80211_nop_finished_send(wdev->wiphy, df, &mcd_single_chan);
  }

  if (ccd->center_freq2) {
    low_freq = freq2lowfreq(ccd->center_freq2, ccd->width);
    high_freq = freq2highfreq(ccd->center_freq2, ccd->width);

    for(freq = low_freq; freq <= high_freq; freq += 20) {
        mcd_single_chan.center_freq1 = mcd_single_chan.chan.center_freq = freq;
        wv_cfg80211_nop_finished_send(wdev->wiphy, df, &mcd_single_chan);
    }
  }

  mtlk_df_user_schedule_prop_dfs_wq(mtlk_df_get_user(df));
}

void wv_cfg80211_ch_switch_notify(struct net_device *ndev, const struct mtlk_chan_def *mcd, bool sta_exist)
{
  struct cfg80211_chan_def chandef;
  struct wireless_dev *wdev = ndev->ieee80211_ptr;
  mtlk_df_user_t *df_user = mtlk_df_user_from_ndev(ndev);

  /* df_user shouldn't be NULL in a normal situation */
  MTLK_CHECK_DF_USER_NORES(df_user);

  chandef.chan = ieee80211_get_channel(wdev->wiphy, mcd->chan.center_freq);
  chandef.width = mtlkcw2nlcw(mcd->width, mcd->is_noht);
  chandef.center_freq1 = mcd->center_freq1;
  chandef.center_freq2 = mcd->center_freq2;

  if (sta_exist) {
    cfg80211_set_dfs_state(wdev->wiphy, &chandef, NL80211_DFS_AVAILABLE);
    mtlk_df_user_add_propagate_event(df_user, &chandef, NL80211_DFS_AVAILABLE, NL80211_RADAR_CAC_FINISHED, 0, 0);
    mtlk_df_user_schedule_prop_dfs_wq(df_user);
  }

  cfg80211_ch_switch_notify(ndev, &chandef);
}

void wv_cfg80211_set_scan_expire_time(struct wireless_dev *wdev, uint32 time)
{
  MTLK_ASSERT(wdev);
  MTLK_ASSERT(wdev->wiphy);

  cfg80211_set_scan_expire_time(wdev->wiphy, time * HZ);
}

uint32 wv_cfg80211_get_scan_expire_time(struct wireless_dev *wdev)
{
  MTLK_ASSERT(wdev);
  MTLK_ASSERT(wdev->wiphy);

  return cfg80211_get_scan_expire_time(wdev->wiphy) / HZ;
}

void __MTLK_IFUNC
wv_cfg80211_update_wiphy_ant_mask(struct wiphy *wiphy, uint8 available_ant_mask)
{
  wiphy->available_antennas_rx = available_ant_mask;
  wiphy->available_antennas_tx = available_ant_mask;
}
