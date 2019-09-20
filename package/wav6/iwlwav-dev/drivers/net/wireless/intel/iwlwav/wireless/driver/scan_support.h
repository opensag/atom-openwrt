/******************************************************************************

                               Copyright (c) 2014
                            Lantiq Deutschland GmbH

  For licensing information, see the file 'LICENSE' in the root folder of
  this software module.

******************************************************************************/
/*
 * $Id$
 *
 *
 *
 * Scan support header
 *
 */
#ifndef __SCAN_SUPPORT_H__
#define __SCAN_SUPPORT_H__

#include "mtlk_coreui.h"
#include "mhi_umi_propr.h"
#include "progmodel.h"

#define LOG_LOCAL_GID GID_SCAN_SUPPORT
#define LOG_LOCAL_FID 1

/* mimic the linux enum nl80211_scan_flags */
enum mtlk_scan_flags {
  MTLK_SCAN_FLAG_LOW_PRIORITY = 1<<0,
  MTLK_SCAN_FLAG_FLUSH = 1<<1,
  MTLK_SCAN_FLAG_AP = 1<<2,
};

/* mimic the linux enum ieee80211_channel_flags with our own */
enum mtlk_channel_flags
{
  MTLK_CHAN_DISABLED = 1<<0,
  MTLK_CHAN_NO_IR = 1<<1,
  MTLK_CHAN_RADAR = 1<<3,
  MTLK_CHAN_NO_HT40PLUS = 1<<4,
  MTLK_CHAN_NO_HT40MINUS = 1<<5,
  MTLK_CHAN_NO_OFDM = 1<<6,
  MTLK_CHAN_NO_80MHZ = 1<<7,
  MTLK_CHAN_NO_160MHZ = 1<<8,
  MTLK_CHAN_INDOOR_ONLY = 1<<9,
  MTLK_CHAN_GO_CONCURRENT = 1<<10,
  MTLK_CHAN_NO_20MHZ = 1<<11,
  MTLK_CHAN_NO_10MHZ = 1<<12,
};

/* mimic enum nl80211_dfs_state */
enum mtlk_dfs_state
{
  MTLK_DFS_USABLE,
  MTLK_DFS_UNAVAILABLE,
  MTLK_DFS_AVAILABLE,
};

/* These are values for parameter "is_bg_scan" of
   the function "mtlk_scan_recovery_continue_scan" */
enum {
  MTLK_SCAN_SUPPORT_NO_BG_SCAN,
  MTLK_SCAN_SUPPORT_BG_SCAN
};

#define MHZS_PER_20MHZ 20     /* I can't believe it's 20---it's a miracle! */
#define CHANNUMS_PER_20MHZ 4
#define CHANS_IN_80MHZ 4
#define CHANS_IN_160MHZ 8

#define MAX_NUM_SURVEYS NUM_TOTAL_CHANS

#define CHAN_IDX_ILLEGAL 0

/* Conversion from 2.4 and 5 GHz channel index in our array of channel scan support data to channel number */
extern const uint8 chanidx2num_2_5[];
/* Conversion from 5 GHz channel number to its index in our array of extra_chan_data */
extern const uint8 channum2idx_2_5[256]; /* FIXME: size should be taken from scan_support.c */

/* Conversion from our channel-scan-support index to channel number */
static __INLINE
unsigned cssidx2channum(unsigned idx)
{
  MTLK_ASSERT(idx <= NUM_TOTAL_CHANS);
  return chanidx2num_2_5[idx];
}

/* Conversion from channel number to our channel-scan-support index */
static __INLINE
unsigned channum2cssidx(int num)
{
  unsigned idx;

  if ((num < 0) || (num >= ARRAY_SIZE(channum2idx_2_5)))
    return CHAN_IDX_ILLEGAL;

  idx = channum2idx_2_5[(u8) num];
  /* Note: size of array css of struct channel_scan_support must fit to idx value */
  return (idx > NUM_TOTAL_CHANS) ? CHAN_IDX_ILLEGAL : idx;
}

static __INLINE
unsigned channum2csysidx(int num)
{
  unsigned idx;

  if ((num < 0) || (num >= ARRAY_SIZE(channum2idx_2_5)))
    return CHAN_IDX_ILLEGAL;

  idx = channum2idx_2_5[(u8) num];
  return (idx >= MAX_NUM_SURVEYS) ? CHAN_IDX_ILLEGAL : idx;
}

static __INLINE
uint8 is_interf_det_needed(unsigned channel)
{
  MTLK_UNREFERENCED_PARAM(channel);

  /* enable on all bands and channels */
  return 1;
}

static __INLINE
uint8 is_interf_det_switch_needed(unsigned channel)
{
  return ((1 == channel) || (6 == channel) || (11 == channel));
}


enum scanDescriptionFlags
{
  SDF_RUNNING = 0x01,
  SDF_IGNORE_FRAMES = 0x02,
  SDF_PASSIVE = 0x04,
  SDF_ACTIVE = 0x08,
  SDF_SCHEDULED = 0x10,
  SDF_BACKGROUND = 0x20,
  SDF_BG_BREAK = 0x40,
  SDF_ABORT = 0x80,
  SDF_PAUSE_OR_PREVENT = 0x100,
  SDF_STAY_ON_CHAN = 0x200,
  SDF_MAC_RUNNING = 0x400,
};

