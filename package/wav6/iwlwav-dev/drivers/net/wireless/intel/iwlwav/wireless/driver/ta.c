/******************************************************************************

                               Copyright (c) 2012
                            Lantiq Deutschland GmbH

  For licensing information, see the file 'LICENSE' in the root folder of
  this software module.

******************************************************************************/
/*
 * $Id: ta.c 13212 2012-07-04 11:45:27Z sumilovs $
 *
 * Traffic Analyzer
 *
 */

#include "mtlkinc.h"
#include "mtlk_vap_manager.h"
#include "mtlk_coreui.h"
#include "core.h"               /* Interface to STADB and Core */

#include "ta.h"
#include "ta_priv.h"

#define LOG_LOCAL_GID   GID_TA
#define LOG_LOCAL_FID   1

MTLK_INIT_STEPS_LIST_BEGIN(ta)
  MTLK_INIT_STEPS_LIST_ENTRY(ta, STA_WSS_LIST_LOCK)
  MTLK_INIT_STEPS_LIST_ENTRY(ta, STA_WSS_LIST)
  MTLK_INIT_STEPS_LIST_ENTRY(ta, TIMER)
MTLK_INIT_INNER_STEPS_BEGIN(ta)
MTLK_INIT_STEPS_LIST_END(ta);


/****************************************************************************/
/*         Internal Functions                                               */
/****************************************************************************/

static uint32 
_ta_wss_cntr_update (_ta_wss_cntr_t *cntr, uint32 wss_curr)
{
  uint32    wss_delta, wss_prev;

  wss_prev = cntr->prev;
  wss_delta = wss_curr - wss_prev;

  ILOG3_PDDD("TA WSS CNTR (%p) old/new/delta %d/%d/%d", cntr, wss_prev, wss_curr, wss_delta);

  cntr->prev = wss_curr;
  cntr->delta = wss_delta;

  return  wss_delta;
}

static _sta_wss_entry_t* 
_exclude_wss_sta (_traf_analyzer_t *ta, sta_entry *sta){

  mtlk_dlist_entry_t  *entry;
  mtlk_dlist_entry_t  *head;
  
  _sta_wss_entry_t *sta_wss;
  _sta_wss_entry_t *sta_wss_exclude;

  /* Loop over TA list and find & exclude entry*/
  sta_wss_exclude = NULL;
  mtlk_osal_mutex_acquire(&ta->sta_wss_list_lock);
  mtlk_dlist_foreach(&ta->sta_wss_list, entry, head)
  {
    sta_wss = MTLK_LIST_GET_CONTAINING_RECORD(entry, _sta_wss_entry_t, list_entry);
    if (sta_wss->sta == sta){
      mtlk_dlist_remove(&ta->sta_wss_list, entry);
      sta_wss_exclude = sta_wss;
      break;
    }
  }
  mtlk_osal_mutex_release(&ta->sta_wss_list_lock);

  return sta_wss_exclude;
}

/** Convert bytes per interval into bytes per second */
static uint32 
_coc_convert_result (uint32 tp_bytes_interval, uint32 coeff)
{
  return (uint32) (((uint64) tp_bytes_interval * (uint64) coeff) >> TA_Q_SHIFT);
}

static _ta_crit_entry_t* 
_find_crit (_traf_analyzer_t *ta, ta_crit_id_t crit_id)
{
  int i;

  for (i = 0; i < TA_CRIT_NUM; i++){
    if (ta->crit_tbl[i].id == crit_id) break;
  }

  return (i == TA_CRIT_NUM) ? NULL : &ta->crit_tbl[i];
}

