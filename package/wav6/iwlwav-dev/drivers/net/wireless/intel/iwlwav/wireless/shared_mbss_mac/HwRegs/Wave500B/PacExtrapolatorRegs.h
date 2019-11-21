
/***********************************************************************************
File:				PacExtrapolatorRegs.h
Module:				pacExtrapolator
SOC Revision:		843
Purpose:
Description:		This File was auto generated using SOC Online

************************************************************************************/
#ifndef _PAC_EXTRAPOLATOR_REGS_H_
#define _PAC_EXTRAPOLATOR_REGS_H_

/*---------------------------------------------------------------------------------
/						Registers Addresses													 
/----------------------------------------------------------------------------------*/
#include "HwMemoryMap.h"

#define PAC_EXTRAPOLATOR_BASE_ADDRESS                             MEMORY_MAP_UNIT_62_BASE_ADDRESS
#define	REG_PAC_EXTRAPOLATOR_EXTRAP_CONTROL_CFG                (PAC_EXTRAPOLATOR_BASE_ADDRESS + 0x0)
#define	REG_PAC_EXTRAPOLATOR_STA_EN_0_31                       (PAC_EXTRAPOLATOR_BASE_ADDRESS + 0x4)
#define	REG_PAC_EXTRAPOLATOR_STA_EN_32_63                      (PAC_EXTRAPOLATOR_BASE_ADDRESS + 0x8)
#define	REG_PAC_EXTRAPOLATOR_STA_EN_64_95                      (PAC_EXTRAPOLATOR_BASE_ADDRESS + 0xC)
#define	REG_PAC_EXTRAPOLATOR_STA_EN_96_127                     (PAC_EXTRAPOLATOR_BASE_ADDRESS + 0x10)
#define	REG_PAC_EXTRAPOLATOR_EXTRAP_WORD_0_7_CFG               (PAC_EXTRAPOLATOR_BASE_ADDRESS + 0x14)
#define	REG_PAC_EXTRAPOLATOR_EXTRAP_WORD_8_CFG                 (PAC_EXTRAPOLATOR_BASE_ADDRESS + 0x18)
#define	REG_PAC_EXTRAPOLATOR_OFFSET_PHY_WORD_0_7               (PAC_EXTRAPOLATOR_BASE_ADDRESS + 0x1C)
#define	REG_PAC_EXTRAPOLATOR_OFFSET_AVG_DELTA_T                (PAC_EXTRAPOLATOR_BASE_ADDRESS + 0x20)
#define	REG_PAC_EXTRAPOLATOR_OFFSET_TSF                        (PAC_EXTRAPOLATOR_BASE_ADDRESS + 0x24)
#define	REG_PAC_EXTRAPOLATOR_SIGN_EXT_WORD_0_7                 (PAC_EXTRAPOLATOR_BASE_ADDRESS + 0x28)
#define	REG_PAC_EXTRAPOLATOR_STA_ENTRY_SIZE                    (PAC_EXTRAPOLATOR_BASE_ADDRESS + 0x2C)
#define	REG_PAC_EXTRAPOLATOR_PHY_WORDS_WLAN_DB_BASE_ADDR       (PAC_EXTRAPOLATOR_BASE_ADDRESS + 0x30)
#define	REG_PAC_EXTRAPOLATOR_ALFA_FACTORS_WLAN_DB_BASE_ADDR    (PAC_EXTRAPOLATOR_BASE_ADDRESS + 0x34)
#define	REG_PAC_EXTRAPOLATOR_PHY_WORDS_STA_DB_BASE_ADDR        (PAC_EXTRAPOLATOR_BASE_ADDRESS + 0x38)
#define	REG_PAC_EXTRAPOLATOR_ALFA_FACTORS_STA_DB_BASE_ADDR     (PAC_EXTRAPOLATOR_BASE_ADDRESS + 0x3C)
#define	REG_PAC_EXTRAPOLATOR_LN_BETA_LUT_BASE_ADDR             (PAC_EXTRAPOLATOR_BASE_ADDRESS + 0x40)
#define	REG_PAC_EXTRAPOLATOR_TSF_RESOLUTION_CFG                (PAC_EXTRAPOLATOR_BASE_ADDRESS + 0x44)
#define	REG_PAC_EXTRAPOLATOR_SW_REQ                            (PAC_EXTRAPOLATOR_BASE_ADDRESS + 0x48)
#define	REG_PAC_EXTRAPOLATOR_SW_CURRENT_ALFA                   (PAC_EXTRAPOLATOR_BASE_ADDRESS + 0x4C)
#define	REG_PAC_EXTRAPOLATOR_SW_CURRENT_PARAMETER              (PAC_EXTRAPOLATOR_BASE_ADDRESS + 0x50)
#define	REG_PAC_EXTRAPOLATOR_SW_PREVIOUS_AVG_WEIGHTED          (PAC_EXTRAPOLATOR_BASE_ADDRESS + 0x54)
#define	REG_PAC_EXTRAPOLATOR_SW_CURRENT_TSF                    (PAC_EXTRAPOLATOR_BASE_ADDRESS + 0x58)
#define	REG_PAC_EXTRAPOLATOR_SW_PREVIOUS_TSF                   (PAC_EXTRAPOLATOR_BASE_ADDRESS + 0x5C)
#define	REG_PAC_EXTRAPOLATOR_SW_ABS_LN_BETA                    (PAC_EXTRAPOLATOR_BASE_ADDRESS + 0x60)
#define	REG_PAC_EXTRAPOLATOR_SW_NUMBER_OF_VALID_FIELDS         (PAC_EXTRAPOLATOR_BASE_ADDRESS + 0x64)
#define	REG_PAC_EXTRAPOLATOR_SW_HALF_WORD_EN                   (PAC_EXTRAPOLATOR_BASE_ADDRESS + 0x68)
#define	REG_PAC_EXTRAPOLATOR_SW_AVG_DELTA_T                    (PAC_EXTRAPOLATOR_BASE_ADDRESS + 0x6C)
#define	REG_PAC_EXTRAPOLATOR_SW_SIGN_EXT                       (PAC_EXTRAPOLATOR_BASE_ADDRESS + 0x70)
#define	REG_PAC_EXTRAPOLATOR_SW_TSF_RES                        (PAC_EXTRAPOLATOR_BASE_ADDRESS + 0x74)
#define	REG_PAC_EXTRAPOLATOR_SW_NEW_ALFA                       (PAC_EXTRAPOLATOR_BASE_ADDRESS + 0x78)
#define	REG_PAC_EXTRAPOLATOR_SW_NEW_AVG_WEIGHTED               (PAC_EXTRAPOLATOR_BASE_ADDRESS + 0x7C)
#define	REG_PAC_EXTRAPOLATOR_LOGGER_PRIORITY                   (PAC_EXTRAPOLATOR_BASE_ADDRESS + 0x80)
#define	REG_PAC_EXTRAPOLATOR_LOGGER_EN                         (PAC_EXTRAPOLATOR_BASE_ADDRESS + 0x84)
#define	REG_PAC_EXTRAPOLATOR_LOGGER_ACTIVE                     (PAC_EXTRAPOLATOR_BASE_ADDRESS + 0x88)
#define	REG_PAC_EXTRAPOLATOR_DEBUG_CTL                         (PAC_EXTRAPOLATOR_BASE_ADDRESS + 0x8C)
#define	REG_PAC_EXTRAPOLATOR_DEBUG_EXTRAP_SM                   (PAC_EXTRAPOLATOR_BASE_ADDRESS + 0x90)
#define	REG_PAC_EXTRAPOLATOR_DEBUG_CURRENT_TSF                 (PAC_EXTRAPOLATOR_BASE_ADDRESS + 0x94)
#define	REG_PAC_EXTRAPOLATOR_DEBUG_DATA_IN                     (PAC_EXTRAPOLATOR_BASE_ADDRESS + 0x98)
#define	REG_PAC_EXTRAPOLATOR_DEBUG_NEW_ALFA                    (PAC_EXTRAPOLATOR_BASE_ADDRESS + 0x9C)
#define	REG_PAC_EXTRAPOLATOR_DEBUG_DATA_NEW                    (PAC_EXTRAPOLATOR_BASE_ADDRESS + 0xA0)
#define	REG_PAC_EXTRAPOLATOR_DEBUG_LAST_AMC_ADDR               (PAC_EXTRAPOLATOR_BASE_ADDRESS + 0xA4)
/*---------------------------------------------------------------------------------
/						Data Type Definition										
/----------------------------------------------------------------------------------*/
/*REG_PAC_EXTRAPOLATOR_EXTRAP_CONTROL_CFG 0x0 */
typedef union
{
	uint32 val;
	struct
	{
		uint32 extrapEn:1;	// Block enable bit, Default is Disabled
		uint32 extrapBusy:1;	// Block busy bit, Default is Free
		uint32 reserved0:6;
		uint32 extrapStaIdx:7;	// Working STA index
		uint32 reserved1:17;
	} bitFields;
} RegPacExtrapolatorExtrapControlCfg_u;

