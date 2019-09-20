/******************************************************************************

                               Copyright (c) 2012
                            Lantiq Deutschland GmbH

  For licensing information, see the file 'LICENSE' in the root folder of
  this software module.

******************************************************************************/
#ifndef __MTLK_WSSA_DRVINFO_H__
#define __MTLK_WSSA_DRVINFO_H__

#ifdef MTLK_LEGACY_STATISTICS
#include "mhi_statistics.h"
#endif
#include "mtlk_wss_debug.h"
#include "mhi_ieee_address.h"

#ifdef MTLK_LEGACY_STATISTICS
#define MTLK_PACK_ON
#include "mtlkpack.h"

MTIDL_ID_LIST_START
  MTIDL_ID(MTLK_WSSA_DRV_CAPABILITIES_PEER,     0)
  MTIDL_ID(MTLK_WSSA_DRV_STATUS_PEER,           1)
  MTIDL_ID(MTLK_WSSA_DRV_STATUS_HW,             2)
  MTIDL_ID(MTLK_WSSA_DRV_TXM_STATS,             3)
  MTIDL_ID(MTLK_WSSA_DRV_STATUS_WLAN,           4)
  MTIDL_ID(MTLK_WSSA_DRV_EVENT_CONNECTION,      5)
  MTIDL_ID(MTLK_WSSA_DRV_EVENT_DISCONNECTION,   7)
  MTIDL_ID(MTLK_WSSA_DRV_RECOVERY,              9)
  MTIDL_ID(MTLK_WSSA_DRV_DEBUG_STATUS_PEER,    10)
  MTIDL_ID(MTLK_WSSA_DRV_DEBUG_STATUS_HW,      11)
  MTIDL_ID(MTLK_WSSA_DRV_DEBUG_STATUS_WLAN,    12)

  MTIDL_ID(MTLK_WSSA_DRV_TR181_ERROR,          13)
  MTIDL_ID(MTLK_WSSA_DRV_TR181_HW,             14)
  MTIDL_ID(MTLK_WSSA_DRV_TR181_HW_STATS,       15)
  MTIDL_ID(MTLK_WSSA_DRV_TR181_WLAN,           16)
  MTIDL_ID(MTLK_WSSA_DRV_TR181_PEER,           17)

  MTIDL_ID(MTLK_WSSA_DRV_TRAFFIC,              21)
  MTIDL_ID(MTLK_WSSA_DRV_MGMT_INFO,            22)

  MTIDL_ID(MTLK_WSSA_DRV_PEER_TRAFFIC,         31)
  MTIDL_ID(MTLK_WSSA_DRV_PEER_RATE_INFO_1,     32)
  MTIDL_ID(MTLK_WSSA_DRV_PEER_RATES_INFO,      33)
  MTIDL_ID(MTLK_WSSA_DRV_RETRANS_STATS,        34)
MTIDL_ID_LIST_END

/*------------------------------------------------------------------------------
 *  Sub-items for any purpose
 */

MTIDL_ITEM_START(ManFramesStats, MTIDL_TYPE_NONE, MTIDL_PROVIDER_WLAN, MTIDL_SRC_DRV, MTLK_WSSA_DRV_MGMT_INFO, "Management frames statistics")
  MTIDL_LONGVAL(MANFramesResQueue,              "Number of management frames in reserved queue")
  MTIDL_LONGVAL(MANFramesSent,                  "Number of management frames sent")
  MTIDL_LONGVAL(MANFramesConfirmed,             "Number of management frames confirmed")
  MTIDL_LONGVAL(MANFramesReceived,              "Number of management frames received")
  MTIDL_LONGVAL(MANFramesRetryDropped,          "Number of management frames dropped due to retries")
  MTIDL_LONGVAL(BssMgmtTxQueFull,               "Number of management frames dropped due to TX que full")
  MTIDL_LONGVAL(ProbeRespSent,                  "Number of probe responses sent")
  MTIDL_LONGVAL(ProbeRespDropped,               "Number of probe responses dropped")
MTIDL_ITEM_END(mtlk_wssa_drv_mgmt_stats_t)

MTIDL_ITEM_START(TrafficStat, MTIDL_TYPE_NONE, MTIDL_PROVIDER_WLAN, MTIDL_SRC_DRV, MTLK_WSSA_DRV_TRAFFIC, "Traffic Statistics")
  MTIDL_HUGEVAL(BytesSent                     , "BytesSent                - Number of bytes sent successfully (64-bit)")
  MTIDL_HUGEVAL(BytesReceived                 , "BytesReceived            - Number of bytes received (64-bit)")
  MTIDL_HUGEVAL(PacketsSent                   , "PacketsSent              - Number of packets transmitted (64-bit)")
  MTIDL_HUGEVAL(PacketsReceived               , "PacketsReceived          - Number of packets received (64-bit)")
  MTIDL_LONGVAL(UnicastPacketsSent            , "UnicastPacketsSent       - Number of unicast packets transmitted")
  MTIDL_LONGVAL(UnicastPacketsReceived        , "UnicastPacketsReceived   - Number of unicast packets received")
  MTIDL_LONGVAL(MulticastPacketsSent          , "MulticastPacketsSent     - Number of multicast packets transmitted")
  MTIDL_LONGVAL(MulticastPacketsReceived      , "MulticastPacketsReceived - Number of multicast packets received")
  MTIDL_LONGVAL(BroadcastPacketsSent          , "BroadcastPacketsSent     - Number of broadcast packets transmitted")
  MTIDL_LONGVAL(BroadcastPacketsReceived      , "BroadcastPacketsReceived - Number of broadcast packets received")
MTIDL_ITEM_END(mtlk_wssa_drv_traffic_stats_t)

MTIDL_ITEM_START(PeerTrafficStat, MTIDL_TYPE_NONE, MTIDL_PROVIDER_WLAN, MTIDL_SRC_DRV, MTLK_WSSA_DRV_PEER_TRAFFIC, "Peer Traffic Statistics")
  MTIDL_LONGVAL(BytesSent                     , "BytesSent                - Number of bytes sent successfully")
  MTIDL_LONGVAL(BytesReceived                 , "BytesReceived            - Number of bytes received")
  MTIDL_LONGVAL(PacketsSent                   , "PacketsSent              - Number of packets transmitted")
  MTIDL_LONGVAL(PacketsReceived               , "PacketsReceived          - Number of packets received")
MTIDL_ITEM_END(mtlk_wssa_peer_traffic_stats_t)

