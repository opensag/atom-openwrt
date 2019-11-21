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
#include "mtlk_df_nbuf.h"
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

#define LOG_LOCAL_GID   GID_CORE_CONFIG
#define LOG_LOCAL_FID   0


int poll_client_req(mtlk_vap_handle_t vap_handle, sta_entry *sta, int *status)
{
  int                       res = MTLK_ERR_OK;
  mtlk_txmm_msg_t           man_msg;
  mtlk_txmm_data_t          *man_entry = NULL;
  UMI_POLL_CLIENT_t         *pPollClient = NULL;

  *status = 0;

  man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, mtlk_vap_get_txmm(vap_handle), NULL);
  if (NULL == man_entry) {
    ELOG_D("CID-%04x: No man entry available", mtlk_vap_get_oid(vap_handle));
    return MTLK_ERR_NO_RESOURCES;
  }

  man_entry->id = UM_MAN_POLL_CLIENT_REQ;
  man_entry->payload_size = sizeof(UMI_POLL_CLIENT_t);

  pPollClient = (UMI_POLL_CLIENT_t *)(man_entry->payload);
  MTLK_STATIC_ASSERT(sizeof(pPollClient->stationIndex) == sizeof(uint16));
  pPollClient->stationIndex = HOST_TO_MAC16((uint16)mtlk_sta_get_sid(sta));
  pPollClient->isActive = 0;

  res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);

  if (MTLK_ERR_OK == res) {
    *status = pPollClient->isActive;

    if (*status) {
      sta->activity_timestamp = mtlk_osal_timestamp();
    }
  }
  else {
    ELOG_DD("CID-%04x: Can't get poll client status (%i)", mtlk_vap_get_oid(vap_handle), res);
  }

  mtlk_txmm_msg_cleanup(&man_msg);

  return res;
}

int __MTLK_IFUNC core_cfg_get_station (mtlk_handle_t hcore, const void* data, uint32 data_size)
{
  mtlk_core_t *core = HANDLE_T_PTR(mtlk_core_t, hcore);
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;
  const IEEE_ADDR *addr;
  sta_entry *sta = NULL;
  st_info_data_t *info_data;
  unsigned info_data_size;
  int res = MTLK_ERR_OK;
  int status;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  info_data = mtlk_clpb_enum_get_next(clpb, &info_data_size);
  MTLK_CLPB_TRY(info_data, info_data_size)
    if (mtlk_vap_is_ap(core->vap_handle) && !wave_core_sync_hostapd_done_get(core)) {
      ILOG1_D("CID-%04x: HOSTAPD STA database is not synced", mtlk_vap_get_oid(core->vap_handle));
      MTLK_CLPB_EXIT(MTLK_ERR_NOT_READY);
    }

    /* find station in stadb */
    addr = (const IEEE_ADDR *)info_data->mac;
    sta = mtlk_stadb_find_sta(&core->slow_ctx->stadb, addr->au8Addr);
    if(NULL == sta) {
      WLOG_DY("CID-%04x: station %Y not found", mtlk_vap_get_oid(core->vap_handle), addr->au8Addr);
      MTLK_CLPB_EXIT(MTLK_ERR_UNKNOWN);
    }

    res = poll_client_req(core->vap_handle, sta, &status);

    /* reset "filled"  bitstring (update it after each new variable is filled in the station info structure) */
    info_data->stinfo->filled = 0;

    /* connected time (in seconds) */
    info_data->stinfo->connected_time = mtlk_sta_get_age(sta);
    info_data->stinfo->filled |= BIT_ULL(NL80211_STA_INFO_CONNECTED_TIME);

    /* inactive time (in milliseconds) */
    info_data->stinfo->inactive_time = mtlk_sta_get_inactive_time(sta);
    info_data->stinfo->filled |= BIT_ULL(NL80211_STA_INFO_INACTIVE_TIME);

    /* the value is signed, therefore decided to use signal
       field of station_info structure, not max_rssi, which is unsigned */
    info_data->stinfo->signal = mtlk_sta_get_mgmt_max_rssi(sta);
    info_data->stinfo->filled |= BIT_ULL(NL80211_STA_INFO_SIGNAL);

    mtlk_sta_decref(sta); /* De-reference of find */

    ILOG2_DD("STA age: %u sec, STA inactive: %u msec", info_data->stinfo->connected_time, info_data->stinfo->inactive_time);
  MTLK_CLPB_FINALLY(res)
    return mtlk_clpb_push_res(clpb, res);
  MTLK_CLPB_END
}

/* Send current WMM params to FW and set vap_in_fw_is_active to TRUE if sending succeeded */
static int
_mtlk_core_cfg_wmm_param_send(mtlk_core_t *core)
{
  int res;
  mtlk_txmm_msg_t   man_msg;
  mtlk_txmm_data_t *man_entry;
  mtlk_pdb_t       *param_db_core;
  UMI_SET_WMM_PARAMETERS *req;
  unsigned  oid;
  int       i;

  oid = mtlk_vap_get_oid(core->vap_handle);
  ILOG1_D("CID-%04x: Send WMM params", oid);

  /* prepare msg for the FW */
  if (!(man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, mtlk_vap_get_txmm(core->vap_handle), &res)))
  {
    ELOG_DD("CID-%04x: UM_MAN_SET_WMM_PARAMETERS_REQ init failed, err=%i", oid, res);
    return MTLK_ERR_NO_RESOURCES;
  }

  man_entry->id                  = UM_MAN_SET_WMM_PARAMETERS_REQ;
  man_entry->payload_size        = sizeof(*req);

  req = (UMI_SET_WMM_PARAMETERS *) man_entry->payload;
  memset(req, 0, sizeof(*req));

  /* Read WMM settings from the Master VAP ParamDB and fill the message */
  param_db_core = mtlk_vap_get_param_db(mtlk_core_get_master(core)->vap_handle);

  for (i = 0; i < UMI_AC_NUM_ACS; i++)
  {
    struct mtlk_txq_params mtp;
    mtlk_pdb_size_t mtp_len = sizeof(mtp);
    wave_pdb_get_binary(param_db_core, PARAM_DB_CORE_WMM_PARAMS_BE + i, &mtp, &mtp_len);
    req->wmmParamsPerAc[i].u16CWmin = HOST_TO_MAC16(mtp.cwmin);
    req->wmmParamsPerAc[i].u16CWmax = HOST_TO_MAC16(mtp.cwmax);
    req->wmmParamsPerAc[i].u16TXOPlimit = HOST_TO_MAC16(mtp.txop);
    req->wmmParamsPerAc[i].u8Aifsn = mtp.aifs;
    req->wmmParamsPerAc[i].acm_flag = mtp.acm_flag;
    ILOG2_DDDDDD("[%d]: txop %d, cwmin %d, cwmax %d, aifs %d, acm_flag %d",
                i, mtp.txop, mtp.cwmin, mtp.cwmax, mtp.aifs, mtp.acm_flag);
  }

  /* Set VAP ID and send the message */
  req->vapId = mtlk_vap_get_id(core->vap_handle);
  mtlk_dump(3, req, sizeof(*req), "dump of UMI_SET_WMM_PARAMETERS:");

  res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);
  if (MTLK_ERR_OK != res) {
    ELOG_DD("CID-%04x: UM_MAN_SET_WMM_PARAMETERS_REQ send failed, err=%i", oid, res);
  } else {
    core->vap_in_fw_is_active = TRUE;
  }

  mtlk_txmm_msg_cleanup(&man_msg);
  return res;
}

static int
_mtlk_core_cfg_wmm_param_set_sta(mtlk_core_t *core)
{
  ILOG1_D("CID-%04x", mtlk_vap_get_oid(core->vap_handle));
  return _mtlk_core_cfg_wmm_param_send(core);
}

static int
_mtlk_core_cfg_wmm_param_set_ap(mtlk_core_t *master_core)
{
    mtlk_vap_handle_t   vap_handle;
    mtlk_core_t        *vap_core;
    mtlk_vap_manager_t *vap_mgr;
    int                 i, vap_res, max_vaps_count;
    int                 res = MTLK_ERR_OK;

    MTLK_ASSERT(NULL != master_core);

    /* Send a msg with all WMM params to each VAP */
    vap_mgr = mtlk_vap_get_manager(master_core->vap_handle);
    max_vaps_count = mtlk_vap_manager_get_max_vaps_count(vap_mgr);

    for (i = 0; i < max_vaps_count; i++)
    {
      if (MTLK_ERR_OK != mtlk_vap_manager_get_vap_handle(vap_mgr, i, &vap_handle))
      {
        ILOG5_D("VapID %u doesn't exist, goto next", i);
        continue;
      }

      ILOG1_DD("VapID %u, is_ap %d", i, (int)mtlk_vap_is_ap(vap_handle));

      if (!mtlk_vap_is_ap(vap_handle))
        continue;

      vap_core = mtlk_vap_get_core(vap_handle);

      /* Don't send if ADD_VAP has not yet been sent */
      if (vap_core->is_stopped)
        continue;
      
      /* Don't send for the same VAP if already sent */
      if (vap_core->vap_in_fw_is_active)
        continue;
      
      ILOG1_D("CID-%04x: Sending UMI_SET_WMM_PARAMETERS", mtlk_vap_get_oid(vap_handle));
      vap_res = _mtlk_core_cfg_wmm_param_send(vap_core);
      if (vap_res != MTLK_ERR_OK) {
        res = vap_res;
      }
    }

    return res;
}

/* Should be called from master serializer context */
int core_cfg_wmm_param_set_by_params(mtlk_core_t *master_core, struct mtlk_wmm_settings *wmm_settings)
{
  mtlk_vap_handle_t          vap_handle;
  unsigned                   oid;
  int                        res = MTLK_ERR_OK;

  MTLK_ASSERT(master_core == mtlk_core_get_master(master_core));

  oid = mtlk_vap_get_oid(master_core->vap_handle);

  /* Prevent setting WMM while waiting for beacons */
  if (mtlk_core_is_block_tx_mode(master_core)) {
    res = MTLK_ERR_NOT_READY;
    goto end;
  }

  /* Prevent setting WMM while in scan */
  if (mtlk_core_is_in_scan_mode(master_core)) {
    res = MTLK_ERR_RETRY;
    goto end;
  }

  /* FIXME: 1) what is vap_id for STA mode ?   2) does this checking needed or not ? */
  if (MTLK_ERR_OK != mtlk_vap_manager_get_vap_handle(mtlk_vap_get_manager(master_core->vap_handle),
                                                             wmm_settings->vap_id, &vap_handle)) {
    res = MTLK_ERR_VALUE;
    goto end;
  }

  /* AP's will call this every queue
   * while STA's will only call this once for passing all queues data to fw */
  if (mtlk_vap_is_ap(vap_handle)) {
    struct mtlk_txq_params    *tp = &wmm_settings->txq_params;
    ILOG2_DDD("CID-%04x: Saving WMM params for VapID %u queue %u", oid, wmm_settings->vap_id, tp->ac);
    res = wave_pdb_set_binary(mtlk_vap_get_param_db(vap_handle),
                              PARAM_DB_CORE_WMM_PARAMS_BE + tp->ac, tp, sizeof(*tp));
    if (MTLK_ERR_OK != res) {
      WLOG_DDD("CID-%04x: Saving WMM params for queue %u failure (%d)."
               " Continue with default settings.", oid, tp->ac, res);
    }

    if (tp->ac != UMI_AC_BK) /* not the last queue to be refered to by hostapd */
      goto end;

    res = _mtlk_core_cfg_wmm_param_set_ap(master_core);

  } else { /* STA */
    res = _mtlk_core_cfg_wmm_param_set_sta(mtlk_vap_get_core (vap_handle));
  }

end:
  return res;
}

/* Should be called from master serializer context */
int __MTLK_IFUNC core_cfg_wmm_param_set (mtlk_handle_t hcore, const void* data, uint32 data_size)
{
  mtlk_core_t *master_core = HANDLE_T_PTR(mtlk_core_t, hcore);
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;
  struct mtlk_wmm_settings *wmm_settings;
  unsigned wmm_settings_size;
  int res = MTLK_ERR_OK;

  MTLK_ASSERT(sizeof(mtlk_clpb_t *) == data_size);

  wmm_settings = mtlk_clpb_enum_get_next(clpb, &wmm_settings_size);
  MTLK_CLPB_TRY(wmm_settings, wmm_settings_size)
    res = core_cfg_wmm_param_set_by_params(master_core, wmm_settings);
  MTLK_CLPB_FINALLY(res)
    return mtlk_clpb_push_res(clpb, res);
  MTLK_CLPB_END
}

int core_cfg_set_calibration_mask(mtlk_handle_t hcore, const void* data, uint32 data_size)
{
  mtlk_core_t *core = HANDLE_T_PTR(mtlk_core_t, hcore);
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **)data;
  wave_radio_calibration_cfg_t *calibration_cfg;
  unsigned calibration_cfg_size;
  wave_radio_t  *radio;
  int            res = MTLK_ERR_OK;

  MTLK_ASSERT(core == mtlk_vap_manager_get_master_core(mtlk_vap_get_manager(core->vap_handle)));
  MTLK_ASSERT(sizeof(mtlk_clpb_t *) == data_size);

  radio = wave_vap_radio_get(core->vap_handle);

  calibration_cfg = mtlk_clpb_enum_get_next(clpb, &calibration_cfg_size);
  MTLK_CLPB_TRY(calibration_cfg, calibration_cfg_size)
    MTLK_CFG_START_CHEK_ITEM_AND_CALL()
      MTLK_CFG_CHECK_ITEM_AND_CALL(calibration_cfg, calibr_algo_mask, wave_radio_send_hdk_config,
                                   (radio, calibration_cfg->calibr_algo_mask,
                                    WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_ONLINE_ACM)), res);

      MTLK_CFG_CHECK_ITEM_AND_CALL(calibration_cfg, online_calibr_algo_mask, wave_radio_send_hdk_config,
                                   (radio, WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_CALIBRATION_ALGO_MASK),
                                   calibration_cfg->online_calibr_algo_mask), res);
    MTLK_CFG_END_CHEK_ITEM_AND_CALL()
  MTLK_CLPB_FINALLY(res)
    return mtlk_clpb_push_res(clpb, res);
  MTLK_CLPB_END
}

int core_cfg_get_calibration_mask(mtlk_handle_t hcore, const void* data, uint32 data_size)
{
  mtlk_core_t *core = HANDLE_T_PTR(mtlk_core_t, hcore);
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **)data;
  wave_radio_calibration_cfg_t calibration_cfg;
  wave_radio_t  *radio;
  int            res;

  MTLK_ASSERT(core == mtlk_vap_manager_get_master_core(mtlk_vap_get_manager(core->vap_handle)));
  MTLK_ASSERT(sizeof(mtlk_clpb_t *) == data_size);

  radio = wave_vap_radio_get(core->vap_handle);

  /* get from the FW */
  res = wave_radio_read_hdk_config(radio,
                                   &calibration_cfg.calibr_algo_mask,
                                   &calibration_cfg.online_calibr_algo_mask);

  MTLK_CFG_SET_ITEM(&calibration_cfg, calibr_algo_mask, calibration_cfg.calibr_algo_mask);
  MTLK_CFG_SET_ITEM(&calibration_cfg, online_calibr_algo_mask, calibration_cfg.online_calibr_algo_mask);

  /* push result and config into clipboard */
  res = mtlk_clpb_push(clpb, &res, sizeof(res));
  if (MTLK_ERR_OK == res) {
    res = mtlk_clpb_push(clpb, &calibration_cfg, sizeof(calibration_cfg));
  }
  return res;
}

int core_cfg_send_set_chan_by_msg(mtlk_txmm_msg_t *man_msg)
{
  mtlk_txmm_data_t *man_entry = man_msg->data;
  UM_SET_CHAN_PARAMS *req = (UM_SET_CHAN_PARAMS *) man_entry->payload;
  int res;

  /* FW expects bg_scan as a flag 0 or 1, make sure it is the case */
  req->bg_scan = !!req->bg_scan;

  ILOG1_DDDDSDD("Switching channel to %hhu--%u (width %u, primary at %u), type %s, sub band DFS %d, bg_scan %d",
              req->low_chan_num,
              req->low_chan_num + (CHANNUMS_PER_20MHZ << req->chan_width) - CHANNUMS_PER_20MHZ,
              (MHZS_PER_20MHZ << req->chan_width),
              req->low_chan_num + CHANNUMS_PER_20MHZ * req->primary_chan_idx,
              switchtype2string(req->switch_type),
              req->ChannelNotificationMode,
              req->bg_scan);
  mtlk_dump(2, req, sizeof(*req), "dump of UM_SET_CHAN_PARAMS:");

  res = mtlk_txmm_msg_send_blocked(man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);

  if (res != MTLK_ERR_OK)
  {
      ELOG_D("UM_SET_CHAN_REQ send failed, err=%i", res);
  }
  else if (req->result)
  {
    ELOG_D("UM_SET_CHAN_REQ execution failed, result=%hhu", req->result);
    res = MTLK_ERR_MAC;
  }

  return res;
}

static int
mtlk_core_cfg_update_antenna_mask (mtlk_core_t *core, uint8 ant_mask)
{
  int res = MTLK_ERR_OK;
  BOOL update_hw = FALSE;
  mtlk_coc_t *coc_obj = __wave_core_coc_mgmt_get(core);
  mtlk_df_t *df = mtlk_vap_get_df(core->vap_handle);
  mtlk_df_user_t *df_user = mtlk_df_get_user(df);
  struct wireless_dev *wdev = mtlk_df_user_get_wdev(df_user);

  MTLK_ASSERT(coc_obj);

  res = mtlk_coc_update_antenna_cfg(coc_obj, ant_mask, &update_hw);

  if (MTLK_ERR_OK != res)
    return res;

  if (update_hw) { /* Update Antennas config and current mask */
    wave_radio_antenna_cfg_update(wave_vap_radio_get(core->vap_handle), ant_mask);

    wv_cfg80211_update_wiphy_ant_mask(wdev->wiphy, ant_mask);
  }

  return res;
}

static int
_mtlk_core_send_erp_if_needed (mtlk_core_t *core)
{
  int res;
  mtlk_erp_cfg_t erp_cfg;
  mtlk_pdb_size_t erp_size = sizeof(mtlk_erp_cfg_t);

  res = WAVE_RADIO_PDB_GET_BINARY(wave_vap_radio_get(core->vap_handle), PARAM_DB_RADIO_ERP_CFG, &erp_cfg, &erp_size);
  if (MTLK_ERR_OK != res) {
    ELOG_D("CID-%04x: Failed to get ERP configuration", mtlk_vap_get_oid(core->vap_handle));
    return res;
  }

  /* If Initial wait time is set, this means iwpriv was executed during scan */
  if (erp_cfg.initial_wait_time) {
    res = mtlk_core_send_erp_cfg(core, &erp_cfg);

    if (MTLK_ERR_OK != res)
      return res;
  }

  /* Clear values in param DB, as they are used for postponing the sending after scan */
  memset(&erp_cfg, 0, sizeof(erp_cfg));
  erp_size = sizeof(mtlk_erp_cfg_t);
  if (MTLK_ERR_OK != WAVE_RADIO_PDB_SET_BINARY(wave_vap_radio_get(core->vap_handle), PARAM_DB_RADIO_ERP_CFG, &erp_cfg, erp_size))
    ELOG_D("CID-%04x: Failed to clear ERP config in paramDB", mtlk_vap_get_oid(core->vap_handle));

  return res;
}

static BOOL _is_radar_det_needed (wave_radio_t *radio, const struct mtlk_chan_def *cd)
{
  uint32 cw = mtlkcw2cw(cd->width);
  struct wiphy *wiphy = wv_cfg80211_wiphy_get(wave_radio_cfg80211_get(radio));

  if (wv_cfg80211_get_chans_dfs_required(wiphy, cd->center_freq1, cw))
    return TRUE;

  if (!cd->center_freq2)
    return FALSE;

  return wv_cfg80211_get_chans_dfs_required(wiphy, cd->center_freq2, cw);
}

/* cpd may be NULL, only needed for the CSA stuff */
int core_cfg_send_set_chan(mtlk_core_t *core,
                           const struct mtlk_chan_def *cd, struct set_chan_param_data *cpd)
{
  mtlk_txmm_msg_t       man_msg;
  mtlk_txmm_data_t      *man_entry;
  UM_SET_CHAN_PARAMS    *req;
  struct mtlk_chan_def  *ccd = __wave_core_chandef_get(core);
  wave_radio_t          *radio = wave_vap_radio_get(core->vap_handle);
  unsigned              primary_chan_num, old_freq1;
  int                   res = MTLK_ERR_OK;

  ILOG2_DDDD("band=%u, center_freq=%hu, flags=0x%08x, nl_width=%u",
             cd->chan.band, cd->chan.center_freq, cd->chan.flags, cd->width);

  /* prepare msg for the FW */
  if (!(man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, mtlk_vap_get_txmm(core->vap_handle), &res)))
  {
    ELOG_DD("CID-%04x: UM_MAN_SET_CHAN_REQ init failed, err=%i",
            mtlk_vap_get_oid(core->vap_handle), res);
    return MTLK_ERR_NO_RESOURCES;
  }

  man_entry->id = UM_SET_CHAN_REQ;
  man_entry->payload_size = sizeof(*req);
  req = (UM_SET_CHAN_PARAMS *) man_entry->payload;
  memset(req, 0, sizeof(*req));

  req->RegulationType = (uint8)core_cfg_get_regd_code(core);

  /* Make sure that our notion of channel width codes and FW's notion coincide */
  MTLK_STATIC_ASSERT(CW_20 == (unsigned) BANDWIDTH_TWENTY);
  MTLK_STATIC_ASSERT(CW_40 == (unsigned) BANDWIDTH_FOURTY);
  MTLK_STATIC_ASSERT(CW_80 == (unsigned) BANDWIDTH_EIGHTY);
  MTLK_STATIC_ASSERT(CW_160 == (unsigned) BANDWIDTH_ONE_HUNDRED_SIXTY);
  MTLK_STATIC_ASSERT(CW_80_80 == (unsigned) BANDWIDTH_EIGHTY_EIGHTY);

  if (MTLK_SB_DFS_BW_20 == cd->sb_dfs.sb_dfs_bw)
    req->ChannelNotificationMode = CHANNEL_NOTIFICATION_TWENTY;
  else if (MTLK_SB_DFS_BW_40 == cd->sb_dfs.sb_dfs_bw)
    req->ChannelNotificationMode = CHANNEL_NOTIFICATION_FOURTY;
  else if (MTLK_SB_DFS_BW_80 == cd->sb_dfs.sb_dfs_bw)
    req->ChannelNotificationMode = CHANNEL_NOTIFICATION_EIGHTY;
  else
    req->ChannelNotificationMode = CHANNEL_NOTIFICATION_NORMAL;

  if (req->ChannelNotificationMode) {
    req->chan_width = cd->sb_dfs.width;
    req->low_chan_num = freq2lowchannum(cd->sb_dfs.center_freq, cd->sb_dfs.width);
  }
  else {
    req->chan_width = cd->width;
    req->low_chan_num = freq2lowchannum(cd->center_freq1, cd->width);
  }

  primary_chan_num = ieee80211_frequency_to_channel(cd->chan.center_freq);
  req->primary_chan_idx = (primary_chan_num - req->low_chan_num) / CHANNUMS_PER_20MHZ;

  if (!wave_radio_get_sta_vifs_exist(radio) && _is_radar_det_needed(radio, cd)) {
    req->isRadarDetectionNeeded = 1;
  } else {
    req->isRadarDetectionNeeded = 0;
  }
  req->isContinuousInterfererDetectionNeeded = is_interf_det_needed(primary_chan_num);

#if 0 /* for later, if/when 80+80 support gets added */
  if (cd->width == CW_80_80) /* this assumes that center_freq2 > center_freq1 */
  {
    req->low_chan_num2 = freq2lowchannum(cd->center_freq2, cd->chan_width);

    if (req->primary_chan_idx >= CHANS_IN_80MHZ) /* not in the first range, so must be in the second */
      req->primary_chan_idx = CHANS_IN_80MHZ + (primary_chan_num - req->low_chan_num2) / CHANNUMS_PER_20MHZ;
  }
#endif

  if (cpd)
  {
    /* 0 except for CSA, in which case multiplication will already have been done */
    req->chan_switch_time = HOST_TO_MAC16(cpd->chan_switch_time);
    req->switch_type = cpd->switch_type;
    req->block_tx_pre = cpd->block_tx_pre;
    req->block_tx_post = cpd->block_tx_post;
    req->bg_scan = cpd->bg_scan;
    req->u16SID = HOST_TO_MAC16(cpd->sid);
  }

  /* leave result and reserved as 0 */

  old_freq1 = ccd->center_freq1;
  ccd->center_freq1 = 0; /* this marks that channel is "uncertain" (in this case changing) */

  wmb(); /* so that channel gets marked uncertain well before we start switching it */

  (void)mtlk_core_radio_enable_if_needed(core);

  core->chan_state = ST_LAST;

  res = core_cfg_send_set_chan_by_msg(&man_msg);

  /* Send CCA threshold on every set channel */
  if (MTLK_ERR_OK == res) {
    mtlk_core_cfg_send_actual_cca_threshold(core);
  }

  if (res != MTLK_ERR_OK)
  {
    ccd->center_freq1 = old_freq1;
    goto end;
  }

  core->chan_state = req->switch_type;

  if (cpd) {
    wave_memcpy(cpd->rssi, sizeof(cpd->rssi), req->rssi, sizeof(req->rssi));
    wave_memcpy(cpd->noise, sizeof(cpd->noise), req->noise, sizeof(req->noise));
    wave_memcpy(cpd->rf_gain, sizeof(cpd->rf_gain), req->rf_gain, sizeof(req->rf_gain));
    cpd->rate = MAC_TO_HOST32(req->rate);
  }
  {
    /* A lot of code gets the current chan from the param_db, so set it there, too.  But only for the radio! */
    WAVE_RADIO_PDB_SET_INT(radio, PARAM_DB_RADIO_CHANNEL_CUR, freq2lowchannum(cd->chan.center_freq, CW_20));
    /* some code will check the bonding for HT, so this is useful; for VHT we'll need more fixes */
    WAVE_RADIO_PDB_SET_INT(radio, PARAM_DB_RADIO_BONDING_MODE, req->primary_chan_idx);
    /* and let's set this too for old times' sake, but it will have to be called CHANWIDTH eventually */
    WAVE_RADIO_PDB_SET_INT(radio, PARAM_DB_RADIO_SPECTRUM_MODE, req->chan_width);
    /* and one more superfluous thing because it's obvious from the channel number */
    WAVE_RADIO_PDB_SET_INT(radio, PARAM_DB_RADIO_FREQ_BAND_CUR, cd->chan.band);

    if (req->chan_width >= CW_80) {
      mtlk_core_update_network_mode(core, MTLK_NETWORK_11ANAC_MIXED);
    }

    /* succeeded, so remember the current settings */
    __wave_core_chan_switch_type_set(core, req->switch_type);

    ccd->chan = cd->chan; /* struct assignment */
    ccd->width = cd->width;
    ccd->center_freq2 = cd->center_freq2;
    ccd->is_noht = cd->is_noht;
    ccd->sb_dfs.sb_dfs_bw = cd->sb_dfs.sb_dfs_bw;
    ccd->sb_dfs.width = cd->sb_dfs.width;
    ccd->sb_dfs.center_freq = cd->sb_dfs.center_freq;

    wmb(); /* so that the channel is marked "certain" only after we've switched it and recorded everything */

    ccd->center_freq1 = ccd != cd ? cd->center_freq1 : old_freq1;
  }

  mtlk_core_cfg_update_antenna_mask(core, req->antennaMask);

  /* Set these params after setting operating channel */
  if (req->switch_type == ST_NORMAL || req->switch_type == ST_CSA) {
    (void)mtlk_core_set_coc_actual_power_mode(core);
    (void)_mtlk_core_send_erp_if_needed(core);
  }

end:
  (void)mtlk_core_radio_disable_if_needed(core);

  mtlk_txmm_msg_cleanup(&man_msg);
  return res;
}

int core_recovery_cfg_wmm_param_set(mtlk_core_t *core)
{
  return _mtlk_core_cfg_wmm_param_send(core);
}

/* CFG80211: set_ap_chanwidth(), start_radar_detection() and channel_switch() */
/* MAC80211: start(), config(), switch_channel()  all invoke this function */
/* supposed to get done from the Master VAP's serializer */
int __MTLK_IFUNC core_cfg_set_chan_clpb (mtlk_handle_t hcore, const void* data, uint32 data_size)
{
  mtlk_core_t *core = HANDLE_T_PTR(mtlk_core_t, hcore);
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;
  struct mtlk_chan_def *ccd;
  struct set_chan_param_data *cpd;
  unsigned cpd_size;
  BOOL paused_scan = FALSE;
  mtlk_df_t *df = mtlk_vap_get_df(core->vap_handle);
  wave_radio_t          *radio = wave_vap_radio_get(core->vap_handle);
  mtlk_scan_support_t   *ss = __wave_core_scan_support_get(core);
  mtlk_vap_handle_t     vap_handle;
  int res = MTLK_ERR_OK;

  MTLK_ASSERT(core == mtlk_core_get_master(core));
  MTLK_ASSERT(sizeof(mtlk_clpb_t *) == data_size);

  ccd = __wave_core_chandef_get(core);

  cpd = mtlk_clpb_enum_get_next(clpb, &cpd_size);
  MTLK_CLPB_TRY(cpd, cpd_size)
    /* When in block_tx mode, prevent all channel switches except block_tx off */
    if (core->slow_ctx->is_block_tx == TRUE &&
        ccd->wait_for_beacon == TRUE)
    {
      ILOG0_V("Ignoring channel switches when in block_tx mode");
      MTLK_CLPB_EXIT(MTLK_ERR_BUSY);
    }

    if (MTLK_ERR_OK != mtlk_vap_manager_get_vap_handle(mtlk_df_get_vap_manager(df), cpd->vap_id, &vap_handle)) {
      MTLK_CLPB_EXIT(MTLK_ERR_OK);
    }

    if (cpd->cac_time)
      cpd->switch_type = ST_SCAN; /* because we didn't have the ST_SCAN definition in cfg80211.c */
    else if (cpd->chan_switch_time) /* ditto */
      cpd->switch_type = ST_CSA;

    if (!(mtlk_core_get_scan_support(core)->dfs_debug_params.debug_chan && (cpd->switch_type == ST_SCAN))
        && ccd->chan.center_freq /* we already have a channel set */
        && cpd->switch_type == __wave_core_chan_switch_type_get(core) /* with the same switch type as the new request */
        && mtlk_core_is_chandef_identical(&cpd->chandef, ccd)) /* and all the channel params match */
    {
      unsigned low_chan_num;
      unsigned primary_chan_num;
      unsigned primary_chan_idx;

      low_chan_num = freq2lowchannum(ccd->center_freq1, ccd->width);
      primary_chan_num = ieee80211_frequency_to_channel(ccd->chan.center_freq);
      primary_chan_idx = (primary_chan_num - low_chan_num) / CHANNUMS_PER_20MHZ;
      ccd->is_noht = cpd->chandef.is_noht;

      /* Notify cfg80211, AP mode VAP. MAC80211 will call cfg80211_ch_switch_notify
       * for Station role interfaces */
      if (!cpd->dont_notify_kernel) {
        if (wave_radio_get_sta_vifs_exist(radio) && is_channel_certain(ccd))
          wv_cfg80211_ch_switch_notify(cpd->ndev, ccd, TRUE);
        else
          wv_cfg80211_ch_switch_notify(cpd->ndev, ccd, FALSE);
      }

      ILOG1_DDDD("Avoiding setting channel %u--%u (width %u, primary at %u) that is already set",
                 low_chan_num, low_chan_num + (CHANNUMS_PER_20MHZ << ccd->width) - CHANNUMS_PER_20MHZ,
                 (MHZS_PER_20MHZ << ccd->width), low_chan_num + CHANNUMS_PER_20MHZ * primary_chan_idx);
      MTLK_CLPB_EXIT(res);
    }

    if (mtlk_core_is_band_supported(core, cpd->chandef.chan.band) != MTLK_ERR_OK)
    {
      ELOG_D("HW does not support band %i", cpd->chandef.chan.band);
      MTLK_CLPB_EXIT(MTLK_ERR_PARAMS);
    }

    if (is_scan_running(ss))
    {
      if (!(ss->flags & SDF_BACKGROUND))
      {
        ELOG_V("Cannot change channels in the middle of a non-BG scan");
        MTLK_CLPB_EXIT(MTLK_ERR_BUSY);
      }
      else if (!(ss->flags & SDF_BG_BREAK)) /* background scan and not on a break at the moment */
      {
        ILOG0_V("Background scan encountered, so pausing it first");

        if ((res = pause_or_prevent_scan(core)) != MTLK_ERR_OK)
        {
          ELOG_V("Scan is already paused/prevented, canceling the channel switch");
          MTLK_CLPB_EXIT(res);
        }

        paused_scan = TRUE;
      }
      else
        ILOG0_V("Background scan during its break encountered, so changing the channel");
    }

    /* don't allow to set 2.4GHz channel's width to 40 MHz if Multi-Radio Co-Existence is enabled */
    core_cfg_change_chan_width_if_coex_en(core, &cpd->chandef);
    /* set channel */
    res = core_cfg_set_chan(core, &cpd->chandef, cpd);

    if (res != MTLK_ERR_OK) {
      ccd->wait_for_beacon = TRUE;
      MTLK_CLPB_EXIT(res);
    }


    if (cpd->switch_type == ST_NORMAL &&
        core->slow_ctx->is_block_tx) {
      ILOG0_V("Exited block_tx mode successfully");
      core->slow_ctx->is_block_tx = FALSE;
    }

    if (is_scan_running(ss))
      /* save what we just set so the scan can return to it when it finishes */
      ss->orig_chandef = *ccd;

    /* In case of a successful NORMAL or CSA switch, notify the kernel, too, because hostapd need to know */
    if (!cpd->dont_notify_kernel && (cpd->switch_type == ST_NORMAL || cpd->switch_type == ST_CSA))
    {
      ILOG0_DD("Notifying the kernel about the channel switch (width %u, primary at %u)",
               MHZS_PER_20MHZ << cpd->chandef.width, ieee80211_frequency_to_channel(cpd->chandef.chan.center_freq));

      if (wave_radio_get_sta_vifs_exist(radio) && is_channel_certain(ccd))
        wv_cfg80211_ch_switch_notify(cpd->ndev, ccd, TRUE);
      else
        wv_cfg80211_ch_switch_notify(cpd->ndev, ccd, FALSE);
    }
  MTLK_CLPB_FINALLY(res)
    if (paused_scan)
    {
      ILOG0_V("Resuming the paused scan");
      resume_or_allow_scan(core);
    }
    return mtlk_clpb_push_res(clpb, res);
  MTLK_CLPB_END
}

/* Get/set param DB value of BLOCK_TX */
void __MTLK_IFUNC
mtlk_core_cfg_set_block_tx (mtlk_core_t *core, int value)
{
  WAVE_RADIO_PDB_SET_INT(wave_vap_radio_get(core->vap_handle), PARAM_DB_RADIO_BLOCK_TX, value);
}

int __MTLK_IFUNC
mtlk_core_cfg_get_block_tx (mtlk_core_t *core)
{
  return WAVE_RADIO_PDB_GET_INT(wave_vap_radio_get(core->vap_handle), PARAM_DB_RADIO_BLOCK_TX);
}

