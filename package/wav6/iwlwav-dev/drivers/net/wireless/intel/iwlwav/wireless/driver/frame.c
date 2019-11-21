/******************************************************************************

                               Copyright (c) 2012
                            Lantiq Deutschland GmbH

  For licensing information, see the file 'LICENSE' in the root folder of
  this software module.

******************************************************************************/
/*
 * $Id$
 *
 *  *
 * 802.11n frame processing routines.
 *
 * Originaly written by Artem Migaev
 *
 */
#include "mtlkinc.h"
#include "scan.h"
#include "frame.h"
#include "mtlkaux.h"
#include "channels.h"
#include "mtlk_core_iface.h"
#include "core_cfg_iface.h"
#include "core.h"
#include "scan_support.h"
#include "mtlk_packets.h"
#include "wds.h"
#include "mtlknbufpriv.h"
#include "cfg80211.h"

#define LOG_LOCAL_GID   GID_FRAME
#define LOG_LOCAL_FID   1

#define NON_ERP_IE_MASK            BIT(0)
#define USE_PROTECTION_IE_MASK     BIT(1)
#define CAPABILITY_IBSS_MASK       0x0002
#define CAPABILITY_NON_POLL        0x000C
#define CAPABILITY_SHORT_PREAMBLE  BIT(5)
#define CAPABILITY_SHORT_SLOT_TIME BIT(10)


static const char *const man_type_strings[] =
{
  "ASSOC_REQ",
  "ASSOC_RES",
  "REASSOC_REQ",
  "REASSOC_RES",
  "PROBE_REQ",
  "PROBE_RES",
  "6_RESERVED",
  "7_RESERVED",
  "BEACON",
  "ATIM",
  "DISASSOC",
  "AUTH",
  "DEAUTH",
  "ACTION",
  "14_RESERVED",
  "15_RESERVED",
};

void __MTLK_IFUNC core_wds_beacon_process(mtlk_core_t *core, wds_beacon_data_t *beacon_data);

/*!
  \fn  ie_extract_phy_rates()
  \brief  Process Rates informational elements
  \param  ie_data Pointer to IE
  \param  length IE length
  \param  bss_data Pointer to BSS description structure.

  'Supported Rates' and 'Extended Supported Rates' IEs are parsed
  by this function and filled to the supplied bss_data structure.
  All known rates are extracted, unsopported silently ignored.
*/
static __INLINE int
ie_extract_phy_rates (uint8      *ie_data,
                      int32       length,
                      bss_data_t *bss_data)
{
  int res = MTLK_ERR_OK;

  while (length) {
    uint32 rate = 0;
    uint8  val  = *ie_data;
    switch (val & SUPPORTED_BITRATE_MASK) {
    case 6*2: /* 11a rate 0 6mbps */
      rate = BASIC_RATE_SET_11A_6MBPS;
      break;

    case 9*2: /* 11a rate 1 9mbps */
      rate = BASIC_RATE_SET_11A_9MBPS;
      break;

    case 12*2: /* 11a rate 2 12mbps */
      rate = BASIC_RATE_SET_11A_12MBPS;
      break;

    case 18*2: /* 11a rate 3 18mbps */
      rate = BASIC_RATE_SET_11A_18MBPS;
      break;

    case 24*2: /* 11a rate 4 24mbps */
      rate = BASIC_RATE_SET_11A_24MBPS;
      break;

    case 36*2: /* 11a rate 5 36mbps */
      rate = BASIC_RATE_SET_11A_36MBPS;
      break;

    case 48*2: /* 11a rate 6 48mbps */
      rate = BASIC_RATE_SET_11A_48MBPS;
      break;

    case 54*2: /* 11a rate 7 54mbps */
      rate = BASIC_RATE_SET_11A_54MBPS;
      break;

    case 1*2: /* 11b rate 11 1mbps-long-preamble */
      rate = BASIC_RATE_SET_11B_1MBPS_LONG;
      break;

    case 2*2: /* 11b rate 8+12 2mbps-short+long */
      rate = BASIC_RATE_SET_11B_2MBPS_SHORT_LONG;
      break;

    case 11: /* (5.5*2) 11b rate 9+13 5.5mbps-short+long */
      rate = BASIC_RATE_SET_11B_5DOT5MBPS_SHORT_LONG;
      break;

    case 11*2: /* 11b rate 10+14 11mbps-short+long */
      rate = BASIC_RATE_SET_11B_11MBPS_SHORT_LONG;
      break;

    default: /* no other rates are allowed */
      ILOG2_D("Unexpected PHY rate: 0x%02X", (int)val);
      res = MTLK_ERR_PARAMS;
      break;
    };

    bss_data->operational_rate_set |= rate;
    if (val & BASIC_RATESET_FLAG)
      bss_data->basic_rate_set |= rate;

    length--;
    ie_data++;
  }

  if (bss_data->operational_rate_set & BASIC_RATE_SET_2DOT4_OPERATIONAL_RATE)
    bss_data->is_2_4 = 1;

  return res;
}

int
mtlk_frame_ie_extract_phy_rates(uint8      *ie_data,
                                int32       length,
                                bss_data_t *bss_data)
{
    return ie_extract_phy_rates(ie_data, length, bss_data);
}

