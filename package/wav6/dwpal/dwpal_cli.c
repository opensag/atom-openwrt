/*  *****************************************************************************
 *        File Name    : dwpal_cli.c                             	        *
 *        Description  : test utility in order to test D-WPAL control interface *
 *                                                                              *
 *  *****************************************************************************/

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <signal.h>
#include <malloc.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#if defined YOCTO
#include <errno.h>
#endif

#include <readline/readline.h>
#include <readline/history.h>
#include <sys/select.h>

#if defined YOCTO
#include <slibc/string.h>
#else
#include "safe_str_lib.h"
#endif

#include "dwpal.h"
#include "dwpal_ext.h"
#include "dwpal_log.h"	//Logging

#include <pthread.h>

#if defined YOCTO
#if !defined RSIZE_MAX_STR
#define RSIZE_MAX_STR 256
#endif

//Logging Macros
#ifndef LOG_LEVEL
unsigned int LOGLEVEL = SYS_LOG_DEBUG + 1;
#else
unsigned int LOGLEVEL = LOG_LEVEL + 1;
#endif

#ifndef LOG_TYPE
unsigned int LOGTYPE = SYS_LOG_TYPE_CONSOLE | SYS_LOG_TYPE_FILE;
#else
unsigned int LOGTYPE = LOG_TYPE;
#endif

unsigned int LOGPROFILE;
#endif //END YOCTO

#define NUM_OF_LISTENING_EVENTS 16
extern int check_stats_cmd(int num_arg,char *cmd[]);
DWPAL_Ret nlCliOneShotEventCallback(char *ifname, int event, int subevent, size_t len, unsigned char *data);
DWPAL_Ret nlCliEventCallback(char *ifname, int event, int subevent, size_t len, unsigned char *data);
void dwpalCliCtrlEventCallback(char *msg, size_t len);


typedef struct
{
	char *interfaceType;
	char *radioName;
	char *serviceName;
	int  fd, fdCmdGet;
} DwpalService;

typedef struct
{
	char MACAddress[18];
	int  rx_packets;
	char rssi[128];
} DWPAL_unconnected_sta_rssi_event;

typedef struct
{
	char MACAddress[18];
	int  channel;
	int  dialog_token;
	int  measurement_rep_mode;
	int  op_class;
	int  duration;
	int  rcpi;
	int  rsni;
	char bssid[18];
} DWPAL_rrm_beacon_rep_received_event;

typedef struct
{
	int  freq;
	char chan_width[8];
	int  cf1;
} DWPAL_dfs_nop_finished_event;

typedef struct
{
	int  success;
	int  freq;
	int  timeout;
	char chan_width[8];
} DWPAL_dfs_cac_completed_event;

typedef struct
{
	char MACAddress[18];
	int  status_code;
} DWPAL_bss_tm_resp_event;

typedef struct
{
	char VAPName[16];
	char channel[8];
	int  OperatingChannelBandwidt;
	int  ExtensionChannel;
	int  cf1;
	int  dfs_chan;
	char reason[32];
} DWPAL_acs_completed_event;

typedef struct
{
	char VAPName[16];
	int  Channel;
	int  OperatingChannelBandwidt;
	int  ExtensionChannel;
	int  cf1;
	int  dfs_chan;
	char reason[32];
} DWPAL_csa_finished_channel_int_event;

typedef struct
{
	char VAPName[16];
	char Channel[8];
	int  OperatingChannelBandwidt;
	int  ExtensionChannel;
	int  cf1;
	int  dfs_chan;
	char reason[32];
} DWPAL_csa_finished_event;

typedef struct
{
	char VAPName[16];
	char MACAddress[18];
} DWPAL_sta_disconnected_event;

typedef struct
{
	char VAPName[16];
	char MACAddress[18];
	int  signalStrength;
	int  supportedRates[32];
	int  HT_CAP;
	int  HT_MCS[32];
	int  VHT_CAP;
	int  VHT_MCS[32];
	bool btm_supported;
	bool nr_enabled;
	char non_pref_chan[32][HOSTAPD_TO_DWPAL_VALUE_STRING_LENGTH];
	bool cell_capa;
	char assoc_req[HOSTAPD_TO_DWPAL_VALUE_STRING_LENGTH];
} DWPAL_sta_connected_event;

typedef struct
{
	int dialog_token;
} DWPAL_req_beacon;

typedef struct
{
	char ShortTermRSSIAverage[32];
} DWPAL_get_sta_measurements;

typedef struct
{
	char BSSID[18];
	char SSID[128];
	long long int BytesSent;
	char BytesReceived[16];
	char PacketsSent[16];
	char PacketsReceived[16];
	int  ErrorsSent;
	int  ErrorsReceived;
	int  RetransCount;
} DWPAL_get_vap_measurements;

typedef struct
{
	char freq[32];
	int  bandwidth;
} DWPAL_get_failsafe_channel;

typedef struct
{
	char Name[HOSTAPD_TO_DWPAL_VALUE_STRING_LENGTH];
	int  HostapdEnabled;
	int  TxEnabled;
	int  Channel;
	int  BytesSent;
	int  BytesReceived;
	int  PacketsSent;
	int  PacketsReceived;
	int  ErrorsSent;
	int  ErrorsReceived;
	int  DiscardPacketsSent;
	int  DiscardPacketsReceived;
	int  PLCPErrorCount;
	int  FCSErrorCount;
	int  InvalidMACCount;
	int  PacketsOtherReceived;
	int  Noise;
	int  BSS_load;
	int  TxPower;
	int  RxAntennas;
	int  TxAntennas;
	int  Freq;
	int  OperatingChannelBandwidt;
	int  Cf1;
	int  Dfs_chan;
} DWPAL_radio_info_get;

typedef struct
{
	int Ch;
	int BW;
	int DFS;
	int pow;
	int NF;
	int bss;
	int pri;
	int load;
} DWPAL_acs_report_get;