/* cpd may be NULL for normal and scan switches; only needed for CSA */
int __MTLK_IFUNC core_cfg_set_chan (mtlk_core_t *core, const struct mtlk_chan_def *cd, struct set_chan_param_data *cpd)
{
  struct scan_support *ss = __wave_core_scan_support_get(core);
  wave_radio_t  *radio;
  uint32 param_algomask;
  uint32 param_oalgomask;
  mtlk_country_code_t country_code;
  int res = MTLK_ERR_OK;
  BOOL cancel_cac_timer = TRUE;

  MTLK_ASSERT(core == mtlk_vap_manager_get_master_core(mtlk_vap_get_manager(core->vap_handle)));

  radio = wave_vap_radio_get(core->vap_handle);
  param_algomask = WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_CALIBRATION_ALGO_MASK);
  param_oalgomask = WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_ONLINE_ACM);

  if (!is_channel_certain(cd)) {
    ELOG_V("Channel is not set");
    return MTLK_ERR_NOT_SUPPORTED;
  }

  ILOG2_DDDD("band=%u, center_freq=%hu, flags=0x%08x, nl_width=%u",
             cd->chan.band, cd->chan.center_freq, cd->chan.flags, cd->width);

  /* may not do set_chan unless there has been a VAP activated */
  if (0 == mtlk_vap_manager_get_active_vaps_number(mtlk_vap_get_manager(core->vap_handle))
      && ((res = mtlk_mbss_send_vap_activate(core, cd->chan.band)) != MTLK_ERR_OK))
  {
    ELOG_D("Could not activate the master VAP, err=%i", res);
    return res;
  }

  core_cfg_country_code_get(core, &country_code);

  /* set TX power limits */
  core_cfg_set_tx_power_limit(core, cd->center_freq1, cd->width, cd->chan.center_freq, country_code);

  res = core_cfg_send_set_chan(core, cd, cpd);

  if (res == MTLK_ERR_OK && cpd) {
    /* Store channel parameters for Recovery purpose */
    wave_rcvry_chanparam_set(mtlk_vap_get_hw(core->vap_handle),
                             mtlk_vap_get_manager(core->vap_handle), (mtlk_handle_t*)cpd);

    if ((cpd->switch_type == ST_SCAN) && cpd->cac_time) {
      ILOG0_D("Setting the CAC timer for %i ms", cpd->cac_time);
      mtlk_osal_timer_set(&core->slow_ctx->cac_timer, cpd->cac_time);
      cancel_cac_timer = FALSE;

      if (ss->dfs_debug_params.debug_chan)
        ss->dfs_debug_params.switch_in_progress = FALSE;
    }
  }

  if (cancel_cac_timer) {
    mtlk_osal_timer_cancel_sync(&core->slow_ctx->cac_timer);
    mtlk_core_cfg_set_block_tx(core, 0);
  }

  if (cpd && cpd->switch_type == ST_CSA)
  { /* restore default threshold, if adaptive */
    int i;
    iwpriv_cca_th_t    cca_th_params;
    iwpriv_cca_adapt_t cca_adapt_params;
    mtlk_pdb_size_t    cca_adapt_size = sizeof(cca_adapt_params);

    if (MTLK_ERR_OK == WAVE_RADIO_PDB_GET_BINARY(radio, PARAM_DB_RADIO_CCA_ADAPT, &cca_adapt_params, &cca_adapt_size)) {
      if (cca_adapt_params.initial)
      {
        /* read user config */
        if (MTLK_ERR_OK != wave_radio_cca_threshold_get(radio, &cca_th_params)) {
          return MTLK_ERR_PARAMS;
        }

        for (i = 0; i < MTLK_CCA_TH_PARAMS_LEN; i++)
          core->slow_ctx->cca_adapt.cca_th_params[i] = cca_th_params.values[i];

        /* CCA threshold is sent just after set channel */
        /* mtlk_core_cfg_send_cca_threshold_req(core, &cca_th_params); */
      }
    }
  }

  return res;
}


/* If a "set-channel process" is still going on in FW
 * (started by any set-channel CMD and finished by the CFM of the closest set-channel that has
 * either type NORMAL or type CSA and block_tx_post=FALSE) then we have to finish it before starting any
 * other "process" (such as remove_vap or set_bss).  But we only ever use block_tx_post=FALSE so the only
 * way to leave the "set-channel process" open is by doing switch_type=SCAN.
 * This must be run on the MASTER CORE'S SERIALIZER.
 */
int finish_fw_set_chan_if_any(mtlk_core_t *core)
{
  int res = MTLK_ERR_OK;

  MTLK_ASSERT(core == mtlk_core_get_master(core));

  if (mtlk_core_is_in_scan_mode(core) || mtlk_core_is_chan_switch_type_csa_with_block_tx(core))
  {
    struct mtlk_chan_def *ccd = __wave_core_chandef_get(core);

    /* we're in the master serializer and not ourselves changing the channel,
     * so it must be clear and certain, unless it was never set outside the scanning
     * (in which case it also gets restored uncertain after the scan).
     * In this latter weird case we must set it to just about anything valid.
     * But what might be valid?  The type is left as SCAN, so the other fields
     * except center_freq1 must be valid for 20MHz channels. So we copy chan's center_freq
     * to have a valid thing and then proceed as we would otherwise, i.e.,
     * run the official set_chan to have a NORMAL switch type performed and recorded
     */
    if (!is_channel_certain(ccd))
      ccd->center_freq1 = ccd->chan.center_freq;

    ILOG0_V("Finishing a FW set-chan process");
    /* call the almost fullest set-chan machinery to do all the work for us; it will use switch_type NORMAL */
    res = core_cfg_set_chan(core, ccd, NULL);

    if (res != MTLK_ERR_OK)
      return res;

    /* double check it worked */
    MTLK_ASSERT(is_channel_certain(ccd) && __wave_core_chan_switch_type_get(core) == ST_NORMAL);
  }

  return res;
}

/*
 * The last switch_type can be SCAN either because a scan is running or because it finished but no
 * real channel was set because there was none before the scan.  If scan is running we can't just go messing
 * with the currently-set channel.  Even if it is not running, it may start in master core at just about any time.
 * So what we do is this:
 * If we already are in the master serializer then a new scan cannot cut in on us,
 * so we just call the above function to do a NORMAL set-chan in case current type is SCAN.
 * If we are not in the master serializer, then we need to arrange that the master serializer pauses-or-prevents
 * any scan that is going on there or might be starting any time now. After this the FW set-channel process
 * can be closed if necessary (i.e., if last switch_type stayed as SCAN) but still from the master core.
 * So here we just have to enqueue on master-core's serializer that function that will do these two things.
 * core parameter must reflect THE CORE ON WHO'S SERIALIZER WE'RE RUNNING!!!
 */
int finish_and_prevent_fw_set_chan(mtlk_core_t *core, BOOL yes)
{
  mtlk_core_t *mcore = mtlk_vap_manager_get_master_core(mtlk_vap_get_manager(core->vap_handle));
  mtlk_clpb_t *clpb = NULL;
  int res;

  ILOG3_SDPPDD("Running on %s (%d): core=%p, mcore=%p, yes=%hhu, chan_switch_type=%hhu",
               current->comm, current->pid, core, mcore, yes, __wave_core_chan_switch_type_get(mcore));

  if (core == mcore)
  {
    /* we're on master core, things are simple, scan cannot start out of nowhere */
    if (yes && (mtlk_core_is_in_scan_mode(mcore) || mtlk_core_is_chan_switch_type_csa_with_block_tx(core)))
      return finish_fw_set_chan_if_any(mcore);

    return MTLK_ERR_OK;
  }

  /* else we need to call a function in master core that would pause-or-prevent a scan and then set NORMAL switch type */
  res = _mtlk_df_user_invoke_core(mtlk_vap_get_df(mcore->vap_handle), WAVE_RADIO_REQ_FIN_PREV_FW_SC, &clpb, &yes, sizeof(yes));
  res = _mtlk_df_user_process_core_retval(res, clpb, WAVE_RADIO_REQ_FIN_PREV_FW_SC, TRUE);

  return res;
}

/* must be run on the master serializer */
/* see the comments of the previous function to learn why we have it */
int __MTLK_IFUNC finish_and_prevent_fw_set_chan_clpb (mtlk_handle_t hcore, const void *data, uint32 data_size)
{
  mtlk_core_t *core = HANDLE_T_PTR(mtlk_core_t, hcore);
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;
  BOOL *yes;
  uint32 yes_size;
  int res;

  MTLK_ASSERT(!in_atomic());
  MTLK_ASSERT(core == mtlk_core_get_master(core));

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  yes = mtlk_clpb_enum_get_next(clpb, &yes_size);
  MTLK_CLPB_TRY(yes, yes_size)
    if (*yes)
    {
      res = pause_or_prevent_scan(core);

      if (res != MTLK_ERR_OK)
        MTLK_CLPB_EXIT(res);

      res = finish_fw_set_chan_if_any(core);
    }
    else
      res = resume_or_allow_scan(core);
  MTLK_CLPB_FINALLY(res)
    return mtlk_clpb_push_res(clpb, res);
  MTLK_CLPB_END
}

int __MTLK_IFUNC core_cfg_internal_request_sid (mtlk_core_t *nic, IEEE_ADDR *addr, uint16 *p_sid)
{
  mtlk_vap_handle_t vap_handle = nic->vap_handle;
  int res;
  mtlk_txmm_msg_t man_msg;
  mtlk_txmm_data_t *man_entry;
  UMI_REQUEST_SID *umi_req_sid;

  MTLK_ASSERT(NULL != p_sid);
  MTLK_ASSERT(NULL != addr);

  /* prepare the msg to the FW */
  man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, mtlk_vap_get_txmm(vap_handle), &res);

  if (!man_entry) {
    ELOG_DD("CID-%04x: UM_MAN_REQUEST_SID_REQ init failed, err=%i",
            mtlk_vap_get_oid(vap_handle), res);
    return MTLK_ERR_NO_MEM;
  }

  man_entry->id = UM_MAN_REQUEST_SID_REQ;
  man_entry->payload_size = sizeof(UMI_REQUEST_SID);
  umi_req_sid = (UMI_REQUEST_SID *) man_entry->payload;
  memset(umi_req_sid, 0, sizeof(UMI_REQUEST_SID));
  umi_req_sid->sAddr = *addr;

  mtlk_dump(2, umi_req_sid, sizeof(UMI_REQUEST_SID), "dump of UMI_REQUEST_SID before submitting to FW:");

  res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);

  mtlk_dump(2, umi_req_sid, sizeof(UMI_REQUEST_SID), "dump of UMI_REQUEST_SID after submitting to FW:");

  if (res != MTLK_ERR_OK) {
      ELOG_DD("CID-%04x: UM_MAN_REQUEST_SID_REQ send failed, err=%i",
              mtlk_vap_get_oid(vap_handle), res);
  } else if (umi_req_sid->u8Status) {
    ELOG_DD("CID-%04x: UM_MAN_REQUEST_SID_REQ execution failed, status=%hhu",
            mtlk_vap_get_oid(vap_handle), umi_req_sid->u8Status);
    res = MTLK_ERR_MAC;
  }

  if (res == MTLK_ERR_OK) {
    *p_sid = MAC_TO_HOST16(umi_req_sid->u16SID);
    ILOG1_DY("SID:%i is requested for STA:%Y", *p_sid, addr->au8Addr);
  }

  if (NULL != man_entry) {
    mtlk_txmm_msg_cleanup(&man_msg);
  }

  return res;
}

static BOOL _core_cfg_check_sid_connected (mtlk_core_t *nic, IEEE_ADDR *mac_addr)
{
  mtlk_core_t *sta_core;
  sta_entry *sta = mtlk_vap_manager_find_sta(nic, &sta_core, mac_addr);

  if (NULL != sta) {
    ELOG_DYSD("CID-%04x: STA %Y already connected to %s (CID-%04x)!",
              mtlk_vap_get_oid(nic->vap_handle),
              mac_addr->au8Addr, mtlk_vap_is_ap(sta_core->vap_handle) ? "AP-VAP" : "STA-VAP",
              mtlk_vap_get_oid(sta_core->vap_handle));
    mtlk_sta_decref(sta); /* De-reference of find */
    return TRUE;
  }
  return FALSE;
}

int __MTLK_IFUNC core_cfg_request_sid (mtlk_handle_t hcore, const void *data, uint32 data_size)
{
  mtlk_core_t *nic = HANDLE_T_PTR(mtlk_core_t, hcore);
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;
  int res = MTLK_ERR_OK;
  IEEE_ADDR *addr;
  uint32 addr_size;
  uint16 sid;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  addr = mtlk_clpb_enum_get_next(clpb, &addr_size);
  MTLK_CLPB_TRY(addr, addr_size)
    if (mtlk_vap_is_ap(nic->vap_handle) && !wave_core_sync_hostapd_done_get(nic)) {
      ILOG1_D("CID-%04x: HOSTAPD STA database is not synced", mtlk_vap_get_oid(nic->vap_handle));
      MTLK_CLPB_EXIT(MTLK_ERR_NOT_READY);
    }

    /* Check that station is not connected */
    if (_core_cfg_check_sid_connected(nic, addr)) {
      MTLK_CLPB_EXIT(MTLK_ERR_ALREADY_EXISTS);
    }

    res = core_cfg_internal_request_sid(nic, addr, &sid);

  MTLK_CLPB_FINALLY(res)
    return mtlk_clpb_push_res_data(clpb, res, &sid, sizeof(sid));
  MTLK_CLPB_END
}

int __MTLK_IFUNC core_cfg_remove_sid (mtlk_handle_t hcore, const void *data, uint32 data_size)
{
  int res;

  uint16 *sid;
  uint32 sid_size;

  mtlk_core_t *nic = HANDLE_T_PTR(mtlk_core_t, hcore);
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  sid = mtlk_clpb_enum_get_next(clpb, &sid_size);
  MTLK_CLPB_TRY(sid, sid_size)
    if (!wave_core_sync_hostapd_done_get(nic)) {
      ILOG1_D("CID-%04x: HOSTAPD STA database is not synced", mtlk_vap_get_oid(nic->vap_handle));
      MTLK_CLPB_EXIT(MTLK_ERR_NOT_READY);
    }
    /* Don't send any message to halted MAC */
    if (NET_STATE_HALTED == mtlk_core_get_net_state(nic)) {
      MTLK_CLPB_EXIT(MTLK_ERR_OK);
    }
    res = core_remove_sid(nic, *sid);
  MTLK_CLPB_FINALLY(res)
    return mtlk_clpb_push_res(clpb, res);
  MTLK_CLPB_END
}

int core_cfg_ap_disconnect_all (mtlk_handle_t hcore, const void* data, uint32 data_size)
{
  uint32               res = MTLK_ERR_OK;
  mtlk_core_t          *nic = HANDLE_T_PTR(mtlk_core_t, hcore);
  mtlk_clpb_t          *clpb = *(mtlk_clpb_t **) data;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);
  MTLK_ASSERT(mtlk_vap_is_ap(nic->vap_handle));
  MTLK_UNREFERENCED_PARAM(data);
  MTLK_UNREFERENCED_PARAM(data_size);

  if (!wave_core_sync_hostapd_done_get(nic)) {
    ILOG1_D("CID-%04x: HOSTAPD STA database is not synced", mtlk_vap_get_oid(nic->vap_handle));
    res = MTLK_ERR_NOT_READY;
    goto FINISH;
  }

  res = core_ap_disconnect_all(nic);

FINISH:
  return mtlk_clpb_push(clpb, &res, sizeof(res));
}

uint8 core_cfg_get_is_ht_cur(mtlk_core_t *core)
{
  MTLK_ASSERT(NULL != core);

  return MTLK_CORE_PDB_GET_INT(core, PARAM_DB_CORE_IS_HT_CUR);
}

uint8 core_cfg_get_is_ht_cfg(mtlk_core_t *core)
{
  MTLK_ASSERT(NULL != core);

  return MTLK_CORE_PDB_GET_INT(core, PARAM_DB_CORE_IS_HT_CFG);
}

BOOL core_cfg_mbss_check_activation_params (struct nic *nic)
{
  BOOL res = FALSE;
  mtlk_pdb_size_t essid_len;
  char essid[MIB_ESSID_LENGTH];

  MTLK_ASSERT(NULL != nic);
  essid_len = sizeof(essid);

  if (MTLK_ERR_OK != MTLK_CORE_PDB_GET_BINARY(nic, PARAM_DB_CORE_ESSID, essid, &essid_len)) {
    ELOG_D("CID-%04x: ESSID parameter has wrong length", mtlk_vap_get_oid(nic->vap_handle));
    goto FINISH;
  }

  if (0 == essid_len) {
    ELOG_D("CID-%04x: ESSID is not set", mtlk_vap_get_oid(nic->vap_handle));
    goto FINISH;
  }

  res = TRUE;

FINISH:
  return res;
}

int core_cfg_set_mac_addr (mtlk_core_t *nic, const char *mac)
{
  int res = MTLK_ERR_UNKNOWN;

  /* Validate MAC address */
  if (!mtlk_osal_is_valid_ether_addr(mac)) {
    ELOG_DY("CID-%04x: The MAC %Y is invalid", mtlk_vap_get_oid(nic->vap_handle), mac);
    res = MTLK_ERR_PARAMS;
    goto FINISH;
  }

  /*
   * TODO mac80211: sta mode vaps doesn't support mibs for now, investigate later
   */
  if (mtlk_vap_is_master_ap(nic->vap_handle)) {
    MIB_VALUE uValue = {0};
    /* Try to send value to the MAC */
    mtlk_osal_copy_eth_addresses(uValue.au8ListOfu8.au8Elements, mac);

    res = mtlk_set_mib_value_raw(mtlk_vap_get_txmm(nic->vap_handle),
                                 MIB_IEEE_ADDRESS, &uValue);
    if (res != MTLK_ERR_OK) {
      ELOG_D("CID-%04x: Can't set MIB_IEEE_ADDRESS", mtlk_vap_get_oid(nic->vap_handle));
      goto FINISH;
    }
  }

  wave_pdb_set_mac(mtlk_vap_get_param_db(nic->vap_handle), PARAM_DB_CORE_MAC_ADDR, mac);
  ILOG1_DY("CID-%04x: New MAC: %Y", mtlk_vap_get_oid(nic->vap_handle), mac);

  res = MTLK_ERR_OK;

FINISH:
  return res;
}

/* Store of Country Code */
static int
_core_cfg_store_country_code (mtlk_core_t *core, const mtlk_country_code_t *country_code)
{
  int res;
  unsigned oid;
  wave_radio_t *radio;

  MTLK_ASSERT(NULL != core);
  MTLK_ASSERT(NULL != country_code);

  radio = wave_vap_radio_get(core->vap_handle);
  MTLK_ASSERT(NULL != radio);

  oid = mtlk_vap_get_oid(core->vap_handle);
  res = WAVE_RADIO_PDB_SET_BINARY(radio, PARAM_DB_RADIO_COUNTRY_CODE_ALPHA, country_code, sizeof(*country_code));
  if(MTLK_ERR_OK != res) {
    ELOG_D("CID-%04x: Can't update Country Code in PDB", oid);
  } else {
    ILOG0_DS("CID-%04x: Country Code set to: \"%s\"", oid, country_code->country);
  }

  return res;
}

/* Sending of regulatory domain information to firmware */
static int
_core_send_reg_domain_config (mtlk_core_t *core)
{
    mtlk_txmm_msg_t            man_msg;
    mtlk_txmm_data_t           *man_entry;
    UMI_REG_DOMAIN_CONFIG      *mac_msg;
    int                        res;
    unsigned                   oid;
    uint8 etsi;

    MTLK_ASSERT(core != NULL);
    oid = mtlk_vap_get_oid(core->vap_handle);

    /* Allocate a new message */
    man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, mtlk_vap_get_txmm(core->vap_handle), NULL);
    if (!man_entry) {
      ELOG_D("CID-%04x: Can not get TXMM slot", oid);
      return MTLK_ERR_NO_RESOURCES;
    }

    /* Fill the message data */
    man_entry->id = UM_MAN_SET_REG_DOMAIN_CONFIG_REQ;
    man_entry->payload_size = sizeof(*mac_msg);
    mac_msg = (UMI_REG_DOMAIN_CONFIG *)man_entry->payload;

    etsi = (REGD_CODE_ETSI == core_cfg_get_regd_code(core)) ? REG_DOMAIN_CONFIGURATION_1 : REG_DOMAIN_CONFIGURATION_0;

    mac_msg->regDomainConfig = etsi;

    ILOG2_S("Regulatory domain in %s region", etsi ? "ETSI" : "Non-ETSI");

    /* Send the message to FW */
    res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);

    /* Cleanup the message */
    mtlk_txmm_msg_cleanup(&man_msg);

    return res;
}


/* Processing of Country Code */
int __MTLK_IFUNC
core_cfg_country_code_set (mtlk_core_t *core, const mtlk_country_code_t *country_code)
{
  int res;
  res = _core_cfg_store_country_code(core, country_code);

  if(MTLK_ERR_OK == res)
    res = _core_send_reg_domain_config(core);

  return res;
}

int __MTLK_IFUNC
core_cfg_country_code_set_by_str (mtlk_core_t *core, char *str, int len)
{
  mtlk_country_code_t   country_code;

  MTLK_ASSERT(NULL != core);
  MTLK_ASSERT(NULL != str);

  /* Input string may be NOT NUL-terminated, e.g. alpha[2] */
  /* Prepare a NUL-terminated string */
  wave_strncopy(country_code.country, str, sizeof(country_code.country), len);

  return core_cfg_country_code_set(core, &country_code);
}

static int mtlk_core_cfg_set_radar_detect_from_hostapd (mtlk_core_t *core, uint8 radar_detection)
{
#if 0 /* TODO: Revert this back when false radar detection fixed in FW/PHY */
  int res = MTLK_ERR_OK;
  mtlk_clpb_t *clpb = NULL;
  mtlk_scan_and_calib_cfg_t scan_and_calib_cfg;

  memset(&scan_and_calib_cfg, 0, sizeof(scan_and_calib_cfg));
  MTLK_CFG_SET_ITEM(&scan_and_calib_cfg, radar_detect, radar_detection);

  res = _mtlk_df_user_invoke_core(mtlk_vap_get_df(core->vap_handle),
          WAVE_CORE_REQ_SET_SCAN_AND_CALIB_CFG, &clpb, &scan_and_calib_cfg, sizeof(scan_and_calib_cfg));
  res = _mtlk_df_user_process_core_retval(res, clpb,
          WAVE_CORE_REQ_SET_SCAN_AND_CALIB_CFG, TRUE);

  return res;
#endif
  return MTLK_ERR_OK;
}

/* Processing of required initial data (from hostapd) */
int __MTLK_IFUNC
core_cfg_set_hostapd_initial_data (mtlk_core_t *core, struct intel_vendor_initial_data_cfg* data)
{
  uint8 band, net_mode;
  int res;
  MTLK_ASSERT(NULL != core);

  /* Required for sending tcp_config to FW before channel switch */
  if (data->is_11b) {
    net_mode = MTLK_NETWORK_11B_ONLY;
  }
  else { /* OFDM modulation*/
    band = core_cfg_get_freq_band_cfg(core);
    net_mode = get_net_mode(band, TRUE);
  }

  mtlk_core_update_network_mode(core, net_mode);

  res = mtlk_core_cfg_set_radar_detect_from_hostapd(core, data->radar_detection);
  if (MTLK_ERR_OK != res)
    return res;

  return core_cfg_country_code_set_by_str(core, data->alpha2, sizeof(data->alpha2));
}

void __MTLK_IFUNC
core_cfg_country_code_get (mtlk_core_t *core, mtlk_country_code_t *country_code)
{
  wave_radio_t *radio;
  mtlk_pdb_size_t   size = sizeof(*country_code);

  MTLK_ASSERT(NULL != core);
  MTLK_ASSERT(NULL != country_code);

  radio = wave_vap_radio_get(core->vap_handle);
  MTLK_ASSERT(NULL != radio);

  WAVE_RADIO_PDB_GET_BINARY(radio, PARAM_DB_RADIO_COUNTRY_CODE_ALPHA, country_code, &size);
  country_code->country[sizeof(country_code->country) - 1] = '\0';
}

uint32 __MTLK_IFUNC
core_cfg_get_regd_code (mtlk_core_t *core)
{
  mtlk_country_code_t   country_code;

  core_cfg_country_code_get(core, &country_code);

  return mtlk_psdb_country_to_regd_code(country_code.country);
}


void __MTLK_IFUNC
core_cfg_country_code_set_default (mtlk_core_t *core)
{
  if (mtlk_vap_is_master(core->vap_handle)) {
    core_cfg_country_code_set_by_str(core, COUNTRY_CODE_DFLT, COUNTRY_CODE_MAX_LEN);
  }
}

void __MTLK_IFUNC
core_cfg_country_code_reset (mtlk_core_t *core)
{
  if (mtlk_vap_is_master(core->vap_handle)) {
    core_cfg_country_code_set_by_str(core, "", 0);
    ILOG1_D("CID-%04x: Country is reset", mtlk_vap_get_oid(core->vap_handle));
  }
}

static BOOL
_core_cfg_country_code_is_set (mtlk_core_t *core)
{
  mtlk_country_code_t   country_code;

  core_cfg_country_code_get(core, &country_code);

  return ('\0' != country_code.country[0]);
}

void __MTLK_IFUNC
core_cfg_sta_country_code_set_default_on_activate (mtlk_core_t *core)
{
  if(!_core_cfg_country_code_is_set(core)) {
    core_cfg_country_code_set_default(core);
  }
}

void __MTLK_IFUNC
core_cfg_sta_country_code_update_on_connect (mtlk_core_t *core, mtlk_country_code_t *country_code)
{
  core_cfg_country_code_set(mtlk_core_get_master(core), country_code);
  ILOG1_D("CID-%04x: Country has been adopted on connect", mtlk_vap_get_oid(core->vap_handle));
}

int __MTLK_IFUNC
core_cfg_set_country_from_ui (mtlk_core_t *core, mtlk_country_code_t *country_code)
{
  unsigned  oid;
  int       res;
  char     *country = country_code->country;

  MTLK_ASSERT(core);
  MTLK_ASSERT(country_code);

  MTLK_ASSERT(!mtlk_vap_is_slave_ap(core->vap_handle));

  oid = mtlk_vap_get_oid(core->vap_handle);

  if (mtlk_core_get_net_state(core) != NET_STATE_READY) {
    return MTLK_ERR_BUSY;
  }

  if (mtlk_vap_is_ap(core->vap_handle)) {
    ILOG1_D("CID-%04x: Can't change Country. It's read-only parameter.", oid);
    return MTLK_ERR_NOT_SUPPORTED;
  }

  if (!mtlk_vap_is_ap(core->vap_handle) && core_cfg_get_dot11d(core)) {
    ILOG1_D("CID-%04x: Can't change Country until 802.11d extension is enabled.", oid);
    return MTLK_ERR_NOT_SUPPORTED;
  }

  if (!mtlk_psdb_country_is_supported(country)) {
    ELOG_DS("CID-%04x: Country is not supported: \"%s\"", oid, country);
    return MTLK_ERR_VALUE;
  }

  res = core_cfg_country_code_set(core, country_code);
  /* Result already reported */
  return res;
}

BOOL core_cfg_get_dot11d(mtlk_core_t *core)
{
  return WAVE_RADIO_PDB_GET_INT(wave_vap_radio_get(core->vap_handle), PARAM_DB_RADIO_DOT11D_ENABLED);
}

int core_cfg_set_is_dot11d(mtlk_core_t *core, BOOL is_dot11d)
{
  wave_radio_t *radio = wave_vap_radio_get(core->vap_handle);

  if (NET_STATE_READY != mtlk_core_get_net_state(core)) {
    return MTLK_ERR_NOT_READY;
  }

  /* set country code */
  if (!mtlk_vap_is_ap(core->vap_handle)) {
    /* Switched on */
    if (is_dot11d && !core_cfg_get_dot11d(core)) {
      core_cfg_country_code_reset(core);
    }
    /* Switched off */
    else if(!is_dot11d && core_cfg_get_dot11d(core)) {
      core_cfg_country_code_set_default(core);
    }
  }

  WAVE_RADIO_PDB_SET_INT(radio, PARAM_DB_RADIO_DOT11D_ENABLED, !!is_dot11d);

  return MTLK_ERR_OK;
}

uint8 core_cfg_get_freq_band_cur(mtlk_core_t *core)
{
  MTLK_ASSERT(NULL != core);

  return WAVE_RADIO_PDB_GET_INT(wave_vap_radio_get(core->vap_handle), PARAM_DB_RADIO_FREQ_BAND_CUR);
}

uint8 core_cfg_get_freq_band_cfg(mtlk_core_t *core)
{
  wave_radio_t *radio;
  MTLK_ASSERT(NULL != core);
  radio = wave_vap_radio_get(core->vap_handle);

  return WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_FREQ_BAND_CFG);
}

uint8 core_cfg_get_network_mode_cur(mtlk_core_t *core)
{
  MTLK_ASSERT(NULL != core);

  return MTLK_CORE_PDB_GET_INT(core, PARAM_DB_CORE_NET_MODE_CUR);
}

uint8 core_cfg_get_network_mode_cfg(mtlk_core_t *core)
{
  MTLK_ASSERT(NULL != core);

  return MTLK_CORE_PDB_GET_INT(core, PARAM_DB_CORE_NET_MODE_CFG);
}

BOOL core_cfg_net_state_is_connected(uint16 net_state)
{
  return ((NET_STATE_CONNECTED == net_state) ? TRUE:FALSE);
}

BOOL core_cfg_is_connected (mtlk_core_t *core)
{
  return core_cfg_net_state_is_connected(mtlk_core_get_net_state(core));
}

BOOL core_cfg_is_halted (mtlk_core_t *core)
{
  return (mtlk_core_get_net_state(core) & NET_STATE_HALTED);
}

uint8 core_cfg_get_network_mode(mtlk_core_t *core)
{
  MTLK_ASSERT(NULL != core);
  if (core->net_state == NET_STATE_CONNECTED)
    return core_cfg_get_network_mode_cur(core);
  else
    return core_cfg_get_network_mode_cfg(core);
}

/* In case network mode is 11b, return PSDB_PHY_CW_11B. Otherwise convert enum chanWidth to psdb_phy_cw_t. */
static psdb_phy_cw_t _get_phy_cw_mode(mtlk_core_t *core, enum chanWidth cw)
{
    /* Try to detect if network mode is 11B */
    if (MTLK_NETWORK_11B_ONLY == core_cfg_get_network_mode(core))
        return PSDB_PHY_CW_11B;

    switch (cw) {
        case CW_20:
            return PSDB_PHY_CW_OFDM_20;
        case CW_40:
            return PSDB_PHY_CW_OFDM_40;
        case CW_80:
        case CW_80_80:
            return PSDB_PHY_CW_OFDM_80;
        case CW_160:
            return PSDB_PHY_CW_OFDM_160;
        default:
            ELOG_D("chan_width [%d] currently isn't supported.", cw);
            return PSDB_PHY_CW_OFDM_80;
    }
}

static enum chanWidth
_wave_chan_width_validate(enum chanWidth cw)
{
  if (wave_chan_width_is_supported(cw)) {
    return cw;
  } else {
    ELOG_D("Unsupported channel width %d", cw);
    MTLK_ASSERT(FALSE);
    return CW_DEFAULT;
  }
}

void core_cfg_set_tx_power_limit (mtlk_core_t *core, unsigned center_freq, enum chanWidth cfg_width, unsigned cf_primary, mtlk_country_code_t req_country_code)
{
  mtlk_df_t *df               = mtlk_vap_get_df(core->vap_handle); /* this checks vap_handle and df, cannot return NULL */
  mtlk_df_user_t *df_user     = mtlk_df_get_user(df);
  struct wireless_dev *wdev   = mtlk_df_user_get_wdev(df_user);   /* this checks df_user */
  wave_radio_t *radio         = wave_vap_radio_get(core->vap_handle);

  uint16                reg_pw_limits[WAVE_BANDWIDTHS_NUM] = { 0 }; /* array per BW */
  uint16                reg_pw_lim;
  psdb_pw_limits_t      tmp_pwl, psd_pwl, cfg_pwl;
  mtlk_country_code_t   country_code;
  mtlk_pdb_size_t       pw_limits_size = sizeof(psdb_pw_limits_t);
  psdb_phy_cw_t         phy_cw;
  wave_chan_bonding_t   chan_bonding;
  int                   sub_bw; /* subband's width */
  int                   res;

  /* Check Country code */
  core_cfg_country_code_get(core, &country_code); /* read out country code */
  if ((req_country_code.country[0] != country_code.country[0]) ||
      (req_country_code.country[1] != country_code.country[1]) ) {
       ILOG1_SS("req_country_code(%s) is different from current country_code(%s)",
                req_country_code.country, country_code.country);
       return;
  }

  cfg_width = _wave_chan_width_validate(cfg_width);
  phy_cw = _get_phy_cw_mode(core, cfg_width);
  ILOG2_DDDD("cw %d, center_freq %u, cf_primary %u -> phy_cw mode %d",
             cfg_width, center_freq, cf_primary, (int)phy_cw);

  MTLK_ASSERT(phy_cw < ARRAY_SIZE(cfg_pwl.pw_limits));
  memset(&cfg_pwl, 0, sizeof(cfg_pwl));

  MTLK_STATIC_ASSERT((PSDB_PHY_CW_OFDM_20 + 1) == PSDB_PHY_CW_OFDM_40);
  MTLK_STATIC_ASSERT((PSDB_PHY_CW_OFDM_40 + 1) == PSDB_PHY_CW_OFDM_80);
  MTLK_STATIC_ASSERT((PSDB_PHY_CW_OFDM_80 + 1) == PSDB_PHY_CW_OFDM_160);

  MTLK_STATIC_ASSERT(WAVE_BANDWIDTHS_NUM > CW_160);
  MTLK_ASSERT(WAVE_BANDWIDTHS_NUM > cfg_width);
  wave_scan_support_fill_chan_bonding_by_freq(&chan_bonding, center_freq, cf_primary);

  /* Fill data for all subband widths in range [CW_20 ... cfg_width] */
  for (sub_bw = CW_20; sub_bw <= cfg_width; sub_bw++) {
    int ofdm_idx;

    /* read out regulatory power limit for current subband */
    reg_pw_lim = wv_cfg80211_regulatory_limit_get(wdev,
                    channel_to_frequency(chan_bonding.lower_chan[sub_bw]),
                    channel_to_frequency(chan_bonding.upper_chan[sub_bw]));

    reg_pw_limits[sub_bw] = reg_pw_lim;

    /* get power limits from PSDB for current subband */
    core_get_psdb_hw_limits(core, country_code.country,
                            channel_to_frequency(chan_bonding.center_chan[sub_bw]), &tmp_pwl);

    /* apply regulatory limit to all power limits of current subband */
    /* store tmp_pwl in PSD, apply limit and store result in CFG */
#define _PW_LIMIT_APPLY_(tmp, psd, cfg, idx, reg_lim) \
            psd.pw_limits[idx] = tmp.pw_limits[idx];      \
            cfg.pw_limits[idx] = hw_mmb_get_tx_power_target(tmp.pw_limits[idx], reg_lim)

    /* use CW_20 limit for 11b */
    if (CW_20 == sub_bw) {
      _PW_LIMIT_APPLY_(tmp_pwl, psd_pwl, cfg_pwl, (PSDB_PHY_CW_11B), reg_pw_lim);
      ILOG1_DD("Updated PSD/CFG pw_limits 11B: %3u %3u",
               psd_pwl.pw_limits[PSDB_PHY_CW_11B], cfg_pwl.pw_limits[PSDB_PHY_CW_11B]);
    }

    _PW_LIMIT_APPLY_(tmp_pwl, psd_pwl, cfg_pwl, (PSDB_PHY_CW_BF_20   + sub_bw), reg_pw_lim);
    _PW_LIMIT_APPLY_(tmp_pwl, psd_pwl, cfg_pwl, (PSDB_PHY_CW_MU_20   + sub_bw), reg_pw_lim);

    ofdm_idx = PSDB_PHY_CW_OFDM_20 + sub_bw;
    _PW_LIMIT_APPLY_(tmp_pwl, psd_pwl, cfg_pwl, ofdm_idx, reg_pw_lim);

#undef _PW_LIMIT_APPLY_

    ILOG1_DDD("Updated PSD/CFG pw_limits bw%u: %3u %3u",
              sub_bw, psd_pwl.pw_limits[ofdm_idx], cfg_pwl.pw_limits[ofdm_idx]);

    /* we can't send zero value as power limits to FW due to crash */
    /* it is occurred if both REG and PSD power limits are not set */
    if (!cfg_pwl.pw_limits[ofdm_idx]) {
      ELOG_DD("PSD/REG power limits are not set! center_freq %u bandwidth %u", center_freq, sub_bw);
      MTLK_ASSERT(FALSE);
      return;
    }
  }

  WAVE_RADIO_PDB_SET_INT(radio, PARAM_DB_RADIO_TX_POWER_REG_LIM, reg_pw_lim);
  WAVE_RADIO_PDB_SET_INT(radio, PARAM_DB_RADIO_TX_POWER_PSD, psd_pwl.pw_limits[phy_cw]); /* power units */

  /* Store values of tx power PSD limits of 11b and non-11b (OFDM) phy modes.
   * They are not depended of current value of Tx Power PSD in parameters DB. */
  res = WAVE_RADIO_PDB_SET_BINARY(radio, PARAM_DB_RADIO_TPC_PW_LIMITS_PSD, &psd_pwl, pw_limits_size); /* power units */
  if (res != MTLK_ERR_OK)
  {
    ELOG_D("Can't update PSD power limits in PDB, res: %d", res);
    /* MTLK_ASSERT(FALSE); */
  }

  /* call TPC wrapper */
  res = mtlk_reload_tpc_wrapper(core, ieee80211_frequency_to_channel(center_freq), &cfg_pwl);
  if (MTLK_ERR_OK != res) {
    ELOG_D("Failed to reload TPC wrapper, res: %d", res);
  } else {
      /* Store value of current tx power limit (it depends from network mode and channel width)
       * as a current value of Tx Power in parameters DB */
      WAVE_RADIO_PDB_SET_INT(radio, PARAM_DB_RADIO_TX_POWER_CFG, cfg_pwl.pw_limits[phy_cw]); /* power units */
  }
}

