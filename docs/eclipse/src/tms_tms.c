#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <coredef.h>
#define EXTERN extern
#include <struct.h>
#include <poslib.h>
#include "../inc/def.h"
#include "../inc/tms_md5.h"
#include "../inc/tms_tms.h"
#include <Q161Func_From_Devlib.h>

static void *s;
static unsigned char *t_sBuf=NULL;
static unsigned char *t_rBuf=NULL;
int Gfunflag = 0; //0-tms log not output     1-tms log output
int _tms_G_dbgOutDestApp = 0;
char G_token[20+1] = {""};
int GtermAuthFlag = 0;  //0-auth not done     TMSAUTH_DONE-auth done

URL_ENTRY tmsEntry;
struct __TmsTrade__  TmsTrade;
struct __TmsStruct__  TmsStruct;
cJSON * pRoot;
int _tms_G_dbgOutDestApp;

#ifdef __MACHINE_Q181Q190__
int tms_tms_flag = 0; //1-auth  2-upgrade  3-terminal location upload
#endif

static int dev_connect(URL_ENTRY *entry, int timeout)  //ak1
{
	int err;

	if (entry->protocol == PROTOCOL_HTTP)
		s = net_connect(NULL, entry->domain, entry->port, timeout * 1000, 0, &err);
	else
		s = net_connect(NULL, entry->domain, entry->port, timeout * 1000, 1, &err);

	if (s == NULL)
		return CONNECT_ERROR;

	return 0;
}

//return 0-success SEND_ERROR-failed
static int dev_send(unsigned char *buf, int length)
{
	int ret;
#ifdef __MACHINE_Q181Q190__
	ret = net_write(s, buf, length, 5*1000);
#else
	ret = net_write(s, buf, length, 0);
#endif
	if (ret < 0)
		return SEND_ERROR;

	return 0;
}

//return: 0-timeout      <0-failed     >0-length
//Return bytes of received, or error
static int dev_recv(unsigned char *buf, int max, int timeout)
{
	int ret;

	ret = net_read(s, buf, max, timeout * 1000);  //return: 0-timeout      <0-failed     >0-length
	if (ret < 0){
		return RECEIVE_ERROR;
	}

	return ret;
}

static int dev_disconnect(void)
{
	return net_close(s);
}

#ifdef JUMPER_EXIST
void Tms_DispJumpSec(void)
{
#ifdef DISP_STH
	char DispBuf[20];
	memset(DispBuf, 0, sizeof(DispBuf));
	_tms_g_MyJumpSec.Sec++;
	sprintf(DispBuf, "%d", _tms_g_MyJumpSec.Sec);
	ScrDisp_Api(LINE5, 0, DispBuf, CDISP);
#endif
}
#endif

int Tms_StartJumpSec(void)
{
#ifdef JUMPER_EXIST
	_tms_g_MyJumpSec.Timer = CommStartJumpSec_Api(Tms_DispJumpSec);
#endif
	return 0;
}

void Tms_StopJumpSec(void)
{
#ifdef JUMPER_EXIST
	_tms_g_MyJumpSec.Sec = 0;
	CommStopJumpSec_Api(_tms_g_MyJumpSec.Timer);
#endif
}



/**************** Functions about flow Start **************************/
int Tms_BufMalloc()
{
	t_sBuf = malloc(SENDPACKLEN);
	if(t_sBuf == NULL)
	{
		Tms_LstDbgOutApp("Tms_BufMalloc:", (unsigned char *)"t_sBuf malloc failed!", strlen("t_sBuf malloc failed!"), 1);
		return  _TMS_E_MALLOC_NOTENOUGH;
	}

	t_rBuf= malloc(RECVPACKLEN);
	if(t_rBuf == NULL)
	{
		free(t_sBuf);
		t_sBuf = NULL;
		Tms_LstDbgOutApp("Tms_BufMalloc:", (unsigned char *)"t_rBuf malloc failed!", strlen("t_rBuf malloc failed!"), 1);
		return _TMS_E_MALLOC_NOTENOUGH;
	}
	return 0;
}

//Create Resolve package start
//tmscomm_type: gps:0  mobile network:1  WIFI:2
int Tms_CreatePacket(u8 * packData, int * packLen, int ii, int tmscomm_type)
{
	cJSON *root = NULL, *arrayroot = NULL, *subroot = NULL;
	char *out = NULL, *tnet = NULL;
	int dataLen, i, ret;
	char buf[128], tmp[64], timestamp[16], urlPath[128], md5src[1024];
#ifdef __MACHINE_Q181Q190__
	float space;
#else
	int TotalMem, avaiTotal, max_block_size, exflash;
#endif
	int isElect = 0;

	memset(urlPath, 0, sizeof(urlPath));
	root = cJSON_CreateObject();

	cJSON_AddStringToObject((cJSON *)root, "version", TmsStruct.version);

	memset(timestamp, 0, sizeof(timestamp));
	memset(tmp, 0, sizeof(tmp));
	memset(buf, 0, sizeof(buf));
	GetSysTime_Api((unsigned char *)tmp);
	BcdToAsc_Api((char *)timestamp, (unsigned char *)tmp, 14);
	i = 0;
	memcpy(buf+i, timestamp, 4);
	i += 4;
	buf[i++] = '/';
	memcpy(buf+i, timestamp+4, 2);
	i += 2;
	buf[i++] = '/';
	memcpy(buf+i, timestamp+6, 2);
	i += 2;
	buf[i++] = ' ';
	memcpy(buf+i, timestamp+8, 2);
	i += 2;
	buf[i++] = ':';
	memcpy(buf+i, timestamp+10, 2);
	i += 2;
	buf[i++] = ':';
	memcpy(buf+i, timestamp+12, 2);
	cJSON_AddStringToObject((cJSON *)root, "time", buf);
	cJSON_AddStringToObject((cJSON *)root, "sn", TmsStruct.sn);

	switch(TmsTrade.trade_type)
	{
	case TYPE_DEVICEAUTH:
		sprintf((char *)urlPath, "POST /server/trad/deviceAuthentication");

		memset(buf, 0, sizeof(buf));
		memset(tmp, 0, sizeof(tmp));
		if(tmscomm_type == TRANCOMM_SIM)
		{
			//Q161 SIM卡 : 4G模组没有mac, ip是基站分配的ip每次都不一样 是个局域网ip
			dataLen = sizeof(tmp)-1;
			ret = VTMS_wirelessGetIP(tmp, &dataLen); //10.213.231.146,2408:8409:1920:4D44:17ED:AEC4:2400:AFBC
			MAINLOG_L1("VTMS_wirelessGetIP ret:%d dataLen:%d IP:%s:", ret, dataLen, tmp);
		}
		else if(tmscomm_type == TRANCOMM_WIFI)
		{
			ST_WIFI_PARAM pstWifiParam;
			memset(&pstWifiParam, 0, sizeof(pstWifiParam));
			VTMS_wifiGetMac(buf);
			ret = VTMS_wifiGetConnectParam(&pstWifiParam);
			if(ret == 0)
				strcpy(tmp, pstWifiParam.cIp);
		}

		if(strlen(buf) == 0)
			strcpy(buf, "nomac");
		if(strlen(tmp) == 0)
			strcpy(tmp, "noip");

		cJSON_AddStringToObject((cJSON *)root, "mac", buf);
		cJSON_AddStringToObject((cJSON *)root, "ip", tmp);
		break;
	case TYPE_TERMINFO_UPLOAD:
		sprintf((char *)urlPath, "POST /server/trad/reportDeviceInfo");

		cJSON_AddStringToObject((cJSON *)root, "token", G_token);

		memset(buf,0,sizeof(buf));
		memset(tmp,0,sizeof(tmp));
		GetFirmwareModuleData(buf, tmp);
		cJSON_AddStringToObject((cJSON *)root, "firmware", buf);
		cJSON_AddStringToObject((cJSON *)root, "module", tmp);

		memset(tmp,0,sizeof(tmp));
		wirelessGetIMSIAndIMEI_Api(NULL, (unsigned char *)tmp); //IMSI IMEI  //if both parameter is not null, then either one get failed, then return failed; for WIFI without SIM card, IMSI get failed ,so use NULL for the 1st parameter
		cJSON_AddStringToObject((cJSON *)root, "imei", tmp);

		memset(buf, 0, sizeof(buf));
		sysReadBPVersion_Api((unsigned char *)buf);
		cJSON_AddStringToObject((cJSON *)root, "osVersion", buf);

		memset(buf, 0, sizeof(buf));
#ifdef __MACHINE_Q181Q190__
		ret = sysReadHwstring_Api(buf);
		MAINLOG_L1("sysReadHwstring_Api ret:%d:%s:", ret, buf);
#else
		sysReadVerInfo_Api(2, (unsigned char*)buf); //hardware version ;  Q161: V2.3  //Q161 API , not Q181API
#endif
		cJSON_AddStringToObject((cJSON *)root, "hardVersion", buf);

#ifdef __MACHINE_Q181Q190__
		cJSON_AddStringToObject((cJSON *)root, "totalMemory", "16");//16M

		memset(buf, 0, sizeof(buf));
		ret = fileGetFileSysFreeSize_Api();
		MAINLOG_L1( "fileGetFileSysFreeSize_Api:%d", ret);
		space = (float)ret / (float)1048576;
		sprintf(buf, "%f", space);
		cJSON_AddStringToObject((cJSON *)root, "availableMemory", buf);

		cJSON_AddStringToObject((cJSON *)root, "totalStorage", "16");//8M or 16M

		memset(buf, 0, sizeof(buf));
		ret = fileGetExtFileSysFreeSize_Api();
		MAINLOG_L1( "fileGetExtFileSysFreeSize_Api:%d", ret);
		space = (float)ret / (float)1048576;
		sprintf(buf, "%f", space);
		cJSON_AddStringToObject((cJSON *)root, "availableStorage", buf);
#else
		ret = sysGetHeapInfo_Api(&TotalMem, &avaiTotal, &max_block_size);
		MAINLOG_L1("sysGetHeapInfo_Api ret:%d TotalMem:%d avaiTotal:%d max_block_size:%d", ret, TotalMem, avaiTotal, max_block_size);

		memset(buf, 0, sizeof(buf));
		sprintf(buf, "%d", TotalMem/_MBTOBYTES_);
		cJSON_AddStringToObject((cJSON *)root, "totalMemory", buf);

		memset(buf, 0, sizeof(buf));
		sprintf(buf, "%d", avaiTotal/_MBTOBYTES_);
		cJSON_AddStringToObject((cJSON *)root, "availableMemory", buf);

		cJSON_AddStringToObject((cJSON *)root, "totalStorage", "16");//8M or 16M

		memset(buf, 0, sizeof(buf));
		ret = fileGetFileSysFreeSize_Api(); //available external flash
		MAINLOG_L1( "fileGetFileSysFreeSize_Api(available external flash):%d", ret);
		exflash = ret/_MBTOBYTES_;
		sprintf(buf, "%d", exflash);
		cJSON_AddStringToObject((cJSON *)root, "availableStorage", buf);
#endif
		cJSON_AddStringToObject((cJSON *)root, "batterCapacity", "2000"); //2000mA

		ret = BatChargeProc_Api(0, 0, 0, 0, BATER_WARM, &isElect);
		MAINLOG_L1("batteryLevel ret:%d", ret);
		if(ret != 0xaa) {
			memset(buf, 0, sizeof(buf));
			sprintf(buf, "%d", ret);
			cJSON_AddStringToObject((cJSON *)root, "batteryLevel", buf);
		}else
			cJSON_AddStringToObject((cJSON *)root, "batteryLevel", "99");

		if(ret == 0xaa)
			cJSON_AddStringToObject((cJSON *)root, "chargingState", "0");
		else
			cJSON_AddStringToObject((cJSON *)root, "chargingState", "1");

		memset(buf, 0, sizeof(buf));
		sprintf(buf, "%d", G_sys_param.sound_level);
		cJSON_AddStringToObject((cJSON *)root, "volumeLevel", buf);
		break;
	case TYPE_GETTERM_TASK:
		sprintf((char *)urlPath, "POST /server/trad/getDeviceTask");

		cJSON_AddStringToObject((cJSON *)root, "token", G_token);

		memset(buf,0,sizeof(buf));
		memset(tmp,0,sizeof(tmp));
		GetFirmwareModuleData(buf, tmp);
		cJSON_AddStringToObject((cJSON *)root, "firmware", buf);
		cJSON_AddStringToObject((cJSON *)root, "module", tmp);
		break;
	case TYPE_NOTIFY:
		sprintf((char *)urlPath, "POST /server/trad/taskResultNotify");

		cJSON_AddStringToObject((cJSON *)root, "token", G_token);

		memset(tmp, 0, sizeof(tmp));
		sprintf(tmp, "%d", TmsTrade.file[ii].taskId);
		cJSON_AddStringToObject((cJSON *)root, "taskId", tmp);

		memset(tmp, 0, sizeof(tmp));
		sprintf(tmp, "%d", TmsTrade.file[ii].itemId);
		cJSON_AddStringToObject((cJSON *)root, "taskItemId", tmp);

		if(TmsTrade.file[ii].status == STATUS_DLCOMPLETE)
			cJSON_AddStringToObject(root, "status", "0"); 	//success
		else
			cJSON_AddStringToObject(root, "status", "2");	//failure
		break;
	case TYPE_LOCATION_UPLOAD:
		sprintf((char *)urlPath, "POST /server/trad/reportDeviceLocation");

		cJSON_AddStringToObject((cJSON *)root, "token", G_token);
		cJSON_AddInt64ToObject(root, "uploadType", tmscomm_type); //gps:0  mobile network:1  WIFI:2

		memset(tmp,0,sizeof(tmp));
		wirelessGetIMSIAndIMEI_Api(NULL, (unsigned char *)tmp); //if both parameter is not null, then either one get failed, then return failed; for WIFI without SIM card, IMSI get failed ,so use NULL
		cJSON_AddStringToObject((cJSON *)root, "imei", tmp);

		switch(tmscomm_type)
		{
		case TRANCOMM_NONE: //need to change
			cJSON_AddStringToObject(root, "latlng", "");
			break;
		case TRANCOMM_SIM:
			memset(buf, 0, sizeof(buf));
			VTMS_GetWirelessOperName(buf); //'buf' better no less than 128 bytes, in case stack overflow
			cJSON_AddStringToObject(root, "carrier", buf);

			tnet = VTMS_GetTypeOfMobileNetworks(); //return GSM CDMA GPRS CDMA2000 LTE
			MAINLOG_L1("VTMS_GetTypeOfMobileNetworks : %s", tnet);

			if((tnet == NULL) || strcmp(tnet, "CDMA")) //non-cdma:0  cdma:1
				cJSON_AddInt64ToObject(root, "cdma", 0);
			else
				cJSON_AddInt64ToObject(root, "cdma", 1);

			cJSON_AddStringToObject(root, "network", tnet); //gsm/wcdma/lte

			arrayroot = cJSON_CreateArray();
			{
				int num;

				#ifdef __MACHINE_Q181Q190__
					CellInfoAll_Str cellinfo;

					memset((char *)&cellinfo, 0, sizeof(cellinfo));
					ret = wirelessGetCellInfo_Api(&cellinfo, 10*1000);
					MAINLOG_L1("wirelessGetCellInfo_Api ret:%d num:%d ", ret, cellinfo.cellinfonum);
					if(ret >= 0)
					{
						if(cellinfo.cellinfonum > 5)
							num = 5;
						else
							num = cellinfo.cellinfonum;
						for(i = 0; i < num; i++)
						{
							MAINLOG_L1("cell %d: MCC-%d MNC-%d LAC-%d CellID-%d RxDbm-%d", i, cellinfo.cellinfo_str[i].MCC, cellinfo.cellinfo_str[i].MNC, cellinfo.cellinfo_str[i].LAC, cellinfo.cellinfo_str[i].CellID, cellinfo.cellinfo_str[i].RxDbm);
							subroot = cJSON_CreateObject();
							cJSON_AddInt64ToObject(subroot, "cellId", cellinfo.cellinfo_str[i].CellID);
							cJSON_AddInt64ToObject(subroot, "locationAreaCode", cellinfo.cellinfo_str[i].LAC);
							cJSON_AddInt64ToObject(subroot, "mobileCountryCode", cellinfo.cellinfo_str[i].MCC);
							cJSON_AddInt64ToObject(subroot, "mobileNetworkCode", cellinfo.cellinfo_str[i].MNC);
							cJSON_AddNumberToObject(subroot, "signalStrength", cellinfo.cellinfo_str[i].RxDbm);
							cJSON_AddNumberToObject(subroot, "timingAdvance", 0);
							cJSON_AddInt64ToObject(subroot, "age", 0);
							cJSON_AddItemToArray((cJSON *)arrayroot, subroot);
						}
					}
				#else
					BASESTATION BSInfo[5];
					num = CommBaseStationInfo_Api(&BSInfo[0], 5, 60000);
					MAINLOG_L1("CommBaseStationInfo_Api num: %d", num);
					for(i=0; i<num; i++)
					{
						//MAINLOG_L1("base station %d cellId:%d bsic:%d lac:%d mcc:%d mnc:%d rxlev:%d", i, BSInfo[i].ci, BSInfo[i].bsic, BSInfo[i].lac, BSInfo[i].mcc, BSInfo[i].mnc, BSInfo[i].rxlev);
						subroot = cJSON_CreateObject();
						cJSON_AddInt64ToObject(subroot, "cellId", BSInfo[i].ci);
						cJSON_AddInt64ToObject(subroot, "locationAreaCode", BSInfo[i].lac);
						cJSON_AddInt64ToObject(subroot, "mobileCountryCode", BSInfo[i].mcc);
						cJSON_AddInt64ToObject(subroot, "mobileNetworkCode", BSInfo[i].mnc);
						cJSON_AddNumberToObject(subroot, "signalStrength", BSInfo[i].rxlev);
						cJSON_AddNumberToObject(subroot, "timingAdvance", 0);
						cJSON_AddInt64ToObject(subroot, "age", 0);
						cJSON_AddItemToArray((cJSON *)arrayroot, subroot);
					}
				#endif
			}
			cJSON_AddItemToObject(root, "cellTowers", arrayroot);
			break;
		case TRANCOMM_WIFI:
			arrayroot = cJSON_CreateArray();
			{
				int num;
				struct WIFI_SCAN_AP ApArray[5];

				memset(&ApArray, 0, sizeof(ApArray));
				num = CommWifiScanAp_Api(ApArray, 5, 0);
				for(i=0; i<num; i++)
				{
					int j, tn = 0;

					subroot = cJSON_CreateObject();
					memset(tmp, 0, sizeof(tmp));
					for(j=0; j<6; j++)
					{
						BcdToAsc_Api(tmp+tn, &(ApArray[i].BSSID[j]), 2);
						tn += 2;
						tmp[tn++] = ':';
					}
					tmp[tn-1] = 0;
					cJSON_AddStringToObject(subroot, "macAddress", tmp);
					cJSON_AddNumberToObject(subroot, "signalStrength", ApArray[i].Signal);
					cJSON_AddInt64ToObject(subroot, "age", 0);
					//cJSON_AddInt64ToObject(subroot, "channel", "");
					//cJSON_AddNumberToObject(subroot, "signalToNoiseRatio", "");
					cJSON_AddItemToArray((cJSON *)arrayroot, subroot);
				}
			}
			cJSON_AddItemToObject(root, "wifiAccessPoints", arrayroot);
			break;
		}
		break;
	default:
		break;
	}

	out = cJSON_PrintUnformatted(root);
	dataLen = strlen(out);
	memcpy((char *)packData+strlen((char *)packData), urlPath, strlen((char *)urlPath));
	strcat((char *)packData, " HTTP/1.1\r\n");

	memset(timestamp, 0, sizeof(timestamp));
	GetSysTime_Api((unsigned char *)tmp);
	BcdToAsc_Api((char *)timestamp, (unsigned char *)tmp, 14);
	memset(tmp, 0x20, sizeof(tmp));
	memset(buf, 0x20, sizeof(buf));
	memcpy(tmp, TmsStruct.sn, strlen(TmsStruct.sn));
	memcpy(buf, timestamp, strlen((char *)timestamp));
	for(i=0; i<50; i++)
		tmp[i] ^= buf[i];
	memset(buf, 0x20, sizeof(buf));
	memcpy(buf, "998876511QQQWWeerASDHGJKL", strlen("998876511QQQWWeerASDHGJKL"));
	for(i=0; i<50; i++)
		tmp[i] ^= buf[i];
	tmp[50] = 0;
	memset(buf, 0, sizeof(buf));
	memset(md5src, 0, sizeof(md5src));
	memcpy(md5src, out, dataLen);
	BcdToAsc_Api((char *)(md5src+dataLen), (unsigned char *)tmp, 100);
	_tms_MDString((char *)md5src,(unsigned char *)buf);
	memset(tmp, 0, sizeof(tmp));
	BcdToAsc_Api((char *)tmp, (unsigned char *)buf, 32);
	sprintf((char *)packData+strlen((char *)packData), "sign: %s\r\n", tmp);

	sprintf((char *)packData+strlen((char *)packData), "timestamp: %s\r\n", timestamp);
	sprintf((char *)packData+strlen((char *)packData), "Content-Length: %d\r\n", strlen((char *)out));
	strcat((char *)packData, "Content-Type: application/json;charset=UTF-8\r\n");
	sprintf((char *)packData+strlen((char *)packData), "Host: %s\r\n", TmsStruct.hostDomainName);
	strcat((char *)packData, "Connection: Keep-Alive\r\n");
	strcat((char *)packData, "User-Agent: Apache-HttpClient/4.3.5 (java 1.5)\r\n");
	strcat((char *)packData, "Accept-Encoding: gzip,deflate\r\n\r\n");
	memcpy(packData+strlen((char *)packData), out, dataLen);
	*packLen = strlen((char *)packData);
	free(out);

	if(root != NULL)
	{
		cJSON_Delete(root);
	}

	return 0;
}