uint32
mtlk_frame_oper_rateset_to_net_mode(uint32 oper_rateset,
                                    BOOL is_ht,
                                    BOOL is_vht,
                                    BOOL is_he)
{
    uint32  mode;

    if (oper_rateset & BASIC_RATE_SET_2DOT4_OPERATIONAL_RATE) {
        /* MTLK_HW_BAND_2_4_GHZ */
        mode = MTLK_NETWORK_11B_ONLY;
        if (oper_rateset & ~BASIC_RATE_SET_11B_RATES) {
            if(is_he)
                mode = MTLK_NETWORK_11BGNAX_MIXED;
            else if (is_ht)
                mode = MTLK_NETWORK_11BGN_MIXED;
            else
                mode = MTLK_NETWORK_11BG_MIXED;
        }
    } else {
        /* MTLK_HW_BAND_5_2_GHZ */
        if (is_he)
            mode = MTLK_NETWORK_11ANACAX_MIXED;
        else if (is_vht)
            mode = MTLK_NETWORK_11ANAC_MIXED;
        else if (is_ht)
            mode = MTLK_NETWORK_11AN_MIXED;
        else
            mode = MTLK_NETWORK_11A_ONLY;
    }

    ILOG2_DD("Rate set 0x%04X, net_mode %d", oper_rateset, mode);
    return mode;
}

/*!
  \fn  ie_extract_ht_info()
  \brief  Process HT informational element
  \param  ie_data Pointer to IE
  \param  lenfth IE length
  \param  bss_data Pointer to BSS description structure.

  'HT' IE parsed by this function and filled to the supplied
  bss_data structure. CB and Spectrum information extracted.
*/
static __INLINE int
ie_extract_ht_info (uint8      *ie_data,
                    int32       length,
                    bss_data_t *bss_data)
{
  int res = MTLK_ERR_PARAMS;

  if (length < 2) {
    ELOG_D("Wrong HT info length: %d", (int)length);
    goto FINISH;
  }

  bss_data->channel = ie_data[0];
  ILOG4_D("HT info channel is %u", bss_data->channel);

  switch (ie_data[1] & 0x07) {
  case 7: /* 40 lower */
    bss_data->spectrum    = 1;
    bss_data->upper_lower = 1;
    bss_data->secondary_channel_offset = UMI_CHANNEL_SW_MODE_SCB;
    break;

  case 5: /* 40 upper */
    bss_data->spectrum    = 1;
    bss_data->upper_lower = 0;
    bss_data->secondary_channel_offset = UMI_CHANNEL_SW_MODE_SCA;
    break;

  default:
    bss_data->spectrum    = 0;
    bss_data->upper_lower = 0;
    bss_data->secondary_channel_offset = UMI_CHANNEL_SW_MODE_SCN;
    break;
  };

  res = MTLK_ERR_OK;

FINISH:
  return res;
}



/*!
  \fn  ie_extract_htcap_info()
  \brief  Process HT capabilities informational element
  \param  ie_data Pointer to IE
  \param  lenfth IE length
  \param  bss_data Pointer to BSS description structure.

  'HT Capabilities' IE parsed by this function and filled to the
  supplied bss_data structure. Rates information extracted.
*/
static __INLINE int
ie_extract_htcap_info (uint8      *ie_data,
                       int32       length,
                       bss_data_t *bss_data,
                       BOOL is_ap,
                       mtlk_core_t *core)
{
  int res = MTLK_ERR_PARAMS;
  uint8 val = 0;
  htcap_ie_t *htcap = (htcap_ie_t *)ie_data;
  uint16 ht_cap_info = WLAN_TO_HOST16(htcap->info);

  if (length < 7) {
    ELOG_D("Wrong HT Capabilities length: %d", (int)length);
    goto FINISH;
  }

  bss_data->forty_mhz_intolerant = (BOOL)!!(ht_cap_info & HTCAP_40MHZ_INTOLERANT);

  /**********************************************************
   * Modulation and coding scheme parsing.
   *
   * This is a 'Supported MCS Set' field of HT Capabilities
   * Information Element. We're interested only in Rx MCS
   * bitmask (78 bits) subfield. See sections 7.3.2.52.1,
   * 7.3.2.52.4 and 20.6 of 802.11n Draft 3.02
   ***********************************************************/

  val = htcap->supported_mcs[0];
  /* MCS 0..7 are filled to OperationalRateSet bits 15..22
   * For 1 spartial stream with 1 BCC modulator
   * -------------------------
   * Idx          20 MHz                40 MHz
   *      Normal GI   Short GI  Normal GI   Short GI
   * 0    6.5         7.2       13.5        15
   * 1    13          14.4      27          30
   * 2    19.5        21.7      40.5        45
   * 3    26          28.9      54          60
   * 4    39          43.3      81          90
   * 5    52          57.8      108         120
   * 6    58.5        65.0      121.5       135
   * 7    65          72.2      135         150
   */
  bss_data->operational_rate_set |= ((uint32)val) << 15;

  val = htcap->supported_mcs[1];
  /* MCS 8..15 are filled to OperationalRateSet bits 23..30
   * For 2 spartial streams equal modulation with 1 BCC modulator
   * -------------------------
   * Idx          20 MHz                40 MHz
   *      Normal GI   Short GI  Normal GI   Short GI
   * 8    13          14.4      27          30
   * 9    26          28.9      54          60
   * 10   39          43.3      81          90
   * 11   52          57.8      108         120
   * 12   78          86.7      162         180
   * 13   104         115.6     216         240
   * 14   117         130       243         270
   * 15   130         144.4     270         300
   */
  bss_data->operational_rate_set |= ((uint32)val) << 23;

  val = htcap->supported_mcs[4];
  /* MCS 32 is filled to OperationalRateSet bit 31
   * For 1 spartial stream with 1 BCC modulator
   * -------------------------
   * Idx          20 MHz                40 MHz
   *      Normal GI   Short GI  Normal GI   Short GI
   * 32   x           x         6           6.7
   */
  bss_data->operational_rate_set |= ((uint32)val) << 31;

  res = MTLK_ERR_OK;

FINISH:
  return res;
}