static int
_mtlk_core_store_four_addr_mode (mtlk_core_t *core, const uint32 four_addr_mode)
{
    uint32 four_addr_mode_old;
    s32 flush;

    MTLK_ASSERT(core != NULL);
    if (four_addr_mode >= MTLK_CORE_4ADDR_LAST)
      return MTLK_ERR_VALUE;

    four_addr_mode_old = MTLK_CORE_PDB_GET_INT(core, PARAM_DB_CORE_4ADDR_MODE);

    if (four_addr_mode != four_addr_mode_old) {
      MTLK_CORE_PDB_SET_INT(core, PARAM_DB_CORE_4ADDR_MODE, four_addr_mode);
      flush = mtlk_vap_get_id(core->vap_handle);
      wv_cfg80211_handle_flush_stations(core->vap_handle, &flush, sizeof(flush));
    }
    return MTLK_ERR_OK;
}

int __MTLK_IFUNC
mtlk_core_add_four_addr_entry (mtlk_core_t* nic, const IEEE_ADDR *addr)
{
  ieee_addr_entry_t *ieee_addr_entry = NULL;
  MTLK_HASH_ENTRY_T(ieee_addr) *h;

  ILOG2_DY("CID-%04x: four address list entry add %Y",
    mtlk_vap_get_oid(nic->vap_handle), addr->au8Addr);

  if (mtlk_vap_is_ap(nic->vap_handle) &&
      wds_sta_is_peer_ap(&nic->slow_ctx->wds_mng, addr))
    return MTLK_ERR_PROHIB;

  ieee_addr_entry = mtlk_osal_mem_alloc(sizeof(*ieee_addr_entry),
    LQLA_MEM_TAG_FOUR_ADDR_LIST);
  if (!ieee_addr_entry) {
    WLOG_V("Can't alloc four address station list entry");
    return MTLK_ERR_NO_MEM;
  }
  memset(ieee_addr_entry, 0, sizeof(*ieee_addr_entry));

  mtlk_osal_lock_acquire(&nic->four_addr_sta_list.ieee_addr_lock);
  h = mtlk_hash_insert_ieee_addr(&nic->four_addr_sta_list.hash, addr,
    &ieee_addr_entry->hentry);
  mtlk_osal_lock_release(&nic->four_addr_sta_list.ieee_addr_lock);
  if (h) {
    mtlk_osal_mem_free(ieee_addr_entry);
    WLOG_DY("CID-%04x: %Y already in four address station list",
      mtlk_vap_get_oid(nic->vap_handle), addr->au8Addr);
    return MTLK_ERR_OK;
  }
  ILOG2_DY("CID-%04x: four address station list entry %Y added",
    mtlk_vap_get_oid(nic->vap_handle), addr->au8Addr);

  return MTLK_ERR_OK;
}

int __MTLK_IFUNC
mtlk_core_del_four_addr_entry (mtlk_core_t *nic, const IEEE_ADDR *addr)
{
  MTLK_HASH_ENTRY_T(ieee_addr) *h;

  ILOG2_DY("CID-%04x: four address list entry delete %Y",
    mtlk_vap_get_oid(nic->vap_handle), addr->au8Addr);
  mtlk_osal_lock_acquire(&nic->four_addr_sta_list.ieee_addr_lock);

  /* find four address station list entry in four address station list */
  h = mtlk_hash_find_ieee_addr(&nic->four_addr_sta_list.hash, addr);
  if (h) {
    ieee_addr_entry_t *ieee_addr_entry = MTLK_CONTAINER_OF(h,
      ieee_addr_entry_t, hentry);
    mtlk_hash_remove_ieee_addr(&nic->four_addr_sta_list.hash,
      &ieee_addr_entry->hentry);
    mtlk_osal_mem_free(ieee_addr_entry);
  } else {
    WLOG_DY("CID-%04x: Can't remove four address station list entry %Y. "
      "It doesn't exist", mtlk_vap_get_oid(nic->vap_handle), addr->au8Addr);
  }
  mtlk_osal_lock_release(&nic->four_addr_sta_list.ieee_addr_lock);

  return MTLK_ERR_OK;
}

void core_cfg_flush_four_addr_list (mtlk_core_t *nic)
{
  mtlk_hash_enum_t              e;
  MTLK_HASH_ENTRY_T(ieee_addr) *h;

  mtlk_osal_lock_acquire(&nic->four_addr_sta_list.ieee_addr_lock);
  h = mtlk_hash_enum_first_ieee_addr(&nic->four_addr_sta_list.hash, &e);
  while (h) {
    ieee_addr_entry_t *entry = MTLK_CONTAINER_OF(h, ieee_addr_entry_t,
      hentry);

    mtlk_hash_remove_ieee_addr(&nic->four_addr_sta_list.hash,
      &entry->hentry);
    mtlk_osal_mem_free(entry);

    h = mtlk_hash_enum_next_ieee_addr(&nic->four_addr_sta_list.hash, &e);
  }
  mtlk_osal_lock_release(&nic->four_addr_sta_list.ieee_addr_lock);
}

BOOL core_cfg_four_addr_entry_exists (mtlk_core_t *nic,
  const IEEE_ADDR *addr)
{
  MTLK_HASH_ENTRY_T(ieee_addr) *h;

  ILOG3_DY("CID-%04x: looking for four address station list entry %Y",
    mtlk_vap_get_oid(nic->vap_handle), addr->au8Addr);

  mtlk_osal_lock_acquire(&nic->four_addr_sta_list.ieee_addr_lock);
  h = mtlk_hash_find_ieee_addr(&nic->four_addr_sta_list.hash, addr);
  mtlk_osal_lock_release(&nic->four_addr_sta_list.ieee_addr_lock);

  if (!h) {
    ILOG3_DY("CID-%04x: four address station list entry NOT found %Y",
             mtlk_vap_get_oid(nic->vap_handle), addr->au8Addr);
  }

  return (h != NULL);
}

static int
_mtlk_core_get_four_addr_sta_vect (mtlk_core_t *nic, mtlk_clpb_t **ppclpb_sta_vect)
{
  int res = MTLK_ERR_OK;
  mtlk_hash_enum_t              e;
  MTLK_HASH_ENTRY_T(ieee_addr) *h;

  *ppclpb_sta_vect = mtlk_clpb_create();
  if (NULL == *ppclpb_sta_vect) {
    ELOG_V("Cannot allocate clipboard object");
    res = MTLK_ERR_NO_MEM;
    goto end;
  }

  mtlk_osal_lock_acquire(&nic->four_addr_sta_list.ieee_addr_lock);
  h = mtlk_hash_enum_first_ieee_addr(&nic->four_addr_sta_list.hash, &e);
  while (h) {
    ILOG3_DY("CID-%04x: four address station list entry %Y",
      mtlk_vap_get_oid(nic->vap_handle), h->key.au8Addr);
    res = mtlk_clpb_push(*ppclpb_sta_vect, &h->key, sizeof(IEEE_ADDR));
    if (MTLK_ERR_OK != res) {
      ELOG_V("Cannot push data to the clipboard");
      goto err_push_data;
    }

    h = mtlk_hash_enum_next_ieee_addr(&nic->four_addr_sta_list.hash, &e);
  }
  goto no_err;

err_push_data:
  mtlk_clpb_delete(*ppclpb_sta_vect);
  *ppclpb_sta_vect = NULL;

no_err:
  mtlk_osal_lock_release(&nic->four_addr_sta_list.ieee_addr_lock);

end:
  return res;
}

int __MTLK_IFUNC core_cfg_set_four_addr_cfg (mtlk_handle_t hcore, const void *data, uint32 data_size)
{
    int res = MTLK_ERR_OK;
    mtlk_core_t *core = (mtlk_core_t *) hcore;
    mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;
    mtlk_four_addr_cfg_t *four_addr_cfg = NULL;
    uint32 four_addr_cfg_size;

    MTLK_ASSERT(core != NULL);
    MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

    /* get configuration */
    four_addr_cfg = mtlk_clpb_enum_get_next(clpb, &four_addr_cfg_size);
    MTLK_CLPB_TRY(four_addr_cfg, four_addr_cfg_size)
      MTLK_CFG_START_CHEK_ITEM_AND_CALL()
        MTLK_CFG_CHECK_ITEM_AND_CALL(four_addr_cfg, mode, _mtlk_core_store_four_addr_mode,
                            (core, four_addr_cfg->mode), res);
        MTLK_CFG_CHECK_ITEM_AND_CALL(four_addr_cfg, addr_add, mtlk_core_add_four_addr_entry,
                            (core, &four_addr_cfg->addr_add), res);
        MTLK_CFG_CHECK_ITEM_AND_CALL(four_addr_cfg, addr_del, mtlk_core_del_four_addr_entry,
                            (core, &four_addr_cfg->addr_del), res);
      MTLK_CFG_END_CHEK_ITEM_AND_CALL()
    MTLK_CLPB_FINALLY(res)
      /* push result into clipboard */
      return mtlk_clpb_push_res(clpb, res);
    MTLK_CLPB_END
}

int __MTLK_IFUNC
core_cfg_get_four_addr_cfg (mtlk_handle_t hcore, const void *data, uint32 data_size)
{
    int res = MTLK_ERR_OK;
    mtlk_four_addr_cfg_t four_addr_cfg;
    mtlk_core_t *core = (mtlk_core_t*)hcore;
    mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;

    MTLK_ASSERT(core != NULL);
    MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

    memset(&four_addr_cfg, 0, sizeof(four_addr_cfg));

    MTLK_CFG_SET_ITEM(&four_addr_cfg, mode,
      MTLK_CORE_PDB_GET_INT(core, PARAM_DB_CORE_4ADDR_MODE));

    /* push result and config to clipboard*/
    res = mtlk_clpb_push(clpb, &res, sizeof(res));
    if (MTLK_ERR_OK == res) {
        res = mtlk_clpb_push(clpb, &four_addr_cfg, sizeof(four_addr_cfg));
    }

    return res;
}

/* Start of Static Planner processing: set config */

static int
_mtlk_core_send_static_plan_config (mtlk_core_t *core, const UMI_STATIC_PLAN_CONFIG *req)
{
    mtlk_txmm_msg_t         man_msg;
    mtlk_txmm_data_t       *man_entry;
    UMI_STATIC_PLAN_CONFIG *mac_msg;
    int                     res;
    unsigned                oid;

    MTLK_ASSERT(core != NULL);
    oid = mtlk_vap_get_oid(core->vap_handle);

    ILOG0_D("CID-%04x: StaticPlanConfig", oid);

    /* Allocate a new message */
    man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, mtlk_vap_get_txmm(core->vap_handle), NULL);
    if (!man_entry) {
      ELOG_D("CID-%04x: Can not get TXMM slot", oid);
      return MTLK_ERR_NO_RESOURCES;
    }

    /* Fill the message data */
    man_entry->id = UM_MAN_STATIC_PLAN_CONFIG_REQ;
    man_entry->payload_size = sizeof(*mac_msg);
    mac_msg = (UMI_STATIC_PLAN_CONFIG *)man_entry->payload;
    wave_memcpy(mac_msg, sizeof(*mac_msg), req, sizeof(*mac_msg));

    /* Send the message to FW */
    mtlk_dump(1, mac_msg, sizeof(*mac_msg), "dump of StaticPlanConfig");
    res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);

    /* Cleanup the message */
    mtlk_txmm_msg_cleanup(&man_msg);

    return res;
}

int __MTLK_IFUNC
mtlk_core_cfg_set_static_plan (mtlk_handle_t hcore, const void *data, uint32 data_size)
{
    int res = MTLK_ERR_OK;
    mtlk_core_t *core = (mtlk_core_t *) hcore;
    mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;
    mtlk_static_plan_cfg_t *static_plan_cfg = NULL;
    uint32 cfg_size;

    MTLK_ASSERT(core != NULL);
    MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

    /* get configuration */
    static_plan_cfg = mtlk_clpb_enum_get_next(clpb, &cfg_size);
    MTLK_CLPB_TRY(static_plan_cfg, cfg_size)
      MTLK_CFG_START_CHEK_ITEM_AND_CALL()
         MTLK_CFG_CHECK_ITEM_AND_CALL(static_plan_cfg, config, _mtlk_core_send_static_plan_config,
                                      (core, &static_plan_cfg->config), res);
      MTLK_CFG_END_CHEK_ITEM_AND_CALL()
    MTLK_CLPB_FINALLY(res)
      return mtlk_clpb_push_res(clpb, res); /* push result into clipboard */
    MTLK_CLPB_END
}

/* End of Static Planner processing */

int __MTLK_IFUNC
core_cfg_get_four_addr_sta_list (mtlk_handle_t hcore, const void* data, uint32 data_size)
{
  uint32 res = MTLK_ERR_OK;
  mtlk_four_addr_cfg_t four_addr_cfg;
  mtlk_core_t *core = (mtlk_core_t*)hcore;
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  memset(&four_addr_cfg, 0, sizeof(four_addr_cfg));

  MTLK_CFG_SET_ITEM_BY_FUNC(&four_addr_cfg, sta_vect,
    _mtlk_core_get_four_addr_sta_vect, (core, &four_addr_cfg.sta_vect), res);

  res = mtlk_clpb_push(clpb, &res, sizeof(res));
  if (MTLK_ERR_OK == res) {
    res = mtlk_clpb_push(clpb, &four_addr_cfg, sizeof(four_addr_cfg));
  }

  return res;
}

/*------ CCA threshold ----------------------------------------------*/

int mtlk_core_cfg_send_cca_threshold_req(mtlk_core_t *core, iwpriv_cca_th_t *cca_th_params)
{
  int res;
  mtlk_txmm_msg_t           man_msg;
  mtlk_txmm_data_t          *man_entry = NULL;
  UMI_CCA_TH_t              *req = NULL;
  mtlk_vap_handle_t         vap_handle = core->vap_handle;

  MTLK_ASSERT(vap_handle);
  ILOG1_D("CID-%04x: CCA_TH_REQ", mtlk_vap_get_oid(vap_handle));

  man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, mtlk_vap_get_txmm(vap_handle), NULL);
  if (NULL == man_entry) {
    ELOG_D("CID-%04x: No man entry available", mtlk_vap_get_oid(vap_handle));
    return MTLK_ERR_NO_RESOURCES;
  }

  man_entry->id = UM_MAN_CCA_TH_REQ;
  man_entry->payload_size = sizeof(UMI_CCA_TH_t);

  req = (UMI_CCA_TH_t *)(man_entry->payload);
  req->primaryChCCA         = cca_th_params->values[0];
  req->secondaryChCCA       = cca_th_params->values[1];
  req->midPktPrimaryCCA     = cca_th_params->values[2];
  req->midPktSecondary20CCA = cca_th_params->values[3];
  req->midPktSecondary40CCA = cca_th_params->values[4];

  res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);
  if (MTLK_ERR_OK != res) {
    ELOG_DD("CID-%04x: Set CCA_TH_REQ failure (%i)", mtlk_vap_get_oid(vap_handle), res);
  }

  mtlk_txmm_msg_cleanup(&man_msg);

  return res;
}

/* Read CCA Threshold data from PDB */
int mtlk_core_cfg_read_cca_threshold(mtlk_core_t *core, iwpriv_cca_th_t *cca_th_params)
{
  wave_radio_t *radio = wave_vap_radio_get(core->vap_handle);

  if (MTLK_ERR_OK != wave_radio_cca_threshold_get(radio, cca_th_params)) {
    ELOG_D("CID-%04x: Can't read CCA_TH from PDB", mtlk_vap_get_oid(core->vap_handle));
    return MTLK_ERR_PARAMS;
  }

  if (!cca_th_params->is_updated) {
    ILOG1_D("CID-%04x: CCA Threshold is not set", mtlk_vap_get_oid(core->vap_handle));
  }

  return MTLK_ERR_OK;
}

/* Store CCA Threshold in PDB */

static int
_mtlk_core_cfg_store_and_send_cca_threshold(mtlk_core_t *core, iwpriv_cca_th_t *cca_th_params)
{
  wave_radio_t *radio = wave_vap_radio_get(core->vap_handle);
  int i, res;

  cca_th_params->is_updated = 1;
  res = wave_radio_cca_threshold_set(radio, cca_th_params);
  if (MTLK_ERR_OK == res) {
    res = mtlk_core_cfg_send_cca_threshold_req(core, cca_th_params);

    for (i = 0; i < MTLK_CCA_TH_PARAMS_LEN; i++)
      core->slow_ctx->cca_adapt.cca_th_params[i] = cca_th_params->values[i];
  }

  return res;
}

int __MTLK_IFUNC
mtlk_core_cfg_set_auth_cfg (mtlk_handle_t hcore, const void* data, uint32 data_size)
{
  int                       res = MTLK_ERR_OK;
  mtlk_core_t               *nic = HANDLE_T_PTR(mtlk_core_t, hcore);
  mtlk_clpb_t               *clpb = *(mtlk_clpb_t **) data;
  mtlk_core_ui_auth_cfg_t   *auth_cfg;
  uint32                    size;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  if ((mtlk_core_get_net_state(nic) & (NET_STATE_READY | NET_STATE_ACTIVATING | NET_STATE_CONNECTED)) == 0) {
    ILOG1_D("CID-%04x: Invalid card state - request rejected", mtlk_vap_get_oid(nic->vap_handle));
    res = MTLK_ERR_NOT_READY;
    goto FINISH;
  }

  if (mtlk_core_is_stopping(nic)) {
    ILOG1_D("CID-%04x: Can't set AUTH configuration - core is stopping", mtlk_vap_get_oid(nic->vap_handle));
    res = MTLK_ERR_NOT_READY;
    goto FINISH;
  }

  auth_cfg = mtlk_clpb_enum_get_next(clpb, &size);
  if ( (NULL == auth_cfg) || (sizeof(*auth_cfg) != size) ) {
    ELOG_D("CID-%04x: Failed to get AUTH configuration parameters from CLPB", mtlk_vap_get_oid(nic->vap_handle));
    res = MTLK_ERR_UNKNOWN;
    goto FINISH;
  }

  nic->slow_ctx->rsn_enabled = auth_cfg->rsn_enabled;
  ILOG1_DD("CID-%04x: RSN switched to %i", mtlk_vap_get_oid(nic->vap_handle), auth_cfg->rsn_enabled);
  nic->slow_ctx->wep_enabled = auth_cfg->wep_enabled;
  ILOG1_DD("CID-%04x: WEP switched to %i", mtlk_vap_get_oid(nic->vap_handle), auth_cfg->wep_enabled);

  ILOG1_DD("CID-%04x:Authentication is switched to %i", mtlk_vap_get_oid(nic->vap_handle), auth_cfg->authentication);

FINISH:
  return res;
}

int __MTLK_IFUNC
mtlk_core_cfg_get_auth_cfg (mtlk_handle_t hcore, const void* data, uint32 data_size)
{
  int                       res = MTLK_ERR_OK;
  mtlk_core_t               *nic = HANDLE_T_PTR(mtlk_core_t, hcore);
  mtlk_clpb_t               *clpb = *(mtlk_clpb_t **) data;
  mtlk_core_ui_auth_state_t auth_state;
  sta_entry                 *sta;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  if ((mtlk_core_get_net_state(nic) & (NET_STATE_READY | NET_STATE_ACTIVATING | NET_STATE_CONNECTED)) == 0) {
    ILOG1_D("CID-%04x: Invalid card state - request rejected", mtlk_vap_get_oid(nic->vap_handle));
    res = MTLK_ERR_NOT_READY;
    goto FINISH;
  }

  if (mtlk_core_is_stopping(nic)) {
    ILOG1_D("CID-%04x: Can't set AUTH configuration - core is stopping", mtlk_vap_get_oid(nic->vap_handle));
    res = MTLK_ERR_NOT_READY;
    goto FINISH;
  }

  if (mtlk_core_scan_is_running(nic)) {
    ILOG1_D("CID-%04x: Can't set AUTH configuration - scan in progress", mtlk_vap_get_oid(nic->vap_handle));
    res = MTLK_ERR_NOT_READY;
    goto FINISH;
  }

  memset(&auth_state, 0, sizeof(auth_state));

  auth_state.group_cipher = nic->slow_ctx->group_cipher;
  auth_state.cipher_pairwise = -1;

  if (!mtlk_vap_is_ap(nic->vap_handle)) {
    sta = mtlk_stadb_get_ap(&nic->slow_ctx->stadb);
    if (!sta) {
      res = MTLK_ERR_PARAMS;
      goto FINISH;
    }

    auth_state.cipher_pairwise = mtlk_sta_get_cipher(sta);
    mtlk_sta_decref(sta); /* De-reference of get_ap */
  }

  res = mtlk_clpb_push(clpb, &auth_state, sizeof(auth_state));
  if (MTLK_ERR_OK != res) {
    mtlk_clpb_purge(clpb);
  }

FINISH:
  return res;
}

int __MTLK_IFUNC
mtlk_core_cfg_set_cca_threshold (mtlk_handle_t hcore, const void* data, uint32 data_size)
{
  int                    res = MTLK_ERR_OK;
  uint32                 cfg_size;
  mtlk_core_t            *core = (mtlk_core_t*)hcore;
  mtlk_cca_th_cfg_t      *cca_th_cfg = NULL;
  mtlk_clpb_t            *clpb = *(mtlk_clpb_t **)data;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);
  MTLK_ASSERT(core);

  /* get configuration */
  cca_th_cfg = mtlk_clpb_enum_get_next(clpb, &cfg_size);
  MTLK_CLPB_TRY(cca_th_cfg, cfg_size)
    MTLK_CFG_START_CHEK_ITEM_AND_CALL()
      MTLK_CFG_CHECK_ITEM_AND_CALL(cca_th_cfg, cca_th_params,
          _mtlk_core_cfg_store_and_send_cca_threshold, (core, &cca_th_cfg->cca_th_params), res);
    MTLK_CFG_END_CHEK_ITEM_AND_CALL()
  MTLK_CLPB_FINALLY(res)
    /* push result into clipboard */
    return mtlk_clpb_push_res(clpb, res);
  MTLK_CLPB_END
}

int __MTLK_IFUNC
mtlk_core_cfg_get_cca_threshold (mtlk_handle_t hcore, const void* data, uint32 data_size)
{
  int                      res = MTLK_ERR_OK;
  mtlk_cca_th_cfg_t        cca_th_cfg;
  mtlk_core_t              *core = (mtlk_core_t*)hcore;
  mtlk_clpb_t              *clpb = *(mtlk_clpb_t **)data;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  /* read config from internal db */
  MTLK_CFG_SET_ITEM_BY_FUNC(&cca_th_cfg, cca_th_params,
    mtlk_core_cfg_read_cca_threshold, (core, &cca_th_cfg.cca_th_params), res);

  /* push result and config to clipboard*/
  res = mtlk_clpb_push(clpb, &res, sizeof(res));
  if (MTLK_ERR_OK == res) {
    res = mtlk_clpb_push(clpb, &cca_th_cfg, sizeof(cca_th_cfg));
  }

  return res;
}

int __MTLK_IFUNC
mtlk_core_cfg_send_actual_cca_threshold (mtlk_core_t *core)
{
  iwpriv_cca_th_t   cca_th_params;
  int res;

  res = mtlk_core_cfg_read_cca_threshold(core, &cca_th_params);
  if( MTLK_ERR_OK == res) {
    /* TODO: check if the flag is_updated is needed */
    if (cca_th_params.is_updated) {
      res = mtlk_core_cfg_send_cca_threshold_req(core, &cca_th_params);
    }
  }

  return res;
}

int __MTLK_IFUNC
mtlk_core_cfg_init_cca_threshold (mtlk_core_t *core)
{
  return mtlk_core_cfg_send_actual_cca_threshold(core);
}

int __MTLK_IFUNC
mtlk_core_cfg_recovery_cca_threshold (mtlk_core_t *core)
{
  return mtlk_core_cfg_send_actual_cca_threshold(core);
}

/* CCA adaptive intervals */
static int _mtlk_core_send_cca_intervals_req (mtlk_core_t *core, iwpriv_cca_adapt_t *cca_adapt_params)
{
  int res;
  mtlk_txmm_msg_t           man_msg;
  mtlk_txmm_data_t          *man_entry = NULL;
  UMI_BeaconBlockTimerInterval_t *req = NULL;
  mtlk_vap_handle_t         vap_handle = core->vap_handle;

  MTLK_ASSERT(vap_handle);
  ILOG1_D("CID-%04x: BEACON_BLOCKED_INTERVAL_REQ", mtlk_vap_get_oid(vap_handle));

  man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, mtlk_vap_get_txmm(vap_handle), NULL);
  if (NULL == man_entry) {
    ELOG_D("CID-%04x: No man entry available", mtlk_vap_get_oid(vap_handle));
    return MTLK_ERR_NO_RESOURCES;
  }

  man_entry->id = UM_MAN_BEACON_BLOCKED_INTERVAL_REQ;
  man_entry->payload_size = sizeof(UMI_BeaconBlockTimerInterval_t);

  req = (UMI_BeaconBlockTimerInterval_t *)(man_entry->payload);
  req->initial = HOST_TO_MAC32(cca_adapt_params->initial);
  req->iterative = HOST_TO_MAC32(cca_adapt_params->iterative);

  res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);
  if (MTLK_ERR_OK != res) {
    ELOG_DD("CID-%04x: Set BEACON_BLOCKED_INTERVAL_REQ failure (%i)", mtlk_vap_get_oid(vap_handle), res);
  }

  mtlk_txmm_msg_cleanup(&man_msg);

  return res;
}

/* Read CCA adaptive intervals data from PDB, return OK */
static int _mtlk_core_read_cca_intervals (mtlk_core_t *core, iwpriv_cca_adapt_t *cca_adapt_params)
{
  wave_radio_t *radio = wave_vap_radio_get(core->vap_handle);
  mtlk_pdb_size_t cca_adapt_size = sizeof(*cca_adapt_params);

  if (MTLK_ERR_OK != WAVE_RADIO_PDB_GET_BINARY(radio, PARAM_DB_RADIO_CCA_ADAPT, cca_adapt_params, &cca_adapt_size)) {
    ELOG_D("CID-%04x: Can't read CCA_ADAPT from PDB", mtlk_vap_get_oid(core->vap_handle));
    return MTLK_ERR_PARAMS;
  }

  MTLK_ASSERT(sizeof(*cca_adapt_params) == cca_adapt_size);
  return MTLK_ERR_OK;
}

/* Store CCA adaptive intervals in PDB */
static int _mtlk_core_store_cca_intervals(mtlk_core_t *core, iwpriv_cca_adapt_t *cca_adapt_params)
{
  wave_radio_t *radio = wave_vap_radio_get(core->vap_handle);
  int res;

  res = WAVE_RADIO_PDB_SET_BINARY(radio, PARAM_DB_RADIO_CCA_ADAPT, cca_adapt_params, sizeof(*cca_adapt_params));
  if (res != MTLK_ERR_OK)
    ELOG_V("Can't update CCA_ADAPT in PDB");

  return res;
}

static int
_mtlk_core_receive_cca_intervals (mtlk_core_t *core, iwpriv_cca_adapt_t *cca_adapt_params)
{
  int res;
  mtlk_txmm_msg_t   man_msg;
  mtlk_txmm_data_t *man_entry = NULL;
  UMI_BeaconBlockTimerInterval_t *req = NULL;
  mtlk_vap_handle_t vap_handle = core->vap_handle;

  man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, mtlk_vap_get_txmm(vap_handle), NULL);
  if (NULL == man_entry) {
    ELOG_D("CID-%04x: No man entry available", mtlk_vap_get_oid(vap_handle));
    return MTLK_ERR_NO_RESOURCES;
  }

  man_entry->id = UM_MAN_BEACON_BLOCKED_INTERVAL_REQ;
  man_entry->payload_size = sizeof(UMI_BeaconBlockTimerInterval_t);

  req = (UMI_BeaconBlockTimerInterval_t *)(man_entry->payload);
  req->getSetOperation = API_GET_OPERATION;

  res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);

  if (MTLK_ERR_OK == res) {
    cca_adapt_params->initial   = MAC_TO_HOST32(req->initial);
    cca_adapt_params->iterative = MAC_TO_HOST32(req->iterative);
  }
  else {
    ELOG_D("CID-%04x: Receive BEACON_BLOCKED_INTERVAL_REQ failed", mtlk_vap_get_oid(vap_handle));
  }

  mtlk_txmm_msg_cleanup(&man_msg);

  return res;
}

static int
_mtlk_core_get_cca_intervals (mtlk_core_t *core, iwpriv_cca_adapt_t *cca_adapt_params)
{
  int res;
  res = _mtlk_core_read_cca_intervals(core, cca_adapt_params);
  if (MTLK_ERR_OK != res)
    return res;

  /* Update FW related fields */
  return _mtlk_core_receive_cca_intervals(core, cca_adapt_params);
}

static int _mtlk_core_store_and_send_cca_intervals(mtlk_core_t *core, iwpriv_cca_adapt_t *cca_adapt_params)
{
  int res;

  res = _mtlk_core_store_cca_intervals(core, cca_adapt_params);
  if (MTLK_ERR_OK == res)
    res = _mtlk_core_send_cca_intervals_req(core, cca_adapt_params);

  return res;
}

int __MTLK_IFUNC mtlk_core_cfg_set_cca_intervals (mtlk_handle_t hcore, const void* data, uint32 data_size)
{
  int                    res = MTLK_ERR_OK;
  uint32                 cfg_size;
  mtlk_core_t            *core = (mtlk_core_t*)hcore;
  wave_radio_t           *radio = wave_vap_radio_get(core->vap_handle);
  mtlk_cca_adapt_cfg_t   *cca_adapt_cfg = NULL;
  mtlk_clpb_t            *clpb = *(mtlk_clpb_t **)data;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);
  MTLK_ASSERT(core);

  /* get configuration */
  cca_adapt_cfg = mtlk_clpb_enum_get_next(clpb, &cfg_size);
  MTLK_CLPB_TRY(cca_adapt_cfg, cfg_size)
    if (!wave_radio_interfdet_get(radio)) {
      ELOG_V("Cannot enable adaptive CCA while interference detection is deactivated");
      MTLK_CLPB_EXIT(MTLK_ERR_NOT_SUPPORTED); /* do not process */
    }

    MTLK_CFG_START_CHEK_ITEM_AND_CALL()
      MTLK_CFG_CHECK_ITEM_AND_CALL(cca_adapt_cfg, cca_adapt_params,
        _mtlk_core_store_and_send_cca_intervals, (core, &cca_adapt_cfg->cca_adapt_params), res);
    MTLK_CFG_END_CHEK_ITEM_AND_CALL()
  MTLK_CLPB_FINALLY(res)
    return mtlk_clpb_push_res(clpb, res);
  MTLK_CLPB_END
}

int __MTLK_IFUNC
mtlk_core_cfg_get_cca_intervals (mtlk_handle_t hcore, const void* data, uint32 data_size)
{
  int                      res = MTLK_ERR_OK;
  mtlk_cca_adapt_cfg_t     cca_adapt_cfg;
  mtlk_core_t              *core = (mtlk_core_t*)hcore;
  mtlk_clpb_t              *clpb = *(mtlk_clpb_t **)data;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  /* read config from internal db */
  MTLK_CFG_SET_ITEM_BY_FUNC(&cca_adapt_cfg, cca_adapt_params,
    _mtlk_core_read_cca_intervals, (core, &cca_adapt_cfg.cca_adapt_params), res);

  /* push result and config to clipboard*/
  res = mtlk_clpb_push(clpb, &res, sizeof(res));
  if (MTLK_ERR_OK == res) {
    res = mtlk_clpb_push(clpb, &cca_adapt_cfg, sizeof(cca_adapt_cfg));
  }

  return res;
}

int mtlk_core_cfg_init_cca_adapt (mtlk_core_t *core)
{
  int i, res;
  iwpriv_cca_th_t    cca_th_params;
  iwpriv_cca_adapt_t cca_adapt_params;

  /* read config copy it */
  if (MTLK_ERR_OK != mtlk_core_cfg_read_cca_threshold(core, &cca_th_params)) {
    return MTLK_ERR_UNKNOWN;
  }
  memset(&core->slow_ctx->cca_adapt, 0, sizeof(core->slow_ctx->cca_adapt));
  core->slow_ctx->cca_adapt.stepping_down = 0;
  core->slow_ctx->cca_adapt.step_down_coef = 1;
  core->slow_ctx->cca_adapt.last_unblocked_time = 0;

  for (i = 0; i < MTLK_CCA_TH_PARAMS_LEN; i++)
    core->slow_ctx->cca_adapt.cca_th_params[i] = cca_th_params.values[i];

  res = _mtlk_core_get_cca_intervals(core, &cca_adapt_params);
  if (MTLK_ERR_OK != res)
    return res;

  return _mtlk_core_store_cca_intervals(core, &cca_adapt_params);
}

int mtlk_core_cfg_recovery_cca_adapt(mtlk_core_t *core)
{
  iwpriv_cca_adapt_t   cca_adapt_params;

  /* read config and send if it was set */
  if (MTLK_ERR_OK == _mtlk_core_read_cca_intervals(core, &cca_adapt_params)) {
    return _mtlk_core_send_cca_intervals_req(core, &cca_adapt_params);
  }
  else {
    return MTLK_ERR_OK; /* was not set */
  }
}


/*------ Radar Detection RSSI Threshold -----------------------------*/

static BOOL
_mtlk_core_cfg_radar_rssi_is_supported(mtlk_core_t *core)
{
  /* Only 5 GHz */
  return (MTLK_HW_BAND_5_2_GHZ == core_cfg_get_freq_band_cur(core));
}

static int
_mtlk_core_receive_radar_rssi_th (mtlk_core_t *core, int *radar_rssi_th)
{
  int res;
  mtlk_txmm_msg_t                    man_msg;
  mtlk_txmm_data_t                   *man_entry = NULL;
  mtlk_vap_handle_t                  vap_handle = core->vap_handle;
  UMI_RADAR_DETECTION_RSSI_TH_CONFIG *req = NULL;

  man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, mtlk_vap_get_txmm(vap_handle), NULL);
  if (NULL == man_entry) {
    ELOG_D("CID-%04x: No man entry available", mtlk_vap_get_oid(vap_handle));
    return MTLK_ERR_NO_RESOURCES;
  }

  man_entry->id = UM_MAN_RADAR_DETECTION_RSSI_TH_CONFIG_REQ;
  man_entry->payload_size = sizeof(UMI_RADAR_DETECTION_RSSI_TH_CONFIG);
  req = (UMI_RADAR_DETECTION_RSSI_TH_CONFIG *)(man_entry->payload);
  req->getSetOperation = API_GET_OPERATION;

  res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);
  if (MTLK_ERR_OK == res) {
    *radar_rssi_th = MAC_TO_HOST32(req->radarDetectionRssiTh);
  }
  else {
    ELOG_D("CID-%04x: Receive RADAR_RSSI_TH_REQ failed", mtlk_vap_get_oid(vap_handle));
  }

  mtlk_txmm_msg_cleanup(&man_msg);

  return res;
}

static int
_mtlk_core_cfg_send_radar_rssi_th_req(mtlk_core_t *core, int radar_rssi_th)
{
  int res;
  mtlk_txmm_msg_t                    man_msg;
  mtlk_txmm_data_t                   *man_entry = NULL;
  mtlk_vap_handle_t                  vap_handle = core->vap_handle;
  UMI_RADAR_DETECTION_RSSI_TH_CONFIG *req = NULL;

  MTLK_ASSERT(vap_handle);
  ILOG1_DD("CID-%04x: Set RADAR_RSSI_TH to %d", mtlk_vap_get_oid(vap_handle), radar_rssi_th);

  man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, mtlk_vap_get_txmm(vap_handle), NULL);
  if (NULL == man_entry) {
    ELOG_D("CID-%04x: No man entry available", mtlk_vap_get_oid(vap_handle));
    return MTLK_ERR_NO_RESOURCES;
  }

  man_entry->id = UM_MAN_RADAR_DETECTION_RSSI_TH_CONFIG_REQ;
  man_entry->payload_size = sizeof(UMI_RADAR_DETECTION_RSSI_TH_CONFIG);

  req = (UMI_RADAR_DETECTION_RSSI_TH_CONFIG *)(man_entry->payload);
  req->radarDetectionRssiTh = HOST_TO_MAC32((int32)radar_rssi_th);

  res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);
  if (MTLK_ERR_OK != res) {
    ELOG_DD("CID-%04x: Set RADAR_RSSI_TH_REQ failure (%i)", mtlk_vap_get_oid(vap_handle), res);
  }

  mtlk_txmm_msg_cleanup(&man_msg);

  return res;
}