static void 
_ta_crit_coc (_ta_crit_entry_t *crit)
{
  mtlk_dlist_entry_t  *entry;
  mtlk_dlist_entry_t  *head;
  _sta_wss_entry_t    *sta_wss;
  _traf_analyzer_t    *ta;

  uint32  max_tp_bytes, max_tp_mibits;
  uint32  val_rx, val_tx;
  
  MTLK_ASSERT(crit != NULL);
  MTLK_ASSERT(crit->signature == TA_CRIT_SIGN);

  ta = crit->ta;

  ILOG3_V("COC Criterion Calc");

  /* Collect data from WSS counters */
  max_tp_bytes = 0;
  
  mtlk_osal_mutex_acquire(&ta->sta_wss_list_lock);
  mtlk_dlist_foreach(&ta->sta_wss_list, entry, head)
  {
    sta_wss = MTLK_LIST_GET_CONTAINING_RECORD(entry, _sta_wss_entry_t, list_entry);
    val_rx = _ta_wss_cntr_update(&sta_wss->coc_rx_bytes, mtlk_sta_get_stat_cntr_rx_bytes(sta_wss->sta));
    val_tx = _ta_wss_cntr_update(&sta_wss->coc_tx_bytes, mtlk_sta_get_stat_cntr_tx_bytes(sta_wss->sta));
    ILOG3_YDD("WSS STA %Y rx %d, tx %d", mtlk_sta_get_addr(sta_wss->sta), val_rx, val_tx);

    if (!sta_wss->coc_wss_valid){
      /* Skip processing not valid WSS */
      ILOG3_V("WSS STA skipped");
      sta_wss->coc_wss_valid = TRUE;
      continue;
    }

    if (val_rx > max_tp_bytes) max_tp_bytes = val_rx;
    if (val_tx > max_tp_bytes) max_tp_bytes = val_tx;
  }
  mtlk_osal_mutex_release(&ta->sta_wss_list_lock);

  max_tp_mibits = _coc_convert_result(max_tp_bytes, ta->coc_cfg.coeff);
  ILOG3_DD("CoC MAX_TP %d bytes, %d bytes/sec", max_tp_bytes, max_tp_mibits);

  crit->clb(crit->clb_ctx, HANDLE_T(&max_tp_mibits));

  return;
}

static void 
_ta_crit_coc_init (_ta_crit_entry_t *crit)
{
  mtlk_dlist_entry_t  *entry;
  mtlk_dlist_entry_t  *head;
  _sta_wss_entry_t    *sta_wss;
  _traf_analyzer_t *ta;

  MTLK_ASSERT(crit != NULL);
  MTLK_ASSERT(crit->signature == TA_CRIT_SIGN);

  ta = crit->ta;

  ILOG2_V("COC Criterion Init");

  /* Init all CoC WSS counters */
  mtlk_osal_mutex_acquire(&ta->sta_wss_list_lock);
  mtlk_dlist_foreach(&ta->sta_wss_list, entry, head)
  {
    sta_wss = MTLK_LIST_GET_CONTAINING_RECORD(entry, _sta_wss_entry_t, list_entry);

    _ta_wss_cntr_update(&sta_wss->coc_rx_bytes, mtlk_sta_get_stat_cntr_rx_bytes(sta_wss->sta));
    _ta_wss_cntr_update(&sta_wss->coc_tx_bytes, mtlk_sta_get_stat_cntr_tx_bytes(sta_wss->sta));
  
    sta_wss->coc_wss_valid = TRUE;

  }
  mtlk_osal_mutex_release(&ta->sta_wss_list_lock);

  crit->fcn = _ta_crit_coc;
  return;
}

static void 
_mtlk_ta_cleanup (_traf_analyzer_t *ta)
{
  MTLK_ASSERT(ta != NULL);

  MTLK_CLEANUP_BEGIN(ta, MTLK_OBJ_PTR(ta))

    MTLK_CLEANUP_STEP(ta, TIMER, MTLK_OBJ_PTR(ta),
                      mtlk_osal_timer_cleanup, (&ta->timer));

    MTLK_CLEANUP_STEP(ta, STA_WSS_LIST, MTLK_OBJ_PTR(ta),
                      mtlk_dlist_cleanup, (&ta->sta_wss_list));

    MTLK_CLEANUP_STEP(ta, STA_WSS_LIST_LOCK, MTLK_OBJ_PTR(ta),
                      mtlk_osal_mutex_cleanup, (&ta->sta_wss_list_lock));

  MTLK_CLEANUP_END(ta, MTLK_OBJ_PTR(ta));
}