int Tms_RecvPacket(u8 *Packet, int *PacketLen, int WaitTime)
{
	int Ret;
	unsigned short tlen = 0;
	int clen, stlen;

	while(1)
	{
		Tms_StartJumpSec();
		Ret = dev_recv(Packet+tlen, RECEIVE_BUF_SIZE - 1 - tlen, RECEIVE_TIMEOUT);
		Tms_StopJumpSec();

		if (Ret <= 0) {
			Ret = _TMS_E_RECV_PACKET;
			break;
		}
		tlen += Ret;
		clen = 0;
		stlen = 0;
		Ret = 0;
		if(Tms_getContentLen(Packet, &clen, &stlen) == 0)
		{
			if(tlen > stlen)
			{
				MAINLOG_L1("receive error 1 tlen:%d stlen:%d", tlen, stlen);
				Ret = _TMS_E_RECV_PACKET;
				break;
			}
			else if(tlen == stlen)
			{
				*PacketLen = tlen;
				Ret = 0;
				break;
			}
		}
	}
	if(Ret != 0)
		dev_disconnect();
	return Ret;
}

int Tms_UrlRecvPacket(u8 *Packet, int *PacketLen, int WaitTime)
{
	unsigned short len;
	int Ret;
	u8 *p = NULL;
	int tlen = 0, curfidx;
	int clen = 0, stlen=0, hflag = 0, floc = 0;
#ifdef DISP_STH
	char tmp[64];
#endif

    curfidx = TmsTrade.curfindex;
	floc = Tms_GetFileSize(&(TmsTrade.file[TmsTrade.curfindex]));

	while(1)
	{
		Tms_StartJumpSec();
		if(hflag == 0)
			Ret = dev_recv(Packet+tlen, RECEIVE_BUF_SIZE - 1 - tlen, RECEIVE_TIMEOUT);
		else
			Ret = dev_recv(Packet, RECEIVE_BUF_SIZE - 1 - tlen, RECEIVE_TIMEOUT);
		Tms_StopJumpSec();
		if (Ret <= 0) {
			Ret = _TMS_E_RECV_PACKET;
			break;
		}

#ifdef DISP_STH
		memset(tmp, 0, sizeof(tmp));
		ScrClrLine_Api(LINE3, LINE4);
		sprintf(tmp, "%d%% %d/%d", (TmsTrade.file[TmsTrade.curfindex].startPosi*100)/TmsTrade.file[TmsTrade.curfindex].fsize, TmsTrade.file[TmsTrade.curfindex].startPosi, TmsTrade.file[TmsTrade.curfindex].fsize);
		ScrDisp_Api(LINE3, 0, TmsTrade.file[TmsTrade.curfindex].name, FDISP|CDISP);
		ScrDisp_Api(LINE4, 0, tmp,FDISP|CDISP);
#endif
		len = Ret;
		Ret = 0;
		tlen += len;
		if(hflag == 0)
		{
			if(Tms_getContentLen(Packet, &clen, &stlen) != 0)
				continue;

			if(floc+clen != TmsTrade.file[TmsTrade.curfindex].fsize)
			{
				MAINLOG_L1("receive error floc:%d clen:%d fsize:%d", floc, clen, TmsTrade.file[TmsTrade.curfindex].fsize);
				Ret = _TMS_E_RECV_PACKET;
				Tms_LstDbgOutApp("flocfloc:", (u8 *)&floc, sizeof(floc), 0);
				Tms_LstDbgOutApp("clenclen:", (u8 *)&clen, sizeof(clen), 0);
				Tms_LstDbgOutApp("fsize:", (u8 *)&(TmsTrade.file[TmsTrade.curfindex].fsize), sizeof(TmsTrade.file[TmsTrade.curfindex].fsize), 0);
				break;
			}
			p = (u8 *)strstr((char *)Packet, "\r\n\r\n");
			if(tlen-(p+4-Packet) > 0)
			{
				Ret = Tms_WriteFile(&TmsTrade.file[TmsTrade.curfindex], p+4, tlen-(p+4-Packet));
				if(Ret != 0)
				{
					Ret = _TMS_E_FILE_WRITE;
					Tms_LstDbgOutApp("Tms_WriteFile:", (u8 *)&Ret, sizeof(Ret), 0);
					break;
				}

				floc += (tlen-(p+4-Packet));
			}
			hflag = 1;
		}
		else
		{
			Ret = Tms_WriteFile(&TmsTrade.file[TmsTrade.curfindex], Packet, len);
			if(Ret != 0)
			{
				Ret = _TMS_E_FILE_WRITE;
				Tms_LstDbgOutApp("Retwr1:", (u8 *)&Ret, sizeof(Ret), 0);
				break;
			}

			floc += len;
		}

		if(tlen > stlen)
		{
			Ret = _TMS_E_RECV_PACKET;
			MAINLOG_L1("receive error 2 tlen:%d stlen:%d", tlen, stlen);
			break;
		}
		else if(tlen == stlen)
		{
			Ret = 0;
			*PacketLen = tlen;
			Tms_LstDbgOutApp((TmsTrade.file[curfidx].name), (unsigned char*)" down over!", strlen(" down over!"), 1);
			break;
		}
	}
	if(Ret != 0)
		dev_disconnect();
	return Ret;
}

