#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <uci.h>
#include <stdlib.h>
#include "dwpal.h"
#include "dwpal_ext.h"
#include "stats.h"
#include "dwpal_log.h"	//Logging

#if defined YOCTO
#include <slibc/string.h>
#else
#include "safe_str_lib.h"
#endif

/* global declaration */
void *goutData = NULL; // for traversing NL data
int gSpace = 0; // for indentation
int gEnum = 0; // to find enum stat
#define MAX_STR_SIZE 32

static void *convert_str_to_lower(char *src, char *dst)
{
	int i;
	for ( i = 0; i < strnlen_s(src, MAX_STR_SIZE); i++ )
	{
		if( src[i] >= 'A' && src[i] <= 'Z' )
			dst[i] = src[i] - 'A' + 'a';
		else
			dst[i] = src[i];
	}
}

static void convertMac(char *sMac, unsigned char *cMac)
{
	int i;
	for (i = 0; i < 6; i++) {
		cMac[i] = (unsigned char) strtoul(sMac, &sMac, 16);
		sMac++;
	}
}

static unsigned int stat_ipow(unsigned int base, unsigned int power)
{
  unsigned int i;
  unsigned int res = 1;

  for (i = 0; i < power; i++)
  {
    res *= base;
  }

  return res;
}

static void print_type(type t, char *description, int len)
{
	int i, tmp = gSpace;
	if ( t != BITFIELD  && t != ENUM )
		INDENTATION(tmp)
	switch (t)
	{
		case BYTE:
		{
			printf("%20u : %s\n", *((unsigned char *)goutData) , description);
			goutData = (void *)((unsigned int)goutData + sizeof(unsigned char));
		}
		break;
	
		case SLONG:
		{
			printf("%20i : %s\n", *((signed int *) goutData), description);
			goutData = (void *)((unsigned int)goutData + sizeof(signed int));
		}
		break;

		case LONG:
		{
			printf("%20u : %s\n", *((unsigned int *) goutData), description);
			goutData = (void *)((unsigned int)goutData + sizeof(unsigned int));
		}
 		break;

		case SBYTEARRAY:
		{
			printf("%20s : %s\n","",description);
			for (i = 0; i < len; i++)
			{
				tmp = gSpace;
				INDENTATION(tmp)
				printf("%20i : [%d]\n",*((signed char *) goutData),i);
				goutData = (void *)((unsigned int)goutData + sizeof(signed char));
			}
		}
		break;

		case SLONGARRAY:
		{
			printf("%20s : %s\n","",description);
			for (i = 0; i < len; i++)
			{
				tmp = gSpace;
				INDENTATION(tmp)
				printf("%20i : [%d]\n",*((signed int *) goutData),i);
				goutData = (void *)((unsigned int)goutData + sizeof(signed int));
			}
		}
		break;

		case BITFIELD:
		{
			tmp = gSpace;
			if ( len )
				goutData = (void *)((unsigned int)goutData - sizeof(unsigned int));
			if (  ( *((unsigned  int *) goutData) & (0x01<<len) )) {
				INDENTATION(tmp);
				printf("%20s : \n",description);
			}
		}
		break;

		case FLAG:
		{
			printf("%20s : %s\n", *((unsigned  int *) goutData)? "True" : "False",description);
			goutData = (void *)((unsigned int)goutData + sizeof(unsigned int));
		}
		break;

		case ENUM:
		{
			if (len)
			{
				goutData = (void *)((unsigned int)goutData - sizeof(unsigned int));
			}

			if (*((unsigned  int *) goutData) == (unsigned int)len) { 
				tmp = gSpace;
				INDENTATION(tmp)
				gEnum = 1;
				printf("%20s : ", description);
			}
			
		}
		break;

		case TIMESTAMP:
		{
			tmp = gSpace;
			INDENTATION(tmp)
			printf("%11d msec ago : %s\n",*((unsigned int *)goutData),description);
			goutData = (void *)((unsigned int)goutData + sizeof(unsigned int));
		}
		break;

		case LONGFRACT:
		{
                	tmp = gSpace;
	                INDENTATION(tmp)
  			if (len != 0)
			{
				unsigned int base = stat_ipow(10, len);
				unsigned int  re_value =  *(( unsigned int *)goutData) / base;
				unsigned int fract_value = *(( unsigned int *)goutData) % base;
				printf("%*u.%0*u : %s\n", 20 - len - 1, re_value, len, fract_value, description);
			}
			else
      				printf("%20u : %s\n", *(( unsigned int *)goutData), description);

			goutData = (void *)((unsigned int)goutData + sizeof(unsigned int));
		}
		break;

		case SLONGFRACT:
		{
	                tmp = gSpace;
        	        INDENTATION(tmp)
		  	if (len != 0)
			{
				unsigned int base = stat_ipow(10, len);
				int  re_value =  *(( signed int *)goutData) / base;
				int fract_value = *(( signed int *)goutData) % base;
      				printf("%*i.%0*i : %s\n", 20 - len - 1, re_value, len, fract_value, description);
			}
			else
      				printf("%20i : %s\n", *(( signed int *)goutData), description);

			goutData = (void *)((unsigned int)goutData + sizeof(signed int));
		}
		break;

		case HUGE:
		{
	                tmp = gSpace;
        	        INDENTATION(tmp)
			printf("%20lli : %s\n", *((signed long long *)goutData), description);
			goutData = (void *)((unsigned int)goutData + sizeof(signed long long));
		}
		break;

		default:
		break;
	}
}