static int
_mtlk_ta_init (_traf_analyzer_t *ta, mtlk_vap_manager_t *vap_manager)
{
  int i;

  MTLK_ASSERT(ta != NULL);

  ta->vap_manager = vap_manager;
  ta->signature = TA_SIGN;
  mtlk_osal_atomic_set(&ta->timer_res_ms, TA_DEF_TMR_TICKS * TA_TIMER_MS_PER_TICK);

  /* Init Criteria Table */
  for (i = 0; i < TA_CRIT_NUM; i++){
    _ta_crit_entry_t *crit = &ta->crit_tbl[i];
    crit->signature = TA_CRIT_SIGN;
    crit->id = (ta_crit_id_t)i;
    crit->ta = ta;
    crit->fcn = NULL;
  }
    
  MTLK_INIT_TRY(ta, MTLK_OBJ_PTR(ta))
    MTLK_INIT_STEP(ta, STA_WSS_LIST_LOCK, MTLK_OBJ_PTR(ta),
                    mtlk_osal_mutex_init, (&ta->sta_wss_list_lock));

    MTLK_INIT_STEP_VOID(ta, STA_WSS_LIST, MTLK_OBJ_PTR(ta),
                        mtlk_dlist_init, (&ta->sta_wss_list));

    MTLK_INIT_STEP(ta, TIMER, MTLK_OBJ_PTR(ta),
                   mtlk_osal_timer_init,
                   (&ta->timer, mtlk_core_ta_on_timer, (mtlk_handle_t)ta));

  MTLK_INIT_FINALLY(ta, MTLK_OBJ_PTR(ta))
  MTLK_INIT_RETURN(ta, MTLK_OBJ_PTR(ta), _mtlk_ta_cleanup, (ta));
}


/****************************************************************************/
/*         Interface Functions                                              */
/****************************************************************************/

/** Create TA object */
mtlk_handle_t __MTLK_IFUNC 
mtlk_ta_create (mtlk_vap_manager_t *vap_manager)
{
  _traf_analyzer_t *ta;

  ta = mtlk_osal_mem_alloc(sizeof(_traf_analyzer_t), LQLA_MEM_TAG_TA);
  if (NULL != ta)
  {
    memset(ta, 0, sizeof(_traf_analyzer_t));
    if(MTLK_ERR_OK != _mtlk_ta_init(ta, vap_manager))
    {
      mtlk_osal_mem_free(ta);
      ta = NULL;
    }
  }
  
  return (mtlk_handle_t)ta;
}

/** Destroy TA object */
void __MTLK_IFUNC 
mtlk_ta_delete (mtlk_handle_t ta_handle)
{
  _traf_analyzer_t *ta = (_traf_analyzer_t *)ta_handle;

  MTLK_ASSERT(ta_handle != MTLK_INVALID_HANDLE);
  MTLK_ASSERT(ta->signature == TA_SIGN);

  _mtlk_ta_cleanup(ta);

  mtlk_osal_mem_free(ta);
}

/** AOCS configuration function */
void __MTLK_IFUNC 
mtlk_ta_aocs_cfg (mtlk_handle_t ta_handle, ta_crit_aocs_cfg_t *cfg)     
{
  _traf_analyzer_t *ta = (_traf_analyzer_t *)ta_handle;

  MTLK_ASSERT(ta_handle != MTLK_INVALID_HANDLE);
  MTLK_ASSERT(ta->signature == TA_SIGN);

  MTLK_UNREFERENCED_PARAM(ta);

  return;
}