int Tms_SendRecvData( unsigned char *SendBuf, int Senlen, unsigned char *RecvBuf, int *RecvLen,int psWaitTime)
{
	int ret=0;

	ret = dev_send(SendBuf, Senlen);
	Tms_DbgOutApp("send:", SendBuf, Senlen);
	if(ret != 0)
	{
		Tms_LstDbgOutApp("Tms_SendRecvData CommTxd_Api:", (u8 *)&ret, sizeof(ret), 0);
		if(ret != 0xff)
			return _TMS_E_SEND_PACKET;

		ret = Tms_ReSend(SendBuf,  Senlen);
		if(ret != 0)
			return _TMS_E_SEND_PACKET;
	}

	*RecvLen = 0;
	if(TmsTrade.trade_type == TYPE_URLGETFILE)
	{
		ret = Tms_UrlRecvPacket(RecvBuf, RecvLen, psWaitTime);
		//Tms_LstDbgOutApp("Urlrecv22:", RecvBuf, *RecvLen, 0); //hex code   //this will make app crash
	}
	else
	{
		ret = Tms_RecvPacket(RecvBuf, RecvLen, psWaitTime);
		Tms_DbgOutApp("Tms_RecvPacket:", RecvBuf, *RecvLen); //ascii
	}
	return ret;
}

int Tms_CreateTxdRxdResolve(int notifyIdx, int tranmode)
{
	int PackLen=0;
	int ret = 0;

	memset(t_sBuf, 0, SENDPACKLEN);
	ret = Tms_CreatePacket(t_sBuf, &PackLen, notifyIdx, tranmode);
	if(ret != 0)
		return ret;

	memset(t_rBuf, 0, RECVPACKLEN);
	ret = Tms_SendRecvData(t_sBuf, PackLen, t_rBuf, &PackLen, TmsStruct.tradeTimeoutValue);
	if(ret != 0)
	{
		Tms_LstDbgOutApp("Tms_SendRecvData:", (u8 *)&ret, sizeof(ret), 0);
		return ret;
	}

	ret = Tms_ResolveHTTPPacket(t_rBuf);
	if(ret != 0)
	{
		Tms_LstDbgOutApp("Tms_ResolveHTTPPacket:", (u8 *)&ret, sizeof(ret), 0);
		return ret;
	}

	return ret;
}

//return 0:sucess other:failed
u8 Tms_HttpGetTranResult(u8 * data)
{
	u8 *p = NULL, tmp[64];

	if((p = (u8 *)strstr((char *)data, "\n")) == NULL)  //\r\n
		return _TMS_E_RESOLVE_PACKET;
	memset(tmp, 0, sizeof(tmp));
	memcpy(tmp, data, p-data-1);
	if((p = (u8 *)strstr((char *)tmp, "200")) == NULL)
	{
		memset(TmsTrade.respMsg, 0, sizeof(TmsTrade.respMsg));
		if(strlen((char *)tmp) > (sizeof(TmsTrade.respMsg)-1))
			memcpy(TmsTrade.respMsg, tmp, sizeof(TmsTrade.respMsg)-1);
		else
			memcpy(TmsTrade.respMsg, tmp, strlen((char *)tmp));
		return _TMS_E_TRANS_FAIL;    //transaction failed if not "HTTP/1.1 200 OK"
	}
	return 0;
}

//not for TYPE_URLGETFILE
int Tms_ResolveHTTPPacket(u8 *recvPack)
{
	char *ptr = NULL;
	int ret;

	ret = Tms_HttpGetTranResult(recvPack);
	if(ret != 0)
	{
		Tms_LstDbgOutApp("Tms_HttpGetTranResult:", (u8 *)&ret, sizeof(ret), 0);
		return ret;
	}

	if((ptr = strstr((char *)recvPack, "{")) == NULL)
	{
		if(ret == 0)
			return _TMS_E_RESOLVE_PACKET;
		else
			return ret;
	}

	ret = Tms_UnPackPacket(ptr, ret);
	if(pRoot != NULL)
	{
		cJSON_Delete(pRoot);
		pRoot = NULL;
	}
	return ret;
}

int Tms_UnsolveGetTermTask(int reslt)
{
	int taskid = 0,  len = 0;
	cJSON * pItem = NULL, * pitem = NULL, *ppitem = NULL;

	pItem = cJSON_GetObjectItem(pRoot, "taskList");
	if((pItem == NULL) || (pItem->type != cJSON_Array))
		return 0;

	pItem = pItem->child;
	if(pItem == NULL)
		return reslt;

	while(pItem != NULL)
	{
		pitem = cJSON_GetObjectItem(pItem, "taskId");
		if((pitem != NULL) && (pitem->type != cJSON_NULL))
		{
			taskid = pitem->valueint64;
		}

		pitem = cJSON_GetObjectItem(pItem, "taskItemList");
		pitem = pitem->child;
		if(pitem == NULL)
			return reslt;

		while(pitem != NULL)
		{
			ppitem = cJSON_GetObjectItem(pitem, "fileType");
			if((ppitem != NULL) && (ppitem->type != cJSON_NULL))
			{
				len = sizeof(TmsTrade.file[TmsTrade.fnum].type)-1;
				memcpy(TmsTrade.file[TmsTrade.fnum].type, ppitem->valuestring, strlen(ppitem->valuestring) > len ? len:strlen(ppitem->valuestring));
			}

			ppitem = cJSON_GetObjectItem(pitem, "fileVersion");
			if((ppitem != NULL) && (ppitem->type != cJSON_NULL))
			{
				len = sizeof(TmsTrade.file[TmsTrade.fnum].version)-1;
				memcpy(TmsTrade.file[TmsTrade.fnum].version, ppitem->valuestring, strlen(ppitem->valuestring) > len ? len:strlen(ppitem->valuestring));
			}

			ppitem = cJSON_GetObjectItem(pitem, "fileSize");
			if((ppitem != NULL) && (ppitem->type != cJSON_NULL))
				TmsTrade.file[TmsTrade.fnum].fsize = (int)AscToLong_Api((unsigned char *)(ppitem->valuestring), strlen(ppitem->valuestring));

			ppitem = cJSON_GetObjectItem(pitem, "fileUrl");
			if((ppitem != NULL) && (ppitem->type != cJSON_NULL))
			{
				len = sizeof(TmsTrade.file[TmsTrade.fnum].filePath)-1;
				memcpy(TmsTrade.file[TmsTrade.fnum].filePath, ppitem->valuestring, strlen(ppitem->valuestring) > len ? len:strlen(ppitem->valuestring));
			}

			ppitem = cJSON_GetObjectItem(pitem, "fileMd5");
			if((ppitem != NULL) && (ppitem->type != cJSON_NULL))
			{
				AscToBcd_Api(TmsTrade.file[TmsTrade.fnum].md5, ppitem->valuestring, 32);
			}

			ppitem = cJSON_GetObjectItem(pitem, "fileName");
			if((ppitem != NULL) && (ppitem->type != cJSON_NULL))
			{
				len = sizeof(TmsTrade.file[TmsTrade.fnum].name)-1;
				memcpy(TmsTrade.file[TmsTrade.fnum].name, ppitem->valuestring, strlen(ppitem->valuestring) > len ? len:strlen(ppitem->valuestring));
			}

			ppitem = cJSON_GetObjectItem(pitem, "id");
			if((ppitem != NULL) && (ppitem->type != cJSON_NULL))
			{
				TmsTrade.file[TmsTrade.fnum].itemId = ppitem->valueint64;
			}

			TmsTrade.file[TmsTrade.fnum].taskId = taskid;

			if(Tms_CheckNeedDownFile(&TmsTrade.file[TmsTrade.fnum]) == 0) //necessary to download
			{
				TmsTrade.fnum++;
				Tms_LstDbgOutApp("fnum will be loaded:", (unsigned char *)&(TmsTrade.fnum), sizeof(TmsTrade.fnum), 0);
			}
			else
				memset(&TmsTrade.file[TmsTrade.fnum], 0, sizeof(TmsTrade.file[TmsTrade.fnum]));

			pitem = pitem->next;
		}

		pItem = pItem->next;
		if(TmsTrade.fnum > _TMS_MAX_APPLIB)
			return _TMS_E_TOO_MANY_FILES;
	}

	return 0;
}

//return :  E_RESOLVE_PACKET - resolve failed     0-resolve success
int Tms_UnPackPacket(char * data, int reslt)
{
	cJSON * pItem = NULL;
	int ret;

	if(pRoot != NULL)
	{
		cJSON_Delete(pRoot);
		pRoot = NULL;
	}
	pRoot = cJSON_Parse(data);
	if(pRoot == NULL)
		return _TMS_E_RESOLVE_PACKET;

	pItem = cJSON_GetObjectItem(pRoot, "version");
	if((pItem != NULL) && (pItem->type != cJSON_NULL))
	{
		if(strcmp(pItem->valuestring, TmsStruct.version))
			return _TMS_E_PACKAGE_WRONG;
	}

	pItem = cJSON_GetObjectItem(pRoot, "sn");
	if((pItem != NULL) && (pItem->type != cJSON_NULL))
	{
		if(strcmp(pItem->valuestring, TmsStruct.sn) != 0)
			return _TMS_E_PACKAGE_WRONG;
	}

	pItem = cJSON_GetObjectItem(pRoot, "code");
	if((pItem != NULL) && (pItem->type != cJSON_NULL))
		memcpy(TmsTrade.respCode, pItem->valuestring, strlen(pItem->valuestring));

	pItem = cJSON_GetObjectItem(pRoot, "message");
	if((pItem != NULL) && (pItem->type != cJSON_NULL))
		memcpy(TmsTrade.respMsg, pItem->valuestring, strlen(pItem->valuestring));

	if((strcmp(TmsTrade.respCode, "00") != 0) || (reslt != 0))
		return _TMS_E_TRANS_FAIL;

	switch(TmsTrade.trade_type)
	{
	case TYPE_DEVICEAUTH:
		pItem = cJSON_GetObjectItem(pRoot, "token");
		if((pItem != NULL) && (pItem->type != cJSON_NULL))
		{
			memset(G_token, 0, sizeof(G_token));
			memcpy(G_token, pItem->valuestring, strlen(pItem->valuestring));
		}
		break;
	case TYPE_TERMINFO_UPLOAD:
		break;
	case TYPE_GETTERM_TASK:

		ret = Tms_UnsolveGetTermTask(reslt);
		if(ret != 0)
			return ret;
		break;
	case TYPE_NOTIFY:
		break;
	case TYPE_LOCATION_UPLOAD:
		break;
	default:
		return 1;
	}
	return 0;
}

void Tms_DisconnBufFree()
{
	dev_disconnect();

	if(t_sBuf != NULL)
	{
		free(t_sBuf);
		t_sBuf = NULL;
	}

	if(t_rBuf != NULL)
	{
		free(t_rBuf);
		t_rBuf = NULL;
	}
}

int Tms_DeviceAuth(int commode)
{
	memset(TmsTrade.respCode, 0, sizeof(TmsTrade.respCode));
	memset(TmsTrade.respMsg, 0, sizeof(TmsTrade.respMsg));
	TmsTrade.trade_type = TYPE_DEVICEAUTH;
	return Tms_CreateTxdRxdResolve(0, commode);
}

int Tms_TermInfoUpload(void)
{
	memset(TmsTrade.respCode, 0, sizeof(TmsTrade.respCode));
	memset(TmsTrade.respMsg, 0, sizeof(TmsTrade.respMsg));
	TmsTrade.trade_type = TYPE_TERMINFO_UPLOAD;
	return Tms_CreateTxdRxdResolve(0, 0);
}

int Tms_RequestTermTask(void)
{
	int ret, flen;

	memset(TmsTrade.respCode, 0, sizeof(TmsTrade.respCode));
	memset(TmsTrade.respMsg, 0, sizeof(TmsTrade.respMsg));
	TmsTrade.trade_type = TYPE_GETTERM_TASK;
	ret = Tms_CreateTxdRxdResolve(0, 0);
	if(ret != 0)
		return ret;

	if(TmsTrade.fnum <= 0)
	{
		memset(TmsTrade.respCode, 0, sizeof(TmsTrade.respCode));
		memset(TmsTrade.respMsg, 0, sizeof(TmsTrade.respMsg));
		return _TMS_E_NOFILES_D;
	}

	flen = GetFileSize_Api(_DOWN_STATUS_);
	if(flen > 0)
	{
		Tms_CompareVersions(&TmsTrade);
	}

	return ret;
}

