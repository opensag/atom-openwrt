/******************************************************************************

                               Copyright (c) 2012
                            Lantiq Deutschland GmbH

  For licensing information, see the file 'LICENSE' in the root folder of
  this software module.

******************************************************************************/
#ifndef __DRV_VER_H__
#define __DRV_VER_H__

#ifdef CPTCFG_IWLWAV_DEBUG
#define DRV_COMPILATION_TYPE ".Debug"
#else
#  ifdef CPTCFG_IWLWAV_SILENT
#    define DRV_COMPILATION_TYPE ".Silent"
#  else
#    define DRV_COMPILATION_TYPE ".Release"
#  endif
#endif

#ifdef CPTCFG_IWLWAV_LINDRV_HW_PCIEG5
# define MTLK_PCIEG5 ".PcieG5"
#else
# define MTLK_PCIEG5
#endif

#ifdef CPTCFG_IWLWAV_LINDRV_HW_PCIEG6
# define MTLK_PCIEG6 ".PcieG6"
#else
# define MTLK_PCIEG6
#endif

#define MTLK_PLATFORMS  MTLK_PCIEG5 MTLK_PCIEG6

#define DRV_NAME        "mtlk"
#define DRV_VERSION     MTLK_SOURCE_VERSION \
                        MTLK_PLATFORMS DRV_COMPILATION_TYPE
#define DRV_DESCRIPTION "Metalink 802.11n WiFi Network Driver"

#endif /* !__DRV_VER_H__ */

