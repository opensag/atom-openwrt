/******************************************************************************

                               Copyright (c) 2012
                            Lantiq Deutschland GmbH

  For licensing information, see the file 'LICENSE' in the root folder of
  this software module.

******************************************************************************/

#include "mtlkinc.h"
#include "vendor_cmds.h"
#include "mtlk_df_priv.h"
#include "mtlk_df.h"
#include "cfg80211.h"
#include "mac80211.h"
#include "mtlk_df_nbuf.h"
#include "core_config.h"
#include "mtlk_coreui.h"

#define LOG_LOCAL_GID             GID_VENDOR_CMD
#define LOG_LOCAL_FID             0
#define TEXT_ARRAY_SIZE           1024

static const uint8 _bcast_addr[ETH_ALEN] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

void cfg80211_set_dfs_state(struct wiphy *wiphy,
                            const struct cfg80211_chan_def *chandef,
                            enum nl80211_dfs_state dfs_state);

static int _wave_cfg80211_vendor_hapd_get_aid(struct wiphy *wiphy, struct wireless_dev *wdev,
               const void *data, int data_len)
{
  u8 *addr = (u8*)data;
  mtlk_nbuf_t *rsp_nbuf;
  u16 aid;
  int aid_len = sizeof(aid);
  int res;
  wv_cfg80211_t *cfg80211 = wiphy_priv(wiphy);
  MTLK_ASSERT(NULL != cfg80211);

  res = wave_radio_aid_get(wv_cfg80211_wave_radio_get(cfg80211), wdev->netdev, addr, &aid);
  if (res)
  {
    goto end_0;
  }

  rsp_nbuf = wv_cfg80211_vendor_cmd_alloc_reply_skb(wiphy, aid_len);
  if (!rsp_nbuf)
  {
    res = MTLK_ERR_NO_MEM;
    goto end_1;
  }

  nla_put(rsp_nbuf, NL80211_ATTR_VENDOR_DATA, aid_len, &aid);
  return wv_cfg80211_vendor_cmd_reply(rsp_nbuf);

end_0: /* w/o error-code conversion */
  return res;
end_1:
  return _mtlk_df_mtlk_to_linux_error_code(res);
}

static int _wave_cfg80211_vendor_hapd_free_aid(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  u16 *p_aid = (u16*)data;
  u16 aid = *p_aid;
  wv_cfg80211_t *cfg80211 = wiphy_priv(wiphy);
  struct net_device *ndev = wdev->netdev;
  MTLK_ASSERT(NULL != cfg80211);
  MTLK_ASSERT(NULL != ndev);
  return wave_radio_aid_free(wv_cfg80211_wave_radio_get(cfg80211), ndev, aid);
}

static int _wave_cfg80211_vendor_hapd_sync_done(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  wv_cfg80211_t *cfg80211 = wiphy_priv(wiphy);
  struct net_device *ndev = wdev->netdev;
  MTLK_ASSERT(NULL != cfg80211);
  MTLK_ASSERT(NULL != ndev);
  return wave_radio_sync_done(wv_cfg80211_wave_radio_get(cfg80211), ndev);
}

int wave_cfg80211_vendor_hapd_send_eapol(struct wiphy *wiphy, struct wireless_dev *wdev,
                  const void *data, int data_len)
{
  wv_cfg80211_t *cfg80211 = wiphy_priv(wiphy);
  MTLK_ASSERT(NULL != cfg80211);
  return wave_radio_eapol_send(wv_cfg80211_wave_radio_get(cfg80211), wdev, data, data_len);
}

static int _wave_cfg80211_vendor_hapd_send_initial_data(
    struct wiphy *wiphy,
    struct wireless_dev *wdev,
    const void *data,
    int data_len)
{
  wv_cfg80211_t *cfg80211 = wiphy_priv(wiphy);
  MTLK_ASSERT(NULL != cfg80211);
  return wave_radio_initial_data_send(wv_cfg80211_wave_radio_get(cfg80211), wdev->netdev, data, data_len);
}

static int _wave_cfg80211_vendor_hapd_send_dfs_debug_radar_required_chan(
    struct wiphy *wiphy,
    struct wireless_dev *wdev,
    const void *data,
    int data_len)
{
  wv_cfg80211_t *cfg80211 = wiphy_priv(wiphy);
  MTLK_ASSERT(NULL != cfg80211);
  return wave_radio_send_dfs_debug_radar_required_chan(wv_cfg80211_wave_radio_get(cfg80211), wdev->netdev, data, data_len);
}

static int _wave_cfg80211_vendor_hapd_change_dfs_flags(
    struct wiphy *wiphy,
    struct wireless_dev *wdev,
    const void *data,
    int data_len)
{
  wv_cfg80211_t *cfg80211 = wiphy_priv(wiphy);
  MTLK_ASSERT(NULL != cfg80211);
  return wave_radio_dfs_flags_change(wv_cfg80211_wave_radio_get(cfg80211), wdev->netdev, data, data_len);
}

static int _wave_cfg80211_vendor_hapd_set_deny_mac(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  wv_cfg80211_t *cfg80211 = wiphy_priv(wiphy);
  MTLK_ASSERT(NULL != cfg80211);
  return wave_radio_deny_mac_set(wv_cfg80211_wave_radio_get(cfg80211), wdev->netdev, data, data_len);
}

static int _wave_cfg80211_vendor_hapd_sta_steer(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  wv_cfg80211_t *cfg80211 = wiphy_priv(wiphy);
  MTLK_ASSERT(NULL != cfg80211);
  return wave_radio_sta_steer(wv_cfg80211_wave_radio_get(cfg80211), wdev->netdev, data, data_len);
}

static int _wave_cfg80211_vendor_hapd_get_sta_measurements(struct wiphy *wiphy,
  struct wireless_dev *wdev, const void *data, int data_len)
{
  wv_cfg80211_t *cfg80211 = wiphy_priv(wiphy);
  MTLK_ASSERT(NULL != cfg80211);
  return wave_radio_sta_measurements_get(wv_cfg80211_wave_radio_get(cfg80211), wdev->netdev, data, data_len);
}

static int _wave_cfg80211_vendor_hapd_get_vap_measurements(struct wiphy *wiphy,
  struct wireless_dev *wdev, const void *data, int data_len)
{
  wv_cfg80211_t *cfg80211 = wiphy_priv(wiphy);
  MTLK_ASSERT(NULL != cfg80211);
  return wave_radio_vap_measurements_get(wv_cfg80211_wave_radio_get(cfg80211), wdev->netdev, data, data_len);
}

int wave_cfg80211_vendor_hapd_get_radio_info(struct wiphy *wiphy,
  struct wireless_dev *wdev, const void *data, int data_len)
{
  return wave_radio_radio_info_get(wiphy, wdev, data, data_len);
}

int wave_cfg80211_vendor_hapd_get_unconnected_sta(struct wiphy *wiphy,
  struct wireless_dev *wdev, const void *data, int data_len)
{
  wave_radio_t *radio;

  if (wdev->iftype == NL80211_IFTYPE_AP) {
    wv_cfg80211_t *cfg80211 = wiphy_priv(wiphy);
    MTLK_ASSERT(NULL != cfg80211);
    radio = wv_cfg80211_wave_radio_get(cfg80211);
  } else {
    struct ieee80211_hw *hw = wiphy_to_ieee80211_hw(wiphy);
    radio = wv_ieee80211_hw_radio_get(hw);
  }

  return wave_radio_unconnected_sta_get(radio, wdev, data, data_len);
}

static int _wave_cfg80211_vendor_hapd_set_atf_quotas (struct wiphy *wiphy,
  struct wireless_dev *wdev, const void *data, int total_len)
{
  wv_cfg80211_t *cfg80211 = wiphy_priv(wiphy);
  MTLK_ASSERT(NULL != cfg80211);

  return wave_radio_set_atf_quotas(wv_cfg80211_wave_radio_get(cfg80211), wdev->netdev, data, total_len);
}

static int _wave_cfg80211_vendor_hapd_set_wds_wpa_sta (struct wiphy *wiphy,
  struct wireless_dev *wdev, const void *data, int data_len)
{
  wv_cfg80211_t *cfg80211 = wiphy_priv(wiphy);
  MTLK_ASSERT(NULL != cfg80211);

  return wave_radio_set_wds_wpa_sta(wv_cfg80211_wave_radio_get(cfg80211), wdev->netdev, data, data_len);
}

static int _wave_cfg80211_vendor_hapd_set_dgaf_disabled (struct wiphy *wiphy,
  struct wireless_dev *wdev, const void *data, int data_len)
{
  wv_cfg80211_t *cfg80211 = wiphy_priv(wiphy);
  MTLK_ASSERT(NULL != cfg80211);

  return wave_radio_set_dgaf_disabled(wv_cfg80211_wave_radio_get(cfg80211), wdev->netdev, data, data_len);
}

static int _wave_cfg80211_vendor_hapd_block_tx (struct wiphy *wiphy,
  struct wireless_dev *wdev, const void *data, int total_len)
{
  wv_cfg80211_t *cfg80211 = wiphy_priv(wiphy);
  MTLK_ASSERT(NULL != cfg80211);

  mtlk_core_cfg_set_block_tx(_wave_cfg80211_get_maste_core(cfg80211), 1);

  return _mtlk_df_mtlk_to_linux_error_code(MTLK_ERR_OK);
}

static int _wave_cfg80211_vendor_hapd_set_bss_load(struct wiphy *wiphy, struct wireless_dev *wdev,
                                const void *data, int data_len)
{
  mtlk_df_user_t *df_user = mtlk_df_user_from_wdev(wdev);
  mtlk_pdb_t *param_db;
  int res = MTLK_ERR_OK;
  uint8 value;

  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  MTLK_CHECK_DF_USER(df_user);

  if (data_len != sizeof(value)) {
      ELOG_D("Wrong bss_load data length: %d", data_len);
      res = MTLK_ERR_PARAMS;
      goto end;
  }
  value = * (uint8 *) data;

  param_db = mtlk_vap_get_param_db(mtlk_df_get_vap_handle(mtlk_df_user_get_df(df_user)));
  wave_pdb_set_int(param_db, PARAM_DB_CORE_IS_BSS_LOAD_ENABLE, value);

end:
  return _mtlk_df_mtlk_to_linux_error_code(res);
}

static int _wave_cfg80211_vendor_hapd_set_mesh_mode(struct wiphy *wiphy, struct wireless_dev *wdev,
    const void *data, int data_len)
{
  mtlk_df_user_t *df_user = mtlk_df_user_from_wdev(wdev);
  mtlk_df_t *df;
  mtlk_vap_handle_t vap_handle;
  mtlk_pdb_t *param_db;
  int res = MTLK_ERR_OK;
  mesh_mode_t mesh_mode;

  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  MTLK_CHECK_DF_USER(df_user);

  df         = mtlk_df_user_get_df(df_user);
  vap_handle = mtlk_df_get_vap_handle(df);
  param_db   = mtlk_vap_get_param_db(vap_handle);

  if (data_len != sizeof(mesh_mode)) {
      ELOG_D("Wrong mesh_mode data length: %d", data_len);
      res = MTLK_ERR_PARAMS;
      goto end;
  }
  mesh_mode = * (mesh_mode_t *) data;

  if ((mesh_mode != 0) && !mtlk_vap_is_ap(vap_handle)) {
      /* Multi-AP mode can be set only for AP */
      res = MTLK_ERR_NOT_SUPPORTED;
      goto end;
  }
  wave_pdb_set_int(param_db, PARAM_DB_CORE_MESH_MODE, mesh_mode);

end:
  return _mtlk_df_mtlk_to_linux_error_code(res);
}

static int
_wave_cfg80211_vendor_hapd_set_softblock_threshold (struct wiphy *wiphy, struct wireless_dev *wdev,
                                const void *data, int data_len)
{
  int res = MTLK_ERR_OK;
  mtlk_df_user_t *df_user;
  mtlk_clpb_t *clpb = NULL;
  struct intel_vendor_blacklist_cfg softblocklist_cfg;
  mtlk_core_t *core;

  df_user = mtlk_df_user_from_wdev(wdev);
  MTLK_CHECK_DF_USER(df_user);

  core = mtlk_vap_get_core(mtlk_df_get_vap_handle(mtlk_df_user_get_df(df_user)));
  MTLK_ASSERT(NULL != core);

  if (MTLK_CORE_PDB_GET_INT(core, PARAM_DB_CORE_SOFTBLOCK_DISABLE)) {
    ELOG_V("softblock is disabled");
    res = MTLK_ERR_PARAMS;
    goto end;
  }

  /* Expecting Hostap to send only mac addr and the Probe Request High and Low Water Marks */
  if (data_len != sizeof(softblocklist_cfg)) {
    ELOG_DD("Wrong soft Blocklist data length: %d, expected= %d", data_len, sizeof(softblocklist_cfg));
    res = MTLK_ERR_PARAMS;
    goto end;
  }

  if (NULL == data) {
    ELOG_V("Softblock data is NULL");
    res = MTLK_ERR_PARAMS;
    goto end;
  }

  wave_memcpy(&softblocklist_cfg, sizeof(softblocklist_cfg), data, sizeof(softblocklist_cfg));

  if (!mtlk_osal_is_valid_ether_addr(softblocklist_cfg.addr.au8Addr))
  {
    ELOG_Y("Zero or broadcast or multicast MAC address not allowed %Y", softblocklist_cfg.addr.au8Addr);
    res = MTLK_ERR_PARAMS;
    goto end;
  }

  res = _mtlk_df_user_invoke_core(mtlk_df_user_get_df(df_user),
          WAVE_CORE_REQ_SET_SOFTBLOCKLIST_ENTRY, &clpb, &softblocklist_cfg,
          sizeof(softblocklist_cfg));
  res = _mtlk_df_user_process_core_retval(res, clpb,
          WAVE_CORE_REQ_SET_SOFTBLOCKLIST_ENTRY, TRUE);

end:
  return _mtlk_df_mtlk_to_linux_error_code(res);
}

static int _wave_cfg80211_vendor_hapd_check_4addr_mode(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  u8 *addr = (u8*)data;
  mtlk_df_user_t *df_user = mtlk_df_user_from_wdev(wdev);
  mtlk_clpb_t *clpb = NULL;
  mtlk_nbuf_t *rsp_nbuf;
  int res;
  struct vendor_check_4addr_mode *check_4addr_mode;
  uint32 check_4addr_mode_size;

  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  MTLK_CHECK_DF_USER(df_user);

  /* address */
  if (data_len != sizeof(IEEE_ADDR)) {
    ELOG_D("Wrong check_4addr_mode data length: %d", data_len);
    res = MTLK_ERR_PARAMS;
    goto end;
  }
  if (NULL == data) {
    ELOG_V("Check_4addr_mode data is NULL");
    res = MTLK_ERR_PARAMS;
    goto end;
  }
  if (mtlk_osal_eth_is_broadcast(addr)) {
    ELOG_V("Station address can't be broadcast address");
    res = MTLK_ERR_PARAMS;
    goto end;
  }
  if (!mtlk_osal_is_valid_ether_addr(addr)) {
    ELOG_Y("Invalid MAC address %Y", addr);
    res = MTLK_ERR_PARAMS;
    goto end;
  }

  res = _mtlk_df_user_invoke_core(mtlk_df_user_get_df(df_user), WAVE_CORE_REQ_CHECK_4ADDR_MODE, &clpb, addr, IEEE_ADDR_LEN);
  res = _mtlk_df_user_process_core_retval(res, clpb, WAVE_CORE_REQ_CHECK_4ADDR_MODE, FALSE);
  if (res != MTLK_ERR_OK)
    goto end;

  check_4addr_mode = mtlk_clpb_enum_get_next(clpb, &check_4addr_mode_size);
  if (!MTLK_CLPB_CHECK_DATA(check_4addr_mode, check_4addr_mode_size)) {
    mtlk_clpb_delete(clpb);
    res = MTLK_ERR_UNKNOWN;
    goto end;
  }

  rsp_nbuf = wv_cfg80211_vendor_cmd_alloc_reply_skb(wiphy, check_4addr_mode_size);
  if (!rsp_nbuf) {
    mtlk_clpb_delete(clpb);
    res = MTLK_ERR_NO_MEM;
    goto end;
  }

  ILOG2_D("sta_4addr_mode:%d", check_4addr_mode->sta_4addr_mode);
  nla_put(rsp_nbuf, NL80211_ATTR_VENDOR_DATA, check_4addr_mode_size, check_4addr_mode);

  mtlk_clpb_delete(clpb);
  return wv_cfg80211_vendor_cmd_reply(rsp_nbuf);

end:
  return _mtlk_df_mtlk_to_linux_error_code(res);
}

