/******************************************************************************

                               Copyright (c) 2012
                            Lantiq Deutschland GmbH

  For licensing information, see the file 'LICENSE' in the root folder of
  this software module.

******************************************************************************/
#ifndef __HW_MMB_H__
#define __HW_MMB_H__

#define MTLK_UPPER_CPU       (1 << 0)
#define MTLK_LOWER_CPU       (1 << 1)

#ifdef CPTCFG_IWLWAV_SET_PM_QOS
#define MTLK_HW_PM_QOS_VALUE_NO_CLIENTS (90)
#define MTLK_HW_PM_QOS_VALUE_ANY_CLIENT (20)
#define MTLK_HW_PM_QOS_DEFAULT          MTLK_HW_PM_QOS_VALUE_NO_CLIENTS
#endif

#include "mtlkdfdefs.h"
#include "mtlkirbd.h"
#include "mtlk_wss.h"
#include "mcast.h"

#include "mtlk_card_types.h"

/* FIXME: temporary: Recovery is not supported */
#define MTLK_HW_MMB_RCVRY_SUPPORTED     0

#include "mtlk_mmb_drv.h"

#include "mtlk_dcdp.h"

#define SAFE_PLACE_TO_INCLUDE_MTLK_CCR_DECLS
#include "mtlk_ccr_decls.h"

#include "mtlk_vap_manager.h"

#include "shram_ex.h"

#include "mtlk_osal.h"
#include "txmm.h"

#include "eeprom.h"
#include "wave_radio.h"
#include "wave_hal_stats.h"

#define   MTLK_IDEFS_ON
#include "mtlkidefs.h"

/**************************************************************
 * Auxiliary OS/bus abstractions required for MMB
 * NOTE: must be defined within hw_<platfrorm>.h
 **************************************************************/

/**************************************************************/

/**************************************************************
 * MMB Interface
 **************************************************************/

#define DCDP_SWPATH_ENABLED 2
#define FASTPATH_ENABLED    1
#define FASTPATH_DISABLED   0
#define RX_DATA_FRAME_SIZE (2048 - 128 - 192) /* 320 bytes are reserved for PPA driver purpose */
#define DB_UNKNOWN_SID     0x3ffb

typedef struct
{
  uint8  bist_check_permitted;
  uint32 no_pll_write_delay_us;
  uint32 man_req_msg_size;
  uint32 man_ind_msg_size;
  uint32 dbg_msg_size;
} __MTLK_IDATA mtlk_hw_mmb_cfg_t;

typedef struct
{
  mtlk_hw_mmb_cfg_t    cfg;
  mtlk_hw_t           *cards[MTLK_MAX_HW_ADAPTERS_SUPPORTED];
  uint32               nof_cards;
  mtlk_osal_spinlock_t lock;
  uint32               bist_passed;

  MTLK_DECLARE_INIT_STATUS;
} __MTLK_IDATA mtlk_hw_mmb_t;

typedef struct
{
  uint32              *p_OUT_cntr;    /* shared u32OutCounterAddress */
  uint32              *p_OUT_status;  /* shared u32OutStatusAddress */
  uint32              *p_IN_cntr;     /* shared u32InCounterAddress */
  uint32               IN_copy;       /* IN copy in local memory */
  uint32               int_OUT_cntr;  /* OUT internal counter */
} mtlk_ring_regs;

extern mtlk_hw_mmb_t mtlk_mmb_obj;

/* HW radio band configuration modes */
typedef enum
{
  WAVE_HW_RADIO_BAND_CFG_UNSUPPORTED,    /* unsupported configuration */
  WAVE_HW_RADIO_BAND_CFG_SB      = 1,    /* Gen5: 1 Band : 16 VAPs, Gen6: 32 Vaps           */
  WAVE_HW_RADIO_BAND_CFG_DB_2x2  = 2,    /* Gen6: 2 Bands: (16 + 16) VAPs, (2 + 2) Antennas */
  WAVE_HW_RADIO_BAND_CFG_DB_3x1  = 3,    /* Gen6: 2 Bands: (16 + 16) VAPs, (3 + 1) Antennas */
  WAVE_HW_RADIO_BAND_CFG_SCAN    = 4,    /* Gen6: scan: 2 Bands: 32 VAPs + 0 VAPs, or */
                                         /* Gen6: 1 Band : 31 VAPs + 1 VAP reserved   */
  WAVE_HW_RADIO_BAND_CFG_LAST            /* used for checks, keep it last             */
} wave_hw_radio_band_cfg_t;

