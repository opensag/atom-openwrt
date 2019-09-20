/******************************************************************************

                               Copyright (c) 2012
                            Lantiq Deutschland GmbH

  For licensing information, see the file 'LICENSE' in the root folder of
  this software module.

******************************************************************************/
/*
 * Accessor to the Card Information Section from EEPROM
 */

#include "mtlkinc.h"
#include "mtlkerr.h"
#include "channels.h"
#include "mtlkaux.h"

#include "mtlk_eeprom.h"
#include "cis_manager.h"
#include "mtlkbfield.h"

#include "mtlk_tpcv4.h"
#include "mtlk_algorithms.h"
#include "mtlk_coreui.h"
#include "mtlk_df.h"
#include "mtlk_card_selector.h"

#define LOG_LOCAL_GID   GID_CIS
#define LOG_LOCAL_FID   0

/*****************************************************************************
* Local type definitions
******************************************************************************/

/* \cond DOXYGEN_IGNORE */
#define  MTLK_IDEFS_ON
#define  MTLK_IDEFS_PACKING 1
#include "mtlkidefs.h"
/* \endcond */

/* CIS: TPC */
typedef struct
{
    uint8 size_24;
    uint8 size_52;
    /* ... TPC data ... */
}__MTLK_IDATA tpc_header_t;

/* Card setions TPL codes */
typedef enum {
  CIS_TPL_CODE_ZERO            = 0x00,   /* last in EFUSE */
  CIS_TPL_CODE_GEN6_CID        = 0x60,
  CIS_TPL_CODE_GEN6_TX_TSSI    = 0x61,
  CIS_TPL_CODE_GEN6_TX_SUBBAND = 0x62,
  CIS_TPL_CODE_GEN6_RSSI       = 0x63,   /* RX */
  CIS_TPL_CODE_GEN6_XTAL       = 0x64,
  CIS_TPL_CODE_CID             = 0x80,
  CIS_TPL_CODE_TPCG3           = 0x83,
  CIS_TPL_CODE_XTAL            = 0x84,
  CIS_TPL_CODE_CRC             = 0x85,
  CIS_TPL_CODE_4A_5G           = 0x87,
  CIS_TPL_CODE_3A_5G           = 0x88,
  CIS_TPL_CODE_3A_2G           = 0x89,
  CIS_TPL_CODE_4A_2G           = 0x8A,
  CIS_TPL_CODE_RSSI            = 0x8B,
  CIS_TPL_CODE_RFIC            = 0x8F,
  CIS_TPL_CODE_NONE            = 0xFF    /* last in EEPROM */
} cis_tpl_code_t;

/* Card Information Structure header */

typedef mtlk_one_byte_data_t    cis_hdr_data_size_v5_t;
typedef mtlk_two_bytes_data_t   cis_hdr_data_size_v6_t;

typedef struct
{
    uint8                   tpl_code; /** see \ref cis_tpl_code_t */
    cis_hdr_data_size_v5_t  data_size;
    /* ... CIS data ... */
}__MTLK_IDATA cis_header_v5_t;

typedef struct
{
    uint8                   tpl_code; /** see \ref cis_tpl_code_t */
    cis_hdr_data_size_v6_t  data_size;
    /* ... CIS data ... */
}__MTLK_IDATA cis_header_v6_t;

typedef union {
  uint8 tpl_code; /* for fast access */
  cis_header_v5_t   hdr_v5;
  cis_header_v6_t   hdr_v6;
} cis_header_all_t;

/* */
static __INLINE size_t
__cis_header_hdr_size (cis_header_all_t *cis_hdr, BOOL is_v6)
{
    return (is_v6 ? sizeof(cis_header_v6_t) :
                    sizeof(cis_header_v5_t));
}

static __INLINE unsigned
__cis_header_data_size (cis_header_all_t *cis_hdr, BOOL is_v6)
{
    if (is_v6) { /* two bytes data size */
      return mtlk_two_bytes_value(&cis_hdr->hdr_v6.data_size);
    } else { /* one byte size */
      return (cis_hdr->hdr_v5.data_size);
    }
}

#define   MTLK_IDEFS_OFF
#include "mtlkidefs.h"

/*****************************************************************************
* Local definitions
******************************************************************************/
#define TPC_DEBUG 0

/*****************************************************************************
* Function implementation
******************************************************************************/

static void
_tpc_data_free(mtlk_eeprom_tpc_data_t *data)
{
  int i;

  if (NULL == data)
    return;

  if (NULL != data->antenna)
  {
    for(i = 0; i < data->num_antennas; i++)
    {
      if (NULL != data->antenna[i].point)
      {
        mtlk_osal_mem_free(data->antenna[i].point);
        data->antenna[i].point = NULL;
      }
    }

    mtlk_osal_mem_free(data->antenna);
    data->antenna = NULL;
  }

  mtlk_osal_mem_free(data);
}