int Tms_DownloadUrlFilesOneByOne()
{
	char tmp[32], dname[64];
	int i, Ret, curfidx;
	int clen, stlen=0, thisLen,tlen = 0, len=0;
	int PackLen=0;
	char *p = NULL;
	int flagresend = 0;

#ifdef DISP_STH
	ScrClrLine_Api(LINE2, LINE10);
#endif
	while((TmsTrade.file[TmsTrade.curfindex].status == STATUS_DLUNCOMPLETE) && (TmsTrade.curfindex < TmsTrade.fnum))
	{
		curfidx = TmsTrade.curfindex;
		if(TmsTrade.file[TmsTrade.curfindex].startPosi > TmsTrade.file[TmsTrade.curfindex].fsize)
		{
			Tms_LstDbgOutApp("Dfile size wrong startPosi:", (u8 *)&TmsTrade.file[TmsTrade.curfindex].startPosi, sizeof(TmsTrade.file[TmsTrade.curfindex].startPosi), 0);
			Tms_LstDbgOutApp("Dfile size wrong fsize:", (u8 *)&TmsTrade.file[TmsTrade.curfindex].fsize, sizeof(TmsTrade.file[TmsTrade.curfindex].fsize), 0);
			Ret = _TMS_E_FILESIZE;
			break;
		}

		if(TmsTrade.file[TmsTrade.curfindex].startPosi == 0)
		{
			memset(&TmsTrade.context, 0, sizeof(TmsTrade.context));
			_tms_MD5Init (&TmsTrade.context);
			Tms_LstDbgOutApp("_tms_MD5Init:", NULL, 0, 1);
		}

		memset(dname, 0, sizeof(dname));
		Tms_getDomainName(TmsTrade.file[TmsTrade.curfindex].filePath, dname);

		flagresend = 0;
		for(i=TmsTrade.file[TmsTrade.curfindex].startPosi; i<TmsTrade.file[TmsTrade.curfindex].fsize; i+=EXFCONTENT_LEN)
		{
REQUESTFILE:
			memset(TmsTrade.respCode, 0, sizeof(TmsTrade.respCode));
			memset(TmsTrade.respMsg, 0, sizeof(TmsTrade.respMsg));

			memset(t_sBuf, 0, SENDPACKLEN);

			thisLen = (TmsTrade.file[TmsTrade.curfindex].fsize-TmsTrade.file[TmsTrade.curfindex].startPosi)>EXFCONTENT_LEN?EXFCONTENT_LEN:(TmsTrade.file[TmsTrade.curfindex].fsize-TmsTrade.file[TmsTrade.curfindex].startPosi);

			sprintf((char *)t_sBuf, "GET %s HTTP/1.1\r\n", TmsTrade.file[TmsTrade.curfindex].filePath);
			strcat((char *)t_sBuf, "Accept: image/gif, image/x-xbitmap, image/jpeg, image/pjpeg, */*\r\n");
			strcat((char *)t_sBuf, "User-Agent: Mozilla/4.0 (compatible; MSIE 5.5; Windows 98)\r\n");
			sprintf((char *)t_sBuf+strlen((char *)t_sBuf), "Range: bytes=%d-%d\r\n", TmsTrade.file[TmsTrade.curfindex].startPosi, TmsTrade.file[TmsTrade.curfindex].startPosi+thisLen-1);
			sprintf((char *)t_sBuf+strlen((char *)t_sBuf), "Host: %s\r\n\r\n", dname); //"ipos-n.jiewen.com.cn"
			PackLen = strlen((char *)t_sBuf);
			Tms_LstDbgOutApp("request:", t_sBuf, PackLen, 1);
			Ret = dev_send(t_sBuf,  PackLen);
			if(Ret != 0)
			{
				Tms_LstDbgOutApp("DUF CommTxd_Api:", (u8 *)&Ret, sizeof(Ret), 0);
				if(Ret != 0xff)
				{
					Ret = _TMS_E_SEND_PACKET;
					break;
				}

				Ret = Tms_ReSend(t_sBuf,  PackLen);
				if(Ret != 0)
				{
					Ret = _TMS_E_SEND_PACKET;
					break;
				}
			}

			tlen = 0;
			memset(t_rBuf, 0, RECVPACKLEN);
			while(1)
			{
				Tms_StartJumpSec();
				Ret = dev_recv(t_rBuf+tlen, RECEIVE_BUF_SIZE - 1 - tlen, RECEIVE_TIMEOUT);
				Tms_StopJumpSec();
				if (Ret <= 0) {
					MAINLOG_L1("receive error dev_recv ret:%d", Ret);
					if(flagresend == 1) //receive failed twice for same request ,then failed
					{
						Ret = _TMS_E_RECV_PACKET;
						break;
					}
					flagresend = 1;
					goto REQUESTFILE; //try to send request again
				}

				len = Ret;
				Ret = 0;
				//Tms_LstDbgOutApp("receive:", t_rBuf+tlen, len, 0);
				tlen += len;
				if(Tms_getContentLen(t_rBuf, &clen, &stlen) != 0)
					continue;


				if((p = strstr((char *)t_rBuf, "\n")) == NULL)  //\r\n
					return _TMS_E_RESOLVE_PACKET;
				memset(tmp, 0, sizeof(tmp));
				memcpy(tmp, t_rBuf, ((unsigned char*)p)-t_rBuf-1);
				if((strstr(tmp, "200") == NULL) && (strstr(tmp, "206") == NULL))
				{
					memset(TmsTrade.respMsg, 0, sizeof(TmsTrade.respMsg));
					if(strlen(tmp) > (sizeof(TmsTrade.respMsg)-1))
						memcpy(TmsTrade.respMsg, tmp, sizeof(TmsTrade.respMsg)-1);
					else
						memcpy(TmsTrade.respMsg, tmp, strlen(tmp));
					return _TMS_E_TRANS_FAIL;
				}

				p = strstr((char *)t_rBuf, "\r\n\r\n");
				if(tlen > stlen)
				{
					MAINLOG_L1("receive error 3 tlen:%d stlen:%d", tlen, stlen);
					Ret = _TMS_E_RECV_PACKET;
					break;
				}
				else if(tlen == stlen)
				{
					flagresend = 0;
					if((tlen-(p+4-(char *)t_rBuf)) > 0)
					{
#ifdef DISP_STH
						ScrClrLine_Api(LINE3, LINE4);
						memset(tmp, 0, sizeof(tmp));
						sprintf(tmp, "%d%% %d/%d", (TmsTrade.file[TmsTrade.curfindex].startPosi*100)/TmsTrade.file[TmsTrade.curfindex].fsize, TmsTrade.file[TmsTrade.curfindex].startPosi, TmsTrade.file[TmsTrade.curfindex].fsize);
						ScrDisp_Api(LINE3, 0, TmsTrade.file[TmsTrade.curfindex].name, FDISP|CDISP);
						ScrDisp_Api(LINE4, 0, tmp,FDISP|CDISP);
#endif
						Ret = Tms_WriteFile(&TmsTrade.file[TmsTrade.curfindex], (unsigned char*)(p+4), thisLen);
						if(Ret != 0)
						{
							Ret = _TMS_E_FILE_WRITE;
							Tms_LstDbgOutApp("Retwr2:", (u8 *)&Ret, sizeof(Ret), 0);
							break;
						}
					}

					if(Tms_NeedReConnect(t_rBuf) == 0) //reconnect
					{
						dev_disconnect();
						Ret = TmsConnect_Api();
						Tms_LstDbgOutApp("TmsConnect_Api:", (u8 *)&Ret, sizeof(Ret), 0);
						if(Ret != 0)
							Ret = _TMS_E_ERR_CONNECT;
					}
					break;
				}
			}//while(1)
			if(Ret != 0)
			{
				Tms_LstDbgOutApp("in while:", (u8 *)&Ret, sizeof(Ret), 0);
				break;
			}

			if(TmsTrade.file[curfidx].startPosi > TmsTrade.file[curfidx].fsize)
			{
				Ret = _TMS_E_FILESIZE;
				Tms_LstDbgOutApp("recv>real recv:", (u8 *)&TmsTrade.file[curfidx].startPosi, sizeof(TmsTrade.file[curfidx].startPosi), 0);
				Tms_LstDbgOutApp("recv>real real:", (u8 *)&TmsTrade.file[curfidx].fsize, sizeof(TmsTrade.file[curfidx].fsize), 0);
				break;
			}
			if(TmsTrade.file[curfidx].status == STATUS_DLCOMPLETE)
			{
#ifdef DISP_STH
				ScrClrLine_Api(LINE3, LINE4);
				memset(tmp, 0, sizeof(tmp));
				sprintf(tmp, "100%% %d/%d", TmsTrade.file[curfidx].startPosi, TmsTrade.file[curfidx].fsize);
				ScrDisp_Api(LINE3, 0, TmsTrade.file[curfidx].name, FDISP|CDISP);
				ScrDisp_Api(LINE4, 0, tmp,FDISP|CDISP);
#endif
				break;
			}
		}//for - one file
		if(Ret != 0)
			break;
	}//while --all files
	if(Ret != 0)
		dev_disconnect();
	return Ret;
}

int Tms_Notify(void)
{
	int ret=0, i, flag = 0;

	for(i=0; i<TmsTrade.fnum; i++)
	{
		if(TmsTrade.file[i].status == STATUS_DLUNCOMPLETE)
		{
			flag = 1;
			break;
		}
	}

	if(flag == 1)
		return _TMS_E_DOWNUNCOMPLETE;

	memset(TmsTrade.respCode, 0, sizeof(TmsTrade.respCode));
	memset(TmsTrade.respMsg, 0, sizeof(TmsTrade.respMsg));
	TmsTrade.trade_type = TYPE_NOTIFY;

	for(i=0; i<TmsTrade.fnum; i++)
	{
		ret = Tms_CreateTxdRxdResolve(i, 0);
		if(ret != 0)
			return ret;
	}

	return ret;
}

int Tms_TermLocationUpload(int tranmode)
{
	memset(TmsTrade.respCode, 0, sizeof(TmsTrade.respCode));
	memset(TmsTrade.respMsg, 0, sizeof(TmsTrade.respMsg));
	TmsTrade.trade_type = TYPE_LOCATION_UPLOAD;
	return Tms_CreateTxdRxdResolve(0, G_sys_param.commode);
}

int Tms_CommProcess()
{
	int ret=0, tipline;
	if(isSmallMainScreen())
		tipline = LINE2;
	else
		tipline = LINE3;
	
#ifdef DISP_STH
	ScrDisp_Api(tipline, 0, "Request task...", CDISP);
#endif
	ret = Tms_RequestTermTask();
	Tms_LstDbgOutApp("Tms_RequestTermTask:", (u8 *)&ret, sizeof(ret), 0);
	if(ret != 0)
		return ret;

	Tms_LstDbgOutApp("trade_type:", (u8 *)&TmsTrade.trade_type, sizeof(TmsTrade.trade_type), 0);
	if(TmsTrade.trade_type == TYPE_UPDATE)
		return 0;

	if(TmsTrade.trade_type == TYPE_NOTIFY)
		goto _TMS_NOTIFY_;

	AppPlayTip("Downloading");
	ret = Tms_DownloadUrlFilesOneByOne();
	Tms_LstDbgOutApp("Tms_DownloadUrlFilesOneByOne:", (u8 *)&ret, sizeof(ret), 0);
	if(ret != 0)
		return ret;

_TMS_NOTIFY_:

	ret = Tms_Notify();
	if(ret == 0)
	{
		TmsTrade.trade_type = TYPE_UPDATE;
		WriteFile_Api(_DOWN_STATUS_, (unsigned char *)&TmsTrade, 0, sizeof(TmsTrade));
	}

 	return ret;
}

/**************** Functions about flow End **************************/



/**************** Other functions Start **************************/
void Tms_DelAllDownloadInfo(struct __TmsTrade__ *GFfile)
{
	int i;
	char tmp[64], buf[64];

	for(i = 0; i <= GFfile->curfindex; i++)
	{
		if((strcmp(GFfile->file[i].type, TYPE_FIRMWARE) == 0) || (strcmp(GFfile->file[i].type, TYPE_APP) == 0)
				|| (strcmp(GFfile->file[i].type, TYPE_LIB) == 0)  || (strcmp(GFfile->file[i].type, TYPE_WIFIBSP) == 0))
		{
			memset(tmp, 0, sizeof(tmp));
			sprintf(tmp, "%s%s", TMS_FILE_DIR, GFfile->file[i].name);
			DelFile_Api(tmp);
		}
		else if(strcmp(GFfile->file[i].type, TYPE_PARAM) == 0)
		{
			memset(tmp, 0, sizeof(tmp));
			memset(buf, 0, sizeof(buf));
			sprintf(tmp, "%s%s", TMS_FILE_DIR, GFfile->file[i].name);
			strcat(buf, ".bak");
			DelFile_Api(tmp);
			DelFile_Api(buf);
		}
	}
	DelFile_Api(_DOWN_STATUS_);
}