/* default and only one mode for Gen5 */
#define WAVE_HW_RADIO_BAND_CFG_DEFAULT_GEN5 WAVE_HW_RADIO_BAND_CFG_SB
/* default mode for Gen6 */
#define WAVE_HW_RADIO_BAND_CFG_DEFAULT_GEN6 WAVE_HW_RADIO_BAND_CFG_DB_2x2

void
wave_hw_radio_band_cfg_set(mtlk_hw_t *hw);

wave_hw_radio_band_cfg_t __MTLK_IFUNC
wave_hw_radio_band_cfg_get(mtlk_hw_t *hw);

BOOL __MTLK_IFUNC wave_hw_is_cdb(mtlk_hw_t *hw);

mtlk_ndev_t* __MTLK_IFUNC
wave_hw_ndev_get(mtlk_hw_t* hw, unsigned radio_idx);

uint8 __MTLK_IFUNC
wave_hw_band_hd_ep_bit_get(mtlk_hw_t *hw, uint8 radio_id, uint8 vap_id, uint8 *vap_id_fw);

int __MTLK_IFUNC
wave_hw_cfg_radio_number_get(mtlk_ccr_t *ccr, mtlk_hw_t *hw, unsigned *radio_number);

/**************************************************************
* FW recovery related functions
**************************************************************/
void __MTLK_IFUNC
mtlk_hw_mmb_isolate(mtlk_hw_t *hw);

int __MTLK_IFUNC
mtlk_hw_mmb_restore(mtlk_hw_t *hw);

void __MTLK_IFUNC
mtlk_hw_mmb_dis_interrupts(mtlk_hw_t *hw);

/**************************************************************
 * Init/cleanup functions - must be called on driver's
 * loading/unloading
 **************************************************************/
int __MTLK_IFUNC 
mtlk_hw_mmb_init(mtlk_hw_mmb_t *mmb, const mtlk_hw_mmb_cfg_t *cfg);
void __MTLK_IFUNC
mtlk_hw_mmb_cleanup(mtlk_hw_mmb_t *mmb);
/**************************************************************/

int __MTLK_IFUNC
mtlk_mmb_txmm_init(mtlk_hw_t *hw, mtlk_hw_api_t *hw_api);
int __MTLK_IFUNC
mtlk_mmb_txdm_init(mtlk_hw_t *hw, mtlk_hw_api_t *hw_api);

/**************************************************************
 * Auxiliary MMB interface - for BUS module usage
 **************************************************************/
mtlk_txmm_base_t *__MTLK_IFUNC
mtlk_hw_mmb_get_txmm_base(mtlk_hw_t *card);

mtlk_txmm_base_t *__MTLK_IFUNC
mtlk_hw_mmb_get_txdm_base(mtlk_hw_t *card);

mtlk_txmm_t *__MTLK_IFUNC
mtlk_hw_mmb_get_txmm(mtlk_hw_t *hw);

uint8 __MTLK_IFUNC
mtlk_hw_mmb_get_card_idx(mtlk_hw_t *card);

/* Stops all the MAC-initiated events (INDs), sending to MAC still working */
void __MTLK_IFUNC
mtlk_hw_mmb_stop_mac_events(mtlk_hw_t *card);

/**************************************************************
 * DirectConnect interface
 **************************************************************/
#if MTLK_USE_DIRECTCONNECT_DP_API

int32 __MTLK_IFUNC
mtlk_hw_mmb_get_dp_port_id(mtlk_hw_t *hw, int port_idx);

mtlk_hw_dcdp_mode_t __MTLK_IFUNC
mtlk_hw_mmb_get_dp_mode(mtlk_hw_t *hw);
#endif