static void
_tpc_gen5_data_free(mtlk_eeprom_tpc_gen5_data_t *data)
{
  if (NULL == data)
    return;

  mtlk_osal_mem_free(data);
}

static void
_tpc_gen5_list_clean(mtlk_eeprom_tpc_gen5_data_t *tpc)
{
  mtlk_eeprom_tpc_gen5_data_t *prev;

  while (tpc) {
    prev = tpc;
    tpc = tpc->next;
    _tpc_gen5_data_free(prev);
  }
}

/**
  This function tries to find a CIS (Card information
  section) in the buffer specified

  \param id         CIS block id to search for [I]
  \param cis        pointer to the CIS buffer [I]
  \param cis_size   size of CIS buffer in bytes [I]
  \param data_size  returns the size of CIS data [O]
  \param next_cis   pointer to the next CIS buffer [O]

  \return
    NULL if CIS block not found, otherwise pointer to the CIS data
*/
static void*
_cis_data_find (const uint8 *id_array, size_t id_array_size, cis_header_all_t *cis, int cis_size,
                BOOL is_cis_v6, unsigned *data_size, void** next_cis, unsigned int *cis_idx)
{
  uint8   *data = NULL;
  size_t   hsize; /* header size */
  unsigned dsize; /* data size */
  unsigned id_idx;

  /* The header starts with two bytes defining the size of the           */
  /* CIS section in bytes (not including these bytes and the next four). */
  /* A value of 0h will be interpreted as "no CIS exist"                 */
  /* Next comes TPL_LINK byte, defining number of bytes (N) exists       */
  /*  at this CIS structure (not including the current byte).            */

  MTLK_ASSERT(NULL != cis);
  MTLK_ASSERT(NULL != data_size);

  ILOG2_D("Look for 0x%02X ...", id_array[0]);

  while ((cis_size >= sizeof(cis_header_all_t)) &&
         (cis->tpl_code != CIS_TPL_CODE_ZERO) &&
         (cis->tpl_code != CIS_TPL_CODE_NONE))
  {
    hsize = __cis_header_hdr_size(cis, is_cis_v6);
    dsize = __cis_header_data_size(cis, is_cis_v6);

    ILOG2_PDD("cis %p, code 0x%02X, size 0x%02X", cis, cis->tpl_code, dsize);

    if ((hsize + dsize) > cis_size)
    {
      ELOG_DP("Partial CIS[0x%02X] detected at 0x%p",
        cis->tpl_code, cis);
      return NULL;
    }

    data = (uint8*) cis + hsize;
    for(id_idx = 0; id_idx < id_array_size; id_idx++)
    {
      if (cis->tpl_code == id_array[id_idx])
      {
        if (next_cis)
          *next_cis = data + dsize;

        *data_size = dsize;

        if(cis_idx)
        {
          *cis_idx = id_idx;
        }

        return data;
      }
    }

    cis_size -= (hsize + dsize);

    cis = (cis_header_all_t *) (data + dsize); /* next cis */
  }

  return NULL;
}


/**
  Cleanup enumerator for TPC list
*/
static void
_tpc_list_clean(mtlk_eeprom_tpc_data_t *tpc)
{
  mtlk_eeprom_tpc_data_t *prev;

  while (tpc) {
    prev = tpc;
    tpc = tpc->next;
    _tpc_data_free(prev);
  }
}

static mtlk_eeprom_tpc_gen5_data_t *
_tpc_gen5_data_alloc(void)
{
  mtlk_eeprom_tpc_gen5_data_t *data = NULL;

  data = mtlk_osal_mem_alloc(sizeof(mtlk_eeprom_tpc_gen5_data_t),
                             MTLK_MEM_TAG_TPC_DATA);
  if (NULL == data)
    return NULL;

  memset(data, 0, sizeof(mtlk_eeprom_tpc_gen5_data_t));

  return data;
}

static void* _tpc_gen5_list_get_next(void* item)
{
  return (void*) ((mtlk_eeprom_tpc_gen5_data_t*)item)->next;
}

static void _tpc_gen5_list_set_next(void* item, void* next)
{
  ((mtlk_eeprom_tpc_gen5_data_t*)item)->next = (mtlk_eeprom_tpc_gen5_data_t*) next;
}

static int _tpc_gen5_list_is_less(void* item1, void* item2)
{
  return ((mtlk_eeprom_tpc_gen5_data_t*)item1)->channel
    < ((mtlk_eeprom_tpc_gen5_data_t*)item2)->channel;
}

static void
_tpc_gen5_list_sort(mtlk_eeprom_tpc_gen5_data_t** list)
{
  mtlk_sort_slist(
    (void**)list,
    _tpc_gen5_list_get_next,
    _tpc_gen5_list_set_next,
    _tpc_gen5_list_is_less);
}