/*!
  \fn  ie_process()
  \brief  Process management frame IEs (Beacon or Probe Response)
  \param  fbuf Pointer to IE
  \param  flen IE length
  \param  bss_data Pointer to BSS description structure.

  All supported IEs parsed by this function and filled to the
  supplied bss_data structure.
*/
static void
ie_process (mtlk_handle_t context, uint8 *fbuf, int32 flen, bss_data_t *bss_data)
{
  static const uint8 wpa_oui_type[] = {0x00, 0x50, 0xF2, 0x01};
  static const uint8 wps_oui_type[] = {0x00, 0x50, 0xF2, 0x04};
  static const uint8 vsie_oui_type[] = { 0xf8, 0x32, 0xe4 };

  ie_t *ie;
  int32 copy_length;
  mtlk_core_t *core = ((mtlk_core_t*)context);

  ILOG4_D("Frame length: %d", flen);
  bss_data->basic_rate_set       = 0;
  bss_data->operational_rate_set = 0;
  bss_data->ie_used_length = 0;
  bss_data->rsn_offs = 0xFFFF;
  bss_data->wpa_offs = 0xFFFF;
  bss_data->wps_offs = 0xFFFF;
  bss_data->interworking_offs = 0xFFFF;
  bss_data->vsie_offs = 0xFFFF;
  bss_data->is_he = 0;

  while (flen > sizeof(ie_t))
  {
    ie = (ie_t *)fbuf;  /* WARNING! check on different platforms */
    if ((int32)(ie->length + sizeof(ie_t)) > flen) {
        flen = 0;
        break;
    }

    if (bss_data->ie_used_length + ie->length + sizeof(ie_t) > sizeof(bss_data->info_elements))
    {
        ILOG4_V("Not enough room to store all information elements!!!");
        flen = 0;
        break;
    }

    if (ie->length) {
        wave_memcpy(bss_data->info_elements + bss_data->ie_used_length,
            sizeof(bss_data->info_elements) - bss_data->ie_used_length,
            ie, ie->length + sizeof(ie_t));
        switch (ie->id)
        {
        case IE_EXT_SUPP_RATES:
          bss_data->is_2_4 = 1;
          /* FALLTHROUGH */
        case IE_SUPPORTED_RATES:
          ie_extract_phy_rates(GET_IE_DATA_PTR(ie),
                               ie->length,
                               bss_data);
          break;
        case IE_HT_INFORMATION:
          ie_extract_ht_info(GET_IE_DATA_PTR(ie),
                             ie->length,
                             bss_data);
          break;
        case IE_SSID:
          copy_length = ie->length;
          if (copy_length > BSS_ESSID_MAX_SIZE) {
            ILOG1_DD("SSID IE truncated (%u > %d)!!!",
                 copy_length, (int)BSS_ESSID_MAX_SIZE);
            copy_length = BSS_ESSID_MAX_SIZE;
          }
          /* ESSID always zero-terminated */
          wave_strncopy(bss_data->essid, GET_IE_DATA_PTR(ie), sizeof(bss_data->essid), copy_length);
          ILOG4_S("ESSID : %s", bss_data->essid);
          break;
        case IE_RSN:
          bss_data->rsn_offs = bss_data->ie_used_length;
          mtlk_dump(4, ie, ie->length + sizeof(ie_t),
                "RSN information element (WPA2)");
          break;
        case IE_VENDOR_SPECIFIC:
          if (!memcmp(GET_IE_DATA_PTR(ie), wpa_oui_type, sizeof(wpa_oui_type))) {
            bss_data->wpa_offs = bss_data->ie_used_length;
            mtlk_dump(4, ie, ie->length + sizeof(ie_t),
                  "Vendor Specific information element (WPA)");
          }
          else if (!memcmp(GET_IE_DATA_PTR(ie), wps_oui_type, sizeof(wps_oui_type))) {
            bss_data->wps_offs = bss_data->ie_used_length;
            mtlk_dump(4, ie, ie->length + sizeof(ie_t),
                  "Vendor Specific information element (WPS)");
          }
          else if (!memcmp(GET_IE_DATA_PTR(ie), vsie_oui_type, sizeof(vsie_oui_type))) {
            bss_data->vsie_offs = bss_data->ie_used_length;
            mtlk_dump(4, ie, ie->length + sizeof(ie_t),
              "Vendor Specific information element (VSIE)");
          }
          break;
        case IE_DS_PARAM_SET:
          bss_data->channel = GET_IE_DATA_PTR(ie)[0];
          ILOG4_D("DS parameter set channel: %u", bss_data->channel);
          break;
        case IE_HT_CAPABILITIES:
          bss_data->is_ht = 1;
          bss_data->ht_cap_info = GET_IE_DATA_PTR(ie);
          bss_data->ht_cap_info_len = ie->length;
          mtlk_dump(4, ie, ie->length + sizeof(ie_t),
                "HTCap information element");
          ie_extract_htcap_info(GET_IE_DATA_PTR(ie),
                                ie->length,
                                bss_data,
                                mtlk_vap_is_ap(core->vap_handle),
                                core);
          break;
        case IE_VHT_CAPABILITIES:
          bss_data->is_vht = 1;
          bss_data->ht_cap_info = GET_IE_DATA_PTR(ie);
          bss_data->ht_cap_info_len = ie->length;
          mtlk_dump(4, ie, ie->length + sizeof(ie_t),
                "VHTCap information element");
          break;
        case IE_EXTENSION:
          if (WLAN_EID_HE_CAP_EXT == GET_IE_DATA_PTR(ie)[0]) {
          bss_data->is_he = 1;
          bss_data->he_cap_info = GET_IE_DATA_PTR(ie);
          bss_data->he_cap_info_len = ie->length;
          mtlk_dump(4, ie, ie->length + sizeof(ie_t),
                "HECap information element");
          }
          break;
        case IE_PWR_CONSTRAINT:
          bss_data->power = GET_IE_DATA_PTR(ie)[0];
          ILOG4_D("Power constraint: %d", bss_data->power);
          break;
        case IE_COUNTRY:
          MTLK_STATIC_ASSERT(sizeof(bss_data->country_code.country) >= sizeof(bss_data->country_ie->country));
          bss_data->country_ie = (struct country_ie_t *)ie;
          memset(&bss_data->country_code, 0, sizeof(bss_data->country_code));
          wave_memcpy(bss_data->country_code.country, sizeof(bss_data->country_code.country),
                 bss_data->country_ie->country, sizeof(bss_data->country_ie->country));
          break;
        case IE_TIM:
          bss_data->dtim_period = *(GET_IE_DATA_PTR(ie)+1);
          break;
        case IE_ERP_INFO:
          bss_data->erp_info = *(GET_IE_DATA_PTR(ie));
          break;
        case IE_INTERWORKING:
          bss_data->interworking_offs = bss_data->ie_used_length;
          mtlk_dump(4, ie, ie->length + sizeof(ie_t),
                  "Interworking information element");
          break;
        default:
          break;
        } /*switch*/
        bss_data->ie_used_length += ie->length + sizeof(ie_t);
    }
    ILOG4_D("IE length: %d", ie->length);
    fbuf += ie->length + sizeof(ie_t);
    flen -= ie->length + sizeof(ie_t);
  } /*while*/

  /* Discard IEs that cause MAC failures afterward. */
  if ( ((!bss_data->is_2_4) && ((bss_data->basic_rate_set & BASIC_RATE_SET_OFDM_MANDATORY_RATES)
        != BASIC_RATE_SET_OFDM_MANDATORY_RATES_MASK)) ||
       (bss_data->is_2_4 && (bss_data->channel > BSS_DATA_2DOT4_CHANNEL_MAX)) ||
        !bss_data->channel ||
        !bss_data->operational_rate_set ||
       ((bss_data->spectrum &&
       !(bss_data->operational_rate_set & BASIC_RATE_SET_11N_RATE_MSK))))
  {
    memset(&bss_data->essid, 0, sizeof(bss_data->essid));
  }
}