/* Per HW, per VAP and per STA */
MTIDL_ITEM_START(PeerRetransStat, MTIDL_TYPE_NONE, MTIDL_PROVIDER_WLAN, MTIDL_SRC_DRV, MTLK_WSSA_DRV_RETRANS_STATS, "Retransmition Statistics")
  MTIDL_LONGVAL(Retransmissions               , "Retransmissions          - Number of re-transmitted, from the last 100 packets sent")
  MTIDL_LONGVAL(RetransCount                  , "RetransCount             - Total number of transmitted packets which were retransmissions")
  MTIDL_LONGVAL(RetryCount                    , "RetryCount               - Number of Tx packets succeeded after one or more retransmissions")
  MTIDL_LONGVAL(MultipleRetryCount            , "MultipleRetryCount       - Number of Tx packets succeeded after more than one retransmission")
  MTIDL_LONGVAL(FailedRetransCount            , "FailedRetransCount       - Number of Tx packets dropped because of retry limit exceeded")
MTIDL_ITEM_END(mtlk_wssa_retrans_stats_t)

MTIDL_BITFIELD_START
  MTIDL_BITFIELD_ENTRY(MTLK_WSSA_11A_SUPPORTED,  0, "802.11a" )
  MTIDL_BITFIELD_ENTRY(MTLK_WSSA_11B_SUPPORTED,  1, "802.11b" )
  MTIDL_BITFIELD_ENTRY(MTLK_WSSA_11G_SUPPORTED,  2, "802.11g" )
  MTIDL_BITFIELD_ENTRY(MTLK_WSSA_11N_SUPPORTED,  3, "802.11n" )
  MTIDL_BITFIELD_ENTRY(MTLK_WSSA_11AC_SUPPORTED, 4, "802.11ac")
  MTIDL_BITFIELD_ENTRY(MTLK_WSSA_11AX_SUPPORTED, 5, "802.11ax")
MTIDL_BITFIELD_END(mtlk_wssa_net_modes_supported_e)

MTIDL_ENUM_START
  MTIDL_ENUM_ENTRY(MTLK_PHY_MODE_AG, 0, "802.11a/g")
  MTIDL_ENUM_ENTRY(MTLK_PHY_MODE_B,  1, "802.11b")
  MTIDL_ENUM_ENTRY(MTLK_PHY_MODE_N,  2, "802.11n")
  MTIDL_ENUM_ENTRY(MTLK_PHY_MODE_AC, 3, "802.11ac")
  MTIDL_ENUM_ENTRY(MTLK_PHY_MODE_AX, 4, "802.11ax")
MTIDL_ENUM_END(mtlk_wssa_phy_mode_e)

/* Peer Rate info for one direction i.e. RX or TX */
MTIDL_ITEM_START(PeerRateInfo1, MTIDL_TYPE_NONE, MTIDL_PROVIDER_PEER, MTIDL_SRC_DRV, MTLK_WSSA_DRV_PEER_RATE_INFO_1, "Rate info")
  MTIDL_FLAG(InfoFlag, "Rate info is valid")
  MTIDL_ENUM(PhyMode, mtlk_wssa_phy_mode_e, "Network (Phy) Mode")
  MTIDL_SLONGVAL(CbwIdx, "BW index")
  MTIDL_SLONGVAL(CbwMHz, "BW [MHz]")
  MTIDL_SLONGVAL(Scp, "SGI")
  MTIDL_SLONGVAL(Mcs, "MCS index")
  MTIDL_SLONGVAL(Nss, "NSS")
MTIDL_ITEM_END(mtlk_wssa_drv_peer_rate_info1_t)

/*------------------------------------------------------------------------------
 * TR-181
 */

/* HW */
MTIDL_ITEM_START(TR181HW, MTIDL_INFORMATION, MTIDL_PROVIDER_WLAN, MTIDL_SRC_DRV, MTLK_WSSA_DRV_TR181_HW, "TR-181 Device.WiFi.Radio.{i}")
  MTIDL_BYTEVAL(Enable                        , "Enable                   - Enables or disables the radio")
  MTIDL_BYTEVAL(Channel                       , "Channel                  - Current radio channel used")
MTIDL_ITEM_END(mtlk_wssa_drv_tr181_hw_t)


/*------ End of TR-181 -------------------------------------------------------*/

/*------------------------------------------------------------------------------
 * TR-181 Statistics
 */

/* Errors: per HW / per VAP */
MTIDL_ITEM_START(TR181ErrorStat, MTIDL_TYPE_NONE, MTIDL_PROVIDER_WLAN, MTIDL_SRC_DRV, MTLK_WSSA_DRV_TR181_ERROR, "TR-181 Errors")
  MTIDL_LONGVAL(ErrorsSent                    , "ErrorsSent               - Number of Tx packets not transmitted because of errors")
  MTIDL_LONGVAL(ErrorsReceived                , "ErrorsReceived           - Number of Rx packets that contained errors")
  MTIDL_LONGVAL(DiscardPacketsSent            , "DiscardPacketsSent       - Number of Tx packets discarded")
  MTIDL_LONGVAL(DiscardPacketsReceived        , "DiscardPacketsReceived   - Number of Rx packets discarded")
MTIDL_ITEM_END(mtlk_wssa_drv_tr181_error_stats_t)

/* VAP */
MTIDL_ITEM_START(TR181WLANStat, MTIDL_STATISTICS, MTIDL_PROVIDER_WLAN, MTIDL_SRC_DRV, MTLK_WSSA_DRV_TR181_WLAN, "TR-181 Device.WiFi.SSID.{i}.Stats")
  MTIDL_ITEM(mtlk_wssa_drv_traffic_stats_t,     traffic_stats, "Traffic Statistics")
  MTIDL_ITEM(mtlk_wssa_drv_tr181_error_stats_t, error_stats,   "Erros Statistics")
  MTIDL_ITEM(mtlk_wssa_retrans_stats_t        , retrans_stats, "Retransmission statistics")
  MTIDL_LONGVAL(ACKFailureCount               , "ACKFailureCount             - Number of expected ACKs never received")
  MTIDL_LONGVAL(AggregatedPacketCount         , "AggregatedPacketCount       - Number of aggregated packets transmitted")
  MTIDL_LONGVAL(UnknownProtoPacketsReceived   , "UnknownProtoPacketsReceived - Number of Rx packets unknown or unsupported protocol")
MTIDL_ITEM_END(mtlk_wssa_drv_tr181_wlan_stats_t)

/* HW */
#if 0 /* unsupported */
  MTIDL_LONGVAL(PLCPErrorCount                , "PLCPErrorCount         ")
  MTIDL_LONGVAL(InvalidMACCount               , "InvalidMACCount        ")
  MTIDL_LONGVAL(PacketsOtherReceived          , "PacketsOtherReceived   ")
#endif

