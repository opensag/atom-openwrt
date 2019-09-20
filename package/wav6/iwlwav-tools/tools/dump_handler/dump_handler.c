/******************************************************************************
/******************************************************************************

                               Copyright (c) 2019
	                            Intel

  For licensing information, see the file 'LICENSE' in the root folder of
  this software module.

******************************************************************************/
#include "mtlkinc.h"
#include "dump_handler.h"
#include "argv_parser.h"
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <libgen.h>
#include <sys/wait.h>
#include <time.h>

#define LOG_LOCAL_GID   GID_DUMP_HANDLER
#define LOG_LOCAL_FID   1

#define DUMP_HEADER_MAGIC "INTL"
#define DUMP_HEADER_MAGIC_SIZE 4
#define IFACE_NAME_LEN 6 /* wlanX */
#define MAX_FILE_NAME_SIZE 256
#define MAX_CMD_SIZE 1024
#define MAX_FW_FILES 100
#define BUF_SIZE 4096
#define FW_DUMP_FILE_PREFIX "/proc/net/mtlk/card"
#define FW_DUMP_FILE_SUFFIX "/FW/fw_dump"
#define FW_TMP_PATH "/tmp/fw_dump_files"
#define EVENT_LISTENER "/usr/sbin/dwpal_cli"
#define EVENT_LISTENER_PUMA "/usr/bin/dwpal_cli"
#define MAX_DUMP_TAR_FILES 1
#define NUM_OF_LATEST_DUMPS_TO_KEEP 1
#define NUM_OF_OLDEST_DUMPS_TO_KEEP 2

#ifdef DEBUG_BUILD
#define USB_MOUNT_PATH "/mnt/usb/*/*/"
#define USB_MOUNT_PATH_PUMA "/tmp/mnt/*/"
#endif

#define OUI_LTQ 0xAC9A96

typedef struct {
  char name[MAX_FILE_NAME_SIZE];
  unsigned int size;
} fw_file;

static const struct mtlk_argv_param_info_ex param_card_idx =  {
  {
    "i",
    "card_idx",
    MTLK_ARGV_PINFO_FLAG_HAS_STR_DATA
  },
  "card index",
  MTLK_ARGV_PTYPE_MANDATORY
};

static const struct mtlk_argv_param_info_ex param_path =  {
  {
    "f",
    "storage_path",
    MTLK_ARGV_PINFO_FLAG_HAS_STR_DATA
  },
  "persistent storage path",
  MTLK_ARGV_PTYPE_MANDATORY
};

static const struct mtlk_argv_param_info_ex param_offline_dump =  {
  {
    "d",
    "offline_dump",
    MTLK_ARGV_PINFO_FLAG_HAS_STR_DATA
  },
  "parse offline dump",
  MTLK_ARGV_PTYPE_OPTIONAL
};

static BOOL _safe_system_cmd_string (const char * string, int size){
  int i;
  for(i=0; i<size && string[i]!=0; i++){
    if (string[i] == ';' || string[i] == '|' || string[i] == '&' ||
        string[i] == '\n' || string[i] == '\r' || string[i] == '`')
      goto failure;
    if (i<size-1 && string[i] == '$' && string[i+1] == '(')
      goto failure;
  }
  return TRUE;

failure:
  ELOG_S("error: string %s contains unsafe characters", string);
  return FALSE;
}