#if MTLK_USE_DIRECTCONNECT_DP_API
BOOL __MTLK_IFUNC mtlk_mmb_fastpath_available(mtlk_hw_t *hw)    __MTLK_INT_HANDLER_SECTION;
BOOL __MTLK_IFUNC mtlk_mmb_dcdp_path_available(mtlk_hw_t *hw)   __MTLK_INT_HANDLER_SECTION;
BOOL __MTLK_IFUNC mtlk_mmb_dcdp_frag_wa_enabled(mtlk_hw_t *hw)  __MTLK_INT_HANDLER_SECTION;
#else
#define mtlk_mmb_fastpath_available(hw) (FALSE)
#define mtlk_mmb_dcdp_path_available(hw) (FALSE)
#define mtlk_mmb_dcdp_frag_wa_enabled(hw) (FALSE)
#endif

/**************************************************************
 * Add/remove card - must be called on device addition/removal
 **************************************************************/
mtlk_hw_api_t * __MTLK_IFUNC 
mtlk_hw_mmb_add_card (void);

void __MTLK_IFUNC 
mtlk_hw_mmb_remove_card(mtlk_hw_api_t *hw_api);

void __MTLK_IFUNC
mtlk_hw_mmb_global_version_printout (mtlk_hw_mmb_t *mmb,
                                     char *buffer,
                                     uint32 size);

/**************************************************************
 * Init/cleanup card - must be called on device init/cleanup
 **************************************************************/
int __MTLK_IFUNC 
mtlk_hw_mmb_init_card(mtlk_hw_t *hw, mtlk_ccr_t *ccr, unsigned char *mmb_pas,  unsigned char *mmb_pas_phy,
                      wave_radio_descr_t *radio_descr, int fast_path, BOOL is_dual_pci);

void __MTLK_IFUNC 
mtlk_hw_mmb_cleanup_card(mtlk_hw_t *card);

int __MTLK_IFUNC 
mtlk_hw_mmb_start_card(mtlk_hw_t   *hw);

int __MTLK_IFUNC 
mtlk_hw_mmb_reset_mac(mtlk_hw_t   *hw);

int __MTLK_IFUNC
mtlk_hw_mmb_start_card_finalize(mtlk_hw_t   *hw);

void __MTLK_IFUNC 
mtlk_hw_mmb_stop_card(mtlk_hw_t *card);

void __MTLK_IFUNC 
mtlk_hw_mmb_stop_card_finalize(mtlk_hw_t *card);

void __MTLK_IFUNC
mtlk_eeprom_psdb_parse_stop(mtlk_hw_t *hw);

int __MTLK_IFUNC
mtlk_eeprom_psdb_read_and_parse (mtlk_hw_t *hw);

void* __MTLK_IFUNC
wave_card_radio_descr_get(mtlk_hw_t *hw);

/**************************************************************
 * Send logger severity configuration to FW
 **************************************************************/
int __MTLK_IFUNC 
_mtlk_mmb_send_fw_log_severity(mtlk_hw_t *hw,
                               uint32 newLevel,
                               uint32 targetCPU);

/**************************************************************/

/**************************************************************
 * Card's ISR - must be called on interrupt handler
 * Return values:
 *   MTLK_ERR_OK      - do nothing
 *   MTLK_ERR_UNKNOWN - not an our interrupt
 *   MTLK_ERR_PENDING - order bottom half routine (DPC, tasklet etc.)
 **************************************************************/
int __MTLK_IFUNC 
mtlk_hw_mmb_interrupt_handler_msi(mtlk_irq_handler_data *ihd)    __MTLK_INT_HANDLER_SECTION;
int __MTLK_IFUNC
mtlk_hw_mmb_interrupt_handler_legacy(mtlk_irq_handler_data *ihd) __MTLK_INT_HANDLER_SECTION;
/**************************************************************/

/**************************************************************
 * Card's bottom half of irq handling (DPC, tasklet etc.)
 **************************************************************/
