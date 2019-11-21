#include "mtlkinc.h"
#include "scan_support.h"
#include "mhi_umi.h"
#include "mhi_umi_propr.h"
#include "mtlk_vap_manager.h"
#include "mtlk_param_db.h"
#include "mtlk_df_priv.h"
#include "mtlk_df_nbuf.h"
#include "core.h"
#include "core_config.h"
#include "mtlkhal.h"
#include "cfg80211.h"
#include "mac80211.h"
#include "mtlk_df.h"
#include "hw_mmb.h"
#include "vendor_cmds.h"

#define LOG_LOCAL_GID GID_SCAN_SUPPORT
#define LOG_LOCAL_FID 0

#define MTLK_CHANNEL_CW_MASK_5_2_GHZ        0x07  /* Mask to calibrate 20, 40 and 80 MHz bandwidths */
#define MTLK_CHANNEL_CW_MASK_2_4_GHZ        0x03  /* Mask to calibrate 20 and 40 MHz bandwidths */

/* FIXME: HW dependent timeout */
#define MTLK_MM_BLOCKED_CALIBRATION_TIMEOUT_GEN5  20000 /* ms */
#define MTLK_MM_BLOCKED_CALIBRATION_TIMEOUT_GEN6 300000 /* ms */

/* Conversion from 2.4 and 5 GHz channel index in our array of channel scan support data to channel number */
const uint8 chanidx2num_2_5[1 + NUM_TOTAL_CHANS] =
{
  /* --Idx--*/
  /*  0..14 */ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
  /* 15..22 */ 34, 36, 38, 40, 42, 44, 46, 48,
  /* 23..26 */ 52, 56, 60, 64,
  /* 27..38 */ 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 144,
  /* 39..43 */ 149, 153, 157, 161, 165,
  /* 44..47 */ 184, 188, 192, 196
};

/* Conversion from 2.4 and 5 GHz channel number to its index in our array of channel scan support_data
 * Include also central freqs for 40 MHz mode, in total NUM_5GHZ_CENTRAL_FREQS_BW40
 *   Channels 54, 62, 102, 110, 118, 126, 134, 142, 151, 159 --> Idx  48 ... 57
 * And central freqs for 80 MHz mode, in total NUM_5GHZ_CENTRAL_FREQS_BW80
 *   Channels 58, 106, 122, 138, 155 --> Idx  58 ... 62
 *   Channel 42 is already listed in the table
 * And central freqs for 160 MHz mode, in total NUM_5GHZ_CENTRAL_FREQS_BW160
 *   Channels 50 and 114 --> Idx 63 and 64
 */

/* this table uses indices from 0 to NUM_TOTAL_CHANS and to NUM_TOTAL_CHAN_FREQS, so we do a compile-time check here */
#if (47 != NUM_TOTAL_CHANS)
#error "NUM_TOTAL_CHANS is wrong"
#endif
#if (64 != NUM_TOTAL_CHAN_FREQS)
#error "NUM_TOTAL_CHAN_FREQS is wrong"
#endif

const uint8 channum2idx_2_5[256] =
{
   0,  1,  2,  3,  4,  5,  6,  7,  8,  9, /* 0--9 */
  10, 11, 12, 13, 14,  0,  0,  0,  0,  0, /* 10--19 */
   0,  0,  0,  0,  0,  0,  0,  0,  0,  0, /* 20--29 */
   0,  0,  0,  0, 15,  0, 16,  0, 17,  0, /* 30--39 */
  18,  0, 19,  0, 20,  0, 21,  0, 22,  0, /* 40--49 */
  63,  0, 23,  0, 48,  0, 24,  0, 58,  0, /* 50--59 */
  25,  0, 49,  0, 26,  0,  0,  0,  0,  0, /* 60--69 */
   0,  0,  0,  0,  0,  0,  0,  0,  0,  0, /* 70--79 */
   0,  0,  0,  0,  0,  0,  0,  0,  0,  0, /* 80--89 */
   0,  0,  0,  0,  0,  0,  0,  0,  0,  0, /* 90--99 */
  27,  0, 50,  0, 28,  0, 59,  0, 29,  0, /* 100--109 */
  51,  0, 30,  0, 64,  0, 31,  0, 52,  0, /* 110--119 */
  32,  0, 60,  0, 33,  0, 53,  0, 34,  0, /* 120--129 */
   0,  0, 35,  0, 54,  0, 36,  0, 61,  0, /* 130--139 */
  37,  0, 55,  0, 38,  0,  0,  0,  0, 39, /* 140--149 */
   0, 56,  0, 40,  0, 62,  0, 41,  0, 57, /* 150--159 */
   0, 42,  0,  0,  0, 43,  0,  0,  0,  0, /* 160--169 */
   0,  0,  0,  0,  0,  0,  0,  0,  0,  0, /* 170--179 */
   0,  0,  0,  0, 44,  0,  0,  0, 45,  0, /* 180--189 */
   0,  0, 46,  0,  0,  0, 47,  0,  0,  0, /* 190--199 */
   0,  0,  0,  0,  0,  0,  0,  0,  0,  0, /* 200--209 */
   0,  0,  0,  0,  0,  0,  0,  0,  0,  0, /* 210--219 */
   0,  0,  0,  0,  0,  0,  0,  0,  0,  0, /* 220--229 */
   0,  0,  0,  0,  0,  0,  0,  0,  0,  0, /* 230--239 */
   0,  0,  0,  0,  0,  0,  0,  0,  0,  0, /* 240--249 */
   0,  0,  0,  0,  0,  0                  /* 250--255 */
};

/* IEEE 802.11 5 GHz channel bonding
 *   BW  Channels
 *   20:  36  40  44  48  52  56  60  64   100 104 108 112  116 120 124 128  132 136 140 144  149 153 157 161  165
 *   40:    38      46      54      62       102     110      118     126      134     142      151     159
 *   80:        42              58               106              122              138              155
 *  160:                50                               114
 */

/* Channel bonding table.
 * First element of chan_lower array is for BW 20 MHz i.e. chan_lower[0] = primary channel number.
 * A zero value means that BW is not supported.
 */

static wave_chan_bonding_t _chan_5G_bonding_table[] = {
    /*    Lower Channel             Center Channel              Upper Channel    */
    /*  20   40   80  160          20   40   80  160          20   40   80  160  */
    {{  36,  36,  36,  36 },    {  36,  38,  42,  50 },    {  36,  40,  48,  64 }},
    {{  40,  36,  36,  36 },    {  40,  38,  42,  50 },    {  40,  40,  48,  64 }},
    {{  44,  44,  36,  36 },    {  44,  46,  42,  50 },    {  44,  48,  48,  64 }},
    {{  48,  44,  36,  36 },    {  48,  46,  42,  50 },    {  48,  48,  48,  64 }},
    {{  52,  52,  52,  36 },    {  52,  54,  58,  50 },    {  52,  56,  64,  64 }},
    {{  56,  52,  52,  36 },    {  56,  54,  58,  50 },    {  56,  56,  64,  64 }},
    {{  60,  60,  52,  36 },    {  60,  62,  58,  50 },    {  60,  64,  64,  64 }},
    {{  64,  60,  52,  36 },    {  64,  62,  58,  50 },    {  64,  64,  64,  64 }},

    {{ 100, 100, 100, 100 },    { 100, 102, 106, 114 },    { 100, 104, 112, 128 }},
    {{ 104, 100, 100, 100 },    { 104, 102, 106, 114 },    { 104, 104, 112, 128 }},
    {{ 108, 108, 100, 100 },    { 108, 110, 106, 114 },    { 108, 112, 112, 128 }},
    {{ 112, 108, 100, 100 },    { 112, 110, 106, 114 },    { 112, 112, 112, 128 }},
    {{ 116, 116, 116, 100 },    { 116, 118, 122, 114 },    { 116, 120, 128, 128 }},
    {{ 120, 116, 116, 100 },    { 120, 118, 122, 114 },    { 120, 120, 128, 128 }},
    {{ 124, 124, 116, 100 },    { 124, 126, 122, 114 },    { 124, 128, 128, 128 }},
    {{ 128, 124, 116, 100 },    { 128, 126, 122, 114 },    { 128, 128, 128, 128 }},

    {{ 132, 132, 132,   0 },    { 132, 134, 138,   0 },    { 132, 136, 144,   0 }},
    {{ 136, 132, 132,   0 },    { 136, 134, 138,   0 },    { 136, 136, 144,   0 }},
    {{ 140, 140, 132,   0 },    { 140, 142, 138,   0 },    { 140, 144, 144,   0 }},
    {{ 144, 140, 132,   0 },    { 144, 142, 138,   0 },    { 144, 144, 144,   0 }},

    {{ 149, 149, 149,   0 },    { 149, 151, 155,   0 },    { 149, 153, 161,   0 }},
    {{ 153, 149, 149,   0 },    { 153, 151, 155,   0 },    { 153, 153, 161,   0 }},
    {{ 157, 157, 149,   0 },    { 157, 159, 155,   0 },    { 157, 161, 161,   0 }},
    {{ 161, 157, 149,   0 },    { 161, 159, 155,   0 },    { 161, 161, 161,   0 }},

    {{ 165,   0,   0,   0 },    { 165,   0,   0,   0 },    { 165,   0,   0,   0 }},
};

static wave_chan_bonding_t *
_wave_scan_support_find_5G_chan_bonding (unsigned chan) {
  wave_chan_bonding_t *entry;
  wave_chan_bonding_t *found = NULL;
  int  i;

  for (i = 0; i < ARRAY_SIZE(_chan_5G_bonding_table); i++) {
    entry = &_chan_5G_bonding_table[i];
    if (chan == entry->lower_chan[0]) {
      found = entry;
    }
  }

  if (!found) {
    ELOG_D("Unsupported channel %u", chan);
  }

  return found;
}