int __MTLK_IFUNC _mtlk_core_switch_channel_normal(mtlk_core_t *master_core, struct mtlk_chan_def *chandef);
bool _mtlk_core_is_radar_chan(struct mtlk_channel *channel);

int
mtlk_process_man_frame(mtlk_handle_t context, sta_entry *sta, uint8 *fbuf, int32 *buflen,
                       mtlk_mgmt_frame_data_t *fd)
{
  int res = MTLK_ERR_OK;
  mtlk_core_t *core = ((mtlk_core_t*)context);
  uint16 frame_ctrl;
  uint16 subtype;
  frame_head_t *head;
  frame_beacon_head_t *beacon_head;
  bss_data_t *bss_data;
  scan_cache_t *cache = &core->slow_ctx->cache;
  int32 flen = *buflen;
  mtlk_vap_manager_t *vap_manager = mtlk_vap_get_manager(core->vap_handle);
  mtlk_core_t* master_core = mtlk_vap_manager_get_master_core(vap_manager);
  struct scan_support *scan_support = __wave_core_scan_support_get(master_core);
  struct mtlk_chan_def *current_chandef = __wave_core_chandef_get(master_core);
  BOOL wds_on = mtlk_vap_manager_get_wds_bridgemode(vap_manager);

  fd->report_bss = FALSE;
  fd->obss_beacon_report = FALSE;
  fd->notify_hapd_supplicant = FALSE;
  fd->probe_req_wps_ie = FALSE;
  fd->probe_req_interworking_ie = FALSE;
  fd->probe_req_vsie = FALSE;
  fd->probe_req_he_ie = FALSE;
  fd->chan = NULL;

  if (is_channel_certain(current_chandef))
  {
    fd->notify_hapd_supplicant = TRUE;
    fd->chan = &current_chandef->chan;
  }

  /* check that there is at least frame_head_t in buffer was done in
   handle_rx_bss_ind, no need to recheck it */
  head = (frame_head_t *)fbuf;
  fbuf += sizeof(frame_head_t);
  flen -= sizeof(frame_head_t);

  mtlk_dump(4, head, sizeof(frame_head_t), "802.11n head:");

  frame_ctrl = mtlk_wlan_pkt_get_frame_ctl((uint8*) head);
  subtype = (frame_ctrl & FRAME_SUBTYPE_MASK) >> FRAME_SUBTYPE_SHIFT;

  ILOG4_DS("Subtype is %d (%s)", subtype, man_type_strings[subtype]);

  /* if WDS is ON, find peer AP in DB and drop DIASSOC and DEAUTH frames */
  if (mtlk_vap_is_ap(core->vap_handle) &&
      wds_on && ((subtype == MAN_TYPE_DISASSOC) || (subtype == MAN_TYPE_DEAUTH))) {
    if (core_wds_frame_drop(core, &head->src_addr)) {
      fd->notify_hapd_supplicant = FALSE;
      return res;
    }
  }

  if (0 != WLAN_FC_GET_PROTECTED(frame_ctrl))
  {
    if (!sta)
    {
      if (subtype != MAN_TYPE_AUTH) {
        ILOG1_D("CID-%04x: Protected Management Frame received from unknown STA", mtlk_vap_get_oid(core->vap_handle));
        res = MTLK_ERR_NOT_IN_USE;
        goto end;
      }
      else if (!core->slow_ctx->wep_enabled) {
         ILOG1_D("CID-%04x: Protected Authentication Management Frame received when WEP is not enabled", mtlk_vap_get_oid(core->vap_handle));
         res = MTLK_ERR_NOT_IN_USE;
         goto end;
      }
    }

    if ((subtype == MAN_TYPE_AUTH) && (core->slow_ctx->wep_enabled)) {
      uint32 key_idx;
         if (flen < IEEE80211_WEP_IV_LEN + IEEE80211_WEP_ICV_LEN) {
         ILOG1_D("CID-%04x: Authentication Management Frame length is wrong", mtlk_vap_get_oid(core->vap_handle));
         res = MTLK_ERR_NOT_IN_USE;
         goto end;
      }

      flen -= (IEEE80211_WEP_IV_LEN + IEEE80211_WEP_ICV_LEN);
      *buflen -= (IEEE80211_WEP_IV_LEN + IEEE80211_WEP_ICV_LEN);
      key_idx = fbuf[IEEE80211_WEP_IV_IDX_POS] >> IEEE80211_WEP_IV_IDX_BIT;

      /* check key is set */
      if (key_idx >= CORE_NUM_WEP_KEYS) {
         ILOG1_DD("CID-%04x: KEY:%d index is wrong", mtlk_vap_get_oid(core->vap_handle), key_idx);
         res = MTLK_ERR_NOT_IN_USE;
         goto end;
      }

      if(!mtlk_core_crypto_decrypt(core, key_idx, fbuf, fbuf + IEEE80211_WEP_IV_LEN, flen)) {
         ILOG1_D("CID-%04x: Auth frame Decryption fail", mtlk_vap_get_oid(core->vap_handle));
         res = MTLK_ERR_NOT_IN_USE;
         goto end;
      }
      else {
         ILOG1_D("CID-%04x: Auth frame Decryption succsess", mtlk_vap_get_oid(core->vap_handle));
      }

      /* remove security header */
      memmove(fbuf, fbuf + IEEE80211_WEP_IV_LEN, flen);

    }
    else {
      if (mtlk_vap_is_ap(core->vap_handle) || !is_multicast_ether_addr(head->dst_addr.au8Addr)) {
        if (flen < IEEE80211_CCMP_HEADER_LEN) {
          ILOG1_D("CID-%04x: Management Frame length is wrong", mtlk_vap_get_oid(core->vap_handle));
          res = MTLK_ERR_NOT_IN_USE;
          goto end;
        }
        /* Remove security header from managment frames with decreasing the length */
        mtlk_dump(3, head, *buflen, "dump of man frame with security header");
        flen -= IEEE80211_CCMP_HEADER_LEN;
        *buflen -= IEEE80211_CCMP_HEADER_LEN;
        memmove(fbuf, fbuf + IEEE80211_CCMP_HEADER_LEN, flen);
        fd->pmf_flags |= MTLK_MGMT_FRAME_DECRYPTED;
        fd->pmf_flags |= MTLK_MGMT_FRAME_IV_STRIPPED;
      }
      /* else: If received BIP frame in STA mode, mac80211 will handle/decrypt */
    }
  }
  else {
    if ((subtype == MAN_TYPE_DISASSOC) || (subtype == MAN_TYPE_DEAUTH)) {
      if (mtlk_vap_is_ap(core->vap_handle) && (sta) && mtlk_sta_is_mfp(sta)) {
        ILOG1_D("CID-%04x: Unprotected Management Frame received from MFP-enabled STA", mtlk_vap_get_oid(core->vap_handle));
        fd->notify_hapd_supplicant = FALSE;
        return res;
      }
    }
  }

  switch (subtype)
  {
  case MAN_TYPE_BEACON:
    fd->notify_hapd_supplicant = TRUE;

    /* In DFS channels, first beacon is a signal for STA tx unblock */
    if (master_core->slow_ctx->is_block_tx == TRUE &&
        current_chandef->wait_for_beacon == TRUE) {
      current_chandef->wait_for_beacon = FALSE;
      ILOG0_V("Beacon found on DFS channel, Trying to exit block_tx mode");
      _mtlk_core_switch_channel_normal(master_core, current_chandef);
    }

    /* fall-through */
  case MAN_TYPE_PROBE_RES:

    if (!fd->chan)
    {
      ILOG3_V("Beacon or Probe Response received when channel is not precisely known, ignoring");
      ILOG3_DDD("Current chandef info: center_freq1=%u, width=%u, chan->center_freq=%u",
                current_chandef->center_freq1, current_chandef->width, current_chandef->chan.center_freq);
      fd->notify_hapd_supplicant = FALSE;
      fd->report_bss = FALSE;
      fd->obss_beacon_report = FALSE;
      break;
    }

    CPU_STAT_SPECIFY_TRACK(CPU_STAT_ID_RX_MGMT_BEACON);
    if (flen < sizeof(frame_beacon_head_t)) {
      ILOG1_D("CID-%04x: Beacon or Probe Response Management Frame length is wrong",
        mtlk_vap_get_oid(core->vap_handle));
      res = MTLK_ERR_NOT_IN_USE;
      goto end;
    }
    beacon_head = (frame_beacon_head_t *)fbuf;
    fbuf += sizeof(frame_beacon_head_t);
    flen -= sizeof(frame_beacon_head_t);

    bss_data = &cache->temp_bss_data;

    memset(bss_data, 0, sizeof(*bss_data));
    bss_data->channel = freq2lowchannum(fd->chan->center_freq, CW_20);

    bss_data->bssid = head->bssid;
    ILOG4_YD("BSS %Y found on channel %d", bss_data->bssid.au8Addr,
          bss_data->channel);
    bss_data->capability = MAC_TO_HOST16(beacon_head->capability);
    bss_data->beacon_interval = MAC_TO_HOST16(beacon_head->beacon_interval);
    bss_data->beacon_timestamp = WLAN_TO_HOST64(beacon_head->beacon_timestamp);
    bss_data->received_timestamp = mtlk_osal_timestamp();
    ILOG4_D("Advertised capabilities : 0x%04X", bss_data->capability);
    if (bss_data->capability & CAPABILITY_IBSS_MASK) {
      bss_data->bss_type = UMI_BSS_ADHOC;
      ILOG4_V("BSS type is Ad-Hoc");
    } else {
      bss_data->bss_type = UMI_BSS_INFRA;
      ILOG4_V("BSS type is Infra");
      if (bss_data->capability & CAPABILITY_NON_POLL) {
        bss_data->bss_type = UMI_BSS_INFRA_PCF;
        ILOG4_V("PCF supported");
      }
    }

    // RSSI
    MTLK_STATIC_ASSERT(sizeof(fd->phy_info->rssi) == sizeof(bss_data->all_rssi));
    MTLK_STATIC_ASSERT(sizeof(fd->phy_info->rssi) == sizeof(uint32)); /* must be 32bits total */
    wave_memcpy(bss_data->all_rssi, sizeof(bss_data->all_rssi),
                fd->phy_info->rssi, sizeof(fd->phy_info->rssi));

    bss_data->max_rssi = fd->phy_info->max_rssi;
    bss_data->noise    = fd->phy_info->noise_estimation;

    ILOG4_YDDD("BSS %Y, channel %u, max_rssi is %d, noise is %d",
         bss_data->bssid.au8Addr, bss_data->channel,
         bss_data->max_rssi,
         bss_data->noise);

    // Process information elements
    ie_process(context, fbuf, (int32)flen, bss_data);

    /* check whether any BSS-reporting functions of the kernel should be called */
    if (is_scan_running(scan_support))
    {
      if (!is_scan_ignorant(scan_support)) /* only do this at precise moments within a scan */
      {
        ILOG2_SYDDD("SSID '%s' (%Y) heard during scan at rssi %i (subtype %hu, channel %hu)",
                   bss_data->essid, bss_data->bssid.au8Addr, bss_data->max_rssi, subtype, bss_data->channel);
        fd->report_bss = passes_scan_filters(scan_support, bss_data->essid, bss_data->max_rssi);
      }
    }
    else if (MAN_TYPE_BEACON == subtype)
    {
      /* Report 20/40 CoEx OBSS or 40 MHz Intolerant */
      /* IEEE Std 802.11 2012 10.15.12 TE-B */
      if (bss_data->forty_mhz_intolerant)
        fd->obss_beacon_report = TRUE;

      /* For IE HT Protection update */
      if (!bss_data->is_ht || (bss_data->erp_info & ERP_INFO_NON_ERP_PRESENT))
        fd->obss_beacon_report = TRUE;
    }

    bss_data = &cache->temp_bss_data;

    if (wds_on && (MAN_TYPE_BEACON == subtype)) {
        wds_beacon_data_t beacon_data;
        memset(&beacon_data, 0, sizeof(wds_beacon_data_t));
        beacon_data.ie_data            = fbuf;
        beacon_data.ie_data_len        = flen;
        beacon_data.basic_rate_set     = bss_data->basic_rate_set;
        beacon_data.op_rate_set        = bss_data->operational_rate_set;
        beacon_data.beacon_interval    = bss_data->beacon_interval;
        beacon_data.channel            = bss_data->channel;
        beacon_data.second_chan_offset = bss_data->secondary_channel_offset;
        beacon_data.dtim_period        = bss_data->dtim_period;
        beacon_data.protection         = (bss_data->erp_info & (NON_ERP_IE_MASK | USE_PROTECTION_IE_MASK)) ? 1 : 0;
        beacon_data.short_preamble     = (bss_data->capability & CAPABILITY_SHORT_PREAMBLE) ? 1 : 0;
        beacon_data.short_slot_time    = (bss_data->capability & CAPABILITY_SHORT_SLOT_TIME) ? 1 : 0;
        beacon_data.is_ht              = bss_data->is_ht;
        beacon_data.is_vht             = bss_data->is_vht;
        beacon_data.is_he              = bss_data->is_he;
        beacon_data.max_rssi           = bss_data->max_rssi;
        beacon_data.addr               = head->src_addr;
        /* HT */
        if ((bss_data->is_ht) && (bss_data->ht_cap_info_len) && (bss_data->ht_cap_info_len <= HT_CAP_IE_LEN)) {
          beacon_data.ht_cap_info_len = bss_data->ht_cap_info_len;
          wave_memcpy(&beacon_data.ht_cap_info, sizeof(beacon_data.ht_cap_info),
              bss_data->ht_cap_info, bss_data->ht_cap_info_len);
        }
        /* VHT */
        if ((bss_data->is_vht) && (bss_data->vht_cap_info_len) && (bss_data->vht_cap_info_len <= VHT_CAP_IE_LEN)) {
          beacon_data.vht_cap_info_len = bss_data->vht_cap_info_len;
          wave_memcpy(&beacon_data.vht_cap_info, sizeof(beacon_data.vht_cap_info),
              bss_data->vht_cap_info, bss_data->vht_cap_info_len);
        }
        /* HE */
        if ((bss_data->is_he) && (bss_data->he_cap_info_len) && (bss_data->he_cap_info_len <= IEEE80211_HE_CAPABILITIES_SIZE)) {
          beacon_data.he_cap_info_len = bss_data->he_cap_info_len;
          wave_memcpy(&beacon_data.he_cap_info, sizeof(beacon_data.he_cap_info),
              bss_data->he_cap_info, bss_data->he_cap_info_len);
        }

        core_wds_beacon_process(master_core, &beacon_data);
    }

    break;
  case MAN_TYPE_PROBE_REQ:
    if (fd->chan)
      fd->notify_hapd_supplicant = TRUE;

    bss_data = &cache->temp_bss_data;
    ie_process(context, fbuf, (int32)flen, bss_data);
    if (WPS_IE_FOUND(bss_data))
      fd->probe_req_wps_ie = TRUE;
    if (INTERWORKING_IE_FOUND(bss_data))
      fd->probe_req_interworking_ie = TRUE;
    if (VSIE_FOUND(bss_data))
      fd->probe_req_vsie = TRUE;
    if (HE_IE_FOUND(bss_data)) {
      fd->probe_req_he_ie = TRUE;
    }
    break;
  case MAN_TYPE_ACTION:
    CPU_STAT_SPECIFY_TRACK(CPU_STAT_ID_RX_MGMT_ACTION);
    {
      uint8 category = *(uint8 *)fbuf;
      fd->notify_hapd_supplicant = FALSE;

      if (flen < sizeof(category)) {
        ILOG1_D("CID-%04x: Action Management Frame length is wrong",
          mtlk_vap_get_oid(core->vap_handle));
        res = MTLK_ERR_NOT_IN_USE;
        goto end;
      }
      fbuf += sizeof(category);
      flen -= sizeof(category);

      ILOG3_D("ACTION: category=%d", (int)category);

      switch (category) {
      case ACTION_FRAME_CATEGORY_BLOCK_ACK:
        ELOG_V("Block ACK received");
        MTLK_ASSERT(0);
        break;
      case ACTION_FRAME_CATEGORY_VENDOR_SPECIFIC:
      case ACTION_FRAME_CATEGORY_VENDOR_SPECIFIC_PROTECTED:
        break;
      default:
        if (fd->chan)
          fd->notify_hapd_supplicant = TRUE;
        ILOG2_DPD("ACTION: category=%d, chan %p, notify %d", (int)category, fd->chan, fd->notify_hapd_supplicant);
        break;
      }
    }
    break;
  default:
    break;
  }

end:

  return res;
}