/** CoC configuration function */
int __MTLK_IFUNC 
mtlk_ta_coc_cfg (mtlk_handle_t ta_handle, ta_crit_coc_cfg_t *cfg)
{
  _traf_analyzer_t *ta;
  _ta_crit_entry_t *crit;
  uint32  ta_tmr_res_ms;

  MTLK_ASSERT(ta_handle != MTLK_INVALID_HANDLE);

  ta = (_traf_analyzer_t *)ta_handle;
  MTLK_ASSERT(ta->signature == TA_SIGN);

  ta_tmr_res_ms = mtlk_osal_atomic_get(&ta->timer_res_ms);

  if ((cfg->interval * TA_COC_TICK_MSEC) < ta_tmr_res_ms) {
    WLOG_D("Value shouldn't be less than timer resolution %d msec",ta_tmr_res_ms);
    return MTLK_ERR_PARAMS;
  }

  ta->coc_cfg.interval = cfg->interval;

  /* Precalculate coefficient for CoC result conversion */
  ta->coc_cfg.coeff = (1000 << TA_Q_SHIFT) / (ta->coc_cfg.interval * TA_COC_TICK_MSEC); /* 8Q7 */

  crit = &ta->crit_tbl[TA_CRIT_ID_COC];

  /* Convert COC ticks to TA ticks */
  crit->tmr_period = (TA_COC_TICK_MSEC * ta->coc_cfg.interval / ta_tmr_res_ms);
  ILOG2_D("CoC timer set to %d ticks", crit->tmr_period);

  /* Restart counters & timer at next TA tick if already running */
  if (crit->fcn){
    crit->tmr_cnt = crit->tmr_period - 1;
    crit->fcn = _ta_crit_coc_init;
  }

  MTLK_ASSERT(crit->tmr_period != 0);

  return MTLK_ERR_OK;
}

/** Getter for timer resolution */
uint32 __MTLK_IFUNC 
mtlk_ta_get_timer_resolution_ms (mtlk_handle_t ta_handle)
{
  _traf_analyzer_t *ta = (_traf_analyzer_t *)ta_handle;

  MTLK_ASSERT(ta_handle != MTLK_INVALID_HANDLE);
  MTLK_ASSERT(ta->signature == TA_SIGN);

  return mtlk_osal_atomic_get(&ta->timer_res_ms);  
}

/** Getter for timer resolution, in TA ticks */
uint32 __MTLK_IFUNC 
mtlk_ta_get_timer_resolution_ticks (mtlk_handle_t ta_handle)
{
  uint32 timer_res;
  _traf_analyzer_t *ta = (_traf_analyzer_t *)ta_handle;

  MTLK_ASSERT(ta_handle != MTLK_INVALID_HANDLE);
  MTLK_ASSERT(ta->signature == TA_SIGN);

  timer_res = mtlk_osal_atomic_get(&ta->timer_res_ms) / TA_TIMER_MS_PER_TICK;
  
  return timer_res;
}

/** Setter for timer resolution */
int __MTLK_IFUNC 
mtlk_ta_set_timer_resolution_ticks (mtlk_handle_t ta_handle, uint32 timer_resolution)
{
  int res = MTLK_ERR_OK;
  _traf_analyzer_t *ta = (_traf_analyzer_t *)ta_handle;

  MTLK_ASSERT(ta_handle != MTLK_INVALID_HANDLE);
  MTLK_ASSERT(ta->signature == TA_SIGN);

  if ((timer_resolution >= 1 && timer_resolution <= TA_TIMER_MAX_VALUE_IN_TICKS)
      && ((ta->coc_cfg.interval * TA_COC_TICK_MSEC) >= (timer_resolution * TA_TIMER_MS_PER_TICK))) {
    mtlk_osal_atomic_set(&ta->timer_res_ms, timer_resolution * TA_TIMER_MS_PER_TICK);
    ILOG3_D("TA timer resolution set to %d msec", (int)timer_resolution * TA_TIMER_MS_PER_TICK);

    /* Recalculate Criterion timers. I.e. reconfigure with current config */
    mtlk_ta_coc_cfg(ta_handle, &ta->coc_cfg); 
  }
  else {
    res = MTLK_ERR_PARAMS;
    WLOG_D("Value should be in interval 1..%d",
           MIN(TA_TIMER_MAX_VALUE_IN_TICKS, (ta->coc_cfg.interval * TA_COC_TICK_MSEC) / TA_TIMER_MS_PER_TICK));
  }

  return res;
}