/*REG_PAC_EXTRAPOLATOR_STA_EN_0_31 0x4 */
typedef union
{
	uint32 val;
	struct
	{
		uint32 staEn031:32;	// STA enable bits 0 to 31. Default is Disabled
	} bitFields;
} RegPacExtrapolatorStaEn031_u;

/*REG_PAC_EXTRAPOLATOR_STA_EN_32_63 0x8 */
typedef union
{
	uint32 val;
	struct
	{
		uint32 staEn3263:32;	// STA enable bits 32 to 63. Default is Disabled
	} bitFields;
} RegPacExtrapolatorStaEn3263_u;

/*REG_PAC_EXTRAPOLATOR_STA_EN_64_95 0xC */
typedef union
{
	uint32 val;
	struct
	{
		uint32 staEn6495:32;	// STA enable bits 64 to 95. Default is Disabled
	} bitFields;
} RegPacExtrapolatorStaEn6495_u;

/*REG_PAC_EXTRAPOLATOR_STA_EN_96_127 0x10 */
typedef union
{
	uint32 val;
	struct
	{
		uint32 staEn96127:32;	// STA enable bits 96 to 127. Default is Disabled
	} bitFields;
} RegPacExtrapolatorStaEn96127_u;

/*REG_PAC_EXTRAPOLATOR_EXTRAP_WORD_0_7_CFG 0x14 */
typedef union
{
	uint32 val;
	struct
	{
		uint32 word0UpdateEn:1;	// Word0 enable bit.
		uint32 word0Db:1;	// Word0 extrapolation DB: , 0x0: Extrapolation per STA , 0x1: Extrapolation for WLAN
		uint32 word0Method:2;	// Word0 extrapolation method: , 0x0: Basic alfa filter mode , 0x1: Alfa beta Filter mode , 0x2: Copy mode
		uint32 word1UpdateEn:1;	// Word1 enable bit.
		uint32 word1Db:1;	// Word1 extrapolation DB: , 0x0: Extrapolation per STA , 0x1: Extrapolation for WLAN
		uint32 word1Method:2;	// Word1 extrapolation method: , 0x0: Basic alfa filter mode , 0x1: Alfa beta Filter mode , 0x2: Copy mode
		uint32 word2UpdateEn:1;	// Word2 enable bit.
		uint32 word2Db:1;	// Word2 extrapolation DB: , 0x0: Extrapolation per STA , 0x1: Extrapolation for WLAN
		uint32 word2Method:2;	// Word2 extrapolation method: , 0x0: Basic alfa filter mode , 0x1: Alfa beta Filter mode , 0x2: Copy mode
		uint32 word3UpdateEn:1;	// Word3 enable bit.
		uint32 word3Db:1;	// Word3 extrapolation DB: , 0x0: Extrapolation per STA , 0x1: Extrapolation for WLAN
		uint32 word3Method:2;	// Word3 extrapolation method: , 0x0: Basic alfa filter mode , 0x1: Alfa beta Filter mode , 0x2: Copy mode
		uint32 word4UpdateEn:1;	// Word4 enable bit.
		uint32 word4Db:1;	// Word4 extrapolation DB: , 0x0: Extrapolation per STA , 0x1: Extrapolation for WLAN
		uint32 word4Method:2;	// Word4 extrapolation method: , 0x0: Basic alfa filter mode , 0x1: Alfa beta Filter mode , 0x2: Copy mode
		uint32 word5UpdateEn:1;	// Word5 enable bit.
		uint32 word5Db:1;	// Word5 extrapolation DB: , 0x0: Extrapolation per STA , 0x1: Extrapolation for WLAN
		uint32 word5Method:2;	// Word5 extrapolation method: , 0x0: Basic alfa filter mode , 0x1: Alfa beta Filter mode , 0x2: Copy mode
		uint32 word6UpdateEn:1;	// Word6 enable bit.
		uint32 word6Db:1;	// Word6 extrapolation DB: , 0x0: Extrapolation per STA , 0x1: Extrapolation for WLAN
		uint32 word6Method:2;	// Word6 extrapolation method: , 0x0: Basic alfa filter mode , 0x1: Alfa beta Filter mode , 0x2: Copy mode
		uint32 word7UpdateEn:1;	// Word7 enable bit.
		uint32 word7Db:1;	// Word7 extrapolation DB: , 0x0: Extrapolation per STA , 0x1: Extrapolation for WLAN
		uint32 word7Method:2;	// Word7 extrapolation method: , 0x0: Basic alfa filter mode , 0x1: Alfa beta Filter mode , 0x2: Copy mode
	} bitFields;
} RegPacExtrapolatorExtrapWord07Cfg_u;

