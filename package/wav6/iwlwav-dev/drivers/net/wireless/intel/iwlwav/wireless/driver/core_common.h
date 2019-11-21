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
 * Common core module definitions
 *
 */
#ifndef __CORE_COMMON_H__
#define __CORE_COMMON_H__

#define LOG_LOCAL_GID   GID_COMMON_CORE
#define LOG_LOCAL_FID   1

void __MTLK_IFUNC
mtlk_cc_handle_eeprom_failure(mtlk_vap_handle_t vap_handle, const EEPROM_FAILURE_EVENT *data);

void __MTLK_IFUNC
mtlk_cc_handle_generic_event(mtlk_vap_handle_t vap_handle, const GENERIC_EVENT *data);

void __MTLK_IFUNC
mtlk_cc_handle_algo_calibration_failure(mtlk_vap_handle_t vap_handle, const CALIBR_ALGO_EVENT* calibration_event);

void __MTLK_IFUNC
mtlk_cc_handle_dummy_event(mtlk_vap_handle_t vap_handle, const DUMMY_EVENT* data);

void __MTLK_IFUNC
mtlk_cc_handle_unknown_event(mtlk_vap_handle_t vap_handle, uint32 event_id);

#undef LOG_LOCAL_GID
#undef LOG_LOCAL_FID

#endif