/* Read Radar Detection RSSI Threshold data from PDB */
static int
_mtlk_core_cfg_read_radar_rssi_threshold(mtlk_core_t *core)
{
  wave_radio_t *radio = wave_vap_radio_get(core->vap_handle);
  return WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_RADAR_RSSI_TH);
}

/* Store Radar Detection RSSI Threshold in PDB */
static void
_mtlk_core_cfg_store_radar_rssi_threshold(mtlk_core_t *core, int radar_rssi_th)
{
  wave_radio_t *radio = wave_vap_radio_get(core->vap_handle);
  WAVE_RADIO_PDB_SET_INT(radio, PARAM_DB_RADIO_RADAR_RSSI_TH, radar_rssi_th);
}

static int
_mtlk_core_cfg_store_and_send_radar_rssi_threshold(mtlk_core_t *core, int radar_rssi_th)
{
  if(!_mtlk_core_cfg_radar_rssi_is_supported(core)) {
    ILOG0_V("Only 5 GHz band is supported");
    return MTLK_ERR_NOT_SUPPORTED;
  }

  if (PHY_DRIVER_RADAR_DETECTION_RSSI_TH_MIN_VALUE > radar_rssi_th ||
      PHY_DRIVER_RADAR_DETECTION_RSSI_TH_MAX_VALUE < radar_rssi_th) {
    ELOG_DDD("Vaule (%d) is not in range from %d to %d", radar_rssi_th,
             PHY_DRIVER_RADAR_DETECTION_RSSI_TH_MIN_VALUE,
             PHY_DRIVER_RADAR_DETECTION_RSSI_TH_MAX_VALUE
    );
    return MTLK_ERR_PARAMS;

  }

  _mtlk_core_cfg_store_radar_rssi_threshold(core, radar_rssi_th);
  return _mtlk_core_cfg_send_radar_rssi_th_req(core, radar_rssi_th);
}

int __MTLK_IFUNC
mtlk_core_cfg_set_radar_rssi_threshold (mtlk_handle_t hcore, const void* data, uint32 data_size)
{
  int                       res = MTLK_ERR_OK;
  uint32                    cfg_size;
  mtlk_core_t               *core = (mtlk_core_t*)hcore;
  mtlk_radar_rssi_th_cfg_t  *radar_rssi_th_cfg = NULL;
  mtlk_clpb_t               *clpb = *(mtlk_clpb_t **)data;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);
  MTLK_ASSERT(core);

  /* get configuration */
  radar_rssi_th_cfg = mtlk_clpb_enum_get_next(clpb, &cfg_size);
  MTLK_CLPB_TRY(radar_rssi_th_cfg, cfg_size)
    MTLK_CFG_START_CHEK_ITEM_AND_CALL()
      MTLK_CFG_CHECK_ITEM_AND_CALL(radar_rssi_th_cfg, radar_rssi_th,
          _mtlk_core_cfg_store_and_send_radar_rssi_threshold, (core, radar_rssi_th_cfg->radar_rssi_th), res);
    MTLK_CFG_END_CHEK_ITEM_AND_CALL()
  MTLK_CLPB_FINALLY(res)
    /* push result into clipboard */
    return mtlk_clpb_push_res(clpb, res);
  MTLK_CLPB_END
}

int __MTLK_IFUNC
mtlk_core_cfg_get_radar_rssi_threshold (mtlk_handle_t hcore, const void* data, uint32 data_size)
{
  int                      res = MTLK_ERR_OK;
  mtlk_radar_rssi_th_cfg_t radar_rssi_th_cfg;
  mtlk_core_t              *core = (mtlk_core_t*)hcore;
  mtlk_clpb_t              *clpb = *(mtlk_clpb_t **)data;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  memset(&radar_rssi_th_cfg, 0, sizeof(radar_rssi_th_cfg));

  if (_mtlk_core_cfg_radar_rssi_is_supported(core)) {
    MTLK_CFG_SET_ITEM_BY_FUNC(&radar_rssi_th_cfg, radar_rssi_th, _mtlk_core_receive_radar_rssi_th,
                              (core, &radar_rssi_th_cfg.radar_rssi_th), res);
  } else {
    res = MTLK_ERR_NOT_SUPPORTED;
  }

  /* push result and config to clipboard*/
  res = mtlk_clpb_push(clpb, &res, sizeof(res));
  if (MTLK_ERR_OK == res) {
    res = mtlk_clpb_push(clpb, &radar_rssi_th_cfg, sizeof(radar_rssi_th_cfg));
  }

  return res;
}

int __MTLK_IFUNC
mtlk_core_cfg_recovery_radar_rssi_threshold (mtlk_core_t *core)
{
  int radar_rssi_th;
  int res = MTLK_ERR_OK;

  /* read config and send if it is not default value */
  if(_mtlk_core_cfg_radar_rssi_is_supported(core)) {
    radar_rssi_th = _mtlk_core_cfg_read_radar_rssi_threshold(core);

    if (MTLK_PARAM_DB_VALUE_IS_INVALID(radar_rssi_th))
      return MTLK_ERR_OK;

    res = _mtlk_core_cfg_send_radar_rssi_th_req(core, radar_rssi_th);
  }

  return res;
}

/*------ IRE Control B --------------------------------------------*/

#define MTLK_IRE_CTRL_DFLT  SET_AP_LOW_CHANNEL

static BOOL
_mtlk_core_cfg_ire_ctrl_is_supported (mtlk_core_t *core)
{
  /* Only Gen5 */
  return (mtlk_hw_type_is_gen5(mtlk_vap_get_hw(core->vap_handle)) &&
          (MTLK_HW_BAND_5_2_GHZ == core_cfg_get_freq_band_cur(core)));
}

static int
_mtlk_core_cfg_send_ire_ctrl_req (mtlk_core_t *core, IREControl_e req_value)
{
  mtlk_vap_handle_t             vap_handle = core->vap_handle;
  mtlk_txmm_msg_t               man_msg;
  mtlk_txmm_data_t             *man_entry = NULL;
  UMI_CONTROL_t                *req = NULL;
  unsigned                      oid;
  int                           res;

  MTLK_ASSERT(vap_handle);
  oid = mtlk_vap_get_oid(vap_handle);

  ILOG1_DD("CID-%04x: Set IRE_CONTROL_B to %d", oid, req_value);

  man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, mtlk_vap_get_txmm(vap_handle), NULL);
  if (NULL == man_entry) {
    ELOG_D("CID-%04x: No man entry available", oid);
    return MTLK_ERR_NO_RESOURCES;
  }

  man_entry->id = UM_MAN_SET_IRE_SWITCH_B_REQ;
  man_entry->payload_size = sizeof(UMI_CONTROL_t);

  req = (UMI_CONTROL_t *)(man_entry->payload);
  req->setAPHighLowFilterBank = req_value; /* int8 */

  res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);
  if (MTLK_ERR_OK != res) {
    ELOG_DD("CID-%04x: Set IRE_CONTROL_B failure (%d)", oid, res);
  }

  mtlk_txmm_msg_cleanup(&man_msg);

  return res;
}

/* Read IRE Control from PDB */
static int
_mtlk_core_cfg_read_ire_ctrl (mtlk_core_t *core)
{
  int val;
   wave_radio_t *radio = wave_vap_radio_get(core->vap_handle);
  val = WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_IRE_CTRL_B);

  return val;
}

/* Store IRE Control in PDB */
static void
_mtlk_core_cfg_store_ire_ctrl (mtlk_core_t *core, int value)
{
  wave_radio_t *radio = wave_vap_radio_get(core->vap_handle);
  WAVE_RADIO_PDB_SET_INT(radio, PARAM_DB_RADIO_IRE_CTRL_B, value);
}

static int
_mtlk_core_cfg_store_and_send_ire_ctrl (mtlk_core_t *core, int value)
{

  if(!_mtlk_core_cfg_ire_ctrl_is_supported(core)) {
    ELOG_V("IRE Control is only supported on Gen5 with 5 GHz band");
    return MTLK_ERR_NOT_SUPPORTED;
  }

  if (SET_AP_LOW_CHANNEL != value &&
      SET_AP_HIGH_CHANNEL != value) {
    ELOG_DDD("Wrong vaule (%d), expected %d or %d", value,
             SET_AP_LOW_CHANNEL, SET_AP_HIGH_CHANNEL);

    return MTLK_ERR_PARAMS;
  }

  _mtlk_core_cfg_store_ire_ctrl(core, value);

  return _mtlk_core_cfg_send_ire_ctrl_req(core, value);
}

int __MTLK_IFUNC
mtlk_core_cfg_set_ire_ctrl_b (mtlk_handle_t hcore, const void* data, uint32 data_size)
{
  int                       res = MTLK_ERR_OK;
  uint32                    cfg_size;
  mtlk_core_t               *core = (mtlk_core_t*)hcore;
  mtlk_ire_ctrl_cfg_t       *ire_ctrl_cfg = NULL;
  mtlk_clpb_t               *clpb = *(mtlk_clpb_t **)data;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);
  MTLK_ASSERT(core);

  /* get configuration */
  ire_ctrl_cfg = mtlk_clpb_enum_get_next(clpb, &cfg_size);
  MTLK_CLPB_TRY(ire_ctrl_cfg, cfg_size)
    MTLK_CFG_START_CHEK_ITEM_AND_CALL()
      MTLK_CFG_CHECK_ITEM_AND_CALL(ire_ctrl_cfg, ire_ctrl_value,
          _mtlk_core_cfg_store_and_send_ire_ctrl, (core, ire_ctrl_cfg->ire_ctrl_value), res);
    MTLK_CFG_END_CHEK_ITEM_AND_CALL()
  MTLK_CLPB_FINALLY(res)
    /* push result into clipboard */
    return mtlk_clpb_push_res(clpb, res);
  MTLK_CLPB_END
}

int __MTLK_IFUNC
mtlk_core_cfg_get_ire_ctrl_b (mtlk_handle_t hcore, const void* data, uint32 data_size)
{
  int                      res = MTLK_ERR_OK;
  mtlk_ire_ctrl_cfg_t      ire_ctrl_cfg;
  mtlk_core_t              *core = (mtlk_core_t*)hcore;
  mtlk_clpb_t              *clpb = *(mtlk_clpb_t **)data;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  memset(&ire_ctrl_cfg, 0, sizeof(ire_ctrl_cfg));

  if (_mtlk_core_cfg_ire_ctrl_is_supported(core)) {
    /* read config from internal db */
    MTLK_CFG_SET_ITEM(&ire_ctrl_cfg, ire_ctrl_value, _mtlk_core_cfg_read_ire_ctrl(core));
  } else {
    res = MTLK_ERR_NOT_SUPPORTED;
  }

  /* push result and config to clipboard*/
  res = mtlk_clpb_push(clpb, &res, sizeof(res));
  if (MTLK_ERR_OK == res) {
    res = mtlk_clpb_push(clpb, &ire_ctrl_cfg, sizeof(ire_ctrl_cfg));
  }

  return res;
}

void
mtlk_core_cfg_init_ire_ctrl (mtlk_core_t *core)
{
  _mtlk_core_cfg_store_ire_ctrl(core, SET_AP_LOW_CHANNEL);
}

int
mtlk_core_cfg_recovery_ire_ctrl (mtlk_core_t *core)
{
  int   val;
  int   res = MTLK_ERR_OK;

  val = _mtlk_core_cfg_read_ire_ctrl(core);
  if (MTLK_IRE_CTRL_DFLT != val) {
    res = _mtlk_core_cfg_send_ire_ctrl_req(core, val);
  }

  return res;
}

/* SSB Mode Configuration */
static int
_mtlk_core_check_ssb_mode (mtlk_core_t *core, const mtlk_ssb_mode_cfg_t *ssb_mode_cfg)
{
    if (/* All params are within limits */
        (ssb_mode_cfg->params[MTLK_SSB_MODE_CFG_EN] > 1) ||
        (ssb_mode_cfg->params[MTLK_SSB_MODE_CFG_20MODE] > 1) ) {
        ELOG_V("one of params does not fit to the range [0...1]");
        return MTLK_ERR_PARAMS;
    }
    return MTLK_ERR_OK;
}

int __MTLK_IFUNC
mtlk_core_send_ssb_mode (mtlk_core_t *core, const mtlk_ssb_mode_cfg_t *ssb_mode_cfg)
{
    mtlk_txmm_msg_t         man_msg;
    mtlk_txmm_data_t       *man_entry;
    UMI_SSB_Mode_t         *mac_msg;
    int                     res;
    unsigned                oid;

    MTLK_ASSERT(core != NULL);
    oid = mtlk_vap_get_oid(core->vap_handle);

    ILOG2_D("CID-%04x: Send SSB MODE", oid);

    /* allocate a new message */
    man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, mtlk_vap_get_txmm(core->vap_handle), NULL);
    if (!man_entry) {
      ELOG_D("CID-%04x: Can not get TXMM slot", oid);
      return MTLK_ERR_NO_RESOURCES;
    }

    /* fill the message data */
    man_entry->id = UM_MAN_SSB_MODE_REQ;
    man_entry->payload_size = sizeof(UMI_SSB_Mode_t);
    mac_msg = (UMI_SSB_Mode_t *)man_entry->payload;
    memset(mac_msg, 0, sizeof(*mac_msg));

    mac_msg->enableSSB  = ssb_mode_cfg->params[MTLK_SSB_MODE_CFG_EN];
    mac_msg->SSB20Mode  = ssb_mode_cfg->params[MTLK_SSB_MODE_CFG_20MODE];

    /* send the message to FW */
    res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);
    if (MTLK_ERR_OK != res) {
      ELOG_DD("CID-%04x: Send SSB MODE failed (%i)", oid, res);
    }

    /* cleanup the message */
    mtlk_txmm_msg_cleanup(&man_msg);
    return res;
}

static int
_mtlk_core_receive_ssb_mode (mtlk_core_t *core, mtlk_ssb_mode_cfg_t *ssb_mode_cfg)
{
  mtlk_txmm_msg_t   man_msg;
  mtlk_txmm_data_t *man_entry;
  UMI_SSB_Mode_t   *mac_msg;
  int               res;
  unsigned          oid;

  oid = mtlk_vap_get_oid(core->vap_handle);
  man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, mtlk_vap_get_txmm(core->vap_handle), NULL);
  if (!man_entry) {
    ELOG_D("CID-%04x: Can not get TXMM slot", oid);
    return MTLK_ERR_NO_RESOURCES;
  }

  man_entry->id = UM_MAN_SSB_MODE_REQ;
  man_entry->payload_size = sizeof(UMI_SSB_Mode_t);
  mac_msg = (UMI_SSB_Mode_t *)man_entry->payload;
  mac_msg->getSetOperation = API_GET_OPERATION;

  res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);
  if (MTLK_ERR_OK == res) {
    ssb_mode_cfg->params[MTLK_SSB_MODE_CFG_EN]     = mac_msg->enableSSB;
    ssb_mode_cfg->params[MTLK_SSB_MODE_CFG_20MODE] = mac_msg->SSB20Mode;
  }
  else {
    ELOG_D("CID-%04x: Receive UM_MAN_SSB_MODE_REQ failed", oid);
  }

  mtlk_txmm_msg_cleanup(&man_msg);
  return res;
}

static int
_mtlk_core_check_and_send_ssb_mode (mtlk_core_t *core, const mtlk_ssb_mode_cfg_t *ssb_mode_cfg)
{
    int res = _mtlk_core_check_ssb_mode(core, ssb_mode_cfg);
    if (MTLK_ERR_OK == res) {
      res = mtlk_core_send_ssb_mode(core, ssb_mode_cfg);
    }
    return res;
}

static void
_mtlk_core_store_ssb_mode (mtlk_core_t *core, const mtlk_ssb_mode_cfg_t *ssb_mode_cfg, uint32 data_size)
{
    MTLK_ASSERT(core != NULL);
    WAVE_RADIO_PDB_SET_BINARY(wave_vap_radio_get(core->vap_handle), PARAM_DB_RADIO_SSB_MODE, ssb_mode_cfg, data_size);
}

void __MTLK_IFUNC
mtlk_core_read_ssb_mode (mtlk_core_t *core, mtlk_ssb_mode_cfg_t *ssb_mode_cfg, uint32 *data_size)
{
    MTLK_ASSERT(core != NULL);
    WAVE_RADIO_PDB_GET_BINARY(wave_vap_radio_get(core->vap_handle), PARAM_DB_RADIO_SSB_MODE, ssb_mode_cfg, data_size);
}

int __MTLK_IFUNC
mtlk_core_set_ssb_mode (mtlk_handle_t hcore, const void *data, uint32 data_size)
{
    int res = MTLK_ERR_OK;
    mtlk_core_t *core = (mtlk_core_t *) hcore;
    mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;
    mtlk_ssb_mode_cfg_t *ssb_mode_cfg = NULL;
    uint32 ssb_mode_cfg_size;

    MTLK_ASSERT(core != NULL);
    MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

    /* get configuration */
    ssb_mode_cfg = mtlk_clpb_enum_get_next(clpb, &ssb_mode_cfg_size);
    MTLK_CLPB_TRY(ssb_mode_cfg, ssb_mode_cfg_size)
      MTLK_CFG_START_CHEK_ITEM_AND_CALL()
        /* send new config to FW */
        MTLK_CFG_CHECK_ITEM_AND_CALL(ssb_mode_cfg, params, _mtlk_core_check_and_send_ssb_mode,
                                     (core, ssb_mode_cfg), res);
        /* store new config in internal DB */
        MTLK_CFG_CHECK_ITEM_AND_CALL_VOID(ssb_mode_cfg, params, _mtlk_core_store_ssb_mode,
                                          (core, ssb_mode_cfg, MTLK_SSB_MODE_CFG_SIZE));
      MTLK_CFG_END_CHEK_ITEM_AND_CALL()
    MTLK_CLPB_FINALLY(res)
      /* push result into clipboard */
      return mtlk_clpb_push_res(clpb, res);
    MTLK_CLPB_END
}

int __MTLK_IFUNC
mtlk_core_get_ssb_mode (mtlk_handle_t hcore, const void *data, uint32 data_size)
{
    int res = MTLK_ERR_OK;
    mtlk_ssb_mode_cfg_t ssb_mode_cfg;
    mtlk_core_t *core = (mtlk_core_t*)hcore;
    mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;

    MTLK_ASSERT(core != NULL);
    MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

    /* read config from internal DB */
    MTLK_CFG_SET_ITEM_BY_FUNC(&ssb_mode_cfg, params, _mtlk_core_receive_ssb_mode,
                              (core, &ssb_mode_cfg), res);

    /* push result and config into clipboard */
    res = mtlk_clpb_push(clpb, &res, sizeof(res));
    if (MTLK_ERR_OK == res) {
        res = mtlk_clpb_push(clpb, &ssb_mode_cfg, sizeof(ssb_mode_cfg));
    }
    return res;
}

static int
_mtlk_core_cfg_send_atf_quotas (mtlk_core_t *core,
                                const struct intel_vendor_atf_quotas *mtlk_quotas,
                                uint16 max_sid,
                                uint8 *atf_op_res)
{
  mtlk_txmm_msg_t  man_msg;
  mtlk_txmm_data_t *man_entry;
  UMI_ATF_QUOTAS   *umi_quotas;
  int              res, i;
  uint16           oid;

  MTLK_ASSERT(core);

  oid = mtlk_vap_get_oid(core->vap_handle);
  ILOG1_D("CID-%04x: Send ATF QUOTAS", oid);

  /* allocate a new message */
  man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, mtlk_vap_get_txmm(core->vap_handle), &res);
  if (!man_entry) {
    ELOG_DD("CID-%04x: UM_MAN_ATF_QUOTAS_REQ init failed, err=%i", oid, res);
    return MTLK_ERR_NO_RESOURCES;
  }

  /* fill the message data */
  man_entry->id = UM_MAN_ATF_QUOTAS_REQ;
  umi_quotas = (UMI_ATF_QUOTAS *)(man_entry->payload);
  man_entry->payload_size = sizeof(UMI_ATF_QUOTAS);

  memset (umi_quotas, 0, man_entry->payload_size);

  /* transfer data from mtlk_quotas to umi_quotas */
  umi_quotas->u32interval         = HOST_TO_MAC32(mtlk_quotas->interval);
  umi_quotas->u32freeTime         = HOST_TO_MAC32(mtlk_quotas->free_time);
  umi_quotas->u8AtmAlgorithemType = mtlk_quotas->algo_type;
  umi_quotas->u8AtmWeightedType   = mtlk_quotas->weighted_type;
  umi_quotas->u8nof_vaps          = mtlk_quotas->nof_bss;
  umi_quotas->u16nof_sta          = HOST_TO_MAC16(mtlk_quotas->nof_grants);

  for (i = 0; i < mtlk_quotas->nof_bss; i++) {
    umi_quotas->AtmDistributionType[i] = mtlk_quotas->distr_type;
  }

  for (i = 0; i < mtlk_quotas->nof_grants; i++) {
    uint16 sid = mtlk_quotas->sta_grant[i].sid;
    umi_quotas->stationGrant[sid] = HOST_TO_MAC16(mtlk_quotas->sta_grant[i].grant);
  }

  ILOG1_DDDD("ATF: interval=%u, freeTime=%u, u8AtmAlgorithemType=%u, AtmDistributionType=%u",
             mtlk_quotas->interval,
             mtlk_quotas->free_time,
             mtlk_quotas->algo_type,
             mtlk_quotas->distr_type);
  ILOG1_DDDDD("ATF: u8AtmWeightedType=%d, nof_vaps=%u, nof_sta=%u, nof_grants=%u, max_sid=%u",
            mtlk_quotas->weighted_type,
            mtlk_quotas->nof_bss,
            mtlk_quotas->nof_sta,
            mtlk_quotas->nof_grants,
            max_sid);

  mtlk_dump(2, umi_quotas->stationGrant, sizeof(umi_quotas->stationGrant), "STA grant:");

  /* send the message to FW */
  res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);

  if (MTLK_ERR_OK == res) {
    *atf_op_res = umi_quotas->u8error_code;
    ILOG1_DD("ATF: UM_MAN_ATF_QUOTAS_REQ sent, error_code=%d, res=%d", *atf_op_res, res);
  }
  else {
    ELOG_DD("CID-%04x: Send ATF QUOTAS failed (%i)", oid, res);
  }

  /* cleanup the message */
  mtlk_txmm_msg_cleanup(&man_msg);
  return res;
}

int __MTLK_IFUNC
mtlk_core_cfg_set_atf_quotas (mtlk_handle_t hcore, const void *data, uint32 data_size)
{
  int res;
  uint8 atf_op_res;
  uint32 mtlk_quotas_size, expected_size;
  unsigned max_vaps, max_stas, max_sid;
  mtlk_core_t *core = HANDLE_T_PTR(mtlk_core_t, hcore);
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;
  struct intel_vendor_atf_quotas *mtlk_quotas = NULL;
  wave_radio_t *radio;

  MTLK_ASSERT(core != NULL);
  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  /* get configuration */
  mtlk_quotas = mtlk_clpb_enum_get_next(clpb, &mtlk_quotas_size);
  if (__UNLIKELY(NULL == mtlk_quotas)) {
    ELOG_V("failed to get data from clipboard");
    return MTLK_ERR_UNKNOWN;
  }

  expected_size = sizeof(*mtlk_quotas);
  if (__UNLIKELY(mtlk_quotas_size < expected_size)) {
    ELOG_DD("wrong data size in clipboard %u (expected: more than %u)", mtlk_quotas_size, expected_size);
    return MTLK_ERR_UNKNOWN;
  }

  expected_size += mtlk_quotas->data_len;
  if (__UNLIKELY(mtlk_quotas_size != expected_size)) {
    ELOG_DD("wrong data size in clipboard %u (expected: %u)", mtlk_quotas_size, expected_size);
    return MTLK_ERR_UNKNOWN;
  }

  /* Data in UMI_ATF_QUOTAS contain arrays whose dimensions are HW-dependent */
  radio = wave_vap_radio_get(core->vap_handle);
  max_vaps = wave_radio_max_vaps_get(radio);
  max_stas = wave_radio_max_stas_get(radio);

  if (mtlk_quotas->nof_bss > max_vaps) {
    ELOG_DD("nof_bss (%u) > max_vaps (%u)", mtlk_quotas->nof_bss, max_vaps);
    return MTLK_ERR_PARAMS;
  }

  if (mtlk_quotas->nof_sta > max_stas) {
    ELOG_DD("nof_sta (%u) > max_stas (%u)", mtlk_quotas->nof_sta, max_stas);
    return MTLK_ERR_PARAMS;
  }

  max_sid = wave_hw_max_sid_get(mtlk_vap_get_hw(core->vap_handle));

  {
    unsigned i;
    for (i = 0; i < mtlk_quotas->nof_grants; i++) {
      if (mtlk_quotas->sta_grant[i].sid > max_sid) {
        ELOG_DDD("SID (%u) number (%u) more than max SID (%u)",
                 mtlk_quotas->sta_grant[i].sid, i, max_sid);
        return MTLK_ERR_PARAMS;
      }
    }
  }

  /* send configuration to the FW */
  res = _mtlk_core_cfg_send_atf_quotas(core, mtlk_quotas, max_sid, &atf_op_res);

  /* push result and response from FW into clipboard */
  return mtlk_clpb_push_res_data(clpb, res, &atf_op_res, sizeof(atf_op_res));
}

int __MTLK_IFUNC
mtlk_core_cfg_send_slow_probing_mask(mtlk_core_t *core, uint32 mask)
{
  mtlk_txmm_msg_t                 man_msg;
  mtlk_txmm_data_t               *man_entry = NULL;
  UMI_DisablePowerAdapt_t        *probing_mask = NULL;
  mtlk_vap_handle_t               vap_handle = core->vap_handle;
  wave_radio_t                    *radio = wave_vap_radio_get(vap_handle);
  int                             res;

  if (mask > MAX_UINT8) {
    ELOG_D("Wrong slow probing mask: %u, must be from 0 to 0xFF", mask);

    ELOG_V("SLOW_PROBING_POWER\n" \
           "SLOW_PROBING_BW\n" \
           "SLOW_PROBING_CP\n" \
           "SLOW_PROBING_BF\n" \
           "SLOW_PROBING_CDD\n" \
           "SLOW_PROBING_ANT_SEL\n" \
           "SLOW_PROBING_NONE\n" \
           "SLOW_PROBING_TASKS_NUM");

    return MTLK_ERR_PARAMS;
  }

  ILOG2_V("Sending UM_MAN_SLOW_PROBING_MASK_REQ");

  man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, mtlk_vap_get_txmm(vap_handle), NULL);

  if (NULL == man_entry) {
    ELOG_D("CID-%04x: No man entry available", mtlk_vap_get_oid(vap_handle));
    return MTLK_ERR_NO_RESOURCES;
  }

  man_entry->id = UM_MAN_SLOW_PROBING_MASK_REQ;
  man_entry->payload_size = sizeof(UMI_DisablePowerAdapt_t);

  probing_mask = (UMI_DisablePowerAdapt_t *)(man_entry->payload);
  probing_mask->slowProbingMask = (uint8)mask;

  res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);

  if (MTLK_ERR_OK != res) {
    ELOG_DD("CID-%04x: Sending UM_MAN_SLOW_PROBING_MASK_REQ failed (res = %d)", mtlk_vap_get_oid(vap_handle), res);
  }
  else {
    WAVE_RADIO_PDB_SET_INT(radio, PARAM_DB_RADIO_SLOW_PROBING_MASK, mask);
  }

  mtlk_txmm_msg_cleanup(&man_msg);
  return res;
}

int __MTLK_IFUNC
mtlk_core_receive_slow_probing_mask (mtlk_core_t *core, uint32 *mask)
{
  mtlk_txmm_msg_t          man_msg;
  mtlk_txmm_data_t        *man_entry = NULL;
  UMI_DisablePowerAdapt_t *probing_mask = NULL;
  mtlk_vap_handle_t        vap_handle = core->vap_handle;
  int                      res;

  man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, mtlk_vap_get_txmm(vap_handle), NULL);
  if (NULL == man_entry) {
    ELOG_D("CID-%04x: No man entry available", mtlk_vap_get_oid(vap_handle));
    return MTLK_ERR_NO_RESOURCES;
  }

  man_entry->id = UM_MAN_SLOW_PROBING_MASK_REQ;
  man_entry->payload_size = sizeof(UMI_DisablePowerAdapt_t);
  probing_mask = (UMI_DisablePowerAdapt_t *)(man_entry->payload);
  probing_mask->getSetOperation = API_GET_OPERATION;

  res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);

  if (MTLK_ERR_OK == res) {
    *mask = probing_mask->slowProbingMask;
  }
  else {
    ELOG_D("CID-%04x: Receive UM_MAN_SLOW_PROBING_MASK_REQ failed", mtlk_vap_get_oid(vap_handle));
  }

  mtlk_txmm_msg_cleanup(&man_msg);
  return res;
}

int __MTLK_IFUNC
mtlk_core_cfg_send_active_ant_mask (mtlk_core_t *core, uint32 mask)
{
  int               res;
  uint32            hw_antenna_mask;
  mtlk_txmm_msg_t   umi_msg;
  mtlk_txmm_data_t  *man_entry = NULL;
  UMI_SET_ANTENNAS  *umi_set_antennas = NULL;
  mtlk_vap_handle_t vap_handle = core->vap_handle;
  mtlk_hw_t         *hw = mtlk_vap_get_hw(vap_handle);
  wave_radio_t      *radio = wave_vap_radio_get(vap_handle);

  MTLK_ASSERT(hw);

  /* This iwpriv is supported for 5G band only -- the limitation imposed by request from FW team */
  if (mtlk_core_is_band_supported(core, UMI_PHY_TYPE_BAND_5_2_GHZ) != MTLK_ERR_OK) {
    ELOG_V("Active antenna mask is only supported for 5G band");
    return MTLK_ERR_OK;
  }

  /* The given antenna mask has to be supported by hardware */
  if (MTLK_ERR_OK != psdb_get_field_val(mtlk_hw_get_psdb(hw), PSDB_FIELD_TX_ANTENNAS_MASK, &hw_antenna_mask)) {
    return MTLK_ERR_PARAMS;
  }
  if (mask & ~hw_antenna_mask) {
    ELOG_DD("The given active antenna mask (%u) isn't supported by hardware (%u)", mask, hw_antenna_mask);
    return MTLK_ERR_PARAMS;
  }

  /* Zero antenna mask means resetting it to maximum allowed by hardware */
  if (!mask) {
    mask = hw_antenna_mask;
  }

  /* Active antenna mask cannot be altered if Auto CoC is enabled */
  if (mtlk_coc_is_auto_mode(__wave_core_coc_mgmt_get(core))) {
    ELOG_V("Active antenna mask cannot be set if auto CoC is turned on");
    return MTLK_ERR_PARAMS;
  }

  ILOG2_D("Sending MC_MAN_SET_ANTENNAS_REQ, mask = %d", mask);

  man_entry = mtlk_txmm_msg_init_with_empty_data(&umi_msg, mtlk_vap_get_txmm(vap_handle), NULL);

  if (NULL == man_entry) {
    ELOG_D("CID-%04x: No man entry available", mtlk_vap_get_oid(vap_handle));
    return MTLK_ERR_NO_RESOURCES;
  }

  man_entry->id = MC_MAN_SET_ANTENNAS_REQ;
  man_entry->payload_size = sizeof(UMI_SET_ANTENNAS);

  umi_set_antennas = (UMI_SET_ANTENNAS *)(man_entry->payload);
  memset (umi_set_antennas, 0, sizeof(UMI_SET_ANTENNAS));
  umi_set_antennas->RxAntsMask = (uint8) mask;
  umi_set_antennas->TxAntsMask = (uint8) mask;

  res = mtlk_txmm_msg_send_blocked(&umi_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);

  if (MTLK_ERR_OK != res) {
    ELOG_DD("CID-%04x: Sending MC_MAN_SET_ANTENNAS_REQ failed (res = %d)", mtlk_vap_get_oid(vap_handle), res);
  }
  else {
    WAVE_RADIO_PDB_SET_INT(radio, PARAM_DB_RADIO_ACTIVE_ANT_MASK, mask);
    wave_radio_current_antenna_mask_set(radio, mask);
  }

  mtlk_txmm_msg_cleanup(&umi_msg);
  return res;
}

static int
_mtlk_core_cfg_store_channel_prev (mtlk_core_t *core, const mtlk_channel_prev_cfg_t *data)
{
  wave_radio_t *radio = wave_vap_radio_get(core->vap_handle);
  int res = WAVE_RADIO_PDB_SET_BINARY(radio, PARAM_DB_RADIO_CHANNEL_PREV, data, sizeof(*data));

  if (MTLK_ERR_OK != res) {
    ELOG_D("CID-%04x: Failed to store Previous Channel Configuration into Parameter DB.",
           mtlk_vap_get_oid(core->vap_handle));
  }
  return res;
}

static int
_mtlk_core_cfg_read_channel_prev (mtlk_core_t *core, mtlk_channel_prev_cfg_t *data)
{
  int res;
  mtlk_pdb_size_t data_size = sizeof(*data);
  wave_radio_t *radio = wave_vap_radio_get(core->vap_handle);

  res = WAVE_RADIO_PDB_GET_BINARY(radio, PARAM_DB_RADIO_CHANNEL_PREV, data, &data_size);
  MTLK_ASSERT(sizeof(*data) == data_size);

  if (MTLK_ERR_OK != res) {
    ELOG_D("CID-%04x: Failed to read Previous Channel Configuration from Parameter DB.",
           mtlk_vap_get_oid(core->vap_handle));
  }
  return res;
}

static void
_mtlk_core_cfg_reset_channel_prev (mtlk_core_t *core)
{
  mtlk_channel_prev_cfg_t mtlk_channel_prev_cfg;

  /* Reset configuration of previously set channel in parameter data base */
  mtlk_channel_prev_cfg.pri_chan_num = MTLK_CHANNEL_PREV_CFG_PRI_NUM_DEFAULT;
  mtlk_channel_prev_cfg.sec_chan_freq = 0;
  _mtlk_core_cfg_store_channel_prev(core, &mtlk_channel_prev_cfg);
}

static int
_mtlk_core_cfg_set_channel_prev_on_coex_en (mtlk_core_t *core, struct mtlk_chan_def *ccd)
{
  mtlk_channel_prev_cfg_t mtlk_channel_prev_cfg;

  MTLK_ASSERT(ccd->center_freq1 != ccd->chan.center_freq);

  /* Save configuration of currently set channel in parameter data base */
  mtlk_channel_prev_cfg.pri_chan_num = (uint16)ieee80211_frequency_to_channel(ccd->chan.center_freq);
  mtlk_channel_prev_cfg.sec_chan_freq = (uint16)ccd->center_freq1;
  return _mtlk_core_cfg_store_channel_prev(core, &mtlk_channel_prev_cfg);
}

static __INLINE void
__mtlk_core_cfg_prepare_csa_info_on_set_chan_for_coex (mtlk_core_t *core, struct set_chan_param_data *cpd)
{
#define MTLK_CSA_COUNT_DEFAULT  5
  memset(cpd, 0, sizeof(*cpd));
  /* Change channel's switch type to "Channel's Switch Announcement" */
  cpd->switch_type = ST_CSA;
  /* Set a time to wait until channel's switch */
  cpd->chan_switch_time = (uint16)(MTLK_CSA_COUNT_DEFAULT *
                                   WAVE_RADIO_PDB_GET_INT(wave_vap_radio_get(core->vap_handle),
                                                          PARAM_DB_RADIO_BEACON_PERIOD));
#undef MTLK_CSA_COUNT_DEFAULT
}

