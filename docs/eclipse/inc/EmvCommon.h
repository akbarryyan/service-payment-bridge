#ifndef _EMV_COMMON_H_
#define _EMV_COMMON_H_

#define MAX_APPNAME_LEN	33

#define E_NEED_FALLBACK	91

#define ICC_EMV 0 // emv

#define TYPE_CASH			0x01		 
#define TYPE_GOODS			0x02		 
#define TYPE_SERVICE		0x04		 
#define TYPE_CASHBACK		0x08		 
#define TYPE_INQUIRY		0x10		 
#define TYPE_PAYMENT		0x20		 
#define TYPE_ADMINISTRATIVE	0x40		 
#define TYPE_TRANSFER		0x80		 

#define MAX_APP_NUM			32			 
#define MAX_CAPK_NUM		64			 
#define MAX_CAPKREVOKE_NUM	96			 

#define PART_MATCH			0x00		 
#define FULL_MATCH			0x01		 

#define EMV_GET_POSENTRYMODE			0 
#define EMV_GET_BATCHCAPTUREINFO		1 
#define EMV_GET_ADVICESUPPORTINFO		2 

#define EMV_OK   0
#define ERR_EMVRSP   -2
#define ERR_APPBLOCK   -3
#define ERR_NOAPP   -4
#define ERR_USERCANCEL   -5
#define ERR_TIMEOUT   -6
#define ERR_EMVDATA   -7
#define ERR_NOTACCEPT   -8
#define ERR_EMVDENIAL   -9
#define ERR_KEYEXP   -10
#define ERR_NOPINPAD   -11
#define ERR_NOPIN   -12
#define ERR_CAPKCHECKSUM   -13
#define ERR_NOTFOUND   -14
#define ERR_NODATA   -15
#define ERR_OVERFLOW   -16
#define ERR_ICCCMD   -21
#define ERR_ICCBLOCK   -22
#define ERR_USECONTACT   -25
#define ERR_APPEXP   -26
#define ERR_BLACKLIST   -27
#define ERR_USEOTHERCARD   -28
#define ERR_UNSUPPORTED   -32
#define ERR_FILE   -101
#define ERR_PARAM   -102
#define ERR_PINBLOCK   -103
#define ERR_DATA_EXIST   -104
#define ERR_AGAIN   -105
#define ERR_ICCINSERTED   -111
#define ERR_SEEPHONE   -112
#define ERR_CLNOTALLOWED   -113
#define ERR_SELECTNEXT   -114
#define ERR_NOAMOUNT   -115
#define ERR_READ_LAST_REC   -201
#define ERR_DENIALFORECC   -202
#define ERR_LOADDLL   -203
#define ERR_APPEXPOL   -116

#define ERR_ICCRESET       (-20)

#define ERR_RESERVER_FOR_EMV	(-1024)    

#define REFER_APPROVE		0x01		 
#define REFER_DENIAL		0x02		 
#define ONLINE_APPROVE		0x00		 
#define ONLINE_FAILED		0x01		 
#define ONLINE_REFER		0x02		 
#define ONLINE_DENIAL		0x03		 
#define ONLINE_ABORT		0x04		 
#define ONLINE_REFERANDFAIL 0x05	

#define PATH_PBOC			0x00		 
#define PATH_QPBOC			0x01		 
#define PATH_MSD			0x02		 
#define PATH_ECash			0x03

#define TYPE_KER_ERR			0
#define TYPE_KER_PAYWAVE		3
#define TYPE_KER_PAYPASS		7
#define TYPE_KER_QUICS			8
#define TYPE_KER_RUPAY			9

typedef struct {
    unsigned char RID[5];
    unsigned char KeyID;
    unsigned char HashInd;
    unsigned char ArithInd;
    unsigned char ModulLen;
    unsigned char Modul[248];
    unsigned char ExponentLen;
    unsigned char Exponent[3];
    unsigned char ExpDate[3];
    unsigned char CheckSum[20];
}EMV_CAPK;

typedef struct {
	int retCode;
	unsigned char SW[2];

	int FCITemplateLen;
	unsigned char FCITemplate[256];
} COMMON_PPSE_STATUS;