MTIDL_ITEM_START(TR181HWStat, MTIDL_STATISTICS, MTIDL_PROVIDER_WLAN, MTIDL_SRC_DRV, MTLK_WSSA_DRV_TR181_HW_STATS, "TR-181 Device.WiFi.Radio.{i}.Stats")
  MTIDL_ITEM(mtlk_wssa_drv_traffic_stats_t,     traffic_stats, "Traffic Statistics")
  MTIDL_ITEM(mtlk_wssa_drv_tr181_error_stats_t, error_stats, "Erros Statistics")

  MTIDL_LONGVAL(FCSErrorCount                 , "FCSErrorCount            - Number of Rx packets with detected FCS error")
  MTIDL_SLONGVAL(Noise                        , "Noise                    - Average noise strength received [dBm]")
MTIDL_ITEM_END(mtlk_wssa_drv_tr181_hw_stats_t)

/* PEER */
#if 0 /*unsupported */
/* BOOL AuthenticationState - Whether an associated device has authenticated (true) or not (false) */
#endif

MTIDL_ITEM_START(TR181PeerStat, MTIDL_STATISTICS, MTIDL_PROVIDER_PEER, MTIDL_SRC_DRV, MTLK_WSSA_DRV_TR181_PEER, "TR-181 Device.WiFi.AccessPoint.{i}.AssociatedDevice")
  MTIDL_LONGVAL(StationId,                       "StationID")
  MTIDL_BITFIELD(NetModesSupported, mtlk_wssa_net_modes_supported_e, "OperatingStandard    - Supported network modes")
  MTIDL_ITEM(mtlk_wssa_peer_traffic_stats_t,     traffic_stats,        "Traffic statistics")
  MTIDL_ITEM(mtlk_wssa_retrans_stats_t,          retrans_stats,        "Retransmission statistics")
  MTIDL_LONGVAL(ErrorsSent,                      "ErrorsSent           - Number of Tx packets not transmitted because of errors")
  MTIDL_LONGVAL(LastDataDownlinkRate,            "LastDataDownlinkRate - Last data transmit rate (to peer) [kbps]")
  MTIDL_LONGVAL(LastDataUplinkRate,              "LastDataUplinkRate   - Last data receive rate (from peer) [kbps]")
  MTIDL_SLONGVAL(SignalStrength,                 "SignalStrength       - Radio signal strength of the uplink [dBm]")
MTIDL_ITEM_END(mtlk_wssa_drv_tr181_peer_stats_t)

/*------ End of TR-181 Statistics  -----------------------------------------------*/

/* HW Recovery statisctic */
MTIDL_ITEM_START(RecoveryStats, MTIDL_STATISTICS, MTIDL_PROVIDER_WLAN, MTIDL_SRC_DRV, MTLK_WSSA_DRV_RECOVERY, "Recovery statistics")
  MTIDL_LONGVAL(FastRcvryProcessed,                     "Number of FAST recovery processed successfully")
  MTIDL_LONGVAL(FullRcvryProcessed,                     "Number of FULL recovery processed successfully")
  MTIDL_LONGVAL(FastRcvryFailed,                        "Number of FAST recovery failed")
  MTIDL_LONGVAL(FullRcvryFailed,                        "Number of FULL recovery failed")
MTIDL_ITEM_END(mtlk_wssa_drv_recovery_stats_t)

/* HW Radio minimal statistic */

MTIDL_ITEM_START(HWTxmStat, MTIDL_TYPE_NONE, MTIDL_PROVIDER_WLAN, MTIDL_SRC_DRV, MTLK_WSSA_DRV_TXM_STATS, "HW TXM Statistics")
  MTIDL_LONGVAL(txmm_sent,                              "Number of FW MAN messages sent")
  MTIDL_LONGVAL(txmm_cfmd,                              "Number of FW MAN messages confirmed")
  MTIDL_LONGVAL(txmm_peak,                              "Peak number of FW MAN messages sent simultaneously")
  MTIDL_LONGVAL(txdm_sent,                              "Number of FW DBG messages sent")
  MTIDL_LONGVAL(txdm_cfmd,                              "Number of FW DBG messages confirmed")
  MTIDL_LONGVAL(txdm_peak,                              "Peak number of FW DBG messages sent simultaneously")
MTIDL_ITEM_END(mtlk_wssa_drv_hw_txm_stats_t)

MTIDL_ITEM_START(HWFlowStatus, MTIDL_STATISTICS, MTIDL_PROVIDER_WLAN, MTIDL_SRC_DRV, MTLK_WSSA_DRV_STATUS_HW, "HW Radio flow statistics")
  MTIDL_ITEM(mtlk_wssa_drv_recovery_stats_t, rcvry_stats,   "HW Recovery Statistics")
  MTIDL_ITEM(mtlk_wssa_drv_hw_txm_stats_t,   txm_stats,     "HW TXM statistics")

  MTIDL_ITEM(mtlk_wssa_drv_traffic_stats_t,  traffic_stats, "Radio Traffic statistics")
  MTIDL_ITEM(mtlk_wssa_drv_mgmt_stats_t,     mgmt_stats,    "Radio MGMT statistics")

  MTIDL_LONGVAL(RadarsDetected,                         "Radars detected")
  MTIDL_BYTEVAL(ChannelLoad,                            "Channel Load [%]")
  MTIDL_BYTEVAL(ChannelUtil,                            "Channel Utilization [%]")
  MTIDL_BYTEVAL(Airtime,                                "Total Airtime [%]")
  MTIDL_LONGVAL(AirtimeEfficiency,                      "Total Airtime Efficiency [bytes/sec]")
MTIDL_ITEM_END(mtlk_wssa_drv_hw_stats_t)

/* PEER minimal statistics */
MTIDL_ITEM_START(PeerFlowStatus, MTIDL_STATISTICS, MTIDL_PROVIDER_PEER, MTIDL_SRC_DRV, MTLK_WSSA_DRV_STATUS_PEER, "Peer packets flow statistics")
  MTIDL_ITEM(mtlk_wssa_drv_tr181_peer_stats_t,   tr181_stats,          "TR-181 statistics")
  MTIDL_SLONGVAL_ARRAY(ShortTermRSSIAverage, 4,  "ShortTermRSSI        - Short-term RSSI average per antenna [dBm]")
  MTIDL_SBYTEVAL_ARRAY(snr, 4,                   "SNR                  - Signal to Noise ratio per antenna [dB]")
  MTIDL_LONGVAL(AirtimeEfficiency,               "AirtimeEfficiency    - Efficiency of used air time [bytes/sec]")
  MTIDL_BYTEVAL(AirtimeUsage,                    "AirtimeUsage         - Air Time Used by RX/TX to/from STA [%]")