static int
_parse_tpc_gen5(uint8 *tpc, int tpc_size,
                mtlk_eeprom_cis_data_t *parsed_cis, unsigned int ant_num, unsigned int *ant_mask)
{
  int res = MTLK_ERR_UNKNOWN;
  unsigned  tpc_data_size; /* size per antenna */
  unsigned  tpc_freq_size; /* size per antenna including chID */
  unsigned  struct_size;   /* size for all antennas including chID */
  TPC_FREQ_3S2D *tpc_freq; /* the biggest TPC_FREQ data structure */

#define _TPC_G5_FREQ_CH_ID_SIZE_    (sizeof(((TPC_FREQ_3S2D*)0)->chID))

  if(tpc_size < 1)
  {
    ELOG_D("Cannot get struct_size. Invalid TPC size: size = %d", tpc_size);
    res = MTLK_ERR_EEPROM;
    goto END;
  }

  mtlk_dump(1, tpc, tpc_size, "tpc_g5 CIS");

  tpc_data_size = tpc[0]; /* one byte size of data (excluding chID) */
  struct_size = ant_num * tpc_data_size + _TPC_G5_FREQ_CH_ID_SIZE_;
  ILOG1_DDDD("ant_num %d, TPC CIS sizes: %d, %d, %d", ant_num, tpc_size, struct_size, tpc_data_size);

  tpc      += 1; /* tpc_data_size */
  tpc_size -= 1;

  /* The same Channel ID will be included into all TPC_FREQ data structures */
  tpc_freq_size = tpc_data_size + _TPC_G5_FREQ_CH_ID_SIZE_; /* including chID */
  if((tpc_freq_size < sizeof(TPC_FREQ_1S2D_NO_CALIB)) ||
     (tpc_freq_size > sizeof(TPC_FREQ_3S2D)))
  {
    ELOG_D("Invalid single freq struct size in TPC CIS: size = %d", tpc_data_size);
    res = MTLK_ERR_EEPROM;
    goto END;
  }

  *ant_mask = 0;    /* empty mask */
  while(tpc_size >= struct_size)
  {
    uint8 chan_num;
    unsigned int substruct_idx;
    unsigned int curr_ant_mask;

    curr_ant_mask = 0;    /* empty mask */

    tpc_freq = (TPC_FREQ_3S2D*)tpc;
    chan_num = tpc_freq->chID;

    if (MTLK_HW_BAND_5_2_GHZ == channel_to_band(chan_num)) {
      parsed_cis->band52_supported = TRUE;
    } else {
      parsed_cis->band24_supported = TRUE;
    }

    for(substruct_idx = 0; substruct_idx < ant_num; substruct_idx++)
    {
      uint8    maxp, uevm;
      unsigned tpc_ant_num, bit_mask;
      TPC_FREQ_3S2D *tpc_sub_data;
      mtlk_eeprom_tpc_gen5_data_t *data = _tpc_gen5_data_alloc();

      if (!data)
      {
        ELOG_V("Failed to allocate TPC Gen5 data structure");
        res = MTLK_ERR_NO_MEM;
        goto END;
      }

      mtlk_dump(2, (void *)tpc_freq + _TPC_G5_FREQ_CH_ID_SIZE_, tpc_data_size, "tpc_g5"); /* without ChID */

      tpc_sub_data = &data->tpc_g5_data.tpcFreq3s2d;

      wave_memcpy(tpc_sub_data, sizeof(*tpc_sub_data), tpc_freq, tpc_freq_size); /* including chID */
      tpc_sub_data->chID = chan_num; /* the same chID for all tpc_sub_data */

      data->channel     = chan_num;
      data->band        = channel_to_band(chan_num);

      data->bandwidth   = tpc_sub_data->uevm_bw.uevm_bw_fields.BW;
      uevm              = tpc_sub_data->uevm_bw.uevm_bw_fields.ultimateEVM;

      tpc_ant_num       = tpc_sub_data->maxp_ant.maxp_ant_fields.antNum;
      maxp              = tpc_sub_data->maxp_ant.maxp_ant_fields.maxPower;

      ILOG1_DDDDDD("%u: chan %u, ant %u, maxp %2u, bw %u, uevm %2u",
          substruct_idx, data->channel, tpc_ant_num, maxp, data->bandwidth, uevm);

      SLOG0(0, 0, mtlk_eeprom_tpc_gen5_data_t, data);

      bit_mask = (1 << tpc_ant_num);
      if (curr_ant_mask & bit_mask) {
        ILOG1_D("TPC G5 duplicate ant %d", tpc_ant_num);
        mtlk_osal_mem_free(data);
      } else {
        curr_ant_mask |= bit_mask;
        data->next = parsed_cis->tpc_gen5[tpc_ant_num];
        parsed_cis->tpc_gen5[tpc_ant_num] = data;
      }

      tpc_size -= tpc_data_size;
      tpc      += tpc_data_size;
      tpc_freq = (TPC_FREQ_3S2D*)tpc;
    }

    tpc      += _TPC_G5_FREQ_CH_ID_SIZE_; /* next chID */
    tpc_size -= _TPC_G5_FREQ_CH_ID_SIZE_;

    if (*ant_mask == 0) {
        *ant_mask = curr_ant_mask;   /* first setting */
    } else if (*ant_mask != curr_ant_mask) {
        ELOG_DD("TPC G5 CIS contains structs with different antennas mask: 0x%02X and 0x%02X",
                *ant_mask, curr_ant_mask);
        res = MTLK_ERR_EEPROM;
        goto END;
    }
  }

  if(tpc_size)
  {
    ELOG_D("Garbage at the end of TPC CIS. tpc_size = %d", tpc_size);
    res = MTLK_ERR_EEPROM;
    goto END;
  }

  parsed_cis->tpc_data_size = tpc_data_size;
  res = MTLK_ERR_OK;

END:

  return res;
}

