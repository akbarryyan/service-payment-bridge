#ifndef __EMV_H__
#define	__EMV_H__

#define	TYPE_CASH			0x01
#define	TYPE_GOODS			0x02
#define	TYPE_SERVICE		0x04
#define	TYPE_CASHBACK		0x08
#define	TYPE_INQUIRY		0x10
#define	TYPE_PAYMENT		0x20
#define	TYPE_ADMINISTRATIVE	0x40
#define	TYPE_TRANSFER		0x80

// I/O controls
#define	EMV_GET_POSENTRYMODE	  0
#define	EMV_GET_BATCHCAPTUREINFO  1
#define	EMV_GET_ADVICESUPPORTINFO 2

typedef struct {
	unsigned char AID[16];
	unsigned char AidLen;

	unsigned char Version[2];

	unsigned char SelFlag;

	unsigned char TargetPer;
	unsigned char MaxTargetPer;

	unsigned char FloorLimitCheck;
	unsigned char RandTransSel;
	unsigned char VelocityCheck;

	unsigned long FloorLimit;
	unsigned long Threshold;

	unsigned char TACDenial[6];
	unsigned char TACOnline[6];
	unsigned char TACDefault[6];

	unsigned char dDOL[128];
	unsigned char tDOL[128];

	unsigned char RiskManData[9];		// LEN + DATA

	unsigned char reserved[128];
} EMV_APPLIST; 

typedef struct {
	unsigned char Capability[3];
	unsigned char ExCapability[5];

	unsigned char bCheckBlacklist;
	unsigned char ForceOnline;
	unsigned char GetDataPIN;
	unsigned char SupportPSESel;
	unsigned char bSupportAccTypeSel;

	unsigned char reserved[128];
} EMV_TERM_PARAM; 

char *EMV_GetVersion_Api(void);
unsigned char *EMV_GetKernelCheckSum_Api(void);
unsigned char *EMV_GetConfigCheckSum_Api(void);

void EMV_SetTradeAmt_Api(unsigned char *authAmt, unsigned char *CashbackAmt);

int EMV_Init_Api(void);
void EMV_Clear_Api(void);
int EMV_SelectApp_Api(int ReSelect, unsigned long TransNo);
int EMV_InitApp_Api(void);
int EMV_ReadAppData_Api(void);
int EMV_OfflineDataAuth_Api(void);
int EMV_ProcRestrictions_Api(void);
int EMV_VerifyCardholder_Api(void);
int EMV_RiskManagement_Api(void);
int EMV_TermActAnalyse_Api(int *NeedOnline);
int EMV_Complete_Api(unsigned char ucResult, unsigned char *RspCode,
					 unsigned char *AuthCode, int AuthCodeLen,
					 unsigned char *IAuthData, int IAuthDataLen,
					 unsigned char *script, int ScriptLen);
int EMV_GetScriptResult_Api(unsigned char *Result, int *RetLen);
int EMV_SelectAppForLog_Api(void);
int EMV_ReadLogRecord_Api(int RecordNo);
int EMV_GetLogItem_Api(unsigned short Tag, unsigned char *TagData, int *TagLen);
void EMV_GetParam_Api(EMV_TERM_PARAM *Param);
void EMV_SetParam_Api(EMV_TERM_PARAM *Param);
void EMV_SaveParam_Api(EMV_TERM_PARAM *Param);

int EMV_AddApp_Api(EMV_APPLIST *App);
int EMV_GetApp_Api(int Index, EMV_APPLIST *App);
int EMV_DelApp_Api(unsigned char *AID, int AidLen);
void EMV_ClearApp_Api(void);

// Callbacks
#define MENU_LINES					8
int CEmvWaitAppSel(int TryCnt, unsigned char *AppNameList, int AppNum);
void CEmvIoCtrl(unsigned char ioname,unsigned char *iovalue);
int CEmvHandleBeforeGPO(void);
int CEmvInputAmt(unsigned char *AuthAmt, unsigned char *CashBackAmt);
void CEmvVerifyPINOK(void);
int CEmvGetSignature(void);
int CEmvGetHolderPwd(int iTryFlag, int iRemainCnt, unsigned char *pszPlainPin);
void  CEmvGetAllAmt(unsigned char *PANData,int PANDataLen, unsigned char *AuthAmt, unsigned char *bcdTotalAmt);
void CEmvAdviceProc(void);
int CEmvReferProc(void);

#endif /* __EMV_H__ */