MTIDL_ITEM_END(mtlk_wssa_drv_peer_stats_t)

MTIDL_ENUM_START
  MTIDL_ENUM_ENTRY(VENDOR_UNKNOWN, 0, "Unknown")
  MTIDL_ENUM_ENTRY(VENDOR_LANTIQ,  1, "Lantiq" )
  MTIDL_ENUM_ENTRY(VENDOR_W101,    2, "W101"   )
MTIDL_ENUM_END(mtlk_wssa_peer_vendor_t)

/* Peer capabilities */
MTIDL_ITEM_START(PeerCapabilities, MTIDL_INFORMATION, MTIDL_PROVIDER_PEER, MTIDL_SRC_DRV, MTLK_WSSA_DRV_CAPABILITIES_PEER, "Peer capabilities")
  MTIDL_BITFIELD( NetModesSupported, mtlk_wssa_net_modes_supported_e, "Supported network modes")
  MTIDL_FLAG( WMMSupported, "WMM is supported")
  MTIDL_FLAG( CBSupported, "Channel bonding supported")
  MTIDL_FLAG( SGI20Supported, "SGI20 supported")
  MTIDL_FLAG( SGI40Supported, "SGI40 supported")
  MTIDL_FLAG( STBCSupported, "STBC supported")
  MTIDL_FLAG( LDPCSupported, "LDPC supported")
  MTIDL_FLAG( BFSupported, "Explicit beam forming supported")
  MTIDL_FLAG( Intolerant_40MHz, "40MHz intolerant")
  MTIDL_ENUM( Vendor, mtlk_wssa_peer_vendor_t,  "Vendor")
  MTIDL_LONGVAL( MIMOConfigTX, "Max TX spatial streams")
  MTIDL_LONGVAL( MIMOConfigRX, "Max RX spatial streams")
  MTIDL_LONGVAL( AMPDUMaxLengthExp, "Maximum A-MPDU Length Exponent")
  MTIDL_LONGVAL( AMPDUMinStartSpacing, "Minimum MPDU Start Spacing")
  MTIDL_TIMESTAMP( AssociationTimestamp, "Timestamp of station association")
MTIDL_ITEM_END(mtlk_wssa_drv_peer_capabilities_t)

/* Peer Rate info for both TX and RX directions */
MTIDL_ITEM_START(PeerRatesInfo, MTIDL_INFORMATION, MTIDL_PROVIDER_PEER, MTIDL_SRC_DRV, MTLK_WSSA_DRV_PEER_RATES_INFO, "Peer TX/RX info")
  MTIDL_ITEM(mtlk_wssa_drv_peer_rate_info1_t,  rx_mgmt_rate_info, "Mgmt uplink rate info")
  MTIDL_LONGFRACT(RxMgmtRate, 1, "Last mgmt uplink rate [Mbps]")
  MTIDL_ITEM(mtlk_wssa_drv_peer_rate_info1_t,  rx_data_rate_info, "Data uplink rate info")
  MTIDL_LONGFRACT(RxDataRate, 1, "Last data uplink rate [Mbps]")
  MTIDL_ITEM(mtlk_wssa_drv_peer_rate_info1_t,  tx_data_rate_info, "Data downlink rate info")
  MTIDL_LONGFRACT(TxDataRate, 1, "Last data downlink rate [Mbps]")
  MTIDL_LONGVAL(TxBfMode, "Beamforming mode")
  MTIDL_LONGVAL(TxStbcMode, "STBC mode")
  MTIDL_LONGFRACT(TxPwrCur,  2, "TX power for current rate [dBm]")
  MTIDL_LONGFRACT(TxMgmtPwr, 2, "TX management power       [dBm]")
MTIDL_ITEM_END(mtlk_wssa_drv_peer_rates_info_t)

/* WLAN minimal statistic */

MTIDL_ITEM_START(WLANFlowStatus, MTIDL_STATISTICS, MTIDL_PROVIDER_WLAN, MTIDL_SRC_DRV, MTLK_WSSA_DRV_STATUS_WLAN, "WLAN packets flow statistics")
  MTIDL_ITEM(mtlk_wssa_drv_tr181_wlan_stats_t, tr181_stats, "TR-181 statistics")
  MTIDL_ITEM(mtlk_wssa_drv_mgmt_stats_t,       mgmt_stats,  "MGMT statistics")
MTIDL_ITEM_END(mtlk_wssa_drv_wlan_stats_t)

MTIDL_ENUM_START
  MTIDL_ENUM_ENTRY(WSSA_SWR_UNKNOWN,          0,  "Unknown")
  MTIDL_ENUM_ENTRY(WSSA_SWR_OPTIMIZATION,     1,  "Channel optimization")
  MTIDL_ENUM_ENTRY(WSSA_SWR_RADAR,            2,  "Radar detection")
  MTIDL_ENUM_ENTRY(WSSA_SWR_USER,             3,  "User command")
  MTIDL_ENUM_ENTRY(WSSA_20_40_COEXISTENCE,    4,  "20/40 coexistence")
  MTIDL_ENUM_ENTRY(WSSA_SWR_INTERF,           5,  "Interference detection")
  MTIDL_ENUM_ENTRY(WSSA_SWR_AP_SWITCHED,      6,  "AP is switched")
MTIDL_ENUM_END(mtlk_wssa_channel_switch_reasons_t)

MTIDL_ITEM_START(ConnectionEvent, MTIDL_EVENT, MTIDL_PROVIDER_WLAN, MTIDL_SRC_DRV, MTLK_WSSA_DRV_EVENT_CONNECTION, "Peer connection event")
  MTIDL_MACADDR(mac_addr, "Peer MAC address")
  MTIDL_ENUM(auth_type, mtlk_wssa_authentication_type_t, "802.11 authentication type")
  MTIDL_ENUM(status, mtlk_wssa_connection_status_t, "Connection status")
MTIDL_ITEM_END(mtlk_connection_event_t)

MTIDL_ENUM_START
  MTIDL_ENUM_ENTRY(MTLK_DI_THIS_SIDE,  0, "Local side")
  MTIDL_ENUM_ENTRY(MTLK_DI_OTHER_SIDE, 1, "Other side")
MTIDL_ENUM_END(mtlk_wssa_initiators_t)