#if 0
static DwpalService dwpalService[] = { { "hostap", "wlan0", "TWO_WAY", -1, -1 },  /* Will send commands and get events on the same socket */
#else
static DwpalService dwpalService[] = { { "hostap", "wlan0", "ONE_WAY", -1, -1 },  /* Will send commands and get events on a different socket  */
#endif
                                       { "hostap", "wlan1", "ONE_WAY", -1, -1 },
                                       { "hostap", "wlan2", "ONE_WAY", -1, -1 },  /* Will send commands and get events on a different socket */
                                       { "hostap", "wlan3", "ONE_WAY", -1, -1 },
                                       { "hostap", "wlan4", "ONE_WAY", -1, -1 },
                                       { "hostap", "wlan5", "ONE_WAY", -1, -1 },
                                       { "Driver", "ALL",   "TWO_WAY", -1, -1 } };

static void *context[sizeof(dwpalService) / sizeof(DwpalService)] = { [0 ... (sizeof(dwpalService) / sizeof(DwpalService) - 1) ] = NULL };
static bool isCliRunning = true;
static char oneShotCommand[DWPAL_TO_HOSTAPD_MSG_LENGTH] = "\0";
static int numOfListeningEvents = 0;
static char opCodeForListener[NUM_OF_LISTENING_EVENTS][DWPAL_OPCODE_STRING_LENGTH] = { [0 ... (NUM_OF_LISTENING_EVENTS - 1)] = "\0" };  //"\0";
static char oneShotVAPName[DWPAL_VAP_NAME_STRING_LENGTH] = "\0";
static bool isContinueListening = true;


/* Supported commands */
static const char *s_arrDwpalCommands[][2] =
{
    { "help",                          "CLI help"                                  },
    { "exit",                          "Exit the CLI"                              },
    { "quit",                          "Exit the CLI"                              },
	{ "DWPAL_INIT",                    "DWPAL init"                                },
	{ "DWPAL_HOSTAP_CMD_SEND",         "DWPAL hostap command send"                 },
	{ "DWPAL_EXT_HOSTAP_IF_ATTACH",    "DWPAL Extender hostap interface attach"    },
	{ "DWPAL_EXT_HOSTAP_IF_DETACH",    "DWPAL Extender hostap interface detach"    },
	{ "DWPAL_EXT_DRIVER_NL_IF_ATTACH", "DWPAL Extender driver nl interface attach" },
	{ "DWPAL_EXT_DRIVER_NL_IF_DETACH", "DWPAL Extender driver nl interface detach" },
	{ "DWPAL_EXT_DRIVER_NL_CMD_SEND",  "DWPAL Extender driver nl command send"     },

	/* Must be at the end */
    { "__END__", "MUST BE THE LAST ENTRY!" }
};


DWPAL_Ret nlCliOneShotEventCallback(char *ifname, int event, int subevent, size_t len, unsigned char *data)
{
	if ( (ifname == NULL) || ((len > 0) && (data == NULL)) )
	{
		console_printf("%s; ifname or data is NULL ==> Abort!\n", __FUNCTION__);
		return DWPAL_FAILURE;
	}

	if (event == NL80211_CMD_VENDOR)
	{
		switch (subevent)
		{
			case LTQ_NL80211_VENDOR_EVENT_ASSERT_DUMP_READY:
				if (!strncmp("FW_DUMP_READY", opCodeForListener[0], 13))
				{
					if (!strncmp(ifname, oneShotVAPName, 5))
					{
						console_printf("%s; LTQ_NL80211_VENDOR_EVENT_ASSERT_DUMP_READY received => exit!\n", __FUNCTION__);
						exit(0);  /* Terminate the process */
					}

					console_printf("%s; LTQ_NL80211_VENDOR_EVENT_ASSERT_DUMP_READY received from %s " \
								"listening only to %s => ignore\n", __FUNCTION__, ifname, oneShotVAPName);
				}
				break;

			default:
				console_printf("%s; unsupported vendor sub event received; ifname= '%s', event= %d, subevent= %d (len= %d)\n",
				            __FUNCTION__, ifname, event, subevent, len);
				break;
		}
	}
	else
	{
		console_printf("%s; unsupported event (%d) received\n", __FUNCTION__, event);
	}

	if ( (oneShotCommand[0] != '\0') && (opCodeForListener[0][0] == '\0') )
	{
		/* Not waiting for a specific event ==> solicited event received (due to 'get command') */
		console_printf("%s; solicited event received => print first bytes (up to 10), and exit!\n", __FUNCTION__);

		size_t i, lenToPrint = (len <= 10)? len : 10;

		for (i=0; i < lenToPrint; i++)
		{
			console_printf(" 0x%x", data[i]);
		}
		console_printf("\n");

		exit(0);  /* Terminate the process */
	}

	return DWPAL_SUCCESS;
}


DWPAL_Ret nlCliEventCallback(char *ifname, int event, int subevent, size_t len, unsigned char *data)
{
	size_t i, lenToPrint = (len <= 10)? len : 10;

	(void)event;
	(void)subevent;

	if ( (ifname == NULL) || ((len > 0) && (data == NULL)) )
	{
		console_printf("%s; ifname or data is NULL ==> Abort!\n", __FUNCTION__);
		return DWPAL_FAILURE;
	}

	console_printf("%s Entry; ifname= '%s', event= %d, subevent= %d (len= %d), data= 0x%x, lenToPrint= %d\n", __FUNCTION__, ifname, event, subevent, len, (unsigned int)data, lenToPrint);

	for (i=0; i < lenToPrint; i++)
	{
		console_printf(" 0x%x", data[i]);
	}
	console_printf("\n");

	return DWPAL_SUCCESS;
}


static DWPAL_Ret nlCliCmdGetCallback(char *ifname, int event, int subevent, size_t len, unsigned char *data)
{
	size_t i, lenToPrint = (len <= 10)? len : 10;

	(void)ifname;
	(void)event;
	(void)subevent;
	(void)data;

	console_printf("%s Entry; ifname= '%s', event= %d, subevent= %d (len= %d)\n", __FUNCTION__, ifname, event, subevent, len);

	for (i=0; i < lenToPrint; i++)
	{
		console_printf(" 0x%x", data[i]);
	}
	console_printf("\n");

	return DWPAL_SUCCESS;
}


static DWPAL_Ret nlCliNonVendorEventCallback(struct nl_msg *msg)
{
	(void)msg;

	console_printf("%s Entry; msg= 0x%x\n", __FUNCTION__, (unsigned int)msg);

	/* Use the iw function print_bss_handler() as a reference regarding of how to parse the incoming data */

	return DWPAL_SUCCESS;
}


void dwpalCliCtrlEventCallback(char *msg, size_t len)
{
	(void)len;

	if (msg == NULL)
	{
		console_printf("%s; msg is NULL ==> Abort!\n", __FUNCTION__);
		return;
	}

	console_printf("%s; len= %d, msg= '%s'\n", __FUNCTION__, len, msg);
}


static char *dwpal_cli_tab_completion_entry(const char *text , int start)
{
	(void)text;
	(void)start;

	return (NULL);
}


static void sigterm_handler(void)
{
    isCliRunning = false;
    fclose(stdin);
    console_printf("\n");
}


static void sigint_handler(void)
{
    isCliRunning = false;
    fclose(stdin);
    console_printf("\n");
}


static void init_signals(void)
{
    struct sigaction sigterm_action;
    sigterm_action.sa_handler = (void (*)(int))sigterm_handler;
    sigemptyset(&sigterm_action.sa_mask);
    sigterm_action.sa_flags = 0;
    sigaction(SIGTERM, &sigterm_action, NULL);

    struct sigaction sigint_action;
    sigint_action.sa_handler = (void (*)(int))sigint_handler;
    sigemptyset(&sigint_action.sa_mask);
    sigint_action.sa_flags = 0;
    sigaction(SIGINT, &sigint_action, NULL);
}


static char *dupstr(const char *s)
{
	char *r;

	if (!(r = (char *)malloc((size_t)(strnlen_s(s, 256) + 1))))
	{
		console_printf("Error: Out of memory. Exiting\n");
		sigterm_handler();
	}
	else
	{
		strcpy_s(r, strnlen_s(s, RSIZE_MAX_STR) + 1, s);
	}

	return (r);
}


static char *dwpal_cli_generator(const char *text, int state)
{
	static int list_index, len;
	const char *name;

	if (!state)
	{
		list_index = 0;
		len = strnlen_s(text, 256);
	}

	while (strncmp(s_arrDwpalCommands[list_index][0], "__END__", 7))
	{
		name = s_arrDwpalCommands[list_index++][0];

		if (strncmp(name, text, len) == 0)
			return (dupstr(name));
	}

	return NULL;
}


static char **dwpal_cli_tab_completion(const char *text , int start,  int end)
{
	char **matches = NULL;

	(void)end;

	if (start == 0)
		matches = rl_completion_matches(text, dwpal_cli_generator);
	else
		matches = (char **)NULL;

	return (matches);
}


static void dwpal_cli_show_help(void)
{
	int i = 0;

	console_printf("Supported commands:\n");

	/* run till the end of the list */
	while (strncmp(s_arrDwpalCommands[i][0], "__END__", 7))
	{
		console_printf("%-26s - %s\n", s_arrDwpalCommands[i][0], s_arrDwpalCommands[i][1]);
		i++;
	}

	console_printf("\n");
}


static int interfaceIndexGet(char *interfaceType, const char *radioName)
{
	int    i;
	size_t numOfInterfaces = sizeof(dwpalService) / sizeof(DwpalService);

	for (i=0; i < (int)numOfInterfaces; i++)
	{
		if ( (!strncmp(interfaceType, dwpalService[i].interfaceType, 6)) &&
		     (!strncmp(radioName, dwpalService[i].radioName, 5)) &&
		     ( (!strncmp(dwpalService[i].serviceName, "TWO_WAY", 10)) ||
		       (!strncmp(dwpalService[i].serviceName, "ONE_WAY", 10)) ) )
		{
			return i;
		}
	}

	return -1;
}


static bool resultsPrint(FieldsToParse fieldsToParse[], size_t totalSizeOfArg, size_t sizeOfStruct)
{
	int    i = 0;
	size_t j = 0, k;
	void   *field;
	bool   isValid = true;
	char   indexToPrint[16] = "\0";

	if (fieldsToParse == NULL)
	{
		console_printf("%s; input params error ==> Abort!\n", __FUNCTION__);
		return false;
	}

	for (k=0; k < totalSizeOfArg; k++)
	{
		i = 0;

		if (isValid == false)
		{  /* When parsing many lines, when a complete set of struct params is invalid, stop printing */
			//console_printf("%s; Stop the trace (k= %d)\n", __FUNCTION__, k);
			break;
		}

		if (totalSizeOfArg > 1)
		{
			snprintf(indexToPrint, sizeof(indexToPrint), "[%d] ", k);
			//console_printf("%s; sizeof(indexToPrint)= %d, indexToPrint= '%s'\n", __FUNCTION__, sizeof(indexToPrint), indexToPrint);
		}

		isValid = false;

		while (fieldsToParse[i].parsingType != DWPAL_NUM_OF_PARSING_TYPES)
		{
			if ( (k > 0) && (k >= *(fieldsToParse[i].numOfValidArgs)) )
			{
				i++;
				continue;
			}

			if (*(fieldsToParse[i].numOfValidArgs) == 0)
			{
				console_printf("%s; %s%s=> No valid value!\n", __FUNCTION__, indexToPrint, fieldsToParse[i].stringToSearch);
			}

			/* set the output parameter - move it to the next array index (needed when parsing many lines) */
			field = (void *)((unsigned int)fieldsToParse[i].field + k * sizeOfStruct);
			//console_printf("%s; k= %d, field= 0x%x\n", __FUNCTION__, k, (unsigned int)field);

			switch (fieldsToParse[i].parsingType)
			{
				case DWPAL_STR_PARAM:
					if (fieldsToParse[i].stringToSearch == NULL)
					{  /* Handle mandatory parameters WITHOUT any string-prefix */
						if (field != NULL)
						{
							console_printf("%s; %s\n", __FUNCTION__, (char *)field);
						}
					}
					else
					{
						if (*(fieldsToParse[i].numOfValidArgs) > 0)
						{
							isValid = true;
							console_printf("%s; %s%s %s\n", __FUNCTION__, indexToPrint, fieldsToParse[i].stringToSearch, (char *)field);
						}
					}
					break;

				case DWPAL_STR_ARRAY_PARAM:
					for (j=0; j < *(fieldsToParse[i].numOfValidArgs); j++)
					{
						char   fieldName[DWPAL_FIELD_NAME_LENGTH];
						size_t fieldNameLength = strnlen_s(fieldsToParse[i].stringToSearch, DWPAL_FIELD_NAME_LENGTH) - 1;

						isValid = true;

						/* Copy the entire name except of the last character (which is "=") */
						strncpy_s(fieldName, sizeof(fieldName), fieldsToParse[i].stringToSearch, fieldNameLength);
						fieldName[fieldNameLength] = '\0';

						console_printf("%s; %s%s[%d]= %s\n", __FUNCTION__, indexToPrint, fieldName, j, (char *)&(((char *)field)[j * HOSTAPD_TO_DWPAL_VALUE_STRING_LENGTH]));
					}
					break;

				case DWPAL_CHAR_PARAM:
					if (*(fieldsToParse[i].numOfValidArgs) > 0)
					{
						isValid = true;
						console_printf("%s; %s%s %d\n", __FUNCTION__, indexToPrint, fieldsToParse[i].stringToSearch, *((char *)field));
					}
					break;

				case DWPAL_UNSIGNED_CHAR_PARAM:
					if (*(fieldsToParse[i].numOfValidArgs) > 0)
					{
						isValid = true;
						console_printf("%s; %s%s %d\n", __FUNCTION__, indexToPrint, fieldsToParse[i].stringToSearch, *((unsigned char *)field));
					}
					break;

				case DWPAL_SHORT_INT_PARAM:
					if (*(fieldsToParse[i].numOfValidArgs) > 0)
					{
						isValid = true;
						console_printf("%s; %s%s %d\n", __FUNCTION__, indexToPrint, fieldsToParse[i].stringToSearch, *((short int *)field));
					}
					break;

				case DWPAL_INT_PARAM:
					if (*(fieldsToParse[i].numOfValidArgs) > 0)
					{
						isValid = true;
						console_printf("%s; %s%s %d\n", __FUNCTION__, indexToPrint, fieldsToParse[i].stringToSearch, *((int *)field));
					}
					break;

				case DWPAL_UNSIGNED_INT_PARAM:
					if (*(fieldsToParse[i].numOfValidArgs) > 0)
					{
						isValid = true;
						console_printf("%s; %s%s %u\n", __FUNCTION__, indexToPrint, fieldsToParse[i].stringToSearch, *((unsigned int *)field));
					}
					break;

				case DWPAL_LONG_LONG_INT_PARAM:
					if (*(fieldsToParse[i].numOfValidArgs) > 0)
					{
						isValid = true;
						console_printf("%s; %s%s %lld\n", __FUNCTION__, indexToPrint, fieldsToParse[i].stringToSearch, *((long long int *)field));
					}
					break;

				case DWPAL_UNSIGNED_LONG_LONG_INT_PARAM:
					if (*(fieldsToParse[i].numOfValidArgs) > 0)
					{
						isValid = true;
						console_printf("%s; %s%s %llu\n", __FUNCTION__, indexToPrint, fieldsToParse[i].stringToSearch, *((unsigned long long int *)field));
					}
					break;

				case DWPAL_INT_ARRAY_PARAM:
					for (j=0; j < *(fieldsToParse[i].numOfValidArgs); j++)
					{
						char   fieldName[DWPAL_FIELD_NAME_LENGTH];
						size_t fieldNameLength = strnlen_s(fieldsToParse[i].stringToSearch, DWPAL_FIELD_NAME_LENGTH) - 1;

						isValid = true;

						/* Copy the entire name except of the last character (which is "=") */
						strncpy_s(fieldName, sizeof(fieldName), fieldsToParse[i].stringToSearch, fieldNameLength);
						fieldName[fieldNameLength] = '\0';

						console_printf("%s; %s%s[%d]= %d\n", __FUNCTION__, indexToPrint, fieldName, j, ((int *)field)[j]);
					}
					break;

				case DWPAL_INT_HEX_PARAM:
					if (*(fieldsToParse[i].numOfValidArgs) > 0)
					{
						isValid = true;
						console_printf("%s; %s%s 0x%x\n", __FUNCTION__, indexToPrint, fieldsToParse[i].stringToSearch, *((int *)field));
					}
					break;

				case DWPAL_INT_HEX_ARRAY_PARAM:
					for (j=0; j < *(fieldsToParse[i].numOfValidArgs); j++)
					{
						char   fieldName[DWPAL_FIELD_NAME_LENGTH];
						size_t fieldNameLength = strnlen_s(fieldsToParse[i].stringToSearch, DWPAL_FIELD_NAME_LENGTH) - 1;

						isValid = true;

						/* Copy the entire name except of the last character (which is "=") */
						strncpy_s(fieldName, sizeof(fieldName), fieldsToParse[i].stringToSearch, fieldNameLength);
						fieldName[fieldNameLength] = '\0';

						console_printf("%s; %s%s[%d]= 0x%x\n", __FUNCTION__, indexToPrint, fieldName, j, ((int *)field)[j]);
					}
					break;

				case DWPAL_BOOL_PARAM:
					if (*(fieldsToParse[i].numOfValidArgs) > 0)
					{
						isValid = true;
						console_printf("%s; %s%s %d\n", __FUNCTION__, indexToPrint, fieldsToParse[i].stringToSearch, *((bool *)field));
					}
					break;

				default:
					console_printf("%s; parsingType= %d; ERROR ==> Abort!\n", __FUNCTION__, fieldsToParse[i].parsingType);
					break;
			}

			i++;
		}
	}

	return true;
}


static DWPAL_Ret dwpal_req_beacon_handle(void *localContext, char *VAPName, char *fields[], bool isDwpalExtenderMode)
{
	char             *reply = (char *)malloc((size_t)(HOSTAPD_TO_DWPAL_MSG_LENGTH * sizeof(char)));
	size_t           replyLen = HOSTAPD_TO_DWPAL_MSG_LENGTH * sizeof(char) - 1;
	DWPAL_req_beacon req_beacon;
	DWPAL_Ret        ret;
	char             cmd[DWPAL_TO_HOSTAPD_MSG_LENGTH];
	size_t           i, numOfValidArgs[1];
	FieldsToParse    fieldsToParse[] =
	{
		{ (void *)&req_beacon.dialog_token, &numOfValidArgs[0], DWPAL_INT_PARAM, "dialog_token=", 0 },

		/* Must be at the end */
		{ NULL, NULL, DWPAL_NUM_OF_PARSING_TYPES, NULL, 0 }
	};

	console_printf("%s; VAPName= '%s', isDwpalExtenderMode= %d\n", __FUNCTION__, VAPName, isDwpalExtenderMode);

	if (reply == NULL)
	{
		console_printf("%s; malloc error ==> Abort!\n", __FUNCTION__);
		return DWPAL_FAILURE;
	}
	memset((void *)reply, '\0', HOSTAPD_TO_DWPAL_MSG_LENGTH);  /* Clear the output buffer */

	snprintf(cmd, DWPAL_TO_HOSTAPD_MSG_LENGTH, "REQ_BEACON");
	for (i=0; i < 9; i++)
	{
		snprintf(cmd, DWPAL_TO_HOSTAPD_MSG_LENGTH, "%s %s", cmd, fields[i]);
	}

	console_printf("%s; cmd= '%s'\n", __FUNCTION__, cmd);

	if (isDwpalExtenderMode)
	{
		ret = dwpal_ext_hostap_cmd_send(VAPName, cmd, NULL, reply, &replyLen);
	}
	else
	{
		ret = dwpal_hostap_cmd_send(localContext, cmd, NULL, reply, &replyLen);
	}

	if (ret == DWPAL_FAILURE)
	{
		console_printf("%s; REQ_BEACON command send error\n", __FUNCTION__);
	}
	else
	{
		console_printf("%s; replyLen= %d\nresponse=\n%s\n", __FUNCTION__, replyLen, reply);

		if ((ret = dwpal_string_to_struct_parse(reply, replyLen, fieldsToParse)) == DWPAL_FAILURE)
		{
			console_printf("%s; dwpal_string_to_struct_parse ERROR ==> Abort!\n", __FUNCTION__);
			return DWPAL_FAILURE;
		}

		if (resultsPrint(fieldsToParse, 1, sizeof(DWPAL_radio_info_get)) == false)
		{
			console_printf("%s; resultsPrint ERROR ==> Abort!\n", __FUNCTION__);
		}

		/* Example for return value:
		   dialog_token=8
		*/
	}

	free((void *)reply);

	return DWPAL_SUCCESS;
}


static DWPAL_Ret dwpal_get_sta_measurements_handle(void *localContext, char *VAPName, char *MACAddress, bool isDwpalExtenderMode)
{
	char                       *reply = (char *)malloc((size_t)(HOSTAPD_TO_DWPAL_MSG_LENGTH * sizeof(char)));
	size_t                     replyLen = HOSTAPD_TO_DWPAL_MSG_LENGTH * sizeof(char) - 1;
	DWPAL_get_sta_measurements get_sta_measurements;
	DWPAL_Ret                  ret;
	char                       cmd[DWPAL_TO_HOSTAPD_MSG_LENGTH];
	size_t                     numOfValidArgs[1];
	FieldsToParse              fieldsToParse[] =
	{
		{ (void *)&get_sta_measurements.ShortTermRSSIAverage, &numOfValidArgs[0], DWPAL_STR_PARAM, "ShortTermRSSIAverage=", sizeof(get_sta_measurements.ShortTermRSSIAverage) },

		/* Must be at the end */
		{ NULL, NULL, DWPAL_NUM_OF_PARSING_TYPES, NULL, 0 }
	};

	console_printf("%s; VAPName= '%s', isDwpalExtenderMode= %d\n", __FUNCTION__, VAPName, isDwpalExtenderMode);

	if (reply == NULL)
	{
		console_printf("%s; malloc error ==> Abort!\n", __FUNCTION__);
		return DWPAL_FAILURE;
	}
	memset((void *)reply, '\0', HOSTAPD_TO_DWPAL_MSG_LENGTH);  /* Clear the output buffer */

	snprintf(cmd, DWPAL_TO_HOSTAPD_MSG_LENGTH, "STA_MEASUREMENTS %s %s", VAPName, MACAddress);

	if (isDwpalExtenderMode)
	{
		ret = dwpal_ext_hostap_cmd_send(VAPName, cmd, NULL, reply, &replyLen);
	}
	else
	{
		ret = dwpal_hostap_cmd_send(localContext, cmd, NULL, reply, &replyLen);
	}

	if (ret == DWPAL_FAILURE)
	{
		console_printf("%s; GET_STA_MEASUREMENTS command send error\n", __FUNCTION__);
	}
	else
	{
		console_printf("%s; replyLen= %d\nresponse=\n%s\n", __FUNCTION__, replyLen, reply);

		if ((ret = dwpal_string_to_struct_parse(reply, replyLen, fieldsToParse)) == DWPAL_FAILURE)
		{
			console_printf("%s; dwpal_string_to_struct_parse ERROR ==> Abort!\n", __FUNCTION__);
			return DWPAL_FAILURE;
		}

		if (resultsPrint(fieldsToParse, 1, sizeof(DWPAL_radio_info_get)) == false)
		{
			console_printf("%s; resultsPrint ERROR ==> Abort!\n", __FUNCTION__);
		}

		/* Example for return value:
		   MACAddress=fc:c2:de:f3:e8:de
		   OperatingStandard=b g n ac
		   AuthenticationState=1
		   LastDataDownlinkRate=6500
		   LastDataUplinkRate=1000
		   SignalStrength=-85
		   ShortTermRSSIAverage=-88 -128 -85 -128
		   Retransmissions=0
		   Active=1
		   BytesSent=8448
		   BytesReceived=8409
		   PacketsSent=91
		   PacketsReceived=0
		   ErrorsSent=UNKNOWN
		   RetryCount=0
		   FailedRetransCount=0
		   RetryCount=UNKNOWN
		   MultipleRetryCount=UNKNOWN
		*/
	}

	free((void *)reply);

	return DWPAL_SUCCESS;
}


static DWPAL_Ret dwpal_get_vap_measurements_handle(void *localContext, char *VAPName, bool isDwpalExtenderMode)
{
	char                       *reply = (char *)malloc((size_t)(HOSTAPD_TO_DWPAL_MSG_LENGTH * sizeof(char)));
	size_t                     replyLen = HOSTAPD_TO_DWPAL_MSG_LENGTH * sizeof(char) - 1;
	DWPAL_get_vap_measurements get_vap_measurements;
	DWPAL_Ret                  ret;
	char                       cmd[DWPAL_TO_HOSTAPD_MSG_LENGTH];
	size_t                     numOfValidArgs[9];
	FieldsToParse              fieldsToParse[] =
	{
		{ (void *)&get_vap_measurements.BSSID,           &numOfValidArgs[0],  DWPAL_STR_PARAM,           "BSSID=",           sizeof(get_vap_measurements.BSSID)           },
		{ (void *)&get_vap_measurements.SSID,            &numOfValidArgs[1],  DWPAL_STR_PARAM,           "SSID=",            sizeof(get_vap_measurements.SSID)            },
		{ (void *)&get_vap_measurements.BytesSent,       &numOfValidArgs[2],  DWPAL_LONG_LONG_INT_PARAM, "BytesSent=",       0                                            },
		{ (void *)&get_vap_measurements.BytesReceived,   &numOfValidArgs[3],  DWPAL_STR_PARAM,           "BytesReceived=",   sizeof(get_vap_measurements.BytesReceived)   },
		{ (void *)&get_vap_measurements.PacketsSent,     &numOfValidArgs[4],  DWPAL_STR_PARAM,           "PacketsSent=",     sizeof(get_vap_measurements.PacketsSent)     },
		{ (void *)&get_vap_measurements.PacketsReceived, &numOfValidArgs[5],  DWPAL_STR_PARAM,           "PacketsReceived=", sizeof(get_vap_measurements.PacketsReceived) },
		{ (void *)&get_vap_measurements.ErrorsSent,      &numOfValidArgs[6],  DWPAL_INT_PARAM,           "ErrorsSent=",      0                                            },
		{ (void *)&get_vap_measurements.ErrorsReceived,  &numOfValidArgs[7],  DWPAL_INT_PARAM,           "ErrorsReceived=",  0                                            },
		{ (void *)&get_vap_measurements.RetransCount,    &numOfValidArgs[8],  DWPAL_INT_PARAM,           "RetransCount=",    0                                            },

		/* Must be at the end */
		{ NULL, NULL, DWPAL_NUM_OF_PARSING_TYPES, NULL, 0 }
	};

	console_printf("%s; VAPName= '%s', isDwpalExtenderMode= %d\n", __FUNCTION__, VAPName, isDwpalExtenderMode);

	if (reply == NULL)
	{
		console_printf("%s; malloc error ==> Abort!\n", __FUNCTION__);
		return DWPAL_FAILURE;
	}
	memset((void *)reply, '\0', HOSTAPD_TO_DWPAL_MSG_LENGTH);  /* Clear the output buffer */

	snprintf(cmd, DWPAL_TO_HOSTAPD_MSG_LENGTH, "GET_VAP_MEASUREMENTS %s", VAPName);
	console_printf("%s; cmd= '%s'\n", __FUNCTION__, cmd);

	if (isDwpalExtenderMode)
	{
		ret = dwpal_ext_hostap_cmd_send(VAPName, cmd, NULL, reply, &replyLen);
	}
	else
	{
		ret = dwpal_hostap_cmd_send(localContext, cmd, NULL, reply, &replyLen);
	}

	if (ret == DWPAL_FAILURE)
	{
		console_printf("%s; GET_VAP_MEASUREMENTS command send error\n", __FUNCTION__);
	}
	else
	{
		console_printf("%s; replyLen= %d\nresponse=\n%s\n", __FUNCTION__, replyLen, reply);

		if ((ret = dwpal_string_to_struct_parse(reply, replyLen, fieldsToParse)) == DWPAL_FAILURE)
		{
			console_printf("%s; dwpal_string_to_struct_parse ERROR ==> Abort!\n", __FUNCTION__);
			return DWPAL_FAILURE;
		}

		if (resultsPrint(fieldsToParse, 1, sizeof(DWPAL_radio_info_get)) == false)
		{
			console_printf("%s; resultsPrint ERROR ==> Abort!\n", __FUNCTION__);
		}

		/* Example for return value:
		   Name=wlan0
		   Enable=1
		   BSSID=00:0a:1b:0e:04:60
		   SSID=test_2.4
		   BytesSent=389633
		   BytesReceived=416427
		   PacketsSent=1268
		   PacketsReceived=1531
		   ErrorsSent=14
		   RetransCount=2
		   FailedRetransCount=0
		   RetryCount=0
		   MultipleRetryCount=0
		   ACKFailureCount=328867
		   AggregatedPacketCount=2321066
		   ErrorsReceived=19831
		   UnicastPacketsSent=1006
		   UnicastPacketsReceived=1433
		   DiscardPacketsSent=14
		   DiscardPacketsReceived=0
		   MulticastPacketsSent=0
		   MulticastPacketsReceived=98
		   BroadcastPacketsSent=262
		   BroadcastPacketsReceived=0
		   UnknownProtoPacketsReceived=0
		*/
	}

	free((void *)reply);

	return DWPAL_SUCCESS;
}


static DWPAL_Ret dwpal_get_restricted_channels_handle(void *localContext, char *VAPName, bool isDwpalExtenderMode)
{
	char      *reply = (char *)malloc((size_t)(HOSTAPD_TO_DWPAL_MSG_LENGTH * sizeof(char)));
	size_t    replyLen = HOSTAPD_TO_DWPAL_MSG_LENGTH * sizeof(char) - 1;
	DWPAL_Ret ret;

	console_printf("%s; VAPName= '%s', isDwpalExtenderMode= %d\n", __FUNCTION__, VAPName, isDwpalExtenderMode);

	if (reply == NULL)
	{
		console_printf("%s; malloc error ==> Abort!\n", __FUNCTION__);
		return DWPAL_FAILURE;
	}
	memset((void *)reply, '\0', HOSTAPD_TO_DWPAL_MSG_LENGTH);  /* Clear the output buffer */

	if (isDwpalExtenderMode)
	{
		ret = dwpal_ext_hostap_cmd_send(VAPName, "GET_RESTRICTED_CHANNELS", NULL, reply, &replyLen);
	}
	else
	{
		ret = dwpal_hostap_cmd_send(localContext, "GET_RESTRICTED_CHANNELS", NULL, reply, &replyLen);
	}

	if (ret == DWPAL_FAILURE)
	{
		console_printf("%s; GET_RESTRICTED_CHANNELS command send error\n", __FUNCTION__);
	}
	else
	{
		console_printf("%s; replyLen= %d\nresponse=\n%s\n", __FUNCTION__, replyLen, reply);

		/* Note: "reply" itself is already in the needed parsed format! */

		/* Example for return value:
		   1 2 3 4 5 7 9
		   or
		   3 7 8
		   or
		   Empty String in case the list is empty
		*/
	}

	free((void *)reply);

	return DWPAL_SUCCESS;
}


static DWPAL_Ret dwpal_get_failsafe_channel_handle(void *localContext, char *VAPName, bool isDwpalExtenderMode)
{
	char                       *reply = (char *)malloc((size_t)(HOSTAPD_TO_DWPAL_MSG_LENGTH * sizeof(char)));
	size_t                     replyLen = HOSTAPD_TO_DWPAL_MSG_LENGTH * sizeof(char) - 1;
	DWPAL_get_failsafe_channel get_failsafe_channel;
	DWPAL_Ret                  ret;
	int                        freq;
	size_t                     numOfValidArgs[2];
	FieldsToParse              fieldsToParse[] =
	{
		{ (void *)&get_failsafe_channel.freq,      &numOfValidArgs[0], DWPAL_STR_PARAM, NULL,         sizeof(get_failsafe_channel.freq) },
		{ (void *)&get_failsafe_channel.bandwidth, &numOfValidArgs[1], DWPAL_INT_PARAM, "bandwidth=", 0                                 },

		/* Must be at the end */
		{ NULL, NULL, DWPAL_NUM_OF_PARSING_TYPES, NULL, 0 }
	};

	(void)freq;

	console_printf("%s; VAPName= '%s', isDwpalExtenderMode= %d\n", __FUNCTION__, VAPName, isDwpalExtenderMode);

	if (reply == NULL)
	{
		console_printf("%s; malloc error ==> Abort!\n", __FUNCTION__);
		return DWPAL_FAILURE;
	}
	memset((void *)reply, '\0', HOSTAPD_TO_DWPAL_MSG_LENGTH);  /* Clear the output buffer */

	if (isDwpalExtenderMode)
	{
		ret = dwpal_ext_hostap_cmd_send(VAPName, "GET_FAILSAFE_CHAN", NULL, reply, &replyLen);
	}
	else
	{
		ret = dwpal_hostap_cmd_send(localContext, "GET_FAILSAFE_CHAN", NULL, reply, &replyLen);
	}

	if (ret == DWPAL_FAILURE)
	{
		console_printf("%s; GET_FAILSAFE_CHAN command send error\n", __FUNCTION__);
	}
	else
	{
		console_printf("%s; replyLen= %d\nresponse=\n%s\n", __FUNCTION__, replyLen, reply);

		if ((ret = dwpal_string_to_struct_parse(reply, replyLen, fieldsToParse)) == DWPAL_FAILURE)
		{
			console_printf("%s; dwpal_string_to_struct_parse ERROR ==> Abort!\n", __FUNCTION__);
			return DWPAL_FAILURE;
		}

		freq = atoi(get_failsafe_channel.freq);
		console_printf("%s; freq= %d\n", __FUNCTION__, freq);

		if (resultsPrint(fieldsToParse, 1, sizeof(DWPAL_radio_info_get)) == false)
		{
			console_printf("%s; resultsPrint ERROR ==> Abort!\n", __FUNCTION__);
		}

		/* Example for return value:
		   5745 center_freq1=5775 center_freq2=0 bandwidth=80
		*/
	}

	free((void *)reply);

	return DWPAL_SUCCESS;
}


static DWPAL_Ret dwpal_radio_info_handle(void *localContext, char *VAPName, bool isDwpalExtenderMode)
{
	char                 *reply = (char *)malloc((size_t)(HOSTAPD_TO_DWPAL_MSG_LENGTH * sizeof(char)));
	size_t               replyLen = HOSTAPD_TO_DWPAL_MSG_LENGTH * sizeof(char) - 1;
	DWPAL_radio_info_get radio_info;
	DWPAL_Ret            ret;
	size_t               numOfValidArgs[25];
	FieldsToParse        fieldsToParse[] =
	{
		{ (void *)&radio_info.Name,                     &numOfValidArgs[0],  DWPAL_STR_PARAM, "Name=",                     sizeof(radio_info.Name) },
		{ (void *)&radio_info.HostapdEnabled,           &numOfValidArgs[1],  DWPAL_INT_PARAM, "HostapdEnabled=",           0 },
		{ (void *)&radio_info.TxEnabled,                &numOfValidArgs[2],  DWPAL_INT_PARAM, "TxEnabled=",                0 },
		{ (void *)&radio_info.Channel,                  &numOfValidArgs[3],  DWPAL_INT_PARAM, "Channel=",                  0 },
		{ (void *)&radio_info.BytesSent,                &numOfValidArgs[4],  DWPAL_INT_PARAM, "BytesSent=",                0 },
		{ (void *)&radio_info.BytesReceived,            &numOfValidArgs[5],  DWPAL_INT_PARAM, "BytesReceived=",            0 },
		{ (void *)&radio_info.PacketsSent,              &numOfValidArgs[6],  DWPAL_INT_PARAM, "PacketsSent=",              0 },
		{ (void *)&radio_info.PacketsReceived,          &numOfValidArgs[7],  DWPAL_INT_PARAM, "PacketsReceived=",          0 },
		{ (void *)&radio_info.ErrorsSent,               &numOfValidArgs[8],  DWPAL_INT_PARAM, "ErrorsSent=",               0 },
		{ (void *)&radio_info.ErrorsReceived,           &numOfValidArgs[9],  DWPAL_INT_PARAM, "ErrorsReceived=",           0 },
		{ (void *)&radio_info.DiscardPacketsSent,       &numOfValidArgs[10], DWPAL_INT_PARAM, "DiscardPacketsSent=",       0 },
		{ (void *)&radio_info.DiscardPacketsReceived,   &numOfValidArgs[11], DWPAL_INT_PARAM, "DiscardPacketsReceived=",   0 },
		{ (void *)&radio_info.PLCPErrorCount,           &numOfValidArgs[12], DWPAL_INT_PARAM, "PLCPErrorCount=",           0 },
		{ (void *)&radio_info.FCSErrorCount,            &numOfValidArgs[13], DWPAL_INT_PARAM, "FCSErrorCount=",            0 },
		{ (void *)&radio_info.InvalidMACCount,          &numOfValidArgs[14], DWPAL_INT_PARAM, "InvalidMACCount=",          0 },
		{ (void *)&radio_info.PacketsOtherReceived,     &numOfValidArgs[15], DWPAL_INT_PARAM, "PacketsOtherReceived=",     0 },
		{ (void *)&radio_info.Noise,                    &numOfValidArgs[16], DWPAL_INT_PARAM, "Noise=",                    0 },
		{ (void *)&radio_info.BSS_load,                 &numOfValidArgs[17], DWPAL_INT_PARAM, "BSS load=",                 0 },
		{ (void *)&radio_info.TxPower,                  &numOfValidArgs[18], DWPAL_INT_PARAM, "TxPower=",                  0 },
		{ (void *)&radio_info.RxAntennas,               &numOfValidArgs[19], DWPAL_INT_PARAM, "RxAntennas=",               0 },
		{ (void *)&radio_info.TxAntennas,               &numOfValidArgs[20], DWPAL_INT_PARAM, "TxAntennas=",               0 },
		{ (void *)&radio_info.Freq,                     &numOfValidArgs[21], DWPAL_INT_PARAM, "Freq=",                     0 },
		{ (void *)&radio_info.OperatingChannelBandwidt, &numOfValidArgs[22], DWPAL_INT_PARAM, "OperatingChannelBandwidt=", 0 },
		{ (void *)&radio_info.Cf1,                      &numOfValidArgs[23], DWPAL_INT_PARAM, "Cf1=",                      0 },
		{ (void *)&radio_info.Dfs_chan,                 &numOfValidArgs[24], DWPAL_INT_PARAM, "Dfs_chan=",                 0 },

		/* Must be at the end */
		{ NULL, NULL, DWPAL_NUM_OF_PARSING_TYPES, NULL, 0 }
	};

	console_printf("%s; VAPName= '%s', isDwpalExtenderMode= %d\n", __FUNCTION__, VAPName, isDwpalExtenderMode);

	if (reply == NULL)
	{
		console_printf("%s; malloc error ==> Abort!\n", __FUNCTION__);
		return DWPAL_FAILURE;
	}
	memset((void *)reply, '\0', HOSTAPD_TO_DWPAL_MSG_LENGTH);  /* Clear the output buffer */

	if (isDwpalExtenderMode)
	{
		ret = dwpal_ext_hostap_cmd_send(VAPName, "GET_RADIO_INFO", NULL, reply, &replyLen);
	}
	else
	{
		ret = dwpal_hostap_cmd_send(localContext, "GET_RADIO_INFO", NULL, reply, &replyLen);
	}

	if (ret == DWPAL_FAILURE)
	{
		console_printf("%s; GET_RADIO_INFO command send error\n", __FUNCTION__);
	}
	else
	{
		console_printf("%s; replyLen= %d\nresponse=\n%s\n", __FUNCTION__, replyLen, reply);

		if ((ret = dwpal_string_to_struct_parse(reply, replyLen, fieldsToParse)) == DWPAL_FAILURE)
		{
			console_printf("%s; dwpal_string_to_struct_parse ERROR ==> Abort!\n", __FUNCTION__);
			return DWPAL_FAILURE;
		}

		console_printf("%s; dwpal_string_to_struct_parse() ret= %d\n", __FUNCTION__, ret);

		if (resultsPrint(fieldsToParse, 1, sizeof(DWPAL_radio_info_get)) == false)
		{
			console_printf("%s; resultsPrint ERROR ==> Abort!\n", __FUNCTION__);
		}

		/* Example for return value:
		   Name=wlan3
		   WpaSupplicantEnabled=1
		   HostapdEnabled=1
		   TxEnabled=1
		   Channel=52
		   BytesSent=448
		   BytesReceived=370
		   PacketsSent=4
		   PacketsReceived=3
		   ErrorsSent=0
		   ErrorsReceived=0
		   DiscardPacketsSent=0
		   DiscardPacketsReceived=0
		   PLCPErrorCount=UNKNOWN
		   FCSErrorCount=0
		   InvalidMACCount=UNKNOWN
		   PacketsOtherReceived=UNKNOWN
		   Noise=-61
		   BSS load=1
		   TxPower=23.00
		   RxAntennas=4
		   TxAntennas=4
		   Freq=5260
		   OperatingChannelBandwidt=80
		   Cf1=5290
		   Dfs_chan=1
		*/
	}

	free((void *)reply);

	return DWPAL_SUCCESS;
}


static DWPAL_Ret dwpal_acs_report_handle(void *localContext, char *VAPName, bool isDwpalExtenderMode)
{
	char                 *reply = (char *)malloc((size_t)(HOSTAPD_TO_DWPAL_MSG_LENGTH * sizeof(char)));
	size_t               replyLen = HOSTAPD_TO_DWPAL_MSG_LENGTH * sizeof(char) - 1;
	DWPAL_Ret            ret;
	size_t               numOfValidArgs[8];
	DWPAL_acs_report_get acs_report[128];
	FieldsToParse        fieldsToParse[] =
	{
		{ (void *)&acs_report->Ch,   &numOfValidArgs[0], DWPAL_INT_PARAM, "Ch=",   0 },
		{ (void *)&acs_report->BW,   &numOfValidArgs[1], DWPAL_INT_PARAM, "BW=",   0 },
		{ (void *)&acs_report->DFS,  &numOfValidArgs[2], DWPAL_INT_PARAM, "DFS=",  0 },
		{ (void *)&acs_report->pow,  &numOfValidArgs[3], DWPAL_INT_PARAM, "pow=",  0 },
		{ (void *)&acs_report->NF,   &numOfValidArgs[4], DWPAL_INT_PARAM, "NF=",   0 },
		{ (void *)&acs_report->bss,  &numOfValidArgs[5], DWPAL_INT_PARAM, "bss=",  0 },
		{ (void *)&acs_report->pri,  &numOfValidArgs[6], DWPAL_INT_PARAM, "pri=",  0 },
		{ (void *)&acs_report->load, &numOfValidArgs[7], DWPAL_INT_PARAM, "load=", 0 },

		/* Must be at the end */
		{ NULL, NULL, DWPAL_NUM_OF_PARSING_TYPES, NULL, 0 }
	};

	console_printf("%s; VAPName= '%s', isDwpalExtenderMode= %d\n", __FUNCTION__, VAPName, isDwpalExtenderMode);

	if (reply == NULL)
	{
		console_printf("%s; malloc error ==> Abort!\n", __FUNCTION__);
		return DWPAL_FAILURE;
	}
	memset((void *)reply, '\0', HOSTAPD_TO_DWPAL_MSG_LENGTH);  /* Clear the output buffer */

	if (isDwpalExtenderMode)
	{
		ret = dwpal_ext_hostap_cmd_send(VAPName, "GET_ACS_REPORT", NULL, reply, &replyLen);
	}
	else
	{
		ret = dwpal_hostap_cmd_send(localContext, "GET_ACS_REPORT", NULL, reply, &replyLen);
	}

	if (ret == DWPAL_FAILURE)
	{
		console_printf("%s; GET_ACS_REPORT command send error\n", __FUNCTION__);
	}
	else
	{
		console_printf("%s; replyLen= %d\nresponse=\n%s\n", __FUNCTION__, replyLen, reply);

		if ((ret = dwpal_string_to_struct_parse(reply, replyLen, fieldsToParse)) == DWPAL_FAILURE)
		{
			console_printf("%s; dwpal_string_to_struct_parse ERROR ==> Abort!\n", __FUNCTION__);
			return DWPAL_FAILURE;
		}

		console_printf("%s; dwpal_string_to_struct_parse() ret= %d\n", __FUNCTION__, ret);

		if (resultsPrint(fieldsToParse, sizeof(acs_report) / sizeof(DWPAL_acs_report_get), sizeof(DWPAL_acs_report_get)) == false)
		{
			console_printf("%s; resultsPrint ERROR ==> Abort!\n", __FUNCTION__);
		}

		/* Example for return value:
		   Ch=1 BW=20 DFS=0 pow=43 NF=-86 bss=17 pri=17 load=6
		   Ch=1 BW=40 DFS=0 pow=43 NF=-86 bss=28 pri=17 load=6
		   Ch=2 BW=20 DFS=0 pow=43 NF=-69 bss=19 pri=19 load=3
		   Ch=3 BW=20 DFS=0 pow=43 NF=-128 bss=25 pri=25 load=0
		   Ch=4 BW=20 DFS=0 pow=43 NF=-128 bss=28 pri=28 load=0
		   Ch=5 BW=20 DFS=0 pow=43 NF=-76 bss=20 pri=20 load=4
		*/
	}

	free((void *)reply);

	return DWPAL_SUCCESS;
}


static DWPAL_Ret dwpal_unconnected_sta_rssi_event_parse(char *msg, size_t msgLen)
{
	/* <3>UNCONNECTED-STA-RSSI wlan1 c0:c1:c0:68:a4:c9 rx_bytes=0 rx_packets=0 rssi=-128 -128 -128 -12 SNR=105 98 100 0 rate=15877 */

	DWPAL_unconnected_sta_rssi_event unconnected_sta_rssi_event;
	DWPAL_Ret                        ret;
	size_t                           numOfValidArgs[5];
	FieldsToParse                    fieldsToParse[] =
	{
		{ NULL /*opCode*/,                                &numOfValidArgs[0], DWPAL_STR_PARAM, NULL,          0                                             },
		{ NULL /*VAPName*/,                               &numOfValidArgs[1], DWPAL_STR_PARAM, NULL,          0                                             },
		{ (void *)&unconnected_sta_rssi_event.MACAddress, &numOfValidArgs[2], DWPAL_STR_PARAM, NULL,          sizeof(unconnected_sta_rssi_event.MACAddress) },
		{ (void *)&unconnected_sta_rssi_event.rx_packets, &numOfValidArgs[4], DWPAL_INT_PARAM, "rx_packets=", 0                                             },
		{ (void *)&unconnected_sta_rssi_event.rssi,       &numOfValidArgs[5], DWPAL_STR_PARAM, "rssi=",       sizeof(unconnected_sta_rssi_event.rssi)       },

		/* Must be at the end */
		{ NULL, NULL, DWPAL_NUM_OF_PARSING_TYPES, NULL, 0 }
	};

	if ((ret = dwpal_string_to_struct_parse(msg, msgLen, fieldsToParse)) == DWPAL_FAILURE)
	{
		console_printf("%s; dwpal_string_to_struct_parse ERROR ==> Abort!\n", __FUNCTION__);
		return DWPAL_FAILURE;
	}

	console_printf("%s; dwpal_string_to_struct_parse() ret= %d\n", __FUNCTION__, ret);

	if (resultsPrint(fieldsToParse, 1, 0) == false)
	{
		console_printf("%s; resultsPrint ERROR ==> Abort!\n", __FUNCTION__);
		return DWPAL_FAILURE;
	}

	return DWPAL_SUCCESS;
}


static DWPAL_Ret dwpal_rrm_beacon_rep_received_event_parse(char *msg, size_t msgLen)
{
	/* <3>RRM-BEACON-REP-RECEIVED wlan0 8c:70:5a:ed:55:40 dialog_token=1 measurement_rep_mode=0 op_class=128 channel=11 start_time=1234567892947293847 duration=50
	   frame_info=0F rcpi=DE rsni=AD bssid=d8:fe:e3:3e:bd:14 antenna_id=BE 33 parent_tsf=00012345 wide_band_ch_switch=1,1,1
	   timestamp=00 11 22 33 44 55 66 77 beacon_int=5 capab_info=88 99 aa bb cc ssid=dd ee ff 00 11 22 33 44 rm_capa=55 66 77 88 99 aa bb cc
	   vendor_specific=aa bb cc dd ee ff 00 11 rsn_info=22 33 44 55 66 77 88 99 */

	DWPAL_rrm_beacon_rep_received_event rrm_beacon_rep_received_event;
	DWPAL_Ret                           ret;
	size_t                              numOfValidArgs[5];
	FieldsToParse                       fieldsToParse[] =
	{
		{ NULL /*opCode*/,                                             &numOfValidArgs[0],  DWPAL_STR_PARAM,     NULL,                    0                                                },
		{ NULL /*VAPName*/,                                            &numOfValidArgs[1],  DWPAL_STR_PARAM,     NULL,                    0                                                },
		{ (void *)&rrm_beacon_rep_received_event.MACAddress,           &numOfValidArgs[2],  DWPAL_STR_PARAM,     NULL,                    sizeof(rrm_beacon_rep_received_event.MACAddress) },
		{ (void *)&rrm_beacon_rep_received_event.channel,              &numOfValidArgs[4],  DWPAL_INT_PARAM,     "channel=",              0                                                },
		{ (void *)&rrm_beacon_rep_received_event.dialog_token,         &numOfValidArgs[5],  DWPAL_INT_PARAM,     "dialog_token=",         0                                                },
		{ (void *)&rrm_beacon_rep_received_event.measurement_rep_mode, &numOfValidArgs[6],  DWPAL_INT_PARAM,     "measurement_rep_mode=", 0                                                },
		{ (void *)&rrm_beacon_rep_received_event.op_class,             &numOfValidArgs[7],  DWPAL_INT_PARAM,     "op_class=",             0                                                },
		{ (void *)&rrm_beacon_rep_received_event.duration,             &numOfValidArgs[8],  DWPAL_INT_PARAM,     "duration=",             0                                                },
		{ (void *)&rrm_beacon_rep_received_event.rcpi,                 &numOfValidArgs[9],  DWPAL_INT_HEX_PARAM, "rcpi=",                 0                                                },
		{ (void *)&rrm_beacon_rep_received_event.rsni,                 &numOfValidArgs[10], DWPAL_INT_HEX_PARAM, "rsni=",                 0                                                },
		{ (void *)&rrm_beacon_rep_received_event.bssid,                &numOfValidArgs[11], DWPAL_STR_PARAM,     "bssid=",                sizeof(rrm_beacon_rep_received_event.bssid)      },

		/* Must be at the end */
		{ NULL, NULL, DWPAL_NUM_OF_PARSING_TYPES, NULL, 0 }
	};

	if ((ret = dwpal_string_to_struct_parse(msg, msgLen, fieldsToParse)) == DWPAL_FAILURE)
	{
		console_printf("%s; dwpal_string_to_struct_parse ERROR ==> Abort!\n", __FUNCTION__);
		return DWPAL_FAILURE;
	}

	console_printf("%s; dwpal_string_to_struct_parse() ret= %d\n", __FUNCTION__, ret);

	if (resultsPrint(fieldsToParse, 1, 0) == false)
	{
		console_printf("%s; resultsPrint ERROR ==> Abort!\n", __FUNCTION__);
		return DWPAL_FAILURE;
	}

	return DWPAL_SUCCESS;
}


static DWPAL_Ret dwpal_dfs_nop_finished_event_parse(char *msg, size_t msgLen)
{
	/* <3>DFS-NOP-FINISHED wlan2 freq=5260 ht_enabled=1 chan_offset=0 chan_width=3 cf1=5290 cf2=0 */

	DWPAL_dfs_nop_finished_event dfs_nop_finished_event;
	DWPAL_Ret                    ret;
	size_t                       numOfValidArgs[5];
	FieldsToParse                fieldsToParse[] =
	{
		{ NULL /*opCode*/,                            &numOfValidArgs[0], DWPAL_STR_PARAM, NULL,          0                                         },
		{ NULL /*VAPName*/,                           &numOfValidArgs[1], DWPAL_STR_PARAM, NULL,          0                                         },
		{ (void *)&dfs_nop_finished_event.freq,       &numOfValidArgs[2], DWPAL_INT_PARAM, "freq=",       0                                         },
		{ (void *)&dfs_nop_finished_event.chan_width, &numOfValidArgs[3], DWPAL_STR_PARAM, "chan_width=", sizeof(dfs_nop_finished_event.chan_width) },
		{ (void *)&dfs_nop_finished_event.cf1,        &numOfValidArgs[4], DWPAL_INT_PARAM, "cf1=",        0                                         },

		/* Must be at the end */
		{ NULL, NULL, DWPAL_NUM_OF_PARSING_TYPES, NULL, 0 }
	};

	if ((ret = dwpal_string_to_struct_parse(msg, msgLen, fieldsToParse)) == DWPAL_FAILURE)
	{
		console_printf("%s; dwpal_string_to_struct_parse ERROR ==> Abort!\n", __FUNCTION__);
		return DWPAL_FAILURE;
	}

	console_printf("%s; dwpal_string_to_struct_parse() ret= %d\n", __FUNCTION__, ret);

	if (resultsPrint(fieldsToParse, 1, 0) == false)
	{
		console_printf("%s; resultsPrint ERROR ==> Abort!\n", __FUNCTION__);
		return DWPAL_FAILURE;
	}

	return DWPAL_SUCCESS;
}


static DWPAL_Ret dwpal_dfs_cac_completed_event_parse(char *msg, size_t msgLen)
{
	/* <3>DFS-CAC-COMPLETED wlan2 success=1 freq=5260 ht_enabled=0 chan_offset=0 chan_width=3 cf1=5290 cf2=0 timeout=10 */

	DWPAL_dfs_cac_completed_event dfs_cac_completed_event;
	DWPAL_Ret                     ret;
	size_t                        numOfValidArgs[6];
	FieldsToParse                 fieldsToParse[] =
	{
		{ NULL /*opCode*/,                             &numOfValidArgs[0], DWPAL_STR_PARAM, NULL,          0                                          },
		{ NULL /*VAPName*/,                            &numOfValidArgs[1], DWPAL_STR_PARAM, NULL,          0                                          },
		{ (void *)&dfs_cac_completed_event.success,    &numOfValidArgs[2], DWPAL_INT_PARAM, "success=",    0                                          },
		{ (void *)&dfs_cac_completed_event.freq,       &numOfValidArgs[3], DWPAL_INT_PARAM, "freq=",       0                                          },
		{ (void *)&dfs_cac_completed_event.timeout,    &numOfValidArgs[4], DWPAL_INT_PARAM, "timeout=",    0                                          },
		{ (void *)&dfs_cac_completed_event.chan_width, &numOfValidArgs[5], DWPAL_STR_PARAM, "chan_width=", sizeof(dfs_cac_completed_event.chan_width) },

		/* Must be at the end */
		{ NULL, NULL, DWPAL_NUM_OF_PARSING_TYPES, NULL, 0 }
	};

	if ((ret = dwpal_string_to_struct_parse(msg, msgLen, fieldsToParse)) == DWPAL_FAILURE)
	{
		console_printf("%s; dwpal_string_to_struct_parse ERROR ==> Abort!\n", __FUNCTION__);
		return DWPAL_FAILURE;
	}

	console_printf("%s; dwpal_string_to_struct_parse() ret= %d\n", __FUNCTION__, ret);

	if (resultsPrint(fieldsToParse, 1, 0) == false)
	{
		console_printf("%s; resultsPrint ERROR ==> Abort!\n", __FUNCTION__);
		return DWPAL_FAILURE;
	}

	return DWPAL_SUCCESS;
}


static DWPAL_Ret dwpal_bss_tm_resp_event_parse(char *msg, size_t msgLen)
{
	/* <3>BSS-TM-RESP wlan2 e4:9a:79:d2:6b:0b dialog_token=5 status_code=6 bss_termination_delay=0 target_bssid=12:ab:34:cd:56:10 */

	DWPAL_bss_tm_resp_event bss_tm_resp_event;
	DWPAL_Ret               ret;
	size_t                  numOfValidArgs[4];
	FieldsToParse           fieldsToParse[] =
	{
		{ NULL /*opCode*/,                        &numOfValidArgs[0], DWPAL_STR_PARAM, NULL,           0                                    },
		{ NULL /*VAPName*/,                       &numOfValidArgs[1], DWPAL_STR_PARAM, NULL,           0                                    },
		{ (void *)&bss_tm_resp_event.MACAddress,  &numOfValidArgs[2], DWPAL_STR_PARAM, NULL,           sizeof(bss_tm_resp_event.MACAddress) },
		{ (void *)&bss_tm_resp_event.status_code, &numOfValidArgs[3], DWPAL_INT_PARAM, "status_code=", 0                                    },

		/* Must be at the end */
		{ NULL, NULL, DWPAL_NUM_OF_PARSING_TYPES, NULL, 0 }
	};

	if ((ret = dwpal_string_to_struct_parse(msg, msgLen, fieldsToParse)) == DWPAL_FAILURE)
	{
		console_printf("%s; dwpal_string_to_struct_parse ERROR ==> Abort!\n", __FUNCTION__);
		return DWPAL_FAILURE;
	}

	console_printf("%s; dwpal_string_to_struct_parse() ret= %d\n", __FUNCTION__, ret);

	if (resultsPrint(fieldsToParse, 1, 0) == false)
	{
		console_printf("%s; resultsPrint ERROR ==> Abort!\n", __FUNCTION__);
		return DWPAL_FAILURE;
	}

	return DWPAL_SUCCESS;
}


static DWPAL_Ret dwpal_csa_completed_event_parse(char *msg, size_t msgLen)
{
	/* <3>ACS-COMPLETED wlan2 freq=2462 channel=11 OperatingChannelBandwidt=80 ExtensionChannel=1 cf1=5775 cf2=0 reason=UNKNOWN dfs_chan=0 */

	DWPAL_acs_completed_event acs_completed_event;
	DWPAL_Ret                 ret;
	size_t                    numOfValidArgs[8];
	FieldsToParse             fieldsToParse[] =
	{
		{ NULL /*opCode*/,                                       &numOfValidArgs[0], DWPAL_STR_PARAM, NULL,                        0                                   },
		{ (void *)&acs_completed_event.VAPName,                  &numOfValidArgs[1], DWPAL_STR_PARAM, NULL,                        sizeof(acs_completed_event.VAPName) },
		{ (void *)&acs_completed_event.channel,                  &numOfValidArgs[2], DWPAL_STR_PARAM, "channel=",                  sizeof(acs_completed_event.channel) },
		{ (void *)&acs_completed_event.OperatingChannelBandwidt, &numOfValidArgs[3], DWPAL_INT_PARAM, "OperatingChannelBandwidt=", 0                                   },
		{ (void *)&acs_completed_event.ExtensionChannel,         &numOfValidArgs[4], DWPAL_INT_PARAM, "ExtensionChannel=",         0                                   },
		{ (void *)&acs_completed_event.cf1,                      &numOfValidArgs[5], DWPAL_INT_PARAM, "cf1=",                      0                                   },
		{ (void *)&acs_completed_event.dfs_chan,                 &numOfValidArgs[6], DWPAL_INT_PARAM, "dfs_chan=",                 0                                   },
		{ (void *)&acs_completed_event.reason,                   &numOfValidArgs[7], DWPAL_STR_PARAM, "reason=",                   sizeof(acs_completed_event.reason)  },

		/* Must be at the end */
		{ NULL, NULL, DWPAL_NUM_OF_PARSING_TYPES, NULL, 0 }
	};

	if ((ret = dwpal_string_to_struct_parse(msg, msgLen, fieldsToParse)) == DWPAL_FAILURE)
	{
		console_printf("%s; dwpal_string_to_struct_parse ERROR ==> Abort!\n", __FUNCTION__);
		return DWPAL_FAILURE;
	}

	console_printf("%s; dwpal_string_to_struct_parse() ret= %d\n", __FUNCTION__, ret);

	if (resultsPrint(fieldsToParse, 1, 0) == false)
	{
		console_printf("%s; resultsPrint ERROR ==> Abort!\n", __FUNCTION__);
		return DWPAL_FAILURE;
	}

	return DWPAL_SUCCESS;
}


static DWPAL_Ret dwpal_ap_csa_finished_channel_int_event_parse(char *msg, size_t msgLen)
{
	/* <3>AP-CSA-FINISHED wlan2 freq=5745 Channel=149 OperatingChannelBandwidt=80 ExtensionChannel=1 cf1=5775 cf2=0 reason=UNKNOWN dfs_chan=0 */

	DWPAL_csa_finished_channel_int_event csa_finished_event;
	DWPAL_Ret                            ret;
	size_t                               numOfValidArgs[8];
	FieldsToParse                        fieldsToParse[] =
	{
		{ NULL /*opCode*/,                                      &numOfValidArgs[0], DWPAL_STR_PARAM, NULL,                        0                                  },
		{ (void *)&csa_finished_event.VAPName,                  &numOfValidArgs[1], DWPAL_STR_PARAM, NULL,                        sizeof(csa_finished_event.VAPName) },
		{ (void *)&csa_finished_event.Channel,                  &numOfValidArgs[2], DWPAL_INT_PARAM, "Channel=",                  sizeof(csa_finished_event.Channel) },
		{ (void *)&csa_finished_event.OperatingChannelBandwidt, &numOfValidArgs[3], DWPAL_INT_PARAM, "OperatingChannelBandwidt=", 0                                  },
		{ (void *)&csa_finished_event.ExtensionChannel,         &numOfValidArgs[4], DWPAL_INT_PARAM, "ExtensionChannel=",         0                                  },
		{ (void *)&csa_finished_event.cf1,                      &numOfValidArgs[5], DWPAL_INT_PARAM, "cf1=",                      0                                  },
		{ (void *)&csa_finished_event.dfs_chan,                 &numOfValidArgs[6], DWPAL_INT_PARAM, "dfs_chan=",                 0                                  },
		{ (void *)&csa_finished_event.reason,                   &numOfValidArgs[7], DWPAL_STR_PARAM, "reason=",                   sizeof(csa_finished_event.reason)  },

		/* Must be at the end */
		{ NULL, NULL, DWPAL_NUM_OF_PARSING_TYPES, NULL, 0 }
	};

	if ((ret = dwpal_string_to_struct_parse(msg, msgLen, fieldsToParse)) == DWPAL_FAILURE)
	{
		console_printf("%s; dwpal_string_to_struct_parse ERROR ==> Abort!\n", __FUNCTION__);
		return DWPAL_FAILURE;
	}

	console_printf("%s; dwpal_string_to_struct_parse() ret= %d\n", __FUNCTION__, ret);

	if (resultsPrint(fieldsToParse, 1, 0) == false)
	{
		console_printf("%s; resultsPrint ERROR ==> Abort!\n", __FUNCTION__);
		return DWPAL_FAILURE;
	}

	return DWPAL_SUCCESS;
}


static DWPAL_Ret dwpal_ap_csa_finished_event_parse(char *msg, size_t msgLen)
{
	/* <3>AP-CSA-FINISHED wlan2 freq=5745 Channel=149 OperatingChannelBandwidt=80 ExtensionChannel=1 cf1=5775 cf2=0 reason=UNKNOWN dfs_chan=0 */

	DWPAL_csa_finished_event csa_finished_event;
	DWPAL_Ret                ret;
	size_t                   numOfValidArgs[8];
	FieldsToParse            fieldsToParse[] =
	{
		{ NULL /*opCode*/,                                      &numOfValidArgs[0], DWPAL_STR_PARAM, NULL,                        0                                  },
		{ (void *)&csa_finished_event.VAPName,                  &numOfValidArgs[1], DWPAL_STR_PARAM, NULL,                        sizeof(csa_finished_event.VAPName) },
		{ (void *)&csa_finished_event.Channel,                  &numOfValidArgs[2], DWPAL_STR_PARAM, "Channel=",                  sizeof(csa_finished_event.Channel) },
		{ (void *)&csa_finished_event.OperatingChannelBandwidt, &numOfValidArgs[3], DWPAL_INT_PARAM, "OperatingChannelBandwidt=", 0                                  },
		{ (void *)&csa_finished_event.ExtensionChannel,         &numOfValidArgs[4], DWPAL_INT_PARAM, "ExtensionChannel=",         0                                  },
		{ (void *)&csa_finished_event.cf1,                      &numOfValidArgs[5], DWPAL_INT_PARAM, "cf1=",                      0                                  },
		{ (void *)&csa_finished_event.dfs_chan,                 &numOfValidArgs[6], DWPAL_INT_PARAM, "dfs_chan=",                 0                                  },
		{ (void *)&csa_finished_event.reason,                   &numOfValidArgs[7], DWPAL_STR_PARAM, "reason=",                   sizeof(csa_finished_event.reason)  },

		/* Must be at the end */
		{ NULL, NULL, DWPAL_NUM_OF_PARSING_TYPES, NULL, 0 }
	};

	if ((ret = dwpal_string_to_struct_parse(msg, msgLen, fieldsToParse)) == DWPAL_FAILURE)
	{
		console_printf("%s; dwpal_string_to_struct_parse ERROR ==> Abort!\n", __FUNCTION__);
		return DWPAL_FAILURE;
	}

	console_printf("%s; dwpal_string_to_struct_parse() ret= %d\n", __FUNCTION__, ret);

	if (resultsPrint(fieldsToParse, 1, 0) == false)
	{
		console_printf("%s; resultsPrint ERROR ==> Abort!\n", __FUNCTION__);
		return DWPAL_FAILURE;
	}

	return DWPAL_SUCCESS;
}


static DWPAL_Ret dwpal_ap_sta_disconnected_event_parse(char *msg, size_t msgLen)
{
	/* <3>AP-STA-DISCONNECTED wlan0 14:d6:4d:ac:36:70 */

	DWPAL_sta_disconnected_event sta_disconnected_event;
	DWPAL_Ret                    ret;
	size_t                       numOfValidArgs[3];
	FieldsToParse                fieldsToParse[] =
	{
		{ NULL /*opCode*/,                            &numOfValidArgs[0], DWPAL_STR_PARAM, NULL, 0                                         },
		{ (void *)&sta_disconnected_event.VAPName,    &numOfValidArgs[1], DWPAL_STR_PARAM, NULL, sizeof(sta_disconnected_event.VAPName)    },
		{ (void *)&sta_disconnected_event.MACAddress, &numOfValidArgs[2], DWPAL_STR_PARAM, NULL, sizeof(sta_disconnected_event.MACAddress) },

		/* Must be at the end */
		{ NULL, NULL, DWPAL_NUM_OF_PARSING_TYPES, NULL, 0 }
	};

	if ((ret = dwpal_string_to_struct_parse(msg, msgLen, fieldsToParse)) == DWPAL_FAILURE)
	{
		console_printf("%s; dwpal_string_to_struct_parse ERROR ==> Abort!\n", __FUNCTION__);
		return DWPAL_FAILURE;
	}

	console_printf("%s; dwpal_string_to_struct_parse() ret= %d\n", __FUNCTION__, ret);

	if (resultsPrint(fieldsToParse, 1, 0) == false)
	{
		console_printf("%s; resultsPrint ERROR ==> Abort!\n", __FUNCTION__);
		return DWPAL_FAILURE;
	}

	return DWPAL_SUCCESS;
}


static DWPAL_Ret dwpal_ap_sta_connected_event_parse(char *msg, size_t msgLen)
{
	/* <3>AP-STA-CONNECTED wlan0.1 24:77:03:80:5d:90 SignalStrength=-49 SupportedRates=2 4 11 22 12 18 24 36 48 72 96 108 HT_CAP=107E
	   HT_MCS=FF FF FF 00 00 00 00 00 00 00 C2 01 01 00 00 00 VHT_CAP=03807122 VHT_MCS=FFFA 0000 FFFA 0000 btm_supported=1 nr_enabled=0
	   non_pref_chan=81:200:1:7 non_pref_chan=81:100:2:9 non_pref_chan=81:200:1:7 non_pref_chan=81:100:2:5 cell_capa=1 assoc_req=1234 */

	DWPAL_sta_connected_event sta_connected_event;
	DWPAL_Ret                 ret;
	size_t                    numOfValidArgs[14];
	FieldsToParse             fieldsToParse[] =
	{
		{ NULL /*opCode*/,                             &numOfValidArgs[0],  DWPAL_STR_PARAM,           NULL,              0                                          },
		{ (void *)&sta_connected_event.VAPName,        &numOfValidArgs[1],  DWPAL_STR_PARAM,           NULL,              sizeof(sta_connected_event.VAPName)        },
		{ (void *)&sta_connected_event.MACAddress,     &numOfValidArgs[2],  DWPAL_STR_PARAM,           NULL,              sizeof(sta_connected_event.MACAddress)     },
		{ (void *)&sta_connected_event.signalStrength, &numOfValidArgs[3],  DWPAL_INT_PARAM,           "SignalStrength=", 0                                          },
		{ (void *)&sta_connected_event.supportedRates, &numOfValidArgs[4],  DWPAL_INT_ARRAY_PARAM,     "SupportedRates=", sizeof(sta_connected_event.supportedRates) },
		{ (void *)&sta_connected_event.HT_CAP,         &numOfValidArgs[5],  DWPAL_INT_HEX_PARAM,       "HT_CAP=",         0                                          },
		{ (void *)&sta_connected_event.HT_MCS,         &numOfValidArgs[6],  DWPAL_INT_HEX_ARRAY_PARAM, "HT_MCS=",         sizeof(sta_connected_event.HT_MCS)         },
		{ (void *)&sta_connected_event.VHT_CAP,        &numOfValidArgs[7],  DWPAL_INT_HEX_PARAM,       "VHT_CAP=",        0                                          },
		{ (void *)&sta_connected_event.VHT_MCS,        &numOfValidArgs[8],  DWPAL_INT_HEX_ARRAY_PARAM, "VHT_MCS=",        sizeof(sta_connected_event.VHT_MCS)        },
		{ (void *)&sta_connected_event.btm_supported,  &numOfValidArgs[9],  DWPAL_BOOL_PARAM,          "btm_supported=",  0                                          },
		{ (void *)&sta_connected_event.nr_enabled,     &numOfValidArgs[10], DWPAL_BOOL_PARAM,          "nr_enabled=",     0                                          },
		{ (void *)&sta_connected_event.non_pref_chan,  &numOfValidArgs[11], DWPAL_STR_ARRAY_PARAM,     "non_pref_chan=",  sizeof(sta_connected_event.non_pref_chan)  },
		{ (void *)&sta_connected_event.cell_capa,      &numOfValidArgs[12], DWPAL_BOOL_PARAM,          "cell_capa=",      0                                          },
		{ (void *)&sta_connected_event.assoc_req,      &numOfValidArgs[13], DWPAL_STR_PARAM,           "assoc_req=",      sizeof(sta_connected_event.assoc_req)      },

		/* Must be at the end */
		{ NULL, NULL, DWPAL_NUM_OF_PARSING_TYPES, NULL, 0 }
	};

	if ((ret = dwpal_string_to_struct_parse(msg, msgLen, fieldsToParse)) == DWPAL_FAILURE)
	{
		console_printf("%s; dwpal_string_to_struct_parse ERROR ==> Abort!\n", __FUNCTION__);
		return DWPAL_FAILURE;
	}

	console_printf("%s; dwpal_string_to_struct_parse() ret= %d\n", __FUNCTION__, ret);

	if (resultsPrint(fieldsToParse, 1, 0) == false)
	{
		console_printf("%s; resultsPrint ERROR ==> Abort!\n", __FUNCTION__);
		return DWPAL_FAILURE;
	}

	return DWPAL_SUCCESS;
}


static bool boolFromStringGet(char *boolString)
{
	if (boolString == NULL)
	{
		console_printf("%s; boolString is NULL ==> Abort!\n", __FUNCTION__);
		return false;
	}

	if ( (!strncmp(boolString, "true", 4)) || (!strncmp(boolString, "1", 1)) )
	{
		return true;
	}
	else if ( (!strncmp(boolString, "false", 5)) || (!strncmp(boolString, "1", 0)) )
	{
		return false;
	}
	else
	{
		console_printf("%s; illegal boolString '%s' ==> Abort!\n", __FUNCTION__, boolString);
		return false;
	}
}


static void scanParametersGet(char *strOfParams, ScanParams *scanParams)
{
	int     i = 0, freqIdx = 0, ssidIdx = 0;
	rsize_t dmaxLen;
	char    *p2str, *field, *rest = strOfParams;

	dmaxLen = (rsize_t)strnlen_s(strOfParams, DWPAL_CLI_LINE_STRING_LENGTH);

	console_printf("%s; dmaxLen= %d; strOfParams= '%s'\n", __FUNCTION__, dmaxLen, strOfParams);

	memset((void *)scanParams, 0, sizeof(ScanParams));

	while ( (i++ < 64) && ((field = strtok_s(rest, &dmaxLen, " ", &p2str)) != NULL) )
	{
		console_printf("%s; SCAN parameters; field[%d]= '%s'\n", __FUNCTION__, i, field);

		if (!strncmp(field, "freq=", 5))
		{
			if (freqIdx >= NUM_OF_FREQUENCIES)
			{
				console_printf("%s; exceeding max number of frequencies (%d)\n", __FUNCTION__, NUM_OF_FREQUENCIES);
			}
			else
			{
				scanParams->freq[freqIdx] = atoi(field + 5);
				console_printf("%s; freq[%d]= %d\n", __FUNCTION__, freqIdx, scanParams->freq[freqIdx]);
				freqIdx++;
			}
		}
		else if (!strncmp(field, "ies=", 4))
		{
			strncpy_s(scanParams->ies, sizeof(scanParams->ies), field + 4, strnlen_s(field + 4, DWPAL_GENERAL_STRING_LENGTH));
			console_printf("%s; ies= '%s'\n", __FUNCTION__, scanParams->ies);
		}
		else if (!strncmp(field, "meshid=", 7))
		{
			strncpy_s(scanParams->meshid, sizeof(scanParams->meshid), field + 7, strnlen_s(field + 7, DWPAL_GENERAL_STRING_LENGTH));
			console_printf("%s; meshid= '%s'\n", __FUNCTION__, scanParams->meshid);
		}
		else if (!strncmp(field, "lowpri=", 7))
		{
			scanParams->lowpri = boolFromStringGet(field + 7);
			console_printf("%s; lowpri= %d\n", __FUNCTION__, scanParams->lowpri);
		}
		else if (!strncmp(field, "flush=", 6))
		{
			scanParams->flush = boolFromStringGet(field + 6);
			console_printf("%s; flush= %d\n", __FUNCTION__, scanParams->flush);
		}
		else if (!strncmp(field, "ap_force=", 9))
		{
			scanParams->ap_force = boolFromStringGet(field + 9);
			console_printf("%s; ap_force= %d\n", __FUNCTION__, scanParams->ap_force);
		}
#if IS_RANDOMISE_SUPPORTED_BY_DRIVER  // randomise is NOT supported by the Driver
		else if (!strncmp(field, "randomise=", 10))
		{
			strncpy_s(scanParams->randomise, sizeof(scanParams->randomise), field + 10, strnlen_s(field + 10, DWPAL_GENERAL_STRING_LENGTH));
			console_printf("%s; randomise= '%s'\n", __FUNCTION__, scanParams->randomise);
		}
#endif
		else if (!strncmp(field, "ssid=", 5))
		{
			if (ssidIdx >= NUM_OF_SSIDS)
			{
				console_printf("%s; exceeding max number of SSIDs (%d)\n", __FUNCTION__, NUM_OF_SSIDS);
			}
			else
			{
				strncpy_s(scanParams->ssid[ssidIdx], sizeof(scanParams->ssid[ssidIdx]), field + 5, strnlen_s(field + 5, DWPAL_GENERAL_STRING_LENGTH));
				console_printf("%s; ssid[%d]= '%s'\n", __FUNCTION__, ssidIdx, scanParams->ssid[ssidIdx]);
				ssidIdx++;
			}
		}
		else if (!strncmp(field, "passive=", 8))
		{
			scanParams->passive = boolFromStringGet(field + 8);
			console_printf("%s; passive= %d\n", __FUNCTION__, scanParams->passive);
		}

		rest = NULL;  /* For 2nd parameter and further on */
	}
}


static int dwpalOneShotEventCallback(char *radioName, char *opCode, char *msg, size_t msgStringLen)
{
	int i;

	(void)radioName;
	(void)msg;
	(void)msgStringLen;

	console_printf("%s; radioName= '%s', opCode= '%s', msgStringLen= %d, msg= '%s' (numOfListeningEvents= %d)\n",
	            __FUNCTION__, radioName, opCode, msgStringLen, msg, numOfListeningEvents);

	for (i=0; i < numOfListeningEvents; i++)
	{
		if (!strncmp(opCode, opCodeForListener[i], DWPAL_OPCODE_STRING_LENGTH))
		{
			console_printf("%s; radioName= '%s', opCode= '%s' received ==> Exit!!!\n", __FUNCTION__, radioName, opCode);
			isContinueListening = false;
			break;
		}
	}

	return 0;
}


static int dwpalExtEventCallback(char *radioName, char *opCode, char *msg, size_t msgStringLen)
{
	(void)radioName;
	(void)opCode;
	(void)msg;
	(void)msgStringLen;

	console_printf("%s; radioName= '%s', opCode= '%s', msgStringLen= %d, msg= '%s'\n", __FUNCTION__, radioName, opCode, msgStringLen, msg);
	return 0;
}


static DWPAL_Ret interfaceReset(DwpalService *dwpalServiceLocal, int idx)
{
	if (!strncmp(dwpalServiceLocal->interfaceType, "hostap", 7))
	{
		if (dwpal_hostap_interface_detach(&context[idx]) == DWPAL_FAILURE)
		{
			console_printf("%s; dwpal_hostap_interface_detach (radioName= '%s') returned ERROR ==> Abort!\n", __FUNCTION__, dwpalServiceLocal->radioName);
			return DWPAL_FAILURE;
		}
	}
	else if (!strncmp(dwpalServiceLocal->interfaceType, "Driver", 7))
	{
		if (dwpal_driver_nl_detach(&context[idx] /*IN/OUT*/) == DWPAL_FAILURE)
		{
			console_printf("%s; dwpal_driver_nl_detach returned ERROR ==> Abort!\n", __FUNCTION__);
			return DWPAL_FAILURE;
		}
	}

	return DWPAL_SUCCESS;
}


static DWPAL_Ret interfaceSet(DwpalService *dwpalServiceLocal, int idx)
{
	DWPAL_wpaCtrlEventCallback wpaCtrlEventCallback = NULL;

	console_printf("%s Entry; idx= %d\n", __FUNCTION__, idx);

	if (!strncmp(dwpalServiceLocal->interfaceType, "hostap", 7))
	{
		console_printf("%s Entry; idx= %d ==> hostapd\n", __FUNCTION__, idx);

		if (!strncmp(dwpalServiceLocal->serviceName, "TWO_WAY", 10))
		{
			wpaCtrlEventCallback = dwpalCliCtrlEventCallback;
		}

		if (dwpal_hostap_interface_attach(&context[idx] /*OUT*/, dwpalServiceLocal->radioName, wpaCtrlEventCallback) == DWPAL_FAILURE)
		{
			console_printf("%s; dwpal_hostap_interface_attach (radioName= '%s') returned ERROR ==> Abort!\n", __FUNCTION__, dwpalServiceLocal->radioName);
			return DWPAL_FAILURE;
		}
	}
	else if (!strncmp(dwpalServiceLocal->interfaceType, "Driver", 7))
	{
		console_printf("%s Entry; idx= %d ==> Driver\n", __FUNCTION__, idx);
		if (dwpal_driver_nl_attach(&context[idx] /*OUT*/) == DWPAL_FAILURE)
		{
			console_printf("%s; dwpal_driver_nl_attach returned ERROR ==> Abort!\n", __FUNCTION__);
			return DWPAL_FAILURE;
		}
	}

	return DWPAL_SUCCESS;
}


static DWPAL_Ret hostapdEventHandle(char *opCode, char *msg, size_t msgLen)
{
	//console_printf("%s; opCode= '%s'; msgLen= %d\n", __FUNCTION__, opCode, msgLen);

	if (!strncmp(opCode, "AP-STA-CONNECTED", strnlen_s("AP-STA-CONNECTED", DWPAL_GENERAL_STRING_LENGTH)))
	{
		//console_printf("%s; msg= '%s'\n", __FUNCTION__, msg);
		if (dwpal_ap_sta_connected_event_parse(msg, msgLen) == DWPAL_FAILURE)
		{
			console_printf("%s; dwpal_ap_sta_connected_event_parse ERROR ==> Abort!\n", __FUNCTION__);
			return DWPAL_FAILURE;
		}
	}
	else if (!strncmp(opCode, "AP-STA-DISCONNECTED", strnlen_s("AP-STA-DISCONNECTED", DWPAL_GENERAL_STRING_LENGTH)))
	{
		//console_printf("%s; msg= '%s'\n", __FUNCTION__, msg);
		if (dwpal_ap_sta_disconnected_event_parse(msg, msgLen) == DWPAL_FAILURE)
		{
			console_printf("%s; dwpal_ap_sta_disconnected_event_parse ERROR ==> Abort!\n", __FUNCTION__);
			return DWPAL_FAILURE;
		}
	}
	else if (!strncmp(opCode, "AP-CSA-FINISHED", strnlen_s("AP-CSA-FINISHED", DWPAL_GENERAL_STRING_LENGTH)))
	{
		//console_printf("%s; msg= '%s'\n", __FUNCTION__, msg);
		if (dwpal_ap_csa_finished_event_parse(msg, msgLen) == DWPAL_FAILURE)
		{
			console_printf("%s; dwpal_ap_csa_finished_event_parse ERROR ==> Abort!\n", __FUNCTION__);
			return DWPAL_FAILURE;
		}

		if (dwpal_ap_csa_finished_channel_int_event_parse(msg, msgLen) == DWPAL_FAILURE)
		{
			console_printf("%s; dwpal_ap_csa_finished_channel_int_event_parse ERROR ==> Abort!\n", __FUNCTION__);
			return DWPAL_FAILURE;
		}
	}
	else if (!strncmp(opCode, "ACS-COMPLETED", strnlen_s("ACS-COMPLETED", DWPAL_GENERAL_STRING_LENGTH)))
	{
		//console_printf("%s; msg= '%s'\n", __FUNCTION__, msg);
		if (dwpal_csa_completed_event_parse(msg, msgLen) == DWPAL_FAILURE)
		{
			console_printf("%s; dwpal_csa_completed_event_parse ERROR ==> Abort!\n", __FUNCTION__);
			return DWPAL_FAILURE;
		}
	}
	else if (!strncmp(opCode, "BSS-TM-RESP", strnlen_s("BSS-TM-RESP", DWPAL_GENERAL_STRING_LENGTH)))
	{
		//console_printf("%s; msg= '%s'\n", __FUNCTION__, msg);
		if (dwpal_bss_tm_resp_event_parse(msg, msgLen) == DWPAL_FAILURE)
		{
			console_printf("%s; dwpal_bss_tm_resp_event_parse ERROR ==> Abort!\n", __FUNCTION__);
			return DWPAL_FAILURE;
		}
	}
	else if (!strncmp(opCode, "DFS-CAC-COMPLETED", strnlen_s("DFS-CAC-COMPLETED", DWPAL_GENERAL_STRING_LENGTH)))
	{
		//console_printf("%s; msg= '%s'\n", __FUNCTION__, msg);
		if (dwpal_dfs_cac_completed_event_parse(msg, msgLen) == DWPAL_FAILURE)
		{
			console_printf("%s; dwpal_dfs_cac_completed_event_parse ERROR ==> Abort!\n", __FUNCTION__);
			return DWPAL_FAILURE;
		}
	}
	else if (!strncmp(opCode, "DFS-NOP-FINISHED", strnlen_s("DFS-NOP-FINISHED", DWPAL_GENERAL_STRING_LENGTH)))
	{
		//console_printf("%s; msg= '%s'\n", __FUNCTION__, msg);
		if (dwpal_dfs_nop_finished_event_parse(msg, msgLen) == DWPAL_FAILURE)
		{
			console_printf("%s; dwpal_dfs_nop_finished_event_parse ERROR ==> Abort!\n", __FUNCTION__);
			return DWPAL_FAILURE;
		}
	}
	else if (!strncmp(opCode, "RRM-BEACON-REP-RECEIVED", strnlen_s("RRM-BEACON-REP-RECEIVED", DWPAL_GENERAL_STRING_LENGTH)))
	{
		//console_printf("%s; msg= '%s'\n", __FUNCTION__, msg);
		if (dwpal_rrm_beacon_rep_received_event_parse(msg, msgLen) == DWPAL_FAILURE)
		{
			console_printf("%s; dwpal_rrm_beacon_rep_received_event_parse ERROR ==> Abort!\n", __FUNCTION__);
			return DWPAL_FAILURE;
		}
	}
	else if (!strncmp(opCode, "UNCONNECTED-STA-RSSI", strnlen_s("UNCONNECTED-STA-RSSI", DWPAL_GENERAL_STRING_LENGTH)))
	{
		//console_printf("%s; msg= '%s'\n", __FUNCTION__, msg);
		if (dwpal_unconnected_sta_rssi_event_parse(msg, msgLen) == DWPAL_FAILURE)
		{
			console_printf("%s; dwpal_unconnected_sta_rssi_event_parse ERROR ==> Abort!\n", __FUNCTION__);
			return DWPAL_FAILURE;
		}
	}

	return DWPAL_SUCCESS;
}


static void *listenerThreadStart(void *temp)
{
	int     i, highestValFD, ret, numOfServices = sizeof(dwpalService) / sizeof(DwpalService);
	char    *msg;
	size_t  msgLen, msgStringLen;
	fd_set  rfds;
	char    opCode[DWPAL_OPCODE_STRING_LENGTH];
	struct  timeval tv;

	(void)temp;

	console_printf("%s Entry\n", __FUNCTION__);

	/* Receive the msg */
	while (true)
	{
		FD_ZERO(&rfds);
		highestValFD = 0;

		for (i=0; i < numOfServices; i++)
		{
			if (!strncmp(dwpalService[i].interfaceType, "hostap", 7))
			{
				if (dwpal_hostap_event_fd_get(context[i], &dwpalService[i].fd) == DWPAL_FAILURE)
				{
					/*console_printf("%s; dwpal_hostap_event_fd_get returned error ==> cont. (serviceName= '%s', radioName= '%s')\n",
					       __FUNCTION__, dwpalService[i].serviceName, dwpalService[i].radioName);*/
					continue;
				}

				if (dwpalService[i].fd > 0)
				{
					FD_SET(dwpalService[i].fd, &rfds);
					highestValFD = (dwpalService[i].fd > highestValFD)? dwpalService[i].fd : highestValFD;  /* find the highest value fd */
				}
			}
			else if (!strncmp(dwpalService[i].interfaceType, "Driver", 7))
			{
				if (dwpal_driver_nl_fd_get(context[i], &dwpalService[i].fd, &dwpalService[i].fdCmdGet) == DWPAL_FAILURE)
				{
					/*console_printf("%s; dwpal_driver_nl_fd_get returned error ==> cont. (serviceName= '%s', radioName= '%s')\n",
					       __FUNCTION__, dwpalService[i].serviceName, dwpalService[i].radioName);*/
					continue;
				}

				//console_printf("%s; [BEFORE-Driver] highestValFD= %d\n", __FUNCTION__, highestValFD);
				if (dwpalService[i].fd > 0)
				{
					FD_SET(dwpalService[i].fd, &rfds);
					highestValFD = (dwpalService[i].fd > highestValFD)? dwpalService[i].fd : highestValFD;  /* find the highest value fd */
				}
				//console_printf("%s; [AFTER-Driver] highestValFD= %d\n", __FUNCTION__, highestValFD);
			}
		}

		//console_printf("%s; highestValFD= %d\n", __FUNCTION__, highestValFD);
		if (highestValFD == 0)
		{
			console_printf("%s; there is no active hostapd/supplicant ==> cont...\n", __FUNCTION__);
			break;
		}

		/* Interval of time in which the select() will be released */
		tv.tv_sec = 1;
		tv.tv_usec = 0;

		ret = select(highestValFD + 1, &rfds, NULL, NULL, &tv);
		if (ret < 0)
		{
			console_printf("%s; select() return value= %d ==> cont...; errno= %d ('%s')\n", __FUNCTION__, ret, errno, strerror(errno));
			continue;
		}

		for (i=0; i < numOfServices; i++)
		{
			if (!strncmp(dwpalService[i].interfaceType, "hostap", 7))
			{
				if (dwpalService[i].fd > 0)
				{
					if (FD_ISSET(dwpalService[i].fd, &rfds))
					{
						/*console_printf("%s; event received; interfaceType= '%s', radioName= '%s', serviceName= '%s'\n",
						       __FUNCTION__, dwpalService[i].interfaceType, dwpalService[i].radioName, dwpalService[i].serviceName);*/

						msg = (char *)malloc((size_t)(HOSTAPD_TO_DWPAL_MSG_LENGTH * sizeof(char)));
						if (msg == NULL)
						{
							console_printf("%s; invalid input ('msg') parameter ==> Abort!\n", __FUNCTION__);
							break;
						}

						memset(msg, 0, HOSTAPD_TO_DWPAL_MSG_LENGTH * sizeof(char));  /* Clear the output buffer */
						memset(opCode, 0, sizeof(opCode));
						msgLen = HOSTAPD_TO_DWPAL_MSG_LENGTH - 1;  //was "msgLen = HOSTAPD_TO_DWPAL_MSG_LENGTH;"

						if (dwpal_hostap_event_get(context[i], msg /*OUT*/, &msgLen /*IN/OUT*/, opCode /*OUT*/) == DWPAL_FAILURE)
						{
							console_printf("%s; dwpal_hostap_event_get ERROR; radioName= '%s', serviceName= '%s', msgLen= %d\n",
							       __FUNCTION__, dwpalService[i].radioName, dwpalService[i].serviceName, msgLen);
						}
						else
						{
							//console_printf("%s; msgLen= %d, msg= '%s'\n", __FUNCTION__, msgLen, msg);
#if 0
							{
								static int count = 0;

								switch (count)
								{
									case 0:
										strcpy(msg, "<3>AP-STA-CONNECTED wlan0 24:77:03:80:5d:90 SignalStrength=-49 SupportedRates=2 4 11 22 12 18 24 36 48 72 96 108 HT_CAP=107E HT_MCS=FF FF FF 00 00 00 00 00 00 00 C2 01 01 00 00 00 VHT_CAP=03807122 VHT_MCS=FFFA 0000 FFFA 0000 btm_supported=1 nr_enabled=0 non_pref_chan=81:200:1:5 non_pref_chan=81:100:2:9 non_pref_chan=81:200:1:7 non_pref_chan=81:100:2:5 cell_capa=1 assoc_req=00003A01000A1B0E04606C722002E833000A1B0E0460C04331060200000E746573745F737369645F69736172010882848B960C12182432043048606C30140100000FAC040100000FAC040100000FAC020000DD070050F2020001002D1AEF1903FFFFFF00000000000000000000000000000018040109007F080000000000000040BF0CB059C103EAFF1C02EAFF1C02C70122");
										strcpy(opCode, "AP-STA-CONNECTED");
										break;

									case 1:
										strcpy(msg, "<3>AP-STA-DISCONNECTED wlan0 14:d6:4d:ac:36:70");
										strcpy(opCode, "AP-STA-DISCONNECTED");
										break;

									case 2:
										strcpy(msg, "<3>AP-CSA-FINISHED wlan2 freq=5745 Channel=149 OperatingChannelBandwidt=80 ExtensionChannel=1 cf1=5775 cf2=0 reason=UNKNOWN dfs_chan=0");
										strcpy(opCode, "AP-CSA-FINISHED");
										break;

									case 3:
										strcpy(msg, "<3>ACS-COMPLETED wlan2 freq=2462 channel=11 OperatingChannelBandwidt=80 ExtensionChannel=1 cf1=5775 cf2=0 reason=UNKNOWN dfs_chan=0");
										strcpy(opCode, "ACS-COMPLETED");
										break;

									case 4:
										strcpy(msg, "<3>BSS-TM-RESP wlan2 e4:9a:79:d2:6b:0b dialog_token=5 status_code=6 bss_termination_delay=0 target_bssid=12:ab:34:cd:56:10");
										strcpy(opCode, "BSS-TM-RESP");
										break;

									case 5:
										strcpy(msg, "<3>DFS-CAC-COMPLETED wlan2 success=1 freq=5260 ht_enabled=0 chan_offset=0 chan_width=3 cf1=5290 cf2=0 timeout=10");
										strcpy(opCode, "DFS-CAC-COMPLETED");
										break;

									case 6:
										strcpy(msg, "<3>DFS-NOP-FINISHED wlan2 freq=5260 ht_enabled=1 chan_offset=0 chan_width=3 cf1=5290 cf2=0");
										strcpy(opCode, "DFS-NOP-FINISHED");
										break;

									case 7:
										strcpy(msg, "<3>RRM-BEACON-REP-RECEIVED wlan0 8c:70:5a:ed:55:40 dialog_token=1 measurement_rep_mode=0 op_class=128 channel=11 start_time=1234567892947293847 duration=50 frame_info=0F rcpi=DE rsni=AD bssid=d8:fe:e3:3e:bd:14 antenna_id=BE 33 parent_tsf=00012345 wide_band_ch_switch=1,1,1 timestamp=00 11 22 33 44 55 66 77 beacon_int=5 capab_info=88 99 aa bb cc ssid=dd ee ff 00 11 22 33 44 rm_capa=55 66 77 88 99 aa bb cc vendor_specific=aa bb cc dd ee ff 00 11 rsn_info=22 33 44 55 66 77 88 99");
										strcpy(opCode, "RRM-BEACON-REP-RECEIVED");
										break;

									case 8:
										strcpy(msg, "<3>UNCONNECTED-STA-RSSI wlan1 c0:c1:c0:68:a4:c9 rx_bytes=0 rx_packets=0 rssi=-128 -128 -128 -12 SNR=105 98 100 0 rate=15877");
										strcpy(opCode, "UNCONNECTED-STA-RSSI");
										break;

									default:
										break;

								}

								count++;
							}
#endif

							msgStringLen = strnlen_s(msg, HOSTAPD_TO_DWPAL_MSG_LENGTH);
							//console_printf("%s; opCode= '%s', msg= '%s'\n", __FUNCTION__, opCode, msg);
							if (strncmp(opCode, "", 1))
							{
								if (hostapdEventHandle(opCode, msg, msgStringLen) == DWPAL_FAILURE)
								{
									console_printf("%s; hostapdEventHandle (opCode= '%s') returned ERROR\n", __FUNCTION__, opCode);
								}
							}
						}

						free((void *)msg);
					}
				}
			}
			else if (!strncmp(dwpalService[i].interfaceType, "Driver", 7))
			{
				if ( (dwpalService[i].fd > 0) && (FD_ISSET(dwpalService[i].fd, &rfds)) )
				{
					/*console_printf("%s; [Driver] event received; interfaceType= '%s', radioName= '%s', serviceName= '%s', dwpalService[%d].fd= %d\n",
						   __FUNCTION__, dwpalService[i].interfaceType, dwpalService[i].radioName, dwpalService[i].serviceName, i, dwpalService[i].fd);*/

					if (dwpal_driver_nl_msg_get(context[i], DWPAL_NL_UNSOLICITED_EVENT, nlCliEventCallback) == DWPAL_FAILURE)
					{
						console_printf("%s; dwpal_driver_nl_msg_get ERROR; serviceName= '%s'\n", __FUNCTION__, dwpalService[i].serviceName);
					}
				}
				else if ( (dwpalService[i].fdCmdGet > 0) && (FD_ISSET(dwpalService[i].fdCmdGet, &rfds)) )
				{
					/*console_printf("%s; [Driver] event received; interfaceType= '%s', radioName= '%s', serviceName= '%s', dwpalService[%d].fdCmdGet= %d\n",
						   __FUNCTION__, dwpalService[i].interfaceType, dwpalService[i].radioName, dwpalService[i].serviceName, i, dwpalService[i].fdCmdGet);*/

					if (dwpal_driver_nl_msg_get(context[i], DWPAL_NL_SOLICITED_EVENT, nlCliCmdGetCallback) == DWPAL_FAILURE)
					{
						console_printf("%s; dwpal_driver_nl_msg_get ERROR; serviceName= '%s'\n", __FUNCTION__, dwpalService[i].serviceName);
					}
				}
			}
		}
	}

	return NULL;
}


static DWPAL_Ret listenerThreadCreate(void)
{
	int            ret;
	DWPAL_Ret      dwpalRet = DWPAL_SUCCESS;
	pthread_attr_t attr;
	size_t         stack_size = 4096;
	//void           *res;
	pthread_t      thread_id;

	console_printf("%s Entry\n", __FUNCTION__);

	ret = pthread_attr_init(&attr);
	if (ret != 0)
	{
		console_printf("%s; pthread_attr_init ERROR (ret= %d) ==> Abort!\n", __FUNCTION__, ret);
		return DWPAL_FAILURE;
	}

	ret = pthread_attr_setstacksize(&attr, stack_size);
	if (ret == -1)
	{
		console_printf("%s; pthread_attr_setstacksize ERROR (ret= %d) ==> Abort!\n", __FUNCTION__, ret);
		dwpalRet = DWPAL_FAILURE;
	}

	if (dwpalRet == DWPAL_SUCCESS)
	{
		console_printf("%s; call pthread_create\n", __FUNCTION__);
		ret = pthread_create(&thread_id, &attr, &listenerThreadStart, NULL /*can be used to send params*/);
		if (ret != 0)
		{
			console_printf("%s; pthread_create ERROR (ret= %d) ==> Abort!\n", __FUNCTION__, ret);
			dwpalRet = DWPAL_FAILURE;
		}
		console_printf("%s; return from call pthread_create, ret= %d\n", __FUNCTION__, ret);

		if (dwpalRet == DWPAL_SUCCESS)
		{
#if 0
			/* Causing the thread to be joined with the main process;
			   meaning, the process will suspend due to the thread suspend.
			   Otherwise, when process ends, the thread ends as well (although it is suspended and waiting ) */
			ret = pthread_join(thread_id, &res);
			if (ret != 0)
			{
				console_printf("%s; pthread_join ERROR (ret= %d) ==> Abort!\n", __FUNCTION__, ret);
				dwpalRet = DWPAL_FAILURE;
			}

			free(res);  /* Free memory allocated by thread */
#endif
		}
	}

	/* Destroy the thread attributes object, since it is no longer needed */
	ret = pthread_attr_destroy(&attr);
	if (ret != 0)
	{
		console_printf("%s; pthread_attr_destroy ERROR (ret= %d) ==> Abort!\n", __FUNCTION__, ret);
		dwpalRet = DWPAL_FAILURE;
	}

	return dwpalRet;
}


static void dwpal_init(void)
{
	int i, numOfServices = sizeof(dwpalService) / sizeof(DwpalService);

	/* Init the services */
	for (i=0; i < numOfServices; i++)
	{
		if (interfaceSet(&dwpalService[i], i) == DWPAL_SUCCESS)
		{
			console_printf("%s; interfaceSet (radioName= '%s', serviceName= '%s') successfully\n", __FUNCTION__, dwpalService[i].radioName, dwpalService[i].serviceName);
		}
	}

	/* Start the listener thread */
	if (listenerThreadCreate() == DWPAL_FAILURE)
	{
		console_printf("%s; listener thread failed ==> Abort!\n", __FUNCTION__);
		return;
	}
}


static void dwpal_cli_readline_callback(char *strLine)
{
	int                             i, idx;
	char                            *p2str, *opCode, *VAPName = NULL, *hostapCmdOpcode, *field;
	rsize_t                         dmaxLen;
	static bool                     isDwpalExtenderMode = false;
	enum nl80211_commands           nl80211Command;
	CmdIdType                       cmdIdType;
	enum ltq_nl80211_vendor_subcmds subCommand;
	unsigned char                   vendorData[128] = "\0";
	size_t                          vendorDataSize = 0;

	//console_printf("%s; strLine= '%s'; \n", __FUNCTION__, strLine);

	if (!strncmp(strLine, "", 1))
		return;

	/* Add the command to the history */
	idx = history_search_pos(strLine, 0, 0);
	if (idx != (-1))
	{
		HIST_ENTRY *entry = remove_history(idx);
		if (entry)
		{
			free (entry->line);
			free (entry);
		}
	}

	add_history(strLine);

	dmaxLen = (rsize_t)strnlen_s(strLine, DWPAL_CLI_LINE_STRING_LENGTH);
	opCode  = strtok_s(strLine, &dmaxLen, " ", &p2str);

	if (opCode == NULL)
	{
		console_printf("%s; opCode is NULL ==> Abort!\n", __FUNCTION__);
		return;
	}
	//console_printf("%s; opCode= '%s'\n", __FUNCTION__, opCode);

	/* Exit CLI */
    if (!strncmp(opCode, "exit", 4) || !strncmp(opCode, "quit", 4))
    {
		isCliRunning = false;
        return;
    }

	/* CLI Help */
    if ((opCode[0] == '?') || !strncmp(opCode, "help", 4))
    {
        dwpal_cli_show_help();
        return;
    }

	if (!strncmp(opCode, "DWPAL_INIT", strnlen_s("DWPAL_INIT", DWPAL_GENERAL_STRING_LENGTH)))
	{
		/* Format: DWPAL_INIT */
		console_printf("%s; call dwpal_init()\n", __FUNCTION__);
		isDwpalExtenderMode = false;
		dwpal_init();
	}
	else if (!strncmp(opCode, "DWPAL_EXT_DRIVER_NL_IF_ATTACH", strnlen_s("DWPAL_EXT_DRIVER_NL_IF_ATTACH", DWPAL_GENERAL_STRING_LENGTH)))
	{
		/* Format: DWPAL_EXT_DRIVER_NL_IF_ATTACH */
		if (dwpal_ext_driver_nl_attach(nlCliEventCallback) == DWPAL_FAILURE)
		{
			console_printf("%s; dwpal_ext_driver_nl_attach returned ERROR ==> Abort!\n", __FUNCTION__);
		}
		else
		{
			isDwpalExtenderMode = true;
		}
	}
	else if (!strncmp(opCode, "DWPAL_EXT_DRIVER_NL_IF_DETACH", strnlen_s("DWPAL_EXT_DRIVER_NL_IF_DETACH", DWPAL_GENERAL_STRING_LENGTH)))
	{
		/* Format: DWPAL_EXT_DRIVER_NL_IF_DETACH */
		if (dwpal_ext_driver_nl_detach() == DWPAL_FAILURE)
		{
			console_printf("%s; dwpal_ext_driver_nl_detach returned ERROR ==> Abort!\n", __FUNCTION__);
		}
		else
		{
			isDwpalExtenderMode = true;
		}
	}
	else
	{
		VAPName = strtok_s(NULL, &dmaxLen, " ", &p2str);
		if (VAPName == NULL)
		{
			console_printf("%s; VAPName is NULL ==> Abort!\n", __FUNCTION__);
			return;
		}
		console_printf("%s; VAPName= '%s'\n", __FUNCTION__, VAPName);

		if (VAPName != NULL)
		{
			if (!strncmp(opCode, "DWPAL_HOSTAP_CMD_SEND", strnlen_s("DWPAL_HOSTAP_CMD_SEND", DWPAL_GENERAL_STRING_LENGTH)))
			{
				/* Examples:
				   DWPAL_HOSTAP_CMD_SEND wlan2 STA_ALLOW d8:fe:e3:3e:bd:14
				   DWPAL_HOSTAP_CMD_SEND wlan2 DISASSOCIATE wlan2 d8:fe:e3:3e:bd:14
				   DWPAL_HOSTAP_CMD_SEND wlan0 GET_ACS_REPORT
				   DWPAL_HOSTAP_CMD_SEND wlan2 GET_RADIO_INFO
				   DWPAL_HOSTAP_CMD_SEND wlan0 GET_FAILSAFE_CHAN
				   DWPAL_HOSTAP_CMD_SEND wlan2 GET_RESTRICTED_CHANNELS
				   DWPAL_HOSTAP_CMD_SEND wlan2.1 GET_VAP_MEASUREMENTS
				   DWPAL_HOSTAP_CMD_SEND wlan2 STA_MEASUREMENTS 6C:72:20:02:E8:33
				   DWPAL_HOSTAP_CMD_SEND wlan2 REQ_BEACON 44:85:00:C5:6A:1B 0 0 0 255 1000 50 passive 00:0A:1B:0E:04:60 beacon_rep=1,123
				*/

				if ((idx = interfaceIndexGet("hostap", VAPName)) == -1)
				{
					console_printf("%s; interfaceIndexGet (radioName= '%s', serviceName= 'Both') returned ERROR ==> Abort!\n", __FUNCTION__, VAPName);
					return;
				}

				hostapCmdOpcode = strtok_s(NULL, &dmaxLen, " ", &p2str);
				if (hostapCmdOpcode == NULL)
				{
					console_printf("%s; hostapCmdOpcode is NULL ==> Abort!\n", __FUNCTION__);
					return;
				}

				if (!strncmp(hostapCmdOpcode, "GET_ACS_REPORT", strnlen_s("GET_ACS_REPORT", DWPAL_GENERAL_STRING_LENGTH)))
				{
					if (dwpal_acs_report_handle(context[idx], VAPName, isDwpalExtenderMode) == DWPAL_FAILURE)
					{
						console_printf("%s; dwpal_acs_report_get (VAPName= '%s', serviceName= '%s') returned ERROR ==> Abort!\n", __FUNCTION__, VAPName, dwpalService[idx].serviceName);
					}
				}
				else if (!strncmp(hostapCmdOpcode, "GET_RADIO_INFO", strnlen_s("GET_RADIO_INFO", DWPAL_GENERAL_STRING_LENGTH)))
				{
					if (dwpal_radio_info_handle(context[idx], VAPName, isDwpalExtenderMode) == DWPAL_FAILURE)
					{
						console_printf("%s; dwpal_radio_info_handle (VAPName= '%s', serviceName= '%s') returned ERROR ==> Abort!\n", __FUNCTION__, VAPName, dwpalService[idx].serviceName);
					}
				}
				else if (!strncmp(hostapCmdOpcode, "GET_FAILSAFE_CHAN", strnlen_s("GET_FAILSAFE_CHAN", DWPAL_GENERAL_STRING_LENGTH)))
				{
					if (dwpal_get_failsafe_channel_handle(context[idx], VAPName, isDwpalExtenderMode) == DWPAL_FAILURE)
					{
						console_printf("%s; dwpal_get_failsafe_channel_handle (VAPName= '%s', serviceName= '%s') returned ERROR ==> Abort!\n", __FUNCTION__, VAPName, dwpalService[idx].serviceName);
					}
				}
				else if (!strncmp(hostapCmdOpcode, "GET_RESTRICTED_CHANNELS", strnlen_s("GET_RESTRICTED_CHANNELS", DWPAL_GENERAL_STRING_LENGTH)))
				{
					if (dwpal_get_restricted_channels_handle(context[idx], VAPName, isDwpalExtenderMode) == DWPAL_FAILURE)
					{
						console_printf("%s; dwpal_get_restricted_channels_handle (VAPName= '%s', serviceName= '%s') returned ERROR ==> Abort!\n", __FUNCTION__, VAPName, dwpalService[idx].serviceName);
					}
				}
				else if (!strncmp(hostapCmdOpcode, "GET_VAP_MEASUREMENTS", strnlen_s("GET_VAP_MEASUREMENTS", DWPAL_GENERAL_STRING_LENGTH)))
				{
					if (dwpal_get_vap_measurements_handle(context[idx], VAPName, isDwpalExtenderMode) == DWPAL_FAILURE)
					{
						console_printf("%s; dwpal_get_vap_measurements_handle (VAPName= '%s', serviceName= '%s') returned ERROR ==> Abort!\n", __FUNCTION__, VAPName, dwpalService[idx].serviceName);
					}
				}
				else if (!strncmp(hostapCmdOpcode, "STA_MEASUREMENTS", strnlen_s("STA_MEASUREMENTS", DWPAL_GENERAL_STRING_LENGTH)))
				{
					if ( (field = strtok_s(NULL, &dmaxLen, " ", &p2str)) != NULL)
					{
						if (dwpal_get_sta_measurements_handle(context[idx], VAPName, field, isDwpalExtenderMode) == DWPAL_FAILURE)
						{
							console_printf("%s; dwpal_get_sta_measurements_handle (VAPName= '%s', serviceName= '%s', MACAddress= '%s') returned ERROR ==> Abort!\n", __FUNCTION__, VAPName, dwpalService[idx].serviceName, field);
						}
					}
					else
					{
						console_printf("%s; dwpal_get_sta_measurements_handle (VAPName= '%s', serviceName= '%s') no MAC Address ==> Abort!\n", __FUNCTION__, VAPName, dwpalService[idx].serviceName);
					}
				}
				else if (!strncmp(hostapCmdOpcode, "REQ_BEACON", strnlen_s("REQ_BEACON", DWPAL_GENERAL_STRING_LENGTH)))
				{
					char *fields[9];

					for (i=0; i < 9; i++)
					{
						fields[i] = strtok_s(NULL, &dmaxLen, " ", &p2str);
						if (fields[i] == NULL)
						{
							console_printf("%s; dwpal_req_beacon_handle (VAPName= '%s', serviceName= '%s') returned ERROR (i= %d) ==> Abort!\n", __FUNCTION__, VAPName, dwpalService[idx].serviceName, i);
							break;
						}
					}

					if (i >= 9)
					{
						if (dwpal_req_beacon_handle(context[idx], VAPName, fields, isDwpalExtenderMode) == DWPAL_FAILURE)
						{
							console_printf("%s; dwpal_req_beacon_handle (VAPName= '%s', serviceName= '%s') returned ERROR ==> Abort!\n", __FUNCTION__, VAPName, dwpalService[idx].serviceName);
						}
					}
				}
				else
				{
					char      cmd[DWPAL_TO_HOSTAPD_MSG_LENGTH];
					char      *reply = (char *)malloc((size_t)(HOSTAPD_TO_DWPAL_MSG_LENGTH * sizeof(char)));
					size_t    replyLen = HOSTAPD_TO_DWPAL_MSG_LENGTH * sizeof(char) - 1;
					DWPAL_Ret ret;

					if (reply == NULL)
					{
						console_printf("%s; malloc (for reply) ERROR ==> Abort!\n", __FUNCTION__);
					}
					else
					{
						snprintf(cmd, DWPAL_TO_HOSTAPD_MSG_LENGTH, "%s", hostapCmdOpcode);

						while ( (field = strtok_s(NULL, &dmaxLen, " ", &p2str)) != NULL )
						{
							snprintf(cmd, DWPAL_TO_HOSTAPD_MSG_LENGTH, "%s %s", cmd, field);
						}

						if (isDwpalExtenderMode)
						{
							ret = dwpal_ext_hostap_cmd_send(VAPName, cmd, NULL, reply, &replyLen);
						}
						else
						{
							ret = dwpal_hostap_cmd_send(context[idx], cmd, NULL, reply, &replyLen);
						}

						if (ret == DWPAL_FAILURE)
						{
							console_printf("%s; dwpal_hostap_cmd_send (VAPName= '%s', cmd= '%s') returned ERROR (reply= '%s') ==> Abort!\n", __FUNCTION__, VAPName, cmd, reply);
						}
						else
						{
							console_printf("%s; replyLen= %d, reply= '%s'\n", __FUNCTION__, replyLen, reply);
						}
					}
				}
			}
			else if (!strncmp(opCode, "DWPAL_EXT_HOSTAP_IF_ATTACH", strnlen_s("DWPAL_EXT_HOSTAP_IF_ATTACH", DWPAL_GENERAL_STRING_LENGTH)))
			{
				/* Format: DWPAL_EXT_HOSTAP_IF_ATTACH */
				if ((idx = interfaceIndexGet("hostap", VAPName)) == -1)
				{
					console_printf("%s; interfaceIndexGet (radioName= '%s', serviceName= 'Both') returned ERROR ==> Abort!\n", __FUNCTION__, VAPName);
					return;
				}

				if (dwpal_ext_hostap_interface_attach(VAPName, dwpalExtEventCallback) == DWPAL_FAILURE)
				{
					console_printf("%s; dwpal_ext_hostap_interface_attach returned ERROR (VAPName= '%s') ==> Abort!\n", __FUNCTION__, VAPName);
				}
				else
				{
					isDwpalExtenderMode = true;
				}
			}
			else if (!strncmp(opCode, "DWPAL_EXT_HOSTAP_IF_DETACH", strnlen_s("DWPAL_EXT_HOSTAP_IF_DETACH", DWPAL_GENERAL_STRING_LENGTH)))
			{
				/* Format: DWPAL_EXT_HOSTAP_IF_DETACH */
				if ((idx = interfaceIndexGet("hostap", VAPName)) == -1)
				{
					console_printf("%s; interfaceIndexGet (radioName= '%s', serviceName= 'Both') returned ERROR ==> Abort!\n", __FUNCTION__, VAPName);
					return;
				}

				if (dwpal_ext_hostap_interface_detach(VAPName) == DWPAL_FAILURE)
				{
					console_printf("%s; dwpal_ext_hostap_interface_detach returned ERROR (VAPName= '%s') ==> Abort!\n", __FUNCTION__, VAPName);
				}
			}
			else if ( (!strncmp(opCode, "DWPAL_DRIVER_NL_CMD_SEND", strnlen_s("DWPAL_DRIVER_NL_CMD_SEND", DWPAL_GENERAL_STRING_LENGTH))) ||
			          (!strncmp(opCode, "DWPAL_DRIVER_NL_GET", strnlen_s("DWPAL_DRIVER_NL_GET", DWPAL_GENERAL_STRING_LENGTH))) )
			{
				/* Examples:
				   iw wlan2 iwlwav g11hRadarDetect
				   { nl80211Command = NL80211_CMD_VENDOR=0x67}, { cmdIdType = DWPAL_NETDEV_ID=0 }, { sub_command=0x6b - LTQ_NL80211_VENDOR_SUBCMD_GET_11H_RADAR_DETECT = 107 (=0x6b) }
				   DWPAL_DRIVER_NL_GET wlan0 67 0 6b 0

				   iw dev wlan0 gScanParam
				   { nl80211Command = NL80211_CMD_VENDOR=0x67}, { cmdIdType = DWPAL_NETDEV_ID=0 }, { sub_command=0xc2 - LTQ_NL80211_VENDOR_SUBCMD_GET_SCAN_PARAMS = 194 (=0xc2) }
				   DWPAL_DRIVER_NL_GET wlan0 67 0 c2 0

				   iw dev wlan0 gScanParamBG
				   { nl80211Command = NL80211_CMD_VENDOR=0x67}, { cmdIdType = DWPAL_NETDEV_ID=0 }, { sub_command=0xc2 - LTQ_NL80211_VENDOR_SUBCMD_GET_SCAN_PARAMS_BG = 196 (=0xc4) }
				   DWPAL_DRIVER_NL_GET wlan0 67 0 c4 0

				   iw wlan2 iwlwav g11hRadarDetect
				   { nl80211Command = NL80211_CMD_VENDOR=0x67}, { cmdIdType = DWPAL_NETDEV_ID=0 }, { sub_command=0x6b - LTQ_NL80211_VENDOR_SUBCMD_GET_11H_RADAR_DETECT = 107 (=0x6b) }
				   DWPAL_DRIVER_NL_CMD_SEND wlan0 67 0 6b

				   iw wlan2 iwlwav s11hRadarDetect
				   { nl80211Command = NL80211_CMD_VENDOR=0x67}, { cmdIdType = DWPAL_NETDEV_ID=0 }, { sub_command=0x6a - LTQ_NL80211_VENDOR_SUBCMD_SET_11H_RADAR_DETECT = 106 (=0x6a) }
				   "4" = sizeof(int), "0 0 0 0" or "0 0 0 1" is the integer broken into 4 characters
				   DWPAL_DRIVER_NL_CMD_SEND wlan0 67 0 6a 4 0 0 0 1
				*/

				field = strtok_s(NULL, &dmaxLen, " ", &p2str);
				if (field == NULL)
				{
					console_printf("%s; nl80211Command is NULL ==> Abort!\n", __FUNCTION__);
					return;
				}
				nl80211Command = (enum nl80211_commands)strtol(field, NULL, 16);

				field = strtok_s(NULL, &dmaxLen, " ", &p2str);
				if (field == NULL)
				{
					console_printf("%s; cmdIdType is NULL ==> Abort!\n", __FUNCTION__);
					return;
				}
				cmdIdType = (CmdIdType)atoi(field);

				field = strtok_s(NULL, &dmaxLen, " ", &p2str);
				if (field == NULL)
				{
					console_printf("%s; subCommand is NULL ==> Abort!\n", __FUNCTION__);
					return;
				}
				subCommand = (enum ltq_nl80211_vendor_subcmds)strtol(field, NULL, 16);

				field = strtok_s(NULL, &dmaxLen, " ", &p2str);
				if (field == NULL)
				{
					console_printf("%s; vendorDataSize is NULL ==> cont...\n", __FUNCTION__);
				}
				else
				{
					vendorDataSize = (size_t)atoi(field);
					if (vendorDataSize > (sizeof(vendorData) / sizeof(unsigned char)))
					{
						console_printf("%s; vendorDataSize (%d) bigger than sizeof(vendorData) (%d) ==> Abort!\n", __FUNCTION__, vendorDataSize, sizeof(vendorData));
						return;
					}

					for (i=0; i < (int)vendorDataSize; i++)
					{
						field = strtok_s(NULL, &dmaxLen, " ", &p2str);
						if (field == NULL)
						{
							console_printf("%s; vendorData[%d] is NULL ==> Abort!\n", __FUNCTION__, i);
							return;
						}

						vendorData[i] = (unsigned char)strtol(field, NULL, 16);
					}
				}

				if (isDwpalExtenderMode)
				{
					if (strncmp(opCode, "DWPAL_DRIVER_NL_GET", strnlen_s("DWPAL_DRIVER_NL_GET", DWPAL_GENERAL_STRING_LENGTH)))
					{  /*DWPAL_DRIVER_NL_CMD_SEND*/
						if (dwpal_ext_driver_nl_cmd_send(VAPName, nl80211Command, cmdIdType, subCommand, vendorData, vendorDataSize) == DWPAL_FAILURE)
						{
							console_printf("%s; dwpal_ext_driver_nl_cmd_send returned ERROR ==> Abort!\n", __FUNCTION__);
						}
					}
					else
					{  /*DWPAL_DRIVER_NL_GET*/
						size_t        outLen, lenToPrint;
						unsigned char *outData = (unsigned char *)malloc(1024);

						if (outData == NULL)
						{
							console_printf("%s; outData malloc ERROR ==> Abort!\n", __FUNCTION__);
						}
						else
						{
							memset(outData, '\0', 1024);
							if (dwpal_ext_driver_nl_get(VAPName, nl80211Command, cmdIdType, subCommand, vendorData, vendorDataSize, &outLen, outData) == DWPAL_FAILURE)
							{
								console_printf("%s; dwpal_ext_driver_nl_get returned ERROR ==> Abort!\n", __FUNCTION__);
							}

							lenToPrint = (outLen <= 10)? outLen : 10;

							console_printf("%s; Output data from the 'get' function:\n", __FUNCTION__);
							for (i=0; i < (int)lenToPrint; i++)
							{
								console_printf(" 0x%x", outData[i]);
							}
							console_printf("\n");

							free ((void *)outData);
						}
					}
				}
				else
				{
					if ((idx = interfaceIndexGet("Driver", "ALL")) == -1)
					{
						console_printf("%s; interfaceIndexGet (radioName= 'wlan0', serviceName= 'Both') returned ERROR ==> Abort!\n", __FUNCTION__);
						return;
					}

					console_printf("%s; idx= %d\n", __FUNCTION__, idx);

					if (dwpal_driver_nl_cmd_send(context[idx], DWPAL_NL_UNSOLICITED_EVENT, VAPName, nl80211Command, cmdIdType, subCommand, vendorData, vendorDataSize) == DWPAL_FAILURE)
					{
						console_printf("%s; dwpal_driver_nl_cmd_send (VAPName= '%s', serviceName= '%s') returned ERROR ==> Abort!\n", __FUNCTION__, VAPName, dwpalService[idx].serviceName);
					}
				}
			}
			else if (!strncmp(opCode, "DWPAL_DRIVER_NL_SCAN_TRIGGER", strnlen_s("DWPAL_DRIVER_NL_SCAN_TRIGGER", DWPAL_GENERAL_STRING_LENGTH)))
			{
				/* Usage:
				   iw wlan0 scan trigger [freq <freq>*] [ies <hex as 00:11:..>] [meshid <meshid>] [lowpri,flush,ap-force] [randomise[=<addr>/<mask>]] [ssid <ssid>*|passive]
				   { nl80211Command = NL80211_CMD_TRIGGER_SCAN=0x21}
				   DWPAL_DRIVER_NL_SCAN_TRIGGER wlan0
				   DWPAL_DRIVER_NL_SCAN_TRIGGER wlan2 freq=5180 freq=5200 ies=0:1:2 meshid=abcdef lowpri=false flush=false ap_force=false ssid=test_ssid_0 ssid=test_ssid_1 passive=true
				   (When 'randomise' will be supported): DWPAL_DRIVER_NL_SCAN_TRIGGER wlan2 freq=5180 freq=5200 ies=0:1:2 meshid=abcdef lowpri=false flush=false ap_force=false randomise=00:11:22:33:44:55/ff:ff:ff:ff:ff:ff ssid=test_ssid_0 ssid=test_ssid_1 passive=true
				*/

				char *strEndOfLine = strtok_s(NULL, &dmaxLen, "\n", &p2str);
				ScanParams scanParams, *scanParamsPtr = NULL;

				console_printf("%s; strEndOfLine= '%s'\n", __FUNCTION__, strEndOfLine);

				if (strEndOfLine != NULL)
				{
					scanParametersGet(strEndOfLine, &scanParams);
					scanParamsPtr = &scanParams;
				}

				console_printf("%s; scanParamsPtr= 0x%x\n", __FUNCTION__, (unsigned int)scanParamsPtr);

				if (isDwpalExtenderMode)
				{
					if (dwpal_ext_driver_nl_scan_trigger(VAPName, scanParamsPtr) == DWPAL_FAILURE)
					{
						console_printf("%s; dwpal_ext_driver_nl_scan_trigger returned ERROR ==> Abort!\n", __FUNCTION__);
					}
				}
				else
				{
					if ((idx = interfaceIndexGet("Driver", "ALL")) == -1)
					{
						console_printf("%s; interfaceIndexGet (radioName= 'wlan0', serviceName= 'Both') returned ERROR ==> Abort!\n", __FUNCTION__);
						return;
					}

					console_printf("%s; idx= %d\n", __FUNCTION__, idx);

					if (dwpal_driver_nl_scan_trigger(context[idx], VAPName, scanParamsPtr) == DWPAL_FAILURE)
					{
						console_printf("%s; dwpal_driver_nl_scan_trigger returned ERROR ==> Abort!\n", __FUNCTION__);
					}
				}
			}
			else if (!strncmp(opCode, "DWPAL_DRIVER_NL_SCAN_DUMP", strnlen_s("DWPAL_DRIVER_NL_SCAN_DUMP", DWPAL_GENERAL_STRING_LENGTH)))
			{
				/* Usage:
				   iw wlan0 scan dump
				   { nl80211Command = NL80211_CMD_GET_SCAN=0x20}
				   DWPAL_DRIVER_NL_SCAN_DUMP wlan0
				*/

				if (isDwpalExtenderMode)
				{
					if (dwpal_ext_driver_nl_scan_dump(VAPName, nlCliNonVendorEventCallback) == DWPAL_FAILURE)
					{
						console_printf("%s; dwpal_ext_driver_nl_scan_dump returned ERROR ==> Abort!\n", __FUNCTION__);
					}
				}
				else
				{
					if ((idx = interfaceIndexGet("Driver", "ALL")) == -1)
					{
						console_printf("%s; interfaceIndexGet (radioName= 'wlan0', serviceName= 'Both') returned ERROR ==> Abort!\n", __FUNCTION__);
						return;
					}

					console_printf("%s; idx= %d\n", __FUNCTION__, idx);

					if (dwpal_driver_nl_scan_dump(context[idx], VAPName, nlCliNonVendorEventCallback) == DWPAL_FAILURE)
					{
						console_printf("%s; dwpal_driver_nl_scan_dump returned ERROR ==> Abort!\n", __FUNCTION__);
					}
				}
			}
		}
	}
}


static void dwpalDebugCliStart(void)
{
	int    i, res, numOfServices = sizeof(dwpalService) / sizeof(DwpalService);
	fd_set rfds;

	console_printf("%s Entry\n", __FUNCTION__);

    /* Init signals */
    init_signals();

    /* Read history file */
    read_history("/tmp/dwpal_cli_history");

	/* Readline completion function */
	rl_completion_entry_function = dwpal_cli_tab_completion_entry;
	rl_attempted_completion_function = dwpal_cli_tab_completion;

	/* Enable TAB auto-complete */
	rl_bind_key('\t', rl_complete);

	/* Register readline handler */
	rl_callback_handler_install("(DwpalCLI)>> ", dwpal_cli_readline_callback);

	/* Main event loop */
	while (isCliRunning)
	{
		FD_ZERO(&rfds);
		FD_SET(STDIN_FILENO, &rfds);

		res = select(STDIN_FILENO + 1, &rfds, NULL, NULL, NULL);
		if (res < 0)
		{
			console_printf("%s; select() return value= %d ==> cont...; errno= %d ('%s')\n", __FUNCTION__, res, errno, strerror(errno));
			break;
		}

		if (FD_ISSET(STDIN_FILENO, &rfds))
		{
			rl_callback_read_char();
		}
	}

	console_printf("\n");

	/* DeInit the services - only in case of using non-dwpal-ext mode */
	for (i=0; i < numOfServices; i++)
	{
		if (dwpalService[i].fd != -1)
		{
			if (interfaceReset(&dwpalService[i], i) == DWPAL_SUCCESS)
			{
				console_printf("%s; interfaceReset (radioName= '%s', serviceName= '%s') successfully\n",
				            __FUNCTION__, dwpalService[i].radioName, dwpalService[i].serviceName);
			}
		}
	}

    /* Save history file */
	write_history("/tmp/dwpal_cli_history");

	/* Cleanup */
	console_printf("%s; D-WPAL Debug CLI cleanup...\n", __FUNCTION__);
	rl_callback_handler_remove();

	console_printf("%s; Bye!\n", __FUNCTION__);
}


int main(int argc, char *argv[])
{
	/* Usage:
	   1) Interactive mode:
	   dwpal_cli

	   Examples:
			Start dwpal:
			(DwpalCLI)>> DWPAL_INIT

			Start dwpal_ext:
			(DwpalCLI)>> DWPAL_EXT_HOSTAP_IF_ATTACH wlan2
			(DwpalCLI)>> DWPAL_EXT_DRIVER_NL_IF_ATTACH

	   (DwpalCLI)>> GET_VAP_MEASUREMENTS wlan0
	   (DwpalCLI)>> DWPAL_DRIVER_NL_SCAN_TRIGGER wlan0
	   (DwpalCLI)>> DWPAL_DRIVER_NL_SCAN_TRIGGER wlan2 freq=5180 freq=5200 ies=0:1:2 meshid=abcdef lowpri=false flush=false ap_force=false ssid=test_ssid_0 ssid=test_ssid_1 passive=true
	   (When 'randomise' will be supported): (DwpalCLI)>> DWPAL_DRIVER_NL_SCAN_TRIGGER wlan2 freq=5180 freq=5200 ies=0:1:2 meshid=abcdef lowpri=false flush=false ap_force=false randomise=00:11:22:33:44:55/ff:ff:ff:ff:ff:ff ssid=test_ssid_0 ssid=test_ssid_1 passive=true
	   (DwpalCLI)>> DWPAL_DRIVER_NL_SCAN_DUMP wlan0

	   2) One-shot mode examples:
	   dwpal_cli -ihostap -vwlan0 -c"GET_VAP_MEASUREMENTS wlan0"
	   dwpal_cli -ihostap -vwlan2 -c"GET_VAP_MEASUREMENTS wlan2"

	   Start endless listener for both wlan0 & wlan2:
	   dwpal_cli -ihostap -vwlan0 -vwlan2 &

	   Start listener for wlan0 up until "AP-STA-CONNECTED" will be received:
	   dwpal_cli -ihostap -vwlan0 -l"AP-STA-CONNECTED"

	   Start listener for wlan0 up until "AP-STA-DISCONNECTED" or "AP-STA-CONNECTED" will be received:
	   dwpal_cli -ihostap -vwlan0 -l"AP-STA-DISCONNECTED" -l"AP-STA-CONNECTED"

	   Start endless NL socket listener
	   dwpal_cli -iDriver &

	   Start NL socket listener up until "FW_DUMP_READY" will be received for "wlan0":
	   dwpal_cli -iDriver -v"wlan0" -l"FW_DUMP_READY"

	   Start endless listener for both wlan0 & wlan2:
	   dwpal_cli -ihostap -vwlan0 -vwlan2 -iDriver &

	   Send NL socket command (without data) and wait up until a response will be received:
	   dwpal_cli -iDriver -vwlan0 -c107

	   Send NL socket command (with data, without reply via the callback):
	   dwpal_cli -iDriver -vwlan0 -c106 -d"0 0 0 1"

	   Send NL socket "scan trigger" command (iw wlan0 scan trigger):
	   dwpal_cli -iDriver -vwlan0 -c33
	   dwpal_cli -iDriver -vwlan0 -c33 -p"freq=5180 freq=5200"
	   dwpal_cli -iDriver -vwlan0 -c33 -p"freq=5180 freq=5200 ies=0:1:2 meshid=abcdef lowpri=false flush=false ap_force=false ssid=test_ssid_0 ssid=test_ssid_1 passive=true"
	   (When 'randomise' will be supported): dwpal_cli -iDriver -vwlan0 -c33 -p"freq=5180 freq=5200 ies=0:1:2 meshid=abcdef lowpri=false flush=false ap_force=false randomise=00:11:22:33:44:55/ff:ff:ff:ff:ff:ff ssid=test_ssid_0 ssid=test_ssid_1 passive=true"

	   Send NL socket "scan dump" command (iw wlan0 scan dump):
	   dwpal_cli -iDriver -vwlan0 -c32
	*/

	int           i, arrayIdx, option = 0;
	char          *tempString;
	char          interfaceType[DWPAL_GENERAL_STRING_LENGTH] = "\0";
	char          vendorDataString[512] = "\0", *vendorDataTempString, *reply, *p2str;
	unsigned char vendorData[128] = "\0";
	size_t        stringLength, vendorDataSize = 0, replyLen = HOSTAPD_TO_DWPAL_MSG_LENGTH * sizeof(char) - 1;
	rsize_t       dmaxLen;
	extern char   *optarg;
	ScanParams    scanParams, *scanParamsPtr = NULL;

	console_printf("D-WPAL Debug CLI Function Entry; argc= %d, argv[0]= '%s', argv[1]= '%s', argv[2]= '%s'\n", argc, argv[0], argv[1], argv[2]);

	if (argc == 1)
	{  /* Interactive mode */
		dwpalDebugCliStart();  /* Start the CLI */
	}
	else
	{  /* one-shot mode */
		numOfListeningEvents = 0;
		while ( ((option = getopt(argc, argv, "i:v:c:l:d:p:")) != -1) && (optarg != NULL) )
		{
			console_printf("option= %d, optarg= '%s'\n", option, optarg);

			switch (option)
			{
				case 'i':
					stringLength = strnlen_s(optarg, DWPAL_GENERAL_STRING_LENGTH);
					strncpy_s(interfaceType, sizeof(interfaceType), optarg, stringLength);
					interfaceType[stringLength] = '\0';
					console_printf("interfaceType= '%s'\n", interfaceType);

					if (interfaceType[0] != '\0')
					{
						if (!strncmp(interfaceType, "Driver", 7))
						{
							if (dwpal_ext_driver_nl_attach(nlCliOneShotEventCallback) == DWPAL_FAILURE)
							{
								console_printf("%s; dwpal_ext_driver_nl_attach returned ERROR ==> Abort!\n", __FUNCTION__);
								return -1;
							}
						}
					}
					break;

				case 'v':
					stringLength = strnlen_s(optarg, DWPAL_VAP_NAME_STRING_LENGTH);
					strncpy_s(oneShotVAPName, sizeof(oneShotVAPName), optarg, stringLength);
					oneShotVAPName[stringLength] = '\0';
					console_printf("oneShotVAPName= '%s'\n", oneShotVAPName);

					if (interfaceType[0] != '\0')
					{
						if (!strncmp(interfaceType, "hostap", 7))
						{
							if (oneShotVAPName[0] != '\0')
							{
								if (dwpal_ext_hostap_interface_attach(oneShotVAPName, dwpalOneShotEventCallback) == DWPAL_FAILURE)
								{
									console_printf("%s; dwpal_ext_hostap_interface_attach returned ERROR (oneShotVAPName= '%s') ==> Abort!\n", __FUNCTION__, oneShotVAPName);
									return -1;
								}
							}
						}
					}
					break;

				case 'c':
					stringLength = strnlen_s(optarg, DWPAL_TO_HOSTAPD_MSG_LENGTH);
					strncpy_s(oneShotCommand, sizeof(oneShotCommand), optarg, stringLength);
					oneShotCommand[stringLength] = '\0';
					console_printf("oneShotCommand= '%s'\n", oneShotCommand);
					break;

				case 'l':
					if (numOfListeningEvents >= NUM_OF_LISTENING_EVENTS)
					{
						console_printf("Ignore opCodeForListener ('%s') ==> number of events reached the limit (%d)\n", optarg, NUM_OF_LISTENING_EVENTS);
						break;
					}

					stringLength = strnlen_s(optarg, DWPAL_OPCODE_STRING_LENGTH);
					strncpy_s(opCodeForListener[numOfListeningEvents], sizeof(opCodeForListener[numOfListeningEvents]), optarg, stringLength);
					opCodeForListener[numOfListeningEvents][stringLength] = '\0';
					console_printf("opCodeForListener[%d]= '%s'\n", numOfListeningEvents, opCodeForListener[numOfListeningEvents]);
					numOfListeningEvents++;
					break;

				case 'd':
					stringLength = strnlen_s(optarg, 512);
					strncpy_s(vendorDataString, sizeof(vendorDataString), optarg, stringLength);
					vendorDataString[stringLength] = '\0';
					console_printf("vendorDataString= '%s'\n", vendorDataString);

					arrayIdx = 0;
					vendorDataTempString = vendorDataString;
					dmaxLen = (rsize_t)stringLength;
					console_printf("dmaxLen= %d\n", dmaxLen);
					while ( (tempString = strtok_s(vendorDataTempString, &dmaxLen, " ", &p2str)) != NULL )
					{
						console_printf("tempString[%d]= '%s'\n", arrayIdx, tempString);
						vendorData[arrayIdx++] = atoi(tempString);
						vendorDataTempString = NULL;
					}

					vendorDataSize = arrayIdx;

					console_printf("arrayIdx= %d\n", arrayIdx);
					for (i=0; i < arrayIdx; i++)
					{
						console_printf("vendorData[%d]= %d\n", i, vendorData[i]);
					}
					break;

				case 'p':
				{
					char *strEndOfLine = optarg;

					scanParamsPtr = NULL;
					if (strEndOfLine != NULL)
					{
						scanParametersGet(strEndOfLine, &scanParams);
						scanParamsPtr = &scanParams;
					}

					console_printf("%s; scanParamsPtr= 0x%x\n", __FUNCTION__, (unsigned int)scanParamsPtr);
				}
					break;

				default:
					console_printf("Invalid option (%d) ==> Abort!\n", option);
					return -1;
					break;
			}
		}

		console_printf("interfaceType= '%s', oneShotVAPName= '%s', oneShotCommand= '%s'\n", interfaceType, oneShotVAPName, oneShotCommand);
		if (interfaceType[0] != '\0')
		{
			if (!strncmp(interfaceType, "hostap", 7))
			{
				if (oneShotVAPName[0] != '\0')
				{
					reply = (char *)malloc((size_t)(HOSTAPD_TO_DWPAL_MSG_LENGTH * sizeof(char)));
					if (reply == NULL)
					{
						console_printf("%s; malloc (for reply) ERROR ==> Abort!\n", __FUNCTION__);
						return -1;
					}

					if (oneShotCommand[0] != '\0')
					{
						if (dwpal_ext_hostap_cmd_send(oneShotVAPName, oneShotCommand, NULL, reply, &replyLen) == DWPAL_FAILURE)
						{
							console_printf("%s; dwpal_hostap_cmd_send (oneShotVAPName= '%s', oneShotCommand= '%s') returned ERROR (reply= '%s') ==> Abort!\n", __FUNCTION__, oneShotVAPName, oneShotCommand, reply);
							free((void *)reply);
							return -1;
						}

						console_printf("%s; replyLen= %d\nresponse=\n%s\n", __FUNCTION__, replyLen, reply);
					}

					free((void *)reply);
				}
			}
			else if (!strncmp(interfaceType, "Driver", 7))
			{
				if ( (oneShotVAPName[0] != '\0') && (oneShotCommand[0] != '\0') )
				{
					if (vendorDataSize == 0)
					{  /* 'Driver' with command to send but without any data ==> 'get command' */
						size_t        outLen, lenToPrint;
						int           oneShotCommandVal = (unsigned int)atoi(oneShotCommand);
						unsigned char *outData;

						console_printf("%s; oneShotCommandVal= %d (0x%x)\n", __FUNCTION__, oneShotCommandVal, oneShotCommandVal);

						if (oneShotCommandVal == NL80211_CMD_TRIGGER_SCAN /*0x21*/)
						{
							if (dwpal_ext_driver_nl_scan_trigger(oneShotVAPName, scanParamsPtr) != DWPAL_SUCCESS)
							{
								console_printf("%s; dwpal_ext_driver_nl_scan_trigger returned ERROR ==> Abort!\n", __FUNCTION__);
							}
						}
						else if (oneShotCommandVal == NL80211_CMD_GET_SCAN /*0x20*/)
						{
							if (dwpal_ext_driver_nl_scan_dump(oneShotVAPName, nlCliNonVendorEventCallback) != DWPAL_SUCCESS)
							{
								console_printf("%s; dwpal_ext_driver_nl_scan_dump returned ERROR ==> Abort!\n", __FUNCTION__);
							}
						}
						else
						{
							outData = (unsigned char *)malloc(1024);

							if (outData == NULL)
							{
								console_printf("%s; outData malloc ERROR ==> Abort!\n", __FUNCTION__);
							}
							else
							{
								memset(outData, '\0', 1024);
								if (dwpal_ext_driver_nl_get(oneShotVAPName, NL80211_CMD_VENDOR, DWPAL_NETDEV_ID, (unsigned int)atoi(oneShotCommand), vendorData, vendorDataSize, &outLen, outData) == DWPAL_FAILURE)
								{
									console_printf("%s; dwpal_ext_driver_nl_get returned ERROR ==> Abort!\n", __FUNCTION__);
								}

								lenToPrint = (outLen <= 10)? outLen : 10;

								console_printf("%s; Output data from the 'get' function:\n", __FUNCTION__);
								for (i=0; i < (int)lenToPrint; i++)
								{
									console_printf(" 0x%x", outData[i]);
								}
								console_printf("\n");

								free ((void *)outData);
							}
						}
					}
					else
					{
						if (dwpal_ext_driver_nl_cmd_send(oneShotVAPName, NL80211_CMD_VENDOR, DWPAL_NETDEV_ID, (unsigned int)atoi(oneShotCommand), vendorData, vendorDataSize) == DWPAL_FAILURE)
						{
							console_printf("%s; dwpal_ext_driver_nl_cmd_send returned ERROR ==> Abort!\n", __FUNCTION__);
						}
					}
				}
			}
		}
		else
		{
			if ( check_stats_cmd(argc-1, &argv[1]) )
			{
				console_printf("stats command failed\n");
				return -1;
			}
		}

		if ( ( (!strncmp(interfaceType, "hostap", 7)) && (oneShotCommand[0] == '\0') ) ||  /* 'hostap' without any command to send */
		     ( (!strncmp(interfaceType, "Driver", 7)) && (oneShotCommand[0] == '\0') ) )   /* 'Driver' without any command to send */
		{
			while (isContinueListening)
			{
				sleep(1);
			}
		}
	}

	return 0;
}