void __MTLK_IFUNC
wave_scan_support_fill_chan_bonding_by_chan (wave_chan_bonding_t *out_data, unsigned center_chan, unsigned primary_chan)
{
  wave_chan_bonding_t *cfg = NULL;

  MTLK_ASSERT(out_data);
  MTLK_ASSERT(MAX_UINT8 >= primary_chan);
  MTLK_ASSERT(MAX_UINT8 >= center_chan);

  memset(out_data, 0, sizeof(*out_data));

  if (MTLK_HW_BAND_5_2_GHZ == channel_to_band(primary_chan)) {
    /* 5 GHz band -- search in the table and fill if found */
    cfg = _wave_scan_support_find_5G_chan_bonding(primary_chan);
    if (cfg) {
      *out_data = *cfg;
    }
  } else {
    /* 2.4 GHz band -- fill out_data for BWs 20 MHz and 40 MHz */
    /* Fill BW 20 data always i.e. for any BW setting */
    out_data->center_chan[CW_20] = primary_chan;
    out_data->lower_chan[CW_20]  = primary_chan;
    out_data->upper_chan[CW_20]  = primary_chan;
    /* Fill BW 40 data only if required */
    if (primary_chan != center_chan) { /* BW 40 */
      unsigned lower, upper;
      if (primary_chan < center_chan) {
        lower = primary_chan;
        upper = primary_chan + CHANNUMS_PER_20MHZ;
      } else {
        lower = primary_chan - CHANNUMS_PER_20MHZ;
        upper = primary_chan;
      }

      out_data->center_chan[CW_40] = center_chan;
      out_data->lower_chan[CW_40]  = lower;
      out_data->upper_chan[CW_40]  = upper;
    }
  }
}

void __MTLK_IFUNC
wave_scan_support_fill_chan_bonding_by_freq (wave_chan_bonding_t *out_data, unsigned center_freq, unsigned primary_freq)
{
  return wave_scan_support_fill_chan_bonding_by_chan(out_data,
                                                     ieee80211_frequency_to_channel(center_freq),
                                                     ieee80211_frequency_to_channel(primary_freq));
}

/* the scan timer calls this one, which passes the serious work to the serializer */
/* any context calling abort_scan may also call it */
uint32 scan_timer_clb_func(mtlk_osal_timer_t *timer, mtlk_handle_t nic)
{
  mtlk_core_t *core = (mtlk_core_t *) nic;

  MTLK_ASSERT(core == mtlk_vap_manager_get_master_core(mtlk_vap_get_manager(core->vap_handle)));

  if (wave_rcvry_mac_fatal_pending_get(mtlk_vap_get_hw(core->vap_handle)))
    return MTLK_ERR_OK;

  /* this interface is way too heavy for the simple context switch we need to do */
  _mtlk_df_user_invoke_core_async(mtlk_vap_get_df(core->vap_handle), WAVE_RADIO_REQ_SCAN_TIMEOUT, NULL, 0, NULL, 0);

  return MTLK_ERR_OK;
}

/* the scheduled scan timer calls this one */
uint32 sched_scan_timer_clb_func(mtlk_osal_timer_t *timer, mtlk_handle_t nic)
{
  mtlk_core_t *core = (mtlk_core_t *) nic;
  struct mtlk_scan_request *sr = core->slow_ctx->sched_scan_req;
  int res = MTLK_ERR_OK;

  if (sr)
  {
    mtlk_df_t *master_df = mtlk_vap_manager_get_master_df(mtlk_vap_get_manager(core->vap_handle));
    mtlk_clpb_t *clpb;

    res = _mtlk_df_user_invoke_core(master_df, WAVE_RADIO_REQ_DO_SCAN, &clpb, sr, sizeof(*sr));
    res = _mtlk_df_user_process_core_retval_void(res, clpb, WAVE_RADIO_REQ_DO_SCAN, TRUE);

    mtlk_osal_timer_set(&core->slow_ctx->sched_scan_timer, sr->interval); /* it's in milliseconds already */
  }

  return res;
}

/* the CAC timer calls this one */
uint32 cac_timer_clb_func(mtlk_osal_timer_t *timer, mtlk_handle_t nic)
{
  mtlk_core_t *core = (mtlk_core_t *) nic;

  MTLK_ASSERT(timer == &core->slow_ctx->cac_timer);

  _mtlk_df_user_invoke_core_async(mtlk_vap_get_df(core->vap_handle),
                                  WAVE_RADIO_REQ_CAC_TIMEOUT, NULL, 0, NULL, 0);
  return 0;
}

int cac_timer_serialized_func (mtlk_handle_t hcore, const void* data, uint32 data_size)
{
  mtlk_df_t *df;
  mtlk_df_user_t *df_user;
  struct net_device *ndev;
  mtlk_scan_support_t *ss;
  struct mtlk_chan_def *current_chandef;
  struct set_chan_param_data cpd;
  mtlk_core_t *core = (mtlk_core_t *) hcore;
  wave_radio_t *radio = wave_vap_radio_get(core->vap_handle);

  MTLK_ASSERT(core == mtlk_core_get_master(core));

  /* wait for Radar Detection end */
  wave_radio_radar_detect_end_wait(radio);

  df = mtlk_vap_get_df(core->vap_handle);
  df_user = mtlk_df_get_user(df);
  ndev = mtlk_df_user_get_ndev(df_user);
  ss = mtlk_core_get_scan_support(core);
  current_chandef = __wave_core_chandef_get(core);

  ILOG0_V("CAC timer expired");

  /* If debug switch in progress, it means cac timer has interrupted by _handle_radar_event()
     and we shouldn't notify HostAPD as timer will be canceled */
  if (!ss->dfs_debug_params.switch_in_progress)
    wv_cfg80211_cac_event(ndev, current_chandef, 1); /* 1 means it did finish */

  if (mtlk_core_cfg_get_block_tx(core) == 0) {
    memset(&cpd, 0, sizeof(cpd));
    cpd.chandef = *current_chandef;
    cpd.ndev = ndev;
    cpd.vap_id = mtlk_vap_get_id(mtlk_df_get_vap_handle(df));
    /* Don't notify kernel about channel switch in this specific case to avoid deadlock,
     * as cfg80211_cac_event will trigger all interface up and channel switch procedures */
    cpd.dont_notify_kernel = TRUE;

    _mtlk_df_user_invoke_core_async(df, WAVE_RADIO_REQ_SET_CHAN, &cpd, sizeof(cpd), NULL, 0);
  }
  mtlk_core_cfg_set_block_tx(core, 0);
  return MTLK_ERR_OK;
}

/* called from radio module */
int scan_support_init(mtlk_core_t *core, mtlk_scan_support_t *ss)
{
  wave_radio_t *radio = wave_vap_radio_get(core->vap_handle);
  mtlk_pdb_size_t size;
  int res = MTLK_ERR_OK;

  memset(ss, 0, sizeof(*ss));

  ss->last_chan_idx = -1;
  ss->last_chan_band = -1;
  ss->max_widths[UMI_PHY_TYPE_BAND_2_4_GHZ] = CW_40;
  ss->max_widths[UMI_PHY_TYPE_BAND_5_2_GHZ] = CW_80; /* this should come from HW or calib file */

  size = sizeof(ss->iwpriv_scan_params);
  res = WAVE_RADIO_PDB_GET_BINARY(radio, PARAM_DB_RADIO_SCAN_PARAMS, &ss->iwpriv_scan_params, &size);

  ss->PassiveValid = msecs_to_jiffies(1000 * ss->iwpriv_scan_params.passive_scan_valid_time);
  ss->ActiveValid = msecs_to_jiffies(1000 * ss->iwpriv_scan_params.active_scan_valid_time);

  ss->dfs_debug_params.beacon_count    = -1;
  ss->dfs_debug_params.cac_time        = -1;
  ss->master_core = core; /* save the core too for convenience */

  mtlk_osal_timer_init(&ss->scan_timer, &scan_timer_clb_func, HANDLE_T(core));
  mtlk_osal_event_init(&ss->scan_end_event);

  return res;
}

/* called from radio module */
void scan_support_cleanup(mtlk_scan_support_t *ss)
{
  mtlk_osal_event_cleanup(&ss->scan_end_event);
  mtlk_osal_timer_cleanup(&ss->scan_timer);
}


int low_chan_160[] = { 36, 100 };
int low_chan_80[] = { 36, 52, 100, 116, 132, 149 };
int low_chan_40[] = { 36, 44, 52, 60, 100, 108, 116, 124, 132, 140, 149, 157 };

/* Is the channel with the given chan_num allowed as lower channel ? */
static __INLINE
int low_chan_allowed(int chan_num, unsigned cw, mtlk_hw_band_e band)
{
  int i, low_count, *low_list;

  if (MTLK_HW_BAND_5_2_GHZ != band) return TRUE; /* only checking on 5.2 */

  switch (cw) {
    case CW_160:
      low_list = low_chan_160;
      low_count = sizeof(low_chan_160) / sizeof(int);
      break;

    case CW_80:
      low_list = low_chan_80;
      low_count = sizeof(low_chan_80) / sizeof(int);
      break;

    case CW_40:
      low_list = low_chan_40;
      low_count = sizeof(low_chan_40) / sizeof(int);
      break;

    case CW_20:
      return TRUE;

    default:
      ELOG_D("unsupported channel width %d", cw);
      return 0;
  }

  for (i = 0; i < low_count; i++)
    if (chan_num == low_list[i]) return TRUE;

  return 0;
}


/* Is the channel with the given chan_num enabled in the array of channels? */
static __INLINE
int is_chan_enabled(struct mtlk_channel channels[], int num_channels, int chan_num)
{
  int i;

  for (i = 0; i < num_channels; i++)
    if (freq2lowchannum(channels[i].center_freq, CW_20) == chan_num)
      return !(channels[i].flags & MTLK_CHAN_DISABLED);

  return 0;
}

int send_calibrate(mtlk_core_t *core, mtlk_hw_band_e band, unsigned cw,
                   const u8 chans[MAX_CALIB_CHANS], unsigned num_chans, uint32 *status)
{
  mtlk_txmm_msg_t man_msg;
  mtlk_txmm_data_t *man_entry;
  UMI_CALIBRATE_PARAMS *req;
  int res = MTLK_ERR_OK;

  ILOG3_V("Running");

  /* prepare msg for the FW */
  if (!(man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, mtlk_vap_get_txmm(core->vap_handle), &res)))
  {
    ELOG_DD("CID-%04x: UM_MAN_CALIBRATE_REQ init failed, err=%i",
            mtlk_vap_get_oid(core->vap_handle), res);
    res = MTLK_ERR_NO_RESOURCES;
    goto end;
  }

  man_entry->id = UM_MAN_CALIBRATE_REQ;
  man_entry->payload_size = sizeof(*req);
  req = (UMI_CALIBRATE_PARAMS *) man_entry->payload;
  memset(req, 0, sizeof(*req));

  wave_memcpy(req->chan_nums, sizeof(req->chan_nums), chans, num_chans);
  req->num_chans = num_chans;
  req->chan_width = cw;

  mtlk_dump(1, req, sizeof(*req), "dump of UMI_CALIBRATE_PARAMS:");

  res = mtlk_txmm_msg_send_blocked(&man_msg,
                                   mtlk_hw_type_is_gen5(mtlk_vap_get_hw(core->vap_handle)) ?
                                   MTLK_MM_BLOCKED_CALIBRATION_TIMEOUT_GEN5 :
                                   MTLK_MM_BLOCKED_CALIBRATION_TIMEOUT_GEN6);

  if (res != MTLK_ERR_OK)
  {
      ELOG_DD("CID-%04x: UM_MAN_CALIBRATE_REQ send failed, err=%i",
              mtlk_vap_get_oid(core->vap_handle), res);
  }
  else if (req->status)
  {
    ELOG_DD("CID-%04x: UM_MAN_CALIBRATE_REQ execution failed, status=0x%08x",
            mtlk_vap_get_oid(core->vap_handle), req->status);
    *status = req->status;
  }

  mtlk_txmm_msg_cleanup(&man_msg);

