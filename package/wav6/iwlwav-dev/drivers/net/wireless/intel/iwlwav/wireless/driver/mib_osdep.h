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
 * Authors: originaly written by Joel Isaacson;
 *  further development and support by: Andriy Tkachuk, Artem Migaev,
 *  Oleksandr Andrushchenko.
 *
 */

#ifndef _MIB_OSDEP_H_
#define _MIB_OSDEP_H_

#include "mhi_umi.h"
#include "mhi_mib_id.h"

#include "core.h"

#define LOG_LOCAL_GID   GID_MIBS
#define LOG_LOCAL_FID   0

void  mtlk_mib_set_nic_cfg (struct nic *nic);

int mtlk_set_vap_mibs(struct nic *nic);
int mtlk_mib_set_pre_activate(struct nic *nic);

#undef LOG_LOCAL_GID
#undef LOG_LOCAL_FID

#endif // _MIB_OSDEP_H_