/* The modification flags/modes apply to all VAPs equally.
 * However, depending on each VAP's type (AP or STA) it will use only the applicable modifications
 */
enum ScanModifFlags
{
  SMF_NONE = 0,
  // Passive scan modifications, i.e., typically for APs:
  SMF_ADD_ACTIVE = 1, // add active scanning to passive scanning
  SMF_REMOVE_PASSIVE = 2, // it's a flag, but not to be used without the previous flag
  SMF_ACTIVE_INSTEAD_OF_PASSIVE = 3, // both of the previous 2 flags simultaneously
  // Active scan modifications, i.e., typically for stations:
  SMF_ADD_PASSIVE = 4, // add passive scanning to active scanning
  SMF_REMOVE_ACTIVE = 8, // it's a flag, but not to be used without the previous flag
  SMF_PASSIVE_INSTEAD_OF_ACTIVE = 0x0c, // both of the previous 2 flags simultaneously
  SMF_AUTO_ENABLE_BG = 0x10, // switching to background mode may allow channel switching when VAPs are up
  SMF_AUTO_PASSIVE_ON_NO_IR = 0x20, // switch NO_IR chans to passive mode automatically
  // and for completeness, not a mode or a flag, just a terminator
  SMF_NUM_SMF = 0x40 // upper bound on the more-or-less sensible modification modes
};

struct channel_scan_support
{
  u64 last_passive_scan_time; /* in 64-bit jiffies */
  u64 last_active_scan_time;  /* in 64-bit jiffies */
  u8 calib_done_mask;
  u8 calib_failed_mask;
};

struct channel_survey_support
{
  struct mtlk_channel *channel;
  s8 noise; /* as reported by GET_CHAN_STATUS */
  u8 load;  /* as reported by GET_CHAN_STATUS */
};

#define CHDATA_NOISE_FLOOR            1
#define CHDATA_BUSY_TIME              2
#define CHDATA_TOTAL_TIME             4
#define CHDATA_CALIB                  8
#define CHDATA_NUM_BSS           0x0010
#define CHDATA_DYNBW             0x0020
#define CHDATA_RSSI              0x0040
#define CHDATA_SNR               0x0080
#define CHDATA_CWI_NOISE         0x0100
#ifdef MTLK_LEGACY_STATISTICS
#define CHDATA_NOT_80211_EVT     0x0200
#endif
#define CHDATA_LOW_RSSI          0x0400
#define CHDATA_TX_POWER          0x0800
#define CHDATA_LOAD              0x1000
#define CHDATA_SCAN_MODE       0x800000

struct dfs_debug_params
{
  int cac_time;
  int beacon_count;
  uint32 nop; /* Non Occupancy Period */
  BOOL switch_in_progress;
  u8 debug_chan;
};

#define MAX_SUPPORTED_RATES 8
#define HT_CAPAB_LEN 26
#define MAX_SSID_LEN (MTLK_ESSID_MAX_SIZE - 1)
#define SSID_IE_TAG 0
#define SUPPORTED_RATES_IE_TAG 1
#define HT_CAPABILITIES_IE_TAG 45

typedef struct scan_support
{
  mtlk_core_t *master_core; /* this allows us to easily refer to related stuff, such as the current_chandef */
  mtlk_core_t *requester_core;
  uint32 flags;        /* flags from scanDescriptionFlags */
  struct mtlk_chan_def orig_chandef; /* what to return to after scan, if any */
  u8 max_widths[NUM_SUPPORTED_BANDS];
  struct mtlk_scan_request req; /* sched scan stuff can get saved here, too */
  mtlk_osal_event_t scan_end_event;
  mtlk_osal_timer_t scan_timer;
  mtlk_txmm_msg_t set_chan_man_msg;
  UM_SET_CHAN_PARAMS *set_chan_req; /* points inside the msg above */
  uint32 ScanModifierFlags;
  struct iwpriv_scan_params iwpriv_scan_params;
  struct iwpriv_scan_params_bg iwpriv_scan_params_bg;
  struct dfs_debug_params dfs_debug_params; /*DFS debug params for radar simulation*/
  u32 PassiveValid; /* in jiffies */
  u32 ActiveValid;  /* in jiffies */
  u16 PassiveScanWaitTime; /* in ms, copied PassiveScanTime from either fg or bg iwpriv_scan_params */
  u16 ActiveScanWaitTime;  /* in ms, ditto for copied ActiveScanTime */
  u16 NumProbeReqs;      /* number of probe requests to send for the same SSID */
  u16 ProbeReqInterval;  /* time in ms, after which to fire the next round of probe reqs for the same SSIDs */
  u16 NumChansInChunk;   /* how many channels to probe in one "chunk" (deviation from the real channel in use) */
  u16 ChanChunkInterval; /* how often (in ms) may we check the next chunk of channels in BG scan */
  u32 param_algomask;
  u32 param_oalgomask;
  u64 scan_start_time;
  /* stuff to deal with chunks in BG scans and repeating probes in active scans */
  int chan_in_chunk;
  int last_probe_num;
  /* extra channel data */
  u32 chan_flags;
  int last_chan_idx; /* index in req */
  u32 last_chan_num;
  int last_chan_band;
  u32 last_chan_flags; /* channel flags from enum mtlk_channel_flags */
  struct channel_scan_support css[1 + NUM_TOTAL_CHANS]; /* 0-th entry not used */
  // FIXME we'll have to tie these to a VAP, so that wpa_supplicant's scans don't screw up hostapd's surveys */
  int num_surveys;
  struct channel_survey_support csys[MAX_NUM_SURVEYS];
  /* buffers to use when preparing probe requests for active scans */
  u8 supp_rates[NUM_SUPPORTED_BANDS][MAX_SUPPORTED_RATES + 2];
  u8 ht_capab[NUM_SUPPORTED_BANDS][HT_CAPAB_LEN + 2];
  u8 *probe_req; /* this will be alloced per scan, because we don't know how long the extra IE-s will be */
  size_t probe_req_len;
} mtlk_scan_support_t;