end:
  return res;
}


/* PHY_RX_STATUS */
static struct channel_survey_support *
scan_get_csys_by_chan(mtlk_core_t *core, uint32 channel)
{
  unsigned cssidx = channum2csysidx(channel);
  mtlk_scan_support_t *ss = __wave_core_scan_support_get(core);

  if (cssidx != CHAN_IDX_ILLEGAL)
    return &ss->csys[cssidx];
  else
    return NULL;
}

static struct channel_survey_support *
scan_get_curr_csys(mtlk_core_t *core)
{
    uint32 channel = WAVE_RADIO_PDB_GET_INT(wave_vap_radio_get(core->vap_handle), PARAM_DB_RADIO_CHANNEL_CUR);

    if (0 == channel) /* is not set yet */
        return NULL;
    else return scan_get_csys_by_chan(core, channel);
}

void scan_update_curr_chan_info(mtlk_core_t *core, int noise, unsigned ch_load)
{
    struct channel_survey_support *csys;

    csys = scan_get_curr_csys(core);
    if (NULL != csys) {
        ILOG3_DDD("CID-%04x: noise %d, ch_load %d",
                   mtlk_vap_get_oid(core->vap_handle),
                   noise, ch_load);

        csys->load  = ch_load;
        csys->noise = noise;
    }
}

void scan_chan_survey_set_chan_load (struct channel_survey_support *csys, unsigned ch_load)
{
    ILOG1_DD("Set channel load: 0x%02X (%d)", ch_load, ch_load);
    if (csys)
        csys->load = ch_load;
}

int scan_send_get_chan_load(const mtlk_core_t *core, uint32 *chan_load)
{
  mtlk_txmm_msg_t            man_msg;
  mtlk_txmm_data_t          *man_entry;
  UMI_GET_CHANNEL_LOAD_REQ  *req;
  int res = MTLK_ERR_OK;

  /* prepare msg for the FW */
  if (!(man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, mtlk_vap_get_txmm(core->vap_handle), &res))) {
    ELOG_D("CID-%04x: GET_CHANNEL_LOAD_REQ init failed", mtlk_vap_get_oid(core->vap_handle));
    return MTLK_ERR_NO_RESOURCES;
  }

  man_entry->id = UM_MAN_GET_CHANNEL_LOAD_REQ;
  man_entry->payload_size = sizeof(*req);
  req = (UMI_GET_CHANNEL_LOAD_REQ *)man_entry->payload;
  memset(req, 0, sizeof(*req));

  res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);
  if (res != MTLK_ERR_OK) {
    ELOG_DD("CID-%04x: GET_CHANNEL_LOAD_REQ send failed, err=%i",
      mtlk_vap_get_oid(core->vap_handle), res);
  }
  else {
    *chan_load = MAC_TO_HOST32(req->channelLoad);
  }

  mtlk_txmm_msg_cleanup(&man_msg);

  return res;
}

#ifdef MTLK_LEGACY_STATISTICS
int scan_get_aocs_info (mtlk_core_t *core, struct intel_vendor_channel_data *ch_data, struct channel_survey_support *csys)
{
  mtlk_txmm_msg_t            man_msg;
  mtlk_txmm_data_t          *man_entry;
  UMI_AOCS_INFO_t           *req;
  wave_radio_t              *radio = wave_vap_radio_get(core->vap_handle);
  int res = MTLK_ERR_OK;

  if (WAVE_RADIO_OFF == wave_radio_mode_get(radio)) {
    ILOG2_D("CID-%04x: GET_AOCS_INFO_REQ not sent - radio off", mtlk_vap_get_oid(core->vap_handle));
    return MTLK_ERR_BUSY;
  }

  /* prepare msg for the FW */
  if (!(man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, mtlk_vap_get_txmm(core->vap_handle), &res))) {
    ELOG_D("CID-%04x: GET_AOCS_INFO_REQ init failed", mtlk_vap_get_oid(core->vap_handle));
    return MTLK_ERR_NO_RESOURCES;
  }

  man_entry->id = UM_MAN_AOCS_INFO_REQ;
  man_entry->payload_size = sizeof(*req);
  req = (UMI_AOCS_INFO_t *) man_entry->payload;
  memset(req, 0, sizeof(*req));

  res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);
  if (res != MTLK_ERR_OK) {
    ELOG_DD("CID-%04x: GET_AOCS_INFO_REQ send failed, err=%i",
             mtlk_vap_get_oid(core->vap_handle), res);
  } else {
    uint32 chan_load = 0;

    ch_data->dynBW20             = MAC_TO_HOST32(req->dynamicBW20MHz);
    ch_data->dynBW40             = MAC_TO_HOST32(req->dynamicBW40MHz);
    ch_data->dynBW80             = MAC_TO_HOST32(req->dynamicBW80MHz);
    ch_data->dynBW160            = MAC_TO_HOST32(req->dynamicBW160MHz);
    ch_data->ext_sta_rx          = MAC_TO_HOST32(req->extStaRx);
    ch_data->tx_power            = MAC_TO_HOST16(req->TxPower);
    ch_data->rssi                = req->RSSI;
    ch_data->snr                 = req->SNR;
    ch_data->noise_floor         = req->noiseFloor;
    ch_data->cwi_noise           = req->CWIvalue;
    ch_data->not_80211_rx_evt    = req->not802dot11Event;
    ch_data->busy_time           = (uint32)req->channelLoad * 255 / 100;

    /* During scan special API to get channel load has to be used */
    if (mtlk_core_scan_is_running(core)) {
      res = scan_send_get_chan_load(core, &chan_load);
      if (MTLK_ERR_OK != res) {
        ELOG_D("Failed to get channel load from FW, res = %d", res);
        goto end;
      }
    }
    else {
      chan_load = req->channelLoad;
    }

    /* Value from FW should be in percent */
    if (chan_load > 100) {
      ELOG_D("Invalid channel load value (%u) received from FW", chan_load);
      res = MTLK_ERR_PARAMS;
      goto end;
    }

    ch_data->load = chan_load;

    ch_data->filled_mask = CHDATA_DYNBW | CHDATA_NOISE_FLOOR | CHDATA_RSSI | CHDATA_SNR |
                           CHDATA_CWI_NOISE | CHDATA_NOT_80211_EVT | CHDATA_TX_POWER |
                           CHDATA_LOAD | CHDATA_BUSY_TIME | CHDATA_LOW_RSSI;

    ILOG2_DDDDDDDDDDDD("debug AOCS_info: %d %d %d %d %d %d %d %d %d %d %d %d",
      ch_data->dynBW20, ch_data->dynBW40, ch_data->dynBW80, ch_data->dynBW160, ch_data->ext_sta_rx, ch_data->tx_power,
      ch_data->rssi, ch_data->snr, ch_data->noise_floor, ch_data->cwi_noise, ch_data->not_80211_rx_evt, ch_data->busy_time);

    scan_chan_survey_set_chan_load(csys, chan_load);
  }

end:
  mtlk_txmm_msg_cleanup(&man_msg);

  return res;
}
#endif /* MTLK_LEGACY_STATISTICS */

/* Arranges that the scan gets aborted right away but does not wait for it to really end */
void abort_scan(struct scan_support *ss)
{
  if (is_scan_running(ss))
  {
    ILOG0_V("Aborting the scan");
    ss->flags |= SDF_ABORT;

    /* we ought to have a memory write barrier between setting the flag and enqueueing the scan timer func
     * but the timer cancellation function is complicated enough (locks, etc.)
     * that it should serve as such automatically
     */
    mtlk_osal_timer_cancel_sync(&ss->scan_timer);

    /* call it one more time to make sure it has seen the ABORT flag set */
    scan_timer_clb_func(NULL, HANDLE_T(ss->master_core));
  }
}

static int scan_timeout_func(mtlk_core_t *core, mtlk_scan_support_t *ss);

/* Arranges that the scan gets aborted right away and waits for it. */
/* supposed to be run on the master VAP's serializer */
int abort_scan_sync(mtlk_core_t *mcore)
{
  struct scan_support *ss = __wave_core_scan_support_get(mcore);

  MTLK_ASSERT(!in_atomic());
  MTLK_ASSERT(mcore == mtlk_core_get_master(mcore));

  if (!is_scan_running(ss))
    return MTLK_ERR_OK;

  ILOG0_V("Aborting the scan and waiting for it to end");
  ss->flags |= SDF_ABORT;

  mtlk_osal_timer_cancel_sync(&ss->scan_timer);

  /* we need to call the scan timeout function to make sure it has seen the ABORT flag */
  scan_timeout_func(mcore, ss);

  return MTLK_ERR_OK;
}

/* supposed to be run on the master VAP's serializer */
int pause_or_prevent_scan(mtlk_core_t *mcore)
{
  struct scan_support *ss = __wave_core_scan_support_get(mcore);

  MTLK_ASSERT(!in_atomic());
  MTLK_ASSERT(mcore == mtlk_core_get_master(mcore));

  if (ss->flags & SDF_PAUSE_OR_PREVENT)
  {
    ELOG_V("The scan has already been paused/prevented");
    return MTLK_ERR_OK;
  }

  ss->flags |= SDF_PAUSE_OR_PREVENT; /* prevent a new scan from starting */

  if (!is_scan_running(ss))
  {
    ILOG2_V("New scans prevented");
    return MTLK_ERR_OK;
  }

  ILOG2_V("Pausing the scan");
  mtlk_osal_timer_cancel_sync(&ss->scan_timer);

  scan_timeout_func(mcore, ss);

  ILOG2_V("Scan paused");
  return MTLK_ERR_OK;
}