void __MTLK_IFUNC
mtlk_hw_mmb_deferred_handler(mtlk_irq_handler_data *ihd)    __MTLK_INT_HANDLER_SECTION;
void __MTLK_IFUNC
mtlk_hw_mmb_data_cfm_handler(mtlk_irq_handler_data *ihd)    __MTLK_INT_HANDLER_SECTION;
void __MTLK_IFUNC
mtlk_hw_mmb_data_rx_handler(mtlk_irq_handler_data *ihd)     __MTLK_INT_HANDLER_SECTION;
void __MTLK_IFUNC
mtlk_hw_mmb_bss_cfm_handler(mtlk_irq_handler_data *ihd)     __MTLK_INT_HANDLER_SECTION;
void __MTLK_IFUNC
mtlk_hw_mmb_bss_rx_handler(mtlk_irq_handler_data *ihd)      __MTLK_INT_HANDLER_SECTION;
void __MTLK_IFUNC
mtlk_hw_mmb_mailbox_handler(mtlk_irq_handler_data *ihd)     __MTLK_INT_HANDLER_SECTION;
void __MTLK_IFUNC
mtlk_hw_mmb_bss_shared_handler(mtlk_irq_handler_data *ihd)  __MTLK_INT_HANDLER_SECTION;
void __MTLK_IFUNC
mtlk_hw_mmb_legacy_irq_handler(mtlk_irq_handler_data *ihd)  __MTLK_INT_HANDLER_SECTION;
void __MTLK_IFUNC
mtlk_hw_mmb_mailbox_legacy_shared_handler(mtlk_irq_handler_data *ihd)  __MTLK_INT_HANDLER_SECTION;

/**************************************************************/
/**************************************************************/


/**************************************************************
 * DirectConnect support
 **************************************************************/
#if MTLK_USE_DIRECTCONNECT_DP_API
int __MTLK_IFUNC
mtlk_mmb_dcdp_rx (struct net_device *rxif, struct dp_subif *rx_subif, struct sk_buff *skb, int32_t len);
#endif

/**************************************************************/

mtlk_ccr_t*  __MTLK_IFUNC
mtlk_hw_mmb_get_ccr (mtlk_hw_t *hw);

void __MTLK_IFUNC hw_get_fw_version (mtlk_handle_t hw_handle, char *hw_info, uint32 size);
void __MTLK_IFUNC hw_get_hw_version(mtlk_handle_t hw_handle, uint32 *hw_version);
BOOL __MTLK_IFUNC hw_get_ldpc_support(mtlk_handle_t hw_handle);
BOOL __MTLK_IFUNC hw_get_ampdu_64k_support(mtlk_handle_t hw_handle);
BOOL __MTLK_IFUNC hw_get_ampdu_density_restriction(mtlk_handle_t hw_handle);
BOOL __MTLK_IFUNC hw_get_rx_stbc_support(mtlk_handle_t hw_handle);
BOOL __MTLK_IFUNC hw_get_vht_support(mtlk_handle_t hw_handle);
BOOL __MTLK_IFUNC hw_get_vht_160mhz_support(mtlk_handle_t hw_handle);
BOOL __MTLK_IFUNC hw_get_160mhz_short_gi_support(mtlk_handle_t hw_handle);
BOOL __MTLK_IFUNC hw_get_gcmp_support(mtlk_handle_t hw_handle);
BOOL __MTLK_IFUNC hw_get_he_support(mtlk_handle_t hw_handle);

void __MTLK_IFUNC mtlk_hw_mmb_set_msi_intr_mode(mtlk_hw_t *hw, uint32 mode);
#ifdef CPTCFG_IWLWAV_SET_PM_QOS
int  __MTLK_IFUNC mtlk_mmb_update_cpu_dma_latency(mtlk_hw_t *hw, s32 new_cpu_dma_latency);
#endif

void __MTLK_IFUNC mtlk_mmb_print_tx_dat_ring_info(mtlk_hw_t *hw);
void __MTLK_IFUNC mtlk_mmb_print_tx_bss_ring_info(mtlk_hw_t *hw);
void __MTLK_IFUNC mtlk_mmb_print_tx_bss_res_queue(mtlk_hw_t *hw);

void __MTLK_IFUNC mtlk_mmb_print_tx_dat_ring_info(mtlk_hw_t *hw);
void __MTLK_IFUNC mtlk_mmb_print_tx_bss_ring_info(mtlk_hw_t *hw);

#define   MTLK_IDEFS_OFF
#include "mtlkidefs.h"

#include "mmb_ops.h"

#define SAFE_PLACE_TO_INCLUDE_MTLK_CCR_DEFS
#include "mtlk_ccr_defs.h"