static void
_mtlk_core_cfg_restore_chan_width_on_coex_dis (mtlk_core_t *core)
{
  struct mtlk_chan_def *ccd = __wave_core_chandef_get(core);
  uint16 oid;

  oid = mtlk_vap_get_oid(core->vap_handle);
  ILOG1_D("CID-%04x: 2.4GHz Multi-Radio Co-Existence is deactivated.", oid);

  /* Must be done for 2.4GHz channels of 20MHz width only */
  if ((MTLK_HW_BAND_2_4_GHZ == core_cfg_get_freq_band_cur(core)) &&
      (is_channel_certain(ccd)) && (CW_20 == ccd->width)) {
    uint16 pri_chan_num_curr;
    uint16 pri_chan_num_prev;
    mtlk_channel_prev_cfg_t mtlk_channel_prev_cfg;

    /* Must be done if network mode wasn't changed while Multi-Radio Co-Existence was active */
    if (!is_ht_net_mode(core_cfg_get_network_mode(core))) {
      WLOG_D("CID-%04x: Failed to restore 2.4GHz channel's width to 40MHz "
             "because network mode was changed to non-HT while Multi-Radio Co-Existence was active.", oid);
      _mtlk_core_cfg_reset_channel_prev(core);
      return;
    }

    /* Get configuration of previously set channel from parameter data base */
    if (MTLK_ERR_OK != _mtlk_core_cfg_read_channel_prev(core, &mtlk_channel_prev_cfg))
    {
      WLOG_D("CID-%04x: Failed to restore 2.4GHz channel's width to 40MHz "
             "because can't read parameters from DB.", oid);
      _mtlk_core_cfg_reset_channel_prev(core);
      return;
    }
    pri_chan_num_prev = mtlk_channel_prev_cfg.pri_chan_num;

    /* Must be done if channel's width was changed during activation of Multi-Radio Co-Existence */
    if (MTLK_CHANNEL_PREV_CFG_PRI_NUM_DEFAULT != pri_chan_num_prev) {
      /* Must be done if channel number wasn't changed while Multi-Radio Co-Existence was active */
      pri_chan_num_curr = (uint16)mtlk_core_get_channel(core);
      if (pri_chan_num_curr == pri_chan_num_prev) {
        struct mtlk_chan_def ccd_new;
        struct set_chan_param_data cpd_new;
        mtlk_df_t *df = mtlk_vap_get_df(core->vap_handle);
        mtlk_df_user_t *df_user = mtlk_df_get_user(df);
        struct net_device *ndev = mtlk_df_user_get_ndev(df_user);

        /* Make a copy of channel's current definition */
        ccd_new = *ccd;

        /* Change channel's width to 40MHz */
        ccd_new.width = CW_40;
        /* Change center frequency #1 */
        ccd_new.center_freq1 = (uint32)mtlk_channel_prev_cfg.sec_chan_freq;

        /* Prepare information for channel's switch with "Channel's Switch Announcement" */
        __mtlk_core_cfg_prepare_csa_info_on_set_chan_for_coex(core, &cpd_new);

        if (MTLK_ERR_OK == core_cfg_send_set_chan(core, &ccd_new, &cpd_new)) {
          ILOG1_D("CID-%04x: 2.4GHz channel's width is restored to 40MHz.", oid);
          /* Notify the Hostapd that channel's width is changed */
          wv_cfg80211_ch_switch_notify(ndev, &ccd_new, FALSE);
        }
        else {
          ELOG_D("CID-%04x: Failed to restore 2.4GHz channel's width to 40MHz "
                 "because can't send message to Firmware.", oid);
        }
      }
      else {
        WLOG_DDD("CID-%04x: Failed to restore 2.4GHz channel's width to 40MHz "
                 "because channel was changed (%d -> %d) while Multi-Radio Co-Existence was active.",
                 oid, pri_chan_num_prev, pri_chan_num_curr);
      }
      _mtlk_core_cfg_reset_channel_prev(core);
    }
  }
}

static int
_mtlk_core_cfg_change_chan_width_on_coex_en (mtlk_core_t *core)
{
  struct mtlk_chan_def *ccd = __wave_core_chandef_get(core);
  int res = MTLK_ERR_OK;
  uint16 oid;

  oid = mtlk_vap_get_oid(core->vap_handle);

  ILOG1_D("CID-%04x: 2.4GHz Multi-Radio Co-Existence is activated.", oid);

  /* Must be done for 2.4GHz channels of 40MHz width only */
  if ((MTLK_HW_BAND_2_4_GHZ == core_cfg_get_freq_band_cur(core)) &&
      (is_channel_certain(ccd)) && (CW_40 == ccd->width)) {
    struct mtlk_chan_def ccd_new;
    struct set_chan_param_data cpd_new;
    mtlk_df_t *df = mtlk_vap_get_df(core->vap_handle);
    mtlk_df_user_t *df_user = mtlk_df_get_user(df);
    struct net_device *ndev = mtlk_df_user_get_ndev(df_user);

    /* Make a copy of channel's current definition */
    ccd_new = *ccd;

    res = _mtlk_core_cfg_set_channel_prev_on_coex_en(core, &ccd_new);
    if (MTLK_ERR_OK != res) {
      ELOG_D("CID-%04x: Failed to change 2.4GHz channel's width to 20MHz "
             "because can't store parameters into DB.", oid);
      return res;
    }

    /* Change channel's width to 20MHz */
    ccd_new.width = CW_20;
    /* Change center frequency #1 to center frequency of primary channel */
    ccd_new.center_freq1 = ccd_new.chan.center_freq;

    /* Prepare information for channel's switch with "Channel's Switch Announcement" */
    __mtlk_core_cfg_prepare_csa_info_on_set_chan_for_coex(core, &cpd_new);

    if (MTLK_ERR_OK == core_cfg_send_set_chan(core, &ccd_new, &cpd_new)) {
      ILOG1_D("CID-%04x: 2.4GHz channel's width is changed to 20MHz.", oid);
      /* Notify the Hostapd that channel's width is changed */
      wv_cfg80211_ch_switch_notify(ndev, &ccd_new, FALSE);
    }
    else {
      ELOG_D("CID-%04x: Failed to change 2.4GHz channel's width to 20MHz "
             "because can't send message to Firmware.", oid);
    }
  }
  return res;
}

static int
_mtlk_core_cfg_try_to_change_chan_width_on_coex_en (mtlk_core_t *core, uint8 coex_mode)
{
  int res = _mtlk_core_cfg_change_chan_width_on_coex_en(core);

  if (MTLK_ERR_OK != res) {
    /* Disable Co-Existence due to error */
    res = mtlk_core_send_coex_config_req(core, coex_mode, 0);
    if (MTLK_ERR_OK == res) {
      ELOG_D("CID-%04x: Failed to enable Multi-Radio Co-Existence "
             "because 2.4GHz channel's width is 40MHz.", mtlk_vap_get_oid(core->vap_handle));
      WAVE_RADIO_PDB_SET_INT(wave_vap_radio_get(core->vap_handle), PARAM_DB_RADIO_COEX_ENABLED, 0);
      res = MTLK_ERR_BUSY;
    }
  }
  return res;
}

void __MTLK_IFUNC
core_cfg_change_chan_width_if_coex_en (mtlk_core_t *core, struct mtlk_chan_def *ccd)
{
  /* It's not allowed to set a channel's bandwidth to 40MHz in case if
     it's 2.4GHz channel and Multi-Radio Co-Existence is enabled */
  if ((MTLK_HW_BAND_2_4_GHZ == core_cfg_get_freq_band_cur(core)) && (CW_40 == ccd->width)) {
    if (WAVE_RADIO_PDB_GET_INT(wave_vap_radio_get(core->vap_handle), PARAM_DB_RADIO_COEX_ENABLED)) {
      ccd->width = CW_20;
      ccd->center_freq1 = ccd->chan.center_freq;
      WLOG_V("Can't set 2.4GHz channel's width to 40MHz due to active Multi-Radio Co-Existence. "
             "20MHz width will be used instead.");
    }
  }
}

int __MTLK_IFUNC
mtlk_core_send_coex_config_req (mtlk_core_t *core, uint8 coex_mode, uint8 coex_enable)
{
  mtlk_txmm_msg_t     man_msg;
  mtlk_txmm_data_t   *man_entry;
  UMI_SET_2_4_G_COEX *coex_cfg;
  int                 res;
  unsigned            oid;

  MTLK_ASSERT(core != NULL);
  oid = mtlk_vap_get_oid(core->vap_handle);

  if (MTLK_ERR_OK != mtlk_core_is_band_supported(core, UMI_PHY_TYPE_BAND_2_4_GHZ)) {
    ELOG_D("CID-%04x: MR Coex only supported in 2.4 GHz", mtlk_vap_get_oid(core->vap_handle));
    return MTLK_ERR_NOT_SUPPORTED;
  }

  /* allocate a new message */
  man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, mtlk_vap_get_txmm(core->vap_handle), NULL);
  if (!man_entry) {
    ELOG_D("CID-%04x: Can not get TXMM slot", oid);
    return MTLK_ERR_NO_RESOURCES;
  }

  /* fill the message data */
  man_entry->id = UM_MAN_2_4GHZ_COEX_REQ;
  man_entry->payload_size = sizeof(UMI_SET_2_4_G_COEX);
  coex_cfg = (UMI_SET_2_4_G_COEX *)man_entry->payload;
  memset(coex_cfg, 0, sizeof(*coex_cfg));

  coex_cfg->coExQos =         coex_mode;
  coex_cfg->bCoExActive =     coex_enable;

  /* send the message to FW */
  res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);
  if (MTLK_ERR_OK != res) {
    ELOG_DD("CID-%04x: Sending of 2.4 GHz Coex config failed (%i)", oid, res);
  }

  /* cleanup the message */
  mtlk_txmm_msg_cleanup(&man_msg);
  return res;
}

static int
_mtlk_core_store_and_send_coex_mode (mtlk_core_t *core, uint8 coex_mode)
{
  int res;
  uint8 coex_enabled = (uint8)WAVE_RADIO_PDB_GET_INT(wave_vap_radio_get(core->vap_handle), PARAM_DB_RADIO_COEX_ENABLED);

  if (MTLK_ERR_OK != mtlk_core_is_band_supported(core, UMI_PHY_TYPE_BAND_2_4_GHZ)) {
    ELOG_D("CID-%04x: MR Coex only supported in 2.4 GHz", mtlk_vap_get_oid(core->vap_handle));
    return MTLK_ERR_NOT_SUPPORTED;
  }

  if (CO_EX_QOS_NUM_OF_TYPES <= coex_mode) {
    ELOG_DD("Wrong 2.4 GHz Coex QoS mode given (%u), must be in range from 0 to %u",
           coex_mode, CO_EX_QOS_NUM_OF_TYPES - 1);
    return MTLK_ERR_PARAMS;
  }

  ILOG1_D("Updating GHz Coex QoS mode to %u", coex_mode);
  WAVE_RADIO_PDB_SET_INT(wave_vap_radio_get(core->vap_handle), PARAM_DB_RADIO_COEX_MODE, coex_mode);

  if (!coex_enabled) {
    ELOG_V("2.4 GHz Coexistance is not enabled in driver");
    return MTLK_ERR_UNKNOWN;
  }

  res = mtlk_core_send_coex_config_req(core, coex_mode, coex_enabled);

  if (MTLK_ERR_OK == res) {
    /* Must be done in case of Multi-Radio Co-Existence activation only */
    res = _mtlk_core_cfg_try_to_change_chan_width_on_coex_en(core, coex_mode);
  }
  return res;
}

static int
_mtlk_core_store_and_enable_coex (mtlk_core_t *core, uint8 coex_new_state)
{
  int res;
  wave_radio_t *radio = wave_vap_radio_get(core->vap_handle);
  uint8 coex_curr_state = (uint8)WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_COEX_ENABLED);
  uint8 coex_mode       = (uint8)WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_COEX_MODE);

  if (MTLK_ERR_OK != mtlk_core_is_band_supported(core, UMI_PHY_TYPE_BAND_2_4_GHZ)) {
    ELOG_D("CID-%04x: MR Coex only supported in 2.4 GHz", mtlk_vap_get_oid(core->vap_handle));
    return MTLK_ERR_NOT_SUPPORTED;
  }

  if ((FALSE != coex_new_state) && (TRUE  != coex_new_state)) {
    ELOG_D("Wrong 2.4 GHz coex enable state (%u), must be 0 or 1", coex_new_state);
    return MTLK_ERR_PARAMS;
  }

  if (coex_curr_state == coex_new_state) {
    WLOG_S("2.4 GHz coexistance is already %s", coex_curr_state ? "enabled" : "disabled");
    return MTLK_ERR_OK;
  }

  WAVE_RADIO_PDB_SET_INT(radio, PARAM_DB_RADIO_COEX_ENABLED, coex_new_state);

  res = mtlk_core_send_coex_config_req(core, coex_mode, coex_new_state);

  if (MTLK_ERR_OK == res) {
    if (coex_new_state) {
      /* Must be done in case of Multi-Radio Co-Existence activation only */
      res = _mtlk_core_cfg_try_to_change_chan_width_on_coex_en(core, coex_mode);
    }
    else {
      /* Must be done in case of Multi-Radio Co-Existence deactivation only */
      _mtlk_core_cfg_restore_chan_width_on_coex_dis(core);
    }
  }
  return res;
}

int __MTLK_IFUNC
mtlk_core_set_coex_cfg (mtlk_handle_t hcore, const void *data, uint32 data_size)
{
  int res = MTLK_ERR_OK;
  mtlk_core_t *core = (mtlk_core_t *) hcore;
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;
  mtlk_coex_cfg_t *coex_cfg = NULL;
  uint32 coex_cfg_size;

  MTLK_ASSERT(core != NULL);
  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  /* get configuration */
  coex_cfg = mtlk_clpb_enum_get_next(clpb, &coex_cfg_size);
  MTLK_CLPB_TRY(coex_cfg, coex_cfg_size)
    MTLK_CFG_START_CHEK_ITEM_AND_CALL()
      MTLK_CFG_CHECK_ITEM_AND_CALL(coex_cfg, coex_mode, _mtlk_core_store_and_send_coex_mode,
                                   (core, coex_cfg->coex_mode), res);
      MTLK_CFG_CHECK_ITEM_AND_CALL(coex_cfg, coex_enable, _mtlk_core_store_and_enable_coex,
                                   (core, coex_cfg->coex_enable), res);
    MTLK_CFG_END_CHEK_ITEM_AND_CALL()
  MTLK_CLPB_FINALLY(res)
    /* push result into clipboard */
    return mtlk_clpb_push_res(clpb, res);
  MTLK_CLPB_END
}

int __MTLK_IFUNC
mtlk_core_receive_coex_config (mtlk_core_t *core, uint8 *enabled, uint8 *mode)
{
  mtlk_txmm_msg_t     man_msg;
  mtlk_txmm_data_t   *man_entry;
  UMI_SET_2_4_G_COEX *coex_cfg;
  int                 res;
  unsigned            oid;

  oid = mtlk_vap_get_oid(core->vap_handle);
  if (MTLK_ERR_OK != mtlk_core_is_band_supported(core, UMI_PHY_TYPE_BAND_2_4_GHZ)) {
    ELOG_D("CID-%04x: MR Coex only supported in 2.4 GHz", mtlk_vap_get_oid(core->vap_handle));
    return MTLK_ERR_NOT_SUPPORTED;
  }

  man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, mtlk_vap_get_txmm(core->vap_handle), NULL);
  if (!man_entry) {
    ELOG_D("CID-%04x: Can not get TXMM slot", oid);
    return MTLK_ERR_NO_RESOURCES;
  }

  man_entry->id = UM_MAN_2_4GHZ_COEX_REQ;
  man_entry->payload_size = sizeof(UMI_SET_2_4_G_COEX);
  coex_cfg = (UMI_SET_2_4_G_COEX *)man_entry->payload;
  coex_cfg->getSetOperation = API_GET_OPERATION;

  res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);
  if (MTLK_ERR_OK == res) {
    *mode    = coex_cfg->coExQos;
    *enabled = coex_cfg->bCoExActive;
  }
  else {
    ELOG_D("CID-%04x: reeiving UM_MAN_2_4GHZ_COEX_REQ failed", oid);
  }

  mtlk_txmm_msg_cleanup(&man_msg);
  return res;
}

int __MTLK_IFUNC
mtlk_core_get_coex_cfg (mtlk_handle_t hcore, const void *data, uint32 data_size)
{
  int res = MTLK_ERR_OK;
  uint8 enabled, mode;
  mtlk_coex_cfg_t coex_cfg;
  mtlk_core_t *core = (mtlk_core_t*)hcore;
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;

  MTLK_ASSERT(core != NULL);
  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  if (MTLK_ERR_OK != mtlk_core_is_band_supported(core, UMI_PHY_TYPE_BAND_2_4_GHZ)) {
    ELOG_D("CID-%04x: MR Coex only supported in 2.4 GHz", mtlk_vap_get_oid(core->vap_handle));
    res = MTLK_ERR_NOT_SUPPORTED;
  }
  else {
    res = mtlk_core_receive_coex_config(core, &enabled, &mode);
    if (MTLK_ERR_OK == res) {
      MTLK_CFG_SET_ITEM(&coex_cfg, coex_mode, mode);
      MTLK_CFG_SET_ITEM(&coex_cfg, coex_enable, enabled);
    }
  }

  res = mtlk_clpb_push(clpb, &res, sizeof(res));
  if (MTLK_ERR_OK == res) {
    res = mtlk_clpb_push(clpb, &coex_cfg, sizeof(coex_cfg));
  }

  return res;
}

/* ================ Multicast range processing ================== */
static int
_mtlk_core_cfg_setup_mcast_range (mtlk_core_t *core, mtlk_mcast_range_t *mcast_range)
{
  MTLK_ASSERT(mcast_range->action < MTLK_MCAST_ACTION_LAST);
  switch (mcast_range->action) {
    case  MTLK_MCAST_ACTION_CLEANUP:
      return mtlk_mc_ranges_cleanup(core, mcast_range->netmask.type);
    case  MTLK_MCAST_ACTION_ADD:
      return mtlk_mc_ranges_add(core, &mcast_range->netmask);
    case  MTLK_MCAST_ACTION_DEL:
      return mtlk_mc_ranges_del(core, &mcast_range->netmask);
    default:
      return  MTLK_ERR_PARAMS;
  }
}

int __MTLK_IFUNC
mtlk_core_cfg_set_mcast_range (mtlk_handle_t hcore, const void* data, uint32 data_size)
{
  int                        res = MTLK_ERR_OK;
  uint32                     cfg_size;
  mtlk_core_t                *core = (mtlk_core_t*)hcore;
  mtlk_mcast_range_cfg_t     *mcast_range_cfg;
  mtlk_clpb_t                *clpb = *(mtlk_clpb_t **)data;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);
  MTLK_ASSERT(core);

  /* get configuration */
  mcast_range_cfg = mtlk_clpb_enum_get_next(clpb, &cfg_size);
  MTLK_CLPB_TRY(mcast_range_cfg, cfg_size)
    MTLK_CFG_START_CHEK_ITEM_AND_CALL()
      MTLK_CFG_CHECK_ITEM_AND_CALL(mcast_range_cfg, range,
         _mtlk_core_cfg_setup_mcast_range, (core, &mcast_range_cfg->range), res);
    MTLK_CFG_END_CHEK_ITEM_AND_CALL()
  MTLK_CLPB_FINALLY(res)
    /* push result into clipboard */
    return mtlk_clpb_push_res(clpb, res);
  MTLK_CLPB_END
}

static int
_mtlk_core_cfg_get_mcast_range_list (mtlk_handle_t hcore, const void* data, uint32 data_size, ip_type_t ip_type)
{
  uint32 res = MTLK_ERR_OK;
  mtlk_mcast_range_vect_cfg_t mcast_range_vect_cfg;
  mtlk_core_t *core = (mtlk_core_t*)hcore;
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);
  MTLK_ASSERT(core);

  memset(&mcast_range_vect_cfg, 0, sizeof(mcast_range_vect_cfg));

  MTLK_CFG_SET_ITEM_BY_FUNC(&mcast_range_vect_cfg, range_vect,
      mtlk_mc_ranges_get_vect, (core, ip_type, &mcast_range_vect_cfg), res);

  res = mtlk_clpb_push(clpb, &res, sizeof(res));
  if (MTLK_ERR_OK == res) {
    res = mtlk_clpb_push(clpb, &mcast_range_vect_cfg, sizeof(mcast_range_vect_cfg));
  }

  return res;
}

int __MTLK_IFUNC
mtlk_core_cfg_get_mcast_range_list_ipv4 (mtlk_handle_t hcore, const void* data, uint32 data_size)
{
  return _mtlk_core_cfg_get_mcast_range_list(hcore, data, data_size, MTLK_IPv4);
}

int __MTLK_IFUNC
mtlk_core_cfg_get_mcast_range_list_ipv6 (mtlk_handle_t hcore, const void* data, uint32 data_size)
{
  return _mtlk_core_cfg_get_mcast_range_list(hcore, data, data_size, MTLK_IPv6);
}

void mtlk_core_cfg_flush_wds_wpa_list (mtlk_core_t *nic)
{
  _mtlk_core_flush_ieee_addr_list(nic, &nic->wds_wpa_sta_list, "WDS WPA");
}

BOOL mtlk_core_cfg_wds_wpa_entry_exists (mtlk_core_t *nic, const IEEE_ADDR *addr)
{
  return _mtlk_core_ieee_addr_entry_exists(nic, addr, &nic->wds_wpa_sta_list, "WDS WPA");
}

int mtlk_core_cfg_add_wds_wpa_entry (mtlk_core_t* nic, const IEEE_ADDR *addr)
{
  return _mtlk_core_add_ieee_addr_entry(nic, addr, &nic->wds_wpa_sta_list, "WDS WPA");
}

int mtlk_core_cfg_del_wds_wpa_entry (mtlk_core_t* nic, const IEEE_ADDR *addr)
{
  return _mtlk_core_del_ieee_addr_entry(nic, addr, &nic->wds_wpa_sta_list, "WDS WPA",
    TRUE);
}

int mtlk_core_cfg_get_wds_wpa_entry(mtlk_handle_t hcore, const void* data,
  uint32 data_size)
{
  mtlk_core_t *core = HANDLE_T_PTR(mtlk_core_t, hcore);

  return _mtlk_core_dump_ieee_addr_list (core, &core->wds_wpa_sta_list,
    "WDS WPA", data, data_size);
}

int mtlk_core_cfg_get_multi_ap_blacklist_entries(mtlk_handle_t hcore,
  const void* data, uint32 data_size)
{
  mtlk_core_t *core = HANDLE_T_PTR(mtlk_core_t, hcore);

  return _mtlk_core_dump_ieee_addr_list (core, &core->multi_ap_blacklist,
    "Multi-AP black", data, data_size);
}

/************* Max MPDU length ******************/

uint32 mtlk_core_cfg_read_max_mpdu_length (mtlk_core_t *core)
{
    MTLK_ASSERT(core != NULL);
    return WAVE_RADIO_PDB_GET_INT(wave_vap_radio_get(core->vap_handle), PARAM_DB_RADIO_MAX_MPDU_LENGTH);
}

static void
_mtlk_core_store_max_mpdu_length (mtlk_core_t *core, const uint32 max_mpdu_length)
{
    MTLK_ASSERT(core != NULL);
    WAVE_RADIO_PDB_SET_INT(wave_vap_radio_get(core->vap_handle), PARAM_DB_RADIO_MAX_MPDU_LENGTH, max_mpdu_length);
}

int __MTLK_IFUNC
mtlk_core_cfg_receive_max_mpdu_length (mtlk_core_t *core, uint32 *max_mpdu_length)
{
    mtlk_txmm_msg_t         man_msg;
    mtlk_txmm_data_t        *man_entry;
    UMI_MAX_MPDU            *mac_msg;
    int                     res;
    unsigned                oid;


    oid = mtlk_vap_get_oid(core->vap_handle);
    man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, mtlk_vap_get_txmm(core->vap_handle), NULL);
    if (!man_entry) {
      ELOG_D("CID-%04x: Can not get TXMM slot", oid);
      return MTLK_ERR_NO_RESOURCES;
    }

    man_entry->id = UM_MAN_MAX_MPDU_LENGTH_REQ;
    man_entry->payload_size = sizeof(UMI_MAX_MPDU);
    mac_msg = (UMI_MAX_MPDU *)man_entry->payload;
    mac_msg->getSetOperation = API_GET_OPERATION;

    res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);

    if (MTLK_ERR_OK == res) {
      *max_mpdu_length = MAC_TO_HOST32(mac_msg->maxMpduLength);
    }
    else {
      ELOG_D("CID-%04x: Receiving UM_MAN_MAX_MPDU_LENGTH_REQ failed", oid);
    }

    mtlk_txmm_msg_cleanup(&man_msg);
    return res;
}

int __MTLK_IFUNC
mtlk_core_cfg_send_max_mpdu_length (mtlk_core_t *core, const uint32 max_mpdu_length)
{
    mtlk_txmm_msg_t         man_msg;
    mtlk_txmm_data_t        *man_entry;
    UMI_MAX_MPDU            *mac_msg;
    int                     res;
    int                     net_state;
    unsigned                oid;

    MTLK_ASSERT(core != NULL);
    oid = mtlk_vap_get_oid(core->vap_handle);
    net_state = mtlk_core_get_net_state(core);

    if (NET_STATE_READY != net_state) {
      /* command can be issued only if interface is not in "up" state */
      WLOG_DS("CID-%04x: Invalid card state %s - request rejected", oid, mtlk_net_state_to_string(net_state));
      return MTLK_ERR_NOT_READY;
    }

    ILOG2_DD("CID-%04x: Set MAX MPDU LENGTH to %u", oid, max_mpdu_length);

    /* allocate a new message */
    man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, mtlk_vap_get_txmm(core->vap_handle), NULL);
    if (!man_entry)
    {
      ELOG_D("CID-%04x: Can not get TXMM slot", oid);
      return MTLK_ERR_NO_RESOURCES;
    }

    /* fill the message data */
    man_entry->id = UM_MAN_MAX_MPDU_LENGTH_REQ;
    man_entry->payload_size = sizeof(UMI_MAX_MPDU);
    mac_msg = (UMI_MAX_MPDU *)man_entry->payload;
    memset(mac_msg, 0, sizeof(*mac_msg));
    mac_msg->maxMpduLength = HOST_TO_MAC32(max_mpdu_length);

    /* send the message to FW */
    res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);

    /* New structure does not contain field status */

    /* cleanup the message */
    mtlk_txmm_msg_cleanup(&man_msg);

    return res;
}


int __MTLK_IFUNC
mtlk_core_cfg_set_max_mpdu_length (mtlk_handle_t hcore, const void *data, uint32 data_size)
{
    int res = MTLK_ERR_OK;
    mtlk_core_t *core = (mtlk_core_t *) hcore;
    mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;
    mtlk_max_mpdu_len_cfg_t *max_mpdu_len_cfg = NULL;
    uint32 max_mpdu_len_cfg_size;

    MTLK_ASSERT(core != NULL);
    MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

    /* get configuration */
    max_mpdu_len_cfg = mtlk_clpb_enum_get_next(clpb, &max_mpdu_len_cfg_size);
    MTLK_CLPB_TRY(max_mpdu_len_cfg, max_mpdu_len_cfg_size)
      MTLK_CFG_START_CHEK_ITEM_AND_CALL()
        /* send new config to FW */
        MTLK_CFG_CHECK_ITEM_AND_CALL(max_mpdu_len_cfg, max_mpdu_length, mtlk_core_cfg_send_max_mpdu_length,
                                     (core, max_mpdu_len_cfg->max_mpdu_length), res);
        /* store new config in internal db*/
        MTLK_CFG_CHECK_ITEM_AND_CALL_VOID(max_mpdu_len_cfg, max_mpdu_length, _mtlk_core_store_max_mpdu_length,
                                          (core, max_mpdu_len_cfg->max_mpdu_length));
      MTLK_CFG_END_CHEK_ITEM_AND_CALL()
    MTLK_CLPB_FINALLY(res)
      /* push result into clipboard */
      return mtlk_clpb_push_res(clpb, res);
    MTLK_CLPB_END
}

int __MTLK_IFUNC
mtlk_core_cfg_get_max_mpdu_length (mtlk_handle_t hcore, const void *data, uint32 data_size)
{
    int res = MTLK_ERR_OK;
    mtlk_max_mpdu_len_cfg_t max_mpdu_len_cfg;
    mtlk_core_t *core = (mtlk_core_t*)hcore;
    mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;

    MTLK_ASSERT(core != NULL);
    MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

    /* read config from internal db */
    MTLK_CFG_SET_ITEM_BY_FUNC(&max_mpdu_len_cfg, max_mpdu_length, mtlk_core_cfg_receive_max_mpdu_length,
                              (core, &max_mpdu_len_cfg.max_mpdu_length), res);

    /* push result and config to clipboard*/
    res = mtlk_clpb_push(clpb, &res, sizeof(res));
    if (MTLK_ERR_OK == res) {
        res = mtlk_clpb_push(clpb, &max_mpdu_len_cfg, sizeof(max_mpdu_len_cfg));
    }

    return res;
}

/* Frequency Jump Mode Configuration */

int __MTLK_IFUNC
mtlk_core_cfg_send_freq_jump_mode (mtlk_core_t *core, uint32 freq_jump_mode)
{
    mtlk_txmm_msg_t             man_msg;
    mtlk_txmm_data_t            *man_entry;
    UMI_ENABLE_FREQUENCY_JUMP_t *mac_msg;
    int                         res;
    uint16                      oid;

    MTLK_ASSERT(core != NULL);
    MTLK_ASSERT(core == mtlk_core_get_master(core));

    oid = mtlk_vap_get_oid(core->vap_handle);
    ILOG2_DD("CID-%04x: Set FREQUENCY JUMP MODE to %d", oid, freq_jump_mode);

    /* allocate a new message */
    man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, mtlk_vap_get_txmm(core->vap_handle), NULL);
    if (!man_entry) {
      ELOG_D("CID-%04x: Can not get TXMM slot", oid);
      return MTLK_ERR_NO_RESOURCES;
    }

    /* fill the message data */
    man_entry->id = UM_MAN_FREQ_JUMP_MODE_REQ;
    man_entry->payload_size = sizeof(UMI_ENABLE_FREQUENCY_JUMP_t);
    mac_msg = (UMI_ENABLE_FREQUENCY_JUMP_t *)man_entry->payload;
    memset(mac_msg, 0, sizeof(*mac_msg));

    mac_msg->u32FreqJumpOn = HOST_TO_MAC32(freq_jump_mode);

    /* send the message to FW */
    res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);
    if (MTLK_ERR_OK != res) {
      ELOG_DD("CID-%04x: Send FREQUENCY JUMP MODE failed (%i)", oid, res);
    }

    /* cleanup the message */
    mtlk_txmm_msg_cleanup(&man_msg);
    return res;
}

static int _mtlk_core_cfg_check_freq_jump_mode (mtlk_core_t *core, uint32 freq_jump_mode)
{
    uint16 oid;
    uint32 freq_jump_mode_old =
       WAVE_RADIO_PDB_GET_INT(wave_vap_radio_get(core->vap_handle), PARAM_DB_RADIO_FREQ_JUMP_MODE);

    oid = mtlk_vap_get_oid(core->vap_handle);
    if (freq_jump_mode_old == freq_jump_mode) {
      ELOG_D("CID-%04x: Set FREQUENCY JUMP MODE failed, this value set already", oid);
      return MTLK_ERR_PARAMS;
    }
    return MTLK_ERR_OK;
}

int __MTLK_IFUNC
mtlk_core_cfg_set_freq_jump_mode (mtlk_handle_t hcore, const void *data, uint32 data_size)
{
    int res = MTLK_ERR_OK;
    mtlk_core_t *core = (mtlk_core_t *) hcore;
    mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;
    mtlk_freq_jump_mode_cfg_t *freq_jump_mode_cfg = NULL;
    uint32 freq_jump_mode_cfg_size;
    uint16 oid;

    MTLK_ASSERT(core != NULL);
    MTLK_ASSERT(core == mtlk_core_get_master(core));
    MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);
    oid = mtlk_vap_get_oid(core->vap_handle);

    /* get configuration */
    freq_jump_mode_cfg = mtlk_clpb_enum_get_next(clpb, &freq_jump_mode_cfg_size);
    MTLK_CLPB_TRY(freq_jump_mode_cfg, freq_jump_mode_cfg_size)
      MTLK_CFG_START_CHEK_ITEM_AND_CALL()
        /* check new config */
        MTLK_CFG_CHECK_ITEM_AND_CALL(freq_jump_mode_cfg, freq_jump_mode, _mtlk_core_cfg_check_freq_jump_mode,
                                     (core, freq_jump_mode_cfg->freq_jump_mode), res);
        /* store new config in internal DB */
        MTLK_CFG_CHECK_ITEM_AND_CALL_VOID(freq_jump_mode_cfg, freq_jump_mode, WAVE_RADIO_PDB_SET_INT,
                                          (wave_vap_radio_get(core->vap_handle), PARAM_DB_RADIO_FREQ_JUMP_MODE, freq_jump_mode_cfg->freq_jump_mode));
        /* send new config to FW */
        MTLK_CFG_CHECK_ITEM_AND_CALL(freq_jump_mode_cfg, freq_jump_mode, mtlk_core_cfg_send_freq_jump_mode,
                                     (core, freq_jump_mode_cfg->freq_jump_mode), res);
      MTLK_CFG_END_CHEK_ITEM_AND_CALL()
    MTLK_CLPB_FINALLY(res)
      /* push result into clipboard */
      return mtlk_clpb_push_res(clpb, res);
    MTLK_CLPB_END
}

int __MTLK_IFUNC
mtlk_core_cfg_set_wds_wep_enc_cfg (mtlk_handle_t hcore, const void* data, uint32 data_size)
{
  mtlk_core_t *nic = HANDLE_T_PTR(mtlk_core_t, hcore);
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;
  mtlk_core_ui_enc_cfg_t   *enc_cfg = NULL;
  uint32                    enc_cfg_size;
  MIB_WEP_KEY              *enc_cfg_wep_key;
  int                       res = MTLK_ERR_OK;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  enc_cfg = mtlk_clpb_enum_get_next(clpb, &enc_cfg_size);
  MTLK_CLPB_TRY(enc_cfg, enc_cfg_size)
    if ((mtlk_core_get_net_state(nic) & (NET_STATE_READY | NET_STATE_ACTIVATING | NET_STATE_CONNECTED)) == 0) {
      ILOG1_D("CID-%04x: Invalid card state - request rejected", mtlk_vap_get_oid(nic->vap_handle));
      MTLK_CLPB_EXIT(MTLK_ERR_NOT_READY);
    }

  /* New key always passed in array's slot 0 */
    enc_cfg_wep_key = &enc_cfg->wep_keys.sKey[0];

    /* check the Cipher suite */
    if(MIB_WEP_KEY_WEP2_LENGTH == enc_cfg_wep_key->u8KeyLength) { /* 104 bit */
      nic->slow_ctx->wds_keys[enc_cfg->key_id].cipher = UMI_RSN_CIPHER_SUITE_WEP104;
    }
    else if (MIB_WEP_KEY_WEP1_LENGTH == enc_cfg_wep_key->u8KeyLength) { /* 40 bit */
      nic->slow_ctx->wds_keys[enc_cfg->key_id].cipher = UMI_RSN_CIPHER_SUITE_WEP40;
    }
    else {
     nic->slow_ctx->wds_keys[enc_cfg->key_id].cipher = UMI_RSN_CIPHER_SUITE_NONE;
    }

    wave_memcpy(nic->slow_ctx->wds_keys[enc_cfg->key_id].key,
        sizeof(nic->slow_ctx->wds_keys[enc_cfg->key_id].key),
        enc_cfg_wep_key->au8KeyData, sizeof(enc_cfg_wep_key->au8KeyData));
    nic->slow_ctx->wds_keys[enc_cfg->key_id].key_len = enc_cfg_wep_key->u8KeyLength;

    ILOG1_DD("CID-%04x: Key index:%d", mtlk_vap_get_oid(nic->vap_handle), enc_cfg->key_id);
    mtlk_dump(1, enc_cfg_wep_key->au8KeyData, sizeof(enc_cfg_wep_key->au8KeyData), "KEY:");

  MTLK_CLPB_FINALLY(res)
    /* Don't need to push result to clipboard */
    return res;
  MTLK_CLPB_END;
}

int __MTLK_IFUNC
mtlk_core_cfg_get_wds_wep_enc_cfg (mtlk_handle_t hcore, const void* data, uint32 data_size)
{
  int                       res = MTLK_ERR_OK;
  mtlk_core_t               *nic = HANDLE_T_PTR(mtlk_core_t, hcore);
  mtlk_clpb_t               *clpb = *(mtlk_clpb_t **) data;
  mtlk_core_ui_enc_cfg_t    enc_cfg;
  int                       i;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  if ((mtlk_core_get_net_state(nic) & (NET_STATE_READY | NET_STATE_ACTIVATING | NET_STATE_CONNECTED)) == 0) {
    ILOG1_D("CID-%04x: Invalid card state - request rejected", mtlk_vap_get_oid(nic->vap_handle));
    res = MTLK_ERR_NOT_READY;
    goto FINISH;
  }

  memset(&enc_cfg, 0, sizeof(enc_cfg));

  /* set wep enable flag */
  if(nic->slow_ctx->peerAPs_key_idx) {
    enc_cfg.wep_enabled = 1;
  }

  ILOG1_DD("CID-%04x: WEP enabled:%d", mtlk_vap_get_oid(nic->vap_handle), enc_cfg.wep_enabled);

  /* copy wds wep keys */
  for (i = 0; i < MIB_WEP_N_DEFAULT_KEYS; i++) {

    wave_memcpy(&enc_cfg.wep_keys.sKey[i].au8KeyData, sizeof(enc_cfg.wep_keys.sKey[0].au8KeyData),
        nic->slow_ctx->wds_keys[i].key, sizeof(enc_cfg.wep_keys.sKey[0].au8KeyData));
    enc_cfg.wep_keys.sKey[i].u8KeyLength = nic->slow_ctx->wds_keys[i].key_len;

    ILOG1_DD("CID-%04x: Key_len:%d", mtlk_vap_get_oid(nic->vap_handle), nic->slow_ctx->wds_keys[i].key_len);
    mtlk_dump(1, &enc_cfg.wep_keys.sKey[i].au8KeyData, sizeof(enc_cfg.wep_keys.sKey[0].au8KeyData), "KEY:");
  }

  if (nic->slow_ctx->peerAPs_key_idx) {
    enc_cfg.key_id = nic->slow_ctx->peerAPs_key_idx - 1; /* peerAP keys from 1 till 4 */
  }

  res = mtlk_clpb_push(clpb, &enc_cfg, sizeof(enc_cfg));
  if (MTLK_ERR_OK != res) {
    mtlk_clpb_purge(clpb);
  }

FINISH:
  return res;
}