static int
_wave_cfg80211_vendor_hapd_set_disable_softblock (struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  mtlk_df_user_t *df_user = mtlk_df_user_from_wdev(wdev);
  mtlk_pdb_t *param_db;
  int res = MTLK_ERR_OK;
  uint32 value;

  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  MTLK_CHECK_DF_USER(df_user);

  if (data_len != sizeof(value)) {
    ELOG_D("Wrong disable softblock data length: %d", data_len);
    res = MTLK_ERR_PARAMS;
    goto end;
  }
  value = * (uint32 *) data;

  param_db = mtlk_vap_get_param_db(mtlk_df_get_vap_handle(mtlk_df_user_get_df(df_user)));
  wave_pdb_set_int(param_db, PARAM_DB_CORE_SOFTBLOCK_DISABLE, value);

end:
  return _mtlk_df_mtlk_to_linux_error_code(res);
}

static int
_wave_cfg80211_vendor_hapd_set_dfs_state (struct wiphy *wiphy, struct wireless_dev *wdev,
                                          const void *data, int data_len)
{
  int res = MTLK_ERR_OK;
  struct ieee80211_channel *chan;
  struct cfg80211_chan_def chandef;
  struct intel_vendor_dfs_chandef *hapd_dfs_chandef;

  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);

  if (data_len != sizeof(struct intel_vendor_dfs_chandef)) {
    ELOG_D("Wrong intel vendor dfs chandef data length: %d", data_len);
    res = MTLK_ERR_PARAMS;
    goto end;
  }

  hapd_dfs_chandef = (struct intel_vendor_dfs_chandef *)data;

  chan = ieee80211_get_channel(wiphy, hapd_dfs_chandef->primary_chan_freq);

  if (!chan) {
    ELOG_D("Failed to get channel data for freq %d", hapd_dfs_chandef->primary_chan_freq);
    res = MTLK_ERR_VALUE;
    goto end;
  }

  chandef.chan = chan;
  chandef.width = hapd_dfs_chandef->bandwidth;
  chandef.center_freq1 = hapd_dfs_chandef->center_freq1;
  chandef.center_freq2 = hapd_dfs_chandef->center_freq2;

  cfg80211_set_dfs_state(wiphy, &chandef, hapd_dfs_chandef->dfs_state);

end:
  return _mtlk_df_mtlk_to_linux_error_code(res);
}

/************************* SET FUNCTIONS *************************/
static int set_int_params(struct wiphy *wiphy, struct wireless_dev *wdev,
               const void *data, int data_len, uint16 min_num_of_params, uint16 max_num_of_params, uint16 cmd)
{
  int res = MTLK_ERR_VALUE;
  uint16 recv_num_of_params = data_len / sizeof(int);
  mtlk_df_user_t *df_user = mtlk_df_user_from_wdev(wdev);
  MTLK_CHECK_DF_USER(df_user);

  if (recv_num_of_params < min_num_of_params || recv_num_of_params > max_num_of_params)
    goto end;

  res = mtlk_df_ui_set_cfg(df_user, cmd, (char*) data, MTLK_IW_PRIW_PARAM(INT_ITEM,recv_num_of_params));

end:
  return _mtlk_df_mtlk_to_linux_error_code(res);
}

static int set_addr_params(struct wiphy *wiphy, struct wireless_dev *wdev,
               const void *data, int data_len, uint16 min_num_of_params, uint16 max_num_of_params, uint16 cmd)
{
  int res = MTLK_ERR_VALUE;
  struct sockaddr addr_param;
  uint16 recv_num_of_params = data_len / (sizeof(addr_param.sa_family) + ETH_ALEN);
  mtlk_df_user_t *df_user = mtlk_df_user_from_wdev(wdev);
  MTLK_CHECK_DF_USER(df_user);

  if (min_num_of_params != 1 || max_num_of_params != 1) {
    ELOG_V("Currently only one ADDR parameter is supported");
    goto end;
  }

  if (recv_num_of_params < min_num_of_params || recv_num_of_params > max_num_of_params)
    goto end;

  memset(&addr_param, 0, sizeof(addr_param));
  wave_memcpy(&addr_param, sizeof(addr_param), data, data_len);

  res = mtlk_df_ui_set_cfg(df_user, cmd, (char*) &addr_param, MTLK_IW_PRIW_PARAM(ADDR_ITEM,recv_num_of_params));

end:
  return _mtlk_df_mtlk_to_linux_error_code(res);
}

static int set_text_params(struct wiphy *wiphy, struct wireless_dev *wdev,
               const void *data, int data_len, uint16 cmd)
{
  int res;
  uint16 recv_num_of_params = data_len / sizeof(char);
  mtlk_df_user_t *df_user = mtlk_df_user_from_wdev(wdev);
  MTLK_CHECK_DF_USER(df_user);

  res = mtlk_df_ui_set_cfg(df_user, cmd, (char*) data, MTLK_IW_PRIW_PARAM(TEXT_ITEM,recv_num_of_params));
  return _mtlk_df_mtlk_to_linux_error_code(res);
}

static int _wave_cfg80211_vendor_set_LongRetryLimit(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return set_int_params(wiphy, wdev, data, data_len, 1, 1, MIB_LONG_RETRY_LIMIT);
}

static int _wave_cfg80211_vendor_set_DTIMPeriod(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return set_int_params(wiphy, wdev, data, data_len, 1, 1, MIB_DTIM_PERIOD);
}

static int _wave_cfg80211_vendor_set_BeaconPeriod(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return set_int_params(wiphy, wdev, data, data_len, 1, 1, MIB_BEACON_PERIOD);
}

static int _wave_cfg80211_vendor_set_11hRadarDetect(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return set_int_params(wiphy, wdev, data, data_len, 1, 1, PRM_ID_11H_RADAR_DETECTION);
}

static int _wave_cfg80211_vendor_set_11hBeaconCount(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return set_int_params(wiphy, wdev, data, data_len, 1, 1, PRM_ID_11H_BEACON_COUNT);
}

static int _wave_cfg80211_vendor_set_11hChCheckTime(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return set_int_params(wiphy, wdev, data, data_len, 1, 1, PRM_ID_11H_CHANNEL_AVAILABILITY_CHECK_TIME);
}

static int _wave_cfg80211_vendor_set_NonOccupatePrd(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return set_int_params(wiphy, wdev, data, data_len, 1, 1, PRM_ID_11H_NOP);
}

static int _wave_cfg80211_vendor_set_11hEmulatRadar(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return set_int_params(wiphy, wdev, data, data_len, 1, 2, PRM_ID_11H_EMULATE_RADAR_DETECTION);
}

static int _wave_cfg80211_vendor_set_AddPeerAP(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return set_addr_params(wiphy, wdev, data, data_len, 1, 1, PRM_ID_ADD_PEERAP);
}

static int _wave_cfg80211_vendor_set_DelPeerAP(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return set_addr_params(wiphy, wdev, data, data_len, 1, 1, PRM_ID_DEL_PEERAP);
}

static int _wave_cfg80211_vendor_set_PeerAPkeyIdx(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return set_int_params(wiphy, wdev, data, data_len, 1, 1, PRM_ID_PEERAP_KEY_IDX);
}

static int _wave_cfg80211_vendor_set_11dActive(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return set_int_params(wiphy, wdev, data, data_len, 1, 1, PRM_ID_11D);
}

static int _wave_cfg80211_vendor_set_MACWdTimeoutMs(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return set_int_params(wiphy, wdev, data, data_len, 1, 1, PRM_ID_MAC_WATCHDOG_TIMEOUT_MS);
}

static int _wave_cfg80211_vendor_set_MACWdPeriodMs(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return set_int_params(wiphy, wdev, data, data_len, 1, 1, PRM_ID_MAC_WATCHDOG_PERIOD_MS);
}

static int _wave_cfg80211_vendor_set_StaKeepaliveIN(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return set_int_params(wiphy, wdev, data, data_len, 1, 1, PRM_ID_STA_KEEPALIVE_INTERVAL);
}

static int _wave_cfg80211_vendor_set_BridgeMode(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return set_int_params(wiphy, wdev, data, data_len, 1, 1, PRM_ID_BRIDGE_MODE);
}

static int _wave_cfg80211_vendor_set_ReliableMcast(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return set_int_params(wiphy, wdev, data, data_len, 1, 1, PRM_ID_RELIABLE_MULTICAST);
}

static int _wave_cfg80211_vendor_set_APforwarding(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return set_int_params(wiphy, wdev, data, data_len, 1, 1, PRM_ID_AP_FORWARDING);
}

static int _wave_cfg80211_vendor_set_BasicRateSet(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return set_int_params(wiphy, wdev, data, data_len, 1, 1, PRM_ID_BSS_BASIC_RATE_SET);
}

static int _wave_cfg80211_vendor_set_HiddenSSID(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return set_int_params(wiphy, wdev, data, data_len, 1, 1, PRM_ID_HIDDEN_SSID);
}

static int _wave_cfg80211_vendor_set_UpRescanExmpTm(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return set_int_params(wiphy, wdev, data, data_len, 1, 1, PRM_ID_UP_RESCAN_EXEMPTION_TIME);
}

static int _wave_cfg80211_vendor_set_FwLogSeverity(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return set_int_params(wiphy, wdev, data, data_len, 2, 2, PRM_ID_FW_LOG_SEVERITY);
}

static int _wave_cfg80211_vendor_set_WDSHostTO(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return set_int_params(wiphy, wdev, data, data_len, 1, 1, PRM_ID_WDS_HOST_TIMEOUT);
}

static int _wave_cfg80211_vendor_set_IRBPngEnabled(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
#ifdef CPTCFG_IWLWAV_IRB_DEBUG
  return set_int_params(wiphy, wdev, data, data_len, 1, 1, PRM_ID_IRB_PINGER_ENABLED);
#else
  return _mtlk_df_mtlk_to_linux_error_code(MTLK_ERR_NO_ENTRY);
#endif
}

static int _wave_cfg80211_vendor_set_IRBPngStatsRst(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
#ifdef CPTCFG_IWLWAV_IRB_DEBUG
  return set_int_params(wiphy, wdev, data, data_len, 1, 1, PRM_ID_IRB_PINGER_STATS);
#else
  return _mtlk_df_mtlk_to_linux_error_code(MTLK_ERR_NO_ENTRY);
#endif
}

static int _wave_cfg80211_vendor_set_LtPathEnabled(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
#if MTLK_USE_DIRECTCONNECT_DP_API
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return set_int_params(wiphy, wdev, data, data_len, 1, 1, PRM_ID_DCDP_API_LITEPATH);
#else
  return _mtlk_df_mtlk_to_linux_error_code(MTLK_ERR_NO_ENTRY);
#endif
}

static int _wave_cfg80211_vendor_set_IpxPpaEnabled(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
#if MTLK_USE_DIRECTCONNECT_DP_API
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return set_int_params(wiphy, wdev, data, data_len, 1, 1, PRM_ID_DCDP_API_LITEPATH);
#else
  return _mtlk_df_mtlk_to_linux_error_code(MTLK_ERR_NO_ENTRY);
#endif
}

static int _wave_cfg80211_vendor_set_UdmaEnabled(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
#if MTLK_USE_PUMA6_UDMA
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return set_int_params(wiphy, wdev, data, data_len, 1, 1, PRM_ID_UDMA_API);
#else
  return _mtlk_df_mtlk_to_linux_error_code(MTLK_ERR_NO_ENTRY);
#endif
}

static int _wave_cfg80211_vendor_set_UdmaEnabledExt(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
#if MTLK_USE_PUMA6_UDMA
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return set_int_params(wiphy, wdev, data, data_len, 1, 1, PRM_ID_UDMA_API_EXT);
#else
  return _mtlk_df_mtlk_to_linux_error_code(MTLK_ERR_NO_ENTRY);
#endif
}

static int _wave_cfg80211_vendor_set_UdmaVlanId(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
#if MTLK_USE_PUMA6_UDMA
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return set_int_params(wiphy, wdev, data, data_len, 1, 1, PRM_ID_UDMA_VLAN_ID);
#else
  return _mtlk_df_mtlk_to_linux_error_code(MTLK_ERR_NO_ENTRY);
#endif
}

static int _wave_cfg80211_vendor_set_UdmaVlanIdExt(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
#if MTLK_USE_PUMA6_UDMA
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return set_int_params(wiphy, wdev, data, data_len, 1, 1, PRM_ID_UDMA_VLAN_ID_EXT);
#else
  return _mtlk_df_mtlk_to_linux_error_code(MTLK_ERR_NO_ENTRY);
#endif
}

static int _wave_cfg80211_vendor_set_CoCPower(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return set_int_params(wiphy, wdev, data, data_len, 1, 3, PRM_ID_COC_POWER_MODE);
}

static int _wave_cfg80211_vendor_set_CoCAutoCfg(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return set_int_params(wiphy, wdev, data, data_len, 10, 10, PRM_ID_COC_AUTO_PARAMS);
}

static int _wave_cfg80211_vendor_set_PCoCPower(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
#ifdef CPTCFG_IWLWAV_PMCU_SUPPORT
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return set_int_params(wiphy, wdev, data, data_len, 1, 1, PRM_ID_PCOC_POWER_MODE);
#else
  return _mtlk_df_mtlk_to_linux_error_code(MTLK_ERR_NO_ENTRY);
#endif
}

static int _wave_cfg80211_vendor_set_PCoCAutoCfg(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
#ifdef CPTCFG_IWLWAV_PMCU_SUPPORT
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return set_int_params(wiphy, wdev, data, data_len, 4, 4, PRM_ID_PCOC_AUTO_PARAMS);
#else
  return _mtlk_df_mtlk_to_linux_error_code(MTLK_ERR_NO_ENTRY);
#endif
}

static int _wave_cfg80211_vendor_set_PMCUDebug(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
#ifdef CPTCFG_IWLWAV_PMCU_SUPPORT
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return set_int_params(wiphy, wdev, data, data_len, 1, 1, PRM_ID_PCOC_PMCU_DEBUG);
#else
  return _mtlk_df_mtlk_to_linux_error_code(MTLK_ERR_NO_ENTRY);
#endif
}