static int
_mtlk_cis_area_parse_g5 (mtlk_eeprom_cis_data_t *parsed_cis, void *cis, void *cis_end)
{
  uint8 *tpc = NULL;
  unsigned            data_size = 0;
  unsigned int        cis_idx = 0, actual_cis_idx;
  unsigned int        ant_idx, ant_num;
  unsigned int        ant_mask, actual_ant_mask;
  const unsigned char cis_tpl_code_tpcg5[]  = { CIS_TPL_CODE_4A_5G, CIS_TPL_CODE_3A_5G,
                                                CIS_TPL_CODE_3A_2G, CIS_TPL_CODE_4A_2G };
  const unsigned char ant_num_by_cis_idx[]  = { 4, 3, 3, 4 };

  MTLK_STATIC_ASSERT(ARRAY_SIZE(cis_tpl_code_tpcg5) == ARRAY_SIZE(ant_num_by_cis_idx));

  /* CIS may contain multiple TPC areas, we have to parse all of them */
  tpc = _cis_data_find(cis_tpl_code_tpcg5, sizeof(cis_tpl_code_tpcg5), cis, cis_end - cis,
                       parsed_cis->is_cis_v6, &data_size, &cis, &cis_idx);
  if (NULL == tpc) {
    ELOG_V("CIS TPC G5 should be present");
    return MTLK_ERR_EEPROM;
  }

  actual_ant_mask = 0;
  actual_cis_idx = cis_idx;
  ant_num = ant_num_by_cis_idx[cis_idx];

  do {
    if (MTLK_ERR_OK != _parse_tpc_gen5(tpc, data_size, parsed_cis, ant_num, &ant_mask)) {
      goto ERROR;
    }

    tpc = _cis_data_find(cis_tpl_code_tpcg5, sizeof(cis_tpl_code_tpcg5), cis, cis_end - cis,
                         parsed_cis->is_cis_v6, &data_size, &cis, &cis_idx);

    if (cis_idx != actual_cis_idx) {
      ELOG_DD("CIS contains TPC G5 areas with different codes: 0x%02X and 0x%02X",
              cis_tpl_code_tpcg5[actual_cis_idx], cis_tpl_code_tpcg5[cis_idx]);
      goto ERROR;
    }

    if (actual_ant_mask == 0) {
      actual_ant_mask = ant_mask;   /* first setting */
    } else if (actual_ant_mask != ant_mask) {
      ELOG_DD("CIS contains TPC G5 areas with different antennas mask: 0x%02X and 0x%02X",
              actual_ant_mask, ant_mask);
      goto ERROR;
    }
  } while(NULL != tpc);

  /* Sort TPC data and calculate actual antennas number */
  ant_num = 0;
  for(ant_idx = 0; ant_idx < EEPROM_GEN5_MAX_ANT; ant_idx++)
  {
    if (parsed_cis->tpc_gen5[ant_idx]) {
      _tpc_gen5_list_sort(&parsed_cis->tpc_gen5[ant_idx]);
      ant_num++;
    }
  }

  ILOG1_DD("EEPROM: TX antennas number %d, mask 0x%02X", ant_num, ant_mask);
  parsed_cis->ant_num   = ant_num;
  parsed_cis->ant_mask  = ant_mask;
  parsed_cis->tpc_valid = TPC_VALID_GEN5;

  return MTLK_ERR_OK;

ERROR:
  mtlk_eeprom_cis_data_clean(parsed_cis);
  return MTLK_ERR_EEPROM;
}

static __INLINE size_t
__cis_v6_get_tpc_data_size (unsigned nof_regions)
{
    switch (nof_regions) {
    case 1:     return CIS_V6_TX_CHAN_DATA_SIZE(1);
    case 2:     return CIS_V6_TX_CHAN_DATA_SIZE(2);
    case 3:     return CIS_V6_TX_CHAN_DATA_SIZE(3);
    default:    return 0;
    }
}

uint32 count_bits_set (uint32 v);

