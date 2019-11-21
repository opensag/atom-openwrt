/******************************************************************************

                               Copyright (c) 2012
                            Lantiq Deutschland GmbH

  For licensing information, see the file 'LICENSE' in the root folder of
  this software module.

******************************************************************************/
#include "mtlkinc.h"

#include "mtlkmib.h"
#include "mtlkaux.h"
#include "eeprom.h"
#include "channels.h"
#include "mtlk_vap_manager.h"

#define LOG_LOCAL_GID   GID_MIBS
#define LOG_LOCAL_FID   2

int __MTLK_IFUNC
mtlk_set_mib_value_uint8 (mtlk_txmm_t *txmm, uint16 u16ObjectID, uint8 value)
{
  MIB_VALUE uValue;
  memset(&uValue, 0, sizeof(MIB_VALUE));
  uValue.u8Uint8 = value;
  return mtlk_set_mib_value_raw(txmm, u16ObjectID, &uValue);
}

int __MTLK_IFUNC
mtlk_get_mib_value_uint8 (mtlk_txmm_t *txmm, uint16 u16ObjectID, uint8 *value)
{
  MIB_VALUE uValue;
  int res;
  res = mtlk_get_mib_value_raw(txmm, u16ObjectID, &uValue);
  *value = uValue.u8Uint8;
  return res;
}

int __MTLK_IFUNC
mtlk_set_mib_value_uint16 (mtlk_txmm_t *txmm, uint16 u16ObjectID, uint16 value)
{
  MIB_VALUE uValue;

  memset(&uValue, 0, sizeof(MIB_VALUE));
  uValue.u16Uint16 = HOST_TO_MAC16(value);
  return mtlk_set_mib_value_raw(txmm, u16ObjectID, &uValue);
}

int __MTLK_IFUNC
mtlk_get_mib_value_uint16 (mtlk_txmm_t *txmm, uint16 u16ObjectID, uint16 *value)
{
  MIB_VALUE uValue;
  int res;
  res = mtlk_get_mib_value_raw(txmm, u16ObjectID, &uValue);
  *value = MAC_TO_HOST16(uValue.u16Uint16);

  return res;
}

int __MTLK_IFUNC
mtlk_set_mib_value_uint32 (mtlk_txmm_t *txmm, uint16 u16ObjectID, uint32 value)
{
  MIB_VALUE uValue;
  memset(&uValue, 0, sizeof(MIB_VALUE));
  uValue.u32Uint32 = HOST_TO_MAC32(value);
  return mtlk_set_mib_value_raw(txmm, u16ObjectID, &uValue);
}

int __MTLK_IFUNC
mtlk_get_mib_value_uint32 (mtlk_txmm_t *txmm, uint16 u16ObjectID, uint32 *value)
{
  MIB_VALUE uValue;
  int res;
  res = mtlk_get_mib_value_raw(txmm, u16ObjectID, &uValue);
  *value = MAC_TO_HOST32(uValue.u32Uint32);
  return res;
}

static int access_mib_value (mtlk_txmm_t *txmm, uint16 u16ObjectID,
  uint16 direction, MIB_VALUE *uValue);

int __MTLK_IFUNC
mtlk_set_mib_value_raw (mtlk_txmm_t *txmm,
                        uint16 u16ObjectID, 
                        MIB_VALUE *uValue)
{
  return access_mib_value(txmm, u16ObjectID, UM_MAN_SET_MIB_REQ, uValue);
}

int __MTLK_IFUNC
mtlk_get_mib_value_raw (mtlk_txmm_t *txmm,
                        uint16 u16ObjectID,
                        MIB_VALUE *uValue)
{
  return access_mib_value(txmm, u16ObjectID, UM_MAN_GET_MIB_REQ, uValue);
}

static int
access_mib_value (mtlk_txmm_t *txmm,
                  uint16 u16ObjectID,
                  uint16 direction,
                  MIB_VALUE *uValue)
{
  mtlk_txmm_msg_t   man_msg;
  mtlk_txmm_data_t *man_entry = NULL;
  UMI_MIB *psSetMib;
  int res = MTLK_ERR_OK; /* Equal to UMI_OK */

  ASSERT(txmm);
  ASSERT(uValue);
  ASSERT((direction == UM_MAN_SET_MIB_REQ) ||
         (direction == UM_MAN_GET_MIB_REQ));

  man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, txmm, NULL);
  if (!man_entry) {
    ELOG_V("Can't access MIB value due to lack of MAN_MSG");
    res = MTLK_ERR_NO_RESOURCES;
    goto end;
  }
  man_entry->id = direction;
  man_entry->payload_size = sizeof(UMI_MIB);
  memset(man_entry->payload, 0, man_entry->payload_size);
  psSetMib = (UMI_MIB*)man_entry->payload;
  psSetMib->u16ObjectID = HOST_TO_MAC16(u16ObjectID);

  if (direction == UM_MAN_SET_MIB_REQ)
    psSetMib->uValue = *uValue;

  res = mtlk_txmm_msg_send_blocked(&man_msg, MTLK_MM_BLOCKED_SEND_TIMEOUT);
  if (res != MTLK_ERR_OK) goto end;

  if (direction == UM_MAN_GET_MIB_REQ)
    *uValue = psSetMib->uValue;

  res = MAC_TO_HOST16(psSetMib->u16Status);