static int _wave_cfg80211_vendor_set_TpcLoopType(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return set_int_params(wiphy, wdev, data, data_len, 1, 1, PRM_ID_TPC_LOOP_TYPE);
}

static int _wave_cfg80211_vendor_set_InterfDetTime(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return set_int_params(wiphy, wdev, data, data_len, 6, 6, PRM_ID_INTERFER_TIMEOUTS);
}

static int _wave_cfg80211_vendor_set_InterfDetThresh(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return set_int_params(wiphy, wdev, data, data_len, 6, 6, PRM_ID_INTERFER_THRESH);
}

static int _wave_cfg80211_vendor_set_InterfDetScan(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return set_int_params(wiphy, wdev, data, data_len, 2, 2, PRM_ID_INTERFER_SCANTIMES);
}

static int _wave_cfg80211_vendor_set_11bAntSelection(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return set_int_params(wiphy, wdev, data, data_len, 3, 3, PRM_ID_11B_ANTENNA_SELECTION);
}

static int _wave_cfg80211_vendor_set_DoSimpleCLI(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return set_int_params(wiphy, wdev, data, data_len, 1, 4, PRM_ID_DBG_CLI);
}

static int _wave_cfg80211_vendor_set_DoFwDebug(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return set_int_params(wiphy, wdev, data, data_len, 1, 2, PRM_ID_FW_DEBUG);
}

static int _wave_cfg80211_vendor_set_DoDebugAssert(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  #define MAX_LMIPS_NUMBER    1   /* 0 or 1 are allowed */

  uint32 *int_data = (uint32*)data;
  mtlk_df_user_t *df_user = mtlk_df_user_from_wdev(wdev);
  mtlk_df_t *master_df = NULL;
  int res = MTLK_ERR_PARAMS;
  int arg_assert_type = MTLK_CORE_UI_ASSERT_TYPE_NONE;
  int mips_no = -1;
  unsigned nr_1, nr_2 = 0;
  mtlk_vap_handle_t vap_handle = NULL;
  mtlk_hw_t *hw = NULL;
  mtlk_clpb_t *clpb = NULL;
  
  mtlk_core_ui_dbg_assert_type_e    arg1_to_assert_type_table[] = {
                      MTLK_CORE_UI_ASSERT_TYPE_NONE,      /* 0 */
                      MTLK_CORE_UI_ASSERT_TYPE_FW_UMIPS,  /* 1 */
                      MTLK_CORE_UI_ASSERT_TYPE_FW_LMIPS0, /* 2 0 */
                      MTLK_CORE_UI_ASSERT_TYPE_ALL_MACS,  /* 3 */
                      MTLK_CORE_UI_ASSERT_TYPE_DRV_DIV0,  /* 4 */
                      MTLK_CORE_UI_ASSERT_TYPE_DRV_BLOOP  /* 5 */ };

  MTLK_CHECK_DF_USER(df_user);

  master_df = mtlk_df_user_get_master_df(df_user);
  vap_handle = mtlk_df_get_vap_handle(master_df);
  hw = mtlk_vap_get_hw(vap_handle);

  if (data_len != sizeof(uint32) && data_len != 2*sizeof(uint32)) {
        ELOG_DD("CID-%04x: Wrong number of parameters: %d", mtlk_vap_get_oid(vap_handle), data_len);
    goto end;
  }
  nr_1 = *int_data;
  if (data_len == 2*sizeof(uint32))
    nr_2 = *(int_data + 1);

  if (nr_1 < MTLK_ARRAY_SIZE(arg1_to_assert_type_table)) {
    arg_assert_type = arg1_to_assert_type_table[nr_1];
  }

  if (arg_assert_type == MTLK_CORE_UI_ASSERT_TYPE_NONE) {
    ELOG_DD("CID-%04x: Unsupported assert type: %d", mtlk_vap_get_oid(vap_handle), nr_1);
    goto end;
  }

  /* Special cases for LMIPS0/LMIPS1 */
  if(arg_assert_type == MTLK_CORE_UI_ASSERT_TYPE_FW_LMIPS0) {
    if (nr_2 <= MAX_LMIPS_NUMBER) {
      arg_assert_type += nr_2;
    } else {
      ELOG_DD("CID-%04x: Illegal LowerMAC number: %d", mtlk_vap_get_oid(vap_handle), nr_2);
      goto end;
    }
  }

  if ((MTLK_CORE_UI_ASSERT_TYPE_FW_UMIPS <= arg_assert_type) &&
      (arg_assert_type <= (MTLK_CORE_UI_ASSERT_TYPE_FW_LMIPS0 + MAX_LMIPS_NUMBER)))
  {
    mips_no = hw_assert_type_to_core_nr(hw, arg_assert_type);
    if (mips_no < 0) {
      ELOG_DDD("CID-%04x: Can't get MAC number from (%d, %d)", mtlk_vap_get_oid(vap_handle), nr_1, nr_2);
      goto end;
    }
  }

  switch (arg_assert_type)
  {
  case MTLK_CORE_UI_ASSERT_TYPE_ALL_MACS:
    ILOG0_D("CID-%04x: All CPUs assertion without any events confirmation", mtlk_vap_get_oid(vap_handle));
    break;
  case MTLK_CORE_UI_ASSERT_TYPE_FW_LMIPS0:
    /* Check whether the LMIPS0 has already been asserted. If so - doesn't call the Core */
    res = wave_rcvry_mac_hang_evt_is_set(hw, mips_no);
    if (res == MTLK_ERR_OK) {
      ILOG0_D("CID-%04x: LMIPS0 already asserted", mtlk_vap_get_oid(vap_handle));
      goto end;
    }
    break;
  case MTLK_CORE_UI_ASSERT_TYPE_FW_LMIPS1:
    /* Check whether the LMIPS1 has already been asserted. If so - doesn't call the Core */
    res = wave_rcvry_mac_hang_evt_is_set(hw, mips_no);
    if (res == MTLK_ERR_OK) {
      ILOG0_D("CID-%04x: LMIPS1 already asserted", mtlk_vap_get_oid(vap_handle));
      goto end;
    }
    break;
  case MTLK_CORE_UI_ASSERT_TYPE_FW_UMIPS:
    /* Check whether the UMIPS has already been asserted. If so - doesn't call the Core */
    res = wave_rcvry_mac_hang_evt_is_set(hw, mips_no);
    if (res == MTLK_ERR_OK) {
      ILOG0_D("CID-%04x: UMIPS already asserted", mtlk_vap_get_oid(vap_handle));
      goto end;
    }
    break;
  default:
    break;
  }

  res = _mtlk_df_user_invoke_core(master_df, WAVE_RADIO_REQ_SET_MAC_ASSERT, &clpb,
                                   &arg_assert_type, sizeof(arg_assert_type));
  _mtlk_df_user_process_core_retval_void(res, clpb, WAVE_RADIO_REQ_SET_MAC_ASSERT, TRUE);

  if (res == MTLK_ERR_NOT_READY) {
    ILOG0_D("CID-%04x: Already asserted", mtlk_vap_get_oid(vap_handle));
    goto end;
  }

  switch (arg_assert_type)
  {
  case MTLK_CORE_UI_ASSERT_TYPE_ALL_MACS:
    /* All CPUs assertion without any events confirmation */
    /* res = MTLK_ERR_OK; */
    break;
  case MTLK_CORE_UI_ASSERT_TYPE_FW_LMIPS0:
    /* Wait for LMIPS0 to detect ASSERTION */
    res = wave_rcvry_mac_hang_evt_wait(hw, mips_no);
    if (res != MTLK_ERR_OK) {
      WLOG_DD("CID-%04x: LMIPS0 assertion failed (res=%d)", mtlk_vap_get_oid(vap_handle), res);
    }
    else {
      ILOG0_D("CID-%04x: LMIPS0 asserted", mtlk_vap_get_oid(vap_handle));
    }
    break;
  case MTLK_CORE_UI_ASSERT_TYPE_FW_LMIPS1:
    /* Wait for LMIPS1 to detect ASSERTION */
    res = wave_rcvry_mac_hang_evt_wait(hw, mips_no);
    if (res != MTLK_ERR_OK) {
      WLOG_DD("CID-%04x: LMIPS1 assertion failed (res=%d)", mtlk_vap_get_oid(vap_handle), res);
    }
    else {
      ILOG0_D("CID-%04x: LMIPS1 asserted", mtlk_vap_get_oid(vap_handle));
    }
    break;
  case MTLK_CORE_UI_ASSERT_TYPE_FW_UMIPS:
    res = wave_rcvry_mac_hang_evt_wait(hw, mips_no);
    if (res != MTLK_ERR_OK) {
      WLOG_DD("CID-%04x: UMIPS assertion failed (res=%d)", mtlk_vap_get_oid(vap_handle), res);
    }
    else {
      ILOG0_D("CID-%04x: UMIPS asserted", mtlk_vap_get_oid(vap_handle));
    }
    break;
  default:
    break;
  }

end:
  return _mtlk_df_mtlk_to_linux_error_code(res);

  #undef MAX_LMIPS_NUMBER
}

static int _wave_cfg80211_vendor_set_TATimerRes(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return set_int_params(wiphy, wdev, data, data_len, 1, 1, PRM_ID_TA_TIMER_RESOLUTION);
}

static int _wave_cfg80211_vendor_set_FWRecovery(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return set_int_params(wiphy, wdev, data, data_len, 5, 5, PRM_ID_FW_RECOVERY);
}

static int _wave_cfg80211_vendor_set_ScanParams(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return set_int_params(wiphy, wdev, data, data_len, 6, 6, PRM_ID_SCAN_PARAMS);
}

static int _wave_cfg80211_vendor_set_ScanParamsBG(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return set_int_params(wiphy, wdev, data, data_len, 6, 6, PRM_ID_SCAN_PARAMS_BG);
}

static int _wave_cfg80211_vendor_set_ScanModifFlags(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return set_int_params(wiphy, wdev, data, data_len, 1, 1, PRM_ID_SCAN_MODIFS);
}

static int _wave_cfg80211_vendor_set_ScanCalCwMasks(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return set_int_params(wiphy, wdev, data, data_len, 2, 2, PRM_ID_CALIB_CW_MASKS);
}

static int _wave_cfg80211_vendor_set_ScanExpTime(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return set_int_params(wiphy, wdev, data, data_len, 1, 1, PRM_ID_SCAN_EXP_TIME);
}

static int _wave_cfg80211_vendor_set_TaskletLimits(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return set_int_params(wiphy, wdev, data, data_len, 5, 5, PRM_ID_TASKLET_LIMITS);
}

static int _wave_cfg80211_vendor_set_EnableRadio(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return set_int_params(wiphy, wdev, data, data_len, 1, 1, PRM_ID_RADIO_MODE);
}

static int _wave_cfg80211_vendor_set_AggrConfig(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return set_int_params(wiphy, wdev, data, data_len, 3, 3, PRM_ID_AGGR_CONFIG);
}

static int _wave_cfg80211_vendor_set_NumMsduInAmsdu(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return set_int_params(wiphy, wdev, data, data_len, 1, 3, PRM_ID_AMSDU_NUM);
}

static int _wave_cfg80211_vendor_set_AggRateLimit(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return set_int_params(wiphy, wdev, data, data_len, 2, 2, PRM_ID_AGG_RATE_LIMIT);
}

static int _wave_cfg80211_vendor_set_BssLoadInBeacon(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return set_int_params(wiphy, wdev, data, data_len, 1, 1, PRM_ID_BSS_LOAD_IN_BEACON);
}

static int _wave_cfg80211_vendor_set_AvailAdmCap(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return set_int_params(wiphy, wdev, data, data_len, 1, 1, PRM_ID_ADMISSION_CAPACITY);
}

static int _wave_cfg80211_vendor_set_SetRxTH(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return set_int_params(wiphy, wdev, data, data_len, 1, 1, PRM_ID_RX_THRESHOLD);
}

static int _wave_cfg80211_vendor_set_RxDutyCyc(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return set_int_params(wiphy, wdev, data, data_len, 2, 2, PRM_ID_RX_DUTY_CYCLE);
}

static int _wave_cfg80211_vendor_set_PowerSelection(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return set_int_params(wiphy, wdev, data, data_len, 1, 1, PRM_ID_TX_POWER_LIMIT_OFFSET);
}

static int _wave_cfg80211_vendor_set_11nProtection(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return set_int_params(wiphy, wdev, data, data_len, 1, 1, PRM_ID_PROTECTION_METHOD);
}

static int _wave_cfg80211_vendor_set_CalibOnDemand(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return set_int_params(wiphy, wdev, data, data_len, 1, 1, PRM_ID_TEMPERATURE_SENSOR);
}

static int _wave_cfg80211_vendor_set_QAMplus(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return set_int_params(wiphy, wdev, data, data_len, 1, 1, PRM_ID_QAMPLUS_MODE);
}

static int _wave_cfg80211_vendor_set_AcsUpdateTo(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return set_int_params(wiphy, wdev, data, data_len, 1, 1, PRM_ID_ACS_UPDATE_TO);
}

static int _wave_cfg80211_vendor_set_MuOperation(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return set_int_params(wiphy, wdev, data, data_len, 1, 1, PRM_ID_MU_OPERATION);
}

static int _wave_cfg80211_vendor_set_CcaTh(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return set_int_params(wiphy, wdev, data, data_len, 5, 5, PRM_ID_CCA_THRESHOLD);
}

static int _wave_cfg80211_vendor_set_CcaAdapt(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return set_int_params(wiphy, wdev, data, data_len, 7, 7, PRM_ID_CCA_ADAPT);
}

static int _wave_cfg80211_vendor_set_RadarRssiTh(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return set_int_params(wiphy, wdev, data, data_len, 1, 1, PRM_ID_RADAR_RSSI_TH);
}

static int _wave_cfg80211_vendor_set_CpuDmaLatency(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
#ifdef CPTCFG_IWLWAV_SET_PM_QOS
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return set_int_params(wiphy, wdev, data, data_len, 1, 1, PRM_ID_CPU_DMA_LATENCY);
#else
  return _mtlk_df_mtlk_to_linux_error_code(MTLK_ERR_NO_ENTRY);
#endif
}

static int _wave_cfg80211_vendor_set_RTSmode(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return set_int_params(wiphy, wdev, data, data_len, 2, 2, PRM_ID_RTS_MODE);
}

static int _wave_cfg80211_vendor_set_MaxMpduLen(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return set_int_params(wiphy, wdev, data, data_len, 1, 1, PRM_ID_MAX_MPDU_LENGTH);
}

static int _wave_cfg80211_vendor_set_BfMode(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return set_int_params(wiphy, wdev, data, data_len, 1, 1, PRM_ID_BF_MODE);
}

static int _wave_cfg80211_vendor_set_FixedRateCfg(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return set_int_params(wiphy, wdev, data, data_len, 11, 11, PRM_ID_FIXED_RATE);
}

static int _wave_cfg80211_vendor_set_ActiveAntMask(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return set_int_params(wiphy, wdev, data, data_len, 1, 1, PRM_ID_ACTIVE_ANT_MASK);
}

static int _wave_cfg80211_vendor_set_CtrlBFilterBank(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return set_int_params(wiphy, wdev, data, data_len, 1, 1, PRM_ID_IRE_CTRL_B);
}