/*REG_PAC_EXTRAPOLATOR_EXTRAP_WORD_8_CFG 0x18 */
typedef union
{
	uint32 val;
	struct
	{
		uint32 avgDeltaTWlanUpdateEn:1;	// Avg delta T wlan enable bit.
		uint32 avgDeltaTStaUpdateEn:1;	// Avg delta T sta enable bit.
		uint32 reserved0:30;
	} bitFields;
} RegPacExtrapolatorExtrapWord8Cfg_u;

/*REG_PAC_EXTRAPOLATOR_OFFSET_PHY_WORD_0_7 0x1C */
typedef union
{
	uint32 val;
	struct
	{
		uint32 offsetWord0:4;	// Word0 offset indicates word index at entry
		uint32 offsetWord1:4;	// Word1 offset indicates word index at entry
		uint32 offsetWord2:4;	// Word2 offset indicates word index at entry
		uint32 offsetWord3:4;	// Word3 offset indicates word index at entry
		uint32 offsetWord4:4;	// Word4 offset indicates word index at entry
		uint32 offsetWord5:4;	// Word5 offset indicates word index at entry
		uint32 offsetWord6:4;	// Word6 offset indicates word index at entry
		uint32 offsetWord7:4;	// Word7 offset indicates word index at entry
	} bitFields;
} RegPacExtrapolatorOffsetPhyWord07_u;