int
mtlk_process_ctl_frame (mtlk_handle_t context, uint8 *fbuf, int32 flen)
{
  int res = MTLK_ERR_OK;
  uint16 fc, subtype;

  if (flen < sizeof(fc))
    return res;

  fc = MAC_TO_HOST16(*(uint16 *)fbuf);
  subtype = WLAN_FC_GET_STYPE(fc);

  ILOG4_D("Subtype is 0x%04X", subtype);

  return res;
}

/*  MTLK Unique Identifier  */
#define MTLK_PROPRIETARY_OUI_OCTET_0          (0x00)
#define MTLK_PROPRIETARY_OUI_OCTET_1          (0x09)
#define MTLK_PROPRIETARY_OUI_OCTET_2          (0x86)

#define MTLK_PROPRIETARY_OBSOLETE_OUI_OCTET_0 (0xA5)
#define MTLK_PROPRIETARY_OBSOLETE_OUI_OCTET_1 (0xB8)
#define MTLK_PROPRIETARY_OBSOLETE_OUI_OCTET_2 (0x76)

#define W101_PROPRIETARY_OUI_OCTET_0          (0x00)
#define W101_PROPRIETARY_OUI_OCTET_1          (0x03)
#define W101_PROPRIETARY_OUI_OCTET_2          (0x7F)

