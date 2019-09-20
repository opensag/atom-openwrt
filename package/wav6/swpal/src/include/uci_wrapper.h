#ifndef __UCI_WRAPPER_API_H_
#define __UCI_WRAPPER_API_H_

#ifndef CONFIG_RPCD
#include <puma_safe_libc.h>
#else
#include "safe_str_lib.h"
#define snprintf_s snprintf
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef u_int_32
#define u_int_32 unsigned int
#endif

#ifndef _cplusplus
#include <stdbool.h>
#endif

#define FILE_SIZE 1024
#define MAX_LEN_PARAM_VALUE 128
#define MAX_LEN_VALID_VALUE 1024
#define MAX_UCI_BUF_LEN 64
#define MAC_LENGTH 17
#define ATF_STA_GRANTS_LEN 22
#define DUMMY_VAP_OFFSET 100

#define MAX_NUM_OF_RADIOS 3
#define MAX_VAPS_PER_RADIO 16
#define VAP_RPC_IDX_OFFSET 10
#define MAC_ADDR_STR_LEN 18
#define MAX_RPC_VAP_IDX VAP_RPC_IDX_OFFSET + MAX_NUM_OF_RADIOS * MAX_VAPS_PER_RADIO
#define MAX_RDKB_VAP_IDX MAX_NUM_OF_RADIOS + MAX_VAPS_PER_RADIO * MAX_NUM_OF_RADIOS

#define RETURN_ERR_NOT_FOUND -2
#define RETURN_ERR -1
#define RETURN_OK 0

/*Log-Helper-Functions defined in liblishelper*/
#define CRIT(fmt, args...) LOGF_LOG_CRITICAL(fmt, ##args)
#define ERROR(fmt, args...) LOGF_LOG_ERROR(fmt, ##args)
#define WARN(fmt, args...) LOGF_LOG_WARNING(fmt, ##args)
#define INFO(fmt, args...) LOGF_LOG_INFO(fmt, ##args)
#define DEBUG(fmt, args...) LOGF_LOG_DEBUG(fmt, ##args)
/*END Log-Helper-Functions*/


/* use only even phy numbers since odd phy's are used for station interfaces */
#define RADIO_INDEX_SKIP 2

#define UCI_INDEX(iftype, index) iftype == TYPE_RADIO ? (RADIO_INDEX_SKIP * index) : \
                                ( VAP_RPC_IDX_OFFSET + (MAX_VAPS_PER_RADIO * RADIO_INDEX_SKIP * (index%2)) + ((index-(index%2))/2) )

#define UCI_RETURN_INDEX(iftype, rpcIndex) iftype == TYPE_RADIO ? (rpcIndex/RADIO_INDEX_SKIP) : \
                                ( 2 * (rpcIndex - (VAP_RPC_IDX_OFFSET + MAX_VAPS_PER_RADIO * ((rpcIndex-VAP_RPC_IDX_OFFSET)/MAX_VAPS_PER_RADIO))) + \
                                        (((rpcIndex-VAP_RPC_IDX_OFFSET)/MAX_VAPS_PER_RADIO)/RADIO_INDEX_SKIP) )

enum paramType
{
        TYPE_RADIO = 0,
        TYPE_VAP
};

enum uci_hwmode {
        UCI_HWMODE_11B = 0,
        UCI_HWMODE_11BG,
        UCI_HWMODE_11NG,
        UCI_HWMODE_11BGN,
        UCI_HWMODE_11BGNAX,
        UCI_HWMODE_11N_24G,
        UCI_HWMODE_11N_5G,
        UCI_HWMODE_11A,
        UCI_HWMODE_11AN,
        UCI_HWMODE_11NAC,
        UCI_HWMODE_11AC,
        UCI_HWMODE_11ANAC,
        UCI_HWMODE_11ANACAX,

        UCI_HWMODE_LAST /* Keep last */
};

enum uci_htmode {
        UCI_HTMODE_HT20 = 0,
        UCI_HTMODE_HT40PLUS,
        UCI_HTMODE_HT40MINUS,
        UCI_HTMODE_VHT20,
        UCI_HTMODE_VHT40PLUS,
        UCI_HTMODE_VHT40MINUS,
        UCI_HTMODE_VHT80,
        UCI_HTMODE_VHT160,

        UCI_HTMODE_LAST /* Keep last */
};

bool is_empty_str(const char *str);