void __MTLK_IFUNC 
mtlk_ta_on_connect (mtlk_handle_t ta_handle, sta_entry *sta)
{
  /* Note: This function may called from VAP's serializer context,
           running in parallel. So, protection for STA WSS LIST is required
  */
  _traf_analyzer_t *ta;
  _sta_wss_entry_t *sta_wss;

  MTLK_ASSERT(ta_handle != MTLK_INVALID_HANDLE);

  ta = (_traf_analyzer_t *)ta_handle;
  MTLK_ASSERT(ta->signature == TA_SIGN);

  sta_wss = mtlk_osal_mem_alloc(sizeof(_sta_wss_entry_t), LQLA_MEM_TAG_TA);

  if (!sta_wss){
    WLOG_V("Can't allocate STA_WSS");
    return;
  }

  ILOG3_PY("Create STA WSS entry (%p) for %Y", sta_wss, mtlk_sta_get_addr(sta));
  memset(sta_wss, 0, sizeof(_sta_wss_entry_t));

  /* Init STA WSS list entry */
  mtlk_sta_incref(sta);
  sta_wss->sta = sta;

  sta_wss->coc_wss_valid = FALSE;  /* WSS Counters will be initialized at next criterion call */

  mtlk_osal_mutex_acquire(&ta->sta_wss_list_lock);
  mtlk_dlist_push_front(&ta->sta_wss_list, &sta_wss->list_entry);
  mtlk_osal_mutex_release(&ta->sta_wss_list_lock);

  return;
}

void __MTLK_IFUNC 
mtlk_ta_on_disconnect (mtlk_handle_t ta_handle, sta_entry *sta)
{
  /* Note: This function may called from VAP's serializer context,
           running in parallel. So, protection for STA WSS LIST is required
  */
  _traf_analyzer_t *ta;
  _sta_wss_entry_t *sta_wss;

  MTLK_ASSERT(ta_handle != MTLK_INVALID_HANDLE);
  MTLK_ASSERT(sta != NULL);

  ta = (_traf_analyzer_t *)ta_handle;
  MTLK_ASSERT(ta->signature == TA_SIGN);

  /* Find wss sta */
  sta_wss = _exclude_wss_sta(ta, sta);

  /* STA must be in the STA_WSS list */
  MTLK_ASSERT(sta_wss != NULL);

  ILOG3_PY("Delete STA WSS entry (%p) for %Y", sta_wss, mtlk_sta_get_addr(sta));

  mtlk_sta_decref(sta);

  /* Delete STA_WSS entry */
  mtlk_osal_mem_free(sta_wss);

  return;
}

/** Start processing given criterion */
void __MTLK_IFUNC 
mtlk_ta_crit_start (mtlk_handle_t crit_handle)
{
  _traf_analyzer_t *ta;
  _ta_crit_entry_t *crit;

  ILOG2_D("Crit START 0x%08X", (uint32)crit_handle);
  crit = (_ta_crit_entry_t*)crit_handle;

  MTLK_ASSERT(crit != NULL);
  MTLK_ASSERT(crit->signature == TA_CRIT_SIGN);

  /* Check criterion configuration */
  MTLK_ASSERT(crit->tmr_period != 0);

  ta = crit->ta;
  if (0 == ta->active_crit_num) {
    ILOG2_V("TA timer started");
    mtlk_osal_timer_set(&ta->timer, mtlk_osal_atomic_get(&ta->timer_res_ms));
  }

  /* Check is criterion already started */
  if (!crit->fcn){

    /* Restart counters & timer at next TA tick */
    switch(crit->id){
      case TA_CRIT_ID_COC:  crit->fcn = _ta_crit_coc_init;  break;
      default : break;
    }
    crit->tmr_cnt = crit->tmr_period - 1;

    ta->active_crit_num ++;
    ILOG2_DP("Criterion %d (%p) STARTED ", (int)crit->id, crit);
  }

  return;
}

/** Stop processing given criterion */
void __MTLK_IFUNC 
mtlk_ta_crit_stop (mtlk_handle_t crit_handle)
{
  _traf_analyzer_t *ta;
  _ta_crit_entry_t *crit = (_ta_crit_entry_t*)crit_handle;

  MTLK_ASSERT(crit != NULL);
  MTLK_ASSERT(crit->signature == TA_CRIT_SIGN);

  ta = crit->ta;

  /* Check is criterion started */
  if (crit->fcn){
    ta->active_crit_num --;
    crit->fcn = NULL;
    ILOG2_DP("Criterion %d (%p) STOPPED ", (int)crit->id, crit);
  }

  if (0 == ta->active_crit_num) {
    ILOG2_V("TA timer stoped");
    mtlk_osal_timer_cancel(&ta->timer);
  }
 
  return;
}