static void _zip_fw_files (const char *storage_path, int card_idx, BOOL no_limit_dumps){
  char                system_cmd[MAX_CMD_SIZE], out_str[MAX_CMD_SIZE];
  char                * b_name = NULL;
  char                * d_name = NULL;
  int                 snprintf_res = 0;
  char                * tmp_dump_dir = NULL;
  char                tmp_dump_dir_full_path[MAX_FILE_NAME_SIZE];
  char                tar_file_name[MAX_FILE_NAME_SIZE];
  char                * tmp_dump_dir_parent = NULL;
  int                 full_path_length = 0;
  time_t              t = time(NULL);
  struct              tm tm = *localtime(&t);
  unsigned long long  remaining_space_in_kb = 0;
  FILE                *pf = NULL;
  BOOL                zip_file_removed = FALSE;

  /* sanity */
  if(!_safe_system_cmd_string(storage_path, MAX_FILE_NAME_SIZE)){
    ELOG_V("Invalid storage path");
    goto end;
  }

  snprintf_res = snprintf(tmp_dump_dir_full_path, sizeof(tmp_dump_dir_full_path), "%s_card_%d/", FW_TMP_PATH, card_idx);

  if(snprintf_res <= 0 || snprintf_res >= sizeof(tmp_dump_dir_full_path)){
    ELOG_V("FW file tar command failure");
    goto end;
  }

  full_path_length = mtlk_osal_strnlen(tmp_dump_dir_full_path, MAX_FILE_NAME_SIZE) + 1;

  b_name = malloc(full_path_length);
  if (!b_name){
    ELOG_V("FW file tar command failure");
    goto end;
  }
  mtlk_osal_strlcpy(b_name, tmp_dump_dir_full_path, MAX_FILE_NAME_SIZE);
  tmp_dump_dir = basename(b_name);

  d_name = malloc(full_path_length);
  if (!d_name){
    ELOG_V("FW file tar command failure");
    goto end;
  }
  mtlk_osal_strlcpy(d_name, tmp_dump_dir_full_path, MAX_FILE_NAME_SIZE);
  tmp_dump_dir_parent = dirname(d_name);

  /* keep oldest and latest files */
  if (!no_limit_dumps){
    snprintf_res = snprintf(system_cmd, sizeof (system_cmd),
                            "files=`/bin/ls -tr1 %s/fw_dump_*tar.gz 2>/dev/null`;"
                            "if [ -n \"$files\" ]; then"
                            "  count=`/bin/echo $files | /usr/bin/wc -w 2>/dev/null`;"
                            "  if [ $count -gt %d ]; then"
                            "    files=`/bin/echo $files | /usr/bin/tr \"' '\" \"'\\n'\"| /usr/bin/tail -n $((count-%d)) | /usr/bin/head -n $((count-%d))`;"
                            "    /bin/echo $files | /usr/bin/xargs /bin/rm > /dev/null 2>&1;"
                            "  fi;"
                            "fi;",
                            storage_path,
                            NUM_OF_OLDEST_DUMPS_TO_KEEP + NUM_OF_LATEST_DUMPS_TO_KEEP - 1,
                            NUM_OF_OLDEST_DUMPS_TO_KEEP,
                            NUM_OF_OLDEST_DUMPS_TO_KEEP + NUM_OF_LATEST_DUMPS_TO_KEEP - 1);

    if (snprintf_res <= 0 || snprintf_res >= sizeof (system_cmd)){
      ELOG_V("Old file removal command has failed");
      goto end;
    }
    system(system_cmd);
  }

  /* tar+zip fw dump files */
  snprintf_res = snprintf(tar_file_name, sizeof (tar_file_name),
                          "fw_dump_%d_%02d_%02d_%02d_%02d_%02d.tar.gz",
                          tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                          tm.tm_hour, tm.tm_min, tm.tm_sec);
  if (snprintf_res <= 0 || snprintf_res >= sizeof (system_cmd)){
    ELOG_V("FW file tar filename failure");
    goto end;
  }

  snprintf_res = snprintf(system_cmd, sizeof (system_cmd),
                          "/bin/tar -C %s -czf %s/%s %s > /dev/null 2>&1 &&"
                          " /bin/rm -r %s > /dev/null 2>&1",
                          tmp_dump_dir_parent, storage_path, tar_file_name, tmp_dump_dir, tmp_dump_dir_full_path);
  if((!_safe_system_cmd_string(tmp_dump_dir_parent, full_path_length)) ||
     (!_safe_system_cmd_string(tmp_dump_dir, full_path_length)) ||
     (!_safe_system_cmd_string(tar_file_name, full_path_length)) ||
     (!_safe_system_cmd_string(tmp_dump_dir_full_path, full_path_length)) ||
      snprintf_res <= 0 || snprintf_res >= sizeof (system_cmd)){
    ELOG_V("FW file tar command failure");
    goto end;
  }

  if (system(system_cmd)){
    ELOG_V("FW file tar command returned error");
  }

  /* check remaining space on storage device */
  snprintf_res = snprintf(system_cmd, sizeof (system_cmd),
                          "/bin/df -P %s 2>/dev/null | awk 'NR==2 {print $4}'",
                          storage_path);
  if(snprintf_res <= 0 || snprintf_res >= sizeof (system_cmd)){
    ELOG_V("Invalid df command string");
  }
  else {
    pf =  popen(system_cmd, "r");
    if (pf && (fgets(out_str, MAX_FILE_NAME_SIZE , pf) != NULL)){
      if (sscanf(out_str, "%llu", &remaining_space_in_kb) != 1){
        ELOG_S("Invalid df result %s", out_str);
      }
      else
      {
        /* leave at least ~0.5MB on persistent storage */
        if ((remaining_space_in_kb) <= (512)){
          ELOG_V("not enough space left on storage device, removing dump file");
          snprintf_res = snprintf(system_cmd, sizeof (system_cmd),
                                  "/bin/rm %s/%s > /dev/null 2>&1",
                                  storage_path, tar_file_name);
          if (snprintf_res <= 0 || snprintf_res >= sizeof (system_cmd)) {
            ELOG_V("File removal failed");
          }
          else {
            system(system_cmd);
            zip_file_removed = TRUE;
          }
        }
      }
    }
    else {
      ELOG_V("df command failure");
    }
  }

  if (!zip_file_removed){
    ILOG0_SS("Firmware dump tar file created. Location: %s/%s",
            storage_path, tar_file_name);
    printf("Dump Handler: Firmware dump file saved to: %s/%s\n",
            storage_path, tar_file_name);
  }


end:
  if (b_name)
    free(b_name);
  if (d_name)
    free(d_name);
  if (pf)
      pclose(pf);

}