BOOL core_cfg_equal_range(struct mtlk_chan_def *chan1, struct mtlk_chan_def *chan2)
{
  return (chan1->width == chan2->width
        && chan1->center_freq1 == chan2->center_freq1
        && chan1->center_freq2 == chan2->center_freq2);
}

BOOL core_cfg_channels_overlap(struct mtlk_chan_def *chandef1,
  struct mtlk_chan_def *chandef2)
{
#define CORE_CFG_OVERLAP_CHECK(cf1, hw1, cf2, hw2, ret)     \
  do {                                                      \
    if ((cf1 <= cf2) && (cf1 + hw1 > cf2 - hw2)) {          \
      ret = TRUE;                                           \
      break;                                                \
    }                                                       \
    if ((cf1 > cf2) && (cf1 - hw1 < cf2 + hw2)) {           \
      ret = TRUE;                                           \
      break;                                                \
    }                                                       \
    ret = FALSE;                                            \
  } while (0);

  uint32 half_width1, half_width2;
  int low_chan_num1, low_chan_num2;
  BOOL ret = FALSE;

  MTLK_ASSERT(chandef1 != NULL);
  MTLK_ASSERT(chandef2 != NULL);

  low_chan_num1 = freq2lowchannum(chandef1->center_freq1, chandef1->width);
  low_chan_num2 = freq2lowchannum(chandef2->center_freq1, chandef2->width);
  half_width1 = mtlkcw2cw(chandef1->width) >> 1;
  half_width2 = mtlkcw2cw(chandef2->width) >> 1;
  CORE_CFG_OVERLAP_CHECK(chandef1->center_freq1, half_width1,
    chandef2->center_freq1, half_width2, ret);
  if (ret)
    goto out;

  if (chandef1->center_freq2 != 0) {
    CORE_CFG_OVERLAP_CHECK(chandef1->center_freq2, half_width1,
      chandef2->center_freq1, half_width2, ret);
    if (ret)
      goto out;
    if (chandef2->center_freq2 != 0) {
      CORE_CFG_OVERLAP_CHECK(chandef1->center_freq2, half_width1,
        chandef2->center_freq2, half_width2, ret);
      if (ret)
        goto out;
    }
  }
  if (chandef2->center_freq2 != 0) {
    CORE_CFG_OVERLAP_CHECK(chandef1->center_freq1, half_width1,
      chandef2->center_freq2, half_width2, ret);
  }

out:
  ILOG2_DDDDDDS("Channels %d--%d (width %u) and %d--%d (width %u) %s",
    low_chan_num1,
    low_chan_num1 + (CHANNUMS_PER_20MHZ << chandef1->width) - CHANNUMS_PER_20MHZ,
    mtlkcw2cw(chandef1->width),
    low_chan_num2,
    low_chan_num2 + (CHANNUMS_PER_20MHZ << chandef2->width) - CHANNUMS_PER_20MHZ,
    mtlkcw2cw(chandef2->width),
    ret ? "overlap" : "don't overlap");

  return ret;
}

int __MTLK_IFUNC
mtlk_core_cfg_set_operating_mode (mtlk_handle_t hcore, const void *data, uint32 data_size)
{

  mtlk_txmm_msg_t        man_msg;
  mtlk_txmm_data_t       *man_entry = NULL;
  OperatingMode_t        *pOperatingMode = NULL;
  mtlk_core_t            *core = (mtlk_core_t *) hcore;
  mtlk_vap_handle_t      vap_handle = core->vap_handle;
  mtlk_operating_mode_t  *operating_mode = NULL;
  uint32                  operating_mode_cfg_size;
  mtlk_clpb_t            *clpb = *(mtlk_clpb_t **) data;
  int                    res = MTLK_ERR_OK;

  ILOG0_V("mtlk_core_cfg_set_operating_mode");

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  operating_mode = mtlk_clpb_enum_get_next(clpb, &operating_mode_cfg_size);
  MTLK_CLPB_TRY(operating_mode, operating_mode_cfg_size)
    man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, mtlk_vap_get_txmm(vap_handle), NULL);
    if (NULL == man_entry) {
      ELOG_D("CID-%04x: No man entry available", mtlk_vap_get_oid(vap_handle));
      MTLK_CLPB_EXIT(MTLK_ERR_NO_RESOURCES);
    }

    man_entry->id = UM_MAN_OPERATING_MODE_REQ;
    man_entry->payload_size = sizeof(OperatingMode_t);

    pOperatingMode = (OperatingMode_t *)(man_entry->payload);
    pOperatingMode->channelWidth = operating_mode->channel_width;
    pOperatingMode->rxNss = operating_mode->rx_nss;
    pOperatingMode->stationId = HOST_TO_MAC16(operating_mode->station_id);

    res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);
    if (MTLK_ERR_OK != res) {
      ELOG_DD("CID-%04x: operating mode set failure (%i)", mtlk_vap_get_oid(vap_handle), res);
    }

    mtlk_txmm_msg_cleanup(&man_msg);
  MTLK_CLPB_FINALLY(res)
    /* Don't need to push result to clipboard */
    return res;
  MTLK_CLPB_END
}


int __MTLK_IFUNC mtlk_core_cfg_set_wds_wpa_entry (mtlk_handle_t hcore, const void* data,
  uint32 data_size)
{
  mtlk_core_t *nic = HANDLE_T_PTR(mtlk_core_t, hcore);
  int res = MTLK_ERR_OK;
  struct intel_vendor_mac_addr_list_cfg *addrlist_cfg = NULL;
  uint32 addrlist_cfg_size;
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **)data;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  /* get configuration */
  addrlist_cfg = mtlk_clpb_enum_get_next(clpb, &addrlist_cfg_size);
  MTLK_CLPB_TRY(addrlist_cfg, addrlist_cfg_size)
    if (addrlist_cfg->remove)
      res = mtlk_core_cfg_del_wds_wpa_entry(nic, &addrlist_cfg->addr);
    else
      res = mtlk_core_cfg_add_wds_wpa_entry(nic, &addrlist_cfg->addr);
  MTLK_CLPB_FINALLY(res)
    /* push result into clipboard */
    return mtlk_clpb_push_res(clpb, res);
  MTLK_CLPB_END
}

/* Restricted AC Mode Configuration */

static int
_mtlk_core_receive_restricted_ac_mode (mtlk_core_t *core, UMI_SET_RESTRICTED_AC *restricted_ac_mode)
{
  mtlk_txmm_msg_t             man_msg;
  mtlk_txmm_data_t            *man_entry;
  UMI_SET_RESTRICTED_AC       *mac_msg;
  int                         res;
  uint16                      oid;

  oid = mtlk_vap_get_oid(core->vap_handle);
  man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, mtlk_vap_get_txmm(core->vap_handle), NULL);
  if (!man_entry) {
    ELOG_D("CID-%04x: Can not get TXMM slot", oid);
    return MTLK_ERR_NO_RESOURCES;
  }

  man_entry->id = UM_MAN_RESTRICTED_AC_MODE_REQ;
  man_entry->payload_size = sizeof(UMI_SET_RESTRICTED_AC);
  mac_msg = (UMI_SET_RESTRICTED_AC *)man_entry->payload;
  mac_msg->getSetOperation = API_GET_OPERATION;

  res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);
  if (MTLK_ERR_OK == res) {
    restricted_ac_mode->restrictedAcThreshEnter = MAC_TO_HOST16(mac_msg->restrictedAcThreshEnter);
    restricted_ac_mode->restrictedAcThreshExit  = MAC_TO_HOST16(mac_msg->restrictedAcThreshExit);
    restricted_ac_mode->restrictedAcModeEnable  = mac_msg->restrictedAcModeEnable;
    restricted_ac_mode->acRestrictedBitmap      = mac_msg->acRestrictedBitmap;
  }
  else {
    ELOG_D("CID-%04x: Receiving UM_MAN_RESTRICTED_AC_MODE_REQ failed", oid);
  }

  mtlk_txmm_msg_cleanup(&man_msg);
  return res;
}

int __MTLK_IFUNC
mtlk_core_cfg_send_restricted_ac_mode (mtlk_core_t *core,
        const UMI_SET_RESTRICTED_AC *restricted_ac_mode)
{
  mtlk_txmm_msg_t             man_msg;
  mtlk_txmm_data_t            *man_entry;
  UMI_SET_RESTRICTED_AC       *mac_msg;
  int                         res;
  uint16                      oid;

  MTLK_ASSERT(core != NULL);
  MTLK_ASSERT(core == mtlk_core_get_master(core));

  oid = mtlk_vap_get_oid(core->vap_handle);
  ILOG2_D("CID-%04x: Send RESTRICTED AC MODE", oid);

  /* allocate a new message */
  man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, mtlk_vap_get_txmm(core->vap_handle), NULL);
  if (!man_entry) {
    ELOG_D("CID-%04x: Can not get TXMM slot", oid);
    return MTLK_ERR_NO_RESOURCES;
  }

  /* fill the message data */
  man_entry->id = UM_MAN_RESTRICTED_AC_MODE_REQ;
  man_entry->payload_size = sizeof(UMI_SET_RESTRICTED_AC);
  mac_msg = (UMI_SET_RESTRICTED_AC *)man_entry->payload;
  memset(mac_msg, 0, sizeof(*mac_msg));

  mac_msg->restrictedAcThreshEnter = HOST_TO_MAC16(restricted_ac_mode->restrictedAcThreshEnter);
  mac_msg->restrictedAcThreshExit = HOST_TO_MAC16(restricted_ac_mode->restrictedAcThreshExit);
  mac_msg->restrictedAcModeEnable = restricted_ac_mode->restrictedAcModeEnable;
  mac_msg->acRestrictedBitmap = restricted_ac_mode->acRestrictedBitmap;

  /* send the message to FW */
  res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);
  if (MTLK_ERR_OK != res) {
    ELOG_DD("CID-%04x: Send RESTRICTED AC MODE failed (%i)", oid, res);
  }

  /* cleanup the message */
  mtlk_txmm_msg_cleanup(&man_msg);
  return res;
}

static int
_mtlk_core_receive_pd_threshold (mtlk_core_t *core, UMI_SET_PD_THRESH *pd_thresh)
{
  mtlk_txmm_msg_t             man_msg;
  mtlk_txmm_data_t            *man_entry;
  UMI_SET_PD_THRESH           *mac_msg;
  int                         res;
  uint16                      oid;


  oid = mtlk_vap_get_oid(core->vap_handle);
  man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, mtlk_vap_get_txmm(core->vap_handle), NULL);
  if (!man_entry) {
    ELOG_D("CID-%04x: Can not get TXMM slot", oid);
    return MTLK_ERR_NO_RESOURCES;
  }

  man_entry->id = UM_MAN_QOS_PD_THRESH_REQ;
  man_entry->payload_size = sizeof(UMI_SET_PD_THRESH);
  mac_msg = (UMI_SET_PD_THRESH *)man_entry->payload;
  mac_msg->getSetOperation = API_GET_OPERATION;

  res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);
  if (MTLK_ERR_OK == res) {
    pd_thresh->minPdDiff   = MAC_TO_HOST16(mac_msg->minPdDiff);
    pd_thresh->mode        = mac_msg->mode;
    pd_thresh->minPdAmount = mac_msg->minPdAmount;
  }
  else {
    ELOG_D("CID-%04x: Receiving UM_MAN_QOS_PD_THRESH_REQ failed", oid);
  }

  mtlk_txmm_msg_cleanup(&man_msg);
  return res;
}

int __MTLK_IFUNC
mtlk_core_cfg_send_pd_threshold (mtlk_core_t *core, const UMI_SET_PD_THRESH *pd_thresh)
{
  mtlk_txmm_msg_t             man_msg;
  mtlk_txmm_data_t            *man_entry;
  UMI_SET_PD_THRESH           *mac_msg;
  int                         res;
  uint16                      oid;

  MTLK_ASSERT(core != NULL);
  MTLK_ASSERT(core == mtlk_core_get_master(core));

  oid = mtlk_vap_get_oid(core->vap_handle);
  ILOG2_D("CID-%04x: Send PD THRESHOLD", oid);

  /* allocate a new message */
  man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, mtlk_vap_get_txmm(core->vap_handle), NULL);
  if (!man_entry) {
    ELOG_D("CID-%04x: Can not get TXMM slot", oid);
    return MTLK_ERR_NO_RESOURCES;
  }

  /* fill the message data */
  man_entry->id = UM_MAN_QOS_PD_THRESH_REQ;
  man_entry->payload_size = sizeof(UMI_SET_PD_THRESH);
  mac_msg = (UMI_SET_PD_THRESH *)man_entry->payload;
  memset(mac_msg, 0, sizeof(*mac_msg));

  mac_msg->minPdDiff = HOST_TO_MAC16(pd_thresh->minPdDiff);
  mac_msg->mode = pd_thresh->mode;
  mac_msg->minPdAmount = pd_thresh->minPdAmount;

  /* send the message to FW */
  res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);
  if (MTLK_ERR_OK != res) {
    ELOG_DD("CID-%04x: Send PD THRESHOLD failed (%i)", oid, res);
  }

  /* cleanup the message */
  mtlk_txmm_msg_cleanup(&man_msg);
  return res;
}

void __MTLK_IFUNC
mtlk_core_cfg_get_restricted_ac_mode (mtlk_core_t *core,
        UMI_SET_RESTRICTED_AC *restricted_ac_mode)
{
  mtlk_pdb_size_t data_size = sizeof(*restricted_ac_mode);
  WAVE_RADIO_PDB_GET_BINARY(wave_vap_radio_get(core->vap_handle),
                            PARAM_DB_RADIO_RESTRICTED_AC_MODE, restricted_ac_mode, &data_size);
}

void __MTLK_IFUNC
mtlk_core_cfg_get_pd_threshold (mtlk_core_t *core, UMI_SET_PD_THRESH *pd_thresh)
{
  mtlk_pdb_size_t data_size = sizeof(*pd_thresh);
  WAVE_RADIO_PDB_GET_BINARY(wave_vap_radio_get(core->vap_handle),
                            PARAM_DB_RADIO_PD_THRESHOLD, pd_thresh, &data_size);
}

static int
_mtlk_core_cfg_check_restricted_ac_mode (mtlk_core_t *core,
        const UMI_SET_RESTRICTED_AC *restricted_ac_mode)
{
  uint16 oid = mtlk_vap_get_oid(core->vap_handle);

  MTLK_UNREFERENCED_PARAM(oid);

  if (restricted_ac_mode->restrictedAcThreshEnter > MTLK_RESTRICTED_AC_MODE_THRESH_EXIT_MAX) {
    ELOG_DDD("CID-%04x: Invalid ThresholdEnter value (%d). Allowed values are 0...%d.",
            oid, restricted_ac_mode->restrictedAcThreshEnter, MTLK_RESTRICTED_AC_MODE_THRESH_EXIT_MAX);
    return MTLK_ERR_VALUE;
  }
  if (restricted_ac_mode->restrictedAcThreshExit > MTLK_RESTRICTED_AC_MODE_THRESH_EXIT_MAX) {
    ELOG_DDD("CID-%04x: Invalid ThresholdExit value (%d). Allowed values are 0...%d.",
            oid, restricted_ac_mode->restrictedAcThreshExit, MTLK_RESTRICTED_AC_MODE_THRESH_EXIT_MAX);
    return MTLK_ERR_VALUE;
  }
  return MTLK_ERR_OK;
}

static int
_mtlk_core_cfg_send_and_store_restricted_ac_mode (mtlk_core_t *core,
        const UMI_SET_RESTRICTED_AC *restricted_ac_mode)
{
  int res;
  uint16 oid;

  oid = mtlk_vap_get_oid(core->vap_handle);
  /* check parameters */
  res = _mtlk_core_cfg_check_restricted_ac_mode(core, restricted_ac_mode);

  if (MTLK_ERR_OK == res) {
    /* send new config to FW */
    res = mtlk_core_cfg_send_restricted_ac_mode(core, restricted_ac_mode);
    if (MTLK_ERR_OK == res) {
      /* store new config into internal DB */
      res = WAVE_RADIO_PDB_SET_BINARY(wave_vap_radio_get(core->vap_handle),
                                      PARAM_DB_RADIO_RESTRICTED_AC_MODE,
                                      restricted_ac_mode, sizeof(*restricted_ac_mode));
      if (MTLK_ERR_OK != res) {
        ELOG_DD("CID-%04x: Store RESTRICTED AC MODE failed (%i)", oid, res);
      }
    }
  }
  return res;
}

static int
_mtlk_core_cfg_send_and_store_pd_threshold (mtlk_core_t *core,
        const UMI_SET_PD_THRESH *pd_thresh)
{
  /* send new config to FW */
  int res = mtlk_core_cfg_send_pd_threshold(core, pd_thresh);

  if (MTLK_ERR_OK == res) {
    /* store new config into internal DB */
    res = WAVE_RADIO_PDB_SET_BINARY(wave_vap_radio_get(core->vap_handle),
                                    PARAM_DB_RADIO_PD_THRESHOLD,
                                    pd_thresh, sizeof(*pd_thresh));
    if (MTLK_ERR_OK != res) {
      ELOG_DD("CID-%04x: Store PD THRESHOLD failed (%i)",
              mtlk_vap_get_oid(core->vap_handle), res);
    }
  }
  return res;
}

int __MTLK_IFUNC
mtlk_core_cfg_set_restricted_ac_mode_cfg (mtlk_handle_t hcore, const void *data, uint32 data_size)
{
  uint32 restricted_ac_mode_cfg_size;
  int res = MTLK_ERR_OK;
  mtlk_core_t *core = (mtlk_core_t *) hcore;
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;
  mtlk_restricted_ac_mode_cfg_t *restricted_ac_mode_cfg = NULL;

  MTLK_ASSERT(core != NULL);
  MTLK_ASSERT(core == mtlk_core_get_master(core));
  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  /* get configuration */
  restricted_ac_mode_cfg = mtlk_clpb_enum_get_next(clpb, &restricted_ac_mode_cfg_size);
  MTLK_CLPB_TRY(restricted_ac_mode_cfg, restricted_ac_mode_cfg_size)
    /* send configuration */
    MTLK_CFG_START_CHEK_ITEM_AND_CALL()
      MTLK_CFG_CHECK_ITEM_AND_CALL(restricted_ac_mode_cfg, pd_thresh_params, _mtlk_core_cfg_send_and_store_pd_threshold,
                                   (core, &restricted_ac_mode_cfg->pd_thresh_params), res);
      MTLK_CFG_CHECK_ITEM_AND_CALL(restricted_ac_mode_cfg, ac_mode_params, _mtlk_core_cfg_send_and_store_restricted_ac_mode,
                                   (core, &restricted_ac_mode_cfg->ac_mode_params), res);
    MTLK_CFG_END_CHEK_ITEM_AND_CALL()
  MTLK_CLPB_FINALLY(res)
    /* push result into clipboard */
    return mtlk_clpb_push_res(clpb, res);
  MTLK_CLPB_END
}

int __MTLK_IFUNC
mtlk_core_cfg_get_restricted_ac_mode_cfg (mtlk_handle_t hcore, const void *data, uint32 data_size)
{
  mtlk_restricted_ac_mode_cfg_t restricted_ac_mode_cfg;
  int res = MTLK_ERR_OK;
  mtlk_core_t *core = (mtlk_core_t *) hcore;
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;

  MTLK_ASSERT(core != NULL);
  MTLK_ASSERT(core == mtlk_core_get_master(core));
  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  memset(&restricted_ac_mode_cfg, 0, sizeof(restricted_ac_mode_cfg));

  MTLK_CFG_SET_ITEM_BY_FUNC(&restricted_ac_mode_cfg, pd_thresh_params,
                            _mtlk_core_receive_pd_threshold, (core, &restricted_ac_mode_cfg.pd_thresh_params), res);
  MTLK_CFG_SET_ITEM_BY_FUNC(&restricted_ac_mode_cfg, ac_mode_params,
                            _mtlk_core_receive_restricted_ac_mode, (core, &restricted_ac_mode_cfg.ac_mode_params), res);

  /* push result into clipboard */
  res = mtlk_clpb_push(clpb, &res, sizeof(res));
  if (MTLK_ERR_OK == res) {
    res = mtlk_clpb_push(clpb, &restricted_ac_mode_cfg, sizeof(restricted_ac_mode_cfg));
  }
  return res;
}

int __MTLK_IFUNC mtlk_core_cfg_set_dgaf_disabled (mtlk_handle_t hcore, const void* data,
  uint32 data_size)
{
  mtlk_core_t *nic = HANDLE_T_PTR(mtlk_core_t, hcore);
  int res = MTLK_ERR_OK;
  uint32 *dgaf_disabled = NULL;
  uint32 dgaf_disabled_size;
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **)data;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  /* get configuration */
  dgaf_disabled = mtlk_clpb_enum_get_next(clpb, &dgaf_disabled_size);
  MTLK_CLPB_TRY(dgaf_disabled, dgaf_disabled_size)
    nic->dgaf_disabled = *dgaf_disabled == 0 ? FALSE : TRUE;
  MTLK_CLPB_FINALLY(res)
    /* push result into clipboard */
    return mtlk_clpb_push_res(clpb, res);
  MTLK_CLPB_END
}

static int
_mtlk_core_send_fixed_ltf_and_gi (mtlk_core_t *core, const UMI_FIXED_LTF_AND_GI_REQ_t *req)
{
    mtlk_txmm_msg_t            man_msg;
    mtlk_txmm_data_t           *man_entry;
    UMI_FIXED_LTF_AND_GI_REQ_t *mac_msg;
    int                        res;
    unsigned                   oid;

    MTLK_ASSERT(core != NULL);
    oid = mtlk_vap_get_oid(core->vap_handle);

    ILOG0_DDD("CID-%04x: Fixed LTF and GI - isAuto:(%d) ltfAndGi:(%d)", oid, req->isAuto, req->ltfAndGi);

    /* Allocate a new message */
    man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, mtlk_vap_get_txmm(core->vap_handle), NULL);
    if (!man_entry) {
      ELOG_D("CID-%04x: Can not get TXMM slot", oid);
      return MTLK_ERR_NO_RESOURCES;
    }

    /* Fill the message data */
    man_entry->id = UM_MAN_FIXED_LTF_AND_GI_REQ;
    man_entry->payload_size = sizeof(*mac_msg);
    mac_msg = (UMI_FIXED_LTF_AND_GI_REQ_t *)man_entry->payload;
    mac_msg->isAuto = req->isAuto;
    mac_msg->ltfAndGi = req->ltfAndGi;

    /* Send the message to FW */
    res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);

    /* Cleanup the message */
    mtlk_txmm_msg_cleanup(&man_msg);

    return res;
}

static int
_mtlk_core_cfg_store_fixed_ltf_and_gi (mtlk_core_t *core,
        const UMI_FIXED_LTF_AND_GI_REQ_t *fixed_ltf_and_gi)
{
  int res;
  /* store new config into internal DB */
  res = WAVE_RADIO_PDB_SET_BINARY(wave_vap_radio_get(core->vap_handle),
                                  PARAM_DB_RADIO_FIXED_LTF_AND_GI,
                                  fixed_ltf_and_gi, sizeof(*fixed_ltf_and_gi));
  if (MTLK_ERR_OK != res) {
    ELOG_DD("CID-%04x: Store Fixed LTF and GI failed (%i)",
            mtlk_vap_get_oid(core->vap_handle), res);
  }
  return res;
}


void __MTLK_IFUNC
mtlk_core_cfg_get_fixed_ltf_and_gi (mtlk_core_t *core, UMI_FIXED_LTF_AND_GI_REQ_t *fixed_ltf_and_gi)
{
  mtlk_pdb_size_t data_size = sizeof(*fixed_ltf_and_gi);
  WAVE_RADIO_PDB_GET_BINARY(wave_vap_radio_get(core->vap_handle),
                            PARAM_DB_RADIO_FIXED_LTF_AND_GI, fixed_ltf_and_gi, &data_size);
}

int __MTLK_IFUNC
mtlk_core_set_fixed_ltf_and_gi (mtlk_handle_t hcore, const void *data, uint32 data_size)
{
    int res = MTLK_ERR_OK;
    mtlk_core_t *core = (mtlk_core_t*)hcore;
    mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;
    mtlk_fixed_ltf_and_gi_t *fixed_ltf_and_gi = NULL;
    uint32 cfg_size;

    MTLK_ASSERT(core != NULL);
    MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

    /* get new configuration and send it to FW */
    fixed_ltf_and_gi = mtlk_clpb_enum_get_next(clpb, &cfg_size);
    MTLK_CLPB_TRY(fixed_ltf_and_gi, cfg_size)
      MTLK_CFG_START_CHEK_ITEM_AND_CALL()
         MTLK_CFG_CHECK_ITEM_AND_CALL(fixed_ltf_and_gi, fixed_ltf_and_gi_params, _mtlk_core_send_fixed_ltf_and_gi,
                                      (core, &fixed_ltf_and_gi->fixed_ltf_and_gi_params), res);
         MTLK_CFG_CHECK_ITEM_AND_CALL(fixed_ltf_and_gi, fixed_ltf_and_gi_params, _mtlk_core_cfg_store_fixed_ltf_and_gi,
                                      (core, &fixed_ltf_and_gi->fixed_ltf_and_gi_params), res);
      MTLK_CFG_END_CHEK_ITEM_AND_CALL()
    MTLK_CLPB_FINALLY(res)
      return mtlk_clpb_push_res(clpb, res); /* push result into clipboard */
    MTLK_CLPB_END
}

int __MTLK_IFUNC
mtlk_core_get_fixed_ltf_and_gi (mtlk_handle_t hcore, const void *data, uint32 data_size)
{
    int res = MTLK_ERR_OK;
    mtlk_fixed_ltf_and_gi_t fixed_ltf_and_gi;
    mtlk_core_t *core = (mtlk_core_t*)hcore;
    mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;
    MTLK_ASSERT(core != NULL);
    MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

    memset(&fixed_ltf_and_gi, 0, sizeof(fixed_ltf_and_gi));
    MTLK_CFG_SET_ITEM_BY_FUNC_VOID(&fixed_ltf_and_gi, fixed_ltf_and_gi_params, mtlk_core_cfg_get_fixed_ltf_and_gi,
                                   (core, &fixed_ltf_and_gi.fixed_ltf_and_gi_params));


    /* push result and config into clipboard */
    res = mtlk_clpb_push(clpb, &res, sizeof(res));
    if (MTLK_ERR_OK == res) {
        res = mtlk_clpb_push(clpb, &fixed_ltf_and_gi, sizeof(fixed_ltf_and_gi));
    }
    return res;
}

int __MTLK_IFUNC
mtlk_core_set_mu_operation (mtlk_core_t *core, uint8 mu_operation)
{
  mtlk_txmm_msg_t                 man_msg;
  mtlk_txmm_data_t               *man_entry = NULL;
  UMI_MU_OPERATION_CONFIG        *mu_operation_config = NULL;
  mtlk_vap_handle_t               vap_handle = core->vap_handle;
  int                             res;
  wave_radio_t                   *radio = wave_vap_radio_get(vap_handle);

  ILOG2_V("Sending UMI_MU_OPERATION_CONFIG");

  man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, mtlk_vap_get_txmm(vap_handle), NULL);
  if (NULL == man_entry) {
    ELOG_D("CID-%04x: No man entry available", mtlk_vap_get_oid(vap_handle));
    return MTLK_ERR_NO_RESOURCES;
  }

  man_entry->id = UM_MAN_MU_OPERATION_CONFIG_REQ;
  man_entry->payload_size = sizeof(UMI_MU_OPERATION_CONFIG);

  mu_operation_config = (UMI_MU_OPERATION_CONFIG *)(man_entry->payload);
  mu_operation_config->enableMuOperation = mu_operation;

  res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);

  if (MTLK_ERR_OK != res) {
    ELOG_DD("CID-%04x: Set UMI_MU_OPERATION_CONFIG failed (res = %d)", mtlk_vap_get_oid(vap_handle), res);
  }
  else {
    WAVE_RADIO_PDB_SET_INT(radio, PARAM_DB_RADIO_MU_OPERATION, mu_operation);
  }

  mtlk_txmm_msg_cleanup(&man_msg);
  return res;
}

int __MTLK_IFUNC
mtlk_core_receive_mu_operation (mtlk_core_t *core, BOOL *mu_operation)
{
  mtlk_txmm_msg_t                 man_msg;
  mtlk_txmm_data_t               *man_entry = NULL;
  UMI_MU_OPERATION_CONFIG        *mu_operation_config = NULL;
  mtlk_vap_handle_t               vap_handle = core->vap_handle;
  int                             res;

  man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, mtlk_vap_get_txmm(vap_handle), NULL);
  if (NULL == man_entry) {
    ELOG_D("CID-%04x: No man entry available", mtlk_vap_get_oid(vap_handle));
    return MTLK_ERR_NO_RESOURCES;
  }

  man_entry->id = UM_MAN_MU_OPERATION_CONFIG_REQ;
  man_entry->payload_size = sizeof(UMI_MU_OPERATION_CONFIG);

  mu_operation_config = (UMI_MU_OPERATION_CONFIG *)(man_entry->payload);
  mu_operation_config->getSetOperation = API_GET_OPERATION;

  res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);

  if (MTLK_ERR_OK == res) {
    *mu_operation = mu_operation_config->enableMuOperation;
  }
  else {
    ELOG_D("CID-%04x: Receive UMI_MU_OPERATION_CONFIG failed", mtlk_vap_get_oid(vap_handle));
  }

  mtlk_txmm_msg_cleanup(&man_msg);
  return res;
}

int __MTLK_IFUNC
mtlk_core_set_rts_mode (mtlk_core_t *core, uint8 dynamic_bw, uint8 static_bw)
{
  mtlk_txmm_msg_t      man_msg;
  mtlk_txmm_data_t    *man_entry = NULL;
  UMI_RTS_MODE_CONFIG *rts_mode_config = NULL;
  mtlk_vap_handle_t    vap_handle = core->vap_handle;
  int                  res;
  wave_radio_t         *radio = wave_vap_radio_get(vap_handle);

  ILOG2_V("Sending UMI_RTS_MODE_CONFIG");

  man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, mtlk_vap_get_txmm(vap_handle), NULL);
  if (NULL == man_entry) {
    ELOG_D("CID-%04x: No man entry available", mtlk_vap_get_oid(vap_handle));
    return MTLK_ERR_NO_RESOURCES;
  }

  man_entry->id = UM_MAN_RTS_MODE_CONFIG_REQ;
  man_entry->payload_size = sizeof(UMI_RTS_MODE_CONFIG);

  rts_mode_config = (UMI_RTS_MODE_CONFIG *)(man_entry->payload);
  rts_mode_config->dynamicBw = dynamic_bw;
  rts_mode_config->staticBw = static_bw;

  res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);

  if (MTLK_ERR_OK != res) {
    ELOG_DD("CID-%04x: Set UMI_RTS_MODE_CONFIG failed (res = %d)", mtlk_vap_get_oid(vap_handle), res);
  }
  else {
    WAVE_RADIO_PDB_SET_INT(radio, PARAM_DB_RADIO_RTS_DYNAMIC_BW, dynamic_bw);
    WAVE_RADIO_PDB_SET_INT(radio, PARAM_DB_RADIO_RTS_STATIC_BW, static_bw);
  }

  mtlk_txmm_msg_cleanup(&man_msg);
  return res;
}

int __MTLK_IFUNC
mtlk_core_receive_rts_mode (mtlk_core_t *core, mtlk_core_rts_mode_t *rts_params)
{
  mtlk_txmm_msg_t      man_msg;
  mtlk_txmm_data_t    *man_entry = NULL;
  UMI_RTS_MODE_CONFIG *rts_mode_config = NULL;
  mtlk_vap_handle_t    vap_handle = core->vap_handle;
  int                  res;

  man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, mtlk_vap_get_txmm(vap_handle), NULL);
  if (NULL == man_entry) {
    ELOG_D("CID-%04x: No man entry available", mtlk_vap_get_oid(vap_handle));
    return MTLK_ERR_NO_RESOURCES;
  }

  man_entry->id = UM_MAN_RTS_MODE_CONFIG_REQ;
  man_entry->payload_size = sizeof(UMI_RTS_MODE_CONFIG);

  rts_mode_config = (UMI_RTS_MODE_CONFIG *)(man_entry->payload);
  rts_mode_config->getSetOperation = API_GET_OPERATION;

  res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);

  if (MTLK_ERR_OK == res) {
    rts_params->dynamic_bw = rts_mode_config->dynamicBw;
    rts_params->static_bw  = rts_mode_config->staticBw;
  }
  else {
    ELOG_D("CID-%04x: Receive UMI_RTS_MODE_CONFIG failed", mtlk_vap_get_oid(vap_handle));
  }

  mtlk_txmm_msg_cleanup(&man_msg);
  return res;
}

int __MTLK_IFUNC
mtlk_core_set_reliable_multicast (mtlk_core_t *core, uint8 flag)
{
  int                 res = MTLK_ERR_OK;
  mtlk_txmm_msg_t     man_msg;
  mtlk_txmm_data_t   *man_entry = NULL;
  UMI_MULTICAST_MODE *pMcastMode = NULL;
  mtlk_vap_handle_t   vap_handle = core->vap_handle;

  MTLK_ASSERT(vap_handle != 0);

  MTLK_CORE_PDB_SET_INT(core, PARAM_DB_CORE_RELIABLE_MCAST, flag);

  man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, mtlk_vap_get_txmm(vap_handle), NULL);
  if (NULL == man_entry) {
    ELOG_D("CID-%04x: No man entry available", mtlk_vap_get_oid(vap_handle));
    res = MTLK_ERR_NO_RESOURCES;
    goto FINISH;
  }

  man_entry->id = UM_MAN_SET_MULTICAST_MODE_REQ;
  man_entry->payload_size = sizeof(UMI_MULTICAST_MODE);

  pMcastMode = (UMI_MULTICAST_MODE *)(man_entry->payload);
  pMcastMode->u8Action = flag;
  pMcastMode->u8VapID = mtlk_vap_get_id(vap_handle);

  ILOG2_DSD("CID-%04x: Multicast FW request: Set %s mode for VapID %u",
            mtlk_vap_get_oid(vap_handle), flag ? "Reliable Multicast" : "Regular Multicast", pMcastMode->u8VapID);

  res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);
  if (MTLK_ERR_OK != res) {
    ELOG_DD("CID-%04x: Set Reliable Multicast failure (%i)", mtlk_vap_get_oid(vap_handle), res);
    goto FINISH;
  }

FINISH:
  if (man_entry) {
    mtlk_txmm_msg_cleanup(&man_msg);
  }

  return res;
}

int __MTLK_IFUNC
mtlk_core_receive_reliable_multicast (mtlk_core_t *core, uint8 *flag)
{
  int                 res = MTLK_ERR_OK;
  mtlk_txmm_msg_t     man_msg;
  mtlk_txmm_data_t   *man_entry = NULL;
  UMI_MULTICAST_MODE *pMcastMode = NULL;
  mtlk_vap_handle_t   vap_handle = core->vap_handle;

  man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, mtlk_vap_get_txmm(vap_handle), NULL);
  if (NULL == man_entry) {
    ELOG_D("CID-%04x: No man entry available", mtlk_vap_get_oid(vap_handle));
    return MTLK_ERR_NO_RESOURCES;
  }

  man_entry->id = UM_MAN_SET_MULTICAST_MODE_REQ;
  man_entry->payload_size = sizeof(UMI_MULTICAST_MODE);

  pMcastMode = (UMI_MULTICAST_MODE *)(man_entry->payload);
  pMcastMode->getSetOperation = API_GET_OPERATION;
  pMcastMode->u8VapID = mtlk_vap_get_id(vap_handle);

  res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);
  if (MTLK_ERR_OK == res) {
    *flag = pMcastMode->u8Action;
  }
  else {
    ELOG_D("CID-%04x: Receive UM_MAN_SET_MULTICAST_MODE_REQ failed", mtlk_vap_get_oid(vap_handle));
  }

  mtlk_txmm_msg_cleanup(&man_msg);
  return res;
}

