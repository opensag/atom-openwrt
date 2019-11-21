/******************************************************************************

                               Copyright (c) 2012
                            Lantiq Deutschland GmbH

  For licensing information, see the file 'LICENSE' in the root folder of
  this software module.

******************************************************************************/
/*
 * $Id$
 *
 * 
 *
 * Written by: Grygorii Strashko
 *
 * Power management functionality implementation in compliance with
 * Code of Conduct on Energy Consumption of Broadband Equipment (a.k.a CoC)
 *
 */


#ifndef __MTLK_POWER_MANAGEMENT_H__
#define __MTLK_POWER_MANAGEMENT_H__

#define   MTLK_IDEFS_ON
#include "mtlkidefs.h"

/**********************************************************************
 * Public declaration
***********************************************************************/
typedef struct _mtlk_coc_t mtlk_coc_t;

typedef struct _mtlk_coc_auto_cfg_t
{
  uint32 interval_1x1;
  uint32 interval_2x2;
  uint32 interval_3x3;
  uint32 interval_4x4;
  uint32 high_limit_1x1;
  uint32 low_limit_2x2;
  uint32 high_limit_2x2;
  uint32 low_limit_3x3;
  uint32 high_limit_3x3;
  uint32 low_limit_4x4;
} __MTLK_IDATA mtlk_coc_auto_cfg_t;

typedef struct _mtlk_coc_antenna_cfg_t
{
  uint8 num_tx_antennas;
  uint8 num_rx_antennas;
} __MTLK_IDATA mtlk_coc_antenna_cfg_t;

typedef struct _mtlk_coc_cfg_t
{
  mtlk_vap_handle_t vap_handle;
  mtlk_txmm_t *txmm;
  mtlk_coc_antenna_cfg_t hw_antenna_cfg;
  mtlk_coc_auto_cfg_t default_auto_cfg;
} __MTLK_IDATA mtlk_coc_cfg_t;

/**********************************************************************
 * Public function declaration
***********************************************************************/
mtlk_coc_t* __MTLK_IFUNC
mtlk_coc_create(const mtlk_coc_cfg_t *cfg);

void __MTLK_IFUNC
mtlk_coc_delete(mtlk_coc_t *coc_obj);

int __MTLK_IFUNC
mtlk_coc_set_auto_params(mtlk_coc_t *coc_obj, const mtlk_coc_auto_cfg_t *auto_params);

int __MTLK_IFUNC
mtlk_coc_set_antenna_params(mtlk_coc_t *coc_obj, const mtlk_coc_antenna_cfg_t *antenna_params);

mtlk_coc_auto_cfg_t * __MTLK_IFUNC
mtlk_coc_get_auto_params(mtlk_coc_t *coc_obj);

mtlk_coc_antenna_cfg_t * __MTLK_IFUNC
mtlk_coc_get_current_params(mtlk_coc_t *coc_obj);

int __MTLK_IFUNC
mtlk_coc_set_power_mode(mtlk_coc_t *coc_obj, const BOOL is_auto_mode);

int __MTLK_IFUNC
mtlk_coc_set_actual_power_mode(mtlk_coc_t *coc_obj);

BOOL __MTLK_IFUNC
mtlk_coc_is_auto_mode(mtlk_coc_t *coc_obj);

BOOL __MTLK_IFUNC
mtlk_coc_get_auto_mode_cfg(mtlk_coc_t *coc_obj);

void __MTLK_IFUNC
mtlk_coc_auto_mode_disable(mtlk_coc_t *coc_obj);

void __MTLK_IFUNC
mtlk_coc_reset_antenna_params(mtlk_coc_t *coc_obj);

int __MTLK_IFUNC
mtlk_coc_on_rcvry_configure(mtlk_coc_t *coc_obj);

void __MTLK_IFUNC
mtlk_coc_on_rcvry_isol(mtlk_coc_t *coc_obj);

int __MTLK_IFUNC
mtlk_coc_update_antenna_cfg (mtlk_coc_t *coc_obj, uint8 and_mask, BOOL *update_hw);

#define   MTLK_IDEFS_OFF
#include "mtlkidefs.h"

#endif /* __MTLK_POWER_MANAGEMENT_H__ */
