/******************************************************************************

                               Copyright (c) 2012
                            Lantiq Deutschland GmbH

  For licensing information, see the file 'LICENSE' in the root folder of
  this software module.

******************************************************************************/
/*
 * $Id$
 *
 * 
 *
 * Written by: Roman Sikorskyy 
 *
 */

#ifndef __CHANNELS_H__
#define __CHANNELS_H__

#include "mhi_umi.h"
#include "mhi_mib_id.h"
#include "eeprom.h"
#include "mtlk_clipboard.h"

#define MTLK_IDEFS_ON
#include "mtlkidefs.h"

#define ALTERNATE_LOWER     1
#define ALTERNATE_UPPER     0
#define ALTERNATE_NONE      (-1)

typedef struct _tx_limit_t
{
  /* this is needed for HW limits - to load initial data */
  uint16 vendor_id;
  uint16 device_id;
  uint8 hw_type;
  uint8 hw_revision;
  uint8 num_tx_antennas;
  uint8 num_rx_antennas;
} __MTLK_IDATA tx_limit_t;

typedef struct _drv_params_t
{
    uint8 band;
    uint8 bandwidth;
    uint8 upper_lower;
    uint8 reg_domain;
    uint8 spectrum_mode;
} __MTLK_IDATA drv_params_t;

typedef struct {
  uint16             freq;
  uint8              tx_lim;
  uint8              spectrum;
  uint8              phy_mode;
  uint8              reg_domain;
} __MTLK_IDATA mtlk_hw_limits_stat_entry_t;


#define WAVE_BANDWIDTHS_NUM     MAXIMUM_BANDWIDTHS_GEN6

typedef struct {
    uint8  lower_chan[WAVE_BANDWIDTHS_NUM];
    uint8  center_chan[WAVE_BANDWIDTHS_NUM];
    uint8  upper_chan[WAVE_BANDWIDTHS_NUM];
} __MTLK_IDATA wave_chan_bonding_t;


static __INLINE uint16
mtlk_channels_get_secondary_channel_no_by_offset(uint16 primary_channel_no, uint8 secondary_channel_offset)
{
#define CB_CHANNEL_OFFSET 4

  uint16 res = 0;

  switch (secondary_channel_offset)
  {
  case UMI_CHANNEL_SW_MODE_SCA:
    res = primary_channel_no + CB_CHANNEL_OFFSET;
    break;
  case UMI_CHANNEL_SW_MODE_SCB:
    res = primary_channel_no - CB_CHANNEL_OFFSET;
    break;
  case UMI_CHANNEL_SW_MODE_SCN:
  default:
    break;
  }

  return res;

#undef CB_CHANNEL_OFFSET
}

#define WAVE_FIRST_5G_CHAN      (36)
#define WAVE_FIRST_5G_FREQ      (5000u + (5 * WAVE_FIRST_5G_CHAN))
#define WAVE_FREQ_IS_5G(freq)   (WAVE_FIRST_5G_FREQ <= (freq))

static __INLINE uint8
channel_to_band (uint16 channel)
{
  return (channel < WAVE_FIRST_5G_CHAN)? MTLK_HW_BAND_2_4_GHZ : MTLK_HW_BAND_5_2_GHZ;
}

static __INLINE uint8
frequency_to_band (uint16 frequency)
{
  return (frequency < WAVE_FIRST_5G_FREQ) ? MTLK_HW_BAND_2_4_GHZ : MTLK_HW_BAND_5_2_GHZ;
}

static __INLINE uint16
channel_to_frequency (uint16 channel)
{
#define CHANNEL_THRESHOLD 180
  uint16 res;
  /* IEEE Std 802.11-2012: 20.3.15 Channel numbering and channelization
    Channel center frequency = starting frequency + 5 * ch
  */
  if (channel_to_band(channel) == MTLK_HW_BAND_2_4_GHZ)
  {
    res = 2407 + 5*channel;
    if(channel == 14) /* IEEE Std 802.11-2012: 17.4.6.3 Channel Numbering of operating channels */
      res += 7;
  }
  else if (channel < CHANNEL_THRESHOLD)
    res = 5000 + 5*channel;
  else
    res = 4000 + 5*channel;
#undef CHANNEL_THRESHOLD
  return res;
}

#define MTLK_IDEFS_OFF
#include "mtlkidefs.h"

#endif /* __CHANNELS_H__ */
