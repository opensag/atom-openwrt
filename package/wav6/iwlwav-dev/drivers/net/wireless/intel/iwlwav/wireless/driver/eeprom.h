/******************************************************************************

                               Copyright (c) 2012
                            Lantiq Deutschland GmbH

  For licensing information, see the file 'LICENSE' in the root folder of
  this software module.

******************************************************************************/
#ifndef _MTLK_EEPROM_H_
#define _MTLK_EEPROM_H_

#include "mtlkdfdefs.h"
#include "mtlk_ab_manager.h"
#include "txmm.h"
#include "mtlk_clipboard.h"

#define  MTLK_IDEFS_ON
#include "mtlkidefs.h"

/* TODO: GS: Move it in some common configuration file */
#define NUM_TX_ANTENNAS_GEN5 (4)
#define MAX_NUM_TX_ANTENNAS  NUM_TX_ANTENNAS_GEN5
#define MAX_NUM_RX_ANTENNAS  MAX_NUM_TX_ANTENNAS

/* hardware codes as read from the EEPROM.
   These constants are used for Windows debug messages */
#define MTLK_EEPROM_HW_CODE_EVM          0xC5
#define MTLK_EEPROM_HW_CODE_CARDBUS      0xC6
#define MTLK_EEPROM_HW_CODE_LONGPCI      0xC4
#define MTLK_EEPROM_HW_CODE_SHORTPCI     0xC9

#define MTLK_EEPROM_TYPE_LEN   8
#define MTLK_EEPROM_SN_LEN     3

/* Supported EEPROM sizes:
    MAX - for both types FILE and GPIO (flash)
    MIN - for GPIO only
 */
#define MTLK_MAX_EEPROM_SIZE   (2048)
#define MTLK_MIN_EEPROM_SIZE   ( 256)

/* This value indicates that Calibration File type is non-operational */
#define MTLK_EEPROM_PRODUCTION_MODE      0x72

typedef struct _mtlk_eeprom_data_t mtlk_eeprom_data_t;

struct _mtlk_eeprom_data_cfg_t;
struct _mtlk_eeprom_cfg_t;

/* band TODO: GS: Move it in some common configuration file */
enum mtlk_hw_band {
  MTLK_HW_BAND_5_2_GHZ,
  MTLK_HW_BAND_2_4_GHZ,
  MTLK_HW_BAND_BOTH,
  MTLK_HW_BAND_NONE
};

typedef enum mtlk_hw_band  mtlk_hw_band_e;

/* EEPROM to DF UI interface structure*/
typedef struct {
  BOOL   ap_disabled;
  BOOL   disable_sm_channels;
} __MTLK_IDATA mtlk_eeprom_data_stat_entry_t;

/* Wave500/Wave600 EFUSE support */

typedef struct {
    union {
      uint8   bytes[AFE_CALIBRATION_DATA_SIZE_GEN5];
      struct {
        /* byte 0 */
#if defined(__LITTLE_ENDIAN_BITFIELD)
        uint8   bgp_vref_conf   : 3;
        uint8   r_bias_conf     : 4;
        uint8   trim_indicator  : 1;
#elif defined(__BIG_ENDIAN_BITFIELD)
        uint8   bgp_vref_conf   : 3;
        uint8   r_bias_conf     : 4;
        uint8   trim_indicator  : 1;
#endif
      };
    };
} wave_efuse_afe_data_v5_t;

typedef struct {
    union {
      uint8   bytes[AFE_CALIBRATION_DATA_SIZE_GEN6];
      uint32  word0; /* only 1st word will be parsed */
      struct {
#if defined(__LITTLE_ENDIAN_BITFIELD)
             /* Name in ABB API    Bits in Byte  Name in EFUSE map */
        uint32  ldo_central_clkin   : 4; /*  3   ldofsys        */
        uint32  ldo_central_clkd    : 4; /*  3   ldoclkd        */
        uint32  iptattrim           : 4; /*  2   r_ptat_conf    */
        uint32  ldo_central_pllpost : 4; /*  2   ldo0v95pllpost */
        uint32  ldo_central_pllref  : 4; /*  1   ldo0v95pllvref */
        uint32  ldo_central_pll1v8  : 3; /*  1   vreg1v8pll     */
        uint32  trim_indicator      : 1; /*  1   trim_indicator */
        uint32  vreftrim            : 4; /*  0   bgp_vref_conf  */
        uint32  ibiastrim           : 4; /*  0   r_bias_conf    */
#elif defined(__BIG_ENDIAN_BITFIELD)
             /* Name in ABB API    Bits in Byte  Name in EFUSE map */
        uint32  ibiastrim           : 4; /*  0   r_bias_conf    */
        uint32  vreftrim            : 4; /*  0   bgp_vref_conf  */
        uint32  trim_indicator      : 1; /*  1   trim_indicator */
        uint32  ldo_central_pll1v8  : 3; /*  1   vreg1v8pll     */
        uint32  ldo_central_pllref  : 4; /*  1   ldo0v95pllvref */
        uint32  ldo_central_pllpost : 4; /*  2   ldo0v95pllpost */
        uint32  iptattrim           : 4; /*  2   r_ptat_conf    */
        uint32  ldo_central_clkd    : 4; /*  3   ldoclkd        */
        uint32  ldo_central_clkin   : 4; /*  3   ldofsys        */
#endif
      };
    };
} wave_efuse_afe_data_v6_t;

