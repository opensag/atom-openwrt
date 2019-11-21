/******************************************************************************

                               Copyright (c) 2013
                            Lantiq Deutschland GmbH

  For licensing information, see the file 'LICENSE' in the root folder of
  this software module.

******************************************************************************/

/*
 * $Id$
 *
 *
 *
 * Core configuration implementation
 *
 */
#include "mtlkinc.h"
#include "core.h"
#include "core_config.h"
#include "mtlk_vap_manager.h"
#include "eeprom.h"
#include "hw_mmb.h"
#include "mtlkaux.h"
#include "mtlk_df_user_priv.h"
#include "mtlkdfdefs.h"
#include "mhi_umi.h"
#include "mhi_umi_propr.h"
#include "scan_support.h"
#include "mtlk_df_priv.h"
#include "mtlk_df.h"
#include "cfg80211.h"
#include "mac80211.h"
#include "mcast.h"
#include "wave_fapi_if.h"
#include "wave_hal_stats.h"
#include "bitrate.h"

#define LOG_LOCAL_GID   GID_CORE
#define LOG_LOCAL_FID   5

int __MTLK_IFUNC
mtlk_core_get_associated_dev_tid_stats (mtlk_handle_t hcore, const void *data, uint32 data_size)
{
  mtlk_core_t *core = HANDLE_T_PTR(mtlk_core_t, hcore);
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;
  int res = MTLK_ERR_OK;
  IEEE_ADDR *addr;
  uint32 addr_size;
  sta_entry *sta = NULL;
  peerTidStats tid_stats;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);
  addr = mtlk_clpb_enum_get_next(clpb, &addr_size);
  MTLK_CLPB_TRY(addr, addr_size)

    /* find station in stadb */
    sta = mtlk_stadb_find_sta(&core->slow_ctx->stadb, addr->au8Addr);
    if (NULL == sta) {
      WLOG_DY("CID-%04x: station %Y not found",
        mtlk_vap_get_oid(core->vap_handle), addr);
      MTLK_CLPB_EXIT(MTLK_ERR_UNKNOWN);
    } else {
      memset(&tid_stats, 0, sizeof(peerTidStats));
      mtlk_sta_decref(sta);
    }

  MTLK_CLPB_FINALLY(res)
    return mtlk_clpb_push_res_data(clpb, res, &tid_stats, sizeof(peerTidStats));
  MTLK_CLPB_END;
}

int __MTLK_IFUNC
mtlk_core_get_channel_stats (mtlk_handle_t hcore, const void *data, uint32 data_size)
{
  mtlk_core_t *core = HANDLE_T_PTR(mtlk_core_t, hcore);
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;
  mtlk_channel_stats *chan_stats = NULL;
  mtlk_hw_band_e  hw_band;
  int res = MTLK_ERR_OK;
  unsigned int num_of_channels = 0;
  size_t total_size = 0;
  mtlk_hw_t *hw = NULL;

  hw = mtlk_vap_get_hw(core->vap_handle);

  hw_band = core_cfg_get_freq_band_cur(core);
  num_of_channels = wave_core_cfg_get_nof_channels(hw_band);
  if (num_of_channels == 0) {
    res = MTLK_ERR_UNKNOWN;
    goto fail;
  }
  total_size = sizeof(mtlk_channel_stats) * num_of_channels;
  chan_stats = mtlk_osal_mem_alloc(total_size, MTLK_MEM_TAG_EXTENSION);
  if (NULL == chan_stats) {
    ELOG_V("Can't allocate memory");
    res = MTLK_ERR_NO_MEM;
    goto fail;
  }

  memset(chan_stats, 0, total_size);

  mtlk_hw_copy_channel_stats(hw, hw_band, chan_stats, total_size);

  res = mtlk_clpb_push_res_data_no_copy(clpb, MTLK_ERR_OK, chan_stats, total_size);
  if (MTLK_ERR_OK != res) {
    mtlk_osal_mem_free(chan_stats);
    return res;
  }

  return res;
fail:
  return mtlk_clpb_push_res(clpb, res);
}