/********** Reception Duty Cycle setting *********************/
static void
_mtlk_core_store_rx_duty_cycle (mtlk_core_t *core, const mtlk_rx_duty_cycle_cfg_t *rx_duty_cycle_cfg)
{
  wave_radio_t *radio = wave_vap_radio_get(core->vap_handle);

  MTLK_ASSERT(core != NULL);
  MTLK_ASSERT(rx_duty_cycle_cfg != NULL);

  WAVE_RADIO_PDB_SET_INT(radio, PARAM_DB_RADIO_RX_DUTY_CYCLE_ON_TIME,
                         rx_duty_cycle_cfg->duty_cycle.onTime);
  WAVE_RADIO_PDB_SET_INT(radio, PARAM_DB_RADIO_RX_DUTY_CYCLE_OFF_TIME,
                         rx_duty_cycle_cfg->duty_cycle.offTime);
}

int __MTLK_IFUNC
mtlk_core_set_rx_duty_cycle (mtlk_core_t *core, const mtlk_rx_duty_cycle_cfg_t *rx_duty_cycle_cfg)
{
  mtlk_txmm_msg_t    man_msg;
  mtlk_txmm_data_t  *man_entry;
  UMI_RX_DUTY_CYCLE *mac_msg;
  int                res;

  MTLK_ASSERT((rx_duty_cycle_cfg->duty_cycle.onTime<=32767) && (rx_duty_cycle_cfg->duty_cycle.offTime<=32767));

  ILOG2_DDD("CID-%04x: Set RX Duty Cycle. onTime: %u, offTime: %u",
          mtlk_vap_get_oid(core->vap_handle), rx_duty_cycle_cfg->duty_cycle.onTime, rx_duty_cycle_cfg->duty_cycle.offTime);

  /* allocate a new message */
  man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, mtlk_vap_get_txmm(core->vap_handle), NULL);
  if (!man_entry) {
      ELOG_D("CID-%04x: Can not get TXMM slot", mtlk_vap_get_oid(core->vap_handle));
      return MTLK_ERR_NO_RESOURCES;
  }

  /* fill the message data */
  man_entry->id = UM_MAN_SET_RX_DUTY_CYCLE_REQ;
  man_entry->payload_size = sizeof(UMI_RX_DUTY_CYCLE);
  mac_msg = (UMI_RX_DUTY_CYCLE *)man_entry->payload;
  mac_msg->onTime  = HOST_TO_MAC32(rx_duty_cycle_cfg->duty_cycle.onTime);
  mac_msg->offTime = HOST_TO_MAC32(rx_duty_cycle_cfg->duty_cycle.offTime);

  /* send the message to FW */
  res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);

  /* cleanup the message */
  mtlk_txmm_msg_cleanup(&man_msg);

  return res;
}

static int
_mtlk_core_receive_rx_duty_cycle (mtlk_core_t *core, UMI_RX_DUTY_CYCLE *rx_duty_cycle_cfg)
{
  mtlk_txmm_msg_t    man_msg;
  mtlk_txmm_data_t  *man_entry;
  UMI_RX_DUTY_CYCLE *mac_msg;
  int                res;

  man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, mtlk_vap_get_txmm(core->vap_handle), NULL);
  if (!man_entry) {
      ELOG_D("CID-%04x: Can not get TXMM slot", mtlk_vap_get_oid(core->vap_handle));
      return MTLK_ERR_NO_RESOURCES;
  }

  man_entry->id = UM_MAN_SET_RX_DUTY_CYCLE_REQ;
  man_entry->payload_size = sizeof(UMI_RX_DUTY_CYCLE);
  mac_msg = (UMI_RX_DUTY_CYCLE *)man_entry->payload;
  mac_msg->getSetOperation = API_GET_OPERATION;

  res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);

  if (MTLK_ERR_OK == res) {
    rx_duty_cycle_cfg->onTime  = MAC_TO_HOST32(mac_msg->onTime);
    rx_duty_cycle_cfg->offTime = MAC_TO_HOST32(mac_msg->offTime);
  }
  else {
    ELOG_D("CID-%04x: Receive UM_MAN_SET_RX_DUTY_CYCLE_REQ failed", mtlk_vap_get_oid(core->vap_handle));
  }

  mtlk_txmm_msg_cleanup(&man_msg);
  return res;
}

int __MTLK_IFUNC
mtlk_core_cfg_set_rx_duty_cycle (mtlk_handle_t hcore, const void* data, uint32 data_size)
{
  int res = MTLK_ERR_OK;
  mtlk_core_t *core = (mtlk_core_t *) hcore;

  mtlk_rx_duty_cycle_cfg_t *duty_cycle_cfg = NULL;
  uint32 duty_cycle_cfg_size;
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  /* get configuration */
  duty_cycle_cfg = mtlk_clpb_enum_get_next(clpb, &duty_cycle_cfg_size);
  MTLK_CLPB_TRY(duty_cycle_cfg, duty_cycle_cfg_size)
    MTLK_CFG_START_CHEK_ITEM_AND_CALL()
      /* send new config to FW */
      MTLK_CFG_CHECK_ITEM_AND_CALL(duty_cycle_cfg, duty_cycle, mtlk_core_set_rx_duty_cycle,
                                    (core, duty_cycle_cfg), res);
      /* store new config in internal db*/
      MTLK_CFG_CHECK_ITEM_AND_CALL_VOID(duty_cycle_cfg, duty_cycle, _mtlk_core_store_rx_duty_cycle,
                                        (core, duty_cycle_cfg));
    MTLK_CFG_END_CHEK_ITEM_AND_CALL()
  MTLK_CLPB_FINALLY(res)
    /* push result into clipboard */
    return mtlk_clpb_push_res(clpb, res);
  MTLK_CLPB_END
}

int __MTLK_IFUNC
mtlk_core_cfg_get_rx_duty_cycle (mtlk_handle_t hcore, const void* data, uint32 data_size)
{
  int res = MTLK_ERR_OK;
  mtlk_rx_duty_cycle_cfg_t rx_duty_cycle_cfg;
  mtlk_core_t *core = (mtlk_core_t*)hcore;
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  memset(&rx_duty_cycle_cfg, 0, sizeof(rx_duty_cycle_cfg));
  MTLK_CFG_SET_ITEM_BY_FUNC(&rx_duty_cycle_cfg, duty_cycle, _mtlk_core_receive_rx_duty_cycle,
                           (core, &rx_duty_cycle_cfg.duty_cycle), res);

  /* push result and config to clipboard*/
  res = mtlk_clpb_push(clpb, &res, sizeof(res));
  if (MTLK_ERR_OK == res) {
      res = mtlk_clpb_push(clpb, &rx_duty_cycle_cfg, sizeof(rx_duty_cycle_cfg));
  }

  return res;
}

/************** Set High Reception Threshold ****************/
static void
_mtlk_core_store_rx_threshold (mtlk_core_t *core, const mtlk_rx_th_cfg_t *rx_th_cfg)
{
  MTLK_ASSERT(core != NULL);
  MTLK_ASSERT(rx_th_cfg != NULL);

  MTLK_CORE_PDB_SET_INT(core, PARAM_DB_CORE_RX_THRESHOLD, rx_th_cfg->rx_threshold);
}

int __MTLK_IFUNC
mtlk_core_set_rx_threshold (mtlk_core_t *core, const mtlk_rx_th_cfg_t *rx_th_cfg)
{
  mtlk_txmm_msg_t   man_msg;
  mtlk_txmm_data_t *man_entry;
  UMI_RX_TH        *mac_msg;
  int               res;

  ILOG2_DD("CID-%04x: Set RX threshold: %d", mtlk_vap_get_oid(core->vap_handle), rx_th_cfg->rx_threshold);

  /* allocate a new message */
  man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, mtlk_vap_get_txmm(core->vap_handle), NULL);
  if (!man_entry)
  {
      ELOG_D("CID-%04x: Can not get TXMM slot", mtlk_vap_get_oid(core->vap_handle));
      return MTLK_ERR_NO_RESOURCES;
  }

  /* fill the message data */
  man_entry->id = UM_MAN_SET_RX_TH_REQ;
  man_entry->payload_size = sizeof(UMI_RX_TH);
  mac_msg = (UMI_RX_TH *)man_entry->payload;
  mac_msg->rxThValue = rx_th_cfg->rx_threshold;

  /* send the message to FW */
  res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);

  /* cleanup the message */
  mtlk_txmm_msg_cleanup(&man_msg);

  return res;
}

static int
_mtlk_core_receive_rx_threshold (mtlk_core_t *core, int8 *rx_thresh)
{
  mtlk_txmm_msg_t   man_msg;
  mtlk_txmm_data_t *man_entry;
  UMI_RX_TH        *mac_msg;
  int               res;

  man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, mtlk_vap_get_txmm(core->vap_handle), NULL);
  if (!man_entry) {
      ELOG_D("CID-%04x: Can not get TXMM slot", mtlk_vap_get_oid(core->vap_handle));
      return MTLK_ERR_NO_RESOURCES;
  }

  man_entry->id = UM_MAN_SET_RX_TH_REQ;
  man_entry->payload_size = sizeof(UMI_RX_TH);
  mac_msg = (UMI_RX_TH *)man_entry->payload;
  mac_msg->getSetOperation = API_GET_OPERATION;

  res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);

  if (MTLK_ERR_OK == res) {
    *rx_thresh = mac_msg->rxThValue;
  }
  else {
    ELOG_D("CID-%04x: Receive UM_MAN_SET_RX_TH_REQ failed", mtlk_vap_get_oid(core->vap_handle));
  }

  mtlk_txmm_msg_cleanup(&man_msg);
  return res;
}

int __MTLK_IFUNC
mtlk_core_cfg_set_rx_threshold (mtlk_handle_t hcore, const void* data, uint32 data_size)
{
  int res = MTLK_ERR_OK;
  mtlk_core_t *core = (mtlk_core_t *) hcore;

  mtlk_rx_th_cfg_t *rxth_cfg = NULL;
  uint32 rxth_cfg_size;
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  /* get configuration */
  rxth_cfg = mtlk_clpb_enum_get_next(clpb, &rxth_cfg_size);
  MTLK_CLPB_TRY(rxth_cfg, rxth_cfg_size)
    MTLK_CFG_START_CHEK_ITEM_AND_CALL()
      /* send new config to FW */
      MTLK_CFG_CHECK_ITEM_AND_CALL(rxth_cfg, rx_threshold, mtlk_core_set_rx_threshold,
                                    (core, rxth_cfg), res);
      /* store new config in internal db*/
      MTLK_CFG_CHECK_ITEM_AND_CALL_VOID(rxth_cfg, rx_threshold, _mtlk_core_store_rx_threshold,
                                        (core, rxth_cfg));
    MTLK_CFG_END_CHEK_ITEM_AND_CALL()
  MTLK_CLPB_FINALLY(res)
    /* push result into clipboard */
    return mtlk_clpb_push_res(clpb, res);
  MTLK_CLPB_END
}

int __MTLK_IFUNC
mtlk_core_cfg_get_rx_threshold (mtlk_handle_t hcore, const void* data, uint32 data_size)
{
  int res = MTLK_ERR_OK;
  mtlk_rx_th_cfg_t rxth_cfg;
  mtlk_core_t *core = (mtlk_core_t*)hcore;
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  /* read config from internal db */
  memset(&rxth_cfg, 0, sizeof(rxth_cfg));
  MTLK_CFG_SET_ITEM_BY_FUNC(&rxth_cfg, rx_threshold, _mtlk_core_receive_rx_threshold,
                            (core, &rxth_cfg.rx_threshold), res);

  /* push result and config to clipboard*/
  res = mtlk_clpb_push(clpb, &res, sizeof(res));
  if (MTLK_ERR_OK == res) {
      res = mtlk_clpb_push(clpb, &rxth_cfg, sizeof(rxth_cfg));
  }

  return res;
}

#define TXOP_LAST_STATE MAX(UMI_TXOP_MODE_DISABLED, MAX(UMI_TXOP_MODE_FORCED, UMI_TXOP_MODE_DYNAMIC))

int __MTLK_IFUNC
mtlk_core_send_txop_mode (mtlk_core_t *core, uint32 sid, uint32 txop_mode)
{
  mtlk_txmm_msg_t                 man_msg;
  mtlk_txmm_data_t               *man_entry = NULL;
  UMI_SET_TXOP_CONFIG_t          *txop_config = NULL;
  mtlk_vap_handle_t               vap_handle = core->vap_handle;
  int                             res;
  wave_radio_t                   *radio = wave_vap_radio_get(vap_handle);

  if (TXOP_LAST_STATE < txop_mode) {
    ELOG_DDDD("Wrong TXOP mode: %u, allowed: %u (disabled), %u (forced) or %u (dynamic)",
              txop_mode, UMI_TXOP_MODE_DISABLED, UMI_TXOP_MODE_FORCED, UMI_TXOP_MODE_DYNAMIC);
    return MTLK_ERR_PARAMS;
  }

  if (!mtlk_hw_is_sid_valid_or_all_sta_sid(mtlk_vap_get_hw(vap_handle), sid)) {
    return MTLK_ERR_PARAMS; /* Error already logged */
  }

  ILOG2_V("Sending UM_MAN_TXOP_CONFIG_REQ");

  man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, mtlk_vap_get_txmm(vap_handle), NULL);

  if (NULL == man_entry) {
    ELOG_D("CID-%04x: No man entry available", mtlk_vap_get_oid(vap_handle));
    res = MTLK_ERR_NO_RESOURCES;
    return res;
  }

  man_entry->id = UM_MAN_SET_TXOP_CONFIG_REQ;
  man_entry->payload_size = sizeof(UMI_SET_TXOP_CONFIG_t);

  txop_config = (UMI_SET_TXOP_CONFIG_t *)(man_entry->payload);
  txop_config->staId = HOST_TO_MAC32(sid);
  txop_config->mode  = txop_mode;

  res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);

  if (MTLK_ERR_OK != res) {
    ELOG_DD("CID-%04x: Sending UM_MAN_SET_TXOP_CONFIG_REQ failed (res = %d)", mtlk_vap_get_oid(vap_handle), res);
  }
  else {
    WAVE_RADIO_PDB_SET_INT(radio, PARAM_DB_RADIO_TXOP_MODE, txop_mode);
    WAVE_RADIO_PDB_SET_INT(radio, PARAM_DB_RADIO_TXOP_SID, sid);
  }

  mtlk_txmm_msg_cleanup(&man_msg);
  return res;
}

/* Returns last STA ID set, if not set then ALL STA ID */
int __MTLK_IFUNC
mtlk_core_receive_txop_mode (mtlk_core_t *core, mtlk_core_txop_mode_t *txop_mode)
{
  mtlk_txmm_msg_t        man_msg;
  mtlk_txmm_data_t      *man_entry = NULL;
  UMI_SET_TXOP_CONFIG_t *txop_config = NULL;
  mtlk_vap_handle_t      vap_handle = core->vap_handle;
  int                    res;
  wave_radio_t          *radio = wave_vap_radio_get(vap_handle);
  uint32                 sta_id = WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_TXOP_SID);

  /* Cannot request TXOP mode for ALL STA SID from FW */
  if (mtlk_hw_is_sid_all_sta_sid(mtlk_vap_get_hw(vap_handle), sta_id)) {
    txop_mode->sid  = sta_id;
    txop_mode->mode = WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_TXOP_MODE);
    return MTLK_ERR_OK;
  }

  man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, mtlk_vap_get_txmm(vap_handle), NULL);
  if (NULL == man_entry) {
    ELOG_D("CID-%04x: No man entry available", mtlk_vap_get_oid(vap_handle));
    return MTLK_ERR_NO_RESOURCES;
  }

  man_entry->id = UM_MAN_SET_TXOP_CONFIG_REQ;
  man_entry->payload_size = sizeof(UMI_SET_TXOP_CONFIG_t);
  txop_config = (UMI_SET_TXOP_CONFIG_t *)(man_entry->payload);
  txop_config->getSetOperation = API_GET_OPERATION;
  txop_config->staId = HOST_TO_MAC32(sta_id);

  res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);

  if (MTLK_ERR_OK == res) {
    txop_mode->sid  = sta_id;
    txop_mode->mode = txop_config->mode;
  }
  else {
    ELOG_D("CID-%04x: Receive UM_MAN_SET_TXOP_CONFIG_REQ failed", mtlk_vap_get_oid(vap_handle));
  }

  mtlk_txmm_msg_cleanup(&man_msg);
  return res;
}

int __MTLK_IFUNC
mtlk_core_set_admission_capacity (mtlk_core_t *core, uint32 value)
{
  int                           res = MTLK_ERR_OK;
  mtlk_txmm_msg_t               man_msg;
  mtlk_txmm_data_t              *man_entry = NULL;
  UMI_UPDATE_ADMISSION_CAPACITY *pAvailAdmissionCap = NULL;
  mtlk_vap_handle_t             vap_handle = core->vap_handle;

  MTLK_ASSERT(vap_handle != 0);

  MTLK_CORE_PDB_SET_INT(core, PARAM_DB_CORE_ADMISSION_CAPACITY, value);

  man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, mtlk_vap_get_txmm(vap_handle), NULL);
  if (NULL == man_entry) {
    ELOG_D("CID-%04x: No man entry available", mtlk_vap_get_oid(vap_handle));
    res = MTLK_ERR_NO_RESOURCES;
    goto FINISH;
  }

  man_entry->id = UM_MAN_SET_ADMISSION_CAPACITY_REQ;
  man_entry->payload_size = sizeof(UMI_UPDATE_ADMISSION_CAPACITY);

  pAvailAdmissionCap = (UMI_UPDATE_ADMISSION_CAPACITY *)(man_entry->payload);
  pAvailAdmissionCap->availableAdmissionCapacity = HOST_TO_MAC32(value);

  res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);
  if (MTLK_ERR_OK != res) {
    ELOG_DD("CID-%04x: Set Availbale Admission Capacity failure (%i)", mtlk_vap_get_oid(vap_handle), res);
  }

  mtlk_txmm_msg_cleanup(&man_msg);

FINISH:
  return res;
}

int __MTLK_IFUNC
mtlk_core_receive_admission_capacity (mtlk_core_t *core, uint32 *value)
{
  int                           res = MTLK_ERR_OK;
  mtlk_txmm_msg_t               man_msg;
  mtlk_txmm_data_t              *man_entry = NULL;
  UMI_UPDATE_ADMISSION_CAPACITY *pAvailAdmissionCap = NULL;
  mtlk_vap_handle_t             vap_handle = core->vap_handle;

  man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, mtlk_vap_get_txmm(vap_handle), NULL);
  if (NULL == man_entry) {
    ELOG_D("CID-%04x: No man entry available", mtlk_vap_get_oid(vap_handle));
    return MTLK_ERR_NO_RESOURCES;
  }

  man_entry->id = UM_MAN_SET_ADMISSION_CAPACITY_REQ;
  man_entry->payload_size = sizeof(UMI_UPDATE_ADMISSION_CAPACITY);
  pAvailAdmissionCap = (UMI_UPDATE_ADMISSION_CAPACITY *)(man_entry->payload);
  pAvailAdmissionCap->getSetOperation = API_GET_OPERATION;

  res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);
  if (MTLK_ERR_OK == res) {
    *value = MAC_TO_HOST32(pAvailAdmissionCap->availableAdmissionCapacity);
  }
  else {
    ELOG_D("CID-%04x: Receive UM_MAN_SET_ADMISSION_CAPACITY_REQ failed", mtlk_vap_get_oid(vap_handle));
  }

  mtlk_txmm_msg_cleanup(&man_msg);
  return res;
}

int __MTLK_IFUNC
mtlk_core_cfg_send_fast_drop (mtlk_core_t *core, uint8 fast_drop)
{
  int                         res;
  mtlk_txmm_msg_t             man_msg;
  mtlk_txmm_data_t           *man_entry = NULL;
  UMI_FAST_DROP_CONFIG_REQ_t *fd_config = NULL;
  mtlk_vap_handle_t           vap_handle = core->vap_handle;

  ILOG2_V("Sending UM_MAN_FAST_DROP_CONFIG_REQ");

  man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, mtlk_vap_get_txmm(vap_handle), NULL);

  if (NULL == man_entry) {
    ELOG_D("CID-%04x: No man entry available", mtlk_vap_get_oid(vap_handle));
    return MTLK_ERR_NO_RESOURCES;
  }

  man_entry->id = UM_MAN_FAST_DROP_CONFIG_REQ;
  man_entry->payload_size = sizeof(UMI_FAST_DROP_CONFIG_REQ_t);

  fd_config = (UMI_FAST_DROP_CONFIG_REQ_t *)(man_entry->payload);
  fd_config->enableFastDrop = fast_drop;

  res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);

  if (MTLK_ERR_OK != res) {
    ELOG_DD("CID-%04x: Sending UM_MAN_FAST_DROP_CONFIG_REQ failed (res = %d)", mtlk_vap_get_oid(vap_handle), res);
  }

  mtlk_txmm_msg_cleanup(&man_msg);
  return res;
}

int __MTLK_IFUNC
mtlk_core_receive_fast_drop (mtlk_core_t *core, uint8 *fast_drop)
{
  int                         res;
  mtlk_txmm_msg_t             man_msg;
  mtlk_txmm_data_t           *man_entry = NULL;
  UMI_FAST_DROP_CONFIG_REQ_t *fd_config = NULL;
  mtlk_vap_handle_t           vap_handle = core->vap_handle;

  man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, mtlk_vap_get_txmm(vap_handle), NULL);
  if (NULL == man_entry) {
    ELOG_D("CID-%04x: No man entry available", mtlk_vap_get_oid(vap_handle));
    return MTLK_ERR_NO_RESOURCES;
  }

  man_entry->id = UM_MAN_FAST_DROP_CONFIG_REQ;
  man_entry->payload_size = sizeof(UMI_FAST_DROP_CONFIG_REQ_t);
  fd_config = (UMI_FAST_DROP_CONFIG_REQ_t *)(man_entry->payload);
  fd_config->getSetOperation = API_GET_OPERATION;

  res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);

  if (MTLK_ERR_OK == res) {
    *fast_drop = fd_config->enableFastDrop;
  }
  else {
    ELOG_D("CID-%04x: Receiving UM_MAN_FAST_DROP_CONFIG_REQ failed", mtlk_vap_get_oid(vap_handle));
  }

  mtlk_txmm_msg_cleanup(&man_msg);
  return res;
}

int __MTLK_IFUNC
mtlk_core_cfg_set_fast_drop (mtlk_core_t *core, uint8 fast_drop)
{
  if ((DISABLE_FAST_DROP != fast_drop) && (ENABLE_FAST_DROP != fast_drop)) {
    ELOG_DDD("Invalid value %d passed, must be %d or %d",
             fast_drop, DISABLE_FAST_DROP, ENABLE_FAST_DROP);
    return MTLK_ERR_PARAMS;
  }

  WAVE_RADIO_PDB_SET_INT(wave_vap_radio_get(core->vap_handle), PARAM_DB_RADIO_FAST_DROP, fast_drop);
  return mtlk_core_cfg_send_fast_drop(core, fast_drop);
}

static int
_mtlk_core_store_erp_cfg (mtlk_core_t *core, mtlk_erp_cfg_t *erp_cfg)
{
  int res;
  mtlk_pdb_size_t erp_size = sizeof(mtlk_erp_cfg_t);

  res = WAVE_RADIO_PDB_SET_BINARY(wave_vap_radio_get(core->vap_handle), PARAM_DB_RADIO_ERP_CFG, erp_cfg, erp_size);
  if (MTLK_ERR_OK != res)
    ELOG_D("CID-%04x: Failed to store ERP configuration", mtlk_vap_get_oid(core->vap_handle));

  return res;
}

int __MTLK_IFUNC
mtlk_core_send_erp_cfg (mtlk_core_t *core, mtlk_erp_cfg_t *erp_cfg)
{
  int                res;
  mtlk_txmm_msg_t    man_msg;
  mtlk_txmm_data_t  *man_entry = NULL;
  UMI_ERPSet_t      *erp = NULL;
  mtlk_vap_handle_t  vap_handle = core->vap_handle;
  uint16             oid;
  int                net_state = mtlk_core_get_net_state(core);

  oid = mtlk_vap_get_oid(vap_handle);
  /* Allow only if the VAP has already been activated */
  if (!(net_state & (NET_STATE_ACTIVATING | NET_STATE_DEACTIVATING | NET_STATE_CONNECTED))) {
    ELOG_D("CID-%04x: VAP not activated yet", oid);
    return MTLK_ERR_NOT_READY;
  }

  /* Store config, will be sent after scan */
  if (mtlk_core_is_in_scan_mode(core))
    return _mtlk_core_store_erp_cfg(core, erp_cfg);

  res = mtlk_core_radio_enable_if_needed(core);
  if (MTLK_ERR_OK != res)
    return res;

  ILOG2_D("CID-%04x: Sending UM_MAN_ERP_SET_REQ", oid);

  man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, mtlk_vap_get_txmm(vap_handle), NULL);

  if (NULL == man_entry) {
    ELOG_D("CID-%04x: No man entry available", oid);
    return MTLK_ERR_NO_RESOURCES;
  }

  man_entry->id = UM_MAN_ERP_SET_REQ;
  man_entry->payload_size = sizeof(UMI_ERPSet_t);

  erp                          = (UMI_ERPSet_t *)(man_entry->payload);
  erp->initalWaitTimeInSeconds = HOST_TO_MAC32(erp_cfg->initial_wait_time);
  erp->radioOffTimeInMsecs     = HOST_TO_MAC32(erp_cfg->radio_off_time);
  erp->radioOnTimerInMsecs     = HOST_TO_MAC32(erp_cfg->radio_on_time);
  erp->isErpEnable             = HOST_TO_MAC32(erp_cfg->erp_enabled);

  res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);

  if (MTLK_ERR_OK != res) {
    ELOG_DD("CID-%04x: Sending UM_MAN_ERP_SET_REQ failed (res = %d)", oid, res);
  }

  mtlk_txmm_msg_cleanup(&man_msg);
  res = mtlk_core_radio_disable_if_needed(core);

  return res;
}

int wave_core_cfg_send_rcvry_msg (mtlk_handle_t hcore, const void *data, uint32 data_size)
{
  uint32 msgpay_size;
  wave_fapi_msgpay_common_t *msgpay;
  mtlk_core_t *core = (mtlk_core_t *) hcore;
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;
  int res = MTLK_ERR_OK;

  MTLK_ASSERT(core != NULL);
  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);
  MTLK_UNREFERENCED_PARAM(core);

  /* get message */
  msgpay = mtlk_clpb_enum_get_next(clpb, &msgpay_size);

  /* send message to FAPI */
  MTLK_CLPB_TRY(msgpay, msgpay_size)
    switch (msgpay->subcmd_id)
    {
      case WAVE_FAPI_SUBCMDID_PROD_CAL_FILE_EVT:
      {
        res = wave_prod_nl_send_msg_cal_file_evt(msgpay->card_idx);
        break;
      }
      default: break;
    }
  MTLK_CLPB_FINALLY(res)
    return mtlk_clpb_push_res(clpb, res);
  MTLK_CLPB_END;
}

static int
_wave_core_send_nfrp_cfg (mtlk_core_t *core, const UMI_NFRP_CONFIG *req)
{
    mtlk_txmm_msg_t  man_msg;
    mtlk_txmm_data_t *man_entry;
    UMI_NFRP_CONFIG  *mac_msg;
    int              res;
    unsigned         oid;

    MTLK_ASSERT(core != NULL);
    oid = mtlk_vap_get_oid(core->vap_handle);

    ILOG1_DDD("CID-%04x: NFRP config - nfrpSupport:(%d) nfrpThreshold:(%d)", oid, req->nfrpSupport, req->nfrpThreshold);

    /* Allocate a new message */
    man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, mtlk_vap_get_txmm(core->vap_handle), NULL);
    if (!man_entry) {
      ELOG_D("CID-%04x: Can not get TXMM slot", oid);
      return MTLK_ERR_NO_RESOURCES;
    }

    /* Fill the message data */
    man_entry->id = UM_MAN_NFRP_CONFIG_REQ;
    man_entry->payload_size = sizeof(*mac_msg);
    mac_msg = (UMI_NFRP_CONFIG *)man_entry->payload;
    mac_msg->nfrpSupport = req->nfrpSupport;
    mac_msg->nfrpThreshold = req->nfrpThreshold;

    mtlk_dump(1, mac_msg, sizeof(*mac_msg), "dump of UMI_NFRP_CONFIG:");

    /* Send the message to FW */
    res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);

    /* Cleanup the message */
    mtlk_txmm_msg_cleanup(&man_msg);

    return res;
}

int __MTLK_IFUNC
wave_core_set_nfrp_cfg (mtlk_handle_t hcore, const void *data, uint32 data_size)
{
    int res = MTLK_ERR_OK;
    mtlk_core_t *core = (mtlk_core_t*)hcore;
    mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;
    mtlk_nfrp_cfg_t *nfrp_cfg = NULL;
    uint32 cfg_size;

    MTLK_ASSERT(core != NULL);
    MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);
    MTLK_ASSERT(mtlk_vap_is_master_ap(core->vap_handle));

    /* get new configuration and send it to FW */
    nfrp_cfg = mtlk_clpb_enum_get_next(clpb, &cfg_size);
    MTLK_CLPB_TRY(nfrp_cfg, cfg_size)
      MTLK_CFG_START_CHEK_ITEM_AND_CALL()
         MTLK_CFG_CHECK_ITEM_AND_CALL(nfrp_cfg, nfrp_cfg_params, _wave_core_send_nfrp_cfg,
                                      (core, &nfrp_cfg->nfrp_cfg_params), res);
      MTLK_CFG_END_CHEK_ITEM_AND_CALL()
    MTLK_CLPB_FINALLY(res)
      return mtlk_clpb_push_res(clpb, res); /* push result into clipboard */
    MTLK_CLPB_END
}

static int
_wave_core_read_pvt_sensor (mtlk_core_t *core, iwpriv_pvt_t *pvt_params)
{
  int res;
  mtlk_txmm_msg_t    man_msg;
  mtlk_txmm_data_t  *man_entry = NULL;
  UMI_PVT_t         *req = NULL;
  mtlk_vap_handle_t  vap_handle = core->vap_handle;

  MTLK_ASSERT(pvt_params);

  man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, mtlk_vap_get_txmm(vap_handle), NULL);
  if (NULL == man_entry) {
    ELOG_D("CID-%04x: No man entry available", mtlk_vap_get_oid(vap_handle));
    return MTLK_ERR_NO_RESOURCES;
  }

  man_entry->id = UM_MAN_PVT_READ_REQ;
  man_entry->payload_size = sizeof(*req);
  req = (UMI_PVT_t *)(man_entry->payload);

  res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);
  if (MTLK_ERR_OK == res) {
    pvt_params->voltage     = MAC_TO_HOST32(req->voltage);
    pvt_params->temperature = MAC_TO_HOST32(req->temperature);
  } else {
    ELOG_DD("CID-%04x: Read UM_MAN_PVT_READ_REQ failure (%d)", mtlk_vap_get_oid(vap_handle), res);
    memset(pvt_params, 0, sizeof(*pvt_params));
  }

  mtlk_txmm_msg_cleanup(&man_msg);

  return res;
}

int __MTLK_IFUNC
wave_core_get_pvt_sensor (mtlk_handle_t hcore, const void *data, uint32 data_size)
{
  int                      res = MTLK_ERR_OK;
  wave_pvt_sensor_t        pvt_sensor;
  mtlk_core_t              *core = (mtlk_core_t*)hcore;
  mtlk_clpb_t              *clpb = *(mtlk_clpb_t **)data;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  MTLK_CFG_SET_ITEM_BY_FUNC(&pvt_sensor, pvt_params,
    _wave_core_read_pvt_sensor, (core, &pvt_sensor.pvt_params), res);

  /* push result and config to clipboard*/
  res = mtlk_clpb_push(clpb, &res, sizeof(res));
  if (MTLK_ERR_OK == res) {
    res = mtlk_clpb_push(clpb, &pvt_sensor, sizeof(pvt_sensor));
  }

  return res;
}

/* Test Bus mode configuration */

static void
_wave_radio_store_test_bus_mode (wave_radio_t *radio, const int mode)
{
  MTLK_ASSERT(radio != NULL);
  WAVE_RADIO_PDB_SET_INT(radio, PARAM_DB_RADIO_TEST_BUS_MODE, mode);
}

int __MTLK_IFUNC
wave_core_send_test_bus_mode (mtlk_core_t *core, const uint32 mode)
{
  mtlk_txmm_msg_t         man_msg;
  mtlk_txmm_data_t       *man_entry;
  UMI_testBusEn_t        *mac_msg;
  int                     res = MTLK_ERR_OK;

  ILOG1_DD("CID-%04x: TestBus mode %d", mtlk_vap_get_oid(core->vap_handle), mode);

  /* allocate a new message */
  man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, mtlk_vap_get_txmm(core->vap_handle), NULL);
  if (!man_entry) {
    ELOG_D("CID-%04x: Can not get TXMM slot", mtlk_vap_get_oid(core->vap_handle));
    return MTLK_ERR_NO_RESOURCES;
  }

  /* fill the message data */
  man_entry->id = UM_MAN_TEST_BUS_EN_REQ;
  man_entry->payload_size = sizeof(*mac_msg);
  mac_msg = (UMI_testBusEn_t *)man_entry->payload;
  memset(mac_msg, 0, sizeof(*mac_msg));

  MTLK_STATIC_ASSERT(sizeof(uint32) == sizeof(mac_msg->enable));
  mac_msg->enable = MAC_TO_HOST32(mode);

  /* send the message to FW */
  res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);

  /* cleanup the message */
  mtlk_txmm_msg_cleanup(&man_msg);

  return res;
}

int __MTLK_IFUNC
wave_core_set_test_bus_mode (mtlk_handle_t hcore, const void *data, uint32 data_size)
{
  int res = MTLK_ERR_OK;
  mtlk_core_t       *core = mtlk_core_get_master((mtlk_core_t*)hcore);
  wave_radio_t      *radio = wave_vap_radio_get(core->vap_handle);
  wave_ui_mode_t    *cfg;
  uint32             cfg_size;
  mtlk_clpb_t       *clpb = *(mtlk_clpb_t **)data;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  /* get configuration */
  cfg = mtlk_clpb_enum_get_next(clpb, &cfg_size);
  MTLK_CLPB_TRY(cfg, cfg_size)
    MTLK_CFG_START_CHEK_ITEM_AND_CALL()
      /* store new config in internal DB */
      MTLK_CFG_CHECK_ITEM_AND_CALL_VOID(cfg, mode, _wave_radio_store_test_bus_mode,
                                        (radio, cfg->mode));
      /* send new config to FW */
      MTLK_CFG_CHECK_ITEM_AND_CALL(cfg, mode, wave_core_send_test_bus_mode,
                                   (core, cfg->mode), res);
    MTLK_CFG_END_CHEK_ITEM_AND_CALL()
  MTLK_CLPB_FINALLY(res)
    /* push result into clipboard */
    return mtlk_clpb_push_res(clpb, res);
  MTLK_CLPB_END
}

int __MTLK_IFUNC mtlk_core_cfg_set_ssid (mtlk_core_t *core, const u8 *ssid, u8 ssid_len)
{
  struct wireless_dev *wdev = mtlk_df_user_get_wdev(mtlk_df_get_user(mtlk_vap_get_df(core->vap_handle)));
  mtlk_gen_core_cfg_t core_cfg;
  int res;

  MTLK_ASSERT(NULL != wdev);

  if (ssid_len == 0)
    return MTLK_ERR_OK;
  if (ssid_len > IEEE80211_MAX_SSID_LEN || ssid_len >= MTLK_ESSID_MAX_SIZE)
    return MTLK_ERR_PARAMS;

  wave_strncopy(core_cfg.essid, ssid, sizeof(core_cfg.essid), ssid_len);

  res = mtlk_core_set_essid_by_cfg(core, &core_cfg);
  if (res != MTLK_ERR_OK)
    return res;

  /* wdev_lock not needed, lock taken by nl80211_set_beacon */
  wave_memcpy(wdev->ssid, sizeof(wdev->ssid), ssid, ssid_len);
  wdev->ssid_len = ssid_len;
  return MTLK_ERR_OK;
}

/************* WLAN counters source *******************/
static uint32 _mtlk_core_cfg_read_counters_src (mtlk_core_t *core)
{
    MTLK_ASSERT(core != NULL);
    return WAVE_RADIO_PDB_GET_INT(wave_vap_radio_get(core->vap_handle), PARAM_DB_RADIO_WLAN_COUNTERS_SRC);
}