//UCI HELPER APIS
int uci_getIndexFromInterface(char *interfaceName, int *rpc_index);
int uci_getIndexFromBssid(char *bssid, int *rpc_index);
int uci_converter_system(char *cmd);
int uci_converter_popen(char *cmd, char *output, unsigned int outputSize);
int uci_converter_get(char* path, char* value, size_t length);
int uci_converter_get_str(enum paramType type, int index, const char param[], char *value);
void uci_converter_get_optional_str(enum paramType type, int index, const char param[], char *value, char* default_val);
int uci_converter_get_int(enum paramType type, int index, const char param[], int *value);
void uci_converter_get_optional_int(enum paramType type, int index, const char param[], int *value, int default_val);
int uci_converter_get_uint(enum paramType type, int index, const char param[], unsigned int *value);
void uci_converter_get_optional_uint(enum paramType type, int index, const char param[], unsigned int *value, unsigned int default_val);
int uci_converter_get_ulong(enum paramType type, int index, const char param[], unsigned long *value);
void uci_converter_get_optional_ulong(enum paramType type, int index, const char param[], unsigned long *value, unsigned long default_val);
int uci_converter_get_ushort(enum paramType type, int index, const char param[], unsigned short *value);
void uci_converter_get_optional_ushort(enum paramType type, int index, const char param[], unsigned short *value, unsigned short default_val);
int uci_converter_get_bool(enum paramType type, int index, const char param[], bool *value);
void uci_converter_get_optional_bool(enum paramType type, int index, const char param[], bool *value, bool default_val);
void set_uci_converter_fun(int (*callback)(char* path, const char* option, const char* value));
#ifdef CONFIG_RPCD
int (*uci_converter_set)(char* path, const char* option, const char* value);
#else
int uci_converter_set(char* path, const char* option, const char* value);
#endif
int uci_converter_add_device(char* config_file, char* device_name, char* device_type);
int uci_converter_set_str(enum paramType type, int index, const char param[], const char *value);
int uci_converter_set_int(enum paramType type, int index, const char param[], int value);
int uci_converter_set_uint(enum paramType type, int index, const char param[], unsigned int value);
int uci_converter_set_ulong(enum paramType type, int index, const char param[], unsigned long value);
int uci_converter_set_ushort(enum paramType type, int index, const char param[], unsigned short value);
int uci_converter_set_bool(enum paramType type, int index, const char param[], bool value);
int uci_converter_del_elem(char* path);
int uci_converter_del(enum paramType type, int index, char* param);
int uci_converter_add_list(char* path, char* option, char* value);
int uci_converter_add_list_str(enum paramType type, int index, char param[], char *value);
int uci_converter_del_list(char* path, char* option, char* value);
int uci_converter_del_list_str(enum paramType type, int index, char param[], char *value);
int uci_converter_get_list(char* list, char **output);
int uci_converter_get_list_str(enum paramType type, int index,
                                      char param[], char **output);
int uci_count_elements_list(char *list, unsigned int *num_of_elements);
int uci_converter_count_elements_list(enum paramType type, int index, char param[],
                                             unsigned int *num_of_elements);
int uci_converter_count_elems(const char *sec_type, const char *sec_name,
                              const char* opt_name, const char* opt_val,
                              int *num);
int uci_get_existing_interfaces(int *ifacesArr, const unsigned int ifacesArrSize, unsigned int *numOfIfFound);
int uci_converter_add_meta_wireless(int radio_idx, int vap_idx);
int uci_converter_set_param_changed(enum paramType type, int index, bool reconf_val);
int uci_converter_set_interface_changed(int radio_index);
int uci_converter_reset_param_changed(int radio_index);
int uci_converter_clean_param_changed(int radio_index);
void uci_converter_rewrite_param_changed(int radio_index);
int uci_get_param_change(enum paramType ifType, int index, const char *paramName, char* paramChange);
int uci_converter_revert_elem(char* config, enum paramType type, int index, char* param);
int uci_converter_revert_radio(char* path, int index);
int uci_converter_revert_meta(void);
int uci_converter_commit(char* path, const char* alt_dir);
int uci_converter_commit_wireless(void);
int uci_converter_commit_metawireless(void);
int set_htmode_enum(int index, enum uci_htmode uci_htmode);
int get_htmode_enum(int index, enum uci_htmode *uci_htmode);
int wlan_encryption_set(int index);
int set_hwmode_enum(int index, enum uci_hwmode uci_hwmode);
int get_hwmode_enum(int index, enum uci_hwmode *uci_hwmode);
void uci_converter_prepare_for_up(int radio_idx);
void uci_converter_clean_after_up(int radio_idx, bool change_status);
//UCI HELPER ENDS

#endif //__UCI_WRAPPER_API_H_
