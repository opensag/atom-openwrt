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
 * Utilities.
 *
 * Originally written by Andrey Fidrya
 *
 */

#ifndef _UTILS_H_
#define _UTILS_H_

#ifndef CPTCFG_IWLWAV_SILENT
#define mtlk_aux_print_hex(buf, l) _mtlk_aux_print_hex_ex(buf, l, LOG_CONSOLE_TEXT_INFO, LOG_LOCAL_OID, LOG_LOCAL_GID, LOG_LOCAL_FID, __LINE__, 0)
#endif

#define mtlk_dump(level, buf, len, str) \
    { if(IWLWAV_RTLOG_MAX_DLEVEL >= level) \
        __mtlk_dump(buf, len, str, LOG_CONSOLE_TEXT_INFO, LOG_LOCAL_OID, LOG_LOCAL_GID, LOG_LOCAL_FID, __LINE__, level); \
    }

void __mtlk_dump(const void *buf, uint32 len, char *str, const char *fname, uint8 oid, uint8 gid, uint8 fid, uint16 lid, int level);


uint32 mtlk_shexdump (char *buffer, uint8 *data, size_t size);

char * __MTLK_IFUNC mtlk_get_token (char *str, char *buf, size_t len, char delim);

int __MTLK_IFUNC
mtlk_str_to_mac (char const *str, uint8 *addr);

/*
  Trim spaces from left side

  \param str - pointer to the string [I]

  \return
     shifted pointer to the string without spaces
*/
static __INLINE char const*
mtlk_str_ltrim(char const *str)
{
  while(str && ((*str == ' ') || (*str == '\t'))) str++;

  return str;
}

/*
  Convert character hex value to the integer number

  \param c - character with hex value [I]

  \return
     unsinged integer
*/
static __INLINE uint8
mtlk_str_x1tol(char const c)
{
  uint8 val = 0;

  if ((c >= '0') && (c <= '9'))
    val = c - '0';
  else if ((c >= 'A') && (c <= 'F'))
    val = 10 + c - 'A';
  else if ((c >= 'a') && (c <= 'f'))
    val = 10 + c - 'a';

  return val;
}

/*
  Convert two byte character hex values to the integer number

  \param str - character array with hex values [I]

  \return
     unsinged integer
*/
static __INLINE uint8
mtlk_str_x2tol(char const str[2])
{
  uint8 val;

  val   = mtlk_str_x1tol(str[0]);
  val <<= 4;
  val  |= mtlk_str_x1tol(str[1]);

  return val;
}

size_t __MTLK_IFUNC wave_remove_spaces (char* str, size_t len);

#endif // _UTILS_H_