static void _mtlk_core_cfg_store_counters_src (mtlk_core_t *core, const unsigned src)
{
    MTLK_ASSERT(core != NULL);
    WAVE_RADIO_PDB_SET_INT(wave_vap_radio_get(core->vap_handle), PARAM_DB_RADIO_WLAN_COUNTERS_SRC, src);
}

int __MTLK_IFUNC mtlk_core_cfg_get_counters_src (mtlk_handle_t hcore, const void* data, uint32 data_size)
{
  uint32 res = MTLK_ERR_OK;
  mtlk_wlan_counters_src_cfg_t counters_cfg;
  mtlk_core_t *core = (mtlk_core_t*)hcore;
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);
  MTLK_ASSERT(mtlk_vap_is_master_ap(core->vap_handle));

  memset(&counters_cfg, 0, sizeof(counters_cfg));
  MTLK_CFG_SET_ITEM(&counters_cfg, src, _mtlk_core_cfg_read_counters_src(core));

  res = mtlk_clpb_push(clpb, &res, sizeof(res));
  if (MTLK_ERR_OK == res) {
    res = mtlk_clpb_push(clpb, &counters_cfg, sizeof(counters_cfg));
  }
  return res;
}

int __MTLK_IFUNC mtlk_core_cfg_set_counters_src (mtlk_handle_t hcore, const void* data, uint32 data_size)
{
  uint32 res = MTLK_ERR_OK;
  mtlk_core_t *core = (mtlk_core_t*)hcore;
  mtlk_wlan_counters_src_cfg_t *counters_cfg = NULL;
  uint32 counters_cfg_size;
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;
  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  counters_cfg = mtlk_clpb_enum_get_next(clpb, &counters_cfg_size);
  MTLK_CLPB_TRY(counters_cfg, counters_cfg_size)
    MTLK_CFG_START_CHEK_ITEM_AND_CALL()
      MTLK_CFG_CHECK_ITEM_AND_CALL_VOID(counters_cfg, src, _mtlk_core_cfg_store_counters_src, (core, counters_cfg->src));
    MTLK_CFG_END_CHEK_ITEM_AND_CALL()
  MTLK_CLPB_FINALLY(res)
    return mtlk_clpb_push_res(clpb, res);
  MTLK_CLPB_END
}

int __MTLK_IFUNC mtlk_core_get_tpc_cfg (mtlk_handle_t hcore, const void* data, uint32 data_size)
{
  uint32 res = MTLK_ERR_OK;
  mtlk_tpc_cfg_t tpc_cfg;
  mtlk_core_t *core = (mtlk_core_t*)hcore;
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  memset(&tpc_cfg, 0, sizeof(tpc_cfg));

  MTLK_CFG_SET_ITEM(&tpc_cfg, loop_type,
                    WAVE_RADIO_PDB_GET_INT(wave_vap_radio_get(core->vap_handle), PARAM_DB_RADIO_TPC_LOOP_TYPE));

  return mtlk_clpb_push_res_data(clpb, res, &tpc_cfg, sizeof(tpc_cfg));
}

static int
_wave_core_cfg_get_cutoff_point_req(mtlk_core_t *core, uint32 *cutoff_point)
{
  int                       res = MTLK_ERR_OK;
  mtlk_txmm_msg_t           man_msg;
  mtlk_txmm_data_t          *man_entry = NULL;
  UMI_Protection_Rate_Config_t       *req = NULL;
  mtlk_vap_handle_t                  vap_handle = core->vap_handle;

  man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg,
    mtlk_vap_get_txmm(vap_handle), NULL);
  if (NULL == man_entry) {
    ELOG_D("CID-%04x: No man entry available", mtlk_vap_get_oid(vap_handle));
    return MTLK_ERR_NO_RESOURCES;
  }

  man_entry->id = UM_MAN_RTS_RATE_SET_REQ;
  man_entry->payload_size = sizeof(UMI_Protection_Rate_Config_t);

  req = (UMI_Protection_Rate_Config_t *)(man_entry->payload);
  req->getSetOperation = API_GET_OPERATION;

  res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);

  if (MTLK_ERR_OK == res)
    *cutoff_point = req->cutoffPoint;
  else
    ELOG_D("CID-%04x: Receive UM_MAN_RTS_RATE_SET_REQ failed",
      mtlk_vap_get_oid(vap_handle));

  mtlk_txmm_msg_cleanup(&man_msg);

  return res;
}

#if defined (CPTCFG_IWLWAV_LINDRV_HW_PCIEG5)
static void
tpc_config_default_g5(tpcConfig_t *tpcConfig)
{
  tpcConfig->calType = 1;   /* TPC_CAL_TYPE_LOGARITHMIC */
  tpcConfig->fixedGain = NO_FIXED_GAIN;
}
#endif

#if defined (CPTCFG_IWLWAV_LINDRV_HW_PCIEG6)
static void
tpc_config_default_g6(tpcConfig_t *tpcConfig)
{
  tpcConfig->calType = 1;   /* TPC_CAL_TYPE_LOGARITHMIC */
  tpcConfig->fixedGain = NO_FIXED_GAIN;
}
#endif

static int
_mtlk_set_tpc_config (struct nic *nic)
{
  mtlk_txmm_data_t          *man_entry = NULL;
  mtlk_txmm_msg_t           man_msg;
  int                       result = MTLK_ERR_OK;
  tpcConfig_t               *tpcConfig = NULL;
  mtlk_card_type_t          card_type = MTLK_CARD_UNKNOWN;
  psdb_pw_limits_t          pwl;
  mtlk_pdb_size_t           pwl_size = sizeof(psdb_pw_limits_t);
  int                       i;
  wave_radio_t              *radio = wave_vap_radio_get(nic->vap_handle);

  ILOG1_V("SET_TPC_CONFIG_REQ");
  man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, mtlk_vap_get_txmm(nic->vap_handle), &result);
  if (man_entry == NULL) {
    ELOG_D("CID-%04x: Can't send UM_MAN_SET_TPC_CONFIG_REQ to MAC due to the lack of MAN_MSG", mtlk_vap_get_oid(nic->vap_handle));
    goto FINISH;
  }

  man_entry->id           = UM_MAN_SET_TPC_CONFIG_REQ;
  man_entry->payload_size = sizeof(*tpcConfig);
  tpcConfig = man_entry->payload;
  memset(tpcConfig, 0, sizeof(*tpcConfig));

  mtlk_hw_get_prop(mtlk_vap_get_hwapi(nic->vap_handle), MTLK_HW_PROP_CARD_TYPE, &card_type, sizeof(&card_type));

  CARD_SELECTOR_START(card_type);
  IF_CARD_G5          ( tpc_config_default_g5(tpcConfig) );
  IF_CARD_G6          ( tpc_config_default_g6(tpcConfig) );
  CARD_SELECTOR_END();

  tpcConfig->tpcLoopType = WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_TPC_LOOP_TYPE);

  result = WAVE_RADIO_PDB_GET_BINARY(radio, PARAM_DB_RADIO_TPC_PW_LIMITS_CFG, &pwl, &pwl_size);
  if (result != MTLK_ERR_OK) {
    ELOG_D("CID-%04x: Can't send UM_MAN_SET_TPC_CONFIG_REQ request due to PDB read error", mtlk_vap_get_oid(nic->vap_handle));
    goto FINISH;
  }
  MTLK_ASSERT(sizeof(pwl)==pwl_size);

  tpcConfig->powerLimit11b      = HOST_TO_MAC16(pwl.pw_limits[PSDB_PHY_CW_11B]);

  for (i = 0; i < MAXIMUM_BANDWIDTHS_GEN6; i++) {
    tpcConfig->regulationLimit[i]   = HOST_TO_MAC16(pwl.pw_limits[PSDB_PHY_CW_OFDM_20 + i]);
    tpcConfig->regulationLimitMU[i] = HOST_TO_MAC16(pwl.pw_limits[PSDB_PHY_CW_MU_20 + i]);
    tpcConfig->regulationLimitBF[i] = HOST_TO_MAC16(pwl.pw_limits[PSDB_PHY_CW_BF_20 + i]);
  }

  mtlk_dump(2, tpcConfig, sizeof(*tpcConfig), "dump of tpcConfig");
  SLOG1(0, 0, tpcConfig_t, tpcConfig);
  result = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);
  if (result != MTLK_ERR_OK) {
    ELOG_DD("CID-%04x: Can't send UM_MAN_SET_TPC_CONFIG_REQ request to MAC (err=%d)", mtlk_vap_get_oid(nic->vap_handle), result);
    goto FINISH;
  }

FINISH:
  if (man_entry) {
    mtlk_txmm_msg_cleanup(&man_msg);
  }

  return result;
}

int __MTLK_IFUNC mtlk_core_set_tpc_cfg (mtlk_handle_t hcore, const void* data, uint32 data_size)
{
  uint32 res = MTLK_ERR_OK;
  mtlk_core_t *core = (mtlk_core_t*)hcore;
  mtlk_tpc_cfg_t *tpc_cfg = NULL;
  uint32          tpc_cfg_size;
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  tpc_cfg = mtlk_clpb_enum_get_next(clpb, &tpc_cfg_size);
  MTLK_CLPB_TRY(tpc_cfg, tpc_cfg_size)
    MTLK_CFG_START_CHEK_ITEM_AND_CALL()
      MTLK_CFG_CHECK_ITEM_AND_CALL_VOID(tpc_cfg, loop_type, WAVE_RADIO_PDB_SET_INT,
                              (wave_vap_radio_get(core->vap_handle), PARAM_DB_RADIO_TPC_LOOP_TYPE, tpc_cfg->loop_type));
      MTLK_CFG_CHECK_ITEM_AND_CALL(tpc_cfg, loop_type, _mtlk_set_tpc_config,
                              (core), res);
    MTLK_CFG_END_CHEK_ITEM_AND_CALL()
  MTLK_CLPB_FINALLY(res)
    return mtlk_clpb_push_res(clpb, res);
  MTLK_CLPB_END
}

int __MTLK_IFUNC wave_core_cfg_get_rts_rate (mtlk_handle_t hcore, const void* data, uint32 data_size)
{
  int                      res = MTLK_ERR_OK;
  mtlk_wlan_rts_rate_cfg_t rts_rate;
  mtlk_core_t              *core = (mtlk_core_t*)hcore;
  mtlk_clpb_t              *clpb = *(mtlk_clpb_t **)data;

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);
  MTLK_ASSERT(mtlk_vap_is_master_ap(core->vap_handle));

  MTLK_CFG_SET_ITEM_BY_FUNC(&rts_rate, cutoff_point, _wave_core_cfg_get_cutoff_point_req,
                           (core, &rts_rate.cutoff_point), res);

  /* push result and config to clipboard */
  return mtlk_clpb_push_res_data(clpb, res, &rts_rate, sizeof(rts_rate));
}

static int
_wave_core_cfg_set_cutoff_point_req (mtlk_core_t *core, uint32 cutoff_point)
{
#define MIN_RTS_RATE_CUTOFF 0
#define MAX_RTS_RATE_CUTOFF 2
  int res;
  mtlk_txmm_msg_t           man_msg;
  mtlk_txmm_data_t          *man_entry = NULL;
  UMI_Protection_Rate_Config_t  *req = NULL;
  mtlk_vap_handle_t         vap_handle = core->vap_handle;

  if (cutoff_point > MAX_RTS_RATE_CUTOFF) {
    ELOG_DDD("Invalid cutoff_point: %d. Allowed values: %d..%d", cutoff_point,
      MIN_RTS_RATE_CUTOFF, MAX_RTS_RATE_CUTOFF);
    return MTLK_ERR_PARAMS;
  }
  MTLK_ASSERT(vap_handle);

  ILOG1_DD("CID-%04x:RTS protection rate FW request: Set %d",
    mtlk_vap_get_oid(vap_handle), cutoff_point);

  man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, mtlk_vap_get_txmm(vap_handle), NULL);
  if (NULL == man_entry) {
    ELOG_D("CID-%04x: No man entry available", mtlk_vap_get_oid(vap_handle));
    return MTLK_ERR_NO_RESOURCES;
  }

  man_entry->id = UM_MAN_RTS_RATE_SET_REQ;
  man_entry->payload_size = sizeof(UM_MAN_RTS_RATE_SET_REQ);

  req = (UMI_Protection_Rate_Config_t *)(man_entry->payload);
  req->getSetOperation = API_SET_OPERATION;
  req->cutoffPoint = (uint8)cutoff_point;

  res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);
  if (MTLK_ERR_OK != res) {
    ELOG_DD("CID-%04x: Set RTS protection rate failure (%i)", mtlk_vap_get_oid(vap_handle), res);
  }

  mtlk_txmm_msg_cleanup(&man_msg);
  return res;
#undef MIN_RTS_RATE_CUTOFF
#undef MAX_RTS_RATE_CUTOFF
}

inline static void __wave_core_cfg_store_cutoff_point (mtlk_core_t *core,
  const uint32 cutoff_point)
{
  wave_radio_t *radio = wave_vap_radio_get(core->vap_handle);
  MTLK_ASSERT(radio != NULL);

  WAVE_RADIO_PDB_SET_INT(radio, PARAM_DB_RADIO_RTS_CUTOFF_POINT, cutoff_point);
}

int __MTLK_IFUNC wave_core_cfg_set_rts_rate (mtlk_handle_t hcore, const void* data, uint32 data_size)
{
  int                    res = MTLK_ERR_OK;
  mtlk_core_t            *core = (mtlk_core_t*)hcore;
  mtlk_wlan_rts_rate_cfg_t   *rts_rate = NULL;
  uint32                 rts_rate_size;
  mtlk_clpb_t            *clpb = *(mtlk_clpb_t **)data;
  wave_radio_t           *radio = wave_vap_radio_get(core->vap_handle);

  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);
  MTLK_ASSERT(mtlk_vap_is_master_ap(core->vap_handle));

  if (wave_radio_sta_cnt_get(radio) > 0) {
    WLOG_V("Setting RTS rate is disabled while connected STA or peer AP exists");
    return MTLK_ERR_NOT_SUPPORTED;
  }
  /* get configuration */
  rts_rate = mtlk_clpb_enum_get_next(clpb, &rts_rate_size);
  MTLK_CLPB_TRY(rts_rate, rts_rate_size)
    MTLK_CFG_START_CHEK_ITEM_AND_CALL()
      /* send new config to FW */
      MTLK_CFG_CHECK_ITEM_AND_CALL(rts_rate, cutoff_point, _wave_core_cfg_set_cutoff_point_req,
                                  (core, rts_rate->cutoff_point), res);
      /* store new config in internal db*/
      MTLK_CFG_CHECK_ITEM_AND_CALL_VOID(rts_rate, cutoff_point, __wave_core_cfg_store_cutoff_point,
                                        (core, rts_rate->cutoff_point));
    MTLK_CFG_END_CHEK_ITEM_AND_CALL()
  MTLK_CLPB_FINALLY(res)
    /* push result into clipboard */
    return mtlk_clpb_push_res(clpb, res);
  MTLK_CLPB_END
}

int __MTLK_IFUNC
mtlk_reload_tpc_wrapper (struct nic *nic, uint32 channel, psdb_pw_limits_t *pwl)
{
  int res = MTLK_ERR_PARAMS;
  mtlk_pdb_size_t pw_limits_size = sizeof(psdb_pw_limits_t);
  wave_radio_t *radio;
  MTLK_ASSERT(nic);
  MTLK_ASSERT(pwl);

  radio = wave_vap_radio_get(nic->vap_handle);

  res = WAVE_RADIO_PDB_SET_BINARY(radio, PARAM_DB_RADIO_TPC_PW_LIMITS_CFG, pwl->pw_limits, pw_limits_size);
  if(res != MTLK_ERR_OK)
  {
    ELOG_D("CID-%04x: Can't update power limits in PDB", mtlk_vap_get_oid(nic->vap_handle));
    goto END;
  }

  res = _mtlk_set_tpc_config(nic);
  if(res != MTLK_ERR_OK)
  {
    goto END;
  }

  res = mtlk_reload_tpc(
      (mtlk_hw_band_e)WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_FREQ_BAND_CUR),
      WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_PROG_MODEL_SPECTRUM_MODE),
      WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_BONDING_MODE),
      channel,
      mtlk_vap_get_txmm(nic->vap_handle),
      nic->txmm_async_eeprom_msgs,
      ARRAY_SIZE(nic->txmm_async_eeprom_msgs),
      mtlk_core_get_eeprom(nic),
      mtlk_eeprom_get_num_antennas(mtlk_core_get_eeprom(nic)));

  if(res != MTLK_ERR_OK)
  {
    goto END;
  }

  res = MTLK_ERR_OK;

END:

  return res;
}

int __MTLK_IFUNC wave_core_cfg_recover_cutoff_point (mtlk_core_t *core)
{
  wave_radio_t *radio = wave_vap_radio_get(core->vap_handle);
  uint32 cutoff_point  = WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_RTS_CUTOFF_POINT);

  if (MTLK_PARAM_DB_VALUE_IS_INVALID(cutoff_point))
      return MTLK_ERR_OK;

  return _wave_core_cfg_set_cutoff_point_req(core, cutoff_point);
}

static __INLINE unsigned int
wave_band_to_nof_channels (mtlk_hw_band_e band)
{
  if ((MTLK_HW_BAND_2_4_GHZ == band)) {
    return NUM_2GHZ_CHANS;
  }
  else if ((MTLK_HW_BAND_5_2_GHZ == band)) {
    return NUM_5GHZ_CHANS;
  }
  else {
    ELOG_V("Invalid band");
    return 0;
  }
}

unsigned int __MTLK_IFUNC
wave_core_cfg_get_nof_channels (mtlk_hw_band_e  hw_band)
{
  return wave_band_to_nof_channels(hw_band);
}

/* Dynamic MU Configuration */

static int _core_cfg_receive_dynamic_mu_type (mtlk_core_t *core, UMI_DYNAMIC_MU_TYPE *dynamic_mu_type)
{
  mtlk_txmm_msg_t man_msg;
  mtlk_txmm_data_t *man_entry;
  UMI_DYNAMIC_MU_TYPE *mac_msg;
  int res;
  uint16 oid;

  MTLK_ASSERT(core != NULL);
  MTLK_ASSERT(core == mtlk_core_get_master(core));

  oid = mtlk_vap_get_oid(core->vap_handle);
  ILOG2_D("CID-%04x: Receive DYNAMIC MU TYPE", oid);

  /* allocate a new message */
  man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, mtlk_vap_get_txmm(core->vap_handle), NULL);
  if (!man_entry) {
    ELOG_D("CID-%04x: Can not get TXMM slot", oid);
    return MTLK_ERR_NO_RESOURCES;
  }

  /* fill the message data */
  man_entry->id = UM_MAN_SET_DYNAMIC_MU_TYPE_REQ;
  man_entry->payload_size = sizeof(UMI_DYNAMIC_MU_TYPE);
  mac_msg = (UMI_DYNAMIC_MU_TYPE *)man_entry->payload;
  mac_msg->getSetOperation = API_GET_OPERATION;

  /* send the message to FW */
  res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);
  if (MTLK_ERR_OK == res) {
    dynamic_mu_type->dlMuType = mac_msg->dlMuType;
    dynamic_mu_type->ulMuType = mac_msg->ulMuType;
    dynamic_mu_type->minStationsInGroup = mac_msg->minStationsInGroup;
    dynamic_mu_type->maxStationsInGroup = mac_msg->maxStationsInGroup;
    dynamic_mu_type->cdbConfig = mac_msg->cdbConfig;
  }
  else {
    ELOG_DD("CID-%04x: Receive DYNAMIC MU TYPE failed (%i)", oid, res);
  }

  /* cleanup the message */
  mtlk_txmm_msg_cleanup(&man_msg);
  return res;
}

int __MTLK_IFUNC wave_core_cfg_send_dynamic_mu_type (mtlk_core_t *core, const UMI_DYNAMIC_MU_TYPE *dynamic_mu_type)
{
  mtlk_txmm_msg_t man_msg;
  mtlk_txmm_data_t *man_entry;
  UMI_DYNAMIC_MU_TYPE *mac_msg;
  int res;
  uint16 oid;

  MTLK_ASSERT(core != NULL);
  MTLK_ASSERT(core == mtlk_core_get_master(core));

  oid = mtlk_vap_get_oid(core->vap_handle);
  ILOG2_D("CID-%04x: Send DYNAMIC MU TYPE", oid);

  /* allocate a new message */
  man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, mtlk_vap_get_txmm(core->vap_handle), NULL);
  if (!man_entry) {
    ELOG_D("CID-%04x: Can not get TXMM slot", oid);
    return MTLK_ERR_NO_RESOURCES;
  }

  /* fill the message data */
  man_entry->id = UM_MAN_SET_DYNAMIC_MU_TYPE_REQ;
  man_entry->payload_size = sizeof(UMI_DYNAMIC_MU_TYPE);
  mac_msg = (UMI_DYNAMIC_MU_TYPE *)man_entry->payload;
  memset(mac_msg, 0, sizeof(*mac_msg));

  mac_msg->dlMuType = dynamic_mu_type->dlMuType;
  mac_msg->ulMuType = dynamic_mu_type->ulMuType;
  mac_msg->minStationsInGroup = dynamic_mu_type->minStationsInGroup;
  mac_msg->maxStationsInGroup = dynamic_mu_type->maxStationsInGroup;
  mac_msg->cdbConfig = dynamic_mu_type->cdbConfig;

  /* send the message to FW */
  res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);
  if (MTLK_ERR_OK != res) {
    ELOG_DD("CID-%04x: Send DYNAMIC MU TYPE failed (%i)", oid, res);
  }

  /* cleanup the message */
  mtlk_txmm_msg_cleanup(&man_msg);
  return res;
}

static int _core_cfg_receive_he_mu_fixed_parameters (mtlk_core_t *core,
                                                     UMI_HE_MU_FIXED_PARAMTERS *he_mu_fixed_parameters)
{
  mtlk_txmm_msg_t man_msg;
  mtlk_txmm_data_t *man_entry;
  UMI_HE_MU_FIXED_PARAMTERS *mac_msg;
  int res;
  uint16 oid;

  MTLK_ASSERT(core != NULL);
  MTLK_ASSERT(core == mtlk_core_get_master(core));

  oid = mtlk_vap_get_oid(core->vap_handle);
  ILOG2_D("CID-%04x: Receive HE MU FIXED PARAMETERS", oid);

  /* allocate a new message */
  man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, mtlk_vap_get_txmm(core->vap_handle), NULL);
  if (!man_entry) {
    ELOG_D("CID-%04x: Can not get TXMM slot", oid);
    return MTLK_ERR_NO_RESOURCES;
  }

  /* fill the message data */
  man_entry->id = UMI_MAN_SET_HE_MU_FIXED_PARAMETERS;
  man_entry->payload_size = sizeof(UMI_HE_MU_FIXED_PARAMTERS);
  mac_msg = (UMI_HE_MU_FIXED_PARAMTERS *)man_entry->payload;
  mac_msg->getSetOperation = API_GET_OPERATION;

  /* send the message to FW */
  res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);
  if (MTLK_ERR_OK == res) {
    he_mu_fixed_parameters->muSequence = mac_msg->muSequence;
    he_mu_fixed_parameters->ltf_GI = mac_msg->ltf_GI;
    he_mu_fixed_parameters->codingType = mac_msg->codingType;
    he_mu_fixed_parameters->heRate = mac_msg->heRate;
  }
  else {
    ELOG_DD("CID-%04x: Receive HE MU FIXED PARAMETERS failed (%i)", oid, res);
  }

  /* cleanup the message */
  mtlk_txmm_msg_cleanup(&man_msg);
  return res;
}

int __MTLK_IFUNC wave_core_cfg_send_he_mu_fixed_parameters (mtlk_core_t *core,
                                  const UMI_HE_MU_FIXED_PARAMTERS *he_mu_fixed_parameters)
{
  mtlk_txmm_msg_t man_msg;
  mtlk_txmm_data_t *man_entry;
  UMI_HE_MU_FIXED_PARAMTERS *mac_msg;
  int res;
  uint16 oid;

  MTLK_ASSERT(core != NULL);
  MTLK_ASSERT(core == mtlk_core_get_master(core));

  oid = mtlk_vap_get_oid(core->vap_handle);
  ILOG2_D("CID-%04x: Send HE MU FIXED PARAMETERS", oid);

  /* allocate a new message */
  man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, mtlk_vap_get_txmm(core->vap_handle), NULL);
  if (!man_entry) {
    ELOG_D("CID-%04x: Can not get TXMM slot", oid);
    return MTLK_ERR_NO_RESOURCES;
  }

  /* fill the message data */
  man_entry->id = UMI_MAN_SET_HE_MU_FIXED_PARAMETERS;
  man_entry->payload_size = sizeof(UMI_HE_MU_FIXED_PARAMTERS);
  mac_msg = (UMI_HE_MU_FIXED_PARAMTERS *)man_entry->payload;
  memset(mac_msg, 0, sizeof(*mac_msg));

  mac_msg->muSequence = he_mu_fixed_parameters->muSequence;
  mac_msg->ltf_GI = he_mu_fixed_parameters->ltf_GI;
  mac_msg->codingType = he_mu_fixed_parameters->codingType;
  mac_msg->heRate = he_mu_fixed_parameters->heRate;

  /* send the message to FW */
  res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);
  if (MTLK_ERR_OK != res) {
    ELOG_DD("CID-%04x: Send HE MU FIXED PARAMETERS failed (%i)", oid, res);
  }

  /* cleanup the message */
  mtlk_txmm_msg_cleanup(&man_msg);
  return res;
}

static int _core_cfg_receive_he_mu_duration (mtlk_core_t *core, UMI_HE_MU_DURATION *he_mu_duration)
{
  mtlk_txmm_msg_t man_msg;
  mtlk_txmm_data_t *man_entry;
  UMI_HE_MU_DURATION *mac_msg;
  int res;
  uint16 oid;

  MTLK_ASSERT(core != NULL);
  MTLK_ASSERT(core == mtlk_core_get_master(core));

  oid = mtlk_vap_get_oid(core->vap_handle);
  ILOG2_D("CID-%04x: Receive HE MU DURATION", oid);

  /* allocate a new message */
  man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, mtlk_vap_get_txmm(core->vap_handle), NULL);
  if (!man_entry) {
    ELOG_D("CID-%04x: Can not get TXMM slot", oid);
    return MTLK_ERR_NO_RESOURCES;
  }

  /* fill the message data */
  man_entry->id = UMI_MAN_SET_HE_MU_DURATION;
  man_entry->payload_size = sizeof(UMI_HE_MU_DURATION);
  mac_msg = (UMI_HE_MU_DURATION *)man_entry->payload;
  mac_msg->getSetOperation = API_GET_OPERATION;

  /* send the message to FW */
  res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);
  if (MTLK_ERR_OK == res) {
    he_mu_duration->PpduDuration = MAC_TO_HOST16(mac_msg->PpduDuration);
    he_mu_duration->TxopDuration = MAC_TO_HOST16(mac_msg->TxopDuration);
    he_mu_duration->TfLength = MAC_TO_HOST16(mac_msg->TfLength);
    he_mu_duration->NumberOfRepetitions = mac_msg->NumberOfRepetitions;
  }
  else {
    ELOG_DD("CID-%04x: Receive HE MU DURATION failed (%i)", oid, res);
  }

  /* cleanup the message */
  mtlk_txmm_msg_cleanup(&man_msg);
  return res;
}

int __MTLK_IFUNC wave_core_cfg_send_he_mu_duration (mtlk_core_t *core, const UMI_HE_MU_DURATION *he_mu_duration)
{
  mtlk_txmm_msg_t man_msg;
  mtlk_txmm_data_t *man_entry;
  UMI_HE_MU_DURATION *mac_msg;
  int res;
  uint16 oid;

  MTLK_ASSERT(core != NULL);
  MTLK_ASSERT(core == mtlk_core_get_master(core));

  oid = mtlk_vap_get_oid(core->vap_handle);
  ILOG2_D("CID-%04x: Send HE MU DURATION", oid);

  /* allocate a new message */
  man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, mtlk_vap_get_txmm(core->vap_handle), NULL);
  if (!man_entry) {
    ELOG_D("CID-%04x: Can not get TXMM slot", oid);
    return MTLK_ERR_NO_RESOURCES;
  }

  /* fill the message data */
  man_entry->id = UMI_MAN_SET_HE_MU_DURATION;
  man_entry->payload_size = sizeof(UMI_HE_MU_DURATION);
  mac_msg = (UMI_HE_MU_DURATION *)man_entry->payload;
  memset(mac_msg, 0, sizeof(*mac_msg));

  mac_msg->PpduDuration = HOST_TO_MAC16(he_mu_duration->PpduDuration);
  mac_msg->TxopDuration = HOST_TO_MAC16(he_mu_duration->TxopDuration);
  mac_msg->TfLength = HOST_TO_MAC16(he_mu_duration->TfLength);
  mac_msg->NumberOfRepetitions = he_mu_duration->NumberOfRepetitions;

  /* send the message to FW */
  res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);
  if (MTLK_ERR_OK != res) {
    ELOG_DD("CID-%04x: Send HE MU DURATION failed (%i)", oid, res);
  }

  /* cleanup the message */
  mtlk_txmm_msg_cleanup(&man_msg);
  return res;
}

void __MTLK_IFUNC wave_core_cfg_get_dynamic_mu_type (mtlk_core_t *core,
                                                     UMI_DYNAMIC_MU_TYPE *dynamic_mu_type)
{
  mtlk_pdb_size_t data_size = sizeof(*dynamic_mu_type);
  WAVE_RADIO_PDB_GET_BINARY(wave_vap_radio_get(core->vap_handle),
                            PARAM_DB_RADIO_DYNAMIC_MU_TYPE, dynamic_mu_type, &data_size);
}

void __MTLK_IFUNC wave_core_cfg_get_he_mu_fixed_parameters (mtlk_core_t *core,
                                                            UMI_HE_MU_FIXED_PARAMTERS *he_mu_fixed_parameters)
{
  mtlk_pdb_size_t data_size = sizeof(*he_mu_fixed_parameters);
  WAVE_RADIO_PDB_GET_BINARY(wave_vap_radio_get(core->vap_handle),
                            PARAM_DB_RADIO_HE_MU_FIXED_PARAMTERS, he_mu_fixed_parameters, &data_size);
}

void __MTLK_IFUNC wave_core_cfg_get_he_mu_duration (mtlk_core_t *core,
                                                    UMI_HE_MU_DURATION *he_mu_duration)
{
  mtlk_pdb_size_t data_size = sizeof(*he_mu_duration);
  WAVE_RADIO_PDB_GET_BINARY(wave_vap_radio_get(core->vap_handle),
                            PARAM_DB_RADIO_HE_MU_DURATION, he_mu_duration, &data_size);
}

static int _core_cfg_send_and_store_dynamic_mu_type (mtlk_core_t *core,
                                                     const UMI_DYNAMIC_MU_TYPE *dynamic_mu_type)
{
  /* send new config to FW */
  int res = wave_core_cfg_send_dynamic_mu_type(core, dynamic_mu_type);

  if (MTLK_ERR_OK == res) {
    /* store new config into internal DB */
    res = WAVE_RADIO_PDB_SET_BINARY(wave_vap_radio_get(core->vap_handle),
                                    PARAM_DB_RADIO_DYNAMIC_MU_TYPE,
                                    dynamic_mu_type, sizeof(*dynamic_mu_type));
    if (MTLK_ERR_OK != res) {
      ELOG_DD("CID-%04x: Store DYNAMIC MU TYPE failed (%i)",
              mtlk_vap_get_oid(core->vap_handle), res);
    }
  }
  return res;
}

static int _core_cfg_send_and_store_he_mu_fixed_parameters (mtlk_core_t *core,
                                  const UMI_HE_MU_FIXED_PARAMTERS *he_mu_fixed_parameters)
{
  /* send new config to FW */
  int res = wave_core_cfg_send_he_mu_fixed_parameters(core, he_mu_fixed_parameters);

  if (MTLK_ERR_OK == res) {
    /* store new config into internal DB */
    res = WAVE_RADIO_PDB_SET_BINARY(wave_vap_radio_get(core->vap_handle),
                                    PARAM_DB_RADIO_HE_MU_FIXED_PARAMTERS,
                                    he_mu_fixed_parameters, sizeof(*he_mu_fixed_parameters));
    if (MTLK_ERR_OK != res) {
      ELOG_DD("CID-%04x: Store HE MU FIXED PARAMETERS failed (%i)",
              mtlk_vap_get_oid(core->vap_handle), res);
    }
  }
  return res;
}

static int _core_cfg_send_and_store_he_mu_duration (mtlk_core_t *core,
                                                    const UMI_HE_MU_DURATION *he_mu_duration)
{
  /* send new config to FW */
  int res = wave_core_cfg_send_he_mu_duration(core, he_mu_duration);

  if (MTLK_ERR_OK == res) {
    /* store new config into internal DB */
    res = WAVE_RADIO_PDB_SET_BINARY(wave_vap_radio_get(core->vap_handle),
                                    PARAM_DB_RADIO_HE_MU_DURATION,
                                    he_mu_duration, sizeof(*he_mu_duration));
    if (MTLK_ERR_OK != res) {
      ELOG_DD("CID-%04x: Store HE MU DURATION failed (%i)",
              mtlk_vap_get_oid(core->vap_handle), res);
    }
  }
  return res;
}

int __MTLK_IFUNC wave_core_cfg_set_dynamic_mu_cfg (mtlk_handle_t hcore, const void *data, uint32 data_size)
{
  uint32 dynamic_mu_cfg_size;
  int res = MTLK_ERR_OK;
  mtlk_core_t *core = (mtlk_core_t *) hcore;
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;
  wave_dynamic_mu_cfg_t *dinamic_mu_cfg = NULL;

  MTLK_ASSERT(core != NULL);
  MTLK_ASSERT(core == mtlk_core_get_master(core));
  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  /* get configuration */
  dinamic_mu_cfg = mtlk_clpb_enum_get_next(clpb, &dynamic_mu_cfg_size);
  MTLK_CLPB_TRY(dinamic_mu_cfg, dynamic_mu_cfg_size)
    /* send configuration */
    MTLK_CFG_START_CHEK_ITEM_AND_CALL()
      MTLK_CFG_CHECK_ITEM_AND_CALL(dinamic_mu_cfg, dynamic_mu_type_params, _core_cfg_send_and_store_dynamic_mu_type,
                                   (core, &dinamic_mu_cfg->dynamic_mu_type_params), res);
      MTLK_CFG_CHECK_ITEM_AND_CALL(dinamic_mu_cfg, he_mu_fixed_params, _core_cfg_send_and_store_he_mu_fixed_parameters,
                                   (core, &dinamic_mu_cfg->he_mu_fixed_params), res);
      MTLK_CFG_CHECK_ITEM_AND_CALL(dinamic_mu_cfg, he_mu_duration_params, _core_cfg_send_and_store_he_mu_duration,
                                   (core, &dinamic_mu_cfg->he_mu_duration_params), res);
    MTLK_CFG_END_CHEK_ITEM_AND_CALL()
  MTLK_CLPB_FINALLY(res)
    /* push result into clipboard */
    return mtlk_clpb_push_res(clpb, res);
  MTLK_CLPB_END
}

int __MTLK_IFUNC wave_core_cfg_get_dynamic_mu_cfg (mtlk_handle_t hcore, const void *data, uint32 data_size)
{
  wave_dynamic_mu_cfg_t dinamic_mu_cfg;
  int res = MTLK_ERR_OK;
  mtlk_core_t *core = (mtlk_core_t *) hcore;
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;

  MTLK_ASSERT(core != NULL);
  MTLK_ASSERT(core == mtlk_core_get_master(core));
  MTLK_ASSERT(sizeof(mtlk_clpb_t*) == data_size);

  memset(&dinamic_mu_cfg, 0, sizeof(dinamic_mu_cfg));

  MTLK_CFG_SET_ITEM_BY_FUNC(&dinamic_mu_cfg, dynamic_mu_type_params,
                            _core_cfg_receive_dynamic_mu_type, (core, &dinamic_mu_cfg.dynamic_mu_type_params), res);
  MTLK_CFG_SET_ITEM_BY_FUNC(&dinamic_mu_cfg, he_mu_fixed_params,
                            _core_cfg_receive_he_mu_fixed_parameters, (core, &dinamic_mu_cfg.he_mu_fixed_params), res);
  MTLK_CFG_SET_ITEM_BY_FUNC(&dinamic_mu_cfg, he_mu_duration_params,
                            _core_cfg_receive_he_mu_duration, (core, &dinamic_mu_cfg.he_mu_duration_params), res);

  /* push result and data into clipboard */
  return mtlk_clpb_push_res_data(clpb, res, &dinamic_mu_cfg, sizeof(dinamic_mu_cfg));
}