static int
_parse_tpc_g6 (cis_v6_tx_common_t *tpc, int data_size,
               mtlk_eeprom_cis_data_t *parsed_cis, unsigned *tpc_ant_mask)
{
  int       res = MTLK_ERR_EEPROM;
  size_t    tpc_data_size;
  size_t    tpc_struct_size;
  unsigned  idx, ant_num, band, chan, bw;
  cis_v6_tx_chan_hdr_t  *chan_hdr;
  uint8                 *chan_data; /* per antenna channel data */

  unsigned  ant_regions = tpc->ant_mask_and_nof_regions;
  unsigned  ant_mask    = MTLK_BFIELD_GET(ant_regions, CIS_TX_COMMON_ANT_MASK);
  unsigned  nof_regions = MTLK_BFIELD_GET(ant_regions, CIS_TX_COMMON_NOF_REGIONS);

  MTLK_STATIC_ASSERT(sizeof(cis_v6_tx_chan_data_t) == sizeof(TPC_FREQ_3_REGION));

  ant_num           = count_bits_set(ant_mask);
  tpc_data_size     = __cis_v6_get_tpc_data_size(nof_regions);
  tpc_struct_size   = sizeof(*chan_hdr) + (ant_num * tpc_data_size);

  ILOG1_DD("ant_mask 0x%02X, nof_regions %u", ant_mask, nof_regions);
  ILOG1_DDDD("ant_num %d, TPC CIS sizes: %d, %d, %d", ant_num, data_size, tpc_struct_size, tpc_data_size);

  data_size -= sizeof(tpc->ant_mask_and_nof_regions);
  chan_hdr   = tpc->chan_hdr;

  while (data_size >= tpc_struct_size) {
    uint16 chan_and_bw = mtlk_two_bytes_value(&chan_hdr->chan_num_and_bw);
    chan = MTLK_BFIELD_GET(chan_and_bw, CIS_TX_COMMON_CHAN_NUM);
    bw   = MTLK_BFIELD_GET(chan_and_bw, CIS_TX_COMMON_BANDWIDTH);
    band = channel_to_band(chan);

    data_size -= sizeof(*chan_hdr);
    chan_data  = chan_hdr->chan_data;

    if (MTLK_HW_BAND_5_2_GHZ == band) {
      parsed_cis->band52_supported = TRUE;
    } else {
      parsed_cis->band24_supported = TRUE;
    }

    ILOG1_DDD("band %d, chan %u, bw %u", band, chan, bw);

    for(idx = 0; idx < EEPROM_GEN6_MAX_ANT; idx++) {
      mtlk_eeprom_tpc_gen6_data_t *data;

      if (0 == (ant_mask & (1 << idx))) { /* none data for this antenna */
        continue;
      }

      data = (void *)_tpc_gen5_data_alloc(); /* as Gen5 */
      if (!data) {
        goto END; /* MTLK_ERR_EEPROM */
      }

      /* Common Gen5 and Gen6 */
      memset(data, 0, sizeof(*data));
      data->band      = band;
      data->channel   = chan;
      data->bandwidth = bw;
      data->data_size = tpc_data_size; /* only data */

      /* Gen6 specific */
      data->tpc_g6_data.chan_num_and_bw = chan_hdr->chan_num_and_bw;
      wave_memcpy(&data->tpc_g6_data.chan_data, sizeof(data->tpc_g6_data.chan_data), chan_data, tpc_data_size);

      /* Put data into list */
      data->next = parsed_cis->tpc_gen6[idx];
      parsed_cis->tpc_gen6[idx] = data;

      ILOG0_DDD("TPC data for chan %u, bw %u, antenna %u", chan, bw, idx);
      mtlk_dump(2, &data->tpc_g6_data, sizeof(data->tpc_g6_data), "");

      if (0 == parsed_cis->tpc_data_size) { /* first setting */
        parsed_cis->tpc_data_size = data->data_size;
      } else if (parsed_cis->tpc_data_size != data->data_size) {
        ELOG_DDD("Different TPC sizes for band %u: %u and %u",
                 band, parsed_cis->tpc_data_size, data->data_size);

        goto END; /* MTLK_ERR_EEPROM */
      }

      chan_data += tpc_data_size;
      data_size -= tpc_data_size;
    }

    chan_hdr = (cis_v6_tx_chan_hdr_t *)chan_data; /* next */
  }

  if(data_size) {
    ELOG_D("Garbage at the end of CIS TPC of size = %d", data_size);
      goto END; /* MTLK_ERR_EEPROM */
  }

  *tpc_ant_mask = ant_mask;

  res = MTLK_ERR_OK;

END:

  return res;
}