/*REG_PAC_EXTRAPOLATOR_OFFSET_AVG_DELTA_T 0x20 */
typedef union
{
	uint32 val;
	struct
	{
		uint32 offsetAvgDeltaTWlan:4;	// Avg delta T wlan offset indicates word index at entry
		uint32 offsetAvgDeltaTSta:4;	// Avg delta T sta offset indicates word index at entry
		uint32 reserved0:24;
	} bitFields;
} RegPacExtrapolatorOffsetAvgDeltaT_u;

/*REG_PAC_EXTRAPOLATOR_OFFSET_TSF 0x24 */
typedef union
{
	uint32 val;
	struct
	{
		uint32 offsetTsfWlan:4;	// TSF wlan offset indicates word index at entry
		uint32 offsetTsfSta:4;	// TSF sta offset indicates word index at entry
		uint32 reserved0:24;
	} bitFields;
} RegPacExtrapolatorOffsetTsf_u;

/*REG_PAC_EXTRAPOLATOR_SIGN_EXT_WORD_0_7 0x28 */
typedef union
{
	uint32 val;
	struct
	{
		uint32 signExtWord0:4;	// Word0 sign ext indication per word field
		uint32 signExtWord1:4;	// Word1 sign ext indication per word field
		uint32 signExtWord2:4;	// Word2 sign ext indication per word field
		uint32 signExtWord3:4;	// Word3 sign ext indication per word field
		uint32 signExtWord4:4;	// Word4 sign ext indication per word field
		uint32 signExtWord5:4;	// Word5 sign ext indication per word field
		uint32 signExtWord6:4;	// Word6 sign ext indication per word field
		uint32 signExtWord7:4;	// Word7 sign ext indication per word field
	} bitFields;
} RegPacExtrapolatorSignExtWord07_u;

