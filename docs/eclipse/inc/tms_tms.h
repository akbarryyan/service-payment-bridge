#ifndef _tms_HX_TMS_H_
#define _tms_HX_TMS_H_

#include <coredef.h>
#include <string.h>
#include <struct.h>
#include <cJSON.h>
#include "./def.h"
#include "./tms_md5.h"
#ifdef __MACHINE_Q181Q190Y__
#include "./Q181YiKeVTMS.h"
#elif (defined(__MACHINE_Q181Q190Z__))
#include "./Q181ZHONGYUNVTMS.h"
#endif

#define DISP_STH  // display something //for terminals without display, close it
//#define JUMPER_EXIST

#define __APPSUFIX__		".img"   //L: ".img"   E: ".bin" //APP suffix

#define TMS_FILE_DIR  		"/ext/tms/"
#define _DOWN_STATUS_ 		"/ext/tms/_tms_dinfo_"
#define PARAM_FILE_DIR  	"/ext/"

#define _TMS_HOST_IP_		"47.91.110.91"
#define _TMS_HOST_DOMAIN_	"vtms.vanstone.com.cn"
#define _TMS_HOST_PORT_		"443"

#define	PROTOCOL_HTTP		0
#define	PROTOCOL_HTTPS		1

#define SENDPACKLEN 		1500   	//location upload transaction message length maybe 1195 or more, 1024 not enough.
#define RECVPACKLEN 		1024*7
#define EXFCONTENT_LEN 		6000

#define TYPE_APP			"APP" //application
#define TYPE_LIB			"DD" //dynamic library --so
#define TYPE_FONT			"WS" //font
#define TYPE_PARAM  		"PF" //common file/parameters file
#define TYPE_FIRMWARE  		"UF" //  UF-firmware
#define TYPE_WIFIBSP		"WB" //WIFI BSP/firmware

#define _TMS_MAX_APPLIB		25

#define _MBTOBYTES_			1048576  //1024*1024

/*****************ERROR CODE****************************/
#define  _TMS_E_TRANS_FAIL			2		//not 200 in response package  or Response code not "00" , or not "200"/"206" while download files
#define  _TMS_TERM_AUTH_UNDONE		3		//terminal authentication not done
#define  _TMS_E_ERR_CONNECT			5		//connect server failed //unuseful
#define  _TMS_E_SEND_PACKET			6		//send package failed
#define  _TMS_E_RECV_PACKET			7		//receive response failed
#define  _TMS_E_RESOLVE_PACKET		8		//resolve package failed
#define  _TMS_E_PREADDFILE			9		//pre-add files failed
#define  _TMS_E_TOO_MANY_FILES		10		//support less than 20 files
#define  _TMS_E_PACKAGE_WRONG		11		//package received not for this terminal
#define  _TMS_E_FILE_WRITE			13		//write file failed
#define  _TMS_E_MD5_ERR				14		//MD5 error
#define  _TMS_E_DOWNUNCOMPLETE		15		//files download uncomplete
#define  _TMS_E_FILE_SEEK			16		//file seek error
#define	 _TMS_E_FILESIZE			25		//file size error
#define  _TMS_E_NOFILES_D			28    //no files necessary to download for this terminal , or host app version is not higher than app in terminal
#define  _TMS_E_MALLOC_NOTENOUGH	29

#define STATUS_DLUNCOMPLETE		0		//dowload uncomplete
#define STATUS_DLCOMPLETE		1		//download complete
#define STATUS_UPDATE			2		//already update

#define _TMS_FILE_LINUX_		1
#define _TMS_FILE_VOS_			2
#define _TMS_FILE_OTHRE_		0xff

// Erorr codes
#define	CONNECT_ERROR		-4
#define	SEND_ERROR			-5
#define	RECEIVE_ERROR		-6

// The following macros can be configured to adapt field requirements
#define	MAX_DOMAIN_LENGTH	128
#define	MAX_PATH_LENGTH		128
#define	CONNECT_TIMEOUT		10
#define	RECEIVE_TIMEOUT		30
#define	RECEIVE_BUF_SIZE	2048

enum TRADE_TYPE
{
	TYPE_DEVICEAUTH = 1,
	TYPE_TERMINFO_UPLOAD,
	TYPE_GETTERM_TASK,
	TYPE_URLGETFILE, 
	TYPE_NOTIFY,
	TYPE_UPDATE,
	TYPE_LOCATION_UPLOAD
}; 

enum TMSAUTH_STATUS
{
	TMSAUTH_UNDONE = 0,
	TMSAUTH_DONE
};

#ifdef __MACHINE_Q181Q190__

typedef struct CELLINFO_STR{
	int MCC;
	int MNC;
	int LAC;
	int CellID;
	int RxDbm;
}CellInfo_Str;

typedef struct CELLINFOALL_STR{
	char cellinfonum;
	CellInfo_Str cellinfo_str[16];
}CellInfoAll_Str;

int wirelessGetCellInfo_lib( CellInfoAll_Str *cellinfoall_info, unsigned int timeout);

#endif