static int
_mtlk_cis_area_parse_g6 (mtlk_eeprom_cis_data_t *parsed_cis, void *cis, void *cis_end)
{
  cis_v6_tx_common_t  *tpc = NULL;
  unsigned      data_size = 0;
  unsigned      ant_idx, ant_num = 0, ant_mask = 0;
  unsigned      tpc_ant_mask;
  const uint8   cis_tpl_code_tpc_g6[]  = { CIS_TPL_CODE_GEN6_TX_TSSI };

  /* CIS may contain multiple TPC areas, we have to parse all of them */
  tpc = _cis_data_find(cis_tpl_code_tpc_g6, sizeof(cis_tpl_code_tpc_g6), cis, cis_end - cis,
                       parsed_cis->is_cis_v6, &data_size, &cis, NULL);

  if (NULL == tpc) {
    WLOG_V("CIS TPC G6 missing"); /* It is Ok */
    ant_num  = 4; /* set to max */
    ant_mask = 0x0F;
    goto FINISH;
  }

  while(tpc) {

    if (MTLK_ERR_OK != _parse_tpc_g6(tpc, data_size, parsed_cis, &tpc_ant_mask)) {
      goto ERROR;
    }

    ant_mask |= tpc_ant_mask;

    tpc = _cis_data_find(cis_tpl_code_tpc_g6, sizeof(cis_tpl_code_tpc_g6), cis, cis_end - cis,
                         parsed_cis->is_cis_v6, &data_size, &cis, NULL);
  }

  /* Sort TPC data and calculate actual antennas number */
  ant_num = 0;
  for(ant_idx = 0; ant_idx < EEPROM_GEN6_MAX_ANT; ant_idx++)
  {
    if (parsed_cis->tpc_gen6[ant_idx]) {
      _tpc_gen5_list_sort(&parsed_cis->tpc_gen6[ant_idx]);
      ant_num++;
    }
  }

FINISH:
  ILOG0_DD("EEPROM: TX antennas number %d, mask 0x%02X", ant_num, ant_mask);
  parsed_cis->ant_num   = ant_num;
  parsed_cis->ant_mask  = ant_mask;
  parsed_cis->tpc_valid = TPC_VALID_GEN6;

  return MTLK_ERR_OK;

ERROR:
  mtlk_eeprom_cis_data_clean(parsed_cis);
  return MTLK_ERR_EEPROM;
}

int __MTLK_IFUNC
mtlk_cis_search_and_parse_rfic (void *raw_cis, int raw_cis_size, mtlk_eeprom_cis_data_t *parsed_cis)
{
  uint8    *rfic_cis = NULL;
  unsigned  cis_size = 0;
  const unsigned char cis_tpl_code_rfic[]  = {CIS_TPL_CODE_RFIC};

  rfic_cis = _cis_data_find(cis_tpl_code_rfic, sizeof(cis_tpl_code_rfic), raw_cis, raw_cis_size,
                            parsed_cis->is_cis_v6, &cis_size, NULL, NULL);

  if (!rfic_cis) {
    ILOG1_V("RFIC CIS not found");
    return MTLK_ERR_OK;
  }

  if (cis_size > (RFIC_CIS_MAX_SIZE)) {
    ELOG_D("Size of RFIC CIS too big (%u)", cis_size);
    return MTLK_ERR_EEPROM;
  }

  /* Store data */
  MTLK_STATIC_ASSERT(RFIC_CIS_MAX_SIZE <= MAX_UINT8);
  parsed_cis->rfic.cis_size = (uint8)cis_size;
  wave_memcpy(&parsed_cis->rfic.cis_data, sizeof(parsed_cis->rfic.cis_data), rfic_cis, cis_size);

  mtlk_dump(2, &parsed_cis->rfic.cis_data, cis_size, "dump of RFIC CIS data");

  return MTLK_ERR_OK;
}

static uint8 *
_mtlk_cis_search_rssi (uint8 cis_tpl_code,
                       void *raw_cis, unsigned raw_cis_size,
                       unsigned *size, void **next_cis,
                       mtlk_eeprom_cis_data_t *parsed_cis)
{
  uint8    *cis;

  cis = _cis_data_find(&cis_tpl_code, 1, /* exact code*/
                       raw_cis, raw_cis_size,
                       parsed_cis->is_cis_v6, size, next_cis, NULL);

  if (!cis) {
    return NULL;
  }

  ILOG2_D("RSSI CIS size %u", *size);

  MTLK_STATIC_ASSERT(MAX_SIZE_OF_RSSI_TABLE <= MAX_UINT8);

  if (*size > (MAX_SIZE_OF_RSSI_TABLE)) {
    ELOG_D("Size of RSSI CIS too big (%u)", *size);
    return NULL;
  }

  mtlk_dump(2, cis, *size, "dump of RSSI CIS data");

  return cis;
}

/* Single RSSI CIS for version 5 */
static int
_mtlk_cis_search_and_parse_rssi_v5 (void *raw_cis, void *raw_cis_end, mtlk_eeprom_cis_data_t *parsed_cis)
{
  uint8    *cis = NULL;
  unsigned  size = 0;

  cis = _mtlk_cis_search_rssi(CIS_TPL_CODE_RSSI,
                              raw_cis, raw_cis_end - raw_cis,
                              &size, NULL, parsed_cis);

  if (cis) {
    /* Store data */
    parsed_cis->rssi_v5.cis_size = (uint8)size;
    wave_memcpy(&parsed_cis->rssi_v5.cis_data, sizeof(parsed_cis->rssi_v5.cis_data), cis, size);
  } else {
    ILOG0_V("RSSI CIS not found");
  }

  return MTLK_ERR_OK;
}

