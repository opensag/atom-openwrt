
/***********************************************************************************
File:				MacClcRegs.h
Module:				macClc
SOC Revision:		843
Purpose:
Description:		This File was auto generated using SOC Online

************************************************************************************/
#ifndef _MAC_CLC_REGS_H_
#define _MAC_CLC_REGS_H_

/*---------------------------------------------------------------------------------
/						Registers Addresses													 
/----------------------------------------------------------------------------------*/
#include "HwMemoryMap.h"

#define MAC_CLC_BASE_ADDRESS                             MEMORY_MAP_UNIT_19_BASE_ADDRESS
#define	REG_MAC_CLC_DYNAMIC            (MAC_CLC_BASE_ADDRESS + 0x0)
#define	REG_MAC_CLC_STOP               (MAC_CLC_BASE_ADDRESS + 0x4)
#define	REG_MAC_CLC_EXTEND_REGCLK      (MAC_CLC_BASE_ADDRESS + 0x8)
#define	REG_MAC_CLC_FOLLOW_REGCLK      (MAC_CLC_BASE_ADDRESS + 0x10)
#define	REG_MAC_CLC_DYNAMIC_2          (MAC_CLC_BASE_ADDRESS + 0x14)
#define	REG_MAC_CLC_STOP_2             (MAC_CLC_BASE_ADDRESS + 0x18)
#define	REG_MAC_CLC_FOLLOW_REGCLK_2    (MAC_CLC_BASE_ADDRESS + 0x1C)
/*---------------------------------------------------------------------------------
/						Data Type Definition										
/----------------------------------------------------------------------------------*/
/*REG_MAC_CLC_DYNAMIC 0x0 */
typedef union
{
	uint32 val;
	struct
	{
		uint32 dynamic:32;	// CLC  dynamic
	} bitFields;
} RegMacClcDynamic_u;

/*REG_MAC_CLC_STOP 0x4 */
typedef union
{
	uint32 val;
	struct
	{
		uint32 stop:32;	// CLC  stop
	} bitFields;
} RegMacClcStop_u;

/*REG_MAC_CLC_EXTEND_REGCLK 0x8 */
typedef union
{
	uint32 val;
	struct
	{
		uint32 extendRegclk:6;	// CLC  extend regclk
		uint32 reserved0:26;
	} bitFields;
} RegMacClcExtendRegclk_u;

/*REG_MAC_CLC_FOLLOW_REGCLK 0x10 */
typedef union
{
	uint32 val;
	struct
	{
		uint32 followRegclk:32;	// CLC  follow regclk
	} bitFields;
} RegMacClcFollowRegclk_u;

/*REG_MAC_CLC_DYNAMIC_2 0x14 */
typedef union
{
	uint32 val;
	struct
	{
		uint32 dynamic2:32;	// CLC  dynamic_2
	} bitFields;
} RegMacClcDynamic2_u;

/*REG_MAC_CLC_STOP_2 0x18 */
typedef union
{
	uint32 val;
	struct
	{
		uint32 stop2:32;	// CLC  stop
	} bitFields;
} RegMacClcStop2_u;

/*REG_MAC_CLC_FOLLOW_REGCLK_2 0x1C */
typedef union
{
	uint32 val;
	struct
	{
		uint32 followRegclk2:32;	// CLC  follow regclk
	} bitFields;
} RegMacClcFollowRegclk2_u;



#endif // _MAC_CLC_REGS_H_
