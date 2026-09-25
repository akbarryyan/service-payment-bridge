#ifndef __DEF_H__
#define	__DEF_H__

#define __MACHINE_Q161PRO__		//for Q161Pro

// Merchant id milik pemilik soundbox ini, ditetapkan gateway pembayaran. Perangkat melanggan
// topic_<merchant id>, jadi nilainya harus sama persis dengan yang dikirim backend. Satu biner
// berlaku untuk satu merchant: ganti baris ini lalu build ulang sebelum mem-flash unit lain.
//
// Maksimal 25 karakter -- mqtt_topic menampung 31, dan "topic_" memakan 6.
#define MQTT_MERCHANT_ID	"MT58530503"

#define BOOT_TIP_FILE		"/ext/welcome.mp3"

#define CONNECTED_SERVER	"/ext/tip-server-menghubungkan.mp3"
#define SERVER_CONNECTED 	"/ext/tip-server-terhubung.mp3"

#define EXTERN extern

extern int fileFilter;
extern int needUpdate;
extern int Device_Type;
extern unsigned char updateAppName[32];

extern int G_InputFlag;

// ---- QRIS dinamis -------------------------------------------------------------------------
//
// Menu berjalan di utas sendiri (MenuThread), sedangkan MQTTClient hidup sebagai variabel lokal
// di dalam mQTTMainThread. Klien itu tidak aman dipakai dua utas, jadi menu tidak menerbitkan
// sendiri: ia menaruh nominal lalu menaikkan G_qrisReqPending, dan utas MQTT yang menerbitkannya
// di putaran MQTTYield berikutnya. Balasan mengalir kembali lewat onTopicMessageArrived.
//
// Payload QRIS sungguhan panjangnya 250-350 karakter, jauh di atas 256 byte yang dipakai
// QRCodeDisp bawaan demo -- karena itu buffernya 512.
#define QRIS_REQUEST_TOPIC	"qris/request"
#define QRIS_REPLY_PREFIX	"QR:"
#define QRIS_MAX_LEN		512

extern volatile int  G_qrisReqPending;		// 1 = menu minta QRIS diterbitkan
extern volatile long G_qrisReqAmount;		// nominal dalam sen
extern volatile int  G_qrisReplyReady;		// 1 = G_qrisReply sudah berisi balasan
extern char          G_qrisReply[QRIS_MAX_LEN];

void QRCodeDisp(void);
void QRCodeDispText(const char *text);
void QrisDinamis(void);

extern int G_scrtype;  //screen type

#define __LCDDISP__
#define __SECCODEDISP__
#define __SETDNS__
#define __CTLSTRADE__

#define __CHECKTYPESTATUS__ //broadcast will interrupt type/input(like GetScanfEx_Api), so it's needed.

#define	TIMEOUT	-2

enum tms_flag {
 TMS_FLAG_BOOT = 0,
 TMS_FLAG_VOS,
 TMS_FLAG_L1,
 TMS_FLAG_L2,
 TMS_FLAG_FONT,
 TMS_FLAG_MAPP,
 TMS_FLAG_ACQUIRER,
 TMS_FLAG_APP_PK,
 TMS_FLAG_APP,
 TMS_FLAG_VOS_NOFILE,
 TMS_FLAG_APP_NOFILE,
 TMS_FLAG_DIFFOS,
 TMS_FLAG_MAX,
};

enum TRANCOMM_MODE
{
	TRANCOMM_NONE = 0,
	TRANCOMM_SIM, 			//SIM //mobile network
	TRANCOMM_WIFI			//WIFI
};

enum SCRMAIN_TYPE
{
	SCR_BLWH_128X96 = 0,
	SCR_COLOR_320X240,
	SCR_COLOR_320X480,
	SCR_BLWH_128X32
};

/*typedef struct
{
	int iEcn;
	char cSsid[64];
	int iRssi;
	char cBssid[20];
	int iChannel;
	int iFreqOffset;
}ST_AP_LIST;*/