/* Multiple RSSI CIS for version 6 */
static int
_mtlk_cis_search_and_parse_rssi_v6 (void *raw_cis, void *raw_cis_end, mtlk_eeprom_cis_data_t *parsed_cis)
{
  uint16            frequency;
  mtlk_hw_band_e    band;
  mtlk_cis_rssi_t  *parsed_rssi;
  cis_v6_rssi_t    *rssi;
  unsigned          size;

  rssi = (cis_v6_rssi_t *)_mtlk_cis_search_rssi(CIS_TPL_CODE_GEN6_RSSI,
                                raw_cis, raw_cis_end - raw_cis, &size, &raw_cis, parsed_cis);

  while (rssi) {
    frequency = mtlk_two_bytes_value(&rssi->calibration_chan);
    band = frequency_to_band(frequency);

    mtlk_dump(2, rssi, size, "RSSI");

    ILOG0_DDD("RSSI CIS found ant_mask 0x%02X, freq %u, band %d",
              rssi->ant_mask, frequency, (int)band);

    parsed_rssi = mtlk_cis_get_rssi_data(parsed_cis, band);
    if (parsed_rssi->cis_size) {
      WLOG_D("Duplicate RSSI CIS data for band %d -- skip", (int)band);
    } else {
      /* Store data */
      parsed_rssi->cis_size = (uint8)size;
      wave_memcpy(&parsed_rssi->cis_data, sizeof(parsed_rssi->cis_data), rssi, size);

    }

    rssi = (cis_v6_rssi_t *)_mtlk_cis_search_rssi(CIS_TPL_CODE_GEN6_RSSI,
                                raw_cis, raw_cis_end - raw_cis, &size, &raw_cis, parsed_cis);
  }

  return MTLK_ERR_OK;
}

/**
  This routine provide parsing of CIS area from EEPROM

  \param raw_cis      Pointer to raw CIS data
  \param raw_cis_size Size of raw CIS data [I]
  \param parsed_cis   Pointer to the buffer for parsed data [O]
  \param is_v5_format Flag of version5 (new) format

  \return
    MTLK_ERR_OK on success or error value from \ref mtlk_error_t
*/
int __MTLK_IFUNC
mtlk_cis_data_parse (void *raw_cis,
                     int   raw_cis_size,
                     mtlk_eeprom_cis_data_t *parsed_cis,
                     BOOL is_v5_format, BOOL is_v6_format)
{
  BOOL is_cis_v6;
  void *cis_end = NULL;
  unsigned data_size;
  mtlk_cis_cardid_t *card_id = NULL;
  uint16 *xtal_data = NULL;
  int res = MTLK_ERR_EEPROM;
  const unsigned char cis_tpl_code_cid[]  = {CIS_TPL_CODE_CID, CIS_TPL_CODE_GEN6_CID};
  const unsigned char cis_tpl_code_xtal[] = {CIS_TPL_CODE_XTAL, CIS_TPL_CODE_GEN6_XTAL};

  MTLK_ASSERT(raw_cis);
  MTLK_ASSERT(raw_cis_size > 0);
  MTLK_ASSERT(parsed_cis);

  is_cis_v6 = parsed_cis->is_cis_v6;
  parsed_cis->tpc_valid = TPC_VALID_INVALID;

  cis_end = raw_cis + raw_cis_size;
  card_id = _cis_data_find(cis_tpl_code_cid, sizeof(cis_tpl_code_cid), raw_cis, raw_cis_size,
                           is_cis_v6, &data_size, NULL, NULL);

  if (!card_id)
  {
    ELOG_V("Can not find Card ID CIS");
    goto ERROR;
  }

  if (data_size != sizeof(*card_id))
  {
    ELOG_V("Incorrect size of Card ID CIS");
    goto ERROR;
  }

  /* check card_id values */
  ILOG0_DDC("EEPROM Card ID: type 0x%02X, revision 0x%02X (%c)",
            card_id->type, card_id->revision, card_id->revision);

  /* Extend Card ID revision with printable chars from 0x40 ('@') to 0x7E ('~') */
  if ((card_id->revision < 0x40) || (card_id->revision > 0x7E))
  {
    ELOG_V("Invalid Card revision");
    goto ERROR;
  }

  parsed_cis->card_id = *card_id;

  /* Handle optional XTAL configuration */
  xtal_data = _cis_data_find(cis_tpl_code_xtal, sizeof(cis_tpl_code_xtal), raw_cis, raw_cis_size,
                             is_cis_v6, &data_size, NULL, NULL);
  if (NULL != xtal_data)
  {
    /* The Xtal section may be longer and include
       additional byte for Temperature sensor */
    if (data_size < sizeof(parsed_cis->xtal))
    {
      ELOG_V("Incorrect size of XTAL CIS");
      goto ERROR;
    }

    /* copy Xtal value */
    wave_memcpy(&parsed_cis->xtal, sizeof(parsed_cis->xtal), xtal_data, sizeof(parsed_cis->xtal));
  }
  else
  {
    parsed_cis->xtal = HOST_TO_MAC16(XTAL_DEFAULT_VALUE);
  }

  /* Handle CIX RX (RSSI) */
  if (is_v5_format) {
    res = _mtlk_cis_search_and_parse_rssi_v5(raw_cis, cis_end, parsed_cis);
  } else if (is_v6_format) {
    res = _mtlk_cis_search_and_parse_rssi_v6(raw_cis, cis_end, parsed_cis);
  }
  if (res != MTLK_ERR_OK) {
    goto ERROR;
  }

  /* Handle CIX TX (TPC) */
  parsed_cis->band24_supported = FALSE;
  parsed_cis->band52_supported = FALSE;

  if (is_v5_format) {
    res = _mtlk_cis_area_parse_g5(parsed_cis, raw_cis, cis_end);
    if(res != MTLK_ERR_OK) {
      ELOG_V("No TPC data CIS in calibration file");
      goto ERROR;
    }
  } else if (is_v6_format) {
    res = _mtlk_cis_area_parse_g6(parsed_cis, raw_cis, cis_end);
  }

  if (res != MTLK_ERR_OK) {
    goto ERROR;
  }

  /* One and only one of bands should be specified in TPC data */
  /* At least one of them is supported because parsing was Ok */
  ILOG0_SS("EEPROM supported bands: 2.4 - %s, 5.2 - %s",
            parsed_cis->band24_supported ? "Yes" : "NO",
            parsed_cis->band52_supported ? "Yes" : "NO");

  if (parsed_cis->band24_supported && parsed_cis->band52_supported) {
    WLOG_V("EEPROM contain TPC data for both bands");
    /* res = MTLK_ERR_EEPROM; */
    /* goto END; */
  }

  SLOG1(1, 0, mtlk_eeprom_cis_data_t, parsed_cis);

  return MTLK_ERR_OK;

ERROR:
  return MTLK_ERR_EEPROM;
}

