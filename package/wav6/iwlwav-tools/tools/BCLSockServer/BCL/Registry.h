/******************************************************************************

                               Copyright (c) 2012
                            Lantiq Deutschland GmbH

  For licensing information, see the file 'LICENSE' in the root folder of
  this software module.

******************************************************************************/
#if !defined(AFX_REGISTRY_H__8561A161_52EB_4E8E_B5DC_DF6036E9154C__INCLUDED_)
#define AFX_REGISTRY_H__8561A161_52EB_4E8E_B5DC_DF6036E9154C__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

// 'identifier' : macro redefinition
#pragma warning(disable: 4005)
#include <windows.h>

LONG Registry_Open(LPCTSTR lpPath);
VOID Registry_Close(VOID);
LONG Registry_GetKey(LPCTSTR lpKey, LPCTSTR lpValue, INT iBufferLength);
LONG Registry_SetKey(LPCTSTR lpKey, LPCTSTR lpValue);

#ifndef UNDER_CE // Driver index is included into driver's guid
INT Registry_FindDriverIndex(LPCTSTR lpGuid);
#endif // !UNDER_CE

#endif // !defined(AFX_REGISTRY_H__8561A161_52EB_4E8E_B5DC_DF6036E9154C__INCLUDED_)