/*REG_PAC_EXTRAPOLATOR_STA_ENTRY_SIZE 0x2C */
typedef union
{
	uint32 val;
	struct
	{
		uint32 staEntrySize:4;	// STA entry size in DB
		uint32 reserved0:28;
	} bitFields;
} RegPacExtrapolatorStaEntrySize_u;

/*REG_PAC_EXTRAPOLATOR_PHY_WORDS_WLAN_DB_BASE_ADDR 0x30 */
typedef union
{
	uint32 val;
	struct
	{
		uint32 wordsWlanDbBaseAddr:24;	// PHY words wlan DB base address at SHRAM, word aligned 
		uint32 reserved0:8;
	} bitFields;
} RegPacExtrapolatorPhyWordsWlanDbBaseAddr_u;

/*REG_PAC_EXTRAPOLATOR_ALFA_FACTORS_WLAN_DB_BASE_ADDR 0x34 */
typedef union
{
	uint32 val;
	struct
	{
		uint32 alfaFactorsWlanDbBaseAddr:24;	// Alfa factors wlan DB base address at SHRAM, word aligned 
		uint32 reserved0:8;
	} bitFields;
} RegPacExtrapolatorAlfaFactorsWlanDbBaseAddr_u;

/*REG_PAC_EXTRAPOLATOR_PHY_WORDS_STA_DB_BASE_ADDR 0x38 */
typedef union
{
	uint32 val;
	struct
	{
		uint32 wordsStaDbBaseAddr:24;	// PHY words sta DB base address at SHRAM, word aligned 
		uint32 reserved0:8;
	} bitFields;
} RegPacExtrapolatorPhyWordsStaDbBaseAddr_u;

/*REG_PAC_EXTRAPOLATOR_ALFA_FACTORS_STA_DB_BASE_ADDR 0x3C */
typedef union
{
	uint32 val;
	struct
	{
		uint32 alfaFactorsStaDbBaseAddr:24;	// Alfa factors sta DB base address at SHRAM, word aligned 
		uint32 reserved0:8;
	} bitFields;
} RegPacExtrapolatorAlfaFactorsStaDbBaseAddr_u;

/*REG_PAC_EXTRAPOLATOR_LN_BETA_LUT_BASE_ADDR 0x40 */
typedef union
{
	uint32 val;
	struct
	{
		uint32 lnBetaLutBaseAddr:24;	// Ln Beta LUT base address at SHRAM, word aligned
		uint32 reserved0:8;
	} bitFields;
} RegPacExtrapolatorLnBetaLutBaseAddr_u;

/*REG_PAC_EXTRAPOLATOR_TSF_RESOLUTION_CFG 0x44 */
typedef union
{
	uint32 val;
	struct
	{
		uint32 tsfResolution:4;	// TSF resolution to determine time units
		uint32 reserved0:28;
	} bitFields;
} RegPacExtrapolatorTsfResolutionCfg_u;

/*REG_PAC_EXTRAPOLATOR_SW_REQ 0x48 */
typedef union
{
	uint32 val;
	struct
	{
		uint32 swReqSet:1;	// Set SW request
		uint32 swReqIrqClear:1;	// Clear interrupt indication
		uint32 reserved0:6;
		uint32 swReqAlfaMode:1;	// Set SW request alfa mode: , 0x0: Basic alfa mode , 0x1: Alfa Beta mode
		uint32 reserved1:23;
	} bitFields;
} RegPacExtrapolatorSwReq_u;