#define INTEL_VENDOR_SPECIFIC_OUI_OCTET_0     (0x00)
#define INTEL_VENDOR_SPECIFIC_OUI_OCTET_1     (0x17)
#define INTEL_VENDOR_SPECIFIC_OUI_OCTET_2     (0x35)

#define BROADCOM_VENDOR_SPECIFIC_OUI_OCTET_0  0x00
#define BROADCOM_VENDOR_SPECIFIC_OUI_OCTET_1  0x10
#define BROADCOM_VENDOR_SPECIFIC_OUI_OCTET_2  0x18


/**
  FIXCFG80211: TODO: description
*/
int __MTLK_IFUNC
mtlk_mgmt_parse_elems (
  const uint8 *start,
  uint32 len,
  wv_ie80211_elems *elems)
{
  uint32 left = len;
  const uint8 *pos = start;

  MTLK_ASSERT(NULL != start);
  MTLK_ASSERT(NULL != elems);

  memset(elems, 0, sizeof(wv_ie80211_elems));

  /* minimal length of information element = (id & size) */
  while (left >= 2) {
    uint8 ie_id;
    uint8 ie_len;

    ie_id = *pos++;   /* Element ID */
    ie_len = *pos++;  /* Length */
    left -= 2;

    if (ie_len > left) {
      return MTLK_ERR_CORRUPTED;
    }

    switch (ie_id) {
      case WLAN_EID_BSS_COEX_2040:
        /* 20/40 BSS Coexistence (IEEE 802.11, 8.4.2.62) */
        if (1 != ie_len) {
          return MTLK_ERR_CORRUPTED;
        }
        elems->bss_coex_20_40 = pos;
        break;
      case WLAN_EID_VENDOR_SPECIFIC:
        /* Vendor Specific (IEEE 802.11, 8.4.2.28) */
        if (ie_len >= 4) {
          if ((pos[0] == MTLK_PROPRIETARY_OBSOLETE_OUI_OCTET_0) &&
              (pos[1] == MTLK_PROPRIETARY_OBSOLETE_OUI_OCTET_1) &&
              (pos[2] == MTLK_PROPRIETARY_OBSOLETE_OUI_OCTET_2)) {
            /* Lantiq OLD (Metalink) */
            elems->vendor_metalink = pos;
          }
          else if ((pos[0] == MTLK_PROPRIETARY_OUI_OCTET_0) &&
                   (pos[1] == MTLK_PROPRIETARY_OUI_OCTET_1) &&
                   (pos[2] == MTLK_PROPRIETARY_OUI_OCTET_2)) {
            /* Lantiq NEW */
            elems->vendor_lantiq = pos;
          }
          else if ((pos[0] == W101_PROPRIETARY_OUI_OCTET_0) &&
                   (pos[1] == W101_PROPRIETARY_OUI_OCTET_1) &&
                   (pos[2] == W101_PROPRIETARY_OUI_OCTET_2)) {
            /* W101 */
            elems->vendor_w101 = pos;
          }
          else if ((pos[0] == INTEL_VENDOR_SPECIFIC_OUI_OCTET_0) &&
                   (pos[1] == INTEL_VENDOR_SPECIFIC_OUI_OCTET_1) &&
                   (pos[2] == INTEL_VENDOR_SPECIFIC_OUI_OCTET_2)) {
            /* Intel */
            elems->vendor_intel = pos;
          }
          else if ((pos[0] == BROADCOM_VENDOR_SPECIFIC_OUI_OCTET_0) &&
                   (pos[1] == BROADCOM_VENDOR_SPECIFIC_OUI_OCTET_1) &&
                   (pos[2] == BROADCOM_VENDOR_SPECIFIC_OUI_OCTET_2)) {
            /* Broadcom */
            elems->vendor_brcm = pos;
          }
          else if ((pos[0] == ((WLAN_OUI_WFA >> 16) & 0xff)) &&
                   (pos[1] == ((WLAN_OUI_WFA >> 8)  & 0xff)) &&
                   (pos[2] == ((WLAN_OUI_WFA >> 0)  & 0xff))) {
            /* Wi-Fi Alliance */
            elems->vendor_wfa = pos;
          }
        }
        break;
      case WLAN_EID_RSN:
      {
         const uint8 *rsn = pos;
         int rsn_len = ie_len;
         uint16 tmp;
         if (rsn_len < RSNE_VERSION_LEN+RSNE_GROUP_DATA_CSUITE_LEN)
           break;
         rsn_len -= RSNE_VERSION_LEN+RSNE_GROUP_DATA_CSUITE_LEN;
         rsn     += RSNE_VERSION_LEN+RSNE_GROUP_DATA_CSUITE_LEN;
         if (rsn_len < RSNE_PAIRWISE_CSUITE_COUNT_LEN)
           break;
         tmp = *(uint16 *)rsn; /*pairwise cipher suite count */
         rsn_len -= RSNE_PAIRWISE_CSUITE_COUNT_LEN;
         rsn     += RSNE_PAIRWISE_CSUITE_COUNT_LEN;
         if ((!tmp) || (tmp * RSNE_PAIRWISE_CSUITE_LEN >= rsn_len))
           break;
         rsn_len -= tmp * RSNE_PAIRWISE_CSUITE_LEN;
         rsn     += tmp * RSNE_PAIRWISE_CSUITE_LEN;
         if (rsn_len < RSNE_AKM_SUITE_COUNT_LEN)
           break;
         tmp = *(uint16 *)rsn; /*AKM suite count */
         rsn_len -= RSNE_AKM_SUITE_COUNT_LEN;
         rsn     += RSNE_AKM_SUITE_COUNT_LEN;
         if ((!tmp) || (tmp * RSNE_AKM_SUITE_LEN >= rsn_len))
           break;
         rsn     += tmp * RSNE_AKM_SUITE_LEN;
         rsn_len -= tmp * RSNE_AKM_SUITE_LEN;
         if (rsn_len < RSNE_CAPAB_LEN)
           break;
         elems->rsne_capab = (uint16 *)rsn;
      }
      break;
      case WLAN_EID_OPMODE_NOTIF:
        if (1 != ie_len) {
          return MTLK_ERR_CORRUPTED;
        }
        elems->opmode_notif = pos;
        break;
      default:
        break;
    }

    pos  += ie_len;
    left -= ie_len;
  }

  if (0 != left) {
    return MTLK_ERR_CORRUPTED;
  }

  return MTLK_ERR_OK;
}