/* supposed to be run on the master VAP's serializer */
int resume_or_allow_scan(mtlk_core_t *mcore)
{
  struct scan_support *ss = __wave_core_scan_support_get(mcore);

  MTLK_ASSERT(!in_atomic());
  MTLK_ASSERT(mcore == mtlk_core_get_master(mcore));

  if (!(ss->flags & SDF_PAUSE_OR_PREVENT))
  {
    ELOG_V("The scan is not paused/prevented");
    return MTLK_ERR_BUSY;
  }

  ss->flags &= ~SDF_PAUSE_OR_PREVENT;

  if (!is_scan_running(ss))
  {
    ILOG2_V("New scans allowed");
    return MTLK_ERR_OK;
  }

  ILOG2_V("Resuming the scan");

  scan_timeout_func(mcore, ss);

  ILOG2_V("Scan resumed");
  return MTLK_ERR_OK;
}


/* gets done only from the master serializer's context */
int clean_up_after_this_scan(mtlk_core_t *core, struct scan_support *ss)
{
  mtlk_df_t *df = mtlk_vap_get_df(core->vap_handle);
  const char *name = mtlk_df_get_name(df);
  int res = MTLK_ERR_OK;
  struct mtlk_chan_def *cd = __wave_core_chandef_get(core);

  MTLK_UNREFERENCED_PARAM(name); /* printouts only */

  /* Make the current channel "uncertain". it will probably stay uncertain until
   * hostapd issues some command that will end up setting it. We do the above as
   * it's nice to ignore frames because they're quite likely not on the channel
   * we're going to use and because we haven't yet bothered to set the channel number in param DB.
   */
  cd->center_freq1 = 0;

  wmb(); /* so that channel gets marked uncertain before we mark the scan as having ended */

  ss->flags = 0; /* do this before set_channel as it influences the way set_channel is done */

  wmb(); /* so that set_channel really knows the scan has ended */

  /* we have to restore the channel, if we had one, and set normal mode (not scan mode) */
  if (is_channel_certain(&ss->orig_chandef))
    res = core_cfg_set_chan(core, &ss->orig_chandef, NULL); /* this takes care of hdk config, etc. */
  else
    ILOG0_S("%s: Leaving the channel as uncertain after the scan", name);

  /* else it will soon get set explicitly by hostapd and most likely has been set by the scan, just the mode is still SCAN */

  if (ss->set_chan_man_msg.data) /* if the set_chan msg was initialized */
  {
    mtlk_txmm_msg_cleanup(&ss->set_chan_man_msg);
    ss->set_chan_man_msg.data = NULL;
  }

  if (ss->probe_req)
  {
    mtlk_osal_mem_free(ss->probe_req);
    ss->probe_req = NULL;
    ss->probe_req_len = 0;
  }

  ss->last_chan_idx = -1;
  ss->chan_in_chunk = 0;
  return res;
}

int mtlk_alter_scan(mtlk_handle_t hcore, const void* data, uint32 data_size)
{
  unsigned size;
  int res = MTLK_ERR_OK;
  mtlk_alter_scan_t *alter_scan;
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **)data;
  mtlk_core_t *core = HANDLE_T_PTR(mtlk_core_t, hcore);

  MTLK_ASSERT(sizeof(mtlk_clpb_t *) == data_size);

  /* supposed to be run on the Master VAP's serializer */
  MTLK_ASSERT(core == mtlk_vap_manager_get_master_core(mtlk_vap_get_manager(core->vap_handle)));

  alter_scan = mtlk_clpb_enum_get_next(clpb, &size);
  MTLK_CLPB_TRY(alter_scan, size)
    if (alter_scan->abort_scan)
      abort_scan_sync(core);

    if (alter_scan->pause_or_prevent)
      pause_or_prevent_scan(core);

    if (alter_scan->resume_or_allow)
      resume_or_allow_scan(core);
  MTLK_CLPB_FINALLY(res)
    return mtlk_clpb_push_res(clpb, res);
  MTLK_CLPB_END
}

/* supposed to be run on the Master VAP's serializer */
int scan_timeout_async_func(mtlk_handle_t hcore, const void* data, uint32 data_size)
{
  mtlk_core_t *core = HANDLE_T_PTR(mtlk_core_t, hcore);
  struct scan_support *ss = __wave_core_scan_support_get(core);

  MTLK_ASSERT(core == mtlk_vap_manager_get_master_core(mtlk_vap_get_manager(core->vap_handle)));

  return scan_timeout_func(core, ss); /* call the function that will get the real work done */
}

#define MGMT_FRAME_HEADER_LEN 24 /* we could also get this from some OS-specific includes, e.g. ieee80211.h */

static __INLINE
void finalize_and_send_probe_req(mtlk_core_t *core, struct scan_support *ss, struct mtlk_scan_request *req, int k)
{
  uint64 cookie;
  u8 *ie_ptr = ss->probe_req + MGMT_FRAME_HEADER_LEN; /* the spot where the IEs start in probe requests */
  u8 *ie_ptr_end = ss->probe_req + ss->probe_req_len;
  size_t len = wave_strlen(req->ssids[k], MAX_SSID_LEN);

  *ie_ptr++ = SSID_IE_TAG;
  *ie_ptr++ = len;

  wave_memcpy(ie_ptr, ie_ptr_end - ie_ptr, req->ssids[k], len);
  ie_ptr += len;

  /* if any bits were set in the band ratemask, use the appropriate prepared supported rates array */
  if (req->rates[ss->last_chan_band])
  {
    size_t size = ss->supp_rates[ss->last_chan_band][1] + 2;
    wave_memcpy(ie_ptr, ie_ptr_end - ie_ptr, ss->supp_rates[ss->last_chan_band], size);
    ie_ptr += size;
  }

  /* always? */
  wave_memcpy(ie_ptr, ie_ptr_end - ie_ptr, ss->ht_capab[ss->last_chan_band], HT_CAPAB_LEN + 2);
  ie_ptr += HT_CAPAB_LEN + 2;

  if (req->ie_len)
  {
    wave_memcpy(ie_ptr, ie_ptr_end - ie_ptr, req->ie, req->ie_len);
    ie_ptr += req->ie_len;
  }

  len = ie_ptr - ss->probe_req;

  ILOG2_SD("Sending a probe request for SSID '%s' on channel %u", req->ssids[k], ss->last_chan_num);
  mtlk_dump(3, ss->probe_req, len, "dump of probe_req:");
  mtlk_mmb_bss_mgmt_tx(core->vap_handle, ss->probe_req, ie_ptr - ss->probe_req, ss->last_chan_num,
                       FALSE, TRUE, FALSE /* unicast */, &cookie, PROCESS_MANAGEMENT, NULL,
                       FALSE, NTS_TID_USE_DEFAULT);
}

int mtlk_send_chan_data (mtlk_vap_handle_t vap_handle, void *data, int data_len)
{
  mtlk_df_t *df               = mtlk_vap_get_df(vap_handle);
  mtlk_df_user_t *df_user     = mtlk_df_get_user(df);
  struct wireless_dev *wdev   = mtlk_df_user_get_wdev(df_user);
  mtlk_nbuf_t *evt_nbuf;
  uint8 *cp;

  MTLK_ASSERT(NULL != wdev);

  evt_nbuf = wv_cfg80211_vendor_event_alloc(wdev, data_len,
                                            LTQ_NL80211_VENDOR_EVENT_CHAN_DATA);
  if (!evt_nbuf)
  {
    return MTLK_ERR_NO_MEM;
  }

  cp = mtlk_df_nbuf_put(evt_nbuf, data_len);
  wave_memcpy(cp, data_len, data, data_len);


  ILOG3_D("CID-%04x: channel data", mtlk_vap_get_oid(vap_handle));
  mtlk_dump(3, evt_nbuf->data, evt_nbuf->len, "channel data vendor event");
  wv_cfg80211_vendor_event(evt_nbuf);

  return MTLK_ERR_OK;
}

static void
mtlk_core_scan_done (mtlk_core_t *core, struct mtlk_scan_request *request, bool aborted)
{
  struct set_chan_param_data cpd;

  if (mtlk_core_is_in_scan_mode(core)) {
    memset(&cpd, 0, sizeof(cpd));
    cpd.chandef = *__wave_core_chandef_get(core); /* struct copy */
    cpd.switch_type = ST_NORMAL;
    cpd.chandef.width = CW_20;

    /* We can take it from primary channels structure as scan is always done in 20 MHz bandwidth */
    cpd.chandef.center_freq1 = cpd.chandef.chan.center_freq;

    if (MTLK_ERR_OK != core_cfg_set_chan(core, &cpd.chandef, &cpd)) {
      ILOG0_V("Failed to set channel to NORMAL after AP scan");
    }
  }

  wv_cfg80211_scan_done(request, aborted);
}