MTIDL_ITEM_START(DisconnectionEvent, MTIDL_EVENT, MTIDL_PROVIDER_WLAN, MTIDL_SRC_DRV, MTLK_WSSA_DRV_EVENT_DISCONNECTION, "Peer disconnection event")
  MTIDL_MACADDR(mac_addr,                                           "Peer MAC address")
  MTIDL_ENUM(initiator, mtlk_wssa_initiators_t,                     "Initiated by")
  MTIDL_ENUM(reason, mtlk_wssa_connection_status_t,                 "Disconnect reason")
  MTIDL_ITEM(mtlk_wssa_drv_peer_capabilities_t, peer_capabilities,  "Additional info")
  MTIDL_ITEM(mtlk_wssa_drv_peer_stats_t, peer_stats,                "Additional info")
MTIDL_ITEM_END(mtlk_disconnection_event_t)

/* HW Debug (full) statistic */
MTIDL_ITEM_START(HWFlowStatusDebug, MTIDL_STATISTICS, MTIDL_PROVIDER_HW, MTIDL_SRC_DRV, MTLK_WSSA_DRV_DEBUG_STATUS_HW, "Hardware flow (with DEBUG driver only)")
  MTIDL_HUGEVAL(RxPacketsSucceeded,                     "Number of packets received (64-bit)")
  MTIDL_HUGEVAL(RxBytesSucceeded,                       "Number of bytes received (64-bit)")
  MTIDL_HUGEVAL(TxPacketsSucceeded,                     "Number of packets transmitted (64-bit)")
  MTIDL_HUGEVAL(TxBytesSucceeded,                       "Number of bytes sent successfully (64-bit)")
  MTIDL_LONGVAL(RxPackets802_1x,                        "Number of 802_1X packets received")
  MTIDL_LONGVAL(TxPackets802_1x,                        "Number of 802_1X packets transmitted")
  MTIDL_LONGVAL(TxDiscardedPackets802_1x,               "Number of 802_1X packets discarded")
  MTIDL_LONGVAL(FwdRxPackets,                           "Number of packets received that should be forwarded to one or more STAs")
  MTIDL_LONGVAL(FwdRxBytes,                             "Number of bytes received that should be forwarded to one or more STAs")
  MTIDL_LONGVAL(UnicastPacketsSent,                     "Number of unicast packets transmitted")
  MTIDL_LONGVAL(UnicastPacketsReceived,                 "Number of unicast packets received")
  MTIDL_LONGVAL(MulticastPacketsSent,                   "Number of multicast packets transmitted")
  MTIDL_LONGVAL(MulticastPacketsReceived,               "Number of multicast packets received")
  MTIDL_LONGVAL(MulticastBytesSent,                     "Number of multicast bytes transmitted")
  MTIDL_LONGVAL(MulticastBytesReceived,                 "Number of multicast bytes received")
  MTIDL_LONGVAL(BroadcastBytesSent,                     "Number of broadcast bytes transmitted")
  MTIDL_LONGVAL(BroadcastBytesReceived,                 "Number of broadcast bytes received")

  MTIDL_LONGVAL(RxPacketsDiscardedDrvTooOld,            "Too old RX packets dropped by reordering mechanism")
  MTIDL_LONGVAL(RxPacketsDiscardedDrvDuplicate,         "Duplicate RX packets dropped by reordering mechanism")
  MTIDL_LONGVAL(PairwiseMICFailurePackets,              "Number of pairwise MIC failure packets")
  MTIDL_LONGVAL(GroupMICFailurePackets,                 "Number of group MIC failure packets")
  MTIDL_LONGVAL(UnicastReplayedPackets,                 "Number of unicast replayed packets")
  MTIDL_LONGVAL(MulticastReplayedPackets,               "Number of multicast/broadcast replayed packets")
  MTIDL_LONGVAL(ManagementReplayedPackets,              "Number of management replayed packets")
  MTIDL_LONGVAL(DATFramesReceived,                      "Number of data frames received")
  MTIDL_LONGVAL(CTLFramesReceived,                      "Number of control frames received")

  MTIDL_LONGVAL(MANFramesResQueue,                      "Number of management frames in reserved queue")
  MTIDL_LONGVAL(MANFramesSent,                          "Number of management frames sent")
  MTIDL_LONGVAL(MANFramesConfirmed,                     "Number of management frames confirmed")
  MTIDL_LONGVAL(MANFramesReceived,                      "Number of management frames received")
  MTIDL_LONGVAL(MANFramesRetryDropped,                  "Number of management frames dropped due to retries")
  MTIDL_LONGVAL(MANFramesRejectedCfg80211,              "Number of management frames rejected by CFG80211")

  MTIDL_LONGVAL(BssMgmtTxMaxBds,                        "Maximal length of BSS MGM queue")
  MTIDL_LONGVAL(BssMgmtTxFreeBds,                       "Number of free entries in BSS MGM queue")
  MTIDL_LONGVAL(BssMgmtTxUsagePeak,                     "BSS MGM queue usage peak")
  MTIDL_LONGVAL(BssMgmtTxResMaxBds,                     "Maximal length of BSS MGM queue")
  MTIDL_LONGVAL(BssMgmtTxResFreeBds,                    "Number of free entries in BSS MGM queue")
  MTIDL_LONGVAL(BssMgmtTxResUsagePeak,                  "BSS MGM queue usage peak")

  MTIDL_LONGVAL(FreeTxMSDUs,                            "Number of free TX MSDUs")
  MTIDL_LONGVAL(TxMSDUsUsagePeak,                       "TX MSDUs usage peak")
  MTIDL_LONGVAL(FWLoggerPacketsProcessed,               "FW logger packets processed")
  MTIDL_LONGVAL(FWLoggerPacketsDropped,                 "FW logger packets dropped")
  MTIDL_LONGVAL(BssRxPacketsProcessed,                  "BSS RX packets processed")
  MTIDL_LONGVAL(BssRxPacketsRejected,                   "BSS RX packets rejected")
  MTIDL_FLAG(BISTCheckPassed,                           "HW device passed BIST check successfully")
  MTIDL_LONGVAL(ISRsTotal,                              "Number of interrupts received")
  MTIDL_LONGVAL(ISRsForeign,                            "Number of foreign interrupts received")
  MTIDL_LONGVAL(ISRsNotPending,                         "Number of non-pending interrupts received")
  MTIDL_LONGVAL(ISRsInit,                               "Number of interrupts received while in init")
  MTIDL_LONGVAL(ISRsToDPC,                              "Number of DPCs scheduled by ISR") 
  MTIDL_LONGVAL(PostISRDPCs,                            "Number of post-ISR DPCs received")
  MTIDL_LONGVAL(LegIndReceived,                         "Number of legacy indications received")
  MTIDL_LONGVAL(RxAllocFailed,                          "Number of RX Buff allocations failed")
  MTIDL_LONGVAL(RxReAllocFailed,                        "Number of RX Buff re-allocations failed")
  MTIDL_LONGVAL(RxReAllocated,                          "Number of RX Buff re-allocated")
