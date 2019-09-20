/******************************************************************************

                               Copyright (c) 2012
                            Lantiq Deutschland GmbH

  For licensing information, see the file 'LICENSE' in the root folder of
  this software module.

******************************************************************************/
#ifndef __MTIDL_READER_H__
#define __MTIDL_READER_H__

#include "mtlkwssa.h"
#include "mtidl_ini_parser.h"

int __MTLK_IFUNC mtlk_calculate_item_size(const char* mtidl_path,
                                          const char* root_binary_type,
                                          uint32 *size);

int __MTLK_IFUNC
mtlk_print_mtidl_item(const char *mtidl_path,
                      const char *binary_type,
                      mtlk_wss_data_source_t source,
                      const void *buffer,
                      uint32 size);

int __MTLK_IFUNC
mtlk_print_mtidl_item_by_id(const char *mtidl_path,
                            mtlk_wss_data_source_t source,
                            int info_id,
                            const void *buffer,
                            uint32      size);

int __MTLK_IFUNC
mtlk_print_requestable_mtidl_items_list(const char* mtidl_path);

int __MTLK_IFUNC
mtlk_count_mtidl_items(const char* mtidl_path, uint32 *items_number);

int __MTLK_IFUNC
mtlk_request_mtidl_item(const char* mtidl_path,
                        const char* ifname,
                        const char* friendly_name,
                        void* provider_id,
                        uint16 info_index,
                        const char* bin_path,
                        const char* bin_write_path,
                        uint32 offset);

#endif /* !__MTIDL_READER_H__ */