static int
_fetch_dumps (char *fw_dump_filename, const char *storage_path, int card_idx, BOOL no_limit_dumps){
  FILE                *dump_file = NULL;
  FILE                *out_file = NULL;
  char                line[MAX_FILE_NAME_SIZE];
  int                 header_size=0;
  void                *buf[BUF_SIZE];
  fw_file             fw_files [MAX_FW_FILES];
  int                 num_files = 0;
  int                 cur_file;
  struct              stat st = {0};
  char                tmp_fw_folder[MAX_FILE_NAME_SIZE];
  int                 res = MTLK_ERR_OK;
  int                 snprintf_res = 0;
  char                dump_header_magic[DUMP_HEADER_MAGIC_SIZE+1];
  BOOL                is_start = TRUE;
  int                 i;

  MTLK_ASSERT(MAX_FILE_NAME_SIZE >= DUMP_HEADER_MAGIC_SIZE);

  /*
   * Header format:
   * -Magic identifier, 4 bytes: INTL (ASCII): 0x49, 0x4E, 0x54, 0x4C
   * -LF
   * The following is repeated per file:
   *   -File name in ASCII
   *   -LF
   *   -File size in ASCII hex string
   *   -LF
   * -Marker of the end of the header, 4 bytes ‘<<<<’: 0x3C, 0x3C, 0x3C, 0x3C
   * -LF
   * -Data. A concatenation of the dump files.
  */

  dump_file = fopen(fw_dump_filename, "rb+");
  if (dump_file == NULL){
    ELOG_S("Cannot open firmware dump file %s", fw_dump_filename);
    res = MTLK_ERR_FILEOP;
    goto end;
  }

  /* Read file header */
  while (fgets(line, sizeof(line), dump_file)) {
    if (line[0] == '\0'){
      ELOG_S("Invalid firmware dump file %s", fw_dump_filename);
      res = MTLK_ERR_UNKNOWN;
      goto end;
    }

    mtlk_osal_strlcpy(dump_header_magic, DUMP_HEADER_MAGIC, DUMP_HEADER_MAGIC_SIZE+1);

    /*Done like this to avoid klocwork errors*/
    for (i=0;i<DUMP_HEADER_MAGIC_SIZE;i++){
      if (dump_header_magic[i] != line[i]){
        is_start = FALSE;
        break;
      }
    }

    /*Verify header magic*/
    if (header_size == 0 && !is_start){
      ELOG_S("Invalid firmware dump file %s", fw_dump_filename);
      res = MTLK_ERR_UNKNOWN;
      goto end;
    }

    header_size += mtlk_osal_strnlen (line, sizeof(line));
		
    /* look for where the header ends */
    if (line[0] == '<')
      break;

    if (!is_start){
      line [(mtlk_osal_strnlen (line, sizeof(line))) -1 ] = '\0';
      mtlk_osal_strlcpy(fw_files[num_files].name, line, MAX_FILE_NAME_SIZE);

      /* get file size */
      if (!fgets(line, sizeof(line), dump_file)){
        ELOG_S("Invalid firmware dump format, missing file size %s", fw_dump_filename);
        res = MTLK_ERR_UNKNOWN;
        goto end;
      }

      if (sscanf(line, "%x\r", &fw_files[num_files].size) != 1){
        ELOG_S("Invalid firmware dump format, wrong file size %s", fw_dump_filename);
        res = MTLK_ERR_UNKNOWN;
        goto end;
      }

      num_files ++;

    }

  }

  /* break up dump file to seperate files */
  snprintf_res = snprintf(tmp_fw_folder, sizeof(tmp_fw_folder), "%s_card_%d/", FW_TMP_PATH, card_idx);
  if (snprintf_res <= 0 || snprintf_res >= sizeof(tmp_fw_folder)){
    ELOG_V("snprintf error");
    res = MTLK_ERR_UNKNOWN;
    goto end;
  }
  if (stat(tmp_fw_folder, &st) == 0)
    rmdir(tmp_fw_folder);

  mkdir(tmp_fw_folder, 0700);

  for (cur_file = 0; cur_file < num_files ; cur_file++){
    int remaining = fw_files[cur_file].size;
    int read, written;
    char out_file_full_name[MAX_FILE_NAME_SIZE];

    snprintf_res = snprintf(out_file_full_name, MAX_FILE_NAME_SIZE, "%s/%s", tmp_fw_folder,
                            fw_files[cur_file].name);
    if (snprintf_res <= 0 || snprintf_res >= MAX_FILE_NAME_SIZE){
      ELOG_V("snprintf error");
      res = MTLK_ERR_UNKNOWN;
      goto end;
    }
    out_file = fopen(out_file_full_name, "wb+");
    if (out_file == NULL){
      ELOG_S("Cannot open output dump file %s", fw_files[cur_file].name);
      res = MTLK_ERR_FILEOP;
      goto end;
    }

    while(remaining > 0){

      read = fread(buf, 1, remaining < BUF_SIZE ?remaining:BUF_SIZE, dump_file);

      if (!read){
        ELOG_S("Error reading %s", fw_files[cur_file].name);
        res = MTLK_ERR_FILEOP;
        goto end;
      }
      remaining -= read;

      while (read){
        written = fwrite (buf, 1, read, out_file);
        if (!written){
          ELOG_S("Error writing %s", fw_files[cur_file].name);
          res = MTLK_ERR_FILEOP;
          goto end;
        }
        read -= written;
      }
    }

    fclose (out_file);
    out_file = NULL;
  }

  _zip_fw_files(storage_path, card_idx, no_limit_dumps);

end:

  if (dump_file)
    fclose(dump_file);

  if (out_file)
    fclose (out_file);

  return res;
}

