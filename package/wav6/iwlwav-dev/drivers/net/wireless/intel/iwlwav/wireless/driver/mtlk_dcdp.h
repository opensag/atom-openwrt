/******************************************************************************

                               Copyright (c) 2012
                            Lantiq Deutschland GmbH

  For licensing information, see the file 'LICENSE' in the root folder of
  this software module.

******************************************************************************/
/*
 * $Id$
 *
 * Definition of DirectConnect types
 *
 */

#ifndef __MTLK_DCDP_H__
#define __MTLK_DCDP_H__

#define MTLK_DCDP_SEPARATE_REG      (0)     /* Register and initilalize datapath device in one place (0) or separately (1). */
                                            /* Should be removed further! */
#if MTLK_USE_DIRECTCONNECT_DP_API
#include <net/directconnect_dp_api.h>

#if 0
/* Define new flags (if not defined in API header file) */
#ifndef DC_DP_F_MC_FW_RESET
#define DC_DP_F_MC_FW_RESET          (0x10)
#endif

#ifndef DC_DP_F_MC_NEW_STA
#define DC_DP_F_MC_NEW_STA           (0x20)
#endif
#endif

#define MTLK_DCDP_DCCNTR_NUM        (1)     /* Number of DC counters. For WAVE500 it is 1 */
#define MTLK_DCDP_DCCNTR_SIZE       (4)     /* Counter has 4 bytes length */
#define MTLK_DCDP_DCCNTR_LENGTH     (1)     /* Length of counters. */

#define WAVE_DCDP_MAX_PORTS         (2)     /* Max number of supported ports */
#define WAVE_DCDP_MAX_NDEVS         (2)     /* Max number of supported ndevs */

typedef enum _mtlk_hw_dcdp_mode_t {
  MTLK_DC_DP_MODE_UNREGISTERED,     /*!< Device unregistered */
  MTLK_DC_DP_MODE_UNKNOWN,          /*!< Unknown mode  */
  MTLK_DC_DP_MODE_FASTPATH_GRX350,  /*!< Fastpath mode (GRX350/550) */
  MTLK_DC_DP_MODE_FASTPATH_GRX750,  /*!< Fastpath mode (GRX750/Puma) */
  MTLK_DC_DP_MODE_SWPATH,           /*!< Software path mode */
  MTLK_DC_DP_MODE_LITEPATH,         /*!< LitePath mode + SW path */
  MTLK_DC_DP_MODE_LITEPATH_ONLY     /*!< LitePatn mode only. SW path should be supported internally by driver */
} mtlk_hw_dcdp_mode_t;

typedef enum _mtlk_dcdp_cntr_mode_t {
  MTLK_DC_DP_CNTR_MODE_LITTLE_ENDIAN,
  MTLK_DC_DP_CNTR_MODE_BIG_ENDIAN,
} mtlk_dcdp_cntr_mode_t;

typedef struct _mtlk_dcdp_init_info_t {
  uint32                rd_pool_size;
  uint32                tx_ring_size;
  uint32                rx_ring_size;
  uint32                frag_ring_size;
  /* RX_IN + TX_IN */
  void                 *soc2dev_enq_phys_base;
  void                 *soc2dev_enq_base;
  void                 *dev2soc_ret_enq_phys_base;
  void                 *dev2soc_ret_enq_base;
  /* RX_OUT + TX_OUT */
  void                 *soc2dev_ret_deq_phys_base;
  void                 *soc2dev_ret_deq_base;
  void                 *dev2soc_deq_phys_base;
  void                 *dev2soc_deq_base;
  /* Fragmentation ring */
  void                 *dev2soc_frag_ring_phys_base;
  void                 *dev2soc_frag_ring_base;

  void                 *dev2soc_frag_deq_phys_base;
  void                 *dev2soc_frag_deq_base;
  void                 *dev2soc_frag_enq_phys_base;
  void                 *dev2soc_frag_enq_base;

  mtlk_dcdp_cntr_mode_t cntr_mode;
} mtlk_dcdp_init_info_t;


typedef struct _mtlk_dcdp_dev_t {
  /* port dependent part */
  BOOL                  is_registered[WAVE_DCDP_MAX_NDEVS];
  uint8                 dp_dev_port  [WAVE_DCDP_MAX_NDEVS]; /*!< DirectConnect dev port */
  int32                 dp_port_id   [WAVE_DCDP_MAX_PORTS]; /*!< DirectConnect port id */
  /* ndev dependent part */
  mtlk_ndev_t          *dp_ndev      [WAVE_DCDP_MAX_NDEVS]; /*!< network device */
  mtlk_ndev_t          *dp_radio_ndev[WAVE_DCDP_MAX_NDEVS]; /*!< radio network device (created separately for Litepath mode) */
  struct dc_dp_dev      dp_devspec   [WAVE_DCDP_MAX_NDEVS];
 /* common part */
  uint32                dp_port_flags;      /*!< DirectConnect port allocation flags */
  mtlk_hw_dcdp_mode_t   dp_mode;            /*!< Actual working mode */
  struct dc_dp_host_cap dp_cap;             /*!< DCDP driver capabilities */
  struct dc_dp_dccntr   dp_dccntr[MTLK_DCDP_DCCNTR_NUM]; /*!< dc dp counters */
  struct dc_dp_res      dp_resources;
  BOOL                  dp_frag_wa_enable;  /*!< TRUE if Fragmentation workaround is required */
} mtlk_dcdp_dev_t;

#endif /* MTLK_USE_DIRECTCONNECT_DP_API */

#endif /* __MTLK_DCDP_H__ */