mtlk_eeprom_data_t* __MTLK_IFUNC mtlk_eeprom_create(void);
void __MTLK_IFUNC mtlk_eeprom_delete(mtlk_eeprom_data_t *eeprom_data);

int __MTLK_IFUNC
mtlk_eeprom_read_and_parse(mtlk_hw_api_t *hw_api, mtlk_eeprom_data_t *ee_data, uint32 chip_id);

int __MTLK_IFUNC
mtlk_eeprom_check_ee_data(mtlk_eeprom_data_t *eeprom, mtlk_txmm_t* txmm_p, BOOL is_ap);

int __MTLK_IFUNC
mtlk_reload_tpc(mtlk_hw_band_e band, uint8 spectrum_mode, uint8 upper_lower, uint16 channel, mtlk_txmm_t *txmm,
                mtlk_txmm_msg_t *msgs, uint32 nof_msgs, mtlk_eeprom_data_t *eeprom, uint8 num_tx_antennas);

void __MTLK_IFUNC mtlk_clean_eeprom_data(mtlk_eeprom_data_t *eeprom_data);

char* __MTLK_IFUNC mtlk_eeprom_band_to_string(mtlk_hw_band_e band);

/* EEPROM data accessors */

uint32 __MTLK_IFUNC
mtlk_eeprom_get_xtal_value(const mtlk_eeprom_data_t *ee_data);

BOOL __MTLK_IFUNC mtlk_eeprom_is_band_supported(const mtlk_eeprom_data_t *ee_data, mtlk_hw_band_e band);

int __MTLK_IFUNC mtlk_eeprom_is_valid(const mtlk_eeprom_data_t *ee_data);

uint8 __MTLK_IFUNC mtlk_eeprom_get_nic_type(mtlk_eeprom_data_t *eeprom_data);

uint8 __MTLK_IFUNC mtlk_eeprom_get_nic_revision(mtlk_eeprom_data_t *eeprom_data);

uint8 __MTLK_IFUNC mtlk_eeprom_get_tx_ant_mask(mtlk_eeprom_data_t *eeprom_data);

const uint8* __MTLK_IFUNC mtlk_eeprom_get_nic_mac_addr(mtlk_eeprom_data_t *eeprom_data);

void mtlk_eeprom_get_cfg(mtlk_eeprom_data_t *eeprom, struct _mtlk_eeprom_data_cfg_t *cfg);

int __MTLK_IFUNC
mtlk_eeprom_get_raw_data(mtlk_hw_api_t *hw_api, struct _mtlk_eeprom_cfg_t *eeprom_cfg);

int __MTLK_IFUNC
mtlk_eeprom_access_init(mtlk_vap_handle_t vap);

void __MTLK_IFUNC
mtlk_eeprom_access_cleanup(mtlk_vap_handle_t vap);

int __MTLK_IFUNC mtlk_eeprom_get_caps (const mtlk_eeprom_data_t *eeprom, mtlk_clpb_t *clpb);

uint8 __MTLK_IFUNC mtlk_eeprom_get_num_antennas(mtlk_eeprom_data_t *eeprom);

uint8 __MTLK_IFUNC mtlk_eeprom_get_disable_sm_channels(mtlk_eeprom_data_t *eeprom);

uint8 __MTLK_IFUNC wave_eeprom_cal_file_type_get(mtlk_eeprom_data_t *eeprom);

static __INLINE BOOL wave_eeprom_is_production_mode(uint8 cal_file_type)
{
  return (cal_file_type == MTLK_EEPROM_PRODUCTION_MODE);
}

static __INLINE const char *wave_eeprom_cal_file_type_to_string(uint8 cal_file_type)
{
  return wave_eeprom_is_production_mode(cal_file_type) ? "Production" : "Operational";
}

uint16 __MTLK_IFUNC mtlk_eeprom_get_vendor_id(mtlk_eeprom_data_t *eeprom);

uint16 __MTLK_IFUNC mtlk_eeprom_get_device_id(mtlk_eeprom_data_t *eeprom);

uint16 mtlk_eeprom_get_version(mtlk_eeprom_data_t *eeprom);
BOOL   mtlk_eeprom_is_new_format(mtlk_eeprom_data_t *eeprom);

uint8 mtlk_eeprom_get_numpoints52(mtlk_eeprom_data_t *eeprom);
uint8 mtlk_eeprom_get_numpoints24(mtlk_eeprom_data_t *eeprom);

uint8 *mtlk_eeprom_get_afe_data(mtlk_eeprom_data_t *eeprom, unsigned *size);
uint8 *mtlk_eeprom_get_rfic_data(mtlk_eeprom_data_t *eeprom, unsigned *size);
uint8 *mtlk_eeprom_get_rssi_data(mtlk_eeprom_data_t *eeprom, unsigned *size, mtlk_hw_band_e band);

#define   MTLK_IDEFS_OFF
#include "mtlkidefs.h"

#endif