static void
_print_help (const char *app_name)
{
  const struct mtlk_argv_param_info_ex *all_params[] = {
    &param_card_idx,
    &param_path,
    &param_offline_dump,
  };
  const char *app_fname = strrchr(app_name, '/');

  if (!app_fname) {
    app_fname = app_name;
  }
  else {
    ++app_fname; /* skip '/' */
  }

  mtlk_argv_print_help(stdout,
                       app_fname,
                       "Firmware dump evacuation application",
                       all_params,
                       (uint32)ARRAY_SIZE(all_params));
}

/*Check if recovery has already happend before this script was initiated
*(posibly before nl80211 has been initialized) */
static BOOL _rcvry_happend(char *dump_file_path){
  char                sys_cmd[MAX_CMD_SIZE];

  int snprintf_res = snprintf (sys_cmd, sizeof(sys_cmd), "/usr/bin/head -1 %s  2>/dev/null | /bin/grep %s > /dev/null",
                               dump_file_path, DUMP_HEADER_MAGIC);
  if((!_safe_system_cmd_string(dump_file_path, sizeof (sys_cmd))) ||
     (!_safe_system_cmd_string(DUMP_HEADER_MAGIC, sizeof (sys_cmd)))){
    ELOG_V("unsafe command failure");
    return FALSE;
  }
  if (snprintf_res <= 0 || snprintf_res >= sizeof(sys_cmd)){
    ELOG_V("snprintf error");
    return FALSE;
  }
  return (system (sys_cmd) == 0);
}

