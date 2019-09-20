/******************************************************************************

                               Copyright (c) 2013
                            Lantiq Deutschland GmbH

  For licensing information, see the file 'LICENSE' in the root folder of
  this software module.

******************************************************************************/
/* $Id$ */

#ifndef _MTLK_CCR_API_H_
#define _MTLK_CCR_API_H_

typedef int   (__MTLK_IFUNC *mtlk_ccr_init_f)(void *ccr_mem, mtlk_hw_t *hw, void *bar0, void *bar1, void *bar1_phy, void *dev_ctx);
typedef void  (__MTLK_IFUNC *mtlk_ccr_cleanup_f)(void *ccr_mem);
typedef uint32(__MTLK_IFUNC *mtlk_ccr_read_chip_id_f)(void *ccr_mem);
typedef int   (__MTLK_IFUNC *mtlk_ccr_get_fw_info_f)(void *ccr_mem, uint8 cpu, mtlk_fw_info_t *fw_info);
typedef int   (__MTLK_IFUNC *mtlk_ccr_get_fw_dump_info_f)(void *ccr_mem, wave_fw_dump_info_t **fw_info);
typedef int   (__MTLK_IFUNC *mtlk_ccr_get_hw_dump_info_f)(void *ccr_mem, wave_hw_dump_info_t **fw_info);
typedef void  (__MTLK_IFUNC *mtlk_ccr_enable_interrupts_f)(void *ccr_mem, uint32 mask);
typedef void  (__MTLK_IFUNC *mtlk_ccr_disable_interrupts_f)(void *ccr_mem, uint32 mask);
typedef uint32(__MTLK_IFUNC *mtlk_ccr_clear_interrupts_if_pending_f)(void *ccr_mem, uint32 clear_mask);
typedef BOOL  (__MTLK_IFUNC *mtlk_ccr_disable_interrupts_if_pending_f)(void *ccr_mem, uint32 mask);
typedef void  (__MTLK_IFUNC *mtlk_ccr_initiate_doorbell_inerrupt_f)(void *ccr_mem);
typedef uint32(__MTLK_IFUNC *mtlk_ccr_is_interrupt_pending_f)(void *ccr_mem);
typedef void  (__MTLK_IFUNC *mtlk_ccr_recover_interrupts_f)(void *ccr_mem);
typedef void  (__MTLK_IFUNC *mtlk_ccr_release_ctl_from_reset_f)(void *ccr_mem, uint32 chip_id);
typedef void  (__MTLK_IFUNC *mtlk_ccr_put_ctl_to_reset_f)(void *ccr_mem);
typedef void  (__MTLK_IFUNC *mtlk_ccr_boot_from_bus_f)(void *ccr_mem);
typedef void  (__MTLK_IFUNC *mtlk_ccr_clear_boot_from_bus_f)(void *ccr_mem);
typedef void  (__MTLK_IFUNC *mtlk_ccr_switch_to_iram_boot_f)(void *ccr_mem);
typedef void  (__MTLK_IFUNC *mtlk_ccr_exit_debug_mode_f)(void *ccr_mem);
typedef void  (__MTLK_IFUNC *mtlk_ccr_put_cpus_to_reset_f)(void *ccr_mem);
typedef void  (__MTLK_IFUNC *mtlk_ccr_reset_mac_f)(void *ccr_mem);
typedef void  (__MTLK_IFUNC *mtlk_ccr_power_on_cpus_f)(void *ccr_mem);
typedef void  (__MTLK_IFUNC *mtlk_ccr_get_afe_value_f)(void *ccr_mem, uint32 *values);
typedef uint32(__MTLK_IFUNC *mtlk_ccr_get_progmodel_version_f)(void *ccr_mem);
typedef void  (__MTLK_IFUNC *mtlk_ccr_bist_efuse_f)(void *ccr_mem, uint32 chip_id);
typedef int   (__MTLK_IFUNC *mtlk_ccr_init_pll_f)(void *ccr_mem, uint32 chip_id);
typedef void  (__MTLK_IFUNC *mtlk_ccr_release_cpus_reset_f)(void *ccr_mem);
typedef BOOL  (__MTLK_IFUNC *mtlk_ccr_check_bist_f)(void *ccr_mem, uint32 *bist_result);
typedef void  (__MTLK_IFUNC *mtlk_ccr_eeprom_init_f)(void *ccr_mem);
typedef int   (__MTLK_IFUNC *mtlk_ccr_eeprom_override_write_f)(void *ccr_mem, uint32 data, uint32 mask);
typedef uint32(__MTLK_IFUNC *mtlk_ccr_eeprom_override_read_f)(void *ccr_mem);
typedef uint32(__MTLK_IFUNC *mtlk_ccr_get_mips_freq_f)(void *ccr_mem, uint32 chip_id);
typedef void  (__MTLK_IFUNC *mtlk_ccr_set_msi_intr_mode_f)(void *ccr_mem, uint32 set_msi_intr_mode);
#ifdef CPTCFG_IWLWAV_TSF_TIMER_ACCESS_ENABLED
typedef void  (__MTLK_IFUNC *mtlk_ccr_read_hw_timestamp_f)(void *ccr_mem, uint32 *low, uint32 *high);
#endif
typedef void  (__MTLK_IFUNC *mtlk_ccr_ctrl_ring_init_f)(void *ccr_mem, void *ring_regs, unsigned char *mmb_base);
typedef uint32(__MTLK_IFUNC *mtlk_ccr_ring_get_hds_to_process_f)(void *ccr_mem, void *ring_regs);
typedef void  (__MTLK_IFUNC *mtlk_ccr_ring_clear_interrupt_f)(void *ring_regs, uint32 hds_processed);
typedef void  (__MTLK_IFUNC *mtlk_ccr_ring_initiate_doorbell_f)(void *ccr_mem, void *ring_regs);
typedef void  (__MTLK_IFUNC *mtlk_ccr_print_irq_regs_f)(void *ccr_mem);
typedef uint32(__MTLK_IFUNC *mtlk_ccr_efuse_read_f)(void *ccr_mem, void *buf, uint32 offset, uint32 size);
typedef uint32(__MTLK_IFUNC *mtlk_ccr_efuse_write_f)(void *ccr_mem, void *buf, uint32 offset, uint32 size);
typedef uint32(__MTLK_IFUNC *mtlk_ccr_efuse_get_size_f)(void *ccr_mem);
typedef uint32(__MTLK_IFUNC *mtlk_ccr_read_uram_version_f)(void *ccr_mem);
typedef int   (__MTLK_IFUNC *mtlk_ccr_read_phy_inband_power_f)(void *ccr_mem, unsigned radio_id, uint32 ant_mask,
                                                               int32 *noise_estim, int32 *system_gain);