/* this runs from the Master VAP's serializer because it waits on a lot of things from the FW */
static int scan_timeout_func(mtlk_core_t *core, mtlk_scan_support_t *ss)
{
  mtlk_hw_t *hw;
  struct mtlk_chan_def *ccd;
  struct mtlk_scan_request *req = &ss->req;
  struct channel_scan_support *css = ss->css;
  mtlk_df_t *df = mtlk_vap_get_df(core->vap_handle);
  const char *name = mtlk_df_get_name(df);
  uint32 old_freq1, chan_flags;
  unsigned idx;
  int res = MTLK_ERR_OK;
  unsigned num, band;
  int i;

  if (req->n_channels > ARRAY_SIZE(req->channels)) {
    return MTLK_ERR_VALUE;
  }

  MTLK_UNREFERENCED_PARAM(name); /* only in printouts */

  MTLK_ASSERT(!in_atomic());
  MTLK_ASSERT(core == mtlk_vap_manager_get_master_core(mtlk_vap_get_manager(core->vap_handle)));

  ccd = __wave_core_chandef_get(core);

  ss->flags |= SDF_IGNORE_FRAMES;

  if (!is_scan_running(ss))
  {
    ILOG0_S("%s: Scan no longer running", name);
    return MTLK_ERR_OK;
  }

  if (ss->flags & SDF_ABORT)
  {
    res = MTLK_ERR_CANCELED;
    goto end_scan;
  }

  if (ss->flags & SDF_PAUSE_OR_PREVENT)
  {
    /* Need to ensure that we'll repeat scanning from last_chan_idx again, if it had been started */
    if (ss->last_chan_idx >= 0)
      ss->chan_flags |= SDF_PAUSE_OR_PREVENT;
    /* else do nothing, will start the normal way once PAUSE_OR_PREVENT is cleared */

    return MTLK_ERR_OK;
  }

  ILOG3_SDDD("%s: chan_flags=0x%02x, last_chan_idx=%i, last_chan_num=%i",
             name, ss->chan_flags, ss->last_chan_idx, ss->last_chan_num);

  if (ss->chan_flags & SDF_PAUSE_OR_PREVENT) /* if we're running the first time after a pause */
  {
    ss->chan_flags &= ~SDF_PAUSE_OR_PREVENT;
    i = ss->last_chan_idx; /* note that PAUSE_OR_PREVENT in chan_flags will not have been set if last_chan_idx is still -1 */
    num = ss->last_chan_num;
    band = ss->last_chan_band;
    chan_flags = ss->last_chan_flags;

    /* We must repeat scanning the channel that was attempted before the pause, so just jump right to doing it */
    goto set_chan;
  }

  if (ss->last_chan_idx >= 0 && !(ss->flags & SDF_BG_BREAK)) /* not the initial invocation and not returning from a BG-break */
  {
    idx = channum2cssidx(ss->last_chan_num);

    if (idx == CHAN_IDX_ILLEGAL) {
      WLOG_D("Invalid channel index is got for channel %d", ss->last_chan_num);
      goto continue_chan;
    }

    /* mark last scan times for the just scanned channel */
    if (ss->chan_flags & SDF_ACTIVE)
      css[idx].last_active_scan_time = ss->scan_start_time;

    if (ss->chan_flags & SDF_PASSIVE)
      css[idx].last_passive_scan_time = ss->scan_start_time;

    if (ss->num_surveys < MAX_NUM_SURVEYS)
    {
      struct channel_survey_support *csys = ss->csys + ss->num_surveys;
      struct intel_vendor_channel_data ch_data;
      mtlk_osal_msec_t cur_time = mtlk_osal_timestamp_to_ms(mtlk_osal_timestamp());

      memset(csys, 0, sizeof(*csys));
      csys->channel = req->channels + ss->last_chan_idx;

      memset(&ch_data, 0, sizeof(ch_data));
      ch_data.channel = ss->last_chan_num;
      ch_data.BW = 20;
      ch_data.primary = ss->last_chan_num;
      ch_data.secondary = 0;
      ch_data.freq = channel_to_frequency(ch_data.channel);
      ch_data.total_time = 255;
      ch_data.calibration = css[idx].calib_done_mask;

      /* _mtlk_hw_get_statistics is called directly from scan context of
       * current core  and not from stats timer context of any other core,
       * to avoid both scan(current core) and stats(for any other core)
       * from overwriting the common stats buffer.
       * */

      res = _mtlk_hw_get_statistics(mtlk_vap_get_hw(core->vap_handle));
      if (res != MTLK_ERR_OK) {
        ELOG_V("Failed to retrieve Statistics ");
        return res;
      }

      res = scan_get_aocs_info(core, &ch_data, csys);

      core->acs_updated_ts = cur_time;

      ch_data.filled_mask |= CHDATA_SCAN_MODE | CHDATA_CALIB | CHDATA_TOTAL_TIME;

      if (MTLK_ERR_OK == res) res = mtlk_send_chan_data(core->vap_handle, &ch_data, sizeof(ch_data));

      ss->num_surveys++;
    }
    else
      ELOG_D("%i surveys already collected, cannot collect more", ss->num_surveys);
  }

continue_chan:
  /* now move on to the next channel */
  for (i = ss->last_chan_idx + 1; i < req->n_channels; i++)
  {
    band = req->channels[i].band;
    num = freq2lowchannum(req->channels[i].center_freq, CW_20);
    chan_flags = req->channels[i].flags;
    idx = channum2cssidx(num);

    if (idx == CHAN_IDX_ILLEGAL) {
      WLOG_D("Invalid channel index is got for channel %d", num);
      continue;
    }

    if (mtlk_core_is_band_supported(core, band) != MTLK_ERR_OK)
    {
      ILOG2_SD("%s: Skipping band-not-supported chan %i", name, num);
      continue;
    }

    /* process the reasons to skip this channel */
    if (req->channels[i].flags & MTLK_CHAN_DISABLED)
    {
      ILOG2_SD("%s: Skipping disabled chan %i", name, num);
      continue;
    }

    if (css[idx].calib_failed_mask & (1 << CW_20))
    {
      ILOG2_SD("%s: Skipping calib-failed chan %i", name, num);
      continue;
    }

    ss->chan_flags = ss->flags; /* copy channel flags from the overall scan flags */

    if ((ss->chan_flags & SDF_ACTIVE) /* active asked when active not allowed */
        && (req->channels[i].flags & (MTLK_CHAN_NO_IR | MTLK_CHAN_RADAR)))
    {
      ILOG2_SD("%s: Turning off active scan for no-ir/radar chan %i", name, num);
      ss->chan_flags &= ~SDF_ACTIVE;

      if (!(ss->chan_flags & SDF_PASSIVE)
          && (ss->ScanModifierFlags & SMF_AUTO_PASSIVE_ON_NO_IR))
      {
        ILOG2_SD("%s: Adding passive scan for no-ir chan %i", name, num);
        ss->chan_flags |= SDF_PASSIVE;
      }
    }

    if ((req->type == MTLK_SCAN_AP || req->type == MTLK_SCAN_SCHED_AP) && /* is an AP scan */
        (ss->chan_flags & SDF_PASSIVE) && /* passive scan needed */
        ss->scan_start_time < ss->PassiveValid + css[idx].last_passive_scan_time) /* but still is valid */
    {
      ILOG2_SD("%s: Turning off passive scan on still-valid chan %i", name, num);
      ss->chan_flags &= ~SDF_PASSIVE; /* turn it off */
    }

    if ((req->type == MTLK_SCAN_AP || req->type == MTLK_SCAN_SCHED_AP) && /* is an AP scan */
        (ss->chan_flags & SDF_ACTIVE) && /* active scan needed */
        ss->scan_start_time < ss->ActiveValid + css[idx].last_active_scan_time) /* but still is valid */
    {
      ILOG2_SD("%s: Turning off active scan on still-valid chan %i", name, num);
      ss->chan_flags &= ~SDF_ACTIVE; /* turn it off */
    }

    if (!(ss->chan_flags & (SDF_ACTIVE | SDF_PASSIVE)))
    {
      ILOG2_SD("%s: Skipping no-scans-remaining chan %i", name, num);
      continue;
    }

    break; /* get out and process the channel that has passed all the tests */
  } /* for (i = ss->last_chan_idx + 1; i < req->num_freqs; i++) */

  if (i >= req->n_channels) /* no more channels that need processing in this scan */
    goto end_scan;

set_chan:

  MTLK_ASSERT(i >= 0);

  /* AP background scans need to get some breaks, i.e., need to return back to original channel, or else we might miss too much */
  if (ss->flags & SDF_BACKGROUND)
  {
    if (ss->flags & SDF_BG_BREAK) /* if returning from a background scan break */
    {
      ss->flags &= ~SDF_BG_BREAK;
      ss->chan_in_chunk = 0;
    }

    if (++ss->chan_in_chunk > ss->NumChansInChunk)
    {
      ILOG2_S("%s: taking a break in a BG scan", name);
      ss->flags |= SDF_BG_BREAK;

      /* this skips the HDK config and pays no attention to is_scan_running() */
      if ((res = core_cfg_send_set_chan(core, &ss->orig_chandef, NULL)) != MTLK_ERR_OK)
        goto end_scan;

      mtlk_osal_timer_set(&ss->scan_timer, ss->ChanChunkInterval);
      return MTLK_ERR_OK;
    }
  }

  ILOG2_SDDDD("%s: switching to channel with index %i in the request, number %i, chan_in_chunk %i/%i",
              name, i, num, ss->chan_in_chunk, ss->NumChansInChunk);
  ss->set_chan_req->low_chan_num = num;
  ss->set_chan_req->isRadarDetectionNeeded = (chan_flags & MTLK_CHAN_RADAR) ? 1 : 0;
  ss->set_chan_req->isContinuousInterfererDetectionNeeded = is_interf_det_needed(num);

  /* typically either the chan will change or at least the switch type, so setting the channel is now unconditional */
  /* there is a small chance that we issue set_channel-s with equal params consecutively, but it is legal */

  old_freq1 = ccd->center_freq1;
  ccd->center_freq1 = 0; /* this marks that channel is "uncertain" (in this case changing) */

  wmb(); /* so that channel gets marked uncertain well before we start switching it */

  /* set TX power limits */
  core_cfg_set_tx_power_limit(core, req->channels[i].center_freq, CW_20, req->channels[i].center_freq, req->country_code);

  core->chan_state = ST_LAST;

  res = core_cfg_send_set_chan_by_msg(&ss->set_chan_man_msg);

  /* Send CCA threshold on every set channel */
  if (MTLK_ERR_OK == res) {
    mtlk_core_cfg_send_actual_cca_threshold(core);
  }

  if (res != MTLK_ERR_OK)
  {
    ccd->center_freq1 = old_freq1; /* this makes it "certain" again (assuming it was certain to begin with) */
    goto end_scan;
  }

  core->chan_state = ss->set_chan_req->switch_type;

  ccd->chan = req->channels[i]; /* struct assignment */
  ccd->width = CW_20;
  ccd->center_freq2 = 0;
  __wave_core_chan_switch_type_set(core, ST_SCAN);
  /* don't set the current channel in param db; it will get set when the scan ends: either by us
   * restoring the channel we had before the scan, or by hostapd setting the channel to work on
   */

  wmb(); /* so that the last entry we write is the one that marks the channel "certain" again */
  ccd->center_freq1 = req->channels[i].center_freq;
  ss->flags &= ~SDF_IGNORE_FRAMES; /* from this moment on we're ready to report the frames */

  ss->last_chan_idx = i;
  ss->last_chan_num = num;
  ss->last_chan_band = band;
  ss->last_chan_flags = chan_flags;

  {
    int timeout_val = 0;

    if (ss->chan_flags & SDF_ACTIVE)
    {
      int j, k;

      for (j = ss->NumProbeReqs; j > 0; j--)
      {
        for (k = 0; k < req->n_ssids; k++)
          finalize_and_send_probe_req(ss->requester_core, ss, req, k);

        if (j) /* in all but the last iteration */
          msleep(ss->ProbeReqInterval);
      }

      timeout_val = ss->ActiveScanWaitTime;
    }

    if (ss->chan_flags & SDF_PASSIVE)
    {
      timeout_val = MAX(timeout_val, ss->PassiveScanWaitTime);
    }

    mtlk_osal_timer_set(&ss->scan_timer, timeout_val);
    return MTLK_ERR_OK;
  }

end_scan:
  /* Do not clean up and end scan in case of MAC Fatal pending and fast or full recovery will be started
     Return MTLK_ERR_OK so in case its initial invocation, we'll notify hostapd that scan stared successfully */
  hw = mtlk_vap_get_hw(core->vap_handle);
  if (wave_rcvry_mac_fatal_pending_get(hw) && wave_rcvry_is_configured(hw))
    return MTLK_ERR_OK;

  clean_up_after_this_scan(core, ss);

  mtlk_df_resume_stat_timers(mtlk_df_get_user(df));

  ILOG0_SD("%s: Scan done: res=%i", name, res);
  ILOG2_PPPPP("  ss=%p, master_core=%p, saved_request=%p, wiphy=%p, saved_req_wiphy=%p",
              ss, ss->master_core, ss->req.saved_request, ss->req.wiphy,
              ((struct cfg80211_scan_request *) ss->req.saved_request)->wiphy);
/*  ILOG2_P("wiphy->bands[0]=%p", ((struct wiphy *) ss->req.wiphy)->bands[0]); */

  switch (ss->req.type)
  {
  case MTLK_SCAN_AP:
    mtlk_core_scan_done(core, &ss->req, res != MTLK_ERR_OK);
    break;
  case MTLK_SCAN_SCHED_AP:
    wv_cfg80211_sched_scan_results(&ss->req);
    break;
  case MTLK_SCAN_STA:
    wv_ieee80211_scan_completed(wave_vap_ieee80211_hw_get(core->vap_handle), res != MTLK_ERR_OK);
    break;
  case MTLK_SCAN_SCHED_STA:
    wv_ieee80211_sched_scan_results(wave_vap_ieee80211_hw_get(core->vap_handle));
    break;
  }

  mtlk_osal_event_set(&ss->scan_end_event);
  return res;
}

