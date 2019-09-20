/******************************************************************************

                               Copyright (c) 2012
                            Lantiq Deutschland GmbH

  For licensing information, see the file 'LICENSE' in the root folder of
  this software module.

******************************************************************************/
/****************************************************************************
****************************************************************************
**
** COMPONENT:      ENET Upper MAC SW
**
** MODULE:         $File: //bwp/enet/demo153_sw/develop/src/shared/mhi_statistics.h $
**
** VERSION:        $Revision: #2 $
**
** DATED:          $Date: 2004/03/22 $
**
** AUTHOR:         S Sondergaard
**
** DESCRIPTION:    Statistics header
**
**
** LAST MODIFIED BY:   $Author: prh $
**
**
****************************************************************************
*
*   
*
*   
*
****************************************************************************/

#ifndef __MHI_STATISTICS_INC
#define __MHI_STATISTICS_INC


#define   MTLK_PACK_ON
#include "mtlkpack.h"

#include "mtidl_c.h" // external from the driver trunk. defines the MTIDL.


/* Reason for network connection/disconnection */

MTIDL_ENUM_START
  MTIDL_ENUM_ENTRY(UMI_BSS_NEW_NETWORK,            0,  "BSS Created")
  MTIDL_ENUM_ENTRY(UMI_BSS_JOINED,                 1,  "BSS Joined")
  MTIDL_ENUM_ENTRY(UMI_BSS_DEAUTHENTICATED,        2,  "Peer deauthenticated")
  MTIDL_ENUM_ENTRY(UMI_BSS_DISASSOCIATED,          3,  "Peer disassociated")
  MTIDL_ENUM_ENTRY(UMI_BSS_JOIN_FAILED,            4,  "Join failed")
  MTIDL_ENUM_ENTRY(UMI_BSS_AUTH_FAILED,            5,  "Authentication failed")
  MTIDL_ENUM_ENTRY(UMI_BSS_ASSOC_FAILED,           6,  "Association failed")
  MTIDL_ENUM_ENTRY(UMI_BSS_PEERAP_INCOMPATIBLE,    7,  "Peer AP capabilities incompatible")
MTIDL_ENUM_END(mtlk_wssa_peer_removal_reasons_t)

MTIDL_ENUM_START
  MTIDL_ENUM_ENTRY(UMI_BSS_AUTH_OPEN,       15, "Open")
  MTIDL_ENUM_ENTRY(UMI_BSS_AUTH_SHARED_KEY, 16, "Shared key")
MTIDL_ENUM_END(mtlk_wssa_authentication_type_t)

