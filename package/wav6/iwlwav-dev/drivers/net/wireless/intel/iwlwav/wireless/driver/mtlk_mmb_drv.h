/******************************************************************************

                               Copyright (c) 2012
                            Lantiq Deutschland GmbH

  For licensing information, see the file 'LICENSE' in the root folder of
  this software module.

******************************************************************************/
#ifndef __MTLK_MMB_DRV_H_INCLUDED__ 
#define __MTLK_MMB_DRV_H_INCLUDED__

/**************************************************************
 *  Export global data types                                  *
 **************************************************************/

typedef struct
{
  void* start;
  int length;
  BOOL swap;
} mtlk_cpu_mem_t;

#define FW_NAME_MAX_LEN 0x40
#define CHI_CPU_MEM_CNT 3
#define LOGGER_SID_INVALID 0 /* can't be zero, leads to FW crash? */

#define CPU_FREQ_RESERVED MAX_UINT32

typedef struct
{
  char fname[FW_NAME_MAX_LEN];
  mtlk_cpu_mem_t mem[CHI_CPU_MEM_CNT];
} mtlk_fw_info_t;

enum mtlk_proc_fw_type {
    PROC_FW_IO = 0,
    PROC_FW_BCL,
};

typedef struct {
    char                   *name;
    enum mtlk_proc_fw_type  type;
    uint32                  io_base;
    uint32                  io_size;
} wave_fw_dump_info_t;

typedef struct {
    char                   *name;
    uint32                  io_base;
    uint32                  io_size;
} wave_hw_dump_info_t;

typedef struct _mtlk_mmb_drv_t mtlk_mmb_drv_t;

/**************************************************************
 *  Export global objects                                     *
 **************************************************************/

extern int tx_ring_size      [MTLK_MAX_HW_ADAPTERS_SUPPORTED];
extern int loggersid         [MTLK_MAX_HW_ADAPTERS_SUPPORTED];
extern int band_cfg_sys_mode [MTLK_MAX_HW_ADAPTERS_SUPPORTED];
extern int panic_on_fw_error [MTLK_MAX_HW_ADAPTERS_SUPPORTED];
extern int mgmttx_en         [MTLK_MAX_HW_ADAPTERS_SUPPORTED];

#ifdef CPTCFG_IWLWAV_SET_PM_QOS
extern int cpu_dma_latency;
#endif

/**************************************************************
 *  Export global interfaces                                  *
 **************************************************************/

int __MTLK_IFUNC
mtlk_mmb_drv_postpone_irq_handler(mtlk_irq_handler_data *ihd);

uint32 __MTLK_IFUNC
mtlk_mmb_drv_get_disable_11d_hint_param(void);

struct pci_dev * __MTLK_IFUNC
mtlk_mmb_drv_get_pci_dev(struct _mtlk_mmb_drv_t *mmb_drv);


#endif /* __MTLK_MMB_DRV_H_INCLUDED__ */
