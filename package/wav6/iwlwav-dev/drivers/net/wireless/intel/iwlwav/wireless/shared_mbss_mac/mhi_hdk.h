/******************************************************************************

                               Copyright (c) 2012
                            Lantiq Deutschland GmbH

  For licensing information, see the file 'LICENSE' in the root folder of
  this software module.

******************************************************************************/
/***************************************************************
 File:	mhi_hdk.h
 Module:	
 Purpose: 	
 Description: 
***************************************************************/
#ifndef __MHI_HDK_INCLUDED_H
#define __MHI_HDK_INCLUDED_H

//---------------------------------------------------------------------------------
//						Includes									
//---------------------------------------------------------------------------------
//#define   MTLK_PACK_ON
//#include "mtlkpack.h"

//---------------------------------------------------------------------------------
//						Defines									
//---------------------------------------------------------------------------------


//--------------- Data Structures ---------------------------------------------------

typedef struct CorrResults_tag
{
	int32	II;
	int32	QQ;
	int32	IQ;
}CorrRes_t;

typedef struct S2dParams_tag
{
	uint32 antMask;
	int32 gain;
	int32 iOffset;
	int8 regionIndex; //0 or 1
	int8 reserved[3];
} S2dParams_t;

typedef struct DutRssiOffsetGain_tag
{
	uint32 antMask;
    uint16 rssiResult[4]; //Out
    int Method;
    uint16 NOS;	
} DutRssiOffsetGain_t;

typedef struct RssiCwPower_tag
{
	CorrRes_t corrResults[4];
	uint32 antMask;	
	int method;//not used
	int FreqOffset;// not used
	int NOS;// not used
} RssiCwPower_t;

typedef struct RssiGainBlock_tag
{
	uint8 antMask;
	int8 lnaIndex;
	int8 pgc1;
	int8 pgc2;
	int8 pgc3;
} RssiGainBlock_t;




//#define MTLK_PACK_OFF
//#include "mtlkpack.h"

#endif
















