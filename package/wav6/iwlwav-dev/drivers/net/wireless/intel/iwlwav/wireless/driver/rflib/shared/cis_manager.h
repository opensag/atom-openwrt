/******************************************************************************

                               Copyright (c) 2012
                            Lantiq Deutschland GmbH

  For licensing information, see the file 'LICENSE' in the root folder of
  this software module.

******************************************************************************/
#ifndef __CIS_MANAGER__
#define __CIS_MANAGER__

#define  MTLK_IDEFS_ON
#define  MTLK_IDEFS_PACKING 1
#include "mtlkidefs.h"

/**
  Generic CIS memory area structure
 */
typedef struct {
  /** Number of bytes from address until last CIS section */
  uint16 size;
  /** Base address the CIS sections will be loaded to */
  uint32 base_address; /* FIXME: to be ready for unaligned address */
  /** Minor version*/
  uint8 version0;
  /** Major version*/
  uint8 version1;
} __MTLK_IDATA mtlk_cis_area_v4_t;

typedef struct {
  /** Major version*/
  uint8 version1;
} __MTLK_IDATA mtlk_cis_area_v5_t;

/* NOTE:
 * all cards that are not updated will have 0x42 value by default
 */
typedef union
{
  struct {
#if defined(__LITTLE_ENDIAN_BITFIELD)
    uint8 ap_disabled:1;
    uint8 disable_sm_channels:1;
    uint8 __reserved:6;
#elif defined(__BIG_ENDIAN_BITFIELD)
    uint8 __reserved:6;
    uint8 disable_sm_channels:1;
    uint8 ap_disabled:1;
#else
  # error Endianess not defined!
#endif
  } __MTLK_IDATA s;
  uint8 d;
} __MTLK_IDATA mtlk_cis_dev_opt_mask_t;

/* CIS: card ID */
typedef struct {
  uint8 type;
  uint8 revision;
  uint8 country_code;   /* is not supported anymore */
  mtlk_cis_dev_opt_mask_t dev_opt_mask;
  uint8 rf_chip_number;
  uint8 mac_address[ETH_ALEN];
  uint8 sn[MTLK_EEPROM_SN_LEN];
  uint8 production_week;
  uint8 production_year;
} __MTLK_IDATA mtlk_cis_cardid_t;

#define   MTLK_IDEFS_OFF
#include "mtlkidefs.h"

#define  MTLK_IDEFS_ON
#include "mtlkidefs.h"

#define EEPROM_GEN5_MAX_ANT 4
#define EEPROM_GEN6_MAX_ANT 4

typedef enum
{
  TPC_VALID_INVALID = 0,
  TPC_VALID_GEN5,
  TPC_VALID_GEN6
} tpc_valid_t;

typedef struct _mtlk_cis_rfic_t {
    uint8   cis_size;
    uint8   cis_data[RFIC_CIS_MAX_SIZE];
} mtlk_cis_rfic_t;

typedef struct _mtlk_cis_rssi_t {
    uint8   cis_size;
    uint8   cis_data[MAX_SIZE_OF_RSSI_TABLE];
} mtlk_cis_rssi_t;

/* CIS data read from the EEPROM as a structure */
typedef struct {
  uint16            version;
  uint16            xtal;
  mtlk_cis_cardid_t card_id;

  mtlk_cis_rfic_t   rfic;     /* including size */

  union {
    mtlk_cis_rssi_t     rssi_v5;  /* including size */
    struct {
      mtlk_cis_rssi_t   rssi_24;  /* including size, 2.4 GHz */
      mtlk_cis_rssi_t   rssi_52;  /* including size, 5.2 GHz */
    };
  };

  mtlk_eeprom_tpc_data_t *tpc_24;
  mtlk_eeprom_tpc_data_t *tpc_52;

  union {
    mtlk_eeprom_tpc_gen5_data_t *tpc_gen5[EEPROM_GEN5_MAX_ANT];
    mtlk_eeprom_tpc_gen6_data_t *tpc_gen6[EEPROM_GEN6_MAX_ANT];
  };

  size_t            tpc_data_size;
  tpc_valid_t       tpc_valid;
  uint8             ant_num;    /* TX */
  uint8             ant_mask;   /* TX */
  BOOL              band24_supported;
  BOOL              band52_supported;
  BOOL              is_cis_v6;
} __MTLK_IDATA mtlk_eeprom_cis_data_t;

#define   MTLK_IDEFS_OFF
#include "mtlkidefs.h"

#define XTAL_DEFAULT_VALUE  0x005C

int __MTLK_IFUNC
mtlk_cis_data_parse (void *raw_cis, int raw_cis_size,
                     mtlk_eeprom_cis_data_t *parsed_cis,
                     BOOL is_v5_format, BOOL is_v6_format);

int __MTLK_IFUNC
mtlk_cis_search_and_parse_rfic (void *raw_cis, int raw_cis_size,
                                mtlk_eeprom_cis_data_t *parsed_cis);

void __MTLK_IFUNC
mtlk_eeprom_cis_data_clean (mtlk_eeprom_cis_data_t *parsed_cis);

int __MTLK_IFUNC
mtlk_cis_crc_parse (void *raw_cis, int raw_cis_size,
                    uint32 *crc_value, uint16 *data_len, BOOL is_cis_v6);

static __INLINE mtlk_cis_rssi_t *
mtlk_cis_get_rssi_data (mtlk_eeprom_cis_data_t *parsed_cis, mtlk_hw_band_e band)
{
    mtlk_cis_rssi_t *rssi = NULL;

    if (parsed_cis->is_cis_v6) {
      if(MTLK_HW_BAND_2_4_GHZ == band) {
        rssi = &parsed_cis->rssi_24;
      } else if(MTLK_HW_BAND_5_2_GHZ == band) {
        rssi = &parsed_cis->rssi_52;
      }
    } else { /* v5 */
        rssi = &parsed_cis->rssi_v5;
    }

    return rssi;
}

#endif /* __CIS_MANAGER__ */