typedef struct _BASESTATION_{
	unsigned int lac; // 位置区号码）
	unsigned int ci; // （小区识别码）
	unsigned int bsic;// （基站识别码）
	unsigned int mcc; // （移动国家码）
	unsigned int mnc; // （移动网号）
	int rxlev; // 信号等级
	unsigned int bcch; // （传输通用信息，用于移动台测量信号强度和识别小区 标志等）
	unsigned char reserved[16]; // 预留
}BASESTATION;

int CommBaseStationInfo_Api(BASESTATION* BSInfo, int Max, int TimeOutMs);

struct __FileStruct__
{
	char	name[32];
	char	type[5];
	char	version[26];
	char	filePath[128];
	u8		md5[16];
	int 	fsize;
	int 	startPosi;
	int 	status;  //0-not download completely      1-download completely    2-already update
	int 	taskId;
	int 	itemId;
};

struct __TmsTrade__
{
	int		trade_type;  //trade type
	char	respCode[16];
	char	respMsg[32];
	int		fnum; //the number of file
	int 	curfindex;  //index of current file downloading
	struct __FileStruct__  file[_TMS_MAX_APPLIB]; 
	_tms_MD5_CTX context; 
	u8  	md5data[64];
	int 	mdlen;
	//char	deviceType[8];  //M
};

struct __TmsStruct__   //Open for Clients  //M-mandatory   O-Optional
{
	char	version[32];  //   M
	char	sn[18]; //SN   M
	char	merNo[32];  //merchent number  M
	char	termNo[32];  //terminal number M
	char	hostIP[16];  //O      one of hostIP and hostDomainName is mandatory
	char	hostPort[8];   //M
	char	hostDomainName[64];  // M    mandatory for http (check version and notify) message
	char    oldAppVer[26];  //old app version
	int		tradeTimeoutValue;
};

typedef struct {
	int protocol;
	char domain[MAX_DOMAIN_LENGTH];
	char path[MAX_PATH_LENGTH];
	char port[8];
} URL_ENTRY;

int Tms_StartJumpSec(void);
void Tms_StopJumpSec(void);						

/**************** Functions about flow Start **************************/
int Tms_BufMalloc();
int Tms_CreatePacket(u8 * packData, int * packLen, int ii, int tmscomm_type);
int Tms_RecvPacket(u8 *Packet, int *PacketLen, int WaitTime);
int Tms_UrlRecvPacket(u8 *Packet, int *PacketLen, int WaitTime);
int Tms_SendRecvData( unsigned char *SendBuf, int Senlen, unsigned char *RecvBuf, int *RecvLen,int psWaitTime);
int Tms_CreateTxdRxdResolve(int notifyIdx, int tranmode);
u8 Tms_HttpGetTranResult(u8 * data);
int Tms_ResolveHTTPPacket(u8 *recvPack);
int Tms_UnsolveGetTermTask(int reslt);
int Tms_UnPackPacket(char * data, int reslt);
void Tms_DisconnBufFree();
int Tms_DeviceAuth(int commode);
int Tms_TermInfoUpload(void);
int Tms_RequestTermTask(void);
int Tms_DownloadUrlFilesOneByOne();
int Tms_Notify(void);
int Tms_TermLocationUpload(int tranmode);
int Tms_CommProcess();
/**************** Functions about flow End **************************/

/**************** Other functions Start **************************/
void Tms_DelAllDownloadInfo(struct __TmsTrade__ *GFfile);
int Tms_CompareVersions(struct __TmsTrade__ *Curr);
int Tms_getDomainName(char *url, char *DomainName);
int Tms_Trim(u8 *str, u8 *out);
int Tms_GetFirmwareVerSion(char *VerSion);
int Tms_CheckNeedDownFile(struct __FileStruct__ *file);
int Tms_GetFileSize(struct __FileStruct__ *file);
int Tms_WriteFile(struct __FileStruct__ *file, unsigned char *Buf,unsigned int Length);
void Tms_DelFile(struct __FileStruct__ *file);
int tms_filezipcheck(char *filename);
void GetFirmwareModuleData(char *firm, char *module);
int Tms_getContentLen(u8 *packdata, int *Clen, int *Tlen);
int Tms_NeedReConnect(u8 *packdata);
int Tms_ReConnect();
int Tms_ReSend(u8 *packData, int PackLen);
void Tms_PlayDisplayTips(char *str);
void Tms_GetErrorMsg(int errCode, char *msg);
int CompareVosVersion(char *newVer);
//flag : 0-BSP  1-WIFI BSP
int CompareFWVersion(char *newVer, int flag);
/**************** Other functions End **************************/

/**************** TMS LOG OUTPUT Start **************************/
void OutputLogSwitch(int OnorOff, int logDest);
void Tms_LstDbgOutApp(const char *title, unsigned char *pData, int dLen, u8 type);
void Tms_DbgOutApp(const char *title, unsigned char *pData, int dLen);
/**************** TMS LOG OUTPUT End **************************/