typedef struct {
	unsigned char MerchName[128];				// Merchant Name
	unsigned char MerchCateCode[2];			// Merchant Category Code
	unsigned char MerchId[15];			// Merchant ID
	unsigned char TermId[8];			// Terminal ID
	unsigned char TerminalType;			// Terminal Type
	//unsigned char Capability[3];			// Terminal Capability
	//unsigned char ExCapability[5];			// Extended Terminal Capability

	unsigned char TransCurrExp;			// Transaction Currency Exponent
	unsigned char ReferCurrExp;			// Reference Currency Exponent
	unsigned char ReferCurrCode[2];			// Reference Currency Code
	unsigned char CountryCode[2];			// Terminal Country Code
	unsigned char TransCurrCode[2];			// Transaction Currency Code
	unsigned long ReferCurrCon;			// Reference Currency Conversion
	// (Refer/Trans) * 1000
	char AcquierId[12];				// Acquier ID, ASII, terminated by 0							

	unsigned char TransType;			// Transaction Type
	//unsigned char ForceOnline;			// Force online by merchant
	//unsigned char GetDataPIN;			// If reading TryCount before PIN
	//unsigned char SupportPSESel;			// If PSE method supported
	//unsigned char IfSkipCardHolderVerify;		// If skipping CVM
	//unsigned char bCheckBlacklist;			// If checking black list

	//unsigned char bSupportAccTypeSel;		// If Account Selection supported
	// FIXME: it's not supported yet
	unsigned char reserve[128];			//reserve

} COMMON_TERMINAL_PARAM;

void RemoveTailChars(char* pString, char cRemove);
void FormatAmt_Str(char *pDest, char *pSrc);
 
unsigned char GetPosEntryMode(void); 
unsigned char GetPosBatchCaptureInfo(void); 
unsigned char GetPosAdviceSupportInfo(void); 

//comlib init
int Common_Init_Api();
char* Common_GetVersion_Api();
int Common_SetIcCardType_Api(unsigned char ucType, unsigned char ucSlot);
int Common_SelectPPSE_Api(COMMON_PPSE_STATUS* pComPPSEStatus);

//TLV api
int Common_SetTLV_Api(unsigned int Tag, unsigned char *Data, int len);
int Common_GetTLV_Api(unsigned int Tag, unsigned char *DataOut, int *OutLen);
unsigned short Common_GetTagAttr_Api(unsigned int Tag);


//black list
void Common_ClearBlackList_Api(void);
int Common_AddBlackList_Api(const char *cardNo, unsigned char seq);
int Common_DelBlackList_Api(const char *cardNo, unsigned char seq);
int Common_GetBlackList_Api(int index, unsigned char *blackPan, unsigned char *seq);

//cert
int Common_AddCapk_Api(EMV_CAPK *capk);
int Common_GetCapk_Api(int Index, EMV_CAPK *capk);
int Common_SearchCapk_Api(EMV_CAPK *pCapk, const unsigned char *rid,
						  unsigned char keyID);
int Common_DelCapk_Api(unsigned char KeyID, unsigned char *RID);
int Common_CheckCapk_Api(unsigned char *KeyID, unsigned char *RID);
void Common_ClearCapk_Api(void);
void Common_ClearIPKRevoke_Api(void);
int Common_AddIPKRevoke_Api(unsigned char *rid, unsigned char capki,
							unsigned char *certserial);
int Common_GetIPKRevoke_Api(int slotNo, unsigned char *rid,
							unsigned char *capki, unsigned char *certserial);
int Common_DelIPKRevoke_Api(unsigned char *rid, unsigned char capki,
							unsigned char *certserial);

//param
void Common_GetParam_Api(COMMON_TERMINAL_PARAM *Param);
void Common_SetParam_Api(COMMON_TERMINAL_PARAM *Param);
void Common_SaveParam_Api(const COMMON_TERMINAL_PARAM *Param);

//Log dbg
void Common_DbgEN_Api(int nEnDbg);
int Common_DbgReadLog_Api(char* pcLog, int* pnLen);
void Common_DbgDelLog_Api();
int Common_DbgGetFileLen_Api();
int Common_DbgReadLogByPosLen_Api(int nPos, char* pcLog, int* pnLen);

void CTLPreProcess();
void initCtlsConfig(int transType) ;
int DispCardNo(void);
int App_CommonSelKernel();
int App_RupayTrans();
int App_QuicsTrans();
int App_PaypassTrans();
int App_PaywaveTrans() ;
int PaywaveTransComplete();

void PayPassAddAppExp(int transType);
void PayWaveAddAppExp();
void QuicsAddAppExp();
void AddCapkExample();

#define RemoveCard   -1001
#define Approved   -1002
#define OnlineProc   -1003
#define ComFail   -1004
#define UseOtherIntrf   -1005
#define WaveCardAgain   -1006
#define ProcessingMsg   -1007
#define InputPsdMsgErr   -1008
#define ContactDetected   -1009
#define MultiCard   -1010
#define PICCOpenErr   -1011
#define MsgUseICC   -1012
#define MsgUseMag   -1013
#define GetTrackError   -1014
#define MagDetected   -1015
#define MsgPICCStart   -1016
#define MsgMsdNoSupport   -1017
#define InputOnlinePin   -1018
#define PICCTimeOut   -1019


#define		TIMEOUT			-2

#endif