int __MTLK_IFUNC
mtlk_hw_get_fw_dump_info(mtlk_hw_t *hw, wave_fw_dump_info_t **fw_info);

BOOL mtlk_is_band_supported(mtlk_handle_t hw_handle, unsigned radio_id, mtlk_hw_band_e band);

BOOL mtlk_mmb_bss_mgmt_tx_check(mtlk_vap_handle_t vap_handle);
int mtlk_mmb_bss_mgmt_tx (mtlk_vap_handle_t vap_handle, const uint8 *buf, size_t len, int channum,
                          BOOL no_cck, BOOL dont_wait_for_ack, BOOL is_broadcast,
                          uint64 *cookie, uint32 extra_processing, mtlk_nbuf_t *skb,
                          BOOL power_mgmt_on, uint32 tid);

typedef enum {
    MTLK_IRQ_MODE_MSI_8 = 0,
    MTLK_IRQ_MODE_MSI_4 = 1,
    MTLK_IRQ_MODE_MSI_1 = 2,
    MTLK_IRQ_MODE_LEGACY,
    MTLK_IRQ_MODE_INVALID,
} mtlk_irq_mode_e;

enum {
  PROCESS_MANAGEMENT = 0,
  PROCESS_EAPOL,
  PROCESS_NULL_DATA_PACKET,
};

uint8 __MTLK_IFUNC
mtlk_hw_eeprom_get_nic_type(mtlk_hw_t *hw);

BOOL wave_hw_mmb_eeprom_is_production_mode(mtlk_hw_t *hw);

void __MTLK_IFUNC
mtlk_hw_inc_radar_cntr (mtlk_hw_t *hw);

void __MTLK_IFUNC
mtlk_hw_reset_radar_cntr (mtlk_hw_t *hw);

BOOL __MTLK_IFUNC
mtlk_hw_type_is_gen5(mtlk_hw_t *hw);

BOOL __MTLK_IFUNC
mtlk_hw_type_is_gen6(mtlk_hw_t *hw);

BOOL __MTLK_IFUNC
mtlk_hw_type_is_gen6_a0(mtlk_hw_t *hw);

BOOL __MTLK_IFUNC
mtlk_hw_type_is_gen6_b0(mtlk_hw_t *hw);

BOOL __MTLK_IFUNC
mtlk_hw_type_is_gen6_d2(mtlk_hw_t *hw);

BOOL __MTLK_IFUNC
wave_hw_mmb_card_is_asic (mtlk_hw_t *hw);

BOOL __MTLK_IFUNC
wave_hw_mmb_card_is_phy_dummy (mtlk_hw_t *hw);

unsigned
mtlk_hw_get_rrsi_offs(mtlk_hw_t *hw);

unsigned __MTLK_IFUNC
hw_num_cores (mtlk_hw_t *hw);

int __MTLK_IFUNC
hw_assert_type_to_core_nr(mtlk_hw_t *hw, mtlk_core_ui_dbg_assert_type_e assert_type);

uint32 __MTLK_IFUNC hw_mac_pmcu_check_lock(mtlk_hw_t *hw, mtlk_core_t *core);
void __MTLK_IFUNC hw_mac_pmcu_unlock_fw(mtlk_hw_t *hw, mtlk_core_t *core);

void __MTLK_IFUNC
mtlk_mmb_get_tr181_hw_stats(mtlk_hw_t* hw, mtlk_wssa_drv_tr181_hw_stats_t* stats);

#if MTLK_CCR_DEBUG_PRINT_INTR_REGS
void __MTLK_IFUNC mtlk_hw_debug_print_all_ring_regs(mtlk_hw_t *hw);
#endif

typedef struct {
  uint16   pw_min_ant[PHY_STAT_RATE_CBW_MAX + 1]; /* ultimate EVM power (GEN5) per antenna, as per calibration */
  uint16   pw_max_ant[PHY_STAT_RATE_CBW_MAX + 1]; /* max power per antenna, as per calibration */
  uint8   pw_size;
} mtlk_hw_tx_power_t;