/** Register callback for the given criterion */
mtlk_handle_t __MTLK_IFUNC 
mtlk_ta_crit_register (mtlk_handle_t ta_handle, ta_crit_id_t crit_id, ta_crit_clb_t clb, mtlk_handle_t clb_ctx)
{
  _traf_analyzer_t *ta;
  _ta_crit_entry_t *crit;

  MTLK_ASSERT(ta_handle != MTLK_INVALID_HANDLE);
  MTLK_ASSERT(clb != NULL);

  ta = (_traf_analyzer_t *)ta_handle;
  MTLK_ASSERT(ta->signature == TA_SIGN);

  crit = _find_crit(ta, crit_id);
  if (!crit){
    WLOG_D("Error! Criterion %d not found", crit_id);
    return MTLK_INVALID_HANDLE;
  }

  /* Criterion found. Validate registration */
  if (crit->fcn || crit->clb) { /* already started or already registered */
    WLOG_D("Error! Criterion %d is busy", crit_id);
    return MTLK_INVALID_HANDLE;
  }

  /* Everything is OK, register callback */
  crit->clb = clb;
  crit->clb_ctx = clb_ctx;

  ILOG2_DPPD("Callback registered for crit. %d (%p), CLB %p, CLB_CTX 0x%08X", (int)crit_id, crit, clb, (uint32)clb_ctx);

  return (mtlk_handle_t)crit;
}

/** Unregister callback from the given criterion */
void __MTLK_IFUNC 
mtlk_ta_crit_unregister (mtlk_handle_t crit_handle)
{
  _ta_crit_entry_t *crit = (_ta_crit_entry_t*)crit_handle;

  MTLK_ASSERT(crit != NULL);
  MTLK_ASSERT(crit->signature == TA_CRIT_SIGN);

  ILOG2_DP("Unregister criterion %d (%p)", (int)crit->id, crit);

  mtlk_ta_crit_stop(crit_handle);

  /* Everything is OK, register callback */
  crit->clb = NULL;
  crit->clb_ctx = HANDLE_T(NULL);

  return;
}

/** Get VAP handle from TA object */
mtlk_vap_handle_t __MTLK_IFUNC 
mtlk_ta_get_vap_handle (mtlk_handle_t ta_handle)
{
  _traf_analyzer_t *ta;
  mtlk_vap_handle_t  vap_handle = NULL;

  MTLK_ASSERT(ta_handle != MTLK_INVALID_HANDLE);

  ta = (_traf_analyzer_t *)ta_handle;

  MTLK_ASSERT(ta->signature == TA_SIGN);
  MTLK_ASSERT(ta->vap_manager != NULL);

  mtlk_vap_manager_get_master_vap(ta->vap_manager, &vap_handle); 

  return vap_handle; 
}

/** Traffic analyzer entry point  */
int ta_timer (mtlk_handle_t ta_handle, const void *data,  uint32 data_size)
{
  int i;
  _traf_analyzer_t *ta;

  /* Note: Executed within Serializer context of Master AP or STA
           Called periodically based on TA timer from the core.
  */
  MTLK_ASSERT(ta_handle != MTLK_INVALID_HANDLE);
  MTLK_ASSERT(data_size == 0);

  ta = (_traf_analyzer_t *)ta_handle;

  MTLK_ASSERT(ta->signature == TA_SIGN);

  /* Loop over all criteria, execute if started */
  for (i = 0; i < TA_CRIT_NUM; i++){
    _ta_crit_entry_t *crit;

    crit = &ta->crit_tbl[i];

    if (!crit->fcn) continue;   /* Not started */
    if (!crit->clb) continue;   /* No one registered */

    /* Check is time to start calculation */
    crit->tmr_cnt ++;
    
    ILOG3_DD("Crit %d tmr %d", crit->id, crit->tmr_cnt);

    if (crit->tmr_cnt == crit->tmr_period){
      crit->tmr_cnt = 0;
      crit->fcn(crit);     /* _ta_crit_coc_init() -> _ta_crit_coc() */
    }
  }

  return MTLK_ERR_OK;
}