//return : 1-not match     0-match     //Curr:current     GFfile:get from file
int Tms_CompareVersions(struct __TmsTrade__ *Curr)
{
	int i, flag = 0, ret, len;
	struct __TmsTrade__ GFfile;

	len = sizeof(struct __TmsTrade__);
	ret = ReadFile_Api(_DOWN_STATUS_, (unsigned char*)&GFfile, 0, (unsigned int *)&len);
	if(ret != 0)
		goto _TMS_DEL_ALL;

	if((Curr->fnum != GFfile.fnum) )  //file exist // need to compare all files and versions
		goto _TMS_DEL_ALL;

	for(i = 0; i <= GFfile.fnum; i++)
	{
		if((strcmp(GFfile.file[i].name, Curr->file[i].name) == 0) && (strcmp(GFfile.file[i].type, Curr->file[i].type) == 0)
			&& (strcmp(GFfile.file[i].version, Curr->file[i].version) == 0) && (GFfile.file[i].fsize == Curr->file[i].fsize))
		{
			continue;
		}
		else
		{
			flag = 1;
			break;
		}
	}
	if(flag == 0)  //all same, use information in the files
	{
		memcpy(Curr, &GFfile, sizeof(GFfile));
		return 0;
	}

_TMS_DEL_ALL :
	Tms_DelAllDownloadInfo(&GFfile);
	return 1;
}

int Tms_getDomainName(char *url, char *DomainName)
{
	char *p = NULL,  *q = NULL;

	if((p = strstr(url, "//")) == NULL)
		return -1;
	if((q = strstr(p+2, "/")) == NULL)
		return -1;

	memcpy(DomainName, p+2, (q-p-2));
	return (q-p-2);
}

int Tms_Trim(u8 *str, u8 *out)
{
	u8 *p = NULL;
	u8 *q = NULL;

	if((str == NULL) || (strlen((char *)str)<=0))
		return -1;

	p = str;
	q = str+strlen((char *)str)-1;
	while(p<=q)
	{
		if(*p != ' ')
			break;
		p++;
	}

	while(q>=p)
	{
		if(*q != ' ')
			break;
		q--;
	}
	memcpy(out, p, q-p+1);
	return 0;
}

int Tms_GetFirmwareVerSion(char *VerSion)
{
	unsigned char ver[64];
	int ret;

	memset(ver, 0, sizeof(ver));
	ret = sysReadBPVersion_Api(ver);
	MAINLOG_L1("TagTms_sysReadBPVersion == %d, version: %s", ret, ver);
	if(ret < 0)
		return -1;

	memcpy(VerSion, ver, strlen((const char*)ver));

	return 0;
}

//return :  0-update necessary      Others-download unnecessary
int Tms_CheckNeedDownFile(struct __FileStruct__ *file)
{
	char	ver[64];

	memset(ver, 0, sizeof(ver));
	if(strcmp(file->type, TYPE_APP) == 0) //APP version
	{
		if(strcmp(file->version, TmsStruct.oldAppVer) > 0)
			return 0;
		else
			return 1;
	}
	else if(strcmp(file->type, TYPE_PARAM) == 0)
	{
		return 0;
	}
	else if(strcmp(file->type, TYPE_FIRMWARE) == 0)
	{
		return CompareFWVersion(file->version, 0);  //for fw ota - 78.16.04 to 78.16.03, version like this: V1.03.11.781604to781603  ; 4th part contain base version and new version after upgrade
	}
	else if(strcmp(file->type, TYPE_LIB) == 0)
	{
		return CompareVosVersion(file->version); //for vos V1.01.11, version like this: V1.03.11.10111 ;
	}
	else if(strcmp(file->type, TYPE_WIFIBSP) == 0)
	{
		return CompareFWVersion(file->version, 1);
	}
	return 1;
}

int CompareFWVersion(char *newVer, int flag)
{
	char	ver[64];
	int 	ret, len, j;
	char 	*ptr = NULL, tmp[64], i, *ptr1 = NULL;

	memset(ver, 0, sizeof(ver));
	memset(tmp, 0, sizeof(tmp));
	if(flag == 1) //Q181Q190Y WIFI BSP
	{
		ret = wifiGetUserVersion_Api(ver, sizeof(ver));
		if(ret == -6300)
		{
			wifiOpen_Api();
			ret = wifiGetUserVersion_Api(ver, sizeof(ver));
			if(G_sys_param.commode == TRANCOMM_SIM)
				wifiClose_Api();
		}
		MAINLOG_L1("wifiGetUserVersion_Api ret:%d ver:%s",  ret, ver);
		if(ret <= 0)
		{
			Tms_DbgOutApp("WIFI BP ver:", (unsigned char*)"not fond !", strlen("not fond !"));
			return 1;
		}

		j = 0;
		for(i=1; i<strlen(ver); i++)
		{
			if(ver[i] != '.')
				tmp[j++] = ver[i];  //current wifi BSP version: V4.02.06 ->40206
		}

		j = 0;
		ptr = newVer;  //V1.07.03.40308
		for(i = 0; i < 3; i++)
		{
			if((ptr = strstr(ptr, ".")) == NULL)
				return 1;
			ptr += 1;
		}
		MAINLOG_L1("ptr:%s: tms:%s:",  ptr, tmp);
		if(strcmp(ptr, tmp) > 0)
			return 0; //have not updated
		else
			return 1; //have updated
	}
	else if(flag == 0) //firmware/BSP
	{
		ret = sysReadBPVersion_Api((unsigned char *)ver);
		if(ret <= 0)
		{
			Tms_DbgOutApp("BP ver:", (unsigned char*)"not fond !", strlen("not fond !"));
			return 1;
		}

		MAINLOG_L1("BP Version == %d, version: %s", ret, ver);

		j = 0;
		ptr = ver;
		len = strlen(ptr);
		for(i = 0; i < len; i++)
		{
			if(*ptr != '.')
				tmp[j++] = *ptr;  //current VOS version
			ptr++;
		}
		Tms_DbgOutApp("old BP ver:", (unsigned char*)tmp, strlen(tmp));  //old vos ver: dlen-25 :240306_19:37:37-V1.01.10

		j = 0;
		ptr = newVer;  //V1.03.11.10111
		for(i = 0; i < 3; i++)
		{
			if((ptr = strstr(ptr, ".")) == NULL)
				return 1;
			ptr += 1;
		}
		Tms_DbgOutApp("new BP ver:", (unsigned char*)ptr, strlen(ptr));

		ptr1 = strstr(ptr, "T");
		if(ptr1 == NULL)
			ptr1 = strstr(ptr, "t");
		if(ptr1 == NULL)
			return 1; // not got "To"/"to"/"TO"/"tO"

		if((ptr1-ptr == strlen(tmp)) && (memcmp(ptr, tmp, strlen(tmp)) == 0) && (memcmp(ptr1+2, tmp, strlen(tmp)) > 0))
			return 0;
		else
			return 1;
	}
}

int CompareVosVersion(char *newVer)
{
	char	ver[64];
	int 	ret = 0, len, j;
	char 	*ptr = NULL, tmp[64], i;

	memset(ver, 0, sizeof(ver));
	memset(tmp, 0, sizeof(tmp));

#ifndef __MACHINE_Q181Q190__ //Q161/Q161Pro
	ret = sysReadVerInfo_Api(1, (unsigned char *)ver); ////Q161 : 240523_14:58:55-V1.01.11
#endif

	if(ret == 0)
		MAINLOG_L1("vos Version == %d, version: %s", ret, ver);
	else
	{
		Tms_DbgOutApp("vos ver:", (unsigned char*)"get failed !", strlen("get failed !"));
		return	1;
	}

	ptr = strstr(ver, "-V");
	if(ptr == NULL)
	{
		Tms_DbgOutApp("vos ver:", (unsigned char*)"no -V", strlen("no -V"));
		return 1;
	}

	j = 0;
	ptr += 2;
	len = strlen(ptr);
	for(i = 0; i <= len; i++)
	{
		if(*ptr != '.')
			tmp[j++] = *ptr;  //current VOS version
		ptr++;
	}
	Tms_DbgOutApp("old vos ver:", (unsigned char*)tmp, strlen(tmp));  //old vos ver: dlen-25 :240306_19:37:37-V1.01.10

	j = 0;
	ptr = newVer;  //V1.03.11.10111
	for(i = 0; i < 3; i++)
	{
		if((ptr = strstr(ptr, ".")) == NULL)
			return 1;
		ptr += 1;
	}
	Tms_DbgOutApp("new vos ver:", (unsigned char*)ptr, strlen(ptr));

	if(strcmp(ptr, tmp) > 0)
		return 0;
	else
		return 1;
}

int Tms_GetFileSize(struct __FileStruct__ *file)
{
	char tmp[64];

	memset(tmp, 0, sizeof(tmp));
	sprintf(tmp, "%s%s", TMS_FILE_DIR, file->name);
	if((memcmp(file->type, TYPE_APP, 3) == 0) || (memcmp(file->type, TYPE_FIRMWARE, 2) == 0)
			|| (memcmp(file->type, TYPE_LIB, 2) == 0) || (memcmp(file->type, TYPE_WIFIBSP, 2) == 0))
		return GetFileSize_Api(tmp);
	else if(memcmp(file->type, TYPE_PARAM, 2) == 0)
	{
		strcat(tmp, ".bak");
		return GetFileSize_Api(tmp);
	}
	return 0;
}

int Tms_WriteFile(struct __FileStruct__ *file, unsigned char *Buf,unsigned int Length)
{
	int ret = -1, flen=0;
	char tmp[128];


	memset(tmp, 0, sizeof(tmp));
	sprintf(tmp, "%s%s", TMS_FILE_DIR, file->name);
	if(strcmp(TYPE_PARAM, file->type) == 0)
		strcat(tmp, ".bak");

	flen = GetFileSize_Api(tmp);
	ret = WriteFile_Api(tmp, Buf, file->startPosi, Length);
	if(ret != 0)
		return _TMS_E_FILE_WRITE;

	file->startPosi += Length;
	_tms_MD5Update(&TmsTrade.context, (unsigned char*)Buf, Length);

	if(file->startPosi == file->fsize)
	{
		//check md5
		memset(tmp, 0, sizeof(tmp));
		_tms_MD5Final((unsigned char*)tmp, &TmsTrade.context);
		if(memcmp(file->md5, tmp, 16) != 0)
		{
			Tms_LstDbgOutApp("cal md5:", (unsigned char *)tmp, 16, 0);
			Tms_LstDbgOutApp("host md5:", (unsigned char *)file->md5, 16, 0);
			file->startPosi = 0;
			Tms_DelFile(file);
			WriteFile_Api(_DOWN_STATUS_, (unsigned char *)&TmsTrade, 0, sizeof(TmsTrade));
			return _TMS_E_MD5_ERR;
		}
		else
		{
			file->status = STATUS_DLCOMPLETE;
			TmsTrade.curfindex++;
		}
	}
	else if(file->startPosi > file->fsize)
	{
		file->startPosi = 0;
		Tms_DelFile(file);
		WriteFile_Api(_DOWN_STATUS_, (unsigned char *)&TmsTrade, 0, sizeof(TmsTrade));
		return _TMS_E_FILESIZE;
	}

	ret = WriteFile_Api(_DOWN_STATUS_, (unsigned char *)&TmsTrade, 0, sizeof(TmsTrade));
	if(ret != 0)
		return _TMS_E_FILE_WRITE;

	if(Gfunflag == 1)
	{
		memset(tmp, 0, sizeof(tmp));
		sprintf(tmp, "fsizebeforewrite:%d writestart:%d writelen:%d", flen, file->startPosi, Length);
		Tms_LstDbgOutApp("writelen:", (unsigned char *)tmp, strlen(tmp), 1);
	}
	return 0;
}

void Tms_DelFile(struct __FileStruct__ *file)
{
	char tmp[64];

	memset(tmp, 0, sizeof(tmp));
	sprintf(tmp, "%s%s", TMS_FILE_DIR, file->name);

	if(strcmp(TYPE_PARAM, file->type) == 0)
		strcat(tmp, ".bak");

	DelFile_Api(tmp);
}

//return:  0-zip file   1-non zip file
int tms_filezipcheck(char *filename)
{
	if(strstr(filename, ".zip") != NULL)
		return 0;
	return 1;
}