static int _wave_cfg80211_vendor_set_FourAddrMode(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return set_int_params(wiphy, wdev, data, data_len, 1, 1, PRM_ID_4ADDR_MODE);
}

static int _wave_cfg80211_vendor_set_AddFourAddrSta(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return set_addr_params(wiphy, wdev, data, data_len, 1, 1, PRM_ID_4ADDR_STA_ADD);
}

static int _wave_cfg80211_vendor_set_DelFourAddrSta(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return set_addr_params(wiphy, wdev, data, data_len, 1, 1, PRM_ID_4ADDR_STA_DEL);
}

static int _wave_cfg80211_vendor_set_TxopConfig(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return set_int_params(wiphy, wdev, data, data_len, 2, 2, PRM_ID_TXOP_CONFIG);
}

static int _wave_cfg80211_vendor_set_SsbMode(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return set_int_params(wiphy, wdev, data, data_len, 2, 2, PRM_ID_SSB_MODE);
}

static int _wave_cfg80211_vendor_set_EnableMRCoex(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return set_int_params(wiphy, wdev, data, data_len, 1, 1, PRM_ID_COEX_ENABLE);
}

static int _wave_cfg80211_vendor_set_ConfigMRCoex(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return set_int_params(wiphy, wdev, data, data_len, 1, 1, PRM_ID_COEX_CFG);
}

static int _wave_cfg80211_vendor_set_SlowProbingMask(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return set_int_params(wiphy, wdev, data, data_len, 1, 1, PRM_ID_PROBING_MASK);
}

static int _wave_cfg80211_vendor_set_McastRange(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return set_text_params(wiphy, wdev, data, data_len, PRM_ID_MCAST_RANGE_SETUP);
}

static int _wave_cfg80211_vendor_set_McastRange6(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return set_text_params(wiphy, wdev, data, data_len, PRM_ID_MCAST_RANGE_SETUP_IPV6);
}

static int _wave_cfg80211_vendor_set_OnlineACM(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return set_int_params(wiphy, wdev, data, data_len, 1, 1, PRM_ID_ONLINE_CALIBRATION_ALGO_MASK);
}

static int _wave_cfg80211_vendor_set_AlgoCalibrMask(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return set_int_params(wiphy, wdev, data, data_len, 1, 1, PRM_ID_CALIBRATION_ALGO_MASK);
}

static int _wave_cfg80211_vendor_set_ShortCyclcPrfx(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return set_int_params(wiphy, wdev, data, data_len, 1, 1, PRM_ID_USE_SHORT_CYCLIC_PREFIX);
}

static int _wave_cfg80211_vendor_set_FreqJumpMode(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return set_int_params(wiphy, wdev, data, data_len, 1, 1, PRM_ID_FREQ_JUMP_MODE);
}

static int _wave_cfg80211_vendor_set_FixedPower(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return set_int_params(wiphy, wdev, data, data_len, 4, 4, PRM_ID_FIXED_POWER);
}

static int _wave_cfg80211_vendor_set_UnconnTime(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return set_int_params(wiphy, wdev, data, data_len, 1, 1, PRM_ID_UNCONNECTED_STA_SCAN_TIME);
}

static int _wave_cfg80211_vendor_set_RestrictAcMode(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return set_int_params(wiphy, wdev, data, data_len, 4, 4, PRM_ID_RESTRICTED_AC_MODE);
}

static int _wave_cfg80211_vendor_set_PdThresh(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return set_int_params(wiphy, wdev, data, data_len, 3, 3, PRM_ID_PD_THRESHOLD);
}

static int _wave_cfg80211_vendor_set_FastDrop(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return set_int_params(wiphy, wdev, data, data_len, 1, 1, PRM_ID_FAST_DROP);
}

static int _wave_cfg80211_vendor_set_ErpSet(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return set_int_params(wiphy, wdev, data, data_len, 4, 4, PRM_ID_ERP);
}

static int _wave_cfg80211_vendor_set_DmrConfig(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return set_int_params(wiphy, wdev, data, data_len, 1, 1, PRM_ID_FAST_DYNAMIC_MC_RATE);
}

static int _wave_cfg80211_vendor_set_CountersSrc(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return set_int_params(wiphy, wdev, data, data_len, 1, 1, PRM_ID_SWITCH_COUNTERS_SRC);
}

static int _wave_cfg80211_vendor_set_FixedLtfGi(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return set_int_params(wiphy, wdev, data, data_len, 2, 2, PRM_ID_FIXED_LTF_AND_GI);
}

static int _wave_cfg80211_vendor_set_MuStatPlanCfg(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return set_int_params(wiphy, wdev, data, data_len, 21, 173, PRM_ID_STATIC_PLAN_CONFIG);
}

static int _wave_cfg80211_vendor_set_enable_test_bus(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return set_int_params(wiphy, wdev, data, data_len, 1, 1, PRM_ID_TEST_BUS);
}

static int _wave_cfg80211_vendor_set_he_mu_operation(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return set_int_params(wiphy, wdev, data, data_len, 1, 1, PRM_ID_HE_MU_OPERATION);
}

static int _wave_cfg80211_vendor_set_dynamic_mu_type(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return set_int_params(wiphy, wdev, data, data_len,
                        WAVE_DYNAMIC_MU_TYPE_CFG_SIZE, WAVE_DYNAMIC_MU_TYPE_CFG_SIZE, PRM_ID_DYNAMIC_MU_TYPE);
}

static int _wave_cfg80211_vendor_set_he_mu_fixed_parameters(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return set_int_params(wiphy, wdev, data, data_len,
                        WAVE_HE_MU_FIXED_PARAMTERS_CFG_SIZE, WAVE_HE_MU_FIXED_PARAMTERS_CFG_SIZE, PRM_ID_HE_MU_FIXED_PARAMTERS);
}

static int _wave_cfg80211_vendor_set_he_mu_duration(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return set_int_params(wiphy, wdev, data, data_len,
                        WAVE_HE_MU_DURATION_CFG_SIZE, WAVE_HE_MU_DURATION_CFG_SIZE, PRM_ID_HE_MU_DURATION);
}

/* WIFI HAL API's*/
static int _wave_cfg80211_vendor_get_associated_dev_stats (struct wiphy *wiphy,
  struct wireless_dev *wdev, const void *data, int data_len)
{
  wv_cfg80211_t *cfg80211 = wiphy_priv(wiphy);
  MTLK_ASSERT(NULL != cfg80211);
  return wave_radio_get_associated_dev_stats(wv_cfg80211_wave_radio_get(cfg80211), wdev->netdev, data, data_len);
}

static int _wave_cfg80211_vendor_get_associated_dev_tid_stats (struct wiphy *wiphy,
  struct wireless_dev *wdev, const void *data, int data_len)
{
  wv_cfg80211_t *cfg80211 = wiphy_priv(wiphy);
  MTLK_ASSERT(NULL != cfg80211);
  return wave_radio_get_associated_dev_tid_stats(wv_cfg80211_wave_radio_get(cfg80211), wdev->netdev, data, data_len);
}

static int _wave_cfg80211_vendor_get_channel_stats (struct wiphy *wiphy,
  struct wireless_dev *wdev, const void *data, int data_len)
{
  wv_cfg80211_t *cfg80211 = wiphy_priv(wiphy);
  MTLK_ASSERT(NULL != cfg80211);
  return wave_radio_get_channel_stats(wv_cfg80211_wave_radio_get(cfg80211), wdev->netdev, data, data_len);
}

static int _wave_cfg80211_vendor_get_associated_dev_rate_info_rx_stats (struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  wv_cfg80211_t *cfg80211 = wiphy_priv(wiphy);
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  MTLK_ASSERT(NULL != cfg80211);
  return wave_radio_get_associated_dev_rate_info_rx_stats(wv_cfg80211_wave_radio_get(cfg80211), wdev->netdev, data, data_len);
}

static int _wave_cfg80211_vendor_get_associated_dev_rate_info_tx_stats (struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  wv_cfg80211_t *cfg80211 = wiphy_priv(wiphy);
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  MTLK_ASSERT(NULL != cfg80211);
  return wave_radio_get_associated_dev_rate_info_tx_stats(wv_cfg80211_wave_radio_get(cfg80211), wdev->netdev, data, data_len);
}

#ifndef MTLK_LEGACY_STATISTICS
static int _wave_cfg80211_vendor_get_peer_list (struct wiphy *wiphy,
  struct wireless_dev *wdev, const void *data, int data_len)
{
  wv_cfg80211_t *cfg80211 = wiphy_priv(wiphy);
  MTLK_ASSERT(NULL != cfg80211);
  return wave_radio_get_peer_list(wv_cfg80211_wave_radio_get(cfg80211), wdev->netdev, data, data_len);
}

static int _wave_cfg80211_vendor_get_peer_flow_status (struct wiphy *wiphy,
  struct wireless_dev *wdev, const void *data, int data_len)
{
  wv_cfg80211_t *cfg80211 = wiphy_priv(wiphy);
  MTLK_ASSERT(NULL != cfg80211);
  return wave_radio_get_peer_flow_status(wv_cfg80211_wave_radio_get(cfg80211), wdev->netdev, data, data_len);
}

static int _wave_cfg80211_vendor_get_peer_capabilities (struct wiphy *wiphy,
  struct wireless_dev *wdev, const void *data, int data_len)
{
  wv_cfg80211_t *cfg80211 = wiphy_priv(wiphy);
  MTLK_ASSERT(NULL != cfg80211);
  return wave_radio_get_peer_capabilities(wv_cfg80211_wave_radio_get(cfg80211), wdev->netdev, data, data_len);
}

static int _wave_cfg80211_vendor_get_peer_rate_info (struct wiphy *wiphy,
  struct wireless_dev *wdev, const void *data, int data_len)
{
  wv_cfg80211_t *cfg80211 = wiphy_priv(wiphy);
  MTLK_ASSERT(NULL != cfg80211);
  return wave_radio_get_peer_rate_info(wv_cfg80211_wave_radio_get(cfg80211), wdev->netdev, data, data_len);
}

static int _wave_cfg80211_vendor_get_recovery_stats (struct wiphy *wiphy,
  struct wireless_dev *wdev, const void *data, int data_len)
{
  wv_cfg80211_t *cfg80211 = wiphy_priv(wiphy);
  MTLK_ASSERT(NULL != cfg80211);
  return wave_radio_get_recovery_statistics(wv_cfg80211_wave_radio_get(cfg80211), wdev->netdev, data, data_len);
}

static int _wave_cfg80211_vendor_get_hw_flow_status (struct wiphy *wiphy,
  struct wireless_dev *wdev, const void *data, int data_len)
{
  wv_cfg80211_t *cfg80211 = wiphy_priv(wiphy);
  MTLK_ASSERT(NULL != cfg80211);
  return wave_radio_get_hw_flow_status(wv_cfg80211_wave_radio_get(cfg80211), wdev->netdev, data, data_len);
}

static int _wave_cfg80211_vendor_get_tr181_wlan_stats (struct wiphy *wiphy,
  struct wireless_dev *wdev, const void *data, int data_len)
{
  wv_cfg80211_t *cfg80211 = wiphy_priv(wiphy);
  MTLK_ASSERT(NULL != cfg80211);
  return wave_radio_get_tr181_wlan_statistics(wv_cfg80211_wave_radio_get(cfg80211), wdev->netdev, data, data_len);
}

static int _wave_cfg80211_vendor_get_tr181_hw_stats (struct wiphy *wiphy,
  struct wireless_dev *wdev, const void *data, int data_len)
{
  wv_cfg80211_t *cfg80211 = wiphy_priv(wiphy);
  MTLK_ASSERT(NULL != cfg80211);
  return wave_radio_get_tr181_hw_statistics(wv_cfg80211_wave_radio_get(cfg80211), wdev->netdev, data, data_len);
}

static int _wave_cfg80211_vendor_get_tr181_peer_stats (struct wiphy *wiphy,
  struct wireless_dev *wdev, const void *data, int data_len)
{
  wv_cfg80211_t *cfg80211 = wiphy_priv(wiphy);
  MTLK_ASSERT(NULL != cfg80211);
  return wave_radio_get_tr181_peer_statistics(wv_cfg80211_wave_radio_get(cfg80211), wdev->netdev, data, data_len);
}
#endif /* MTLK_LEGACY_STATISTICS */

static int _wave_cfg80211_vendor_hapd_set_disable_master_vap(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  wv_cfg80211_t *cfg80211 = wiphy_priv(wiphy);

  MTLK_ASSERT(NULL != cfg80211);
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  if (data_len != sizeof (int)){
    ELOG_V("_wave_cfg80211_vendor_set_disable_master_vap, incorrect data length");
    return _mtlk_df_mtlk_to_linux_error_code(MTLK_ERR_PARAMS);
  }
  WAVE_RADIO_PDB_SET_INT(wv_cfg80211_wave_radio_get(cfg80211), PARAM_DB_RADIO_DISABLE_MASTER_VAP, (*(int *)data));
  return _mtlk_df_mtlk_to_linux_error_code(MTLK_ERR_OK);
}

static int _wave_cfg80211_vendor_set_rts_rate(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return set_int_params(wiphy, wdev, data, data_len, 1, 1, PRM_ID_RTS_RATE);
}

static int _wave_cfg80211_vendor_set_NfrpCfg(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return set_int_params(wiphy, wdev, data, data_len, 2, 2, PRM_ID_NFRP_CFG);
}

static int _wave_cfg80211_vendor_set_wds_wep_enc_cfg(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  int                      res = MTLK_ERR_OK;
  mtlk_df_user_t           *df_user = mtlk_df_user_from_wdev(wdev);
  mtlk_clpb_t              *clpb = NULL;
  mtlk_core_ui_enc_cfg_t   enc_cfg;
  int                      key_id;
  char                     *wep_key_data = (char *) data + sizeof(int);
  int                      wep_key_data_len = data_len - sizeof(int);

  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  MTLK_CHECK_DF_USER(df_user);

  memset(&enc_cfg, 0, sizeof(enc_cfg));

  enc_cfg.wep_enabled = TRUE;
  wave_memcpy(&key_id, sizeof(key_id), data, sizeof(key_id));
  if (MIB_WEP_N_DEFAULT_KEYS < key_id ) {
    WLOG_S("%s: Unsupported WEP key index", wdev->netdev->name);
    res = MTLK_ERR_PARAMS;
    goto finish;
  }
  enc_cfg.key_id = key_id - 1;

  if (mtlk_df_ui_validate_wep_key(wep_key_data, wep_key_data_len) != MTLK_ERR_OK) {
    WLOG_S("%s: Bad WEP key", wdev->netdev->name);
    res = MTLK_ERR_PARAMS;
    goto finish;
  }
  /* Only one key might be passed from OS. So, slot 0 is used */
  enc_cfg.wep_keys.sKey[0].u8KeyLength = wep_key_data_len;
  wave_memcpy(enc_cfg.wep_keys.sKey[0].au8KeyData, sizeof(enc_cfg.wep_keys.sKey[0].au8KeyData), wep_key_data, wep_key_data_len);

  res = _mtlk_df_user_invoke_core(mtlk_df_user_get_df(df_user), WAVE_CORE_REQ_SET_WDS_WEP_ENC_CFG,
                                  &clpb, (char*)&enc_cfg, sizeof(enc_cfg));
  res = _mtlk_df_user_process_core_retval_void(res, clpb, WAVE_CORE_REQ_SET_WDS_WEP_ENC_CFG, TRUE);

finish:
  return _mtlk_df_mtlk_to_linux_error_code(res);
}