typedef struct mtlk_alter_scan
{
  BOOL abort_scan;
  BOOL pause_or_prevent;
  BOOL resume_or_allow;
} mtlk_alter_scan_t;

/* Checks whether some kind of scan is going on right now */
/* Won't tell about a SCAN_DO_SCAN sitting on master serializer's queue ready to start.
 * Such a thing can be identified by looking at rdev->scan_req
 * but it looks just like that also after a scan ends and before the kernel has reported the results...
 */
static __INLINE
unsigned is_scan_running(const struct scan_support *ss)
{
  return ss->flags & SDF_RUNNING;
}

static __INLINE
unsigned is_mac_scan_running(const struct scan_support *ss)
{
  return ss->flags & SDF_MAC_RUNNING;
}

/* This is an optimization, the scan will be "ignorant" (and won't report frames to the kernel)
 * at all those times when it's not explicitly sitting on the right channel waiting the timeout.
 * I.e., during processing, during breaks from a background scan, initial preparation, etc.
 * Things we would hear during those times are those on our regular channel
 * or duplicates of what we just heard or will hear on any explicitly scannable channel.
 */
static __INLINE
unsigned is_scan_ignorant(const struct scan_support *ss)
{
  return ss->flags & SDF_IGNORE_FRAMES;
}

BOOL passes_scan_filters(const struct scan_support *ss, const char *ssid, s32 rssi);
int mtlk_scan_recovery_get_channel(mtlk_core_t *core, struct mtlk_chan_def *chandef);
int mtlk_scan_recovery_continue_scan(mtlk_core_t *core, BOOL is_bg_scan);

void abort_scan(struct scan_support *ss);
int abort_scan_sync(mtlk_core_t *core);
int pause_or_prevent_scan(mtlk_core_t *core);
int resume_or_allow_scan(mtlk_core_t *core);
int clean_up_after_this_scan(mtlk_core_t *core, struct scan_support *ss);
uint32 sched_scan_timer_clb_func(mtlk_osal_timer_t *timer, mtlk_handle_t nic);
uint32 cac_timer_clb_func(mtlk_osal_timer_t *timer, mtlk_handle_t nic);
int cac_timer_serialized_func(mtlk_handle_t hcore, const void* data, uint32 data_size);

int scan_support_init(mtlk_core_t *core, mtlk_scan_support_t *ss);
void scan_support_cleanup(mtlk_scan_support_t *ss);
int _scan_do_scan(mtlk_core_t *core, struct mtlk_scan_request *request);
int scan_timeout_async_func(mtlk_handle_t hcore, const void* data, uint32 data_size);
int scan_do_scan(mtlk_handle_t hcore, const void* data, uint32 data_size);
int scan_dump_survey(mtlk_handle_t hcore, const void* data, uint32 data_size);
int scan_start_sched_scan(mtlk_handle_t hcore, const void* data, uint32 data_size);
int scan_stop_sched_scan(mtlk_handle_t hcore, const void* data, uint32 data_size);

int __MTLK_IFUNC
mtlk_alter_scan(mtlk_handle_t hcore, const void* data, uint32 data_size);

int scan_get_aocs_info(mtlk_core_t *core, struct intel_vendor_channel_data *ch_data, struct channel_survey_support *csys);

int mtlk_send_chan_data(mtlk_vap_handle_t vap_handle, void *data, int data_len);

void __MTLK_IFUNC
wave_scan_support_fill_chan_bonding_by_chan(wave_chan_bonding_t *out_data, unsigned center_chan, unsigned primary_chan);

void __MTLK_IFUNC
wave_scan_support_fill_chan_bonding_by_freq(wave_chan_bonding_t *out_data, unsigned center_freq, unsigned primary_freq);

void scan_chan_survey_set_chan_load(struct channel_survey_support *csys, unsigned ch_load);
int scan_send_get_chan_load(const mtlk_core_t *core, uint32 *chan_load);
#undef LOG_LOCAL_GID
#undef LOG_LOCAL_FID

#endif /* __SCAN_SUPPORT_H__ */