static int NlEventCallback(char *ifname, int event, int subevent, size_t len, unsigned char *data)
{
	int i;

	(void)ifname;
	(void)event;
	(void)subevent;

	for (i=0; i < (int)len; i++)
		printf("%x ",data[i]);

	return 0;
}

static void help_print(stat_id c,bool original)
{
	int i,tmp=0;

	for (i = 0; i < gStat[c].size; i++)
	{
		if ( !i && original )
			gSpace++;
		if ( gStat[c].sts[i].t != NONE )
		{
			print_type(gStat[c].sts[i].t, (char *)gStat[c].sts[i].description, gStat[c].sts[i].element);
			if (gStat[c].sts[i].t == BITFIELD || gStat[c].sts[i].t == ENUM)
				goutData = (void *)((unsigned int)goutData + sizeof(unsigned int));
		}
		else
		{
			tmp = gSpace;
				if ( gStat[c].sts[i].c == NETWORK_BITFIELD ) {
					INDENTATION(tmp)
					printf("%20s : %s\n","",gStat[c].sts[i].description);
				}
				else {
					if ( ( gStat[c].sts[i].c != PHY_ENUM ) && ( gStat[c].sts[i].c != VENDOR_ENUM ) ) {
						INDENTATION(tmp)
						PRINT_DESCRIPTION(gStat[c].sts[i])
					}
				}
		}

		if ( gStat[c].sts[i].c != c ) {
			if ( gStat[c].sts[i].c != NETWORK_BITFIELD )
				gSpace++;
			help_print(gStat[c].sts[i].c, false );
		}
	}
	if ( !original )
		gSpace--;
	i--;
	if ( ( gStat[c].sts[i].t == ENUM ) ) {
		char ptr[64]= {'\0'};
		switch ( gStat[c].sts[i].c )
		{
			case VENDOR_ENUM:
				snprintf(ptr,sizeof("Vendor"),"Vendor");
				break;
			case PHY_ENUM:
				snprintf(ptr,sizeof("Network (Phy) Mode"),"Network (Phy) Mode");
				break;
			default:
				;
		}
		if ( !gEnum )
		{
			tmp = gSpace;
			INDENTATION(tmp); 
			printf("%20s : %s\n","Unknown value",ptr);
		}
		else {
			printf("%s\n",ptr);
			gEnum = 0;
		}
	}	
	
}