/**************** TMS Entrence API Start **************************/
int TmsParamSet_Api(char *hostIp, char *hostPort, char *hostDomainName);
int TmsConnect_Api();
//terminal authentication and terminal information upload
int TmsTermAuth_Api(char commode);
//upgrade files/APP from VTMS
int TmsDownload_Api(char *appCurrVer);
int TmsUpdate_Api(char *appCurrVer);
int TmsStatusCheck_Api(char *appCurrVer);
int TmsTermLocUpload_Api(char commode);
/**************** TMS Entrence API End **************************/

/**************** App sample code Start **************************/
int TmsConnectServer();
int TmsTermAuthTest();
int TmsRemoteUpgrade();
void CheckTmsStatus();
#ifdef __MACHINE_Q181Q190__
void tms_TMSThread(void);
#endif
/**************** App sample code End **************************/

#if (defined(__MACHINE_Q181Q190LE__) || defined(__MACHINE_Q161__) || defined(__MACHINE_Q161PRO__))

/**************** Q161VTMS.h Start (Q161:libQ161_L610_VTMS__20240819.a)**************************/
typedef struct
{
	int iDHCPEnable;		/*DHCP使能, 0 -- 关闭 1 -- 开启*/
	char cIp[20]; 			/*静态IP---字符串参数*/
	char cNetMask[20];		/*子网掩码---字符串参数*/
	char cGateWay[20];		/*网关---字符串参数*/
}ST_WIFI_PARAM;//station模式下的热点WiFi参数

typedef struct
{
    char cSsid[64]; 		//字符串参数,AP的名字
    char cBssid[20];		//字符串参数,AP的MAC地址
    int iChannel;			//信道号
    int iRssi;				//信号强度
}ST_AP_INFO;//已连接AP信息

/*
* 函数功能：获取无线网络类型
* 入口参数：无
* 出口参数：无
* 返 回 值：失败：NULL 成功：网络类型（如："4G,2G" )
*/
char* VTMS_GetWirelessNetType(void);
/*
* 函数功能：获取设备模组名称
* 入口参数：Name（如："Q161-L610-LG"）
* 出口参数：无
* 返 回 值：>0-Name内容长度 其他-失败
*/
int VTMS_GetSysModuleName(char* Name);
/*
* 函数功能：获取无线运营商名称
* 入口参数：OperName
* 出口参数：无
* 返 回 值：>0:OperName内容长度 其他:失败
* 备    注：OperName空间建议128字节，避免溢出
*/
int VTMS_GetWirelessOperName(char* OperName);
/*
* 函数功能：获取无线网络服务类型
* 入口参数：无
* 出口参数：无
* 返 回 值：失败：NULL 成功：网络服务类型（如：GSM、CDMA、GPRS、CDMA2000、LTE）
*/
char* VTMS_GetTypeOfMobileNetworks(void);
/*
* 函数功能：获取WIFI网络信号强度
* 入口参数：无
* 出口参数：pstApInfo
* 返 回 值：<0 - 失败 , >=0 - 成功
*/
int VTMS_wifiCheck(ST_AP_INFO *pstApInfo);
/*
* 函数功能：获取无线网络信号强度
* 入口参数：无
* 出口参数：无
* 返 回 值：>=0 -信号强度 其他-失败
*/
int VTMS_wirelessGetSingnal(void);
/*
* 函数功能：获取设备MAC地址
* 入口参数：无
* 出口参数：MacAddr-MAC地址
* 返 回 值：0:成功 其他:失败
*/
int VTMS_wifiGetMac(char* MacAddr);
/*
* 函数功能：获取WIFI网络IP地址
* 入口参数：无
* 出口参数：pstWifiParam
* 返 回 值：0:成功 其他:失败
*/
int VTMS_wifiGetConnectParam(ST_WIFI_PARAM *pstWifiParam);
/*
* 函数功能：获取无线网络IP地址
* 入口参数：iIPlen - 预期最大长度 (不能为0，否则返回-6244)
* 出口参数：pcIP-IP地址 iIPlen-IP地址长度
* 返 回 值：0:成功 其他:失败
*/
int VTMS_wirelessGetIP(char *pcIP, int *iIPlen);

#endif

/*
* Function : search wifi routes
* Esc : exit or not if press cancle key      1-yes   0-no
* return : -2:wifi module error     -1:user cancel     0:no route    >0:the number of route
*/
int CommWifiScanAp_Api(struct WIFI_SCAN_AP *ApArray, int ApMaxNum, int Esc);


/*
*@Brief:        读取终端的版本信息
*@Param IN:     uiId[输入] 0  boot 版本 1  vos 版本 2  硬件配置版本 3  tms 版本 4  Lib 版本 5  HVN 和 FVN 版本
*@Param OUT:    pucInfo[输出] 用于存放产品序列号的缓冲区地址,需要预先分配 64 字节 的空间。
*@Return:       0:成功; <0:失败
*/
int sysReadVerInfo_Api(unsigned int uiId, unsigned char* pucVerInfo);

/**************** Q161VTMS.h End (Q161:libQ161_L610_VTMS__20240819.a)**************************/

#endif

void CompareTwoFiles(char *orgfile, char *downfile, int len);