void GetFirmwareModuleData(char *firm, char *module)
{
	int ret;
	char *type = NULL, buf[64];

	//01-B(bluetooth)    02-G(2G)   03-L(4G)   11-LG(4G,2G)
	type = VTMS_GetWirelessNetType();  //like : 4G,2G
	MAINLOG_L1("VTMS_GetWirelessNetType :%s", type);
	if(strstr(type, "2G")){
		if(strstr(type, "4G"))
			strcpy(firm, "11");
		else
			strcpy(firm, "02");
	}
	else if(strstr(type, "4G"))
		strcpy(firm, "03");
	else
		strcpy(firm, "00");

	memset(buf, 0, sizeof(buf));
	ret = VTMS_GetSysModuleName(buf);  //Q161-L610-LG //Q181YK: 17 ,  Q181-L511C-X2S-CN
	MAINLOG_L1("VTMS_GetSysModuleName ret:%d buf:%s", ret, buf);
	if(ret <= 0)
		return ;

	//01-MC6699 	02-LC610    03-L610  04-L509 (V66) //if undefined in protocol, define yourself, make sure it match module in the file name
	if(strstr(buf, "MC6699"))
		strcpy(module, "01");
	else if(strstr(buf, "V77-LC610"))
		strcpy(module, "10");
	else if(strstr(buf, "LC610"))
		strcpy(module, "02");
	else if(strstr(buf, "Q161-L610-LG"))
		strcpy(module, "03");
	else if(strstr(buf, "L509"))
		strcpy(module, "04");
	else if(strstr(buf, "Q161Pro-L610-LG"))
		strcpy(module, "05");
	else if(strstr(buf, "L610")) //Q181L
		strcpy(module, "06");
	else if(strstr(buf, "L511C")) //Q181Q190Y : "L511C-X2S-CN"/"L511C-2S-EU"/"L511C-2S-LA"/"L511C-2S-ELA"
		strcpy(module, "07");
	else if(strstr(buf, "ZX800")) //Q181Q190Z : "Q181-ZX800-SG-CA" / "Q181-ZX800-SG-CS" / "Q181-ZX800-VG-CA"
		strcpy(module, "08");
	else if(strstr(buf, "EC200A_AUAB")) //V66
		strcpy(module, "09");
	else
		strcpy(module, "00");
}

#define ERR_NO_CONTENTLEN  -1
#define ERR_CONLEN_FORMAT_WRONG  -2
//Clen:content length      Tlen:total length
int Tms_getContentLen(u8 *packdata, int *Clen, int *Tlen)
{
	u8 *p = NULL;
	char *q = NULL,  *s = NULL, *e = NULL;
	u8 tmp[16], buf[16];

	p = packdata;
	if((q = strstr((char *)packdata, (char *)"\r\n\r\n")) == NULL)
	{
		return ERR_NO_CONTENTLEN;
	}

	if((s = strstr((char *)p, (char *)"Content-Length:")) == NULL)
	{
		return ERR_NO_CONTENTLEN;
	}

	s += strlen("Content-Length:");
	if((e = strstr(s, "\r\n")) == NULL)
	{
		return ERR_CONLEN_FORMAT_WRONG;
	}

	if(e > q)
	{
		return ERR_CONLEN_FORMAT_WRONG;
	}

	memset(buf, 0, sizeof(buf));
	memset(tmp, 0, sizeof(tmp));
	memcpy(tmp,	s, e-s);
	if(Tms_Trim(tmp, buf) != 0)
		*Clen = 0;
	else
		*Clen = (int)atoi((char *)buf);

	*Tlen = (q-(char *)packdata+4+(*Clen));
	return 0;
}

//reconnect or not  ,  0:reconnect   others:unnecessary
int Tms_NeedReConnect(u8 *packdata)
{
	u8 *p = NULL,  *q = NULL, *s = NULL, *e = NULL;
	u8 tmp[16], buf[16];

	p = packdata;
	if((q = (u8 *)strstr((char *)packdata, "\r\n\r\n")) == NULL)
		return -1;

	if((s = (u8 *)strstr((char *)p, "Connection:")) == NULL)
		return -1;

	s += strlen("Connection:");
	if((e = (u8 *)strstr((char *)s, "\r\n")) == NULL)
		return -1;

	if(e > q)
		return -1;

	memset(buf, 0, sizeof(buf));
	memset(tmp, 0, sizeof(tmp));
	memcpy(tmp,	s, e-s);
	if(Tms_Trim(tmp, buf) != 0)
		return -1;

	if(strcmp((char *)buf, "close") == 0)
		return 0;

	return -1;
}

//return:  0-success    Others-failed
int Tms_ReConnect()
{
	int i, ret;

	for(i=0; i<2; i++)
	{
		dev_disconnect();
		ret = TmsConnect_Api();
		if(ret == 0)
			break;
	}
	return ret;
}

////only for case 0xff
//return : 0-success   _TMS_E_SEND_PACKET--reconnect or send failed
int Tms_ReSend(u8 *packData, int PackLen)
{
	int ret;
#ifdef DISP_STH
	ScrDisp_Api(LINE2, 0, "ReConnecting..", FDISP|CDISP);
#endif
	ret = Tms_ReConnect();
#ifdef DISP_STH
	ScrClrLine_Api(LINE2, LINE2);
#endif
	if(ret != 0)
		return _TMS_E_SEND_PACKET;

	ret = dev_send((unsigned char *)packData,  PackLen);
	if(ret != 0)
	{
		Tms_LstDbgOutApp("Tms_ReSend CommTxd_Api:", (u8 *)&ret, sizeof(ret), 0);
		return _TMS_E_SEND_PACKET;
	}
	return ret;
}

void Tms_PlayDisplayTips(char *str)
{
	AppPlayTip(str);
#ifdef DISP_STH
	if(isSmallMainScreen())
		ScrDisp_Api(LINE2, 0, str, CDISP);
	else
		ScrDisp_Api(LINE3, 0, str, CDISP);
	Delay_Api(4000);
#endif
}

void Tms_GetErrMsgByCode(char *code, char *msg)
{
	if(memcmp(code, "11", 2) == 0)
		sprintf(msg, "%s:Verify token failed", code);
	else if(memcmp(code, "12", 2) == 0)
		sprintf(msg, "%s:Token expired", code);
	else if(memcmp(code, "21", 2) == 0)
		sprintf(msg, "%s:Upload TERM Info first", code);
	else if(memcmp(code, "31", 2) == 0)
		sprintf(msg, "%s:TERM No. mismatch", code);
	else if(memcmp(code, "35", 2) == 0)
		sprintf(msg, "%s:Task ID mismatch", code);
	else if(memcmp(code, "36", 2) == 0)
		sprintf(msg, "%s:Item ID mismatch", code);
	else if(memcmp(code, "41", 2) == 0)
		sprintf(msg, "%s:MSG ID mismatch", code);
	else if(memcmp(code, "51", 2) == 0)
		sprintf(msg, "%s:Locate failed", code);
	else if(memcmp(code, "93", 2) == 0)
		sprintf(msg, "%s:HTTP body NULL", code);
	else if(memcmp(code, "94", 2) == 0)
		sprintf(msg, "%s:SIGN data NULL", code);
	else if(memcmp(code, "95", 2) == 0)
		sprintf(msg, "%s:Timestamp  NULL", code);
	else if(memcmp(code, "96", 2) == 0)
		sprintf(msg, "%s:version NULL", code);
	else if(memcmp(code, "97", 2) == 0)
		sprintf(msg, "%s:Sign data mismatch", code);
	else if(memcmp(code, "98", 2) == 0)
		sprintf(msg, "%s:Parameters missed", code);
	else if(memcmp(code, "99", 2) == 0)
		sprintf(msg, "%s:Internal system error", code);
	else
		sprintf(msg, "%s:Other error", code);
}

void Tms_GetErrorMsg(int errCode, char *msg)
{
	if(strlen(TmsTrade.respCode) == 2)
	{
		if(strlen(TmsTrade.respMsg) > 0)
			sprintf(msg, "%s:%s", TmsTrade.respCode, TmsTrade.respMsg);
		else
			Tms_GetErrMsgByCode(TmsTrade.respCode, msg);

		return ;
	}

	switch(errCode)
	{
	case _TMS_E_TRANS_FAIL:
		strcpy(msg, "Transaction failed");
		break;
	case _TMS_TERM_AUTH_UNDONE:
		strcpy(msg, "Not authenticated");
		break;
	case _TMS_E_ERR_CONNECT:
		strcpy(msg, "Connect server failed");
		break;
	case _TMS_E_SEND_PACKET:
		strcpy(msg, "Send failed");
		break;
	case _TMS_E_RECV_PACKET:
		strcpy(msg, "Receive failed");
		break;
	case _TMS_E_RESOLVE_PACKET:
		strcpy(msg, "Resolve failed");
		break;
	case _TMS_E_PREADDFILE:
		strcpy(msg, "Pre-add failed");
		break;
	case _TMS_E_TOO_MANY_FILES:
		strcpy(msg, "Too many updates");
		break;
	case _TMS_E_PACKAGE_WRONG:
		strcpy(msg, "Not for this terminal");
		break;
	case _TMS_E_FILE_WRITE:
		strcpy(msg, "Write failed");
		break;
	case _TMS_E_MD5_ERR:
		strcpy(msg, "MD5 error");
		break;
	case _TMS_E_DOWNUNCOMPLETE:
		strcpy(msg, "Download uncomplete");
		break;
	case _TMS_E_FILE_SEEK:
		strcpy(msg, "File seek error");
		break;
	case _TMS_E_FILESIZE:
		strcpy(msg, "File size error");
		break;
	case _TMS_E_NOFILES_D:
		strcpy(msg, "No update");
		break;
	case _TMS_E_MALLOC_NOTENOUGH:
		strcpy(msg, "Malloc error");
		break;
	default:
		sprintf(msg, "Error:%d", errCode);
		break;
	}
}

/**************** Other functions End **************************/



/**************** TMS LOG OUTPUT Start **************************/
//OnorOff: output or not : 0-off; 1-on;
//logDest: destination that log output :1-log written to file "dbgapp.txt"      2-log output to port
//port : port index
void OutputLogSwitch(int OnorOff, int logDest)
{
	if(OnorOff)
		Gfunflag = 1;
	else
		Gfunflag = 0;
	_tms_G_dbgOutDestApp = logDest;
}

#define LOGSPCS_BYTES 700  //450  //MAINLOG_L1 support 1k at most
#define LOGPRINT_MALLOC 1500 //1024
//output logs start
//type 0-BCD  1-ASC 2-%X 3-%d
void Tms_LstDbgOutApp(const char *title, unsigned char *pData, int dLen, u8 type)
{
	if(Gfunflag == 1)
	{
		int i, plen, tlen = dLen, titlen;
		char buf[LOGPRINT_MALLOC];

		memset(buf, 0, LOGPRINT_MALLOC);
		if((_tms_G_dbgOutDestApp & 0xff) == 2)
		{
			if(title != NULL)
				sprintf(buf+strlen(buf), "%s:", title);
			else
				sprintf(buf+strlen(buf), "%s", ":");

			if((pData == NULL) || (dLen <= 0))
			{
				MAINLOG_L1("TagTms_%s", buf);
				return ;
			}

			if(type==0)
			{
				titlen = strlen(buf);
				for(i = 0; i < (dLen+LOGSPCS_BYTES-1)/LOGSPCS_BYTES; i++)
				{
					memset(buf+titlen, 0, LOGPRINT_MALLOC-titlen);
					plen = tlen * 2 > LOGSPCS_BYTES*2 ? LOGSPCS_BYTES*2 : tlen * 2;
					BcdToAsc_Api(buf+titlen, pData+i*LOGSPCS_BYTES, plen);
					MAINLOG_L1("TagTms_%s", buf);
					tlen -= plen/2;
					if(tlen <= 0)
						break;
				}
				return ;
			}
			else if(type==1)
			{
				if(dLen > LOGSPCS_BYTES*2)
				{
					MAINLOG_L1("TagTms_%s", "print 900 bytes in front");
					memcpy(buf+strlen(buf), pData, LOGSPCS_BYTES*2);
				}
				else
					memcpy(buf+strlen(buf), pData, dLen);
			}
			else if(type==2)
			{
				sprintf(buf+strlen(buf), "0x%x", *pData);
			}
			else if(type == 3)
			{
				if(dLen == 1)
				{
					char aa;
					memcpy(&aa, pData, dLen);
					sprintf(buf+strlen(buf), "%d", aa);
				}
				else if(dLen == 2)
				{
					unsigned short aa;
					memcpy(&aa, pData, dLen);
					sprintf(buf+strlen(buf), "%d", aa);
				}
				else if(dLen == 4)
				{
					int aa;
					memcpy(&aa, pData, dLen);
					sprintf(buf+strlen(buf), "%d", aa);
				}
			}
			MAINLOG_L1("TagTms_%s", buf);
		}
	}
}

void Tms_DbgOutApp(const char *title, unsigned char *pData, int dLen)
{
	if(Gfunflag == 1)
	{
		char buf[LOGPRINT_MALLOC];

		memset(buf, 0, LOGPRINT_MALLOC);
		if((_tms_G_dbgOutDestApp & 0xff) == 2)
		{
			if(title != NULL)
				sprintf(buf+strlen(buf), "%s dlen-%d :", title, dLen);
			else
				sprintf(buf+strlen(buf), " dlen-%d :", dLen);

			if((pData != NULL) && (dLen > 0))
			{
				if(dLen > LOGSPCS_BYTES*2)
					memcpy(buf+strlen(buf), pData, LOGSPCS_BYTES*2);
				else
					memcpy(buf+strlen(buf), pData, dLen);

			}
			MAINLOG_L1("TagTms_%s", buf);
			if(strlen(buf) > 900)
				MAINLOG_L1("last_buf_%s", buf+900);
		}
	}
}
/**************** TMS LOG OUTPUT End **************************/