static void dump_sta_list(char *outData,unsigned int outLen)
{
  unsigned int sta_number = ( outLen - NL_ATTR_HDR )/sizeof(peer_list_t);
  peer_list_t *sta = (peer_list_t *)&outData[NL_ATTR_HDR]; 

  if (sta_number > 0)
  {
    unsigned int i;
    fprintf(stdout, "\n\n%u peer(s) connected:\n\n", sta_number);
    for (i = 0; i < sta_number; i++)
    {
      if (sta->is_sta_auth)
      {
        fprintf(stdout, "\t" MAC_PRINTF_FMT "\n", MAC_PRINTF_ARG(&sta->addr));
      }
      else
      {
        fprintf(stdout, "\t" MAC_PRINTF_FMT " (not authorized)\n", MAC_PRINTF_ARG(&sta->addr));
      }
	sta++;
    }
  }
  else
  {
    fprintf(stdout, "\n\nNo peers connected.\n\n");
  }

  fprintf(stdout, "\n");
}


static void print_cmd_help( char *cmd )
{
	int i, found = 0;

	if ( !cmd )
		printf("\n\t Help for supported statistics are:");

	for ( i=0; i < (int)(sizeof(gCmd)/sizeof(gCmd[0])); i++ )
	{
		if ( cmd )
		{
			if ( !strncmp(cmd, gCmd[i].cmd, strnlen_s(gCmd[i].cmd, MAX_STR_SIZE)) )
			{
				printf("\n\t Help for %s statistics is:",cmd);
				printf("\n\t %s",gCmd[i].usage);
				found = 1;
			}
		}
		else
			printf("\n\t %s",gCmd[i].usage);
	}

	if ( !cmd || found )
	{
		printf("\n\t Note:\n\t\t INTERFACENAME can be wlan0,wlan0.0,wlan2,wlan2.0...");
		printf("\n\t\t MACADDR corresponds to macaddr of connected station\n");
	}
}

int check_stats_cmd(int num_arg, char *cmd[])
{
	unsigned char outData[MAX_NL_REPLY] = { '\0' };
	unsigned int outLen,i;
	unsigned char Vendordata[6] = {'\0'};
	int VendorDataLen = 0;
	char input_cmd[MAX_STR_SIZE] = { '\0' };

	if ( !strncmp(cmd[0],"help",sizeof("help")-1) ) {
		if ( num_arg > 1 )
			print_cmd_help(cmd[1]);
		else
			print_cmd_help(NULL);
		return 0;
	}

	if ( num_arg <  2) {
		PRINT_ERROR(" Need atleaset 2 arguments ie interfacename and command\n");
		return -1;
	}
	
	convert_str_to_lower(cmd[1],input_cmd);
	
	for ( i = 0; i < sizeof(gCmd)/sizeof(gCmd[0]); i++ ) 
	{
		if ( !strncmp(input_cmd, gCmd[i].cmd, strnlen_s(gCmd[i].cmd,MAX_STR_SIZE)) ) {
			if ( num_arg -1 != gCmd[i].num_arg ) {
				printf("%s\n",(char *)&gCmd[i].usage);
				return -1;
			}
			if ( num_arg > 2 ) {
				convertMac(cmd[2],Vendordata);
				VendorDataLen = sizeof(Vendordata);
				PRINT_DEBUG("Vendordata %s\n",Vendordata);
			}
			if (dwpal_ext_driver_nl_attach(NlEventCallback) == DWPAL_FAILURE)
			{
				PRINT_ERROR("%s; dwpal_driver_nl_attach returned ERROR ==> Abort!\n", __FUNCTION__);
				return DWPAL_FAILURE;
			}
			if (dwpal_ext_driver_nl_get(cmd[0], NL80211_CMD_VENDOR, DWPAL_NETDEV_ID, gCmd[i].id ,\
				 Vendordata, VendorDataLen, &outLen, outData) != DWPAL_SUCCESS)
			{
				PRINT_ERROR("%s; dwpal_ext_driver_nl_get returned ERROR ==> Abort!\n", __FUNCTION__);
				return -1;
			}
			if ( outLen )
				goutData = &outData[NL_ATTR_HDR];
			else
				return -1;

			if ( gCmd[i].c != PEER_LIST )
				help_print( gCmd[i].c, true );
			else
				dump_sta_list((char *)outData, outLen);
			break;
		}
	}
	goutData = NULL;
	return 0;
}
		
		

