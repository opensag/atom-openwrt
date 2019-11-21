/******************************************************************************

                               Copyright (c) 2012
                            Lantiq Deutschland GmbH

  For licensing information, see the file 'LICENSE' in the root folder of
  this software module.

******************************************************************************/
#include "mtlkinc.h"

#define SAFE_PLACE_TO_DEFINE_CHIP_INFO
#include "hw_mmb.h"

#include "mtlk_df.h"
#include "mtlkmsg.h"
#include "mtlk_assert.h"

#include "shram.h"
#include "mtlklist.h"
#ifdef MTLK_LEGACY_STATISTICS
#include "mhi_statistics_manager_enums.h"
#endif

#include "mtlk_dfg.h"
#include "mtlk_df_nbuf.h"
#include "mtlk_df_fw.h"
#include "mtlk_gpl_helper.h"
#include "mtlk_vap_manager.h"
#include "mtlkdfdefs.h"
#include "mtlk_df_priv.h"
#include "mtlk_df_user_priv.h"
#include "mtlk_packets.h"
#include "mtlkhal.h"
#include "mtlk_snprintf.h"
#include "mtlkwlanirbdefs.h"
#include "mtlkwssa_drvinfo.h"
#include "mtlk_wss_debug.h"
#include "fw_recovery.h"
#include "wave_hal_stats.h"

#include "mtlk_psdb.h"

#include "mmb_ops.h"
#include "drvver.h"

#include "linux/netdevice.h"
#ifdef CPTCFG_IWLWAV_SET_PM_QOS
#include "linux/pm_qos.h"
#endif

#include "core.h" /* FIXME */
#include "channels.h"
#include "mtlk_dbg.h"

#include "mtlk_rtlog.h"
#ifdef CPTCFG_IWLWAV_PMCU_SUPPORT
#include "mtlk_pcoc.h"
#endif

#include "scan_support.h"
#include "wave_radio.h"
#include "mac80211.h"
#include "Statistics/Wave600B/Statistics_Descriptors.h"
#include "Statistics/Wave600/Statistics_Descriptors.h"

#define LOG_LOCAL_GID   GID_HWMMB
#define LOG_LOCAL_FID   1

/* Debug */
#ifdef MTLK_DEBUG
    /* A semicolon ';' at the end of line is required for proper LOG macro generation */
    #define TRACE_PARAM_INT(value)      do { ILOG2_SD("%s: 0x%08X", #value, (uint32)(value));                } while (0)
    #define TRACE_PARAM_PTR(value)      do { ILOG2_SP("%s: 0x%p",   #value, (value));                        } while (0)
    #define TRACE_PARAM_STR(value)      do { ILOG2_SS("%s: %s",     #value, (value));                        } while (0)
    #define TRACE_PARAM_MAC32(value)    do { ILOG2_SD("%s: 0x%08X", #value, MAC_TO_HOST32((uint32)(value))); } while (0)
#else
    #define TRACE_PARAM_INT(value)
    #define TRACE_PARAM_PTR(value)
    #define TRACE_PARAM_STR(value)
    #define TRACE_PARAM_MAC32(value)
#endif

#define MTLK_CHI_MAGIC_TIMEOUT_ASIC     5000 /* ms */
#define MTLK_CHI_MAGIC_TIMEOUT_EMUL   200000 /* ms */

#define MTLK_READY_CFM_TIMEOUT         10000 /* ms */
#define MTLK_SW_RESET_CFM_TIMEOUT      10000 /* ms */
#define MTLK_PRGMDL_LOAD_CHUNK_TIMEOUT 10000 /* ms */
#define MTLK_RX_BUFFS_RECOVERY_PERIOD  10000 /* ms */

#define BCL_ON_EXC_CTL_TIMEOUT           10 /* Timeout for SHARED_RAM_BCL_ON_EXCEPTION_CTL, in ms */
#define BCL_ON_EXC_CTL_LOOP_DELAY         5 /* Typical time for SHARED_RAM_BCL_ON_EXCEPTION_CTL to get executed, in microseconds */
                                            /* On GRX550 / GPB514H system a 5 microsecond delay is sufficient to get the result */
                                            /* ready at the first attempt in 99.995% of the cases*/

#define MTLK_MAX_RX_BUFFS_TO_RECOVER   ((uint16)-1) /* no limit */

#define RX_MAX_MSG_OFFSET       2    /* alignment offset from MAC. TODO: ???? */

#define HW_PCI_TXMM_MAX_MSGS 32
#define HW_PCI_TXDM_MAX_MSGS 2
#define HW_PCI_TXMM_GRANULARITY 1000
#define HW_PCI_TXDM_GRANULARITY 1000

#define HW_PCI_TXM_MAX_FAILS 5

#define DAT_CFM_ID_NONE      0xFF

#ifndef MTLK_RX_BUFF_ALIGNMENT
#define MTLK_RX_BUFF_ALIGNMENT 0     /* No special alignment required */
#endif

#ifndef HIBYTE
#define HIBYTE(s) ((uint8)((uint16)(s) >> 8))
#endif
#ifndef LOBYTE
#define LOBYTE(s) ((uint8)(s))
#endif

#define bswap_32(x) __swab32(x)

#define MTLK_OWNER_DRV 1
#define MTLK_OWNER_FW  0

#define STRING_IF_VERSIONS_SIZE 768
#define STRING_PROGMODEL_SIZE   128

#define FPGA_DMA_ADDR_MASK (0x08000000)

#define RX_DATA_QUE_POS 0
#define RX_MGMT_QUE_POS 1

#define BSS_MGMT_FRAME_SIZE     2348

#define RX_MGMT_ALIGN_SIZE      (16u) /* Gen6 buffer alignment for both Frame and RxMetrics */
#define RX_MGMT_ALIGN_MASK      (RX_MGMT_ALIGN_SIZE - 1)
#define RX_MGMT_ALIGNMENT(n)    ((n) + RX_MGMT_ALIGN_SIZE - ((n) & RX_MGMT_ALIGN_MASK))

#define G5_RX_MGMT_FRAME_SIZE   (BSS_MGMT_FRAME_SIZE + sizeof(RX_PHY_METRICS))
#define G6_RX_MGMT_FRAME_SIZE   (RX_MGMT_ALIGNMENT(BSS_MGMT_FRAME_SIZE) + sizeof(G6_RX_PHY_METRICS))
/* RX buffer: Offset, Frame and RxPhyMetrics. Reserve RX_MGMT_ALIGN_SIZE for the maximall offset */
#define RX_MGMT_ALLOC_SIZE      (RX_MGMT_ALIGN_SIZE + MAX(G5_RX_MGMT_FRAME_SIZE, G6_RX_MGMT_FRAME_SIZE))

#define MTLK_GENRISC_BASE_RELEASE_OFFSET 64
#define MTLK_GENRISC_CURR_RELEASE_OFFSET 68
#define MTLK_GENRISC_LOCAL_BINARY_FLAG   (1<<16)

#define G5_SHARED_RAM_OFFSET    0x00000000
#define G6_SHARED_RAM_OFFSET    0x00300000 /* SHARED_RAM_OFFSET_FROM_BAR0 from HwHeaders/Wave600/HwMemoryMap.h */

#define UNUSED_PARAM(_param) ((_param)=(_param))

#define MTLK_INTERRUPT_RECOVERY_PERIOD  50

#define MTLK_STATS_DATA_OFFSET(parameter, id) ((_chipid_is_gen6_a0(id)) ? \
(hw_stats->stats_data + (int)MTLK_OFFSET_OF(StatisticsDb_t_wave600, parameter)) \
: (hw_stats->stats_data + (int)MTLK_OFFSET_OF(StatisticsDb_t_wave600b, parameter)))

#define MTLK_STATS_COPY_OFFSET(parameter, id) ((_chipid_is_gen6_a0(id)) ? \
(hw_stats->stats_copy + (int)MTLK_OFFSET_OF(StatisticsDb_t_wave600, parameter)) \
: (hw_stats->stats_copy + (int)MTLK_OFFSET_OF(StatisticsDb_t_wave600b, parameter)))

typedef struct
{
  uint32 percentage;  /* percent */
  uint32 min_buffers; /* nof     */
  uint32 data_size;   /* bytes   */
} mtlk_hw_rx_queue_cfg_t;

typedef struct
{
  uint16 max_que_size;  /* Maximum queue size */
  uint16 que_size;      /* Current queue size */
} mtlk_hw_rx_queue_t;


/*****************************************************
 * IND/REQ BD-related definitions
 *****************************************************/
typedef struct
{
  uint32 offset; /* BD offset (PAS) */
  uint16 size;   /* BD size         */
  uint16 idx;    /* BD access index */
} mtlk_hw_bd_t;

typedef struct
{
  mtlk_hw_bd_t ind;
  mtlk_hw_bd_t req;
} mtlk_hw_ind_req_bd_t;
/*****************************************************/

/*****************************************************
 * Data Rx-related definitions
 *****************************************************/
typedef struct
{
  mtlk_hw_mirror_hdr_t hdr;     /* Header */
  union {
    mtlk_nbuf_t        *nbuf;   /* Network buffer (in SW path) */
    void*              *data;   /* In FastPath mode */
  };
  uint32               dma_addr;/* DMA mapped address */
  uint32               size;    /* Data buffer size */
  uint8                que_idx; /* Rx Queue Index */
  mtlk_lslist_entry_t  pend_l;  /* Pending list entry */
} mtlk_hw_data_ind_mirror_t;
/*****************************************************/

/*****************************************************
 * Logger-related definitions
 *****************************************************/
typedef struct
{
  mtlk_hw_mirror_hdr_t hdr;         /* Header */
  void*                virt_addr;   /* data buffer virtual address    */
  uint32               dma_addr;    /* data buffer DMA mapped address */
  uint16               bufsize;
} mtlk_hw_log_ind_mirror_t;
/*****************************************************/

#ifdef MTLK_LEGACY_STATISTICS

/*****************************************************
 * MHI_STATISTICS definitions
 *****************************************************/

typedef struct {
    uint16  type;
    uint16  subtype;
} mhi_stats_cfg_t;

typedef struct {
    uint16  start_offset;   /* offset of data block */
    uint16  next_idx_offs;  /* offset to next index */
} mhi_stats_offs_t;

typedef enum {
    STAT_HW_TX_POWER_MIN = 0,
    STAT_HW_TX_POWER_MAX,
    STAT_HW_TX_POWER_LAST
} mtlk_mhi_stat_tx_power_e;

typedef struct
{
  mtlk_osal_spinlock_t  lock;
  uint8                *db_data;    /* data block received from FW, in DMA area */
  uint8                *db_copy;    /* copy of db_data, in non-DMA area */
  StatsEntryDecoded_t  *table;      /* table */
  uint16                db_size;    /* data size */
  uint16                tab_size;   /* table size in bytes or entries */

  mtlk_hw_tx_power_t    tx_power[WAVE_CARD_RADIO_NUM_MAX];   /* min/max Tx power per BW and pw_size */

  mhi_stats_offs_t      stats_hw_tx_pw_offs_table[STAT_HW_TX_POWER_LAST];
  mhi_stats_offs_t      stats_vap_offs_table[STAT_VAP_TOTAL];
  mhi_stats_offs_t      stats_sta_offs_table[STAT_STA_CNTR_TOTAL];
  mhi_stats_offs_t      stats_tx_rate_offs_table[STAT_STA_TX_RATE_TOTAL];
  mhi_stats_offs_t      tx_time_offs;
  uint8                 stats_tx_rate_tables_dim2; /* second dimension for 2D arrays */
} hw_mhi_stats_t;
/*****************************************************/
#endif /* MTLK_LEGACY_STATISTICS */

/*****************************************************
 * PHY_RX_STATUS definitions
 *****************************************************/
typedef struct
{
  mtlk_osal_mutex_t   mutex;
  wholePhyRxStatusDb_t *db_data;
  uint16                db_size;
  uint16                all_sta_sid;
  uint16                max_sid;
  uint8                 noise_offs;
  uint8                 rssi_offs;
} hw_phy_rx_status_t;
/*****************************************************/

/*****************************************************
 * Control Messages (CM = MM and DM) Tx-related definitions
 *****************************************************/
typedef struct _mtlk_hw_cm_req_obj_t
{
  mtlk_hw_mirror_hdr_t hdr;        /* Header */
#ifdef CPTCFG_IWLWAV_DEBUG
  mtlk_atomic_t        usage_cnt;  /* message usage counter */
#endif
  UMI_MSG_HEADER       msg_hdr;
} mtlk_hw_cm_req_mirror_t;
/*****************************************************/

/*****************************************************
 * Control Messages (CM = MM and DM) Rx-related definitions
 * NOTE: msg member must be 1st in these structures because
 *       it is used for copying messages to/from PAS and
 *       buffers that are used for copying to/from PAS must
 *       be aligned to 32 bits boundary (see
 *       _mtlk_mmb_memcpy...() functions)
 *****************************************************/
typedef struct
{
  mtlk_hw_mirror_hdr_t hdr;
  UMI_MSG_HEADER       msg_hdr;
} mtlk_hw_cm_ind_mirror_t;
/*****************************************************/

/**************************************************************/
/* TODO: DEV_DF made external for init in DFG will be fixed */
mtlk_hw_mmb_t mtlk_mmb_obj;

/*****************************************************
 * Auxiliary BD ring-related definitions
 *****************************************************/
/********************************************************************
 * Number of BD descriptors
 * PAS offset of BD array (ring)
 * Local BD mirror (array)
*********************************************************************/
typedef struct {
  uint32  nof_bds;
  void   *iom_bdr_pos;
  uint16  iom_bd_size;
  void   *hst_bdr_mirror;
  uint16  hst_bd_size;
  MTLK_DECLARE_INIT_STATUS;
} mtlk_mmb_basic_bdr_t;

/* BSS Management path stuff */
struct mtlk_hw_bss_req_mirror
{
  mtlk_hw_mirror_hdr_t hdr; /* index and doubly linked list ptrs */
  uint64 cookie;
  uint8 *virt_addr; /* mgmt frame */
  uint32 dma_addr;
  uint32 size;      /* size of the valid data in the buffer, not the buffer size */
  uint32 extra_processing;
  uint8  vap_id;
  uint8  radio_id;
  uint8  subtype;
  uint8  action_code;
  BOOL   is_broadcast;
  IEEE_ADDR dst_addr;
  mtlk_nbuf_t *skb;
  BOOL   power_mgmt_on;
  uint32 tid;
};

struct mtlk_hw_bss_ind_mirror
{
  mtlk_hw_mirror_hdr_t  hdr;        /* index, etc. */
  uint32                dma_addr;
  uint8                 *virt_addr; /* mgmt frame */
  uint32 size;                      /* size of the buffer */
};

/* FW <--> DRV messages interface */

#define DESCR_TO_MSG(msg_, descr_) \
  msg_.index    = (uint16)MTLK_BFIELD_GET(descr_, HIM_DESC_INDEX); \
  msg_.radio_id = (uint8) MTLK_BFIELD_GET(descr_, HIM_DESC_RADIO); \
  msg_.vap_id   = (uint8) MTLK_BFIELD_GET(descr_, HIM_DESC_VAP);

#define MSG_TO_DESCR(descr_, type_, index_, radio_id_, vap_id_) \
  descr_ = MTLK_BFIELD_VALUE(HIM_DESC_TYPE, type_, uint32) | \
           MTLK_BFIELD_VALUE(HIM_DESC_INDEX, index_, uint32) | \
           MTLK_BFIELD_VALUE(HIM_DESC_RADIO, radio_id_, uint32) | \
           MTLK_BFIELD_VALUE(HIM_DESC_VAP, vap_id_, uint32);

typedef struct {
  uint16 index;
  uint8  radio_id;
  uint8  vap_id;
} him_msg_t;

/* HIM logger indication */
typedef struct {
  unsigned short index;
  unsigned short data_size;
} him_log_ind_msg_t;

/* Workaround: make correct alignment for HOST_DSC */
typedef struct _HOST_DSC __attribute__((aligned(4))) HOST_DSC_TYPE;
#define HOST_DSC HOST_DSC_TYPE

typedef struct
{
  uint32                  size;          /* ring size */
  uint32                  read_ptr;      /* ring read pointer */
  uint32                  write_ptr;     /* ring write pointer */
  HOST_DSC               *base_addr;     /* pointer to ring buffer for access*/
  dma_addr_t              dma_addr;      /* physical address of ring buffer */
  mtlk_ring_regs          regs;          /* registers in shared mem and their local copies */
} mtlk_ring_buffer;

typedef uint32 him_descr_t;

MTLK_INIT_STEPS_LIST_BEGIN(mmb_basic_bdr)
  MTLK_INIT_STEPS_LIST_ENTRY(mmb_basic_bdr, ALLOC_HST_BDR_MIRROR)
MTLK_INIT_INNER_STEPS_BEGIN(mmb_basic_bdr)
MTLK_INIT_STEPS_LIST_END(mmb_basic_bdr);

static mtlk_hw_msg_t* _mtlk_hw_get_msg_to_send(mtlk_hw_t *hw, mtlk_vap_handle_t vap_handle, uint32 *nof_free_tx_msgs)       __MTLK_INT_HANDLER_SECTION;
static int            _mtlk_hw_send_data(mtlk_hw_t *hw, mtlk_vap_handle_t vap_handle, mtlk_hw_data_req_mirror_t *data)      __MTLK_INT_HANDLER_SECTION;
static int            _mtlk_hw_release_msg_to_send(mtlk_hw_t *hw, mtlk_hw_msg_t *msg)                                       __MTLK_INT_HANDLER_SECTION;
static int            _mtlk_hw_set_prop(mtlk_hw_t *hw, mtlk_hw_prop_e prop_id, void *buffer, uint32 size);
static int            _mtlk_hw_get_prop(mtlk_hw_t *hw, mtlk_hw_prop_e prop_id, void *buffer, uint32 size)                   __MTLK_INT_HANDLER_SECTION;
static int            _mtlk_hw_load_file(mtlk_hw_t *hw, const char *name, mtlk_df_fw_file_buf_t *fb);
static void           _mtlk_hw_unload_file(mtlk_hw_t *hw, mtlk_df_fw_file_buf_t *fb);
static int            _mtlk_hw_get_info(mtlk_hw_t *card, char *hw_info, uint32 size);
static int            _mtlk_mmb_handle_sw_trap (mtlk_hw_t *hw);

static PMSG_OBJ __MTLK_IFUNC _txmm_msg_get_from_pool(mtlk_handle_t usr_data);
static PMSG_OBJ __MTLK_IFUNC _txdm_msg_get_from_pool(mtlk_handle_t usr_data);
static void     __MTLK_IFUNC _txmm_msg_free_to_pool(mtlk_handle_t usr_data, PMSG_OBJ pmsg);
static void     __MTLK_IFUNC _txdm_msg_free_to_pool(mtlk_handle_t usr_data, PMSG_OBJ pmsg);
static void     __MTLK_IFUNC _txmm_send(mtlk_handle_t usr_data, PMSG_OBJ pmsg, mtlk_vap_handle_t vap_handle);
static void     __MTLK_IFUNC _txdm_send(mtlk_handle_t usr_data, PMSG_OBJ pmsg, mtlk_vap_handle_t vap_handle);
static void     __MTLK_IFUNC _txm_msg_timed_out(mtlk_handle_t usr_data, uint16 msg_id);
#ifdef CPTCFG_IWLWAV_LEGACY_INT_RECOVERY_MON
static uint32   __MTLK_IFUNC _mtlk_hw_mmb_interrupt_recover_mon(mtlk_osal_timer_t *timer, mtlk_handle_t usr_data);
#endif

static void   _mtlk_mmb_put_hd_to_ring(mtlk_hw_t *hw, HOST_DSC *pHD, mtlk_ring_buffer *ring);

static int _hw_init_phy_rx_status (mtlk_hw_t *hw);
static void _hw_cleanup_phy_rx_status (mtlk_hw_t *hw);

static __INLINE mtlk_hw_mirror_hdr_t *
_mtlk_basic_bdr_get_mirror_bd_hdr (mtlk_mmb_basic_bdr_t *bbdr, uint32 index)
{
  MTLK_ASSERT(bbdr != NULL);
  MTLK_ASSERT(bbdr->nof_bds >= index);

  return (mtlk_hw_mirror_hdr_t *)&((uint8 *)bbdr->hst_bdr_mirror)[index * bbdr->hst_bd_size];
}

static __INLINE void*
__mtlk_basic_bdr_get_mirror_bd_safe (mtlk_mmb_basic_bdr_t *bbdr, uint32 index)
{
  MTLK_ASSERT(bbdr != NULL);
  MTLK_ASSERT(bbdr->nof_bds >= index);

  return &((uint8 *)bbdr->hst_bdr_mirror)[index * bbdr->hst_bd_size];
}

#define _mtlk_basic_bdr_get_mirror_bd(bbdr, index, type) \
  ((type *)__mtlk_basic_bdr_get_mirror_bd_safe((bbdr), (index)))

static __INLINE void *
__mtlk_basic_bdr_get_iom_bd_safe (mtlk_mmb_basic_bdr_t *bbdr, uint32 index, uint32 expected_iom_bd_size)
{
  MTLK_ASSERT(bbdr != NULL);
  MTLK_ASSERT(bbdr->nof_bds >= index);
  MTLK_ASSERT(bbdr->iom_bd_size == expected_iom_bd_size);

  return &((uint8 *)bbdr->iom_bdr_pos)[index * bbdr->iom_bd_size];
}

#define _mtlk_basic_bdr_get_iom_bd(bbdr, index, type) \
  __mtlk_basic_bdr_get_iom_bd_safe((bbdr), (index), sizeof(type))

static __INLINE BOOL
_mtlk_basic_bdr_contains_hst_bd (mtlk_mmb_basic_bdr_t *bbdr, const void *hst_bd)
{
  MTLK_ASSERT(bbdr != NULL);

  return (bbdr->hst_bdr_mirror <= hst_bd &&
          (bbdr->hst_bdr_mirror + bbdr->nof_bds * bbdr->hst_bd_size) > hst_bd);
}

static void
_mtlk_basic_bdr_cleanup (mtlk_mmb_basic_bdr_t *bbdr)
{
  MTLK_CLEANUP_BEGIN(mmb_basic_bdr, MTLK_OBJ_PTR(bbdr))
    MTLK_CLEANUP_STEP(mmb_basic_bdr, ALLOC_HST_BDR_MIRROR, MTLK_OBJ_PTR(bbdr),
                      mtlk_osal_mem_ddr_dma_pages_free, (bbdr->hst_bdr_mirror, bbdr->nof_bds * bbdr->hst_bd_size));
  MTLK_CLEANUP_END(mmb_basic_bdr, MTLK_OBJ_PTR(bbdr))
}

static int
_mtlk_basic_bdr_init (mtlk_mmb_basic_bdr_t *bbdr,
                      uint32                nof_bds,
                      uint8                *iom_bdr_pos,
                      uint16                iom_bd_size,
                      uint16                hst_bd_size)
{
  uint32 i;

  MTLK_ASSERT(bbdr != NULL);
  MTLK_ASSERT(nof_bds != 0);
  MTLK_ASSERT(iom_bd_size != 0);
  MTLK_ASSERT(hst_bd_size != 0);

  MTLK_INIT_TRY(mmb_basic_bdr, MTLK_OBJ_PTR(bbdr))
    MTLK_INIT_STEP_EX(mmb_basic_bdr, ALLOC_HST_BDR_MIRROR, MTLK_OBJ_PTR(bbdr),
                      (void *)mtlk_osal_mem_ddr_dma_pages_alloc, (nof_bds * hst_bd_size),
                      bbdr->hst_bdr_mirror, bbdr->hst_bdr_mirror != NULL, MTLK_ERR_NO_MEM);

    memset(bbdr->hst_bdr_mirror, 0, nof_bds * hst_bd_size);
    bbdr->nof_bds     = nof_bds;
    bbdr->iom_bdr_pos = iom_bdr_pos;
    bbdr->iom_bd_size = iom_bd_size;
    bbdr->hst_bd_size = hst_bd_size;
    for (i = 0; i < nof_bds; i++) {
      mtlk_hw_mirror_hdr_t *hdr = _mtlk_basic_bdr_get_mirror_bd_hdr(bbdr, i);
      hdr->index = i;
    }
  MTLK_INIT_FINALLY(mmb_basic_bdr, MTLK_OBJ_PTR(bbdr))
  MTLK_INIT_RETURN(mmb_basic_bdr, MTLK_OBJ_PTR(bbdr), _mtlk_basic_bdr_cleanup, (bbdr));
}

typedef struct {
  mtlk_mmb_basic_bdr_t basic;
  mtlk_dlist_t         free_list;
  mtlk_dlist_t         used_list;
  mtlk_osal_spinlock_t lock;
  MTLK_DECLARE_INIT_STATUS;
  MTLK_DECLARE_INIT_LOOP(REG_FREE);
} mtlk_mmb_advanced_bdr_t;

MTLK_INIT_STEPS_LIST_BEGIN(mmb_advanced_bdr)
  MTLK_INIT_STEPS_LIST_ENTRY(mmb_advanced_bdr, BASIC_BDR)
  MTLK_INIT_STEPS_LIST_ENTRY(mmb_advanced_bdr, LIST_LOCK)
  MTLK_INIT_STEPS_LIST_ENTRY(mmb_advanced_bdr, FREE_LIST)
  MTLK_INIT_STEPS_LIST_ENTRY(mmb_advanced_bdr, REG_FREE)
  MTLK_INIT_STEPS_LIST_ENTRY(mmb_advanced_bdr, USED_LIST)
MTLK_INIT_INNER_STEPS_BEGIN(mmb_advanced_bdr)
MTLK_INIT_STEPS_LIST_END(mmb_advanced_bdr);

static void
_mtlk_advanced_bdr_cleanup (mtlk_mmb_advanced_bdr_t *abdr)
{
  int i;
  MTLK_CLEANUP_BEGIN(mmb_advanced_bdr, MTLK_OBJ_PTR(abdr))
    MTLK_CLEANUP_STEP(mmb_advanced_bdr, USED_LIST, MTLK_OBJ_PTR(abdr),
                      mtlk_dlist_cleanup, (&abdr->used_list));
    for (i = 0; MTLK_CLEANUP_ITERATONS_LEFT(MTLK_OBJ_PTR(abdr), REG_FREE) > 0; i++) {
      /* Empty list to prevent ASSERT on cleanup */
      MTLK_CLEANUP_STEP_LOOP(mmb_advanced_bdr, REG_FREE, MTLK_OBJ_PTR(abdr),
                             mtlk_dlist_pop_front, (&abdr->free_list));
    }
    MTLK_CLEANUP_STEP(mmb_advanced_bdr, FREE_LIST, MTLK_OBJ_PTR(abdr),
                      mtlk_dlist_cleanup, (&abdr->free_list));
    MTLK_CLEANUP_STEP(mmb_advanced_bdr, LIST_LOCK, MTLK_OBJ_PTR(abdr),
                      mtlk_osal_lock_cleanup, (&abdr->lock));
    MTLK_CLEANUP_STEP(mmb_advanced_bdr, BASIC_BDR, MTLK_OBJ_PTR(abdr),
                      _mtlk_basic_bdr_cleanup, (&abdr->basic));
  MTLK_CLEANUP_END(mmb_advanced_bdr, MTLK_OBJ_PTR(abdr))
}

static int
_mtlk_advanced_bdr_init (mtlk_mmb_advanced_bdr_t *abdr,
                         uint32                   nof_bds,
                         uint8                   *iom_bdr_pos,
                         uint16                   iom_bd_size,
                         uint16                   hst_bd_size)
{
  uint32 i = 0;

  MTLK_ASSERT(abdr != NULL);

  ILOG1_PPDDD("abdr %p, iom %p, nof_bds %u, iom_size %u, hst_size %u",
              abdr, iom_bdr_pos, nof_bds, iom_bd_size, hst_bd_size);

  MTLK_INIT_TRY(mmb_advanced_bdr, MTLK_OBJ_PTR(abdr))
    MTLK_INIT_STEP(mmb_advanced_bdr, BASIC_BDR, MTLK_OBJ_PTR(abdr),
                   _mtlk_basic_bdr_init, (&abdr->basic, nof_bds, iom_bdr_pos, iom_bd_size, hst_bd_size))
    MTLK_INIT_STEP(mmb_advanced_bdr, LIST_LOCK, MTLK_OBJ_PTR(abdr),
                   mtlk_osal_lock_init, (&abdr->lock));
    MTLK_INIT_STEP_VOID(mmb_advanced_bdr, FREE_LIST, MTLK_OBJ_PTR(abdr),
                        mtlk_dlist_init, (&abdr->free_list));
    for (i = 0; i < abdr->basic.nof_bds; i++) {
      mtlk_hw_mirror_hdr_t *hdr = _mtlk_basic_bdr_get_mirror_bd_hdr(&abdr->basic, i);
      MTLK_INIT_STEP_VOID_LOOP(mmb_advanced_bdr, REG_FREE, MTLK_OBJ_PTR(abdr),
                               mtlk_dlist_push_back, (&abdr->free_list, &hdr->list_entry));
   }
    MTLK_INIT_STEP_VOID(mmb_advanced_bdr, USED_LIST, MTLK_OBJ_PTR(abdr),
                        mtlk_dlist_init, (&abdr->used_list));
  MTLK_INIT_FINALLY(mmb_advanced_bdr, MTLK_OBJ_PTR(abdr))
  MTLK_INIT_RETURN(mmb_advanced_bdr, MTLK_OBJ_PTR(abdr), _mtlk_advanced_bdr_cleanup, (abdr));
}
/*****************************************************/

typedef struct
{
  mtlk_lslist_t     lbufs; /* Rx Data Buffers to be re-allocated */
  mtlk_osal_timer_t timer; /* Recovery Timer */
} mtlk_hw_rx_pbufs_t; /* failed RX buffers allocations recovery */

typedef enum
{
  MTLK_ISR_NONE,
  MTLK_ISR_INIT_EVT,
  MTLK_ISR_MSGS_PUMP,
  MTLK_ISR_LAST
} mtlk_hw_mmb_isr_type_e;

/**************************************************************
    WSS counters for HW card statistics
 */
typedef enum
{
  /* Note: all HW source counters at first */
  MTLK_HW_SOURCE_CNT_FW_LOGGER_PACKETS_PROCESSED,
  MTLK_HW_SOURCE_CNT_FW_LOGGER_PACKETS_DROPPED,
  MTLK_HW_SOURCE_CNT_ISRS_TOTAL,
  MTLK_HW_SOURCE_CNT_ISRS_FOREIGN,
  MTLK_HW_SOURCE_CNT_ISRS_NOT_PENDING,
  MTLK_HW_SOURCE_CNT_ISRS_INIT,
  MTLK_HW_SOURCE_CNT_ISRS_TO_DPC,
  MTLK_HW_SOURCE_CNT_POST_ISR_DPCS,
  MTLK_HW_SOURCE_CNT_LEGACY_IND_RECEIVED,
  MTLK_HW_SOURCE_CNT_RX_BUF_ALLOC_FAILED,
  MTLK_HW_SOURCE_CNT_RX_BUF_REALLOC_FAILED,
  MTLK_HW_SOURCE_CNT_RX_BUF_REALLOCATED,
  MTLK_HW_SOURCE_CNT_BSS_RX_PACKETS_PROCESSED,
  MTLK_HW_SOURCE_CNT_BSS_RX_PACKETS_REJECTED,
#ifdef CPTCFG_IWLWAV_LEGACY_INT_RECOVERY_MON
  MTLK_HW_SOURCE_CNT_ISR_LOST_SUSPECT,
  MTLK_HW_SOURCE_CNT_ISR_LOST_RECOVER,
#endif
  MTLK_HW_SOURCE_CNT_LAST,

  /* HW listener counters */
  MTLK_HW_CNT_NOF_FAST_RCVRY_PROCESSED = MTLK_HW_SOURCE_CNT_LAST,
  MTLK_HW_CNT_NOF_FULL_RCVRY_PROCESSED,
  MTLK_HW_CNT_NOF_FAST_RCVRY_FAILED,
  MTLK_HW_CNT_NOF_FULL_RCVRY_FAILED,

  MTLK_HW_CARD_CNT_LAST
} mtlk_hw_source_wss_cnt_id_e;

/* Statistic ALLOWED flags */
#define MTLK_HW_SOURCE_CNT_FW_LOGGER_PACKETS_PROCESSED_ALLOWED      MTLK_WWSS_WLAN_STAT_ID_FW_LOGGER_PACKETS_PROCESSED_ALLOWED
#define MTLK_HW_SOURCE_CNT_FW_LOGGER_PACKETS_DROPPED_ALLOWED        MTLK_WWSS_WLAN_STAT_ID_FW_LOGGER_PACKETS_DROPPED_ALLOWED
#define MTLK_HW_SOURCE_CNT_ISRS_TOTAL_ALLOWED                       MTLK_WWSS_WLAN_STAT_ID_ISRS_TOTAL_ALLOWED
#define MTLK_HW_SOURCE_CNT_ISRS_FOREIGN_ALLOWED                     MTLK_WWSS_WLAN_STAT_ID_ISRS_FOREIGN_ALLOWED
#define MTLK_HW_SOURCE_CNT_ISRS_NOT_PENDING_ALLOWED                 MTLK_WWSS_WLAN_STAT_ID_ISRS_NOT_PENDING_ALLOWED
#define MTLK_HW_SOURCE_CNT_ISRS_INIT_ALLOWED                        MTLK_WWSS_WLAN_STAT_ID_ISRS_INIT_ALLOWED
#define MTLK_HW_SOURCE_CNT_ISRS_TO_DPC_ALLOWED                      MTLK_WWSS_WLAN_STAT_ID_ISRS_TO_DPC_ALLOWED
#define MTLK_HW_SOURCE_CNT_POST_ISR_DPCS_ALLOWED                    MTLK_WWSS_WLAN_STAT_ID_POST_ISR_DPCS_ALLOWED
#define MTLK_HW_SOURCE_CNT_LEGACY_IND_RECEIVED_ALLOWED              MTLK_WWSS_WLAN_STAT_ID_LEGACY_IND_RECEIVED_ALLOWED
#define MTLK_HW_SOURCE_CNT_RX_BUF_ALLOC_FAILED_ALLOWED              MTLK_WWSS_WLAN_STAT_ID_RX_BUF_ALLOC_FAILED_ALLOWED
#define MTLK_HW_SOURCE_CNT_RX_BUF_REALLOC_FAILED_ALLOWED            MTLK_WWSS_WLAN_STAT_ID_RX_BUF_REALLOC_FAILED_ALLOWED
#define MTLK_HW_SOURCE_CNT_RX_BUF_REALLOCATED_ALLOWED               MTLK_WWSS_WLAN_STAT_ID_RX_BUF_REALLOCATED_ALLOWED
#define MTLK_HW_SOURCE_CNT_BSS_RX_PACKETS_PROCESSED_ALLOWED         MTLK_WWSS_WLAN_STAT_ID_BSS_RX_PACKETS_PROCESSED_ALLOWED
#define MTLK_HW_SOURCE_CNT_BSS_RX_PACKETS_REJECTED_ALLOWED          MTLK_WWSS_WLAN_STAT_ID_BSS_RX_PACKETS_REJECTED_ALLOWED
#ifdef CPTCFG_IWLWAV_LEGACY_INT_RECOVERY_MON
#define MTLK_HW_SOURCE_CNT_ISR_LOST_SUSPECT_ALLOWED                 MTLK_WWSS_WLAN_STAT_ID_ISR_LOST_SUSPECT_ALLOWED
#define MTLK_HW_SOURCE_CNT_ISR_LOST_RECOVER_ALLOWED                 MTLK_WWSS_WLAN_STAT_ID_ISR_LOST_RECOVER_ALLOWED
#endif

/**************************************************************/

typedef struct
{
  uint32 buf_size;
  uint32 data_size;
  void*  data;
} mtlk_bss_rx_eapol;

struct mtlk_bss_management
{
  mtlk_mmb_advanced_bdr_t tx_bdr;  /* Tx BSS control */
  mtlk_mmb_advanced_bdr_t rx_bdr;  /* Rx BSS control */
  mtlk_mmb_advanced_bdr_t tx_res_bdr;  /* Tx BSS reserve control */
  mtlk_ring_buffer        rx_ring;
  mtlk_ring_buffer        tx_ring;
  mtlk_atomic_t           tx_ring_last_cfm_ts;
  uint32                  tx_bdr_max_used_bds; /* Maximal number of used REQ BD descriptors */
  uint32                  tx_res_bdr_max_used_bds; /* Maximal number of used reserved REQ BD descriptors */
  uint64                  cookie_counter;
  mtlk_bss_rx_eapol       rx_eapol; /* Struct for defragmentation of EAPOL frames */
#ifdef BD_DBG
  volatile unsigned long *used_bd_map;   /* bitmap of BD buffers, currently being processed within FW, one per BD */
#endif
};

struct mtlk_tx
{
  mtlk_mmb_advanced_bdr_t bdr_data;      /* Tx Data BDs */
  uint32                  nof_free_bds;  /* Number of free REQ BD descriptors */
  uint32                  max_used_bds;  /* Maximal number of used REQ BD descriptors */
  mtlk_ring_buffer        ring;          /* TX ring data */
#ifdef MTLK_DEBUG
  uint32                  max_received_cfm;      /* max received cfm for packets within one tasklet */
  uint64                  max_int_tasklet_time;  /* time between interrupt to tasklet */
  uint64                  max_int_processed_cfm; /* time between interrupt and cfm processed */
#endif
#ifdef BD_DBG
  volatile unsigned long *used_bd_map;   /* bitmap of BD buffers, currently being processed within FW, one per BD */
#endif
};

struct mtlk_rx
{
  mtlk_mmb_basic_bdr_t    bdr_data;      /* Rx Data BDs */
  mtlk_hw_rx_pbufs_t      pending;       /* Rx Data Buffers recovery */
  mtlk_ring_buffer        ring;          /* TX ring data */
  mtlk_nbuf_t            *first_fragment;/* fragments received and not yet sent up */
  mtlk_nbuf_t            *last_fragment; /* fragments received and not yet sent up */
#ifdef MTLK_DEBUG
  uint32                  max_received_pckts;       /* max received packets within one tasklet */
  uint64                  max_int_tasklet_time;     /* time between interrupt to tasklet */
  uint64                  max_int_processed_packet; /* time between interrupt to packet processed */
#endif
};

#ifdef CPTCFG_IWLWAV_SET_PM_QOS
typedef struct
{
    struct pm_qos_request pm_qos;
} mtlk_pm_qos_t;
#endif

struct mtlk_ctrl
{
  mtlk_ring_buffer        ring;          /* RX ctrl message ring data */
};

#define HW_FLAGS_PREPARED_MAN_REQ_BDR           0x00000001
#define HW_FLAGS_PREPARED_MAN_IND_BDR           0x00000002
#define HW_FLAGS_PREPARED_DBG_REQ_BDR           0x00000004
#define HW_FLAGS_PREPARED_DBG_IND_BDR           0x00000008
#define HW_FLAGS_PREPARED_DATA_REQ_BDR          0x00000010
#define HW_FLAGS_PREPARED_DATA_IND_BDR          0x00000020
#define HW_FLAGS_PREPARED_MGMT_IND_BDR          0x00000040
#define HW_FLAGS_PREPARED_LOG_IND_BDR           0x00000080
#define HW_FLAGS_PREPARED_RX_DATA_BUFFERS       0x00000100
#define HW_FLAGS_PREPARED_RX_MGMT_BUFFERS       0x00000200
#define HW_FLAGS_PREPARED_BSS_REQ_BDR           0x00000400

typedef struct {
  MTLK_DECLARE_START_STATUS;
} hw_start_final_t;

typedef struct {
  MTLK_DECLARE_START_STATUS;
} hw_start_eeprom_t;

typedef void (mtlk_isr_action_fn_t)(mtlk_hw_t *hw, mtlk_irq_handler_data *ihd);

typedef struct _mtlk_hw_mc_t
{
  mtlk_mc_addr_t mc_addr;
  uint32         ref_cnt;
} mtlk_hw_mc_t;


#if CPTCFG_IWLWAV_TRACER_HISTORY_LENGTH
/* tracer enum */
typedef enum _mtlk_tracer_id {
  MTLK_TR_NONE,
  MTLK_TR_INTR_ENTER,  /* interrupt handler enter */
  MTLK_TR_INTR_EXIT,   /* interrupt handler exit */
  MTLK_TR_INTR_DISABLE_INT,
  MTLK_TR_INTR_ENABLE_INT,
  MTLK_TR_INTR_CLEAR_INT,
  MTLK_TR_INTR_SCHED_TASKLET,   /* tasklet was scheduled */
  MTLK_TR_TASKLET_START,        /* tasklet was executed */
  MTLK_TR_TASKLET_STOP,         /* returned from tasklet */
  MTLK_TR_DATA_TXOUT_START,
  MTLK_TR_DATA_TXOUT_END,
  MTLK_TR_DATA_TXOUT_REM,
  MTLK_TR_DATA_RX_START,
  MTLK_TR_DATA_RX_END,
  MTLK_TR_DATA_RX_REM,
  MTLK_TR_BSS_RX_START,
  MTLK_TR_BSS_RX_END,
  MTLK_TR_BSS_RX_REM,
  MTLK_TR_BSS_CFM_START,
  MTLK_TR_BSS_CFM_END,
  MTLK_TR_BSS_CFM_REM,
  MTLK_TR_MSG_START,
  MTLK_TR_MSG_COUNT,
  MTLK_TR_MSG_END,
  MTLK_TR_MSG_REM,
  MTLK_TR_MSG_SEND_ID,        /* Sent message to FW */
  MTLK_TR_MSG_SEND_CFM_ID,    /* Received confirmation from FW for previously sent message */
  MTLK_TR_MSG_RECEIVE_ID,     /* Received message from FW */
  MTLK_TR_MSG_RECEIVE_CFM_ID, /* Sent confirmation to FW for previously received message */
  MTLK_TR_MSG_SEND_LEN,
  MTLK_TR_MSG_SEND_DESCR,
  MTLK_TR_COUNT
} mtlk_tracer_id;

static const char * _mtlk_tracer_idtostring(mtlk_tracer_id id)
{
#define RET_ID(id)  case id: return #id;

  switch (id)
  {
    RET_ID(MTLK_TR_NONE)
    RET_ID(MTLK_TR_INTR_ENTER)
    RET_ID(MTLK_TR_INTR_EXIT)
    RET_ID(MTLK_TR_INTR_DISABLE_INT)
    RET_ID(MTLK_TR_INTR_ENABLE_INT)
    RET_ID(MTLK_TR_INTR_CLEAR_INT)
    RET_ID(MTLK_TR_INTR_SCHED_TASKLET)
    RET_ID(MTLK_TR_TASKLET_START)
    RET_ID(MTLK_TR_TASKLET_STOP)
    RET_ID(MTLK_TR_DATA_TXOUT_START)
    RET_ID(MTLK_TR_DATA_TXOUT_END)
    RET_ID(MTLK_TR_DATA_TXOUT_REM)
    RET_ID(MTLK_TR_DATA_RX_START)
    RET_ID(MTLK_TR_DATA_RX_END)
    RET_ID(MTLK_TR_DATA_RX_REM)
    RET_ID(MTLK_TR_BSS_RX_START)
    RET_ID(MTLK_TR_BSS_RX_END)
    RET_ID(MTLK_TR_BSS_RX_REM)
    RET_ID(MTLK_TR_BSS_CFM_START)
    RET_ID(MTLK_TR_BSS_CFM_END)
    RET_ID(MTLK_TR_BSS_CFM_REM)
    RET_ID(MTLK_TR_MSG_START)
    RET_ID(MTLK_TR_MSG_COUNT)
    RET_ID(MTLK_TR_MSG_END)
    RET_ID(MTLK_TR_MSG_REM)
    RET_ID(MTLK_TR_MSG_SEND_ID)
    RET_ID(MTLK_TR_MSG_SEND_CFM_ID)
    RET_ID(MTLK_TR_MSG_RECEIVE_ID)
    RET_ID(MTLK_TR_MSG_RECEIVE_CFM_ID)
    RET_ID(MTLK_TR_MSG_SEND_LEN)
    RET_ID(MTLK_TR_MSG_SEND_DESCR)
    RET_ID(MTLK_TR_COUNT)
    default:
         return "unknown";
  }
#undef RET_ID
}

struct _mtlk_tracer_entry {
  uint32          value;
  uint32          ts_low;
  uint32          ts_high;
  mtlk_tracer_id  id;
};

typedef struct _mtlk_tracer_t {
  struct _mtlk_tracer_entry    *mem;
  mtlk_osal_spinlock_t         lock;
  unsigned                     idx;
  BOOL                         enabled;
  BOOL                         no_print;
}mtlk_tracer_t;
#endif /* #if CPTCFG_IWLWAV_TRACER_HISTORY_LENGTH */

/* HW card info */
typedef struct _mtlk_hw_card_info_t {
  mtlk_card_type_t  card_type;
  uint16            chip_id;
  uint8             hw_type;
  uint8             hw_vers; /* version/revision */
  BOOL              is_gen5;
  BOOL              is_gen6;
  BOOL              is_asic; /* ASIC i.e. real chip but not FPGA/Emul */
  BOOL              is_emul; /* Emulation: Palladium i.e. non ASIC */
  BOOL              is_fpga; /* FPGA */
  BOOL              is_phy_dummy; /* FALSE: Real, TRUE: Dummy */
} mtlk_hw_card_info_t;

typedef void (*mtlk_mmb_data_handler_fn_t) (mtlk_irq_handler_data *ihd);


typedef union {
  uint32 words[3];
  struct {
    uint32 band_cfg;
    uint32 dual_pcie_cfg;
    uint32 platform_type;
  } card_cfg;
} fw_chi_card_cfg_t;

struct _mtlk_hw_t
{
  mtlk_ccr_t            *ccr;
  mtlk_hw_mmb_t         *mmb;
  unsigned char         *mmb_base;
  unsigned char         *mmb_pas;
  unsigned char         *mmb_pas_phy;
  char                  fw_version[512];

  char                  if_version[STRING_IF_VERSIONS_SIZE];
  char                  progmodel[STRING_PROGMODEL_SIZE];

  uint8                 card_idx;

  /* TODO: maybe need it atomic or protected with locks */
  uint8                 rx_hd_ep_mask;     /* from HD: EP bits to Radio index */
  uint8                 rx_hd_vap_mask;    /* from HD: VAP bits to VAP id */
  wave_hw_radio_band_cfg_t radio_band_cfg; /* radio band configuration */
  fw_chi_card_cfg_t      fw_card_cfg;      /* card configuration for fw */

  uint8                  max_vaps_fw; /* FW supported */
  uint8                  max_vaps;    /* Radios config */
  uint16                 max_stas;

  mtlk_eeprom_data_t*   ee_data; /* EEPROM parsed data */

  mtlk_hw_card_info_t   card_info;

  /* mtlk_vap_manager_t    *vap_manager; */
  wave_radio_descr_t    *radio_descr;

  mtlk_hw_state_e        state;
  mtlk_irq_mode_e        irq_mode; /* Save mode of irqs for recovery */

  VECTOR_AREA            chi_area;

  void                   *calibr_buffer;  /* Calibration Extension related */
  uint32                 calibr_dma_addr;

  uint8                  fw_log_is_supported;    /* FW Logger Extension related */
  uint8                  fw_log_is_supported_ex;
  mtlk_mmb_basic_bdr_t   fw_log_buffers;        /* FW Logger Buffers Queue */

  mtlk_osal_spinlock_t   reg_lock;  /* used in IRQ as irqsave lock */
  mtlk_osal_spinlock_t   version_lock; /* used as spinlock to protect version info */
  volatile int           init_evt; /* used during the initialization */
  mtlk_isr_action_fn_t  *perform_isr_action;
  uint32                 last_irq_status;

  mtlk_hw_ind_req_bd_t   bds;     /* IND/REQ BD */

  struct mtlk_tx         tx;      /* DAT TX-related variables */
#ifdef MTLK_DEBUG
  mtlk_dbg_hres_ts_t     tx_interrupt_time;
#endif

  struct mtlk_ctrl       ctrl;    /* MSG CNTRL related variables */

  struct mtlk_rx         rx;      /* DAT RX-related variables */
#ifdef MTLK_DEBUG
  mtlk_dbg_hres_ts_t     rx_interrupt_time;
#endif

  mtlk_mmb_advanced_bdr_t tx_man;  /* Tx MM related */
  mtlk_mmb_basic_bdr_t    rx_man;  /* Rx MM related */
  mtlk_mmb_advanced_bdr_t tx_dbg;  /* Tx DM related */
  mtlk_mmb_basic_bdr_t    rx_dbg;  /* Rx DM related */

  struct mtlk_bss_management bss_mgmt; /* BSS management frame related things */

  mtlk_txmm_t            txmm;
  mtlk_txmm_t            txdm;

  mtlk_txmm_base_t       txmm_base;
  mtlk_txmm_base_t       txdm_base;
  hw_start_final_t       hw_start_fin;
  hw_start_eeprom_t      hw_start_eeprom;

  int                    mac_events_stopped; /* No INDs must be passed to Core except those needed to perform cleanup */
  int                    mac_events_stopped_completely; /* No INDs must be passed to Core at all*/
  BOOL                   mac_reset_logic_initialized;

  BOOL                   fast_path_is_available;
  BOOL                   dcdp_path_is_available;

  mtlk_irbd_t            *irbd;
  mtlk_wss_t             *wss;
  mtlk_wss_cntr_handle_t *wss_hcntrs[MTLK_HW_SOURCE_CNT_LAST];

#ifdef MTLK_LEGACY_STATISTICS
  /* todo: will be moved to radio module */
  hw_mhi_stats_t        hw_mhi_stats;
#endif

  /* todo: will be moved to radio module */
  hw_phy_rx_status_t    phy_rx_status;

  hw_statistics_t       hw_stats;

  hw_psdb_t             psdb;

  uint8                 tx_ant_num;    /* Number of TX antennas */
  uint8                 rx_ant_num;    /* Number of RX antennas */
  uint8                 tx_ant_mask;   /* TX antennas mask */
  uint8                 rx_ant_mask;   /* RX antennas mask */

  uint32                flags;
  mtlk_atomic_t         radars_detected;

  mtlk_mmb_data_handler_fn_t data_txout_process;
  mtlk_mmb_data_handler_fn_t data_rx_process;

#if MTLK_USE_DIRECTCONNECT_DP_API
  mtlk_dcdp_dev_t       dp_dev;
#endif

  /* consider using struct tasklet_limits from mtlk_coreui.h here? */
  unsigned              data_txout_lim;
  unsigned              data_rx_lim;
  unsigned              bss_rx_lim;
  unsigned              bss_cfm_lim;
  unsigned              legacy_lim;

  mtlk_osal_spinlock_t  mc_group_lock;                    /* multicast_group lock */
  mtlk_hw_mc_t          mc_groups[MC_GROUPS];
#if CPTCFG_IWLWAV_TRACER_HISTORY_LENGTH
  mtlk_tracer_t         tracer;
#endif

#ifdef CPTCFG_IWLWAV_SET_PM_QOS
  mtlk_pm_qos_t         pm_qos;                           /* To control wakeup latency */
#endif

  /* SID for logger, taken from insmod param, but kept here for fast runtime access */
  uint16 logger_sid;

#ifdef CPTCFG_IWLWAV_LEGACY_INT_RECOVERY_MON
  BOOL                 need_legacy_int_recovery;         /* Flag to recover lost legacy interrupt */
  mtlk_osal_timer_t    recover_timer;                    /* Recovery monitor timer */
#endif

  /* counters for BSS RX/TX tasklets execution */
  unsigned              bss_rx_zero_hds_num;
  unsigned              bss_cfm_num;
  unsigned              bss_ind_num;

  uint32               stats_poll_period;

  mtlk_channel_stats chan_statistics[NUM_TOTAL_CHANS + 1];

  MTLK_DECLARE_INIT_STATUS;
  MTLK_DECLARE_START_STATUS;
};

static __INLINE wave_radio_t *
__mtlk_hw_wave_radio_get (mtlk_hw_t *hw, wave_uint idx)
{
    return wave_radio_descr_wave_radio_get(hw->radio_descr, idx);
}

#if CPTCFG_IWLWAV_TRACER_HISTORY_LENGTH
static int
MTLK_TRACER_INIT(mtlk_hw_t *hw)
{
  hw->tracer.mem = (struct _mtlk_tracer_entry*) mtlk_osal_mem_alloc(sizeof(struct _mtlk_tracer_entry) * CPTCFG_IWLWAV_TRACER_HISTORY_LENGTH,
                      MTLK_MEM_TAG_LOGGER);
  if (hw->tracer.mem == NULL)
    return MTLK_ERR_NO_MEM;

  hw->tracer.idx = 0;
  hw->tracer.enabled = FALSE;
  hw->tracer.no_print = FALSE;

  memset(hw->tracer.mem, 0, sizeof(struct _mtlk_tracer_entry) * CPTCFG_IWLWAV_TRACER_HISTORY_LENGTH);

  if(MTLK_ERR_OK != mtlk_osal_lock_init(&hw->tracer.lock)) {
    goto error;
  }

  WLOG_D("TRACER enabled (history length: %u)", CPTCFG_IWLWAV_TRACER_HISTORY_LENGTH);
  return MTLK_ERR_OK;

error:
  mtlk_osal_mem_free(hw->tracer.mem);
  return MTLK_ERR_UNKNOWN;
}

static int
MTLK_TRACER_CLEAN(mtlk_hw_t *hw)
{
  mtlk_osal_lock_cleanup(&hw->tracer.lock);
  if (hw->tracer.mem) {
      mtlk_osal_mem_free(hw->tracer.mem);
      hw->tracer.mem = NULL;
  }
  return MTLK_ERR_OK;
}

static void
MTLK_TRACER_ADD_ENTRY(mtlk_hw_t *hw, mtlk_tracer_id id, uint32 value)
{
  mtlk_handle_t lock;
  struct _mtlk_tracer_entry *entry;

  lock = mtlk_osal_lock_acquire_irq(&hw->tracer.lock);
  if (hw->tracer.enabled) {
    entry = &hw->tracer.mem[hw->tracer.idx];
    entry->ts_low = mtlk_log_get_timestamp();
    entry->value = value;
    entry->id = id;

    if(++hw->tracer.idx >= CPTCFG_IWLWAV_TRACER_HISTORY_LENGTH)
      hw->tracer.idx = 0;
  }
  mtlk_osal_lock_release_irq(&hw->tracer.lock, lock);
}

static void
MTLK_TRACER_START(mtlk_hw_t *hw)
{
  mtlk_handle_t lock;

  lock = mtlk_osal_lock_acquire_irq(&hw->tracer.lock);
  hw->tracer.enabled = TRUE;
  mtlk_osal_lock_release_irq(&hw->tracer.lock, lock);
}

static void
MTLK_TRACER_STOP(mtlk_hw_t *hw)
{
  mtlk_handle_t lock;

  lock = mtlk_osal_lock_acquire_irq(&hw->tracer.lock);
  hw->tracer.enabled = FALSE;
  mtlk_osal_lock_release_irq(&hw->tracer.lock, lock);
}

static void
MTLK_TRACER_PRINT(mtlk_hw_t *hw)
{
  struct _mtlk_tracer_entry *entry;
  unsigned i;
  signed idx;

  if(hw->tracer.no_print)
      return;
  MTLK_TRACER_STOP(hw);
  idx = hw->tracer.idx;

  ELOG_V("TRACER PRINT HISTORY");
  for (i=0; i < CPTCFG_IWLWAV_TRACER_HISTORY_LENGTH; i++)
  {
    idx--;
    if(idx < 0)
      idx =  CPTCFG_IWLWAV_TRACER_HISTORY_LENGTH - 1;

    entry = &hw->tracer.mem[idx];
    ELOG_DDDDDS("idx:%04x ID:%02d value:%08X ts:%08x%08x, %s", i, entry->id, entry->value, entry->ts_high, entry->ts_low, _mtlk_tracer_idtostring(entry->id));
  }

  hw->tracer.no_print = TRUE;
}

#else
#define MTLK_TRACER_START(hw)
#define MTLK_TRACER_STOP(hw)
#define MTLK_TRACER_PRINT(hw)
#define MTLK_TRACER_ADD_ENTRY(hw, id, value)
#endif /* CPTCFG_IWLWAV_TRACER_HISTORY_LENGTH */

static const uint32 _mtlk_hw_card_wss_id_map[] =
{
  /* Note: all HW source counters at first */
  MTLK_WWSS_WLAN_STAT_ID_FW_LOGGER_PACKETS_PROCESSED, /* MTLK_HW_SOURCE_CNT_FW_LOGGER_PACKETS_PROCESSED */
  MTLK_WWSS_WLAN_STAT_ID_FW_LOGGER_PACKETS_DROPPED,   /* MTLK_HW_SOURCE_CNT_FW_LOGGER_PACKETS_DROPPED */
  MTLK_WWSS_WLAN_STAT_ID_ISRS_TOTAL,                  /* MTLK_HW_SOURCE_CNT_ISRS_TOTAL */
  MTLK_WWSS_WLAN_STAT_ID_ISRS_FOREIGN,                /* MTLK_HW_SOURCE_CNT_ISRS_FOREIGN */
  MTLK_WWSS_WLAN_STAT_ID_ISRS_NOT_PENDING,            /* MTLK_HW_SOURCE_CNT_ISRS_NOT_PENDING */
  MTLK_WWSS_WLAN_STAT_ID_ISRS_INIT,                   /* MTLK_HW_SOURCE_CNT_ISRS_INIT */
  MTLK_WWSS_WLAN_STAT_ID_ISRS_TO_DPC,                 /* MTLK_HW_SOURCE_CNT_ISRS_TO_DPC */
  MTLK_WWSS_WLAN_STAT_ID_POST_ISR_DPCS,               /* MTLK_HW_SOURCE_CNT_POST_ISR_DPCS */
  MTLK_WWSS_WLAN_STAT_ID_LEGACY_IND_RECEIVED,         /* MTLK_HW_SOURCE_CNT_LEGACY_IND_RECEIVED */
  MTLK_WWSS_WLAN_STAT_ID_RX_BUF_ALLOC_FAILED,         /* MTLK_HW_SOURCE_CNT_RX_BUF_ALLOC_FAILED */
  MTLK_WWSS_WLAN_STAT_ID_RX_BUF_REALLOC_FAILED,       /* MTLK_HW_SOURCE_CNT_RX_BUF_REALLOC_FAILED */
  MTLK_WWSS_WLAN_STAT_ID_RX_BUF_REALLOCATED,          /* MTLK_HW_SOURCE_CNT_RX_BUF_REALLOCATED */
  MTLK_WWSS_WLAN_STAT_ID_BSS_RX_PACKETS_PROCESSED,    /* MTLK_HW_SOURCE_CNT_BSS_RX_PACKETS_PROCESSED */
  MTLK_WWSS_WLAN_STAT_ID_BSS_RX_PACKETS_REJECTED,     /* MTLK_HW_SOURCE_CNT_BSS_RX_PACKETS_REJECTED */
#ifdef CPTCFG_IWLWAV_LEGACY_INT_RECOVERY_MON
  MTLK_WWSS_WLAN_STAT_ID_ISR_LOST_SUSPECT,            /* MTLK_HW_SOURCE_ISR_LOST_SUSPECT */
  MTLK_WWSS_WLAN_STAT_ID_ISR_LOST,                    /* MTLK_HW_SOURCE_ISR_LOST_RECOVER */
#endif

  /* HW listener counters */
  MTLK_WWSS_WLAN_STAT_ID_NOF_FAST_RCVRY_PROCESSED,    /* MTLK_HW_CNT_NOF_FAST_RCVRY_PROCESSED */
  MTLK_WWSS_WLAN_STAT_ID_NOF_FULL_RCVRY_PROCESSED,    /* MTLK_HW_CNT_NOF_FULL_RCVRY_PROCESSED */
  MTLK_WWSS_WLAN_STAT_ID_NOF_FAST_RCVRY_FAILED,       /* MTLK_HW_CNT_NOF_FAST_RCVRY_FAILED */
  MTLK_WWSS_WLAN_STAT_ID_NOF_FULL_RCVRY_FAILED,       /* MTLK_HW_CNT_NOF_FULL_RCVRY_FAILED */
};

/**********************************************************************
 **********************************************************************/
/* with checking ALLOWED */
#define _mtlk_mmb_hw_inc_cnt(hw, id)    { if (id##_ALLOWED) __mtlk_mmb_hw_inc_cnt(hw, id); }

static __INLINE void
__mtlk_mmb_hw_inc_cnt(mtlk_hw_t                  *hw,
                     mtlk_hw_source_wss_cnt_id_e cnt_id)
{
  MTLK_ASSERT(cnt_id >= 0 && cnt_id < MTLK_HW_SOURCE_CNT_LAST);

  mtlk_wss_cntr_inc(hw->wss_hcntrs[cnt_id]);
}

/**********************************************************************
 * INIT event: impemented as flag + sleep
 * NOTE: can't be OSAL event because of SET from ISR (OSAL limitation)
 **********************************************************************/
#define MTLK_HW_INIT_EVT_STEP_MS      20 /* INIT_EVT WAIT resolution */

static __INLINE int
MTLK_HW_INIT_EVT_INIT (mtlk_hw_t *hw)
{
  hw->init_evt = 0;
  return MTLK_ERR_OK;
}

static __INLINE void
MTLK_HW_INIT_EVT_SET (mtlk_hw_t *hw)
{
  hw->init_evt = 1;
}

static __INLINE void
MTLK_HW_INIT_EVT_RESET (mtlk_hw_t *hw)
{
  hw->init_evt = 0;
}

static __INLINE void
MTLK_HW_INIT_EVT_CLEANUP (mtlk_hw_t *hw)
{
}

static __INLINE int
MTLK_HW_INIT_EVT_WAIT (mtlk_hw_t *hw, uint32 msec)
{
  int res = MTLK_ERR_UNKNOWN;

  while (1) {
    if (hw->init_evt) {
      res = MTLK_ERR_OK;
      break;
    }
    else if (msec < MTLK_HW_INIT_EVT_STEP_MS) {
      res = MTLK_ERR_TIMEOUT;
      break;
    }
    else {
      mtlk_osal_msleep(MTLK_HW_INIT_EVT_STEP_MS);
      msec -= MTLK_HW_INIT_EVT_STEP_MS;
    }
  }

  return res;
}

void isr_action_init (mtlk_hw_t *hw, mtlk_irq_handler_data *ihd)
{
  (void)mtlk_ccr_clear_interrupts(hw->ccr, MTLK_IRQ_LEGACY);
  MTLK_HW_INIT_EVT_SET(hw); /* Interrupts will be enabled by bootstrap code */
  _mtlk_mmb_hw_inc_cnt(hw, MTLK_HW_SOURCE_CNT_ISRS_INIT);
}

void isr_action_normal (mtlk_hw_t *hw, mtlk_irq_handler_data *ihd)
{
  _mtlk_mmb_hw_inc_cnt(hw, MTLK_HW_SOURCE_CNT_ISRS_TO_DPC);
  /* Interrupts will be enabled by tasklet */
  mtlk_mmb_drv_postpone_irq_handler(ihd);
  MTLK_TRACER_ADD_ENTRY(hw, MTLK_TR_INTR_SCHED_TASKLET, 0);
}

/**********************************************************************/

static __INLINE BOOL
_hw_type_is_gen5_b11 (mtlk_hw_t *hw)
{
  MTLK_ASSERT(hw);
  return _chipid_is_gen5_b11(hw->ccr->chip_info->id);
}

static __INLINE BOOL
_hw_type_is_gen5 (mtlk_hw_t *hw)
{
  MTLK_ASSERT(hw);
  return _chipid_is_gen5(hw->ccr->chip_info->id);
}

BOOL __MTLK_IFUNC
mtlk_hw_type_is_gen5(mtlk_hw_t *hw)
{
    return _hw_type_is_gen5(hw);
}

static __INLINE BOOL
_hw_type_is_gen6 (mtlk_hw_t *hw)
{
  MTLK_ASSERT(hw);
  return _chipid_is_gen6(hw->ccr->chip_info->id);
}

BOOL __MTLK_IFUNC
mtlk_hw_type_is_gen6(mtlk_hw_t *hw)
{
    return _hw_type_is_gen6(hw);
}

static __INLINE BOOL
_hw_type_is_gen6_a0 (mtlk_hw_t *hw)
{
  return _chipid_is_gen6_b0(hw->ccr->chip_info->id);
}

BOOL __MTLK_IFUNC
mtlk_hw_type_is_gen6_a0 (mtlk_hw_t *hw)
{
  MTLK_ASSERT(hw);
  return _hw_type_is_gen6_a0(hw);
}

static __INLINE BOOL
_hw_type_is_gen6_b0 (mtlk_hw_t *hw)
{
  return _chipid_is_gen6_b0(hw->ccr->chip_info->id);
}

BOOL __MTLK_IFUNC
mtlk_hw_type_is_gen6_b0 (mtlk_hw_t *hw)
{
  MTLK_ASSERT(hw);
  return _hw_type_is_gen6_b0(hw);
}

static __INLINE BOOL
_hw_type_is_gen6_d2 (mtlk_hw_t *hw)
{
  return _chipid_is_gen6_d2(hw->ccr->chip_info->id);
}

BOOL __MTLK_IFUNC
mtlk_hw_type_is_gen6_d2 (mtlk_hw_t *hw)
{
  MTLK_ASSERT(hw);
  return _hw_type_is_gen6_d2(hw);
}

static __INLINE unsigned
_hw_num_cores (mtlk_hw_t *hw)
{
  MTLK_ASSERT(hw);
  /* todo: should we add define for 2 in shram.h? */
  return _chipid_is_gen5(hw->ccr->chip_info->id) ? 2 : MAX_NUM_OF_FW_CORES;
}

unsigned __MTLK_IFUNC
hw_num_cores (mtlk_hw_t *hw)
{
  return _hw_num_cores(hw);
}

int __MTLK_IFUNC
hw_assert_type_to_core_nr(mtlk_hw_t *hw, mtlk_core_ui_dbg_assert_type_e assert_type)
{
  switch (assert_type) {
    case MTLK_CORE_UI_ASSERT_TYPE_FW_LMIPS0:
      return FW_CORE0;
      break;
    case MTLK_CORE_UI_ASSERT_TYPE_FW_LMIPS1:
      if (_hw_type_is_gen6(hw))
        return FW_CORE1;
      else
        return -1;
      break;
    case MTLK_CORE_UI_ASSERT_TYPE_FW_UMIPS:
      if (_hw_type_is_gen6(hw))
        return FW_CORE2;
      else
        return FW_CORE1;
      break;
    default:
      return -1;
  }
}

#define _mtlk_mmb_pas_writel(hw, comment, index, v)                 \
  for(;;) {                                                         \
    ILOG6_S("Write PAS: %s", comment);                    \
    mtlk_writel((v),(hw)->mmb_pas + (uint32)(index));     \
	break;															\
  }

#define _mtlk_mmb_pas_readl(hw, comment, index, v)                  \
  for(;;) {                                                         \
    ILOG6_S("Read PAS: %s", comment);                     \
    (v) = mtlk_readl((hw)->mmb_pas + (uint32)(index));    \
	break;														    \
  }

static __INLINE int
_mtlk_mmb_memcpy_fromio (void       *to,
                         const void *from,
                         uint32      count)
{
  if ((((mtlk_handle_t)to | (mtlk_handle_t)from | count) & 0x3) == 0) {
    while (count) {
      *((uint32 *)to) = mtlk_raw_readl(from);
      from   = ((uint8 *)from) + 4;
      to     = ((uint8 *)to) + 4;
      count -= 4;
    }
    return 1;
  }
  else {
    ELOG_PPD("Unaligned access (to=0x%p, from=0x%p, size=%d)",
          to, from, count);
    MTLK_ASSERT(FALSE);
    return 0;
  }
}

void __MTLK_IFUNC
wave_hw_mmb_memcpy_fromio (void *to, void *mmb_base, uint32 off, uint32 count)
{
  (void)_mtlk_mmb_memcpy_fromio(to, mmb_base + off, count);
}

uint8 * __MTLK_IFUNC
mtlk_hw_get_mmb_io_data (mtlk_hw_t *hw, uint32 off)
{
  return hw->mmb_base + off;
}

static __INLINE mtlk_vap_handle_t
_mtlk_mmb_get_vap_handle_from_vap_id (mtlk_hw_t *hw, unsigned radio_idx, uint8 vap_id)
{
  mtlk_vap_handle_t vap_handle = NULL;

  /* Note that vap_hadle will not changed in case of error, so just ignore return code */
  mtlk_vap_manager_get_vap_handle(wave_radio_descr_vap_manager_get(hw->radio_descr, radio_idx),
                                  vap_id, &vap_handle);

  return vap_handle;
}

static void _mtlk_mmb_log_error_radio_not_exist(const char *msg, uint32 radio_id)
{
    ELOG_SD("%s: RadioID %u does not exist", msg, radio_id);
}

static void _mtlk_mmb_log_error_vap_not_exist(const char *msg, uint32 radio_id, uint32 vap_id)
{
    ELOG_SDD("%s: RadioID %u VapID %u does not exist", msg, radio_id, vap_id);
}

static void _mtlk_mmb_log_error_vap_not_ready(const char *msg, uint32 radio_id, uint32 vap_id)
{
    ILOG2_SDD("%s: RadioID %u VapID %u is not ready", msg,  radio_id, vap_id);
}

static __INLINE BOOL
_mtlk_mmb_get_vap_is_not_ready (mtlk_hw_t *hw, unsigned radio_idx, uint8 vap_id)
{
  return mtlk_vap_manager_vap_is_not_ready(wave_radio_descr_vap_manager_get(hw->radio_descr, radio_idx), vap_id);
}

static __INLINE uint8
_mtlk_mmb_get_vap_id_from_vap_handle (mtlk_vap_handle_t vap_handle)
{
  uint8  vap_index = 0;
  if (vap_handle) {
    vap_index = mtlk_vap_get_id(vap_handle);
  }
  MTLK_ASSERT(vap_index <= MTLK_BFIELD_VALUE(HIM_DESC_VAP, -1, uint8));
  return vap_index;
}


#define _mtlk_mmb_memcpy_toio(to, from, count) \
        _mtlk_mmb_memcpy_toio_swap(to, from, count, FALSE)

static __INLINE int
_mtlk_mmb_memcpy_toio_swap (void *to, const void *from, uint32 count, BOOL swap)
{
  MTLK_ASSERT(NULL != to);
  MTLK_ASSERT(NULL != from);

  if ((((mtlk_handle_t)to | (mtlk_handle_t)from | count) & 0x3) == 0) {
    if (swap) {
      while (count) {
        mtlk_raw_writel(bswap_32(*(uint32 *)from), to);
        from   = ((uint8 *)from) + 4;
        to     = ((uint8 *)to) + 4;
        count -= 4;
      }
    }
    else {
      while (count) {
        mtlk_raw_writel(*(uint32 *)from, to);
        from   = ((uint8 *)from) + 4;
        to     = ((uint8 *)to) + 4;
        count -= 4;
      }
    }
    return 1;
  }
  else {
    ELOG_PPD("Unaligned access (to=0x%p, from=0x%p, size=%d)",
          to, from, count);
    MTLK_ASSERT(FALSE);
    return 0;
  }
}

#ifdef CPTCFG_IWLWAV_FW_WRITE_VALIDATION

static __INLINE int
_mtlk_mmb_memcpy_validate_toio (void *to, const void *from, uint32 count, int swap)
{
  uint32 test_value;
  uint32 validation_errors = 0;

  ILOG0_DPP("Validating memory chunk write of size %u, from %p, to %p", count, from, to);

  if(_mtlk_mmb_memcpy_toio_swap(to, from, count, swap)) {
    while (count) {
      test_value = mtlk_raw_readl(to);
      if (swap) {
        test_value = bswap_32(test_value);
      }

      if(test_value != *(uint32 *)from) {
        if (validation_errors < 34) { /* don't print all */
          ILOG0_PDD("Write validation error at %p: written %08X, read %08X", to, *(uint32 *)from, test_value);
        }
        validation_errors++;
      }

      from   = ((uint8 *)from) + 4;
      to     = ((uint8 *)to) + 4;
      count -= 4;
    }

    ILOG0_DD("Amount of write validation errors: %u (%u bytes)", validation_errors, 4 * validation_errors);
    return 1;
  }

  return 0;
}

#endif /* CPTCFG_IWLWAV_FW_WRITE_VALIDATION */

static __INLINE int
_mtlk_mmb_memcpy_toio_no_pll (mtlk_hw_t  *hw,
                              void       *to,
                              const void *from,
                              uint32     count)
{
  if ((((mtlk_handle_t)to | (mtlk_handle_t)from | count) & 0x3) == 0) {
    while (count) {
      if (hw->mmb->cfg.no_pll_write_delay_us) {
        mtlk_udelay(hw->mmb->cfg.no_pll_write_delay_us);
      }
      mtlk_raw_writel(*(uint32 *)from, to);
      from   = ((uint8 *)from) + 4;
      to     = ((uint8 *)to) + 4;
      count -= 4;
    }
    return 1;
  }
  else {
    ELOG_PPD("Unaligned access (to=0x%p, from=0x%p, size=%d)",
          to, from, count);
    MTLK_ASSERT(FALSE);
    return 0;
  }
}

#define _mtlk_mmb_pas_get(hw, comment, index, ptr, n) \
  _mtlk_mmb_memcpy_fromio((ptr), (hw)->mmb_pas + (index), (n))
#define _mtlk_mmb_pas_put(hw, comment, index, ptr, n) \
  _mtlk_mmb_memcpy_toio((hw)->mmb_pas + (index), (ptr), (n))

/* currently not used; please note byte swapping parameter is off */
#ifdef CPTCFG_IWLWAV_FW_WRITE_VALIDATION
#define _mtlk_mmb_pas_put_validate(hw, comment, index, ptr, n) \
  _mtlk_mmb_memcpy_validate_toio((hw)->mmb_pas + (index), (ptr), (n), FALSE)
#endif

static int _mtlk_mmb_send_msg (mtlk_hw_t *hw, him_descr_t descriptor);

static void txmm_on_cfm(mtlk_hw_t *hw, PMSG_OBJ pmsg);
static void txdm_on_cfm(mtlk_hw_t *hw, PMSG_OBJ pmsg);

static void _mtlk_mmb_free_unconfirmed_tx_buffers(mtlk_hw_t *hw);

#define DATA_REQ_MIRROR_PTR(msg) ((mtlk_hw_data_req_mirror_t *)(msg))
#define MAN_IND_MIRROR_PTR(msg)  ((mtlk_hw_man_ind_mirror_t *)(msg))
#define MAN_DBG_MIRROR_PTR(msg)  ((mtlk_hw_dbg_ind_mirror_t *)(msg))

static mtlk_vap_manager_t *
_hw_vap_manager_get(mtlk_hw_t *hw, unsigned radio_idx)
{
  return wave_radio_descr_vap_manager_get((hw)->radio_descr, radio_idx);
}


#if MTLK_RX_BUFF_ALIGNMENT
static __INLINE mtlk_nbuf_t *
_mtlk_mmb_nbuf_alloc (uint32 size)
{
  mtlk_nbuf_t *nbuf = mtlk_df_nbuf_alloc(size);
  if (nbuf) {
    /* Align skbuffer if required by HW */
    mtlk_handle_t tail = ((mtlk_handle_t)mtlk_df_nbuf_get_virt_addr(nbuf)) &
                         (MTLK_RX_BUFF_ALIGNMENT - 1);
    if (tail) {
      mtlk_handle_t nof_pad_bytes = MTLK_RX_BUFF_ALIGNMENT - tail;
      mtlk_df_nbuf_reserve(nbuf, (uint32)nof_pad_bytes);
    }
  }

  return nbuf;
}
#else
#define _mtlk_mmb_nbuf_alloc(size)  mtlk_df_nbuf_alloc((size))
#endif
#define _mtlk_mmb_nbuf_free(nbuf)   mtlk_df_nbuf_free(nbuf)

/* Convert card index to wlan interface index of the master AP VAP
 * for hw idx 0/1/2 interface names are wlan0/wlan2/wlan4 respectively
 * AP interfaces use the even numbers while sta interfaces use the odd indexes */
static __INLINE int
card_idx_to_master_vap_idx (int card_idx) {

  return card_idx * 2;
}

#if MTLK_USE_DIRECTCONNECT_DP_API

static __INLINE BOOL
__mtlk_mmb_cntr_big_endian_supported (mtlk_hw_t *hw)
{
     return (_hw_type_is_gen5_b11(hw) || _hw_type_is_gen6(hw));
}

static __INLINE BOOL
__mtlk_mmb_fastpath_supported(mtlk_hw_t *hw)
{
     return (_hw_type_is_gen5(hw) || _hw_type_is_gen6(hw));
}

static __INLINE BOOL
__mtlk_mmb_fastpath_available(mtlk_hw_t *hw)
{
    return hw->fast_path_is_available;
}

static __INLINE BOOL
__mtlk_mmb_dcdp_path_available(mtlk_hw_t *hw)
{
    /*
    return  (MTLK_DC_DP_MODE_UNREGISTERED  != hw->dp_dev.dp_mode) &&
            (MTLK_DC_DP_MODE_UNKNOWN       != hw->dp_dev.dp_mode) &&
            (MTLK_DC_DP_MODE_LITEPATH_ONLY != hw->dp_dev.dp_mode);
            */
    return hw->dcdp_path_is_available;
}

BOOL __MTLK_IFUNC
mtlk_mmb_fastpath_available(mtlk_hw_t *hw)
{
    return __mtlk_mmb_fastpath_available(hw);
}

BOOL __MTLK_IFUNC
mtlk_mmb_dcdp_path_available (mtlk_hw_t *hw)
{
    return __mtlk_mmb_dcdp_path_available(hw);
}

BOOL __MTLK_IFUNC
mtlk_mmb_dcdp_frag_wa_enabled(mtlk_hw_t *hw)
{
    return hw->dp_dev.dp_frag_wa_enable;
}

#else /* MTLK_USE_DIRECTCONNECT_DP_API */

#define __mtlk_mmb_fastpath_supported(hw)  (FALSE)
#define __mtlk_mmb_fastpath_available(hw)  (FALSE)
#define __mtlk_mmb_dcdp_path_available(hw) (FALSE)
/* mtlk_mmb_fastpath_available() and mtlk_mmb_fastpath_available() are defined in hw_mmb.h */

#endif /* MTLK_USE_DIRECTCONNECT_DP_API */

static __INLINE char* find_print_position(char *text, int* bytes_left) {
  int   len = strlen(text);
  char* print_position = text + len;
  *bytes_left -= len;
  return print_position;
}

static void
get_firmware_version (mtlk_hw_t       *hw,
                      int             cpu,
                      const char      *fname,
                      const char      *data,
                      unsigned long    size)
{
  static const char MAC_VERSION_SIGNATURE[] = "@@@ VERSION INFO @@@";
  const char *border = data + size;
  char *text = hw->if_version;
  char *print_position;
  int bytes_left = STRING_IF_VERSIONS_SIZE;

  mtlk_osal_lock_acquire(&hw->version_lock);
  if ((CHI_CPU_NUM_TX == cpu) || (CHI_CPU_NUM_IF == cpu) || (CHI_CPU_NUM_RX == cpu)) {
      uint32 base_release, curr_release;
      base_release = LE32_TO_CPU(*(uint32*)(data + MTLK_GENRISC_BASE_RELEASE_OFFSET));
      curr_release = LE32_TO_CPU(*(uint32*)(data + MTLK_GENRISC_CURR_RELEASE_OFFSET));
      do {
          print_position = text;
          print_position = find_print_position(print_position, &bytes_left);
          if (bytes_left <= 0) break;
          mtlk_snprintf(print_position, bytes_left, "%s: base release %u, curr.release %u", fname,
                  base_release, curr_release);
          print_position = find_print_position(print_position, &bytes_left);
          if (bytes_left <= 0) break;
          mtlk_snprintf(print_position, bytes_left, "%s\n",
                 (curr_release & MTLK_GENRISC_LOCAL_BINARY_FLAG)? "(unofficial)" : "");
          text[STRING_IF_VERSIONS_SIZE - 1] = '\0';
      } while(0);
  }
  else
  {   /* if cpu == CHI_CPU_NUM_UM or CHI_CPU_NUM_LM */
      data = mtlk_osal_str_memchr(data, '@', border - data);
      while (data) {
        if (strncmp(data, MAC_VERSION_SIGNATURE, ARRAY_SIZE(MAC_VERSION_SIGNATURE)-1) == 0) {
          print_position = find_print_position(text, &bytes_left);
          if (bytes_left <= 0)
            break;
          mtlk_snprintf(print_position, bytes_left, "%s: %s\n", fname, data);
          text[STRING_IF_VERSIONS_SIZE - 1] = '\0';
          break;
        }
        data = mtlk_osal_str_memchr(data + 1, '@', border - data - 1);
      }
  }
  mtlk_osal_lock_release(&hw->version_lock);
}

static int
_mtlk_mmb_notify_firmware(mtlk_hw_t *hw,
                          int cpu,
                          const char *fname,
                          const mtlk_df_fw_file_buf_t* fb)
{
  get_firmware_version(hw, cpu, fname, fb->buffer, fb->size);
  return MTLK_ERR_OK;
}

static mtlk_hw_data_req_mirror_t *
_mtlk_mmb_get_msg_from_data_pool(mtlk_hw_t *hw, mtlk_vap_handle_t vap_handle)
{
  mtlk_hw_data_req_mirror_t *data_req = NULL;
  uint32 nof_used_bds;

  mtlk_osal_lock_acquire(&hw->tx.bdr_data.lock);
  if (mtlk_dlist_size(&hw->tx.bdr_data.free_list)) {
    mtlk_dlist_entry_t *node = mtlk_dlist_pop_front(&hw->tx.bdr_data.free_list);

    if (__LIKELY(NULL != node)) {
      data_req = MTLK_LIST_GET_CONTAINING_RECORD(node,
                                                 mtlk_hw_data_req_mirror_t,
                                                 hdr.list_entry);
      hw->tx.nof_free_bds--;

      nof_used_bds = hw->tx.bdr_data.basic.nof_bds - hw->tx.nof_free_bds;
      if (nof_used_bds > hw->tx.max_used_bds)
        hw->tx.max_used_bds = nof_used_bds;

      data_req->vap_id = mtlk_vap_get_id(vap_handle);

      /* add to the "used" list */
      mtlk_dlist_push_back(&hw->tx.bdr_data.used_list, &data_req->hdr.list_entry);
    }
  }
  mtlk_osal_lock_release(&hw->tx.bdr_data.lock);

  ILOG4_PD("got msg %p, %d free msgs", data_req, hw->tx.nof_free_bds);

  return data_req;
}

static int
_mtlk_mmb_free_sent_msg_to_data_pool(mtlk_hw_t                 *hw,
                                     mtlk_hw_data_req_mirror_t *data_req)
{
  mtlk_osal_lock_acquire(&hw->tx.bdr_data.lock);
  /* remove from the "used" list */
  mtlk_dlist_remove(&hw->tx.bdr_data.used_list,
                    &data_req->hdr.list_entry);
  /* add to the "free" list */
  mtlk_dlist_push_back(&hw->tx.bdr_data.free_list,
                       &data_req->hdr.list_entry);
  hw->tx.nof_free_bds++;
  mtlk_osal_lock_release(&hw->tx.bdr_data.lock);

  ILOG4_DD("%u msg freed, %u free msgs", data_req->hdr.index, hw->tx.nof_free_bds);

  return hw->tx.nof_free_bds;
}

static __INLINE uint32
_mtlk_mmb_cm_bdr_get_iom_bd_ind_size (BOOL is_man)
{
  return is_man ? sizeof(SHRAM_IND_MSG) : sizeof(SHRAM_DBG_MSG);
}

static __INLINE uint32
_mtlk_mmb_cm_bdr_get_iom_bd_req_size (BOOL is_man)
{
  return is_man ? sizeof(SHRAM_MAN_MSG) : sizeof(SHRAM_DBG_MSG);
}

static __INLINE mtlk_mmb_basic_bdr_t *
_mtlk_mmb_cm_get_ind_bbdr (mtlk_hw_t *hw,
                           BOOL       is_man)
{
  return is_man?&hw->rx_man:&hw->rx_dbg;
}

static __INLINE mtlk_mmb_basic_bdr_t *
_mtlk_mmb_cm_get_req_bbdr (mtlk_hw_t *hw,
                           BOOL       is_man)
{
  return is_man?&hw->tx_man.basic:&hw->tx_dbg.basic;
}

static __INLINE void *
_mtlk_mmb_cm_bdr_get_iom_bd (mtlk_mmb_basic_bdr_t *bdr,
                             uint32                index,
                             uint32                iom_size)
{
  return __mtlk_basic_bdr_get_iom_bd_safe(bdr, index, iom_size);
}

static __INLINE void *
_mtlk_mmb_cm_get_mirror_bd (mtlk_mmb_basic_bdr_t *bdr,
                            uint32                index)
{
  return __mtlk_basic_bdr_get_mirror_bd_safe(bdr, index);
}

#define _mtlk_mmb_cm_ind_get_mirror_bd(b, i) \
  ((mtlk_hw_cm_ind_mirror_t *)_mtlk_mmb_cm_get_mirror_bd((b), (i)))

#define _mtlk_mmb_cm_req_get_mirror_bd(b, i) \
  ((mtlk_hw_cm_req_mirror_t*)_mtlk_mmb_cm_get_mirror_bd((b), (i)))

static void
_mtlk_mmb_resp_cm_ind (mtlk_hw_t                     *hw,
                       BOOL                           is_man,
                       const mtlk_hw_cm_ind_mirror_t *cm_ind,
                       unsigned radio_id, unsigned vap_id)
{
  uint32 descr;
  uint32 iom_size = _mtlk_mmb_cm_bdr_get_iom_bd_ind_size(is_man);
  void  *iom      = _mtlk_mmb_cm_bdr_get_iom_bd(_mtlk_mmb_cm_get_ind_bbdr(hw, is_man), cm_ind->hdr.index, iom_size);

  _mtlk_mmb_memcpy_toio(iom, &cm_ind->msg_hdr, iom_size);

  MSG_TO_DESCR(descr, (is_man?ARRAY_MAN_IND:ARRAY_DBG_IND), cm_ind->hdr.index, radio_id, vap_id);
  _mtlk_mmb_send_msg(hw, descr);
}

static __INLINE
void __wave_hw_rx_hd_decode(mtlk_hw_t *hw, uint8 ep_bits, uint8 vap_bits, uint8 *radio_id, uint8 *vap_id);

/* FIXME: should be taken from FW headers */
#define TX_DATA_BD_INDEX    MTLK_BFIELD_INFO(0, 16) /* 16 bits starting bit0  */

#define HD_BD_INDEX_GET(hd) (MTLK_BFIELD_GET(LE32_TO_CPU((hd)->u32BdIndex), TX_DATA_BD_INDEX))

static __INLINE void
_mtlk_mmb_handle_dat_cfm (mtlk_hw_t *hw,
                          HOST_DSC  *hd)
{
  mtlk_hw_data_req_mirror_t  *data_req;
  uint32 u32BdIndex    = HD_BD_INDEX_GET(hd);
  uint32 u32frameInfo0 = LE32_TO_CPU(hd->u32frameInfo0);
  uint32 u32frameInfo1 = LE32_TO_CPU(hd->u32frameInfo1);
  uint8  vap_id, radio_id;
  mtlk_vap_handle_t    vap_handle;

  __wave_hw_rx_hd_decode(hw,
                         MTLK_BFIELD_GET(u32frameInfo1, TX_DATA_INFO_EP),
                         MTLK_BFIELD_GET(u32frameInfo0, TX_DATA_INFO_VAPID),
                         &radio_id,
                         &vap_id);

#ifdef BD_DBG
  if (__UNLIKELY(u32BdIndex >= hw->tx.ring.size)) {
    ELOG_DD("Out of range BD index returned on DAT CFM: %d read pointer 0x%08X", u32BdIndex, hw->tx.ring.read_ptr);
    mtlk_dump(0, hd, sizeof(HOST_DSC), "HD in error");
    mtlk_hw_set_prop(wave_radio_descr_hw_api_get(hw->radio_descr, radio_id), MTLK_HW_RESET, NULL, 0);
    return; /*BUG();*/
  }

  if (__UNLIKELY(0 == test_and_clear_bit(u32BdIndex, hw->tx.used_bd_map))) {
    ELOG_D("Unused BD returned on CFM, index %d\n", u32BdIndex);
    goto the_trap;
  }
#endif

  /* TODO: use shift instead of multiplication */
  data_req = _mtlk_basic_bdr_get_mirror_bd(&hw->tx.bdr_data.basic, u32BdIndex, mtlk_hw_data_req_mirror_t);

  if (__UNLIKELY(_mtlk_mmb_get_vap_is_not_ready(hw, radio_id, vap_id))) {
    _mtlk_mmb_log_error_vap_not_ready(__FUNCTION__, radio_id, vap_id);
    goto skip_process;
  }

  vap_handle = _mtlk_mmb_get_vap_handle_from_vap_id(hw, radio_id, vap_id);
  if (__UNLIKELY(!vap_handle)) {
    _mtlk_mmb_log_error_vap_not_exist(__FUNCTION__, radio_id, vap_id);
    goto skip_process;
  }

#ifdef BD_DBG
  if (radio_id != data_req->radio_id) {
    ELOG_DD("Received data frame cfm from wrong RADIO:%d bss_req_RADIO:%d", radio_id, data_req->radio_id);
    goto the_trap;
  }
  if (__UNLIKELY(vap_id != data_req->vap_id)) {
    ELOG_DD("Received data frame cfm from wrong VapID:%u bss_req_VapID:%u", vap_id, data_req->vap_id);
    goto the_trap;
  }
#endif


  if (data_req->dma_addr) { /* NULL keep-alive packets are not mapped */
    ILOG5_DDD("handling CFM: index %d, payload %08X, HD payload %08X", u32BdIndex, data_req->dma_addr, LE32_TO_CPU(hd->u32HostPayloadAddr));
      mtlk_df_nbuf_unmap_phys_addr(mtlk_ccr_get_dev_ctx(hw->ccr),
                                   data_req->nbuf,
                                   data_req->dma_addr,
                                   data_req->size,
                                   MTLK_DATA_TO_DEVICE);
  }

  /* at this time status is not returned by FW */
  data_req->status          = (UMI_STATUS)UMI_OK; //MTLK_BFIELD_GET(info, IND_REQ_TX_STATUS);
  /*  data.nof_retries     = MTLK_BFIELD_GET(info, IND_REQ_NUM_RETRANSMISSIONS); */
  /* TODO at this time not returned, pretend everything is perfect */
  data_req->nof_retries     = 0;

  mtlk_vap_get_core_vft(vap_handle)->release_tx_data(vap_handle, data_req);

skip_process:
  /* TODO: get rid of used list */
  _mtlk_mmb_free_sent_msg_to_data_pool(hw, data_req);
  return;

the_trap:
  ELOG_DDDD("internal variables: tx_read_ptr %X tx_write_ptr %X tx_IN_copy %X tx_int_OUT_cntr %X", hw->tx.ring.read_ptr, hw->tx.ring.write_ptr, hw->tx.ring.regs.IN_copy, hw->tx.ring.regs.int_OUT_cntr);
  mtlk_dump(0, hd, sizeof(HOST_DSC), "HD in error");
  /*mtlk_dump(0, hw->tx.ring, hw->tx.ring_size * sizeof(HOST_DSC), "Dump of the whole HD ring");*/
  mtlk_hw_set_prop(wave_radio_descr_hw_api_get(hw->radio_descr, radio_id), MTLK_HW_RESET, NULL, 0);
  return; /*BUG();*/
}

#if (IWLWAV_RTLOG_FLAGS & RTLF_REMOTE_ENABLED)

static __inline int
_mtlk_mmb_push_logger_buf(mtlk_hw_t *hw,
                          void* buff_addr,
                          uint16 buff_size)
{
  uint8 *pdata = NULL;
  mtlk_log_buf_entry_t *pbuf = mtlk_log_new_pkt_reserve(buff_size, &pdata);

  if (NULL != pbuf) {
    MTLK_ASSERT(NULL != pdata);
    wave_memcpy(pdata, buff_size, buff_addr, buff_size);
    mtlk_log_new_pkt_release(pbuf);
    _mtlk_mmb_hw_inc_cnt(hw, MTLK_HW_SOURCE_CNT_FW_LOGGER_PACKETS_PROCESSED);
    return MTLK_ERR_OK;
  }
  else {
    _mtlk_mmb_hw_inc_cnt(hw, MTLK_HW_SOURCE_CNT_FW_LOGGER_PACKETS_DROPPED);
    return MTLK_ERR_NO_MEM;
  }
}

#endif

static __INLINE int
_mtlk_mmb_handle_logger_buf_indication(mtlk_hw_t *hw, him_descr_t descriptor)
{
  uint32 descr;
  int res = MTLK_ERR_OK;

#if 0 /* WAVE600: logger does not fit to new HIM, it should use Data path. */
  him_log_ind_msg_t log_ind_msg;
  him_log_ind_msg_t *p_ind = &log_ind_msg;

#if (IWLWAV_RTLOG_FLAGS & RTLF_REMOTE_ENABLED)
  mtlk_hw_log_ind_mirror_t *log_ind;
  uint16 log_data_size;
#endif

  DESCR_TO_LOG_IND(log_ind_msg, descriptor);

/* Just drop packet in case of RTLF_REMOTE_ENABLED is not set */
#if (IWLWAV_RTLOG_FLAGS & RTLF_REMOTE_ENABLED)
  log_ind = _mtlk_basic_bdr_get_mirror_bd(&hw->fw_log_buffers,
                                          p_ind->index,
                                          mtlk_hw_log_ind_mirror_t);
  log_data_size = p_ind->data_size;

  mtlk_osal_unmap_phys_addr(mtlk_ccr_get_dev_ctx(hw->ccr),
                            log_ind->dma_addr,
                            log_data_size,
                            MTLK_DATA_FROM_DEVICE);

  ILOG4_DD("log_ind: recv size %d, dma_addr 0x%08X", log_data_size, log_ind->dma_addr);

  res = _mtlk_mmb_push_logger_buf(hw, log_ind->virt_addr, log_data_size);

  MTLK_ASSERT(0 != log_ind->bufsize);

  log_ind->dma_addr = mtlk_osal_map_to_phys_addr(mtlk_ccr_get_dev_ctx(hw->ccr),
                                                 log_ind->virt_addr,
                                                 log_ind->bufsize,
                                                 MTLK_DATA_FROM_DEVICE);

  ILOG4_DPD("log_ind: dma_addr 0x%08X, virt_addr 0x%p, size %d", log_ind->dma_addr, log_ind->virt_addr, log_ind->bufsize);

  MTLK_ASSERT(0 != log_ind->dma_addr);
#endif
  /* prepare response */
  MSG_TO_DESCR(descr, ARRAY_DAT_LOGGER_IND, p_ind->index, 0, 0);
#else /* #if 0*/
  /* prepare response */
  descr = descriptor;
#endif /* #if 0*/

  /* Send response */
  _mtlk_mmb_send_msg(hw, descr);
  return res;
}

static __INLINE void
_mtlk_mmb_rx_free_all_fragments(mtlk_hw_t *hw)
{
  mtlk_nbuf_t *nbuf;

  while (hw->rx.first_fragment) {
    nbuf = hw->rx.first_fragment->next;
    mtlk_df_nbuf_free(hw->rx.first_fragment);
    hw->rx.first_fragment = nbuf;
  }
  hw->rx.last_fragment = NULL;
}

#define FRAG_FIRST ((1 << 1) | 0) /* SoP == 1, EoP == 0 */
#define FRAG_LAST  ((0 << 1) | 1) /* SoP == 0, EoP == 1 */
#define FRAG_MID   ((0 << 1) | 0) /* SoP == 0, EoP == 0 */
#define NO_FRAG    ((1 << 1) | 1) /* SoP == 1, EoP == 1 */

#define DBG_DUMP_HD     0   /* FIXME: HDs debug */
#define DBG_HW_LOGGER   0   /* FIXME: HW Logger */
#define OWN_ITER_CNTR   100

static __INLINE int _mtlk_mmb_handle_dat_ind (mtlk_hw_t *hw, volatile HOST_DSC *pHD)
{
  int                        res = MTLK_ERR_UNKNOWN;
  uint32                     u32frameInfo3;
  uint32                     u32frameInfo1;
  uint32                     u32frameInfo0;
  uint32                     u32BdIndex;
  uint32                     hostpayload;
  uint32                     offset;
  uint32                     data_size;
  uint32                     total_size;
  mtlk_core_handle_rx_data_t data;
  mtlk_hw_data_ind_mirror_t *data_ind;
  mtlk_vap_handle_t          vap_handle;
  uint8                      vap_id;
  uint8                      radio_id;
  uint8                      frame_type;
  uint8                      frag_type;

  u32frameInfo3 = LE32_TO_CPU(pHD->u32frameInfo3);
  if (__UNLIKELY(MTLK_OWNER_FW == MTLK_BFIELD_GET(u32frameInfo3, TX_DATA_INFO_OWN_BIT))) {
    /* Loop some time in case HD is not written yet */
    int i = OWN_ITER_CNTR;
    do {
      u32frameInfo3 = LE32_TO_CPU(pHD->u32frameInfo3);
      if (MTLK_OWNER_FW != MTLK_BFIELD_GET(u32frameInfo3, TX_DATA_INFO_OWN_BIT))
        break;
    } while (--i);

    if (i == 0) {
      ELOG_V("HD Owner bit error");
      mtlk_hw_set_prop(wave_radio_descr_hw_api_get(hw->radio_descr, 0), MTLK_HW_RESET, NULL, 0);
      return MTLK_ERR_UNKNOWN;
    }
  }

  u32BdIndex    = HD_BD_INDEX_GET(pHD);
  u32frameInfo0 = LE32_TO_CPU(pHD->u32frameInfo0);
  u32frameInfo1 = LE32_TO_CPU(pHD->u32frameInfo1);
  hostpayload   = LE32_TO_CPU(pHD->u32HostPayloadAddr);
  offset        = MTLK_BFIELD_GET(u32frameInfo3, TX_DATA_INFO_OFFSET);
  data_size     = MTLK_BFIELD_GET(u32frameInfo3, TX_DATA_INFO_LENGTH);
  frame_type    = MTLK_BFIELD_GET(u32frameInfo0, TX_DATA_INFO_FRAMETYPE);
  frag_type     = MTLK_BFIELD_GET(u32frameInfo3, TX_DATA_INFO_SOP_EOP);

#ifdef BD_DBG
  if (__UNLIKELY(u32BdIndex >= hw->rx.bdr_data.nof_bds)) {
    ELOG_DD("Index out of bounds: %d >= %d", u32BdIndex, hw->rx.bdr_data.nof_bds);
    ELOG_DDDD("internal variables: rx_read_ptr %X rx_write_ptr %X rx_IN_copy %X rx_int_OUT_cntr %X", hw->rx.ring.read_ptr, hw->rx.ring.write_ptr, hw->rx.ring.regs.IN_copy, hw->rx.ring.regs.int_OUT_cntr);
    mtlk_dump(0, (void *)pHD, sizeof(HOST_DSC), "HD in error");
    /* WAVE600: TODO: do it for the first radio now, but rework later! */
    mtlk_hw_set_prop(wave_radio_descr_hw_api_get(hw->radio_descr, 0), MTLK_HW_RESET, NULL, 0);
    return MTLK_ERR_UNKNOWN; /* BUG(); */
  }
#endif

  __wave_hw_rx_hd_decode(hw,
                         MTLK_BFIELD_GET(u32frameInfo1, TX_DATA_INFO_EP),
                         MTLK_BFIELD_GET(u32frameInfo0, TX_DATA_INFO_VAPID),
                         &radio_id,
                         &vap_id);

  data_ind = _mtlk_basic_bdr_get_mirror_bd(&hw->rx.bdr_data, u32BdIndex, mtlk_hw_data_ind_mirror_t);

  MTLK_ASSERT(data_size < data_ind->size);

  ILOG6_DD("RX IND: al=%d ro=%d", (int)data_size, (int)offset);
  ILOG6_DD("radio_index %d vap_index %d", radio_id, vap_id);

  /* On Gen6, a real offset is 4 bits wide, but HD "offset" field contains only 3 bits.
     We need to extract MSB from the HD u32HostPayloadAddr */
  if (_hw_type_is_gen6(hw)) {
    offset += (0x08 & hostpayload);
    /* offset += (0x08 & (hostpayload ^ data_ind->dma_addr); */
  }

  data.nbuf     = data_ind->nbuf;

  if(MTLK_BFIELD_GET(u32frameInfo0, TX_DATA_INFO_UNKNOWN_SID)) {
    data.sid = DB_UNKNOWN_SID;
  } else {
    data.sid = MTLK_BFIELD_GET(u32frameInfo0, TX_DATA_INFO_STAID);
  }

  total_size    = offset + data_size;

#if DBG_DUMP_HD
  {
    ILOG3_DDDDDD("HD: RadioID %u VapID %u, StaID %u, offset %u, data_size %u, nbuf.len %u",
                  radio_id, vap_id, data.sid, offset, data_size, data.nbuf->len);

    mtlk_dump(3, (void *)pHD, sizeof(HOST_DSC), "HD dump");
    mtlk_dump(4, data.nbuf->data, MIN(80, total_size), "nbuf.data");
  }
#endif /* DBG_DUMP_HD */

  mtlk_df_nbuf_unmap_phys_addr(mtlk_ccr_get_dev_ctx(hw->ccr),
                               data_ind->nbuf,
                               data_ind->dma_addr,
                               total_size,
                               MTLK_DATA_FROM_DEVICE);

  if (__UNLIKELY(total_size > mtlk_df_nbuf_get_tail_room_size(data_ind->nbuf))) {
    ELOG_DDDD("CID-%02x: (offset + data_size) > nbuf size --> (%d + %d) > %d", hw->card_idx,
              offset, data_size, mtlk_df_nbuf_get_tail_room_size(data_ind->nbuf));
    goto buf_return;
  }

  mtlk_df_nbuf_put(data.nbuf, total_size);  /* offset + data_size */
  mtlk_df_nbuf_pull(data.nbuf, offset);

  vap_handle = _mtlk_mmb_get_vap_handle_from_vap_id(hw, radio_id, vap_id);

  /* HW LOGGER */
  if (hw->logger_sid == data.sid) {
#if DBG_HW_LOGGER
    ILOG3_DDDDDD("HD: RadioID %u VapID %u, StaID %u, offset %u, data_size %u, nbuf.len %u",
                radio_id, vap_id, data.sid, offset, data_size, data.nbuf->len);
    mtlk_dump(3, (void *)pHD, sizeof(HOST_DSC), "HD dump");
    mtlk_dump(4, data.nbuf->data, MIN(80, data.nbuf->len), "nbuf.data");
#endif
    mtlk_rtlog_forward_log_packet(data.nbuf);
    res = MTLK_ERR_OK;
    goto _after_parse;
  }

  if (__UNLIKELY(_mtlk_mmb_get_vap_is_not_ready(hw, radio_id, vap_id))) {
    _mtlk_mmb_log_error_vap_not_ready(__FUNCTION__, radio_id, vap_id);
    mtlk_df_nbuf_push(data.nbuf, offset);
    mtlk_df_nbuf_trim(data.nbuf, 0);
    goto buf_return;
  }

  if (__LIKELY(vap_handle))
  {
    data.priority = MTLK_BFIELD_GET(u32frameInfo1, TX_DATA_INFO_CLASS);

    switch (frag_type)
    {
      case FRAG_FIRST: if (hw->rx.first_fragment) {
                         _mtlk_mmb_rx_free_all_fragments(hw);
                         ELOG_V("Received first fragment while already having fragments");
                       }
                       hw->rx.first_fragment = hw->rx.last_fragment = data_ind->nbuf;
                       res = MTLK_ERR_OK;
                       break;

      case FRAG_LAST:  {
                         uint32 total_size = 0, pos = 0;
                         mtlk_nbuf_t *nbuf, *comb_nbuf;

                         if (!hw->rx.first_fragment) {
                           ELOG_V("Received last fragment without having first");
                           res = MTLK_ERR_NOT_IN_USE;
                           break;
                         }

                         hw->rx.last_fragment->next = data_ind->nbuf;
                         hw->rx.last_fragment = data_ind->nbuf;

                         for (nbuf = hw->rx.first_fragment; nbuf; nbuf = nbuf->next) {
                           total_size += nbuf->len;
                         }

                         comb_nbuf = _mtlk_mmb_nbuf_alloc(total_size);
                         if (__UNLIKELY(comb_nbuf == NULL)) {
                           _mtlk_mmb_rx_free_all_fragments(hw);
                           res = MTLK_ERR_OK;
                           break;
                         }

                         for (nbuf = hw->rx.first_fragment; nbuf; nbuf = nbuf->next) {
                           wave_memcpy(comb_nbuf->data + pos, total_size - pos, nbuf->data, nbuf->len);
                           pos += nbuf->len;
                         }

                         _mtlk_mmb_rx_free_all_fragments(hw);

                         data.nbuf = comb_nbuf;

                         mtlk_df_nbuf_put(comb_nbuf, total_size);

                         if (FRAME_TYPE_EAPOL != frame_type){
                           res = mtlk_vap_get_core_vft(vap_handle)->handle_rx_data(vap_handle, &data);
                           break;
                         } else {
                           res = mtlk_handle_eapol(vap_handle, data.nbuf->data, data.nbuf->len);
                           mtlk_df_nbuf_free(data.nbuf);
                           break;
                         }
                       }

      case FRAG_MID:   if (!hw->rx.first_fragment) {
                         ELOG_V("Received middle fragment without having first");
                         res = MTLK_ERR_NOT_IN_USE;
                         break;
                       }
                       hw->rx.last_fragment->next = data_ind->nbuf;
                       hw->rx.last_fragment = data_ind->nbuf;
                       res = MTLK_ERR_OK;
                       break;

      case NO_FRAG:
      default:         if (hw->rx.first_fragment) {
                         ELOG_V("Received unfragmented when already having fragments");
                         _mtlk_mmb_rx_free_all_fragments(hw);
                       }

                       if (FRAME_TYPE_EAPOL != frame_type) {
                         res = mtlk_vap_get_core_vft(vap_handle)->handle_rx_data(vap_handle, &data);
                         break;
                       } else {
                         res = mtlk_handle_eapol(vap_handle, data.nbuf->data, data.nbuf->len);
                         mtlk_df_nbuf_free(data.nbuf);
                         break;
                       }
    }
  }
  else
  {
    /* unexpected traffic */
    _mtlk_mmb_log_error_vap_not_exist(__FUNCTION__, radio_id, vap_id);
    ILOG1_DDDD("internal variables: tx_read_ptr %X tx_write_ptr %X tx_IN_copy %X tx_int_OUT_cntr %X", hw->rx.ring.read_ptr, hw->rx.ring.write_ptr, hw->rx.ring.regs.IN_copy, hw->rx.ring.regs.int_OUT_cntr);
    mtlk_dump(1, (void *)pHD, sizeof(HOST_DSC), "HD in error");
    res = MTLK_ERR_UNKNOWN;
    mtlk_df_nbuf_push(data.nbuf, offset);
    mtlk_df_nbuf_trim(data.nbuf, 0);
    goto buf_return;
  }

_after_parse:

  if (res == MTLK_ERR_NOT_IN_USE) {
    _mtlk_mmb_nbuf_free(data.nbuf);
  }

#if MTLK_USE_DIRECTCONNECT_DP_API
  data_ind->nbuf = mtlk_df_dcdp_nbuf_alloc(mtlk_vap_get_df(vap_handle), data_ind->size);
  if (NULL == data_ind->nbuf)
#endif
    data_ind->nbuf = _mtlk_mmb_nbuf_alloc(data_ind->size);

  if (__UNLIKELY(data_ind->nbuf == NULL)) {
    /* Handler failed. Fill requested buffer size and put the
       RX Data Ind mirror element to Pending list to allow
       recovery (reallocation) later.
     */
    _mtlk_mmb_hw_inc_cnt(hw, MTLK_HW_SOURCE_CNT_RX_BUF_ALLOC_FAILED);
    mtlk_lslist_push(&hw->rx.pending.lbufs, &data_ind->pend_l);
    ILOG2_DD("RX Data HANDLE_REALLOC failed! Slot#%d (%d bytes) added to pending list",
         (int)u32BdIndex, (int)data_ind->size);
    goto FINISH;
  }

buf_return:
  data_ind->dma_addr = mtlk_df_nbuf_map_to_phys_addr(mtlk_ccr_get_dev_ctx(hw->ccr),
                                                     data_ind->nbuf,
                                                     data_ind->size,
                                                     MTLK_DATA_FROM_DEVICE);
  if (!data_ind->dma_addr) {
    return MTLK_ERR_NO_RESOURCES;
  }

  /* Set new payload buffer address */
  pHD->u32HostPayloadAddr = CPU_TO_LE32(data_ind->dma_addr);
  _mtlk_mmb_put_hd_to_ring(hw, (HOST_DSC *)pHD, &hw->rx.ring);

FINISH:
  return res;
}

#if MTLK_USE_DIRECTCONNECT_DP_API
/* helper function for DataPath driver RX callback  */
int __MTLK_IFUNC
mtlk_mmb_dcdp_rx (struct net_device *rxif, struct dp_subif *rx_subif, struct sk_buff *skb, int32_t len)
{
  int                        res           = DC_DP_SUCCESS;
  mtlk_vap_handle_t          vap_handle    = NULL;
  mtlk_df_user_t            *df_user       = mtlk_df_user_from_ndev(rxif); /* retrieve DF User handle from Linux network device */
  mtlk_hw_t                 *hw;
  sta_entry                 *src_sta       = NULL;
  int                        sid           = MTLK_BFIELD_GET(rx_subif->subif, TX_DATA_INFO_STAID);
  uint8                      vap_id        = MTLK_BFIELD_GET(rx_subif->subif, TX_DATA_INFO_VAPID);
  struct ethhdr             *ether_header  = (struct ethhdr *)skb->data;

  if (__UNLIKELY(NULL == df_user)) {
    dev_kfree_skb_any(skb);
    return DC_DP_FAILURE;
  }

  hw = mtlk_vap_get_hw(mtlk_df_get_vap_handle(mtlk_df_user_get_df(df_user)));
  if (__UNLIKELY(NULL == hw)) {
    dev_kfree_skb_any(skb);
    return DC_DP_FAILURE;
  }

  ILOG3_DD("RX IND: len=%d skb->len=%d", (int)len, (int)skb->len);
  ILOG3_DD("vap_index %d SID %d",  vap_id, sid);
  mtlk_dump(3, skb->data, skb->len, "SKB data");

  mtlk_nbuf_start_tracking(skb);

  if (__UNLIKELY(hw->mac_events_stopped)) {
    res = DC_DP_SUCCESS;
    goto finish;
  }

  mtlk_vap_manager_get_vap_handle(mtlk_vap_get_manager(mtlk_df_get_vap_handle(mtlk_df_user_get_df(df_user))),
                                  vap_id,
                                  &vap_handle);
  if (vap_handle)
  {
    mtlk_core_t *nic = mtlk_vap_get_core(vap_handle);

    if (len != skb->len) {
      ELOG_DDD("CID-%04x: len %d not equal to skb len %d", mtlk_vap_get_oid(vap_handle), len, skb->len);
    }

    /* HW LOGGER */
    if (hw->logger_sid == sid) {
      mtlk_nbuf_stop_tracking(skb);
      mtlk_rtlog_forward_log_packet(skb);
      return DC_DP_SUCCESS;
    }

    /* Try to find source MAC of transmitter */
    src_sta = mtlk_stadb_find_sid(mtlk_core_get_stadb(nic), sid);
    if (src_sta == NULL) {
      ILOG2_V("SOURCE of RX packet not found!");
      res = DC_DP_FAILURE;
      goto finish;
    } else {

      ILOG5_YD("STA %Y found by SID %d", src_sta->info.addr.au8Addr, sid);

      if (MESH_MODE_BACKHAUL_AP != MTLK_CORE_HOT_PATH_PDB_GET_INT(nic, CORE_DB_CORE_MESH_MODE)) {
        /* - On AP we need to update HOST's entry in database of registered
             HOSTs behind connected STAs
           - In Backhaul AP mode only one STA is connected (IRE), so we do not need to update hostdb
        */
        if (memcmp(ether_header->h_source, mtlk_sta_get_addr(src_sta)->au8Addr, ETH_ALEN)) {
          ILOG5_YD("adding STA address %Y to hostdb, SID %d", src_sta->info.addr.au8Addr, sid);
          mtlk_hstdb_update_host(mtlk_core_get_hstdb(nic), ether_header->h_source, src_sta);
        }
      }
    }

    if (mtlk_wlan_pkt_is_802_1X(ether_header->h_proto)) {
      ILOG3_V("Received EAPOL from data path driver");
      mtlk_handle_eapol(vap_handle, skb->data, skb->len);
      mtlk_df_nbuf_free(skb);
    } else {
      mtlk_core_handle_tx_data_t tx_data;
      tx_data.nbuf = skb;
      tx_data.dst_sta = NULL;
      tx_data.rx_if = rxif;
      tx_data.rx_subif = rx_subif;

      if (MTLK_DC_DP_MODE_SWPATH == hw->dp_dev.dp_mode) {
        /* SW path only */
        skb->dev = rxif;
      }

      mtlk_core_analyze_and_send_up(nic, &tx_data, src_sta);
    }

    if (src_sta) {
      mtlk_sta_decref(src_sta); /* De-reference of find */
    }
    return DC_DP_SUCCESS;
  }
  else
  {
    /* unexpected traffic */
    _mtlk_mmb_log_error_vap_not_exist(__FUNCTION__, 0, vap_id);
    res = DC_DP_FAILURE;
  }

finish:
  mtlk_df_nbuf_free(skb);
  return res;
}
#endif /* MTLK_USE_DIRECTCONNECT_DP_API */

static void
_mtlk_mmb_handle_cm_ind (mtlk_hw_t *hw,
                         BOOL       is_man,
                         him_msg_t *p_ind)
{
  void                     *payload;
  uint32                    payload_size;
  uint32                    msg_id    = 0;
  mtlk_mmb_basic_bdr_t     *bbdr      = _mtlk_mmb_cm_get_ind_bbdr(hw, is_man);
  mtlk_hw_cm_ind_mirror_t  *ind_obj   = _mtlk_mmb_cm_ind_get_mirror_bd(bbdr, p_ind->index);
  uint32                    iom_size  = _mtlk_mmb_cm_bdr_get_iom_bd_ind_size(is_man);
  void                     *iom       = _mtlk_mmb_cm_bdr_get_iom_bd(bbdr, p_ind->index, iom_size);
  mtlk_vap_handle_t         vap_handle;
  unsigned                  vap_id = p_ind->vap_id;
  unsigned                  radio_id = p_ind->radio_id;

  /* get MAN ind header + data */
  _mtlk_mmb_memcpy_fromio(&ind_obj->msg_hdr,
                          iom,
                          iom_size);

  payload = (void *)((uint8 *)&ind_obj->msg_hdr) + sizeof(ind_obj->msg_hdr);
  msg_id = (uint32)MAC_TO_HOST16(ind_obj->msg_hdr.u16MsgId);
  MTLK_TRACER_ADD_ENTRY(hw, MTLK_TR_MSG_RECEIVE_ID, msg_id);

  if (msg_id == MC_MAN_MAC_EVENT_IND) {
    MAC_EVENT *pevt = (MAC_EVENT*)payload;
    switch (MAC_TO_HOST32(pevt->u32EventID) & 0xff)
    {
      case EVENT_EXCEPTION:
      {
        wave_rcvry_process_mac_exception(hw, &pevt->u.sAppFatalEvent);
        goto _end;
      }
      case EVENT_APP_FATAL:
      {
        wave_rcvry_process_mac_fatal(hw, &pevt->u.sAppFatalEvent);
        goto _end;
      }
      default: break;
    }
  }

  payload_size = iom_size - sizeof(ind_obj->msg_hdr);
  vap_handle = _mtlk_mmb_get_vap_handle_from_vap_id(hw, radio_id, vap_id);
  if (__UNLIKELY(!vap_handle)) {
    /* We can use either SHRAM_IND_MSG or SHRAM_DBG_MSG for debug printout its head */
    SHRAM_IND_MSG *msg = (SHRAM_IND_MSG *)(&ind_obj->msg_hdr);

    _mtlk_mmb_log_error_vap_not_exist(__FUNCTION__, radio_id, vap_id);
    ELOG_DDPDDDD("hw_idx %u: HIM index 0x%04X, iom %p, RadioID %u, VapID %u, is_man %d, msg_id 0x%04X",
                  hw->card_idx, p_ind->index, iom, radio_id, vap_id, (int)is_man, msg_id);
    mtlk_dump(0, &msg->sHdr, sizeof(msg->sHdr), "MSG header");
    mtlk_dump(2, &msg->sMsg, MIN(128, payload_size), "MSG data");
  } else {
    mtlk_vap_get_core_vft(vap_handle)->handle_rx_ctrl(vap_handle, msg_id, payload, payload_size);
  }

_end:
  ILOG3_DDD("MsgID 0x%x, RadioID %u VapID %u", msg_id, radio_id, vap_id);

  _mtlk_mmb_resp_cm_ind(hw, is_man, ind_obj, radio_id, vap_id);
  MTLK_TRACER_ADD_ENTRY(hw, MTLK_TR_MSG_RECEIVE_CFM_ID, msg_id);
}

static __INLINE void
_mtlk_mmb_dbg_verify_msg_send(mtlk_hw_cm_req_mirror_t *obj)
{
#ifdef CPTCFG_IWLWAV_DEBUG
  if (1 != mtlk_osal_atomic_inc(&obj->usage_cnt)) {
    ELOG_D("Message being sent twice, msg id 0x%x",
          MAC_TO_HOST16(MSG_OBJ_GET_ID(&obj->msg_hdr)));
    MTLK_ASSERT(FALSE);
  }
#else
  MTLK_UNREFERENCED_PARAM(obj);
#endif
}

static __INLINE void
_mtlk_mmb_dbg_verify_msg_recv(mtlk_hw_cm_req_mirror_t *obj)
{
#ifdef CPTCFG_IWLWAV_DEBUG
  if (0 != mtlk_osal_atomic_dec(&obj->usage_cnt)) {
    ELOG_D("Message received from HW twice, msg id 0x%x",
          MAC_TO_HOST16(MSG_OBJ_GET_ID(&obj->msg_hdr)));
    MTLK_ASSERT(FALSE);
  }
#else
  MTLK_UNREFERENCED_PARAM(obj);
#endif
}

static __INLINE void
_mtlk_mmb_dbg_init_msg_verifier(mtlk_mmb_basic_bdr_t *bbdr)
{
#ifdef CPTCFG_IWLWAV_DEBUG
  uint8 i;
  for (i = 0; i < bbdr->nof_bds; i++) {
    mtlk_hw_cm_req_mirror_t *obj = _mtlk_basic_bdr_get_mirror_bd(bbdr, i, mtlk_hw_cm_req_mirror_t);
    mtlk_osal_atomic_set(&obj->usage_cnt, 0);
  }
#else
  MTLK_UNREFERENCED_PARAM(bbdr);
#endif
}

static __INLINE void
_mtlk_mmb_handle_cm_cfm (mtlk_hw_t *hw,
                         BOOL       is_man,
                         him_msg_t *p_cfm)
{
  mtlk_mmb_basic_bdr_t    *bbdr     = _mtlk_mmb_cm_get_req_bbdr (hw, is_man);
  mtlk_hw_cm_req_mirror_t *req_obj  = _mtlk_mmb_cm_req_get_mirror_bd(bbdr, p_cfm->index);
  uint32                   iom_size = _mtlk_mmb_cm_bdr_get_iom_bd_req_size(is_man);
  void                    *iom      = _mtlk_mmb_cm_bdr_get_iom_bd(bbdr, p_cfm->index, iom_size);

  _mtlk_mmb_dbg_verify_msg_recv(req_obj);

  /* get data */
  _mtlk_mmb_memcpy_fromio(MSG_OBJ_PAYLOAD(&req_obj->msg_hdr),
                          ((uint8 *)iom) + sizeof(req_obj->msg_hdr),
                          iom_size - sizeof(req_obj->msg_hdr));

  MTLK_TRACER_ADD_ENTRY(hw, MTLK_TR_MSG_SEND_CFM_ID, MSG_OBJ_GET_ID(&req_obj->msg_hdr));

  /*send it to TXMM */
  if (is_man) {
    txmm_on_cfm(hw, &req_obj->msg_hdr);
  }
  else {
    txdm_on_cfm(hw, &req_obj->msg_hdr);
  }
}

int _mtlk_mmb_bss_mgmt_init(mtlk_hw_t *hw);
int _mtlk_mmb_recover_bss_mgmt(mtlk_hw_t *hw);
void _mtlk_mmb_bss_mgmt_cleanup(mtlk_hw_t *hw);
int _mtlk_mmb_handle_bss_ind(mtlk_hw_t *hw, volatile HOST_DSC *pHD);
int _mtlk_mmb_handle_bss_cfm(mtlk_hw_t *hw, HOST_DSC *pHD);

static void
_mtlk_mmb_handle_received_msg (mtlk_hw_t *hw, him_descr_t descriptor)
{
  him_msg_t msg;
  int type = MTLK_BFIELD_GET(descriptor,HIM_DESC_TYPE);
  DESCR_TO_MSG(msg,descriptor);

  /* WAVE600: All band related message will be passed via Master VAP */
  if (msg.vap_id == HIM_DESC_VAP_INVALID) {
    msg.vap_id = 0;
  }
  /* WAVE600: All card related messages will be passed via Radio 0 */
  if (msg.radio_id == HIM_DESC_RADIO_INVALID) {
    msg.radio_id = 0;
    msg.vap_id = 0;
  }

  switch (type) {
  case ARRAY_MAN_IND:
    CPU_STAT_SPECIFY_TRACK(CPU_STAT_ID_RX_MM);
    if (__LIKELY(!hw->mac_events_stopped))
      _mtlk_mmb_handle_cm_ind(hw, TRUE, &msg);
    break;
  case ARRAY_MAN_REQ:
    CPU_STAT_SPECIFY_TRACK(CPU_STAT_ID_TX_MM_CFM);
    if (__LIKELY(!hw->mac_events_stopped_completely))
      _mtlk_mmb_handle_cm_cfm(hw, TRUE, &msg);
    break;
  case ARRAY_DBG_IND:
    CPU_STAT_SPECIFY_TRACK(CPU_STAT_ID_RX_MM);
    if (__LIKELY(!hw->mac_events_stopped))
      _mtlk_mmb_handle_cm_ind(hw, FALSE, &msg);
    break;
  case ARRAY_DBG_REQ:
    CPU_STAT_SPECIFY_TRACK(CPU_STAT_ID_TX_MM_CFM);
    if (__LIKELY(!hw->mac_events_stopped_completely))
      _mtlk_mmb_handle_cm_cfm(hw, FALSE, &msg);
    break;
  case ARRAY_DAT_LOGGER_IND:
    CPU_STAT_SPECIFY_TRACK(CPU_STAT_ID_RX_LOG);
    if (__LIKELY(!hw->mac_events_stopped_completely))
      _mtlk_mmb_handle_logger_buf_indication(hw, descriptor);
    break;
  case ARRAY_NULL:
  default:
    CPU_STAT_SPECIFY_TRACK(CPU_STAT_ID_RX_MM);
    ELOG_D("Wrong message type (%d)", type);
    break;
  }
}

#ifdef CPTCFG_IWLWAV_CPU_STAT
  static const cpu_stat_track_id_e ids[] = {
    CPU_STAT_ID_RX_MM,
    CPU_STAT_ID_RX_EMPTY,
    CPU_STAT_ID_RX_LOG,
    CPU_STAT_ID_TX_MM_CFM,
  };
  cpu_stat_track_id_e ts_handle;

  static const cpu_stat_track_id_e bss_ids[] = {
    CPU_STAT_ID_RX_BSS,
    CPU_STAT_ID_RX_MGMT_BEACON,
    CPU_STAT_ID_RX_MGMT_ACTION,
    CPU_STAT_ID_RX_MGMT_OTHER,
    CPU_STAT_ID_RX_CTL
  };
  cpu_stat_track_id_e ts_handle_bss;
#endif

#if defined (CPTCFG_IWLWAV_G6_BAR0_TEST)
void _bar0_test_code(void *ccr_mem, char *msg, void *data_tx_r, void *data_rx_r, void *bss_tx_r, void *bss_rx_r);

void hw_mmb_bar0_test(mtlk_hw_t *hw, char *id_str) {
  _bar0_test_code(&hw->ccr->mem, id_str, &hw->tx.ring.regs, &hw->rx.ring.regs, &hw->bss_mgmt.tx_ring.regs, &hw->bss_mgmt.rx_ring.regs);
}
#endif

/******************************************************************
 * Put these functions into single sections (for better performance)
 ******************************************************************/
static int  _mtlk_hw_send_data(mtlk_hw_t *hw,
    mtlk_vap_handle_t vap_handle, mtlk_hw_data_req_mirror_t *data_req)          __MTLK_INT_HANDLER_SECTION;

static void _mtlk_mmb_put_hd_to_ring(mtlk_hw_t *hw,
    HOST_DSC *pHD, mtlk_ring_buffer *ring)                                      __MTLK_INT_HANDLER_SECTION;

static void _mtlk_mmb_data_txout_process (mtlk_irq_handler_data *ihd)            __MTLK_INT_HANDLER_SECTION;
static void _mtlk_mmb_data_rx_process (mtlk_irq_handler_data *ihd)               __MTLK_INT_HANDLER_SECTION;

#if MTLK_USE_DIRECTCONNECT_DP_API
static void _mtlk_mmb_empty_data_handler (mtlk_irq_handler_data *ihd)            __MTLK_INT_HANDLER_SECTION;
static void _mtlk_mmb_data_txout_process_dcdp (mtlk_irq_handler_data *ihd)       __MTLK_INT_HANDLER_SECTION;
static void _mtlk_mmb_data_rx_process_dcdp (mtlk_irq_handler_data *ihd)          __MTLK_INT_HANDLER_SECTION;
static void _mtlk_mmb_data_rx_frag_wa_process_dcdp (mtlk_irq_handler_data *ihd)  __MTLK_INT_HANDLER_SECTION;
#endif
/******************************************************************/

#if MTLK_USE_DIRECTCONNECT_DP_API
static void
_mtlk_mmb_empty_data_handler (mtlk_irq_handler_data *ihd)
{
  return;
}

static void
_mtlk_mmb_data_txout_process_dcdp (mtlk_irq_handler_data *ihd)
{
  int res;
  mtlk_hw_t *hw = ihd->hw;

  if (__UNLIKELY(hw->mac_events_stopped)) {
    return;
  }

  /* Optimization: don't call API function if no HD's in ring buffer */
  if (0 == mtlk_ccr_ring_get_hds_to_process(hw->ccr, &hw->tx.ring.regs)) {
    return;
  }

  res = mtlk_df_dcdp_handle_txout_ring_sw(&hw->dp_dev);
  if (res < 0) {
    ILOG2_D("DC DP TxOut ring handler failure (card_idx:%u)", hw->card_idx);
    return;
  }
  return;
}

static void
_mtlk_mmb_data_rx_process_dcdp (mtlk_irq_handler_data *ihd)
{
  int res;
  mtlk_hw_t *hw = ihd->hw;

  /* Optimization: don't call API function if no HD's in ring buffer */
  if (0 == mtlk_ccr_ring_get_hds_to_process(hw->ccr, &hw->rx.ring.regs)) {
    return;
  }

  res = mtlk_df_dcdp_handle_rx_ring_sw(&hw->dp_dev);
  if (res < 0) {
    ILOG2_D("DC DP RxIn ring handler failure (card_idx:%u)", hw->card_idx);
    return;
  }
  return;
}

static void
_mtlk_mmb_data_rx_frag_wa_process_dcdp (mtlk_irq_handler_data *ihd)
{
  int res;
  mtlk_hw_t *hw = ihd->hw;

  /* Optimization: don't call API function if no HD's in ring buffer */
  if (0 == mtlk_ccr_ring_get_hds_to_process(hw->ccr, &hw->rx.ring.regs)) {
    return;
  }

  res = mtlk_df_dcdp_handle_frag_wa_ring(&hw->dp_dev);
  if (res < 0) {
    ILOG2_D("DC DP frag W/A ring handler failure (card_idx:%u)", hw->card_idx);
    return;
  }
  return;
}
#endif

static void
_mtlk_mmb_data_txout_process (mtlk_irq_handler_data *ihd)
{
#ifdef MTLK_DEBUG
  mtlk_dbg_hres_ts_t       time_current;
  uint64  time_diff;
#endif
  mtlk_hw_t *hw = ihd->hw;

  MTLK_ASSERT(NULL != hw->ccr);

  if (__LIKELY(!hw->mac_events_stopped)) {
    uint32 cur_hd, hds_to_process;

#ifdef MTLK_DEBUG
    /* update max interrupt to tasklet statistic */
    mtlk_dbg_hres_ts(&time_current);
    time_diff = mtlk_dbg_hres_diff_uint64(&time_current, &hw->tx_interrupt_time);
    if(hw->tx.max_int_tasklet_time < time_diff) {
      hw->tx.max_int_tasklet_time = time_diff;
    }
#endif

    hds_to_process = mtlk_ccr_ring_get_hds_to_process(hw->ccr, &hw->tx.ring.regs);
    MTLK_TRACER_ADD_ENTRY(hw, MTLK_TR_DATA_TXOUT_START, hds_to_process);
    if (__UNLIKELY(hds_to_process == 0)) {
      MTLK_TRACER_ADD_ENTRY(hw, MTLK_TR_DATA_TXOUT_END, 0);
      return;
    }

    if (hds_to_process > hw->data_txout_lim) {
      hds_to_process = hw->data_txout_lim;
    }

    ILOG5_DD("TX OUT shared counter: %d, internal OUT counter %d", LE32_TO_CPU(mtlk_raw_readl(hw->tx.ring.regs.p_OUT_cntr)), hw->tx.ring.regs.int_OUT_cntr);
    ILOG5_DD("handling TX-OUT interrupt, to process %d HDs, IRQ status is %08X", hds_to_process, hw->last_irq_status);

    /* read HDs
       process each HD - TX confirmation
    ****************************************************/
    for (cur_hd = 0; cur_hd < hds_to_process; cur_hd++) {
      CPU_STAT_BEGIN_TRACK(CPU_STAT_ID_TX_CFM);
      _mtlk_mmb_handle_dat_cfm(hw, hw->tx.ring.base_addr + hw->tx.ring.read_ptr);
      INC_WRAP_IDX(hw->tx.ring.read_ptr, hw->tx.ring.size); /* wrap around */
      CPU_STAT_END_TRACK(CPU_STAT_ID_TX_CFM);
    }

#ifdef MTLK_DEBUG
    /* update max packet statistic */
    if(hw->tx.max_received_cfm < hds_to_process) {
      hw->tx.max_received_cfm = hds_to_process;
    }

    /* update max interrupt to processed cfm statistic */
    mtlk_dbg_hres_ts(&time_current);
    time_diff = mtlk_dbg_hres_diff_uint64(&time_current, &hw->tx_interrupt_time);
    if(hw->tx.max_int_processed_cfm < time_diff) {
      hw->tx.max_int_processed_cfm = time_diff;
    }
#endif

    mtlk_ccr_ring_clear_interrupt(hw->ccr, &hw->tx.ring.regs, hds_to_process);
    MTLK_TRACER_ADD_ENTRY(hw, MTLK_TR_DATA_TXOUT_END, hds_to_process);
    MTLK_TRACER_ADD_ENTRY(hw, MTLK_TR_DATA_TXOUT_REM, mtlk_ccr_ring_get_hds_to_process(hw->ccr, &hw->tx.ring.regs));
  }

  return;
}

static void
_mtlk_mmb_data_rx_process (mtlk_irq_handler_data *ihd)
{
#ifdef MTLK_DEBUG
  mtlk_dbg_hres_ts_t       time_current;
  uint64  time_diff;
#endif
  mtlk_hw_t *hw = ihd->hw;
  uint32 cur_hd, hds_to_process;

  MTLK_ASSERT(NULL != hw->ccr);

#ifdef MTLK_DEBUG
  /* update max interrupt to tasklet statistic */
  mtlk_dbg_hres_ts(&time_current);
  time_diff = mtlk_dbg_hres_diff_uint64(&time_current, &hw->rx_interrupt_time);
  if(hw->rx.max_int_tasklet_time < time_diff) {
    hw->rx.max_int_tasklet_time = time_diff;
  }
#endif

  hds_to_process = mtlk_ccr_ring_get_hds_to_process(hw->ccr, &hw->rx.ring.regs);
  MTLK_TRACER_ADD_ENTRY(hw, MTLK_TR_DATA_RX_START, hds_to_process);
  if (__UNLIKELY(hds_to_process == 0)) {
    MTLK_TRACER_ADD_ENTRY(hw, MTLK_TR_DATA_RX_END, 0);
    return;
  }

  if (hds_to_process > hw->data_rx_lim) {
    hds_to_process = hw->data_rx_lim;
  }

  ILOG5_DD("DATA IND shared counter: %d, internal OUT counter %d", LE32_TO_CPU(mtlk_raw_readl(hw->rx.ring.regs.p_OUT_cntr)), hw->rx.ring.regs.int_OUT_cntr);
  ILOG5_DD("handling DATA IND interrupt, to process %d HDs, IRQ status is %08X", hds_to_process, hw->last_irq_status);

  for (cur_hd = 0; cur_hd < hds_to_process; cur_hd++) {
    CPU_STAT_BEGIN_TRACK(CPU_STAT_ID_RX_DAT);
    CPU_STAT_BEGIN_TRACK(CPU_STAT_ID_RX_HW);
    if (__LIKELY(!hw->mac_events_stopped)) {
      _mtlk_mmb_handle_dat_ind(hw, hw->rx.ring.base_addr + hw->rx.ring.read_ptr);
    } else {
      /* must confirm w/o processing */
      // tx_bd.u32frameInfo3      = OWN_SOP_EOP; /* probably this is not necessary */
      _mtlk_mmb_put_hd_to_ring(hw, hw->rx.ring.base_addr + hw->rx.ring.read_ptr, &hw->rx.ring);
    }
    INC_WRAP_IDX(hw->rx.ring.read_ptr, hw->rx.ring.size);
    CPU_STAT_END_TRACK(CPU_STAT_ID_RX_DAT);
    CPU_STAT_END_TRACK(CPU_STAT_ID_RX_HW);
  }

#ifdef MTLK_DEBUG
  /* update max packet statistic */
  if(hw->rx.max_received_pckts < hds_to_process) {
    hw->rx.max_received_pckts = hds_to_process;
  }

  /* update max interrupt to time statistic */
  mtlk_dbg_hres_ts(&time_current);
  time_diff = mtlk_dbg_hres_diff_uint64(&time_current, &hw->rx_interrupt_time);
  if(hw->rx.max_int_processed_packet < time_diff) {
    hw->rx.max_int_processed_packet = time_diff;
  }
#endif

  mtlk_ccr_ring_clear_interrupt(hw->ccr, &hw->rx.ring.regs, hds_to_process);
  MTLK_TRACER_ADD_ENTRY(hw, MTLK_TR_DATA_RX_END, hds_to_process);
  MTLK_TRACER_ADD_ENTRY(hw, MTLK_TR_DATA_RX_REM, mtlk_ccr_ring_get_hds_to_process(hw->ccr, &hw->rx.ring.regs));

  return;
}

static void
_mtlk_mmb_init_data_handlers (mtlk_hw_t *hw)
{
  /* Use internal handlers by default */
  hw->data_rx_process    = _mtlk_mmb_data_rx_process;
  hw->data_txout_process = _mtlk_mmb_data_txout_process;

#if MTLK_USE_DIRECTCONNECT_DP_API
  if (mtlk_mmb_fastpath_available(hw)) {
    /* fastpath - init with empty handlers first */
    hw->data_rx_process    = _mtlk_mmb_empty_data_handler;
    hw->data_txout_process = _mtlk_mmb_empty_data_handler;
    if (mtlk_mmb_dcdp_frag_wa_enabled(hw)) {
      /* Fastpath, but fragmentation W/A required */
      hw->data_rx_process  = _mtlk_mmb_data_rx_frag_wa_process_dcdp;
    }
  }
  else if (mtlk_mmb_dcdp_path_available(hw)) {
    /* In SW mode - check if SW path is supported by datapath driver */
    hw->data_rx_process    = _mtlk_mmb_data_rx_process_dcdp;
    hw->data_txout_process = _mtlk_mmb_data_txout_process_dcdp;
  }
#endif
}

static __INLINE void
bss_cfm_process (mtlk_irq_handler_data *ihd)
{
  mtlk_hw_t *hw        = ihd->hw;

  MTLK_ASSERT(NULL != hw->ccr);

  /* management frame send confirmation - BSS TX-OUT */

  hw->bss_cfm_num++;

  if (__LIKELY(!hw->mac_events_stopped_completely)) {
    uint32 cur_hd, hds_to_process = 0;

    hds_to_process = mtlk_ccr_ring_get_hds_to_process(hw->ccr, &hw->bss_mgmt.tx_ring.regs);
    MTLK_TRACER_ADD_ENTRY(hw, MTLK_TR_BSS_CFM_START, hds_to_process);
    if (__UNLIKELY(hds_to_process == 0)) {
      MTLK_TRACER_ADD_ENTRY(hw, MTLK_TR_BSS_CFM_END, 0);
      return;
    }

    if (hds_to_process > hw->bss_cfm_lim) {
      hds_to_process = hw->bss_cfm_lim;
    }

    ILOG5_DD("BSS CFM shared counter: %d, internal OUT counter %d", LE32_TO_CPU(mtlk_raw_readl(hw->bss_mgmt.tx_ring.regs.p_OUT_cntr)), hw->bss_mgmt.tx_ring.regs.int_OUT_cntr);
    ILOG5_DD("handling BSS CFM interrupt, to process %d HDs, IRQ status is %08X", hds_to_process, hw->last_irq_status);

    for (cur_hd = 0; cur_hd < hds_to_process; cur_hd++) {
      _mtlk_mmb_handle_bss_cfm(hw, hw->bss_mgmt.tx_ring.base_addr + hw->bss_mgmt.tx_ring.read_ptr);
      INC_WRAP_IDX(hw->bss_mgmt.tx_ring.read_ptr, hw->bss_mgmt.tx_ring.size);
    }

    mtlk_ccr_ring_clear_interrupt(hw->ccr, &hw->bss_mgmt.tx_ring.regs, hds_to_process);
    MTLK_TRACER_ADD_ENTRY(hw, MTLK_TR_BSS_CFM_END, hds_to_process);
    MTLK_TRACER_ADD_ENTRY(hw, MTLK_TR_BSS_CFM_REM, mtlk_ccr_ring_get_hds_to_process(hw->ccr, &hw->bss_mgmt.tx_ring.regs));
  }
  return;
}

static __INLINE void
bss_rx_process (mtlk_irq_handler_data *ihd)
{
  mtlk_hw_t *hw = ihd->hw;

  MTLK_ASSERT(NULL != hw->ccr);

  /* management frame arrival */

  hw->bss_ind_num++;

  if (__LIKELY(!hw->mac_events_stopped)) {
    uint32 cur_hd, hds_to_process = 0;

    hds_to_process = mtlk_ccr_ring_get_hds_to_process(hw->ccr, &hw->bss_mgmt.rx_ring.regs);
    MTLK_TRACER_ADD_ENTRY(hw, MTLK_TR_BSS_RX_START, hds_to_process);
    if (__UNLIKELY(hds_to_process == 0)) {
      hw->bss_rx_zero_hds_num++;
      MTLK_TRACER_ADD_ENTRY(hw, MTLK_TR_BSS_RX_END, 0);
      return;
    }

    if (hds_to_process > hw->bss_rx_lim) {
      hds_to_process = hw->bss_rx_lim;
    }

    ILOG5_DD("BSS IND shared counter: %d, internal OUT counter %d", LE32_TO_CPU(mtlk_raw_readl(hw->bss_mgmt.rx_ring.regs.p_OUT_cntr)), hw->bss_mgmt.rx_ring.regs.int_OUT_cntr);
    ILOG5_DD("handling BSS IND interrupt, to process %d HDs, IRQ status is %08X", hds_to_process, hw->last_irq_status);

    for (cur_hd = 0; cur_hd < hds_to_process; cur_hd++) {
      CPU_STAT_BEGIN_TRACK_SET(bss_ids, ARRAY_SIZE(bss_ids), &ts_handle_bss);
      CPU_STAT_SPECIFY_TRACK(CPU_STAT_ID_RX_BSS);
      _mtlk_mmb_handle_bss_ind(hw, hw->bss_mgmt.rx_ring.base_addr + hw->bss_mgmt.rx_ring.read_ptr);
      INC_WRAP_IDX(hw->bss_mgmt.rx_ring.read_ptr, hw->bss_mgmt.rx_ring.size);
      CPU_STAT_END_TRACK_SET(ts_handle_bss);
    }

    mtlk_ccr_ring_clear_interrupt(hw->ccr, &hw->bss_mgmt.rx_ring.regs, hds_to_process);
    MTLK_TRACER_ADD_ENTRY(hw, MTLK_TR_BSS_RX_END, hds_to_process);
    MTLK_TRACER_ADD_ENTRY(hw, MTLK_TR_BSS_RX_REM, mtlk_ccr_ring_get_hds_to_process(hw->ccr, &hw->bss_mgmt.rx_ring.regs));
  }
  return;
}

/* processing all IRQ handlers */
#define HW_MMB_ALL_IRQ_PROCESSING    1

static __INLINE void
legacy_irq_process (mtlk_irq_handler_data *ihd)
{
  mtlk_hw_t *hw = ihd->hw;
  int processed_count = 0;
  int count_to_process;
  /* WLS-2479 zero_elem must be 4-aligned.                   */
  /* Considering variety of kernel configuration options     */
  /* related to packing and alignment, the only fool proof   */
  /* way to secure this requirement is to declare it as 4    */
  /* bytes integer type.                                     */
  static const uint32 zero_elem = 0;
  uint32 fifo_descriptor;

  MTLK_ASSERT(NULL != hw->ccr);

  count_to_process = (hw->legacy_lim < hw->bds.ind.size) ? hw->legacy_lim : hw->bds.ind.size;


  MTLK_TRACER_ADD_ENTRY(hw, MTLK_TR_MSG_START, count_to_process);

  while (processed_count < count_to_process) {
    processed_count++;
    CPU_STAT_BEGIN_TRACK_SET(ids, ARRAY_SIZE(ids), &ts_handle);

    _mtlk_mmb_pas_get(hw,
                      "get next message",
                      hw->bds.ind.offset + hw->bds.ind.idx * sizeof(fifo_descriptor),
                      &fifo_descriptor,
                      sizeof(fifo_descriptor));

    fifo_descriptor = MAC_TO_HOST32(fifo_descriptor);

    ILOG5_D("MSG READ: 0x%08x", fifo_descriptor);

    if ((int)MTLK_BFIELD_GET(fifo_descriptor,HIM_DESC_TYPE) == 0) /* NULL type means empty descriptor */
    {
      CPU_STAT_SPECIFY_TRACK(CPU_STAT_ID_RX_EMPTY);
      CPU_STAT_END_TRACK_SET(ts_handle);
      ELOG_V("Nothing in the MSG buffer");
      goto _finish;     /* all packets are processed */
    }

    /***********************************************************************
     * Zero handled BD
     ***********************************************************************/
    _mtlk_mmb_pas_put(hw,
                      "zero received message",
                      hw->bds.ind.offset + hw->bds.ind.idx * sizeof(zero_elem),
                      &zero_elem,
                      sizeof(zero_elem));
    /***********************************************************************/

    _mtlk_mmb_hw_inc_cnt(hw, MTLK_HW_SOURCE_CNT_LEGACY_IND_RECEIVED);

#ifdef MTLK_CCR_CLEAR_INT_UNCONDIT
    if (_hw_type_is_gen5_b11(hw) || _hw_type_is_gen6(hw)) {
        /* clear interrupts unconditionally */
        (void)mtlk_ccr_clear_interrupts(hw->ccr, MTLK_IRQ_LEGACY);
    }
#endif

    _mtlk_mmb_handle_received_msg(hw, fifo_descriptor);

    INC_WRAP_IDX(hw->bds.ind.idx, hw->bds.ind.size);

    CPU_STAT_END_TRACK_SET(ts_handle);
  }

_finish:
  MTLK_TRACER_ADD_ENTRY(hw, MTLK_TR_MSG_END, processed_count);
  return;
}

static __INLINE void
mailbox_rx_process(mtlk_irq_handler_data *ihd)
{
  mtlk_hw_t * hw = ihd->hw;
  unsigned processed_count = 0;
  unsigned count_from_ring = 0;
  unsigned count_to_process = 0;

  /* WLS-2479 zero_elem must be 4-aligned.                   */
  /* Considering variety of kernel configuration options     */
  /* related to packing and alignment, the only fool proof   */
  /* way to secure this requirement is to declare it as 4    */
  /* bytes integer type.                                     */
  static const uint32 zero_elem = 0;
  uint32 fifo_descriptor;

  MTLK_ASSERT(NULL != hw->ccr);

  count_to_process = (hw->legacy_lim < hw->bds.ind.size) ? hw->legacy_lim : hw->bds.ind.size;
  count_from_ring = mtlk_ccr_ring_get_hds_to_process(hw->ccr, &hw->ctrl.ring.regs);
  count_to_process = MIN(count_to_process, count_from_ring);
  MTLK_TRACER_ADD_ENTRY(hw, MTLK_TR_MSG_START, count_from_ring);
  MTLK_TRACER_ADD_ENTRY(hw, MTLK_TR_MSG_COUNT, count_to_process);

  while (processed_count < count_to_process) {
    processed_count++;
    CPU_STAT_BEGIN_TRACK_SET(ids, ARRAY_SIZE(ids), &ts_handle);

      _mtlk_mmb_pas_get(hw,
                        "get next message",
                        hw->bds.ind.offset + hw->bds.ind.idx * sizeof(fifo_descriptor),
                        &fifo_descriptor,
                        sizeof(fifo_descriptor));
      
      fifo_descriptor = MAC_TO_HOST32(fifo_descriptor);
      
      ILOG5_D("MSG READ: 0x%08x", fifo_descriptor);
      
      if ((int)MTLK_BFIELD_GET(fifo_descriptor, HIM_DESC_TYPE) == 0) /* NULL type means empty descriptor */
      {
        CPU_STAT_SPECIFY_TRACK(CPU_STAT_ID_RX_EMPTY);
        CPU_STAT_END_TRACK_SET(ts_handle);
        ELOG_V("Nothing in the MSG buffer");
        goto _finish;     /* all packets are processed */
      }
      
      /***********************************************************************
       * Zero handled BD
       ***********************************************************************/
      _mtlk_mmb_pas_put(hw,
                        "zero received message",
                        hw->bds.ind.offset + hw->bds.ind.idx * sizeof(zero_elem),
                        &zero_elem,
                        sizeof(zero_elem));
      /***********************************************************************/
      
      _mtlk_mmb_hw_inc_cnt(hw, MTLK_HW_SOURCE_CNT_LEGACY_IND_RECEIVED);
      _mtlk_mmb_handle_received_msg(hw, fifo_descriptor);
      INC_WRAP_IDX(hw->bds.ind.idx, hw->bds.ind.size);
      CPU_STAT_END_TRACK_SET(ts_handle);
  }

_finish:
  if (processed_count != 0) {
    mtlk_ccr_ring_clear_interrupt(hw->ccr, &hw->ctrl.ring.regs, processed_count);
    MTLK_TRACER_ADD_ENTRY(hw, MTLK_TR_MSG_END, processed_count);
    ILOG3_D("mailbox processed %d message(s)", processed_count);
  }
  MTLK_TRACER_ADD_ENTRY(hw, MTLK_TR_MSG_REM, mtlk_ccr_ring_get_hds_to_process(hw->ccr, &hw->ctrl.ring.regs));

  return;
}


static int
_mtlk_mmb_cause_mac_assert (mtlk_hw_t *hw, uint32 mips_no)
{
  uint32 pas_offset;
  uint32 val = 0;

  MTLK_ASSERT(mips_no < _hw_num_cores(hw));

  if (!hw->chi_area.sMipsControl.sData.u32DescriptorLocation) {
    return MTLK_ERR_NOT_SUPPORTED;
  }

  pas_offset =
    hw->chi_area.sMipsControl.sData.u32DescriptorLocation +
    MTLK_OFFSET_OF(FW_CORE_CONTROL_DESCRIPTOR, u32FwCoreCtrl[mips_no]);

  WLOG_PPD("mmb_pas=%p | mmb_pas_phy=%p | pas_offset=%08X", hw->mmb_pas, hw->mmb_pas_phy, pas_offset);

  _mtlk_mmb_pas_readl(hw,
                      "MIPS Ctrl",
                      pas_offset,
                      val);

  MTLK_BFIELD_SET(val, FW_CORE_CTRL_DO_ASSERT, 1);

  _mtlk_mmb_pas_writel(hw,
                       "MIPS Ctrl",
                       pas_offset,
                       val);

  WLOG_DPD("FW MAC#%d assert is done: mmb_pas %p, pas_offset %08X",
          mips_no, hw->mmb_pas, pas_offset);

  return MTLK_ERR_OK;
}

static int
_mtlk_mmb_cause_all_macs_assert (mtlk_hw_t *hw)
{
  int mips_no;

  for (mips_no = 0; mips_no < _hw_num_cores(hw); mips_no++) {
    (void)_mtlk_mmb_cause_mac_assert (hw, mips_no);
  }
  return MTLK_ERR_OK;
}

static int
_mtlk_mmb_handle_sw_trap (mtlk_hw_t *hw)
{
  int res = MTLK_ERR_OK;
  uint32 mips_no;
  mtlk_vap_handle_t master_vap_handle;

  if ((hw->chi_area.sMipsControl.sData.u32DescriptorLocation) &&
      (hw->state != MTLK_HW_STATE_MAC_ASSERTED) &&
      (hw->state != MTLK_HW_STATE_APPFATAL)) { /* TODO: check if mtlk_hw_is_halted(hw->state) can be used */
    /* MIPS Ctrl extension supported => cause MAC assert =>
     * Core will receive and handle it in regular way
     */

    /* Assert UMAC if not asserted yet */
    wave_hw_mmb_cause_umac_assert(hw);
    /* TODO: remove LMAC assert (required by design) as soon as FW is implemented */
    wave_hw_mmb_cause_lmac_assert(hw);
  }
  else {
    /* MIPS Ctrl extension NOT supported => notify Core =>
     * Core will "simulate" MAC assertion
     * OR
     * MAC assertion was sent, but FW didn't send
     * neither EVENT_APP_FATAL nor EVENT_EXCEPTION in response
     */

    /* WAVE600: TODO: do it for the first radio now, but rework later! */
    res = mtlk_vap_manager_get_master_vap(wave_radio_descr_vap_manager_get(hw->radio_descr, 0), &master_vap_handle);
    if (MTLK_ERR_OK == res)
    {
      mips_no = hw_assert_type_to_core_nr(hw, MTLK_CORE_UI_ASSERT_TYPE_FW_UMIPS);
      res = mtlk_vap_get_core_vft(master_vap_handle)->set_prop(master_vap_handle,
                                                               MTLK_CORE_PROP_MAC_STUCK_DETECTED,
                                                               &mips_no,
                                                               sizeof(mips_no));
    }
  }

  return res;
}

static int
_mtlk_mmb_process_bcl(mtlk_hw_t *hw, UMI_BCL_REQUEST* preq, int get_req)
{
  int res     = MTLK_ERR_OK;
  int bcl_ctl = 0;
  mtlk_osal_timestamp_t start_ts;

  /* WARNING: _mtlk_mmb_pas_writel can't be used here since both
   * header and data should came in the same endianness
   */
  _mtlk_mmb_pas_put(hw, "Write unit", SHARED_RAM_BCL_ON_EXCEPTION_UNIT,
                    &preq->Unit, sizeof(preq->Unit));
  _mtlk_mmb_pas_put(hw, "Write size", SHARED_RAM_BCL_ON_EXCEPTION_SIZE,
                    &preq->Size, sizeof(preq->Size));
  _mtlk_mmb_pas_put(hw, "Write adress", SHARED_RAM_BCL_ON_EXCEPTION_ADDR,
                    &preq->Address, sizeof(preq->Address));

  if (get_req)
  {
    bcl_ctl = BCL_READ;
  }
  else
  {
    _mtlk_mmb_pas_put(hw, "", SHARED_RAM_BCL_ON_EXCEPTION_DATA, preq->Data, sizeof(preq->Data));
    bcl_ctl = BCL_WRITE;
  }

  _mtlk_mmb_pas_writel(hw, "Write request", SHARED_RAM_BCL_ON_EXCEPTION_CTL, bcl_ctl);

  /* Wait for the command to get executed, timeout = BCL_ON_EXC_CTL_TIMEOUT, in ms */
  start_ts = mtlk_osal_timestamp();
  do {
    /* Keep balance between performance of this function and PCI bus load. */
    mtlk_udelay(BCL_ON_EXC_CTL_LOOP_DELAY);
    _mtlk_mmb_pas_readl(hw, "Reading BCL request status", SHARED_RAM_BCL_ON_EXCEPTION_CTL, bcl_ctl);
    if (bcl_ctl == BCL_IDLE)
      break;
  } while (mtlk_osal_time_passed_ms(start_ts) < BCL_ON_EXC_CTL_TIMEOUT);

  if (bcl_ctl != BCL_IDLE)
  {
    WLOG_V("Timeout on BCL request SHARED_RAM_BCL_ON_EXCEPTION_CTL");
    res = MTLK_ERR_TIMEOUT;
  }

  if (get_req)
  {
    _mtlk_mmb_pas_get(hw, "", SHARED_RAM_BCL_ON_EXCEPTION_DATA, preq->Data, sizeof(preq->Data));
  }

  return res;
}


static int
_mtlk_mmb_set_rdpool_entry (mtlk_hw_t *hw, uint32 dma_addr, uint32 idx)
{
  int res  = MTLK_ERR_NO_MEM;
  uint32 poolElemSize = hw->chi_area.sBasic.sRdPOOL.u32RdSizeInBytes;
  uint32 poolPayloadOffset = hw->chi_area.sBasic.sRdPOOL.u32PayloadAddressOffsetInRd;
  uint32 poolIndexOffset = hw->chi_area.sBasic.sRdPOOL.u32BdIndexOffsetInRd;
  uint8 *poolStart, *poolElemAddr;
  union {
    struct {
      uint16 rdCount_plus_reserved;
      uint16 bdIndex;
    } fld;
    uint32 bdIndex_word;
  } rx_descriptor_word7 = {.bdIndex_word = 0};

  CARD_SELECTOR_START(hw->ccr->hw_type)
    IF_CARD_PCIEG5 ( poolStart = (uint8 *)(hw->mmb_base + hw->chi_area.sBasic.sRdPOOL.u32RdPoolBaseAddress) );
    IF_CARD_PCIEG6 ( poolStart = (uint8 *)(hw->mmb_base + hw->chi_area.sBasic.sRdPOOL.u32RdPoolBaseAddress) );
  CARD_SELECTOR_END(); /* other types not supported yet */

  poolElemAddr = poolStart + (idx * poolElemSize);

  mtlk_raw_writel(HOST_TO_MAC32(dma_addr),  poolElemAddr + poolPayloadOffset);

  if (_hw_type_is_gen5(hw)) {
    rx_descriptor_word7.fld.bdIndex = HOST_TO_MAC16((u16)(idx & 0xFFFF));
  } else {
    rx_descriptor_word7.bdIndex_word = HOST_TO_MAC32(idx);
  }

  mtlk_raw_writel(rx_descriptor_word7.bdIndex_word, (u8*)((uintptr_t)(poolElemAddr + poolIndexOffset) & (~3)));

  if (idx < 4) {
      ILOG1_DPDD("poolElemAddr[%d] %p, dma %08x, word7 %08x",
               idx, poolElemAddr, dma_addr,
               MAC_TO_HOST32(rx_descriptor_word7.bdIndex_word));
  }

  res = MTLK_ERR_OK;

  return res;
}

static int
_mtlk_mmb_set_mgmt_pool_entry (mtlk_hw_t *hw, uint32 dma_addr, uint32 idx)
{
  int res  = MTLK_ERR_NO_MEM;
  uint32 poolElemSize = hw->chi_area.sBasic.sRdPOOL.u32RdSizeInBytes;
  uint32 poolPayloadOffset = hw->chi_area.sBasic.sRdPOOL.u32PayloadAddressOffsetInRd;
  uint32 poolIndexOffset = hw->chi_area.sBasic.sRdPOOL.u32BdIndexOffsetInRd;
  uint8 *poolStart, *poolElemAddr;
  uint32 pool_offset = hw->chi_area.sBasic.sRdPOOL.u32RdPoolBaseAddress +
                      (hw->chi_area.sBasic.sRdPOOL.u32NumRdsInPool * poolElemSize);
  union {
    struct {
      uint16 rdCount_plus_reserved;
      uint16 bdIndex;
    } fld;
    uint32 bdIndex_word;
  } rx_descriptor_word7 = {.bdIndex_word = 0};

  CARD_SELECTOR_START(hw->ccr->hw_type)
    IF_CARD_PCIEG5 ( poolStart = (uint8 *)(hw->mmb_base + pool_offset));
    IF_CARD_PCIEG6 ( poolStart = (uint8 *)(hw->mmb_base + pool_offset));
  CARD_SELECTOR_END(); /* other types not supported yet */

  poolElemAddr = poolStart + idx * poolElemSize;

  mtlk_raw_writel(HOST_TO_MAC32(dma_addr),  poolElemAddr + poolPayloadOffset);

  if (_hw_type_is_gen5(hw)) {
    rx_descriptor_word7.fld.bdIndex = HOST_TO_MAC16((u16)(idx & 0xFFFF));
  } else {
    rx_descriptor_word7.bdIndex_word = HOST_TO_MAC32(idx);
  }

  mtlk_raw_writel(rx_descriptor_word7.bdIndex_word, (u8*)((uintptr_t)(poolElemAddr + poolIndexOffset) & (~3)));

  if (idx < 4) {
      ILOG1_DPDD("poolElemAddr[%d] %p, dma %08x, word7 %08x",
               idx, poolElemAddr, dma_addr,
               MAC_TO_HOST32(rx_descriptor_word7.bdIndex_word));
  }

  res = MTLK_ERR_OK;

  return res;
}

static int
_mtlk_mmb_prealloc_and_set_rx_data_buffer (mtlk_hw_t                 *hw,
                                      mtlk_hw_data_ind_mirror_t *data_ind)
{
  int res  = MTLK_ERR_NO_MEM;

  data_ind->nbuf = _mtlk_mmb_nbuf_alloc(data_ind->size);
  if (!data_ind->nbuf) {
    ILOG2_D("WARNING: failed to allocate buffer of %u bytes", data_ind->size);
    goto FINISH;
  }

  data_ind->dma_addr = mtlk_df_nbuf_map_to_phys_addr(mtlk_ccr_get_dev_ctx(hw->ccr),
                                                     data_ind->nbuf,
                                                     data_ind->size,
                                                     MTLK_DATA_FROM_DEVICE);
  if (!data_ind->dma_addr) {
    ILOG2_P("WARNING: failed mapping 0x%p to physical address", data_ind->nbuf);
    res = MTLK_ERR_NO_RESOURCES;
    goto FINISH;
  }

  _mtlk_mmb_set_rdpool_entry(hw, data_ind->dma_addr, data_ind->hdr.index);
  res = MTLK_ERR_OK;

FINISH:
  return res;
}

static int
_mtlk_mmb_alloc_and_set_rx_buffer (mtlk_hw_t                 *hw,
                                   mtlk_hw_data_ind_mirror_t *data_ind)
{
  int res  = MTLK_ERR_NO_MEM;

  data_ind->nbuf = _mtlk_mmb_nbuf_alloc(data_ind->size);
  if (!data_ind->nbuf) {
    ILOG2_D("WARNING: failed to allocate buffer of %u bytes", data_ind->size);
    goto FINISH;
  }

  data_ind->dma_addr = mtlk_df_nbuf_map_to_phys_addr(mtlk_ccr_get_dev_ctx(hw->ccr),
                                                     data_ind->nbuf,
                                                     data_ind->size,
                                                     MTLK_DATA_FROM_DEVICE);
  if (!data_ind->dma_addr) {
    ILOG2_P("WARNING: failed mapping 0x%p to physical address", data_ind->nbuf);
    res = MTLK_ERR_NO_RESOURCES;
    goto FINISH;
  }

  ILOG3_PD("hbuff: p=0x%p l=%u", data_ind->nbuf, data_ind->size);
  res = MTLK_ERR_OK;

FINISH:
  return res;
}

static void
_mtlk_mmb_recover_rx_buffers (mtlk_hw_t *hw, uint16 max_buffers)
{
  unsigned i;

  for (i = 0; i < max_buffers; i++) {
    int                        ares     = MTLK_ERR_UNKNOWN;
    mtlk_lslist_entry_t       *lentry   = mtlk_lslist_pop(&hw->rx.pending.lbufs);
    mtlk_hw_data_ind_mirror_t *data_ind = NULL;
    HOST_DSC                   hd;

    if (!lentry) /* no more pending entries */
      break;

    data_ind = MTLK_LIST_GET_CONTAINING_RECORD(lentry,
                                               mtlk_hw_data_ind_mirror_t,
                                               pend_l);

    ares = _mtlk_mmb_alloc_and_set_rx_buffer(hw, data_ind);
    if (ares != MTLK_ERR_OK) {
      _mtlk_mmb_hw_inc_cnt(hw, MTLK_HW_SOURCE_CNT_RX_BUF_REALLOC_FAILED);
      /* Failed again. Put it back to the pending list and stop recovery. */
      mtlk_lslist_push(&hw->rx.pending.lbufs, &data_ind->pend_l);
      break;
    }

    _mtlk_mmb_hw_inc_cnt(hw, MTLK_HW_SOURCE_CNT_RX_BUF_REALLOCATED);

    hd.u32BdIndex         = CPU_TO_LE32(data_ind->hdr.index);
    hd.u32HostPayloadAddr = CPU_TO_LE32(data_ind->dma_addr & ~7);
    hd.u32frameInfo3      = CPU_TO_LE32(MTLK_BFIELD_VALUE(TX_DATA_INFO_SOP_EOP, NO_FRAG, uint32) |
                                        MTLK_BFIELD_VALUE(TX_DATA_INFO_OFFSET,
                                                          data_ind->dma_addr & 7,
                                                          uint32) |
                                        MTLK_BFIELD_VALUE(TX_DATA_INFO_LENGTH,
                                                          data_ind->size,
                                                          uint32));

    /* Succeeded. Send it to MAC as response. */

    _mtlk_mmb_put_hd_to_ring(hw, &hd, &hw->rx.ring);

    ILOG2_DD("Slot#%d (%d bytes) returned to MAC",
         (int)data_ind->hdr.index,
         (int)data_ind->size);
  }
}

static uint32 __MTLK_IFUNC
_mtlk_mmb_on_rx_buffs_recovery_timer (mtlk_osal_timer_t *timer,
                                      mtlk_handle_t      clb_usr_data)
{
  mtlk_hw_t *hw = (mtlk_hw_t*)clb_usr_data;

  MTLK_UNREFERENCED_PARAM(timer);
  /* TODO: correct implementation required if DirectConnect driver is used. */
  _mtlk_mmb_recover_rx_buffers(hw, MTLK_MAX_RX_BUFFS_TO_RECOVER);

  return MTLK_RX_BUFFS_RECOVERY_PERIOD;
}

static void
_mtlk_mmb_power_on (mtlk_hw_t *hw)
{
  uint32 val = 0;

  MTLK_ASSERT(NULL != hw->ccr);

  mtlk_ccr_release_ctl_from_reset(hw->ccr);

  hw->mmb->bist_passed = 1;
  if (hw->mmb->cfg.bist_check_permitted) {
    if (!mtlk_ccr_check_bist(hw->ccr, &val)) {
      ILOG0_D("WARNING: Device self test status: 0x%08lu", (unsigned long)val);
      hw->mmb->bist_passed = 0;
    }
  }

  mtlk_ccr_boot_from_bus(hw->ccr);
  mtlk_ccr_exit_debug_mode(hw->ccr);
  mtlk_ccr_power_on_cpus(hw->ccr);
}

static int
_mtlk_mmb_load_fw2mem(mtlk_cpu_mem_t *mem, mtlk_df_fw_file_buf_t *buf)
{
  uint32 bytes_written = 0;
  int i;

  for(i = 0; (0 != mem[i].length) && (bytes_written < buf->size); i++ )
  {

#ifdef CPTCFG_IWLWAV_FW_WRITE_VALIDATION
  #define FW_WRITE_FUNC _mtlk_mmb_memcpy_validate_toio
#else
  #define FW_WRITE_FUNC _mtlk_mmb_memcpy_toio_swap
#endif
    if(!FW_WRITE_FUNC(mem[i].start, buf->buffer + bytes_written,
                      MIN(mem[i].length, buf->size - bytes_written), mem[i].swap))
    {
        ELOG_V("Failed to put firmware to shared memory");
        return MTLK_ERR_FW;
    }
#undef FW_WRITE_FUNC

    bytes_written += MIN(mem[i].length, buf->size - bytes_written);
    MTLK_ASSERT(bytes_written <= buf->size);

      ILOG1_DPDDD("mem[%d] start %p len %u (0x%X) written %u", i,
          mem[i].start,
          mem[i].length, mem[i].length,
          bytes_written);
  }

  if (bytes_written != buf->size)
  {
    ELOG_DD("Firmware file is to big to fit into the cpu memory (%d > %d)",
         buf->size, bytes_written);
    return MTLK_ERR_FW;
  }

  return MTLK_ERR_OK;
}

/*--- Start of Platform Specific ---*/
void __MTLK_IFUNC   mtlk_psdb_data_cleanup (hw_psdb_t *psdb);
int  __MTLK_IFUNC   mtlk_psdb_file_read_and_parse(mtlk_hw_api_t *hw_api, hw_psdb_t *psdb,
                                                  uint32 chip_id, uint32 hw_type, uint32 hw_rev);

static __INLINE hw_psdb_t *
_mtlk_hw_get_psdb (mtlk_hw_t *hw)
{
    return &hw->psdb;
}

hw_psdb_t *
mtlk_hw_get_psdb (mtlk_hw_t *hw)
{
    MTLK_ASSERT(hw);
    return _mtlk_hw_get_psdb(hw);
}

int __MTLK_IFUNC
mtlk_psdb_load_to_hw (mtlk_hw_t *hw, mtlk_txmm_t *txmm)
{
    MTLK_ASSERT(hw);
    return mtlk_psdb_load_data(hw, txmm, _mtlk_hw_get_psdb(hw));
}

psdb_any_table_t *
mtlk_hw_get_psdb_regd_table (mtlk_hw_t *hw)
{
    MTLK_ASSERT(hw);
    return mtlk_psdb_get_regd_table(_mtlk_hw_get_psdb(hw));
}

uint32 __MTLK_IFUNC
mtlk_hw_get_chip_revision (mtlk_hw_t *hw)
{
    MTLK_ASSERT(hw);
    return mtlk_psdb_get_chip_revision(_mtlk_hw_get_psdb(hw));
}

BOOL __MTLK_IFUNC
wave_hw_is_channel_supported (mtlk_hw_t *hw, uint32 channel)
{
  BOOL  res = FALSE;

  MTLK_ASSERT(hw);
  if (hw) {
    res = (0 != wave_psdb_get_ant_mask_by_channel (mtlk_hw_get_psdb(hw), channel));
  }

  return res;
}

static void
_mtlk_hw_fill_card_info (mtlk_hw_t *hw, mtlk_hw_card_info_t *card_info)
{
    uint32  chip_id, hw_type, hw_vers;

    card_info->chip_id = chip_id = hw->ccr->chip_info->id;
    card_info->hw_type = hw_type = mtlk_eeprom_get_nic_type(hw->ee_data);
    card_info->hw_vers = hw_vers = mtlk_eeprom_get_nic_revision(hw->ee_data);

    card_info->is_gen5 = _chipid_is_gen5(chip_id);
    card_info->is_gen6 = _chipid_is_gen6(chip_id);
    card_info->is_asic = _mtlk_card_is_asic(hw_type);

    if (card_info->is_asic) { /* neither FPGA nor Emulation */
        card_info->is_emul      = FALSE;
        card_info->is_fpga      = FALSE;
        card_info->is_phy_dummy = FALSE;
    } else { /* either FPGA or Emulation */
        card_info->is_emul      = _mtlk_card_is_emul(hw_vers); /* Emulation */
        card_info->is_fpga      = !card_info->is_emul;
        card_info->is_phy_dummy = _mtlk_card_is_phy_dummy(hw_vers);
    }

    ILOG0_DDDD("is_asic:%d, is_emul:%d, is_fpga:%d, is_phy_dummy:%d",
               card_info->is_asic, card_info->is_emul, card_info->is_fpga, card_info->is_phy_dummy);
}

static __INLINE BOOL
__hw_mmb_card_is_asic (mtlk_hw_t *hw)
{
    return hw->card_info.is_asic;
}

BOOL __MTLK_IFUNC
wave_hw_mmb_card_is_asic (mtlk_hw_t *hw)
{
    MTLK_ASSERT(NULL != hw);
    return __hw_mmb_card_is_asic(hw);
}

static __INLINE BOOL
__hw_mmb_card_is_emul (mtlk_hw_t *hw)
{
    return hw->card_info.is_emul;
}

static __INLINE BOOL
__hw_mmb_card_is_fpga (mtlk_hw_t *hw)
{
    return hw->card_info.is_fpga;
}

static __INLINE BOOL
__hw_mmb_card_is_phy_real (mtlk_hw_t *hw)
{
    return !hw->card_info.is_phy_dummy;
}

static __INLINE BOOL
__hw_mmb_card_is_phy_dummy (mtlk_hw_t *hw)
{
    return hw->card_info.is_phy_dummy;
}

BOOL __MTLK_IFUNC
wave_hw_mmb_card_is_phy_dummy (mtlk_hw_t *hw)
{
  return __hw_mmb_card_is_phy_dummy(hw);
}

static __INLINE uint32
__hw_mmb_get_chip_id (mtlk_hw_t *hw)
{
    return hw->card_info.chip_id;
}

static __INLINE uint32
__hw_mmb_get_hw_type (mtlk_hw_t *hw)
{
    return hw->card_info.hw_type;
}

static __INLINE uint32
__hw_mmb_get_hw_vers (mtlk_hw_t *hw)
{
    return hw->card_info.hw_vers;
}

static void
_mtlk_mmb_psdb_cleanup(mtlk_hw_t* hw)
{
    mtlk_psdb_data_cleanup(&hw->psdb);
}

static int
_mtlk_mmb_psdb_read_and_parse(mtlk_hw_t* hw, mtlk_hw_api_t *hw_api)
{
    hw_psdb_t           *psdb;
    wave_ant_params_t   *psdb_ant_params;

    uint32  chip_id = __hw_mmb_get_chip_id(hw);
    uint32  hw_type = __hw_mmb_get_hw_type(hw);
    uint32  hw_vers = __hw_mmb_get_hw_vers(hw);
    int     res;

    psdb = _mtlk_hw_get_psdb(hw);
    res = mtlk_psdb_file_read_and_parse(hw_api, psdb, chip_id, hw_type, hw_vers);

    /* Compare TX anttenas mask from EEPROM and PSDB */
    if (MTLK_ERR_OK == res) {
        uint8 psdb_tx_ant_mask;
        uint8 eeprom_tx_ant_mask;

        psdb_ant_params    = wave_psdb_get_card_ant_params(psdb);
        psdb_tx_ant_mask   = psdb_ant_params->tx_ant_mask;
        eeprom_tx_ant_mask = mtlk_eeprom_get_tx_ant_mask(hw->ee_data);

        if (psdb_tx_ant_mask != 0) { /* found in PSDB */
          if (psdb_tx_ant_mask != eeprom_tx_ant_mask) {
            WLOG_DD("PSDB and EEPROM TX antennas masks are differ: 0x%02X and 0x%02X",
                    psdb_tx_ant_mask, eeprom_tx_ant_mask);

            /* PSDB antennas mask can be a submask of EEPROM antennas mask */
            /*   i.e. PSDB number of antennas <= EEPROM number of antennas */
            if (!_hw_type_is_gen6(hw) && /* FIXME: it is not critical on Gen6 */
                (~eeprom_tx_ant_mask & (eeprom_tx_ant_mask ^ psdb_tx_ant_mask))) {
                ELOG_V("Illegal PSDB antennas mask");
                _mtlk_mmb_psdb_cleanup(hw);
                return MTLK_ERR_EEPROM;
            }
          }
          hw->tx_ant_mask = psdb_ant_params->tx_ant_mask;
          hw->rx_ant_mask = psdb_ant_params->rx_ant_mask;
          hw->tx_ant_num  = psdb_ant_params->tx_ant_num;
          hw->rx_ant_num  = psdb_ant_params->rx_ant_num;
        } else { /* PSDB missing. Use EEPROM data for TX and RX */
          hw->tx_ant_mask = eeprom_tx_ant_mask;
          hw->tx_ant_num  = count_bits_set(hw->tx_ant_mask);
          hw->rx_ant_mask = hw->tx_ant_mask;
          hw->rx_ant_num  = hw->tx_ant_num;
        }
    }

    return res;
}

/*--- End of Platform Specific ---*/

/* Composition of the Firmware file name */

#define FW_AP_NAME_PREFFIX            "ap"
#define FW_STA_NAME_PREFFIX           "sta"

#define FW_NAME_SUFFIX_NONE           ""
#define FW_NAME_SUFFIX_WRX500         "_wrx_500"
#define FW_NAME_SUFFIX_WRX514         "_wrx_514"

#define FW_NAME_SUFFIX_WRX600A        "_wrx_600"
#define FW_NAME_SUFFIX_WRX600B        "_wrx_600b"
#define FW_NAME_SUFFIX_WRX600D2       FW_NAME_SUFFIX_WRX600B

#define FW_PHY_TYPE_SUFFIX_DUMMY      "dummy"
#define FW_PHY_TYPE_SUFFIX_REAL       "real"

#define FW_UPPER_NAME_FMT             "%s_upper_%s%s.bin"
#define FW_LOWER_NAME_FMT             "%s_lower_%s%s.bin"

#define FW_TRANS_NAME_FMT             "tx_sender_%s.bin"
#define FW_RXHANDLER_NAME_FMT         "rx_handler_%s.bin"
#define FW_HOSTIF_NAME_FMT            "host_interface_%s.bin"

#ifdef MTLK_PUMA_PLATFORM
#define FW_HOSTIF_FAST_FMT            "host_interface_%s_dc_mode1.bin"
#define FW_SUFFIX_HOSTIF              "_dc_mode1"
#else
#define FW_HOSTIF_FAST_FMT            "host_interface_%s_dc_mode0.bin"
#define FW_SUFFIX_HOSTIF              "_dc_mode0"
#endif

static __INLINE void
__mtlk_hw_get_fw_name(mtlk_hw_t *hw, uint8 cpu,
                     mtlk_fw_info_t *fw_info,
                     uint32 hw_type_info, uint32 hw_vers_info,
                     uint32 hw_chip_rev, int fast_path)
{
  mtlk_ccr_t *ccr = hw->ccr;
  unsigned hw_chip_rev_idx = MTLK_CHIP_REV_NONE;
  char *chip_name;
  char *fw_suffix;
  char *fw_suffixes[MTLK_CHIP_REV_CNT] = {
                FW_NAME_SUFFIX_NONE, /* not available */
                FW_NAME_SUFFIX_WRX500,
                FW_NAME_SUFFIX_WRX514,
                FW_NAME_SUFFIX_NONE  /* none */
  };

  MTLK_ASSERT(NULL != fw_info);
  MTLK_ASSERT(NULL != ccr);
  MTLK_ASSERT(NULL != ccr->chip_info);
  MTLK_ASSERT(MTLK_CHIP_REV_CNT > hw_chip_rev);

  chip_name = ccr->chip_info->name;

  /* FIXME: compose of the file name can be optimized */

  /*------ Support of HAPS70_FPGA as new type of PCIe card ----------------*/
  if (HW_TYPE_HAPS70_G5 == hw_type_info) /* FPGA */
  {
    char *fmt = "%s%s";
    char *suffix = __hw_mmb_card_is_phy_dummy(hw) ? FW_PHY_TYPE_SUFFIX_DUMMY : FW_PHY_TYPE_SUFFIX_REAL;

    switch (cpu) {
      case CHI_CPU_NUM_UM: fmt = "ap_upper_%s_wrx_500_fpga_%s_phy.bin"; break;
      case CHI_CPU_NUM_LM: fmt = "ap_lower_%s_wrx_500_fpga_%s_phy.bin"; break;
      case CHI_CPU_NUM_TX: fmt = "tx_sender_%s%s.bin";  suffix = "";    break;
      case CHI_CPU_NUM_RX: fmt = "rx_handler_%s%s.bin"; suffix = "";    break;
      case CHI_CPU_NUM_IF: fmt = "host_interface_%s%s.bin";
                           suffix = fast_path ?  FW_SUFFIX_HOSTIF : ""; break;
      default: MTLK_ASSERT(!"Must not be here");
    }

    snprintf(fw_info->fname, sizeof(fw_info->fname), fmt, chip_name, suffix);

    return;
  }

  if ((HW_TYPE_HAPS70_G6 == hw_type_info) || /* FPGA or Emul */
      (HW_TYPE_WRX_600   == hw_type_info))   /* Wave600 ASIC */
  {
    char *fmt = "%s%s";
    char *suffix = "";

    /* "FPGA" suffix has to be added for non ASIC cards */
    char *fpga_suffix = __hw_mmb_card_is_asic(hw) ? "" : "_fpga";

    switch (cpu) {
      case CHI_CPU_NUM_UM: /* Unified RAM */
        /* FW name suffix */
        if(_chipid_is_gen6_b0(ccr->chip_info->id)) {
          fw_suffix = FW_NAME_SUFFIX_WRX600B;
        } else if(_chipid_is_gen6_d2(ccr->chip_info->id)) {
          fw_suffix = FW_NAME_SUFFIX_WRX600D2;
        } else {
          fw_suffix = FW_NAME_SUFFIX_WRX600A;
        }

        /* PHY type suffix */
        suffix = __hw_mmb_card_is_phy_dummy(hw) ? FW_PHY_TYPE_SUFFIX_DUMMY : FW_PHY_TYPE_SUFFIX_REAL;

        snprintf(fw_info->fname, sizeof(fw_info->fname),
                 "ap_ram_%s%s%s_%s_phy.bin", chip_name, fw_suffix, fpga_suffix, suffix);
        return;
      case CHI_CPU_NUM_LM:
        fw_info->fname[0] = '\0'; /* Unified RAM: no firmware required */
        return;
      case CHI_CPU_NUM_IF: fmt = "host_interface_%s%s.bin";
        suffix = fast_path ? FW_SUFFIX_HOSTIF : ""; break;
      case CHI_CPU_NUM_TX:
      case CHI_CPU_NUM_TX_1:
        fmt = "tx_sender_%s%s.bin";  suffix = "";    break;
      case CHI_CPU_NUM_RX:
      case CHI_CPU_NUM_RX_1:
        fmt = "rx_handler_%s%s.bin"; suffix = "";    break;
      default: MTLK_ASSERT(!"Must not be here");
    }

    snprintf(fw_info->fname, sizeof(fw_info->fname), fmt, chip_name, suffix);

    return;
  }

  hw_chip_rev_idx = hw_chip_rev;
  if (MTLK_CHIP_REV_NONE == hw_chip_rev_idx) { /* is not defined */
    if (_mtlk_card_is_wrx500(hw_type_info)) {
      hw_chip_rev_idx = MTLK_CHIP_REV_WRX500;
    } else if (_mtlk_card_is_wrx514(hw_type_info, hw_vers_info)) {
      hw_chip_rev_idx = MTLK_CHIP_REV_WRX514;
    }
    ILOG2_D("hw_chip_revision: apply %d", hw_chip_rev_idx);
  }

  fw_suffix = fw_suffixes[hw_chip_rev_idx];

  switch (cpu) {
    case CHI_CPU_NUM_UM:
      /* There is no separate file for ap and sta, FW can function as both. Use ap prefix */
      snprintf(fw_info->fname, sizeof(fw_info->fname), FW_UPPER_NAME_FMT, FW_AP_NAME_PREFFIX,
                 chip_name, fw_suffix);
      break;

    case CHI_CPU_NUM_LM:
      /* There is no separate file for ap and sta, FW can function as both. Use ap prefix */
        snprintf(fw_info->fname, sizeof(fw_info->fname), FW_LOWER_NAME_FMT, FW_AP_NAME_PREFFIX,
                    chip_name, fw_suffix);
      break;

    case CHI_CPU_NUM_TX:
      snprintf(fw_info->fname, sizeof(fw_info->fname), FW_TRANS_NAME_FMT, chip_name);
      break;

    case CHI_CPU_NUM_IF:
      if (_chipid_is_gen5(ccr->chip_info->id) && fast_path)
        snprintf(fw_info->fname, sizeof(fw_info->fname), FW_HOSTIF_FAST_FMT, chip_name);
      else
        snprintf(fw_info->fname, sizeof(fw_info->fname), FW_HOSTIF_NAME_FMT, chip_name);
      break;

    case CHI_CPU_NUM_RX:
      snprintf(fw_info->fname, sizeof(fw_info->fname), FW_RXHANDLER_NAME_FMT, chip_name);
      break;

    default: MTLK_ASSERT(!"Must not be here");
  }
}

BOOL __MTLK_IFUNC
wave_hw_radio_band_cfg_is_single (mtlk_hw_t *hw)
{
  BOOL res = TRUE;
  MTLK_ASSERT(NULL != hw);

  if (NULL != hw) {
    res = (WAVE_HW_RADIO_BAND_CFG_SB == hw->radio_band_cfg);
  }

  return res;
}

static int
_mtlk_mmb_load_firmware(mtlk_hw_t* hw)
{
  mtlk_df_fw_file_buf_t fb;
  mtlk_fw_info_t fw_info;
  int cpu = 0;
  int res = MTLK_ERR_FW;

  mtlk_osal_lock_acquire(&hw->version_lock);
  hw->if_version[0] = 0;
  mtlk_osal_lock_release(&hw->version_lock);

  for(cpu = 0; cpu < CHI_CPU_NUM_CNT; cpu++)
  {
    if (MTLK_ERR_OK != mtlk_ccr_get_fw_info(hw->ccr, cpu, &fw_info)) {
      continue; /* cpu is not supported, no FW required */
    }

    /* FIXME: Don't load second band in single band mode */
    if ((WAVE_HW_RADIO_BAND_CFG_SB == hw->radio_band_cfg) &&
        ((CHI_CPU_NUM_TX_1 == cpu) || (CHI_CPU_NUM_RX_1 == cpu))) {
      WLOG_V("Skip loading second band CPU due to single band mode");
      continue;
    }

    __mtlk_hw_get_fw_name(hw, cpu, &fw_info,
                        mtlk_eeprom_get_nic_type(hw->ee_data),
                        mtlk_eeprom_get_nic_revision(hw->ee_data),
                        mtlk_hw_get_chip_revision(hw),
                        __mtlk_mmb_fastpath_available(hw));

    /* Gen6: do it for the first radio only (FW bins are the same for both radios) */
    res = mtlk_hw_load_file(wave_radio_descr_hw_api_get(hw->radio_descr, 0), fw_info.fname, &fb);
    if (res != MTLK_ERR_OK)
    {
      ELOG_S("can not load file '%s'", fw_info.fname);
      break;
    }

    ILOG1_SD("Loading '%s' of %d bytes", fw_info.fname, fb.size);

    res = _mtlk_mmb_load_fw2mem(fw_info.mem, &fb);

    if (MTLK_ERR_OK == res) {
      _mtlk_mmb_notify_firmware(hw, cpu, fw_info.fname, &fb);
    }

    /* WAVE600: TODO: do it for the first radio now, but rework later! */
    mtlk_hw_unload_file(wave_radio_descr_hw_api_get(hw->radio_descr, 0), &fb);

    if (res != MTLK_ERR_OK)
    {
      ELOG_S("'%s' load timed out or interrupted", fw_info.fname);
      break;
    }
  }

  return res;
}

int __MTLK_IFUNC
mtlk_hw_get_fw_dump_info (mtlk_hw_t *hw, wave_fw_dump_info_t **fw_info)
{
  MTLK_ASSERT(NULL != hw);

  return mtlk_ccr_get_fw_dump_info(hw->ccr, fw_info);
}

int __MTLK_IFUNC
mtlk_hw_get_hw_dump_info (mtlk_hw_t *hw, wave_hw_dump_info_t **hw_dump_info)
{
  MTLK_ASSERT(NULL != hw);

  return mtlk_ccr_get_hw_dump_info(hw->ccr, hw_dump_info);
}

#define PROGMODEL_MODIFIED_MASK 0x80000000u
#define PROGMODEL_VERSION_MASK  0x7FFFFFFFu

static void prepare_progmodel_string(mtlk_hw_t *hw, const mtlk_core_firmware_file_t *ff)
{
  uint32 version = 0;
  int res = MTLK_ERR_FW;
  int len = 0;
  BOOL modified = FALSE;
  mtlk_vap_handle_t master_vap_handle;

  /* We should not read this in dummy phy versions */
  if (__hw_mmb_card_is_phy_real(hw)) {
      version = mtlk_ccr_get_progmodel_version(hw->ccr);
  }

  modified = !!(version & PROGMODEL_MODIFIED_MASK);
  version &= PROGMODEL_VERSION_MASK;

  mtlk_osal_lock_acquire(&hw->version_lock);

  len = mtlk_snprintf(hw->progmodel, STRING_PROGMODEL_SIZE, "ProgModel: %s version %u%s\n",
                      ff->fname, version, modified ? " (modified)" : "");

  hw->progmodel[STRING_PROGMODEL_SIZE - 1] = '\0';

  mtlk_osal_lock_release(&hw->version_lock);

  /* WAVE600: TODO: do it for the first radio now, but rework later! */
  res = mtlk_vap_manager_get_master_vap(wave_radio_descr_vap_manager_get(hw->radio_descr, 0), &master_vap_handle);
  if (len > 0 && res == MTLK_ERR_OK)
  {
    ILOG0_DS("CID-%04x: %s", mtlk_vap_get_oid(master_vap_handle), hw->progmodel);
  }
}

static unsigned
_mtlk_mmb_get_shram_progmodel_addr (mtlk_hw_t *hw)
{
  /* Take address for Progmodel loading (i.e. PAS offset) from CHI area */
  return hw->chi_area.sCalibr.sData.pPsdProgmodelAddress;
}

static int
_mtlk_mmb_load_progmodel_to_hw (mtlk_hw_t *hw, const wave_progmodel_data_t *pm_data)
{
  int                    res       = MTLK_ERR_FW;
  unsigned int           loc       = 0;
  unsigned int           pm_addr;
  mtlk_txmm_msg_t        man_msg;
  mtlk_txmm_data_t      *man_entry = NULL;
  mtlk_core_firmware_file_t *ff;

#if 1 /* FIXME: to be removed */
  UMI_PROG_MODEL_20_40_DIFFS_PARAMS *umi_params;
#endif

  man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, pm_data->txmm, NULL);
  if (!man_entry) {
    ELOG_V("Can not get TXMM slot");
    goto FINISH;
  }

  pm_addr = _mtlk_mmb_get_shram_progmodel_addr(hw);
  ff = pm_data->ff;
  ILOG2_DDS("ProgmodelAddress 0x%04X, ftype %d, fname %s", pm_addr, ff->ftype, ff->fname);

#if 1 /* FIXME: to be removed */
  umi_params = (UMI_PROG_MODEL_20_40_DIFFS_PARAMS *)man_entry->payload;
#endif

  /* compose man_msg depending on the file type */
  switch(ff->ftype) {
    default:
    case MTLK_FILE_TYPE_HW:
      man_entry->id           = UM_DOWNLOAD_PROG_MODEL_REQ;
      man_entry->payload_size = 0;
      break;
    case MTLK_FILE_TYPE_ANT_TX:
      man_entry->id           = UM_MAN_DOWNLOAD_PROG_MODEL_ANT_DEPENDENT_TX_REQ;
      man_entry->payload_size = 0;
      break;
    case MTLK_FILE_TYPE_ANT_RX:
      man_entry->id           = UM_MAN_DOWNLOAD_PROG_MODEL_ANT_DEPENDENT_RX_REQ;
      man_entry->payload_size = 0;
      break;
  }

  while (loc < ff->fcontents.size) {
    unsigned int left;
    uint32 zero_padding[3] = {0,0,0};

    if ((ff->fcontents.size - loc) >  PROGMODEL_CHUNK_SIZE)
      left = PROGMODEL_CHUNK_SIZE;
    else
      left = ff->fcontents.size - loc;

    _mtlk_mmb_pas_put(hw, "",  pm_addr, ff->fcontents.buffer + loc, left);
    _mtlk_mmb_pas_put(hw, "",  pm_addr + left, zero_padding, sizeof(zero_padding));
    ILOG4_DD("wrote %d bytes to PAS offset 0x%x\n", left, pm_addr);

    res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_PRGMDL_LOAD_CHUNK_TIMEOUT);
    if (res != MTLK_ERR_OK) {
      ELOG_D("Can't download programming model, timed-out. Err#%d", res);
#if 1
      /* a2k - do not exit - allow to connect to driver through BCL for debugging */
      res = MTLK_ERR_OK;
#endif
      goto FINISH;
    }

    loc+= left;
    ILOG3_DD("loc %d, left %d", loc, left);
  }

  ILOG3_V("End program mode");

  if (ff->update_version)
  {
    prepare_progmodel_string(hw, ff);
  }

  res = MTLK_ERR_OK;

FINISH:

  if (man_entry)
    mtlk_txmm_msg_cleanup(&man_msg);

  return res;
}

static void
_mtlk_mmb_run_firmware(mtlk_hw_t *hw)
{
  MTLK_ASSERT(NULL != hw->ccr);

  mtlk_ccr_disable_interrupts(hw->ccr, MTLK_IRQ_ALL);
  mtlk_ccr_clear_interrupts_if_pending(hw->ccr, (uint32) -1);
  mtlk_ccr_switch_to_iram_boot(hw->ccr);
  hw->perform_isr_action = isr_action_init;
  MTLK_HW_INIT_EVT_RESET(hw);
  mtlk_ccr_enable_interrupts(hw->ccr, MTLK_IRQ_ALL);
  mtlk_ccr_release_cpus_reset(hw->ccr);
#ifdef CPTCFG_IWLWAV_LEGACY_INT_RECOVERY_MON
  mtlk_osal_timer_set(&hw->recover_timer, MTLK_INTERRUPT_RECOVERY_PERIOD);
#endif
}

static int
_wave_chi_extension_parse(mtlk_hw_t *hw)
{
  struct chi_ext_entry {
    VECTOR_AREA_EXTENSION_HEADER *ext_hdr;
    uint32 ext_id;
    uint32 data_size;
  };

  int i;

  /* Only extension vectors used in Driver are parsed */
  struct chi_ext_entry chi_ext_tab[] = {
    {
      &hw->chi_area.sCalibr.sHeader,
      VECTOR_AREA_CALIBR_EXTENSION_ID,
      sizeof(VECTOR_AREA_CALIBR_EXTENSION_DATA)
    },
    {
      &hw->chi_area.sMipsControl.sHeader,
      VECTOR_AREA_MIPS_CONTROL_EXTENSION_ID,
      sizeof(VECTOR_AREA_MIPS_CONTROL_EXTENSION_DATA)
    },
    {
      &hw->chi_area.sLoggerExt.sHeader,
      VECTOR_AREA_LOGGER_EXTENSION_ID,
      sizeof(VECTOR_AREA_LOGGER_EXTENSION_DATA)
    },
    {
      &hw->chi_area.sLoggerBufInfoExt.sHeader,
      VECTOR_AREA_LOGGER_BUF_INFO_EXTENSION_ID,
      sizeof(VECTOR_AREA_LOGGER_BUF_INFO_EXTENSION_DATA)
    },
    {
      &hw->chi_area.sLoggerRdExt.sHeader,
      VECTOR_AREA_LOGGER_RD_EXTENSION_ID,
      sizeof(VECTOR_AREA_LOGGER_RD_EXTENSION_DATA)
    },
#ifdef MTLK_LEGACY_STATISTICS
    {
      &hw->chi_area.sStatisticsManager.sHeader,
      VECTOR_AREA_STATISTICS_MANAGER_EXTENSION_ID,
      sizeof(VECTOR_AREA_STATISTICS_MANAGER_EXTENSION_DATA)
    },
#endif
    {
      &hw->chi_area.sHdFragmentationRingExt.sHeader,
      VECTOR_AREA_FRAGMENTATION_RING_EXTENSION_ID,
      sizeof(VECTOR_AREA_FRAGMENTATION_RING_EXTENSION_DATA)
    },
    {
      &hw->chi_area.sDebugExt.sHeader,
      VECTOR_AREA_VECTOR_AREA_DEBUG_EXTENSION_ID,
      sizeof(VECTOR_AREA_DEBUG_EXTENSION_DATA)
    },
  };

  struct chi_ext_entry *p_chi_ext = chi_ext_tab;

  for (i = 0; i < ARRAY_SIZE(chi_ext_tab); i++, p_chi_ext++) {
    VECTOR_AREA_EXTENSION_HEADER *ext_hdr = p_chi_ext->ext_hdr;

    ILOG2_DDDDD("CHI ext ID 0x%02X header: offset 0x%04X, magic 0x%08X, ID 0x%02X, size 0x%02X",
                p_chi_ext->ext_id,
                (wave_addr)ext_hdr - (wave_addr)&hw->chi_area,
                ext_hdr->u32ExtensionMagic, ext_hdr->u32ExtensionID, ext_hdr->u32ExtensionDataSize);

    if (ext_hdr->u32ExtensionMagic != HOST_EXTENSION_MAGIC) {
      ELOG_DD("Wrong magic 0x%08X for CHI ext ID %d", ext_hdr->u32ExtensionMagic, p_chi_ext->ext_id);
      return MTLK_ERR_HW;
    }
    if (ext_hdr->u32ExtensionID != p_chi_ext->ext_id) {
      ELOG_DD("Unknown CHI ext ID %d, expecting %d", ext_hdr->u32ExtensionID, p_chi_ext->ext_id);
      return MTLK_ERR_HW;
    }
    if (ext_hdr->u32ExtensionDataSize != p_chi_ext->data_size) {
      ELOG_DDD("Wrong data size %d for CHI ext ID %d, expecting %d",
              ext_hdr->u32ExtensionDataSize,
              ext_hdr->u32ExtensionID, p_chi_ext->data_size);
      return MTLK_ERR_HW;
    }
  }

  /* TODO: remove copy, use values from hw->chi_area directly */
#if (IWLWAV_RTLOG_FLAGS & RTLF_REMOTE_ENABLED)
  hw->fw_log_is_supported = 1;
  hw->fw_log_is_supported_ex = 1;
#else /* #if (IWLWAV_RTLOG_FLAGS & RTLF_REMOTE_ENABLED) */
  WLOG_V("Runtime logger is not supported by driver. Firmware logging suppressed.");
  hw->fw_log_is_supported = 0;
  hw->fw_log_is_supported_ex = 0;
#endif /* #if (IWLWAV_RTLOG_FLAGS & RTLF_REMOTE_ENABLED) */

  return MTLK_ERR_OK;
}


#define WAVE_SIZE_TO_U32(n_) ((n_) / sizeof(uint32))

static void
_wave_hw_chi_area_write(void *dst, void *src, size_t data_size, uint32 ext_id)
{
  uint32 *dst_data, *src_data;
  VECTOR_AREA_EXTENSION_HEADER *ext_hdr = (VECTOR_AREA_EXTENSION_HEADER *)dst;
  ext_hdr->u32ExtensionMagic = HOST_EXTENSION_MAGIC;
  ext_hdr->u32ExtensionID = ext_id;
  ext_hdr->u32ExtensionDataSize = data_size;

  WLOG_PD("hdr %p, id %d", ext_hdr, ext_id);

  dst_data = (uint32 *)(dst + sizeof(*ext_hdr));
  src_data = (uint32 *)src;
  data_size = WAVE_SIZE_TO_U32(data_size);
  while (data_size--) {
    WLOG_PD("dst %p, data 0x%02X", dst_data, *src_data);
    *dst_data++ = *src_data++;
  }
}

static void
_wave_hw_mem_host_to_mac32(void *dst, void *src, size_t data_size)
{
  uint32 *dst_data = (uint32 *)dst;
  uint32 *src_data = (uint32 *)src;
  data_size = WAVE_SIZE_TO_U32(data_size);
  while (data_size--) {
    *dst_data++ = HOST_TO_MAC32(*src_data++);
  }
}

static void
_wave_hw_mem_mac_to_host32(void *dst, void *src, size_t data_size)
{
  uint32 *dst_data = (uint32 *)dst;
  uint32 *src_data = (uint32 *)src;
  data_size = WAVE_SIZE_TO_U32(data_size);
  while (data_size--) {
    *dst_data++ = MAC_TO_HOST32(*src_data++);
  }
}

static void
_mtlk_mmb_chi_init(mtlk_hw_t *hw)
{
  /* cleanup cached values */
  memset (&hw->chi_area, 0, sizeof(hw->chi_area));

  /* Fill up FW memory with initial configuration */
  MTLK_STATIC_ASSERT(sizeof(hw->fw_card_cfg) == sizeof(hw->chi_area.sCardConfigExt.sData));
  _wave_hw_chi_area_write(&hw->chi_area.sCardConfigExt, &hw->fw_card_cfg.words[0], sizeof(hw->fw_card_cfg),
                          VECTOR_AREA_CARD_CONFIGURATION_EXTENSION_ID);

  /* init FW memory */
  _wave_hw_mem_host_to_mac32(&hw->chi_area, &hw->chi_area, sizeof(hw->chi_area));
  _mtlk_mmb_pas_put(hw,
                    "CHI Vector Area",
                    CHI_ADDR,
                    &hw->chi_area,
                    sizeof(hw->chi_area));

  return;
}

#if defined(CPTCFG_IWLWAV_G6_FPGA_IRAM_TEST)
void iram_rw_test(void *ccr_mem);
void hw_mmb_iram_rw_test(mtlk_hw_t *hw) {
  if (_hw_type_is_gen6(hw)) {
    iram_rw_test(&hw->ccr->mem);
  }
}
#endif

static void
_wave_hw_mem_mac_to_host32(void *dst, void *src, size_t data_size);

static int
_mtlk_mmb_wait_chi_magic(mtlk_hw_t *hw)
{
  typedef enum
  {
    MTLK_HW_INIT_EVT_WAIT_RES_OK,
    MTLK_HW_INIT_EVT_WAIT_RES_FAILED,
    MTLK_HW_INIT_EVT_WAIT_RES_POLLING,
    MTLK_HW_INIT_EVT_WAIT_RES_LAST
  } mtlk_hw_init_evt_wait_res_e;

  mtlk_hw_init_evt_wait_res_e wait_res = MTLK_HW_INIT_EVT_WAIT_RES_LAST;
  uint32 timeout;
  int res;

#ifdef CPTCFG_IWLWAV_USE_INTERRUPT_POLLING
  mtlk_osal_timestamp_t start_ts = mtlk_osal_timestamp();
#endif

  MTLK_ASSERT(NULL != hw->ccr);

  /* Check for the magic value and then get the base address and length of the CHI area */
  timeout = __hw_mmb_card_is_asic(hw) ?
                MTLK_CHI_MAGIC_TIMEOUT_ASIC : MTLK_CHI_MAGIC_TIMEOUT_EMUL;

  ILOG0_D("Wait for CHI Magic (%u ms)...", timeout);
  wait_res =
    (MTLK_HW_INIT_EVT_WAIT(hw, timeout) == MTLK_ERR_OK)?
      MTLK_HW_INIT_EVT_WAIT_RES_OK:MTLK_HW_INIT_EVT_WAIT_RES_FAILED;

#ifdef CPTCFG_IWLWAV_USE_INTERRUPT_POLLING
  wait_res = MTLK_HW_INIT_EVT_WAIT_RES_POLLING;

  do
  {
#endif
    if (hw->irq_mode != MTLK_IRQ_MODE_LEGACY)
      (void)mtlk_ccr_clear_interrupts(hw->ccr, MTLK_IRQ_LEGACY);

    _mtlk_mmb_pas_get(hw,
                      "CHI Vector Area",
                      CHI_ADDR,
                      &hw->chi_area,
                      sizeof(hw->chi_area));

    _wave_hw_mem_mac_to_host32(&hw->chi_area, &hw->chi_area, sizeof(hw->chi_area));

    if (wait_res != MTLK_HW_INIT_EVT_WAIT_RES_FAILED &&
        hw->chi_area.sBasic.u32Magic == HOST_MAGIC) {
      res = _wave_chi_extension_parse(hw);
      if (MTLK_ERR_OK != res) {
        ELOG_V("Failed to parse CHI extensions");
      }
      return res;
    }

#ifdef CPTCFG_IWLWAV_USE_INTERRUPT_POLLING
    mtlk_osal_msleep(100);
  }
  while( mtlk_osal_time_passed_ms(start_ts) <= timeout );
#endif

  ELOG_DD("Wait for CHI Magic failed: res %d, Magic 0x%08x",
          wait_res, hw->chi_area.sBasic.u32Magic);

  if (_hw_type_is_gen5(hw)) {
    ELOG_DD("Core info: LM 0x%08X, UM 0x%08X",
             hw->chi_area.sDebugExt.sData.u32Core0_info, hw->chi_area.sDebugExt.sData.u32Core2_info);
  } else { /* gen6 */
    ELOG_DDD("Core info: LM0 0x%08X, LM1 0x%08X, UM 0x%08X",
             hw->chi_area.sDebugExt.sData.u32Core0_info,
             hw->chi_area.sDebugExt.sData.u32Core1_info,
             hw->chi_area.sDebugExt.sData.u32Core2_info);
  }

  return MTLK_ERR_HW;
}

int
_mtlk_mmb_send_hw_dep_cfg (mtlk_hw_t *hw)
{
  int                           res;
  mtlk_txmm_msg_t               man_msg;
  mtlk_txmm_data_t              *man_entry;
  UmiHwDependentConfig_t        *hw_cfg_msg;
  uint32                        xtal;
  uint32                        afe_value[2] = {0};

  man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, &hw->txmm, NULL);

  if (!man_entry) {
    ELOG_V("Can not get TXMM slot");
    return MTLK_ERR_NO_RESOURCES;
  }

  xtal = mtlk_eeprom_get_xtal_value(hw->ee_data);
  mtlk_ccr_get_afe_value(hw->ccr, afe_value);
  ILOG0_DDD("XTAL value 0x%02X, AFE values: 0x%08X, 0x%08X", xtal, afe_value[0], afe_value[1]);

  man_entry->id = UM_MAN_HW_DEPENDENT_CONFIG_REQ;
  man_entry->payload_size = sizeof(UmiHwDependentConfig_t);

  hw_cfg_msg = (UmiHwDependentConfig_t *) man_entry->payload;
  hw_cfg_msg->xtal          = HOST_TO_MAC32(xtal);
  hw_cfg_msg->AFEvalue[0]   = HOST_TO_MAC32(afe_value[0]);
  hw_cfg_msg->AFEvalue[1]   = HOST_TO_MAC32(afe_value[1]);

  res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);
  mtlk_txmm_msg_cleanup(&man_msg);

  if (res != MTLK_ERR_OK) {
    ELOG_D("FW set HW dependent configuration failed (err=%d)", res);
  }

  return res;
}

int __MTLK_IFUNC
_mtlk_mmb_send_fw_log_severity(mtlk_hw_t *hw,
                               uint32 newLevel,
                               uint32 targetCPU)
{
  int                           res;
  mtlk_txmm_msg_t               man_msg;
  mtlk_txmm_data_t              *man_entry;

  if (targetCPU >= UMI_CPU_ID_MAX) {
    ELOG_D("Incorrect targetCPU=%d", targetCPU);
    return MTLK_ERR_PARAMS;
  }

  man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, &hw->txdm, NULL);
  if (!man_entry) {
    ELOG_V("Can not get TXMM slot");
    return MTLK_ERR_NO_RESOURCES;
  }

  ILOG2_DD("newLevel=%d, targetCPU=%d.", newLevel, targetCPU);

  man_entry->id = UM_DBG_LOGGER_SET_SEVERITY_REQ;
  man_entry->payload_size = sizeof(UmiLoggerMsgSetSeverity_t);
  ((UmiLoggerMsgSetSeverity_t *) man_entry->payload)->newLevel  = HOST_TO_MAC32(newLevel);
  ((UmiLoggerMsgSetSeverity_t *) man_entry->payload)->targetCPU = HOST_TO_MAC32(targetCPU);

  res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);
  mtlk_txmm_msg_cleanup(&man_msg);

  if (res != MTLK_ERR_OK) {
    ELOG_D("FW logger severity configuration failed (err=%d)", res);
  }

  return res;
}

static void
_mtlk_mmb_cleanup_man_req_bdr (mtlk_hw_t *hw)
{
  if (hw->flags & HW_FLAGS_PREPARED_MAN_REQ_BDR) {
    _mtlk_advanced_bdr_cleanup(&hw->tx_man);
    hw->flags &= ~HW_FLAGS_PREPARED_MAN_REQ_BDR;
  }
}

static void
_mtlk_mmb_cleanup_man_ind_bdr (mtlk_hw_t *hw)
{
  if (hw->flags & HW_FLAGS_PREPARED_MAN_IND_BDR) {
    _mtlk_basic_bdr_cleanup(&hw->rx_man);
    hw->flags &= ~HW_FLAGS_PREPARED_MAN_IND_BDR;
  }
}

static void
_mtlk_mmb_cleanup_dbg_req_bdr (mtlk_hw_t *hw)
{
  if (hw->flags & HW_FLAGS_PREPARED_DBG_REQ_BDR) {
    _mtlk_advanced_bdr_cleanup(&hw->tx_dbg);
    hw->flags &= ~HW_FLAGS_PREPARED_DBG_REQ_BDR;
  }
}

static void
_mtlk_mmb_cleanup_dbg_ind_bdr (mtlk_hw_t *hw)
{
  if (hw->flags & HW_FLAGS_PREPARED_DBG_IND_BDR) {
    _mtlk_basic_bdr_cleanup(&hw->rx_dbg);
    hw->flags &= ~HW_FLAGS_PREPARED_DBG_IND_BDR;
  }
}

static void
_mtlk_mmb_free_hd_ring (mtlk_hw_t *hw, mtlk_ring_buffer *ring)
{
    uint32 ring_size_in_bytes = ring->size * sizeof(HOST_DSC);
    mtlk_osal_mem_dma_free_coherent(mtlk_ccr_get_dev_ctx(hw->ccr),
        ring_size_in_bytes, ring->base_addr, ring->dma_addr);
}

static void
_mtlk_mmb_cleanup_data_req_bdr (mtlk_hw_t *hw)
{
  if (hw->flags & HW_FLAGS_PREPARED_DATA_REQ_BDR) {
    _mtlk_mmb_free_unconfirmed_tx_buffers(hw);
    _mtlk_advanced_bdr_cleanup(&hw->tx.bdr_data);
    _mtlk_mmb_free_hd_ring(hw, &hw->tx.ring);
#ifdef BD_DBG
    if (hw->tx.used_bd_map) mtlk_osal_mem_free((void *)hw->tx.used_bd_map);
#endif
    hw->flags &= ~HW_FLAGS_PREPARED_DATA_REQ_BDR;
  }
}

static void
_mtlk_mmb_cleanup_data_ind_bdr (mtlk_hw_t *hw)
{
  if (hw->flags & HW_FLAGS_PREPARED_DATA_IND_BDR) {
    _mtlk_basic_bdr_cleanup(&hw->rx.bdr_data);

    /* if DirectConnect path is available, all rings allocations are done by DC driver */
    if (!mtlk_mmb_dcdp_path_available(hw))
      _mtlk_mmb_free_hd_ring(hw, &hw->rx.ring);

    hw->flags &= ~HW_FLAGS_PREPARED_DATA_IND_BDR;
  }
}

static void
_mtlk_mmb_cleanup_log_ind_bdr (mtlk_hw_t *hw)
{
  MTLK_ASSERT(hw->fw_log_is_supported);

  if (hw->flags & HW_FLAGS_PREPARED_LOG_IND_BDR) {
    _mtlk_basic_bdr_cleanup(&hw->fw_log_buffers);
    hw->flags &= ~HW_FLAGS_PREPARED_LOG_IND_BDR;
  }
}

/* Use some wrappers to proper reflect the line numbers by LOG_CHI_xxxx */
#define LOG_CHI_AREA(d) __log_chi_area(__LINE__, #d, &hw->chi_area.sBasic.d)
#define LOG_CHI_RING(d) __log_chi_ring(__LINE__, #d, &hw->chi_area.sBasic.d)
#define LOG_CHI_POOL(d) __log_chi_pool(__LINE__, #d, &hw->chi_area.sBasic.d)

static void
__log_chi_area(uint32 line, char *name, CHI_MEM_AREA_DESC *desc)
{
    ILOG0_DSDDDD("Line %d: CHI: %s: is=0x%x in=%d rs=0x%x rn=%d",
        line, name,
        desc->u32IndStartOffset,
        desc->u32IndNumOfElements,
        desc->u32ReqStartOffset,
        desc->u32ReqNumOfElements);
}

static void
__log_chi_ring(uint32 line, char *name, CHI_MEM_RING *desc)
{
#if MTLK_USE_DIRECTCONNECT_DP_API
    ILOG0_DSDDDDDD("Line %d: CHI: %s: inCounter=0x%x inStatus=0x%x outCounter=0x%x outStatus=0x%x inCounter_BE=0x%x outFreedCounter_BE=0x%x",
#else
    ILOG0_DSDDDD  ("Line %d: CHI: %s: inCounter=0x%x inStatus=0x%x outCounter=0x%x outStatus=0x%x",
#endif
        line, name,
        desc->u32InCounterAddress,
        desc->u32InStatusAddress,
        desc->u32OutCounterAddress,
        desc->u32OutStatusAddress
#if MTLK_USE_DIRECTCONNECT_DP_API
        , desc->u32InCounterAddressBigEnd
        , desc->u32OutFreedCounterAddressBigEnd
#endif
        );
}

static void
__log_chi_pool(uint32 line, char *name, CHI_MEM_RD_POOL *desc)
{
    ILOG0_DSDDDDDD("Line %d: CHI: %s: RdPoolBaseAddress=0x%x RdSizeInBytes=0x%x PayloadAddressOffsetInRd=0x%x BdIndexOffsetInRd=0x%x NumRdsInPool=%d NumMgtRdsInPool=%d",
        line, name,
        desc->u32RdPoolBaseAddress,
        desc->u32RdSizeInBytes,
        desc->u32PayloadAddressOffsetInRd,
        desc->u32BdIndexOffsetInRd,
        desc->u32NumRdsInPool,
        desc->u32NumMgtRdsInPool);
}

static int
_mtlk_mmb_prepare_man_req_bdr(mtlk_hw_t *hw)
{
  /* Management Requests BD initialization */
  int    res     = MTLK_ERR_UNKNOWN;
  uint32 nof_bds = hw->chi_area.sBasic.sMAN.u32ReqNumOfElements;
  mtlk_ring_buffer *ring = &hw->ctrl.ring;

  MTLK_ASSERT(nof_bds < ((uint8)-1));

  mtlk_ccr_ctrl_ring_init(hw->ccr, &ring->regs, hw->mmb_base);

  res = _mtlk_advanced_bdr_init(&hw->tx_man,
                                (uint32)nof_bds,
                                hw->mmb_pas + hw->chi_area.sBasic.sMAN.u32ReqStartOffset,
                                _mtlk_mmb_cm_bdr_get_iom_bd_req_size(TRUE),
                                sizeof(mtlk_hw_cm_req_mirror_t) + hw->mmb->cfg.man_req_msg_size);
  if (res == MTLK_ERR_OK) {
    _mtlk_mmb_dbg_init_msg_verifier(&hw->tx_man.basic);
    hw->flags |= HW_FLAGS_PREPARED_MAN_REQ_BDR;
  }

  return res;
}

static int
_mtlk_mmb_prepare_man_ind_bdr(mtlk_hw_t *hw)
{
  int res = MTLK_ERR_UNKNOWN;

  /* Management Indications BD initialization */
  uint32 nof_bds = hw->chi_area.sBasic.sMAN.u32IndNumOfElements;

  MTLK_ASSERT(nof_bds < ((uint8)-1));

  res = _mtlk_basic_bdr_init(&hw->rx_man,
                             (uint8)nof_bds,
                             hw->mmb_pas + hw->chi_area.sBasic.sMAN.u32IndStartOffset,
                             _mtlk_mmb_cm_bdr_get_iom_bd_ind_size(TRUE),
                             sizeof(mtlk_hw_cm_ind_mirror_t) + hw->mmb->cfg.man_ind_msg_size);
  if (res == MTLK_ERR_OK) {
    hw->flags |= HW_FLAGS_PREPARED_MAN_IND_BDR;
  }

  return res;
}

static int
_mtlk_mmb_prepare_dbg_req_bdr(mtlk_hw_t *hw)
{
  /* DBG Requests BD initialization */
  int    res     = MTLK_ERR_UNKNOWN;
  uint32 nof_bds = hw->chi_area.sBasic.sDBG.u32ReqNumOfElements;

  MTLK_ASSERT(nof_bds < ((uint8)-1));

  res = _mtlk_advanced_bdr_init(&hw->tx_dbg,
                                (uint32)nof_bds,
                                hw->mmb_pas + hw->chi_area.sBasic.sDBG.u32ReqStartOffset,
                                _mtlk_mmb_cm_bdr_get_iom_bd_req_size(FALSE),
                                sizeof(mtlk_hw_cm_req_mirror_t) + hw->mmb->cfg.dbg_msg_size);

  if (res == MTLK_ERR_OK) {
    _mtlk_mmb_dbg_init_msg_verifier(&hw->tx_dbg.basic);
    hw->flags |= HW_FLAGS_PREPARED_DBG_REQ_BDR;
  }

  return res;
}

static int
_mtlk_mmb_prepare_dbg_ind_bdr(mtlk_hw_t *hw)
{
  int res = MTLK_ERR_UNKNOWN;
  /* DBG Indications BD initialization */
  uint32 nof_bds = hw->chi_area.sBasic.sDBG.u32IndNumOfElements;

  MTLK_ASSERT(nof_bds < ((uint8)-1));

  res = _mtlk_basic_bdr_init(&hw->rx_dbg,
                             (uint8)nof_bds,
                             hw->mmb_pas + hw->chi_area.sBasic.sDBG.u32IndStartOffset,
                             _mtlk_mmb_cm_bdr_get_iom_bd_ind_size(FALSE),
                             sizeof(mtlk_hw_cm_ind_mirror_t) + hw->mmb->cfg.dbg_msg_size);
  if (res == MTLK_ERR_OK) {
    hw->flags |= HW_FLAGS_PREPARED_DBG_IND_BDR;
  }

  return res;
}

extern int tx_ring_size[MTLK_MAX_HW_ADAPTERS_SUPPORTED];

static void
_mtlk_mmb_prepare_ring_counters(mtlk_hw_t *hw, mtlk_ring_buffer *ring, CHI_MEM_RING *chi_ring, const char* ring_name)
{
  MTLK_ASSERT(hw);
  MTLK_ASSERT(hw->ccr);

  /* initialize shared counter addresses */
  ring->regs.p_IN_cntr    = (uint32*)(hw->mmb_base + chi_ring->u32InCounterAddress);
  ring->regs.p_OUT_status = (uint32*)(hw->mmb_base + chi_ring->u32OutStatusAddress);
  ring->regs.p_OUT_cntr   = (uint32*)(hw->mmb_base + chi_ring->u32OutCounterAddress);

  CARD_SELECTOR_START(hw->ccr->hw_type) /* read TX-OUT shared counter */
    IF_CARD_PCIEG5 ( ring->regs.int_OUT_cntr = mtlk_raw_readl(ring->regs.p_OUT_status) );
    IF_CARD_PCIEG6 ( ring->regs.int_OUT_cntr = mtlk_raw_readl(ring->regs.p_OUT_status) );
  CARD_SELECTOR_END(); /* other types should not get here */

  ILOG0_SPPP("[%s Ring Regs] p_IN_cntr:0x%p, p_OUT_status:0x%p, p_OUT_cntr:0x%p", ring_name,
            ring->regs.p_IN_cntr, ring->regs.p_OUT_status, ring->regs.p_OUT_cntr);

  MTLK_ASSERT(0 == ring->regs.int_OUT_cntr);
  ring->regs.IN_copy = 0;
}

static int
_mtlk_mmb_recover_ring_buffer (mtlk_hw_t *hw, mtlk_ring_buffer *ring, CHI_MEM_RING *chi_ring, uint32 ring_size, const char* ring_name)
{
  uint32 ring_size_in_bytes = ring_size * sizeof(HOST_DSC);

  MTLK_ASSERT(hw);
  MTLK_ASSERT(hw->ccr);

  ring->size = ring_size;
  ILOG3_SDD("[%s Ring] ring_size:%u, ring_size_in_bytes:%u", ring_name, ring_size, ring_size_in_bytes);

  memset(ring->base_addr, 0, ring_size_in_bytes);
  ring->read_ptr  = 0;
  ring->write_ptr = 0;

  /* initialize shared counter addresses */
  _mtlk_mmb_prepare_ring_counters(hw, ring, chi_ring, ring_name);
  return MTLK_ERR_OK;
}

static int
_mtlk_mmb_prepare_ring_buffer (mtlk_hw_t *hw, mtlk_ring_buffer *ring, CHI_MEM_RING *chi_ring, uint32 ring_size, const char* ring_name)
{
  uint32 ring_size_in_bytes = ring_size * sizeof(HOST_DSC);

  MTLK_ASSERT(hw);
  MTLK_ASSERT(hw->ccr);

  ring->size = ring_size;
  ILOG0_SDD("[%s Ring] ring_size:%u, ring_size_in_bytes:%u", ring_name, ring_size, ring_size_in_bytes);

  ring->base_addr = mtlk_osal_mem_dma_alloc_coherent(
      mtlk_ccr_get_dev_ctx(hw->ccr), ring_size_in_bytes, &ring->dma_addr);
  if (NULL == ring->base_addr) {
    return MTLK_ERR_NO_MEM;
  }

  ILOG0_SPP("[%s Ring] dma_addr:0x%p, base_addr:0x%p", ring_name, (void*)ring->dma_addr, ring->base_addr);

  memset(ring->base_addr, 0, ring_size_in_bytes);
  ring->read_ptr  = 0;
  ring->write_ptr = 0;

  /* initialize shared counter addresses */
  _mtlk_mmb_prepare_ring_counters(hw, ring, chi_ring, ring_name);
  return MTLK_ERR_OK;
}

/*********** Get rings size ************/
static uint32
_mtlk_get_tx_ring_size(mtlk_hw_t *hw)
{
    int    param_ring_size = tx_ring_size[mtlk_hw_mmb_get_card_idx(hw)];
    uint32 ring_size = 0;

    MTLK_ASSERT(hw->ccr);
    if (param_ring_size > 0)
    {
      ring_size = (uint32) param_ring_size;
    } else {
      CARD_SELECTOR_START(hw->ccr->hw_type) /* initialize ring_size */
      IF_CARD_PCIEG5 (ring_size = GEN5_TX_RING_SIZE);
      IF_CARD_PCIEG6 (ring_size = GEN5_TX_RING_SIZE);
      CARD_SELECTOR_END(); /* other types should not get here */
    }
    return ring_size;
}

static uint32
_mtlk_get_rx_ring_size(mtlk_hw_t *hw)
{
    uint32 pool_size = hw->chi_area.sBasic.sRdPOOL.u32NumRdsInPool;
    uint32 ring_size = 0;

    MTLK_ASSERT(hw->ccr);
    CARD_SELECTOR_START(hw->ccr->hw_type) /* initialize ring_size */
      IF_CARD_PCIEG5 (ring_size = GEN5_RX_RING_SIZE);
      IF_CARD_PCIEG6 (ring_size = GEN5_RX_RING_SIZE);
    CARD_SELECTOR_END(); /* other types should not get here */

    /* possibly this can be removed later after testing, currently not supported */
    MTLK_ASSERT(ring_size >= pool_size);

    /* do not allow bigger ring than size of the pool */
    if (ring_size > pool_size) {
      WLOG_DD("RX ring size %u defined bigger than RD pool size %u, using RD pool size as ring size",
                                                                       ring_size, pool_size);
      ring_size = pool_size;
    }
    return ring_size;
}

static uint32
_mtlk_get_mgmt_tx_ring_size(mtlk_hw_t *hw)
{
    uint32 ring_size = 0;

    MTLK_ASSERT(hw->ccr);
    CARD_SELECTOR_START(hw->ccr->hw_type) /* initialize ring_size */
      IF_CARD_PCIEG5 (ring_size = GEN5_MANG_TX_RING_SIZE);
      IF_CARD_PCIEG6 (ring_size = GEN5_MANG_TX_RING_SIZE);
    CARD_SELECTOR_END(); /* other types should not get here */
    return ring_size;
}

static uint32
_mtlk_get_mgmt_rx_ring_size(mtlk_hw_t *hw)
{
    uint32 pool_size = hw->chi_area.sBasic.sRdPOOL.u32NumMgtRdsInPool;
    uint32 ring_size = 0;

    MTLK_ASSERT(hw->ccr);
    CARD_SELECTOR_START(hw->ccr->hw_type) /* initialize ring_size */
      IF_CARD_PCIEG5 (ring_size = GEN5_MANG_RX_RING_SIZE);
      IF_CARD_PCIEG6 (ring_size = GEN5_MANG_RX_RING_SIZE);
    CARD_SELECTOR_END(); /* other types should not get here */

    /* do not allow bigger ring than size of the pool */
    if (ring_size > pool_size) {
      WLOG_DD("BSS RX ring %u defined bigger than size from CHI %u, using MGMT RD pool size as ring size",
                                                                       ring_size, pool_size);
      ring_size = pool_size;
    }
    return ring_size;
}

static int
_mtlk_mmb_recover_data_req_bdr(mtlk_hw_t *hw)
{
  /* Data Requests BD initialization */
  int    res;
  uint32 ring_size;

  MTLK_ASSERT(hw);

  if (mtlk_mmb_dcdp_path_available(hw)) {
    /* Initialize HW counters only */
    _mtlk_mmb_prepare_ring_counters(hw, &hw->tx.ring, &hw->chi_area.sBasic.sTxRING, "TX");
    return MTLK_ERR_OK;
  }

  /* Initialize ring buffers and HW counters */
  ring_size = _mtlk_get_tx_ring_size(hw);
  res = _mtlk_mmb_recover_ring_buffer(hw, &hw->tx.ring, &hw->chi_area.sBasic.sTxRING, ring_size, "TX");
  if (MTLK_ERR_OK != res) {
    return res;
  }

#ifdef BD_DBG
  memset((void *)hw->tx.used_bd_map, 0, (ring_size + 7) >> 3);
#endif

  hw->tx.nof_free_bds = hw->tx.ring.size;
  hw->tx.max_used_bds = 0;

  return res;

}

/********* bdr preparation ******/
static int
_mtlk_mmb_prepare_data_req_bdr(mtlk_hw_t *hw)
{
  /* Data Requests BD initialization */
  int    res;
  uint32 ring_size;

  MTLK_ASSERT(hw);

  if (mtlk_mmb_dcdp_path_available(hw)) {
    /* Initialize HW counters only */
    _mtlk_mmb_prepare_ring_counters(hw, &hw->tx.ring, &hw->chi_area.sBasic.sTxRING, "TX");
    return MTLK_ERR_OK;
  }

  /* Initialize ring buffers and HW counters */
  ring_size = _mtlk_get_tx_ring_size(hw);
  res = _mtlk_mmb_prepare_ring_buffer(hw, &hw->tx.ring, &hw->chi_area.sBasic.sTxRING, ring_size, "TX");
  if (MTLK_ERR_OK != res) {
    return res;
  }

#ifdef BD_DBG
  hw->tx.used_bd_map = mtlk_osal_mem_alloc((ring_size + 7) >> 3, MTLK_MEM_TAG_HW);
  if (!hw->tx.used_bd_map) {
    return MTLK_ERR_NO_MEM;
  }
  memset((void *)hw->tx.used_bd_map, 0, (ring_size + 7) >> 3);
#endif

  /* Initialize BDR */
  res = _mtlk_advanced_bdr_init(&hw->tx.bdr_data,
                                hw->tx.ring.size,
                                (uint8*)hw->tx.ring.base_addr, /* parameter not really used anymore */
                                sizeof(HOST_DSC),
                                sizeof(mtlk_hw_data_req_mirror_t));
  if (MTLK_ERR_OK != res) {
    goto Failed;
  }

  hw->tx.nof_free_bds = hw->tx.ring.size;
  hw->tx.max_used_bds = 0;
  hw->flags |= HW_FLAGS_PREPARED_DATA_REQ_BDR;
  return res;

Failed:
#ifdef BD_DBG
  mtlk_osal_mem_free((void *)hw->tx.used_bd_map);
#endif
  return res;
}

static int
_mtlk_mmb_recover_data_ind_bdr(mtlk_hw_t *hw)
{
  int    res = MTLK_ERR_OK;

  MTLK_ASSERT(hw);

  if (mtlk_mmb_dcdp_path_available(hw)) {
    /* Initialize HW counters only */
    _mtlk_mmb_prepare_ring_counters(hw, &hw->rx.ring, &hw->chi_area.sBasic.sRxRING, "RX");
  }
  else {
    /* Initialize ring buffers and HW counters */
    uint32 ring_size = _mtlk_get_rx_ring_size(hw);
    res = _mtlk_mmb_recover_ring_buffer(hw, &hw->rx.ring, &hw->chi_area.sBasic.sRxRING, ring_size, "RX");
    if (MTLK_ERR_OK != res) {
      return res;
    }
  }

  return res;
}

static int
_mtlk_mmb_prepare_data_ind_bdr(mtlk_hw_t *hw)
{
  int    res;
  uint32 pool_size;

  MTLK_ASSERT(hw);

  if (mtlk_mmb_dcdp_path_available(hw)) {
    /* Initialize HW counters only */
    _mtlk_mmb_prepare_ring_counters(hw, &hw->rx.ring, &hw->chi_area.sBasic.sRxRING, "RX");
  } else {
    /* Initialize ring buffers and HW counters */
    uint32 ring_size = _mtlk_get_rx_ring_size(hw);
    res = _mtlk_mmb_prepare_ring_buffer(hw, &hw->rx.ring, &hw->chi_area.sBasic.sRxRING, ring_size, "RX");
    if (MTLK_ERR_OK != res) {
      return res;
    }
  }

  /* Data Indications BD initialization */
  pool_size = hw->chi_area.sBasic.sRdPOOL.u32NumRdsInPool;
  res = _mtlk_basic_bdr_init(&hw->rx.bdr_data,
                             pool_size,
                             (uint8*)hw->rx.ring.base_addr, /* parameter not really used anymore */
                             sizeof(HOST_DSC),
                             sizeof(mtlk_hw_data_ind_mirror_t));
  if (MTLK_ERR_OK == res) {
    hw->flags |= HW_FLAGS_PREPARED_DATA_IND_BDR;
  }

  return res;
}

static int
_mtlk_mmb_prepare_log_ind_bdr(mtlk_hw_t *hw)
{
  int res = MTLK_ERR_UNKNOWN;
  uint32 nof_bds  = hw->chi_area.sLoggerExt.sData.u32NumOfBufferDescriptors;
  uint32 bdr_offs = hw->chi_area.sLoggerExt.sData.u32BufferDescriptorsLocation;

  /* Logger data Indications BD initialization */
  MTLK_ASSERT(hw->fw_log_is_supported);

  res = _mtlk_basic_bdr_init(&hw->fw_log_buffers,
                             nof_bds,
                             hw->mmb_pas + bdr_offs,
                             sizeof(BUFFER_DAT_IND_MSG_DESC),
                             sizeof(mtlk_hw_log_ind_mirror_t));
  if (res == MTLK_ERR_OK) {
    hw->flags |= HW_FLAGS_PREPARED_LOG_IND_BDR;
  }

  return res;
}
static void
_mtlk_mmb_free_data_tx_buffers(mtlk_hw_t *hw)
{
  if (hw->flags & HW_FLAGS_PREPARED_DATA_REQ_BDR) {
    _mtlk_mmb_free_unconfirmed_tx_buffers(hw);
  }
}

static void
_mtlk_mmb_free_unconfirmed_tx_buffers(mtlk_hw_t *hw)
{
  ILOG3_V("Freeing unconfirmed TX buffers");

  while (TRUE) {
    mtlk_hw_data_req_mirror_t  *data_req;
    mtlk_dlist_entry_t         *node =
      mtlk_dlist_pop_front(&hw->tx.bdr_data.used_list);

    if (!node) {
      break; /* No more buffers */
    }

    data_req = MTLK_LIST_GET_CONTAINING_RECORD(node,
                                               mtlk_hw_data_req_mirror_t,
                                               hdr.list_entry);

    if (data_req->dma_addr) {
      mtlk_df_nbuf_unmap_phys_addr(mtlk_ccr_get_dev_ctx(hw->ccr),
                                   data_req->nbuf,
                                   data_req->dma_addr,
                                   data_req->size,
                                   MTLK_DATA_TO_DEVICE);
    }

    /* Simply free the buffers without any statistic update due to with next steps
       card will be completely deleted. */
    mtlk_df_nbuf_free(data_req->nbuf);
  }
}

static int
_mtlk_mmb_txmm_restore (mtlk_hw_t *hw)
{
  int res;
  mtlk_txmm_restore(&hw->txmm_base);
  res = mtlk_txmm_start(&hw->txmm_base);
  return res;
}

static int
_mtlk_mmb_txdm_restore (mtlk_hw_t *hw)
{
  int res;
  mtlk_txmm_restore(&hw->txdm_base);
  res = mtlk_txmm_start(&hw->txdm_base);
  return res;
}

static void
_mtlk_mmb_set_fw_logger_buffers (mtlk_hw_t *hw){

  int i;

  for (i = 0; i < hw->fw_log_buffers.nof_bds; i++){
    BUFFER_DAT_IND_MSG_DESC bd;
    mtlk_hw_log_ind_mirror_t *log_ind =
      _mtlk_basic_bdr_get_mirror_bd(&hw->fw_log_buffers, i, mtlk_hw_log_ind_mirror_t);

    MTLK_ASSERT(0 != log_ind->dma_addr);
    bd.u32HostPayloadAddr = HOST_TO_MAC32(log_ind->dma_addr);

    _mtlk_mmb_memcpy_toio(_mtlk_basic_bdr_get_iom_bd(&hw->fw_log_buffers, i, BUFFER_DAT_IND_MSG_DESC),
                          &bd,
                          sizeof(bd));
  }
}

static void
_mtlk_mmb_empty_pending_rx_buffers (mtlk_hw_t *hw)
{
  mtlk_slist_entry_t *list_entry;

  while (1) {
    mtlk_hw_data_ind_mirror_t *data_ind;

    list_entry = mtlk_lslist_pop(&hw->rx.pending.lbufs);
    if (NULL == list_entry)
      break;

    data_ind = MTLK_LIST_GET_CONTAINING_RECORD(list_entry,
                                               mtlk_hw_data_ind_mirror_t,
                                               pend_l);
  }
}

static void
_mtlk_mmb_free_preallocated_rx_data_buffers (mtlk_hw_t *hw)
{

  if (!(hw->flags & HW_FLAGS_PREPARED_RX_DATA_BUFFERS)) {
    return;
  }

  if (!mtlk_mmb_dcdp_path_available(hw)) {
    uint32 data_ring_size;
    unsigned i;

    data_ring_size = hw->chi_area.sBasic.sRdPOOL.u32NumRdsInPool;

    for (i = 0; i < data_ring_size; i++) {
      mtlk_hw_data_ind_mirror_t *data_ind =
        _mtlk_basic_bdr_get_mirror_bd(&hw->rx.bdr_data, i, mtlk_hw_data_ind_mirror_t);

      MTLK_ASSERT(NULL != data_ind->nbuf || !"nbuf is NULL");
      if (!data_ind->nbuf) {
        continue;
      }

      mtlk_df_nbuf_unmap_phys_addr(mtlk_ccr_get_dev_ctx(hw->ccr),
                                 data_ind->nbuf,
                                 data_ind->dma_addr,
                                 data_ind->size,
                                 MTLK_DATA_FROM_DEVICE);

      _mtlk_mmb_nbuf_free(data_ind->nbuf);
    }
  }

  /* fragments have also just been released */
  hw->rx.first_fragment = hw->rx.last_fragment = NULL;

  hw->flags &= ~HW_FLAGS_PREPARED_RX_DATA_BUFFERS;

}

static int
_mtlk_mmb_prealloc_rx_data_buffers (mtlk_hw_t *hw)
{
  uint32 i;
  mtlk_hw_data_ind_mirror_t *data_ind;
  uint32 data_ring_size;
  int ares;

  data_ring_size = hw->chi_area.sBasic.sRdPOOL.u32NumRdsInPool;
  ares  = MTLK_ERR_OK;

  if(!mtlk_mmb_dcdp_path_available(hw)) { /* DataPath driver allocates for FastPath */
    for (i = 0; i < data_ring_size ; i++) {
      data_ind =
        _mtlk_basic_bdr_get_mirror_bd(&hw->rx.bdr_data, i, mtlk_hw_data_ind_mirror_t);

      data_ind->que_idx = RX_DATA_QUE_POS;
      data_ind->size = RX_DATA_FRAME_SIZE;

      ares = _mtlk_mmb_prealloc_and_set_rx_data_buffer(hw, data_ind);
      if (ares != MTLK_ERR_OK) {
        /* BRD pointer hole produced here (NULL-pointer) */
         ILOG2_D("WARNING: Can't preallocate buffer of %d bytes.",
             (int)RX_DATA_FRAME_SIZE);
         return MTLK_ERR_NO_MEM;
      }
    }
  }


  ILOG2_DDDD("Total %d from %d buffers allocated for queue#%d (%d bytes each)",
       (int)data_ring_size,
       (int)hw->rx.bdr_data.nof_bds,
       (int)RX_DATA_QUE_POS,
       (int)RX_DATA_FRAME_SIZE);


  hw->rx.first_fragment = hw->rx.last_fragment = NULL;

  hw->flags |= HW_FLAGS_PREPARED_RX_DATA_BUFFERS;


  return MTLK_ERR_OK;
}

static int
_mtlk_mmb_prealloc_rx_mgmt_buffers (mtlk_hw_t *hw)
{
  uint32 i;
  struct mtlk_hw_bss_ind_mirror *bss_ind;

  MTLK_ASSERT(hw->bss_mgmt.rx_bdr.basic.nof_bds == hw->bss_mgmt.rx_ring.size);
  ILOG2_D("About to allocate %d pointers", hw->bss_mgmt.rx_ring.size);

  for (i = 0; i < hw->bss_mgmt.rx_ring.size; i++) {

      bss_ind = _mtlk_basic_bdr_get_mirror_bd(&hw->bss_mgmt.rx_bdr.basic, i, struct mtlk_hw_bss_ind_mirror);

      bss_ind->dma_addr = mtlk_osal_map_to_phys_addr(mtlk_ccr_get_dev_ctx(hw->ccr), (void *)bss_ind->virt_addr,
                                                     bss_ind->size, MTLK_DATA_FROM_DEVICE);
      if (!bss_ind->dma_addr) {
        ILOG2_P("WARNING: failed mapping 0x%p to physical address", bss_ind->virt_addr);
        return MTLK_ERR_NO_RESOURCES;
      }

    _mtlk_mmb_set_mgmt_pool_entry(hw, bss_ind->dma_addr, i);
    ILOG3_DPP("#%d allocated at virt_addr:%p from bss_ind:%p", i, bss_ind->virt_addr, bss_ind);
  }

  ILOG2_D("Total %u buffers set to management RD", hw->bss_mgmt.rx_ring.size);

  hw->flags |= HW_FLAGS_PREPARED_RX_MGMT_BUFFERS;

  return MTLK_ERR_OK;
}

static void
_mtlk_mmb_free_preallocated_rx_mgmt_buffers (mtlk_hw_t *hw)
{
  uint32 i;
  struct mtlk_hw_bss_ind_mirror *bss_ind;

  if (!(hw->flags & HW_FLAGS_PREPARED_RX_MGMT_BUFFERS)) {
    return;
  }

  ILOG3_D("About to free %d pointers", hw->bss_mgmt.rx_ring.size);
  for (i = 0; i < hw->bss_mgmt.rx_ring.size; i++) {
    bss_ind = _mtlk_basic_bdr_get_mirror_bd(&hw->bss_mgmt.rx_bdr.basic, i, struct mtlk_hw_bss_ind_mirror);

    ILOG3_DDP("#%d Freeing at virt_addr:0x%08X from bss_ind:%p", i, bss_ind->dma_addr, bss_ind);

    if (!bss_ind->dma_addr) {
      continue;
    }

    mtlk_osal_unmap_phys_addr(mtlk_ccr_get_dev_ctx(hw->ccr),
                              bss_ind->dma_addr,
                              bss_ind->size,
                              MTLK_DATA_FROM_DEVICE);
  }

  hw->flags &= ~HW_FLAGS_PREPARED_RX_MGMT_BUFFERS;
}


static int
_mtlk_mmb_send_msg (mtlk_hw_t *hw, him_descr_t descriptor)
{
  mtlk_handle_t         lock_val;
  MTLK_ASSERT(NULL != hw->ccr);

  descriptor = HOST_TO_MAC32(descriptor);
  ILOG5_D("MSG WRITE: 0x%08x", descriptor);

  lock_val = mtlk_osal_lock_acquire_irq(&hw->reg_lock);
  _mtlk_mmb_pas_put(hw,
                    "new BD descriptor",
                    hw->bds.req.offset + (hw->bds.req.idx * sizeof(descriptor)), /* DB Array [BD Idx] */
                    &descriptor,
                    sizeof(descriptor));

  INC_WRAP_IDX(hw->bds.req.idx, hw->bds.req.size);

  MTLK_TRACER_ADD_ENTRY(hw, MTLK_TR_MSG_SEND_DESCR, descriptor);
  mtlk_ccr_ring_initiate_doorbell_interrupt(hw->ccr, &hw->ctrl.ring.regs);

  mtlk_osal_lock_release_irq(&hw->reg_lock, lock_val);

  return MTLK_ERR_OK;
}

static int
_mtlk_mmb_send_sw_reset_mac_req(mtlk_hw_t *hw)
{
  int               res       = MTLK_ERR_UNKNOWN;
  mtlk_txmm_msg_t   man_msg;
  mtlk_txmm_data_t *man_entry = NULL;

  man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, &hw->txmm, NULL);
  if (!man_entry) {
    ELOG_V("Can't send request to MAC due to the lack of MAN_MSG");
    goto FINISH;
  }

  man_entry->id           = UM_MAN_SW_RESET_MAC_REQ;
  man_entry->payload_size = 0;

  res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_SW_RESET_CFM_TIMEOUT);
  if (res != MTLK_ERR_OK) {
    ELOG_V("Can't send sw reset request to MAC, timed-out");
    goto FINISH;
  }

  res = MTLK_ERR_OK;

FINISH:
  if (man_entry)
    mtlk_txmm_msg_cleanup(&man_msg);
  return res;
}

static void
_mtlk_mmb_cleanup_reset_mac(mtlk_hw_t *hw)
{
  MTLK_ASSERT(NULL != hw->ccr);

  mtlk_ccr_put_cpus_to_reset(hw->ccr);
  mtlk_ccr_clear_boot_from_bus(hw->ccr);
  mtlk_ccr_put_ctl_to_reset(hw->ccr);
  ILOG0_V("FW CPU reset is done");
}

static void
_mtlk_mmb_stop_events_completely(mtlk_hw_t *hw)
{
  /* NOTE: mac_events_stopped must be also set here to avoid additional checks
   * in _mtlk_mmb_handle_received_msg() (hw->mac_events_stopped || hw->mac_events_stopped_completely) */
  hw->mac_events_stopped            = 1;
  hw->mac_events_stopped_completely = 1;
}

static void
_mtlk_mmb_reset_all_events(mtlk_hw_t *hw)
{
    hw->mac_events_stopped            = 0;
    hw->mac_events_stopped_completely = 0;
}

static void
_mtlk_mmb_cleanup_calibration_cache(mtlk_hw_t *hw)
{
  mtlk_osal_unmap_phys_addr(mtlk_ccr_get_dev_ctx(hw->ccr),
                            hw->calibr_dma_addr,
                            hw->chi_area.sCalibr.sData.u32BufferRequestedSize,
                            MTLK_DATA_FROM_DEVICE);
  mtlk_osal_mem_free(hw->calibr_buffer);
  hw->calibr_dma_addr = 0;
  hw->calibr_buffer = NULL;
}

static int _mtlk_mmb_allocate_calibration_buffers (mtlk_hw_t *hw)
{
  hw->calibr_buffer = mtlk_osal_mem_dma_alloc(hw->chi_area.sCalibr.sData.u32BufferRequestedSize,
                                              MTLK_MEM_TAG_EXTENSION);
  if (!hw->calibr_buffer) {
    WLOG_D("Can't allocate Calibration buffer of %u bytes", hw->chi_area.sCalibr.sData.u32BufferRequestedSize);
    return MTLK_ERR_NO_MEM;
  }
  memset(hw->calibr_buffer, 0, hw->chi_area.sCalibr.sData.u32BufferRequestedSize);

  hw->calibr_dma_addr = mtlk_osal_map_to_phys_addr(mtlk_ccr_get_dev_ctx(hw->ccr),
                                                   hw->calibr_buffer,
                                                   hw->chi_area.sCalibr.sData.u32BufferRequestedSize,
                                                   MTLK_DATA_BIDIRECTIONAL);
  MTLK_ASSERT(0 != hw->calibr_dma_addr);
  if (!hw->calibr_dma_addr) {
    WLOG_P("Can't map Calibration buffer 0x%p to physical address", hw->calibr_buffer);
    return MTLK_ERR_NO_RESOURCES;
  }

  ILOG0_DPD("Calibration buffer: dma_addr 0x%08X, virt_addr 0x%p, size %u",
            hw->calibr_dma_addr, hw->calibr_buffer, hw->chi_area.sCalibr.sData.u32BufferRequestedSize);
  return MTLK_ERR_OK;
}

#define __CCR_IO_MASK__         ((1u << 24) - 1) /* 16M - accessible from the host */

static int
_mtlk_mmb_init_calibration_cache (mtlk_hw_t *hw)
{
  uint32 pm_addr;

  ILOG0_DD("u32BufferRequestedSize %d, pPsdProgmodelAddress 0x%04X",
            hw->chi_area.sCalibr.sData.u32BufferRequestedSize,
            hw->chi_area.sCalibr.sData.pPsdProgmodelAddress);

  /* FIXME: wrong address has to be fixed by FW */
  pm_addr = __CCR_IO_MASK__ & hw->chi_area.sCalibr.sData.pPsdProgmodelAddress;
  if (hw->chi_area.sCalibr.sData.pPsdProgmodelAddress != pm_addr) {
      ELOG_DD("pPsdProgmodelAddress 0x%04X is wrong, fixed to 0x%04X",
              hw->chi_area.sCalibr.sData.pPsdProgmodelAddress, pm_addr);

      hw->chi_area.sCalibr.sData.pPsdProgmodelAddress = pm_addr;
  }

  if (0 == hw->chi_area.sCalibr.sData.u32BufferRequestedSize) {
    WLOG_V("u32BufferRequestedSize is zero. Will not be allocated!");
    return MTLK_ERR_OK; /* FIXME: OK or Error ??? */
  }

  return _mtlk_mmb_allocate_calibration_buffers(hw);
}

static void
_mtlk_mmb_init_mips_control(mtlk_hw_t *hw)
{
  ILOG2_D("MIPS Ctrl Descriptor PAS offset: 0x%x",
          hw->chi_area.sMipsControl.sData.u32DescriptorLocation);
}

static void
_mtlk_mmb_cleanup_fw_logger(mtlk_hw_t *hw)
{
  int i;

  if (!(hw->flags & HW_FLAGS_PREPARED_LOG_IND_BDR))
    return;

  for(i = 0; i < hw->chi_area.sLoggerExt.sData.u32NumOfBufferDescriptors; i++) {
    mtlk_hw_log_ind_mirror_t *log_ind =
      _mtlk_basic_bdr_get_mirror_bd(&hw->fw_log_buffers, i, mtlk_hw_log_ind_mirror_t);

    if(NULL == log_ind->virt_addr)
      continue;

    MTLK_ASSERT(log_ind->dma_addr);

    mtlk_osal_unmap_phys_addr(mtlk_ccr_get_dev_ctx(hw->ccr), log_ind->dma_addr,
                              log_ind->bufsize, MTLK_DATA_FROM_DEVICE);
    mtlk_osal_mem_free(log_ind->virt_addr);
    log_ind->virt_addr = NULL;
  }

  _mtlk_mmb_cleanup_log_ind_bdr(hw);
}

#if (IWLWAV_RTLOG_FLAGS & RTLF_REMOTE_ENABLED)
static void
__mtlk_mmb_prepare_data_fw_logger (mtlk_hw_t *hw, uint32 bp_addr, int buffer_cnt, int buffer_size)
{
  int i;
  uint8 *data_start = NULL;
  uint32 val4bytes = 0;
  int res = MTLK_ERR_OK;

  MTLK_ASSERT(0 != buffer_size);
  MTLK_ASSERT(0 != buffer_cnt);

  data_start = mtlk_osal_mem_alloc(MTLK_IPAD4(buffer_size + sizeof(uint32)), MTLK_MEM_TAG_FW_LOGGER);
  if (NULL == data_start) {
    ELOG_V("Can't allocate memory for FW Logger. No data is parsed");
    goto finish;
  }

  for (i = 0; i < buffer_cnt; i++) {
    int offset = 0;
    _mtlk_mmb_pas_get(hw, "FW logger buffer", bp_addr + (i * (buffer_size + sizeof(uint32))), (void *)data_start, MTLK_IPAD4(buffer_size + sizeof(uint32)));

    wave_memcpy(&val4bytes, sizeof(val4bytes), data_start, sizeof(uint32));
    val4bytes = MAC_TO_HOST32(val4bytes);

    if (val4bytes != USED_NSSBUF)
      continue;

    offset += sizeof(uint32);
    while ((offset <= buffer_size) && (val4bytes != LOGGER_END_OF_BUFFER_MARK)) {
      wave_memcpy(&val4bytes, sizeof(val4bytes), data_start + offset, sizeof(uint32));
      val4bytes = MAC_TO_HOST32(val4bytes);
      offset += sizeof(uint8);
    }
    offset -= sizeof(uint8);
    if (val4bytes == LOGGER_END_OF_BUFFER_MARK)
      offset -= sizeof(uint32);
    if (offset > buffer_size)
      offset = buffer_size;
    if (offset) {
      res = _mtlk_mmb_push_logger_buf(hw, data_start + sizeof(uint32), offset);
    }
  }

finish:
  if (data_start) {
    mtlk_osal_mem_free(data_start);
  }
}

static void
_mtlk_mmb_parse_fw_logger (mtlk_hw_t *hw)
{
  uint32 bp_addr = 0;
  uint16 nof_bdr = 0;
  uint16 size = 0;

  bp_addr = hw->chi_area.sLoggerBufInfoExt.sData.lmBufferInfo.u32BufferPoolAddress;
  if (bp_addr) {
    nof_bdr = (uint16)hw->chi_area.sLoggerBufInfoExt.sData.lmBufferInfo.u32NumberOfBuffers;
    size    = (uint16)hw->chi_area.sLoggerBufInfoExt.sData.lmBufferInfo.u32Size;
    __mtlk_mmb_prepare_data_fw_logger(hw, bp_addr, nof_bdr, size);
  }

  bp_addr = hw->chi_area.sLoggerBufInfoExt.sData.umBufferInfo.u32BufferPoolAddress;
  if (bp_addr) {
    nof_bdr = (uint16)hw->chi_area.sLoggerBufInfoExt.sData.umBufferInfo.u32NumberOfBuffers;
    size    = (uint16)hw->chi_area.sLoggerBufInfoExt.sData.umBufferInfo.u32Size;
    __mtlk_mmb_prepare_data_fw_logger(hw, bp_addr, nof_bdr, size);
  }
}
#endif /* (IWLWAV_RTLOG_FLAGS & RTLF_REMOTE_ENABLED) */

static int
_mtlk_mmb_alloc_fw_logger_buffers (mtlk_hw_t *hw, int start_idx, int count, uint16 bufsize)
{
  int res = MTLK_ERR_OK;
  int i;

  ILOG2_DD("Allocate %d buffers of size %d", count, bufsize);

  for (i = start_idx; i < (start_idx + count); i++) {
    mtlk_hw_log_ind_mirror_t *log_ind =
      _mtlk_basic_bdr_get_mirror_bd(&hw->fw_log_buffers, i, mtlk_hw_log_ind_mirror_t);

    log_ind->virt_addr = mtlk_osal_mem_dma_alloc(bufsize, MTLK_MEM_TAG_FW_LOGGER);
    if(NULL == log_ind->virt_addr) {
      res = MTLK_ERR_NO_MEM;
      break;
    }

    if (log_ind->virt_addr) {
      log_ind->bufsize = bufsize;
      log_ind->dma_addr = mtlk_osal_map_to_phys_addr(mtlk_ccr_get_dev_ctx(hw->ccr), log_ind->virt_addr,
                                                     bufsize, MTLK_DATA_FROM_DEVICE);
      if (!log_ind->dma_addr) {
        ILOG2_P("WARNING: failed mapping 0x%p to physical address", log_ind->virt_addr);
        return MTLK_ERR_NO_RESOURCES;
      }
    }
    ILOG2_DPD("log_ind: dma_addr 0x%08X, virt_addr 0x%p, size %u", log_ind->dma_addr, log_ind->virt_addr, log_ind->bufsize);
  }

  return res;
}

static int
_mtlk_mmb_init_fw_logger (mtlk_hw_t *hw)
{
  int res = MTLK_ERR_OK;
  uint32 nof_bdr = 0;
  uint16 nof_bdr_lm = 0;
  uint16 nof_bdr_um = 0;
  uint16 bufsize_lm = 0;
  uint16 bufsize_um = 0;

  res = _mtlk_mmb_prepare_log_ind_bdr(hw);
  if (MTLK_ERR_OK != res)
    return res;

  nof_bdr = hw->chi_area.sLoggerExt.sData.u32NumOfBufferDescriptors;

  if (hw->fw_log_is_supported_ex) {
    nof_bdr_lm = (uint16)hw->chi_area.sLoggerBufInfoExt.sData.lmBufferInfo.u32NumberOfBuffers;
    nof_bdr_um = (uint16)hw->chi_area.sLoggerBufInfoExt.sData.umBufferInfo.u32NumberOfBuffers;

    if (nof_bdr != (nof_bdr_lm + nof_bdr_um)) {
      ELOG_DDD("Incorrect FW logger capabilities: %d != %d + %d", nof_bdr, nof_bdr_lm, nof_bdr_um);
      return MTLK_ERR_PARAMS;
    }

    bufsize_lm = (uint16)hw->chi_area.sLoggerBufInfoExt.sData.lmBufferInfo.u32Size;
    bufsize_um = (uint16)hw->chi_area.sLoggerBufInfoExt.sData.umBufferInfo.u32Size;

    if ((nof_bdr_lm) && (bufsize_lm)) {
      res |= _mtlk_mmb_alloc_fw_logger_buffers(hw, 0, nof_bdr_lm, bufsize_lm);
    }
    if ((nof_bdr_um) && (bufsize_um)) {
      res |= _mtlk_mmb_alloc_fw_logger_buffers(hw, nof_bdr_lm, nof_bdr_um, bufsize_um);
    }
  }
  else {
    res = _mtlk_mmb_alloc_fw_logger_buffers(hw, 0, nof_bdr, 120);
  }

  if (MTLK_ERR_OK == res) {
    _mtlk_mmb_set_fw_logger_buffers(hw);
  }
  else {
    _mtlk_mmb_cleanup_fw_logger(hw);
  }

  return res;
}

#define DATA_TXOUT_LIM_DEFAULT  300
#define DATA_RX_LIM_DEFAULT     300
#define BSS_RX_LIM_DEFAULT      15
#define BSS_CFM_LIM_DEFAULT     50
#define LEGACY_LIM_DEFAULT     150

static void _mtlk_mmb_init_rxtx_limits (mtlk_hw_t *hw)
{
  hw->data_txout_lim = DATA_TXOUT_LIM_DEFAULT;
  hw->data_rx_lim    = DATA_RX_LIM_DEFAULT;
  hw->bss_rx_lim     = BSS_RX_LIM_DEFAULT;
  hw->bss_cfm_lim    = BSS_CFM_LIM_DEFAULT;
  hw->legacy_lim     = LEGACY_LIM_DEFAULT;
}

#ifdef MTLK_LEGACY_STATISTICS
/*------ MHI_STATISTICS ------*/

/* HW Tx power per BW */
static mhi_stats_cfg_t  _hw_mhi_stats_tx_power_list[] = {
    { STATISTIC_TYPE_LINK_ADAPTATION_STATS,     LINK_ADAPTATION_MIN_POWER   },
    { STATISTIC_TYPE_LINK_ADAPTATION_STATS,     LINK_ADAPTATION_MAX_POWER   },
};

/* All VAP/STA traffic related counters have the same type */
typedef uint32      mhi_stats_txrx_cntr_t;
#define MHI_STATS_TXRX_CNTR_TO_HOST(x)  (MAC_TO_HOST32(x))

/* List should be aligned with mtlk_mhi_stat_vap_e and mtlk_mhi_stats_vap_t */
static mhi_stats_cfg_t  _hw_mhi_stats_vap_list[] = {
    { STATISTIC_TYPE_HOSTIF_COUNTERS,   HOSTIF_COUNTERS_TX_IN_UNICAST_HD              },    /* UnicastPacketsSent */
    { STATISTIC_TYPE_HOSTIF_COUNTERS,   HOSTIF_COUNTERS_TX_IN_MULTICAST_HD            },    /* MulticastPacketsSent */
    { STATISTIC_TYPE_HOSTIF_COUNTERS,   HOSTIF_COUNTERS_TX_IN_BROADCAST_HD            },    /* BroadcastPacketsSent */
    { STATISTIC_TYPE_HOSTIF_COUNTERS,   HOSTIF_COUNTERS_TX_IN_UNICAST_NUM_OF_BYTES    },
    { STATISTIC_TYPE_HOSTIF_COUNTERS,   HOSTIF_COUNTERS_TX_IN_MULTICAST_NUM_OF_BYTES  },
    { STATISTIC_TYPE_HOSTIF_COUNTERS,   HOSTIF_COUNTERS_TX_IN_BROADCAST_NUM_OF_BYTES  },

    { STATISTIC_TYPE_HOSTIF_COUNTERS,   HOSTIF_COUNTERS_RX_OUT_UNICAST_HD             },    /* UnicastPacketsReceived */
    { STATISTIC_TYPE_HOSTIF_COUNTERS,   HOSTIF_COUNTERS_RX_OUT_MULTICAST_HD           },    /* MulticastPacketsReceived */
    { STATISTIC_TYPE_HOSTIF_COUNTERS,   HOSTIF_COUNTERS_RX_OUT_BROADCAST_HD           },    /* BroadcastPacketsReceived */
    { STATISTIC_TYPE_HOSTIF_COUNTERS,   HOSTIF_COUNTERS_RX_OUT_UNICAST_NUM_OF_BYTES   },
    { STATISTIC_TYPE_HOSTIF_COUNTERS,   HOSTIF_COUNTERS_RX_OUT_MULTICAST_NUM_OF_BYTES },
    { STATISTIC_TYPE_HOSTIF_COUNTERS,   HOSTIF_COUNTERS_RX_OUT_BROADCAST_NUM_OF_BYTES },

    { STATISTIC_TYPE_HOSTIF_COUNTERS,   HOSTIF_COUNTERS_AGER_COUNT                    },    /* ErrorsSent */
    { STATISTIC_TYPE_RX_COUNTERS,       RX_COUNTERS_DROP_MPDU                         },    /* ErrorsReceived */

    { STATISTIC_TYPE_RX_COUNTERS,       RX_COUNTERS_MPDU_UNICAST_OR_MNGMNT            },
    { STATISTIC_TYPE_RX_COUNTERS,       RX_COUNTERS_MPDU_RETRY_COUNT                  },
    { STATISTIC_TYPE_RX_COUNTERS,       RX_COUNTERS_AMSDU                             },
    { STATISTIC_TYPE_RX_COUNTERS,       RX_COUNTERS_MPDU_TYPE_NOTSUPPORTED            },    /* UnknownProtoPacketsReceived */
    { STATISTIC_TYPE_RX_COUNTERS,       RX_COUNTERS_REPLAY_DATA                       },
    { STATISTIC_TYPE_RX_COUNTERS,       RX_COUNTERS_REPLAY_MNGMNT                     },
    { STATISTIC_TYPE_RX_COUNTERS,       RX_COUNTERS_TKIP_COUNT                        },
    { STATISTIC_TYPE_RX_COUNTERS,       RX_COUNTERS_SECURITY_FAILURE                  },
    { STATISTIC_TYPE_RX_COUNTERS,       RX_COUNTERS_AMSDU_BYTES                       },
    { STATISTIC_TYPE_RX_COUNTERS,       RX_COUNTERS_AMPDU                             },
    { STATISTIC_TYPE_RX_COUNTERS,       RX_COUNTERS_MPDU_IN_AMPDU                     },
    { STATISTIC_TYPE_RX_COUNTERS,       RX_COUNTERS_OCTETS_IN_AMPDU                   },
    { STATISTIC_TYPE_RX_COUNTERS,       RX_COUNTERS_RX_CLASSIFIER_SECURITY_MISMATCH   },
    { STATISTIC_TYPE_RX_COUNTERS,       RX_COUNTERS_BC_MC_MPDUS                       },

    { STATISTIC_TYPE_BAA_COUNTERS,       BAA_COUNTERS_RTS_SUCCESS_COUNT               },
    { STATISTIC_TYPE_BAA_COUNTERS,       BAA_COUNTERS_QOS_TRANSMITTED_FRAMES          },
    { STATISTIC_TYPE_BAA_COUNTERS,       BAA_COUNTERS_TRANSMITTED_AMSDU               },    /* AggregatedPacketCount */
    { STATISTIC_TYPE_BAA_COUNTERS,       BAA_COUNTERS_TRANSMITTED_OCTETS_IN_AMSDU     },
    { STATISTIC_TYPE_BAA_COUNTERS,       BAA_COUNTERS_TRANSMITTED_AMPDU               },
    { STATISTIC_TYPE_BAA_COUNTERS,       BAA_COUNTERS_TRANSMITTED_MPDU_IN_AMPDU       },
    { STATISTIC_TYPE_BAA_COUNTERS,       BAA_COUNTERS_TRANSMITTED_OCTETS_IN_AMPDU     },
    { STATISTIC_TYPE_BAA_COUNTERS,       BAA_COUNTERS_BEAMFORMING_FRAMES              },
    { STATISTIC_TYPE_BAA_COUNTERS,       BAA_COUNTERS_TRANSMIT_STREAM_RPRT_MSDU_FAILED},
    { STATISTIC_TYPE_BAA_COUNTERS,       BAA_COUNTERS_RTS_FAILURE                     },
    { STATISTIC_TYPE_BAA_COUNTERS,       BAA_COUNTERS_ACK_FAILURE                     },    /* ACKFailureCount */
    { STATISTIC_TYPE_BAA_COUNTERS,       BAA_COUNTERS_FAILED_AMSDU                    },
    { STATISTIC_TYPE_BAA_COUNTERS,       BAA_COUNTERS_RETRY_AMSDU                     },    /* RetryCount */
    { STATISTIC_TYPE_BAA_COUNTERS,       BAA_COUNTERS_MULTIPLE_RETRY_AMSDU            },    /* MultipleRetryCount */
    { STATISTIC_TYPE_BAA_COUNTERS,       BAA_COUNTERS_AMSDU_ACK_FAILURE               },
    { STATISTIC_TYPE_BAA_COUNTERS,       BAA_COUNTERS_IMPLICIT_BAR_FAILURE            },
    { STATISTIC_TYPE_BAA_COUNTERS,       BAA_COUNTERS_EXPLICIT_BAR_FAILURE            },
    { STATISTIC_TYPE_BAA_COUNTERS,       BAA_COUNTERS_TRANSMIT_STREAM_RPRT_MULTIPLE_RETRY_COUNT }, /* RetransCount */
    { STATISTIC_TYPE_BAA_COUNTERS,       BAA_COUNTERS_TRANSMIT_BW20                   },
    { STATISTIC_TYPE_BAA_COUNTERS,       BAA_COUNTERS_TRANSMIT_BW40                   },
    { STATISTIC_TYPE_BAA_COUNTERS,       BAA_COUNTERS_TRANSMIT_BW80                   },
    { STATISTIC_TYPE_BAA_COUNTERS,       BAA_COUNTERS_RX_GROUP_FRAME                  },
    { STATISTIC_TYPE_BAA_COUNTERS,       BAA_COUNTERS_TX_SENDER_ERROR                 },
};

/* STAs traffic statistics, should be aligned with mtlk_mhi_stats_sta_cntr_t */
static mhi_stats_cfg_t  _hw_mhi_stats_sta_list[] = {
    { STATISTIC_TYPE_BAA_COUNTERS,              BAA_COUNTERS_MPDU_FIRST_RETRANSMISSION  }, /* Retransmissions */
    { STATISTIC_TYPE_BAA_COUNTERS,              BAA_COUNTERS_MPDU_TRANSMITTED           }, /* TX frames */
    { STATISTIC_TYPE_BAA_COUNTERS,              BAA_COUNTERS_MPDU_BYTE_TRANSMITTED      }, /* TX bytes  */
    { STATISTIC_TYPE_RX_COUNTERS,               RX_COUNTERS_RD_COUNT                    }, /* RX frames */
    { STATISTIC_TYPE_HOSTIF_COUNTERS,           HOSTIF_COUNTERS_RX_OUT_STA_NUM_OF_BYTES }, /* RX bytes  */
    { STATISTIC_TYPE_RX_COUNTERS,               RX_COUNTERS_SW_UPDATE_DROP              }, /* DiscardPacketsReceived */
    { STATISTIC_TYPE_PER_CLIENT_TRANSMIT_COUNT, PCT_STATISTICS_RETRY_COUNT              }, /* Pkts successfully transmitted after one or more retransmissions */
    { STATISTIC_TYPE_PER_CLIENT_TRANSMIT_COUNT, PCT_STATISTICS_RETRY_EXHAUSTED_COUNT    }, /* Pkts dropped because of retransmit limit */
};

/* STAs TX rate statistics, all values are uint8 */
/* List should be aligned with mtlk_mhi_stats_sta_tx_rate_t */
static mhi_stats_cfg_t  _hw_mhi_stats_tx_rate_list[] = {
    { STATISTIC_TYPE_LINK_ADAPTATION_STATS,     LINK_ADAPTATION_DATA_BW_LIMIT    },
    { STATISTIC_TYPE_LINK_ADAPTATION_STATS,     LINK_ADAPTATION_DATA_PHY_MODE    },
    { STATISTIC_TYPE_LINK_ADAPTATION_STATS,     LINK_ADAPTATION_SCP_DATA         },
    { STATISTIC_TYPE_LINK_ADAPTATION_STATS,     LINK_ADAPTATION_MCS_DATA         },
    { STATISTIC_TYPE_LINK_ADAPTATION_STATS,     LINK_ADAPTATION_NSS_DATA         },
    { STATISTIC_TYPE_LINK_ADAPTATION_STATS,     LINK_ADAPTATION_BF_MODE_DATA     },
    { STATISTIC_TYPE_LINK_ADAPTATION_STATS,     LINK_ADAPTATION_STBC_MODE_DATA   },
    { STATISTIC_TYPE_LINK_ADAPTATION_STATS,     LINK_ADAPTATION_POWER_DATA       },
    { STATISTIC_TYPE_LINK_ADAPTATION_STATS,     LINK_ADAPTATION_POWER_MANAGEMENT },
    { STATISTIC_TYPE_LINK_ADAPTATION_STATS,     LINK_ADAPTATION_MANAGEMENT_PHY_MODE },
};

/* Update HW related statistics (under mhi_stats lock) */

static void
_mtlk_hw_mhi_update_hw_stats (mtlk_hw_t *hw, hw_mhi_stats_t *mhi_stats)
{
    uint8               *db_pw_min, *db_pw_max;
    unsigned             cbw;
    unsigned             radio_idx;

    db_pw_min = (void *)mhi_stats->db_copy + mhi_stats->stats_hw_tx_pw_offs_table[STAT_HW_TX_POWER_MIN].start_offset;
    db_pw_max = (void *)mhi_stats->db_copy + mhi_stats->stats_hw_tx_pw_offs_table[STAT_HW_TX_POWER_MAX].start_offset;

    for (radio_idx = 0; radio_idx < hw->radio_descr->num_radios; radio_idx++) {
      for (cbw = 0; cbw < PHY_STAT_RATE_CBW_NUM; cbw++) {
        mhi_stats->tx_power[radio_idx].pw_min_ant[cbw] = STAT_PW_TO_POWER(db_pw_min[cbw]);
        mhi_stats->tx_power[radio_idx].pw_max_ant[cbw] = STAT_PW_TO_POWER(db_pw_max[cbw]);
      }
      db_pw_min += PHY_STAT_RATE_CBW_NUM; /* seek next radio */
      db_pw_max += PHY_STAT_RATE_CBW_NUM;
    }
}

void __MTLK_IFUNC
mtlk_hw_mhi_get_tx_power (mtlk_hw_t *hw, mtlk_hw_tx_power_t *tx_power, unsigned radio_idx)
{
    hw_mhi_stats_t *mhi_stats;

    MTLK_ASSERT(hw);
    MTLK_ASSERT(tx_power);

    mhi_stats = &hw->hw_mhi_stats;
    mtlk_osal_lock_acquire(&mhi_stats->lock);
    *tx_power = mhi_stats->tx_power[radio_idx];  /* struct copy */
    mtlk_osal_lock_release(&mhi_stats->lock);
}
#else /* MTLK_LEGACY_STATISTICS */
static void
_mtlk_hw_mhi_update_hw_stats (mtlk_hw_t *hw, hw_statistics_t *hw_stats)
{
    unsigned             cbw;
    unsigned             radio_idx;
    int                  chip_id = __hw_mmb_get_chip_id(hw);

    mtlk_osal_lock_acquire(&hw_stats->lock);
    for (radio_idx = 0; radio_idx < hw->radio_descr->num_radios; radio_idx++) {
      for (cbw = 0; cbw < PHY_STAT_RATE_CBW_NUM; cbw++) {
        hw_stats->tx_power[radio_idx].pw_min_ant[cbw] =
              STAT_PW_TO_POWER(*(unsigned char *)(MTLK_STATS_COPY_OFFSET(linkAdaptationStats.minPower[radio_idx][cbw], chip_id)));
        hw_stats->tx_power[radio_idx].pw_max_ant[cbw] =
              STAT_PW_TO_POWER(*(unsigned char *)(MTLK_STATS_COPY_OFFSET(linkAdaptationStats.maxPower[radio_idx][cbw], chip_id)));
      }
    }
    mtlk_osal_lock_release(&hw_stats->lock);
}

void __MTLK_IFUNC
mtlk_hw_mhi_get_tx_power (mtlk_hw_t *hw, mtlk_hw_tx_power_t *tx_power, unsigned radio_idx)
{
    hw_statistics_t *hw_stats;

    MTLK_ASSERT(hw);
    MTLK_ASSERT(tx_power);

    hw_stats = &hw->hw_stats;
    mtlk_osal_lock_acquire(&hw_stats->lock);
    *tx_power = hw_stats->tx_power[radio_idx];  /* struct copy */
    mtlk_osal_lock_release(&hw_stats->lock);
}
#endif /* MTLK_LEGACY_STATISTICS */

#ifdef MTLK_LEGACY_STATISTICS
/* Get MHI Statistics Data using the same UMI req as for the Statistics Data */
int __MTLK_IFUNC
mtlk_hw_mhi_get_stats (mtlk_hw_t *hw)
{
    mtlk_txmm_msg_t          man_msg;
    mtlk_txmm_data_t        *man_entry;
    UMI_GET_STATISTICS_DATA *req;
    hw_mhi_stats_t          *mhi_stats;
    uint32                   dma_addr;
    int                      res = MTLK_ERR_UNKNOWN;

    /* prepare msg for the FW */
    if (!(man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, &hw->txmm, NULL))) {
        ELOG_D("card_idx %d: UMI_GET_STATISTICS_DATA init failed", hw->card_idx);
      return MTLK_ERR_NO_RESOURCES;
    }

    mhi_stats = &hw->hw_mhi_stats;

    dma_addr = mtlk_osal_map_to_phys_addr(mtlk_ccr_get_dev_ctx(hw->ccr),
                                          mhi_stats->db_data,
                                          mhi_stats->db_size,
                                          MTLK_DATA_FROM_DEVICE);
    if (!dma_addr) {
      ILOG2_P("WARNING: failed mapping 0x%p to physical address", mhi_stats->db_data);
      return MTLK_ERR_NO_RESOURCES;
    }

    man_entry->id = UM_MAN_GET_STATISTICS_DATA_REQ;
    man_entry->payload_size = sizeof(*req);
    req = (UMI_GET_STATISTICS_DATA *) man_entry->payload;
    req->ddrBufferAddress = HOST_TO_MAC32(dma_addr);
    req->u16length        = HOST_TO_MAC16(mhi_stats->db_size);
    req->u8status         = UMI_OK;

    res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);

    mtlk_osal_unmap_phys_addr(mtlk_ccr_get_dev_ctx(hw->ccr),
                              dma_addr, mhi_stats->db_size,
                              MTLK_DATA_FROM_DEVICE);

    mtlk_txmm_msg_cleanup(&man_msg);

    if (res != MTLK_ERR_OK) {
        ELOG_DD("card_idx %d: Sending failed, err=%i", hw->card_idx, res);
    } else if (UMI_OK != req->u8status) {
        ELOG_D("Failed to retrieve MHI statistics, error %d", req->u8status);
        res = MTLK_ERR_MAC;
    }

    if (res == MTLK_ERR_OK) {
        mtlk_osal_lock_acquire(&mhi_stats->lock);

        wave_memcpy(mhi_stats->db_copy, mhi_stats->db_size,  mhi_stats->db_data, mhi_stats->db_size);
        mtlk_dump(3, mhi_stats->db_copy, MIN(32, mhi_stats->db_size), "dump of MHI statistics");

        /* Update HW related statistics */
        _mtlk_hw_mhi_update_hw_stats(hw, mhi_stats);

        mtlk_osal_lock_release(&mhi_stats->lock);
    }

    return res;
}

static int
_mtlk_hw_mhi_table_get_offset (mtlk_hw_t *hw, mhi_stats_offs_t *offs,
                               unsigned type, unsigned subt, unsigned unit_size)
{
    StatsEntryDecoded_t *entry;

    MTLK_ASSERT(offs);
    entry = (void *)hw->hw_mhi_stats.table + hw->chi_area.sStatisticsManager.sData.u32TypeTableList[type];
    entry += subt;
    offs->start_offset  = entry->offset;          /* fill anyway */
    offs->next_idx_offs = entry->nextIdxOffset;

    ILOG2_DDDDDD("MHI stats: entry %d, type %d, subt %d, offs 0x%04X (%d), idx_offs %2d",
                entry - hw->hw_mhi_stats.table,
                type, subt,
                offs->start_offset, offs->start_offset,
                offs->next_idx_offs);

    /* Set next_idx_offs if it is not set */
    if (offs->next_idx_offs == 0)
        offs->next_idx_offs = entry->unitSize;

    /* Check for type and unit_size */
    if ((type != entry->type) || (unit_size != entry->unitSize)) {
        ELOG_DDDDD("Wrong table entry for type %d subt %d unitSize %d: type %d unitSize %d",
                    type, subt, unit_size, entry->type, entry->unitSize);
        return MTLK_ERR_PARAMS;
    } else {
        return MTLK_ERR_OK;
    }
}

static int
_mtlk_hw_mhi_parse_cfg_list (mtlk_hw_t *hw, mhi_stats_offs_t *offs_table,
                             mhi_stats_cfg_t *list, int list_size, unsigned unit_size)
{
    int                     i, res = MTLK_ERR_OK;

    for (i = 0; i < list_size; i++) {
        res = _mtlk_hw_mhi_table_get_offset(hw, &offs_table[i], list[i].type, list[i].subtype, unit_size);
        if (MTLK_ERR_OK != res) {
            break;
        }
    }

    return res;
}

static int
_mtlk_hw_mhi_parse_table (mtlk_hw_t *hw)
{
    hw_mhi_stats_t      *mhi_stats;
    StatsEntryDecoded_t *entry;
    uint32               total, size, next, entry_offs;
    uint32               type_last;
    uint32               subtype;
    int                  i, num;
    int                  res = MTLK_ERR_NOT_SUPPORTED;

    mhi_stats = &hw->hw_mhi_stats;
    total = mhi_stats->db_size; /* total data size */
    num   = mhi_stats->tab_size / sizeof(StatsEntryDecoded_t);
    if (mhi_stats->tab_size != (num * sizeof(StatsEntryDecoded_t))) {
        ELOG_DD("Table size (%d) is not multiply of %d", mhi_stats->tab_size, sizeof(StatsEntryDecoded_t));
        goto finish; /* error */
    }

    /* store nof_entries instead of nof_bytes and check all entries */
    mhi_stats->tab_size = num;
    entry = mhi_stats->table;
    entry_offs = 0;
    next = 0;
    subtype = 0; /* subtype will be calculated due to it is not filled in the entry */
    type_last = ~0;
    for (i = 0; i < num; i++, entry++, entry_offs += sizeof(*entry)) {
        /* Make convertion for all non uint8 items */
        entry->offset     = MAC_TO_HOST16(entry->offset);
        entry->dimension1 = MAC_TO_HOST16(entry->dimension1);
        size = entry->unitSize * entry->dimension1 * entry->dimension2;

        /* compare offset with table */
        if (type_last != entry->type) {
            type_last  = entry->type;
            subtype = 0; /* reset subtype because type is changed */
            if (entry_offs != hw->chi_area.sStatisticsManager.sData.u32TypeTableList[entry->type]) {
                ELOG_DDD("Mismatch offset with TableList for type %d: %d != %d",
                    entry->type, entry_offs, hw->chi_area.sStatisticsManager.sData.u32TypeTableList[entry->type]);

                goto finish; /* error */
            }
        }

        ILOG2_DDDDDDDDDDD("[%4d: %3d] offs %4d, type %d, subt %2d, unit %d, dims %d (%dx%d), idx_offs %d, size %d",
            entry_offs, i, entry->offset, entry->type, subtype,
            entry->unitSize, entry->numOfDimensions,
            entry->dimension1, entry->dimension2,
            entry->nextIdxOffset,
            size);

        subtype++;
        next = entry->offset + size;
        if (next > total) {
            ELOG_DDDDD("[%4d: %3d] (offs %d + size %d) > (total %d)", entry_offs, i, entry->offset, size, total);
            goto finish; /* error */
        }
    }

    /* Check for HW power table sizes and fill offset's table */

    MTLK_STATIC_ASSERT(STAT_HW_TX_POWER_LAST == ARRAY_SIZE(_hw_mhi_stats_tx_power_list));
    res = _mtlk_hw_mhi_parse_cfg_list(hw, mhi_stats->stats_hw_tx_pw_offs_table,
                            _hw_mhi_stats_tx_power_list,
                            ARRAY_SIZE(_hw_mhi_stats_tx_power_list),
                            sizeof(uint8));

    if (MTLK_ERR_OK != res) {
        goto finish;
    }

    for (i = 0; i < WAVE_CARD_RADIO_NUM_MAX; i++)
        mhi_stats->tx_power[i].pw_size = PHY_STAT_RATE_CBW_NUM;

    /* Check for VAP counters size and fill offset's table */
    MTLK_STATIC_ASSERT(STAT_VAP_TOTAL == ARRAY_SIZE(_hw_mhi_stats_vap_list));

    res = _mtlk_hw_mhi_parse_cfg_list(hw, mhi_stats->stats_vap_offs_table,
                            _hw_mhi_stats_vap_list,
                            ARRAY_SIZE(_hw_mhi_stats_vap_list),
                            sizeof(mhi_stats_txrx_cntr_t));

    if (MTLK_ERR_OK != res) {
        goto finish;
    }

    /* Check for STA counters size and fill offset's table */
    MTLK_STATIC_ASSERT(STAT_STA_CNTR_TOTAL == ARRAY_SIZE(_hw_mhi_stats_sta_list));

    res = _mtlk_hw_mhi_parse_cfg_list(hw, mhi_stats->stats_sta_offs_table,
                            _hw_mhi_stats_sta_list,
                            ARRAY_SIZE(_hw_mhi_stats_sta_list),
                            sizeof(mhi_stats_txrx_cntr_t));

    if (MTLK_ERR_OK != res) {
        goto finish;
    }

    /* Check for STAs TX rate table */
    MTLK_STATIC_ASSERT(STAT_STA_TX_RATE_TOTAL == ARRAY_SIZE(_hw_mhi_stats_tx_rate_list));

    res = _mtlk_hw_mhi_parse_cfg_list(hw, mhi_stats->stats_tx_rate_offs_table,
                            _hw_mhi_stats_tx_rate_list,
                            ARRAY_SIZE(_hw_mhi_stats_tx_rate_list),
                            sizeof(uint8));

    if (MTLK_ERR_OK != res) {
        goto finish;
    }

    for (i = 0; i < STAT_STA_TX_RATE_TOTAL; i++) {
        MTLK_ASSERT(1 == mhi_stats->stats_tx_rate_offs_table[i].next_idx_offs);
    }

    res = _mtlk_hw_mhi_table_get_offset(hw, &mhi_stats->tx_time_offs, STATISTIC_TYPE_LINK_ADAPTATION_STATS,
                                        LINK_ADAPTATION_SU_TX_TIME, sizeof(uint32));

    /* second dimension for 2D arrays */
    mhi_stats->stats_tx_rate_tables_dim2 = (_hw_type_is_gen5(hw) ||
      _hw_type_is_gen6(hw)) ? (1 + CW_160) : 1;

finish:
    return res;
}

/* Get MHI Statistics Table using the same UMI req as for the Statistics Data */
static int
_mtlk_hw_mhi_get_table (mtlk_hw_t *hw)
{
    mtlk_txmm_msg_t          man_msg;
    mtlk_txmm_data_t        *man_entry;
    UMI_GET_STATISTICS_DATA *req;
    hw_mhi_stats_t          *mhi_stats;
    uint32                   dma_addr;
    int                      res = MTLK_ERR_UNKNOWN;

    /* prepare msg for the FW */
    if (!(man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, &hw->txmm, NULL))) {
        ELOG_D("card_idx %d: GET_STATISTICS_TABLE_REQ init failed", hw->card_idx);
      return MTLK_ERR_NO_RESOURCES;
    }

    mhi_stats = &hw->hw_mhi_stats;

    dma_addr = mtlk_osal_map_to_phys_addr(mtlk_ccr_get_dev_ctx(hw->ccr),
                                          mhi_stats->db_data,
                                          mhi_stats->tab_size,   /* table size */
                                          MTLK_DATA_FROM_DEVICE);
    if (!dma_addr) {
      ILOG2_P("WARNING: failed mapping 0x%p to physical address", mhi_stats->db_data);
      return MTLK_ERR_NO_RESOURCES;
    }

    man_entry->id = UM_MAN_GET_STATISTICS_TABLE_REQ;
    man_entry->payload_size = sizeof(*req);
    req = (UMI_GET_STATISTICS_DATA *) man_entry->payload;
    req->ddrBufferAddress = HOST_TO_MAC32(dma_addr);
    req->u16length        = HOST_TO_MAC16(mhi_stats->tab_size);
    req->u8status         = UMI_OK;

    res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);

    mtlk_osal_unmap_phys_addr(mtlk_ccr_get_dev_ctx(hw->ccr),
                              dma_addr, mhi_stats->tab_size,
                              MTLK_DATA_FROM_DEVICE);

    mtlk_txmm_msg_cleanup(&man_msg);

    if (res != MTLK_ERR_OK) {
        ELOG_DD("card_idx %d: Sending failed, err=%i", hw->card_idx, res);
    } else if (UMI_OK != req->u8status) {
        ELOG_D("Failed to retrieve MHI statistics, error %d", req->u8status);
        res = MTLK_ERR_MAC;
    }

    /* without lock because it is called once during init */
    if (res == MTLK_ERR_OK) {
        ILOG0_DDPD("CID-%02x: MHI statistics table: size %d, buffer 0x%p, phys_addr 0x%08X",
                   hw->card_idx, mhi_stats->tab_size,
                   mhi_stats->table, virt_to_phys(mhi_stats->table));

        wave_memcpy(mhi_stats->table, mhi_stats->tab_size, mhi_stats->db_data, mhi_stats->tab_size);
        memset(mhi_stats->db_data, 0, mhi_stats->tab_size); /* clean */
        res = _mtlk_hw_mhi_parse_table(hw);
    }

    return res;
}

int __MTLK_IFUNC
mtlk_hw_mhi_copy_stats (mtlk_hw_t *hw, uint8 *buff, uint32 *size)
{
    hw_mhi_stats_t          *mhi_stats;

    MTLK_ASSERT(hw);
    MTLK_ASSERT(buff);
    MTLK_ASSERT(size);

    mhi_stats = &hw->hw_mhi_stats;

    if (*size < mhi_stats->db_size) {
        return MTLK_ERR_NO_MEM;
    }

    *size = mhi_stats->db_size;
    mtlk_osal_lock_acquire(&mhi_stats->lock);
    wave_memcpy(buff, *size, mhi_stats->db_copy, *size);
    mtlk_osal_lock_release(&mhi_stats->lock);

    return MTLK_ERR_OK;
}

void __MTLK_IFUNC
mtlk_hw_mhi_get_vap_stats (mtlk_hw_t *hw, mtlk_mhi_stats_vap_t *cntrs, unsigned vap_id)
{
    hw_mhi_stats_t          *mhi_stats;
    mhi_stats_txrx_cntr_t   *hw_cntr;
    mhi_stats_txrx_cntr_t   *values;
    mhi_stats_offs_t        *offsets;
    unsigned                 db_offs;
    int                      i;

    MTLK_ASSERT(hw);
    MTLK_ASSERT(vap_id < hw->max_vaps_fw); /* max_vaps for card */

    ILOG3_DD("hw_idx %d, VapID %u", hw->card_idx, vap_id);

    values    = (uint32 *)cntrs->stat;
    mhi_stats = &hw->hw_mhi_stats;
    offsets   = mhi_stats->stats_vap_offs_table;

    mtlk_osal_lock_acquire(&mhi_stats->lock);

    for (i = 0; i < ARRAY_SIZE(mhi_stats->stats_vap_offs_table); i++) {
        db_offs = offsets[i].start_offset + (vap_id * offsets[i].next_idx_offs);
        hw_cntr = (void *)mhi_stats->db_copy + db_offs;
        values[i] = MHI_STATS_TXRX_CNTR_TO_HOST(*hw_cntr);
        ILOG3_DDD("[%2d] offs %4d, val %d", i, db_offs, values[i]);
    }

    mtlk_osal_lock_release(&mhi_stats->lock);
}
#else /* MTLK_LEGACY_STATISTICS */
void __MTLK_IFUNC
mtlk_hw_mhi_get_vap_stats (mtlk_hw_t *hw, mtlk_mhi_stats_vap_t *vap_stats, unsigned vap_id)
{
    hw_statistics_t        *hw_stats;
    int                     chip_id = __hw_mmb_get_chip_id(hw);

    MTLK_ASSERT(hw);
    MTLK_ASSERT(vap_id < hw->max_vaps_fw); /* max_vaps for card */

    hw_stats = &hw->hw_stats;

    mtlk_osal_lock_acquire(&hw_stats->lock);
    vap_stats->stats.txInUnicastHd = *(uint32*)(MTLK_STATS_COPY_OFFSET(hostIfCounters.vapCounts[vap_id].txInUnicastHd, chip_id));
    vap_stats->stats.txInMulticastHd = *(uint32*)(MTLK_STATS_COPY_OFFSET(hostIfCounters.vapCounts[vap_id].txInMulticastHd, chip_id));
    vap_stats->stats.txInBroadcastHd = *(uint32*)(MTLK_STATS_COPY_OFFSET(hostIfCounters.vapCounts[vap_id].txInBroadcastHd, chip_id));
    vap_stats->stats.txInUnicastNumOfBytes = *(uint32*)(MTLK_STATS_COPY_OFFSET(hostIfCounters.vapCounts[vap_id].txInUnicastNumOfBytes, chip_id));
    vap_stats->stats.txInMulticastNumOfBytes = *(uint32*)(MTLK_STATS_COPY_OFFSET(hostIfCounters.vapCounts[vap_id].txInMulticastNumOfBytes, chip_id));
    vap_stats->stats.txInBroadcastNumOfBytes = *(uint32*)(MTLK_STATS_COPY_OFFSET(hostIfCounters.vapCounts[vap_id].txInBroadcastNumOfBytes, chip_id));
    vap_stats->stats.rxOutUnicastHd = *(uint32*)(MTLK_STATS_COPY_OFFSET(hostIfCounters.vapCounts[vap_id].rxOutUnicatHd, chip_id));
    vap_stats->stats.rxOutMulticastHd = *(uint32*)(MTLK_STATS_COPY_OFFSET(hostIfCounters.vapCounts[vap_id].rxOutMulticastHd, chip_id));
    vap_stats->stats.rxOutBroadcastHd = *(uint32*)(MTLK_STATS_COPY_OFFSET(hostIfCounters.vapCounts[vap_id].rxOutBroadcastHd, chip_id));
    vap_stats->stats.rxOutUnicastNumOfBytes = *(uint32*)(MTLK_STATS_COPY_OFFSET(hostIfCounters.vapCounts[vap_id].rxOutUnicastNumOfBytes, chip_id));
    vap_stats->stats.rxOutMulticastNumOfBytes = *(uint32*)(MTLK_STATS_COPY_OFFSET(hostIfCounters.vapCounts[vap_id].rxOutMulticastNumOfBytes, chip_id));
    vap_stats->stats.rxOutBroadcastNumOfBytes = *(uint32*)(MTLK_STATS_COPY_OFFSET(hostIfCounters.vapCounts[vap_id].rxOutBroadcastNumOfBytes, chip_id));
    vap_stats->stats.agerCount = *(uint32*)(MTLK_STATS_COPY_OFFSET(hostIfCounters.vapCounts[vap_id].agerCount, chip_id));

    vap_stats->stats.dropMpdu = *(uint32*)(MTLK_STATS_COPY_OFFSET(rxCounters.rxppVapCounts[vap_id].dropMpdu, chip_id));
    vap_stats->stats.mpduUnicastOrMngmnt = *(uint32*)(MTLK_STATS_COPY_OFFSET(rxCounters.rxppVapCounts[vap_id].mpduUnicastOrMngmnt, chip_id));
    vap_stats->stats.mpduRetryCount = *(uint32*)(MTLK_STATS_COPY_OFFSET(rxCounters.rxppVapCounts[vap_id].mpduRetryCount, chip_id));
    vap_stats->stats.amsdu = *(uint32*)(MTLK_STATS_COPY_OFFSET(rxCounters.vapCounts[vap_id].amsdu, chip_id));
    vap_stats->stats.mpduTypeNotSupported = *(uint32*)(MTLK_STATS_COPY_OFFSET(rxCounters.rxppVapCounts[vap_id].mpduTypeNotSupported, chip_id));
    vap_stats->stats.replayData = *(uint32*)(MTLK_STATS_COPY_OFFSET(rxCounters.rxppVapCounts[vap_id].replayData, chip_id));
    vap_stats->stats.replayMngmnt = *(uint32*)(MTLK_STATS_COPY_OFFSET(rxCounters.rxppVapCounts[vap_id].replayMngmnt, chip_id));
    if (_chipid_is_gen6_a0(chip_id)) {
      vap_stats->stats.tkipCount = *(uint32*)( hw_stats->stats_copy + (int)MTLK_OFFSET_OF(StatisticsDb_t_wave600, rxCounters.rxppVapCounts[vap_id].tkipCount));
      vap_stats->stats.securityFailure = *(uint32*)( hw_stats->stats_copy + (int)MTLK_OFFSET_OF(StatisticsDb_t_wave600, rxCounters.rxppVapCounts[vap_id].securityFailure));
    } else /* if(_chipid_is_gen6_b0(chip_id)) */ { /* all others: b0 and d2 */
      vap_stats->stats.tkipCount = *(uint32*)( hw_stats->stats_copy + (int)MTLK_OFFSET_OF(StatisticsDb_t_wave600b, rxCounters.vapCounts[vap_id].tkipCount));
      vap_stats->stats.securityFailure = *(uint32*)( hw_stats->stats_copy + (int)MTLK_OFFSET_OF(StatisticsDb_t_wave600b, rxCounters.vapCounts[vap_id].securityFailure));
    }

    vap_stats->stats.amsduBytes = *(uint32*)(MTLK_STATS_COPY_OFFSET(rxCounters.vapCounts[vap_id].amsduBytes, chip_id));
    vap_stats->stats.ampdu = *(uint32*)(MTLK_STATS_COPY_OFFSET(rxCounters.rxppVapCounts[vap_id].ampdu, chip_id));
    vap_stats->stats.mpduInAmpdu = *(uint32*)(MTLK_STATS_COPY_OFFSET(rxCounters.vapCounts[vap_id].mpduInAmpdu, chip_id));
    vap_stats->stats.octetsInAmpdu = *(uint32*)(MTLK_STATS_COPY_OFFSET(rxCounters.vapCounts[vap_id].octetsInAmpdu, chip_id));
    vap_stats->stats.rxClassifierSecurityMismatch = *(uint32*)(MTLK_STATS_COPY_OFFSET(rxCounters.vapCounts[vap_id].rxCoordinatorSecurityMismatch, chip_id));
    vap_stats->stats.bcMcCountVap = *(uint32*)(MTLK_STATS_COPY_OFFSET(rxCounters.rxppVapCounts[vap_id].bcMcCountVap, chip_id));

    vap_stats->stats.rtsSuccessCount = *(uint32*)(MTLK_STATS_COPY_OFFSET(baaCounters.vapCounts[vap_id].rtsSuccessCount, chip_id));
    vap_stats->stats.qosTransmittedFrames = *(uint32*)(MTLK_STATS_COPY_OFFSET(baaCounters.vapCounts[vap_id].qosTransmittedFrames, chip_id));
    vap_stats->stats.transmittedAmsdu = *(uint32*)(MTLK_STATS_COPY_OFFSET(baaCounters.vapCounts[vap_id].transmittedAmsdu, chip_id));
    vap_stats->stats.transmittedOctetsInAmsdu = *(uint32*)(MTLK_STATS_COPY_OFFSET(baaCounters.vapCounts[vap_id].transmittedOctetsInAmsdu, chip_id));
    vap_stats->stats.transmittedAmpdu = *(uint32*)(MTLK_STATS_COPY_OFFSET(baaCounters.vapCounts[vap_id].transmittedAmpdu, chip_id));
    vap_stats->stats.transmittedMpduInAmpdu = *(uint32*)(MTLK_STATS_COPY_OFFSET(baaCounters.vapCounts[vap_id].transmittedMpduInAmpdu, chip_id));
    vap_stats->stats.transmittedOctetsInAmpdu = *(uint32*)(MTLK_STATS_COPY_OFFSET(baaCounters.vapCounts[vap_id].transmittedOctetsInAmpdu, chip_id));
    vap_stats->stats.beamformingFrames = *(uint32*)(MTLK_STATS_COPY_OFFSET(baaCounters.vapCounts[vap_id].beamformingFrames, chip_id));
    vap_stats->stats.transmitStreamRprtMSDUFailed = *(uint32*)(MTLK_STATS_COPY_OFFSET(baaCounters.vapCounts[vap_id].transmitStreamRprtMSDUFailed, chip_id));
    vap_stats->stats.rtsFailure = *(uint32*)(MTLK_STATS_COPY_OFFSET(baaCounters.vapCounts[vap_id].rtsFailure, chip_id));
    vap_stats->stats.ackFailure = *(uint32*)(MTLK_STATS_COPY_OFFSET(baaCounters.vapCounts[vap_id].ackFailure, chip_id));
    vap_stats->stats.failedAmsdu = *(uint32*)(MTLK_STATS_COPY_OFFSET(baaCounters.vapCounts[vap_id].failedAmsdu, chip_id));
    vap_stats->stats.retryAmsdu = *(uint32*)(MTLK_STATS_COPY_OFFSET(baaCounters.vapCounts[vap_id].retryAmsdu, chip_id));
    vap_stats->stats.multipleRetryAmsdu = *(uint32*)(MTLK_STATS_COPY_OFFSET(baaCounters.vapCounts[vap_id].multipleRetryAmsdu, chip_id));
    vap_stats->stats.amsduAckFailure = *(uint32*)(MTLK_STATS_COPY_OFFSET(baaCounters.vapCounts[vap_id].amsduAckFailure, chip_id));
    vap_stats->stats.implicitBarFailure = *(uint32*)(MTLK_STATS_COPY_OFFSET(baaCounters.vapCounts[vap_id].implicitBarFailure, chip_id));
    vap_stats->stats.explicitBarFailure = *(uint32*)(MTLK_STATS_COPY_OFFSET(baaCounters.vapCounts[vap_id].explicitBarFailure, chip_id));
    vap_stats->stats.transmitStreamRprtMultipleRetryCount = *(uint32*)(MTLK_STATS_COPY_OFFSET(baaCounters.vapCounts[vap_id].transmitStreamRprtMultipleRetryCount, chip_id));
    vap_stats->stats.transmitBw20 = *(uint32*)(MTLK_STATS_COPY_OFFSET(baaCounters.vapCounts[vap_id].transmitBw20, chip_id));
    vap_stats->stats.transmitBw40 = *(uint32*)(MTLK_STATS_COPY_OFFSET(baaCounters.vapCounts[vap_id].transmitBw40, chip_id));
    vap_stats->stats.transmitBw80 = *(uint32*)(MTLK_STATS_COPY_OFFSET(baaCounters.vapCounts[vap_id].transmitBw80, chip_id));
    vap_stats->stats.rxGroupFrame = *(uint32*)(MTLK_STATS_COPY_OFFSET(baaCounters.rxGroupFrame[vap_id], chip_id));
    vap_stats->stats.txSenderError = *(uint32*)(MTLK_STATS_COPY_OFFSET(baaCounters.txSenderError[vap_id], chip_id));
    mtlk_osal_lock_release(&hw_stats->lock);
}
#endif /* MTLK_LEGACY_STATISTICS */

#ifdef MTLK_LEGACY_STATISTICS
static void
_mtlk_hw_mhi_sta_fill_tx_rate_statistic (mtlk_hw_t *hw, mtlk_mhi_stats_sta_tx_rate_t *tx_stats, uint32 sta_sid)
{
    hw_mhi_stats_t      *mhi_stats = &hw->hw_mhi_stats;
    mhi_stats_offs_t    *offs_table;
    uint8               *db_data;
    unsigned             cbw, idx2;

    /* all inputs params are Ok */

    db_data    = mhi_stats->db_copy;
    offs_table = mhi_stats->stats_tx_rate_offs_table;

    mtlk_osal_lock_acquire(&mhi_stats->lock);

    /* One dimentional arrays */
#define STA_STAT_ITEM(id)  { tx_stats->stat[id] = db_data[offs_table[id].start_offset + sta_sid]; }

    STA_STAT_ITEM(STAT_STA_TX_RATE_CBW);
    STA_STAT_ITEM(STAT_STA_TX_BF_MODE);
    STA_STAT_ITEM(STAT_STA_TX_STBC_MODE);

#undef STA_STAT_ITEM

    /* we need to use idx2=cbw on Gen5/Gen6 */
    cbw = tx_stats->stat[STAT_STA_TX_RATE_CBW];
    idx2 = cbw;

    if (idx2 >= mhi_stats->stats_tx_rate_tables_dim2) {
        ELOG_DDD("SID 0x%02X cbw too big (%d >= %d)", sta_sid, cbw, mhi_stats->stats_tx_rate_tables_dim2);
    } else {
        /* all data (excluding cbw) are 2-dims arrays: data[sid][dim2] */
        /* so data offset is the same for all arrays */
        db_data += sta_sid * mhi_stats->stats_tx_rate_tables_dim2;  /* (sid*dim2) */
        db_data += idx2;                                            /* idx2 */

#define STA_STAT_ITEM(id)  { tx_stats->stat[id] = db_data[offs_table[id].start_offset]; }

        STA_STAT_ITEM(STAT_STA_TX_RATE_MODE);
        STA_STAT_ITEM(STAT_STA_TX_RATE_SCP);
        STA_STAT_ITEM(STAT_STA_TX_RATE_MCS);
        STA_STAT_ITEM(STAT_STA_TX_RATE_NSS);
        STA_STAT_ITEM(STAT_STA_TX_POWER_DATA);
        STA_STAT_ITEM(STAT_STA_TX_POWER_MGMT);
        STA_STAT_ITEM(STAT_STA_TX_PHYMODE_MGMT);

#undef STA_STAT_ITEM
    }

    mtlk_osal_lock_release(&mhi_stats->lock);
}
#else /* MTLK_LEGACY_STATISTICS */

static void
_mtlk_hw_mhi_sta_fill_tx_rate_statistic (mtlk_hw_t *hw, mtlk_mhi_stats_sta_tx_rate_t *tx_stats, uint32 sta_sid)
{
    hw_statistics_t    *hw_stats = &hw->hw_stats;
    uint8               cbw = 0;
    int                 chip_id = __hw_mmb_get_chip_id(hw);

    mtlk_osal_lock_acquire(&hw_stats->lock);
    tx_stats->dataBwLimit = *(uint32*)(MTLK_STATS_COPY_OFFSET(linkAdaptationStats.dataBwLimit[sta_sid], chip_id));

    cbw = tx_stats->dataBwLimit;
    tx_stats->DataPhyMode = *(uint32*)(MTLK_STATS_COPY_OFFSET(linkAdaptationStats.DataPhyMode[sta_sid][cbw], chip_id));
    tx_stats->scpData = *(uint32*)(MTLK_STATS_COPY_OFFSET(linkAdaptationStats.scpData[sta_sid][cbw], chip_id));
    tx_stats->mcsData = *(uint32*)(MTLK_STATS_COPY_OFFSET(linkAdaptationStats.mcsData[sta_sid][cbw], chip_id));
    tx_stats->nssData = *(uint32*)(MTLK_STATS_COPY_OFFSET(linkAdaptationStats.nssData[sta_sid][cbw], chip_id));
    tx_stats->bfModeData = *(uint32*)(MTLK_STATS_COPY_OFFSET(linkAdaptationStats.bfModeData[sta_sid], chip_id));
    tx_stats->stbcModeData = *(uint32*)(MTLK_STATS_COPY_OFFSET(linkAdaptationStats.stbcModeData[sta_sid], chip_id));
    tx_stats->powerData = *(uint32*)(MTLK_STATS_COPY_OFFSET(linkAdaptationStats.powerData[sta_sid][cbw], chip_id));
    tx_stats->powerManagement = *(uint32*)(MTLK_STATS_COPY_OFFSET(linkAdaptationStats.powerManagement[sta_sid][cbw], chip_id));
    tx_stats->ManagementPhyMode = *(uint32*)(MTLK_STATS_COPY_OFFSET(linkAdaptationStats.ManagementPhyMode[sta_sid][cbw], chip_id));
    mtlk_osal_lock_release(&hw_stats->lock);
}
#endif /* MTLK_LEGACY_STATISTICS */

#define STAT_POLL_PERIOD_USEC (MTLK_OSAL_USEC_IN_MSEC * _DF_STAT_POLL_PERIOD)

uint32 __MTLK_IFUNC
wave_hw_calculate_airtime_efficiency (uint64 bytes, uint32 time)
{
  uint64 efficiency = bytes;

  /* modifies the 64-bit dividend _in_place_ */
  do_div(efficiency, time); /* ignore the reminder */

  if (efficiency > MAX_UINT32) {
    ILOG1_HHD("Efficiency of %llu bytes per second? deltaBytes %llu, time %u", efficiency, bytes, time);
    efficiency = 0;
  }
  return (uint32)efficiency;
}

#ifndef MTLK_LEGACY_STATISTICS
void update_overrun_of_stats (uint64* sum, uint32* prev_val, unsigned long* new_val)
{
  uint32 delta;

  delta = *new_val - *prev_val;
  *sum += (uint64) delta;
  *prev_val = *new_val;

}

#endif

static void
_mtlk_sta_update_statistics (mtlk_hw_t *hw, sta_entry *sta, stationPhyRxStatusDb_t *sta_status)
{
  uint32             new_rx_time, new_tx_time, airtime, efficiency = 0;
  uint32             sta_sid, rx_bytes, tx_bytes, delta_bytes;
  airtime_stats_t   *as = &sta->info.stats.airtime_stats;
#ifdef MTLK_LEGACY_STATISTICS
  int                i;
  unsigned           db_offs;
  uint32            *new_tx_time_p;
  mhi_sta_cntr_t    *values;
  mhi_sta_cntr_t    *hw_cntr;
  uint8             *db_data = hw->hw_mhi_stats.db_copy;
  mhi_stats_offs_t  *offsets = hw->hw_mhi_stats.stats_sta_offs_table;
  mhi_stats_offs_t  *tx_time_offs = &hw->hw_mhi_stats.tx_time_offs;
#else
  mtlk_mhi_stats_sta_cntr_t *values;
  mtlk_mhi_stats64_sta_cntr_t *stats64;
  hw_statistics_t *hw_stats;
  unsigned long tx_errors;
  unsigned vap_id = mtlk_vap_get_id(sta->vap_handle);
  int chip_id = __hw_mmb_get_chip_id(hw);

  hw_stats = &hw->hw_stats;
#endif
  sta_sid = mtlk_sta_get_sid(sta);

#ifdef MTLK_LEGACY_STATISTICS
  /* Get RX Time from PHY Statistics */
  new_rx_time = MAC_TO_HOST32(sta_status->perClientRxtimeUsage);

  /* Get TX Time from MAC Statistics */
  new_tx_time_p = (void *)db_data + tx_time_offs->start_offset + (sta_sid * tx_time_offs->next_idx_offs);
  new_tx_time = MAC_TO_HOST32(*new_tx_time_p);
#else
  /* Get RX Time from PHY Statistics */
  new_rx_time = sta_status->perClientRxtimeUsage;

  mtlk_osal_lock_acquire(&hw_stats->lock);
  new_tx_time = *(uint32*)(MTLK_STATS_COPY_OFFSET(linkAdaptationStats.totalTxTime[sta_sid], chip_id));
#endif

  /* RX time and TX time are coming from different entities and requests,
   * so it is possible, although unlikely, that their sum is bigger than polling period */
  airtime = (new_rx_time - as->rx_time) + (new_tx_time - as->tx_time);
  airtime = MIN((MTLK_OSAL_USEC_IN_MSEC * hw->stats_poll_period), airtime);
  as->airtime_usage = MTLK_VALUE_TO_PERCENT(airtime, (MTLK_OSAL_USEC_IN_MSEC * hw->stats_poll_period));

  values = mtlk_sta_get_mhi_stat_array(sta);
  stats64 = mtlk_sta_get_mhi_stat64_array(sta);

#ifdef MTLK_LEGACY_STATISTICS
  /* Previous TX/RX Bytes */
  rx_bytes = values[STAT_STA_CNTR_RX_BYTES];
  tx_bytes = values[STAT_STA_CNTR_TX_BYTES];

  for (i = 0; i < STAT_STA_CNTR_TOTAL; i++) {
    db_offs = offsets[i].start_offset + (sta_sid * offsets[i].next_idx_offs);
    hw_cntr = (void *)hw->hw_mhi_stats.db_copy + db_offs;
    values[i] = MHI_STATS_STA_CNTR_TO_HOST(*hw_cntr);
    ILOG3_DDD("[%2d] offs %4d, val %d", i, db_offs, values[i]);
  }

  delta_bytes = (values[STAT_STA_CNTR_RX_BYTES] - rx_bytes) + (values[STAT_STA_CNTR_TX_BYTES] - tx_bytes);

#else /* MTLK_LEGACY_STATISTICS */
  rx_bytes = values->rxOutStaNumOfBytes;
  tx_bytes = values->mpduByteTransmitted;
  tx_errors = *(uint32*)(MTLK_STATS_COPY_OFFSET(baaCounters.vapCounts[vap_id].rtsFailure, chip_id)) +
              *(uint32*)(MTLK_STATS_COPY_OFFSET(baaCounters.vapCounts[vap_id].transmitStreamRprtMSDUFailed, chip_id)) +
              *(uint32*)(MTLK_STATS_COPY_OFFSET(baaCounters.vapCounts[vap_id].ackFailure, chip_id)) +
              *(uint32*)(MTLK_STATS_COPY_OFFSET(baaCounters.vapCounts[vap_id].failedAmsdu, chip_id)) +
              *(uint32*)(MTLK_STATS_COPY_OFFSET(baaCounters.txSenderError[vap_id], chip_id)) +
              *(uint32*)(MTLK_STATS_COPY_OFFSET(baaCounters.fwPairWiseMicFailurePackets[vap_id], chip_id)) +
              *(uint32*)(MTLK_STATS_COPY_OFFSET(baaCounters.fwMulticastReplayedPackets[vap_id], chip_id));

  update_overrun_of_stats(&stats64->mpduFirstRetransmission, &values->mpduFirstRetransmission,
                                   (unsigned long*)(MTLK_STATS_COPY_OFFSET(baaCounters.staCounts[sta_sid].mpduFirstRetransmission, chip_id)));

  update_overrun_of_stats(&stats64->mpduTransmitted, &values->mpduTransmitted,
                                   (unsigned long*)(MTLK_STATS_COPY_OFFSET(baaCounters.staCounts[sta_sid].mpduTransmitted, chip_id)));

  update_overrun_of_stats(&stats64->mpduByteTransmitted, &values->mpduByteTransmitted,
                                   (unsigned long*)(MTLK_STATS_COPY_OFFSET(baaCounters.staCounts[sta_sid].mpduByteTransmitted, chip_id)));

  update_overrun_of_stats(&stats64->rdCount, &values->rdCount,
                                   (unsigned long*)(MTLK_STATS_COPY_OFFSET(rxCounters.rdCount[sta_sid], chip_id)));

  update_overrun_of_stats(&stats64->rxOutStaNumOfBytes, &values->rxOutStaNumOfBytes,
                                   (unsigned long*)(MTLK_STATS_COPY_OFFSET(hostIfCounters.staCounts[sta_sid].rxOutStaNumOfBytes, chip_id)));

  update_overrun_of_stats(&stats64->swUpdateDrop, &values->swUpdateDrop,
                                   (unsigned long*)(MTLK_STATS_COPY_OFFSET(rxCounters.staCounts[sta_sid].swUpdateDrop, chip_id)));

  update_overrun_of_stats(&stats64->mpduRetransmission, &values->mpduRetransmission,
      (unsigned long *)( hw_stats->stats_data + (int)MTLK_OFFSET_OF(StatisticsDb_t_wave600b, baaCounters.staCounts[sta_sid].mpduRetransmission)));

  update_overrun_of_stats(&stats64->exhaustedCount, &values->exhaustedCount,
                                   (unsigned long*)(MTLK_STATS_COPY_OFFSET(perClientStats.exhaustedCount[sta_sid], chip_id)));

  update_overrun_of_stats(&stats64->successCount, &values->successCount,
                                   (unsigned long*)(MTLK_STATS_COPY_OFFSET(perClientStats.successCount[sta_sid], chip_id)));

  update_overrun_of_stats(&stats64->rdDuplicateDrop, &values->rdDuplicateDrop,
                                   (unsigned long*)(MTLK_STATS_COPY_OFFSET(rxCounters.staCounts[sta_sid].rdDuplicateDrop, chip_id)));

  update_overrun_of_stats(&stats64->missingSn, &values->missingSn,
                                   (unsigned long*)(MTLK_STATS_COPY_OFFSET(rxCounters.staCounts[sta_sid].missingSn, chip_id)));

  update_overrun_of_stats(&stats64->mpduInAmpdu, &values->mpduInAmpdu,
                                   (unsigned long*)(MTLK_STATS_COPY_OFFSET(rxCounters.vapCounts[vap_id].mpduInAmpdu, chip_id)));

  update_overrun_of_stats(&stats64->ampdu, &values->ampdu,
                                   (unsigned long*)(MTLK_STATS_COPY_OFFSET(rxCounters.rxppVapCounts[vap_id].ampdu, chip_id)));

  update_overrun_of_stats(&stats64->mpduRetryCount, &values->mpduRetryCount,
                                   (unsigned long*)(MTLK_STATS_COPY_OFFSET(rxCounters.rxppVapCounts[vap_id].mpduRetryCount, chip_id)));


  update_overrun_of_stats(&stats64->transmittedAmpdu, &values->transmittedAmpdu,
                                   (unsigned long*)(MTLK_STATS_COPY_OFFSET(baaCounters.vapCounts[vap_id].transmittedAmpdu, chip_id)));

  update_overrun_of_stats(&stats64->tx_errors, &values->tx_errors, &tx_errors);

  mtlk_osal_lock_release(&hw_stats->lock);

  delta_bytes = (values->rxOutStaNumOfBytes - rx_bytes) + (values->mpduByteTransmitted - tx_bytes);
#endif /* MTLK_LEGACY_STATISTICS */

  /* If there is at least one percent air time usage */
  if (airtime && as->airtime_usage) {
    efficiency = wave_hw_calculate_airtime_efficiency((uint64)delta_bytes * MTLK_OSAL_USEC_IN_SEC, airtime);
  }

  as->rx_time = new_rx_time;
  as->tx_time = new_tx_time;
  as->airtime_efficiency = efficiency;
  ILOG3_DDD("Airtime %u (usec), Airtime %u (%%), Airtime Efficiency %u (Bytes/sec)",
            airtime, as->airtime_usage, as->airtime_efficiency);
}

#ifndef MTLK_LEGACY_STATISTICS
static void
_mtlk_hw_statistics_cleanup (mtlk_hw_t *hw)
{
    hw_statistics_t    *hw_stats;

    hw_stats = &hw->hw_stats;

    _hw_cleanup_phy_rx_status(hw);

    if (hw_stats->stats_data) mtlk_osal_mem_free(hw_stats->stats_data);
    if (hw_stats->stats_copy) mtlk_osal_mem_free(hw_stats->stats_copy);

    mtlk_osal_lock_cleanup(&hw_stats->lock);

    memset(hw_stats, 0, sizeof(*hw_stats));
}

void mtlk_hw_set_stats_available (mtlk_hw_t *hw, BOOL value)
{
  hw_statistics_t *hw_stats = &hw->hw_stats;
  hw_stats->available = value;
  return;
}

BOOL mtlk_hw_get_stats_available (mtlk_hw_t *hw)
{
  hw_statistics_t *hw_stats = &hw->hw_stats;
  return hw_stats->available;
}

static inline void CONVERT_ENDIANESS_UINT16 (uint16* parameter,int iterations)
{
    int i;
    for (i = 0; i < iterations; i++) {
        parameter[i] = MAC_TO_HOST16(parameter[i]);
        ILOG3_DD("Parameter - %d index - %d\n", parameter[i], i);
    }
}

static inline void CONVERT_ENDIANESS_2D_UINT32 (uint32 *parameter,int rows,int column)
{
    int i, j;
    for (i = 0; i < rows; i++) {
        for (j = 0; j < column; j++) {
            *(parameter + (i * column) + (j)) = MAC_TO_HOST32(*(parameter + (i * column) + (j)));
            ILOG3_DDD("Parameter - %d index1 - %d index2 - %d\n", *(parameter + (i * column) + (j)), i, j);
        }
    }
}

static inline void CONVERT_ENDIANESS_UINT32 (uint32 *parameter,int iterations)
{
    int i;
    for (i = 0; i < iterations; i++) {
        parameter[i] = MAC_TO_HOST32(parameter[i]);
        ILOG3_DD("Parameter - %d index - %d\n", parameter[i], i);
    }
}

static inline void CONVERT_ENDIANESS_3D_UINT32 (uint32* parameter,int rows,int column, int iter)
{
    int i, j, k;
    for (i = 0; i < rows; i++) {
        for (j = 0; j < column; j++) {
          for (k = 0; k < iter; k++) {
            *(parameter + (i * column * iter) + (j * iter) + k) = MAC_TO_HOST32(*(parameter + (i * column * iter) + (j * column) + k));
            ILOG3_DDDD("Parameter - %d index1 - %d, index2 - %d, index3 - %d\n", *(parameter + (i * column * iter) + (j * iter) + k), i, j, k);
          }
        }
    }
}

static inline void CONVERT_ENDIANESS_UINT64 (uint64* parameter,int iterations)
{
    int i;
    for (i = 0; i < iterations; i++) {
        parameter[i] = MAC_TO_HOST64(parameter[i]);
        ILOG3_DD("Parameter - %d index - %d\n", parameter[i], i);
    }
}

static void
_mtlk_hw_check_and_convert_endianess (mtlk_hw_t *hw)
{
  hw_statistics_t         *hw_stats;
  int                      i;
  int                      chip_id = __hw_mmb_get_chip_id(hw);
  uint32                  *value;

  hw_stats = &hw->hw_stats;
  CONVERT_ENDIANESS_2D_UINT32( MTLK_STATS_DATA_OFFSET(hostIfCounters.qosByteCountSta[0][0], chip_id), HW_NUM_OF_STATIONS, NUM_OF_TIDS);
  CONVERT_ENDIANESS_2D_UINT32( MTLK_STATS_DATA_OFFSET(hostIfCounters.qosTxSta[0][0], chip_id) , HW_NUM_OF_STATIONS, NUM_OF_TIDS);
  CONVERT_ENDIANESS_2D_UINT32( MTLK_STATS_DATA_OFFSET(hostIfCounters.qosTxVap[0][0], chip_id), HW_NUM_OF_VAPS, ACCESS_CATEGORY_NUM);
  for (i = 0; i < HW_NUM_OF_VAPS; i++) {

    value = MTLK_STATS_DATA_OFFSET(hostIfCounters.vapCounts[i].txInUnicastHd , chip_id);
    *(uint32 *)(value) = MAC_TO_HOST32(*(uint32 *)(value));
    value = MTLK_STATS_DATA_OFFSET(hostIfCounters.vapCounts[i].txInMulticastHd , chip_id);
    *(uint32 *)(value) = MAC_TO_HOST32(*(uint32 *)(value));
    value = MTLK_STATS_DATA_OFFSET(hostIfCounters.vapCounts[i].txInBroadcastHd , chip_id);
    *(uint32 *)(value) = MAC_TO_HOST32(*(uint32 *)(value));
    value = MTLK_STATS_DATA_OFFSET(hostIfCounters.vapCounts[i].txInUnicastNumOfBytes , chip_id);
    *(uint32 *)(value) = MAC_TO_HOST32(*(uint32 *)(value));
    value = MTLK_STATS_DATA_OFFSET(hostIfCounters.vapCounts[i].txInMulticastNumOfBytes , chip_id);
    *(uint32 *)(value) = MAC_TO_HOST32(*(uint32 *)(value));
    value = MTLK_STATS_DATA_OFFSET(hostIfCounters.vapCounts[i].txInBroadcastNumOfBytes , chip_id);
    *(uint32 *)(value) = MAC_TO_HOST32(*(uint32 *)(value));
    value = MTLK_STATS_DATA_OFFSET(hostIfCounters.vapCounts[i].rxOutUnicatHd , chip_id);
    *(uint32 *)(value) = MAC_TO_HOST32(*(uint32 *)(value));
    value = MTLK_STATS_DATA_OFFSET(hostIfCounters.vapCounts[i].rxOutMulticastHd , chip_id);
    *(uint32 *)(value) = MAC_TO_HOST32(*(uint32 *)(value));
    value = MTLK_STATS_DATA_OFFSET(hostIfCounters.vapCounts[i].rxOutBroadcastHd , chip_id);
    *(uint32 *)(value) = MAC_TO_HOST32(*(uint32 *)(value));
    value = MTLK_STATS_DATA_OFFSET(hostIfCounters.vapCounts[i].rxOutUnicastNumOfBytes , chip_id);
    *(uint32 *)(value) = MAC_TO_HOST32(*(uint32 *)(value));
    value = MTLK_STATS_DATA_OFFSET(hostIfCounters.vapCounts[i].rxOutMulticastNumOfBytes , chip_id);
    *(uint32 *)(value) = MAC_TO_HOST32(*(uint32 *)(value));
    value = MTLK_STATS_DATA_OFFSET(hostIfCounters.vapCounts[i].rxOutMulticastNumOfBytes , chip_id);
    *(uint32 *)(value) = MAC_TO_HOST32(*(uint32 *)(value));
    value = MTLK_STATS_DATA_OFFSET(hostIfCounters.vapCounts[i].agerCount , chip_id);
    *(uint32 *)(value) = MAC_TO_HOST32(*(uint32 *)(value));

    value = MTLK_STATS_DATA_OFFSET(rxCounters.vapCounts[i].amsdu , chip_id);
    *(uint32 *)(value) = MAC_TO_HOST32(*(uint32 *)(value));
    value = MTLK_STATS_DATA_OFFSET(rxCounters.vapCounts[i].amsduBytes , chip_id);
    *(uint32 *)(value) = MAC_TO_HOST32(*(uint32 *)(value));
    value = MTLK_STATS_DATA_OFFSET(rxCounters.vapCounts[i].dropCount , chip_id);
    *(uint32 *)(value) = MAC_TO_HOST32(*(uint32 *)(value));
    value = MTLK_STATS_DATA_OFFSET(rxCounters.vapCounts[i].mpduInAmpdu , chip_id);
    *(uint32 *)(value) = MAC_TO_HOST32(*(uint32 *)(value));
    value = MTLK_STATS_DATA_OFFSET(rxCounters.vapCounts[i].octetsInAmpdu , chip_id);
    *(uint32 *)(value) = MAC_TO_HOST32(*(uint32 *)(value));
    value = MTLK_STATS_DATA_OFFSET(rxCounters.vapCounts[i].rxCoordinatorSecurityMismatch , chip_id);
    *(uint32 *)(value) = MAC_TO_HOST32(*(uint32 *)(value));

    if (_chipid_is_gen6_a0(chip_id)) {
      value = ( hw_stats->stats_data + (int)MTLK_OFFSET_OF(StatisticsDb_t_wave600, rxCounters.rxppVapCounts[i].tkipCount));
      *(uint32 *)(value) = MAC_TO_HOST32(*(uint32 *)(value));
      value = ( hw_stats->stats_data + (int)MTLK_OFFSET_OF(StatisticsDb_t_wave600, rxCounters.rxppVapCounts[i].securityFailure));
      *(uint32 *)(value) = MAC_TO_HOST32(*(uint32 *)(value));
    } else /* if (_chipid_is_gen6_b0(chip_id)) */ {
      value = ( hw_stats->stats_data + (int)MTLK_OFFSET_OF(StatisticsDb_t_wave600b, rxCounters.vapCounts[i].tkipCount));
      *(uint32 *)(value) = MAC_TO_HOST32(*(uint32 *)(value));
      value = ( hw_stats->stats_data + (int)MTLK_OFFSET_OF(StatisticsDb_t_wave600b, rxCounters.vapCounts[i].securityFailure));
      *(uint32 *)(value) = MAC_TO_HOST32(*(uint32 *)(value));
    }

    value = MTLK_STATS_DATA_OFFSET(rxCounters.rxppVapCounts[i].mpduUnicastOrMngmnt , chip_id);
    *(uint32 *)(value) = MAC_TO_HOST32(*(uint32 *)(value));
    value = MTLK_STATS_DATA_OFFSET(rxCounters.rxppVapCounts[i].mpduRetryCount , chip_id);
    *(uint32 *)(value) = MAC_TO_HOST32(*(uint32 *)(value));
    value = MTLK_STATS_DATA_OFFSET(rxCounters.rxppVapCounts[i].dropMpdu , chip_id);
    *(uint32 *)(value) = MAC_TO_HOST32(*(uint32 *)(value));
    value = MTLK_STATS_DATA_OFFSET(rxCounters.rxppVapCounts[i].ampdu , chip_id);
    *(uint32 *)(value) = MAC_TO_HOST32(*(uint32 *)(value));
    value = MTLK_STATS_DATA_OFFSET(rxCounters.rxppVapCounts[i].mpduTypeNotSupported , chip_id);
    *(uint32 *)(value) = MAC_TO_HOST32(*(uint32 *)(value));
    value = MTLK_STATS_DATA_OFFSET(rxCounters.rxppVapCounts[i].replayData , chip_id);
    *(uint32 *)(value) = MAC_TO_HOST32(*(uint32 *)(value));
    value = MTLK_STATS_DATA_OFFSET(rxCounters.rxppVapCounts[i].replayMngmnt , chip_id);
    *(uint32 *)(value) = MAC_TO_HOST32(*(uint32 *)(value));
    value = MTLK_STATS_DATA_OFFSET(rxCounters.rxppVapCounts[i].bcMcCountVap , chip_id);
    *(uint32 *)(value) = MAC_TO_HOST32(*(uint32 *)(value));

    value = MTLK_STATS_DATA_OFFSET(baaCounters.vapCounts[i].rtsSuccessCount , chip_id);
    *(uint32 *)(value) = MAC_TO_HOST32(*(uint32 *)(value));
    value = MTLK_STATS_DATA_OFFSET(baaCounters.vapCounts[i].rtsFailure , chip_id);
    *(uint32 *)(value) = MAC_TO_HOST32(*(uint32 *)(value));
    value = MTLK_STATS_DATA_OFFSET(baaCounters.vapCounts[i].transmitStreamRprtMSDUFailed , chip_id);
    *(uint32 *)(value) = MAC_TO_HOST32(*(uint32 *)(value));
    value = MTLK_STATS_DATA_OFFSET(baaCounters.vapCounts[i].qosTransmittedFrames , chip_id);
    *(uint32 *)(value) = MAC_TO_HOST32(*(uint32 *)(value));
    value = MTLK_STATS_DATA_OFFSET(baaCounters.vapCounts[i].transmittedAmsdu , chip_id);
    *(uint32 *)(value) = MAC_TO_HOST32(*(uint32 *)(value));
    value = MTLK_STATS_DATA_OFFSET(baaCounters.vapCounts[i].transmittedOctetsInAmsdu , chip_id);
    *(uint32 *)(value) = MAC_TO_HOST32(*(uint32 *)(value));
    value = MTLK_STATS_DATA_OFFSET(baaCounters.vapCounts[i].transmittedAmpdu , chip_id);
    *(uint32 *)(value) = MAC_TO_HOST32(*(uint32 *)(value));
    value = MTLK_STATS_DATA_OFFSET(baaCounters.vapCounts[i].transmittedMpduInAmpdu , chip_id);
    *(uint32 *)(value) = MAC_TO_HOST32(*(uint32 *)(value));
    value = MTLK_STATS_DATA_OFFSET(baaCounters.vapCounts[i].transmittedOctetsInAmpdu , chip_id);
    *(uint32 *)(value) = MAC_TO_HOST32(*(uint32 *)(value));
    value = MTLK_STATS_DATA_OFFSET(baaCounters.vapCounts[i].beamformingFrames , chip_id);
    *(uint32 *)(value) = MAC_TO_HOST32(*(uint32 *)(value));
    value = MTLK_STATS_DATA_OFFSET(baaCounters.vapCounts[i].ackFailure , chip_id);
    *(uint32 *)(value) = MAC_TO_HOST32(*(uint32 *)(value));
    value = MTLK_STATS_DATA_OFFSET(baaCounters.vapCounts[i].failedAmsdu , chip_id);
    *(uint32 *)(value) = MAC_TO_HOST32(*(uint32 *)(value));
    value = MTLK_STATS_DATA_OFFSET(baaCounters.vapCounts[i].retryAmsdu , chip_id);
    *(uint32 *)(value) = MAC_TO_HOST32(*(uint32 *)(value));
    value = MTLK_STATS_DATA_OFFSET(baaCounters.vapCounts[i].multipleRetryAmsdu , chip_id);
    *(uint32 *)(value) = MAC_TO_HOST32(*(uint32 *)(value));
    value = MTLK_STATS_DATA_OFFSET(baaCounters.vapCounts[i].amsduAckFailure , chip_id);
    *(uint32 *)(value) = MAC_TO_HOST32(*(uint32 *)(value));
    value = MTLK_STATS_DATA_OFFSET(baaCounters.vapCounts[i].implicitBarFailure , chip_id);
    *(uint32 *)(value) = MAC_TO_HOST32(*(uint32 *)(value));
    value = MTLK_STATS_DATA_OFFSET(baaCounters.vapCounts[i].explicitBarFailure , chip_id);
    *(uint32 *)(value) = MAC_TO_HOST32(*(uint32 *)(value));
    value = MTLK_STATS_DATA_OFFSET(baaCounters.vapCounts[i].transmitStreamRprtMultipleRetryCount , chip_id);
    *(uint32 *)(value) = MAC_TO_HOST32(*(uint32 *)(value));
    value = MTLK_STATS_DATA_OFFSET(baaCounters.vapCounts[i].transmitBw20 , chip_id);
    *(uint32 *)(value) = MAC_TO_HOST32(*(uint32 *)(value));
    value = MTLK_STATS_DATA_OFFSET(baaCounters.vapCounts[i].transmitBw40 , chip_id);
    *(uint32 *)(value) = MAC_TO_HOST32(*(uint32 *)(value));
    value = MTLK_STATS_DATA_OFFSET(baaCounters.vapCounts[i].transmitBw80 , chip_id);
    *(uint32 *)(value) = MAC_TO_HOST32(*(uint32 *)(value));
    value = MTLK_STATS_DATA_OFFSET(baaCounters.vapCounts[i].transmitBw160 , chip_id);
    *(uint32 *)(value) = MAC_TO_HOST32(*(uint32 *)(value));
  }

  for (i = 0; i < HW_NUM_OF_STATIONS; i++) {
    value = MTLK_STATS_DATA_OFFSET(hostIfCounters.staCounts[i].rxOutStaNumOfBytes , chip_id);
    *(uint32 *)(value) = MAC_TO_HOST32(*(uint32 *)(value));
    value = MTLK_STATS_DATA_OFFSET(rxCounters.staCounts[i].rddelayed , chip_id);
    *(uint32 *)(value) = MAC_TO_HOST32(*(uint32 *)(value));
    value = MTLK_STATS_DATA_OFFSET(rxCounters.staCounts[i].swUpdateDrop , chip_id);
    *(uint32 *)(value) = MAC_TO_HOST32(*(uint32 *)(value));
    value = MTLK_STATS_DATA_OFFSET(rxCounters.staCounts[i].rdDuplicateDrop , chip_id);
    *(uint32 *)(value) = MAC_TO_HOST32(*(uint32 *)(value));
    value = MTLK_STATS_DATA_OFFSET(rxCounters.staCounts[i].missingSn , chip_id);
    *(uint32 *)(value) = MAC_TO_HOST32(*(uint32 *)(value));
    value = MTLK_STATS_DATA_OFFSET(baaCounters.staCounts[i].mpduFirstRetransmission , chip_id);
    *(uint32 *)(value) = MAC_TO_HOST32(*(uint32 *)(value));
    value = MTLK_STATS_DATA_OFFSET(baaCounters.staCounts[i].mpduTransmitted , chip_id);
    *(uint32 *)(value) = MAC_TO_HOST32(*(uint32 *)(value));
    value = MTLK_STATS_DATA_OFFSET(baaCounters.staCounts[i].mpduByteTransmitted , chip_id);
    *(uint32 *)(value) = MAC_TO_HOST32(*(uint32 *)(value));

    if (!_chipid_is_gen6_a0(chip_id)) { /* b0 etc */
      value = ( hw_stats->stats_data + (int)MTLK_OFFSET_OF(StatisticsDb_t_wave600b, hostIfCounters.staCounts[i].agerPdNoTransmitCountSta));
      *(uint32 *)(value) = MAC_TO_HOST32(*(uint32 *)(value));
      value = ( hw_stats->stats_data + (int)MTLK_OFFSET_OF(StatisticsDb_t_wave600b, baaCounters.staCounts[i].mpduRetransmission));
      *(uint32 *)(value) = MAC_TO_HOST32(*(uint32 *)(value));
      value = ( hw_stats->stats_data + (int)MTLK_OFFSET_OF(StatisticsDb_t_wave600b, baaCounters.staCounts[i].channelTransmitTime));
      *(uint32 *)(value) = MAC_TO_HOST32(*(uint32 *)(value));
    }
  }

  for (i = 0; i < GEN6_NUM_OF_BANDS; i++) {
    value = MTLK_STATS_DATA_OFFSET(rxCounters.bandCounts[i].barMpduCount , chip_id);
    *(uint32 *)(value) = MAC_TO_HOST32(*(uint32 *)(value));
    value = MTLK_STATS_DATA_OFFSET(rxCounters.bandCounts[i].crcErrorCount , chip_id);
    *(uint32 *)(value) = MAC_TO_HOST32(*(uint32 *)(value));
    value = MTLK_STATS_DATA_OFFSET(rxCounters.bandCounts[i].delCrcError , chip_id);
    *(uint32 *)(value) = MAC_TO_HOST32(*(uint32 *)(value));
    value = MTLK_STATS_DATA_OFFSET(baaCounters.retryCounts[i].retryCount , chip_id);
    *(uint32 *)(value) = MAC_TO_HOST32(*(uint32 *)(value));
    value = MTLK_STATS_DATA_OFFSET(baaCounters.retryCounts[i].multipleRetryCount , chip_id);
    *(uint32 *)(value) = MAC_TO_HOST32(*(uint32 *)(value));

  }

  CONVERT_ENDIANESS_UINT32(MTLK_STATS_DATA_OFFSET(rxCounters.rdCount, chip_id), HW_NUM_OF_STATIONS);
  CONVERT_ENDIANESS_UINT16(MTLK_STATS_DATA_OFFSET(rxCounters.qosRxSta, chip_id), HW_NUM_OF_STATIONS);

  CONVERT_ENDIANESS_UINT32(MTLK_STATS_DATA_OFFSET(baaCounters.fwMngmntframesSent, chip_id), GEN6_NUM_OF_BANDS);
  CONVERT_ENDIANESS_UINT32(MTLK_STATS_DATA_OFFSET(baaCounters.fwMngmntFramesConfirmed, chip_id), GEN6_NUM_OF_BANDS);
  CONVERT_ENDIANESS_UINT32(MTLK_STATS_DATA_OFFSET(baaCounters.fwMngmntframesRecieved, chip_id), GEN6_NUM_OF_BANDS);
  CONVERT_ENDIANESS_UINT32(MTLK_STATS_DATA_OFFSET(baaCounters.fwctrlFramesSent, chip_id), GEN6_NUM_OF_BANDS);
  CONVERT_ENDIANESS_UINT32(MTLK_STATS_DATA_OFFSET(baaCounters.fwctrlFramesRecieved, chip_id), GEN6_NUM_OF_BANDS);
  CONVERT_ENDIANESS_UINT32(MTLK_STATS_DATA_OFFSET(baaCounters.fwMulticastReplayedPackets, chip_id), GEN6_NUM_OF_BANDS);
  CONVERT_ENDIANESS_UINT32(MTLK_STATS_DATA_OFFSET(baaCounters.fwPairWiseMicFailurePackets, chip_id), GEN6_NUM_OF_BANDS);
  CONVERT_ENDIANESS_UINT32(MTLK_STATS_DATA_OFFSET(baaCounters.groupMicFailurePackets, chip_id), GEN6_NUM_OF_BANDS);
  CONVERT_ENDIANESS_UINT32(MTLK_STATS_DATA_OFFSET(baaCounters.beaconProbeResponsePhyType, chip_id), GEN6_NUM_OF_BANDS);
  CONVERT_ENDIANESS_UINT32(MTLK_STATS_DATA_OFFSET(baaCounters.rxBasicReport, chip_id), GEN6_NUM_OF_BANDS);
  CONVERT_ENDIANESS_UINT32(MTLK_STATS_DATA_OFFSET(baaCounters.txFrameErrorCount, chip_id), GEN6_NUM_OF_BANDS);
  CONVERT_ENDIANESS_UINT32(MTLK_STATS_DATA_OFFSET(baaCounters.rxGroupFrame, chip_id), HW_NUM_OF_VAPS);
  CONVERT_ENDIANESS_UINT32(MTLK_STATS_DATA_OFFSET(baaCounters.txSenderError, chip_id), HW_NUM_OF_VAPS);

  CONVERT_ENDIANESS_UINT32(MTLK_STATS_DATA_OFFSET(tsManagerInitiatorTidGlobalStats.InitiatorTidStatisticsEventAddbaRequestWasSent, chip_id), GEN6_NUM_OF_BANDS);
  CONVERT_ENDIANESS_UINT32(MTLK_STATS_DATA_OFFSET(tsManagerInitiatorTidGlobalStats.InitiatorTidStatisticsEventAddbaRequestConfiremd, chip_id), GEN6_NUM_OF_BANDS);
  CONVERT_ENDIANESS_UINT32(MTLK_STATS_DATA_OFFSET(tsManagerInitiatorTidGlobalStats.InitiatorTidStatisticsEventAddbaResponseReceivedWithStatusSuccessAndValid, chip_id), GEN6_NUM_OF_BANDS);
  CONVERT_ENDIANESS_UINT32(MTLK_STATS_DATA_OFFSET(tsManagerInitiatorTidGlobalStats.InitiatorTidStatisticsEventAddbaResponseReceivedWithStatusSuccessAndInvalid, chip_id), GEN6_NUM_OF_BANDS);
  CONVERT_ENDIANESS_UINT32(MTLK_STATS_DATA_OFFSET(tsManagerInitiatorTidGlobalStats.InitiatorTidStatisticsEventAddbaResponseReceivedWithStatusFailure, chip_id), GEN6_NUM_OF_BANDS);
  CONVERT_ENDIANESS_UINT32(MTLK_STATS_DATA_OFFSET(tsManagerInitiatorTidGlobalStats.InitiatorTidStatisticsEventAddbaResponseTimeout, chip_id), GEN6_NUM_OF_BANDS);
  CONVERT_ENDIANESS_UINT32(MTLK_STATS_DATA_OFFSET(tsManagerInitiatorTidGlobalStats.InitiatorTidStatisticsEventBaAgreementOpened, chip_id), GEN6_NUM_OF_BANDS);
  CONVERT_ENDIANESS_UINT32(MTLK_STATS_DATA_OFFSET(tsManagerInitiatorTidGlobalStats.InitiatorTidStatisticsEventBarWasSent, chip_id), GEN6_NUM_OF_BANDS);
  CONVERT_ENDIANESS_UINT32(MTLK_STATS_DATA_OFFSET(tsManagerInitiatorTidGlobalStats.InitiatorTidStatisticsEventBarWasDiscarded, chip_id), GEN6_NUM_OF_BANDS);
  CONVERT_ENDIANESS_UINT32(MTLK_STATS_DATA_OFFSET(tsManagerInitiatorTidGlobalStats.InitiatorTidStatisticsEventBarWasAnsweredWithAck, chip_id), GEN6_NUM_OF_BANDS);
  CONVERT_ENDIANESS_UINT32(MTLK_STATS_DATA_OFFSET(tsManagerInitiatorTidGlobalStats.InitiatorTidStatisticsEventBarWasAnsweredWithBa, chip_id), GEN6_NUM_OF_BANDS);
  CONVERT_ENDIANESS_UINT32(MTLK_STATS_DATA_OFFSET(tsManagerInitiatorTidGlobalStats.InitiatorTidStatisticsEventInactivityTimeout, chip_id), GEN6_NUM_OF_BANDS);
  CONVERT_ENDIANESS_UINT32(MTLK_STATS_DATA_OFFSET(tsManagerInitiatorTidGlobalStats.InitiatorTidStatisticsEventDelbaWasSent, chip_id), GEN6_NUM_OF_BANDS);
  CONVERT_ENDIANESS_UINT32(MTLK_STATS_DATA_OFFSET(tsManagerInitiatorTidGlobalStats.InitiatorTidStatisticsEventDelbaReceived, chip_id), GEN6_NUM_OF_BANDS);
  CONVERT_ENDIANESS_UINT32(MTLK_STATS_DATA_OFFSET(tsManagerInitiatorTidGlobalStats.InitiatorTidStatisticsEventCloseTid, chip_id), GEN6_NUM_OF_BANDS);
  CONVERT_ENDIANESS_UINT32(MTLK_STATS_DATA_OFFSET(tsManagerInitiatorTidGlobalStats.InitiatorTidStatisticsEventOpenTid, chip_id), GEN6_NUM_OF_BANDS);

  CONVERT_ENDIANESS_UINT16(MTLK_STATS_DATA_OFFSET(tsManagerInitiatorStationGlobalStats.InitiatorStationStatisticsEventRemove, chip_id), GEN6_NUM_OF_BANDS);
  CONVERT_ENDIANESS_UINT16(MTLK_STATS_DATA_OFFSET(tsManagerInitiatorStationGlobalStats.InitiatorStationStatisticsEventAdd, chip_id), GEN6_NUM_OF_BANDS);
  CONVERT_ENDIANESS_UINT16(MTLK_STATS_DATA_OFFSET(tsManagerInitiatorStationGlobalStats.InitiatorStationStatisticsEventAddbaResponseReceivedWithIllegalTid, chip_id), GEN6_NUM_OF_BANDS);
  CONVERT_ENDIANESS_UINT16(MTLK_STATS_DATA_OFFSET(tsManagerInitiatorStationGlobalStats.Reserve, chip_id), GEN6_NUM_OF_BANDS);

  CONVERT_ENDIANESS_UINT32(MTLK_STATS_DATA_OFFSET(tsManagerRecipientTidGlobalStats.RecipientTidStatisticsEventAddbaRequestAccepted, chip_id), GEN6_NUM_OF_BANDS);
  CONVERT_ENDIANESS_UINT32(MTLK_STATS_DATA_OFFSET(tsManagerRecipientTidGlobalStats.RecipientTidStatisticsEventAddbaRequestWasReceivedWithInvalidParameters, chip_id), GEN6_NUM_OF_BANDS);
  CONVERT_ENDIANESS_UINT32(MTLK_STATS_DATA_OFFSET(tsManagerRecipientTidGlobalStats.RecipientTidStatisticsEventAddbaResponseWasSentWithStatusSuccess, chip_id), GEN6_NUM_OF_BANDS);
  CONVERT_ENDIANESS_UINT32(MTLK_STATS_DATA_OFFSET(tsManagerRecipientTidGlobalStats.RecipientTidStatisticsEventAddbaResponseWasSentWithStatusFailure, chip_id), GEN6_NUM_OF_BANDS);
  CONVERT_ENDIANESS_UINT32(MTLK_STATS_DATA_OFFSET(tsManagerRecipientTidGlobalStats.RecipientTidStatisticsEventBaAgreementOpened, chip_id), GEN6_NUM_OF_BANDS);
  CONVERT_ENDIANESS_UINT32(MTLK_STATS_DATA_OFFSET(tsManagerRecipientTidGlobalStats.RecipientTidStatisticsEventBaAgreementClosed, chip_id), GEN6_NUM_OF_BANDS);
  CONVERT_ENDIANESS_UINT32(MTLK_STATS_DATA_OFFSET(tsManagerRecipientTidGlobalStats.RecipientTidStatisticsEventBarWasReceivedWithBaAgreementOpened, chip_id), GEN6_NUM_OF_BANDS);
  CONVERT_ENDIANESS_UINT32(MTLK_STATS_DATA_OFFSET(tsManagerRecipientTidGlobalStats.RecipientTidStatisticsEventInactivityTimeout, chip_id), GEN6_NUM_OF_BANDS);
  CONVERT_ENDIANESS_UINT32(MTLK_STATS_DATA_OFFSET(tsManagerRecipientTidGlobalStats.RecipientTidStatisticsEventDelbaReceived, chip_id), GEN6_NUM_OF_BANDS);
  CONVERT_ENDIANESS_UINT32(MTLK_STATS_DATA_OFFSET(tsManagerRecipientTidGlobalStats.RecipientTidStatisticsEventDelbaWasSent, chip_id), GEN6_NUM_OF_BANDS);
  CONVERT_ENDIANESS_UINT32(MTLK_STATS_DATA_OFFSET(tsManagerRecipientTidGlobalStats.RecipientTidStatisticsEventIllegalAggregationWasReceived, chip_id), GEN6_NUM_OF_BANDS);
  CONVERT_ENDIANESS_UINT32(MTLK_STATS_DATA_OFFSET(tsManagerRecipientTidGlobalStats.RecipientTidStatisticsEventCloseTid, chip_id), GEN6_NUM_OF_BANDS);

  CONVERT_ENDIANESS_UINT16(MTLK_STATS_DATA_OFFSET(tsManagerRecipientStationGlobalStats.RecipientStationStatisticsEventRemove, chip_id), GEN6_NUM_OF_BANDS);
  CONVERT_ENDIANESS_UINT16(MTLK_STATS_DATA_OFFSET(tsManagerRecipientStationGlobalStats.RecipientStationStatisticsEventAdd, chip_id), GEN6_NUM_OF_BANDS);
  CONVERT_ENDIANESS_UINT16(MTLK_STATS_DATA_OFFSET(tsManagerRecipientStationGlobalStats.RecipientStationStatisticsEventAddbaRequestReceivedWithIllegalTid, chip_id), GEN6_NUM_OF_BANDS);
  CONVERT_ENDIANESS_UINT16(MTLK_STATS_DATA_OFFSET(tsManagerRecipientStationGlobalStats.Reserve, chip_id), GEN6_NUM_OF_BANDS);

  CONVERT_ENDIANESS_2D_UINT32(MTLK_STATS_DATA_OFFSET(linkAdaptationStats.ratesMask[0][0], chip_id), HW_NUM_OF_STATIONS, RATES_BIT_MASK_SIZE);
  CONVERT_ENDIANESS_UINT32(MTLK_STATS_DATA_OFFSET(linkAdaptationStats.bfReportReceivedCounter, chip_id), GEN6_NUM_OF_BANDS);
  CONVERT_ENDIANESS_UINT32(MTLK_STATS_DATA_OFFSET(linkAdaptationStats.protectionSentCounter, chip_id), GEN6_NUM_OF_BANDS);
  CONVERT_ENDIANESS_UINT32(MTLK_STATS_DATA_OFFSET(linkAdaptationStats.protectionSucceededCounter, chip_id), GEN6_NUM_OF_BANDS);
  CONVERT_ENDIANESS_UINT32(MTLK_STATS_DATA_OFFSET(linkAdaptationStats.totalTxTime, chip_id), HW_NUM_OF_STATIONS);
  CONVERT_ENDIANESS_UINT32(MTLK_STATS_DATA_OFFSET(linkAdaptationStats.rxDc, chip_id), MAX(NUM_OF_BW, PHY_STATISTICS_MAX_RX_ANT));
  CONVERT_ENDIANESS_UINT32(MTLK_STATS_DATA_OFFSET(linkAdaptationStats.txLo, chip_id), MAX(NUM_OF_BW, PHY_STATISTICS_MAX_RX_ANT));
  CONVERT_ENDIANESS_UINT32(MTLK_STATS_DATA_OFFSET(linkAdaptationStats.rxIq, chip_id), MAX(NUM_OF_BW, PHY_STATISTICS_MAX_RX_ANT));
  CONVERT_ENDIANESS_UINT32(MTLK_STATS_DATA_OFFSET(linkAdaptationStats.txIq, chip_id), MAX(NUM_OF_BW, PHY_STATISTICS_MAX_RX_ANT));

  value = MTLK_STATS_DATA_OFFSET(multicastStats.notEnoughClonePds , chip_id);
    *(uint32 *)(value) = MAC_TO_HOST32(*(uint32 *)(value));
  value = MTLK_STATS_DATA_OFFSET( multicastStats.allClonesFinishedTransmission, chip_id);
    *(uint32 *)(value) = MAC_TO_HOST32(*(uint32 *)(value));
  value = MTLK_STATS_DATA_OFFSET( multicastStats.noStationsInGroup, chip_id);
    *(uint32 *)(value) = MAC_TO_HOST32(*(uint32 *)(value));

  value = MTLK_STATS_DATA_OFFSET( trainingManagerStats.trainingTimerExpiredCounter, chip_id);
    *(uint32 *)(value) = MAC_TO_HOST32(*(uint32 *)(value));
  value = MTLK_STATS_DATA_OFFSET( trainingManagerStats.trainingStartedCounter, chip_id);
    *(uint32 *)(value) = MAC_TO_HOST32(*(uint32 *)(value));
  value = MTLK_STATS_DATA_OFFSET( trainingManagerStats.trainingFinishedSuccessfullyCounter, chip_id);
    *(uint32 *)(value) = MAC_TO_HOST32(*(uint32 *)(value));
  value = MTLK_STATS_DATA_OFFSET( trainingManagerStats.trainingFinishedUnsuccessfullyCounter, chip_id);
    *(uint32 *)(value) = MAC_TO_HOST32(*(uint32 *)(value));
  value = MTLK_STATS_DATA_OFFSET( trainingManagerStats.trainingNotStartedCounter, chip_id);
    *(uint32 *)(value) = MAC_TO_HOST32(*(uint32 *)(value));

  CONVERT_ENDIANESS_2D_UINT32(MTLK_STATS_DATA_OFFSET(linkAdaptationMuStats.ratesMask[0][0], chip_id), TX_MU_GROUPS, MAX_USP_IN_VHT_GROUP);
  CONVERT_ENDIANESS_UINT32(MTLK_STATS_DATA_OFFSET(linkAdaptationMuStats.protectionSentCounter, chip_id), TX_MU_GROUPS);
  CONVERT_ENDIANESS_UINT32(MTLK_STATS_DATA_OFFSET(linkAdaptationMuStats.protectionSucceededCounter, chip_id), TX_MU_GROUPS);
  CONVERT_ENDIANESS_UINT32(MTLK_STATS_DATA_OFFSET(linkAdaptationMuStats.DataPhyMode, chip_id), TX_MU_GROUPS);
  CONVERT_ENDIANESS_2D_UINT32(MTLK_STATS_DATA_OFFSET(linkAdaptationMuStats.powerData[0][0], chip_id), TX_MU_GROUPS, NUM_OF_BW);
  CONVERT_ENDIANESS_2D_UINT32(MTLK_STATS_DATA_OFFSET(linkAdaptationMuStats.scpData[0][0], chip_id), TX_MU_GROUPS, NUM_OF_BW);
  CONVERT_ENDIANESS_UINT32(MTLK_STATS_DATA_OFFSET(linkAdaptationMuStats.dataBwLimit, chip_id), TX_MU_GROUPS);
  CONVERT_ENDIANESS_UINT32(MTLK_STATS_DATA_OFFSET(linkAdaptationMuStats.groupGoodput, chip_id), TX_MU_GROUPS);
  CONVERT_ENDIANESS_UINT32(MTLK_STATS_DATA_OFFSET(linkAdaptationMuStats.raState, chip_id), TX_MU_GROUPS);
  CONVERT_ENDIANESS_2D_UINT32(MTLK_STATS_DATA_OFFSET(linkAdaptationMuStats.raStability[0][0], chip_id), TX_MU_GROUPS, MAX_USP_IN_VHT_GROUP);
  CONVERT_ENDIANESS_2D_UINT32(MTLK_STATS_DATA_OFFSET(linkAdaptationMuStats.nssData[0][0], chip_id), TX_MU_GROUPS, MAX_USP_IN_VHT_GROUP);
  CONVERT_ENDIANESS_2D_UINT32(MTLK_STATS_DATA_OFFSET(linkAdaptationMuStats.mcsData[0][0], chip_id), TX_MU_GROUPS, MAX_USP_IN_VHT_GROUP);

  CONVERT_ENDIANESS_3D_UINT32(MTLK_STATS_DATA_OFFSET(ptaStats.illegalEvent[0][0][0], chip_id), PTA_EVENT_NUM_OF_EVENTS, PTA_STATE_NUM_OF_STATES, PTA_RADIO_NUM_OF_TYPES);
  CONVERT_ENDIANESS_3D_UINT32(MTLK_STATS_DATA_OFFSET(ptaStats.ignoreEvent[0][0][0], chip_id), PTA_EVENT_NUM_OF_EVENTS, PTA_STATE_NUM_OF_STATES, PTA_RADIO_NUM_OF_TYPES);
  value = MTLK_STATS_DATA_OFFSET( ptaStats.zigbeeStarvationTimerExpired, chip_id);
    *(uint32 *)(value) = MAC_TO_HOST32(*(uint32 *)(value));
  value = MTLK_STATS_DATA_OFFSET( ptaStats.btStarvationTimerExpired, chip_id);
    *(uint32 *)(value) = MAC_TO_HOST32(*(uint32 *)(value));
  value = MTLK_STATS_DATA_OFFSET( ptaStats.zigbeeDenyOverTime, chip_id);
    *(uint32 *)(value) = MAC_TO_HOST32(*(uint32 *)(value));
  value = MTLK_STATS_DATA_OFFSET( ptaStats.btDenyOverTime, chip_id);
    *(uint32 *)(value) = MAC_TO_HOST32(*(uint32 *)(value));
  value = MTLK_STATS_DATA_OFFSET( ptaStats.gpio0TxDemandLow, chip_id);
    *(uint32 *)(value) = MAC_TO_HOST32(*(uint32 *)(value));
  value = MTLK_STATS_DATA_OFFSET( ptaStats.zigbeeGrnatNotUsed, chip_id);
    *(uint32 *)(value) = MAC_TO_HOST32(*(uint32 *)(value));
  value = MTLK_STATS_DATA_OFFSET( ptaStats.btGrnatNotUsed, chip_id);
    *(uint32 *)(value) = MAC_TO_HOST32(*(uint32 *)(value));
  value = MTLK_STATS_DATA_OFFSET( ptaStats.zigbeeGrnatUsed, chip_id);
    *(uint32 *)(value) = MAC_TO_HOST32(*(uint32 *)(value));
  value = MTLK_STATS_DATA_OFFSET( ptaStats.btGrnatUsed, chip_id);
    *(uint32 *)(value) = MAC_TO_HOST32(*(uint32 *)(value));
  value = MTLK_STATS_DATA_OFFSET( ptaStats.zigbeeTxOngoing, chip_id);
    *(uint32 *)(value) = MAC_TO_HOST32(*(uint32 *)(value));
  value = MTLK_STATS_DATA_OFFSET( ptaStats.btTxOngoing, chip_id);
    *(uint32 *)(value) = MAC_TO_HOST32(*(uint32 *)(value));
  value = MTLK_STATS_DATA_OFFSET( ptaStats.gpio, chip_id);
    *(uint32 *)(value) = MAC_TO_HOST32(*(uint32 *)(value));

  CONVERT_ENDIANESS_UINT32(MTLK_STATS_DATA_OFFSET(twtStats.numOfAgreementsForSta, chip_id), HW_NUM_OF_STATIONS);
  CONVERT_ENDIANESS_UINT32(MTLK_STATS_DATA_OFFSET(twtStats.numOfStaInSp, chip_id), MAX_NUM_OF_SP);

  CONVERT_ENDIANESS_UINT32(MTLK_STATS_DATA_OFFSET(perClientStats.successCount, chip_id), HW_NUM_OF_STATIONS);
  CONVERT_ENDIANESS_UINT32(MTLK_STATS_DATA_OFFSET(perClientStats.exhaustedCount, chip_id), HW_NUM_OF_STATIONS);
  CONVERT_ENDIANESS_UINT32(MTLK_STATS_DATA_OFFSET(perClientStats.clonedCount, chip_id), HW_NUM_OF_STATIONS);

  CONVERT_ENDIANESS_UINT32(MTLK_STATS_DATA_OFFSET(generalStats.calibrationMask, chip_id), GEN6_NUM_OF_BANDS);
  CONVERT_ENDIANESS_UINT32(MTLK_STATS_DATA_OFFSET(generalStats.onlineCalibrationMask, chip_id), GEN6_NUM_OF_BANDS);
  CONVERT_ENDIANESS_UINT32(MTLK_STATS_DATA_OFFSET(generalStats.dropReasonClassifier, chip_id), GEN6_NUM_OF_BANDS);
  CONVERT_ENDIANESS_UINT32(MTLK_STATS_DATA_OFFSET(generalStats.dropReasonDisconnect, chip_id), GEN6_NUM_OF_BANDS);
  CONVERT_ENDIANESS_UINT32(MTLK_STATS_DATA_OFFSET(generalStats.dropReasonATF, chip_id), GEN6_NUM_OF_BANDS);
  CONVERT_ENDIANESS_UINT32(MTLK_STATS_DATA_OFFSET(generalStats.dropReasonTSFlush, chip_id), GEN6_NUM_OF_BANDS);
  CONVERT_ENDIANESS_UINT32(MTLK_STATS_DATA_OFFSET(generalStats.dropReasonReKey, chip_id), GEN6_NUM_OF_BANDS);
  CONVERT_ENDIANESS_UINT32(MTLK_STATS_DATA_OFFSET(generalStats.dropReasonSetKey, chip_id), GEN6_NUM_OF_BANDS);
  CONVERT_ENDIANESS_UINT32(MTLK_STATS_DATA_OFFSET(generalStats.dropReasonDiscard, chip_id), GEN6_NUM_OF_BANDS);
  CONVERT_ENDIANESS_UINT32(MTLK_STATS_DATA_OFFSET(generalStats.dropReasonDsabled, chip_id), GEN6_NUM_OF_BANDS);
  CONVERT_ENDIANESS_UINT32(MTLK_STATS_DATA_OFFSET(generalStats.dropReasonAggError, chip_id), GEN6_NUM_OF_BANDS);
  CONVERT_ENDIANESS_UINT32(MTLK_STATS_DATA_OFFSET(generalStats.defragStart, chip_id), GEN6_NUM_OF_BANDS);
  CONVERT_ENDIANESS_UINT32(MTLK_STATS_DATA_OFFSET(generalStats.defragEnd, chip_id), GEN6_NUM_OF_BANDS);
  CONVERT_ENDIANESS_UINT32(MTLK_STATS_DATA_OFFSET(generalStats.defragTimeout, chip_id), GEN6_NUM_OF_BANDS);
  CONVERT_ENDIANESS_UINT32(MTLK_STATS_DATA_OFFSET(generalStats.classViolationErrors, chip_id), GEN6_NUM_OF_BANDS);
  CONVERT_ENDIANESS_UINT32(MTLK_STATS_DATA_OFFSET(generalStats.rxhErrors, chip_id), GEN6_NUM_OF_BANDS);
  CONVERT_ENDIANESS_UINT32(MTLK_STATS_DATA_OFFSET(generalStats.rxSmps, chip_id), GEN6_NUM_OF_BANDS);
  CONVERT_ENDIANESS_UINT32(MTLK_STATS_DATA_OFFSET(generalStats.rxOmn, chip_id), GEN6_NUM_OF_BANDS);
  CONVERT_ENDIANESS_UINT32(MTLK_STATS_DATA_OFFSET(generalStats.txMngFromHost, chip_id), GEN6_NUM_OF_BANDS);
  CONVERT_ENDIANESS_UINT32(MTLK_STATS_DATA_OFFSET(generalStats.txDataFromHost, chip_id), GEN6_NUM_OF_BANDS);
  CONVERT_ENDIANESS_UINT32(MTLK_STATS_DATA_OFFSET(generalStats.cyclicBufferOverflow, chip_id), GEN6_NUM_OF_BANDS);
  CONVERT_ENDIANESS_UINT32(MTLK_STATS_DATA_OFFSET(generalStats.beaconsTransmitted, chip_id), GEN6_NUM_OF_BANDS);
  CONVERT_ENDIANESS_UINT32(MTLK_STATS_DATA_OFFSET(generalStats.debugStatistics, chip_id), GEN6_NUM_OF_BANDS);

  CONVERT_ENDIANESS_UINT32(MTLK_STATS_DATA_OFFSET(dynamicBwStats.dynamicBW20MHz, chip_id), GEN6_NUM_OF_BANDS);
  CONVERT_ENDIANESS_UINT32(MTLK_STATS_DATA_OFFSET(dynamicBwStats.dynamicBW40MHz, chip_id), GEN6_NUM_OF_BANDS);
  CONVERT_ENDIANESS_UINT32(MTLK_STATS_DATA_OFFSET(dynamicBwStats.dynamicBW80MHz, chip_id), GEN6_NUM_OF_BANDS);
  CONVERT_ENDIANESS_UINT32(MTLK_STATS_DATA_OFFSET(dynamicBwStats.dynamicBW160MHz, chip_id), GEN6_NUM_OF_BANDS);

  for (i = 0; i < GEN6_NUM_OF_BANDS; i++) {
    value = MTLK_STATS_DATA_OFFSET( phyStatistics.devicePhyRxStatus[i].irad, chip_id);
    *(uint32 *)(value) = MAC_TO_HOST32(*(uint32 *)(value));
    value = MTLK_STATS_DATA_OFFSET( phyStatistics.devicePhyRxStatus[i].tsf, chip_id);
    *(uint32 *)(value) = MAC_TO_HOST32(*(uint32 *)(value));
    value = MTLK_STATS_DATA_OFFSET( phyStatistics.devicePhyRxStatus[i].extStaRx, chip_id);
    *(uint32 *)(value) = MAC_TO_HOST32(*(uint32 *)(value));
    value = MTLK_STATS_DATA_OFFSET( phyStatistics.devicePhyRxStatus[i].txPower, chip_id);
    *(uint32 *)(value) = MAC_TO_HOST32(*(uint32 *)(value));

  }

  for (i = 0; i < HW_NUM_OF_STATIONS; i++) {
    value = MTLK_STATS_DATA_OFFSET( phyStatistics.staPhyRxStatus[i].phyRate, chip_id);
    *(uint32 *)(value) = MAC_TO_HOST32(*(uint32 *)(value));
    value = MTLK_STATS_DATA_OFFSET( phyStatistics.staPhyRxStatus[i].irad, chip_id);
    *(uint32 *)(value) = MAC_TO_HOST32(*(uint32 *)(value));
    value = MTLK_STATS_DATA_OFFSET( phyStatistics.staPhyRxStatus[i].lastTsf, chip_id);
    *(uint32 *)(value) = MAC_TO_HOST32(*(uint32 *)(value));
    value = MTLK_STATS_DATA_OFFSET( phyStatistics.staPhyRxStatus[i].perClientRxtimeUsage, chip_id);
    *(uint32 *)(value) = MAC_TO_HOST32(*(uint32 *)(value));
  }

  for (i = 0; i < GEN6_NUM_OF_BANDS; i++) {
    value = MTLK_STATS_DATA_OFFSET( currentChannelStats.channelUtilizationStats[i].chUtilizationTotal, chip_id);
    *(uint32 *)(value) = MAC_TO_HOST32(*(uint32 *)(value));
    value = MTLK_STATS_DATA_OFFSET( currentChannelStats.channelUtilizationStats[i].chUtilizationBusy, chip_id);
    *(uint32 *)(value) = MAC_TO_HOST32(*(uint32 *)(value));
    value = MTLK_STATS_DATA_OFFSET( currentChannelStats.channelUtilizationStats[i].chUtilizationBusyTx, chip_id);
    *(uint32 *)(value) = MAC_TO_HOST32(*(uint32 *)(value));
    value = MTLK_STATS_DATA_OFFSET( currentChannelStats.channelUtilizationStats[i].chUtilizationBusyRx, chip_id);
    *(uint32 *)(value) = MAC_TO_HOST32(*(uint32 *)(value));
    value = MTLK_STATS_DATA_OFFSET( currentChannelStats.channelUtilizationStats[i].chUtilizationBusySelf, chip_id);
    *(uint32 *)(value) = MAC_TO_HOST32(*(uint32 *)(value));
    value = MTLK_STATS_DATA_OFFSET( currentChannelStats.channelUtilizationStats[i].chUtilizationBusyExt, chip_id);
    *(uint32 *)(value) = MAC_TO_HOST32(*(uint32 *)(value));
  }
}

static int
_mtlk_hw_parse_statistics (mtlk_hw_t *hw)
{
  int                  res = MTLK_ERR_OK, size = 0, phy_stats_size = 0;
  hw_statistics_t     *hw_stats;
  hw_phy_rx_status_t  *phy_rx_status;
  int                  chip_id = __hw_mmb_get_chip_id(hw);

  hw_stats = &hw->hw_stats;
  phy_rx_status = &hw->phy_rx_status;

  if (_chipid_is_gen6_a0 (chip_id)) {
      size = sizeof(StatisticsDb_t_wave600);
      phy_stats_size = sizeof(wholePhyRxStatusDb_t_wave600);
  } else /* if (_chipid_is_gen6_b0 (chip_id)) */ {
      size = sizeof(StatisticsDb_t_wave600b);
      phy_stats_size = sizeof(wholePhyRxStatusDb_t_wave600b);
  }

  ILOG3_V("Dumping the statistics");
  mtlk_dump(3, hw_stats->stats_data, MIN(32,  size), "dump of Statistics");

  /* Convert endianess */
  mtlk_osal_lock_acquire(&hw_stats->lock);
  _mtlk_hw_check_and_convert_endianess(hw);
  mtlk_osal_lock_release(&hw_stats->lock);

  /* copy stats_data to stats_copy */
  mtlk_osal_lock_acquire(&hw_stats->lock);
  wave_memcpy(hw_stats->stats_copy, size, hw_stats->stats_data, size);
  mtlk_osal_lock_release(&hw_stats->lock);


  /* Parse phyStatistics to phy_rx_status */
  mtlk_osal_lock_acquire(&hw_stats->lock);
  wave_memcpy(phy_rx_status->db_data, phy_stats_size, MTLK_STATS_DATA_OFFSET(phyStatistics, chip_id), phy_stats_size);
  mtlk_osal_lock_release(&hw_stats->lock);
  return res;
}

int
_mtlk_hw_get_statistics (mtlk_hw_t *hw)
{
  mtlk_txmm_msg_t          man_msg;
  mtlk_txmm_data_t        *man_entry;
  UMI_GET_STATISTICS_DATA *req;
  hw_statistics_t         *hw_stats;
  uint32                   dma_addr;
  int                      res = MTLK_ERR_UNKNOWN, size = 0;
  int                      chip_id = __hw_mmb_get_chip_id(hw);

  if (_chipid_is_gen6_a0 (chip_id)) {
      size = sizeof(StatisticsDb_t_wave600);
  } else /* if (_chipid_is_gen6_b0 (chip_id)) */ {
      size = sizeof(StatisticsDb_t_wave600b);
  }
  /* prepare msg for the FW */
  if (!(man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, &hw->txmm, NULL))) {
    ELOG_D("card_idx %d: GET_STATISTICS_REQ init failed", hw->card_idx);
    return MTLK_ERR_NO_RESOURCES;
  }

  hw_stats = &hw->hw_stats;

  dma_addr = mtlk_osal_map_to_phys_addr(mtlk_ccr_get_dev_ctx(hw->ccr),
                                        hw_stats->stats_data, size,
                                        MTLK_DATA_FROM_DEVICE);

  man_entry->id = UM_MAN_GET_STATISTICS_REQ;
  man_entry->payload_size = sizeof(*req);
  req = (UMI_GET_STATISTICS_DATA *) man_entry->payload;
  req->ddrBufferAddress = HOST_TO_MAC32(dma_addr);
  req->length        = HOST_TO_MAC32(size);
  req->status         = UMI_OK;

  ILOG2_DDD("CID-%02x: Statistics data: size %d, dma_addr 0x%08x, ",
                hw->card_idx, size, dma_addr);
  res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);

  mtlk_osal_unmap_phys_addr(mtlk_ccr_get_dev_ctx(hw->ccr),
                            dma_addr, size,
                            MTLK_DATA_FROM_DEVICE);

  mtlk_txmm_msg_cleanup(&man_msg);

  if (res != MTLK_ERR_OK) {
    ELOG_DD("card_idx %d: Sending failed, err=%i", hw->card_idx, res);
  } else if (UMI_OK != req->status) {
    ELOG_D("Failed to retrieve Statistics, error %d", req->status);
    res = MTLK_ERR_MAC;
  }

  /* without lock because it is called once during init */
  if (res == MTLK_ERR_OK) {
    ILOG2_DDPD("CID-%02x: MHI statistics table: size %d, buffer 0x%p, phys_addr 0x%08X",
               hw->card_idx, size,
               hw_stats->stats_data, virt_to_phys(hw_stats->stats_data));

    res = _mtlk_hw_parse_statistics(hw);

    /* Update HW related statistics */
    _mtlk_hw_mhi_update_hw_stats(hw, hw_stats);
  }
  return res;
}

static int
_mtlk_hw_statistics_init (mtlk_hw_t *hw)
{
    hw_statistics_t    *hw_stats;
    uint32              dma_addr;
    int                 res, size = 0;
    int                 chip_id = __hw_mmb_get_chip_id(hw);

    if (_chipid_is_gen6_a0 (chip_id)) {
        size = sizeof(StatisticsDb_t_wave600);
    } else /* if (_chipid_is_gen6_b0 (chip_id)) */ {
        size = sizeof(StatisticsDb_t_wave600b);
    }

    hw_stats = &hw->hw_stats;
    memset(hw_stats, 0, sizeof(*hw_stats));
    memset(hw->chan_statistics, 0, sizeof(hw->chan_statistics));

    ILOG2_V("_mtlk_hw_statistics_init entry");
    res = mtlk_osal_lock_init(&hw_stats->lock);
    if (MTLK_ERR_OK != res) {
        return res;
    }

    hw_stats->stats_data = mtlk_osal_mem_dma_alloc(size, MTLK_MEM_TAG_EXTENSION);
    hw_stats->stats_copy = mtlk_osal_mem_alloc(size, MTLK_MEM_TAG_EXTENSION);
    if (!hw_stats->stats_data || !hw_stats->stats_copy) {
        ELOG_V("Can't allocate statistics buffers");
        res = MTLK_ERR_NO_MEM;
        goto finish;
    }

    memset(hw_stats->stats_data, 0, size);
    memset(hw_stats->stats_copy, 0, size);

    mtlk_hw_set_stats_available(hw, FALSE);

    _hw_init_phy_rx_status(hw);

    /* Initialization test for map and unmap of statistics */
    dma_addr = mtlk_osal_map_to_phys_addr(mtlk_ccr_get_dev_ctx(hw->ccr), hw_stats->stats_data,
                                          size, MTLK_DATA_TO_DEVICE);

    ILOG0_DDPDP("CID-%02x: Statistics data: size %d, buffer 0x%p, dma_addr 0x%08x, buff_copy 0x%p",
                hw->card_idx, size, hw_stats->stats_data, dma_addr, hw_stats->stats_copy);
    if (dma_addr) {
        mtlk_osal_unmap_phys_addr(mtlk_ccr_get_dev_ctx(hw->ccr), dma_addr, size, MTLK_DATA_TO_DEVICE);
        res = MTLK_ERR_OK;
    } else {
        ELOG_V("Statistics DMA mapped addr is 0");
        res = MTLK_ERR_UNKNOWN;
        goto finish;
    }

finish:
    if (MTLK_ERR_OK != res) {
        _mtlk_hw_statistics_cleanup(hw); /* free allocated buffers */
    }

    return res;
}

#else /* MTLK_LEGACY_STATISTICS */

int  __MTLK_IFUNC    mtlk_df_ui_mhi_stats_create(mtlk_df_user_t* df_user, unsigned size);
void __MTLK_IFUNC    mtlk_df_ui_mhi_stats_delete(mtlk_df_user_t* df_user);

static void
_mtlk_hw_mhi_stats_cleanup (mtlk_hw_t *hw)
{
    hw_mhi_stats_t      *mhi_stats;
    int i;

    mhi_stats = &hw->hw_mhi_stats;

    if (0 != mhi_stats->db_size) { /* proc file "mhi_stats" was created */
        for (i = 0; i < hw->radio_descr->num_radios; i++)
            mtlk_df_ui_mhi_stats_delete(mtlk_df_get_user(mtlk_vap_manager_get_master_df(wave_radio_descr_vap_manager_get(hw->radio_descr, i))));
     }

    if (mhi_stats->db_data) mtlk_osal_mem_free(mhi_stats->db_data);
    if (mhi_stats->db_copy) mtlk_osal_mem_free(mhi_stats->db_copy);
    if (mhi_stats->table)   mtlk_osal_mem_free(mhi_stats->table);

    mtlk_osal_lock_cleanup(&mhi_stats->lock);

    memset(mhi_stats, 0, sizeof(*mhi_stats));
}

static int
_mtlk_hw_mhi_stats_init (mtlk_hw_t *hw)
{
    hw_mhi_stats_t      *mhi_stats;
    uint32               size, db_size;
    uint32               dma_addr;
    unsigned             i;
    int                  res;

    mhi_stats = &hw->hw_mhi_stats;
    memset(mhi_stats, 0, sizeof(*mhi_stats));

    res = mtlk_osal_lock_init(&mhi_stats->lock);
    if (MTLK_ERR_OK != res) {
        return res;
    }

    /* check buffer size */
    size = hw->chi_area.sStatisticsManager.sData.u32StatisticsDataSize;
    ILOG0_DD("u32StatisticsDataSize:  0x%04X (%d)", size, size);

    db_size = size;
    if (0 == size) {
        ELOG_V("Requested size of MHI statistics is zero");
        res = MTLK_ERR_NOT_SUPPORTED;
        goto finish;
    }

    /* all other sizes and offsets should be less than db_size */
    size = hw->chi_area.sStatisticsManager.sData.u32StatisticsTableSize;
    ILOG0_DD("u32StatisticsTableSize: 0x%04X (%d)", size, size);
    mhi_stats->tab_size = size;
    if (db_size < size) {
        ELOG_D("u32StatisticsTableSize (%d) too big", size);
        res = MTLK_ERR_NOT_SUPPORTED;
        goto finish;
    }

    for (i = 0; i < ARRAY_SIZE(hw->chi_area.sStatisticsManager.sData.u32TypeTableList); i++) {
        size = hw->chi_area.sStatisticsManager.sData.u32TypeTableList[i];
        ILOG1_DDD("u32TypeTableList [%2d] : 0x%04X (%d)", i, size, size);

        if (db_size < size) {
            ELOG_DD("u32TypeTableList[%2d]: (%d) too big", i, size);
            res = MTLK_ERR_NOT_SUPPORTED;
            goto finish;
        }
    }

    /* Allocate 2 buffers for data (work and copy) and 1 buffer for tables */
    mhi_stats->db_size = db_size;
    mhi_stats->db_data = mtlk_osal_mem_dma_alloc(db_size, MTLK_MEM_TAG_EXTENSION);
    mhi_stats->db_copy = mtlk_osal_mem_alloc(db_size, MTLK_MEM_TAG_EXTENSION);
    mhi_stats->table   = mtlk_osal_mem_alloc(mhi_stats->tab_size, MTLK_MEM_TAG_EXTENSION);
    if (!mhi_stats->db_data || !mhi_stats->db_copy || !mhi_stats->table) {
        ELOG_V("Can't allocate MHI statistics buffers");
        res = MTLK_ERR_NO_MEM;
        goto finish;
    }

    memset(mhi_stats->db_data, 0, db_size);
    memset(mhi_stats->db_copy, 0, db_size);
    memset(mhi_stats->table, 0, mhi_stats->tab_size);

    /* TEST */
    dma_addr = mtlk_osal_map_to_phys_addr(mtlk_ccr_get_dev_ctx(hw->ccr), mhi_stats->db_data,
                                          db_size, MTLK_DATA_TO_DEVICE);
    ILOG0_DDPDP("CID-%02x: MHI statistics data: size %d, buffer 0x%p, dma_addr 0x%08x, buff_copy 0x%p",
                hw->card_idx, db_size, mhi_stats->db_data, dma_addr, mhi_stats->db_copy);

    if (dma_addr) {
        mtlk_osal_unmap_phys_addr(mtlk_ccr_get_dev_ctx(hw->ccr), dma_addr, db_size, MTLK_DATA_TO_DEVICE);
        res = MTLK_ERR_OK;
    } else {
        ELOG_P("Failed mapping 0x%p to physical address", mhi_stats->db_data);
        res = MTLK_ERR_NO_RESOURCES;
        goto finish;
    }

    /* Get Statistics Table */
    res = _mtlk_hw_mhi_get_table(hw);
    for (i = 0; i < hw->radio_descr->num_radios; i++) {
        res = mtlk_df_ui_mhi_stats_create(mtlk_df_get_user(mtlk_vap_manager_get_master_df(wave_radio_descr_vap_manager_get(hw->radio_descr, i))), db_size);
        if (MTLK_ERR_OK != res) {
          unsigned radio_idx;

          for (radio_idx = 0; radio_idx < i; radio_idx++)
              mtlk_df_ui_mhi_stats_delete(mtlk_df_get_user(mtlk_vap_manager_get_master_df(wave_radio_descr_vap_manager_get(hw->radio_descr, radio_idx))));
          break;
        }
    }

finish:
    if (MTLK_ERR_OK != res) {
        mhi_stats->db_size = 0;         /* proc file "mhi_stats" was not created */
        _mtlk_hw_mhi_stats_cleanup(hw); /* free allocated buffers */
    }

    return res;
}
#endif /* MTLK_LEGACY_STATISTICS */

uint32 __MTLK_IFUNC
mtlk_hw_sta_stat_to_power (mtlk_hw_t *hw, unsigned radio_idx, uint32 stat_value, uint32 cbw)
{
  MTLK_ASSERT(hw);

  /* Gen5/Gen6: stat_value in StatPowerUnits */
  stat_value = STAT_PW_TO_POWER(stat_value);

  return stat_value;
}
/*--- end of MHI_STATISTICS --*/

/*------ PHY_RX_STATUS ------*/
#if 1 /* FIXME: should be in FW shared headers */
#define PHY_METRIC_2_NOISE_SHIFT    4
#endif

static __INLINE unsigned
_mtlk_hw_get_rrsi_offs(mtlk_hw_t *hw)
{
    return hw->phy_rx_status.rssi_offs;
}

unsigned
mtlk_hw_get_rrsi_offs(mtlk_hw_t *hw)
{
    MTLK_ASSERT(hw);
    return _mtlk_hw_get_rrsi_offs(hw);
}

static void
_hw_cleanup_phy_rx_status (mtlk_hw_t *hw)
{
    hw_phy_rx_status_t  *phy_rx_status = &hw->phy_rx_status;
#ifdef MTLK_LEGACY_STATISTICS
    int i;

    if (0 != phy_rx_status->db_size) { /* proc files were created */
        for (i = 0; i < hw->radio_descr->num_radios; i++)
            mtlk_df_ui_phy_rx_status_delete(mtlk_df_get_user(mtlk_vap_manager_get_master_df(
                wave_radio_descr_vap_manager_get(hw->radio_descr, i))));
    }
#endif

    mtlk_osal_mutex_cleanup(&hw->phy_rx_status.mutex);

    mtlk_osal_mem_free(phy_rx_status->db_data);
    memset(phy_rx_status, 0, sizeof(*phy_rx_status));
}

static int
_hw_init_phy_rx_status (mtlk_hw_t *hw)
{
    hw_phy_rx_status_t  *phy_rx_status;
    void                *virt_addr;
    uint32               size;
    uint32               max_nof_sta;   /* maximal possiblle nof_STAs according to FW shared files */
    uint32               cur_nof_sta;   /* nof_STAs from CHI area, i.e. actual supported by FW */
    uint32               all_sta_sid;
    uint32               rssi_offs, noise_offs;
    int                  res = MTLK_ERR_OK;
#ifdef MTLK_LEGACY_STATISTICS
    uint32               dma_addr;
    int                  i;
#endif

    phy_rx_status = &hw->phy_rx_status;
    memset(phy_rx_status, 0, sizeof(*phy_rx_status));

    if (_hw_type_is_gen6(hw)) {
        all_sta_sid = GEN6_ALL_STA_SID;
        max_nof_sta = GEN6_MAX_SID;
        rssi_offs   = GEN6_PHY_RSSI_TO_DBM;
        noise_offs  = GEN6_NOISE_OFFS;
    } else { /* gen5 */
        all_sta_sid = GEN5_ALL_STA_SID;
        max_nof_sta = GEN5_MAX_SID;
        rssi_offs   = GEN5_PHY_RSSI_TO_DBM;
        noise_offs  = GEN5_NOISE_OFFS;
    }

    cur_nof_sta = hw->max_stas;
    if (cur_nof_sta > max_nof_sta) {
        ELOG_DD("Number of supported STAs over: (%d > %d)", cur_nof_sta, max_nof_sta);
        return MTLK_ERR_NOT_SUPPORTED;
    }

    /* allocate buffer for the max nof_devices also maximal nof_STAs */
    size = (GEN6_NUM_OF_BANDS * sizeof(devicePhyRxStatusDb_t)) +
           (max_nof_sta * sizeof(stationPhyRxStatusDb_t));

    phy_rx_status->db_size     = size; /* proc files were created */

#ifdef MTLK_LEGACY_STATISTICS
    virt_addr = mtlk_osal_mem_dma_alloc(size, MTLK_MEM_TAG_EXTENSION);
#else
    virt_addr = mtlk_osal_mem_alloc(size, MTLK_MEM_TAG_EXTENSION);
#endif
    if (NULL == virt_addr) {
        ELOG_V("Can't allocate PhyRxStatus buffer");
        return MTLK_ERR_NO_MEM;
    }

    mtlk_osal_mutex_init(&hw->phy_rx_status.mutex);

    phy_rx_status->db_data     = virt_addr;
    phy_rx_status->all_sta_sid = all_sta_sid;
    phy_rx_status->max_sid     = cur_nof_sta; /* limit max SID with actual nof_STAs */
    phy_rx_status->rssi_offs   = rssi_offs;
    phy_rx_status->noise_offs  = noise_offs;
    ILOG0_DD("PhyRxStatus: max_sid %u, all_sta_sid %u",
             phy_rx_status->max_sid, phy_rx_status->all_sta_sid);

#ifdef MTLK_LEGACY_STATISTICS
    /* TEST */
    dma_addr = mtlk_osal_map_to_phys_addr(mtlk_ccr_get_dev_ctx(hw->ccr),
                                          virt_addr, size, MTLK_DATA_FROM_DEVICE);
    ILOG0_PDD("PhyRxStatus buffer 0x%p, dma_addr 0x%08x, size %u", virt_addr, dma_addr, size);

    if (dma_addr) {
        mtlk_osal_unmap_phys_addr(mtlk_ccr_get_dev_ctx(hw->ccr), dma_addr, size, MTLK_DATA_FROM_DEVICE);
    } else {
        ELOG_P("WARNING: failed mapping 0x%p to physical address", virt_addr);
        res = MTLK_ERR_NO_RESOURCES;
        goto finish;
    }

    for (i = 0; i < hw->radio_descr->num_radios; i++) {
      res = mtlk_df_ui_phy_rx_status_create(mtlk_df_get_user(mtlk_vap_manager_get_master_df(
              wave_radio_descr_vap_manager_get(hw->radio_descr, i))), size);
      if (MTLK_ERR_OK != res) { /* delete created files */
        while (--i >= 0) {
          mtlk_df_ui_phy_rx_status_delete(mtlk_df_get_user(mtlk_vap_manager_get_master_df(
              wave_radio_descr_vap_manager_get(hw->radio_descr, i))));
        }
        goto finish;
      }
    }

    phy_rx_status->db_size     = size; /* proc files were created */

finish:
#endif /* MTLK_LEGACY_STATISTICS */
    if (MTLK_ERR_OK != res) {
      _hw_cleanup_phy_rx_status(hw);
    }

    return res;
}

int __MTLK_IFUNC
mtlk_hw_copy_phy_rx_status (mtlk_hw_t *hw, uint8 *buff, uint32 *size)
{
    hw_phy_rx_status_t  *phy_rx_status;

    MTLK_ASSERT(hw);
    MTLK_ASSERT(buff);
    MTLK_ASSERT(size);

    phy_rx_status = &hw->phy_rx_status;
    if (*size < phy_rx_status->db_size) {
        return MTLK_ERR_NO_MEM;
    }

    *size = phy_rx_status->db_size;
    mtlk_osal_mutex_acquire(&hw->phy_rx_status.mutex);
    wave_memcpy(buff, *size, phy_rx_status->db_data, *size);
    mtlk_osal_mutex_release(&hw->phy_rx_status.mutex);

    return MTLK_ERR_OK;
}

static __INLINE uint32
__mtlk_hw_get_all_sta_sid (mtlk_hw_t *hw)
{
    return hw->phy_rx_status.all_sta_sid;
}

static __INLINE uint32
__mtlk_hw_get_max_sid (mtlk_hw_t *hw)
{
    return hw->phy_rx_status.max_sid;
}

unsigned __MTLK_IFUNC
wave_hw_max_sid_get (mtlk_hw_t *hw)
{
    MTLK_ASSERT(hw);
    return _hw_type_is_gen6(hw) ? GEN6_MAX_SID : GEN5_MAX_SID;
}

BOOL __MTLK_IFUNC
mtlk_hw_is_sid_all_sta_sid (mtlk_hw_t *hw, uint32 sid)
{
    return (sid == __mtlk_hw_get_all_sta_sid(hw));
}

static BOOL
_mtlk_hw_is_sid_valid (mtlk_hw_t *hw, uint32 sid)
{
    return (sid < __mtlk_hw_get_max_sid(hw));
}

static BOOL
_mtlk_hw_is_sid_valid_or_all_sta_sid (mtlk_hw_t *hw, uint32 sid)
{
    if (__LIKELY(_mtlk_hw_is_sid_valid(hw, sid) ||
                 mtlk_hw_is_sid_all_sta_sid(hw, sid))) {
        return TRUE;
    }

    WLOG_DDD("Invalid sta_sid: %d is nor in range [0...%u] nor %u",
            sid, __mtlk_hw_get_max_sid(hw) - 1, __mtlk_hw_get_all_sta_sid(hw));

    return FALSE;
}

BOOL __MTLK_IFUNC
mtlk_hw_is_sid_valid_or_all_sta_sid (mtlk_hw_t *hw, uint32 sid)
{
    MTLK_ASSERT(hw);
    return _mtlk_hw_is_sid_valid_or_all_sta_sid(hw, sid);
}

void scan_update_curr_chan_info(const mtlk_core_t *core, int noise, unsigned ch_load);

int __MTLK_IFUNC
mtlk_hw_noise_phy_to_noise_dbm (mtlk_hw_t *hw, uint32 phy_noise, uint32 rf_gain)
{
  int noise_dbm = phy_noise;

  /* Unused or No antenna */
  if ((GEN5_PHY_METRIC_INVALID == phy_noise) || (GEN6_PHY_METRIC_INVALID == phy_noise))
    return 0;

  /* On HW B0 the noise value in dBm already (int8) */
  if (_hw_type_is_gen6_b0(hw)) {
    noise_dbm = (int8) phy_noise;
    goto end;
  }

  /* convert noise PHY metric to integer value */
  noise_dbm *= 3;
  noise_dbm += (1 << (PHY_METRIC_2_NOISE_SHIFT - 1)); /* round up */
  noise_dbm >>= PHY_METRIC_2_NOISE_SHIFT;             /* shift */

  /* calculate estimate noise (signed value) */
  noise_dbm -= (hw->phy_rx_status.noise_offs + rf_gain);

end:
  return MAX(MIN_NOISE, noise_dbm);
}

static int
_mtlk_hw_get_noise_by_phy_metrics (mtlk_hw_t *hw, uint8 *noise, uint8 *rf_gain)
{
    uint32  max_gain;
    uint32  max_noise;
    int     i;

    /* get max noise and rf_gain for this */
    max_noise = max_gain = 0;
    for (i = 0; i < MAX_NUM_RX_ANTENNAS; i++) {
        if (max_noise < noise[i]) {
            max_noise = noise[i];
            max_gain  = rf_gain[i];
        }
    }

    return mtlk_hw_noise_phy_to_noise_dbm(hw, max_noise, max_gain);
}

static devicePhyRxStatusDb_t *
_wave_hw_radio_get_dev_status (mtlk_hw_t *hw, uint32 radio_id)
{
    MTLK_ASSERT(radio_id < GEN6_NUM_OF_BANDS);
#ifdef MTLK_LEGACY_STATISTICS
    return &hw->phy_rx_status.db_data->dev_status[radio_id];
#else
    return &hw->phy_rx_status.db_data->devicePhyRxStatus[radio_id];
#endif
}

static __INLINE unsigned
_wave_hw_radio_dev_status_get_chan_load (devicePhyRxStatusDb_t *dev_status)
{
    MTLK_STATIC_ASSERT(sizeof(uint8) == sizeof(dev_status->channel_load));
    return dev_status->channel_load;
}

static __INLINE unsigned
_wave_hw_radio_dev_status_get_chan_util (devicePhyRxStatusDb_t *dev_status)
{
    MTLK_STATIC_ASSERT(sizeof(uint8) == sizeof(dev_status->totalChannelUtilization));
    return dev_status->totalChannelUtilization;
}

static unsigned
_wave_hw_radio_get_chan_util (mtlk_hw_t *hw, uint32 radio_id)
{
    devicePhyRxStatusDb_t  *dev_status;

    dev_status = _wave_hw_radio_get_dev_status(hw, radio_id);

    return _wave_hw_radio_dev_status_get_chan_util(dev_status);
}

static void
_wave_hw_phy_rx_status_update_dev_info (mtlk_hw_t *hw,  mtlk_core_t *core)
{
    devicePhyRxStatusDb_t   *dev_status;
    wave_radio_phy_stat_t    radio_stat_data, *radio_status = &radio_stat_data;
    uint8   radio_id;

    radio_id = wave_vap_radio_id_get(core->vap_handle);
    MTLK_ASSERT(radio_id < GEN6_NUM_OF_BANDS);

    /* Read devicePhyRxStatus data and update Radio Phy Status and Core channel info */
    dev_status = _wave_hw_radio_get_dev_status(hw, radio_id);
    radio_status->noise   = _mtlk_hw_get_noise_by_phy_metrics(hw, dev_status->noise, dev_status->rf_gain);
    radio_status->ch_load = _wave_hw_radio_dev_status_get_chan_load(dev_status);
    radio_status->ch_util = _wave_hw_radio_dev_status_get_chan_util(dev_status);

    wave_radio_phy_status_update(__mtlk_hw_wave_radio_get(hw, radio_id), radio_status);
    scan_update_curr_chan_info(core, radio_status->noise, radio_status->ch_load);
}

/* Update TX/RX statistics for all STAs */
void
hw_mac_update_peers_stats (mtlk_hw_t *hw, mtlk_core_t *core)
{
    wholePhyRxStatusDb_t *phy_db;
    sta_db               *sta_db;
    const sta_entry      *sta;
    uint32                sta_sid;
    BOOL                  is_gen6;

    mtlk_stadb_iterator_t iter;

    ILOG3_D("CID-%04x", mtlk_vap_get_oid(core->vap_handle));

    mtlk_osal_mutex_acquire(&hw->phy_rx_status.mutex);

    phy_db = hw->phy_rx_status.db_data;
    sta_db = mtlk_core_get_stadb(core);
    is_gen6 = _hw_type_is_gen6(hw);

    sta = mtlk_stadb_iterate_first(sta_db, &iter);
    if (sta) {
      do {
        sta_sid = mtlk_sta_get_sid(sta);
        if (_mtlk_hw_is_sid_valid(hw, sta_sid)) {
#ifdef MTLK_LEGACY_STATISTICS
            stationPhyRxStatusDb_t *sta_status = &phy_db->sta_status[sta_sid];
#else
            stationPhyRxStatusDb_t *sta_status = &phy_db->staPhyRxStatus[sta_sid];
#endif

            mtlk_sta_update_phy_info((sta_entry *)sta, hw, sta_status, is_gen6);

            _mtlk_hw_mhi_sta_fill_tx_rate_statistic(hw, mtlk_sta_get_mhi_tx_stats((sta_entry *)sta), sta_sid);

            _mtlk_sta_update_statistics(hw, (sta_entry *)sta, sta_status);

            mtlk_sta_update_mhi_peers_stats((sta_entry *)sta);
        }

        sta = mtlk_stadb_iterate_next(&iter);
      } while (sta);
      mtlk_stadb_iterate_done(&iter);
    }

    mtlk_osal_mutex_release(&hw->phy_rx_status.mutex);
}

#ifdef MTLK_LEGACY_STATISTICS
int __MTLK_IFUNC
hw_phy_rx_status_get (mtlk_hw_t *hw, mtlk_core_t *core)
{
    mtlk_txmm_msg_t      man_msg;
    mtlk_txmm_data_t    *man_entry;
    UMI_PHY_STATUS_REQ  *req;
    int                  res = MTLK_ERR_OK;

    hw_phy_rx_status_t  *phy_rx_status;
    uint32               dma_addr;

    phy_rx_status = &hw->phy_rx_status;

    mtlk_osal_mutex_acquire(&hw->phy_rx_status.mutex);

    /* Retrive PHY status only for 1st radio, but update for any */
    if (!wave_radio_id_is_first(wave_vap_radio_id_get(core->vap_handle))) {
      goto READY;
    }

    /* prepare msg for the FW */
    if (!(man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, &hw->txmm, NULL))) {
        ELOG_D("card_idx %d: UM_MAN_PHY_STATUS_REQ init failed", hw->card_idx);
      res = MTLK_ERR_NO_RESOURCES;
      goto FINISH;
    }

    dma_addr = mtlk_osal_map_to_phys_addr(mtlk_ccr_get_dev_ctx(hw->ccr),
                                          phy_rx_status->db_data,
                                          phy_rx_status->db_size,
                                          MTLK_DATA_FROM_DEVICE);
    if (!dma_addr) {
      ILOG2_P("WARNING: failed mapping 0x%p to physical address", phy_rx_status->db_data);
      res = MTLK_ERR_NO_RESOURCES;
      goto FINISH;
    }

    man_entry->id = UM_MAN_PHY_STATUS_REQ;
    man_entry->payload_size = sizeof(*req);
    req = (UMI_PHY_STATUS_REQ *) man_entry->payload;
    req->ddrBufferAddress = HOST_TO_MAC32(dma_addr);
    req->ddrBufferSize = HOST_TO_MAC32(phy_rx_status->db_size);

    res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);
    mtlk_txmm_msg_cleanup(&man_msg);

    mtlk_osal_unmap_phys_addr(mtlk_ccr_get_dev_ctx(hw->ccr),
                              dma_addr, phy_rx_status->db_size,
                              MTLK_DATA_FROM_DEVICE);

    if (res != MTLK_ERR_OK) {
        ELOG_DD("card_idx %d: Sending failed, err=%i", hw->card_idx, res);
        goto FINISH;
    }

READY: /* Parse received data */
    _wave_hw_phy_rx_status_update_dev_info(hw, core);

FINISH:
    mtlk_osal_mutex_release(&hw->phy_rx_status.mutex);

    return res;
}
#else /* MTLK_LEGACY_STATISTICS */

int __MTLK_IFUNC
hw_phy_rx_status_get (mtlk_hw_t *hw, mtlk_core_t *core)
{
    hw_phy_rx_status_t  *phy_rx_status;

    phy_rx_status = &hw->phy_rx_status;

    mtlk_osal_mutex_acquire(&hw->phy_rx_status.mutex);

     /* Parse received data */
    _wave_hw_phy_rx_status_update_dev_info(hw, core);

    mtlk_osal_mutex_release(&hw->phy_rx_status.mutex);

    return MTLK_ERR_OK;
}

#endif /* MTLK_LEGACY_STATISTICS */

#ifdef CPTCFG_IWLWAV_PMCU_SUPPORT
static int _mtlk_mmb_pmcu_gen5_detector(void *ctx, ePMCU_request request)
{
  mtlk_hw_t *hw;

  hw = (mtlk_hw_t *)ctx;
  ILOG2_DD("Request: %d 0x%08x", request, hw);
  UNUSED_PARAM(hw);
  return MTLK_ERR_OK;
}

static int _mtlk_mmb_pmcu_gen5_notifier(void *ctx, ePMCU_request request)
{
  mtlk_hw_t *hw;

  hw = (mtlk_hw_t *)ctx;
  ILOG2_DD("Request: %d for hw: 0x%08x", request, hw);
  UNUSED_PARAM(hw);
  return MTLK_ERR_OK;
}

static int _mtlk_mmb_pmcu_register(mtlk_hw_t *hw)
{
  PMCU_client_t client;
  int ret;

  ILOG2_D("Registering PMCU client: 0x%08x", hw);

  client.ctx = hw;
  if (mtlk_hw_type_is_gen5(hw)) {
    client.detector = _mtlk_mmb_pmcu_gen5_detector;
    client.notifier = _mtlk_mmb_pmcu_gen5_notifier;
  } else {
    client.detector = _mtlk_mmb_pmcu_gen5_detector;
    client.notifier = _mtlk_mmb_pmcu_gen5_notifier;
  }
  ret = wv_PMCU_Client_Register(&client);
  if (MTLK_ERR_OK != ret) {
    ELOG_D("failed to register in PMCU, ret=%d",ret);
    return ret;
  }

  ret = wv_PMCU_Switch_Freq(PMCU_SWITCH_TO_HIGH_INIT);
  if (MTLK_ERR_OK != ret) {
    ELOG_D("failed to request high freq, ret=%d",ret);
    return ret;
  }

  return MTLK_ERR_OK;
}

static int _mtlk_mmb_pmcu_unregister(mtlk_hw_t *hw)
{
  int ret;
  ILOG2_D("Unregistering PMCU client: 0x%08x", hw);
  ret = wv_PMCU_Client_Unregister(hw);
  if (MTLK_ERR_OK != ret) {
    ELOG_D("failed to unregister in PMCU, ret=%d",ret);
    return ret;
  }
  return MTLK_ERR_OK;
}

#endif /* CPTCFG_IWLWAV_PMCU_SUPPORT */

#ifdef CPTCFG_IWLWAV_SET_PM_QOS
static int _mtlk_mmb_pm_qos_init (mtlk_hw_t *hw, s32 cpu_dma_latency)
{
    pm_qos_add_request(&hw->pm_qos.pm_qos, PM_QOS_CPU_DMA_LATENCY, cpu_dma_latency);
    ILOG0_D("cpu_dma_latency: %d", cpu_dma_latency);
    return MTLK_ERR_OK;
}

static int _mtlk_mmb_pm_qos_cleanup (mtlk_hw_t *hw)
{
    pm_qos_update_request(&hw->pm_qos.pm_qos, PM_QOS_DEFAULT_VALUE); /*restore to default value */
    pm_qos_remove_request(&hw->pm_qos.pm_qos);
    return MTLK_ERR_OK;
}

int __MTLK_IFUNC
mtlk_mmb_update_cpu_dma_latency (mtlk_hw_t *hw, s32 new_cpu_dma_latency)
{
    MTLK_ASSERT(hw);
    cpu_dma_latency = new_cpu_dma_latency;
    pm_qos_update_request(&hw->pm_qos.pm_qos, new_cpu_dma_latency);
    return MTLK_ERR_OK;
}
#endif


MTLK_INIT_STEPS_LIST_BEGIN(hw_mmb_card)
#if CPTCFG_IWLWAV_TRACER_HISTORY_LENGTH
  MTLK_INIT_STEPS_LIST_ENTRY(hw_mmb_card, HW_TRACER_INIT)
#endif
#if MTLK_USE_DIRECTCONNECT_DP_API
  MTLK_INIT_STEPS_LIST_ENTRY(hw_mmb_card, HW_DCDP_INIT)
#endif
  MTLK_INIT_STEPS_LIST_ENTRY(hw_mmb_card, HW_DATA_HANDLERS_INIT)
  MTLK_INIT_STEPS_LIST_ENTRY(hw_mmb_card, HW_IRB_ALLOC)
  MTLK_INIT_STEPS_LIST_ENTRY(hw_mmb_card, HW_IRB_INIT)
  MTLK_INIT_STEPS_LIST_ENTRY(hw_mmb_card, HW_WSS_CREATE)
  MTLK_INIT_STEPS_LIST_ENTRY(hw_mmb_card, HW_SOURCE_CNTRs)
  MTLK_INIT_STEPS_LIST_ENTRY(hw_mmb_card, HW_RX_DATA_LIST)
  MTLK_INIT_STEPS_LIST_ENTRY(hw_mmb_card, HW_REG_LOCK)
  MTLK_INIT_STEPS_LIST_ENTRY(hw_mmb_card, HW_VERSION_LOCK)
  MTLK_INIT_STEPS_LIST_ENTRY(hw_mmb_card, HW_REQ_MC_LOCK)
  MTLK_INIT_STEPS_LIST_ENTRY(hw_mmb_card, HW_INIT_EVT)
  MTLK_INIT_STEPS_LIST_ENTRY(hw_mmb_card, HW_RX_PEND_TIMER)
  MTLK_INIT_STEPS_LIST_ENTRY(hw_mmb_card, HW_RX_TX_LIMITS)
#ifdef CPTCFG_IWLWAV_SET_PM_QOS
  MTLK_INIT_STEPS_LIST_ENTRY(hw_mmb_card, HW_PM_QOS_INIT)
#endif
#ifdef CPTCFG_IWLWAV_LEGACY_INT_RECOVERY_MON
  MTLK_INIT_STEPS_LIST_ENTRY(hw_mmb_card, HW_INT_REC_MON)
#endif
MTLK_INIT_INNER_STEPS_BEGIN(hw_mmb_card)
  MTLK_INIT_STEPS_LIST_ENTRY(hw_mmb_card, HW_IRB_NAME)
MTLK_INIT_STEPS_LIST_END(hw_mmb_card);


MTLK_START_STEPS_LIST_BEGIN(hw_mmb_card)
#ifdef CPTCFG_IWLWAV_PMCU_SUPPORT
  MTLK_START_STEPS_LIST_ENTRY(hw_mmb_card, HW_PMCU_INIT_CLIENT)
#endif /*CPTCFG_IWLWAV_PMCU_SUPPORT */
  MTLK_START_STEPS_LIST_ENTRY(hw_mmb_card, HW_POWER_ON)
  MTLK_START_STEPS_LIST_ENTRY(hw_mmb_card, HW_BIST_READ)
  MTLK_START_STEPS_LIST_ENTRY(hw_mmb_card, HW_INIT_PLL)
  MTLK_START_STEPS_LIST_ENTRY(hw_mmb_card, HW_CHI_INIT)
  MTLK_START_STEPS_LIST_ENTRY(hw_mmb_card, HW_LOAD_FIRMWARE)
  MTLK_START_STEPS_LIST_ENTRY(hw_mmb_card, HW_RUN_FIRMWARE)
  MTLK_START_STEPS_LIST_ENTRY(hw_mmb_card, HW_WAIT_CHI_MAGIC)
#if MTLK_DCDP_SEPARATE_REG
  MTLK_START_STEPS_LIST_ENTRY(hw_mmb_card, HW_DATA_PATH_REGISTER)
#endif
  MTLK_START_STEPS_LIST_ENTRY(hw_mmb_card, HW_MAN_REQ_BDR)
  MTLK_START_STEPS_LIST_ENTRY(hw_mmb_card, HW_MAN_IND_BDR)
  MTLK_START_STEPS_LIST_ENTRY(hw_mmb_card, HW_DBG_REQ_BDR)
  MTLK_START_STEPS_LIST_ENTRY(hw_mmb_card, HW_DBG_IND_BDR)
  MTLK_START_STEPS_LIST_ENTRY(hw_mmb_card, HW_DAT_REQ_BDR)
  MTLK_START_STEPS_LIST_ENTRY(hw_mmb_card, HW_DAT_IND_BDR)
  MTLK_START_STEPS_LIST_ENTRY(hw_mmb_card, HW_BSS_MGMT)
  MTLK_START_STEPS_LIST_ENTRY(hw_mmb_card, HW_RX_DATA_BUFFERS)
  MTLK_START_STEPS_LIST_ENTRY(hw_mmb_card, HW_RX_MGMT_BUFFERS)
  MTLK_START_STEPS_LIST_ENTRY(hw_mmb_card, HW_CALIBRATION_CACHE)
  MTLK_START_STEPS_LIST_ENTRY(hw_mmb_card, HW_MIPS_CONTROL)
  MTLK_START_STEPS_LIST_ENTRY(hw_mmb_card, HW_FW_LOGGER)
MTLK_START_INNER_STEPS_BEGIN(hw_mmb_card)
MTLK_START_STEPS_LIST_END(hw_mmb_card);

MTLK_START_STEPS_LIST_BEGIN(hw_mmb_card_fin)
  MTLK_START_STEPS_LIST_ENTRY(hw_mmb_card_fin, HW_TXMM)
  MTLK_START_STEPS_LIST_ENTRY(hw_mmb_card_fin, HW_TXDM)
#if !MTLK_DCDP_SEPARATE_REG
  MTLK_START_STEPS_LIST_ENTRY(hw_mmb_card_fin, HW_DATA_PATH_REGISTER)
#endif
  MTLK_START_STEPS_LIST_ENTRY(hw_mmb_card_fin, HW_DATA_PATH)
  MTLK_START_STEPS_LIST_ENTRY(hw_mmb_card_fin, HW_HW_DEP_CFG)
  MTLK_START_STEPS_LIST_ENTRY(hw_mmb_card_fin, HW_FW_LOG_SEVERITY)
  MTLK_START_STEPS_LIST_ENTRY(hw_mmb_card_fin, HW_RX_PEND_TIMER)
#ifdef MTLK_LEGACY_STATISTICS
  MTLK_START_STEPS_LIST_ENTRY(hw_mmb_card_fin, HW_MHI_STATISTIC)
  MTLK_START_STEPS_LIST_ENTRY(hw_mmb_card_fin, HW_PHY_RX_STATUS)
#else
  MTLK_START_STEPS_LIST_ENTRY(hw_mmb_card_fin, HW_STATISTICS)
#endif
MTLK_START_INNER_STEPS_BEGIN(hw_mmb_card_fin)
MTLK_START_STEPS_LIST_END(hw_mmb_card_fin);

MTLK_START_STEPS_LIST_BEGIN(hw_mmb_eeprom)
  MTLK_START_STEPS_LIST_ENTRY(hw_mmb_eeprom, HW_EEPROM_READ)
  MTLK_START_STEPS_LIST_ENTRY(hw_mmb_eeprom, HW_PSDB_READ)
MTLK_START_INNER_STEPS_BEGIN(hw_mmb_eeprom)
MTLK_START_STEPS_LIST_END(hw_mmb_eeprom);

static void _mtlk_mmb_data_path_unregister(mtlk_hw_t *hw);
static int _mtlk_mmb_data_path_uninit(mtlk_hw_t *hw, BOOL is_recovery);

void __MTLK_IFUNC
mtlk_hw_mmb_stop_card(mtlk_hw_t *hw)
{
  MTLK_STOP_BEGIN(hw_mmb_card, MTLK_OBJ_PTR(hw))
    MTLK_STOP_STEP(hw_mmb_card, HW_FW_LOGGER, MTLK_OBJ_PTR(hw),
                   _mtlk_mmb_cleanup_fw_logger, (hw));
    MTLK_STOP_STEP(hw_mmb_card, HW_MIPS_CONTROL, MTLK_OBJ_PTR(hw),
                   MTLK_NOACTION, ());
    MTLK_STOP_STEP(hw_mmb_card, HW_CALIBRATION_CACHE, MTLK_OBJ_PTR(hw),
                   _mtlk_mmb_cleanup_calibration_cache, (hw));

    MTLK_STOP_STEP(hw_mmb_card, HW_RX_MGMT_BUFFERS, MTLK_OBJ_PTR(hw),
                   _mtlk_mmb_free_preallocated_rx_mgmt_buffers, (hw));

    MTLK_STOP_STEP(hw_mmb_card, HW_RX_DATA_BUFFERS, MTLK_OBJ_PTR(hw),
                   _mtlk_mmb_free_preallocated_rx_data_buffers, (hw));

    MTLK_STOP_STEP(hw_mmb_card, HW_BSS_MGMT, MTLK_OBJ_PTR(hw),
                   _mtlk_mmb_bss_mgmt_cleanup, (hw));
    MTLK_STOP_STEP(hw_mmb_card, HW_DAT_IND_BDR, MTLK_OBJ_PTR(hw),
                   _mtlk_mmb_cleanup_data_ind_bdr, (hw));
    MTLK_STOP_STEP(hw_mmb_card, HW_DAT_REQ_BDR, MTLK_OBJ_PTR(hw),
                   _mtlk_mmb_cleanup_data_req_bdr, (hw));
    MTLK_STOP_STEP(hw_mmb_card, HW_DBG_IND_BDR, MTLK_OBJ_PTR(hw),
                   _mtlk_mmb_cleanup_dbg_ind_bdr, (hw));
    MTLK_STOP_STEP(hw_mmb_card, HW_DBG_REQ_BDR, MTLK_OBJ_PTR(hw),
                   _mtlk_mmb_cleanup_dbg_req_bdr, (hw));
    MTLK_STOP_STEP(hw_mmb_card, HW_MAN_IND_BDR, MTLK_OBJ_PTR(hw),
                   _mtlk_mmb_cleanup_man_ind_bdr, (hw));
    MTLK_STOP_STEP(hw_mmb_card, HW_MAN_REQ_BDR, MTLK_OBJ_PTR(hw),
                   _mtlk_mmb_cleanup_man_req_bdr, (hw));
#if MTLK_DCDP_SEPARATE_REG
    MTLK_STOP_STEP(hw_mmb_card, HW_DATA_PATH_REGISTER, MTLK_OBJ_PTR(hw),
                   _mtlk_mmb_data_path_unregister, (hw));
#endif
    MTLK_STOP_STEP(hw_mmb_card, HW_WAIT_CHI_MAGIC, MTLK_OBJ_PTR(hw), MTLK_NOACTION, ());
    MTLK_STOP_STEP(hw_mmb_card, HW_RUN_FIRMWARE, MTLK_OBJ_PTR(hw), MTLK_NOACTION, ());
    MTLK_STOP_STEP(hw_mmb_card, HW_LOAD_FIRMWARE, MTLK_OBJ_PTR(hw), MTLK_NOACTION, ());
    MTLK_STOP_STEP(hw_mmb_card, HW_CHI_INIT, MTLK_OBJ_PTR(hw), MTLK_NOACTION, ());
    MTLK_STOP_STEP(hw_mmb_card, HW_INIT_PLL, MTLK_OBJ_PTR(hw), MTLK_NOACTION, ());
    MTLK_STOP_STEP(hw_mmb_card, HW_BIST_READ, MTLK_OBJ_PTR(hw), MTLK_NOACTION, ());
    MTLK_STOP_STEP(hw_mmb_card, HW_POWER_ON, MTLK_OBJ_PTR(hw), MTLK_NOACTION, ());
#ifdef CPTCFG_IWLWAV_PMCU_SUPPORT
    MTLK_STOP_STEP(hw_mmb_card, HW_PMCU_INIT_CLIENT, MTLK_OBJ_PTR(hw), _mtlk_mmb_pmcu_unregister, (hw));
#endif
#ifndef CPTCFG_IWLWAV_NO_FW_RESET_ON_STOP
    _mtlk_mmb_cleanup_reset_mac(hw);
#endif
  MTLK_STOP_END(hw_mmb_card, MTLK_OBJ_PTR(hw))
}

void __MTLK_IFUNC
mtlk_hw_mmb_stop_card_finalize(mtlk_hw_t *hw)
{
  int res;
  uint32 mac_soft_reset_enable = 0;
  int exception = mtlk_hw_is_halted(hw->state);
  mtlk_handle_t lock_val;

  MTLK_ASSERT(NULL != hw->ccr);

  hw->state = MTLK_HW_STATE_UNLOADING;

  /* WAVE600: TODO: do it for the first radio now, but rework later! */
  res = mtlk_hw_get_prop(wave_radio_descr_hw_api_get(hw->radio_descr, 0), MTLK_CORE_PROP_MAC_SW_RESET_ENABLED, &mac_soft_reset_enable, sizeof(mac_soft_reset_enable));
  if (res != MTLK_ERR_OK) {
    mac_soft_reset_enable = 0;
  }

  MTLK_STOP_BEGIN(hw_mmb_card_fin, MTLK_OBJ_PTR(&hw->hw_start_fin))
#ifdef MTLK_LEGACY_STATISTICS
    MTLK_STOP_STEP(hw_mmb_card_fin, HW_PHY_RX_STATUS, MTLK_OBJ_PTR(&hw->hw_start_fin),
                   _hw_cleanup_phy_rx_status, (hw));

    MTLK_STOP_STEP(hw_mmb_card_fin, HW_MHI_STATISTIC, MTLK_OBJ_PTR(&hw->hw_start_fin),
                   _mtlk_hw_mhi_stats_cleanup, (hw));
#else
    MTLK_STOP_STEP(hw_mmb_card_fin, HW_STATISTICS, MTLK_OBJ_PTR(&hw->hw_start_fin),
                   _mtlk_hw_statistics_cleanup, (hw));
#endif

    MTLK_STOP_STEP(hw_mmb_card_fin, HW_RX_PEND_TIMER, MTLK_OBJ_PTR(&hw->hw_start_fin),
                   mtlk_osal_timer_cancel_sync, (&hw->rx.pending.timer));
    MTLK_STOP_STEP(hw_mmb_card_fin, HW_FW_LOG_SEVERITY, MTLK_OBJ_PTR(&hw->hw_start_fin), MTLK_NOACTION,());
    MTLK_STOP_STEP(hw_mmb_card_fin, HW_HW_DEP_CFG, MTLK_OBJ_PTR(&hw->hw_start_fin), MTLK_NOACTION,());
    MTLK_STOP_STEP(hw_mmb_card_fin, HW_DATA_PATH, MTLK_OBJ_PTR(&hw->hw_start_fin), _mtlk_mmb_data_path_uninit, (hw, FALSE));
#if !MTLK_DCDP_SEPARATE_REG
    MTLK_STOP_STEP(hw_mmb_card_fin, HW_DATA_PATH_REGISTER, MTLK_OBJ_PTR(&hw->hw_start_fin), _mtlk_mmb_data_path_unregister, (hw));
#endif

    if (hw->mac_reset_logic_initialized && !exception) {
      ILOG3_V("Calling _mtlk_pci_send_sw_reset_mac_req");
      if (_mtlk_mmb_send_sw_reset_mac_req(hw) != MTLK_ERR_OK) {
        hw->mac_reset_logic_initialized = FALSE;
      }
    } else if (exception && (mac_soft_reset_enable == 0)) {
      hw->mac_reset_logic_initialized = FALSE;
    }

    /* Disable Interrupts and recovery timer */
#ifdef CPTCFG_IWLWAV_LEGACY_INT_RECOVERY_MON
    mtlk_osal_timer_cancel_sync(&hw->recover_timer);
#endif
    lock_val = mtlk_osal_lock_acquire_irq(&hw->reg_lock);
    mtlk_ccr_disable_interrupts(hw->ccr, MTLK_IRQ_ALL);
    mtlk_osal_lock_release_irq(&hw->reg_lock, lock_val);

    _mtlk_mmb_stop_events_completely(hw);

    MTLK_STOP_STEP(hw_mmb_card_fin, HW_TXDM, MTLK_OBJ_PTR(&hw->hw_start_fin),
                   mtlk_txmm_stop, (&hw->txdm_base));
    MTLK_STOP_STEP(hw_mmb_card_fin, HW_TXMM, MTLK_OBJ_PTR(&hw->hw_start_fin),
                   mtlk_txmm_stop, (&hw->txmm_base));
  MTLK_STOP_END(hw_mmb_card_fin, MTLK_OBJ_PTR(&hw->hw_start_fin))
}

void __MTLK_IFUNC
mtlk_hw_mmb_cleanup_card(mtlk_hw_t *hw)
{
  MTLK_CLEANUP_BEGIN(hw_mmb_card, MTLK_OBJ_PTR(hw))
#ifdef CPTCFG_IWLWAV_LEGACY_INT_RECOVERY_MON
    MTLK_CLEANUP_STEP(hw_mmb_card, HW_INT_REC_MON, MTLK_OBJ_PTR(hw),
                      mtlk_osal_timer_cleanup, (&hw->recover_timer));
#endif
#ifdef CPTCFG_IWLWAV_SET_PM_QOS
    MTLK_CLEANUP_STEP(hw_mmb_card, HW_PM_QOS_INIT, MTLK_OBJ_PTR(hw),
                      _mtlk_mmb_pm_qos_cleanup, (hw));
#endif
    MTLK_CLEANUP_STEP(hw_mmb_card, HW_RX_TX_LIMITS, MTLK_OBJ_PTR(hw),
                      MTLK_NOACTION, ());
    MTLK_CLEANUP_STEP(hw_mmb_card, HW_RX_PEND_TIMER, MTLK_OBJ_PTR(hw),
                      mtlk_osal_timer_cleanup, (&hw->rx.pending.timer));
    MTLK_CLEANUP_STEP(hw_mmb_card, HW_INIT_EVT, MTLK_OBJ_PTR(hw),
                      MTLK_HW_INIT_EVT_CLEANUP, (hw));
    MTLK_CLEANUP_STEP(hw_mmb_card, HW_REQ_MC_LOCK, MTLK_OBJ_PTR(hw),
                      mtlk_osal_lock_cleanup, (&hw->mc_group_lock));
    MTLK_CLEANUP_STEP(hw_mmb_card, HW_VERSION_LOCK, MTLK_OBJ_PTR(hw),
                      mtlk_osal_lock_cleanup, (&hw->version_lock));
    MTLK_CLEANUP_STEP(hw_mmb_card, HW_REG_LOCK, MTLK_OBJ_PTR(hw),
                      mtlk_osal_lock_cleanup, (&hw->reg_lock));
    MTLK_CLEANUP_STEP(hw_mmb_card, HW_RX_DATA_LIST, MTLK_OBJ_PTR(hw),
                      mtlk_lslist_cleanup, (&hw->rx.pending.lbufs));
    MTLK_CLEANUP_STEP(hw_mmb_card, HW_SOURCE_CNTRs, MTLK_OBJ_PTR(hw),
                      mtlk_wss_cntrs_close, (hw->wss, hw->wss_hcntrs,
                      MTLK_HW_SOURCE_CNT_LAST));
    MTLK_CLEANUP_STEP(hw_mmb_card, HW_WSS_CREATE, MTLK_OBJ_PTR(hw),
                      mtlk_wss_delete, (hw->wss));
    MTLK_CLEANUP_STEP(hw_mmb_card, HW_IRB_INIT, MTLK_OBJ_PTR(hw),
                      mtlk_irbd_cleanup, (hw->irbd));
    MTLK_CLEANUP_STEP(hw_mmb_card, HW_IRB_ALLOC, MTLK_OBJ_PTR(hw),
                      mtlk_irbd_free, (hw->irbd));
    MTLK_CLEANUP_STEP(hw_mmb_card, HW_DATA_HANDLERS_INIT, MTLK_OBJ_PTR(hw),
                      MTLK_NOACTION, ());
#if MTLK_USE_DIRECTCONNECT_DP_API
    MTLK_CLEANUP_STEP(hw_mmb_card, HW_DCDP_INIT, MTLK_OBJ_PTR(hw),
                      MTLK_NOACTION, ());
#endif
    hw->irbd = NULL;
#if CPTCFG_IWLWAV_TRACER_HISTORY_LENGTH
    MTLK_CLEANUP_STEP(hw_mmb_card, HW_TRACER_INIT, MTLK_OBJ_PTR(hw),
                      MTLK_TRACER_CLEAN, (hw));
#endif
  MTLK_CLEANUP_END(hw_mmb_card, MTLK_OBJ_PTR(hw));
}

/* set card config flag for dual PCIe*/
static void _wave_hw_dual_pci_mode_set(mtlk_hw_t *hw, BOOL is_dual_pci)
{
  if (is_dual_pci) {
    ILOG0_V("dual PCI mode ON");
    hw->fw_card_cfg.card_cfg.dual_pcie_cfg = CARD_CONFIGURATION_PCI_MODE_DUAL_PCI;
  }
  else {
    ILOG0_V("dual PCI mode OFF");
    hw->fw_card_cfg.card_cfg.dual_pcie_cfg = CARD_CONFIGURATION_PCI_MODE_SINGLE_PCI;
  }
}

static void
_wave_hw_cfg_platform_type_set (mtlk_hw_t *hw)
{
  hw->fw_card_cfg.card_cfg.platform_type = __hw_mmb_card_is_emul(hw) ?
    TEST_PLATFORM_TYPE_PDXP : TEST_PLATFORM_TYPE_FPGA; /* is ignored on real chip */
  ILOG0_D("TestPlatformType: %d", hw->fw_card_cfg.card_cfg.platform_type);
}

static int _wave_hw_radio_band_cfg_init(mtlk_hw_t *hw);

int __MTLK_IFUNC
mtlk_hw_mmb_init_card(mtlk_hw_t *hw, mtlk_ccr_t *ccr, unsigned char *mmb_pas, unsigned char *mmb_pas_phy,
                      wave_radio_descr_t *radio_descr, int fast_path, BOOL is_dual_pci)
{
  char irb_node_name[sizeof(MTLK_IRB_HW_NAME) + 3]; /* 3 chars for card index */
  BOOL fast_path_supported;
  int  tmp;

  MTLK_ASSERT(NULL != hw);
  MTLK_ASSERT(NULL != ccr);
  MTLK_ASSERT(NULL != mmb_pas);
  MTLK_ASSERT(NULL != radio_descr);
  MTLK_ASSERT(ARRAY_SIZE(hw->wss_hcntrs) == MTLK_HW_SOURCE_CNT_LAST);

  hw->mmb_base = mmb_pas;

  /* Shared RAM offset depending on platform */
  tmp = (_hw_type_is_gen6(hw) ? G6_SHARED_RAM_OFFSET : G5_SHARED_RAM_OFFSET);
  hw->mmb_pas = mmb_pas + tmp;
  /* Fixme: mmb_pas_phy requires no offset from base. Fix naming later! */
  hw->mmb_pas_phy = mmb_pas_phy;

  hw->radio_descr = radio_descr;
  hw->state = MTLK_HW_STATE_INITIATING;
  hw->irq_mode = MTLK_IRQ_MODE_INVALID;
#ifdef CPTCFG_IWLWAV_LEGACY_INT_RECOVERY_MON
  hw->need_legacy_int_recovery = FALSE;
#endif
  /* init logger sid in hw context with insmod param */
  if ((loggersid[hw->card_idx] > MAX_UINT16) || (LOGGER_SID_INVALID == loggersid[hw->card_idx])) {
    ELOG_DD("Invalid logger_sid: %d for the card_idx: %d", loggersid[hw->card_idx], hw->card_idx);
    return MTLK_ERR_PARAMS;
  }
  hw->logger_sid = (uint16)loggersid[hw->card_idx];

  fast_path_supported = __mtlk_mmb_fastpath_supported(hw);
  hw->fast_path_is_available = (fast_path_supported && (fast_path == FASTPATH_ENABLED));
  hw->dcdp_path_is_available = ((fast_path == DCDP_SWPATH_ENABLED) || (hw->fast_path_is_available));
  ILOG0_DPPPDDD("card_idx:%d, mmb_base:0x%p, mmb_pas:0x%p, mmb_pas_phy:0x%p, fastpath supported:%d enabled:%d -> available:%d",
      hw->card_idx, hw->mmb_base, hw->mmb_pas, hw->mmb_pas_phy,
      fast_path_supported, fast_path, hw->fast_path_is_available);

  /* init radio mode configuration */
  if (MTLK_ERR_OK != _wave_hw_radio_band_cfg_init(hw))
    return MTLK_ERR_PARAMS;

  /* init dual pci flag*/
  _wave_hw_dual_pci_mode_set(hw, is_dual_pci);

  MTLK_INIT_TRY(hw_mmb_card, MTLK_OBJ_PTR(hw))
#if CPTCFG_IWLWAV_TRACER_HISTORY_LENGTH
    MTLK_INIT_STEP(hw_mmb_card, HW_TRACER_INIT, MTLK_OBJ_PTR(hw),
                      MTLK_TRACER_INIT, (hw));
    MTLK_TRACER_START(hw);
#endif
#if MTLK_USE_DIRECTCONNECT_DP_API
    MTLK_INIT_STEP_VOID(hw_mmb_card, HW_DCDP_INIT, MTLK_OBJ_PTR(hw),
                      mtlk_df_dcdp_datapath_dev_init, (hw, &hw->dp_dev));
#endif
    MTLK_INIT_STEP_VOID(hw_mmb_card, HW_DATA_HANDLERS_INIT, MTLK_OBJ_PTR(hw),
                      _mtlk_mmb_init_data_handlers, (hw));
    MTLK_INIT_STEP_EX(hw_mmb_card, HW_IRB_ALLOC, MTLK_OBJ_PTR(hw),
                      mtlk_irbd_alloc, (),
                      hw->irbd, hw->irbd != NULL, MTLK_ERR_NO_MEM);
    MTLK_INIT_STEP_EX(hw_mmb_card, HW_IRB_NAME, MTLK_OBJ_NONE,
                      mtlk_snprintf, (irb_node_name, sizeof(irb_node_name), "%s%d", MTLK_IRB_HW_NAME, hw->card_idx),
                      tmp, tmp > 0 && tmp < sizeof(irb_node_name), MTLK_ERR_NO_RESOURCES);
    MTLK_INIT_STEP(hw_mmb_card, HW_IRB_INIT, MTLK_OBJ_PTR(hw),
                      mtlk_irbd_init, (hw->irbd, mtlk_dfg_get_driver_irbd(), irb_node_name));

    /* Create WSS counters for both HW_SOURCE and HW_LISTENER, but open only for HW_SOURCE */
    MTLK_INIT_STEP_EX(hw_mmb_card, HW_WSS_CREATE, MTLK_OBJ_PTR(hw),
                      mtlk_wss_create, (mtlk_dfg_get_driver_wss(),
                      _mtlk_hw_card_wss_id_map, ARRAY_SIZE(_mtlk_hw_card_wss_id_map)),
                      hw->wss, hw->wss != NULL, MTLK_ERR_NO_MEM);
    MTLK_INIT_STEP(hw_mmb_card, HW_SOURCE_CNTRs, MTLK_OBJ_PTR(hw),
                   mtlk_wss_cntrs_open, (hw->wss, _mtlk_hw_card_wss_id_map,
                   hw->wss_hcntrs, MTLK_HW_SOURCE_CNT_LAST));

    MTLK_INIT_STEP_VOID(hw_mmb_card, HW_RX_DATA_LIST, MTLK_OBJ_PTR(hw),
                        mtlk_lslist_init, (&hw->rx.pending.lbufs));
    MTLK_INIT_STEP(hw_mmb_card, HW_REG_LOCK, MTLK_OBJ_PTR(hw),
                   mtlk_osal_lock_init, (&hw->reg_lock));
    MTLK_INIT_STEP(hw_mmb_card, HW_VERSION_LOCK, MTLK_OBJ_PTR(hw),
                   mtlk_osal_lock_init, (&hw->version_lock));
    MTLK_INIT_STEP(hw_mmb_card, HW_REQ_MC_LOCK, MTLK_OBJ_PTR(hw),
                   mtlk_osal_lock_init, (&hw->mc_group_lock));
    MTLK_INIT_STEP(hw_mmb_card, HW_INIT_EVT, MTLK_OBJ_PTR(hw),
                   MTLK_HW_INIT_EVT_INIT, (hw));
    MTLK_INIT_STEP(hw_mmb_card, HW_RX_PEND_TIMER, MTLK_OBJ_PTR(hw),
                   mtlk_osal_timer_init, (&hw->rx.pending.timer,
                                          _mtlk_mmb_on_rx_buffs_recovery_timer,
                                          HANDLE_T(hw)));
    MTLK_INIT_STEP_VOID(hw_mmb_card, HW_RX_TX_LIMITS, MTLK_OBJ_PTR(hw),
                   _mtlk_mmb_init_rxtx_limits, (hw));
#ifdef CPTCFG_IWLWAV_SET_PM_QOS
    MTLK_INIT_STEP(hw_mmb_card, HW_PM_QOS_INIT, MTLK_OBJ_PTR(hw),
                   _mtlk_mmb_pm_qos_init, (hw, cpu_dma_latency));
#endif
#ifdef CPTCFG_IWLWAV_LEGACY_INT_RECOVERY_MON
    MTLK_INIT_STEP(hw_mmb_card, HW_INT_REC_MON, MTLK_OBJ_PTR(hw),
                   mtlk_osal_timer_init, (&hw->recover_timer, _mtlk_hw_mmb_interrupt_recover_mon, HANDLE_T(hw)));
#endif
    _mtlk_mmb_reset_all_events(hw);

  MTLK_INIT_FINALLY(hw_mmb_card, MTLK_OBJ_PTR(hw));
    MTLK_CLEANUP_STEP(hw_mmb_card, HW_IRB_NAME, MTLK_OBJ_NONE,
                      MTLK_NOACTION, ());
  MTLK_INIT_RETURN(hw_mmb_card, MTLK_OBJ_PTR(hw), mtlk_hw_mmb_cleanup_card, (hw));
}

static void
_wave_hw_get_recovery_stats (mtlk_hw_t *hw, mtlk_wssa_drv_recovery_stats_t *stats)
{
  stats->FastRcvryProcessed = mtlk_wss_get_stat(hw->wss, MTLK_HW_CNT_NOF_FAST_RCVRY_PROCESSED);
  stats->FullRcvryProcessed = mtlk_wss_get_stat(hw->wss, MTLK_HW_CNT_NOF_FULL_RCVRY_PROCESSED);
  stats->FastRcvryFailed    = mtlk_wss_get_stat(hw->wss, MTLK_HW_CNT_NOF_FAST_RCVRY_FAILED);
  stats->FullRcvryFailed    = mtlk_wss_get_stat(hw->wss, MTLK_HW_CNT_NOF_FULL_RCVRY_FAILED);
}

void __MTLK_IFUNC
wave_hw_get_recovery_stats (mtlk_hw_t *hw, mtlk_wssa_drv_recovery_stats_t *stats)
{
  _wave_hw_get_recovery_stats(hw, stats);
}

static void
_wave_hw_get_txm_stats (mtlk_hw_t *hw, mtlk_wssa_drv_hw_txm_stats_t *stats)
{
  mtlk_txmm_stats_t txmm_stats;

  /* minimal statistic level */

  /* TXMM stats */
  mtlk_txmm_base_get_stats(&hw->txmm_base, &txmm_stats);
  stats->txmm_sent = txmm_stats.nof_sent;
  stats->txmm_cfmd = txmm_stats.nof_cfmed;
  stats->txmm_peak = txmm_stats.used_peak;

  /* TXDM stats */
  mtlk_txmm_base_get_stats(&hw->txdm_base, &txmm_stats);
  stats->txdm_sent = txmm_stats.nof_sent;
  stats->txdm_cfmd = txmm_stats.nof_cfmed;
  stats->txdm_peak = txmm_stats.used_peak;
};

void __MTLK_IFUNC
wave_hw_get_hw_stats (mtlk_hw_t *hw, mtlk_wssa_drv_hw_stats_t *hw_stats)
{
  _wave_hw_get_txm_stats(hw, &hw_stats->txm_stats);
  _wave_hw_get_recovery_stats(hw, &hw_stats->rcvry_stats);

  hw_stats->RadarsDetected = mtlk_osal_atomic_get(&hw->radars_detected);
}

#ifdef MTLK_LEGACY_STATISTICS
#if MTLK_MTIDL_HW_STAT_FULL
void __MTLK_IFUNC
wave_hw_get_debug_hw_stats(mtlk_hw_t* hw, mtlk_wssa_drv_debug_hw_stats_t* stats)
{
  stats->FreeTxMSDUs                = hw->tx.nof_free_bds;
  stats->TxMSDUsUsagePeak           = hw->tx.max_used_bds;
  stats->BISTCheckPassed            = hw->mmb->bist_passed;

  stats->BssMgmtTxMaxBds            = hw->bss_mgmt.tx_bdr.basic.nof_bds;
  stats->BssMgmtTxFreeBds           = mtlk_dlist_size(&hw->bss_mgmt.tx_bdr.free_list);
  stats->BssMgmtTxUsagePeak         = hw->bss_mgmt.tx_bdr_max_used_bds;

  stats->BssMgmtTxResMaxBds         = hw->bss_mgmt.tx_res_bdr.basic.nof_bds;
  stats->BssMgmtTxResFreeBds        = mtlk_dlist_size(&hw->bss_mgmt.tx_res_bdr.free_list);
  stats->BssMgmtTxResUsagePeak      = hw->bss_mgmt.tx_res_bdr_max_used_bds;

  /* WSS counters */
  stats->FWLoggerPacketsProcessed   = mtlk_wss_get_stat(hw->wss, MTLK_HW_SOURCE_CNT_FW_LOGGER_PACKETS_PROCESSED);
  stats->FWLoggerPacketsDropped     = mtlk_wss_get_stat(hw->wss, MTLK_HW_SOURCE_CNT_FW_LOGGER_PACKETS_DROPPED);
  stats->BssRxPacketsProcessed      = mtlk_wss_get_stat(hw->wss, MTLK_HW_SOURCE_CNT_BSS_RX_PACKETS_PROCESSED);
  stats->BssRxPacketsRejected       = mtlk_wss_get_stat(hw->wss, MTLK_HW_SOURCE_CNT_BSS_RX_PACKETS_REJECTED);

  stats->ISRsTotal                  = mtlk_wss_get_stat(hw->wss, MTLK_HW_SOURCE_CNT_ISRS_TOTAL);
  stats->ISRsForeign                = mtlk_wss_get_stat(hw->wss, MTLK_HW_SOURCE_CNT_ISRS_FOREIGN);
  stats->ISRsNotPending             = mtlk_wss_get_stat(hw->wss, MTLK_HW_SOURCE_CNT_ISRS_NOT_PENDING);
  stats->ISRsInit                   = mtlk_wss_get_stat(hw->wss, MTLK_HW_SOURCE_CNT_ISRS_INIT);
  stats->ISRsToDPC                  = mtlk_wss_get_stat(hw->wss, MTLK_HW_SOURCE_CNT_ISRS_TO_DPC);
  stats->PostISRDPCs                = mtlk_wss_get_stat(hw->wss, MTLK_HW_SOURCE_CNT_POST_ISR_DPCS);
  stats->LegIndReceived             = mtlk_wss_get_stat(hw->wss, MTLK_HW_SOURCE_CNT_LEGACY_IND_RECEIVED);

  stats->RxAllocFailed              = mtlk_wss_get_stat(hw->wss, MTLK_HW_SOURCE_CNT_RX_BUF_ALLOC_FAILED);
  stats->RxReAllocFailed            = mtlk_wss_get_stat(hw->wss, MTLK_HW_SOURCE_CNT_RX_BUF_REALLOC_FAILED);
  stats->RxReAllocated              = mtlk_wss_get_stat(hw->wss, MTLK_HW_SOURCE_CNT_RX_BUF_REALLOCATED);
}
#endif /* MTLK_MTIDL_HW_STAT_FULL */
#endif /* MTLK_LEGACY_STATISTICS */

/*--- Begin of HW RT-Logger specific ---*/

static int
_mtlk_mmb_rtlog_fw_add_stream (mtlk_hw_t *hw, void *data /* rtlog_fw_stream_cfg_t *fw_str_cfg */)
{
    int                     res = MTLK_ERR_OK;
    mtlk_txmm_msg_t         man_msg;
    mtlk_txmm_data_t       *man_entry = NULL;
    UMI_ADD_STREAM_REQ_t   *umi_params;
    rtlog_fw_stream_cfg_t  *fw_str_cfg = data;
    rtlog_hw_fifo_cfg_t     hw_fifo_cfg;
    int                     i;

    if (!fw_str_cfg->is_active) {
        return MTLK_ERR_OK;
    }

    ILOG0_DD("wlan_if %d, FwAddStream %d request", fw_str_cfg->wlan_if, fw_str_cfg->str_id);

    SLOG0(0, 0, rtlog_fw_stream_cfg_t, fw_str_cfg);

    man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, &hw->txmm, NULL);
    if (!man_entry){
      ELOG_V("Can't init data path parameters due to lack of MAN_MSG");
      res = MTLK_ERR_NO_RESOURCES;
      goto finish;
    }

    man_entry->id = UM_MAN_HW_LOGGER_ADD_STREAM_REQ;
    man_entry->payload_size = sizeof(*umi_params);

    umi_params = (UMI_ADD_STREAM_REQ_t*)man_entry->payload;
    umi_params->u16Status         = 0; /* clear status */
    umi_params->logVersion        = 0; /* fixed vaules */
    umi_params->be0               = 0;
    umi_params->be1               = 1;

    umi_params->loggerActiveMode  = fw_str_cfg->mode;
    umi_params->wlanIf            = fw_str_cfg->wlan_if >> 1; /* use card no */
    umi_params->streamId          = fw_str_cfg->str_id;

    umi_params->bufferThreshold   = HOST_TO_MAC16(fw_str_cfg->buf_thr);

    MTLK_STATIC_ASSERT(sizeof(uint32) == sizeof(umi_params->assignedFifosId));
    umi_params->assignedFifosId   = HOST_TO_MAC32(fw_str_cfg->fifos);

    umi_params->swPreCalcChecksum = HOST_TO_MAC32(fw_str_cfg->chk_sum);

    /* Get HW FIFO config */
    res = mtlk_rtlog_fw_get_hw_fifo(fw_str_cfg->wlan_if, &hw_fifo_cfg);
    if (MTLK_ERR_OK != res) {
        ELOG_V("Can't get HW FIFO logger config");
        goto finish;
    }

    MTLK_STATIC_ASSERT(IWLWAV_RTLOG_HW_FIFOS == ARRAY_SIZE(umi_params->hwModuleFifo));
    for (i = 0; i < IWLWAV_RTLOG_HW_FIFOS; i++) {
        umi_params->hwModuleFifo[i] = HOST_TO_MAC16(hw_fifo_cfg.config[i]);
    }

    /* UDP header already in proper endian */
    for (i = 0; i < IWLWAV_RTLOG_PCK_HDR_LEN_IN_WORDS; i++) {
        umi_params->udpHeader[i] = fw_str_cfg->pck_hdr.words[i];
    }

    mtlk_dump(1, umi_params, sizeof(*umi_params) - sizeof(umi_params->udpHeader), "dump of umi_params");
    mtlk_dump(1, umi_params->udpHeader, sizeof(umi_params->udpHeader), "dump of pck_header");

    res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);
    if (MTLK_ERR_OK != res || UMI_OK != MAC_TO_HOST16(umi_params->u16Status)) {
        ELOG_DD("FwAddStream failure: ret %d, status %d",
                res, MAC_TO_HOST16(umi_params->u16Status));

        res = MTLK_ERR_MAC; /* the same ret code for both errors */
    }

finish:
    if (NULL != man_entry) {
      mtlk_txmm_msg_cleanup(&man_msg);
    }

    return res;
}

static int
_mtlk_mmb_rtlog_fw_rem_stream (mtlk_hw_t *hw, void *data /* rtlog_fw_stream_rem_t *fw_str_cfg */)
{
    int                      res = MTLK_ERR_OK;
    mtlk_txmm_msg_t          man_msg;
    mtlk_txmm_data_t        *man_entry = NULL;
    UMI_REMOVE_STREAM_REQ_t *umi_params;
    rtlog_fw_stream_rem_t   *fw_str_cfg = data;

    ILOG0_DD("wlan_if %d, FwRemStream %d", hw->card_idx, fw_str_cfg->str_id);

    man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, &hw->txmm, NULL);
    if (!man_entry){
      ELOG_V("Can't init data path parameters due to lack of MAN_MSG");
      res = MTLK_ERR_NO_RESOURCES;
      goto finish;
    }

    man_entry->id = UM_MAN_HW_LOGGER_REMOVE_STREAM_REQ;
    man_entry->payload_size = sizeof(*umi_params);

    umi_params = (UMI_REMOVE_STREAM_REQ_t*)man_entry->payload;
    umi_params->u16Status         = 0;    /* clear status */
    umi_params->streamId          = fw_str_cfg->str_id;

    res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);
    if (MTLK_ERR_OK != res || UMI_OK != MAC_TO_HOST16(umi_params->u16Status)) {
        ELOG_DD("FwRemStream failure: ret %d, status %d",
                res, MAC_TO_HOST16(umi_params->u16Status));

        res = MTLK_ERR_MAC; /* the same ret code for both errors */
    }

finish:
    if (NULL != man_entry) {
      mtlk_txmm_msg_cleanup(&man_msg);
    }

    return res;
}

static int
_mtlk_mmb_rtlog_fw_add_filter (mtlk_hw_t *hw, void *data /* rtlog_fw_filter_cfg_t *fw_flt_cfg */)
{
    rtlog_fw_filter_cfg_t *fw_flt_cfg = data;

    if (!fw_flt_cfg->is_active) {
        ILOG2_DD("Card index %d, FilterType %d is not set", hw->card_idx, fw_flt_cfg->flt_type);
        return MTLK_ERR_OK;
    }

    ILOG0_DDDD("Card index %d, FilterType %d, LogLevel %d, ModBitmap 0x%02x",
        hw->card_idx, fw_flt_cfg->flt_type, fw_flt_cfg->log_lvl, fw_flt_cfg->bitmap);

    /* FIXME: is not implemented */
    WLOG_V("UMI_ADD_FIFO_FILTER is not implemented");

    return MTLK_ERR_OK;
}

static int
_mtlk_mmb_rtlog_fw_rem_filter (mtlk_hw_t *hw, void *data /* rtlog_fw_filter_rem_t *fw_flt_cfg */)
{
#ifndef CPTCFG_IWLWAV_SILENT
  rtlog_fw_filter_rem_t *fw_flt_cfg = data;
#endif

  ILOG0_DD("wlan_if %d, FilterType %d", hw->card_idx, fw_flt_cfg->flt_type);

  /* FIXME: is not implemented */
  WLOG_V("UMI_REM_FIFO_FILTER is not implemented");

  return MTLK_ERR_OK;
}

/* Note: Table entries should be sorted by rtlog_cfg_id_t */
struct set_rtlog_cfg_entry {
    int     (*func)(mtlk_hw_t *hw, void *buff);
    uint32  size;
};

static struct set_rtlog_cfg_entry     _set_rtlog_table[] = {
    { _mtlk_mmb_rtlog_fw_add_stream,  sizeof(rtlog_fw_stream_cfg_t) },
    { _mtlk_mmb_rtlog_fw_rem_stream,  sizeof(rtlog_fw_stream_rem_t) },
    { _mtlk_mmb_rtlog_fw_add_filter,  sizeof(rtlog_fw_filter_cfg_t) },
    { _mtlk_mmb_rtlog_fw_rem_filter,  sizeof(rtlog_fw_filter_rem_t) },
};

int __MTLK_IFUNC
hw_mmb_set_rtlog_cfg(mtlk_hw_t *hw, void *buff, uint32 size)
{
    rtlog_cfg_id_t               cfg_id;
    struct set_rtlog_cfg_entry  *entry;
    int res;

    MTLK_STATIC_ASSERT(IWLWAV_RTLOG_CFG_REQ_MAX == ARRAY_SIZE(_set_rtlog_table));

    MTLK_ASSERT(hw);
    MTLK_ASSERT(buff);
    MTLK_ASSERT(size >= sizeof(rtlog_cfg_id_t)); /* at least cfg_id */

    /* Config req ID as 1st word */
    cfg_id = *((rtlog_cfg_id_t *)buff);
    if (cfg_id < IWLWAV_RTLOG_CFG_REQ_MAX) {
        entry = &_set_rtlog_table[cfg_id];
        if (size == entry->size) {
            res = entry->func(hw, buff);
        } else {
            WLOG_DD("Incorrect data size %d (%d expected)", size, entry->size);
            res = MTLK_ERR_BUSY;
        }
    } else {
        WLOG_D("Invalid cfg_id %d", cfg_id);
        res = MTLK_ERR_BUSY;
    }

    return res;
}

static int
_mtlk_mmb_rtlog_fw_cfg_init (mtlk_hw_t* hw, rtlog_fw_cfg_t *fw_cfg)
{
    int i, res;

    for (i = 0; i < fw_cfg->max_nof_streams; i++) {
        res = _mtlk_mmb_rtlog_fw_add_stream(hw, &fw_cfg->stream_cfg[i]);
        if (MTLK_ERR_OK != res) {
            goto FINISH;
        }
    }

    for (i = 0; i < IWLWAV_RTLOG_FW_MAX_FILTER; i++) {
        res = _mtlk_mmb_rtlog_fw_add_filter(hw, &fw_cfg->filter_cfg[i]);
        if (MTLK_ERR_OK != res) {
            goto FINISH;
        }
    }

FINISH:
  return res;
}

/*--- End of HW RT-Logger specific ---*/

static void
_mtlk_mmb_data_path_unregister (mtlk_hw_t *hw)
{
}

static int
_mtlk_mmb_data_path_uninit (mtlk_hw_t *hw, BOOL is_recovery)
{
  int res = MTLK_ERR_OK;
#if MTLK_USE_DIRECTCONNECT_DP_API
  res = mtlk_df_dcdp_datapath_dev_unregister(hw, &hw->dp_dev, is_recovery);
#endif
  return res;
}

static int
_mtlk_mmb_data_path_register (mtlk_hw_t* hw /*, BOOL is_recovery*/)
{
  int res = MTLK_ERR_OK;

#if MTLK_USE_DIRECTCONNECT_DP_API
  MTLK_STATIC_ASSERT(WAVE_DCDP_MAX_NDEVS == WAVE_DCDP_MAX_PORTS);
  MTLK_STATIC_ASSERT(WAVE_DCDP_MAX_NDEVS == WAVE_CARD_RADIO_NUM_MAX);

#if 0
  if (is_recovery && (hw->dp_dev.dp_mode == MTLK_DC_DP_MODE_LITEPATH_ONLY)) {
    /* In case of recovery AND LITEPATH_ONLY mode do not re-register DC DP device */
    goto finish;
  }
#endif

  {
    mtlk_dcdp_init_info_t dp_init;
    dp_init.rd_pool_size = hw->chi_area.sBasic.sRdPOOL.u32NumRdsInPool;
    dp_init.tx_ring_size = _mtlk_get_tx_ring_size(hw);
    dp_init.rx_ring_size = _mtlk_get_rx_ring_size(hw);

#if defined(MTLK_PUMA_PLATFORM)
    if (__mtlk_mmb_cntr_big_endian_supported(hw)) {
      dp_init.cntr_mode = MTLK_DC_DP_CNTR_MODE_BIG_ENDIAN;
      /* RX_IN + TX_IN */
      dp_init.soc2dev_enq_phys_base     = (void *)(hw->mmb_pas_phy + hw->chi_area.sBasic.sTxRING.u32InCounterAddressBigEnd);
      dp_init.soc2dev_enq_base          = (void *)(hw->mmb_base    + hw->chi_area.sBasic.sTxRING.u32InCounterAddressBigEnd);
      dp_init.dev2soc_ret_enq_phys_base = (void *)(hw->mmb_pas_phy + hw->chi_area.sBasic.sRxRING.u32InCounterAddressBigEnd);
      dp_init.dev2soc_ret_enq_base      = (void *)(hw->mmb_base    + hw->chi_area.sBasic.sRxRING.u32InCounterAddressBigEnd);
      /* RX_OUT + TX_OUT */
      dp_init.soc2dev_ret_deq_phys_base = (void *)(hw->mmb_pas_phy + hw->chi_area.sBasic.sTxRING.u32OutFreedCounterAddressBigEnd);
      dp_init.soc2dev_ret_deq_base      = (void *)(hw->mmb_base    + hw->chi_area.sBasic.sTxRING.u32OutFreedCounterAddressBigEnd);
      dp_init.dev2soc_deq_phys_base     = (void *)(hw->mmb_pas_phy + hw->chi_area.sBasic.sRxRING.u32OutFreedCounterAddressBigEnd);
      dp_init.dev2soc_deq_base          = (void *)(hw->mmb_base    + hw->chi_area.sBasic.sRxRING.u32OutFreedCounterAddressBigEnd);
    } else
#endif
    {
      dp_init.cntr_mode = MTLK_DC_DP_CNTR_MODE_LITTLE_ENDIAN;
      /* RX_IN + TX_IN */
      dp_init.soc2dev_enq_phys_base     = (void *)(hw->mmb_pas_phy + hw->chi_area.sBasic.sTxRING.u32InCounterAddress);
      dp_init.soc2dev_enq_base          = (void *)(hw->mmb_base    + hw->chi_area.sBasic.sTxRING.u32InCounterAddress);
      dp_init.dev2soc_ret_enq_phys_base = (void *)(hw->mmb_pas_phy + hw->chi_area.sBasic.sRxRING.u32InCounterAddress);
      dp_init.dev2soc_ret_enq_base      = (void *)(hw->mmb_base    + hw->chi_area.sBasic.sRxRING.u32InCounterAddress);
      /* RX_OUT + TX_OUT */
      dp_init.soc2dev_ret_deq_phys_base = (void *)(hw->mmb_pas_phy + hw->chi_area.sBasic.sTxRING.u32OutCounterAddress);
      dp_init.soc2dev_ret_deq_base      = (void *)(hw->mmb_base    + hw->chi_area.sBasic.sTxRING.u32OutCounterAddress);
      dp_init.dev2soc_deq_phys_base     = (void *)(hw->mmb_pas_phy + hw->chi_area.sBasic.sRxRING.u32OutCounterAddress);
      dp_init.dev2soc_deq_base          = (void *)(hw->mmb_base    + hw->chi_area.sBasic.sRxRING.u32OutCounterAddress);
    }

    {
      /* Fill ring and HW counters addresses for Fragmentation WA.
       * Always setup Little-endian counters (on GRX750 this W/A is N/A) */
      dp_init.frag_ring_size              = hw->chi_area.sHdFragmentationRingExt.sData.u32FragmentationRingNumOfEntries;
      dp_init.dev2soc_frag_ring_phys_base = (void *)(hw->mmb_pas_phy + hw->chi_area.sHdFragmentationRingExt.sData.u32FragmentationRingAddress);
      dp_init.dev2soc_frag_ring_base      = (void *)(hw->mmb_base    + hw->chi_area.sHdFragmentationRingExt.sData.u32FragmentationRingAddress);
      dp_init.dev2soc_frag_deq_phys_base  = (void *)(hw->mmb_pas_phy + hw->chi_area.sBasic.sRxRING.u32OutCounterAddress);
      dp_init.dev2soc_frag_deq_base       = (void *)(hw->mmb_base    + hw->chi_area.sBasic.sRxRING.u32OutCounterAddress);
      dp_init.dev2soc_frag_enq_phys_base  = (void *)(hw->mmb_pas_phy + hw->chi_area.sBasic.sRxRING.u32OutStatusAddress);
      dp_init.dev2soc_frag_enq_base       = (void *)(hw->mmb_base    + hw->chi_area.sBasic.sRxRING.u32OutStatusAddress);
    }

    res = mtlk_df_dcdp_datapath_dev_register (hw, &hw->dp_dev, &dp_init /*, is_recovery*/);
  }
#endif

  return res;
}

#define GRX350_HW_HD_TXOUT_SIZE    (4*1)
#define GRX350_HW_HD_RXIN_SIZE     (4*1)
#define GRX350_HW_HD_SIZE          (4*4)
#define GRX750_HW_HD_SIZE          (4*4)
#define SW_HD_SIZE                 (sizeof(HOST_DSC))

/* 64-bit specific code */
#ifdef __LP64__
#define check_dma_addr(ptr) ___check_dma_addr(ptr, MTLK_SLID)

static void _mtlk_assert_dma(void *ptr, mtlk_slid_t slid)
{
    ELOG_P("Invalid DMA address: %p", ptr);
    __mtlk_assert(slid);
}

static __INLINE uint32 ___check_dma_addr (void *ptr, mtlk_slid_t slid)
{
    if ((((uint64)ptr) >> 32) != 0) {
        _mtlk_assert_dma(ptr, slid);
    }
    return (uint32)(uint64)(ptr);
}
#else
#define check_dma_addr(ptr) (uint32)(ptr)
#endif

static int
_mtlk_mmb_data_path_init (mtlk_hw_t* hw /*, BOOL is_recovery*/)
{
  int res;
  rtlog_fw_cfg_t     rtlog_fw_cfg;
  mtlk_txmm_msg_t     man_msg;
  mtlk_txmm_data_t   *man_entry = NULL;
  UMI_DATA_PATH_INIT_PARAMS *umi_params;
  uint32 val = HOST_MAGIC;
  mtlk_df_user_t         *df_user;
  mtlk_vap_manager_t     *vap_mgr;
#if MTLK_USE_DIRECTCONNECT_DP_API
  mtlk_dcdp_dev_t        *dp_dev = &hw->dp_dev;
#endif

  /*UNUSED_PARAM(is_recovery);*/
  MTLK_ASSERT(NULL != hw->ccr);

  vap_mgr = _hw_vap_manager_get(hw, 0); /* 1st radio */
  df_user = mtlk_df_get_user(mtlk_vap_manager_get_master_df(vap_mgr));

  /* Get HW RT-Logger */
  res = mtlk_rtlog_fw_get_config(mtlk_vap_manager_get_wlan_index(vap_mgr), &rtlog_fw_cfg);
  if (res != MTLK_ERR_OK) {
      ELOG_D("Can't get FW logger config, err %d", res);
      goto finish;
  }

#if MTLK_USE_DIRECTCONNECT_DP_API

#if 0
  if (is_recovery && (dp_dev->dp_mode == MTLK_DC_DP_MODE_LITEPATH_ONLY)) {
    /* In case of recovery AND LitePath mode do not uninitialize datapath */
    skip_datapath_init = TRUE;
  }
#endif

  /* Setup buffers only if DCDP path is available */
  if (mtlk_mmb_dcdp_path_available(hw) /* && !skip_datapath_init*/)
  {
      struct dc_dp_buf_pool  *entry;
      unsigned idx;

      /* initialize RDpool with buffers */
      MTLK_ASSERT(DC_DP_RING_NONE != dp_dev->dp_devspec[0].dc_tx_ring_used);
      MTLK_ASSERT(dp_dev->dp_resources.buflist);

      for (idx=0, entry=dp_dev->dp_resources.buflist; idx < dp_dev->dp_resources.num_bufpools; idx++, entry++)
      {
        uint32 dma_addr;
        mtlk_hw_data_ind_mirror_t *data_ind;
        data_ind = _mtlk_basic_bdr_get_mirror_bd(&hw->rx.bdr_data, idx, mtlk_hw_data_ind_mirror_t);
        data_ind->dma_addr = check_dma_addr(entry->phys_pool);
        data_ind->data     = (void*)entry->pool;
        data_ind->size     = entry->size;

        if (!entry->phys_pool || !entry->pool) {
          ELOG_PP("Buffer entry contains null address: entry->phys_pool=%p, entry->pool=%p", entry->phys_pool, entry->pool);
          mtlk_df_dcdp_release_buflist(dp_dev);
          res = MTLK_ERR_UNKNOWN;
          goto finish;
        }

        /* Mark memory for uncached access. We do not need to store result, as we already have physical address,
         * but we have to check mapping */
        dma_addr = mtlk_osal_map_to_phys_addr(mtlk_ccr_get_dev_ctx(hw->ccr), data_ind->data, data_ind->size, MTLK_DATA_FROM_DEVICE);
        if (!dma_addr) {
          ILOG2_P("WARNING: failed mapping 0x%p to physical address", data_ind->data);
          mtlk_df_dcdp_release_buflist(dp_dev);
          res = MTLK_ERR_NO_RESOURCES;
          goto finish;
        }
        _mtlk_mmb_set_rdpool_entry(hw, data_ind->dma_addr, idx);

  #if 0
        if (idx<5) {
          TRACE_PARAM_INT(idx);
          TRACE_PARAM_PTR(entry->phys_pool);
          TRACE_PARAM_PTR(entry->pool);
          TRACE_PARAM_INT(entry->size);
        }
  #endif
      }

      mtlk_df_dcdp_release_buflist(dp_dev);
  }
#endif

  man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, &hw->txmm, NULL);
  if (!man_entry){
    ELOG_V("Can't init data path parameters due to lack of MAN_MSG");
    res = MTLK_ERR_NO_RESOURCES;
    goto finish;
  }

  umi_params = (UMI_DATA_PATH_INIT_PARAMS*)man_entry->payload;
  man_entry->id = UM_MAN_DATA_PATH_INIT_REQ;
  man_entry->payload_size = sizeof(*umi_params);
  memset(man_entry->payload, 0, man_entry->payload_size);

  /* Setup rings */
#if MTLK_USE_DIRECTCONNECT_DP_API
  {
    umi_params->cbmFragmentationWaEnable = dp_dev->dp_frag_wa_enable;
    TRACE_PARAM_INT(umi_params->cbmFragmentationWaEnable);

    /* 3 MSBits of 4 bits DCDP port number */
    umi_params->ep3Msb = (0x0F & (dp_dev->dp_port_id[0])) >> 1;
    TRACE_PARAM_INT(umi_params->ep3Msb);
    ILOG0_DDD("card_idx:%d, DP_port_id:%d, ep3Msb:%d", hw->card_idx, dp_dev->dp_port_id[0], umi_params->ep3Msb);

    if (DC_DP_RING_HW_MODE1 == dp_dev->dp_devspec[0].dc_tx_ring_used) {
        /* GRX750 / PUMA7 */
        if (1*MTLK_DCDP_DCCNTR_SIZE == dp_dev->dp_resources.dccntr[0].dev2soc_enq_dccntr_len) {
            umi_params->rxOutReadyCounterAddress = HOST_TO_MAC32(check_dma_addr(dp_dev->dp_resources.dccntr[0].dev2soc_enq_phys_base));
            umi_params->txOutReadyCounterAddress = HOST_TO_MAC32(check_dma_addr(dp_dev->dp_resources.dccntr[0].soc2dev_ret_enq_phys_base));
            umi_params->rxInFreedCounterAddress  = HOST_TO_MAC32(check_dma_addr(dp_dev->dp_resources.dccntr[0].dev2soc_ret_deq_phys_base));
            umi_params->txInFreedCounterAddress  = HOST_TO_MAC32(check_dma_addr(dp_dev->dp_resources.dccntr[0].soc2dev_deq_phys_base));
        } else {
            ELOG_D("Unsupported size of UMT counters: %u", dp_dev->dp_resources.dccntr[0].dev2soc_enq_dccntr_len);
            res = MTLK_ERR_UNKNOWN;
            goto finish;
        }
    }

    switch (dp_dev->dp_devspec[0].dc_tx_ring_used) {
      case DC_DP_RING_HW_MODE0:
        /* In GRX350/550 mode only 3 rings */
        ILOG0_V("DCDP: setup HW_MODE0 rings");
        umi_params->rxInRingStartAddress  = 0;
        umi_params->rxInRingSizeBytes     = 0;
        umi_params->rxOutRingStartAddress = HOST_TO_MAC32(check_dma_addr(dp_dev->dp_resources.rings.dev2soc.phys_base));
        umi_params->rxOutRingSizeBytes    = HOST_TO_MAC32(        dp_dev->dp_resources.rings.dev2soc.size * GRX350_HW_HD_SIZE);
        umi_params->txInRingStartAddress  = HOST_TO_MAC32(check_dma_addr(dp_dev->dp_resources.rings.soc2dev.phys_base));
        umi_params->txInRingSizeBytes     = HOST_TO_MAC32(        dp_dev->dp_resources.rings.soc2dev.size * GRX350_HW_HD_SIZE);
        umi_params->txOutRingStartAddress = HOST_TO_MAC32(check_dma_addr(dp_dev->dp_resources.rings.soc2dev_ret.phys_base));
        umi_params->txOutRingSizeBytes    = HOST_TO_MAC32(        dp_dev->dp_resources.rings.soc2dev_ret.size * GRX350_HW_HD_TXOUT_SIZE);
        umi_params->dataPathMode = DATA_PATH_MODE_DC_MODE_0; /* GRX350/550 */
        break;
      case DC_DP_RING_HW_MODE1:
        /* GRX750 / PUMA7 */
        ILOG0_V("DCDP: setup HW_MODE1 rings");
        umi_params->rxInRingStartAddress  = HOST_TO_MAC32(check_dma_addr(dp_dev->dp_resources.rings.dev2soc_ret.phys_base));
        umi_params->rxInRingSizeBytes     = HOST_TO_MAC32(        dp_dev->dp_resources.rings.dev2soc_ret.size * GRX750_HW_HD_SIZE);
        umi_params->rxOutRingStartAddress = HOST_TO_MAC32(check_dma_addr(dp_dev->dp_resources.rings.dev2soc.phys_base));
        umi_params->rxOutRingSizeBytes    = HOST_TO_MAC32(        dp_dev->dp_resources.rings.dev2soc.size * GRX750_HW_HD_SIZE);
        umi_params->txInRingStartAddress  = HOST_TO_MAC32(check_dma_addr(dp_dev->dp_resources.rings.soc2dev.phys_base));
        umi_params->txInRingSizeBytes     = HOST_TO_MAC32(        dp_dev->dp_resources.rings.soc2dev.size * GRX750_HW_HD_SIZE);
        umi_params->txOutRingStartAddress = HOST_TO_MAC32(check_dma_addr(dp_dev->dp_resources.rings.soc2dev_ret.phys_base));
        umi_params->txOutRingSizeBytes    = HOST_TO_MAC32(        dp_dev->dp_resources.rings.soc2dev_ret.size * GRX750_HW_HD_SIZE);
        umi_params->dataPathMode = DATA_PATH_MODE_DC_MODE_1; /* GRX750/PUMA7 */
        break;
      case DC_DP_RING_SW_MODE1:      /* SWPath only, LitePath + SWPath */
        ILOG0_V("DCDP: setup SW_MODE1 rings");
        umi_params->rxInRingStartAddress  = HOST_TO_MAC32(check_dma_addr(dp_dev->dp_resources.rings.dev2soc_ret.phys_base));
        umi_params->rxInRingSizeBytes     = HOST_TO_MAC32(dp_dev->dp_resources.rings.dev2soc_ret.size * SW_HD_SIZE);
        umi_params->rxOutRingStartAddress = HOST_TO_MAC32(check_dma_addr(dp_dev->dp_resources.rings.dev2soc.phys_base));
        umi_params->rxOutRingSizeBytes    = HOST_TO_MAC32(dp_dev->dp_resources.rings.dev2soc.size * SW_HD_SIZE);
        umi_params->txInRingStartAddress  = HOST_TO_MAC32(check_dma_addr(dp_dev->dp_resources.rings.soc2dev.phys_base));
        umi_params->txInRingSizeBytes     = HOST_TO_MAC32(dp_dev->dp_resources.rings.soc2dev.size * SW_HD_SIZE);
        umi_params->txOutRingStartAddress = HOST_TO_MAC32(check_dma_addr(dp_dev->dp_resources.rings.soc2dev_ret.phys_base));
        umi_params->txOutRingSizeBytes    = HOST_TO_MAC32(dp_dev->dp_resources.rings.soc2dev_ret.size * SW_HD_SIZE);
        umi_params->dataPathMode = DATA_PATH_MODE_DC_NONE;
        break;
      case DC_DP_RING_NONE:
        /* LitePath Only. SW path will be supported completely by driver */
        ILOG0_V("DCDP: setup internal SW rings");
        umi_params->rxInRingStartAddress  = HOST_TO_MAC32(hw->rx.ring.dma_addr);
        umi_params->rxInRingSizeBytes     = HOST_TO_MAC32((uint32)(hw->rx.ring.size * SW_HD_SIZE));
        umi_params->rxOutRingStartAddress = HOST_TO_MAC32(hw->rx.ring.dma_addr);
        umi_params->rxOutRingSizeBytes    = HOST_TO_MAC32((uint32)(hw->rx.ring.size * SW_HD_SIZE));
        umi_params->txInRingStartAddress  = HOST_TO_MAC32(hw->tx.ring.dma_addr);
        umi_params->txInRingSizeBytes     = HOST_TO_MAC32((uint32)(hw->tx.ring.size * SW_HD_SIZE));
        umi_params->txOutRingStartAddress = HOST_TO_MAC32(hw->tx.ring.dma_addr);
        umi_params->txOutRingSizeBytes    = HOST_TO_MAC32((uint32)(hw->tx.ring.size * SW_HD_SIZE));
        umi_params->dataPathMode = DATA_PATH_MODE_DC_NONE;
        break;

      default:
        ELOG_D("Unsupported ring mode:%u", dp_dev->dp_devspec[0].dc_tx_ring_used);
        break;
    }
  }
#else
  {
    ILOG0_V("SW: setup internal SW rings");
    umi_params->rxInRingStartAddress  = HOST_TO_MAC32(hw->rx.ring.dma_addr);
    umi_params->rxInRingSizeBytes     = HOST_TO_MAC32((uint32)(hw->rx.ring.size * SW_HD_SIZE));
    umi_params->rxOutRingStartAddress = HOST_TO_MAC32(hw->rx.ring.dma_addr);
    umi_params->rxOutRingSizeBytes    = HOST_TO_MAC32((uint32)(hw->rx.ring.size * SW_HD_SIZE));
    umi_params->txInRingStartAddress  = HOST_TO_MAC32(hw->tx.ring.dma_addr);
    umi_params->txInRingSizeBytes     = HOST_TO_MAC32((uint32)(hw->tx.ring.size * SW_HD_SIZE));
    umi_params->txOutRingStartAddress = HOST_TO_MAC32(hw->tx.ring.dma_addr);
    umi_params->txOutRingSizeBytes    = HOST_TO_MAC32((uint32)(hw->tx.ring.size * SW_HD_SIZE));
    umi_params->dataPathMode          = DATA_PATH_MODE_DC_NONE;
    umi_params->cbmFragmentationWaEnable = FALSE;
  }
#endif

  umi_params->mangTxRingStartAddress = HOST_TO_MAC32(hw->bss_mgmt.tx_ring.dma_addr);
  umi_params->mangTxRingSizeBytes    = HOST_TO_MAC32((uint32)(hw->bss_mgmt.tx_ring.size * SW_HD_SIZE));
  umi_params->mangRxRingStartAddress = HOST_TO_MAC32(hw->bss_mgmt.rx_ring.dma_addr);
  umi_params->mangRxRingSizeBytes    = HOST_TO_MAC32((uint32)(hw->bss_mgmt.rx_ring.size * SW_HD_SIZE));

  umi_params->FWinterface            = hw->card_idx;
  umi_params->loggerMaxStreamNumber  = rtlog_fw_cfg.max_nof_streams;    /* old: fixed 1 */

  umi_params->hostEndianessMode      = (val == HOST_TO_MAC32(val)) ? HOST_ENDIANESS_MODE_LE :
                                                                     HOST_ENDIANESS_MODE_BE;
  umi_params->hdOwnBitValue          = WLAN_HOST_OWN_BIT_VALUE;
  umi_params->loggerRxSid            = HOST_TO_MAC16(hw->logger_sid);

  ILOG0_SDDD("%s: Initializing data path (card_idx:%d, rtlog max_nof_streams:%d, logger_sid:%d)",
      mtlk_df_user_get_name(df_user), hw->card_idx, (uint32)rtlog_fw_cfg.max_nof_streams, hw->logger_sid);

  ILOG2_V("umi_params:");
  TRACE_PARAM_MAC32(umi_params->txInRingStartAddress);
  TRACE_PARAM_MAC32(umi_params->txInRingSizeBytes);
  TRACE_PARAM_MAC32(umi_params->txOutRingStartAddress);
  TRACE_PARAM_MAC32(umi_params->txOutRingSizeBytes);
  TRACE_PARAM_MAC32(umi_params->rxInRingStartAddress);
  TRACE_PARAM_MAC32(umi_params->rxInRingSizeBytes);
  TRACE_PARAM_MAC32(umi_params->rxOutRingStartAddress);
  TRACE_PARAM_MAC32(umi_params->rxOutRingSizeBytes);
  TRACE_PARAM_MAC32(umi_params->mangTxRingStartAddress);
  TRACE_PARAM_MAC32(umi_params->mangTxRingSizeBytes);
  TRACE_PARAM_MAC32(umi_params->mangRxRingStartAddress);
  TRACE_PARAM_MAC32(umi_params->mangRxRingSizeBytes);
#if MTLK_USE_DIRECTCONNECT_DP_API
  TRACE_PARAM_MAC32(umi_params->txOutReadyCounterAddress);
  TRACE_PARAM_MAC32(umi_params->rxOutReadyCounterAddress);
  TRACE_PARAM_MAC32(umi_params->txInFreedCounterAddress);
  TRACE_PARAM_MAC32(umi_params->rxInFreedCounterAddress);
#endif
  TRACE_PARAM_INT   (umi_params->dataPathMode);
  TRACE_PARAM_INT   (umi_params->hostEndianessMode);
  TRACE_PARAM_INT   (umi_params->hdOwnBitValue);
  TRACE_PARAM_INT   (umi_params->FWinterface);
  TRACE_PARAM_INT   (umi_params->loggerMaxStreamNumber);

  /* Finally enable all interrupts and send first message */
  mtlk_ccr_enable_interrupts(hw->ccr, MTLK_IRQ_ALL);
#ifdef CPTCFG_IWLWAV_LEGACY_INT_RECOVERY_MON
  mtlk_osal_timer_set(&hw->recover_timer, MTLK_INTERRUPT_RECOVERY_PERIOD);
#endif

  res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_READY_CFM_TIMEOUT);
  if (MTLK_ERR_OK != res) {
    ELOG_D("Error initializing data path parameters, res = %d", res);
  }

 finish:
  if (NULL != man_entry) {
    mtlk_txmm_msg_cleanup(&man_msg);
  }

  /* HW RT-Logger config */
  if (MTLK_ERR_OK == res) {
    res = _mtlk_mmb_rtlog_fw_cfg_init(hw, &rtlog_fw_cfg);
  }

  return res;
}

static const mtlk_hw_vft_t hw_mmb_vft =
{
  _mtlk_hw_get_msg_to_send,
  _mtlk_hw_send_data,
  _mtlk_hw_release_msg_to_send,
  _mtlk_hw_set_prop,
  _mtlk_hw_get_prop,
  _mtlk_hw_load_file,
  _mtlk_hw_unload_file,
  _mtlk_hw_get_info
};

int __MTLK_IFUNC
mtlk_hw_mmb_reset_mac(mtlk_hw_t *hw)
{
  MTLK_ASSERT(hw != NULL);

  /* Don't control on FPGA */
  if (!__hw_mmb_card_is_fpga(hw)) {
    mtlk_ccr_reset_mac(hw->ccr);
  }

  return MTLK_ERR_OK;
}

int __MTLK_IFUNC
mtlk_hw_mmb_start_card(mtlk_hw_t *hw)
{
  MTLK_ASSERT(hw != NULL);
  /* MTLK_ASSERT(hw->vap_manager != NULL); */

  MTLK_START_TRY(hw_mmb_card, MTLK_OBJ_PTR(hw))

    /* Reset FW chip */
    _mtlk_mmb_cleanup_reset_mac(hw);

#ifdef CPTCFG_IWLWAV_PMCU_SUPPORT
    MTLK_START_STEP(hw_mmb_card, HW_PMCU_INIT_CLIENT, MTLK_OBJ_PTR(hw),
                    _mtlk_mmb_pmcu_register, (hw));
#endif
    MTLK_START_STEP_VOID(hw_mmb_card, HW_POWER_ON, MTLK_OBJ_PTR(hw),
                         _mtlk_mmb_power_on, (hw));
    MTLK_START_STEP_VOID(hw_mmb_card, HW_BIST_READ, MTLK_OBJ_PTR(hw),
                         mtlk_ccr_bist_efuse, (hw->ccr));
    hw->perform_isr_action = isr_action_init;
    MTLK_START_STEP(hw_mmb_card, HW_INIT_PLL, MTLK_OBJ_PTR(hw),
                    mtlk_ccr_init_pll, (hw->ccr));
    MTLK_START_STEP_VOID(hw_mmb_card, HW_CHI_INIT, MTLK_OBJ_PTR(hw),
                         _mtlk_mmb_chi_init, (hw));
    MTLK_START_STEP(hw_mmb_card, HW_LOAD_FIRMWARE, MTLK_OBJ_PTR(hw),
                    _mtlk_mmb_load_firmware, (hw));
    MTLK_START_STEP_VOID(hw_mmb_card, HW_RUN_FIRMWARE, MTLK_OBJ_PTR(hw),
                         _mtlk_mmb_run_firmware, (hw));
    MTLK_START_STEP(hw_mmb_card, HW_WAIT_CHI_MAGIC, MTLK_OBJ_PTR(hw),
                    _mtlk_mmb_wait_chi_magic, (hw));
    LOG_CHI_AREA(sFifoQ);
    LOG_CHI_AREA(sDAT);
    LOG_CHI_AREA(sMAN);
    LOG_CHI_AREA(sDBG);

    LOG_CHI_RING(sMangTxRING);
    LOG_CHI_RING(sMangRxRING);

    LOG_CHI_RING(sTxRING);
    LOG_CHI_RING(sRxRING);
    LOG_CHI_POOL(sRdPOOL);

    hw->bds.ind.offset = hw->chi_area.sBasic.sFifoQ.u32IndStartOffset;
    hw->bds.ind.size = (uint16)hw->chi_area.sBasic.sFifoQ.u32IndNumOfElements;
    hw->bds.ind.idx    = 0;

    hw->bds.req.offset = hw->chi_area.sBasic.sFifoQ.u32ReqStartOffset;
    hw->bds.req.size = (uint16)hw->chi_area.sBasic.sFifoQ.u32ReqNumOfElements;
    hw->bds.req.idx    = 0;

#if MTLK_DCDP_SEPARATE_REG
    MTLK_START_STEP(hw_mmb_card, HW_DATA_PATH_REGISTER, MTLK_OBJ_PTR(hw),
                    _mtlk_mmb_data_path_register, (hw));
#endif
    MTLK_START_STEP(hw_mmb_card, HW_MAN_REQ_BDR, MTLK_OBJ_PTR(hw),
                    _mtlk_mmb_prepare_man_req_bdr, (hw));
    MTLK_START_STEP(hw_mmb_card, HW_MAN_IND_BDR, MTLK_OBJ_PTR(hw),
                    _mtlk_mmb_prepare_man_ind_bdr, (hw));
    MTLK_START_STEP(hw_mmb_card, HW_DBG_REQ_BDR, MTLK_OBJ_PTR(hw),
                    _mtlk_mmb_prepare_dbg_req_bdr, (hw));
    MTLK_START_STEP(hw_mmb_card, HW_DBG_IND_BDR, MTLK_OBJ_PTR(hw),
                    _mtlk_mmb_prepare_dbg_ind_bdr, (hw));
    MTLK_START_STEP(hw_mmb_card, HW_DAT_REQ_BDR, MTLK_OBJ_PTR(hw),
                    _mtlk_mmb_prepare_data_req_bdr, (hw));
    MTLK_START_STEP(hw_mmb_card, HW_DAT_IND_BDR, MTLK_OBJ_PTR(hw),
                    _mtlk_mmb_prepare_data_ind_bdr, (hw));
    MTLK_START_STEP(hw_mmb_card, HW_BSS_MGMT, MTLK_OBJ_PTR(hw),
                    _mtlk_mmb_bss_mgmt_init, (hw));
    MTLK_START_STEP(hw_mmb_card, HW_RX_DATA_BUFFERS, MTLK_OBJ_PTR(hw),
                    _mtlk_mmb_prealloc_rx_data_buffers, (hw));
    MTLK_START_STEP(hw_mmb_card, HW_RX_MGMT_BUFFERS, MTLK_OBJ_PTR(hw),
                    _mtlk_mmb_prealloc_rx_mgmt_buffers, (hw));

#if 1 /* We need to read CHI area. BufSize will be checked inside */
    MTLK_START_STEP(
#else
    MTLK_START_STEP_IF(0 != hw->chi_area.sCalibr.sData.u32BufferRequestedSize,
#endif
                       hw_mmb_card, HW_CALIBRATION_CACHE, MTLK_OBJ_PTR(hw),
                       _mtlk_mmb_init_calibration_cache, (hw));
    MTLK_START_STEP_VOID_IF(0 != hw->chi_area.sMipsControl.sData.u32DescriptorLocation,
                            hw_mmb_card, HW_MIPS_CONTROL, MTLK_OBJ_PTR(hw),
                            _mtlk_mmb_init_mips_control, (hw));
    MTLK_START_STEP_IF(hw->fw_log_is_supported,
                       hw_mmb_card, HW_FW_LOGGER, MTLK_OBJ_PTR(hw),
                       _mtlk_mmb_init_fw_logger, (hw));
    ILOG2_V("HW layer preactivated");
  MTLK_START_FINALLY(hw_mmb_card, MTLK_OBJ_PTR(hw));
  MTLK_START_RETURN(hw_mmb_card, MTLK_OBJ_PTR(hw), mtlk_hw_mmb_stop_card, (hw));
}

int __MTLK_IFUNC
mtlk_hw_mmb_start_card_finalize(mtlk_hw_t *hw)
{
  MTLK_ASSERT(hw != NULL);
  /* MTLK_ASSERT(hw->vap_manager != NULL); */

  do {
      int mips_no;
      uint32 pas_offset;
      if (!hw->chi_area.sMipsControl.sData.u32DescriptorLocation)
        break;

      for (mips_no = 0; mips_no < hw_num_cores(hw); mips_no++) {
        pas_offset = hw->chi_area.sMipsControl.sData.u32DescriptorLocation + MTLK_OFFSET_OF(FW_CORE_CONTROL_DESCRIPTOR, u32FwCoreCtrl[mips_no]);
        ILOG0_DD(" mips_no:%d, pas_offset:0x%08X", mips_no, pas_offset);
      }
  } while(0);

  MTLK_START_TRY(hw_mmb_card_fin, MTLK_OBJ_PTR(&hw->hw_start_fin))

    MTLK_START_STEP(hw_mmb_card_fin, HW_TXMM, MTLK_OBJ_PTR(&hw->hw_start_fin),
                    mtlk_txmm_start, (&hw->txmm_base));

    hw->mac_reset_logic_initialized = TRUE;

    MTLK_START_STEP(hw_mmb_card_fin, HW_TXDM, MTLK_OBJ_PTR(&hw->hw_start_fin),
                    mtlk_txmm_start, (&hw->txdm_base));

    hw->state    = MTLK_HW_STATE_WAITING_READY;
    hw->perform_isr_action = isr_action_normal;

#if !MTLK_DCDP_SEPARATE_REG
    MTLK_START_STEP(hw_mmb_card_fin, HW_DATA_PATH_REGISTER, MTLK_OBJ_PTR(&hw->hw_start_fin),
                    _mtlk_mmb_data_path_register, (hw));
#endif

    MTLK_START_STEP(hw_mmb_card_fin, HW_DATA_PATH, MTLK_OBJ_PTR(&hw->hw_start_fin),
                    _mtlk_mmb_data_path_init, (hw));

    MTLK_START_STEP(hw_mmb_card_fin, HW_HW_DEP_CFG, MTLK_OBJ_PTR(&hw->hw_start_fin),
                    _mtlk_mmb_send_hw_dep_cfg, (hw));

    MTLK_START_STEP(hw_mmb_card_fin, HW_FW_LOG_SEVERITY, MTLK_OBJ_PTR(&hw->hw_start_fin),
                    _mtlk_mmb_send_fw_log_severity, (hw, LOGGER_SEVERITY_DEFAULT_LEVEL, UMI_CPU_ID_UM));

    /* Must be done after READY message since the recovery may
       send pseudo-responses for non-allocated messages.
       Such pseudo-responses sending is allowed after the MAC has finished
       its initialization (i.e. after READY CFM from driver's point of view).
     */
    MTLK_START_STEP(hw_mmb_card_fin, HW_RX_PEND_TIMER, MTLK_OBJ_PTR(&hw->hw_start_fin),
                    mtlk_osal_timer_set, (&hw->rx.pending.timer,
                                          MTLK_RX_BUFFS_RECOVERY_PERIOD));

#ifdef MTLK_LEGACY_STATISTICS
    MTLK_START_STEP(hw_mmb_card_fin, HW_MHI_STATISTIC, MTLK_OBJ_PTR(&hw->hw_start_fin),
                   _mtlk_hw_mhi_stats_init, (hw));

    MTLK_START_STEP(hw_mmb_card_fin, HW_PHY_RX_STATUS, MTLK_OBJ_PTR(&hw->hw_start_fin),
                   _hw_init_phy_rx_status, (hw));
#else
    MTLK_START_STEP(hw_mmb_card_fin, HW_STATISTICS, MTLK_OBJ_PTR(&hw->hw_start_fin),
                   _mtlk_hw_statistics_init, (hw));
#endif


    hw->state  = MTLK_HW_STATE_READY;
    ILOG2_V("HW layer activated");
  MTLK_START_FINALLY(hw_mmb_card_fin, MTLK_OBJ_PTR(&hw->hw_start_fin));
  MTLK_START_RETURN(hw_mmb_card_fin, MTLK_OBJ_PTR(&hw->hw_start_fin), mtlk_hw_mmb_stop_card_finalize, (hw));
}

/**************************************************************/

/*
 * Returns TRUE if BSS TX queue not empty
 */
static __INLINE
BOOL __wave_hw_mmb_bss_mgmt_tx_not_empty (struct mtlk_bss_management *bss_mgmt)
{
    uint32 nof_bds, nof_free_bds;
    /* max size of queue */
    nof_bds = bss_mgmt->tx_bdr.basic.nof_bds;
    /* free space in queue */
    nof_free_bds = mtlk_dlist_size(&bss_mgmt->tx_bdr.free_list);
    /* check if queue not empty */
    return (nof_bds != nof_free_bds);
}

static void _mtlk_hw_debug_print_ring_regs(mtlk_ring_regs *ring_regs, const char *reg_name);

static BOOL
_wave_hw_mmb_ring_queue_check (mtlk_hw_t *hw, mtlk_ring_buffer *ring, const char *name)
{
  uint32 hds_to_process = mtlk_ccr_ring_get_hds_to_process(hw->ccr, &ring->regs);

  /* check for PCI stall at first - hds_to_process equal to all FFs */
  if (MAX_UINT32 == hds_to_process) {
    ELOG_DS("CID-%02x: PCI stall on %s", hw->card_idx, name);
    _mtlk_hw_debug_print_ring_regs(&ring->regs, name);
    /* ignore unused rings */
    return (ring->size != 0);
  }

  return FALSE;
}

/* If last BSS TX confirmation was more than 5 s ago, reset FW */
#define BSS_TX_CFM_THRESHOLD_MS 5000u

/* Check the status of BSS TX queue and timestamp of last CFM */
static BOOL _wave_hw_mmb_bss_tx_heartbit (mtlk_hw_t *hw)
{
#if 0 /* FIXME: to be removed */
  uint32 ts;

  if (__wave_hw_mmb_bss_mgmt_tx_not_empty(&hw->bss_mgmt)) {
    ts = mtlk_osal_time_passed_ms(mtlk_osal_atomic_get(&hw->bss_mgmt.tx_ring_last_cfm_ts));
    if (ts > BSS_TX_CFM_THRESHOLD_MS) {
      ELOG_DD("CID-%02x: TX BSS last confirmation %u ms ago", hw->card_idx, ts);
      return TRUE;
    }
  }
  else {
      /* BSS TX queue is empty, reload the last CFM timestamp */
      mtlk_osal_atomic_set(&hw->bss_mgmt.tx_ring_last_cfm_ts, mtlk_osal_timestamp());
  }
#endif

  return FALSE;
}

BOOL __MTLK_IFUNC
wave_hw_mmb_all_rings_queue_check (mtlk_hw_t *hw)
{

  /* Always check all queues -- don't use logical OR */
  BOOL all_queues_check =
    (_wave_hw_mmb_ring_queue_check(hw, &hw->tx.ring,          "TX DAT") |
     _wave_hw_mmb_ring_queue_check(hw, &hw->rx.ring,          "RX DAT") |
     _wave_hw_mmb_ring_queue_check(hw, &hw->bss_mgmt.rx_ring, "RX BSS") |
     _wave_hw_mmb_ring_queue_check(hw, &hw->bss_mgmt.tx_ring, "TX BSS"));

  if (all_queues_check)
    return TRUE;

  return _wave_hw_mmb_bss_tx_heartbit(hw);
}

/**************************************************************
 * TX MAN MSG module wrapper
 **************************************************************/
#define CM_REQ_MIRROR_BY_MSG_OBJ(pmsg)                                  \
  MTLK_CONTAINER_OF(pmsg, mtlk_hw_cm_req_mirror_t, msg_hdr)

static void
_txm_send (mtlk_hw_t *hw, BOOL is_man, PMSG_OBJ pmsg, mtlk_vap_handle_t vap_handle)
{
  mtlk_hw_cm_req_mirror_t *req_obj  = CM_REQ_MIRROR_BY_MSG_OBJ(pmsg);
  void                    *msg_hdr  = &req_obj->msg_hdr;
  uint16                   msg_id   = MSG_OBJ_GET_ID(msg_hdr);
  mtlk_mmb_basic_bdr_t    *bbdr     = _mtlk_mmb_cm_get_req_bbdr(hw, is_man);
  uint32                   iom_size = _mtlk_mmb_cm_bdr_get_iom_bd_req_size(is_man);
  void                    *iom      = _mtlk_mmb_cm_bdr_get_iom_bd(bbdr, req_obj->hdr.index, iom_size);
  uint32                  descr;

  _mtlk_mmb_dbg_verify_msg_send(req_obj);

  /* Must do this in order to deal with MsgID endianess */
  MSG_OBJ_SET_ID(msg_hdr, HOST_TO_MAC16(msg_id));

  /* Tx MAN BD */
  _mtlk_mmb_memcpy_toio(iom, msg_hdr, MTLK_OFFSET_OF(UMI_MSG_HEADER,u32MessageRef));
  /* Do copy in two steps: skip the last word of the header, as byte #14 shouldn't be modifyed */
  _mtlk_mmb_memcpy_toio(((uint8 *)iom)+sizeof(UMI_MSG_HEADER), MSG_OBJ_PAYLOAD(msg_hdr), iom_size-sizeof(UMI_MSG_HEADER));

  MSG_OBJ_SET_ID(msg_hdr, msg_id);

  MSG_TO_DESCR(descr, (is_man?ARRAY_MAN_REQ:ARRAY_DBG_REQ), req_obj->hdr.index,
               wave_vap_radio_id_txmm_get(vap_handle),
               _mtlk_mmb_get_vap_id_from_vap_handle(vap_handle));

  MTLK_TRACER_ADD_ENTRY(hw, MTLK_TR_MSG_SEND_ID, msg_id);
  _mtlk_mmb_send_msg(hw, descr);
}

static void __MTLK_IFUNC
_txm_msg_timed_out(mtlk_handle_t usr_data, uint16 msg_id)
{
  int res;
  mtlk_hw_t* hw = HANDLE_T_PTR(mtlk_hw_t, usr_data);
  BOOL is_pending = FALSE;
  mtlk_vap_handle_t master_vap;


  WLOG_DD("CID-%02x: Resetting FW because of message timeout. Message ID is 0x%X",
          hw->card_idx, msg_id);

  WLOG_DD("bds.ind.idx:%u, bds.req.idx:%u\n", hw->bds.ind.idx,hw->bds.req.idx);
  mtlk_ccr_print_irq_regs(hw->ccr);
  mtlk_dump(0, hw->mmb_pas + hw->bds.ind.offset, hw->bds.ind.size * sizeof(uint32), "dump of Indication Queue");
  MTLK_TRACER_PRINT(hw);

  /* WAVE600: TODO: do it for the first radio now, but rework later! */
  if (MTLK_ERR_OK == mtlk_vap_manager_get_master_vap(wave_radio_descr_vap_manager_get(hw->radio_descr, 0), &master_vap)) {
    res = mtlk_vap_get_core_vft(master_vap)->get_prop(master_vap,
                                MTLK_CORE_PROP_IS_MAC_FATAL_PENDING,
                                &is_pending,
                                sizeof(is_pending));
    if (res != MTLK_ERR_OK) {
      is_pending = FALSE;
    }
  }

  if (!is_pending) {
    (void)mtlk_hw_set_prop(
          /* WAVE600: TODO: do it for the first radio now, but rework later! */
          wave_radio_descr_hw_api_get(hw->radio_descr, 0),
          MTLK_HW_RESET, NULL, 0);
  }
}

static PMSG_OBJ
_txm_msg_get_from_pool (mtlk_hw_t *hw, BOOL is_man)
{
  mtlk_mmb_advanced_bdr_t *abdr  = is_man?&hw->tx_man:&hw->tx_dbg;
  PMSG_OBJ                 pmsg  = NULL;

  mtlk_osal_lock_acquire(&abdr->lock);
  if (mtlk_dlist_size(&abdr->free_list))
  {
    mtlk_dlist_entry_t      *node = mtlk_dlist_pop_front(&abdr->free_list);
    mtlk_hw_cm_req_mirror_t *man_req;

    man_req = MTLK_LIST_GET_CONTAINING_RECORD(node,
                                              mtlk_hw_cm_req_mirror_t,
                                              hdr.list_entry);
    pmsg = &man_req->msg_hdr;
  }
  mtlk_osal_lock_release(&abdr->lock);

  return pmsg;
}

static void
_txm_msg_free_to_pool (mtlk_hw_t *hw, BOOL is_man, PMSG_OBJ pmsg)
{
  mtlk_mmb_advanced_bdr_t *abdr    = is_man?&hw->tx_man:&hw->tx_dbg;
  mtlk_hw_cm_req_mirror_t *req_obj = CM_REQ_MIRROR_BY_MSG_OBJ(pmsg);

  mtlk_osal_lock_acquire(&abdr->lock);
  mtlk_dlist_push_back(&abdr->free_list,
                       &req_obj->hdr.list_entry);
  mtlk_osal_lock_release(&abdr->lock);
}

static PMSG_OBJ __MTLK_IFUNC
_txmm_msg_get_from_pool (mtlk_handle_t usr_data)
{
  return _txm_msg_get_from_pool(HANDLE_T_PTR(mtlk_hw_t, usr_data), TRUE);
}

static void __MTLK_IFUNC
_txmm_msg_free_to_pool (mtlk_handle_t usr_data, PMSG_OBJ pmsg)
{
  _txm_msg_free_to_pool(HANDLE_T_PTR(mtlk_hw_t, usr_data), TRUE, pmsg);
}

static void __MTLK_IFUNC
_txmm_send (mtlk_handle_t usr_data, PMSG_OBJ pmsg, mtlk_vap_handle_t vap_handle)
{
  _txm_send(HANDLE_T_PTR(mtlk_hw_t, usr_data), TRUE, pmsg, vap_handle);
}

static PMSG_OBJ __MTLK_IFUNC
_txdm_msg_get_from_pool (mtlk_handle_t usr_data)
{
  return _txm_msg_get_from_pool(HANDLE_T_PTR(mtlk_hw_t, usr_data), FALSE);
}

static void __MTLK_IFUNC
_txdm_msg_free_to_pool (mtlk_handle_t usr_data, PMSG_OBJ pmsg)
{
  _txm_msg_free_to_pool(HANDLE_T_PTR(mtlk_hw_t, usr_data), FALSE, pmsg);
}

static void __MTLK_IFUNC
_txdm_send (mtlk_handle_t usr_data, PMSG_OBJ pmsg, mtlk_vap_handle_t vap_handle)
{
  _txm_send(HANDLE_T_PTR(mtlk_hw_t, usr_data), FALSE, pmsg, vap_handle);
}

static void
txmm_on_cfm (mtlk_hw_t *hw, PMSG_OBJ pmsg)
{
  mtlk_txmm_on_cfm(&hw->txmm_base, pmsg);
  mtlk_txmm_pump(&hw->txmm_base, pmsg);
}

static void
txdm_on_cfm(mtlk_hw_t *hw, PMSG_OBJ pmsg)
{
  mtlk_txmm_on_cfm(&hw->txdm_base, pmsg);
  mtlk_txmm_pump(&hw->txdm_base, pmsg);
}

/**************************************************************/

/**************************************************************
 * HW interface implementation for VFT
 **************************************************************/
static mtlk_hw_msg_t*
_mtlk_hw_get_msg_to_send (mtlk_hw_t *hw, mtlk_vap_handle_t vap_handle, uint32 *nof_free_tx_msgs)
{
  mtlk_hw_data_req_mirror_t *data_req;

  data_req = _mtlk_mmb_get_msg_from_data_pool(hw, vap_handle);

  if (nof_free_tx_msgs) {
    *nof_free_tx_msgs = (uint32)hw->tx.nof_free_bds;
  }

  return HW_MSG_PTR(data_req);
}

#define HIDE_PAYLOAD_TYPE_BUG

#ifdef HIDE_PAYLOAD_TYPE_BUG
/* WARNING: We suspect the PayloadType feature harms the throughput, so
 *          writing the last TXDAT_REQ_MSG_DESC's DWORD including
 *          u8RFMgmtData and u8PayloadType to Shared RAM is prohibited
 *          until the bug is fixed whether in driver or MAC.
 *          Once the bug is fixed, TXDATA_INFO_SIZE define could be
 *          removed from the code and _mtlk_mmb_memcpy_toio below can
 *          use sizeof(tx_bd) instead.
 */
#define TXDATA_INFO_SIZE MTLK_OFFSET_OF(TXDAT_REQ_MSG_DESC, u8RFMgmtData)
#else
#define TXDATA_INFO_SIZE sizeof(TXDAT_REQ_MSG_DESC)
#endif


void __MTLK_IFUNC
mtlk_mmb_print_tx_dat_ring_info (mtlk_hw_t *hw)
{
  ELOG_DDDPPDD("TX DAT ring: size %u, read ptr %u, write ptr %u, base %p, dma_addr %p, IN copy %u, int OUT counter %u",
              hw->tx.ring.size, hw->tx.ring.read_ptr, hw->tx.ring.write_ptr,
              hw->tx.ring.base_addr, (void*)(uintptr_t)hw->tx.ring.dma_addr,
              hw->tx.ring.regs.IN_copy, hw->tx.ring.regs.int_OUT_cntr);
  mtlk_dump(0, hw->tx.ring.base_addr, hw->tx.ring.size * sizeof(HOST_DSC), "TX DAT HD ring");
}

void __MTLK_IFUNC
mtlk_mmb_print_tx_bss_ring_info (mtlk_hw_t *hw)
{
  ELOG_DDDPPDD("TX BSS ring: size %u, read ptr %u, write ptr %u, base %p, dma_addr %p, IN copy %d, int OUT counter %d",
              hw->bss_mgmt.tx_ring.size, hw->bss_mgmt.tx_ring.read_ptr, hw->bss_mgmt.tx_ring.write_ptr,
              hw->bss_mgmt.tx_ring.base_addr, (void*)(uintptr_t)hw->bss_mgmt.tx_ring.dma_addr,
              hw->bss_mgmt.tx_ring.regs.IN_copy, hw->bss_mgmt.tx_ring.regs.int_OUT_cntr);
  mtlk_dump(0, hw->bss_mgmt.tx_ring.base_addr, hw->bss_mgmt.tx_ring.size * sizeof(HOST_DSC), "TX BSS HD ring");
}

static void
_mtlk_mmb_put_hd_to_ring(mtlk_hw_t *hw, HOST_DSC *pHD, mtlk_ring_buffer *ring)
{
  mtlk_handle_t lock_val;
  uint32 u32frameInfo3;

  MTLK_ASSERT(hw);
  MTLK_ASSERT(ring->write_ptr < ring->size);

  /* Change OWN bit */
  u32frameInfo3 = LE32_TO_CPU(pHD->u32frameInfo3);
  MTLK_BFIELD_SET(u32frameInfo3, TX_DATA_INFO_OWN_BIT, MTLK_OWNER_FW);
  pHD->u32frameInfo3 = CPU_TO_LE32(u32frameInfo3);

  lock_val = mtlk_osal_lock_acquire_irq(&hw->reg_lock);

  /* copy descriptor into ring */
  *(ring->base_addr + ring->write_ptr) = *pHD;
  mmiowb();

  while(__UNLIKELY(memcmp(pHD, ring->base_addr + ring->write_ptr, sizeof(HOST_DSC)))){
    mtlk_dump(0,ring->base_addr + ring->write_ptr, sizeof(HOST_DSC), "PHD");
    mtlk_dump(0,pHD, sizeof(HOST_DSC), "stored");
  }

  INC_WRAP_IDX(ring->write_ptr, ring->size);

  mtlk_ccr_ring_initiate_doorbell_interrupt(hw->ccr, &ring->regs);
  mtlk_osal_lock_release_irq(&hw->reg_lock, lock_val);
}

static int
_mtlk_hw_send_data(mtlk_hw_t *hw, mtlk_vap_handle_t vap_handle,
                   mtlk_hw_data_req_mirror_t *data_req)
{
  int       res = MTLK_ERR_OK;
  HOST_DSC  tx_bd;

#if (defined MTLK_DEBUG_IPERF_PAYLOAD_RX) || (defined MTLK_DEBUG_IPERF_PAYLOAD_TX)
  data_req->ts           = mtlk_osal_timestamp();
#endif
  data_req->vap_id = mtlk_vap_get_id(vap_handle);
  data_req->radio_id = wave_vap_radio_id_get(vap_handle);

  if (data_req->size != 0) { /* not a NULL-packet */
    data_req->dma_addr = mtlk_df_nbuf_map_to_phys_addr(mtlk_ccr_get_dev_ctx(hw->ccr),
                                    data_req->nbuf,
                                    data_req->size,
                                    MTLK_DATA_TO_DEVICE);
    if (!data_req->dma_addr) {
      ILOG2_P("WARNING: failed mapping 0x%p to physical address", data_req->nbuf);
      return MTLK_ERR_PKT_DROPPED;
    }
  }
  else {
    data_req->dma_addr = 0;
  }

  tx_bd.u32BdIndex         = CPU_TO_LE32(data_req->hdr.index);
  tx_bd.u32frameInfo3      = CPU_TO_LE32(MTLK_BFIELD_VALUE(TX_DATA_INFO_SOP_EOP, NO_FRAG, uint32) |
                             MTLK_BFIELD_VALUE(TX_DATA_INFO_OFFSET,
                                               data_req->dma_addr & 7,
                                               uint32) |
                             MTLK_BFIELD_VALUE(TX_DATA_INFO_LENGTH,
                                               data_req->size,
                                               uint32));

  tx_bd.u32HostPayloadAddr = CPU_TO_LE32(data_req->dma_addr & ~7);

  tx_bd.u32frameInfo0      = CPU_TO_LE32(MTLK_BFIELD_VALUE(TX_DATA_INFO_VAPID,
                                               mtlk_vap_get_id(vap_handle),
                                               uint32) |
                             MTLK_BFIELD_VALUE(TX_DATA_INFO_FRAMETYPE,
                                               data_req->frame_type,
                                               uint32) |
         ((data_req->mcf) ? (MTLK_BFIELD_VALUE(TX_DATA_INFO_MCIDX,
                                               data_req->mc_index,
                                               uint32) |
                             MTLK_BFIELD_VALUE(TX_DATA_INFO_MCF,
                                               1,
                                               uint32)) :
              /* unicast */
              ((DB_UNKNOWN_SID == data_req->sid) ?
                             /* SID unknown */
                             MTLK_BFIELD_VALUE(TX_DATA_INFO_STAID, 0, uint32) |
                             MTLK_BFIELD_VALUE(TX_DATA_INFO_MCF, 0, uint32) |
                             MTLK_BFIELD_VALUE(TX_DATA_INFO_UNKNOWN_SID, 1, uint32)
                            :
                             /* SID known */
                             MTLK_BFIELD_VALUE(TX_DATA_INFO_STAID, data_req->sid, uint32) |
                             MTLK_BFIELD_VALUE(TX_DATA_INFO_MCF, 0, uint32) |
                             MTLK_BFIELD_VALUE(TX_DATA_INFO_UNKNOWN_SID, 0, uint32))
                                         ));

  tx_bd.u32frameInfo1      = CPU_TO_LE32(MTLK_BFIELD_VALUE(TX_DATA_INFO_CLASS,
                                               data_req->tid,
                                               uint32) |
                             MTLK_BFIELD_VALUE(TX_DATA_INFO_EP,
                                               wave_vap_ep_bit_get(vap_handle),
                                               uint32));
  CPU_STAT_BEGIN_TRACK(CPU_STAT_ID_TX_HW);

  ILOG4_DP("Mapping %08x, data %p",
       (int)data_req->dma_addr,
       data_req->nbuf);

#if 0 /* Used for debug */
  if (data_req->size != 0) { /* not a NULL-packet */
    mtlk_dump(2, data_req->nbuf->data, data_req->size, "Sending NBUF");

    ILOG2_DD("HD data: size %d, datapointer %X", data_req->size, (uint32)data_req->dma_addr);
    ILOG2_D("index: %d", data_req->hdr.index);

    mtlk_dump(2, &tx_bd, sizeof(tx_bd), "HD contents");
  }
#endif

#ifdef BD_DBG
  if (__UNLIKELY(data_req->hdr.index >= hw->tx.ring.size)) {
    ELOG_D("BD index out of range, %d", data_req->hdr.index);
    mtlk_hw_set_prop(mtlk_vap_get_hwapi(vap_handle), MTLK_HW_RESET, NULL, 0);
    return MTLK_ERR_PKT_DROPPED;

  }
  if (__UNLIKELY(test_and_set_bit(data_req->hdr.index, hw->tx.used_bd_map))) {
    ELOG_D("attempt to reuse BD without releasing (on TX), index %d\n", data_req->hdr.index);
    mtlk_hw_set_prop(mtlk_vap_get_hwapi(vap_handle), MTLK_HW_RESET, NULL, 0);
    return MTLK_ERR_PKT_DROPPED;
  }
#endif

  _mtlk_mmb_put_hd_to_ring(hw, &tx_bd, &hw->tx.ring);
  CPU_STAT_END_TRACK(CPU_STAT_ID_TX_HW);
  return res;
}

static int
_mtlk_hw_release_msg_to_send (mtlk_hw_t *hw, mtlk_hw_msg_t *msg)
{
  mtlk_hw_data_req_mirror_t *data_req = DATA_REQ_MIRROR_PTR(msg);

  _mtlk_mmb_free_sent_msg_to_data_pool(hw, data_req);

  return MTLK_ERR_OK;
}

static void _mtlk_hw_mmb_version_printout(mtlk_hw_t *hw, char *buffer, uint32 size)
{
  mtlk_osal_lock_acquire(&hw->version_lock);

  mtlk_snprintf(buffer, size,
                "Driver version:\n%s\nMAC/PHY versions:\n%s%s",
                DRV_VERSION, hw->if_version, hw->progmodel);

  mtlk_osal_lock_release(&hw->version_lock);
}

static int
_mtlk_hw_set_prop (mtlk_hw_t *hw, mtlk_hw_prop_e prop_id, void *buffer, uint32 size)
{
  int res = MTLK_ERR_NOT_SUPPORTED;

  switch (prop_id)
  {
  case MTLK_HW_PROGMODEL:
    res = _mtlk_mmb_load_progmodel_to_hw(hw, buffer);
    break;
  case MTLK_HW_PROP_STATE:
    if (size == sizeof(mtlk_hw_state_e))
    {
      mtlk_hw_state_e *val = (mtlk_hw_state_e *)buffer;
      hw->state = *val;
      res       = MTLK_ERR_OK;
      if (mtlk_hw_is_halted(hw->state)) {
        mtlk_txmm_halt(&hw->txmm_base);
        mtlk_txmm_halt(&hw->txdm_base);

        mtlk_ccr_print_irq_regs(hw->ccr); /* fixme: should be removed later */
        MTLK_TRACER_PRINT(hw);
      }
    }
    break;
  case MTLK_HW_BCL_ON_EXCEPTION:
    if (size == sizeof(UMI_BCL_REQUEST))
    {
      UMI_BCL_REQUEST *preq = (UMI_BCL_REQUEST *)buffer;
      res = _mtlk_mmb_process_bcl(hw, preq, 0);
    }
    break;
  case MTLK_HW_RESET:
    {
      res = MTLK_ERR_NOT_READY;
      if (!mtlk_hw_is_halted(hw->state)) {
        res = wave_rcvry_task_initiate(hw);
        _mtlk_mmb_handle_sw_trap(hw);
      }
    }
    break;
  case MTLK_HW_DBG_ASSERT_ALL_MACS:
    res = _mtlk_mmb_cause_all_macs_assert(hw);
    break;
  case MTLK_HW_DBG_ASSERT_FW:
    if (buffer && size == sizeof(uint32))
    {
      uint32 *mips_no = (uint32 *)buffer;
      res = _mtlk_mmb_cause_mac_assert(hw, *mips_no);
    }
    break;

#if (IWLWAV_RTLOG_FLAGS & RTLF_REMOTE_ENABLED)
  case MTLK_HW_FW_LOG_BUFFER:
    if (buffer && size == sizeof(uint8))
    {
      if (*(uint8 *)buffer)
      {
        _mtlk_mmb_parse_fw_logger(hw);
      }
      else
      {
        ELOG_V("Driver doesn't support logger buffers extension");
        res = MTLK_ERR_PARAMS;
      }
    }
    else if (buffer && size == sizeof(mtlk_core_fw_log_buffer_t))
    {
      void* data;
      mtlk_core_fw_log_buffer_t* descr = (mtlk_core_fw_log_buffer_t*) buffer;

      data = mtlk_osal_mem_alloc(MTLK_IPAD4(descr->length), MTLK_MEM_TAG_FW_LOGGER);
      if(NULL != data)
      {
        _mtlk_mmb_pas_get(hw, "FW logger buffer", descr->addr, data, MTLK_IPAD4(descr->length));
        res = _mtlk_mmb_push_logger_buf(hw, data, descr->length);
        mtlk_osal_mem_free(data);
      }
      else
      {
        ELOG_V("Failed to process logger buffer due to lack of memory");
        res = MTLK_ERR_NO_MEM;
      }
    }
    break;
  case MTLK_HW_LOG:
    if (size >= sizeof(mtlk_log_event_t))
    {
      res = _mtlk_mmb_push_logger_buf(hw, buffer, size);
    }
    break;
#endif
  case MTLK_HW_MC_GROUP_ID:
    if (buffer && (size == sizeof(mtlk_core_ui_mc_grid_action_t)))
    {
      mtlk_core_ui_mc_grid_action_t  *req = (mtlk_core_ui_mc_grid_action_t *)buffer;
      mtlk_mc_addr_t *mc_addr = &req->mc_addr;
      int i, first_free = 0;

      if ((MTLK_GRID_DEL == req->action) && (MCAST_GROUP_UNDEF == req->grp_id)) {
        res = MTLK_ERR_OK;
      }

      ILOG1_DS("MC GRID Action: %d (%s)", req->action, (req->action == MTLK_GRID_ADD) ? "add group" : "del group");

      if (MTLK_IPv4 == req->mc_addr.type) {
        ILOG1_DD("IPv4:%B --> %B", htonl(req->mc_addr.src_ip.ip4_addr.s_addr), htonl(req->mc_addr.grp_ip.ip4_addr.s_addr));
      } else {
        ILOG1_KK("IPv6:%K --> %K", req->mc_addr.src_ip.ip6_addr.s6_addr, req->mc_addr.grp_ip.ip6_addr.s6_addr);
      }

      /* Sanity check */
      if ((req->grp_id >= MC_GROUPS) && (req->grp_id != MCAST_GROUP_UNDEF)) {
        ELOG_DD("Group ID (%u) exceeds range 0..%d", req->grp_id, MC_GROUPS-1);
        req->grp_id = MCAST_GROUP_UNDEF;
        res = MTLK_ERR_VALUE;
        break;
      }

      mtlk_osal_lock_acquire(&hw->mc_group_lock);

      for (i = MC_MIN_GROUP; i < MC_GROUPS; i++) {
        if (hw->mc_groups[i].ref_cnt) {
          if (0==memcmp(&hw->mc_groups[i].mc_addr, mc_addr, sizeof (mtlk_mc_addr_t))) {
            /* entry found */
            break;
          }
        }
        else {
          if (!first_free) {
            first_free = i;
          }
        }
      }

      if (req->grp_id != MCAST_GROUP_UNDEF) {
        if (i == MC_GROUPS) {
          first_free = req->grp_id;
        }
        else {
          if (i != req->grp_id) {
            ELOG_DD("Group ID %d not match with HW table GRID %d", req->grp_id, i);
          }
        }
      }

      if (req->action == MTLK_GRID_ADD) {
        /* Add a new MC group into table */
        if (i == MC_GROUPS) {
          if (!first_free) {
            ELOG_V("No space available for new group");
          } else {
            ILOG1_V("Group is not found, adding new");
            hw->mc_groups[first_free].mc_addr = *mc_addr;
            if (hw->mc_groups[first_free].ref_cnt) {
              ELOG_V("ERROR ref counter is not zero");
            }
            hw->mc_groups[first_free].ref_cnt++;
            req->grp_id = first_free;
          }
        } else {
           hw->mc_groups[i].ref_cnt++;
           ILOG1_D("Group is already available, increase ref counter:%d", hw->mc_groups[i].ref_cnt);
           req->grp_id = i;
        }
      } else {
        /* Remove MC group from table */
        if (i == MC_GROUPS) {
          ILOG1_V("Group is not found");
        } else {
          ILOG1_D("group ref counter:%d", hw->mc_groups[i].ref_cnt);
          if (0 == hw->mc_groups[i].ref_cnt)
          {
            ELOG_V("Multicast group refecence counter already is equal zero before decrement");
          }

          MTLK_ASSERT(hw->mc_groups[i].ref_cnt);

          hw->mc_groups[i].ref_cnt--;
          if (0 == hw->mc_groups[i].ref_cnt)
          {
            ILOG1_D("Clearing group %d", i);
            memset(&hw->mc_groups[i].mc_addr, 0, sizeof(mtlk_mc_addr_t));
            hw->mc_groups[i].mc_addr.type = MTLK_IP_NONE;
          }
          req->grp_id = i;
        }
      }

      mtlk_osal_lock_release(&hw->mc_group_lock);
      res = MTLK_ERR_OK;
    }
    break;

  case MTLK_HW_DATA_TXOUT_LIM:
    if (sizeof(hw->data_txout_lim) == size) {
      hw->data_txout_lim = *(unsigned *)buffer;
      res = MTLK_ERR_OK;
    }
    break;

  case MTLK_HW_DATA_RX_LIM:
    if (sizeof(hw->data_rx_lim) == size) {
      hw->data_rx_lim = *(unsigned *)buffer;
      res = MTLK_ERR_OK;
    }
    break;

  case MTLK_HW_BSS_RX_LIM:
    if (sizeof(hw->bss_rx_lim) == size) {
      hw->bss_rx_lim = *(unsigned *)buffer;
      res = MTLK_ERR_OK;
    }
    break;

  case MTLK_HW_BSS_CFM_LIM:
    if (sizeof(hw->bss_cfm_lim) == size) {
      hw->bss_cfm_lim = *(unsigned *)buffer;
      res = MTLK_ERR_OK;
    }
    break;

  case MTLK_HW_LEGACY_LIM:
    if (sizeof(hw->legacy_lim) == size) {
      hw->legacy_lim = *(unsigned *)buffer;
      res = MTLK_ERR_OK;
    }
    break;
#ifdef CPTCFG_IWLWAV_PMCU_SUPPORT
  case MTLK_HW_PROP_PMCU_FREQ:
    res = wv_PMCU_Switch_Freq(PMCU_SWITCH_TO_HIGH);
    break;
#endif /* CPTCFG_IWLWAV_PMCU_SUPPORT */

  default:
    break;
  }

  return res;
}

static int
_mtlk_hw_get_prop (mtlk_hw_t *hw, mtlk_hw_prop_e prop_id, void *buffer, uint32 size)
{
  int res = MTLK_ERR_NOT_SUPPORTED;

  MTLK_ASSERT(NULL != hw);

  switch (prop_id)
  {
  case MTLK_HW_PROP_STATE:
    if (size == sizeof(mtlk_hw_state_e))
    {
      mtlk_hw_state_e *val = (mtlk_hw_state_e *)buffer;
      *val = hw->state;
      res  = MTLK_ERR_OK;
    }
    break;
  case MTLK_HW_FREE_TX_MSGS:
    if (size == sizeof(uint32))
    {
      uint32 *val = (uint32 *)buffer;
      *val = hw->tx.nof_free_bds;
      res  = MTLK_ERR_OK;
    }
    break;
  case MTLK_HW_TX_MSGS_USED_PEAK:
    if (size == sizeof(uint32))
    {
      uint32 *val = (uint32 *)buffer;
      *val = hw->tx.max_used_bds;
      res  = MTLK_ERR_OK;
    }
    break;
  case MTLK_HW_DUMP:
    if (size == sizeof(mtlk_hw_dump_t))
    {
      mtlk_hw_dump_t *dump = (mtlk_hw_dump_t *)buffer;
      _mtlk_mmb_pas_get(hw, "dbg dump", dump->addr, dump->buffer, dump->size);
      res  = MTLK_ERR_OK;
    }
    break;
  case MTLK_HW_BCL_ON_EXCEPTION:
    if (size == sizeof(UMI_BCL_REQUEST))
    {
      UMI_BCL_REQUEST *preq = (UMI_BCL_REQUEST *)buffer;
      res = _mtlk_mmb_process_bcl(hw, preq, 1);
    }
    break;
  case MTLK_HW_BIST:
    if (size == sizeof(uint32))
    {
      uint32 *val = (uint32 *)buffer;

      MTLK_ASSERT(NULL != hw->mmb);

      *val = hw->mmb->bist_passed;
      res  = MTLK_ERR_OK;
    }
    break;
  case MTLK_HW_FW_BUFFERS_PROCESSED:
    if (size == sizeof(uint32))
    {
      *(uint32 *)buffer = mtlk_wss_get_stat(hw->wss, MTLK_HW_SOURCE_CNT_FW_LOGGER_PACKETS_PROCESSED);
      res  = MTLK_ERR_OK;
    }
    break;
  case MTLK_HW_FW_BUFFERS_DROPPED:
    if (size == sizeof(uint32))
    {
      *(uint32 *)buffer = mtlk_wss_get_stat(hw->wss, MTLK_HW_SOURCE_CNT_FW_LOGGER_PACKETS_DROPPED);
      res  = MTLK_ERR_OK;
    }
    break;
  case MTLK_HW_RADARS_DETECTED:
    if (size == sizeof(uint32))
    {
      *(uint32 *)buffer = mtlk_osal_atomic_get(&hw->radars_detected);
      res  = MTLK_ERR_OK;
    }
    break;
#ifdef CPTCFG_IWLWAV_LEGACY_INT_RECOVERY_MON
  case MTLK_HW_ISR_LOST_SUSPECT:
    if (size == sizeof(uint32))
    {
      *(uint32 *)buffer = mtlk_wss_get_stat(hw->wss, MTLK_HW_SOURCE_CNT_ISR_LOST_SUSPECT);
      res  = MTLK_ERR_OK;
    }
    break;
  case MTLK_HW_ISR_LOST_RECOVERED:
    if (size == sizeof(uint32))
    {
      *(uint32 *)buffer = mtlk_wss_get_stat(hw->wss, MTLK_HW_SOURCE_CNT_ISR_LOST_RECOVER);
      res  = MTLK_ERR_OK;
    }
    break;
#endif
  case MTLK_HW_IRBD:
    if (size != sizeof(mtlk_irbd_t**))
    {
      res = MTLK_ERR_PARAMS;
    }
    else
    {
      mtlk_irbd_t** irbd = (mtlk_irbd_t**)buffer;
      *irbd = hw->irbd;
      res  = MTLK_ERR_OK;
    }
    break;
  case MTLK_HW_WSS:
    if (size != sizeof(mtlk_wss_t**))
    {
      res = MTLK_ERR_PARAMS;
    }
    else
    {
      mtlk_wss_t** wss = (mtlk_wss_t**)buffer;
      *wss = hw->wss;
      res  = MTLK_ERR_OK;
    }
    break;
  case MTLK_HW_PROP_EEPROM_DATA:
    if (size != sizeof(mtlk_eeprom_data_t**))
    {
      res = MTLK_ERR_PARAMS;
    }
    else
    {
      mtlk_eeprom_data_t **ee_data = (mtlk_eeprom_data_t**)buffer;
      *ee_data = hw->ee_data;
      res  = MTLK_ERR_OK;
    }
    break;
#if (IWLWAV_RTLOG_FLAGS & RTLF_REMOTE_ENABLED)
  case MTLK_HW_FW_LOGGER_IS_EX:
    if (size != sizeof(uint8))
    {
      res = MTLK_ERR_PARAMS;
    }
    else
    {
      uint8 *val = (uint8 *)buffer;
      *val = hw->fw_log_is_supported_ex;
      res  = MTLK_ERR_OK;
    }
    break;
  case MTLK_HW_LOG_BUFFERS_CNT:
    if (size == sizeof(uint32))
    {
      *(uint32 *)buffer = hw->chi_area.sLoggerExt.sData.u32NumOfBufferDescriptors;
      res = MTLK_ERR_OK;
    }
    else
    {
      res = MTLK_ERR_PARAMS;
    }
    break;
#endif
  case MTLK_HW_PROP_CCR:
    if (size != sizeof(mtlk_ccr_t**))
    {
      res = MTLK_ERR_PARAMS;
    }
    else
    {
      mtlk_ccr_t **ccr = (mtlk_ccr_t**)buffer;
      *ccr = hw->ccr;
      res  = MTLK_ERR_OK;
    }
    break;
  case MTLK_HW_PROP_CARD_TYPE:
    if (size != sizeof(mtlk_card_type_t*))
    {
      res = MTLK_ERR_PARAMS;
    }
    else
    {
      mtlk_card_type_t *card_type = (mtlk_card_type_t*)buffer;

      MTLK_ASSERT(NULL != hw->ccr);

      *card_type = hw->ccr->hw_type;
      res  = MTLK_ERR_OK;
    }
    break;
  case MTLK_HW_PROP_CARD_TYPE_INFO:
    if (size != sizeof(mtlk_card_type_info_t*))
    {
      res = MTLK_ERR_PARAMS;
    }
    else
    {
      mtlk_card_type_info_t *card_type_info = (mtlk_card_type_info_t*)buffer;

      MTLK_ASSERT(NULL != hw->ccr);

      *card_type_info = mtlk_eeprom_get_nic_type(hw->ee_data);
      res  = MTLK_ERR_OK;
    }
    break;
  case MTLK_HW_PROP_CHIP_REVISION:
    if (size != sizeof(uint8))
    {
      res = MTLK_ERR_PARAMS;
    }
    else
    {
      uint8 *val = (uint8 *)buffer;
      *val = (uint8)mtlk_hw_get_chip_revision(hw);
      res  = MTLK_ERR_OK;
    }
    break;
  case MTLK_HW_PROP_CHIP_NAME:
    if (size != sizeof(char const **))
    {
      res = MTLK_ERR_PARAMS;
    }
    else
    {
      char const **name = (char const **)buffer;

      MTLK_ASSERT(NULL != hw->ccr);
      MTLK_ASSERT(NULL != hw->ccr->chip_info);

      *name = hw->ccr->chip_info->name;
      res  = MTLK_ERR_OK;
    }
    break;
  case MTLK_HW_EEPROM_NAME:
    if (0 == size)
    {
      res = MTLK_ERR_PARAMS;
    }
    else
    {
      char *fname = (char *)buffer;

      snprintf(fname, size, "cal_wlan%d.bin", card_idx_to_master_vap_idx (hw->card_idx));

      res  = MTLK_ERR_OK;

    }
    break;
  case MTLK_HW_VERSION_PRINTOUT:
    {
      _mtlk_hw_mmb_version_printout(hw, (char *)buffer, size);

      res  = MTLK_ERR_OK;
    }
    break;
  case MTLK_HW_PROP_TXMM_BASE:
    if (size == sizeof(mtlk_txmm_base_t **))
    {
      mtlk_txmm_base_t **txmm_base = (mtlk_txmm_base_t **)buffer;
      *txmm_base = &hw->txmm_base;
      res  = MTLK_ERR_OK;
    }
    break;
  case MTLK_HW_PROP_TXDM_BASE:
    if (size == sizeof(mtlk_txmm_base_t **))
    {
      mtlk_txmm_base_t **txdm_base = (mtlk_txmm_base_t **)buffer;
      *txdm_base = &hw->txdm_base;
      res  = MTLK_ERR_OK;
    }
    break;

#ifdef MTLK_DEBUG
  case MTLK_HW_FW_CFM_IN_TASKLET:
    if (size == sizeof(uint32))
    {
      uint32 *val = (uint32 *)buffer;
      *val = hw->tx.max_received_cfm;
      res  = MTLK_ERR_OK;
    }
    break;
  case MTLK_HW_FW_TX_TIME_INT_TO_TASKLET:
    if (size == sizeof(uint64))
    {
      uint64 *val = (uint64 *)buffer;
      *val = hw->tx.max_int_tasklet_time;
      res  = MTLK_ERR_OK;
    }
    break;
  case MTLK_HW_FW_TX_TIME_INT_TO_CFM:
    if (size == sizeof(uint64))
    {
      uint64 *val = (uint64 *)buffer;
      *val = hw->tx.max_int_processed_cfm;
      res  = MTLK_ERR_OK;
    }
    break;
  case MTLK_HW_FW_DATA_IN_TASKLET:
    if (size == sizeof(uint32))
    {
      uint32 *val = (uint32 *)buffer;
      *val = hw->rx.max_received_pckts;
      res  = MTLK_ERR_OK;
    }
    break;
  case MTLK_HW_FW_RX_TIME_INT_TO_TASKLET:
    if (size == sizeof(uint64))
    {
      uint64 *val = (uint64 *)buffer;
      *val = hw->rx.max_int_tasklet_time;
      res  = MTLK_ERR_OK;
    }
    break;
  case MTLK_HW_FW_RX_TIME_INT_TO_PCK:
    if (size == sizeof(uint64))
    {
      uint64 *val = (uint64 *)buffer;
      *val = hw->rx.max_int_processed_packet;
      res  = MTLK_ERR_OK;
    }
    break;
#endif /* MTLK_DEBUG */

  case MTLK_HW_CALIB_BUF_DMA_ADDR:
    if (size == sizeof(uint32))
    {
      uint32 *val = (uint32 *) buffer;

      *val = hw->calibr_dma_addr;
      res = MTLK_ERR_OK;
    }
    break;
  case MTLK_HW_MC_GROUP_ID:
    if (buffer && (size == sizeof(mtlk_core_ui_mc_grid_action_t)))
    {
      mtlk_core_ui_mc_grid_action_t *req = (mtlk_core_ui_mc_grid_action_t *) buffer;

      /* Sanity check */
      if (req->grp_id >= MC_GROUPS) {
        ELOG_DD("Group ID (%u) exceeds range 0..%d", req->grp_id, MC_GROUPS-1);
        req->grp_id = MCAST_GROUP_UNDEF;
        memset(&req->mc_addr, 0, sizeof(req->mc_addr));
        res = MTLK_ERR_VALUE;
      }
      else {
        req->mc_addr = hw->mc_groups[req->grp_id].mc_addr;
        res = MTLK_ERR_OK;
      }
    }
    break;
  case MTLK_HW_DATA_TXOUT_LIM:
    if (sizeof(hw->data_txout_lim) == size) {
      unsigned *val = (unsigned *) buffer;
      *val = hw->data_txout_lim;
      res = MTLK_ERR_OK;
    }
    break;
  case MTLK_HW_DATA_RX_LIM:
    if (sizeof(hw->data_rx_lim) == size) {
      unsigned *val = (unsigned *) buffer;
      *val = hw->data_rx_lim;
      res = MTLK_ERR_OK;
    }
    break;
  case MTLK_HW_BSS_RX_LIM:
    if (sizeof(hw->bss_rx_lim) == size) {
      unsigned *val = (unsigned *) buffer;
      *val = hw->bss_rx_lim;
      res = MTLK_ERR_OK;
    }
    break;
  case MTLK_HW_BSS_CFM_LIM:
    if (sizeof(hw->bss_cfm_lim) == size) {
      unsigned *val = (unsigned *) buffer;
      *val = hw->bss_cfm_lim;
      res = MTLK_ERR_OK;
    }
    break;
  case MTLK_HW_LEGACY_LIM:
    if (sizeof(hw->legacy_lim) == size) {
      unsigned *val = (unsigned *) buffer;
      *val = hw->legacy_lim;
      res = MTLK_ERR_OK;
    }
    break;

  case MTLK_HW_BSS_MGMT_MAX_MSGS:
    if (size == sizeof(uint32))
    {
      uint32 *val = (uint32 *)buffer;
      *val = hw->bss_mgmt.tx_bdr.basic.nof_bds;
      res  = MTLK_ERR_OK;
    }
    break;
  case MTLK_HW_BSS_MGMT_FREE_MSGS:
    if (size == sizeof(uint32))
    {
      uint32 *val = (uint32 *)buffer;
      *val = mtlk_dlist_size(&hw->bss_mgmt.tx_bdr.free_list);
      res  = MTLK_ERR_OK;
    }
    break;
  case MTLK_HW_BSS_MGMT_MSGS_USED_PEAK:
    if (size == sizeof(uint32))
    {
      uint32 *val = (uint32 *)buffer;
      *val = hw->bss_mgmt.tx_bdr_max_used_bds;
      res  = MTLK_ERR_OK;
    }
    break;
  case MTLK_HW_BSS_MGMT_MAX_RES_MSGS:
    if (size == sizeof(uint32))
    {
      uint32 *val = (uint32 *)buffer;
      *val = hw->bss_mgmt.tx_res_bdr.basic.nof_bds;
      res  = MTLK_ERR_OK;
    }
    break;
  case MTLK_HW_BSS_MGMT_FREE_RES_MSGS:
    if (size == sizeof(uint32))
    {
      uint32 *val = (uint32 *)buffer;
      *val = mtlk_dlist_size(&hw->bss_mgmt.tx_res_bdr.free_list);
      res  = MTLK_ERR_OK;
    }
    break;
  case MTLK_HW_BSS_MGMT_MSGS_RES_USED_PEAK:
    if (size == sizeof(uint32))
    {
      uint32 *val = (uint32 *)buffer;
      *val = hw->bss_mgmt.tx_res_bdr_max_used_bds;
      res  = MTLK_ERR_OK;
    }
    break;
#ifdef CPTCFG_IWLWAV_PMCU_SUPPORT
  case MTLK_HW_PROP_PMCU_FREQ:
    if (size == sizeof(BOOL))
    {
      *(BOOL *)buffer = wv_PMCU_Is_High();
      res  = MTLK_ERR_OK;
    }
    break;
#endif /* CPTCFG_IWLWAV_PMCU_SUPPORT */

  default:
    break;
  }

  return res;
}

static int
_mtlk_hw_load_file (mtlk_hw_t *hw, const char *name, mtlk_df_fw_file_buf_t *fb)
{
  MTLK_ASSERT(NULL != hw);

  return (mtlk_df_fw_load_file(mtlk_ccr_get_dev_ctx(hw->ccr), name, fb));
}

static void
_mtlk_hw_unload_file (mtlk_hw_t *hw, mtlk_df_fw_file_buf_t *fb)
{
  MTLK_ASSERT(NULL != hw);

  mtlk_df_fw_unload_file(mtlk_ccr_get_dev_ctx(hw->ccr), fb);
}

static int
_mtlk_hw_get_info (mtlk_hw_t *card, char *hw_info, uint32 size)
{
  int res = MTLK_ERR_OK;

  MTLK_ASSERT(NULL != card);

  if (0 > snprintf(hw_info, size, "Card[%d], MAC/PHY version: %s", card->card_idx, card->fw_version)) {
    res = MTLK_ERR_UNKNOWN;
  }

  return res;
}

static void
__mtlk_print_endianess (void)
{
  uint32 val;

  val = HOST_MAGIC;
  ILOG0_SDD("The system is %s endian (0x%08x, 0x%08x)",
            (val == HOST_TO_MAC32(val))?"Little":"Big", val, HOST_TO_MAC32(val));
}

static void
__mtlk_print_driver_version (void)
{
  /* display Driver version */
  ILOG0_S("Driver version: %s", DRV_VERSION);
}

/**************************************************************/

/**************************************************************
 * MMB interface implementation
 **************************************************************/

MTLK_INIT_STEPS_LIST_BEGIN(hw_mmb)
  MTLK_INIT_STEPS_LIST_ENTRY(hw_mmb, HW_MMB_LOCK)
MTLK_INIT_INNER_STEPS_BEGIN(hw_mmb)
MTLK_INIT_STEPS_LIST_END(hw_mmb);

int __MTLK_IFUNC
mtlk_mmb_txmm_init(mtlk_hw_t *hw, mtlk_hw_api_t *hw_api)
{
  int                  res = MTLK_ERR_UNKNOWN;
  mtlk_txmm_cfg_t      cfg;
  mtlk_txmm_wrap_api_t api;

  memset(&cfg, 0, sizeof(cfg));
  memset(&api, 0, sizeof(api));

  cfg.max_msgs          = HW_PCI_TXMM_MAX_MSGS;
  cfg.max_payload_size  = hw->mmb->cfg.man_req_msg_size;
  cfg.tmr_granularity   = HW_PCI_TXMM_GRANULARITY;

  api.usr_data          = HANDLE_T(hw);
  api.msg_get_from_pool = _txmm_msg_get_from_pool;
  api.msg_free_to_pool  = _txmm_msg_free_to_pool;
  api.msg_send          = _txmm_send;
  api.msg_timed_out     = _txm_msg_timed_out;

  hw->txmm.hw_api = hw_api;
  hw->txmm.base = &hw->txmm_base;
  /* WAVE600 TODO: hw uses TXMM as the master VAP of radio 0 user.
     That is fine unless the better soulution is found. */
  if (wave_radio_descr_master_vap_handle_get(hw->radio_descr, 0, &hw->txmm.vap_handle) != MTLK_ERR_OK) {
    ELOG_V("Can't get master VAP handle");
    return res;
  }

  res = mtlk_txmm_init(&hw->txmm_base, &cfg, &api);
  if (res != MTLK_ERR_OK) {
    ELOG_D("Can't init TXMM object (err=%d)", res);
    goto FINISH;
  }

  res = MTLK_ERR_OK;

FINISH:
  return res;
}

int __MTLK_IFUNC
mtlk_mmb_txdm_init(mtlk_hw_t *hw, mtlk_hw_api_t *hw_api)
{
  int                  res = MTLK_ERR_UNKNOWN;
  mtlk_txmm_cfg_t      cfg;
  mtlk_txmm_wrap_api_t api;

  memset(&cfg, 0, sizeof(cfg));
  memset(&api, 0, sizeof(api));

  cfg.max_msgs          = HW_PCI_TXDM_MAX_MSGS;
  cfg.max_payload_size  = hw->mmb->cfg.dbg_msg_size;
  cfg.tmr_granularity   = HW_PCI_TXDM_GRANULARITY;

  api.usr_data          = HANDLE_T(hw);
  api.msg_get_from_pool = _txdm_msg_get_from_pool;
  api.msg_free_to_pool  = _txdm_msg_free_to_pool;
  api.msg_send          = _txdm_send;
  api.msg_timed_out     = _txm_msg_timed_out;

  hw->txdm.hw_api = hw_api;
  hw->txdm.base = &hw->txdm_base;
  /* WAVE600 TODO: hw uses TXDM as the master VAP of radio 0 user.
     That is fine unless the better soulution is found. */
  if (wave_radio_descr_master_vap_handle_get(hw->radio_descr, 0, &hw->txdm.vap_handle) != MTLK_ERR_OK) {
    ELOG_V("Can't get master VAP handle");
    return res;
  }

  res = mtlk_txmm_init(&hw->txdm_base, &cfg, &api);
  if (res != MTLK_ERR_OK) {
    ELOG_D("Can't init TXDM object (err=%d)", res);
    goto FINISH;
  }

  res = MTLK_ERR_OK;

FINISH:
  return res;
}

int __MTLK_IFUNC
mtlk_hw_mmb_init (mtlk_hw_mmb_t *mmb, const mtlk_hw_mmb_cfg_t *cfg)
{
  MTLK_ASSERT(mmb != NULL);
  MTLK_ASSERT(cfg != NULL);
  MTLK_ASSERT(cfg->man_req_msg_size != 0);
  MTLK_ASSERT(cfg->man_ind_msg_size != 0);
  MTLK_ASSERT(cfg->dbg_msg_size != 0);
  MTLK_ASSERT((cfg->man_req_msg_size & 0x3) == 0);
  MTLK_ASSERT((cfg->man_ind_msg_size & 0x3) == 0);
  MTLK_ASSERT((cfg->dbg_msg_size & 0x3) == 0);

  memset(mmb, 0, sizeof(*mmb));
  mmb->cfg = *cfg;

#if MTLK_RX_BUFF_ALIGNMENT
  ILOG2_DD("HW requires Rx buffer alignment to %d (0x%02x)",
       MTLK_RX_BUFF_ALIGNMENT,
       MTLK_RX_BUFF_ALIGNMENT);
#endif

  __mtlk_print_endianess();
  __mtlk_print_driver_version();

  MTLK_INIT_TRY(hw_mmb, MTLK_OBJ_PTR(mmb))
    MTLK_INIT_STEP(hw_mmb, HW_MMB_LOCK, MTLK_OBJ_PTR(mmb),
                   mtlk_osal_lock_init, (&mmb->lock));
  MTLK_INIT_FINALLY(hw_mmb, MTLK_OBJ_PTR(mmb))
  MTLK_INIT_RETURN(hw_mmb, MTLK_OBJ_PTR(mmb),
                   mtlk_hw_mmb_cleanup, (mmb));
}

void __MTLK_IFUNC
mtlk_hw_mmb_cleanup (mtlk_hw_mmb_t *mmb)
{
  MTLK_CLEANUP_BEGIN(hw_mmb, MTLK_OBJ_PTR(mmb))
    MTLK_CLEANUP_STEP(hw_mmb, HW_MMB_LOCK, MTLK_OBJ_PTR(mmb),
                      mtlk_osal_lock_cleanup, (&mmb->lock));
  MTLK_CLEANUP_END(hw_mmb, MTLK_OBJ_PTR(mmb))

  memset(mmb, 0, sizeof(*mmb));
}

mtlk_txmm_base_t *__MTLK_IFUNC
mtlk_hw_mmb_get_txmm_base (mtlk_hw_t *hw)
{
  return &hw->txmm_base;
}

mtlk_txmm_base_t *__MTLK_IFUNC
mtlk_hw_mmb_get_txdm_base (mtlk_hw_t *hw)
{
  return &hw->txdm_base;
}

mtlk_txmm_t *__MTLK_IFUNC
mtlk_hw_mmb_get_txmm(mtlk_hw_t *hw)
{
  return &hw->txmm;
}

uint8 __MTLK_IFUNC
mtlk_hw_mmb_get_card_idx(mtlk_hw_t *hw)
{
  return hw->card_idx;
}

#if MTLK_USE_DIRECTCONNECT_DP_API
int32 __MTLK_IFUNC
mtlk_hw_mmb_get_dp_port_id(mtlk_hw_t *hw, int port_idx)
{
  MTLK_ASSERT(NULL != hw);
  MTLK_ASSERT(port_idx < WAVE_DCDP_MAX_PORTS);
  MTLK_ASSERT(TRUE == hw->dp_dev.is_registered[port_idx]);

  return hw->dp_dev.dp_port_id[port_idx];
}

mtlk_hw_dcdp_mode_t __MTLK_IFUNC
mtlk_hw_mmb_get_dp_mode (mtlk_hw_t *hw)
{
    return hw->dp_dev.dp_mode;
}
#endif

void __MTLK_IFUNC
mtlk_hw_mmb_stop_mac_events (mtlk_hw_t *hw)
{
  hw->mac_events_stopped = 1;
}

mtlk_hw_api_t * __MTLK_IFUNC
mtlk_hw_mmb_add_card (void)
{
  mtlk_hw_t     *hw     = NULL;
  mtlk_hw_api_t *hw_api = NULL;
  int           i       = 0;

  mtlk_osal_lock_acquire(&mtlk_mmb_obj.lock);

  if (mtlk_mmb_obj.nof_cards >= ARRAY_SIZE(mtlk_mmb_obj.cards)) {
    ELOG_D("Maximum %d boards supported", (int)ARRAY_SIZE(mtlk_mmb_obj.cards));
    goto FINISH;
  }

  hw_api = (mtlk_hw_api_t *)mtlk_osal_mem_alloc(sizeof(*hw_api), MTLK_MEM_TAG_HW);
  if (!hw_api) {
    ELOG_V("Can't allocate HW API object");
    goto FINISH;
  }

  hw = (mtlk_hw_t *)mtlk_osal_mem_alloc(sizeof(*hw), MTLK_MEM_TAG_HW);
  if (!hw) {
    ELOG_V("Can't allocate HW object");
    mtlk_osal_mem_free(hw_api);
    hw_api = NULL;
    goto FINISH;
  }

  memset(hw_api, 0, sizeof(*hw_api));
  memset(hw, 0, sizeof(*hw));

  hw->mmb = &mtlk_mmb_obj;

  for (i = 0; i < ARRAY_SIZE(mtlk_mmb_obj.cards); i++) {
    if (!mtlk_mmb_obj.cards[i]) {
      mtlk_mmb_obj.cards[i] = hw;
      mtlk_mmb_obj.cards[i]->card_idx = i;
      mtlk_mmb_obj.nof_cards++;
      break;
    }
  }

  hw_api->hw = hw;
  hw_api->vft = &hw_mmb_vft;

FINISH:
  mtlk_osal_lock_release(&mtlk_mmb_obj.lock);

  return hw_api;
}

void __MTLK_IFUNC
mtlk_hw_mmb_remove_card (mtlk_hw_api_t *hw_api)
{
  int i = 0;

  mtlk_osal_lock_acquire(&mtlk_mmb_obj.lock);
  for (i = 0; i < ARRAY_SIZE(mtlk_mmb_obj.cards); i++) {
    if (mtlk_mmb_obj.cards[i] == hw_api->hw) {
      mtlk_mmb_obj.cards[i] = NULL;
      mtlk_mmb_obj.nof_cards--;
      break;
    }
  }
  mtlk_osal_mem_free(hw_api->hw);
  mtlk_osal_mem_free(hw_api);
  mtlk_osal_lock_release(&mtlk_mmb_obj.lock);
}

wave_uint __MTLK_IFUNC
wave_hw_mmb_get_current_card_idx (void)
{
  return mtlk_mmb_obj.nof_cards; /* without lock */
}

void __MTLK_IFUNC
mtlk_hw_mmb_global_version_printout (mtlk_hw_mmb_t *mmb,
                                     char *buffer,
                                     uint32 size)
{
  int i;
  int bytes_left = size - 1;
  int bytes_printed;

  buffer[size - 1] = 0;
  bytes_printed = snprintf(buffer, bytes_left, "Driver version:\n%s\nMAC/PHY versions:\n", DRV_VERSION);
  if (bytes_printed < 0)
  {
    buffer[0] = 0;
    return;
  }
  bytes_left -= bytes_printed;
  buffer += bytes_printed;

  mtlk_osal_lock_acquire(&mmb->lock);

  for (i = 0; i < mmb->nof_cards; i++) {
    mtlk_hw_t *hw = mmb->cards[i];

    mtlk_osal_lock_acquire(&hw->version_lock);

    if (bytes_left > 0)
    {
      bytes_printed = snprintf(buffer, bytes_left, "%s%s", hw->if_version, hw->progmodel);
      if (bytes_printed < 0)
      {
        buffer[0] = 0;
        bytes_left = -1; /* Break loop, release locks */
        return;
      }
      else
      {
        bytes_left -= bytes_printed;
        buffer += bytes_printed;
      }
    }

    mtlk_osal_lock_release(&hw->version_lock);
  }

  mtlk_osal_lock_release(&mmb->lock);
}

#ifdef CPTCFG_IWLWAV_LEGACY_INT_RECOVERY_MON

#define MTLK_PCI_CONFIG_SPACE_SIZE  (256)
typedef uint8 pci_config_space_type; /* use pci_read_config_byte() for config space reading */

static uint32 __MTLK_IFUNC
_mtlk_hw_mmb_interrupt_recover_mon (mtlk_osal_timer_t *timer, mtlk_handle_t usr_data)
{
  mtlk_handle_t lock;
  mtlk_hw_t *hw = (mtlk_hw_t*)usr_data;
  BOOL recovered = FALSE;
  BOOL bus_stuck = FALSE;
  uint32 period = MTLK_INTERRUPT_RECOVERY_PERIOD;
  uint32 irq_status;
#ifdef CPTCFG_IWLWAV_BUS_PCI_PCIE
  pci_config_space_type pci_config_space[MTLK_PCI_CONFIG_SPACE_SIZE/sizeof(pci_config_space_type)];
  BOOL config_space_readed = FALSE;
#endif /* CPTCFG_IWLWAV_BUS_PCI_PCIE */

  MTLK_UNREFERENCED_PARAM(timer);

  MTLK_ASSERT(NULL != hw);
  MTLK_ASSERT(NULL != hw->ccr);

  lock = mtlk_osal_lock_acquire_irq(&hw->reg_lock);

  irq_status = mtlk_ccr_is_interrupt_pending(hw->ccr);
  if (irq_status == 0xFFFFFFFF ) {
    /* PCIE bus stuck is detected. Do not reschedule timer */
    bus_stuck = TRUE;
    period = 0;

  #if 0 /* #ifdef CPTCFG_IWLWAV_BUS_PCI_PCIE */ /* In 6.0.8 there is no way to get a pci_dev pointer */
    { /* Read out PCI config space */
      pci_config_space_type *pci_config_space_ptr = pci_config_space;
      mtlk_mmb_drv_t *mmb_drv = mtlk_vap_manager_get_mmb_drv(hw->vap_manager);
      struct pci_dev *pdev = mtlk_mmb_drv_get_pci_dev(mmb_drv);
      int offs;

      memset(pci_config_space, 0, sizeof(pci_config_space));
      if (pdev) {
        config_space_readed = TRUE;
        for (offs = 0; offs < MTLK_PCI_CONFIG_SPACE_SIZE; offs += sizeof(pci_config_space_type), pci_config_space_ptr++) {
          pci_read_config_byte(pdev, offs, pci_config_space_ptr);
        }
      }
    }
  #endif /* CPTCFG_IWLWAV_BUS_PCI_PCIE */
  } else if (irq_status && hw->irq_mode == MTLK_IRQ_MODE_LEGACY) {
    /* Possible Interrupts stuck is detected */
    if (hw->need_legacy_int_recovery) {
      /* Interrupts stuck is detected, try to recover */
      mtlk_ccr_recover_interrupts(hw->ccr);
      hw->need_legacy_int_recovery = FALSE;
      _mtlk_mmb_hw_inc_cnt(hw, MTLK_HW_SOURCE_CNT_ISR_LOST_RECOVER);
      recovered = TRUE;
    } else {
      /* Schedule recovery for next time to give chance for normal interrupt handling */
      hw->need_legacy_int_recovery = TRUE;
      _mtlk_mmb_hw_inc_cnt(hw, MTLK_HW_SOURCE_CNT_ISR_LOST_SUSPECT);
    }
  }

  mtlk_osal_lock_release_irq(&hw->reg_lock, lock);

  if (bus_stuck) {
    ILOG0_D("CID-%02x: PCIE BUS stuck detected, interrupt recovery monitor stopped", hw->card_idx);
#ifdef CPTCFG_IWLWAV_BUS_PCI_PCIE
    if (config_space_readed) {
      mtlk_dump(0, pci_config_space, sizeof(pci_config_space), "PCI config space:");
    } else {
      ELOG_D("CID-%02x: cannot read PCI config space", hw->card_idx);
    }
#endif
  }

  if (recovered) {
    ILOG0_D("CID-%02x: Recovered from lost interrupt", hw->card_idx);
  }

  return period;
}
#endif /* CPTCFG_IWLWAV_LEGACY_INT_RECOVERY_MON */

int __MTLK_IFUNC
mtlk_hw_mmb_interrupt_handler_msi (mtlk_irq_handler_data *ihd)
{
  mtlk_hw_t    *hw;
  MTLK_ASSERT(NULL != ihd);

  hw = ihd->hw;

  MTLK_ASSERT(NULL != hw);
  MTLK_ASSERT(NULL != hw->ccr);

  hw->perform_isr_action(hw, ihd);
  return MTLK_ERR_OK;
}

int __MTLK_IFUNC
mtlk_hw_mmb_interrupt_handler_legacy (mtlk_irq_handler_data *ihd)
{
  int ret = MTLK_ERR_OK;
  mtlk_handle_t lock;
  mtlk_hw_t    *hw;
  MTLK_ASSERT(NULL != ihd);

  hw = ihd->hw;

  MTLK_ASSERT(NULL != hw);
  MTLK_ASSERT(NULL != hw->ccr);

  lock = mtlk_osal_lock_acquire_irq(&hw->reg_lock);

  CPU_STAT_BEGIN_TRACK(CPU_STAT_ID_ISR);

  MTLK_TRACER_ADD_ENTRY(hw, MTLK_TR_INTR_ENTER, 0);
  do
  {
    _mtlk_mmb_hw_inc_cnt(hw, MTLK_HW_SOURCE_CNT_ISRS_TOTAL);

    if (!mtlk_ccr_disable_interrupts_if_pending(hw->ccr, ihd->irq_no))
    {
      _mtlk_mmb_hw_inc_cnt(hw, MTLK_HW_SOURCE_CNT_ISRS_FOREIGN);
      ret = MTLK_ERR_UNKNOWN; /* not our interrupt */
      break; /* do */
    }
    MTLK_TRACER_ADD_ENTRY(hw, MTLK_TR_INTR_DISABLE_INT, 0);

    MTLK_TRACER_ADD_ENTRY(hw, MTLK_TR_INTR_CLEAR_INT, ihd->irq_no);
    if (0 == (ihd->status = mtlk_ccr_clear_interrupts_if_pending(hw->ccr, ihd->irq_no)))
    {
      _mtlk_mmb_hw_inc_cnt(hw, MTLK_HW_SOURCE_CNT_ISRS_NOT_PENDING);
      mtlk_ccr_enable_interrupts(hw->ccr, MTLK_IRQ_ALL);
      MTLK_TRACER_ADD_ENTRY(hw, MTLK_TR_INTR_ENABLE_INT, MTLK_IRQ_ALL);
      break; /* do */
    }
    else {
#ifdef MTLK_DEBUG
      if (ihd->status & MTLK_IRQ_TXOUT) {
        mtlk_dbg_hres_ts(&hw->tx_interrupt_time);
      }

      if (ihd->status & MTLK_IRQ_RX) {
        mtlk_dbg_hres_ts(&hw->rx_interrupt_time);
      }
#endif
    }

    hw->perform_isr_action(hw, ihd);

  } while(0);

#ifdef CPTCFG_IWLWAV_LEGACY_INT_RECOVERY_MON
  if(ret == MTLK_ERR_OK) {
    hw->need_legacy_int_recovery = FALSE; /* we handled interrupt, no need to recover */
  }
#endif

  CPU_STAT_END_TRACK(CPU_STAT_ID_ISR);

  mtlk_osal_lock_release_irq(&hw->reg_lock, lock);

  MTLK_TRACER_ADD_ENTRY(hw, MTLK_TR_INTR_EXIT, ret);
  return ret;
}

/* TODO: make a CPU optimization in IRQ_ENABLE_OR_RESCHEDULE (return rescheduling of tasklet) */
#define IRQ_ENABLE_OR_RESCHEDULE(ihd){ \
    mtlk_handle_t lock_val = mtlk_osal_lock_acquire_irq(&ihd->hw->reg_lock); \
    mtlk_ccr_enable_interrupts(ihd->hw->ccr, ihd->irq_no); \
    MTLK_TRACER_ADD_ENTRY(ihd->hw, MTLK_TR_INTR_ENABLE_INT, ihd->irq_no); \
    mtlk_osal_lock_release_irq(&ihd->hw->reg_lock, lock_val); }

void __MTLK_IFUNC
mtlk_hw_mmb_deferred_handler(mtlk_irq_handler_data *ihd)
{
  _mtlk_mmb_hw_inc_cnt(ihd->hw, MTLK_HW_SOURCE_CNT_POST_ISR_DPCS);

  MTLK_TRACER_ADD_ENTRY(ihd->hw, MTLK_TR_TASKLET_START, 0);

#if HW_MMB_ALL_IRQ_PROCESSING /* processing all IRQ handlers */
  /* interrupt will be raised by counter update if not all data in rings are processed */
  ihd->hw->data_txout_process(ihd);
  ihd->hw->data_rx_process(ihd);
  bss_rx_process(ihd);
  bss_cfm_process(ihd);
  mailbox_rx_process(ihd);
#else
  if (ihd->status & MTLK_IRQ_TXOUT)   ihd->hw->data_txout_process(ihd);
  if (ihd->status & MTLK_IRQ_RX)      ihd->hw->data_rx_process(ihd);
  if (ihd->status & MTLK_IRQ_BSS_IND) bss_rx_process(ihd);
  if (ihd->status & MTLK_IRQ_BSS_CFM) bss_cfm_process(ihd);
  if (ihd->status & MTLK_IRQ_LEGACY)  mailbox_rx_process(ihd); //legacy_irq_process(ihd);
  else ihd->status = 0;
#endif

  IRQ_ENABLE_OR_RESCHEDULE(ihd);
  MTLK_TRACER_ADD_ENTRY(ihd->hw, MTLK_TR_TASKLET_STOP, 0);
}

void __MTLK_IFUNC
mtlk_hw_mmb_data_cfm_handler (mtlk_irq_handler_data *ihd)
{
  _mtlk_mmb_hw_inc_cnt(ihd->hw, MTLK_HW_SOURCE_CNT_POST_ISR_DPCS);
  ihd->hw->data_txout_process(ihd);
}

void __MTLK_IFUNC
mtlk_hw_mmb_data_rx_handler (mtlk_irq_handler_data *ihd)
{
  _mtlk_mmb_hw_inc_cnt(ihd->hw, MTLK_HW_SOURCE_CNT_POST_ISR_DPCS);
  ihd->hw->data_rx_process(ihd);
}

void __MTLK_IFUNC
mtlk_hw_mmb_bss_cfm_handler (mtlk_irq_handler_data *ihd)
{
  _mtlk_mmb_hw_inc_cnt(ihd->hw, MTLK_HW_SOURCE_CNT_POST_ISR_DPCS);
  bss_cfm_process(ihd);
}

void __MTLK_IFUNC
mtlk_hw_mmb_bss_rx_handler (mtlk_irq_handler_data *ihd)
{
  _mtlk_mmb_hw_inc_cnt(ihd->hw, MTLK_HW_SOURCE_CNT_POST_ISR_DPCS);
  bss_rx_process(ihd);
}

void __MTLK_IFUNC
mtlk_hw_mmb_mailbox_handler (mtlk_irq_handler_data *ihd)
{
  _mtlk_mmb_hw_inc_cnt(ihd->hw, MTLK_HW_SOURCE_CNT_POST_ISR_DPCS);
  mailbox_rx_process(ihd);
}

void __MTLK_IFUNC
mtlk_hw_mmb_bss_shared_handler (mtlk_irq_handler_data *ihd)
{
  _mtlk_mmb_hw_inc_cnt(ihd->hw, MTLK_HW_SOURCE_CNT_POST_ISR_DPCS);
  bss_rx_process(ihd);
  bss_cfm_process(ihd);
}

void __MTLK_IFUNC
mtlk_hw_mmb_legacy_irq_handler (mtlk_irq_handler_data *ihd)
{
  _mtlk_mmb_hw_inc_cnt(ihd->hw, MTLK_HW_SOURCE_CNT_POST_ISR_DPCS);
  legacy_irq_process(ihd);
}

void __MTLK_IFUNC
mtlk_hw_mmb_mailbox_legacy_shared_handler(mtlk_irq_handler_data *ihd)
{
  _mtlk_mmb_hw_inc_cnt(ihd->hw, MTLK_HW_SOURCE_CNT_POST_ISR_DPCS);
  mailbox_rx_process(ihd);
}

mtlk_ccr_t*  __MTLK_IFUNC
mtlk_hw_mmb_get_ccr (mtlk_hw_t *hw)
{
  MTLK_ASSERT(NULL != hw);
  return hw->ccr;
}

#ifdef CPTCFG_IWLWAV_TSF_TIMER_ACCESS_ENABLED
uint32 __MTLK_IFUNC
mtlk_hw_get_timestamp(mtlk_vap_handle_t vap_handle)
{
  mtlk_hw_t *hw = mtlk_vap_get_hw(vap_handle);

  uint32 low;
  uint32 high;

  MTLK_ASSERT(NULL != hw);
  MTLK_ASSERT(NULL != hw->ccr);

  /*  Bar1                                  */
  /*  equ mac_pac_tsf_timer_low 0x200738    */
  /*  equ mac_pac_tsf_timer_high 0x20073C   */

    mtlk_ccr_read_hw_timestamp(hw->ccr, &low, &high);

  return low;
}

/* Getting 2 timestamps: Logger and FW TSF */
/* 1. Linux VPE can be stopped by Voice FW for ~32 microseconds.
 *    This is possible even in case of Voice FW is not activated, i.e. only loaded.
 * 2. The Instruction cache misses influence to the log precision is up to 4 usec.
 *
 * In total we have up to 36 usec difference between the Driver and FW logs timestamps.
 */

void __MTLK_IFUNC
mtlk_hw_get_log_fw_timestamps(mtlk_vap_handle_t vap_handle, uint32 *t_log, uint32 *t_tsf)
{
    volatile uint32 t1, t2;
    mtlk_handle_t   lock_val;
    mtlk_hw_t      *hw = mtlk_vap_get_hw(vap_handle);
    int             i = 2;

    MTLK_ASSERT(NULL != hw);
    MTLK_ASSERT(NULL != hw->ccr);

    lock_val = mtlk_osal_lock_acquire_irq(&hw->reg_lock);

    /* This code will be executed twice to reduce the effect of Instruction cache misses */
    do {
        t1 = mtlk_log_get_timestamp();
        t2 = mtlk_hw_get_timestamp(vap_handle);
    } while (--i >= 0);

    *t_log = t1;
    *t_tsf = t2;

    mtlk_osal_lock_release_irq(&hw->reg_lock, lock_val);
}
#endif /* CPTCFG_IWLWAV_TSF_TIMER_ACCESS_ENABLED */

void __MTLK_IFUNC
mtlk_hw_mmb_dis_interrupts (mtlk_hw_t *hw)
{
    mtlk_handle_t lock_val;

#ifdef CPTCFG_IWLWAV_LEGACY_INT_RECOVERY_MON
    mtlk_osal_timer_cancel_sync(&hw->recover_timer);
#endif

  lock_val = mtlk_osal_lock_acquire_irq(&hw->reg_lock);
  mtlk_ccr_disable_interrupts(hw->ccr, MTLK_IRQ_ALL);
  mtlk_osal_lock_release_irq(&hw->reg_lock, lock_val);
}

void __MTLK_IFUNC
mtlk_hw_mmb_isolate (mtlk_hw_t *hw)
{
  _mtlk_mmb_stop_events_completely(hw);

  /* Stop MMB RX buffer recovery timer */
  mtlk_osal_timer_cancel_sync(&hw->rx.pending.timer);
  _mtlk_mmb_recover_rx_buffers(hw, MTLK_MAX_RX_BUFFS_TO_RECOVER);

  /* Stop Message sending */
  mtlk_txmm_stop(&hw->txmm_base);
  mtlk_txmm_halt(&hw->txmm_base);

  mtlk_txmm_stop(&hw->txdm_base);
  mtlk_txmm_halt(&hw->txdm_base);
}

static inline char *__hw_mmb_irq_mode_to_string (mtlk_irq_mode_e mode)
{
  switch (mode)
  {
    case MTLK_IRQ_MODE_MSI_8:
      return "MSI_8";
    case MTLK_IRQ_MODE_MSI_4:
      return "MSI_4";
    case MTLK_IRQ_MODE_MSI_1:
      return "MSI_1";
    case MTLK_IRQ_MODE_LEGACY:
      return "LEGACY";
    case MTLK_IRQ_MODE_INVALID:
      return "INVALID";
    default:
      ELOG_D("Unknown IRQ mode 0x%04X", (uint32)mode);
  }
  return "UNKNOWN";
}

static void mtlk_hw_mmb_restore_irq_mode(mtlk_hw_t *hw)
{
  ILOG1_S("Recovery IRQ MODE: %s", __hw_mmb_irq_mode_to_string(hw->irq_mode));
  MTLK_ASSERT(hw->irq_mode != MTLK_IRQ_MODE_INVALID);

  mtlk_hw_mmb_set_msi_intr_mode(hw, hw->irq_mode);
}

static inline void __hw_mmb_restore_all_subifs_stop (mtlk_hw_t *hw)
{
  uint32 i;
  wave_radio_descr_t *radio_descr = hw->radio_descr;
  uint32 num_radios = radio_descr->num_radios;

  for (i = 0; i < num_radios; i++) {
    mtlk_vap_manager_t  *vap_manager = wave_radio_descr_vap_manager_get(radio_descr, i);
    mtlk_vap_manager_rcvry_stop_all_subifs(vap_manager);
  }
}

static inline int __hw_mmb_restore_all_subifs_start (mtlk_hw_t *hw)
{
  uint32 i;
  wave_radio_descr_t *radio_descr = hw->radio_descr;
  uint32 num_radios = radio_descr->num_radios;
  int res = MTLK_ERR_OK;

  for (i = 0; i < num_radios; i++) {
    mtlk_vap_manager_t  *vap_manager = wave_radio_descr_vap_manager_get(radio_descr, i);
    res = mtlk_vap_manager_rcvry_start_all_subifs(vap_manager);
    if (MTLK_ERR_OK != res)
      break;
  }
  return res;
}

int __MTLK_IFUNC
mtlk_hw_mmb_restore (mtlk_hw_t *hw)
{
  int res = MTLK_ERR_OK;

  MTLK_ASSERT(hw != NULL);
  /* MTLK_ASSERT(hw->vap_manager != NULL); */
  MTLK_ASSERT(hw->radio_descr != NULL);

  hw->state = MTLK_HW_STATE_INITIATING;

  /* Reset FW and chip */
  _mtlk_mmb_reset_all_events(hw);
  mtlk_hw_mmb_reset_mac(hw);
  mtlk_hw_mmb_restore_irq_mode(hw);
  _mtlk_mmb_cleanup_reset_mac(hw);

  /* STOP */
  if (mtlk_mmb_dcdp_path_available(hw)) {
    __hw_mmb_restore_all_subifs_stop(hw);
  }
  _mtlk_mmb_data_path_uninit(hw, TRUE);
#if !MTLK_DCDP_SEPARATE_REG
  _mtlk_mmb_data_path_unregister(hw);
#endif
  _mtlk_mmb_cleanup_fw_logger(hw);
  _mtlk_mmb_empty_pending_rx_buffers(hw);
  _mtlk_mmb_free_preallocated_rx_data_buffers(hw);
  _mtlk_mmb_free_data_tx_buffers(hw);
  _mtlk_mmb_free_preallocated_rx_mgmt_buffers(hw);
  _mtlk_mmb_cleanup_dbg_ind_bdr(hw);
  _mtlk_mmb_cleanup_dbg_req_bdr(hw);
  _mtlk_mmb_cleanup_man_ind_bdr(hw);
  _mtlk_mmb_cleanup_man_req_bdr(hw);
#if MTLK_DCDP_SEPARATE_REG
  _mtlk_mmb_data_path_unregister(hw);
#endif
  /* Power ON chip */
  _mtlk_mmb_power_on(hw);

  mtlk_ccr_bist_efuse(hw->ccr);

  hw->perform_isr_action = isr_action_init;

  res = mtlk_ccr_init_pll(hw->ccr); /* both Gen5 and Gen6 */
  if (res != MTLK_ERR_OK) {
    ELOG_D("MMB_Restore HW_INIT_PLL failed(%d)", res);
    return res;
  }

  _mtlk_mmb_chi_init(hw);

  res = _mtlk_mmb_load_firmware(hw);
  if (res != MTLK_ERR_OK) {
    ELOG_D("MMB_Restore HW_LOAD_FIRMWARE failed(%d)", res);
    return res;
  }

  _mtlk_mmb_run_firmware(hw);

  res = _mtlk_mmb_wait_chi_magic(hw);
  if (res != MTLK_ERR_OK) {
    ELOG_D("MMB_Restore HW_WAIT_CHI_MAGIC failed(%d)", res);
    return res;
  }

  LOG_CHI_AREA(sFifoQ);
  LOG_CHI_AREA(sDAT);
  LOG_CHI_AREA(sMAN);
  LOG_CHI_AREA(sDBG);

  LOG_CHI_RING(sMangTxRING);
  LOG_CHI_RING(sMangRxRING);

  LOG_CHI_RING(sTxRING);
  LOG_CHI_RING(sRxRING);
  LOG_CHI_POOL(sRdPOOL);

  MTLK_ASSERT(hw->bds.ind.offset == hw->chi_area.sBasic.sFifoQ.u32IndStartOffset);
  MTLK_ASSERT(hw->bds.ind.size   == (uint16)hw->chi_area.sBasic.sFifoQ.u32IndNumOfElements);
  hw->bds.ind.idx = 0;

  MTLK_ASSERT(hw->bds.req.offset == hw->chi_area.sBasic.sFifoQ.u32ReqStartOffset);
  MTLK_ASSERT(hw->bds.req.size   == (uint16)hw->chi_area.sBasic.sFifoQ.u32ReqNumOfElements);
  hw->bds.req.idx = 0;

  /* Start */
#if MTLK_DCDP_SEPARATE_REG
  res = _mtlk_mmb_data_path_register(hw);
  if (res != MTLK_ERR_OK) {
    ELOG_D("MMB_Restore _mtlk_mmb_data_path_register failed(%d)", res);
    return res;
  }
#endif

  res = _mtlk_mmb_prepare_man_req_bdr(hw);
  if (res != MTLK_ERR_OK) {
    ELOG_D("MMB_Restore _mtlk_mmb_prepare_man_req_bdr failed(%d)", res);
    return res;
  }

  res = _mtlk_mmb_prepare_man_ind_bdr(hw);
  if (res != MTLK_ERR_OK) {
    ELOG_D("MMB_Restore _mtlk_mmb_prepare_man_ind_bdr failed(%d)", res);
    return res;
  }

  res = _mtlk_mmb_prepare_dbg_req_bdr(hw);
  if (res != MTLK_ERR_OK) {
    ELOG_D("MMB_Restore _mtlk_mmb_prepare_dbg_req_bdr failed(%d)", res);
    return res;
  }

  res = _mtlk_mmb_prepare_dbg_ind_bdr(hw);
  if (res != MTLK_ERR_OK) {
    ELOG_D("MMB_Restore _mtlk_mmb_prepare_dbg_ind_bdr failed(%d)", res);
    return res;
  }

  res = _mtlk_mmb_recover_data_req_bdr(hw);
  if (res != MTLK_ERR_OK) {
    ELOG_D("MMB_Restore _mtlk_mmb_prepare_data_req_bdr failed(%d)", res);
    return res;
  }

  res = _mtlk_mmb_recover_data_ind_bdr(hw);
  if (res != MTLK_ERR_OK) {
    ELOG_D("MMB_Restore _mtlk_mmb_prepare_data_ind_bdr failed(%d)", res);
    return res;
  }

  res = _mtlk_mmb_recover_bss_mgmt(hw);
  if (res != MTLK_ERR_OK) {
    ELOG_D("MMB_Restore ( failed(%d)", res);
    return res;
  }

  res = _mtlk_mmb_prealloc_rx_data_buffers(hw);
  if (res != MTLK_ERR_OK) {
    ELOG_D("MMB_Restore _mtlk_mmb_prealloc_rx_data_buffers failed(%d)", res);
    return res;
  }

  res = _mtlk_mmb_prealloc_rx_mgmt_buffers(hw);
  if (res != MTLK_ERR_OK) {
    ELOG_D("MMB_Restore _mtlk_mmb_prealloc_rx_mgmt_buffers failed(%d)", res);
    return res;
  }

  if (hw->chi_area.sMipsControl.sData.u32DescriptorLocation) {
    _mtlk_mmb_init_mips_control(hw);
  }

  if (hw->fw_log_is_supported) {
    res = _mtlk_mmb_init_fw_logger(hw);
    if (res != MTLK_ERR_OK) {
      ELOG_V("FW log buffers initialization failed");
      return res;
    }
  }

  /* Restore */
  res = _mtlk_mmb_txmm_restore(hw);
  if (res != MTLK_ERR_OK) {
    ELOG_D("MMB_Restore _mtlk_mmb_txmm_restore failed(%d)", res);
    return res;
  }
  res = _mtlk_mmb_txdm_restore(hw);
  if (res != MTLK_ERR_OK) {
    ELOG_D("MMB_Restore _mtlk_mmb_txdm_restore failed(%d)", res);
    return res;
  }

  hw->mac_reset_logic_initialized = TRUE;

  hw->state    = MTLK_HW_STATE_WAITING_READY;
  hw->perform_isr_action = isr_action_normal;

#if !MTLK_DCDP_SEPARATE_REG
  res = _mtlk_mmb_data_path_register(hw);
  if (res != MTLK_ERR_OK) {
    ELOG_D("MMB_Restore _mtlk_mmb_data_path_register failed(%d)", res);
    return res;
  }
#endif

  res = _mtlk_mmb_data_path_init(hw);
  if (res != MTLK_ERR_OK) {
    ELOG_D("MMB_Restore _mtlk_mmb_data_path_init failed(%d)", res);
    return res;
  }

  if (mtlk_mmb_dcdp_path_available(hw)) {
    res = __hw_mmb_restore_all_subifs_start(hw);
    if (res != MTLK_ERR_OK) {
      ELOG_D("MMB_Restore registering subinterfaces failed(%d)", res);
      return res;
    }
  }

  res = _mtlk_mmb_send_hw_dep_cfg(hw);
  if (res != MTLK_ERR_OK) {
    ELOG_D("MMB_Recovery failed(%d)", res);
    return res;
  }

  res = _mtlk_mmb_send_fw_log_severity(hw, LOGGER_SEVERITY_DEFAULT_LEVEL, UMI_CPU_ID_UM);
  if (res != MTLK_ERR_OK) {
    ELOG_D("MMB_Recovery failed(%d)", res);
    return res;
  }

  res = mtlk_osal_timer_set(&hw->rx.pending.timer, MTLK_RX_BUFFS_RECOVERY_PERIOD);
  if (res != MTLK_ERR_OK) {
    ELOG_D("MMB_Restore mtlk_osal_timer_set failed(%d)", res);
    return res;
  }

  hw->state = MTLK_HW_STATE_READY;

  ILOG2_V("HW layer restored!");
  return MTLK_ERR_OK;
}

static int
mtlk_mmb_prepare_bss_ind_bdr(mtlk_hw_t *hw, unsigned nof_bds)
{
  /* BSS Mgmt Indication BD initialization */
  int res;
  MTLK_ASSERT(nof_bds < ((uint32)-1));

  res = _mtlk_advanced_bdr_init(&hw->bss_mgmt.rx_bdr, (uint32) nof_bds,
                                hw->mmb_pas + hw->chi_area.sBasic.sBSS.u32ReqStartOffset,
                                sizeof(SHRAM_BSS_REQ_MSG), sizeof(struct mtlk_hw_bss_req_mirror));
  if (res != MTLK_ERR_OK)
    return res;

  hw->flags |= HW_FLAGS_PREPARED_MGMT_IND_BDR;

  return res;
}

static int
mtlk_mmb_prepare_bss_req_bdr(mtlk_hw_t *hw, unsigned nof_bds, unsigned num_res_bds)
{
  /* BSS Mgmt Request BD initialization */
  int res;
  MTLK_ASSERT(nof_bds < ((uint32)-1));

#ifdef BD_DBG
  hw->bss_mgmt.used_bd_map = mtlk_osal_mem_alloc((nof_bds + 7) >> 3, MTLK_MEM_TAG_HW);
  if (!hw->bss_mgmt.used_bd_map) {
    return MTLK_ERR_NO_MEM;
  }
  memset((void *)hw->bss_mgmt.used_bd_map, 0, (nof_bds + 7) >> 3);
#endif

  res = _mtlk_advanced_bdr_init(&hw->bss_mgmt.tx_bdr, (uint32) nof_bds,
                                hw->mmb_pas + hw->chi_area.sBasic.sBSS.u32ReqStartOffset,
                                sizeof(SHRAM_BSS_REQ_MSG), sizeof(struct mtlk_hw_bss_req_mirror));
  if (res != MTLK_ERR_OK)
    return res;

  hw->bss_mgmt.tx_bdr_max_used_bds = 0;
  hw->flags |= HW_FLAGS_PREPARED_BSS_REQ_BDR;

  res = _mtlk_advanced_bdr_init(&hw->bss_mgmt.tx_res_bdr, (uint32) num_res_bds,
                                /* iom_bdr address and size won't get used for this queue */
                                (uint8 *) 0x17171717, 0x1717, sizeof(struct mtlk_hw_bss_req_mirror));
  if (res != MTLK_ERR_OK)
    return res;

  hw->bss_mgmt.tx_res_bdr_max_used_bds = 0;
  return res;
}

static void
_mtlk_free_used_list(mtlk_mmb_advanced_bdr_t *abdr)
{
  mtlk_dlist_entry_t *entry;

  mtlk_osal_lock_acquire(&abdr->lock);

  while (NULL != (entry = mtlk_dlist_pop_front(&abdr->used_list))) {
    mtlk_dlist_push_back(&abdr->free_list, entry);
  }

  mtlk_osal_lock_release(&abdr->lock);
}

static void
mtlk_mmb_cleanup_bss_ind_bdr(mtlk_hw_t *hw)
{
  if (hw->flags & HW_FLAGS_PREPARED_MGMT_IND_BDR) {
    _mtlk_free_used_list(&hw->bss_mgmt.rx_bdr);
    _mtlk_advanced_bdr_cleanup(&hw->bss_mgmt.rx_bdr);
    hw->flags &= ~HW_FLAGS_PREPARED_MGMT_IND_BDR;
  }
}

static void
mtlk_mmb_cleanup_bss_req_bdr(mtlk_hw_t *hw)
{
  if (hw->flags & HW_FLAGS_PREPARED_BSS_REQ_BDR) {
    _mtlk_free_used_list(&hw->bss_mgmt.tx_bdr);
#ifdef BD_DBG
    if (hw->bss_mgmt.used_bd_map) mtlk_osal_mem_free((void *)hw->bss_mgmt.used_bd_map);
#endif
    _mtlk_advanced_bdr_cleanup(&hw->bss_mgmt.tx_bdr);
    _mtlk_free_used_list(&hw->bss_mgmt.tx_res_bdr);
    _mtlk_advanced_bdr_cleanup(&hw->bss_mgmt.tx_res_bdr);
    hw->flags &= ~HW_FLAGS_PREPARED_BSS_REQ_BDR;
  }
}

static int
mtlk_mmb_alloc_bss_tx_buffers(mtlk_hw_t *hw, mtlk_mmb_advanced_bdr_t *control, int count, uint16 bufsize)
{
  int i;

  ILOG2_DD("Allocate %d buffers of size %d", count, bufsize);

  for (i = 0; i < count; i++) {
    struct mtlk_hw_bss_req_mirror *bss_req =
      _mtlk_basic_bdr_get_mirror_bd(&control->basic, i, struct mtlk_hw_bss_req_mirror);
    bss_req->virt_addr = mtlk_osal_mem_dma_alloc(bufsize, MTLK_MEM_TAG_BSS_MGMT); /* alloc a buf */

    if (!bss_req->virt_addr)
      return MTLK_ERR_NO_MEM;

  }

  return MTLK_ERR_OK;
}

static int
mtlk_mmb_alloc_bss_rx_buffers(mtlk_hw_t *hw, mtlk_mmb_advanced_bdr_t *control, int count, uint16 bufsize)
{
  int i;

  ILOG2_DD("Allocate %d buffers of size %d", count, bufsize);

  for (i = 0; i < count; i++) {
    struct mtlk_hw_bss_ind_mirror *bss_ind =
      _mtlk_basic_bdr_get_mirror_bd(&control->basic, i, struct mtlk_hw_bss_ind_mirror);
    bss_ind->virt_addr = mtlk_osal_mem_dma_alloc(bufsize, MTLK_MEM_TAG_BSS_MGMT); /* alloc a buf */
    bss_ind->size = bufsize;
    if (!bss_ind->virt_addr)
      return MTLK_ERR_NO_MEM;
    ILOG3_DPP("#%d allocating at virt_addr:%p from bss_ind:%p", i, bss_ind->virt_addr, bss_ind);
  }

  return MTLK_ERR_OK;
}

static void
mtlk_mmb_free_bss_rx_buffers(mtlk_hw_t *hw, mtlk_mmb_advanced_bdr_t *control)
{
  int i;
  ILOG3_D("About to free %d pointers", control->basic.nof_bds);
  for (i = 0; i < control->basic.nof_bds; i++) {
    struct mtlk_hw_bss_ind_mirror *bss_ind =
      _mtlk_basic_bdr_get_mirror_bd(&control->basic, i, struct mtlk_hw_bss_ind_mirror);


    if (!bss_ind->virt_addr)
      continue;

    ILOG3_DPP("#%d freed virt_addr:%p from bss_ind:%p", i, bss_ind->virt_addr, bss_ind);
    mtlk_osal_mem_free(bss_ind->virt_addr);
    bss_ind->virt_addr = NULL;
  }
}

static void
mtlk_mmb_free_bss_tx_buffers(mtlk_hw_t *hw, mtlk_mmb_advanced_bdr_t *control)
{
  int i;

  for (i = 0; i < control->basic.nof_bds; i++) {
    struct mtlk_hw_bss_req_mirror *bss_req =
      _mtlk_basic_bdr_get_mirror_bd(&control->basic, i, struct mtlk_hw_bss_req_mirror);

    if (!bss_req->virt_addr)
      continue;

    mtlk_osal_mem_free(bss_req->virt_addr);
    bss_req->virt_addr = NULL;
  }
}

static int
_mtlk_mmb_alloc_bss_eapol_frag_buf (mtlk_bss_rx_eapol *rx_eapol, uint32 size)
{
  rx_eapol->data = mtlk_osal_mem_alloc(size, MTLK_MEM_TAG_BSS_MGMT);

  if (NULL == rx_eapol->data)
    return MTLK_ERR_NO_MEM;

  rx_eapol->buf_size = size;
  rx_eapol->data_size = 0;

  return MTLK_ERR_OK;
}

#define BSS_MGMT_NUM_RESERVE_FRAMES (136) /* two frames per 128 STA - DDRAm ring is already 120 */

int _mtlk_mmb_recover_bss_mgmt(mtlk_hw_t *hw)
{
  int res;
  uint32 ring_size;

  /* free used list */
  _mtlk_free_used_list(&hw->bss_mgmt.rx_bdr);

  /* BSS RX ring */
  ring_size = _mtlk_get_mgmt_rx_ring_size(hw);
  res = _mtlk_mmb_recover_ring_buffer(hw, &hw->bss_mgmt.rx_ring, &hw->chi_area.sBasic.sMangRxRING, ring_size, "RxMgmt");
  if (MTLK_ERR_OK != res) {
    return res;
  }

  /* BSS TX ring */
  _mtlk_free_used_list(&hw->bss_mgmt.tx_bdr);
  _mtlk_free_used_list(&hw->bss_mgmt.tx_res_bdr);
  hw->bss_mgmt.tx_bdr_max_used_bds = 0;
  hw->bss_mgmt.tx_res_bdr_max_used_bds = 0;
#ifdef BD_DBG
  memset((void *)hw->bss_mgmt.used_bd_map, 0, (_mtlk_get_mgmt_tx_ring_size(hw) + 7) >> 3);
#endif

  ring_size = _mtlk_get_mgmt_tx_ring_size(hw);
  res = _mtlk_mmb_recover_ring_buffer(hw, &hw->bss_mgmt.tx_ring, &hw->chi_area.sBasic.sMangTxRING, ring_size, "TxMgmt");
  if (MTLK_ERR_OK != res) {
      return res;
  }

  hw->bss_mgmt.rx_eapol.data_size = 0;

  return MTLK_ERR_OK;
}

int _mtlk_mmb_bss_mgmt_init(mtlk_hw_t *hw)
{
  int res;
  uint32 ring_size;

  /* BSS RX ring */
  ring_size = _mtlk_get_mgmt_rx_ring_size(hw);
  res = _mtlk_mmb_prepare_ring_buffer(hw, &hw->bss_mgmt.rx_ring, &hw->chi_area.sBasic.sMangRxRING, ring_size, "RxMgmt");
  if (MTLK_ERR_OK != res) {
    return res;
  }

  res = mtlk_mmb_prepare_bss_ind_bdr(hw, ring_size);

  if (MTLK_ERR_OK != res)
    goto clean_rx_ring;

  res = mtlk_mmb_alloc_bss_rx_buffers(hw, &hw->bss_mgmt.rx_bdr,
                                      hw->bss_mgmt.rx_bdr.basic.nof_bds,
                                      RX_MGMT_ALLOC_SIZE);

  if (MTLK_ERR_OK != res)
    goto clean_rx_bdr;

  /* BSS TX ring */
  ring_size = _mtlk_get_mgmt_tx_ring_size(hw);
  res = _mtlk_mmb_prepare_ring_buffer(hw, &hw->bss_mgmt.tx_ring, &hw->chi_area.sBasic.sMangTxRING, ring_size, "TxMgmt");
  if (MTLK_ERR_OK != res) {
    goto clean_rx;
  }
  mtlk_osal_atomic_set(&hw->bss_mgmt.tx_ring_last_cfm_ts, mtlk_osal_timestamp());

  res = mtlk_mmb_prepare_bss_req_bdr(hw, ring_size, BSS_MGMT_NUM_RESERVE_FRAMES);

  if (MTLK_ERR_OK != res)
    goto clean_tx_ring;

  res = mtlk_mmb_alloc_bss_tx_buffers(hw, &hw->bss_mgmt.tx_bdr,
                                      hw->bss_mgmt.tx_bdr.basic.nof_bds, BSS_MGMT_FRAME_SIZE);

  if (MTLK_ERR_OK != res)
    goto clean_tx;

  res = mtlk_mmb_alloc_bss_tx_buffers(hw, &hw->bss_mgmt.tx_res_bdr,
                                      hw->bss_mgmt.tx_res_bdr.basic.nof_bds, BSS_MGMT_FRAME_SIZE);

  if (MTLK_ERR_OK != res)
    goto clean_tx_res;

  res = _mtlk_mmb_alloc_bss_eapol_frag_buf(&hw->bss_mgmt.rx_eapol, BSS_MGMT_FRAME_SIZE);

  if (MTLK_ERR_OK != res)
    goto clean_tx_res;

  hw->bss_mgmt.cookie_counter = 0;
  return MTLK_ERR_OK;

clean_tx_res:
  mtlk_mmb_free_bss_tx_buffers(hw, &hw->bss_mgmt.tx_res_bdr);

clean_tx:
  mtlk_mmb_free_bss_tx_buffers(hw, &hw->bss_mgmt.tx_bdr);
  mtlk_mmb_cleanup_bss_req_bdr(hw);

clean_tx_ring:
  _mtlk_mmb_free_hd_ring(hw, &hw->bss_mgmt.tx_ring);

clean_rx:
  mtlk_mmb_free_bss_rx_buffers(hw, &hw->bss_mgmt.rx_bdr);

clean_rx_bdr:
  mtlk_mmb_cleanup_bss_ind_bdr(hw);

clean_rx_ring:
  _mtlk_mmb_free_hd_ring(hw, &hw->bss_mgmt.rx_ring);
  return res;
}

void _mtlk_mmb_bss_mgmt_cleanup(mtlk_hw_t *hw)
{
  /* Free RX */
  if (hw->flags & HW_FLAGS_PREPARED_MGMT_IND_BDR) {
    mtlk_mmb_free_bss_rx_buffers(hw, &hw->bss_mgmt.rx_bdr);
    mtlk_mmb_cleanup_bss_ind_bdr(hw);
    _mtlk_mmb_free_hd_ring(hw, &hw->bss_mgmt.rx_ring);
  }

  /* Free TX */
  if (hw->flags & HW_FLAGS_PREPARED_BSS_REQ_BDR) {
    mtlk_mmb_free_bss_tx_buffers(hw, &hw->bss_mgmt.tx_res_bdr);
    mtlk_mmb_free_bss_tx_buffers(hw, &hw->bss_mgmt.tx_bdr);
    mtlk_mmb_cleanup_bss_req_bdr(hw);
    _mtlk_mmb_free_hd_ring(hw, &hw->bss_mgmt.tx_ring);
  }

  /* Free EAPOL defrag buf */
  if (NULL != hw->bss_mgmt.rx_eapol.data) {
    mtlk_osal_mem_free(hw->bss_mgmt.rx_eapol.data);
    hw->bss_mgmt.rx_eapol.data = NULL;
  }
}

static int
_mtlk_mmb_defrag_and_handle_eapol (mtlk_hw_t *hw, mtlk_vap_handle_t vap_handle,
                                   uint8 *buf, uint32 size, uint8 frag_type)
{
  int res = MTLK_ERR_UNKNOWN;
  mtlk_bss_rx_eapol *eapol_frame = &hw->bss_mgmt.rx_eapol;

  switch (frag_type)
  {
    case FRAG_FIRST:
      mtlk_dump(5, buf, size, "EAPOL First frag dump:");
      if (eapol_frame->data_size) {
        ELOG_V("Received first EAPOL fragment while already having fragments");
        eapol_frame->data_size = 0;
      }

      if (eapol_frame->buf_size > size) {
        wave_memcpy(eapol_frame->data, size, buf, size);
        eapol_frame->data_size = size;
        res = MTLK_ERR_OK;
      }
      else {
        ELOG_V("Insufficient buffer size to defragment EAPOL");
        eapol_frame->data_size = 0;
        res = MTLK_ERR_DATA_TOO_BIG;
      }
      break;

    case FRAG_LAST:
      mtlk_dump(5, buf, size, "EAPOL Last frag dump:");
      if (0 == eapol_frame->data_size) {
        ELOG_V("Received last EAPOL fragment without having first");
        res = MTLK_ERR_UNKNOWN;
        break;
      }

      if ((eapol_frame->buf_size - eapol_frame->data_size) >= size) {
        wave_memcpy(eapol_frame->data + eapol_frame->data_size, size, buf, size);
        eapol_frame->data_size += size;
        res = MTLK_ERR_OK;
      }
      else {
        ELOG_V("Insufficient buffer size to defragment EAPOL");
        eapol_frame->data_size = 0;
        res = MTLK_ERR_DATA_TOO_BIG;
        break;
      }

      res = mtlk_handle_eapol(vap_handle, eapol_frame->data, eapol_frame->data_size);
      eapol_frame->data_size = 0;
      break;

    case FRAG_MID:
      mtlk_dump(5, buf, size, "EAPOL Middle frag dump:");
      if (0 == eapol_frame->data_size) {
        ELOG_V("Received middle EAPOL fragment without having first");
        res = MTLK_ERR_NOT_IN_USE;
        break;
      }

      if ((eapol_frame->buf_size - eapol_frame->data_size) > size) {
        wave_memcpy(eapol_frame->data + eapol_frame->data_size, size, buf, size);
        eapol_frame->data_size += size;
        res = MTLK_ERR_OK;
      }
      else {
        ELOG_V("Insufficient buffer size to defragment EAPOL");
        eapol_frame->data_size = 0;
        res = MTLK_ERR_DATA_TOO_BIG;
      }
      break;

    case NO_FRAG:
    default:
      mtlk_dump(5, buf, size, "EAPOL no-frag dump:");
      if (eapol_frame->data_size) {
        ELOG_V("Received unfragmented EAPOL when already having fragments");
        eapol_frame->data_size = 0;
      }
      res = mtlk_handle_eapol(vap_handle, buf, size);
  }

  return res;
}

#define DBG_DFLT_PHY_MODE                PHY_MODE_N /* for both 2G and 5G */
#define DBG_DFLT_RSSI                    (-50)
#define DBG_DFLT_NOISE                   (-65)
#define DBG_DFLT_SNR                     (DBG_DFLT_RSSI - DBG_DFLT_NOISE)

static void
_mtlk_hw_mmb_fill_phy_info_rssi_by_value (mtlk_phy_info_t *phy_info, int8 value)
{
    int i;

    /* Note that rssi_offs is 0 on dummy phy */
    for (i = 0; i < ARRAY_SIZE(phy_info->rssi); i++) {
      phy_info->rssi[i] = value;
    }
    phy_info->max_rssi = value;
}

static void __MTLK_IFUNC
_mtlk_hw_mmb_fill_phy_info_on_metrics_invalid (mtlk_phy_info_t *phy_info)
{
    memset(phy_info, 0, sizeof(*phy_info));
    phy_info->phy_mode          = DBG_DFLT_PHY_MODE;
    phy_info->bitrate_info      = MTLK_BITRATE_INFO_INVALID;
    phy_info->rate_idx          = 0;
    phy_info->max_rssi          = MIN_RSSI;
    phy_info->noise_estimation  = MIN_NOISE;
    phy_info->snr_db            = 0;

    _mtlk_hw_mmb_fill_phy_info_rssi_by_value(phy_info, MIN_RSSI);
}

static void
_mtlk_hw_mmb_fill_phy_info_by_default (mtlk_phy_info_t *phy_info)
{
    MTLK_ASSERT(NULL != phy_info);

    memset(phy_info, 0, sizeof(*phy_info));
    phy_info->phy_mode          = DBG_DFLT_PHY_MODE;
    phy_info->bitrate_info      = MTLK_BITRATE_INFO_INVALID;
    phy_info->rate_idx          = 0;
    phy_info->max_rssi          = DBG_DFLT_RSSI;
    phy_info->noise_estimation  = DBG_DFLT_NOISE;
    phy_info->snr_db            = DBG_DFLT_SNR;

    _mtlk_hw_mmb_fill_phy_info_rssi_by_value(phy_info, DBG_DFLT_RSSI);
}

void __MTLK_IFUNC
mtlk_hw_mmb_fill_phy_info_by_default (mtlk_phy_info_t *phy_info)
{
    MTLK_ASSERT(NULL != phy_info);
    mtlk_hw_mmb_fill_phy_info_by_default(phy_info);
}

static void
_mtlk_hw_mmb_validate_g5_phy_rssi_and_noise (mtlk_hw_t *hw, int8 *rssi, uint8 *noise)
{
    wave_radio_t *radio = __mtlk_hw_wave_radio_get(hw, 0); /* Single radio */
    unsigned      rx_ant_mask = wave_radio_current_antenna_mask_get(radio);
    int i;

    for (i = 0; i < PHY_STATISTICS_MAX_RX_ANT; i++) {
      if (MTLK_BIT_GET(rx_ant_mask, i)) {
        rssi[i] = MAX(MIN_RSSI, rssi[i]);
      } else {
        rssi[i] = MIN_RSSI;
        noise[i] = 0;
      }
    }
}

/* Get RSSI values and max RSSI using RX PHY RSSI and RSSI_offset */
static int
_mtlk_hw_get_rssi_max_by_rx_phy_rssi (mtlk_hw_t *hw, int8 *rx_phy_rssi, int8 *out_rssi)
{
    int i;
    int rssi_val;
    int rssi_max = MIN_RSSI;
    unsigned rssi_offs = _mtlk_hw_get_rrsi_offs(hw);

    for (i = 0; i < PHY_STATISTICS_MAX_RX_ANT; i++) {
        rssi_val = (int)rx_phy_rssi[i] - rssi_offs;
        rssi_val = MAX(MIN_RSSI, rssi_val);
        rssi_max = MAX(rssi_max, rssi_val);
        out_rssi[i] = rssi_val;
    }

    ILOG3_DDDD("RSSI phy 0x%08X, updated 0x%08X, offs %u, max %d",
               *(uint32*)rx_phy_rssi,
               *(uint32*)out_rssi,
               rssi_offs, rssi_max);

    return rssi_max;
}

int __MTLK_IFUNC
mtlk_hw_get_rssi_max_by_rx_phy_rssi (mtlk_hw_t *hw, int8 *rx_phy_rssi, int8 *out_rssi)
{
    return _mtlk_hw_get_rssi_max_by_rx_phy_rssi (hw, rx_phy_rssi, out_rssi);
}

static void
_mtlk_mmb_fill_phy_info_rssi_by_rx_phy_rssi (mtlk_hw_t *hw,
        mtlk_phy_info_t *phy_info, int8 *phy_rx_rssi)
{
    MTLK_STATIC_ASSERT(ARRAY_SIZE(phy_info->rssi) == PHY_STATISTICS_MAX_RX_ANT);
    phy_info->max_rssi =  _mtlk_hw_get_rssi_max_by_rx_phy_rssi(hw, phy_rx_rssi, phy_info->rssi);
}

static void
_mtlk_mmb_fill_phy_info_noise_by_rx_phy_noise_gain (mtlk_hw_t *hw,
        mtlk_phy_info_t *phy_info, uint8 *rx_phy_noise, uint8 *rx_phy_gain)
{
    int i;
    int noise_dbm, max_noise = MIN_NOISE;
    int snr_db, max_snr = 0;

    MTLK_STATIC_ASSERT(ARRAY_SIZE(phy_info->rssi) == PHY_STATISTICS_MAX_RX_ANT);
    for (i = 0; i < ARRAY_SIZE(phy_info->noise); i++) {
      /* Convert Phy Noise to Noise in dBm */
      noise_dbm = mtlk_hw_noise_phy_to_noise_dbm(hw, rx_phy_noise[i], rx_phy_gain[i]);
      phy_info->noise[i] = noise_dbm;
      if (!noise_dbm) continue; /* unavailable */
      /* Calculate SNR by RSSI and Noise */
      snr_db = mtlk_calculate_snr(phy_info->rssi[i], noise_dbm);
      phy_info->snr[i] = snr_db;
      /* Get max Noise and SNR for same antenna */
      if (max_noise < noise_dbm) {
        max_noise = noise_dbm;
        max_snr   = snr_db;
      }
    }

    phy_info->noise_estimation = max_noise;
    phy_info->snr_db = max_snr;
}

static void
_mtlk_mmb_fill_phy_info_rate_by_g5_rx_metrics (mtlk_hw_t *hw,
        mtlk_phy_info_t *phy_info, RX_PHY_METRICS *g5_rx_metrics)
{
    uint64  word0 = MAC_TO_HOST64(g5_rx_metrics->phy_status_word0);
    uint32  word2 = MAC_TO_HOST32(g5_rx_metrics->phy_status_word2_2);
    uint32  rate, info;

    rate = MTLK_BFIELD_GET(word2, PHY_STAT2_2_RATE);
    info = MTLK_BFIELD_GET(word0, PHY_STAT0_RATE_INFO);

    phy_info->phy_mode = MTLK_BFIELD_GET(info, PHY_STAT_RATE_PHYMODE);
    phy_info->bitrate_info = MTLK_BITRATE_INFO_BY_RATE_INFO(rate, info);
    phy_info->rate_idx      = 0;    /* TBD */
}

static void
_mtlk_mmb_fill_phy_info_by_g5_rx_metrics (mtlk_hw_t *hw,
        mtlk_phy_info_t *phy_info, RX_PHY_METRICS *g5_rx_metrics)
{
    ILOG3_DDD("rssi 0x%08X, noise 0x%08X, rf_gain 0x%08X",
              g5_rx_metrics->word_rssi,
              g5_rx_metrics->word_noise_estimation,
              g5_rx_metrics->word_rf_gain);

    MTLK_STATIC_ASSERT(ARRAY_SIZE(phy_info->rssi) <= ARRAY_SIZE(g5_rx_metrics->rssi_arr));

    /* Validate RSSI also Noise because they were not set by Gen5 HW for non-active antennas */
    _mtlk_hw_mmb_validate_g5_phy_rssi_and_noise(hw,
                g5_rx_metrics->rssi_arr, g5_rx_metrics->noise_estimation_arr);

    _mtlk_mmb_fill_phy_info_rate_by_g5_rx_metrics(hw, phy_info, g5_rx_metrics);

    /* 1. Calculate RSSI by Rx metrics */
    /* 2. Calculate Noise and SNR by Rx metrics and calculated RSSI */
    _mtlk_mmb_fill_phy_info_rssi_by_rx_phy_rssi(hw, phy_info, g5_rx_metrics->rssi_arr);
    _mtlk_mmb_fill_phy_info_noise_by_rx_phy_noise_gain (hw, phy_info,
        g5_rx_metrics->noise_estimation_arr, g5_rx_metrics->rf_gain_arr);
}

static void
_mtlk_mmb_fill_phy_info_by_g6_rx_metrics (mtlk_hw_t *hw, mtlk_phy_info_t *phy_info,
                                          G6_RX_PHY_METRICS *g6_rx_metrics)
{
    int    pre_metrics_valid;
    int    post_metrics_valid;
    uint32 word0, word1;


    if (__LIKELY(__hw_mmb_card_is_phy_real(hw))) {
      G6_RXMETRICS_COMMON_PRE_PPDU    *common_pre_ppdu = &g6_rx_metrics->commonPrePpduMetrics;
      G6_RXMETRICS_USER_SPEC_PRE_PPDU *user_pre_ppdu   = &g6_rx_metrics->userSpecPrePpduMetrics;

      word0 = MAC_TO_HOST32(g6_rx_metrics->commonPostPpduMetrics.Status0Word0);
      pre_metrics_valid  = MTLK_BFIELD_GET(word0, G6_COMMON_POST_PPDU_PHY_STAT0_0_PRE_METRICS_VALID);
      post_metrics_valid = MTLK_BFIELD_GET(word0, G6_COMMON_POST_PPDU_PHY_STAT0_0_POST_METRICS_VALID);
      ILOG3_DD("Metrics valid flags: pre:%d, post:%d", pre_metrics_valid, post_metrics_valid);

      MTLK_STATIC_ASSERT(ARRAY_SIZE(phy_info->rssi) <= ARRAY_SIZE(user_pre_ppdu->FdRssi));

      if (pre_metrics_valid) {
        word0 = MAC_TO_HOST32(common_pre_ppdu->Status0Word0);
        phy_info->phy_mode = MTLK_BFIELD_GET(word0, G6_COMMON_PRE_PPDU_PHY_STAT0_0_PHY_MODE);

        word1 = MAC_TO_HOST32(user_pre_ppdu->Status2Word1);
        phy_info->bitrate_info = MTLK_BITRATE_INFO_BY_PHY_RATE(
                                  MTLK_BFIELD_GET(word1, G6_USER_SPEC_PRE_PPDU_PHY_STAT2_1_PHY_RATE));

        phy_info->rate_idx         = 0; /* TBD */

        /* 1. Calculate RSSI by Rx metrics */
        /* 2. Calculate Noise and SNR by Rx metrics and calculated RSSI */
        _mtlk_mmb_fill_phy_info_rssi_by_rx_phy_rssi(hw, phy_info, user_pre_ppdu->FdRssi);
        _mtlk_mmb_fill_phy_info_noise_by_rx_phy_noise_gain (hw, phy_info,
                    common_pre_ppdu->Noise, common_pre_ppdu->RfGain);

        ILOG3_DDDDD("word0 0x%08X, phy_mode %u, bitrate_info 0x%08X, word_rssi 0x%08X, max_rssi %d",
                    word0, phy_info->phy_mode, phy_info->bitrate_info, phy_info->word_rssi, phy_info->max_rssi);

        ILOG3_DDDDDD("Gain 0x%08X, Noise 0x%08X -> 0x%08X, SNR 0x%08X, noise_est %d, snr_db %d",
                     common_pre_ppdu->RfGainWord0, common_pre_ppdu->NoiseWord0,
                     phy_info->word_noise, phy_info->word_snr, phy_info->noise_estimation, phy_info->snr_db);

      } else { /* invalid */
        _mtlk_hw_mmb_fill_phy_info_on_metrics_invalid(phy_info);
      }
    } else { /* dummy */
      _mtlk_hw_mmb_fill_phy_info_by_default(phy_info);
    }
}

int _mtlk_mmb_handle_bss_ind (mtlk_hw_t *hw, volatile HOST_DSC *pHD)
{
  int                           res = MTLK_ERR_OK;
  uint32                        u32frameInfo0;
  uint32                        u32frameInfo1;
  uint32                        u32frameInfo3;
  uint32                        hostpayload;
  uint32                        offset;
  uint32                        size;
  uint8                         vap_id;
  uint8                         radio_id = 0; /* will be updated later */
  uint8                         frame_type;
  uint8                         type;
  mtlk_vap_handle_t             vap_handle;
  BOOL                          is_eapol;
  uint32                        total_size;
  BOOL                          is_gen5 = _hw_type_is_gen5(hw);
  void                         *buf;
  mtlk_core_handle_rx_bss_t     data;
  mtlk_phy_info_t              *phy_info = &data.phy_info;
  uint8                         frag_type;
  uint32                        dma_addr;

  u32frameInfo3 = LE32_TO_CPU(pHD->u32frameInfo3);
  if (__UNLIKELY(MTLK_OWNER_FW == MTLK_BFIELD_GET(u32frameInfo3, TX_DATA_INFO_OWN_BIT))) {
    /* Loop some time in case HD is not written yet */
    int i = OWN_ITER_CNTR;
    do {
      u32frameInfo3 = LE32_TO_CPU(pHD->u32frameInfo3);
      if (MTLK_OWNER_FW != MTLK_BFIELD_GET(u32frameInfo3, TX_DATA_INFO_OWN_BIT))
        break;
    } while (--i);

    if (__UNLIKELY(i == 0)) {
      ELOG_V("HD Owner bit error");
      goto critical_error;
    }
  }
  u32frameInfo0 = LE32_TO_CPU(pHD->u32frameInfo0);
  u32frameInfo1 = LE32_TO_CPU(pHD->u32frameInfo1);
  hostpayload   = LE32_TO_CPU(pHD->u32HostPayloadAddr);
  offset        = MTLK_BFIELD_GET(u32frameInfo3, TX_DATA_INFO_OFFSET);
  size          = MTLK_BFIELD_GET(u32frameInfo3, TX_DATA_INFO_LENGTH);
  frame_type    = MTLK_BFIELD_GET(u32frameInfo0, TX_DATA_INFO_FRAMETYPE);
  type          = MTLK_BFIELD_GET(u32frameInfo0, TX_BSS_EXTRA_TYPE);
  frag_type     = MTLK_BFIELD_GET(u32frameInfo3, TX_DATA_INFO_SOP_EOP);

  __wave_hw_rx_hd_decode(hw,
                         MTLK_BFIELD_GET(u32frameInfo1, TX_DATA_INFO_EP),
                         MTLK_BFIELD_GET(u32frameInfo0, TX_DATA_INFO_VAPID),
                         &radio_id,
                         &vap_id);

  mtlk_dump(4, (void *)pHD, sizeof(HOST_DSC), "HD");
  ILOG3_DDDDDD("RadioID:%u VapID:%u offset:%u size:%u frame_type:%u payload:0x%08x",
               radio_id, vap_id, offset, size, frame_type, hostpayload);

  vap_handle = _mtlk_mmb_get_vap_handle_from_vap_id(hw, radio_id, vap_id);
  if (__UNLIKELY(!vap_handle)) {
    _mtlk_mmb_log_error_vap_not_exist(__FUNCTION__, radio_id, vap_id);
    goto send_resp;
  }

  /* FIXME remove this check when fw will implenet fix for sendig Null data throught mngmnt ring */
  if (size == 0) {
    goto send_resp;
  }

  /* Calculate total size of received data depending on the frame type and platform */
  is_eapol = ((FRAME_TYPE_EAPOL == frame_type) && (type == (IEEE80211_FTYPE_DATA >> 2)));
  total_size = (is_eapol ? size : /* Both: reported size = size of EAPOL frame, RxPhyMetrics is unavailable */
                is_gen5  ? size : /* Gen5: reported size = size of frame + size of RxPhyMetrics */
                                  /* Gen6: reported size = size of frame; need to add RxPhyMetrics with alignment */
                (RX_MGMT_ALIGNMENT(size + offset) - offset + sizeof(G6_RX_PHY_METRICS)));

  hostpayload += offset;
  mtlk_osal_unmap_phys_addr(mtlk_ccr_get_dev_ctx(hw->ccr),
                            hostpayload, total_size, MTLK_DATA_FROM_DEVICE);

  buf = (uint8 *)(phys_to_virt(hostpayload));

  ILOG3_DP("phyaddr:0x%x vaddr:0x%p", hostpayload, buf);
  mtlk_dump(3, buf + sizeof(RX_PHY_METRICS), MIN(32, size),"EAPOL/BSS Frame");

  if ((is_eapol) ||
       ((FRAME_TYPE_ETHERNET == frame_type) && type == (IEEE80211_FTYPE_DATA >> 2) && hw->bss_mgmt.rx_eapol.data_size))
  {
    res = _mtlk_mmb_defrag_and_handle_eapol(hw, vap_handle, buf, size, frag_type);
  } else {
    if (is_gen5) {
      /* buffer: RxPhyMetrics, mgmt frame */
      RX_PHY_METRICS *g5_rx_metrics;

      MTLK_ASSERT(size <= G5_RX_MGMT_FRAME_SIZE); /* total size of frame and metrics */
      MTLK_ASSERT(size >  sizeof(RX_PHY_METRICS));

      data.buf      = buf  + sizeof(RX_PHY_METRICS); /* the real frame data start here */
      data.size     = size - sizeof(RX_PHY_METRICS); /* size of data in buffer */
      g5_rx_metrics = (RX_PHY_METRICS *)buf;
      _mtlk_mmb_fill_phy_info_by_g5_rx_metrics(hw, phy_info, g5_rx_metrics);

    } else { /* Gen6 */
      /* buffer: mgmt frame, RxPhyMetrics start on the next alignment */
      G6_RX_PHY_METRICS *g6_rx_metrics;

      MTLK_ASSERT(size <= BSS_MGMT_FRAME_SIZE); /* size of frame */
      data.buf      = buf;
      data.size     = size;
      g6_rx_metrics = (G6_RX_PHY_METRICS*)(RX_MGMT_ALIGNMENT((wave_addr)buf + size));
      _mtlk_mmb_fill_phy_info_by_g6_rx_metrics(hw, phy_info, g6_rx_metrics);

#ifndef MTLK_LEGACY_STATISTICS
      {
        wave_radio_t *radio = __mtlk_hw_wave_radio_get(hw, radio_id);
        mtlk_core_t *master_core;
        struct mtlk_chan_def *ccd;
        int channel_num, idx;

        MTLK_ASSERT(radio);
        if (radio) {
          master_core = wave_radio_master_core_get(radio);
          ccd = __wave_core_chandef_get(master_core);
          channel_num = freq2lowchannum(ccd->chan.center_freq, CW_20);
          idx = channum2cssidx(channel_num);

          if (idx != CHAN_IDX_ILLEGAL) {
            hw->chan_statistics[idx].ch_number = channel_num;
            hw->chan_statistics[idx].ch_max_80211_rssi = phy_info->max_rssi;
          }
        }

      }
#endif
    }

    ILOG3_YDDD("%Y max_rssi %d, noise_est %d, snr_db %d", WLAN_GET_ADDR2(data.buf),
               phy_info->max_rssi, phy_info->noise_estimation, phy_info->snr_db);

    ILOG3_DPP("data_size=%hu, buff 0x%p, phy_info 0x%p", data.size, data.buf, phy_info);
    CAPWAP1(hw->card_idx, data.buf, data.size, 1 /* is_wifi */, phy_info->max_rssi);

    data.make_assert = FALSE;
    res = mtlk_vap_get_core_vft(vap_handle)->handle_rx_bss(vap_handle, &data);
    if (res == MTLK_ERR_OK) {
      _mtlk_mmb_hw_inc_cnt(hw, MTLK_HW_SOURCE_CNT_BSS_RX_PACKETS_PROCESSED);
    } else {
      _mtlk_mmb_hw_inc_cnt(hw, MTLK_HW_SOURCE_CNT_BSS_RX_PACKETS_REJECTED);
    }

    if (data.make_assert ) {
        ELOG_D("Corrupted frame err_code:%d", res);
        goto critical_error;
    }
  }

  dma_addr =mtlk_osal_map_to_phys_addr(mtlk_ccr_get_dev_ctx(hw->ccr), buf, total_size, MTLK_DATA_FROM_DEVICE);
  if (!dma_addr) {
    ILOG2_P("WARNING: failed mapping 0x%p to physical address", buf);
    goto critical_error;
  }

send_resp:
  _mtlk_mmb_put_hd_to_ring(hw, (HOST_DSC *)pHD, &hw->bss_mgmt.rx_ring);
  return res;

critical_error:
    ELOG_D("CID-%02x: Critical error in bss ring", hw->card_idx);
    ELOG_DDDD("internal variables: hd_read_index %d hd_write_index %d rx_IN_copy %d OUT_cntr %d", hw->bss_mgmt.rx_ring.read_ptr, hw->bss_mgmt.rx_ring.write_ptr, hw->bss_mgmt.rx_ring.regs.IN_copy, hw->bss_mgmt.rx_ring.regs.int_OUT_cntr);
    mtlk_dump(0, (void *)pHD, sizeof(HOST_DSC), "HD in error");
    mtlk_hw_set_prop(wave_radio_descr_hw_api_get(hw->radio_descr, radio_id), MTLK_HW_RESET, NULL, 0);
    return MTLK_ERR_UNKNOWN; /* BUG(); */
}

/* FIXME use normal defines from frame.h */
#define TMP_FRAME_FROMDSTODS_MASK 0x0300
#define TMP_FRAME_FROMDSTODS_SHIFT 8

static int
_mtlk_mmb_bss_mgmt_tx(mtlk_hw_t *hw,
                      struct mtlk_hw_bss_req_mirror *bss_req, mtlk_vap_handle_t vap_handle)
{
  HOST_DSC bss_hd;
  mtlk_core_t *nic = mtlk_vap_get_core(vap_handle);
  uint16 type, subtype, sid, frame_ctl;
  uint32 frame_type, ntid;
  unsigned char *dst_addr;
  uint32 packet_length = bss_req->size;
  BOOL mcf;

#ifdef BD_DBG
  if (bss_req->hdr.index >= hw->bss_mgmt.tx_bdr.basic.nof_bds) {
    ELOG_D("index out of bounds: %d\n", bss_req->hdr.index);
    goto trap;
  }
  if (test_bit(bss_req->hdr.index, hw->bss_mgmt.used_bd_map)) {
    ELOG_D("attempt to reuse BD without releasing (BSS TX), index %d\n", bss_req->hdr.index);
    goto trap;
  }
#else
  MTLK_ASSERT(bss_req->hdr.index < hw->bss_mgmt.tx_bdr.basic.nof_bds);
#endif

  memset(&bss_hd, 0, sizeof(bss_hd));

  mtlk_dump(3, bss_req->virt_addr, bss_req->size, "BSS Mgmt TX frame:");

  switch (bss_req->extra_processing) {
    case PROCESS_MANAGEMENT:
        /* 1. dst address mc bit */
        dst_addr = WLAN_GET_ADDR1(bss_req->virt_addr);
        mcf = mtlk_osal_eth_is_multicast(dst_addr);

        /* 2. sta id by dst address */
        sid = mtlk_core_get_sid_by_addr(nic, dst_addr);

        /* 3. type, subtype, priority, frame type */
        type = IEEE80211_FTYPE_MGMT >> 2;
        frame_ctl = mtlk_wlan_pkt_get_frame_ctl(bss_req->virt_addr);
        /* Check tods and from ds for managment packets must be 0 */
        if (frame_ctl & TMP_FRAME_FROMDSTODS_MASK) {
            ELOG_D("CID-%02x: ToDS and FromDS not null in managment packet", hw->card_idx);
            MTLK_ASSERT(NULL);
            return MTLK_ERR_PKT_DROPPED;
        }
        subtype = (frame_ctl & FRAME_SUBTYPE_MASK) >> FRAME_SUBTYPE_SHIFT;
        frame_type = FRAME_TYPE_ETHERNET; /* default not used */

        /* default max priority VI */
        if(_hw_type_is_gen6(hw)) {
          ntid = NTS_TIDS_GEN6 - 1; /* max priority for GEN6 and up */
        } else {
          ntid = NTS_TIDS - 1; /* max priority VI for GEN5 and below */
        }
        ntid = bss_req->tid == NTS_TID_USE_DEFAULT ? ntid : bss_req->tid;

        CAPWAP1(hw->card_idx, bss_req->virt_addr, bss_req->size, 1 /* is_wifi */, 0 /* rssi */);
        break;
    case PROCESS_NULL_DATA_PACKET:
        /* 1. dst address mc bit */
        dst_addr = WLAN_GET_ADDR1(bss_req->virt_addr);
        mcf = mtlk_osal_eth_is_multicast(dst_addr);
        if(mcf) {
          ELOG_D("CID-%02x: dst is multicast for Data Null packet", hw->card_idx);
          MTLK_ASSERT(NULL);
          return MTLK_ERR_PKT_DROPPED;
        }

        /* 2. sta id by dst address */
        sid = mtlk_core_get_sid_by_addr(nic, dst_addr);
        if (DB_UNKNOWN_SID == sid) {
          ILOG0_D("CID-%02x: dst is unknown for Data Null packet", hw->card_idx);
          return MTLK_ERR_PKT_DROPPED;
        }

        /* 3. type, subtype, priority, frame type */
        type = IEEE80211_FTYPE_DATA >> 2;
        subtype = IEEE80211_STYPE_NULLFUNC >> 4;
        /* default max priority VI*/
        /* todo: why we don't check for gen6 like for management frames? */
        ntid = bss_req->tid == NTS_TID_USE_DEFAULT ? NTS_TIDS - 1 : bss_req->tid;
        frame_type = FRAME_TYPE_ETHERNET; /* default not used */
        packet_length = 0; /* size for Null data packet must be zero */
        CAPWAP1(hw->card_idx, bss_req->virt_addr, bss_req->size, 1 /* is_wifi */, 0 /* rssi */);

        if (!mtlk_vap_is_ap(vap_handle)) {
          wave_vap_increment_ndp_counter(vap_handle);
        }
        break;
    case PROCESS_EAPOL:
        /* 1. dst address mc bit */
        dst_addr = ((struct ethhdr *) bss_req->virt_addr)->h_dest;
        mcf = mtlk_osal_eth_is_multicast(dst_addr);
        if(mcf) {
          ELOG_D("CID-%02x: dst is multicast for  EAPOL packet", hw->card_idx);
          MTLK_ASSERT(NULL);
          return MTLK_ERR_PKT_DROPPED;
        }

        /* 2. sta id by dst address */
        sid = mtlk_core_get_sid_by_addr(nic, dst_addr);
        if (DB_UNKNOWN_SID == sid) {
          ILOG0_D("CID-%02x: dst is unknown for EAPOL packet", hw->card_idx);
          return MTLK_ERR_PKT_DROPPED;
        }

        /* 3. type, subtype, priority, frame type */
        type = IEEE80211_FTYPE_DATA >> 2;
        subtype = IEEE80211_STYPE_QOS_DATA >> 4;
        /* default max priority VI*/
        /* todo: why we don't check for gen6 like for management frames? */
        ntid = bss_req->tid == NTS_TID_USE_DEFAULT ? NTS_TIDS - 1 : bss_req->tid;
        frame_type = FRAME_TYPE_EAPOL;
        CAPWAP1(hw->card_idx, bss_req->virt_addr, bss_req->size, 0 /* is_wifi */, 0 /* rssi */);
        break;
    default:
        ELOG_DD("CID-%02x: Unknown packet type:%d", hw->card_idx, bss_req->extra_processing);
        MTLK_ASSERT(NULL);
        return MTLK_ERR_PKT_DROPPED;
  }

  /* do not touch the data payload after dma map */
  bss_req->dma_addr = mtlk_osal_map_to_phys_addr(mtlk_ccr_get_dev_ctx(hw->ccr), bss_req->virt_addr,
                                                 bss_req->size, MTLK_DATA_TO_DEVICE);
  if (!bss_req->dma_addr) {
    ILOG2_P("WARNING: failed mapping 0x%p to physical address", bss_req->virt_addr);
    goto trap;
  }

  /* leave the u8Reserved, u8Status and retransmissions fields as 0 */

  bss_hd.u32BdIndex         = CPU_TO_LE32(bss_req->hdr.index);
  bss_hd.u32frameInfo0 = CPU_TO_LE32((MTLK_BFIELD_VALUE(TX_BSS_EXTRA_TYPE, type, uint32)
                          | MTLK_BFIELD_VALUE(TX_BSS_EXTRA_SUBTYPE, subtype, uint32)
                          | MTLK_BFIELD_VALUE(TX_BSS_EXTRA_ACTION_CODE, bss_req->action_code, uint32)
                          | MTLK_BFIELD_VALUE(TX_DATA_INFO_FRAMETYPE, frame_type, uint32)
                          | MTLK_BFIELD_VALUE(TX_DATA_INFO_VAPID, bss_req->vap_id, uint32)
                          | (mcf ?
                               /* broadcast */ MTLK_BFIELD_VALUE(TX_DATA_INFO_MCF, 1, uint32)
                             : /* unicast   */ (DB_UNKNOWN_SID == sid) ?
                                 /* SID unknown */
                                MTLK_BFIELD_VALUE(TX_DATA_INFO_STAID, 0, uint32) |
                                MTLK_BFIELD_VALUE(TX_DATA_INFO_UNKNOWN_SID, 1, uint32)
                              :  /* SID known */
                                MTLK_BFIELD_VALUE(TX_DATA_INFO_STAID, sid, uint32) |
                                MTLK_BFIELD_VALUE(TX_DATA_INFO_UNKNOWN_SID, 0, uint32)) |
                                MTLK_BFIELD_VALUE(TX_DATA_INFO_POWERMANAGEMENT, bss_req->power_mgmt_on, uint32)
                         ));

  bss_hd.u32frameInfo1 = CPU_TO_LE32(MTLK_BFIELD_VALUE(TX_DATA_INFO_CLASS, ntid, uint32)
                          | MTLK_BFIELD_VALUE(TX_DATA_INFO_EP, wave_vap_ep_bit_get(vap_handle), uint32));

  bss_hd.u32frameInfo3      = CPU_TO_LE32(MTLK_BFIELD_VALUE(TX_DATA_INFO_SOP_EOP, NO_FRAG, uint32) |
                              MTLK_BFIELD_VALUE(TX_DATA_INFO_OFFSET,
                                                bss_req->dma_addr & 7,
                                                uint32) |
                              MTLK_BFIELD_VALUE(TX_DATA_INFO_LENGTH,
                                                packet_length,
                                                uint32));

  bss_hd.u32HostPayloadAddr = CPU_TO_LE32(bss_req->dma_addr & ~7);

  ILOG3_DDHPDDD("bss_req: RadioID=%u, VapID=%u, cookie=%llu, virt_addr=0x%p, size=%u, dma_addr=0x%08x, subtype=%hu",
    bss_req->radio_id, bss_req->vap_id, bss_req->cookie, bss_req->virt_addr, bss_req->size, bss_req->dma_addr, subtype);

  mtlk_dump(3, &bss_hd, sizeof(bss_hd), "BSS Mgmt TX HD:");

  mtlk_core_on_bss_tx(mtlk_vap_get_core(vap_handle), bss_req->subtype);

#ifdef BD_DBG
   set_bit(bss_req->hdr.index, hw->bss_mgmt.used_bd_map);
#endif

  _mtlk_mmb_put_hd_to_ring(hw, &bss_hd, &hw->bss_mgmt.tx_ring);
  return MTLK_ERR_OK;

#ifdef BD_DBG
trap:
  mtlk_hw_set_prop(mtlk_vap_get_hwapi(vap_handle), MTLK_HW_RESET, NULL, 0);
  return MTLK_ERR_PKT_DROPPED; /* BUG() */
#endif
}

BOOL _mtlk_mmb_is_dst_sta_valid (mtlk_vap_handle_t vap_handle, const uint8 *buf, uint32 line)
{
  uint8 *dst_addr = WLAN_GET_ADDR1((uint8 *) buf);
  uint16 sid = mtlk_core_get_sid_by_addr(mtlk_vap_get_core(vap_handle), dst_addr);

  if (DB_UNKNOWN_SID == sid) {
    WLOG_YD("Unknown SID when sending null data packet, STA address %Y, caller line %d", dst_addr, line);
    return FALSE;
  }

  return TRUE;
}


static __INLINE
void mtlk_mmb_bss_mgmt_update_free_list_stat(struct mtlk_bss_management *bss_mgmt)
{
    uint32 nof_bds, nof_used_bds, nof_free_bds;
    /* max size of queue */
    nof_bds = bss_mgmt->tx_bdr.basic.nof_bds;
    /* free space in queue */
    nof_free_bds = mtlk_dlist_size(&bss_mgmt->tx_bdr.free_list);
    /* used space in queue and usage peak */
    nof_used_bds = nof_bds - nof_free_bds;
    if (nof_used_bds > bss_mgmt->tx_bdr_max_used_bds)
        bss_mgmt->tx_bdr_max_used_bds = nof_used_bds;
}

static __INLINE
void mtlk_mmb_bss_mgmt_update_res_free_list_stat(struct mtlk_bss_management *bss_mgmt)
{
    uint32 nof_bds, nof_used_bds, nof_free_bds;
    /* max size of res queue */
    nof_bds = bss_mgmt->tx_res_bdr.basic.nof_bds;
    /* free space in res queue */
    nof_free_bds = mtlk_dlist_size(&bss_mgmt->tx_res_bdr.free_list);
    /* used space in res queue and usage peak */
    nof_used_bds = nof_bds - nof_free_bds;
    if (nof_used_bds > bss_mgmt->tx_res_bdr_max_used_bds)
        bss_mgmt->tx_res_bdr_max_used_bds = nof_used_bds;
}

/* Check tx_res_bdr.free_list.
 * Return FALSE if lesser than 25% entries left in reserved list, otherwise return TRUE.
 */
BOOL mtlk_mmb_bss_mgmt_tx_check(mtlk_vap_handle_t vap_handle)
{
    mtlk_hw_t *hw = mtlk_vap_get_hw(vap_handle); /* checks everything from vap_handle down to hw */
    struct mtlk_bss_management *bss_mgmt = &hw->bss_mgmt;
    uint32 nof_bds, nof_free_bds;
    BOOL res;

    mtlk_osal_lock_acquire(&bss_mgmt->tx_bdr.lock);
    /* max size of res queue */
    nof_bds = bss_mgmt->tx_res_bdr.basic.nof_bds;
    /* free space in res queue */
    nof_free_bds = mtlk_dlist_size(&bss_mgmt->tx_res_bdr.free_list);
    mtlk_osal_lock_release(&bss_mgmt->tx_bdr.lock);

    res = !(nof_free_bds<(nof_bds/4)); /* left lesser then 25% */
    return res;
}

#define BSS_MGMT_TX_DEBUG 1 /* FIXME turn off when it's clear what's filling the queues */

static void
_mtlk_mmb_dump_bss_mgmt_res_queue(struct mtlk_bss_management *bss_mgmt)
{
#if BSS_MGMT_TX_DEBUG
    int i;
    mtlk_dlist_entry_t *listptr; /* struct list_head */

    ILOG0_V("Dumping BSS reserve queue");
    mtlk_osal_lock_acquire(&bss_mgmt->tx_bdr.lock);
    i = 0;
    list_for_each(listptr, &bss_mgmt->tx_res_bdr.used_list.head)
    {
      struct mtlk_hw_bss_req_mirror *bss_req_res
        = MTLK_LIST_GET_CONTAINING_RECORD(listptr, struct mtlk_hw_bss_req_mirror, hdr.list_entry);
      ILOG0_DDHPDD("bss_req_res #%i: VapID=%u, cookie=%llu, virt_addr=0x%p, size=%u, dma_addr=0x%08x",
                   ++i, bss_req_res->vap_id, bss_req_res->cookie, bss_req_res->virt_addr,
                   bss_req_res->size, bss_req_res->dma_addr);
      mtlk_dump(0, bss_req_res->virt_addr, bss_req_res->size, "BSS Mgmt TX frame:");
    }
    mtlk_osal_lock_release(&bss_mgmt->tx_bdr.lock);
#endif
}

void __MTLK_IFUNC
mtlk_mmb_print_tx_bss_res_queue(mtlk_hw_t *hw)
{
    MTLK_ASSERT(hw);
    _mtlk_mmb_dump_bss_mgmt_res_queue(&hw->bss_mgmt);
}

int mtlk_mmb_bss_mgmt_tx (mtlk_vap_handle_t vap_handle, const uint8 *buf, size_t len, int channum,
                          BOOL no_cck, BOOL dont_wait_for_ack, BOOL is_broadcast,
                          uint64 *cookie, uint32 extra_processing, mtlk_nbuf_t *skb,
                          BOOL power_mgmt_on, uint32 tid)
{
  mtlk_hw_t *hw = mtlk_vap_get_hw(vap_handle); /* checks everything from vap_handle down to hw */
  struct mtlk_bss_management *bss_mgmt = &hw->bss_mgmt;
  struct mtlk_hw_bss_req_mirror *bss_req;
  mtlk_dlist_entry_t *entry;
  uint64 local_cookie = 0;
  BOOL transmit_now = TRUE;
  uint8 subtype = 0;
  uint8 action_code = 0;
  IEEE_ADDR dst_addr;
  uint8 radio_id = wave_vap_radio_id_get(vap_handle);
  wave_radio_t *radio;
  struct mtlk_chan_def *ccd;
  mtlk_core_t *cur_core;
  uint32 net_state;

  cur_core = mtlk_vap_get_core(vap_handle);
  net_state = mtlk_core_get_net_state(cur_core);
  if (net_state != NET_STATE_CONNECTED)
  {
    ELOG_D("VAP not yet activated, net_state=0x%x", net_state);
    return MTLK_ERR_NOT_READY;
  }

  radio = __mtlk_hw_wave_radio_get(hw, radio_id);
  if(__UNLIKELY(!radio)) {
    _mtlk_mmb_log_error_radio_not_exist(__FUNCTION__, radio_id);
    return MTLK_ERR_PARAMS;
  }

  /* we don't allow switching channels */
  ccd = wave_radio_chandef_get(radio);
  if (channum && channum != freq2lowchannum(ccd->chan.center_freq, CW_20))
  {
    ELOG_SDD("%s: Channel %d does not match the current channel %i",
             current->comm, channum, freq2lowchannum(ccd->chan.center_freq, CW_20));
    return MTLK_ERR_PARAMS;
  }

  /* FIXME: don't know what to do with the no_cck flag, so ignore it for now */

  if (len > BSS_MGMT_FRAME_SIZE)
  {
    ILOG3_DD("Requested buffer length %u greater than maximum mgmt frame size %u", len, BSS_MGMT_FRAME_SIZE);
    return MTLK_ERR_PARAMS;
  }

  memset(&dst_addr, 0, sizeof(dst_addr));

  if (PROCESS_MANAGEMENT == extra_processing)
  {
    uint16 frame_ctrl;
    IEEE_ADDR *addr1 = (IEEE_ADDR *)WLAN_GET_ADDR1((uint8 *)buf);
    dst_addr = *addr1;
    frame_ctrl = mtlk_wlan_pkt_get_frame_ctl((uint8 *)buf);
    subtype = (frame_ctrl & FRAME_SUBTYPE_MASK) >> FRAME_SUBTYPE_SHIFT;

    /* Check if we've got not management frame. Confirm and drop them silently. */
    if (__UNLIKELY(IEEE80211_FTYPE_MGMT != WLAN_FC_GET_TYPE(frame_ctrl))) {
      ELOG_D("wrong frame type: %d", WLAN_FC_GET_TYPE(frame_ctrl));
      goto frame_drop;
    }

    /* Check if it is DEAUTH frame, destinated for WDS peer AP. Confirm and drop it. */
    if (mtlk_vap_is_ap(vap_handle) &&
        BR_MODE_WDS == MTLK_CORE_HOT_PATH_PDB_GET_INT(mtlk_vap_get_core(vap_handle), CORE_DB_CORE_BRIDGE_MODE)) {
      if ((subtype == MAN_TYPE_DEAUTH) && (core_wds_frame_drop(mtlk_vap_get_core(vap_handle), addr1)))
          goto frame_drop;
    }

    if (MAN_TYPE_ACTION == subtype) {
      action_code = *(buf + sizeof(frame_head_t));
    }
    if (MAN_TYPE_PROBE_RES == subtype) {
      /* find BSS load ie if exist and update it*/
      const u8 *ies_data = buf + sizeof(frame_head_t) + sizeof(frame_probe_resp_head_t);
      int       ies_len = len - sizeof(frame_head_t) - sizeof(frame_probe_resp_head_t);
      ie_bss_load_t *ie_bss = (ie_bss_load_t *)cfg80211_find_ie(IE_BSS_LOAD, ies_data, ies_len);
      uint8     ch_util;
      if((ie_bss) && (ie_bss->length == 5)) {
        /* update parameters */
        ie_bss->station_cnt = HOST_TO_WLAN16(mtlk_stadb_stas_num(&cur_core->slow_ctx->stadb)); /* max supporting sta's=256 */
        ie_bss->admission_capacity = 0;
        ch_util = _wave_hw_radio_get_chan_util(hw, radio_id);
        ie_bss->channel_utilization = MTLK_PERCENT_TO_VALUE(ch_util, MAX_UINT8);
      }
    }
  }

  if (PROCESS_NULL_DATA_PACKET == extra_processing) {
    IEEE_ADDR *addr1;

    if (!_mtlk_mmb_is_dst_sta_valid(vap_handle, buf, __LINE__)) goto frame_drop;

    addr1 = (IEEE_ADDR *)WLAN_GET_ADDR1((uint8 *)buf);
    dst_addr = *addr1;
  }

  mtlk_osal_lock_acquire(&bss_mgmt->tx_bdr.lock);

  if (!dont_wait_for_ack) {
    local_cookie = ++bss_mgmt->cookie_counter; /* at 4 billion mgmt frames per second, wraps in over 136 years */

    if (__UNLIKELY(local_cookie == 0)) /* waste a cycle or two to have the peace of mind beyond those 136 years */
      local_cookie = ++bss_mgmt->cookie_counter;
  }

  entry = mtlk_dlist_pop_front(&bss_mgmt->tx_bdr.free_list);
  mtlk_mmb_bss_mgmt_update_free_list_stat(bss_mgmt);
  if (__LIKELY(entry)) {
    mtlk_dlist_push_back(&bss_mgmt->tx_bdr.used_list, entry);
    mtlk_osal_lock_release(&bss_mgmt->tx_bdr.lock);
  } else {
    /* No items on the free list, so try the reserve free list, we still have the lock */
    entry = mtlk_dlist_pop_front(&bss_mgmt->tx_res_bdr.free_list);
    mtlk_mmb_bss_mgmt_update_res_free_list_stat(bss_mgmt);
    ILOG2_D("Popped a free reserve entry, %i remaining", mtlk_dlist_size(&bss_mgmt->tx_res_bdr.free_list));
    mtlk_osal_lock_release(&bss_mgmt->tx_bdr.lock);

    if (__LIKELY(entry)) {
      mtlk_core_on_bss_res_queued(cur_core); /* update counter */
    } else {
      /* the reserve free list was empty, too, so everything is full, can't do anything */
      mtlk_core_on_bss_drop_tx_que_full(cur_core);

      ILOG1_V("Both BSS TX queues full, dropping the frame");

      return MTLK_ERR_NO_MEM;
    }

    transmit_now = FALSE;
    /* We got the reserve struct OK, so now we'll copy the data and other valuable info to it.
     * Note that we may not enqueue it back until this is all done,
     * or else the tasklet could grab it from there with incomplete info (and crash).
     */
  }

  bss_req = MTLK_LIST_GET_CONTAINING_RECORD(entry, struct mtlk_hw_bss_req_mirror, hdr.list_entry);

  bss_req->vap_id = mtlk_vap_get_id(vap_handle);
  bss_req->radio_id = radio_id;
  bss_req->cookie = local_cookie;
  wave_memcpy(bss_req->virt_addr, BSS_MGMT_FRAME_SIZE, buf, len);
  bss_req->size = len;
  bss_req->extra_processing = extra_processing;
  bss_req->action_code      = action_code;
  bss_req->subtype          = subtype;
  bss_req->is_broadcast     = is_broadcast;
  bss_req->dst_addr         = dst_addr;
  bss_req->skb              = skb;
  bss_req->power_mgmt_on    = power_mgmt_on;
  bss_req->tid              = tid;
  if (transmit_now)
  {
    _mtlk_mmb_bss_mgmt_tx(hw, bss_req, vap_handle);
  }
  else
  {
    ILOG3_DDHPD("Reserve queue bss_req: RadioID=%u, VapID=%u, cookie=%llu, virt_addr=0x%p, size=%u",
                bss_req->radio_id, bss_req->vap_id, bss_req->cookie, bss_req->virt_addr, bss_req->size);

    /* Note that we don't use the tx_res_bdr.lock but the tx_bdr.lock,
    * because we want both queues protected by the same lock, because they work together
    */
    mtlk_osal_lock_acquire(&bss_mgmt->tx_bdr.lock);
    mtlk_dlist_push_back(&bss_mgmt->tx_res_bdr.used_list, entry);
    mtlk_osal_lock_release(&bss_mgmt->tx_bdr.lock);

    ILOG3_P("Pushed reserve queue entry: 0x%p", entry);
  }

  *cookie = local_cookie;
  return MTLK_ERR_OK;

frame_drop:
  ILOG0_V("Frame dropped");
  if (!dont_wait_for_ack)
  {
    if (mtlk_vap_is_ap(vap_handle))
    {
      mtlk_df_t *df = mtlk_vap_get_df(vap_handle);
      mtlk_df_user_t *df_user = mtlk_df_get_user(df);
      struct wireless_dev *wdev = mtlk_df_user_get_wdev(df_user);
      MTLK_ASSERT(NULL != wdev);
      cfg80211_mgmt_tx_status(wdev,local_cookie,buf,len,TRUE,GFP_ATOMIC);
    }
    else
    {
      struct ieee80211_hw *mac80211_hw = wave_radio_ieee80211_hw_get(radio);
      MTLK_ASSERT(NULL != mac80211_hw);
      MTLK_ASSERT(NULL != skb);
      wv_ieee80211_tx_status(mac80211_hw, skb, FALSE);
    }
  }
  return MTLK_ERR_OK;
}

/* This runs from the tasklet context, and possibly on a different CPU
 * than _mgmt_tx(), therefore a lock is needed in it (a simple spin_lock() will do) */
int _mtlk_mmb_handle_bss_cfm(mtlk_hw_t *hw, HOST_DSC *pHD)
{
  struct mtlk_bss_management *bss_mgmt = &hw->bss_mgmt;
  struct mtlk_hw_bss_req_mirror *bss_req, *bss_req_res;
  struct wireless_dev *wdev;
  struct net_device *ndev;
  frame_head_t *wifi_header;
  mtlk_df_t *df;
  mtlk_df_user_t *df_user;
  mtlk_dlist_entry_t *entry;
  uint8 *tmp_ptr;
  uint32 u32BdIndex    = HD_BD_INDEX_GET(pHD);
  uint32 u32frameInfo0 = LE32_TO_CPU(pHD->u32frameInfo0);
  uint32 u32frameInfo1 = LE32_TO_CPU(pHD->u32frameInfo1);
  uint8  vap_id, radio_id;
  uint8  status = MTLK_BFIELD_GET(u32frameInfo0, TX_BSS_EXTRA_STATUS);
  mtlk_vap_handle_t    vap_handle, vap_handle_res;
  wave_radio_t *radio;

  MTLK_ASSERT(hw);

  __wave_hw_rx_hd_decode(hw,
                         MTLK_BFIELD_GET(u32frameInfo1, TX_DATA_INFO_EP),
                         MTLK_BFIELD_GET(u32frameInfo0, TX_DATA_INFO_VAPID),
                         &radio_id,
                         &vap_id);

  radio = __mtlk_hw_wave_radio_get(hw, radio_id);
  MTLK_ASSERT(radio);
  if(__UNLIKELY(!radio)) {
    ELOG_DD("CID-%02x: Radio is NULL for RadioID %u", hw->card_idx, radio_id);
    goto the_trap;
  }

  ILOG3_D("BSS CFM: index=%hu", u32BdIndex);

  mtlk_osal_atomic_set(&bss_mgmt->tx_ring_last_cfm_ts, mtlk_osal_timestamp());
  bss_req = _mtlk_basic_bdr_get_mirror_bd(&bss_mgmt->tx_bdr.basic, u32BdIndex, struct mtlk_hw_bss_req_mirror);

#ifdef BD_DBG
  if (u32BdIndex >= bss_mgmt->tx_bdr.basic.nof_bds) {
    ELOG_DD("Index out of bounds: %d >= %d", u32BdIndex, bss_mgmt->tx_bdr.basic.nof_bds);
    goto the_trap;
  }
  if (0 == test_and_clear_bit(u32BdIndex, bss_mgmt->used_bd_map)) {
    ILOG3_D("Unused BD returned on BSS CFM, index %d\n", u32BdIndex);
    goto the_trap;
  }
  if (radio_id != bss_req->radio_id) {
    ELOG_DD("Received bss frame cfm with wrong RADIO_ID expected:%d current:%d", bss_req->radio_id, radio_id);
    goto the_trap;
  }
  if (vap_id != bss_req->vap_id) {
    ELOG_DD("Received bss frame cfm with wrong VAP_ID expected:%d current:%d", bss_req->vap_id, vap_id);
    goto the_trap;
  }
#endif

  /* Confirmations can be received after VAP was traffic stopped */
  if (__UNLIKELY(_mtlk_mmb_get_vap_is_not_ready(hw, radio_id, vap_id))) {
    ILOG2_DD("Received bss frame cfm when VAP traffic was stopped RadioID:%u VapID:%u",
             radio_id, vap_id);
  }

  vap_handle = _mtlk_mmb_get_vap_handle_from_vap_id(hw, radio_id, vap_id);
  if (__UNLIKELY(!vap_handle)) {
    ELOG_DD("Received bss frame cfm when VAP was removed RadioID:%u VapID:%u", radio_id, vap_id);
    goto the_trap;
  }

  ILOG3_DDHPDD("Confirmed bss_req: RadioID=%u VapID=%u, cookie=%llu, virt_addr=0x%p, size=%u, dma_addr=0x%08x",
              bss_req->radio_id, bss_req->vap_id, bss_req->cookie, bss_req->virt_addr,
              bss_req->size, bss_req->dma_addr);

  df = mtlk_vap_get_df(vap_handle); /* this checks vap_handle and df, cannot return NULL */
  df_user = mtlk_df_get_user(df);
  ndev = mtlk_df_user_get_ndev(df_user);
  MTLK_ASSERT(NULL != ndev);
  wifi_header = (frame_head_t *)bss_req->virt_addr;

  mtlk_osal_unmap_phys_addr(mtlk_ccr_get_dev_ctx(hw->ccr), bss_req->dma_addr, bss_req->size, MTLK_DATA_TO_DEVICE);

  if (PROCESS_MANAGEMENT == bss_req->extra_processing)
  {
    if (bss_req->cookie)
    {
      if (mtlk_vap_is_ap(vap_handle))
      { /* if the userspace app is waiting for the TX status to be reported */
        wdev = mtlk_df_user_get_wdev(df_user); /* this checks df_user */
        MTLK_ASSERT(NULL != wdev);
        cfg80211_mgmt_tx_status(wdev, bss_req->cookie, bss_req->virt_addr, bss_req->size,
                                (BOOL)status, GFP_ATOMIC);
      }
      else
      {
        struct ieee80211_hw *mac80211_hw = wave_radio_ieee80211_hw_get(radio);
        ILOG2_D("handle bss mgmt cfm status = %u", status);
        MTLK_ASSERT(NULL != mac80211_hw);
        wv_ieee80211_tx_status(mac80211_hw, bss_req->skb, status);
      }
    }
  }

  if (PROCESS_NULL_DATA_PACKET == bss_req->extra_processing)
  {
    if (mtlk_vap_is_ap(vap_handle))
    {
      cfg80211_probe_status(ndev, wifi_header->dst_addr.au8Addr, bss_req->cookie,
                            (BOOL)status, 0, false, GFP_ATOMIC);
    }
    else
    {
      if (bss_req->cookie && bss_req->skb)
      {
        struct ieee80211_hw *mac80211_hw = wave_radio_ieee80211_hw_get(radio);
        ILOG2_D("handle bss null data packet cfm status = %u", status);
        MTLK_ASSERT(NULL != mac80211_hw);
        wv_ieee80211_tx_status(mac80211_hw, bss_req->skb, status);
      }
    }
  }

  /* First do the processing of cfm prior to handling of reserved or main queues */
  mtlk_core_on_bss_cfm(mtlk_vap_get_core(vap_handle), &bss_req->dst_addr, bss_req->extra_processing, bss_req->subtype, bss_req->is_broadcast);

  /* Note that we don't use the tx_res_bdr.lock but the tx_bdr.lock,
   * because we want both queues protected by the same lock, because they work together
   */
  mtlk_osal_lock_acquire(&bss_mgmt->tx_bdr.lock);

  while ((entry = mtlk_dlist_pop_front(&bss_mgmt->tx_res_bdr.used_list)) != NULL)
  {
    ILOG3_P("Popped reserve entry: 0x%p", entry);

    /* We just popped an entry from the reserve queue,
     * so swap the buffer pointers with the done entry, add the cookie and vap_handle,
     * and submit it back to the FW w/o ever taking the done entry off the used_list.
     */
    bss_req_res = MTLK_LIST_GET_CONTAINING_RECORD(entry, struct mtlk_hw_bss_req_mirror, hdr.list_entry);
    ILOG3_DDHPDD("Popped bss_req_res: RadioID=%u, VapID=%u, cookie=%llu, virt_addr=0x%p, size=%u, dma_addr=0x%08x",
                bss_req_res->radio_id, bss_req_res->vap_id, bss_req_res->cookie, bss_req_res->virt_addr,
                bss_req_res->size, bss_req_res->dma_addr);

    /* swap buffers */
    tmp_ptr = bss_req->virt_addr;
    bss_req->virt_addr = bss_req_res->virt_addr;
    bss_req_res->virt_addr = tmp_ptr;

    /* copy the relevant stuff */
    bss_req->size = bss_req_res->size;
    bss_req->cookie = bss_req_res->cookie;
    bss_req->vap_id = bss_req_res->vap_id;
    bss_req->radio_id = bss_req_res->radio_id;
    bss_req->extra_processing = bss_req_res->extra_processing;
    bss_req->action_code      = bss_req_res->action_code;
    bss_req->subtype          = bss_req_res->subtype;
    bss_req->is_broadcast     = bss_req_res->is_broadcast;
    bss_req->dst_addr         = bss_req_res->dst_addr;
    bss_req->skb              = bss_req_res->skb;

    mtlk_dlist_push_back(&bss_mgmt->tx_res_bdr.free_list, entry); /* push onto reserve free list */
    ILOG3_PD("Pushed entry onto reserve free list: 0x%p, %i remaining",
        entry, mtlk_dlist_size(&bss_mgmt->tx_res_bdr.free_list));

    ILOG3_DDHPD("Swapped bss_req: RadioID:%u VapID:%u cookie:%llu, virt_addr:0x%p, size:%u",
                bss_req->radio_id, bss_req->vap_id, bss_req->cookie, bss_req->virt_addr,
                bss_req->size);

    /* All BSS frames should be sent to FW even VAP traffic was stopped */
    if (__UNLIKELY(_mtlk_mmb_get_vap_is_not_ready(hw, bss_req->radio_id, bss_req->vap_id))) {
      ILOG2_DD("Sending bss frame from reserved queue after VAP traffic was stopped RadioID:%u VapID:%u",
                bss_req->radio_id, bss_req->vap_id);
    }

    /* WAVE600: TODO: do it for the first radio now, but rework later! */
    vap_handle_res = _mtlk_mmb_get_vap_handle_from_vap_id(hw, bss_req->radio_id, bss_req->vap_id);
    if (__UNLIKELY(!vap_handle_res)) {
      WLOG_DD("Drop bss frame from reserved queue due to VAP was removed RadioID:%u VapID:%u",
               bss_req->radio_id, bss_req->vap_id);
      continue;
    }

    mtlk_core_on_bss_res_freed(mtlk_vap_get_core(vap_handle_res)); /* update counter */

    /* core net state is NET_STATE_DEACTIVATING in case of VAP traffic was stopped */
    if (0 == ((NET_STATE_CONNECTED | NET_STATE_DEACTIVATING) &
        mtlk_core_get_net_state(mtlk_vap_get_core(vap_handle_res)))) {
      WLOG_DD("Drop bss frame from reserved queue due to VAP is not active RadioID:%u VapID:%u",
               bss_req->radio_id, bss_req->vap_id);
      continue;
    }

    if (MTLK_ERR_OK != _mtlk_mmb_bss_mgmt_tx(hw, bss_req, vap_handle_res)) {
      continue;
    }

    mtlk_osal_lock_release(&bss_mgmt->tx_bdr.lock);

    return MTLK_ERR_OK;
  }

  /* No frames to send from reserved queue */
  mtlk_dlist_remove(&bss_mgmt->tx_bdr.used_list, &bss_req->hdr.list_entry);
  mtlk_dlist_push_back(&bss_mgmt->tx_bdr.free_list, &bss_req->hdr.list_entry);
  mtlk_osal_lock_release(&bss_mgmt->tx_bdr.lock);
  return MTLK_ERR_OK;

the_trap:
  mtlk_hw_set_prop(wave_radio_descr_hw_api_get(hw->radio_descr, radio_id), MTLK_HW_RESET, NULL, 0);
  return MTLK_ERR_UNKNOWN; /* BUG(); */
}

void __MTLK_IFUNC
hw_get_fw_version (mtlk_handle_t hw_handle, char *fw_version, uint32 size)
{
  char *pChr;

  mtlk_hw_t *hw = HANDLE_T_PTR(mtlk_hw_t, hw_handle);
  MTLK_ASSERT(hw);

  fw_version[0] = 0;

  pChr = strstr(hw->fw_version, "FW_");
  if (pChr == NULL) {
    return;
  }

  fw_version[size - 1] = 0;

  if (0 > snprintf(fw_version, size - 1, "%s", pChr)) {
    return;
  }

  pChr = strchr(fw_version, ' ');
  if (pChr) {
    *pChr = 0;
  }
}

uint32 count_bits_set (uint32 v)
{
  unsigned c;

  for (c = 0; v; v >>= 1)
    c += v & 1;

  return c;
}

uint32 hw_get_max_tx_antennas (mtlk_hw_t *hw)
{
  MTLK_ASSERT(hw);

  return hw->tx_ant_num;
}

uint32 hw_get_tx_antenna_mask (mtlk_hw_t *hw)
{
  MTLK_ASSERT(hw);

  return hw->tx_ant_mask;
}

uint32 hw_get_max_rx_antennas (mtlk_hw_t *hw)
{
  MTLK_ASSERT(hw);

  return hw->rx_ant_num;
}

uint32 hw_get_rx_antenna_mask (mtlk_hw_t *hw)
{
  MTLK_ASSERT(hw);

  return hw->rx_ant_mask;
}

/* Fix antenna mask so that the number of "1" bits in it doesn't exceed num_antennas.
 * Set the lowest num_antennas bits in full_mask */
uint32 hw_get_antenna_mask (uint32 full_mask, uint32 num_antennas)
{
  uint32 cur_bit, cur_mask, result = 0;

  /* Do while "1" in cur_mask isn't shifted out and the number of antennas isn't exceeded */
  for (cur_bit = 0, cur_mask = 1; num_antennas && (cur_bit < (sizeof(result) * 8)); cur_bit++, cur_mask <<= 1) {
    result |= full_mask & cur_mask;
    if (full_mask & cur_mask) num_antennas--;
  }
  return result;
}


#define MAX_NON_WAVE500_STS 2

uint32 __MTLK_IFUNC
wave_hw_get_num_sts_by_ant_mask (mtlk_hw_t *hw, uint32 ant_mask)
{
  uint32 num_sts;
  MTLK_ASSERT(hw);
  if (!hw) {
      return 0;
  }

  num_sts = wave_hw_get_ant_num_by_ant_mask(ant_mask);

  return num_sts;
}

void __MTLK_IFUNC
hw_get_hw_version (mtlk_handle_t hw_handle, uint32 *hw_version)
{
  mtlk_hw_t         *hw = HANDLE_T_PTR(mtlk_hw_t, hw_handle);
  uint16 device_id;
  uint8 hw_type, hw_revision;

  MTLK_ASSERT(hw && hw->ee_data);

  if (hw && hw->ee_data && hw_version) {
    device_id   = MAC_TO_HOST16(mtlk_eeprom_get_device_id(hw->ee_data));
    hw_type     = mtlk_eeprom_get_nic_type(hw->ee_data);
    hw_revision = mtlk_eeprom_get_nic_revision(hw->ee_data);

    *hw_version = ((device_id << 16) | (hw_type << 8) | hw_revision);
  }
}

/* FIXME: is supported on gen6 ? */
BOOL __MTLK_IFUNC
hw_get_ldpc_support(mtlk_handle_t hw_handle)
{
  mtlk_hw_t *hw = HANDLE_T_PTR(mtlk_hw_t, hw_handle);

  MTLK_ASSERT(hw);
  MTLK_UNREFERENCED_PARAM(hw);

  return TRUE;
}

BOOL __MTLK_IFUNC
hw_get_ampdu_64k_support(mtlk_handle_t hw_handle)
{
  mtlk_hw_t *hw = HANDLE_T_PTR(mtlk_hw_t, hw_handle);
  MTLK_ASSERT(hw);
  return (_hw_type_is_gen5(hw) || _hw_type_is_gen6(hw));
}

BOOL __MTLK_IFUNC
hw_get_ampdu_density_restriction(mtlk_handle_t hw_handle)
{
  mtlk_hw_t *hw = HANDLE_T_PTR(mtlk_hw_t, hw_handle);
  MTLK_ASSERT(hw);
  return (!_hw_type_is_gen5(hw) && !_hw_type_is_gen6(hw));
}

BOOL __MTLK_IFUNC
hw_get_rx_stbc_support(mtlk_handle_t hw_handle)
{
  mtlk_hw_t *hw = HANDLE_T_PTR(mtlk_hw_t, hw_handle);
  MTLK_ASSERT(hw);
  return _hw_type_is_gen5(hw);
}

BOOL __MTLK_IFUNC
hw_get_vht_support(mtlk_handle_t hw_handle)
{
  mtlk_hw_t *hw = HANDLE_T_PTR(mtlk_hw_t, hw_handle);
  MTLK_ASSERT(hw);
  return (_hw_type_is_gen5(hw) || _hw_type_is_gen6(hw));
}

BOOL __MTLK_IFUNC
hw_get_vht_160mhz_support(mtlk_handle_t hw_handle)
{
  mtlk_hw_t *hw = HANDLE_T_PTR(mtlk_hw_t, hw_handle);
  MTLK_ASSERT(hw);
  return (_hw_type_is_gen6(hw));
}

BOOL __MTLK_IFUNC
hw_get_he_support(mtlk_handle_t hw_handle)
{
  mtlk_hw_t *hw = HANDLE_T_PTR(mtlk_hw_t, hw_handle);
  MTLK_ASSERT(hw);
  return (_hw_type_is_gen6(hw));
}

BOOL __MTLK_IFUNC
hw_get_160mhz_short_gi_support(mtlk_handle_t hw_handle)
{
  mtlk_hw_t *hw = HANDLE_T_PTR(mtlk_hw_t, hw_handle);
  MTLK_ASSERT(hw);
  return (_hw_type_is_gen6(hw));
}

BOOL __MTLK_IFUNC
hw_get_gcmp_support(mtlk_handle_t hw_handle)
{
  mtlk_hw_t *hw = HANDLE_T_PTR(mtlk_hw_t, hw_handle);
  MTLK_ASSERT(hw);
  return (_hw_type_is_gen6(hw));
}

void __MTLK_IFUNC
mtlk_hw_mmb_set_msi_intr_mode(mtlk_hw_t *hw, mtlk_irq_mode_e mode)
{
  /* Save irq_mode for recovery */
  hw->irq_mode = mode;

  /* Set MSI registers on ASIC and Emul, and not set on FPGA */
  if (!__hw_mmb_card_is_fpga(hw)) {
    mtlk_ccr_pcie_set_msi_intr_mode(hw->ccr, mode);
  } else {
    ILOG0_V("Skip MSI registers setting on FPGA");
  }
}

BOOL mtlk_is_band_supported (mtlk_handle_t hw_handle, unsigned radio_id, mtlk_hw_band_e band)
{
  unsigned   hw_type, hw_vers;
  BOOL       res;
  mtlk_hw_t *hw = HANDLE_T_PTR(mtlk_hw_t, hw_handle);
  MTLK_ASSERT(hw);

  hw_type = hw->card_info.hw_type;
  hw_vers = hw->card_info.hw_vers;

  /* Ignore EEPROM band support which can be defined for both bands (radios) */
  /* Use only PSDB band support for the radio band configuration */
  res  = wave_psdb_is_radio_band_supported (_mtlk_hw_get_psdb(hw), radio_id, band);

  ILOG2_DDD("RadioID %u: band %u supported:%d", radio_id, band, (int)res);

  return res;
}

/*-----------------------------------------------------------------------------*/
/* Platform Specific Data
 */

int __MTLK_IFUNC
mtlk_hw_psdb_send_fields (mtlk_hw_t *hw, mtlk_txmm_t *txmm, uint32 *wbuf, int nof_words)
{
    mtlk_txmm_msg_t       man_msg;
    mtlk_txmm_data_t     *man_entry;
    platformDataFields_t *umi_params;
    int                   res = MTLK_ERR_FW;
    int                   i;

    man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, txmm, NULL);
    if (!man_entry) {
      ELOG_V("Can not get TXMM slot");
      return MTLK_ERR_NO_RESOURCES;
    }

    man_entry->id = UM_MAN_PLATFORM_DATA_FIELDS_REQ;
    man_entry->payload_size = nof_words * sizeof(uint32);

    umi_params  = (platformDataFields_t *)man_entry->payload;

    /* All words already in MAC endian */
    for (i = 0; i < nof_words; i++) {
        umi_params->dataFields[i] = wbuf[i];
    }

    ILOG2_V("Sending PLATFORM_DATA_FIELDS");
    mtlk_dump(2, umi_params, man_entry->payload_size, "dump of platformDataFields_t");

    res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);
    if (res != MTLK_ERR_OK) {
        ELOG_D("Sending PLATFORM_DATA_FIELDS failure (err=%d)", res);
    }

    if (man_entry) {
        mtlk_txmm_msg_cleanup(&man_msg);
    }

    return res;
}

int __MTLK_IFUNC
mtlk_hw_psdb_send_table (mtlk_hw_t *hw, mtlk_txmm_t *txmm, uint32 table_id, uint32 *wbuf, int nof_words)
{
    mtlk_txmm_msg_t        man_msg;
    mtlk_txmm_data_t      *man_entry;
    int                    res       = MTLK_ERR_FW;
    unsigned int           size;

    man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, txmm, NULL);
    if (!man_entry) {
      ELOG_V("Can not get TXMM slot");
      goto FINISH;
    }

    size = nof_words * sizeof(uint32);
    MTLK_ASSERT(size <= PROGMODEL_CHUNK_SIZE);

    _mtlk_mmb_pas_put(hw, "", _mtlk_mmb_get_shram_progmodel_addr(hw), wbuf, size);

    man_entry->id           = UM_MAN_PLATFORM_TABLE_REQ;
    man_entry->payload_size = sizeof(platformTable_t); /* single word */
    ((platformTable_t *)man_entry->payload)->tableID = HOST_TO_MAC32(table_id);

    ILOG1_D("PSDB_TABLE_REQ table_id 0x%x", table_id);
    mtlk_dump(2, wbuf, MIN(128, size), "dump of table");

    res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_PRGMDL_LOAD_CHUNK_TIMEOUT);
    if (res != MTLK_ERR_OK) {
      ELOG_D("Can't download PSD table, timed-out. Err#%d", res);
      goto FINISH;
    }

    res = MTLK_ERR_OK;

FINISH:
    if (man_entry)
      mtlk_txmm_msg_cleanup(&man_msg);

    return res;
}

int __MTLK_IFUNC
mtlk_hw_load_file_by_hw (mtlk_hw_t *hw, char *fname, mtlk_df_fw_file_buf_t *fb)
{
    MTLK_ASSERT(hw);
    /* WAVE600: TODO: do it for the first radio now, but rework later! */
    return mtlk_hw_load_file(wave_radio_descr_hw_api_get(hw->radio_descr, 0), fname, fb);
}

void __MTLK_IFUNC
mtlk_hw_unload_file_by_hw (mtlk_hw_t *hw, mtlk_df_fw_file_buf_t *fb)
{
    MTLK_ASSERT(hw);
    /* WAVE600: TODO: do it for the first radio now, but rework later! */
    mtlk_hw_unload_file(wave_radio_descr_hw_api_get(hw->radio_descr, 0), fb);
}

mtlk_eeprom_data_t * __MTLK_IFUNC
mtlk_hw_get_eeprom(mtlk_hw_t *hw)
{
    MTLK_ASSERT(hw);
    return hw->ee_data;
}

void __MTLK_IFUNC
mtlk_eeprom_psdb_parse_stop(mtlk_hw_t *hw)
{
  MTLK_STOP_BEGIN(hw_mmb_eeprom, MTLK_OBJ_PTR(&hw->hw_start_eeprom))
    MTLK_STOP_STEP(hw_mmb_eeprom, HW_PSDB_READ, MTLK_OBJ_PTR(&hw->hw_start_eeprom),
                   _mtlk_mmb_psdb_cleanup, (hw));
    MTLK_STOP_STEP(hw_mmb_eeprom, HW_EEPROM_READ, MTLK_OBJ_PTR(&hw->hw_start_eeprom),
                   mtlk_clean_eeprom_data, (hw->ee_data));
  MTLK_STOP_END(hw_mmb_eeprom, MTLK_OBJ_PTR(&hw->hw_start_eeprom))

  /* delete eeprom */
  mtlk_eeprom_delete(hw->ee_data);
}

int __MTLK_IFUNC
mtlk_eeprom_psdb_read_and_parse (mtlk_hw_t *hw)
{
  mtlk_hw_api_t hw_api;

  MTLK_ASSERT(hw != NULL);

  hw_api.hw = hw;
  hw_api.vft = &hw_mmb_vft;

  /* create eeprom */
  hw->ee_data = mtlk_eeprom_create();
  if (NULL == hw->ee_data)
    return MTLK_ERR_NO_MEM;

  MTLK_START_TRY(hw_mmb_eeprom, MTLK_OBJ_PTR(&hw->hw_start_eeprom))
    MTLK_START_STEP(hw_mmb_eeprom, HW_EEPROM_READ, MTLK_OBJ_PTR(&hw->hw_start_eeprom),
                    mtlk_eeprom_read_and_parse, (&hw_api, hw->ee_data, hw->ccr->chip_info->id));

    _mtlk_hw_fill_card_info(hw, &hw->card_info);
    _wave_hw_cfg_platform_type_set(hw); /* init platform type */

    MTLK_START_STEP(hw_mmb_eeprom, HW_PSDB_READ, MTLK_OBJ_PTR(&hw->hw_start_eeprom),
                    _mtlk_mmb_psdb_read_and_parse, (hw, &hw_api));
  MTLK_START_FINALLY(hw_mmb_eeprom, MTLK_OBJ_PTR(&hw->hw_start_eeprom));
  MTLK_START_RETURN(hw_mmb_eeprom, MTLK_OBJ_PTR(&hw->hw_start_eeprom), mtlk_eeprom_psdb_parse_stop, (hw));
}

uint8 __MTLK_IFUNC
mtlk_hw_eeprom_get_nic_type(mtlk_hw_t *hw)
{
  MTLK_ASSERT(hw != NULL);
  return mtlk_eeprom_get_nic_type(hw->ee_data);
}

BOOL wave_hw_mmb_eeprom_is_production_mode (mtlk_hw_t *hw)
{
  MTLK_ASSERT(hw != NULL);
  return wave_eeprom_is_production_mode(wave_eeprom_cal_file_type_get(hw->ee_data));
}

static uint8 *
_wave_hw_get_afe_data (mtlk_hw_t *hw, unsigned *size)
{
    return mtlk_eeprom_get_afe_data(hw->ee_data, size);
}

uint8 * __MTLK_IFUNC
wave_hw_get_afe_data (mtlk_hw_t *hw, unsigned *size)
{
    MTLK_ASSERT(hw);
    return _wave_hw_get_afe_data(hw, size);
}

static int
mtlk_hw_send_cal_afe_data(mtlk_hw_t *hw, mtlk_txmm_t *txmm)
{
    mtlk_txmm_msg_t       man_msg;
    mtlk_txmm_data_t     *man_entry;
    afe_calibration_data_t *umi_params;
    uint8                *data;
    unsigned              size = 0;
    int                   res = MTLK_ERR_FW;

    data = _wave_hw_get_afe_data(hw, &size);
    if (size == 0) {
        ILOG0_V("AFE calibration data missing");
        return MTLK_ERR_OK;
    }

    MTLK_ASSERT(NULL != data);
    /* AFE data size was checked during EEPROM parsing */

    man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, txmm, NULL);
    if (!man_entry) {
      ELOG_V("Can not get TXMM slot");
      return MTLK_ERR_NO_RESOURCES;
    }

    man_entry->id = UM_MAN_SET_AFE_CALIBRATION_DATA_REQ;
    man_entry->payload_size = size;

    umi_params  = (afe_calibration_data_t *)man_entry->payload;
    wave_memcpy(umi_params->calibrationData, sizeof(umi_params->calibrationData), data, size);

    mtlk_dump(2, umi_params, sizeof(*umi_params), "dump of afe_calibration_data_t");

    res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);
    if (res != MTLK_ERR_OK) {
        ELOG_D("SET_AFE_CALIBRATION_DATA_REQ failure (err=%d)", res);
    }

    mtlk_txmm_msg_cleanup(&man_msg);

    return res;
}

static int
mtlk_hw_send_cal_rfic_data(mtlk_hw_t *hw, mtlk_txmm_t *txmm)
{
    mtlk_txmm_msg_t       man_msg;
    mtlk_txmm_data_t     *man_entry;
    rfic_calibration_data_t *umi_params;
    uint8                *data;
    unsigned              size = 0;
    int                   res = MTLK_ERR_FW;

    data = mtlk_eeprom_get_rfic_data(hw->ee_data, &size);
    if (size == 0) {
        ILOG0_V("RFIC calibration data missing");
        return MTLK_ERR_OK;
    }

    MTLK_ASSERT(NULL != data);
    /* RFIC data size was checked during EEPROM parsing */

    man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, txmm, NULL);
    if (!man_entry) {
      ELOG_V("Can not get TXMM slot");
      return MTLK_ERR_NO_RESOURCES;
    }

    man_entry->id = UM_MAN_SET_RFIC_CALIBRATION_DATA_REQ;
    man_entry->payload_size = size;

    umi_params  = (rfic_calibration_data_t *)man_entry->payload;
    umi_params->rficCisSize = size;
    wave_memcpy(umi_params->rficCis, sizeof(umi_params->rficCis), data, size);

    mtlk_dump(2, umi_params->rficCis, size, "dump of rficCis");

    res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);
    if (res != MTLK_ERR_OK) {
        ELOG_D("SET_RFIC_CALIBRATION_DATA_REQ failure (err=%d)", res);
    }

    mtlk_txmm_msg_cleanup(&man_msg);

    return res;
}

static int
_mtlk_hw_send_cal_rssi_data (mtlk_hw_t *hw, mtlk_txmm_t *txmm, mtlk_hw_band_e band)
{
    mtlk_txmm_msg_t       man_msg;
    mtlk_txmm_data_t     *man_entry;
    UMI_SET_RSSI_CAL_CONFIG *umi_params;
    uint8                *data;
    unsigned              size = 0;
    int                   res = MTLK_ERR_FW;

    data = mtlk_eeprom_get_rssi_data(hw->ee_data, &size, band);
    if (size == 0) {
        ILOG0_D("RSSI calibration data missing for band %d", (int)band);
        return MTLK_ERR_OK;
    }

    MTLK_ASSERT(NULL != data);
    /* RSSI data size was checked during EEPROM parsing */

    man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, txmm, NULL);
    if (!man_entry) {
      ELOG_V("Can not get TXMM slot");
      return MTLK_ERR_NO_RESOURCES;
    }

    man_entry->id = UM_MAN_SET_RSSI_CAL_PARAMS_REQ;
    man_entry->payload_size = size;

    umi_params  = (UMI_SET_RSSI_CAL_CONFIG *)man_entry->payload;
    umi_params->size = size;
    wave_memcpy(umi_params->data, sizeof(umi_params->data), data, size);

    mtlk_dump(2, umi_params->data, size, "dump of RSSI calibration data");

    res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);
    if (res != MTLK_ERR_OK) {
        ELOG_D("SET_RSSI_CAL_PARAMS_REQ failure (err=%d)", res);
    }

    mtlk_txmm_msg_cleanup(&man_msg);

    return res;
}

int __MTLK_IFUNC
mtlk_efuse_eeprom_load_to_hw (mtlk_hw_t *hw, mtlk_txmm_t *txmm, mtlk_hw_band_e band)
{
    int     res;

    /* Only for EEPROM new format */
    if (!mtlk_eeprom_is_new_format(hw->ee_data)) {
        return MTLK_ERR_OK;
    }

    if (MTLK_ERR_OK != (res = mtlk_hw_send_cal_afe_data(hw, txmm))) {
        return res;
    }

    if (MTLK_ERR_OK != (res = mtlk_hw_send_cal_rfic_data(hw, txmm))) {
        return res;
    }

    res = _mtlk_hw_send_cal_rssi_data(hw, txmm, band);

    return res;
}

static int
_mtlk_hw_fill_hdk_radio_ant_params (mtlk_hw_t *hw, UMI_HDK_SET_ANTENNA_REQ *umi_params, unsigned idx)
{
    antennaParameters_t     *umi_ant_params;
    wave_ant_params_t       *cfg_ant_params;

    cfg_ant_params = wave_psdb_get_radio_ant_params(_mtlk_hw_get_psdb(hw), idx);
    if (!cfg_ant_params) {
      ELOG_D("Antenna params missing for RadioID %u", idx);
      return MTLK_ERR_NOT_IN_USE;
    }

    umi_ant_params = &umi_params->antParams[idx];
    umi_ant_params->txAntMask           = cfg_ant_params->tx_ant_mask;
    umi_ant_params->rxAntMask           = cfg_ant_params->rx_ant_mask;
    umi_ant_params->txAntSelectionMask  = cfg_ant_params->tx_ant_sel_mask;
    umi_ant_params->rxAntSelectionMask  = cfg_ant_params->rx_ant_sel_mask;

    return MTLK_ERR_OK;
}

int __MTLK_IFUNC
mtlk_hw_send_hdk_antennas_config (mtlk_hw_t *hw)
{
    mtlk_txmm_msg_t          man_msg;
    mtlk_txmm_data_t        *man_entry;
    UMI_HDK_SET_ANTENNA_REQ *umi_params;
    int                      res = MTLK_ERR_OK;

    MTLK_ASSERT(hw);

    man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, &hw->txmm, NULL);
    if (!man_entry) {
      ELOG_V("Can not get TXMM slot");
      return MTLK_ERR_NO_RESOURCES;
    }

    man_entry->id = UM_MAN_HDK_ANT_CONFIG_REQ;

    man_entry->payload_size = sizeof(*umi_params);
    memset(man_entry->payload, 0, man_entry->payload_size);

    umi_params  = (UMI_HDK_SET_ANTENNA_REQ *)man_entry->payload;

    /* Fill data for both radios */
    res = _mtlk_hw_fill_hdk_radio_ant_params(hw, umi_params, 0);
    if (MTLK_ERR_OK == res) {
      res = _mtlk_hw_fill_hdk_radio_ant_params(hw, umi_params, 1);
    }

    if (MTLK_ERR_OK != res) {
      goto finish;
    }

    mtlk_dump(1, umi_params, sizeof(*umi_params), "dump of HDK_CDB_Antennas config");

    res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);
    if (res != MTLK_ERR_OK) {
        ELOG_D("HDK_CDB_ANT_CONFIG_REQ failure (err=%d)", res);
    }

finish:
    mtlk_txmm_msg_cleanup(&man_msg);

    return res;
}

void __MTLK_IFUNC
mtlk_hw_inc_radar_cntr (mtlk_hw_t *hw)
{
  MTLK_ASSERT(hw != NULL);

 mtlk_osal_atomic_inc(&hw->radars_detected);
}

void __MTLK_IFUNC
mtlk_hw_reset_radar_cntr (mtlk_hw_t *hw)
{
  MTLK_ASSERT(hw != NULL);

  mtlk_osal_atomic_set(&hw->radars_detected, 0);
}

#if MTLK_CCR_DEBUG_PRINT_INTR_REGS

static void _mtlk_hw_debug_print_ring_regs(mtlk_ring_regs *ring_regs, const char *reg_name)
{
    uint32 reg_OUT_status, reg_OUT_counter, reg_IN_counter;
    reg_OUT_status  = LE32_TO_CPU(mtlk_raw_readl(ring_regs->p_OUT_status));
    reg_OUT_counter = LE32_TO_CPU(mtlk_raw_readl(ring_regs->p_OUT_cntr));
    reg_IN_counter  = LE32_TO_CPU(mtlk_raw_readl(ring_regs->p_IN_cntr));
    WLOG_SPDPDPDD("<RING_REGS:%s> OUT_status(COUNT)@%p=%u, OUT_counter(SUB)@%p=%u, IN_counter(ADD)@%p=%u, int_OUT_cntr=%u",
      reg_name, ring_regs->p_OUT_status, reg_OUT_status, ring_regs->p_OUT_cntr, reg_OUT_counter, ring_regs->p_IN_cntr, reg_IN_counter, ring_regs->int_OUT_cntr);
}

void __MTLK_IFUNC
mtlk_hw_debug_print_all_ring_regs(mtlk_hw_t *hw)
{
    if  (!mtlk_mmb_fastpath_available(hw)) {
        _mtlk_hw_debug_print_ring_regs(&hw->tx.ring.regs,      "data_tx");
        _mtlk_hw_debug_print_ring_regs(&hw->rx.ring.regs,      "data_rx");
    }
    _mtlk_hw_debug_print_ring_regs(&hw->bss_mgmt.tx_ring.regs, "mgmt_tx");
    _mtlk_hw_debug_print_ring_regs(&hw->bss_mgmt.rx_ring.regs, "mgmt_rx");
    _mtlk_hw_debug_print_ring_regs(&hw->ctrl.ring.regs,        "   ctrl");
}
#endif

void* __MTLK_IFUNC wave_card_radio_descr_get(mtlk_hw_t *hw)
{
  MTLK_ASSERT(hw != NULL);
  return hw->radio_descr;
}

#define WAVE_HW_RX_HD_EP_MASK_GEN5            0x00
#define WAVE_HW_RX_HD_EP_MASK_GEN6_32         0x00
#define WAVE_HW_RX_HD_EP_MASK_GEN6_CDB_16_16  0x01
#define WAVE_HW_RX_HD_VAP_MASK_GEN5           0x0F
#define WAVE_HW_RX_HD_VAP_MASK_GEN6_32        0x1F
#define WAVE_HW_RX_HD_VAP_MASK_GEN6_CDB_16_16 0x0F

static char *band_cfg_to_str[] = {
  "WAVE_HW_RADIO_BAND_CFG_UNSUPPORTED",
  "WAVE_HW_RADIO_BAND_CFG_SB",
  "WAVE_HW_RADIO_BAND_CFG_DB_2x2",
  "WAVE_HW_RADIO_BAND_CFG_DB_3x1",
  "WAVE_HW_RADIO_BAND_CFG_SCAN",
  "WAVE_HW_RADIO_BAND_CFG_LAST"
};

static wave_radio_limits_t hw_radio_limits_sb_g5[] = {
  { 16, 128 },
  {  0,   0 }
};

static wave_radio_limits_t hw_radio_limits_sb_g6[] = {
  { 16, 255 }, /* Current limitation for Gen6 */
  {  0,   0 }
};

static wave_radio_limits_t hw_radio_limits_db[] = {
  { 8, 127 },  /* Current limitation for Gen6 */
  { 8, 127 }
};

static wave_radio_limits_t hw_radio_limits_sc[] = {
  { 16, 255 },  /* Current limitation for Gen6 */
  {  0,   0 }
};

int __MTLK_IFUNC
wave_hw_cfg_radio_number_get (mtlk_ccr_t *ccr, mtlk_hw_t *hw, unsigned *radio_number)
{
  MTLK_ASSERT(hw != NULL);
  if (hw) {
    wave_psdb_cfg_radio_number_get(_mtlk_hw_get_psdb(hw), radio_number);
    ILOG3_DD("CID-%02x: nof_radios %d", hw->card_idx, *radio_number);
    return MTLK_ERR_OK;
  } else {
    ELOG_V("HW is null");
    *radio_number = 0;
    return MTLK_ERR_UNKNOWN;
  }
}

static int _wave_hw_radio_band_cfg_init (mtlk_hw_t *hw)
{
  wave_radio_limits_t *p_radio_limits = NULL;
  char *band_str;

  MTLK_ASSERT(hw != NULL);
  MTLK_ASSERT(hw->radio_band_cfg != WAVE_HW_RADIO_BAND_CFG_UNSUPPORTED);

  switch (hw->radio_band_cfg) {
  case WAVE_HW_RADIO_BAND_CFG_SB:
    if (_hw_type_is_gen5(hw)) {
      hw->rx_hd_ep_mask = WAVE_HW_RX_HD_EP_MASK_GEN5;
      hw->rx_hd_vap_mask = WAVE_HW_RX_HD_VAP_MASK_GEN5;
      p_radio_limits = hw_radio_limits_sb_g5;
    }
    else if (_hw_type_is_gen6(hw)) {
      hw->rx_hd_ep_mask = WAVE_HW_RX_HD_EP_MASK_GEN6_32;
      hw->rx_hd_vap_mask = WAVE_HW_RX_HD_VAP_MASK_GEN6_32;
      p_radio_limits = hw_radio_limits_sb_g6;
    }
    hw->fw_card_cfg.card_cfg.band_cfg = BAND_CONFIGURATION_MODE_SINGLE_BAND;
    break;
  case WAVE_HW_RADIO_BAND_CFG_DB_2x2:
  case WAVE_HW_RADIO_BAND_CFG_DB_3x1:
    hw->rx_hd_ep_mask = WAVE_HW_RX_HD_EP_MASK_GEN6_CDB_16_16;
    hw->rx_hd_vap_mask = WAVE_HW_RX_HD_VAP_MASK_GEN6_CDB_16_16;
    hw->fw_card_cfg.card_cfg.band_cfg = BAND_CONFIGURATION_MODE_DUAL_BAND;
    p_radio_limits = hw_radio_limits_db;
    break;
  case WAVE_HW_RADIO_BAND_CFG_SCAN:
    hw->rx_hd_ep_mask = WAVE_HW_RX_HD_EP_MASK_GEN6_32;
    hw->rx_hd_vap_mask = WAVE_HW_RX_HD_VAP_MASK_GEN6_32;
    hw->fw_card_cfg.card_cfg.band_cfg = BAND_CONFIGURATION_MODE_SINGLE_BAND_WITH_SCAN;
    p_radio_limits = hw_radio_limits_sc;
    break;
  default:
    /* should not happen */
    MTLK_ASSERT(0);
    break;
  }

  if (NULL == p_radio_limits)
    return MTLK_ERR_PARAMS;

  band_str = band_cfg_to_str[hw->radio_band_cfg];

  ILOG0_DDSDD("Gen5: %d, Gen6: %d, band_cfg_sys_mode: %s, ep_mask: %d, vap_mask: %d",
    (int)_hw_type_is_gen5(hw), (int)_hw_type_is_gen6(hw), band_str,
    (int)hw->rx_hd_ep_mask, (int)hw->rx_hd_vap_mask);

  hw->max_vaps_fw = _hw_type_is_gen5(hw) ? GEN5_MAX_VAP : GEN6_MAX_VAP;
  hw->max_vaps = p_radio_limits[0].max_vaps + p_radio_limits[1].max_vaps;
  hw->max_stas = p_radio_limits[0].max_stas + p_radio_limits[1].max_stas;
  ILOG0_DDD("HW supports max VAPs %u (%u in FW) max STAs %u",
            hw->max_vaps, hw->max_vaps_fw, hw->max_stas);

  wave_radio_limits_set(hw->radio_descr, p_radio_limits);

  return MTLK_ERR_OK;
}

void wave_hw_radio_band_cfg_set (mtlk_hw_t *hw)
{
  unsigned nof_radios = 0;
  int band_cfg;

  MTLK_ASSERT(hw);

  wave_psdb_cfg_radio_number_get (_mtlk_hw_get_psdb(hw), &nof_radios);

  /* TBD: 5G_LOW/5G_HIGH, Altern mode etc */
  band_cfg = (nof_radios == 2) ? WAVE_HW_RADIO_BAND_CFG_DB_2x2 : WAVE_HW_RADIO_BAND_CFG_SB;

  hw->radio_band_cfg = (wave_hw_radio_band_cfg_t)band_cfg;

  ILOG1_DDD("CID-%02x: nof_radios %d, band_cfg %d", hw->card_idx, nof_radios, band_cfg);
}

wave_hw_radio_band_cfg_t __MTLK_IFUNC wave_hw_radio_band_cfg_get(mtlk_hw_t *hw)
{
  MTLK_ASSERT(hw != NULL);
  return hw->radio_band_cfg;
}

static __INLINE uint8
__wave_hw_vap_id_fw_get (mtlk_hw_t *hw, uint8 ep_bit, uint8 vap_id)
{
  return ((ep_bit << 4) | vap_id);
}


static __INLINE void
__wave_hw_rx_hd_decode (mtlk_hw_t *hw, uint8 ep_bits, uint8 vap_bits, uint8 *radio_id, uint8 *vap_id)
{
  uint8 epx = ep_bits ^ 1;
  *radio_id = hw->rx_hd_ep_mask & epx;
  *vap_id   = hw->rx_hd_vap_mask & __wave_hw_vap_id_fw_get(hw, ep_bits, vap_bits);
}

/* TODO: eliminate hardcoded values */
uint8 __MTLK_IFUNC
wave_hw_band_hd_ep_bit_get (mtlk_hw_t *hw, uint8 radio_id, uint8 vap_id, uint8 *vap_id_fw)
{
  uint8 ep_bit = 0;

  MTLK_ASSERT(hw != NULL);

  if (_hw_type_is_gen5(hw))
    ep_bit = 0; /* or 1? as it goes to EP bits */
  else {
    if (_hw_type_is_gen6(hw)) {
      switch (hw->radio_band_cfg) {
      case WAVE_HW_RADIO_BAND_CFG_SCAN:
      case WAVE_HW_RADIO_BAND_CFG_SB:
        if (vap_id >= 16)
          ep_bit = 1;
        break;
      case WAVE_HW_RADIO_BAND_CFG_DB_2x2:
      case WAVE_HW_RADIO_BAND_CFG_DB_3x1:
        MTLK_STATIC_ASSERT(WAVE_CARD_RADIO_NUM_MAX == 2);
        MTLK_ASSERT(WAVE_CARD_RADIO_NUM_MAX > radio_id);
        ep_bit = (radio_id ^ 1) & 1;
        break;
      default:
        break;
      }
    }
  }

  *vap_id_fw = __wave_hw_vap_id_fw_get(hw, ep_bit, vap_id);

  ILOG0_SDDDD("band_cfg:%s RadioID:%u VapID:%u --> ep_bit: %d, vap_id_fw %u",
              band_cfg_to_str[hw->radio_band_cfg], radio_id, vap_id, ep_bit, *vap_id_fw);

  return ep_bit;
}

BOOL __MTLK_IFUNC wave_hw_is_cdb(mtlk_hw_t *hw)
{
  MTLK_ASSERT(hw != NULL);
  return ((hw->radio_band_cfg == WAVE_HW_RADIO_BAND_CFG_DB_2x2) ||
          (hw->radio_band_cfg == WAVE_HW_RADIO_BAND_CFG_DB_3x1));
}

mtlk_ndev_t* __MTLK_IFUNC wave_hw_ndev_get (mtlk_hw_t* hw, unsigned radio_idx)
{
  mtlk_df_user_t  *df_user;

  MTLK_ASSERT(hw != NULL);

  df_user = mtlk_df_get_user(mtlk_vap_manager_get_master_df(_hw_vap_manager_get(hw, radio_idx)));
  return mtlk_df_user_get_ndev(df_user);
}

void __MTLK_IFUNC wave_hw_ccr_set(mtlk_hw_t *hw, mtlk_ccr_t *ccr)
{
  MTLK_ASSERT(hw != NULL);
  MTLK_ASSERT(ccr != NULL);
  hw->ccr = ccr;
}

unsigned char *wave_hw_mmb_get_mmb_base (mtlk_hw_t *hw)
{
  return hw->mmb_base;
}

uint32 wave_hw_mmb_get_stats_poll_period (mtlk_hw_t *hw)
{
  return hw->stats_poll_period;
}

void wave_hw_mmb_set_stats_poll_period (mtlk_df_t *df, uint32 value)
{
  mtlk_hw_t *hw = NULL;

  MTLK_ASSERT(df != NULL);
  hw = mtlk_vap_get_hw(mtlk_df_get_vap_handle(df));
  MTLK_ASSERT(hw);

  hw->stats_poll_period = value;
}

int wave_hw_mmb_set_prop (mtlk_hw_t *hw, const mtlk_hw_prop_e prop_id, void *data, const size_t data_size)
{
  return _mtlk_hw_set_prop(hw, prop_id, data, data_size);
}

int wave_hw_mmb_get_prop (mtlk_hw_t *hw, const mtlk_hw_prop_e prop_id, void *data, const size_t data_size)
{
  return _mtlk_hw_get_prop(hw, prop_id, data, data_size);
}

void wave_hw_mmb_cause_umac_assert (mtlk_hw_t *hw)
{
  uint32 core_no;

  ILOG0_DD("CID-%02x: Asserting UMAC FW: hw_state=%d", hw->card_idx, hw->state);

  core_no = hw_assert_type_to_core_nr(hw, MTLK_CORE_UI_ASSERT_TYPE_FW_UMIPS);

  wave_rcvry_mac_hang_evt_reset(hw, core_no);

  hw->state = MTLK_HW_STATE_MAC_ASSERTED;

  if (MTLK_ERR_OK != _mtlk_mmb_cause_mac_assert(hw, core_no))
    WLOG_D("FW MAC#%d assert not done because not supported", core_no);
}

void wave_hw_mmb_cause_lmac_assert (mtlk_hw_t *hw)
{
  uint32 core_no;

  if (_hw_type_is_gen5(hw))
    return;

  ILOG0_DD("CID-%02x: Asserting LMAC FW: hw_state=%d", hw->card_idx, hw->state);

  core_no = hw_assert_type_to_core_nr(hw, MTLK_CORE_UI_ASSERT_TYPE_FW_LMIPS0);
  if (MTLK_ERR_OK != _mtlk_mmb_cause_mac_assert(hw, core_no))
    WLOG_D("FW MAC#%d assert not done because not supported", core_no);

  core_no = hw_assert_type_to_core_nr(hw, MTLK_CORE_UI_ASSERT_TYPE_FW_LMIPS1);
  if (MTLK_ERR_OK != _mtlk_mmb_cause_mac_assert(hw, core_no))
    WLOG_D("FW MAC#%d assert not done because not supported", core_no);
}

void wave_hw_mmb_wait_umac_assert_evt (mtlk_hw_t *hw)
{
  uint32 cpu_no = hw_assert_type_to_core_nr(hw, MTLK_CORE_UI_ASSERT_TYPE_FW_UMIPS);

  /* Wait for FW Assert Indication from UMAC */
  if (MTLK_ERR_OK != wave_rcvry_mac_hang_evt_wait(hw, cpu_no)) {
    WLOG_DD("CID-%02x: FW Assert Indication not received from UMAC during time-interval %dms. "
            "UMAC stuck", hw->card_idx, WAVE_RCVRY_FW_ASSERT_IND_TIMEOUT);
#if 0 /* TODO: add LMAC assert (required by design) as soon as FW is implemented */
    /* Assert LMAC */
    wave_hw_mmb_cause_lmac_assert(hw);
#endif
  }
  else
    ILOG0_D("CID-%02x: FW Assert Indication received from UMAC", hw->card_idx);
}

BOOL __MTLK_IFUNC rtlog_fw_offline_logging_enabled (const char *ifname);

int __MTLK_IFUNC
mtlk_hw_seq_write_logger_trace_buffers (mtlk_hw_t *hw, mtlk_seq_entry_t *s)
{
  int idx, res;
  void *data = NULL;
  uint16 first_idx, buf_count, buf_size;
  mtlk_hw_data_ind_mirror_t *data_ind;
  mtlk_df_user_t *df_user = mtlk_df_get_user(mtlk_df_proc_seq_entry_get_df(s));

#if 0 /* Always supported */
  if (!hw->fw_logger.is_supported_offline) {
    ELOG_V("FW does not support offline logging");
    return MTLK_ERR_NOT_SUPPORTED;
  }
#endif

  if (!rtlog_fw_offline_logging_enabled(mtlk_df_user_get_name(df_user))) {
    ELOG_S("FW Offline Logging not enabled for %s", mtlk_df_user_get_name(df_user));
    return MTLK_ERR_NOT_SUPPORTED;
  }

  first_idx = MAC_TO_HOST16(hw->chi_area.sLoggerRdExt.sData.u16LoggerRdExtensionFirstRdIndex);
  buf_count = MAC_TO_HOST16(hw->chi_area.sLoggerRdExt.sData.u16LoggerRdExtensionNumOfRDs);

  /* Write 2 byte header with buffer size (assuming all buffers are the same size) */
  data_ind = _mtlk_basic_bdr_get_mirror_bd(&hw->rx.bdr_data, first_idx, mtlk_hw_data_ind_mirror_t);
  if (data_ind->size > MAX_UINT16) {
    ELOG_D("Can't store buffer size (%u) in 2 byte buffer", data_ind->size);
    return MTLK_ERR_NOT_SUPPORTED;
  }
  buf_size = data_ind->size;
  mtlk_aux_seq_write(s, &buf_size, sizeof(buf_size));

  for (idx = first_idx; idx < first_idx + buf_count; idx++)
  {
    data_ind = _mtlk_basic_bdr_get_mirror_bd(&hw->rx.bdr_data, idx, mtlk_hw_data_ind_mirror_t);

    mtlk_osal_unmap_phys_addr(mtlk_ccr_get_dev_ctx(hw->ccr),
                              data_ind->dma_addr,
                              data_ind->size,
                              MTLK_DATA_FROM_DEVICE);

    if (!mtlk_mmb_dcdp_path_available(hw))
      data = data_ind->nbuf->data;
    else
      data = data_ind->data;

    res = mtlk_aux_seq_write(s, data, data_ind->size);

    data_ind->dma_addr = mtlk_osal_map_to_phys_addr(mtlk_ccr_get_dev_ctx(hw->ccr),
                               data_ind->data,
                               data_ind->size,
                               MTLK_DATA_FROM_DEVICE);
    if (!data_ind->dma_addr) {
        ILOG2_P("WARNING: failed mapping 0x%p to physical address", data_ind->data);
        return MTLK_ERR_NO_RESOURCES;
    }

    if (res) /* Overflow */
      return MTLK_ERR_OK;
  }

  return MTLK_ERR_OK;
}

#ifndef MTLK_LEGACY_STATISTICS
int scan_get_rssi (mtlk_core_t *core, hw_statistics_t *hw_stats)
{
  mtlk_hw_t *hw = mtlk_vap_get_hw(core->vap_handle);
  int i = 0, total_rssi = 0;
  int chip_id = __hw_mmb_get_chip_id(hw);
  int num_of_stas = mtlk_stadb_stas_num(&core->slow_ctx->stadb);

  if ( num_of_stas > 0) {
    for (i = 0; i < num_of_stas; i++) {
      if (!_chipid_is_gen6_a0(chip_id)) { /* b0 etc */
        total_rssi += *(int8*)( hw_stats->stats_copy + (int)MTLK_OFFSET_OF(StatisticsDb_t_wave600b, alphaFilterStats.maxRssi[i]));
      }
    }
    return (total_rssi/num_of_stas);
  } else {
     return (-128);
  }
}

int scan_get_noiseFloor (mtlk_core_t *core, hw_statistics_t *hw_stats, int rssi)
{
  mtlk_hw_t *hw = mtlk_vap_get_hw(core->vap_handle);
  wave_radio_t  *radio = wave_vap_radio_get(core->vap_handle);
  unsigned radio_idx;
  int chip_id = __hw_mmb_get_chip_id(hw);

  radio_idx = wave_radio_id_get(radio);

  return _mtlk_hw_get_noise_by_phy_metrics(hw, (uint8*)(MTLK_STATS_COPY_OFFSET(phyStatistics.devicePhyRxStatus[radio_idx].noise, chip_id)),
                                             (uint8*)(MTLK_STATS_COPY_OFFSET(phyStatistics.devicePhyRxStatus[radio_idx].rf_gain, chip_id))); 
}

int scan_get_aocs_info (mtlk_core_t *core, struct intel_vendor_channel_data *ch_data, struct channel_survey_support *csys)
{
  int res = MTLK_ERR_OK;
  mtlk_hw_t *hw = mtlk_vap_get_hw(core->vap_handle);
  uint32 chan_load = 0;
  unsigned radio_idx;
  hw_statistics_t    *hw_stats;
  int chip_id = __hw_mmb_get_chip_id(hw);

  hw_stats = &hw->hw_stats;
  radio_idx = wave_radio_id_get(wave_vap_radio_get(core->vap_handle));
  mtlk_osal_lock_acquire(&hw_stats->lock);
  ch_data->dynBW20             = *(uint32*)(MTLK_STATS_COPY_OFFSET(dynamicBwStats.dynamicBW20MHz[radio_idx], chip_id));
  ch_data->dynBW40             = *(uint32*)(MTLK_STATS_COPY_OFFSET(dynamicBwStats.dynamicBW40MHz[radio_idx], chip_id));
  ch_data->dynBW80             = *(uint32*)(MTLK_STATS_COPY_OFFSET(dynamicBwStats.dynamicBW80MHz[radio_idx], chip_id));
  ch_data->dynBW160            = *(uint32*)(MTLK_STATS_COPY_OFFSET(dynamicBwStats.dynamicBW160MHz[radio_idx], chip_id));
  ch_data->ext_sta_rx          = *(uint32*)(MTLK_STATS_COPY_OFFSET(phyStatistics.devicePhyRxStatus[radio_idx].extStaRx, chip_id));
  ch_data->tx_power            = *(uint32*)(MTLK_STATS_COPY_OFFSET(phyStatistics.devicePhyRxStatus[radio_idx].txPower, chip_id));

  ch_data->rssi                = scan_get_rssi(core, hw_stats);
  ch_data->noise_floor         = scan_get_noiseFloor(core, hw_stats, ch_data->rssi);
  if (mtlk_stadb_stas_num(&core->slow_ctx->stadb) > 0) {
    ch_data->snr                 = (uint8) (ch_data->rssi - ch_data->noise_floor);
  } else {
    ch_data->snr                 = 0;
  }

  ch_data->cwi_noise           = *(int8*)(MTLK_STATS_COPY_OFFSET(phyStatistics.devicePhyRxStatus[radio_idx].CWIvalue, chip_id));
  ch_data->busy_time           = ((uint32)(*(unsigned char*)(MTLK_STATS_COPY_OFFSET(phyStatistics.devicePhyRxStatus[radio_idx].channel_load, chip_id)))) * 255 / 100;
  ch_data->load = *(unsigned char*)(MTLK_STATS_COPY_OFFSET(phyStatistics.devicePhyRxStatus[radio_idx].channel_load, chip_id));
  mtlk_osal_lock_release(&hw_stats->lock);

  ch_data->filled_mask = CHDATA_DYNBW | CHDATA_NOISE_FLOOR | CHDATA_RSSI | CHDATA_SNR |
                         CHDATA_CWI_NOISE | CHDATA_TX_POWER |
                         CHDATA_LOAD | CHDATA_BUSY_TIME | CHDATA_LOW_RSSI;

  ILOG2_DDDDDDDDDDD("debug AOCS_info: %d %d %d %d %d %d %d %d %d %d %d",
    ch_data->dynBW20, ch_data->dynBW40, ch_data->dynBW80, ch_data->dynBW160, ch_data->ext_sta_rx, ch_data->tx_power,
    ch_data->rssi, ch_data->snr, ch_data->noise_floor, ch_data->cwi_noise, ch_data->busy_time);

  scan_chan_survey_set_chan_load(csys, chan_load);
  return res;
}

void __MTLK_IFUNC
mtlk_hw_copy_channel_stats (mtlk_hw_t *hw, mtlk_hw_band_e hw_band, mtlk_channel_stats *chan_stats, size_t total_size)
{
  if ((MTLK_HW_BAND_2_4_GHZ == hw_band)) {
    wave_memcpy(chan_stats, total_size, &hw->chan_statistics[1],
                                            sizeof(mtlk_channel_stats) * NUM_2GHZ_CHANS);
  } else if ((MTLK_HW_BAND_5_2_GHZ == hw_band)) {
    wave_memcpy(chan_stats, total_size, &hw->chan_statistics[NUM_2GHZ_CHANS + 1],
                                                                  sizeof(mtlk_channel_stats) * NUM_5GHZ_CHANS);
  }
}
#endif /* MTLK_LEGACY_STATISTICS */

BOOL __MTLK_IFUNC
mtlk_hw_scan_is_running (mtlk_hw_t *hw)
{
  int i;
  wave_radio_t *radio;

  for (i = 0; i < hw->radio_descr->num_radios; i++) {
    radio = wave_radio_descr_wave_radio_get(hw->radio_descr, i);
    if (wave_radio_scan_is_running(radio)) {
      return TRUE;
    }
  }
  return FALSE;
}