typedef struct {
  mtlk_ccr_init_f                          init;
  mtlk_ccr_cleanup_f                       cleanup;
  mtlk_ccr_read_chip_id_f                  read_chip_id;
  mtlk_ccr_get_fw_info_f                   get_fw_info;
  mtlk_ccr_get_fw_dump_info_f              get_fw_dump_info;
  mtlk_ccr_get_hw_dump_info_f              get_hw_dump_info;
  mtlk_ccr_enable_interrupts_f             enable_interrupts;
  mtlk_ccr_disable_interrupts_f            disable_interrupts;
  mtlk_ccr_clear_interrupts_if_pending_f   clear_interrupts_if_pending;
  mtlk_ccr_disable_interrupts_if_pending_f disable_interrupts_if_pending;
  mtlk_ccr_initiate_doorbell_inerrupt_f    initiate_doorbell_inerrupt;
  mtlk_ccr_is_interrupt_pending_f          is_interrupt_pending;
  mtlk_ccr_recover_interrupts_f            recover_interrupts;
  mtlk_ccr_release_ctl_from_reset_f        release_ctl_from_reset;
  mtlk_ccr_put_ctl_to_reset_f              put_ctl_to_reset;
  mtlk_ccr_boot_from_bus_f                 boot_from_bus;
  mtlk_ccr_clear_boot_from_bus_f           clear_boot_from_bus;
  mtlk_ccr_switch_to_iram_boot_f           switch_to_iram_boot;
  mtlk_ccr_exit_debug_mode_f               exit_debug_mode;
  mtlk_ccr_put_cpus_to_reset_f             put_cpus_to_reset;
  mtlk_ccr_reset_mac_f                     reset_mac;
  mtlk_ccr_power_on_cpus_f                 power_on_cpus;
  mtlk_ccr_get_afe_value_f                 get_afe_value;
  mtlk_ccr_get_progmodel_version_f         get_progmodel_version;
  mtlk_ccr_bist_efuse_f                    bist_efuse;
  mtlk_ccr_init_pll_f                      init_pll;
  mtlk_ccr_release_cpus_reset_f            release_cpus_reset;
  mtlk_ccr_check_bist_f                    check_bist;
  mtlk_ccr_eeprom_init_f                   eeprom_init;
  mtlk_ccr_eeprom_override_write_f         eeprom_override_write;
  mtlk_ccr_eeprom_override_read_f          eeprom_override_read;
  mtlk_ccr_get_mips_freq_f                 get_mips_freq;
  mtlk_ccr_set_msi_intr_mode_f             set_msi_intr_mode;
#ifdef CPTCFG_IWLWAV_TSF_TIMER_ACCESS_ENABLED
  mtlk_ccr_read_hw_timestamp_f             read_hw_timestamp;
#endif
  mtlk_ccr_ctrl_ring_init_f                ctrl_ring_init;
  mtlk_ccr_ring_get_hds_to_process_f       ring_get_hds_to_process;
  mtlk_ccr_ring_clear_interrupt_f          ring_clear_interrupt;
  mtlk_ccr_ring_initiate_doorbell_f        ring_initiate_doorbell_interrupt;
  mtlk_ccr_print_irq_regs_f                print_irq_regs;
  mtlk_ccr_efuse_read_f                    efuse_read;
  mtlk_ccr_efuse_write_f                   efuse_write;
  mtlk_ccr_efuse_get_size_f                efuse_get_size;
  mtlk_ccr_read_uram_version_f             read_uram_version;
  mtlk_ccr_read_phy_inband_power_f         read_phy_inband_power;
} mtlk_ccr_api_t;

const mtlk_ccr_api_t *__MTLK_IFUNC mtlk_ccr_pcieg5_api_get(void);
const mtlk_ccr_api_t *__MTLK_IFUNC mtlk_ccr_pcieg6a0_api_get(void);
const mtlk_ccr_api_t *__MTLK_IFUNC mtlk_ccr_pcieg6b0_api_get(void);
const mtlk_ccr_api_t *__MTLK_IFUNC mtlk_ccr_pcieg6d2_api_get(void);

#endif /*_MTLK_CCR_API_H_*/