typedef struct _LCDINFO{
 int Wigtgh;            //actual width
 int Heigth;            //actual height
 int UseWigtgh;           //available width
 int UseHeigth;           //available width
 unsigned char ColorValue;        // 1:monochromatic 2:RGB332 3:RGB565 4:RGB888
}LCDINFO;


typedef enum{
	SINGLE_CHAR = 0,
	MULTI_CHAR = 1,
	ARABIC_CHAR = 2,
}CHAR_TYPE;

#pragma pack(1)
typedef struct _ST_FONT{
	CHAR_TYPE     CharSet;          /* Record the sub-font is signal-char/multi-char/arabic-char...*/
	int Width;
	int Height;
	int Bold;
	int Italic;
	int EncodeSet;                  /* the sub-font encode type.                                   */
}ST_FONT;
#pragma pack()

#define ENCODE_UNICODE     0x00
#define ENCODE_WEST        0x01u              /*Î÷ÎÄ×ÖÌå                                    */
#define ENCODE_TAI         0x02u              /* ÐèÒª/×îºÃ ÐÞ¸ÄÎªenumÀàÐÍ£¬¹æ·¶¼°ÀàÐÍ¼ì²â   */
#define ENCODE_MID_EUROPE  0x03u
#define ENCODE_VIETNAM     0x04u
#define ENCODE_GREEK       0x05u
#define ENCODE_BALTIC      0x06u
#define ENCODE_TURKEY      0x07u
#define ENCODE_HEBREW      0x08u
#define ENCODE_RUSSIAN     0x09u
#define ENCODE_GB2312      0x0Au
#define ENCODE_GBK         0x0Bu
#define ENCODE_GB18030     0x0Cu
#define ENCODE_BIG5        0x0Du
#define ENCODE_SHIFT_JIS   0x0Eu
#define ENCODE_KOREAN      0x0Fu
#define ENCODE_ARABIA      0x10u
#define ENCODE_DIY  	     0x11u

#define USB_PORT_NUM 10
extern int _IS_PORT_OPEN_;

extern int MENU_THREAD_INITIALIZED;

/* Missed system header definitions */
int tmsUpdateFile_lib(enum tms_flag flag, char *pcFileName, char *signFileName);
int LedTwinkle_Api(int ledNum, int type, int count);
void SetApiCoreLogLevel(int level);		// Not for sure
void ApiCoreLog(char *Tag, const char *func, unsigned int level, char *format, ...);
int NetLinkCheck_Api(int type);
int NetModuleOper_Api(int type, int onOrOff);
void SysPowerReBoot_Api(void);
int fileunZip_Api(const char *file_path,const char *zip_path);

int wirelessGetSingnal_lib(void);
/*rssi[out]:
		0  		-113 dBm or less
 		1  		-111 dBm
 		2...30 	-109..-53 dBm
 		31  	-51 dBm or greater
 		99 		Not known or not detectable
  ber[out]:  0-success    <0-failed
  return  :
 * */
int wirelessGetCSQ_lib(int *rssi, int *ber);

/* Get network registered status
 * status[out]:
		0/2/3/4  		unregistered
 		others  		registered
  return  : 0-success    <0-failed
 * */
int wirelessGetRegInfo_lib(unsigned char *status);
/*
	pvTaskCode : task function
	pcName : task name
	usStackDepth : task size, maximum is 250K
	pvParameters:  input parameters
	uxPriority : priority ;  OSI_PRIORITY_NORMAL is suggested, and highest priority for APP thread is OSI_PRIORITY_NORMAL ;

	return :0-success  <0-failed
*/
int fibo_thread_create(void *pvTaskCode, char *pcName, int usStackDepth, void *pvParameters, int uxPriority);

void PlayMP3File(char *audioFileName);

/*
	Set APN:
	Parameters:
		apn: APN
		username: username; set NULL if unnecessary
		password: password; set NULL if unnecessary
	Return : 0-success   <0-failed
	Noted :  parameters effect after terminal is restarted
			wirelessPdpWriteParam_lib(NULL, "", ""); -- will clear APN
*/
int wirelessPdpWriteParam_lib(char *apn, char *username, char *password);



#define MAINLOG_L1(format, ...) ApiCoreLog("MAINAPI", __FUNCTION__, 1, format, ##__VA_ARGS__)