int mgmt_frame_filter_init (mgmt_frame_filter_t *frame_filter)
{
  memset(frame_filter, 0, sizeof(mgmt_frame_filter_t));
  return MTLK_ERR_OK;
}

void mgmt_frame_filter_cleanup (mgmt_frame_filter_t *frame_filter)
{
  memset(frame_filter, 0, sizeof(mgmt_frame_filter_t));
}

BOOL mgmt_frame_filter_allows (mtlk_core_t *nic, mgmt_frame_filter_t *frame_filter, uint8 *frame, unsigned len, BOOL *is_broadcast)
{
  frame_head_t frame_template;
  uint16 frame_ctrl, subtype;
  unsigned i, idx;
  BOOL found = FALSE;
  frame_head_t *head = (frame_head_t *)frame;

  MTLK_ASSERT(frame != NULL);
  MTLK_ASSERT(len > sizeof(frame_head_t));

  frame_ctrl = WLAN_TO_HOST16(head->frame_control);
  subtype = (frame_ctrl & FRAME_SUBTYPE_MASK) >> FRAME_SUBTYPE_SHIFT;

  if (is_broadcast_ether_addr(head->dst_addr.au8Addr) || is_multicast_ether_addr(head->dst_addr.au8Addr)) {
    *is_broadcast = TRUE;
    return TRUE;
  }

  if (subtype == MAN_TYPE_BEACON) {
    return TRUE;
  }

  wave_memcpy(&frame_template, sizeof(frame_template), frame, sizeof(frame_head_t));
  head = &frame_template;
  /* clear duration in MAC header */
  head->duration = 0;
  /* clear retry bit in frame control */
  frame_ctrl &= ~(BIT(11));
  head->frame_control = HOST_TO_WLAN16(frame_ctrl);

  /* search for the frame template in queue */
  idx = (frame_filter->idx - frame_filter->count) & (MGMT_FRAME_BUF_COUNT - 1);
  for (i = 0; i < frame_filter->count; i++) {
    if(!memcmp(&frame_filter->buffer[idx], &frame_template, sizeof(frame_head_t))) {
      found = TRUE;
      break;
    }
    idx++;
    idx &= (MGMT_FRAME_BUF_COUNT - 1);
  }
  if (found) {
    ILOG3_D("MGMT frame is filtered out %d", subtype);
    mtlk_dump(3, head, sizeof(frame_head_t), "head");
    mtlk_core_inc_cnt(nic, MTLK_CORE_CNT_RX_MAN_FRAMES_RETRY_DROPPED);
    return FALSE;
  }

  /* put new template in a queue */
  idx = frame_filter->idx;
  frame_filter->buffer[idx] = frame_template;
  frame_filter->idx = (idx++) & (MGMT_FRAME_BUF_COUNT - 1);
  if (frame_filter->count < MGMT_FRAME_BUF_COUNT)
    frame_filter->count++;

  return TRUE;
}