typedef struct {
  mtlk_hw_tx_power_t  power_hw; /* minimal and maximal power per antenna */
  BOOL                open_loop;
  uint16              power_usr_offs; /* offset to configured tx power limit in dB */
  uint16              power_reg;
  uint16              power_psd; /* psd limit of current phy mode */
  psdb_pw_limits_t    power_tpc_psd; /* psd limits of all phy modes */
  uint16              power_cfg; /* configured limit of current phy mode */
  psdb_pw_limits_t    power_tpc_cfg; /* configured limits of all phy modes */

  mtlk_country_code_t cur_country_code;
  uint8               cur_band;
  uint8               cur_chan;
  uint8               max_cbw;
  uint8               cur_cbw;
  uint8               max_antennas;
  uint8               cur_antennas;
  uint8               max_ant_gain;
  uint8               cur_ant_gain;
  uint16              pw_min_brd[PHY_STAT_RATE_CBW_MAX + 1]; /* min (GEN4) or ultimate EVM (GEN5) power for all antennas */
  uint16              pw_max_brd[PHY_STAT_RATE_CBW_MAX + 1]; /* max power for all antennas */
  uint16              pw_targets[PHY_STAT_RATE_CBW_MAX + 1]; /* actual power target */
} mtlk_tx_power_data_t;

#ifndef MTLK_LEGACY_STATISTICS
typedef struct
{
  mtlk_osal_spinlock_t  lock;
  void                 *stats_data;
  void                 *stats_copy;
  mtlk_hw_tx_power_t    tx_power[WAVE_CARD_RADIO_NUM_MAX];   /* min/max Tx power per BW and pw_size */
  BOOL                  available;
} hw_statistics_t;

BOOL mtlk_hw_get_stats_available (mtlk_hw_t *hw);
void mtlk_hw_set_stats_available (mtlk_hw_t *hw, BOOL value);
#endif

uint32 __MTLK_IFUNC
wave_hw_calculate_airtime_efficiency(uint64 bytes, uint32 time);

static __INLINE uint16
hw_mmb_get_tx_power_target(uint16 power_cfg, uint16 pw_value)
{
  return ((power_cfg) ? MIN(power_cfg, pw_value) : pw_value);
}

void __MTLK_IFUNC
mtlk_hw_mhi_get_tx_power(mtlk_hw_t *hw, mtlk_hw_tx_power_t *tx_power, unsigned radio_idx);

#ifdef MTLK_LEGACY_STATISTICS
int __MTLK_IFUNC
mtlk_hw_mhi_get_stats (mtlk_hw_t *hw);
#else
int __MTLK_IFUNC
_mtlk_hw_get_statistics (mtlk_hw_t *hw);

BOOL __MTLK_IFUNC
wave_hw_radio_band_cfg_is_single (mtlk_hw_t *hw);
#endif

void __MTLK_IFUNC
mtlk_hw_mhi_get_vap_stats (mtlk_hw_t *hw, mtlk_mhi_stats_vap_t *cntrs, unsigned vap_id);

uint32 __MTLK_IFUNC
mtlk_hw_sta_stat_to_power(mtlk_hw_t *hw, unsigned radio_idx, uint32 stat_value, uint32 cbw);

int __MTLK_IFUNC
hw_phy_rx_status_get (mtlk_hw_t *hw, mtlk_core_t *core);

int __MTLK_IFUNC
mtlk_hw_copy_phy_rx_status (mtlk_hw_t *hw, uint8 *buff, uint32 *size);

uint32 __MTLK_IFUNC
mtlk_hw_get_chip_revision (mtlk_hw_t *hw);

BOOL __MTLK_IFUNC
mtlk_hw_is_sid_valid_or_all_sta_sid (mtlk_hw_t *hw, uint32 sid);

uint32 count_bits_set (uint32 v);

uint32 hw_get_antenna_mask (uint32 full_mask, uint32 num_antennas);

uint32 hw_get_tx_antenna_mask (mtlk_hw_t *hw);

uint32 hw_get_rx_antenna_mask (mtlk_hw_t *hw);

uint32 __MTLK_IFUNC
wave_hw_get_num_sts_by_ant_mask (mtlk_hw_t *hw, uint32 ant_mask);

static __INLINE uint32
wave_hw_get_ant_num_by_ant_mask (uint32 ant_mask)
{
  return count_bits_set(ant_mask);
}