MTIDL_ITEM_END(mtlk_wssa_drv_debug_hw_stats_t)

/* WLAN Debug (full) statistic */
MTIDL_ITEM_START(WLANFlowStatusDebug, MTIDL_STATISTICS, MTIDL_PROVIDER_WLAN, MTIDL_SRC_DRV, MTLK_WSSA_DRV_DEBUG_STATUS_WLAN, "WLAN flow (with DEBUG driver only)")
  MTIDL_HUGEVAL(TxPacketsSucceeded,             "Number of packets transmitted (64-bit)")
  MTIDL_HUGEVAL(RxPacketsSucceeded,             "Number of packets received (64-bit)")
  MTIDL_HUGEVAL(TxBytesSucceeded,               "Number of bytes sent successfully (64-bit)")
  MTIDL_HUGEVAL(RxBytesSucceeded,               "Number of bytes received (64-bit)")
  MTIDL_LONGVAL(UnicastPacketsSent,             "Number of unicast packets transmitted")
  MTIDL_LONGVAL(UnicastPacketsReceived,         "Number of unicast packets received")
  MTIDL_LONGVAL(MulticastPacketsSent,           "Number of multicast packets transmitted")
  MTIDL_LONGVAL(MulticastPacketsReceived,       "Number of multicast packets received")
  MTIDL_LONGVAL(BroadcastPacketsSent,           "Number of broadcast packets transmitted")
  MTIDL_LONGVAL(BroadcastPacketsReceived,       "Number of broadcast packets received")

  MTIDL_LONGVAL(MulticastBytesSent,                               "Number of multicast bytes transmitted")
  MTIDL_LONGVAL(MulticastBytesReceived,                           "Number of multicast bytes received")
  MTIDL_LONGVAL(BroadcastBytesSent,                               "Number of broadcast bytes transmitted")
  MTIDL_LONGVAL(BroadcastBytesReceived,                           "Number of broadcast bytes received")

  MTIDL_LONGVAL(RxPackets802_1x,                                  "Number of 802_1X packets received")
  MTIDL_LONGVAL(TxPackets802_1x,                                  "Number of 802_1X packets transmitted")
  MTIDL_LONGVAL(TxDiscardedPackets802_1x,                         "Number of 802_1X packets discarded")
  MTIDL_LONGVAL(FwdRxPackets,                                     "Number of packets received that should be forwarded to one or more STAs")
  MTIDL_LONGVAL(FwdRxBytes,                                       "Number of bytes received that should be forwarded to one or more STAs")

  MTIDL_LONGVAL(RxPacketsDiscardedDrvTooOld,                      "Too old RX packets dropped by reordering mechanism")
  MTIDL_LONGVAL(RxPacketsDiscardedDrvDuplicate,                   "Duplicate RX packets dropped by reordering mechanism")
  MTIDL_LONGVAL(TxPacketsDiscardedDrvNoResources,                 "TX packets discarded due to system resources exhausted")
  MTIDL_LONGVAL(TxPacketsDiscardedDrvSQOverflow,                  "TX packets discarded due to driver queues overflow")
  MTIDL_LONGVAL(TxPacketsDiscardedDrvEAPOLFilter,                 "TX packets discarded by EAPOL filter")
  MTIDL_LONGVAL(TxPacketsDiscardedDrvDropAllFilter,               "TX packets discarded by drop all filter")
  MTIDL_LONGVAL(TxPacketsDiscardedDrvTXQueueOverflow,             "TX packets discarded due to HW transmit queue overflow")
  MTIDL_LONGVAL(TxPacketsDiscardedDrvNoPeers,                     "TX packets discarded by driver due to no peers were connected")
  MTIDL_LONGVAL(TxPacketsDiscardedDrvACM,                         "TX packets discarded by driver ACM facility")
  MTIDL_LONGVAL(TxPacketsDiscardedEapolCloned,                    "TX EAPOL packets arrived in MAC cloning mode discarded")
  MTIDL_LONGVAL(TxPacketsDiscardedDrvUnknownDestinationDirected,  "Directed TX packets with unknown destination discarded")
  MTIDL_LONGVAL(TxPacketsDiscardedDrvUnknownDestinationMcast,     "Multicast TX packets with unknown destination discarded")
  MTIDL_LONGVAL(PairwiseMICFailurePackets,                        "Number of pairwise MIC failure packets")
  MTIDL_LONGVAL(GroupMICFailurePackets,                           "Number of group MIC failure packets")
  MTIDL_LONGVAL(UnicastReplayedPackets,                           "Number of unicast replayed packets")
  MTIDL_LONGVAL(MulticastReplayedPackets,                         "Number of multicast/broadcast replayed packets")
  MTIDL_LONGVAL(ManagementReplayedPackets,                        "Number of management replayed packets")
  MTIDL_LONGVAL(DATFramesReceived,                                "Number of data frames received")
  MTIDL_LONGVAL(CTLFramesReceived,                                "Number of control frames received")
  MTIDL_LONGVAL(MANFramesResQueue,                                "Number of management frames in reserved queue")
  MTIDL_LONGVAL(MANFramesSent,                                    "Number of management frames sent")
  MTIDL_LONGVAL(MANFramesConfirmed,                               "Number of management frames confirmed")
  MTIDL_LONGVAL(MANFramesReceived,                                "Number of management frames received")
  MTIDL_LONGVAL(MANFramesRetryDropped,                            "Number of management frames dropped due to retries")
  MTIDL_LONGVAL(MANFramesRejectedCfg80211,                        "Number of management frames rejected by CFG80211")
  MTIDL_LONGVAL(BssMgmtTxQueFull,                                 "Number of management frames dropped due to TX que full")
  MTIDL_LONGVAL(ProbeRespSent,                                    "Number of probe responses sent")
  MTIDL_LONGVAL(ProbeRespDropped,                                 "Number of probe responses dropped")
  MTIDL_LONGVAL(CoexElReceived,                                   "Number of coexistent elements received")
  MTIDL_LONGVAL(ScanExRequested,                                  "Number of scan exemptions received")
  MTIDL_LONGVAL(ScanExGranted,                                    "Number of scan exemptions granted")
  MTIDL_LONGVAL(ScanExGrantCancelled,                             "Number of scan exemption grants cancelled")
  MTIDL_LONGVAL(SwitchChannel20To40,                              "Number of switches between 20MHz to 40MHz")
  MTIDL_LONGVAL(SwitchChannel40To20,                              "Number of switches between 40MHz to 20MHz")
  MTIDL_LONGVAL(SwitchChannel40To40,                              "Number of switches between 40MHz to 40MHz (another pair of channels)")
  MTIDL_LONGVAL(TxPacketsToUnicastNoDGAF,                         "TX packets broadcast converted to unicast (DGAF disabled)")
  MTIDL_LONGVAL(TxPacketsSkippedNoDGAF,                           "TX packets broadcast discarded (DGAF disabled)")
