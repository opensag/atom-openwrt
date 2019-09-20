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
#include "mtlkinc.h"

#include "utils.h"

#define LOG_LOCAL_GID   GID_UTILS
#define LOG_LOCAL_FID   1

/*
 * Function outputs buffer in hex format
 */

static __INLINE BOOL _mtlk_check_snprintf(int res, size_t maxsize)
{
  return (res < 0 || (size_t)res >= maxsize) ? FALSE : TRUE;
}

#ifndef CPTCFG_IWLWAV_SILENT

/* 56 bytes: "0000:  36 00 00 00 58 00 00 00  37 00 80 1f 02 42 39 b6" */
#define HEX_DUMP_LINE_LEN (6 + 3 * 8 + 1 + 3 * 8 + 1 /* null */) /* = 56 bytes */

/* Don't remove next line. It is required for generating protype of the function __ELOG_S_8 */
/* ELOG_S("%s", ""); */

static void
_mtlk_aux_print_hex_ex (const void *buf, unsigned int l, const char *fname, uint8 oid, uint8 gid, uint8 fid, uint16 lid, int level)
{
  unsigned int i, j;
  unsigned char lbuf[HEX_DUMP_LINE_LEN];
  int res, lbuf_pos = 0;

  res = snprintf(lbuf, sizeof(lbuf), "cp= 0x%px l=%d", buf, l);
  if (!_mtlk_check_snprintf(res, sizeof(lbuf)))
    return;
  __ELOG_S_8(fname, level, oid, gid, fid, lid, "%s", lbuf);

  for (i = 0; i < l;) {
    res = snprintf(lbuf, sizeof(lbuf), "%04x: ", i);
    if (!_mtlk_check_snprintf(res, sizeof(lbuf)))
      return;
    lbuf_pos = res;
    for (j = 0; (j < 16) && (i < l); j++)
    {
      if (j == 8)
        res = snprintf(&lbuf[lbuf_pos], sizeof(lbuf)-lbuf_pos, "  %02x" , ((const unsigned char*)buf)[i]);
      else
        res = snprintf(&lbuf[lbuf_pos], sizeof(lbuf)-lbuf_pos,  " %02x" , ((const unsigned char*)buf)[i]);
      if (!_mtlk_check_snprintf(res, sizeof(lbuf)-lbuf_pos))
        return;
      lbuf_pos += res;
      i++;
    }
    __ELOG_S_8(fname, level, oid, gid, fid, lid, "%s", lbuf);
  }
}

void __mtlk_dump(const void *buf, uint32 len, char *str, const char *fname, uint8 oid, uint8 gid, uint8 fid, uint16 lid, int level)
{
  int flags;

  flags = mtlk_log_get_flags(level, oid, gid);

  if(flags)
  {
    __ELOG_S_8(fname, level, oid, gid, fid, lid, "%s", str);
    _mtlk_aux_print_hex_ex(buf, len, fname, oid, gid, fid, lid, level);
  }
}
#endif

uint32
mtlk_shexdump (char *buffer, uint8 *data, size_t size)
{
  uint8 line, i;
  uint32 counter = 0;
  int res;

  for (line = 0; size; line++) {
    res = snprintf(buffer + counter, size - counter, "%04x: ", line * 0x10);
    if (!_mtlk_check_snprintf(res, size - counter))
      return counter;
    counter += res;
    for (i = 0x10; i && size; size--,i--,data++) {
      res = snprintf(buffer + counter, size - counter, " %02x", *data);
      if (!_mtlk_check_snprintf(res, size - counter))
        return counter;
      counter += res;
    }
    res = snprintf(buffer + counter, size - counter, "\n");
    if (!_mtlk_check_snprintf(res, size - counter))
      return counter;
    counter += res;
  }
  return counter;
}

char * __MTLK_IFUNC
mtlk_get_token (char *str, char *buf, size_t len, char delim)
{
  char *dlm;

  if (!str) {
    buf[0] = 0;
    return NULL;
  }
  dlm = strchr(str, delim);
  if (dlm) {
    wave_strncopy(buf, str, len, dlm - str);
  } else {
    wave_strcopy(buf, str, len);
  }
  ILOG4_S("Get token: '%s'", buf);
  if (dlm)
    return dlm + 1;
  return NULL;
}

/*
  Extract MAC address from string

  \param str   - string with MAC address [I]
  \param addr  - pointer to MAC storage [O]

  \return
     MTLK_ERR_PARAMS - wrong format of MAC address in the string
     MTLK_ERR_OK - success

  \remark
     accepted the following string formats
       XX:XX:XX:XX:XX:XX
*/
int __MTLK_IFUNC
mtlk_str_to_mac (char const *str, uint8 *addr)
{
  int i;

  MTLK_ASSERT(NULL != str);
  MTLK_ASSERT(NULL != addr);

  str = mtlk_str_ltrim(str);

  if (strlen(str) < 17)
    return MTLK_ERR_PARAMS;

  if ((':' != str[2]) ||
      (':' != str[5]) ||
      (':' != str[8]) ||
      (':' != str[11]) ||
      (':' != str[14]))
    return MTLK_ERR_PARAMS;

  memset (addr, 0, sizeof(*addr));

  for (i = 0; i < 6; i++)
  {
    addr[i] = mtlk_str_x2tol(str + (i * 3));
  }

  return MTLK_ERR_OK;
}

size_t __MTLK_IFUNC wave_remove_spaces (char* str, size_t len)
{
  char* i = str;
  char* j = str;
  while (len-- && *j != '\0') {
    *i = *j++;
    if (*i != ' ' && *i != '\t')
      i++;
  }
  return i - str;
}