static const uint16 hal_cbw[] = {HAL_BW_20MHZ, HAL_BW_40MHZ, HAL_BW_80MHZ, HAL_BW_160MHZ};
int __MTLK_IFUNC
mtlk_core_get_associated_dev_rate_info_rx_stats (mtlk_handle_t hcore, const void *data, uint32 data_size)
{
  mtlk_core_t *core = HANDLE_T_PTR(mtlk_core_t, hcore);
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;
  int res = MTLK_ERR_OK;
  IEEE_ADDR *addr;
  uint32 addr_size;
  uint8 cbw, i;
  sta_entry *sta = NULL;
  peerRateInfoRxStats peer_rx_rate_info;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);
  addr = mtlk_clpb_enum_get_next(clpb, &addr_size);
  MTLK_CLPB_TRY(addr, addr_size)

    /* find station in stadb */
    sta = mtlk_stadb_find_sta(&core->slow_ctx->stadb, addr->au8Addr);
    if (NULL == sta) {
      WLOG_DY("CID-%04x: station %Y not found",
        mtlk_vap_get_oid(core->vap_handle), addr);
      MTLK_CLPB_EXIT(MTLK_ERR_UNKNOWN);
    } else {
      memset(&peer_rx_rate_info, 0, sizeof(peerRateInfoRxStats));

#ifndef MTLK_LEGACY_STATISTICS
      peer_rx_rate_info.nss = mtlk_bitrate_info_get_nss(sta->info.stats.rx_data_rate_info);
      peer_rx_rate_info.mcs = mtlk_bitrate_info_get_mcs(sta->info.stats.rx_data_rate_info);
      cbw = mtlk_bitrate_info_get_cbw(sta->info.stats.rx_data_rate_info);
      peer_rx_rate_info.bw = hal_cbw[cbw];
      peer_rx_rate_info.bytes = sta->sta_stats64_cntrs.rxOutStaNumOfBytes;
      peer_rx_rate_info.flags |= BIT_ULL(HAS_BYTES);
      peer_rx_rate_info.msdus = sta->sta_stats64_cntrs.rdCount;
      peer_rx_rate_info.flags |= BIT_ULL(HAS_MSDUS);
      peer_rx_rate_info.mpdus = sta->sta_stats64_cntrs.mpduInAmpdu;
      peer_rx_rate_info.flags |= BIT_ULL(HAS_MPDUS);
      peer_rx_rate_info.ppdus = sta->sta_stats64_cntrs.ampdu;
      peer_rx_rate_info.flags |= BIT_ULL(HAS_PPDUS);
      peer_rx_rate_info.retries = sta->sta_stats64_cntrs.mpduRetryCount;
      peer_rx_rate_info.rssi_combined = sta->info.stats.max_rssi;
      peer_rx_rate_info.flags |= BIT_ULL(HAS_RSSI_COMB);
      for (i = 0; i < MAX_NUM_RX_ANTENNAS; i++) {
        peer_rx_rate_info.rssi_array[i][0] = sta->info.stats.data_rssi[i];
      }
      peer_rx_rate_info.flags |= BIT_ULL(HAS_RSSI_ARRAY);
#endif
      mtlk_sta_decref(sta);
    }

  MTLK_CLPB_FINALLY(res)
    return mtlk_clpb_push_res_data(clpb, res, &peer_rx_rate_info, sizeof(peerRateInfoRxStats));
  MTLK_CLPB_END;
}

int __MTLK_IFUNC
mtlk_core_get_associated_dev_rate_info_tx_stats (mtlk_handle_t hcore, const void *data, uint32 data_size)
{
  mtlk_core_t *core = HANDLE_T_PTR(mtlk_core_t, hcore);
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;
  int res = MTLK_ERR_OK;
  IEEE_ADDR *addr;
  uint32 addr_size;
  uint8 cbw;
  sta_entry *sta = NULL;
  peerRateInfoTxStats peer_tx_rate_info;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);
  addr = mtlk_clpb_enum_get_next(clpb, &addr_size);
  MTLK_CLPB_TRY(addr, addr_size)

    /* find station in stadb */
    sta = mtlk_stadb_find_sta(&core->slow_ctx->stadb, addr->au8Addr);
    if (NULL == sta) {
      WLOG_DY("CID-%04x: station %Y not found",
        mtlk_vap_get_oid(core->vap_handle), addr);
      MTLK_CLPB_EXIT(MTLK_ERR_UNKNOWN);
    } else {

      memset(&peer_tx_rate_info, 0, sizeof(peerRateInfoTxStats));

#ifndef MTLK_LEGACY_STATISTICS
      peer_tx_rate_info.nss = mtlk_bitrate_params_get_nss(sta->info.stats.tx_data_rate_params);
      peer_tx_rate_info.mcs = mtlk_bitrate_params_get_mcs(sta->info.stats.tx_data_rate_params);
      cbw = mtlk_bitrate_params_get_cbw(sta->info.stats.tx_data_rate_params);
      peer_tx_rate_info.bw = hal_cbw[cbw];
      peer_tx_rate_info.bytes = sta->sta_stats64_cntrs.mpduByteTransmitted;
      peer_tx_rate_info.flags |= BIT_ULL(HAS_BYTES);
      peer_tx_rate_info.msdus = sta->sta_stats64_cntrs.successCount + sta->sta_stats64_cntrs.exhaustedCount;
      peer_tx_rate_info.flags |= BIT_ULL(HAS_MSDUS);
      peer_tx_rate_info.mpdus = sta->sta_stats64_cntrs.mpduTransmitted;
      peer_tx_rate_info.flags |= BIT_ULL(HAS_MPDUS);
      peer_tx_rate_info.ppdus = sta->sta_stats64_cntrs.transmittedAmpdu;
      peer_tx_rate_info.flags |= BIT_ULL(HAS_PPDUS);
      peer_tx_rate_info.retries = sta->sta_stats64_cntrs.mpduRetransmission;
      peer_tx_rate_info.attempts = sta->sta_stats64_cntrs.mpduFirstRetransmission;
#endif

      mtlk_sta_decref(sta);
    }

  MTLK_CLPB_FINALLY(res)
    return mtlk_clpb_push_res_data(clpb, res, &peer_tx_rate_info, sizeof(peerRateInfoTxStats));
  MTLK_CLPB_END;
}