static const IEEE_ADDR BCAST_MAC_ADDR = { {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF} };

int scan_do_scan(mtlk_handle_t hcore, const void* data, uint32 data_size)
 {
  int res = MTLK_ERR_OK;
  unsigned r_size;
  mtlk_core_t *core = HANDLE_T_PTR(mtlk_core_t, hcore);
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **)data;
  struct mtlk_scan_request *request;

  MTLK_ASSERT(sizeof(mtlk_clpb_t *) == data_size);

  request = mtlk_clpb_enum_get_next(clpb, &r_size);

  MTLK_ASSERT(NULL != request);
  MTLK_ASSERT(sizeof(*request) == r_size);

  res = _scan_do_scan(core, request);

  return mtlk_clpb_push(clpb, &res, sizeof(res));
}

#define MAX_BASIC_RATES_LEN 12
#define BASIC_RATE_BIT 0x80
#define MUL10_TO_MUL2_DIV 5

static size_t wv_fill_supported_rates(mtlk_core_t *core, struct mtlk_scan_request *request, mtlk_hw_band_e band, u32 ratemask, u8 *buf, size_t len)
{
  struct wiphy *wiphy = (struct wiphy *) request->wiphy;
  int nlband = mtlkband2nlband(band);
  uint8 basic_rates[MAX_BASIC_RATES_LEN];
  uint32 size;
  int i;

  MTLK_ASSERT(buf && len && wiphy->bands[nlband]);

  size = wave_core_param_db_basic_rates_get(core, band, basic_rates, sizeof(basic_rates));

  if (len > wiphy->bands[nlband]->n_bitrates)
    len = wiphy->bands[nlband]->n_bitrates;

  for (i = 0; i < len; i++)
  {
    uint32 j;

    if (!(ratemask & (0x01 << i))) /* this rate not needed */
      continue;

    buf[i] = wiphy->bands[nlband]->bitrates[i].bitrate / MUL10_TO_MUL2_DIV;

    for (j = 0; j < size; j++)
      if (buf[i] == basic_rates[j]) /* found this rate among the basic rates */
      {
        buf[i] |= BASIC_RATE_BIT;
        break;
      }
  }

  return i;
}