/*REG_PAC_EXTRAPOLATOR_SW_CURRENT_ALFA 0x4C */
typedef union
{
	uint32 val;
	struct
	{
		uint32 swCurrentAlfa:21;	// Set SW current alfa
		uint32 reserved0:11;
	} bitFields;
} RegPacExtrapolatorSwCurrentAlfa_u;

/*REG_PAC_EXTRAPOLATOR_SW_CURRENT_PARAMETER 0x50 */
typedef union
{
	uint32 val;
	struct
	{
		uint32 swCurrentParameter:32;	// Set SW current parameter
	} bitFields;
} RegPacExtrapolatorSwCurrentParameter_u;

/*REG_PAC_EXTRAPOLATOR_SW_PREVIOUS_AVG_WEIGHTED 0x54 */
typedef union
{
	uint32 val;
	struct
	{
		uint32 swPreviousAvgWeighted:32;	// Set SW previous avg weighted
	} bitFields;
} RegPacExtrapolatorSwPreviousAvgWeighted_u;

/*REG_PAC_EXTRAPOLATOR_SW_CURRENT_TSF 0x58 */
typedef union
{
	uint32 val;
	struct
	{
		uint32 swCurrentTsf:32;	// Set SW current TSF
	} bitFields;
} RegPacExtrapolatorSwCurrentTsf_u;

/*REG_PAC_EXTRAPOLATOR_SW_PREVIOUS_TSF 0x5C */
typedef union
{
	uint32 val;
	struct
	{
		uint32 swPreviousTsf:32;	// Set SW previous TSF
	} bitFields;
} RegPacExtrapolatorSwPreviousTsf_u;

/*REG_PAC_EXTRAPOLATOR_SW_ABS_LN_BETA 0x60 */
typedef union
{
	uint32 val;
	struct
	{
		uint32 swAbsLnBeta:12;	// Set SW abs ln beta
		uint32 reserved0:20;
	} bitFields;
} RegPacExtrapolatorSwAbsLnBeta_u;

/*REG_PAC_EXTRAPOLATOR_SW_NUMBER_OF_VALID_FIELDS 0x64 */
typedef union
{
	uint32 val;
	struct
	{
		uint32 swNumberOfValidFields:2;	// Set SW number of valid fields: , 0x0: one field (1 Byte) , 0x1: two fields (2 Byte) , 0x2: three fields (3 Byte) , 0x3: four fields (4 Byte)
		uint32 reserved0:30;
	} bitFields;
} RegPacExtrapolatorSwNumberOfValidFields_u;

/*REG_PAC_EXTRAPOLATOR_SW_HALF_WORD_EN 0x68 */
typedef union
{
	uint32 val;
	struct
	{
		uint32 swHalfWordEn:1;	// Set SW half word enable, if enable number of fields ignored
		uint32 reserved0:31;
	} bitFields;
} RegPacExtrapolatorSwHalfWordEn_u;

/*REG_PAC_EXTRAPOLATOR_SW_AVG_DELTA_T 0x6C */
typedef union
{
	uint32 val;
	struct
	{
		uint32 swAvgDeltaT:15;	// Set SW average delta T
		uint32 reserved0:17;
	} bitFields;
} RegPacExtrapolatorSwAvgDeltaT_u;

/*REG_PAC_EXTRAPOLATOR_SW_SIGN_EXT 0x70 */
typedef union
{
	uint32 val;
	struct
	{
		uint32 swSignExt:4;	// SW sign extension indication per word field
		uint32 reserved0:28;
	} bitFields;
} RegPacExtrapolatorSwSignExt_u;

/*REG_PAC_EXTRAPOLATOR_SW_TSF_RES 0x74 */
typedef union
{
	uint32 val;
	struct
	{
		uint32 swTsfRes:4;	// Set SW tsf resolution
		uint32 reserved0:28;
	} bitFields;
} RegPacExtrapolatorSwTsfRes_u;

/*REG_PAC_EXTRAPOLATOR_SW_NEW_ALFA 0x78 */
typedef union
{
	uint32 val;
	struct
	{
		uint32 swNewAlfa:21;	// SW calculated new alfa parameter
		uint32 reserved0:11;
	} bitFields;
} RegPacExtrapolatorSwNewAlfa_u;