static void _listener(char * iface_name, char * fw_dump_filename, char * storage_path, int card_idx, BOOL no_limit_dumps){
  char                sys_cmd[MAX_CMD_SIZE];
  int res = MTLK_ERR_OK;
  int snprintf_res = 0;

  /*listen to d-wpal dumps-ready event*/
  /*We want to exit only in case of fatal error*/
  while (res != MTLK_ERR_FILEOP) {
    snprintf_res = snprintf (sys_cmd, sizeof(sys_cmd), "EVENT_LISTENER=%s;[ -e $EVENT_LISTENER ] || EVENT_LISTENER=%s;"
                             "$EVENT_LISTENER -iDriver -v%s -l\"FW_DUMP_READY\"",
                             EVENT_LISTENER, EVENT_LISTENER_PUMA,  iface_name);
    if (snprintf_res <= 0 || snprintf_res >= sizeof(sys_cmd)){
      ELOG_V("snprintf error");
      return;
    }
    system (sys_cmd);
    res = _fetch_dumps(fw_dump_filename, storage_path, card_idx, no_limit_dumps);
  }
}

#ifdef DEBUG_BUILD
static int check_if_usb_mounted(char * mnt_path, char * storage_path, BOOL * found_mount)
{

  int                 snprintf_res = 0;
  char                sys_cmd[MAX_CMD_SIZE];
  char                usb_path[MAX_FILE_NAME_SIZE] = {0};
  FILE                *pf;
  int                 res = MTLK_ERR_OK;
  int                 stat;

  *found_mount = FALSE;
  snprintf_res = snprintf (sys_cmd, sizeof (sys_cmd), "/bin/ls -d %s 2>/dev/null | /usr/bin/head -1",
                           mnt_path);

  if (!_safe_system_cmd_string(mnt_path, sizeof (sys_cmd))){
    ELOG_V("unsafe command failure");
    res = MTLK_ERR_PARAMS;
    return res;
  }

  if (snprintf_res <= 0 || snprintf_res >= sizeof(sys_cmd)){
    ELOG_V("snprintf error");
    res = MTLK_ERR_UNKNOWN;
    return res;
  }

  pf =  popen(sys_cmd, "r");
  if (pf && (fgets(usb_path, MAX_FILE_NAME_SIZE , pf) != NULL)){
    stat = pclose(pf);
    if (WEXITSTATUS(stat) == 0){
      if (*usb_path){
        int len;
        wave_strncopy(storage_path, usb_path, MAX_FILE_NAME_SIZE, MAX_FILE_NAME_SIZE);
        len = mtlk_osal_strnlen(storage_path, MAX_FILE_NAME_SIZE);

        if (storage_path[len - 1] == '\n')
          storage_path[len - 1] = '\0';

        *found_mount = TRUE;
      }
    }
  }
  else {
    if (pf){
      pclose(pf);
      pf = NULL;
    }
    ILOG0_S("USB mount path %s not found", mnt_path);
  }
  return res;
}
#endif

