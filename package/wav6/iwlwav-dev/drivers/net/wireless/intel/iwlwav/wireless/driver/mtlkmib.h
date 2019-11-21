/******************************************************************************

                               Copyright (c) 2012
                            Lantiq Deutschland GmbH

  For licensing information, see the file 'LICENSE' in the root folder of
  this software module.

******************************************************************************/
#ifndef __MTLKMIB_H__
#define __MTLKMIB_H__

#include "txmm.h"
#include "frame.h"
#include "mhi_mib_id.h"

int __MTLK_IFUNC mtlk_set_mib_value_raw (mtlk_txmm_t *txmm, uint16 u16ObjectID, MIB_VALUE *uValue);
int __MTLK_IFUNC mtlk_get_mib_value_raw (mtlk_txmm_t *txmm, uint16 u16ObjectID, MIB_VALUE *uValue);

int __MTLK_IFUNC mtlk_set_mib_value_uint8 (mtlk_txmm_t *txmm, uint16 u16ObjectID, uint8 value);
int __MTLK_IFUNC mtlk_get_mib_value_uint8 (mtlk_txmm_t *txmm, uint16 u16ObjectID, uint8 *value);
int __MTLK_IFUNC mtlk_set_mib_value_uint16 (mtlk_txmm_t *txmm, uint16 u16ObjectID, uint16 value);
int __MTLK_IFUNC mtlk_get_mib_value_uint16 (mtlk_txmm_t *txmm, uint16 u16ObjectID, uint16 *value);
int __MTLK_IFUNC mtlk_set_mib_value_uint32 (mtlk_txmm_t *txmm, uint16 u16ObjectID, uint32 value);
int __MTLK_IFUNC mtlk_get_mib_value_uint32 (mtlk_txmm_t *txmm, uint16 u16ObjectID, uint32 *value);

typedef PRE_ACTIVATE_MIB_TYPE mtlk_aux_pm_related_params_t;

int __MTLK_IFUNC mtlk_aux_pm_related_params_set_defaults(mtlk_txmm_t                  *txmm,
                                                         uint8                         net_mode,
                                                         uint8                         spectrum,
                                                         mtlk_aux_pm_related_params_t *result);
int __MTLK_IFUNC mtlk_aux_pm_related_params_set_bss_based(mtlk_txmm_t                  *txmm,
                                                          bss_data_t                   *bss_data,
                                                          uint8                        net_mode,
                                                          uint8                        spectrum,
                                                          mtlk_aux_pm_related_params_t *result);
int __MTLK_IFUNC mtlk_set_mib_pre_activate(mtlk_txmm_t *txmm, mtlk_aux_pm_related_params_t *params);

#endif /* __MTLKMIB_H__ */