int __MTLK_IFUNC
mtlk_core_get_associated_dev_stats (mtlk_handle_t hcore, const void *data, uint32 data_size)
{
  mtlk_core_t *core = HANDLE_T_PTR(mtlk_core_t, hcore);
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;
  int res = MTLK_ERR_OK;
  IEEE_ADDR *addr;
  uint32 addr_size;
  sta_entry *sta = NULL;
  peerFlowStats peer_stats;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);
  addr = mtlk_clpb_enum_get_next(clpb, &addr_size);
  MTLK_CLPB_TRY(addr, addr_size)

    /* find station in stadb */
    sta = mtlk_stadb_find_sta(&core->slow_ctx->stadb, addr->au8Addr);
    if (NULL == sta) {
      WLOG_DY("CID-%04x: station %Y not found",
        mtlk_vap_get_oid(core->vap_handle), addr);
      MTLK_CLPB_EXIT(MTLK_ERR_UNKNOWN);
    } else {

#ifndef MTLK_LEGACY_STATISTICS
      memset(&peer_stats , 0, sizeof(peerFlowStats));
      mtlk_sta_get_associated_dev_stats(sta, &peer_stats);
#endif

      mtlk_sta_decref(sta);
    }

  MTLK_CLPB_FINALLY(res)
    return mtlk_clpb_push_res_data(clpb, res, &peer_stats, sizeof(peerFlowStats));
  MTLK_CLPB_END;
}

int __MTLK_IFUNC
mtlk_core_stats_poll_period_get (mtlk_handle_t hcore, const void* data, uint32 data_size)
{
  int res = MTLK_ERR_OK;
  mtlk_core_t *core = HANDLE_T_PTR(mtlk_core_t, hcore);
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;
  mtlk_hw_t *hw = NULL;
  uint32 poll_period = 0;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);
  hw = mtlk_vap_get_hw(core->vap_handle);
  MTLK_ASSERT(hw);

  /* read config from internal db */
  poll_period = wave_hw_mmb_get_stats_poll_period(hw);

  /* push result and config to clipboard*/
  res = mtlk_clpb_push_res_data(clpb, MTLK_ERR_OK, &poll_period,
    sizeof(poll_period));

  return res;
}

int __MTLK_IFUNC
mtlk_core_stats_poll_period_set (mtlk_handle_t hcore, const void* data, uint32 data_size)
{
  int res = MTLK_ERR_OK;
  mtlk_core_t *core = HANDLE_T_PTR(mtlk_core_t, hcore);
  mtlk_df_t *df = mtlk_vap_get_df(core->vap_handle);
  mtlk_df_user_t *df_user = mtlk_df_get_user(df);
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;
  uint32 *poll_period = NULL, res_size;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  /* get configuration */
  poll_period = mtlk_clpb_enum_get_next(clpb, &res_size);

  if (poll_period) {
    wave_hw_mmb_set_stats_poll_period(df, *poll_period);
    mtlk_df_user_stats_timer_set(df_user, *poll_period);
  } else {
    ILOG2_V("Poll period is NULL");
    res = MTLK_ERR_UNKNOWN;
  }

  return mtlk_clpb_push_res_data(clpb, res, &res, sizeof(res));
}