/************************* GET FUNCTIONS *************************/
static int get_int_params(struct wiphy *wiphy, struct wireless_dev *wdev,
        const void *data, int data_len, uint16 cmd, uint16 num_of_params)
{
  mtlk_df_user_t *df_user = mtlk_df_user_from_wdev(wdev);
  mtlk_nbuf_t *rsp_nbuf;
  int res = MTLK_ERR_OK;
  int *recv_data;
  int array_size = sizeof(int) * num_of_params;
  uint16 len = 0;

  MTLK_CHECK_DF_USER(df_user);

  recv_data = mtlk_osal_mem_alloc(array_size, MTLK_MEM_TAG_VENDOR_CMD_DATA);
  if (!recv_data)
    return _mtlk_df_mtlk_to_linux_error_code(MTLK_ERR_NO_MEM);

  res = mtlk_df_ui_get_cfg(df_user, cmd, (char*)recv_data, MTLK_IW_PRIW_PARAM(INT_ITEM,num_of_params), &len);
  if (res) {
    mtlk_osal_mem_free(recv_data);
    return _mtlk_df_mtlk_to_linux_error_code(res);
  }

  rsp_nbuf = wv_cfg80211_vendor_cmd_alloc_reply_skb(wiphy, array_size);
  if (!rsp_nbuf) {
       mtlk_osal_mem_free(recv_data);
    return _mtlk_df_mtlk_to_linux_error_code(MTLK_ERR_NO_MEM);
  }

  nla_put(rsp_nbuf, NL80211_ATTR_VENDOR_DATA, array_size, recv_data);
  mtlk_osal_mem_free(recv_data);

  return wv_cfg80211_vendor_cmd_reply(rsp_nbuf);
}

static int get_text_params(struct wiphy *wiphy, struct wireless_dev *wdev,
        const void *data, int data_len, uint16 cmd, uint16 num_of_params)
{
  mtlk_df_user_t *df_user = mtlk_df_user_from_wdev(wdev);
  mtlk_nbuf_t *rsp_nbuf;
  int res = MTLK_ERR_OK;
  char *recv_data;
  int array_size = TEXT_SIZE;
  uint16 len = 0;

  MTLK_CHECK_DF_USER(df_user);

  recv_data = mtlk_osal_mem_alloc(array_size, MTLK_MEM_TAG_VENDOR_CMD_DATA);
  if (!recv_data)
    return _mtlk_df_mtlk_to_linux_error_code(MTLK_ERR_NO_MEM);

  res = mtlk_df_ui_get_cfg(df_user, cmd, (char*)recv_data, MTLK_IW_PRIW_PARAM(TEXT_ITEM,array_size), &len);
  if (res) {
    mtlk_osal_mem_free(recv_data);
    return _mtlk_df_mtlk_to_linux_error_code(res);
  }

  rsp_nbuf = wv_cfg80211_vendor_cmd_alloc_reply_skb(wiphy, len);
  if (!rsp_nbuf) {
       mtlk_osal_mem_free(recv_data);
    return _mtlk_df_mtlk_to_linux_error_code(MTLK_ERR_NO_MEM);
  }

  nla_put(rsp_nbuf, NL80211_ATTR_VENDOR_DATA, len, recv_data);
  mtlk_osal_mem_free(recv_data);

  return wv_cfg80211_vendor_cmd_reply(rsp_nbuf);
}

static int _wave_cfg80211_vendor_get_LongRetryLimit(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return get_int_params(wiphy, wdev, data, data_len, MIB_LONG_RETRY_LIMIT, 1);
}

static int _wave_cfg80211_vendor_get_DTIMPeriod(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return get_int_params(wiphy, wdev, data, data_len, MIB_DTIM_PERIOD, 1);
}

static int _wave_cfg80211_vendor_get_BeaconPeriod(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return get_int_params(wiphy, wdev, data, data_len, MIB_BEACON_PERIOD, 1);
}

static int _wave_cfg80211_vendor_get_11hRadarDetect(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return get_int_params(wiphy, wdev, data, data_len, PRM_ID_11H_RADAR_DETECTION, 1);
}

static int _wave_cfg80211_vendor_get_11hBeaconCount(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return get_int_params(wiphy, wdev, data, data_len, PRM_ID_11H_BEACON_COUNT, 1);
}

static int _wave_cfg80211_vendor_get_11hChCheckTime(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return get_int_params(wiphy, wdev, data, data_len, PRM_ID_11H_CHANNEL_AVAILABILITY_CHECK_TIME, 1);
}

static int _wave_cfg80211_vendor_get_NonOccupatePrd(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return get_int_params(wiphy, wdev, data, data_len, PRM_ID_11H_NOP, 1);
}

static int _wave_cfg80211_vendor_get_PeerAPkeyIdx(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return get_int_params(wiphy, wdev, data, data_len, PRM_ID_PEERAP_KEY_IDX, 1);
}

static int _wave_cfg80211_vendor_get_PeerAPs(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return get_text_params(wiphy, wdev, data, data_len, PRM_ID_PEERAP_LIST, 1);

}

static int _wave_cfg80211_vendor_get_11dActive(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return get_int_params(wiphy, wdev, data, data_len, PRM_ID_11D, 1);
}

static int _wave_cfg80211_vendor_get_MACWdTimeoutMs(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return get_int_params(wiphy, wdev, data, data_len, PRM_ID_MAC_WATCHDOG_TIMEOUT_MS, 1);
}

static int _wave_cfg80211_vendor_get_MACWdPeriodMs(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return get_int_params(wiphy, wdev, data, data_len, PRM_ID_MAC_WATCHDOG_PERIOD_MS, 1);
}

static int _wave_cfg80211_vendor_get_StaKeepaliveIN(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return get_int_params(wiphy, wdev, data, data_len, PRM_ID_STA_KEEPALIVE_INTERVAL, 1);
}

static int _wave_cfg80211_vendor_get_BridgeMode(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return get_int_params(wiphy, wdev, data, data_len, PRM_ID_BRIDGE_MODE, 1);
}

static int _wave_cfg80211_vendor_get_ReliableMcast(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return get_int_params(wiphy, wdev, data, data_len, PRM_ID_RELIABLE_MULTICAST, 1);
}

static int _wave_cfg80211_vendor_get_APforwarding(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return get_int_params(wiphy, wdev, data, data_len, PRM_ID_AP_FORWARDING, 1);
}

static int _wave_cfg80211_vendor_get_NetworkMode(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return get_int_params(wiphy, wdev, data, data_len, PRM_ID_NETWORK_MODE, 1);
}

static int _wave_cfg80211_vendor_get_CdbCfg(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return get_int_params(wiphy, wdev, data, data_len, PRM_ID_CDB_CFG, 1);
}

static int _wave_cfg80211_vendor_get_BasicRateSet(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return get_int_params(wiphy, wdev, data, data_len, PRM_ID_BSS_BASIC_RATE_SET, 1);
}

static int _wave_cfg80211_vendor_get_HiddenSSID(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return get_int_params(wiphy, wdev, data, data_len, PRM_ID_HIDDEN_SSID, 1);
}

static int _wave_cfg80211_vendor_get_UpRescanExmpTm(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return get_int_params(wiphy, wdev, data, data_len, PRM_ID_UP_RESCAN_EXEMPTION_TIME, 1);
}

static int _wave_cfg80211_vendor_get_WDSHostTO(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return get_int_params(wiphy, wdev, data, data_len, PRM_ID_WDS_HOST_TIMEOUT, 1);
}

static int _wave_cfg80211_vendor_get_EEPROM(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return get_text_params(wiphy, wdev, data, data_len, PRM_ID_EEPROM, 1);
}

static int _wave_cfg80211_vendor_get_IRBPngEnabled(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
#ifdef CPTCFG_IWLWAV_IRB_DEBUG
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return get_int_params(wiphy, wdev, data, data_len, PRM_ID_IRB_PINGER_ENABLED, 1);
#else
  return _mtlk_df_mtlk_to_linux_error_code(MTLK_ERR_NO_ENTRY);
#endif
}

static int _wave_cfg80211_vendor_get_IRBPngStatsRst(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
#ifdef CPTCFG_IWLWAV_IRB_DEBUG
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return get_text_params(wiphy, wdev, data, data_len, PRM_ID_IRB_PINGER_STATS, 1);
#else
  return _mtlk_df_mtlk_to_linux_error_code(MTLK_ERR_NO_ENTRY);
#endif
}

static int _wave_cfg80211_vendor_get_LtPathEnabled(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
#if MTLK_USE_DIRECTCONNECT_DP_API
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return get_int_params(wiphy, wdev, data, data_len, PRM_ID_DCDP_API_LITEPATH, 1);
#else
  return _mtlk_df_mtlk_to_linux_error_code(MTLK_ERR_NO_ENTRY);
#endif
}

static int _wave_cfg80211_vendor_get_IpxPpaEnabled(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
#if MTLK_USE_DIRECTCONNECT_DP_API
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return get_int_params(wiphy, wdev, data, data_len, PRM_ID_DCDP_API_LITEPATH, 1);
#else
  return _mtlk_df_mtlk_to_linux_error_code(MTLK_ERR_NO_ENTRY);
#endif
}

static int _wave_cfg80211_vendor_get_UdmaEnabled(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
#if MTLK_USE_PUMA6_UDMA
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return get_int_params(wiphy, wdev, data, data_len, PRM_ID_UDMA_API, 1);
#else
  return _mtlk_df_mtlk_to_linux_error_code(MTLK_ERR_NO_ENTRY);
#endif
}

static int _wave_cfg80211_vendor_get_UdmaEnabledExt(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
#if MTLK_USE_PUMA6_UDMA
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return get_int_params(wiphy, wdev, data, data_len, PRM_ID_UDMA_API_EXT, 1);
#else
  return _mtlk_df_mtlk_to_linux_error_code(MTLK_ERR_NO_ENTRY);
#endif
}

static int _wave_cfg80211_vendor_get_UdmaVlanId(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
#if MTLK_USE_PUMA6_UDMA
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return get_int_params(wiphy, wdev, data, data_len, PRM_ID_UDMA_VLAN_ID, 1);
#else
  return _mtlk_df_mtlk_to_linux_error_code(MTLK_ERR_NO_ENTRY);
#endif
}

static int _wave_cfg80211_vendor_get_UdmaVlanIdExt(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
#if MTLK_USE_PUMA6_UDMA
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return get_int_params(wiphy, wdev, data, data_len, PRM_ID_UDMA_VLAN_ID_EXT, 1);
#else
  return _mtlk_df_mtlk_to_linux_error_code(MTLK_ERR_NO_ENTRY);
#endif
}

static int _wave_cfg80211_vendor_get_CoCPower(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return get_int_params(wiphy, wdev, data, data_len, PRM_ID_COC_POWER_MODE, 3);
}

static int _wave_cfg80211_vendor_get_CoCAutoCfg(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return get_int_params(wiphy, wdev, data, data_len, PRM_ID_COC_AUTO_PARAMS, 10);
}

static int _wave_cfg80211_vendor_get_PCoCPower(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
#ifdef CPTCFG_IWLWAV_PMCU_SUPPORT
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return get_int_params(wiphy, wdev, data, data_len, PRM_ID_PCOC_POWER_MODE, 3);
#else
  return _mtlk_df_mtlk_to_linux_error_code(MTLK_ERR_NO_ENTRY);
#endif
}

static int _wave_cfg80211_vendor_get_PCoCAutoCfg(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
#ifdef CPTCFG_IWLWAV_PMCU_SUPPORT
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return get_int_params(wiphy, wdev, data, data_len, PRM_ID_PCOC_AUTO_PARAMS, 4);
#else
  return _mtlk_df_mtlk_to_linux_error_code(MTLK_ERR_NO_ENTRY);
#endif
}

static int _wave_cfg80211_vendor_get_TpcLoopType(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return get_int_params(wiphy, wdev, data, data_len, PRM_ID_TPC_LOOP_TYPE, 1);
}

static int _wave_cfg80211_vendor_get_InterfDetMode(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return get_int_params(wiphy, wdev, data, data_len, PRM_ID_INTERFERENCE_MODE, 1);
}

static int _wave_cfg80211_vendor_get_APCapsMaxSTAs(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return get_int_params(wiphy, wdev, data, data_len, PRM_ID_AP_CAPABILITIES_MAX_STAs, 1);
}

static int _wave_cfg80211_vendor_get_APCapsMaxVAPs(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return get_int_params(wiphy, wdev, data, data_len, PRM_ID_AP_CAPABILITIES_MAX_VAPs, 1);
}

static int _wave_cfg80211_vendor_get_11bAntSelection(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return get_int_params(wiphy, wdev, data, data_len, PRM_ID_11B_ANTENNA_SELECTION, 3);
}

static int _wave_cfg80211_vendor_get_TATimerRes(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return get_int_params(wiphy, wdev, data, data_len, PRM_ID_TA_TIMER_RESOLUTION, 1);
}

static int _wave_cfg80211_vendor_get_TADbg(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return get_text_params(wiphy, wdev, data, data_len, PRM_ID_TA_DBG, 1);
}

static int _wave_cfg80211_vendor_get_FWRecovery(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return get_int_params(wiphy, wdev, data, data_len, PRM_ID_FW_RECOVERY, 5);
}

static int vendor_get_RcvryStats(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return get_int_params(wiphy, wdev, data, data_len, PRM_ID_RECOVERY_STATS, 4);
}

static int _wave_cfg80211_vendor_get_ScanParams(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return get_int_params(wiphy, wdev, data, data_len, PRM_ID_SCAN_PARAMS, 6);
}


static int _wave_cfg80211_vendor_get_ScanParamsBG(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return get_int_params(wiphy, wdev, data, data_len, PRM_ID_SCAN_PARAMS_BG, 6);
}

static int _wave_cfg80211_vendor_get_ScanModifFlags(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return get_int_params(wiphy, wdev, data, data_len, PRM_ID_SCAN_MODIFS, 1);
}

static int _wave_cfg80211_vendor_get_ScanCalCwMasks(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return get_int_params(wiphy, wdev, data, data_len, PRM_ID_CALIB_CW_MASKS, 2);
}

static int _wave_cfg80211_vendor_get_ScanExpTime(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return get_int_params(wiphy, wdev, data, data_len, PRM_ID_SCAN_EXP_TIME, 1);
}

static int _wave_cfg80211_vendor_get_GenlFamilyId(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return get_int_params(wiphy, wdev, data, data_len, PRM_ID_GENL_FAMILY_ID, 1);
}

static int _wave_cfg80211_vendor_get_TaskletLimits(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return get_int_params(wiphy, wdev, data, data_len, PRM_ID_TASKLET_LIMITS, 5);
}

static int _wave_cfg80211_vendor_get_EnableRadio(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return get_int_params(wiphy, wdev, data, data_len, PRM_ID_RADIO_MODE, 1);
}

static int _wave_cfg80211_vendor_get_AggrConfig(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return get_int_params(wiphy, wdev, data, data_len, PRM_ID_AGGR_CONFIG, 3);
}

static int _wave_cfg80211_vendor_get_NumMsduInAmsdu(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return get_int_params(wiphy, wdev, data, data_len, PRM_ID_AMSDU_NUM, 3);
}

