/******************************************************************************

                               Copyright (c) 2017
                                 Intel Corporation

  For licensing information, see the file 'LICENSE' in the root folder of
  this software module.

******************************************************************************/
/*
 * $Id$
 *
 * 
 *
 * 802.11 AX D3.0 related definitions.
 *
 * Originally written by Andrei Grosmanis
 *
 */

#ifndef __WAVE_80211AX_H_
#define __WAVE_80211AX_H_

#define HE_EXTENSION_LEN 0
#define HE_MAC_LEN 6
#define HE_PHY_LEN 11
#define HE_MAC_PHY_LEN (HE_EXTENSION_LEN + HE_MAC_LEN + HE_PHY_LEN)
#define HE_MCS_NSS_80MHZ_LEN 4
#define HE_MCS_NSS_160MHZ_LEN (HE_MCS_NSS_80MHZ_LEN)
#define HE_MCS_NSS_8080MHZ_LEN (HE_MCS_NSS_160MHZ_LEN)
#define HE_MCS_NSS_MAX_LEN (HE_MCS_NSS_80MHZ_LEN + HE_MCS_NSS_160MHZ_LEN + HE_MCS_NSS_8080MHZ_LEN)
#define HE_PPE_TH_MAX_LEN 13
#define HE_OPERATION_PARAMETERS_LEN 11 /* "HE Operation Parameters" field */

#define HE_MAC_PHY_OFFSET 0
#define HE_MCS_NSS_OFFSET (HE_MAC_PHY_OFFSET + HE_MAC_PHY_LEN)
#define HE_PPE_TH_80_MHZ_OFFSET (HE_MCS_NSS_OFFSET + HE_MCS_NSS_80MHZ_LEN)
#define HE_PPE_TH_160_MHZ_OFFSET (HE_PPE_TH_80_MHZ_OFFSET + HE_MCS_NSS_160MHZ_LEN)
#define HE_PPE_TH_8080_MHZ_OFFSET (HE_PPE_TH_160_MHZ_OFFSET + HE_MCS_NSS_8080MHZ_LEN)
#define HE_OPERATION_PARAMETERS_OFFSET (HE_PPE_TH_8080_MHZ_OFFSET + HE_PPE_TH_MAX_LEN)
#define HE_MAC_PHY_CHAN_WIDTH_OFFSET 7
#define HE_MAC_PHY_CHAN_WIDTH_B2 3
#define HE_MAC_PHY_CHAN_WIDTH_B3 4

#define HE_MAC_TWT_RESPONDER_SUPPORT        2
#define HE_MAC_TWT_RESPONDER_SUPPORT_WIDTH  1
#define HE_MAC_TWT_RESPONDER_NOT_SUPPORTED  0
#define HE_MAC_TWT_RESPONDER_SUPPORTED      1

#define IE_HE_LENGTH_LEN 1
#define IE_HE_MAC_TWT_RESPONDER_OFFSET     (HE_EXTENSION_LEN + IE_HE_LENGTH_LEN + 1)
#define IE_HE_PHY_CHANNEL_WIDTH_SET_OFFSET (HE_EXTENSION_LEN + IE_HE_LENGTH_LEN + HE_MAC_LEN)
#define IE_LENGTH_ELEM_ID_LEN 1
#define IE_HE_MCS_NSS_OFFSET (IE_LENGTH_ELEM_ID_LEN + IE_HE_LENGTH_LEN + HE_MAC_PHY_LEN)

#define HE_OPERATION_PARAMETERS_ER_SU_DISABLE_IDX 3
#define HE_OPERATION_PARAMETERS_ER_SU_DISABLE MTLK_BFIELD_INFO(0, 1)

#endif /* __WAVE_80211AX_H_ */