/**
  Cleanup routing for parsed CIS data

  \param parsed_cis   pointer to eeprom CIS data struct [I]
*/
void __MTLK_IFUNC
mtlk_eeprom_cis_data_clean (mtlk_eeprom_cis_data_t *parsed_cis)
{
  unsigned int ant_idx;

  _tpc_list_clean(parsed_cis->tpc_24);
  _tpc_list_clean(parsed_cis->tpc_52);

  for(ant_idx = 0; ant_idx < EEPROM_GEN5_MAX_ANT; ant_idx++)
  {
    _tpc_gen5_list_clean(parsed_cis->tpc_gen5[ant_idx]);
  }
}

/**
  This routine retrieve CRC value from CIS area of EEPROM

  \param cis_area     Pointer to CIS area [I]
  \param crc_value    Handle to the buffer for the CRC value
                      stored in the CIS area [O]
  \param data_len     Handle to the buffer for the length of CIS area data
                      used for CRC calculation [O]
                      Zero - if no CRC in the CIS area

  \return
    MTLK_ERR_OK on success or error value from \ref mtlk_error_t
*/
int __MTLK_IFUNC
mtlk_cis_crc_parse (void *raw_cis, int raw_cis_size,
                    uint32 *crc_value, uint16 *data_len, BOOL is_cis_v6)
{
  unsigned data_size;
  uint32 *eeprom_crc = NULL;
  const unsigned char cis_tpl_code_crc[] = {CIS_TPL_CODE_CRC};

  MTLK_ASSERT(raw_cis);
  MTLK_ASSERT(raw_cis_size > 0);
  MTLK_ASSERT(crc_value);
  MTLK_ASSERT(data_len);

  *data_len = 0;

  /* Handle optional CRC configuration */
  eeprom_crc = _cis_data_find(cis_tpl_code_crc, sizeof(cis_tpl_code_crc), raw_cis, raw_cis_size,
                              is_cis_v6, &data_size, NULL, NULL);

  if (NULL == eeprom_crc)
    return MTLK_ERR_OK;

  if (data_size != sizeof(*crc_value))
  {
    ELOG_V("Incorrect size of CRC CIS");
    return MTLK_ERR_EEPROM;
  }

  /* copy CRC value */
  *crc_value = *eeprom_crc;

  /* The whole CIS area are involved for CRC calculation, including
     CIS Header for CRC.
     NOTE: CRC section should be the last one in CIS area.  */
  *data_len = (uint16)((void*)eeprom_crc - (void*)raw_cis);

  return MTLK_ERR_OK;
}