/**************** TMS Entrence API Start **************************/
//hostDomainName : necessary
int TmsParamSet_Api(char *hostIp, char *hostPort, char *hostDomainName)
{
	char tmp[64], ret;

	memset(&TmsTrade, 0, sizeof(struct __TmsTrade__));
	memset(&TmsStruct, 0, sizeof(struct __TmsStruct__));

	if((hostDomainName == NULL) || (strlen(hostDomainName) == 0))
		return -1;

	strcpy(TmsStruct.hostDomainName, hostDomainName);

	if((hostPort != NULL) && (strlen(hostPort) != 0))
		strcpy(TmsStruct.hostPort, hostPort);

	if((hostIp != NULL) && (strlen(hostIp) != 0))
		strcpy(TmsStruct.hostIP, hostIp);

	pRoot = NULL;
	strcpy(TmsStruct.version, "1.0");
	TmsStruct.tradeTimeoutValue = 60;

	//SN
	memset(tmp, 0, sizeof(tmp));
	ret = PEDReadPinPadSn_Api((unsigned char*)tmp);
	if(ret == 0)
	{
		int len = (int)AscToLong_Api((unsigned char*)tmp, 2);
		memcpy(TmsStruct.sn, tmp+2, len);
	}

	return 0;
}

//Connect to host/send receive data  start
int TmsConnect_Api()
{
	int ret=0;
	ret = dev_connect(&tmsEntry, CONNECT_TIMEOUT);
	if (ret != 0){
		return ret;
	}
	return 0;
}

int TmsTermAuth_Api(char commode)
{
	int ret;

	ret = Tms_BufMalloc();
	if(ret != 0)
		goto TMSTERMAUTHAPI;

	GtermAuthFlag = TMSAUTH_UNDONE;
	ret = Tms_DeviceAuth(commode);
	Tms_LstDbgOutApp("Tms_DeviceAuth:", (u8 *)&ret, sizeof(ret), 0);
	if(ret != 0)
		goto TMSTERMAUTHAPI;

	GtermAuthFlag = TMSAUTH_DONE;

	ret = Tms_TermInfoUpload();
	Tms_LstDbgOutApp("Tms_TermInfoUpload:", (u8 *)&ret, sizeof(ret), 0);

	ret = Tms_TermLocationUpload(commode);
	Tms_LstDbgOutApp("Tms_TermLocationUpload:", (u8 *)&ret, sizeof(ret), 0);

TMSTERMAUTHAPI:
	Tms_DisconnBufFree();
	return ret;
}

int TmsDownload_Api(char *appCurrVer)
{
	int ret;
	int tipline;

	if(isSmallMainScreen())
		tipline = LINE2;
	else
		tipline = LINE3;

	ret = Tms_BufMalloc();
	if(ret != 0)
	{
		Tms_DisconnBufFree();
		return ret;
	}

	if(GtermAuthFlag != TMSAUTH_DONE)
	{
#ifdef DISP_STH
		ScrDisp_Api(tipline, 0, "Terminal Auth...", CDISP);
#endif

		ret = Tms_DeviceAuth(G_sys_param.commode);
		Tms_LstDbgOutApp("Tms_DeviceAuth 1:", (u8 *)&ret, sizeof(ret), 0);
		if(ret != 0)
		{
			Tms_DisconnBufFree();
			return _TMS_TERM_AUTH_UNDONE;
		}

		GtermAuthFlag = TMSAUTH_DONE;

		ret = Tms_TermInfoUpload();
		Tms_LstDbgOutApp("Tms_TermInfoUpload 1:", (u8 *)&ret, sizeof(ret), 0);
		if(ret != 0)
		{
			Tms_DisconnBufFree();
			return ret;
		}
	}

	if(appCurrVer != NULL)
		strcpy(TmsStruct.oldAppVer, (char *)appCurrVer);

	ret = Tms_CommProcess();
	Tms_LstDbgOutApp("Tms_CommProcess:", (u8 *)&ret, sizeof(ret), 0);

	Tms_DisconnBufFree();
	return ret;
}

//0:success  -1:no config file   -2:download uncomplete  -3:update font/param files failed
int TmsUpdate_Api(char *appCurrVer)
{
	int i, len, ret, readLen, j;
	char tmp[64], buf[64], *p = NULL;

	struct __TmsTrade__ GFfile;

	len = sizeof(struct __TmsTrade__);
	ret = ReadFile_Api(_DOWN_STATUS_, (unsigned char *)&GFfile, 0, (unsigned int*)&len);
	if(ret != 0)
		return -1;

	for(i = 0; i < GFfile.fnum; i++)
	{
		if(GFfile.file[i].status == STATUS_DLUNCOMPLETE)//downloading not finished
			return -2;
	}

	//replace font and parameter files

	for(i = 0; i < GFfile.fnum; i++)
	{
		if(GFfile.file[i].status == STATUS_DLCOMPLETE) //downloading finished but not replaced
		{
			if(strcmp(GFfile.file[i].type, TYPE_PARAM) == 0)
			{
				memset(tmp, 0, sizeof(tmp));  //.bak name
				memset(buf, 0, sizeof(buf)); //old name
				sprintf(buf, "%s%s", TMS_FILE_DIR, GFfile.file[i].name);
				sprintf(tmp, "%s.bak", buf);
				DelFile_Api(buf);

				ret = ReNameFile_Api(tmp, buf);
				if(ret != 0)
					ret = ReNameFile_Api(tmp, buf);

				if(ret == 0)
				{
					char *p = strstr(GFfile.file[i].name, ".zip");
					if(p != NULL)
					{
						ret = fileunZip_Api((const char*)buf, (const char*)PARAM_FILE_DIR);
						MAINLOG_L1("fileunZip_Api 11 :%d  file:%s  dest:%s", ret, buf, TMS_FILE_DIR);
						DelFile_Api(buf);
					}
				}

				if(ret == 0)
				{
					GFfile.file[i].status = STATUS_UPDATE;
					WriteFile_Api(_DOWN_STATUS_, (unsigned char *)&GFfile, 0, sizeof(GFfile));
				}

				if(ret != 0)
				{
					Tms_LstDbgOutApp("TmsUpdate_Api rename:", (unsigned char *)tmp, strlen(tmp), 1);
					Tms_LstDbgOutApp("TmsUpdate_Api rename:", (unsigned char *)buf, strlen(buf), 1);
					Tms_LstDbgOutApp("TmsUpdate_Api rename ret:", (unsigned char *)&ret, sizeof(ret), 0);
					Tms_DelAllDownloadInfo(&GFfile);
					return -3;
				}
			}
		}
	}

	//update WIFI BSP
	for(i = 0; i < GFfile.fnum; i++)
	{
		if(GFfile.file[i].status == STATUS_DLCOMPLETE) //downloading finished but not replaced
		{
			if(strcmp(GFfile.file[i].type, TYPE_WIFIBSP) == 0)
			{
				ret = 0;
				memset(tmp, 0, sizeof(tmp));
				sprintf(tmp, "%s%s",  TMS_FILE_DIR, GFfile.file[i].name);
				p = strstr(GFfile.file[i].name, ".zip");
				if(p != NULL)
				{
					ret = fileunZip_Api((const char*)tmp, (const char*)TMS_FILE_DIR);
					MAINLOG_L1("fileunZip_Api WIFI BSP :%d", ret);
					DelFile_Api(tmp);
					if(ret == 0)
					{
						memcpy(p, ".bin", 4);
						WriteFile_Api(_DOWN_STATUS_, (unsigned char *)&GFfile, 0, sizeof(GFfile));
						memset(tmp, 0, sizeof(tmp));
						sprintf(tmp, "%s%s",  TMS_FILE_DIR, GFfile.file[i].name);
					}
				}

				if(ret == 0)
				{
					unsigned char Buff[4096];
					len = GetFileSize_Api(tmp);
					MAINLOG_L1("GetFileSize_Api 33 :%d", len);

					if(ret == 0)
					{
						if(G_sys_param.commode == TRANCOMM_SIM)
							wifiOpen_Api();

						for(j = 0; j < len;)
						{
							readLen = 4096;
							ret = ReadFile_Api(tmp, Buff, j, (unsigned int *)&readLen);
							if(ret != 0)
							{
								MAINLOG_L1("ReadFile_Api %s error ret:%d  start:%d readLen:%d", tmp, ret, j, readLen);
								break;
							}

							ret = wifiUpgradeSendData_Api(Buff, readLen, 0);
							if(ret != 0)
							{
								MAINLOG_L1("wifiUpgradeSendData_Api error ret:%d  start:%d readLen:%d", ret, j, readLen);
								break;
							}

							j += readLen;
						}

						ret = wifiUpgradeSendData_Api(NULL, 0, 1);
						if(ret != 0)
							MAINLOG_L1("wifiUpgradeSendData_Api error ret:%d  last package", ret);

						if(ret == 0)
						{
							int cver = CompareFWVersion(GFfile.file[i].version, 1);
							MAINLOG_L1("cver :%d", cver);
							if(cver != 0)
							{
								GFfile.file[i].status = STATUS_UPDATE;
								WriteFile_Api(_DOWN_STATUS_, (unsigned char *)&GFfile, 0, sizeof(GFfile));
								DelFile_Api(tmp);
							}
						}
						if(G_sys_param.commode == TRANCOMM_SIM)
							wifiClose_Api();
					}
				}

				if(ret != 0)
				{
					Tms_DelAllDownloadInfo(&GFfile);
					return -6;
				}
			}
		}
	}

	//update firmware
	for(i = 0; i < GFfile.fnum; i++)
	{
		if(GFfile.file[i].status == STATUS_DLCOMPLETE) //downloading finished but not replaced
		{
			if((strcmp(GFfile.file[i].type, TYPE_FIRMWARE) == 0) || (strcmp(GFfile.file[i].type, TYPE_LIB) == 0))
			{
				ret = 0;
				memset(tmp, 0, sizeof(tmp));
				sprintf(tmp, "%s%s",  TMS_FILE_DIR, GFfile.file[i].name);
				p = strstr(GFfile.file[i].name, ".zip");
				if(p != NULL)
				{
					ret = fileunZip_Api((const char*)tmp, (const char*)TMS_FILE_DIR);
					MAINLOG_L1("fileunZip_Api firmware :%d", ret);
					DelFile_Api(tmp);
					if(ret == 0)
					{
						memcpy(p, ".bin", 4);
						WriteFile_Api(_DOWN_STATUS_, (unsigned char *)&GFfile, 0, sizeof(GFfile));
						memset(tmp, 0, sizeof(tmp));
						sprintf(tmp, "%s%s",  TMS_FILE_DIR, GFfile.file[i].name);
						//MAINLOG_L1("TagTms_lib name %s", tmp);
					}
				}

				if(ret == 0)
				{
					if(strcmp(GFfile.file[i].type, TYPE_FIRMWARE) == 0)
					{
						ret = tmsUpdateFile_Api(TMS_FLAG_DIFFOS, tmp, NULL);
						MAINLOG_L1("TagTms_tmsUpdateFile_Api fw %s = %d", tmp, ret);
					}
					else
					{
						ret = tmsUpdateFile_Api(TMS_FLAG_VOS, tmp, NULL);
						MAINLOG_L1("TagTms_tmsUpdateFile_vos name %s = %d", tmp, ret);
					}
				}

				if(ret != 0)
				{
					Tms_DelAllDownloadInfo(&GFfile);
					return -4;
				}
			}
		}
	}


	//replace APP  //firmware must be updating before app because new lib may not work with old firmware
	for(i = 0; i < GFfile.fnum; i++)
	{
		if(GFfile.file[i].status == STATUS_DLCOMPLETE) //downloading finished but not replaced
		{
			if(strcmp(GFfile.file[i].type, TYPE_APP) == 0)
			{
				ret = 0;
				memset(tmp, 0, sizeof(tmp));
				sprintf(tmp, "%s%s",  TMS_FILE_DIR, GFfile.file[i].name);
				p = strstr(GFfile.file[i].name, ".zip");
				if(p != NULL)
				{
					ret = fileunZip_Api((const char*)tmp, (const char*)TMS_FILE_DIR);
					MAINLOG_L1("fileunZip_Api app:%d", ret);
					DelFile_Api(tmp);
					if(ret == 0)
					{
						memcpy(p, __APPSUFIX__, 4); //L: ".img"   E: ".bin"
						WriteFile_Api(_DOWN_STATUS_, (unsigned char *)&GFfile, 0, sizeof(GFfile));
						memset(tmp, 0, sizeof(tmp));
						sprintf(tmp, "%s%s",  TMS_FILE_DIR, GFfile.file[i].name);
					}
				}

				if(ret == 0)
				{
					MAINLOG_L1("tmsUpdateFile_Api app name :%s", tmp);
					ret = tmsUpdateFile_Api(TMS_FLAG_APP, tmp, NULL);
					MAINLOG_L1("TagTms_tmsUpdateFile_Api app = %d", ret);
				}
				else
				{
					Tms_DelAllDownloadInfo(&GFfile);
					return -5;
				}
			}
		}
	}

	DelFile_Api(_DOWN_STATUS_);
	return 0;
}