static int _wave_cfg80211_vendor_get_AggRateLimit(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return get_int_params(wiphy, wdev, data, data_len, PRM_ID_AGG_RATE_LIMIT, 2);
}

static int _wave_cfg80211_vendor_get_BssLoadInBeacon(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return get_int_params(wiphy, wdev, data, data_len, PRM_ID_BSS_LOAD_IN_BEACON, 1);
}

static int _wave_cfg80211_vendor_get_AvailAdmCap(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{

  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return get_int_params(wiphy, wdev, data, data_len, PRM_ID_ADMISSION_CAPACITY, 1);
}

static int _wave_cfg80211_vendor_get_SetRxTH(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return get_int_params(wiphy, wdev, data, data_len, PRM_ID_RX_THRESHOLD, 1);
}

static int _wave_cfg80211_vendor_get_RxDutyCyc(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return get_int_params(wiphy, wdev, data, data_len, PRM_ID_RX_DUTY_CYCLE, 2);
}

static int _wave_cfg80211_vendor_get_PowerSelection(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return get_int_params(wiphy, wdev, data, data_len, PRM_ID_TX_POWER_LIMIT_OFFSET, 1);
}

static int _wave_cfg80211_vendor_get_11nProtection(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return get_int_params(wiphy, wdev, data, data_len, PRM_ID_PROTECTION_METHOD, 1);
}

static int _wave_cfg80211_vendor_get_BfExplicitCap(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return get_int_params(wiphy, wdev, data, data_len, PRM_ID_BEAMFORM_EXPLICIT, 1);
}

static int _wave_cfg80211_vendor_get_Temperature(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return get_int_params(wiphy, wdev, data, data_len, PRM_ID_TEMPERATURE_SENSOR, 3);
}

static int _wave_cfg80211_vendor_get_QAMplus(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return get_int_params(wiphy, wdev, data, data_len, PRM_ID_QAMPLUS_MODE, 1);
}

static int _wave_cfg80211_vendor_get_AcsUpdateTo(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return get_int_params(wiphy, wdev, data, data_len, PRM_ID_ACS_UPDATE_TO, 1);
}

static int _wave_cfg80211_vendor_get_MuOperation(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return get_int_params(wiphy, wdev, data, data_len, PRM_ID_MU_OPERATION, 1);
}

static int _wave_cfg80211_vendor_get_CcaTh(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return get_int_params(wiphy, wdev, data, data_len, PRM_ID_CCA_THRESHOLD, 5);
}

static int _wave_cfg80211_vendor_get_CcaAdapt(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return get_int_params(wiphy, wdev, data, data_len, PRM_ID_CCA_ADAPT, 7);
}

static int _wave_cfg80211_vendor_get_RadarRssiTh(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return get_int_params(wiphy, wdev, data, data_len, PRM_ID_RADAR_RSSI_TH, 1);
}

static int _wave_cfg80211_vendor_get_CpuDmaLatency(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
#ifdef CPTCFG_IWLWAV_SET_PM_QOS
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return get_int_params(wiphy, wdev, data, data_len, PRM_ID_CPU_DMA_LATENCY, 1);
#else
  return _mtlk_df_mtlk_to_linux_error_code(MTLK_ERR_NO_ENTRY);
#endif
}

static int _wave_cfg80211_vendor_get_RTSmode(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return get_int_params(wiphy, wdev, data, data_len, PRM_ID_RTS_MODE, 2);
}

static int _wave_cfg80211_vendor_get_MaxMpduLen(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return get_int_params(wiphy, wdev, data, data_len, PRM_ID_MAX_MPDU_LENGTH, 1);
}

static int _wave_cfg80211_vendor_get_BfMode(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return get_int_params(wiphy, wdev, data, data_len, PRM_ID_BF_MODE, 1);
}

static int _wave_cfg80211_vendor_get_ActiveAntMask(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return get_int_params(wiphy, wdev, data, data_len, PRM_ID_ACTIVE_ANT_MASK, 1);
}

static int _wave_cfg80211_vendor_get_CtrlBFilterBank(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return get_int_params(wiphy, wdev, data, data_len, PRM_ID_IRE_CTRL_B, 1);
}

static int _wave_cfg80211_vendor_get_FourAddrMode(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return get_int_params(wiphy, wdev, data, data_len, PRM_ID_4ADDR_MODE, 1);
}

static int _wave_cfg80211_vendor_get_FourAddrStas(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return get_text_params(wiphy, wdev, data, data_len, PRM_ID_4ADDR_STA_LIST, 1);
}

static int _wave_cfg80211_vendor_get_TxopConfig(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return get_int_params(wiphy, wdev, data, data_len, PRM_ID_TXOP_CONFIG, 2);
}

static int _wave_cfg80211_vendor_get_SsbMode(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return get_int_params(wiphy, wdev, data, data_len, PRM_ID_SSB_MODE, 2);
}

static int _wave_cfg80211_vendor_get_EnableMRCoex(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return get_int_params(wiphy, wdev, data, data_len, PRM_ID_COEX_ENABLE, 1);
}

static int _wave_cfg80211_vendor_get_ConfigMRCoex(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return get_int_params(wiphy, wdev, data, data_len, PRM_ID_COEX_CFG, 1);
}

static int _wave_cfg80211_vendor_get_SlowProbingMask(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return get_int_params(wiphy, wdev, data, data_len, PRM_ID_PROBING_MASK, 1);
}

static int _wave_cfg80211_vendor_get_McastRange(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return get_text_params(wiphy, wdev, data, data_len, PRM_ID_MCAST_RANGE_SETUP, 1);
}

static int _wave_cfg80211_vendor_get_McastRange6(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return get_text_params(wiphy, wdev, data, data_len, PRM_ID_MCAST_RANGE_SETUP_IPV6, 1);
}

static int _wave_cfg80211_vendor_get_OnlineACM(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return get_int_params(wiphy, wdev, data, data_len, PRM_ID_ONLINE_CALIBRATION_ALGO_MASK, 1);
}

static int _wave_cfg80211_vendor_get_AlgoCalibrMask(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return get_int_params(wiphy, wdev, data, data_len, PRM_ID_CALIBRATION_ALGO_MASK, 1);
}

static int _wave_cfg80211_vendor_Get_ShortCyclcPrfx(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return get_int_params(wiphy, wdev, data, data_len, PRM_ID_USE_SHORT_CYCLIC_PREFIX, 1);
}

static int _wave_cfg80211_vendor_get_FixedPower(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return get_int_params(wiphy, wdev, data, data_len, PRM_ID_FIXED_POWER, 4);
}

static int _wave_cfg80211_vendor_get_UnconnTime(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return get_int_params(wiphy, wdev, data, data_len, PRM_ID_UNCONNECTED_STA_SCAN_TIME, 1);
}

static int _wave_cfg80211_vendor_get_RestrictAcMode(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return get_int_params(wiphy, wdev, data, data_len, PRM_ID_RESTRICTED_AC_MODE, 4);
}

static int _wave_cfg80211_vendor_get_PdThresh(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return get_int_params(wiphy, wdev, data, data_len, PRM_ID_PD_THRESHOLD, 3);
}

static int _wave_cfg80211_vendor_get_FastDrop(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return get_int_params(wiphy, wdev, data, data_len, PRM_ID_FAST_DROP, 1);
}

static int _wave_cfg80211_vendor_get_DmrConfig(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return get_int_params(wiphy, wdev, data, data_len, PRM_ID_FAST_DYNAMIC_MC_RATE, 1);
}

static int _wave_cfg80211_vendor_get_CountersSrc(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return get_int_params(wiphy, wdev, data, data_len, PRM_ID_SWITCH_COUNTERS_SRC, 1);
}

static int _wave_cfg80211_vendor_get_FixedLtfGi(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return get_int_params(wiphy, wdev, data, data_len, PRM_ID_FIXED_LTF_AND_GI, 2);
}

static int _wave_cfg80211_vendor_get_pvt_sensor(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return get_int_params(wiphy, wdev, data, data_len, PRM_ID_PVT_SENSOR, 2);
}

static int _wave_cfg80211_vendor_get_he_mu_operation(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return get_int_params(wiphy, wdev, data, data_len, PRM_ID_HE_MU_OPERATION, 1);
}

static int _wave_cfg80211_vendor_hapd_set_mbssid_vap(struct wiphy *wiphy, struct wireless_dev *wdev,
                                const void *data, int data_len)
{
  mtlk_df_user_t *df_user = mtlk_df_user_from_wdev(wdev);
  mtlk_pdb_t *param_db;
  int res = MTLK_ERR_OK;
  uint8 value;

  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  MTLK_CHECK_DF_USER(df_user);

  value = * (uint8 *) data;

  if ((data_len != sizeof(value)) || (value > WAVE_RADIO_OPERATION_MODE_MBSSID_LAST)) {
      ELOG_DD("Wrong data length (%d) or value (%d)", data_len, value);
      res = MTLK_ERR_PARAMS;
      goto end;
  }

  param_db = mtlk_vap_get_param_db(mtlk_df_get_vap_handle(mtlk_df_user_get_df(df_user)));
  wave_pdb_set_int(param_db, PARAM_DB_CORE_MBSSID_VAP, value);

end:
  return _mtlk_df_mtlk_to_linux_error_code(res);
}

static int _wave_cfg80211_vendor_get_stats_poll_period(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  struct net_device *ndev = wdev->netdev;
  mtlk_df_user_t *df_user;
  mtlk_clpb_t *clpb = NULL;
  mtlk_nbuf_t *rsp_nbuf;
  int res = MTLK_ERR_OK;
  uint32 *stats_period;
  uint32 stats_size;

  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);

  MTLK_ASSERT(NULL != wiphy);
  MTLK_ASSERT(NULL != ndev);
  df_user = mtlk_df_user_from_ndev(ndev);
  MTLK_CHECK_DF_USER(df_user);

  res = _mtlk_df_user_invoke_core(mtlk_df_user_get_df(df_user),
    WAVE_CORE_REQ_GET_STATS_POLL_PERIOD, &clpb, NULL, 0);
  res = _mtlk_df_user_process_core_retval(res, clpb,
    WAVE_CORE_REQ_GET_STATS_POLL_PERIOD, FALSE);

  if (res != MTLK_ERR_OK)
    goto end;

  stats_period = mtlk_clpb_enum_get_next(clpb, &stats_size);

  MTLK_CLPB_TRY(stats_period, stats_size)
    rsp_nbuf = wv_cfg80211_vendor_cmd_alloc_reply_skb(wiphy,
      sizeof(*stats_period));
    if (!rsp_nbuf) {
      MTLK_CLPB_EXIT(MTLK_ERR_NO_MEM);
    }

    nla_put(rsp_nbuf, NL80211_ATTR_VENDOR_DATA, sizeof(*stats_period),
      stats_period);

    mtlk_clpb_delete(clpb);
    return wv_cfg80211_vendor_cmd_reply(rsp_nbuf);
  MTLK_CLPB_FINALLY(res)
    mtlk_clpb_delete(clpb);
  MTLK_CLPB_END

end:
  return _mtlk_df_mtlk_to_linux_error_code(res);
}

static int _wave_cfg80211_vendor_hapd_set_num_vaps_in_group(struct wiphy *wiphy, struct wireless_dev *wdev,
                                const void *data, int data_len)
{
  mtlk_df_user_t *df_user = mtlk_df_user_from_wdev(wdev);
  mtlk_pdb_t *param_db;
  int res = MTLK_ERR_OK;
  uint8 value;

  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  MTLK_CHECK_DF_USER(df_user);

  if (data_len != sizeof(value)) {
      ELOG_D("Wrong data length: %d", data_len);
      res = MTLK_ERR_PARAMS;
      goto end;
  }
  value = * (uint8 *) data;

  param_db = mtlk_vap_get_param_db(mtlk_df_get_vap_handle(mtlk_df_user_get_df(df_user)));
  wave_pdb_set_int(param_db, PARAM_DB_CORE_MBSSID_NUM_VAPS_IN_GROUP, value);

end:
  return _mtlk_df_mtlk_to_linux_error_code(res);
}

static int _wave_cfg80211_vendor_set_stats_poll_period(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  mtlk_df_user_t *df_user;
  mtlk_clpb_t *clpb = NULL;
  mtlk_nbuf_t *rsp_nbuf;
  uint32 *set_result;
  uint32 ret_code_size;
  int res = MTLK_ERR_OK;

  MTLK_ASSERT(NULL != wdev);
  df_user = mtlk_df_user_from_ndev(wdev->netdev);

  ILOG1_SSDD("%s: SET_STATS_POLL_PERIOD invoked from %s (%i), msg_len = %d", wdev->netdev->name,
    current->comm, current->pid, data_len);
  MTLK_CHECK_DF_USER(df_user);

  if (NULL == data) {
    ELOG_V("SET_STATS_POLL_PERIOD: data is NULL");
    res = MTLK_ERR_PARAMS;
    goto end;
  }

  /* Invoke core to process the message further */
  res = _mtlk_df_user_invoke_core(mtlk_df_user_get_df(df_user),
          WAVE_CORE_REQ_SET_STATS_POLL_PERIOD, &clpb, data, data_len);
  res = _mtlk_df_user_process_core_retval(res, clpb,
          WAVE_CORE_REQ_SET_STATS_POLL_PERIOD, FALSE);
  if(res != MTLK_ERR_OK)
    goto end;

  set_result = mtlk_clpb_enum_get_next(clpb, &ret_code_size);
  if ( (NULL == set_result) || (sizeof(*set_result) != ret_code_size) ) {
    mtlk_clpb_delete(clpb);
    res = MTLK_ERR_NO_MEM;
    goto end;
  }

  rsp_nbuf = wv_cfg80211_vendor_cmd_alloc_reply_skb(wiphy, sizeof(*set_result));
  if (!rsp_nbuf) {
    mtlk_clpb_delete(clpb);
    res = MTLK_ERR_NO_MEM;
    goto end;
  }

  nla_put(rsp_nbuf, NL80211_ATTR_VENDOR_DATA, sizeof(*set_result), set_result);

  mtlk_clpb_delete(clpb);
  return wv_cfg80211_vendor_cmd_reply(rsp_nbuf);

end:
  return _mtlk_df_mtlk_to_linux_error_code(res);
}

static int _wave_cfg80211_vendor_get_rts_rate(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return get_int_params(wiphy, wdev, data, data_len, PRM_ID_RTS_RATE, 1);
}

static int _wave_cfg80211_vendor_hapd_set_management_frames_rate(struct wiphy *wiphy,
                struct wireless_dev *wdev, const void *data, int data_len)
{
  uint8 value;
  mtlk_core_t *core;
  mtlk_df_user_t *df_user = mtlk_df_user_from_wdev(wdev);
  int res = MTLK_ERR_OK;

  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  MTLK_CHECK_DF_USER(df_user);

  if (data_len != sizeof(value)) {
      ELOG_D("Wrong management_frames_rate data length: %d", data_len);
      res = MTLK_ERR_PARAMS;
      goto end;
  }
  value = * (uint8 *) data;

  core = mtlk_vap_get_core(mtlk_df_get_vap_handle(mtlk_df_user_get_df(df_user)));

  ILOG1_DD("CID-%04x: Got new value (%u) of Management Frames Rate",
           mtlk_vap_get_oid(core->vap_handle), value);

  wave_core_management_frames_rate_set(core, (uint32)value);

end:
  return _mtlk_df_mtlk_to_linux_error_code(res);
}