/* supposed to be run on the Master VAP's serializer */
int _scan_do_scan(mtlk_core_t *core, struct mtlk_scan_request *request)
{
  mtlk_hw_t *hw = mtlk_vap_get_hw(core->vap_handle);
  mtlk_df_t *df = mtlk_vap_get_df(core->vap_handle);
  const char *name = mtlk_df_get_name(df);
  mtlk_vap_manager_t *vap_mgr = mtlk_vap_get_manager(core->vap_handle);
  int active_vaps = mtlk_vap_manager_get_active_vaps_number(vap_mgr);
  int core_state, radio_mode;
  struct scan_support *ss = __wave_core_scan_support_get(core);
  wave_radio_t *radio = wave_vap_radio_get(core->vap_handle);
  int res = MTLK_ERR_OK;
  wv_mac80211_t *mac80211 = wave_radio_mac80211_get(radio);
  BOOL has_peer_connections = mtlk_vap_manager_has_peer_connections(vap_mgr);
  BOOL has_stas_connected = wv_mac80211_has_sta_connections(mac80211);
  BOOL do_background_scan = FALSE;
  int i;

  MTLK_UNREFERENCED_PARAM(name); /* printouts only */

  MTLK_ASSERT(core == mtlk_vap_manager_get_master_core(mtlk_vap_get_manager(core->vap_handle)));
  MTLK_ASSERT(NULL != request);

  if (mtlk_core_is_stopping(core)) /* not a good time for a new scan */
  {
    ELOG_S("%s: Core is stopping, cannot start the scan", name);
    res = MTLK_ERR_NOT_READY;
    goto no_clean_exit;
  }

  /* if some scan is already going on, cancel this one, except if restarted from fast or full recovery */
  if (is_scan_running(ss) && !wave_rcvry_restart_scan_get(hw, vap_mgr))
  {
    ELOG_S("%s: A scan is already running, cannot start another", name);
    res = MTLK_ERR_BUSY;
    goto no_clean_exit;
  }

  /* if mac scan is already going on, cancel this one */
  if (is_mac_scan_running(ss)) {
    ELOG_S("%s: A mac scan is already running, cannot start another", name);
    res = MTLK_ERR_BUSY;
    goto no_clean_exit;
  }

  /* delay scan, waiting for beacons */
  if (mtlk_core_is_block_tx_mode(mtlk_vap_manager_get_master_core(vap_mgr))) {
    res = MTLK_ERR_NOT_READY;
    goto no_clean_exit;
  }

  /* This will be true although the above checks
   * are false for radar detection, so delay new scans */
  if (mtlk_core_is_in_scan_mode(mtlk_vap_manager_get_master_core(vap_mgr))) {
    res = MTLK_ERR_RETRY;
    goto no_clean_exit;
  }

  if (ss->flags & SDF_PAUSE_OR_PREVENT)
  {
    ELOG_S("%s: A new scan temporarily disallowed", name);
    res = MTLK_ERR_BUSY;
    goto no_clean_exit;
  }

  /* Save the info to what channel (and how wide) we have to return to after the scan */
  ss->orig_chandef = * __wave_core_chandef_get(core);
  ss->req = *request; /* save the request */

  ILOG0_SD("%s: Beginning scan, requester_vap_index=%i", name, ss->req.requester_vap_index);

  if (request->type == MTLK_SCAN_STA || request->type == MTLK_SCAN_SCHED_STA)
  {
    if (has_peer_connections)
    {
      ILOG0_V("Scan by sta - do drv bg scan");

      if (!is_channel_certain(&ss->orig_chandef))
      {
        ELOG_S("%s: Should do background scan but channel isn't certain", name);
        res = MTLK_ERR_SCAN_FAILED;
        goto no_clean_exit;
      }

      do_background_scan = TRUE;
    }
  }

  ss->flags |= (SDF_RUNNING | SDF_IGNORE_FRAMES); /* prevent heard frames from being reported yet */

  if (!request->n_channels)
  {
    ELOG_S("%s: This is an empty scan", name);
    res = MTLK_ERR_OK;
    goto error_exit;
  }

  if (ss->flags & SDF_ABORT) /* we'll check this before all lengthy operations */
  {
    ELOG_S("%s: Scan has been canceled", name);
    res = MTLK_ERR_CANCELED;
    goto error_exit;
  }

  /* may not scan unless there has been a Master VAP activated */
  if (core->is_stopped && ((res = mtlk_mbss_send_vap_activate(core, request->channels[0].band)) != MTLK_ERR_OK))
  {
    ELOG_SD("%s: Could not activate the master VAP, err=%i", name, res);
    goto error_exit;
  }

  radio_mode = WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_MODE_CURRENT);
  ILOG0_D("radio mode %d", radio_mode);
  res = mtlk_core_radio_enable_if_needed(core);
  if (MTLK_ERR_OK != res) {
    goto error_exit;
  }

  /* get calibration status from radio module */
  for (i = 0; i < 1 + NUM_TOTAL_CHANS; i++) {
    wave_radio_calibration_status_get(radio, i, &ss->css[i].calib_done_mask, &ss->css[i].calib_failed_mask);
  }

  ILOG1_PPPPPD("  ss=%p, master_core=%p, saved_request=%p, wiphy=%p, saved_req_wiphy=%p, type=%i",
               ss, ss->master_core, ss->req.saved_request, ss->req.wiphy,
               ((struct cfg80211_scan_request *) ss->req.saved_request)->wiphy, ss->req.type);

  core_state = mtlk_core_get_net_state(core);
  ss->ScanModifierFlags = WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_SCAN_MODIFS);

  /* Decide whether to do background or foreground for AP scan request */
  if (request->type == MTLK_SCAN_AP || request->type == MTLK_SCAN_SCHED_AP)
  {
    if (is_channel_certain(&ss->orig_chandef)) /* mandatory condition for a background scan---initial set-channel has been done */
    {
      do_background_scan = ((request->flags & MTLK_SCAN_FLAG_LOW_PRIORITY) /* if it's a background scan for real */
                  || ((active_vaps > 1 || (active_vaps == 1 && core_state == NET_STATE_CONNECTED)) /* or it's a case when we can't do regular */
                      && (ss->ScanModifierFlags & SMF_AUTO_ENABLE_BG))
                  || has_stas_connected || has_peer_connections);
    }

    ILOG0_DDD("Scan by ap - orig_certain=%i, has_stas_connected=%i, do_background_scan=%i", is_channel_certain(&ss->orig_chandef), has_stas_connected, do_background_scan);
  }

  if (do_background_scan && !core->vap_in_fw_is_active)
  {
    ELOG_S("%s: VAP activation process is ongoing, cannot start BG-scan", name);
    ss->flags &= ~(SDF_RUNNING | SDF_IGNORE_FRAMES);
    res = MTLK_ERR_NOT_READY;
    goto no_clean_exit;
  }

  ILOG1_SDDDDSDDDDD("%s: Starting scan preparation, modifier_flags=0x%02x, flags=0x%02x, "
      "active_vaps=%i, has_connections=%i, core_state=%s, orig_freq1=%u, request_flags=0x%02x, "
      "low_priority_flag=%i, rates2.4=%i, rates5=%i", name, ss->ScanModifierFlags, ss->flags,
      active_vaps, has_peer_connections, mtlk_net_state_to_string(core_state),
      ss->orig_chandef.center_freq1, request->flags, (request->flags & MTLK_SCAN_FLAG_LOW_PRIORITY),
      request->rates[UMI_PHY_TYPE_BAND_2_4_GHZ], request->rates[UMI_PHY_TYPE_BAND_5_2_GHZ]);

  if (do_background_scan)
  {
    mtlk_pdb_size_t size = sizeof(ss->iwpriv_scan_params_bg);
    WAVE_RADIO_PDB_GET_BINARY(radio, PARAM_DB_RADIO_SCAN_PARAMS_BG,
                        &ss->iwpriv_scan_params_bg, &size);
    ss->flags |= SDF_BACKGROUND;
    ss->PassiveScanWaitTime = ss->iwpriv_scan_params_bg.passive_scan_time;
    ss->ActiveScanWaitTime = ss->iwpriv_scan_params_bg.active_scan_time;
    ss->NumProbeReqs = ss->iwpriv_scan_params_bg.num_probe_reqs;
    ss->ProbeReqInterval = ss->iwpriv_scan_params_bg.probe_req_interval;
    ss->NumChansInChunk = ss->iwpriv_scan_params_bg.num_chans_in_chunk;
    ss->ChanChunkInterval = ss->iwpriv_scan_params_bg.chan_chunk_interval;
  }
  else /* not a background scan */
  {
    mtlk_pdb_size_t size = sizeof(ss->iwpriv_scan_params);
    WAVE_RADIO_PDB_GET_BINARY(radio, PARAM_DB_RADIO_SCAN_PARAMS,
                        &ss->iwpriv_scan_params, &size);

    ss->PassiveValid = msecs_to_jiffies(1000 * ss->iwpriv_scan_params.passive_scan_valid_time);
    ss->ActiveValid = msecs_to_jiffies(1000 * ss->iwpriv_scan_params.active_scan_valid_time);
    ss->PassiveScanWaitTime = ss->iwpriv_scan_params.passive_scan_time;
    ss->ActiveScanWaitTime = ss->iwpriv_scan_params.active_scan_time;
    ss->NumProbeReqs = ss->iwpriv_scan_params.num_probe_reqs;
    ss->ProbeReqInterval = ss->iwpriv_scan_params.probe_req_interval;
  }

  if (ss->flags & SDF_ABORT) /* check this before we set the flags marking scan as running */
  {
    ELOG_S("%s: Scan has been canceled", name);
    res = MTLK_ERR_CANCELED;
    goto error_exit;
  }

  /* apply scan type modifications */
  if (request->n_ssids <= 0) /* passive */
  {
    ss->probe_req = NULL; /* it should already be NULL, BTW */
    ss->probe_req_len = 0;
    ss->flags |= SDF_PASSIVE;

    if (ss->ScanModifierFlags & SMF_ADD_ACTIVE)
      ss->flags |= SDF_ACTIVE;

    if (ss->ScanModifierFlags & SMF_REMOVE_PASSIVE)
      ss->flags &= ~SDF_PASSIVE;
  }
  else /* active */
  {
    ss->flags |= SDF_ACTIVE;

    if (ss->ScanModifierFlags & SMF_ADD_PASSIVE)
      ss->flags |= SDF_PASSIVE;

    if (ss->ScanModifierFlags & SMF_REMOVE_ACTIVE)
      ss->flags &= ~SDF_ACTIVE;
  }

  /* prepare the set_chan msg as much as possible */
  {
    mtlk_txmm_data_t *man_entry;

    /* prepare msg for the FW */
    if (!(man_entry = mtlk_txmm_msg_init_with_empty_data(&ss->set_chan_man_msg,
                                                         mtlk_vap_get_txmm(core->vap_handle), &res)))
    {
      ELOG_DD("CID-%04x: UM_MAN_SET_CHAN_REQ init for scan failed, err=%i",
              mtlk_vap_get_oid(core->vap_handle), res);
      res = MTLK_ERR_NO_RESOURCES;
      goto nomem_exit;
    }

    man_entry->id = UM_SET_CHAN_REQ;
    man_entry->payload_size = sizeof(*ss->set_chan_req);
    ss->set_chan_req = (UM_SET_CHAN_PARAMS *) man_entry->payload;
    memset(ss->set_chan_req, 0, sizeof(*ss->set_chan_req));
    ss->set_chan_req->RegulationType = REGD_CODE_UNKNOWN;
    ss->set_chan_req->chan_width = CW_20;
    ss->set_chan_req->switch_type = ST_SCAN;
    ss->set_chan_req->bg_scan = ss->flags & SDF_BACKGROUND;
  }

  /* init the probe req frame if in the end the scan has become active */
  if (ss->flags & SDF_ACTIVE)
  {
    uint8 sa_addr[ETH_ALEN];
    mtlk_vap_handle_t requester_vap_handle;
    size_t probe_req_max_len = sizeof(struct ieee80211_mgmt)
      + MAX_SSID_LEN + 2 + MAX_SUPPORTED_RATES + 2 + HT_CAPAB_LEN + 2
      + request->ie_len;
    struct ieee80211_mgmt *header = NULL;

    res = mtlk_vap_manager_get_vap_handle(vap_mgr, request->requester_vap_index, &requester_vap_handle);
    if (MTLK_ERR_OK != res) {
      ELOG_S("%s: failed to get requester vap handle", name);
      res = MTLK_ERR_UNKNOWN;
      goto error_exit;
    }

    header = mtlk_osal_mem_alloc(probe_req_max_len, MTLK_MEM_TAG_SCAN_DATA);
    if (!header)
      goto nomem_exit;

    ss->requester_core = mtlk_vap_get_core(requester_vap_handle);
    wave_pdb_get_mac(mtlk_vap_get_param_db(requester_vap_handle), PARAM_DB_CORE_MAC_ADDR, sa_addr);

    ss->probe_req = (uint8 *) header;
    ss->probe_req_len = probe_req_max_len;
    memset(ss->probe_req, 0, probe_req_max_len);

    header->frame_control = cpu_to_le16(IEEE80211_FTYPE_MGMT |IEEE80211_STYPE_PROBE_REQ);
    /* Leave duration and seq_control alone---I don't know what should go in there */
    mtlk_osal_copy_eth_addresses(header->sa, sa_addr);
    mtlk_osal_copy_eth_addresses(header->da, BCAST_MAC_ADDR.au8Addr);
    mtlk_osal_copy_eth_addresses(header->bssid, BCAST_MAC_ADDR.au8Addr);

    if (mtlk_core_is_band_supported(core, UMI_PHY_TYPE_BAND_2_4_GHZ) == MTLK_ERR_OK)
    {
      /* What if hostapd was configured to not work in HT, do we still want this element? how do we know? */
      ss->ht_capab[UMI_PHY_TYPE_BAND_2_4_GHZ][0] = HT_CAPABILITIES_IE_TAG;
      ss->ht_capab[UMI_PHY_TYPE_BAND_2_4_GHZ][1] = HT_CAPAB_LEN;
      wv_fill_ht_capab(request, UMI_PHY_TYPE_BAND_2_4_GHZ,
                       ss->ht_capab[UMI_PHY_TYPE_BAND_2_4_GHZ] + 2,
                       sizeof(ss->ht_capab[UMI_PHY_TYPE_BAND_2_4_GHZ]) - 2);

      /* In theory, if there are a lot of bits in the rates, we may also need the extended supported rates */
      /* In practice there are just 8 for 2.4 GHz and nothing for 5 GHz. No relation to what's actually in hostapd.conf */
      ss->supp_rates[UMI_PHY_TYPE_BAND_2_4_GHZ][0] = SUPPORTED_RATES_IE_TAG;
      ss->supp_rates[UMI_PHY_TYPE_BAND_2_4_GHZ][1] = wv_fill_supported_rates(core, request, UMI_PHY_TYPE_BAND_2_4_GHZ,
                                                                             request->rates[UMI_PHY_TYPE_BAND_2_4_GHZ],
                                                                             ss->supp_rates[UMI_PHY_TYPE_BAND_2_4_GHZ] + 2,
                                                                             sizeof(ss->supp_rates[UMI_PHY_TYPE_BAND_2_4_GHZ]) - 2);
    }

    if (mtlk_core_is_band_supported(core, UMI_PHY_TYPE_BAND_5_2_GHZ) == MTLK_ERR_OK)
    {
      ss->ht_capab[UMI_PHY_TYPE_BAND_5_2_GHZ][0] = HT_CAPABILITIES_IE_TAG;
      ss->ht_capab[UMI_PHY_TYPE_BAND_5_2_GHZ][1] = HT_CAPAB_LEN;
      wv_fill_ht_capab(request, UMI_PHY_TYPE_BAND_5_2_GHZ,
                       ss->ht_capab[UMI_PHY_TYPE_BAND_5_2_GHZ] + 2,
                       sizeof(ss->ht_capab[UMI_PHY_TYPE_BAND_5_2_GHZ]) - 2);

      ss->supp_rates[UMI_PHY_TYPE_BAND_5_2_GHZ][0] = SUPPORTED_RATES_IE_TAG;
      ss->supp_rates[UMI_PHY_TYPE_BAND_5_2_GHZ][1] = wv_fill_supported_rates(core, request, UMI_PHY_TYPE_BAND_5_2_GHZ,
                                                                             request->rates[UMI_PHY_TYPE_BAND_5_2_GHZ],
                                                                             ss->supp_rates[UMI_PHY_TYPE_BAND_5_2_GHZ] + 2,
                                                                             sizeof(ss->supp_rates[UMI_PHY_TYPE_BAND_5_2_GHZ]) - 2);
    }

    ILOG1_SY("%s: scan preparation - is active scan, sa=%Y", name, header->sa);
  }

  ss->last_chan_idx = -1; /* make sure timeout_func knows it's the very start of the scan */
  ss->num_surveys = 0;

  ss->param_algomask = WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_CALIBRATION_ALGO_MASK);
  ss->param_oalgomask = WAVE_RADIO_PDB_GET_INT(radio, PARAM_DB_RADIO_ONLINE_ACM);

  ss->scan_start_time = get_jiffies_64(); /* 32-bit jiffies wrap and that can mess us up */

  if (ss->flags & SDF_ABORT) /* one last check because it's good to avoid an unneeded scan */
  {
    ELOG_S("%s: Scan has been canceled", name);
    res = MTLK_ERR_CANCELED;
    goto error_exit;
  }

  mtlk_osal_event_reset(&ss->scan_end_event);

  ILOG1_SDDD("%s: Scan preparation complete: channels=%u, modifier_flags=0x%02x, flags=0x%02x",
             name, request->n_channels, ss->ScanModifierFlags, ss->flags);

  mtlk_df_stop_stat_timers(mtlk_df_get_user(df));

  /* Now just invoke the regular scan timeout handling func, which will get the real work done */
  return scan_timeout_func(core, ss);