int
main(int argc, char *argv[])
{
  int                 res = MTLK_ERR_UNKNOWN;
  mtlk_argv_parser_t  argv_parser;
  mtlk_argv_param_t   *param = NULL;
  int                 argvparser_inited = 0;
  char                iface_name[IFACE_NAME_LEN] = {0};
  const char          *user_path = NULL;
  const char          *dump_path = NULL;
  char                storage_path[MAX_FILE_NAME_SIZE];
  char                dump_file_path[MAX_FILE_NAME_SIZE];
  BOOL                print_help = FALSE;
  BOOL                no_limit_dumps = FALSE;
  BOOL                offline_dump = FALSE;
  int                 card_idx;
  int                 snprintf_res = 0;
  char                fw_dump_filename[MAX_FILE_NAME_SIZE];
#ifdef DEBUG_BUILD
  BOOL                found_mount = FALSE;
#endif
  char                sys_cmd[MAX_CMD_SIZE];
  FILE                *pf;
  int                 status;
  struct              stat st = {0};

  ILOG0_SD("Firmware dump evacuation application v.%s, pid = %d",
           MTLK_SOURCE_VERSION, (int)getpid());

  res = mtlk_argv_parser_init(&argv_parser, argc, argv);
  if (res != MTLK_ERR_OK) {
    ELOG_D("Can't init argv parser (err=%d)", res);
    goto end;
  }
  argvparser_inited = 1;

  param = mtlk_argv_parser_param_get(&argv_parser, &param_path.info);
  if (param) {
    user_path = mtlk_argv_parser_param_get_str_val(param);
    mtlk_argv_parser_param_release(param);

    if (NULL == user_path) {
      ELOG_V("Invalid storage path");
      goto end;
    }
    if (stat(user_path, &st)){
      ELOG_V("Storage path does not exist");
      goto end;
    }
  }
  else {
    ELOG_V("Storage path must be specified");
    print_help = TRUE;
    goto end;
  }
  mtlk_osal_strlcpy(storage_path, user_path, MAX_FILE_NAME_SIZE);

  param = mtlk_argv_parser_param_get(&argv_parser, &param_card_idx.info);
  if (param) {
    card_idx = mtlk_argv_parser_param_get_uint_val(param, -1);
    mtlk_argv_parser_param_release(param);

    if (card_idx < 0 || card_idx > 4) {
      ELOG_V("Invalid card index");
      goto end;
    }
  }
  else {
    ELOG_V("Card index must be set");
    print_help = TRUE;
    goto end;
  }

  param = mtlk_argv_parser_param_get(&argv_parser, &param_offline_dump.info);
  if (param) {
    dump_path = mtlk_argv_parser_param_get_str_val(param);
    mtlk_argv_parser_param_release(param);

    if (NULL == dump_path) {
      ELOG_V("Invalid dump path");
    }
    else{
      offline_dump = TRUE;
      mtlk_osal_strlcpy(dump_file_path, dump_path, MAX_FILE_NAME_SIZE);
    }
  }

  /* get interface name for the selected card */
  snprintf_res = snprintf (sys_cmd, sizeof (sys_cmd), "line=`/bin/cat /proc/net/mtlk/topology | "
                           "/bin/grep -Fn [hw%d |"
                           "/usr/bin/awk -F ':' '{ print $1 }'`;"
                           "[ \"X$line\" != \"X\" ] && /usr/bin/tail +$line /proc/net/mtlk/topology |"
                           "/bin/grep -om1 \"wlan[0-9]\"", card_idx);

  if (snprintf_res <= 0 || snprintf_res >= sizeof(sys_cmd)){
      ELOG_V("snprintf error");
      res = MTLK_ERR_UNKNOWN;
      goto end;
  }

  pf =  popen(sys_cmd, "r");
  if (pf && (fgets(iface_name, IFACE_NAME_LEN , pf) != NULL)){
    status = pclose(pf);
    pf = NULL;
    if (WEXITSTATUS(status) != 0){
      ELOG_V("Interface name/index is invalid");
      mtlk_osal_strlcpy(iface_name, "wlan0", IFACE_NAME_LEN);
    }
  }
  else {
    if (pf){
      pclose(pf);
      pf = NULL;
    }
    ELOG_V("Failed to retrieve interface name, using default (wlan0)");
    mtlk_osal_strlcpy(iface_name, "wlan0", IFACE_NAME_LEN);
  }

  if (offline_dump){
    res = _fetch_dumps(dump_file_path, storage_path, card_idx, TRUE);
    goto end;
  }

  /* in debug mode: override storage path in case DOK was inserted*/
#ifdef DEBUG_BUILD
  res = check_if_usb_mounted(USB_MOUNT_PATH, storage_path, &found_mount);
  if (res != MTLK_ERR_OK)
    goto end;

  if (found_mount){
    no_limit_dumps = TRUE;
  }
  else{
    res = check_if_usb_mounted(USB_MOUNT_PATH_PUMA, storage_path, &found_mount);
    if (res != MTLK_ERR_OK)
      goto end;

    if (found_mount)
      no_limit_dumps = TRUE;
  }
#endif

  snprintf_res = snprintf (fw_dump_filename, sizeof(fw_dump_filename), "%s%d%s",
                           FW_DUMP_FILE_PREFIX, card_idx, FW_DUMP_FILE_SUFFIX);

  if (snprintf_res <= 0 || snprintf_res >= sizeof(fw_dump_filename)){
    ELOG_V("snprintf error");
    res = MTLK_ERR_UNKNOWN;
    goto end;
  }

  if (_rcvry_happend(fw_dump_filename)){
    ILOG0_S("Firmware recovery detected, trying to retrieve dump files for %s",
            iface_name);
    _fetch_dumps(fw_dump_filename, storage_path, card_idx, no_limit_dumps);
  }

  _listener(iface_name, fw_dump_filename, storage_path, card_idx, no_limit_dumps);

end:
  if (print_help)
    _print_help(argv[0]);

  if (argvparser_inited)
    mtlk_argv_parser_cleanup(&argv_parser);

  ILOG0_V("Dump handler is exiting...");

  return res;
}