#ifndef MTLK_LEGACY_STATISTICS
int __MTLK_IFUNC
mtlk_core_get_peer_list (mtlk_handle_t hcore, const void *data, uint32 data_size)
{
  mtlk_core_t *nic = HANDLE_T_PTR(mtlk_core_t, hcore);
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;

  if (0 == (mtlk_core_get_net_state(nic) & (NET_STATE_HALTED | NET_STATE_CONNECTED))) {
    return MTLK_ERR_OK;
  }

  return mtlk_stadb_get_peer_list(&nic->slow_ctx->stadb, clpb);
}

int __MTLK_IFUNC
mtlk_core_get_peer_flow_status (mtlk_handle_t hcore, const void *data, uint32 data_size)
{
  mtlk_core_t *core = HANDLE_T_PTR(mtlk_core_t, hcore);
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;
  int res = MTLK_ERR_OK;
  IEEE_ADDR *addr;
  uint32 addr_size;
  sta_entry *sta = NULL;
  mtlk_wssa_drv_peer_stats_t peer_flow_status;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);
  addr = mtlk_clpb_enum_get_next(clpb, &addr_size);
  MTLK_CLPB_TRY(addr, addr_size)

    /* find station in stadb */
    sta = mtlk_stadb_find_sta(&core->slow_ctx->stadb, addr->au8Addr);
    if (NULL == sta) {
      WLOG_DY("CID-%04x: station %Y not found",
        mtlk_vap_get_oid(core->vap_handle), addr);
      MTLK_CLPB_EXIT(MTLK_ERR_UNKNOWN);
    } else {
      mtlk_sta_get_peer_stats(sta, &peer_flow_status);
      mtlk_sta_decref(sta);
    }

  MTLK_CLPB_FINALLY(res)
    return mtlk_clpb_push_res_data(clpb, res, &peer_flow_status, sizeof(mtlk_wssa_drv_peer_stats_t));
  MTLK_CLPB_END;
}

int __MTLK_IFUNC
mtlk_core_get_peer_capabilities (mtlk_handle_t hcore, const void *data, uint32 data_size)
{
  mtlk_core_t *core = HANDLE_T_PTR(mtlk_core_t, hcore);
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;
  int res = MTLK_ERR_OK;
  IEEE_ADDR *addr;
  uint32 addr_size;
  sta_entry *sta = NULL;
  mtlk_wssa_drv_peer_capabilities_t peer_capabilities;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);
  addr = mtlk_clpb_enum_get_next(clpb, &addr_size);
  MTLK_CLPB_TRY(addr, addr_size)

    /* find station in stadb */
    sta = mtlk_stadb_find_sta(&core->slow_ctx->stadb, addr->au8Addr);
    if (NULL == sta) {
      WLOG_DY("CID-%04x: station %Y not found",
        mtlk_vap_get_oid(core->vap_handle), addr);
      MTLK_CLPB_EXIT(MTLK_ERR_UNKNOWN);
    } else {
      mtlk_sta_get_peer_capabilities(sta, &peer_capabilities);
      mtlk_sta_decref(sta);
    }

  MTLK_CLPB_FINALLY(res)
    return mtlk_clpb_push_res_data(clpb, res, &peer_capabilities, sizeof(mtlk_wssa_drv_peer_capabilities_t));
  MTLK_CLPB_END;
}

int __MTLK_IFUNC
mtlk_core_get_peer_rate_info (mtlk_handle_t hcore, const void *data, uint32 data_size)
{
  mtlk_core_t *core = HANDLE_T_PTR(mtlk_core_t, hcore);
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;
  int res = MTLK_ERR_OK;
  IEEE_ADDR *addr;
  uint32 addr_size;
  sta_entry *sta = NULL;
  mtlk_wssa_drv_peer_rates_info_t peer_rate_info;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);
  addr = mtlk_clpb_enum_get_next(clpb, &addr_size);
  MTLK_CLPB_TRY(addr, addr_size)

    /* find station in stadb */
    sta = mtlk_stadb_find_sta(&core->slow_ctx->stadb, addr->au8Addr);
    if (NULL == sta) {
      WLOG_DY("CID-%04x: station %Y not found",
        mtlk_vap_get_oid(core->vap_handle), addr);
      MTLK_CLPB_EXIT(MTLK_ERR_UNKNOWN);
    } else {
      _mtlk_sta_get_peer_rates_info(sta, &peer_rate_info);
      mtlk_sta_decref(sta);
    }

  MTLK_CLPB_FINALLY(res)
    return mtlk_clpb_push_res_data(clpb, res, &peer_rate_info, sizeof(mtlk_wssa_drv_peer_rates_info_t));
  MTLK_CLPB_END;
}