static int _wave_cfg80211_vendor_get_dynamic_mu_type(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return get_int_params(wiphy, wdev, data, data_len, PRM_ID_DYNAMIC_MU_TYPE, WAVE_DYNAMIC_MU_TYPE_CFG_SIZE);
}

static int _wave_cfg80211_vendor_get_he_mu_fixed_parameters(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return get_int_params(wiphy, wdev, data, data_len, PRM_ID_HE_MU_FIXED_PARAMTERS, WAVE_HE_MU_FIXED_PARAMTERS_CFG_SIZE);
}

static int _wave_cfg80211_vendor_get_he_mu_duration(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return get_int_params(wiphy, wdev, data, data_len, PRM_ID_HE_MU_DURATION, WAVE_HE_MU_DURATION_CFG_SIZE);
}

static int _wave_cfg80211_vendor_get_phy_inband_power(struct wiphy *wiphy, struct wireless_dev *wdev,
                const void *data, int data_len)
{
  ILOG1_SSD("%s: Invoked from %s (%i)", wdev->netdev->name, current->comm, current->pid);
  return get_text_params(wiphy, wdev, data, data_len, PRM_ID_PHY_INBAND_POWER, 1);
}

#define WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV_UP (WIPHY_VENDOR_CMD_NEED_WDEV |\
                                              WIPHY_VENDOR_CMD_NEED_NETDEV |\
                                              WIPHY_VENDOR_CMD_NEED_RUNNING)

#define WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV    (WIPHY_VENDOR_CMD_NEED_WDEV |\
                                              WIPHY_VENDOR_CMD_NEED_NETDEV)

#define VENDOR_CMD(cmd, func, vendor_flags) { \
  .info  = {OUI_LTQ, (cmd)},    \
  .flags = (vendor_flags),             \
  .doit  = (func)               \
}

static struct
wiphy_vendor_command _wave_cfg80211_vendor_commands[] = {
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_AID,              _wave_cfg80211_vendor_hapd_get_aid, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV_UP),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_FREE_AID,             _wave_cfg80211_vendor_hapd_free_aid, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_TX_EAPOL,              wave_cfg80211_vendor_hapd_send_eapol, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV_UP),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_INITIAL_DATA,         _wave_cfg80211_vendor_hapd_send_initial_data, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV_UP),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SYNC_DONE,            _wave_cfg80211_vendor_hapd_sync_done, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV_UP),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_DFS_DEBUG,            _wave_cfg80211_vendor_hapd_send_dfs_debug_radar_required_chan, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV_UP),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_CAC_COMPLETE,         _wave_cfg80211_vendor_hapd_change_dfs_flags, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV_UP),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_DENY_MAC,         _wave_cfg80211_vendor_hapd_set_deny_mac, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV_UP),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_STA_STEER,            _wave_cfg80211_vendor_hapd_sta_steer, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV_UP),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_STA_MEASUREMENTS, _wave_cfg80211_vendor_hapd_get_sta_measurements, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV_UP),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_UNCONNECTED_STA,   wave_cfg80211_vendor_hapd_get_unconnected_sta, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV_UP),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_VAP_MEASUREMENTS, _wave_cfg80211_vendor_hapd_get_vap_measurements, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV_UP),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_RADIO_INFO,        wave_cfg80211_vendor_hapd_get_radio_info, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV_UP),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_ATF_QUOTAS,       _wave_cfg80211_vendor_hapd_set_atf_quotas, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV_UP),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_WDS_WPA_STA,      _wave_cfg80211_vendor_hapd_set_wds_wpa_sta, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV_UP),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_BLOCK_TX,             _wave_cfg80211_vendor_hapd_block_tx, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV_UP),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_DGAF_DISABLED,    _wave_cfg80211_vendor_hapd_set_dgaf_disabled, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV_UP),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_BSS_LOAD,         _wave_cfg80211_vendor_hapd_set_bss_load, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV_UP),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_MESH_MODE,        _wave_cfg80211_vendor_hapd_set_mesh_mode, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV_UP),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_CHECK_4ADDR_MODE,     _wave_cfg80211_vendor_hapd_check_4addr_mode, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV_UP),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_DISABLE_MASTER_VAP, _wave_cfg80211_vendor_hapd_set_disable_master_vap, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_SOFTBLOCK_THRESHOLDS, _wave_cfg80211_vendor_hapd_set_softblock_threshold, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV_UP),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_SOFTBLOCK_DISABLE, _wave_cfg80211_vendor_hapd_set_disable_softblock, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV_UP),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_DFS_STATE,         _wave_cfg80211_vendor_hapd_set_dfs_state, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV_UP),

  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_LONG_RETRY_LIMIT, _wave_cfg80211_vendor_set_LongRetryLimit, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_LONG_RETRY_LIMIT, _wave_cfg80211_vendor_get_LongRetryLimit, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_DTIM_PERIOD, _wave_cfg80211_vendor_set_DTIMPeriod, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_DTIM_PERIOD, _wave_cfg80211_vendor_get_DTIMPeriod, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_BEACON_PERIOD, _wave_cfg80211_vendor_set_BeaconPeriod, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_BEACON_PERIOD, _wave_cfg80211_vendor_get_BeaconPeriod, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),

  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_11H_RADAR_DETECT, _wave_cfg80211_vendor_set_11hRadarDetect, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_11H_RADAR_DETECT, _wave_cfg80211_vendor_get_11hRadarDetect, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_11H_BEACON_COUNT, _wave_cfg80211_vendor_set_11hBeaconCount, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_11H_BEACON_COUNT, _wave_cfg80211_vendor_get_11hBeaconCount, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_11H_CH_CHECK_TIME, _wave_cfg80211_vendor_set_11hChCheckTime, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_11H_CH_CHECK_TIME, _wave_cfg80211_vendor_get_11hChCheckTime, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_NON_OCCUPATED_PRD, _wave_cfg80211_vendor_set_NonOccupatePrd, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_NON_OCCUPATED_PRD, _wave_cfg80211_vendor_get_NonOccupatePrd, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_11H_EMULAT_RADAR, _wave_cfg80211_vendor_set_11hEmulatRadar, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),


  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_ADD_PEERAP, _wave_cfg80211_vendor_set_AddPeerAP, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_DEL_PEERAP, _wave_cfg80211_vendor_set_DelPeerAP, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_PEERAP_KEY_IDX, _wave_cfg80211_vendor_set_PeerAPkeyIdx, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_PEERAP_KEY_IDX, _wave_cfg80211_vendor_get_PeerAPkeyIdx, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_PEERAP_LIST, _wave_cfg80211_vendor_get_PeerAPs, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),

  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_11AD_ACTIVE, _wave_cfg80211_vendor_set_11dActive, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_11AD_ACTIVE, _wave_cfg80211_vendor_get_11dActive, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),

  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_MAC_WATCHDOG_TIMEOUT_MS, _wave_cfg80211_vendor_set_MACWdTimeoutMs, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_MAC_WATCHDOG_TIMEOUT_MS, _wave_cfg80211_vendor_get_MACWdTimeoutMs, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_MAC_WATCHDOG_PERIOD_MS, _wave_cfg80211_vendor_set_MACWdPeriodMs, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_MAC_WATCHDOG_PERIOD_MS, _wave_cfg80211_vendor_get_MACWdPeriodMs, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),

  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_STA_KEEPALIVE_INTERVAL, _wave_cfg80211_vendor_set_StaKeepaliveIN, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_STA_KEEPALIVE_INTERVAL,_wave_cfg80211_vendor_get_StaKeepaliveIN, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),

  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_BRIDGE_MODE, _wave_cfg80211_vendor_set_BridgeMode, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_BRIDGE_MODE, _wave_cfg80211_vendor_get_BridgeMode, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_RELIABLE_MULTICAST, _wave_cfg80211_vendor_set_ReliableMcast, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_RELIABLE_MULTICAST, _wave_cfg80211_vendor_get_ReliableMcast, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_AP_FORWARDING, _wave_cfg80211_vendor_set_APforwarding, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_AP_FORWARDING, _wave_cfg80211_vendor_get_APforwarding, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_NETWORK_MODE, _wave_cfg80211_vendor_get_NetworkMode, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_CDB_CFG, _wave_cfg80211_vendor_get_CdbCfg, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_BSS_BASIC_RATE_SET, _wave_cfg80211_vendor_set_BasicRateSet, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_BSS_BASIC_RATE_SET, _wave_cfg80211_vendor_get_BasicRateSet, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_HIDDEN_SSID, _wave_cfg80211_vendor_set_HiddenSSID, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_HIDDEN_SSID, _wave_cfg80211_vendor_get_HiddenSSID, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_UP_RESCAN_EXEMPTION_TIME, _wave_cfg80211_vendor_set_UpRescanExmpTm, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_UP_RESCAN_EXEMPTION_TIME, _wave_cfg80211_vendor_get_UpRescanExmpTm, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),

  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_FW_LOG_SEVERITY, _wave_cfg80211_vendor_set_FwLogSeverity, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),

  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_WDS_HOST_TIMEOUT, _wave_cfg80211_vendor_set_WDSHostTO, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_WDS_HOST_TIMEOUT, _wave_cfg80211_vendor_get_WDSHostTO, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),

  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_EEPROM, _wave_cfg80211_vendor_get_EEPROM, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),

  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_IRB_PINGER_ENABLED, _wave_cfg80211_vendor_set_IRBPngEnabled, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_IRB_PINGER_ENABLED, _wave_cfg80211_vendor_get_IRBPngEnabled, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_IRB_PINGER_STATS, _wave_cfg80211_vendor_set_IRBPngStatsRst, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_IRB_PINGER_STATS, _wave_cfg80211_vendor_get_IRBPngStatsRst, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),

  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_DCDP_API_LITEPATH, _wave_cfg80211_vendor_set_LtPathEnabled, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_DCDP_API_LITEPATH, _wave_cfg80211_vendor_get_LtPathEnabled, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_DCDP_API_LITEPATH_COMP, _wave_cfg80211_vendor_set_IpxPpaEnabled, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_DCDP_API_LITEPATH_COMP, _wave_cfg80211_vendor_get_IpxPpaEnabled, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),

  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_UDMA_API, _wave_cfg80211_vendor_set_UdmaEnabled, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_UDMA_API, _wave_cfg80211_vendor_get_UdmaEnabled, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_UDMA_API_EXT, _wave_cfg80211_vendor_set_UdmaEnabledExt, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_UDMA_API_EXT, _wave_cfg80211_vendor_get_UdmaEnabledExt, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_UDMA_VLAN_ID, _wave_cfg80211_vendor_set_UdmaVlanId, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_UDMA_VLAN_ID, _wave_cfg80211_vendor_get_UdmaVlanId, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_UDMA_VLAN_ID_EXT, _wave_cfg80211_vendor_set_UdmaVlanIdExt, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_UDMA_VLAN_ID_EXT, _wave_cfg80211_vendor_get_UdmaVlanIdExt, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),

  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_COC_POWER_MODE, _wave_cfg80211_vendor_set_CoCPower, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_COC_POWER_MODE, _wave_cfg80211_vendor_get_CoCPower, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_COC_AUTO_PARAMS, _wave_cfg80211_vendor_set_CoCAutoCfg, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_COC_AUTO_PARAMS, _wave_cfg80211_vendor_get_CoCAutoCfg, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),

  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_PCOC_POWER_MODE, _wave_cfg80211_vendor_set_PCoCPower, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_PCOC_POWER_MODE, _wave_cfg80211_vendor_get_PCoCPower, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_PCOC_AUTO_PARAMS, _wave_cfg80211_vendor_set_PCoCAutoCfg, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_PCOC_AUTO_PARAMS, _wave_cfg80211_vendor_get_PCoCAutoCfg, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_PCOC_PMCU_DEBUG, _wave_cfg80211_vendor_set_PMCUDebug, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),

  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_PRM_ID_TPC_LOOP_TYPE, _wave_cfg80211_vendor_set_TpcLoopType, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_PRM_ID_TPC_LOOP_TYPE, _wave_cfg80211_vendor_get_TpcLoopType, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),

  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_INTERFER_TIMEOUTS, _wave_cfg80211_vendor_set_InterfDetTime, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_INTERFER_THRESH, _wave_cfg80211_vendor_set_InterfDetThresh, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_INTERFER_SCANTIMES, _wave_cfg80211_vendor_set_InterfDetScan, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_INTERFER_MODE, _wave_cfg80211_vendor_get_InterfDetMode, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),

  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_AP_CAPABILITIES_MAX_STAs, _wave_cfg80211_vendor_get_APCapsMaxSTAs, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_AP_CAPABILITIES_MAX_VAPs, _wave_cfg80211_vendor_get_APCapsMaxVAPs, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),

  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_11B_ANTENNA_SELECTION, _wave_cfg80211_vendor_set_11bAntSelection, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_11B_ANTENNA_SELECTION, _wave_cfg80211_vendor_get_11bAntSelection, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),

  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_DBG_CLI, _wave_cfg80211_vendor_set_DoSimpleCLI, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_FW_DEBUG, _wave_cfg80211_vendor_set_DoFwDebug, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_DBG_ASSERT, _wave_cfg80211_vendor_set_DoDebugAssert, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),

  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_TA_TIMER_RESOLUTION, _wave_cfg80211_vendor_set_TATimerRes, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_TA_TIMER_RESOLUTION, _wave_cfg80211_vendor_get_TATimerRes, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_TA_DBG, _wave_cfg80211_vendor_get_TADbg, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),

  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_FW_RECOVERY, _wave_cfg80211_vendor_set_FWRecovery, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_FW_RECOVERY, _wave_cfg80211_vendor_get_FWRecovery, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_RCVRY_STATS, vendor_get_RcvryStats, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),

  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_SCAN_PARAMS, _wave_cfg80211_vendor_set_ScanParams, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_SCAN_PARAMS, _wave_cfg80211_vendor_get_ScanParams, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),

  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_SCAN_PARAMS_BG, _wave_cfg80211_vendor_set_ScanParamsBG, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_SCAN_PARAMS_BG, _wave_cfg80211_vendor_get_ScanParamsBG, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),

  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_SCAN_MODIFS, _wave_cfg80211_vendor_set_ScanModifFlags, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_SCAN_MODIFS, _wave_cfg80211_vendor_get_ScanModifFlags, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),

  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_CALIB_CW_MASKS, _wave_cfg80211_vendor_set_ScanCalCwMasks, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_CALIB_CW_MASKS, _wave_cfg80211_vendor_get_ScanCalCwMasks, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),

  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_SCAN_EXP_TIME, _wave_cfg80211_vendor_set_ScanExpTime, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_SCAN_EXP_TIME, _wave_cfg80211_vendor_get_ScanExpTime, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),

  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_GENL_FAMILY_ID, _wave_cfg80211_vendor_get_GenlFamilyId, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),

  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_TASKLET_LIMITS, _wave_cfg80211_vendor_set_TaskletLimits, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_TASKLET_LIMITS, _wave_cfg80211_vendor_get_TaskletLimits, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),

  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_RADIO_MODE, _wave_cfg80211_vendor_set_EnableRadio, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_RADIO_MODE, _wave_cfg80211_vendor_get_EnableRadio, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),

  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_AGGR_CONFIG, _wave_cfg80211_vendor_set_AggrConfig, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_AGGR_CONFIG, _wave_cfg80211_vendor_get_AggrConfig, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),

  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_AMSDU_NUM, _wave_cfg80211_vendor_set_NumMsduInAmsdu, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_AMSDU_NUM, _wave_cfg80211_vendor_get_NumMsduInAmsdu, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),

  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_AGG_RATE_LIMIT, _wave_cfg80211_vendor_set_AggRateLimit, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_AGG_RATE_LIMIT, _wave_cfg80211_vendor_get_AggRateLimit, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),

  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_BSS_LOAD_IN_BEACON, _wave_cfg80211_vendor_set_BssLoadInBeacon, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_BSS_LOAD_IN_BEACON, _wave_cfg80211_vendor_get_BssLoadInBeacon, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),

  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_ADMISSION_CAPACITY, _wave_cfg80211_vendor_set_AvailAdmCap, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_ADMISSION_CAPACITY, _wave_cfg80211_vendor_get_AvailAdmCap, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),

  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_RX_THRESHOLD, _wave_cfg80211_vendor_set_SetRxTH, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_RX_THRESHOLD, _wave_cfg80211_vendor_get_SetRxTH, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),

  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_RX_DUTY_CYCLE, _wave_cfg80211_vendor_set_RxDutyCyc, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_RX_DUTY_CYCLE, _wave_cfg80211_vendor_get_RxDutyCyc, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),

  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_TX_POWER_LIMIT_OFFSET, _wave_cfg80211_vendor_set_PowerSelection, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_TX_POWER_LIMIT_OFFSET, _wave_cfg80211_vendor_get_PowerSelection, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),

  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_PROTECTION_METHOD, _wave_cfg80211_vendor_set_11nProtection, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_PROTECTION_METHOD, _wave_cfg80211_vendor_get_11nProtection, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),

  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_BEAMFORM_EXPLICIT, _wave_cfg80211_vendor_get_BfExplicitCap, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),

  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_TEMPERATURE_SENSOR, _wave_cfg80211_vendor_set_CalibOnDemand, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV_UP),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_TEMPERATURE_SENSOR, _wave_cfg80211_vendor_get_Temperature, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV_UP),

  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_QAMPLUS_MODE, _wave_cfg80211_vendor_set_QAMplus, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_QAMPLUS_MODE, _wave_cfg80211_vendor_get_QAMplus, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),

  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_ACS_UPDATE_TO, _wave_cfg80211_vendor_set_AcsUpdateTo, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_ACS_UPDATE_TO, _wave_cfg80211_vendor_get_AcsUpdateTo, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),

  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_MU_OPERATION, _wave_cfg80211_vendor_set_MuOperation, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_MU_OPERATION, _wave_cfg80211_vendor_get_MuOperation, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),

  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_CCA_THRESHOLD, _wave_cfg80211_vendor_set_CcaTh, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_CCA_THRESHOLD, _wave_cfg80211_vendor_get_CcaTh, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),

  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_CCA_ADAPT, _wave_cfg80211_vendor_set_CcaAdapt, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_CCA_ADAPT, _wave_cfg80211_vendor_get_CcaAdapt, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),

  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_RADAR_RSSI_TH, _wave_cfg80211_vendor_set_RadarRssiTh, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_RADAR_RSSI_TH, _wave_cfg80211_vendor_get_RadarRssiTh, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),

  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_CPU_DMA_LATENCY, _wave_cfg80211_vendor_set_CpuDmaLatency, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_CPU_DMA_LATENCY, _wave_cfg80211_vendor_get_CpuDmaLatency, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),

  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_RTS_MODE, _wave_cfg80211_vendor_set_RTSmode, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_RTS_MODE, _wave_cfg80211_vendor_get_RTSmode, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),

  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_MAX_MPDU_LENGTH, _wave_cfg80211_vendor_set_MaxMpduLen, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_MAX_MPDU_LENGTH, _wave_cfg80211_vendor_get_MaxMpduLen, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),

  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_BF_MODE, _wave_cfg80211_vendor_set_BfMode, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_BF_MODE, _wave_cfg80211_vendor_get_BfMode, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),

  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_FIXED_RATE, _wave_cfg80211_vendor_set_FixedRateCfg, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),

  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_ACTIVE_ANT_MASK, _wave_cfg80211_vendor_set_ActiveAntMask, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV_UP),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_ACTIVE_ANT_MASK, _wave_cfg80211_vendor_get_ActiveAntMask, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),

  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_IRE_CTRL_B, _wave_cfg80211_vendor_set_CtrlBFilterBank, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_IRE_CTRL_B, _wave_cfg80211_vendor_get_CtrlBFilterBank, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),

  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_4ADDR_MODE, _wave_cfg80211_vendor_set_FourAddrMode, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_4ADDR_MODE, _wave_cfg80211_vendor_get_FourAddrMode, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_4ADDR_STA_ADD, _wave_cfg80211_vendor_set_AddFourAddrSta, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_4ADDR_STA_DEL, _wave_cfg80211_vendor_set_DelFourAddrSta, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_4ADDR_STA_LIST, _wave_cfg80211_vendor_get_FourAddrStas, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),

  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_TXOP_CONFIG, _wave_cfg80211_vendor_set_TxopConfig, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_TXOP_CONFIG, _wave_cfg80211_vendor_get_TxopConfig, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),

  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_SSB_MODE, _wave_cfg80211_vendor_set_SsbMode, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_SSB_MODE, _wave_cfg80211_vendor_get_SsbMode, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),

  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_COEX_ENABLE, _wave_cfg80211_vendor_set_EnableMRCoex, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_COEX_ENABLE, _wave_cfg80211_vendor_get_EnableMRCoex, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_COEX_CFG, _wave_cfg80211_vendor_set_ConfigMRCoex, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_COEX_CFG, _wave_cfg80211_vendor_get_ConfigMRCoex, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),

  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_PROBING_MASK, _wave_cfg80211_vendor_set_SlowProbingMask, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_PROBING_MASK, _wave_cfg80211_vendor_get_SlowProbingMask, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),

  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_MCAST_RANGE_SETUP, _wave_cfg80211_vendor_set_McastRange, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_MCAST_RANGE_SETUP, _wave_cfg80211_vendor_get_McastRange, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_MCAST_RANGE_SETUP_IPV6, _wave_cfg80211_vendor_set_McastRange6, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_MCAST_RANGE_SETUP_IPV6, _wave_cfg80211_vendor_get_McastRange6, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),

  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_ONLINE_CALIBRATION_ALGO_MASK, _wave_cfg80211_vendor_set_OnlineACM, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_ONLINE_CALIBRATION_ALGO_MASK, _wave_cfg80211_vendor_get_OnlineACM, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_CALIBRATION_ALGO_MASK, _wave_cfg80211_vendor_set_AlgoCalibrMask, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_CALIBRATION_ALGO_MASK, _wave_cfg80211_vendor_get_AlgoCalibrMask, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_USE_SHORT_CYCLIC_PREFIX, _wave_cfg80211_vendor_set_ShortCyclcPrfx, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_USE_SHORT_CYCLIC_PREFIX, _wave_cfg80211_vendor_Get_ShortCyclcPrfx, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),

  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_FREQ_JUMP_MODE, _wave_cfg80211_vendor_set_FreqJumpMode, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),

  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_FIXED_POWER, _wave_cfg80211_vendor_set_FixedPower, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_FIXED_POWER, _wave_cfg80211_vendor_get_FixedPower, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),

  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_UNCONNECTED_STA_SCAN_TIME, _wave_cfg80211_vendor_set_UnconnTime, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_UNCONNECTED_STA_SCAN_TIME, _wave_cfg80211_vendor_get_UnconnTime, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),

  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_RESTRICTED_AC_MODE, _wave_cfg80211_vendor_set_RestrictAcMode, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_RESTRICTED_AC_MODE, _wave_cfg80211_vendor_get_RestrictAcMode, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),

  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_PD_THRESHOLD, _wave_cfg80211_vendor_set_PdThresh, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_PD_THRESHOLD, _wave_cfg80211_vendor_get_PdThresh, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),

  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_FAST_DROP, _wave_cfg80211_vendor_set_FastDrop, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_FAST_DROP, _wave_cfg80211_vendor_get_FastDrop, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),

  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_ERP, _wave_cfg80211_vendor_set_ErpSet, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),

  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_FAST_DYNAMIC_MC_RATE, _wave_cfg80211_vendor_set_DmrConfig, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_FAST_DYNAMIC_MC_RATE, _wave_cfg80211_vendor_get_DmrConfig, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),

  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_COUNTERS_SRC, _wave_cfg80211_vendor_set_CountersSrc, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_COUNTERS_SRC, _wave_cfg80211_vendor_get_CountersSrc, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),

  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_FIXED_LTF_AND_GI, _wave_cfg80211_vendor_set_FixedLtfGi, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_FIXED_LTF_AND_GI, _wave_cfg80211_vendor_get_FixedLtfGi, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),

  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_STATIC_PLAN_CONFIG, _wave_cfg80211_vendor_set_MuStatPlanCfg, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),

  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_WDS_WEP_ENC_CFG, _wave_cfg80211_vendor_set_wds_wep_enc_cfg, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),

  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_PVT_SENSOR, _wave_cfg80211_vendor_get_pvt_sensor, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),

  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_ENABLE_TEST_BUS, _wave_cfg80211_vendor_set_enable_test_bus, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),

  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_HE_MU_OPERATION, _wave_cfg80211_vendor_set_he_mu_operation, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_HE_MU_OPERATION, _wave_cfg80211_vendor_get_he_mu_operation, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),

  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_RTS_RATE, _wave_cfg80211_vendor_set_rts_rate, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_RTS_RATE, _wave_cfg80211_vendor_get_rts_rate, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),

  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_MBSSID_VAP, _wave_cfg80211_vendor_hapd_set_mbssid_vap, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_MBSSID_NUM_VAPS_IN_GROUP, _wave_cfg80211_vendor_hapd_set_num_vaps_in_group, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_ASSOCIATED_DEV_RATE_INFO_RX_STATS, _wave_cfg80211_vendor_get_associated_dev_rate_info_rx_stats, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_ASSOCIATED_DEV_RATE_INFO_TX_STATS, _wave_cfg80211_vendor_get_associated_dev_rate_info_tx_stats, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_STATS_POLL_PERIOD, _wave_cfg80211_vendor_set_stats_poll_period, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_STATS_POLL_PERIOD, _wave_cfg80211_vendor_get_stats_poll_period, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_ASSOCIATED_DEV_STATS, _wave_cfg80211_vendor_get_associated_dev_stats, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_CHANNEL_STATS, _wave_cfg80211_vendor_get_channel_stats, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_ASSOCIATED_DEV_TID_STATS, _wave_cfg80211_vendor_get_associated_dev_tid_stats, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