#define	SYS_PARAM_NAME		"sys_param.dat"

#define TMS_DOWN_FILE		"/ext/downFile.zip"
#define BATER_WARM		 	15
#define BATER_POWR_OFF  	3
#define TMSPLAYONCETIME 	5							//When don't broadcast a trans, time it every five seconds
#define TMSMAXTIME (30+TMSPLAYONCETIME)     			//broadcast 30%10, 3 times
#define SAVE_UPDATE_FILE_NAME	"/ext/ifUpdate"		//record if the updated file, delete the file after update success

// SSL certificates
#define FILE_CERT_ROOT	  "/ext/app/data/ca.pem"
#define FILE_CERT_CHAIN   "/ext/app/data/cli.crt"
#define FILE_CERT_PRIVATE "/ext/app/data/pri.key"

//file
void folderFileDisplay(unsigned char *filePath);
int unzipDownFile(unsigned char *fileName);

// mqtt
void initMqttOs(void);
void mQTTMainThread(void);

// sound
void AppPlayTip(char *tip);
void AppPlayTipFile(char *path, char *fallbackTip);

void MenuThread(void);

// monitor
void MonitorThread(void);

// tms
void set_tms_download_flag(int type);
void TMSThread(void);

// network
void net_init(void);
void *net_connect(void* attch, const char *host,const char *port, int timerOutMs, int ssl, int *errCode);
int net_close(void *netContext);
int net_read(void *netContext, unsigned char* recvBuf, int needLen, int timeOutMs);
int net_write(void *netContext, unsigned char* sendBuf, int sendLen, int timeOutMs);

long long sysGetTicks_lib(void);

int QREncodeString(const char *str, int version, int level, const char *FilePath, int Zoom);

// param
typedef struct {
	char mqtt_server[128];
	char mqtt_port[8];
	int mqtt_ssl;		// 0-TCP, 1-SSL without cert, 2-SSL with cert
	int mqtt_qos;		// 0-QOS0, 1-QOS1, 2-QOS2
	int mqtt_keepalive;	// in seconds

	char mqtt_topic[32];
	char mqtt_client_id[32];
	char sn[32];
	int sound_level;
	char commode;
	char wifiSsid[64];
	char wifiPwd[16];
	char reserved[176];
} SYS_PARAM;

extern SYS_PARAM G_sys_param;

void initParam(void);
void saveParam(void);

void DispMainFace(void);
int WaitEvent(void);
void SelectMainMenuForSmallDisplay(void);
void SelectMainMenuForLargeDisplay(void);

void wifiTest();
void SetCommMode();
void SetVtmsLogOutputFlag();

void TransTapCard();
void UnzipMp3();

/*
*Function:		WifiSetSocketSNI_lib
*Description:	Set socket SNI
*Input:			sockid : soket ID; sni :server/host name of the socket
*Output:		NULL
*Hardware:
*Return:		0:success; <0:failure
*Others:
*/
int WifiSetSocketSNI_lib(int sockid, char *sni);

/*
*Function:		WifiSetSocketSNI_lib
*Description:	Get socket SNI
*Input:			sockid :socket ID;
*Output:		sni :server/host name of the socket
*Hardware:
*Return:		0:success; <0:failure
*Others:
*/
int WifiGetSocketSNI_lib(int sockid, char *sni);

int GetScanf(unsigned int mode, int Min, int Max, char *outBuf, unsigned int TimeOut, unsigned char StartRow, unsigned char EndRow, int howstr);
int GetScanfAndCheck(unsigned int mode, int Min, int Max, char *outBuf, unsigned int TimeOut, unsigned char StartRow, unsigned char EndRow, int howstr, int dmode);

/*
FUNCTION: Get screen type
RETURN VALUE:
			0 - black/white screen(128*96)
			1 - color screen(320*240)
			2 - color screen(320*480)
			3 - black/white screen(128*32)
 * */
int sysGetTermScreenType_lib();

void SelectSettingsMenu(void);
int isSmallMainScreen();

int ScanCodeTest();

#endif /* __DEF_H__ */