int __MTLK_IFUNC
mtlk_core_get_recovery_stats (mtlk_handle_t hcore, const void *data, uint32 data_size)
{
  mtlk_core_t *core = HANDLE_T_PTR(mtlk_core_t, hcore);
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;
  mtlk_wssa_drv_recovery_stats_t recovery_stats;
  mtlk_hw_t *hw = NULL;

  hw = mtlk_vap_get_hw(core->vap_handle);
  MTLK_ASSERT(hw);

  wave_hw_get_recovery_stats(hw, &recovery_stats);

  return mtlk_clpb_push_res_data(clpb, MTLK_ERR_OK, &recovery_stats, sizeof(mtlk_wssa_drv_recovery_stats_t));
}

int __MTLK_IFUNC
mtlk_core_get_hw_flow_status (mtlk_handle_t hcore, const void *data, uint32 data_size)
{
  mtlk_core_t *core = HANDLE_T_PTR(mtlk_core_t, hcore);
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;
  wave_radio_t  *radio = wave_vap_radio_get(core->vap_handle);
  mtlk_wssa_drv_hw_stats_t hw_flow_status;

  wave_radio_get_hw_stats(radio, &hw_flow_status);

  return mtlk_clpb_push_res_data(clpb, MTLK_ERR_OK, &hw_flow_status, sizeof(mtlk_wssa_drv_hw_stats_t));
}

int __MTLK_IFUNC
mtlk_core_get_tr181_wlan_statistics (mtlk_handle_t hcore, const void *data, uint32 data_size)
{
  mtlk_core_t *core = HANDLE_T_PTR(mtlk_core_t, hcore);
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;
  mtlk_wssa_drv_tr181_wlan_stats_t tr181_stats;

  mtlk_core_get_tr181_wlan_stats(core, &tr181_stats);

  return mtlk_clpb_push_res_data(clpb, MTLK_ERR_OK, &tr181_stats, sizeof(mtlk_wssa_drv_tr181_wlan_stats_t));
}

int __MTLK_IFUNC
mtlk_core_get_tr181_hw_statistics (mtlk_handle_t hcore, const void *data, uint32 data_size)
{
  mtlk_core_t *core = HANDLE_T_PTR(mtlk_core_t, hcore);
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;
  wave_radio_t  *radio = wave_vap_radio_get(core->vap_handle);
  mtlk_wssa_drv_tr181_hw_stats_t tr181_stats;

  wave_radio_get_tr181_hw_stats(radio, &tr181_stats);

  return mtlk_clpb_push_res_data(clpb, MTLK_ERR_OK, &tr181_stats, sizeof(mtlk_wssa_drv_tr181_hw_stats_t));
}

int __MTLK_IFUNC
mtlk_core_get_tr181_peer_statistics (mtlk_handle_t hcore, const void *data, uint32 data_size)
{
  mtlk_core_t *core = HANDLE_T_PTR(mtlk_core_t, hcore);
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;
  int res = MTLK_ERR_OK;
  IEEE_ADDR *addr;
  uint32 addr_size;
  sta_entry *sta = NULL;
  mtlk_wssa_drv_tr181_peer_stats_t tr181_stats;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);
  addr = mtlk_clpb_enum_get_next(clpb, &addr_size);
  MTLK_CLPB_TRY(addr, addr_size)

    /* find station in stadb */
    sta = mtlk_stadb_find_sta(&core->slow_ctx->stadb, addr->au8Addr);
    if (NULL == sta) {
      WLOG_DY("CID-%04x: station %Y not found",
        mtlk_vap_get_oid(core->vap_handle), addr);
      MTLK_CLPB_EXIT(MTLK_ERR_UNKNOWN);
    } else {
      mtlk_sta_get_tr181_peer_stats(sta, &tr181_stats);
      mtlk_sta_decref(sta);
    }

  MTLK_CLPB_FINALLY(res)
    return mtlk_clpb_push_res_data(clpb, res, &tr181_stats, sizeof(mtlk_wssa_drv_tr181_peer_stats_t));
  MTLK_CLPB_END;
}
#endif /* MTLK_LEGACY_STATISTICS */