#ifdef CPTCFG_IWLWAV_DEBUG
  {
    char *s;
    if (direction == UM_MAN_SET_MIB_REQ)
      s = "UM_MAN_SET_MIB_REQ";
    else
      s = "UM_MAN_GET_MIB_REQ";

    if (res == UMI_OK)
      ILOG2_SD("Successfull %s, u16ObjectID = 0x%04x", s, u16ObjectID);
    else
      ILOG2_SDD("%s failed, u16ObjectID = 0x%04x, status = 0x%04x",
           s, u16ObjectID, res);
  }
#endif

end:
  if (man_entry) mtlk_txmm_msg_cleanup(&man_msg);
  return res;
}

#define AUX_PREACTIVATION_SET_TIMEOUT 2000

static int 
aux_pm_related_set_to_mac_blocked (mtlk_txmm_t                  *txmm,
                                   uint8                         net_mode,
                                   uint32                        operational_rate_set,
                                   uint32                        basic_rate_set,
                                   uint8                         upper_lower,
                                   uint8                         spectrum,
                                   mtlk_aux_pm_related_params_t *result)
{
  int                    res       = MTLK_ERR_UNKNOWN;
  mtlk_txmm_msg_t        man_msg;
  mtlk_txmm_data_t      *man_entry = NULL;
  UMI_MIB               *psSetMib  = NULL;
  PRE_ACTIVATE_MIB_TYPE *data      = NULL;

  man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, txmm, NULL);
  if (!man_entry) {
    ELOG_V("No MM slot: failed to set pre-activation MIB");
    res = MTLK_ERR_NO_RESOURCES;
    goto FINISH;
  }
  
  man_entry->id           = UM_MAN_SET_MIB_REQ;
  man_entry->payload_size = sizeof(*psSetMib);

  psSetMib = (UMI_MIB*)man_entry->payload;

  memset(psSetMib, 0, sizeof(*psSetMib));

  psSetMib->u16ObjectID = HOST_TO_MAC16(MIB_PRE_ACTIVATE);

  if (!result) {
    result = &psSetMib->uValue.sPreActivateType;
  } else {
    memset(result, 0, sizeof(*result));
  }

  data = &psSetMib->uValue.sPreActivateType;

  result->u32OperationalRateSet = operational_rate_set;
  result->u32BSSbasicRateSet    = basic_rate_set;
  result->u8NetworkMode         = net_mode;
  result->u8UpperLowerChannel   = upper_lower;
  result->u8SpectrumMode        = spectrum;

  /* force the STA to be legacy regardless of the AP type */
  if (!is_ht_net_mode(net_mode)) {
    result->u32OperationalRateSet &= LM_PHY_11A_RATE_MSK | LM_PHY_11B_RATE_MSK;
    result->u8UpperLowerChannel    = 0;
    result->u8SpectrumMode         = CW_20;
  }

  data->u8NetworkMode                    = result->u8NetworkMode;
  data->u8UpperLowerChannel              = result->u8UpperLowerChannel;
  data->u8SpectrumMode                   = result->u8SpectrumMode;
  data->u32OperationalRateSet            = HOST_TO_MAC32(result->u32OperationalRateSet);
  data->u32BSSbasicRateSet               = HOST_TO_MAC32(result->u32BSSbasicRateSet);

  ILOG2_DDDDD("SENDING PREACTIVATION MIB:\n"
       "result->u8NetworkMode                       = %d\n"
       "result->u8UpperLowerChannel                 = %d\n"
       "result->u8SpectrumMode                      = %d\n"
       "result->u32OperationalRateSet               = %X\n"
       "result->u32BSSbasicRateSet                  = %X",
      result->u8NetworkMode,
      result->u8UpperLowerChannel,
      result->u8SpectrumMode,
      result->u32OperationalRateSet,
      result->u32BSSbasicRateSet);

  MTLK_ASSERT(data->u8NetworkMode < NUM_OF_NETWORK_MODES);

  res = mtlk_txmm_msg_send_blocked(&man_msg, AUX_PREACTIVATION_SET_TIMEOUT);
  if (res != MTLK_ERR_OK) {
    ELOG_D("Failed to set Pre-activation MIB (Err#%d)", res);
    goto FINISH;
  } else {  
    res = MTLK_ERR_UNKNOWN;
  }
          
  if (psSetMib->u16Status != HOST_TO_MAC16(UMI_OK)) {
    ELOG_V("Setting Pre-activation MIB status Failed");
    goto FINISH;
  }

  res = MTLK_ERR_OK;