//0:all updated   -1:no config file   -2:download uncommplete  -3:font/param download over but not update       -4:application download over but not update   -5:LIB download over but not update
// 0 -1 :unnecessary to do anything           -2:download again       -3/-4/-5/-6:call TmsUpdate_Api again
int TmsStatusCheck_Api(char *appCurrVer)
{
	int i, len, ret;
	unsigned char tmp[64];

	struct __TmsTrade__ GFfile;

	len = sizeof(struct __TmsTrade__);
	ret = ReadFile_Api(_DOWN_STATUS_, (unsigned char *)&GFfile, 0, (unsigned int*)&len);
	if(ret != 0)
	{
		Tms_LstDbgOutApp("TmsStatusCheck_Api 1:", (u8 *)&ret, sizeof(ret), 0);
		return -1;
	}

	for(i = 0; i < GFfile.fnum; i++)
	{
		if(GFfile.file[i].status == STATUS_DLUNCOMPLETE)//downloading not finished
		{
			Tms_LstDbgOutApp("TmsStatusCheck_Api 2:", (u8 *)&ret, sizeof(ret), 0);
			return -2;
		}
	}

	for(i = 0; i < GFfile.fnum; i++)
	{
		if(GFfile.file[i].status == STATUS_DLCOMPLETE)//downloading finished but not replaced
		{
			if(strcmp(GFfile.file[i].type, TYPE_PARAM) == 0)
			{
				Tms_LstDbgOutApp("TmsStatusCheck_Api 3:", (unsigned char*)"font param download finished but not repaced", strlen("font param download finished but not repaced"), 1);
				return -3;
			}
			else if(strcmp(GFfile.file[i].type, TYPE_FIRMWARE) == 0)
			{
				memset(tmp, 0, sizeof(tmp));
				if(Tms_GetFirmwareVerSion((char *)tmp) == 0)
				{
					if(strcmp(GFfile.file[i].version, (char *)tmp) != 0)  //lib not latest
					{
						Tms_LstDbgOutApp("TmsStatusCheck_Api 51:", (unsigned char*)GFfile.file[i].version, strlen(GFfile.file[i].version), 1);
						Tms_LstDbgOutApp("TmsStatusCheck_Api 52:", tmp, strlen((char *)tmp), 1);
						return -5;
					}
				}
			}
			else if((strcmp(GFfile.file[i].type, TYPE_LIB) == 0) || (strcmp(GFfile.file[i].type, TYPE_WIFIBSP) == 0))
			{
				return -6;
			}
		}
	}

	for(i = 0; i < GFfile.fnum; i++)
	{
		if(GFfile.file[i].status == STATUS_DLCOMPLETE)//downloading finished but not replaced
		{
			if(strcmp(GFfile.file[i].type, TYPE_APP) == 0)
			{
				if(strcmp(GFfile.file[i].version, (char *)appCurrVer) != 0)  //APP not latest
				{
					Tms_LstDbgOutApp("TmsStatusCheck_Api 41:", (unsigned char*)GFfile.file[i].version, strlen(GFfile.file[i].version), 1);
					Tms_LstDbgOutApp("TmsStatusCheck_Api 42:", (unsigned char*)appCurrVer, strlen((char *)appCurrVer), 1);
					return -4;
				}
			}
		}
	}

	//all already updated
	DelFile_Api(_DOWN_STATUS_);
	return 0;
}

//terminal location upload
int TmsTermLocUpload_Api(char commode)
{
	int ret;

	if(GtermAuthFlag != TMSAUTH_DONE)
	{
		AppPlayTip("Authentication first please");
		MAINLOG_L1("Terminal UnAuthentication , Authenticate first please.");
		return _TMS_TERM_AUTH_UNDONE;
	}

	ret = TmsConnectServer();
	if(ret != 0)
		return ret;

	ret = Tms_BufMalloc();
	if(ret != 0)
	{
		Tms_DisconnBufFree();
		return ret;
	}

	if(GtermAuthFlag != TMSAUTH_DONE)
	{
#ifdef DISP_STH
		ScrDisp_Api(LINE3, 0, "Terminal Auth...", CDISP);
#endif

		ret = Tms_DeviceAuth(G_sys_param.commode);
		Tms_LstDbgOutApp("Tms_DeviceAuth 1:", (u8 *)&ret, sizeof(ret), 0);
		if(ret != 0)
		{
			Tms_DisconnBufFree();
			return _TMS_TERM_AUTH_UNDONE;
		}

		GtermAuthFlag = TMSAUTH_DONE;

		ret = Tms_TermInfoUpload();
		Tms_LstDbgOutApp("Tms_TermInfoUpload 1:", (u8 *)&ret, sizeof(ret), 0);
		if(ret != 0)
		{
			Tms_DisconnBufFree();
			return ret;
		}
	}

	ret = Tms_TermLocationUpload(commode);
	Tms_DisconnBufFree();

	return ret;
}

/**************** TMS Entrence API End **************************/



/**************** App sample code Start **************************/
int TmsConnectServer()
{
	int ret;

	t_sBuf = NULL;
	t_rBuf = NULL;
	memset(&tmsEntry, 0, sizeof(tmsEntry));
	tmsEntry.protocol = PROTOCOL_HTTPS;
	strcpy(tmsEntry.domain, _TMS_HOST_DOMAIN_);
	strcpy(tmsEntry.port, _TMS_HOST_PORT_);

	ret = TmsParamSet_Api(_TMS_HOST_IP_, _TMS_HOST_PORT_, _TMS_HOST_DOMAIN_);
	if(ret != 0)
	{
		AppPlayTip("Set parameters failed");
		MAINLOG_L1("TagTms_TmsParamSet_Api = %d", ret);
		return ret;
	}

#ifdef DISP_STH
	ScrClrLine_Api(LINE2, LINE5);
	if(isSmallMainScreen())
		ScrDisp_Api(LINE2, 0, "Connecting...", CDISP);
	else
		ScrDisp_Api(LINE3, 0, "Connecting...", CDISP);
#else
	AppPlayTip("Connecting VTMS server");
#endif
	ret = dev_connect(&tmsEntry, CONNECT_TIMEOUT);  //ret = TmsConnect_Api();
	if (ret != 0){
		MAINLOG_L1("TagTms_TmsConnect_Api = %d", ret);
		Tms_PlayDisplayTips("Connect VTMS server failed");
		return ret;
	}
	return ret;
}

int TmsTermAuthTest()
{
	int ret;

#ifdef DISP_STH
	ScrCls_Api();
	ScrDisp_Api(LINE1, 0, "TerminalAuth", CDISP);
#endif
	ret = TmsConnectServer();
	if(ret != 0)
		return ret;

	return TmsTermAuth_Api(G_sys_param.commode);
}

int TmsRemoteUpgrade()
{
	int ret;
	char tmp[128];
	
#ifdef DISP_STH
	ScrCls_Api();
	ScrDisp_Api(LINE1, 0, "VTMS APP Upgrade", CDISP);
#endif
	memset(tmp, 0, sizeof(tmp));
	sprintf(tmp, "%s%s", TMS_FILE_DIR, "1e");
	ret = GetFileSize_Api(tmp);
	if(ret <= 0)
	{
		ret = WriteFile_Api(tmp, (unsigned char*)"exist", 0, 5);
		if(ret != 0)
		{
			Tms_PlayDisplayTips("Creating folder failed");
			return ret;
		}
		ret = GetFileSize_Api(tmp);
		if(ret <= 0)
		{
			Tms_PlayDisplayTips("Creating folder failed");
			return ret;
		}
	}

	ret = TmsConnectServer();
	if(ret != 0)
		return ret;

	ret = TmsDownload_Api((char *)(App_Msg.Version));
	Tms_LstDbgOutApp("TmsDownload_Api:", (u8 *)&ret, sizeof(ret), 0);
	if(ret != 0)
	{
		memset(tmp, 0, sizeof(tmp));
		Tms_GetErrorMsg(ret, tmp);
		Tms_PlayDisplayTips(tmp);
		return ret;
	}

	AppPlayTip("Updating");
#ifdef DISP_STH
	ScrClrLine_Api(LINE3, LINE5);
	if(isSmallMainScreen())
		ScrDisp_Api(LINE2, 0, "Updating", CDISP);
	else
		ScrDisp_Api(LINE3, 0, "Updating", CDISP);
#endif
	ret = TmsUpdate_Api((char *)(App_Msg.Version));
	MAINLOG_L1("TagTms_Update = %d", ret);
	if(ret != 0)
	{
		Tms_PlayDisplayTips("Update failed");
	}

	return ret;
}

//check if update successfully
void CheckTmsStatus()
{
	int ret = 0;

	ret = TmsStatusCheck_Api((char *)(App_Msg.Version));
	if(ret == 0 || ret == -1) //update successfully  or no update information
		return ;
	else if(ret == -2)
	{
		AppPlayTip("Need to connect and download again");
	}
	else if(ret == -3 || ret == -4 || ret == -5 || ret == -6)
	{
		AppPlayTip("Update locally now");
		TmsUpdate_Api((char *)(App_Msg.Version));
	}
}

#ifdef __MACHINE_Q181Q190__
///tms part
void tms_TMSThread(void)
{
	int ret;

	while (1) {
		while (tms_tms_flag == 0) {
			Delay_Api(1000);
		}

		if(tms_tms_flag == 1)  //auth
		{
			MAINLOG_L1("TagTms_start TmsTermAuthTest");
			ret = TmsTermAuthTest();
			MAINLOG_L1("TmsTermAuthTest : %d", ret);
		}
		else if(tms_tms_flag == 2)
		{
			MAINLOG_L1("TagTms_start tms download");
			ret = TmsRemoteUpgrade();
			MAINLOG_L1("TmsRemoteUpgrade: %d", ret);
		}
		else if(tms_tms_flag == 3)
		{
			MAINLOG_L1("TagTms_start terminal location upload");
			ret = TmsTermLocUpload_Api(G_sys_param.commode);
			MAINLOG_L1("TmsTermLocUpload_Api: %d", ret);
		}
		tms_tms_flag = 0;
	}
}
#endif
/**************** App sample code End **************************/

/*
Function : Compare two files content in length 'len', output their content once different
Parameters:
	orgfile: file1
	downfile: file2
	len : the length of content that will be compared
*/
void CompareTwoFiles(char *orgfile, char *downfile, int len)
{
	int i, ret1, ret2, rlen, rlen1, rlen2, j;
	char *orgbuf, *nowbuf, *printbuf;

	if(len <= 0)
		return;
	orgbuf = malloc(2048);
	if(orgbuf == NULL)
	{
		MAINLOG_L1("orgbuf is null");
		return;
	}
	nowbuf = malloc(2048);
	if(nowbuf == NULL)
	{
		MAINLOG_L1("nowbuf is null");
		return;
	}
	printbuf = malloc(4097);
	if(printbuf == NULL)
	{
		MAINLOG_L1("printbuf is null");
		return;
	}

	MAINLOG_L1("loop");
	for(i = 0; i < len; i++)
	{
		rlen = (len-i) > 2048 ? 2048:(len-i);
		rlen1 = rlen2 = rlen;

		ret1 = ReadFile_Api(orgfile, orgbuf, i, (unsigned int *)&rlen1);
		MAINLOG_L1("ReadFile_Api 11:%d", ret1);
		if(ret1 != 0)
		{
			MAINLOG_L1("ReadFile_Api1 %s:%d start:%d rlen:%d", orgfile, ret1, i, rlen1);
			free(orgbuf);
			free(nowbuf);
			free(printbuf);
			return;
		}

		ret2 = ReadFile_Api(downfile, nowbuf, i, (unsigned int *)&rlen2);
		MAINLOG_L1("ReadFile_Api 22:%d", ret2);
		if(ret2 != 0)
		{
			MAINLOG_L1("ReadFile_Api2 %s:%d start:%d rlen:%d", downfile, ret2, i, rlen2);
			free(orgbuf);
			free(nowbuf);
			free(printbuf);
			return;
		}

		if(memcmp(orgbuf, nowbuf, rlen) != 0)
		{
			memset(printbuf, 0, 4097);
			BcdToAsc_Api(printbuf, orgbuf, rlen*2);

			MAINLOG_L1("--------------Orignal file data i:%d len:%d-----------", i, rlen);
			int plen;
			for(j = 0; j < rlen*2; )
			{
				plen = ((rlen*2)-800) > 800 ? 800 : ((rlen*2)-800);
				memset(orgbuf, 0, 2048);
				sprintf(orgbuf, "orgbuf %d data:", j);
				memcpy(orgbuf + strlen(orgbuf), printbuf+j, plen);
				MAINLOG_L1(orgbuf);
				j += plen;
			}
			MAINLOG_L1("--------------Orignal file data end-----------");

			MAINLOG_L1("--------------Download file data-----------");
			memset(printbuf, 0, 4097);
			BcdToAsc_Api(printbuf, nowbuf, rlen*2);
			for(j = 0; j < rlen*2; )
			{
				plen = ((rlen*2)-800) > 800 ? 800 : ((rlen*2)-800);
				memset(orgbuf, 0, 2048);
				sprintf(orgbuf, "orgbuf %d data:", j);
				memcpy(orgbuf + strlen(orgbuf), printbuf+j, plen);
				MAINLOG_L1(orgbuf);
				j += plen;
			}
			MAINLOG_L1("--------------Download file data end-----------");
			break ;
		}
		i += rlen;
	}
	free(orgbuf);
	free(nowbuf);
	free(printbuf);
}