/*REG_PAC_EXTRAPOLATOR_SW_NEW_AVG_WEIGHTED 0x7C */
typedef union
{
	uint32 val;
	struct
	{
		uint32 swNewAvgWeighted:32;	// SW new avg weighted
	} bitFields;
} RegPacExtrapolatorSwNewAvgWeighted_u;

/*REG_PAC_EXTRAPOLATOR_LOGGER_PRIORITY 0x80 */
typedef union
{
	uint32 val;
	struct
	{
		uint32 loggerPriority:2;	// Logger priority
		uint32 reserved0:30;
	} bitFields;
} RegPacExtrapolatorLoggerPriority_u;

/*REG_PAC_EXTRAPOLATOR_LOGGER_EN 0x84 */
typedef union
{
	uint32 val;
	struct
	{
		uint32 hwLoggerEn:1;	// Logger HW enable
		uint32 swLoggerEn:1;	// Logger SW enable
		uint32 reserved0:30;
	} bitFields;
} RegPacExtrapolatorLoggerEn_u;

/*REG_PAC_EXTRAPOLATOR_LOGGER_ACTIVE 0x88 */
typedef union
{
	uint32 val;
	struct
	{
		uint32 loggerActive:1;	// Logger active
		uint32 reserved0:31;
	} bitFields;
} RegPacExtrapolatorLoggerActive_u;

/*REG_PAC_EXTRAPOLATOR_DEBUG_CTL 0x8C */
typedef union
{
	uint32 val;
	struct
	{
		uint32 debugExtrapHwActive:1;
		uint32 debugExtrapSwActive:1;
		uint32 debugWordIdx:4;
		uint32 debugFieldIdx:2;
		uint32 reserved0:24;
	} bitFields;
} RegPacExtrapolatorDebugCtl_u;

/*REG_PAC_EXTRAPOLATOR_DEBUG_EXTRAP_SM 0x90 */
typedef union
{
	uint32 val;
	struct
	{
		uint32 debugExtrapMainSm:2;
		uint32 reserved0:6;
		uint32 debugExtrapHwSm:5;
		uint32 reserved1:3;
		uint32 debugExtrapSwSm:5;
		uint32 reserved2:11;
	} bitFields;
} RegPacExtrapolatorDebugExtrapSm_u;

/*REG_PAC_EXTRAPOLATOR_DEBUG_CURRENT_TSF 0x94 */
typedef union
{
	uint32 val;
	struct
	{
		uint32 debugCurrentTsf:32;
	} bitFields;
} RegPacExtrapolatorDebugCurrentTsf_u;

/*REG_PAC_EXTRAPOLATOR_DEBUG_DATA_IN 0x98 */
typedef union
{
	uint32 val;
	struct
	{
		uint32 debugDataInField0:8;
		uint32 debugDataInField1:8;
		uint32 debugDataInField2:8;
		uint32 debugDataInField3:8;
	} bitFields;
} RegPacExtrapolatorDebugDataIn_u;

/*REG_PAC_EXTRAPOLATOR_DEBUG_NEW_ALFA 0x9C */
typedef union
{
	uint32 val;
	struct
	{
		uint32 debugNewAlfa:21;
		uint32 reserved0:11;
	} bitFields;
} RegPacExtrapolatorDebugNewAlfa_u;

/*REG_PAC_EXTRAPOLATOR_DEBUG_DATA_NEW 0xA0 */
typedef union
{
	uint32 val;
	struct
	{
		uint32 debugDataNewField0:8;
		uint32 debugDataNewField1:8;
		uint32 debugDataNewField2:8;
		uint32 debugDataNewField3:8;
	} bitFields;
} RegPacExtrapolatorDebugDataNew_u;

/*REG_PAC_EXTRAPOLATOR_DEBUG_LAST_AMC_ADDR 0xA4 */
typedef union
{
	uint32 val;
	struct
	{
		uint32 debugLastAmcAddr:24;
		uint32 reserved0:8;
	} bitFields;
} RegPacExtrapolatorDebugLastAmcAddr_u;



#endif // _PAC_EXTRAPOLATOR_REGS_H_
