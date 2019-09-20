/******************************************************************************

                               Copyright (c) 2012
                            Lantiq Deutschland GmbH

  For licensing information, see the file 'LICENSE' in the root folder of
  this software module.

******************************************************************************/
#ifndef __MTLK_WSS_DEBUG_H__
#define __MTLK_WSS_DEBUG_H__

#ifndef FALSE
#define FALSE (0)
#endif
#ifndef TRUE
#define TRUE  (1)
#endif

/* DEBUG statistic */
#ifdef  CPTCFG_IWLWAV_STAT_CNTRS_DEBUG
#define MTLK_WWSS_WLAN_STAT_DEBUG   TRUE
#else  /* CPTCFG_IWLWAV_STAT_CNTRS_DEBUG */
#define MTLK_WWSS_WLAN_STAT_DEBUG   FALSE
#endif /* CPTCFG_IWLWAV_STAT_CNTRS_DEBUG */

/* Note: might be configured individually in the future */
#define MTLK_WWSS_WLAN_STAT_GROUP_ALLWAYS_ALLOWED       TRUE
#define MTLK_WWSS_WLAN_STAT_GROUP_HOTP_MIN_ALLOWED      MTLK_WWSS_WLAN_STAT_DEBUG
#define MTLK_WWSS_WLAN_STAT_GROUP_HOTP_OPT_ALLOWED      MTLK_WWSS_WLAN_STAT_DEBUG
#define MTLK_WWSS_WLAN_STAT_GROUP_HW_SOURCE_ALLOWED     MTLK_WWSS_WLAN_STAT_DEBUG
#define MTLK_WWSS_WLAN_STAT_GROUP_OTHERS_ALLOWED        MTLK_WWSS_WLAN_STAT_DEBUG
        /* Some Analyzer pocessess */
#define MTLK_WWSS_WLAN_STAT_ANALYZER_RX_LONG_RSSI_ALLOWED   MTLK_WWSS_WLAN_STAT_GROUP_HOTP_OPT_ALLOWED
#define MTLK_WWSS_WLAN_STAT_ANALYZER_RX_RATE_ALLOWED        MTLK_WWSS_WLAN_STAT_GROUP_HOTP_OPT_ALLOWED
#define MTLK_WWSS_WLAN_STAT_ANALYZER_TX_RATE_ALLOWED        MTLK_WWSS_WLAN_STAT_GROUP_HOTP_OPT_ALLOWED

/* MTIDL statistic: Minimal or Full */
#define MTLK_MTIDL_PEER_STAT_FULL                       MTLK_WWSS_WLAN_STAT_DEBUG
#define MTLK_MTIDL_WLAN_STAT_FULL                       MTLK_WWSS_WLAN_STAT_DEBUG
#define MTLK_MTIDL_HW_STAT_FULL                         MTLK_WWSS_WLAN_STAT_DEBUG

#endif /* __MTLK_WSS_DEBUG_H__ */