#ifndef MTLK_LEGACY_STATISTICS
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_PEER_LIST, _wave_cfg80211_vendor_get_peer_list, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_PEER_FLOW_STATUS, _wave_cfg80211_vendor_get_peer_flow_status, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_PEER_CAPABILITIES, _wave_cfg80211_vendor_get_peer_capabilities, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_PEER_RATE_INFO, _wave_cfg80211_vendor_get_peer_rate_info, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_RECOVERY_STATS, _wave_cfg80211_vendor_get_recovery_stats, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_HW_FLOW_STATUS, _wave_cfg80211_vendor_get_hw_flow_status, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_TR181_WLAN_STATS, _wave_cfg80211_vendor_get_tr181_wlan_stats, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_TR181_HW_STATS, _wave_cfg80211_vendor_get_tr181_hw_stats, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_TR181_PEER_STATS, _wave_cfg80211_vendor_get_tr181_peer_stats, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
#endif
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_MGMT_FRAMES_RATE, _wave_cfg80211_vendor_hapd_set_management_frames_rate, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV_UP),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_NFRP_CFG, _wave_cfg80211_vendor_set_NfrpCfg, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),

  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_DYNAMIC_MU_TYPE, _wave_cfg80211_vendor_set_dynamic_mu_type, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_DYNAMIC_MU_TYPE, _wave_cfg80211_vendor_get_dynamic_mu_type, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_HE_MU_FIXED_PARAMETERS, _wave_cfg80211_vendor_set_he_mu_fixed_parameters, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_HE_MU_FIXED_PARAMETERS, _wave_cfg80211_vendor_get_he_mu_fixed_parameters, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_SET_HE_MU_DURATION, _wave_cfg80211_vendor_set_he_mu_duration, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_HE_MU_DURATION, _wave_cfg80211_vendor_get_he_mu_duration, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),

  VENDOR_CMD(LTQ_NL80211_VENDOR_SUBCMD_GET_PHY_INBAND_POWER, _wave_cfg80211_vendor_get_phy_inband_power, WIPHY_VENDOR_CMD_NEED_WDEV_NETDEV),
};

void _wave_cfg80211_register_vendor_cmds(struct wiphy *wiphy)
{
  wiphy->vendor_commands = _wave_cfg80211_vendor_commands;
  wiphy->n_vendor_commands = ARRAY_SIZE(_wave_cfg80211_vendor_commands);
}

#define VENDOR_EVT(cmd) {OUI_LTQ, (cmd)}

static struct
nl80211_vendor_cmd_info _wave_cfg80211_vendor_events[] = {
  VENDOR_EVT(LTQ_NL80211_VENDOR_EVENT_RX_EAPOL),
  VENDOR_EVT(LTQ_NL80211_VENDOR_EVENT_FLUSH_STATIONS),
  VENDOR_EVT(LTQ_NL80211_VENDOR_EVENT_CHAN_DATA),
  VENDOR_EVT(LTQ_NL80211_VENDOR_EVENT_UNCONNECTED_STA),
  VENDOR_EVT(LTQ_NL80211_VENDOR_EVENT_WDS_CONNECT),
  VENDOR_EVT(LTQ_NL80211_VENDOR_EVENT_WDS_DISCONNECT),
  VENDOR_EVT(LTQ_NL80211_VENDOR_EVENT_CSA_RECEIVED),
  VENDOR_EVT(LTQ_NL80211_VENDOR_EVENT_RADAR_DETECTED),
  VENDOR_EVT(LTQ_NL80211_VENDOR_EVENT_ASSERT_DUMP_READY),
  VENDOR_EVT(LTQ_NL80211_VENDOR_EVENT_NO_DUMP),
  VENDOR_EVT(LTQ_NL80211_VENDOR_EVENT_UNRECOVARABLE_ERROR),
  VENDOR_EVT(LTQ_NL80211_VENDOR_EVENT_MAC_ERROR),
  VENDOR_EVT(LTQ_NL80211_VENDOR_EVENT_SOFTBLOCK_DROP)
};

void _wave_cfg80211_register_vendor_evts(struct wiphy *wiphy)
{
  wiphy->vendor_events = _wave_cfg80211_vendor_events;
  wiphy->n_vendor_events = ARRAY_SIZE(_wave_cfg80211_vendor_events);
}

int __MTLK_IFUNC wave_cfg80211_vendor_cmd_reply (struct wiphy *wiphy, const void *data, int data_len)
{
  mtlk_nbuf_t *rsp_nbuf;

  rsp_nbuf = wv_cfg80211_vendor_cmd_alloc_reply_skb(wiphy, data_len);
  if (!rsp_nbuf) {
    return -ENOMEM;
  }

  nla_put(rsp_nbuf, NL80211_ATTR_VENDOR_DATA, data_len, data);

  return wv_cfg80211_vendor_cmd_reply(rsp_nbuf);
}