MTIDL_ITEM_END(mtlk_wssa_drv_debug_wlan_stats_t)

/* PEER Debug (full) statistic */
MTIDL_ITEM_START(PeerFlowStatusDebug, MTIDL_STATISTICS, MTIDL_PROVIDER_PEER, MTIDL_SRC_DRV, MTLK_WSSA_DRV_DEBUG_STATUS_PEER, "Peer flow (with DEBUG driver only)")
  MTIDL_LONGVAL(StationId,                              "StationID")
  MTIDL_LONGVAL(HwTxPacketsSucceeded,                   "HW Number of packets transmitted")
  MTIDL_LONGVAL(HwRxPacketsSucceeded,                   "HW Number of packets received")
  MTIDL_LONGVAL(HwTxBytesSucceeded,                     "HW Number of bytes sent successfully")
  MTIDL_LONGVAL(HwRxBytesSucceeded,                     "HW Number of bytes received")
  MTIDL_LONGVAL(RxPackets802_1x,                        "Number of 802_1X packets received")
  MTIDL_LONGVAL(TxPackets802_1x,                        "Number of 802_1X packets transmitted")
  MTIDL_LONGVAL(TxDiscardedPackets802_1x,               "Number of 802_1X packets discarded")
  MTIDL_LONGVAL(FwdRxPackets,                           "Number of packets received that should be forwarded to one or more STAs")
  MTIDL_LONGVAL(FwdRxBytes,                             "Number of bytes received that should be forwarded to one or more STAs")
  MTIDL_LONGVAL(UnicastPacketsSent,                     "Number of unicast packets transmitted")
  MTIDL_LONGVAL(UnicastPacketsReceived,                 "Number of unicast packets received")
  MTIDL_LONGVAL(TxPacketsDiscardedDrvNoResources,       "TX packets discarded due to system resources exhausted")
  MTIDL_LONGVAL(TxPacketsDiscardedDrvSQOverflow,        "TX packets discarded due to driver queues overflow")
  MTIDL_LONGVAL(TxPacketsDiscardedDrvEAPOLFilter,       "TX packets discarded by EAPOL filter")
  MTIDL_LONGVAL(TxPacketsDiscardedDrvDropAllFilter,     "TX packets discarded by drop all filter")
  MTIDL_LONGVAL(TxPacketsDiscardedDrvTXQueueOverflow,   "TX packets discarded due to HW transmit queue overflow")
  MTIDL_LONGVAL(TxPacketsDiscardedACM,                  "TX packets discarded by driver ACM facility")
  MTIDL_LONGVAL(TxPacketsDiscardedEAPOLCloned,          "TX EAPOL packets arrived in MAC cloning mode discarded")
  MTIDL_LONGVAL(RxPacketsDiscardedDrvTooOld,            "Too old RX packets dropped by reordering mechanism")
  MTIDL_LONGVAL(RxPacketsDiscardedDrvDuplicate,         "Duplicate RX packets dropped by reordering mechanism")
  MTIDL_LONGVAL(PSModeEntrances,                        "Number of time peer entered legacy power save mode")
  MTIDL_LONGFRACT(LastDataDownlinkRate, 1,              "Last data transmit rate")
  MTIDL_LONGFRACT(LastDataUplinkRate, 1,                "Last data receive rate")
  MTIDL_BYTEVAL(AirtimeUsage,                           "Air Time Used by RX/TX to/from STA [%]")
  MTIDL_LONGVAL(AirtimeEfficiency,                      "Efficiency of used air time [bytes/sec]")
  MTIDL_LONGFRACT(LastMgmtUplinkRate,   1,              "Last management receive rate")
  MTIDL_LONGVAL(LastActivityTimestamp,                  "How many msecs ago was active")
  MTIDL_SLONGVAL_ARRAY(ShortTermRSSIAverage, 4,         "Short-term RSSI average per antenna")
  MTIDL_SLONGVAL_ARRAY(LongTermRSSIAverage,  4,         "Long-term RSSI average per antenna")
  MTIDL_SLONGVAL_ARRAY(LastMgmtRSSI, 4,                 "Last management RSSI average per antenna")
  MTIDL_SBYTEVAL_ARRAY(snr, 4,                          "SNR - Signal to Noise ratio per antenna [dB]")

#if 1   /* FIXME: currently mtidl_ini.pl just ignore any C preprocessor "#" lines */
#endif  /* So these items (below) are always present in data struct, but might be not updated */

#if MTLK_WWSS_WLAN_STAT_ANALYZER_TX_RATE_ALLOWED
  MTIDL_LONGVAL(ShortTermTXAverage,                     "Short-term TX throughput average")
  MTIDL_LONGVAL(LongTermTXAverage,                      "Long-term TX throughput average")
#endif
#if MTLK_WWSS_WLAN_STAT_ANALYZER_RX_RATE_ALLOWED
  MTIDL_LONGVAL(ShortTermRXAverage,                     "Short-term RX throughput average")
  MTIDL_LONGVAL(LongTermRXAverage,                      "Long-term RX throughput average")
#endif

MTIDL_ITEM_END(mtlk_wssa_drv_debug_peer_stats_t)

#define MTLK_PACK_OFF
#include "mtlkpack.h"

#define   MTLK_IDEFS_ON
#include "mtlkidefs.h"

#define   MTLK_IDEFS_OFF
#include "mtlkidefs.h"


#else /* MTLK_LEGACY_STATISTICS */
typedef enum{
  MTLK_WSSA_11A_SUPPORTED,
  MTLK_WSSA_11B_SUPPORTED,
  MTLK_WSSA_11G_SUPPORTED,
  MTLK_WSSA_11N_SUPPORTED,
  MTLK_WSSA_11AC_SUPPORTED,
  MTLK_WSSA_11AX_SUPPORTED,
}mtlk_wssa_net_modes_supported_e;

typedef enum{
  VENDOR_UNKNOWN,
  VENDOR_LANTIQ,
  VENDOR_W101,
}mtlk_wssa_peer_vendor_t;

typedef enum{
  MTLK_PHY_MODE_AG,
  MTLK_PHY_MODE_B,
  MTLK_PHY_MODE_N,
  MTLK_PHY_MODE_AC,
  MTLK_PHY_MODE_AX,
}mtlk_wssa_phy_mode_e;