nomem_exit:
  res = MTLK_ERR_NO_MEM;

error_exit:
  if (!wave_rcvry_mac_fatal_pending_get(hw) || !wave_rcvry_is_configured(hw))
    clean_up_after_this_scan(core, ss);

no_clean_exit:
  if (!wave_rcvry_mac_fatal_pending_get(hw) || !wave_rcvry_is_configured(hw)) {
    if (res != MTLK_ERR_OK && res != MTLK_ERR_NOT_READY)
      ELOG_SDP("%s: Scan failed: res=%i, ss=%p", name, res, ss);
  } else {
    wave_rcvry_restart_scan_set(hw, vap_mgr, TRUE);
    ILOG1_V("Scan will be started again during fast or full recovery");
    res = MTLK_ERR_OK; /* Tell hostapd that scan has successfully started. Fast recovery will continue the scan */
  }
  mtlk_osal_event_set(&ss->scan_end_event);
  return res;
}


/* gets called from the user-context invoking sched scan, e.g., wpa_supplicant or iwlist */
int scan_start_sched_scan(mtlk_handle_t hcore, const void* data, uint32 data_size)
{
  mtlk_core_t *core = HANDLE_T_PTR(mtlk_core_t, hcore);
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;
  struct mtlk_scan_request **request;
  unsigned r_size;
  int res = MTLK_ERR_OK;

  MTLK_ASSERT(sizeof(mtlk_clpb_t *) == data_size);

  request = mtlk_clpb_enum_get_next(clpb, &r_size);
  MTLK_CLPB_TRY(request, r_size)
    if (core->slow_ctx->sched_scan_req)
    {
      ELOG_V("A scheduled scan has already been set up here");
      MTLK_CLPB_EXIT(MTLK_ERR_BUSY);
    }
    else
    {
      core->slow_ctx->sched_scan_req = *request;
      /* now just do the same as we would on every sched scan timer alarm */
      sched_scan_timer_clb_func(&core->slow_ctx->sched_scan_timer, HANDLE_T(core));
    }
  MTLK_CLPB_FINALLY(res)
    return mtlk_clpb_push_res(clpb, res);
  MTLK_CLPB_END
}


/* gets called from the user-context cancelling the sched scan, e.g., wpa_supplicant or something */
int scan_stop_sched_scan(mtlk_handle_t hcore, const void* data, uint32 data_size)
{
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;
  mtlk_core_t *core = HANDLE_T_PTR(mtlk_core_t, hcore);
  int res = MTLK_ERR_OK;

  MTLK_ASSERT(sizeof(mtlk_clpb_t *) == data_size);

  if (!core->slow_ctx->sched_scan_req)
  {
    ELOG_V("A scheduled scan is not set up");
    res = MTLK_ERR_PARAMS;
  }
  else
  {
    struct mtlk_scan_request *sr =  core->slow_ctx->sched_scan_req;
    core->slow_ctx->sched_scan_req = NULL;
    mtlk_osal_timer_cancel_sync(&core->slow_ctx->sched_scan_timer);
    mtlk_osal_mem_free(sr); /* it's OK even if scan is still running; scan support has a full local copy of the request */
  }

  return mtlk_clpb_push(clpb, &res, sizeof(res));
}


int scan_dump_survey(mtlk_handle_t hcore, const void* data, uint32 data_size)
{
  mtlk_core_t *core = HANDLE_T_PTR(mtlk_core_t, hcore);
  mtlk_clpb_t *clpb = *(mtlk_clpb_t **) data;
  struct scan_support *ss = __wave_core_scan_support_get(core);
  struct mtlk_chan_def *chandef_in_use = is_scan_running(ss) ? &ss->orig_chandef : __wave_core_chandef_get(core);

  int *idx;
  unsigned idx_size;
  struct mtlk_channel_status cs;
  int res = MTLK_ERR_OK;

  MTLK_ASSERT(core == mtlk_vap_manager_get_master_core(mtlk_vap_get_manager(core->vap_handle)));
  MTLK_ASSERT(sizeof(mtlk_clpb_t *) == data_size);

  ILOG3_V("Running");

  idx = mtlk_clpb_enum_get_next(clpb, &idx_size);

  MTLK_ASSERT(NULL != idx);
  if (!mtlk_clpb_check_data(idx, idx_size, sizeof(*idx), __FUNCTION__, __LINE__ )) {
    return MTLK_ERR_UNKNOWN;
  }

  memset(&cs, 0, sizeof(cs));

  if (*idx >= ss->num_surveys)
    cs.channel = NULL;
  else
  {
    cs.channel = ss->csys[*idx].channel;
    cs.current_primary_chan_freq = chandef_in_use->chan.center_freq;
    cs.load = ss->csys[*idx].load;
    cs.noise = ss->csys[*idx].noise;
  }

  res = mtlk_clpb_push(clpb, &cs, sizeof(cs));

  return res;
}

/* returns non-zero when the given SSID at the given RSSI passes the filters possibly set for this scan */
BOOL passes_scan_filters(const struct scan_support *ss, const char *ssid, s32 rssi)
{
  if (ss->req.n_match_sets <= 0) /* if we have no filters then we pass */
    return TRUE;

  if (rssi >= ss->req.min_rssi_thold) /* the signal is strong enough for some filter entry */
  {
    int i;

    for (i = 0; i < ss->req.n_match_sets; i++)
    {
      const struct mtlk_match_set *ms = ss->req.match_sets + i;

      if (rssi < ms->rssi_thold) /* not strong enough for this SSID */
        continue;

      if (ms->ssid_len == 0 /* no/empty SSID */
          || (!memcmp(ssid, ms->ssid, ms->ssid_len)
              && ssid[ms->ssid_len] == '\0')) /* match found */
      {
        return TRUE;
      }
    }
  }

  return FALSE;
}

int mtlk_scan_recovery_get_channel(mtlk_core_t *core,
        struct mtlk_chan_def *chandef)
{
  struct scan_support *ss = __wave_core_scan_support_get(core);
  struct mtlk_chan_def *chandef_in_use = is_scan_running(ss) ? &ss->orig_chandef : __wave_core_chandef_get(core);

  if (chandef == NULL) {
    return MTLK_ERR_PARAMS;
  }
  *chandef = *chandef_in_use;
  return MTLK_ERR_OK;
}

int mtlk_scan_recovery_continue_scan (mtlk_core_t *core, BOOL is_bg_scan)
{
  int res;
  mtlk_txmm_data_t *man_entry;
  struct scan_support *ss = __wave_core_scan_support_get(core);
  mtlk_hw_t *hw = mtlk_vap_get_hw(core->vap_handle);
  mtlk_vap_manager_t *vap_manager = mtlk_vap_get_manager(core->vap_handle);

  ILOG0_D("CID-%04x: Resuming background scan", mtlk_vap_get_oid(core->vap_handle));

  if (wave_rcvry_restart_scan_get(hw, vap_manager) && !is_bg_scan) {
    if (!(ss->flags & SDF_PAUSE_OR_PREVENT)) {
      ELOG_V("The scan is not paused/prevented");
      return MTLK_ERR_BUSY;
    }

    ss->flags &= ~SDF_PAUSE_OR_PREVENT;

    if (!is_scan_running(ss)) {
      ILOG0_V("No scan is running");
      return MTLK_ERR_OK;
    }

    res = _scan_do_scan(core, &ss->req);
    wave_rcvry_restart_scan_set(hw, vap_manager, FALSE);
    return res;
  }

  /* prepare msg for the FW which was cleaned up due to FW crash */
  if (!(man_entry = mtlk_txmm_msg_init_with_empty_data(&ss->set_chan_man_msg,
      mtlk_vap_get_txmm(core->vap_handle), &res))) {
    ELOG_DD("CID-%04x: UM_MAN_SET_CHAN_REQ init for scan failed, err=%i",
    mtlk_vap_get_oid(core->vap_handle), res);
    return MTLK_ERR_NO_RESOURCES;
  }

  man_entry->id = UM_SET_CHAN_REQ;
  man_entry->payload_size = sizeof(*ss->set_chan_req);
  ss->set_chan_req = (UM_SET_CHAN_PARAMS *)man_entry->payload;
  memset(ss->set_chan_req, 0, sizeof(*ss->set_chan_req));
  ss->set_chan_req->RegulationType = REGD_CODE_UNKNOWN;
  ss->set_chan_req->chan_width = CW_20;
  ss->set_chan_req->switch_type = ST_SCAN;

  if (is_bg_scan)
    ss->set_chan_req->bg_scan |= SDF_BACKGROUND;

  return resume_or_allow_scan(core);
}