/* Status codes */
MTIDL_ENUM_START
  MTIDL_ENUM_ENTRY(FM_STATUSCODE_SUCCESSFUL,                          0,  "Successful")
  MTIDL_ENUM_ENTRY(FM_STATUSCODE_FAILURE,                             1,  "Failure")
  MTIDL_ENUM_ENTRY(FM_STATUSCODE_PREVIOUS_AUTH_NO_LONGER_VALID,       2,  "Previous authentication no longer valid")
  MTIDL_ENUM_ENTRY(FM_STATUSCODE_RESERVED,                            4,  "Reserved")
  MTIDL_ENUM_ENTRY(FM_STATUSCODE_CANT_SUPPORT_CAPABILITIES,          10,  "Can't support capabilities")
  MTIDL_ENUM_ENTRY(FM_STATUSCODE_CANNOT_CONFIRM_ASSOCIATION,         11,  "Can't confirm association")
  MTIDL_ENUM_ENTRY(FM_STATUSCODE_NON_802_11_REASON,                  12,  "Non 802.11 reason")
  MTIDL_ENUM_ENTRY(FM_STATUSCODE_ALGORITHM_NOT_SUPPORTED,            13,  "Algorithm not supported")
  MTIDL_ENUM_ENTRY(FM_STATUSCODE_AUTH_SEQ_UNEXPECTED,                14,  "Authentication sequence unexpected")
  MTIDL_ENUM_ENTRY(FM_STATUSCODE_CHALLENGE_FAILURE,                  15,  "Challenge failure")
  MTIDL_ENUM_ENTRY(FM_STATUSCODE_AUTH_TIMEOUT,                       16,  "Authentication timeout")
  MTIDL_ENUM_ENTRY(FM_STATUSCODE_STATION_OVERLOAD,                   17,  "Station overloaded")
  MTIDL_ENUM_ENTRY(FM_STATUSCODE_BASIC_RATES_UNSUPPORTED,            18,  "Basic rates unsupported")
  MTIDL_ENUM_ENTRY(FM_STATUSCODE_ASSOC_DENIED_REQ_SHORT_PREAMBLE,    19,  "Denied due to short preamble")
  MTIDL_ENUM_ENTRY(FM_STATUSCODE_ASSOC_DENIED_REQ_PBCC,              20,  "Denied due to PBCC")
  MTIDL_ENUM_ENTRY(FM_STATUSCODE_ASSOC_DENIED_REQ_CHANNEL_AGILITY,   21,  "Denied due to channel agility")
  MTIDL_ENUM_ENTRY(FM_STATUSCODE_ASSOC_DENIED_HT_NOT_SUPPORTED,      27,  "Denied due to not supporting HT features")
  MTIDL_ENUM_ENTRY(FM_STATUSCODE_ASSOC_REQ_REJECTED_TEMPORARILY_TRY_LATER,       30,  "Association request rejected temporarily; try again later")
  MTIDL_ENUM_ENTRY(FM_STATUSCODE_ROBUST_MAN_FRAME_POLICY_VIOLATION,  31,  "Robust management frame policy violation")
  MTIDL_ENUM_ENTRY(FM_STATUSCODE_ASSOC_DENIED_QOS_RELATED_REASON,    32,  "Denied due to QoS")
  MTIDL_ENUM_ENTRY(FM_STATUSCODE_STA_LEAVING_BSS,                    36,  "STA is leaving")
  MTIDL_ENUM_ENTRY(FM_STATUSCODE_INVALID_RSN_IE_CAPABILITIES,        45,  "Invalid RSN IE capabilities")
  MTIDL_ENUM_ENTRY(FM_STATUSCODE_BEACON_TIMEOUT,                     101,  "STA has lost contact with network")
  MTIDL_ENUM_ENTRY(FM_STATUSCODE_ROAMING,                            102,  "STA is roaming to another BSS")
  MTIDL_ENUM_ENTRY(FM_STATUSCODE_MANUAL_DISCONNECT,                  103,  "UMI has forced a disconnect")
  MTIDL_ENUM_ENTRY(FM_STATUSCODE_NO_NETWORK,                         104,  "There is no network to join")
  MTIDL_ENUM_ENTRY(FM_STATUSCODE_IBSS_COALESCE,                      105,  "IBSS is coalescing with another one")
  MTIDL_ENUM_ENTRY(FM_STATUSCODE_11H,                                106,  "Radar detected on current channel")
  MTIDL_ENUM_ENTRY(FM_STATUSCODE_AGED_OUT,                           107,  "Station timed out")
  MTIDL_ENUM_ENTRY(FM_STATUSCODE_ASSOCIATED,                         108,  "Station associated successfully")
  MTIDL_ENUM_ENTRY(FM_STATUSCODE_INACTIVITY,                         109,  "Peer data timeout")
  MTIDL_ENUM_ENTRY(FM_STATUSCODE_USER_REQUEST,                       110,  "User request")
  MTIDL_ENUM_ENTRY(FM_STATUSCODE_PEER_PARAMS_CHANGED,                111,  "Peer reconfigured and new parameters not supported")
  MTIDL_ENUM_ENTRY(FM_STATUSCODE_MAC_ADDRESS_FILTER,                 112,  "STA's MAC address is not allowed in this BSS")
  MTIDL_ENUM_ENTRY(FM_STATUSCODE_W101_ASSOCIATED,                    113,  "W101 Station associated successfully")
  MTIDL_ENUM_ENTRY(FM_STATUSCODE_STA_ALREADY_AUTHENTICATED,          114,  "STA tried to authenticate while already authenticated")
  MTIDL_ENUM_END(mtlk_wssa_connection_status_t)


/*********************************************************************************************/
/******************** MTIDL - Metalink Information Definition Language ***********************/
/********************       Debug statistics, delivered to driver      ***********************/
/*********************************************************************************************/

/* Definitions required for the MTIDL*/ 

	/* IDs list */
	MTIDL_ID_LIST_START
		MTIDL_ID(DBG_STATS_RX_DISCARD_REASONS,	0)
		MTIDL_ID(DBG_STATS_TX_DISCARD_REASONS,	1)
		MTIDL_ID(DBG_STATS_STATUS,				2)
		MTIDL_ID(DBG_STATS_SECURITY,			3)
		MTIDL_ID(DBG_STATS_AGGREGATIONS,		4)
		MTIDL_ID(DBG_STATS_NUM_OF_STATS,		5)
	MTIDL_ID_LIST_END

MTIDL_ID_LIST_START
		MTIDL_ID(DBG_STATS_FWSTATUS,		1000)
MTIDL_ID_LIST_END

	
	/* Types list */
	MTIDL_TYPE_LIST_START
		MTIDL_TYPE(MTIDL_TYPE_NONE,   0)
		MTIDL_TYPE(MTIDL_INFORMATION, 1)
		MTIDL_TYPE(MTIDL_STATISTICS,  2)
		MTIDL_TYPE(MTIDL_EVENT,       3)
	MTIDL_TYPE_LIST_END

	/* Levels list */
	MTIDL_LEVEL_LIST_START 
		MTIDL_LEVEL(MTIDL_PROVIDER_HW,    1)
		MTIDL_LEVEL(MTIDL_PROVIDER_WLAN,  2)
		MTIDL_LEVEL(MTIDL_PROVIDER_PEER,  3)
	MTIDL_LEVEL_LIST_END   

	/* Sources list */
	MTIDL_SOURCE_LIST_START
		MTIDL_SOURCE(MTIDL_SRC_FW,  1)
		MTIDL_SOURCE(MTIDL_SRC_DRV, 2)
	MTIDL_SOURCE_LIST_END


#define   MTLK_PACK_OFF
#include "mtlkpack.h"


#endif /* !__MHI_STATISTICS_INC */
