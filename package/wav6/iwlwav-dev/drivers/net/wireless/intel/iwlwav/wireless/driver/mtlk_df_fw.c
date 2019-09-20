/******************************************************************************

                               Copyright (c) 2012
                            Lantiq Deutschland GmbH

  For licensing information, see the file 'LICENSE' in the root folder of
  this software module.

******************************************************************************/
#include "mtlkinc.h"

#include <linux/device.h>
#include <linux/firmware.h>

#include "mtlk_df_fw.h"
#include "mtlkhal.h"

#define LOG_LOCAL_GID   GID_DFFW
#define LOG_LOCAL_FID   1

void __MTLK_IFUNC
mtlk_df_fw_unload_file(void *ctx, mtlk_df_fw_file_buf_t *fb)
{
  MTLK_ASSERT(NULL != ctx);
  MTLK_UNREFERENCED_PARAM(ctx);

  release_firmware((const struct firmware *)fb->context);
}

static const struct firmware *
_mtlk_df_fw_request_firmware(void *ctx, const char *fname)
{
  const struct firmware *fw_entry = NULL;
  int result = 0;
  int try = 0;

  /* on kernels 2.6 it could be that request_firmware returns -EEXIST
     it means that we tried to load firmware file before this time
     and kernel still didn't close sysfs entries it uses for download
     (see hotplug for details). In order to avoid such problems we
     will try number of times to load FW */
try_load_again:
  ILOG0_S("Request Firmware ('%s') is starting", fname);
  result = request_firmware(&fw_entry, fname, ctx);
  if (result == -EEXIST) {
    try++;
    if (try < 10) {
      msleep(10);
      goto try_load_again;
    }
  }
  if (result != 0)
  {
    WLOG_S("Firmware ('%s') is missing", fname);
    fw_entry = NULL;
  }
  else if (!fw_entry) {
    ELOG_S("Firmware ('%s') was not loaded: request_firmware() returned null pointer", fname);
  }
  else
  {
    ILOG3_SDP("%s firmware: size=0x%x, data=0x%p",
              fname, (unsigned int)fw_entry->size, fw_entry->data);
  }

  return fw_entry;
}

int __MTLK_IFUNC
mtlk_df_fw_load_file (void *ctx, const char *name, mtlk_df_fw_file_buf_t *fb)
{
  int                    res      = MTLK_ERR_UNKNOWN;
  const struct firmware *fw_entry = NULL;

  MTLK_ASSERT(NULL != ctx);
  MTLK_ASSERT(NULL != name);
  MTLK_ASSERT(NULL != fb);

  fw_entry = _mtlk_df_fw_request_firmware(ctx, name);

  if (fw_entry) {
    fb->buffer  = fw_entry->data;
    fb->size    = fw_entry->size;
    fb->context = HANDLE_T(fw_entry);
    res         = MTLK_ERR_OK;
  }

  return res;
}

