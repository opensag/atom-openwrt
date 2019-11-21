/******************************************************************************

                               Copyright (c) 2012
                            Lantiq Deutschland GmbH

  For licensing information, see the file 'LICENSE' in the root folder of
  this software module.

******************************************************************************/
#ifdef MTLK_LEGACY_STATISTICS
#ifndef __MTLK_WSSD_H__
#define __MTLK_WSSD_H__

#include "mtlkwssairb.h"

mtlk_irbd_handle_t* __MTLK_IFUNC
mtlk_wssd_register_request_handler(mtlk_irbd_t* irbd,
                                   mtlk_irbd_evt_handler_f handler_func,
                                   mtlk_handle_t handler_ctx);

void __MTLK_IFUNC
mtlk_wssd_unregister_request_handler(mtlk_irbd_t* irbd,
                                     mtlk_irbd_handle_t* reg_handle);

int __MTLK_IFUNC
mtlk_wssd_send_event(mtlk_irbd_t *     irbd,
                     uint32            info_id,
                     void*             data_buff,
                     uint32            buff_length);

#endif /* __MTLK_WSSD_H__ */
#endif /* MTLK_LEGACY_STATISTICS */