void __MTLK_IFUNC
mtlk_ta_get_debug_info (mtlk_handle_t ta_handle, struct _mtlk_ta_debug_info_cfg_t *debug_info)
{
  mtlk_dlist_entry_t  *entry;
  mtlk_dlist_entry_t  *head;
  uint32 i;
  _traf_analyzer_t *ta;

  MTLK_ASSERT(ta_handle != MTLK_INVALID_HANDLE);
  MTLK_ASSERT(debug_info != NULL);

  ta = (_traf_analyzer_t *)ta_handle;

  debug_info->nof_crit = TA_CRIT_NUM;
  for (i = 0; i < debug_info->nof_crit; ++i) {
    _ta_crit_entry_t *entry = &ta->crit_tbl[i];
    mtlk_ta_crit_t   *crit  = &debug_info->crit[i];

    crit->id         = entry->id;
    crit->signature  = entry->signature;
    crit->fcn        = (vfunptr) entry->fcn;
    crit->clb        = (vfunptr) entry->clb;
    crit->clb_ctx    = (uint32)entry->clb_ctx;
    crit->tmr_cnt    = entry->tmr_cnt;
    crit->tmr_period = entry->tmr_period;
  }

  mtlk_osal_mutex_acquire(&ta->sta_wss_list_lock);

  debug_info->nof_sta_wss = ta->sta_wss_list.depth;
  i = 0;
  mtlk_dlist_foreach(&ta->sta_wss_list, entry, head)
  {
    _sta_wss_entry_t  *sta_wss;
    _ta_wss_cntr_t    *coc_rx;
    _ta_wss_cntr_t    *coc_tx;

    mtlk_ta_sta_wss_t *wss = &debug_info->sta_wss[i];

    sta_wss = MTLK_LIST_GET_CONTAINING_RECORD(entry, _sta_wss_entry_t, list_entry);
    wss->addr = *mtlk_sta_get_addr(sta_wss->sta);

    coc_rx  = &sta_wss->coc_rx_bytes;
    coc_tx  = &sta_wss->coc_tx_bytes;

    wss->coc_wss_valid         = sta_wss->coc_wss_valid;

    wss->coc_rx_bytes.prev     = coc_rx->prev;
    wss->coc_rx_bytes.delta    = coc_rx->delta;

    wss->coc_tx_bytes.prev     = coc_tx->prev;
    wss->coc_tx_bytes.delta    = coc_tx->delta;

    ++i;
    if (i >= ARRAY_SIZE(debug_info->sta_wss))
      break;
  }
  mtlk_osal_mutex_release(&ta->sta_wss_list_lock);
}

void __MTLK_IFUNC
mtlk_ta_on_rcvry_isol (mtlk_handle_t ta_handle)
{
  _traf_analyzer_t *ta;

  MTLK_ASSERT(ta_handle != MTLK_INVALID_HANDLE);

  ta = (_traf_analyzer_t *)ta_handle;

  /* Stop Traffic Analyzer tick timer */
  ILOG2_V("TA timer stopped on recover isolation");
  mtlk_osal_timer_cancel_sync(&ta->timer);
}

void __MTLK_IFUNC
mtlk_ta_on_rcvry_restore (mtlk_handle_t ta_handle)
{
  _traf_analyzer_t *ta;

  MTLK_ASSERT(ta_handle != MTLK_INVALID_HANDLE);

  ta = (_traf_analyzer_t *)ta_handle;

  /* Start Traffic Analyzer tick timer */
  if (0 != ta->active_crit_num) {
    ILOG2_V("TA timer started on recover restore");
    mtlk_osal_timer_set(&ta->timer, mtlk_osal_atomic_get(&ta->timer_res_ms));
  }
}