FINISH:
  if (man_entry) {
    mtlk_txmm_msg_cleanup(&man_msg);
  }

  return res;
}

int __MTLK_IFUNC 
mtlk_aux_pm_related_params_set_defaults (mtlk_txmm_t                  *txmm,
                                         uint8                        net_mode,
                                         uint8                        spectrum,
                                         mtlk_aux_pm_related_params_t *result)
{
  return aux_pm_related_set_to_mac_blocked(txmm,
           net_mode,
           get_operate_rate_set(net_mode),
           get_basic_rate_set(net_mode, CFG_BASIC_RATE_SET_DEFAULT),
           ALTERNATE_UPPER,
           spectrum,
           result);
}

int __MTLK_IFUNC 
mtlk_aux_pm_related_params_set_bss_based(mtlk_txmm_t                  *txmm, 
                                         bss_data_t                   *bss_data, 
                                         uint8                         net_mode,
                                         uint8                         spectrum,
                                         mtlk_aux_pm_related_params_t *result)
{
  MTLK_ASSERT(NULL != bss_data);
  MTLK_ASSERT(!mtlk_osal_is_zero_address(bss_data->bssid.au8Addr));

  return aux_pm_related_set_to_mac_blocked(txmm,
                                           net_mode,
                                           bss_data->operational_rate_set,
                                           bss_data->basic_rate_set,
                                           bss_data->upper_lower,
                                           spectrum,
                                           result);
}

int __MTLK_IFUNC 
mtlk_set_mib_pre_activate (mtlk_txmm_t *txmm, mtlk_aux_pm_related_params_t *params)
{
  int                    res       = MTLK_ERR_UNKNOWN;
  mtlk_txmm_msg_t        man_msg;
  mtlk_txmm_data_t      *man_entry = NULL;
  UMI_MIB               *psSetMib  = NULL;
  PRE_ACTIVATE_MIB_TYPE *data      = NULL;

  man_entry = mtlk_txmm_msg_init_with_empty_data(&man_msg, txmm, NULL);
  if (!man_entry) {
    ELOG_V("No MM slot: failed to set pre-activation MIB");
    res = MTLK_ERR_NO_RESOURCES;
    goto FINISH;
  }
  
  man_entry->id           = UM_MAN_SET_MIB_REQ;
  man_entry->payload_size = sizeof(*psSetMib);

  psSetMib = (UMI_MIB*)man_entry->payload;

  memset(psSetMib, 0, sizeof(*psSetMib));

  psSetMib->u16ObjectID = HOST_TO_MAC16(MIB_PRE_ACTIVATE);

  data = &psSetMib->uValue.sPreActivateType;

  data->u8NetworkMode                    = params->u8NetworkMode;
  data->u8UpperLowerChannel              = params->u8UpperLowerChannel;
  data->u8SpectrumMode                   = params->u8SpectrumMode;
  data->u32OperationalRateSet            = HOST_TO_MAC32(params->u32OperationalRateSet);
  data->u32BSSbasicRateSet               = HOST_TO_MAC32(params->u32BSSbasicRateSet);

  ILOG2_DDDDD("SENDING PREACTIVATION MIB:\n"
       "u8NetworkMode                       = %d\n"
       "u8UpperLowerChannel                 = %d\n"
       "u8SpectrumMode                      = %d\n"
       "u32OperationalRateSet               = %X\n"
       "u32BSSbasicRateSet                  = %X",
      data->u8NetworkMode,
      data->u8UpperLowerChannel,
      data->u8SpectrumMode,
      data->u32OperationalRateSet,
      data->u32BSSbasicRateSet);

  MTLK_ASSERT(data->u8NetworkMode < NUM_OF_NETWORK_MODES);

  res = mtlk_txmm_msg_send_blocked(&man_msg, AUX_PREACTIVATION_SET_TIMEOUT);
  if (res != MTLK_ERR_OK) {
    ELOG_D("Failed to set Pre-activation MIB (Err#%d)", res);
    goto FINISH;
  } else {  
    res = MTLK_ERR_UNKNOWN;
  }
          
  if (psSetMib->u16Status != HOST_TO_MAC16(UMI_OK)) {
    ELOG_V("Setting Pre-activation MIB status Failed");
    goto FINISH;
  }

  res = MTLK_ERR_OK;

FINISH:
  if (man_entry) {
    mtlk_txmm_msg_cleanup(&man_msg);
  }

  return res;
}