typedef struct mtlk_wssa_peer_traffic_stats{
  uint32 BytesSent;
  uint32 BytesReceived;
  uint32 PacketsSent;
  uint32 PacketsReceived;
}mtlk_wssa_peer_traffic_stats_t;

typedef struct mtlk_wssa_retrans_stats{
  uint32 Retransmissions;
  uint32 RetransCount;
  uint32 RetryCount;
  uint32 MultipleRetryCount;
  uint32 FailedRetransCount;
}mtlk_wssa_retrans_stats_t;

typedef struct mtlk_wssa_drv_tr181_peer_stats{
  uint32 StationId;
  uint32 NetModesSupported;
  mtlk_wssa_peer_traffic_stats_t traffic_stats;
  mtlk_wssa_retrans_stats_t retrans_stats;
  uint32 ErrorsSent;
  uint32 LastDataDownlinkRate;
  uint32 LastDataUplinkRate;
  int32 SignalStrength;
}mtlk_wssa_drv_tr181_peer_stats_t;

typedef struct mtlk_wssa_drv_peer_stats{
  mtlk_wssa_drv_tr181_peer_stats_t tr181_stats;
  int32 ShortTermRSSIAverage[4];
  int8 snr[4];
  uint32 AirtimeEfficiency;
  uint8 AirtimeUsage;
}mtlk_wssa_drv_peer_stats_t;

typedef struct mtlk_wssa_drv_peer_rate_info1{
  uint32 InfoFlag;
  uint32 PhyMode;
  int32 CbwIdx;
  int32 CbwMHz;
  int32 Scp;
  int32 Mcs;
  int32 Nss;
}mtlk_wssa_drv_peer_rate_info1_t;

typedef struct mtlk_wssa_drv_peer_rates_info{
  mtlk_wssa_drv_peer_rate_info1_t rx_mgmt_rate_info;
  uint32 RxMgmtRate;
  mtlk_wssa_drv_peer_rate_info1_t rx_data_rate_info;
  uint32 RxDataRate;
  mtlk_wssa_drv_peer_rate_info1_t tx_data_rate_info;
  uint32 TxDataRate;
  uint32 TxBfMode;
  uint32 TxStbcMode;
  uint32 TxPwrCur;
  uint32 TxMgmtPwr;
}mtlk_wssa_drv_peer_rates_info_t;

typedef struct mtlk_wssa_drv_traffic_stats{
  uint64 BytesSent;
  uint64 BytesReceived;
  uint64 PacketsSent;
  uint64 PacketsReceived;
  uint32 UnicastPacketsSent;
  uint32 UnicastPacketsReceived;
  uint32 MulticastPacketsSent;
  uint32 MulticastPacketsReceived;
  uint32 BroadcastPacketsSent;
  uint32 BroadcastPacketsReceived;
}mtlk_wssa_drv_traffic_stats_t;

typedef struct mtlk_wssa_drv_tr181_error_stats{
  uint32 ErrorsSent;
  uint32 ErrorsReceived;
  uint32 DiscardPacketsSent;
  uint32 DiscardPacketsReceived;
}mtlk_wssa_drv_tr181_error_stats_t;

typedef struct mtlk_wssa_drv_tr181_wlan_stats{
  mtlk_wssa_drv_traffic_stats_t traffic_stats;
  mtlk_wssa_drv_tr181_error_stats_t error_stats;
  mtlk_wssa_retrans_stats_t retrans_stats;
  uint32 ACKFailureCount;
  uint32 AggregatedPacketCount;
  uint32 UnknownProtoPacketsReceived;
}mtlk_wssa_drv_tr181_wlan_stats_t;

typedef struct mtlk_wssa_drv_tr181_hw{
  uint8 Enable;
  uint8 Channel;
}mtlk_wssa_drv_tr181_hw_t;

typedef struct mtlk_wssa_drv_tr181_hw_stats{
  mtlk_wssa_drv_traffic_stats_t traffic_stats;
  mtlk_wssa_drv_tr181_error_stats_t error_stats;
  uint32 FCSErrorCount;
  int32 Noise;
}mtlk_wssa_drv_tr181_hw_stats_t;

typedef struct mtlk_wssa_drv_recovery_stats{
  uint32 FastRcvryProcessed;
  uint32 FullRcvryProcessed;
  uint32 FastRcvryFailed;
  uint32 FullRcvryFailed;
}mtlk_wssa_drv_recovery_stats_t;

typedef struct mtlk_wssa_drv_peer_capabilities{
  uint32 NetModesSupported;
  uint32 WMMSupported;
  uint32 CBSupported;
  uint32 SGI20Supported;
  uint32 SGI40Supported;
  uint32 STBCSupported;
  uint32 LDPCSupported;
  uint32 BFSupported;
  uint32 Intolerant_40MHz;
  uint32 Vendor;
  uint32 MIMOConfigTX;
  uint32 MIMOConfigRX;
  uint32 AMPDUMaxLengthExp;
  uint32 AMPDUMinStartSpacing;
  uint32 AssociationTimestamp;
}mtlk_wssa_drv_peer_capabilities_t;

typedef struct mtlk_wssa_drv_hw_txm_stats{
  uint32 txmm_sent;
  uint32 txmm_cfmd;
  uint32 txmm_peak;
  uint32 txdm_sent;
  uint32 txdm_cfmd;
  uint32 txdm_peak;
}mtlk_wssa_drv_hw_txm_stats_t;

typedef struct mtlk_wssa_drv_mgmt_stats{
  uint32 MANFramesResQueue;
  uint32 MANFramesSent;
  uint32 MANFramesConfirmed;
  uint32 MANFramesReceived;
  uint32 MANFramesRetryDropped;
  uint32 BssMgmtTxQueFull;
  uint32 ProbeRespSent;
  uint32 ProbeRespDropped;
}mtlk_wssa_drv_mgmt_stats_t;

typedef struct mtlk_wssa_drv_hw_stats{
  mtlk_wssa_drv_recovery_stats_t rcvry_stats;
  mtlk_wssa_drv_hw_txm_stats_t   txm_stats;
  mtlk_wssa_drv_traffic_stats_t  traffic_stats;
  mtlk_wssa_drv_mgmt_stats_t     mgmt_stats;
  uint32 RadarsDetected;
  uint8  ChannelLoad;
  uint8  ChannelUtil;
  uint8  Airtime;
  uint32 AirtimeEfficiency;
}mtlk_wssa_drv_hw_stats_t;

typedef struct mtlk_wssa_drv_peer_list{
  IEEE_ADDR addr;
  uint32 is_sta_auth;
}mtlk_wssa_peer_list_t;
#endif /* MTLK_LEGACY_STATISTICS */
#endif /* __MTLK_WSSA_DRVINFO_H__ */