static BOOL __INLINE
mtlk_hw_is_halted (mtlk_hw_state_e hw_state)
{
  return ((hw_state == MTLK_HW_STATE_EXCEPTION) ||
          (hw_state == MTLK_HW_STATE_APPFATAL) ||
          (hw_state == MTLK_HW_STATE_MAC_ASSERTED));
}

static inline BOOL wave_hw_is_init_state (mtlk_hw_state_e hw_state)
{
  return (hw_state < MTLK_HW_STATE_WAITING_READY);
}

int __MTLK_IFUNC
mtlk_hw_noise_phy_to_noise_dbm (mtlk_hw_t *hw, uint32 phy_noise, uint32 rf_gain);

int __MTLK_IFUNC
mtlk_hw_get_rssi_max_by_rx_phy_rssi (mtlk_hw_t *hw, int8 *rx_phy_rssi, int8 *out_rssi);

void __MTLK_IFUNC
mtlk_hw_mmb_fill_phy_info_by_default (mtlk_phy_info_t *phy_info);

void __MTLK_IFUNC
wave_hw_ccr_set (mtlk_hw_t *hw, mtlk_ccr_t *ccr);

void __MTLK_IFUNC
wave_hw_mmb_memcpy_fromio (void *to, void *mmb_base, uint32 off, uint32 count);

uint8 * __MTLK_IFUNC
mtlk_hw_get_mmb_io_data (mtlk_hw_t *hw, uint32 off);

BOOL __MTLK_IFUNC
wave_hw_mmb_all_rings_queue_check (mtlk_hw_t *hw);

BOOL __MTLK_IFUNC
mtlk_hw_is_sid_all_sta_sid (mtlk_hw_t *hw, uint32 sid);

unsigned __MTLK_IFUNC
wave_hw_max_sid_get (mtlk_hw_t *hw);

unsigned char *wave_hw_mmb_get_mmb_base(mtlk_hw_t *hw);
uint32 wave_hw_mmb_get_stats_poll_period(mtlk_hw_t *hw);
void wave_hw_mmb_set_stats_poll_period(mtlk_df_t *df, uint32 value);
int wave_hw_mmb_set_prop(mtlk_hw_t *hw, const mtlk_hw_prop_e prop_id, void *data, const size_t data_size);
int wave_hw_mmb_get_prop(mtlk_hw_t *hw, const mtlk_hw_prop_e prop_id, void *data, const size_t data_size);
void wave_hw_mmb_cause_umac_assert(mtlk_hw_t *hw);
void wave_hw_mmb_cause_lmac_assert(mtlk_hw_t *hw);
void wave_hw_mmb_wait_umac_assert_evt(mtlk_hw_t *hw);

wave_uint __MTLK_IFUNC
wave_hw_mmb_get_current_card_idx (void);

BOOL __MTLK_IFUNC
wave_hw_is_channel_supported (mtlk_hw_t *hw, uint32 channel);

void __MTLK_IFUNC
wave_hw_get_recovery_stats (mtlk_hw_t *hw, mtlk_wssa_drv_recovery_stats_t *stats);

void __MTLK_IFUNC
wave_hw_get_hw_stats (mtlk_hw_t *hw, mtlk_wssa_drv_hw_stats_t *hw_stats);

#ifdef MTLK_LEGACY_STATISTICS
int __MTLK_IFUNC
mtlk_hw_mhi_copy_stats (mtlk_hw_t *hw, uint8 *buff, uint32 *size);

#if MTLK_MTIDL_HW_STAT_FULL
void __MTLK_IFUNC
wave_hw_get_debug_hw_stats (mtlk_hw_t* hw, mtlk_wssa_drv_debug_hw_stats_t* stats);
#endif
#else
void __MTLK_IFUNC
mtlk_hw_copy_channel_stats (mtlk_hw_t *hw, mtlk_hw_band_e hw_band, mtlk_channel_stats *chan_stats, size_t total_size);
#endif /* MTLK_LEGACY_STATISTICS */

BOOL __MTLK_IFUNC
mtlk_hw_scan_is_running (mtlk_hw_t *hw);

#endif /* __HW_MMB_H__ */
