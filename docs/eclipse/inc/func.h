#ifndef AFX_FUNC_H
#define AFX_FUNC_H

#define	MCARDNO_MAX_LEN	19
#define LAST_REC_LOG 0xffffffff

#define RECORDLOG "record"

#define	PIN_PED	0X00
#define	PIN_PP	0X01

typedef struct
{
	unsigned char ucRecFalg;
	int IccOnline;
	unsigned char IccFallBack;
	unsigned short nIccDataLen;
	unsigned char Trans_id;
	char MainAcc[22];
	unsigned char TradeAmount[6 + 1];
	unsigned char TradeDate[4];
	unsigned char TradeTime[3];
	unsigned char OperatorNo;
	unsigned int lTraceNo;
	unsigned int lNowBatchNum;
	char szRespCode[2 + 1];
	unsigned char ExpDate[4];
	unsigned char EntryMode[4];
	char SysReferNo[13];
	char AuthCode[7];
	char TerminalNo[9];
	char MerchantNo[16];
	char MerchTermNO[5];
	unsigned char SecondAmount[6];
	char SecondAcc[21];
	char HoldCardName[20 + 1];
	char AddInfo[122 + 1];
	unsigned int OldTraceNo;
	unsigned int OldBatchNum;
	char OldTransDate[9];
	char OldSysRefNo[13];
	char OldTermNo[9];
	// EMV
	unsigned char IccData[1 + 255];
	char szCardUnit[4];
	unsigned char bPanSeqNoOk;
	unsigned char ucPanSeqNo;
	unsigned char sAppCrypto[8];
	unsigned char sAuthRspCode[2];
	unsigned char sTVR[5];
	unsigned char sAIP[2];
	unsigned char szUnknowNum[4];
	char szAID[32 + 1];
	char szAppLable[16 + 1];
	unsigned char sTSI[2];
	unsigned char sATC[2];
	unsigned char szAppPreferName[16 + 1];
} LOG_STRC;
#define LOG_SIZE sizeof(LOG_STRC)

typedef struct
{
	unsigned char sPIN[9];
	unsigned char BalanceAmount[1 + 40];
	unsigned char Track1[88];
	unsigned char Track2[2 + 37];
	unsigned char Track3[2 + 107];
	unsigned char Track1Len;
	unsigned char Track2Len;
	unsigned char Track3Len;
	LOG_STRC stTrans;
	unsigned char HaveInputAmt;
	unsigned char HaveInputPin;
	unsigned short nRespIccLen;
	unsigned char RespIccData[512];
} POS_COM;
extern POS_COM PosCom;

struct _CtrlParam
{
	unsigned char pinpad_type;
	unsigned char AKeyIndes;
	unsigned char MainKeyIdx;
	unsigned char PinKeyIndes;
	unsigned char MacKeyIndes;
	int	lTraceNo;
	int	lNowBatchNum;
	unsigned short iTransNum;
	unsigned char beepForInput;
	unsigned char oprTimeoutValue;
	unsigned char tradeTimeoutValue;
	char TerminalNo[9];
	char MerchantNo[16];
	char MerchantName[41];
	unsigned char DesType;
	unsigned char PreDial;
	unsigned char ShieldPAN;
	unsigned char SupportICC;
	unsigned char SupportPICC;
	unsigned char SupportSignPad;
	unsigned char SignTimeoutS;
	unsigned short SingRecNum;
	unsigned char SignMaxNum;
	unsigned short SignBagMaxLen;
};
extern struct _CtrlParam gCtrlParam;

enum {
	MASK_INCARDNO_HANDIN   = 0x01,
	MASK_INCARDNO_MAGCARD  = 0x02,
	MASK_INCARDNO_ICC      = 0x04,
	MASK_INCARDNO_PICC     = 0x08,
	MASK_INCARDNO_KEYAGAIN = 0x10,
};

enum {
	CARD_EMVFULL         = 0x00,
	CARD_EMVSIMPLE       = 0x01,
	CARD_EMVFULLNOCASH   = 0x02,
	CARD_EMVFULLCASH	 = 0x04,
	CARD_EMVFULLCASHSALE = 0x08,
	CARD_QPASSONLINE     = 0x10
};

enum {
	PAN_KEYIN    = 0x01,
	PAN_MAGCARD  = 0x02,
	PAN_ICCARD   = 0x05,
	PAN_PAYWAVE  = 0x07,
	PAN_PAYPASS  = 0x91,
	PAN_MIR      = 0x07,
	PAN_JSPEEDY  = 0x08,
	//PAN_PICCFULL =	0x98,
};

enum {
	POS_SALE = 1,
	POS_SALE_VOID,
	POS_QUE
};

enum {
	PIN_HAVE_INPUT = 0x10,
	PIN_NOT_INPUT  = 0x20,
};

enum
{
	RECORDNORMAL = 0x00,
	RECORDVOID	 = 0x01,
	RECORDHAVEUP = 0x02
};

extern char g_EmvFullOrSim;

#define  E_TRANS_FAIL	   2
#define  E_NO_TRANS		   3
#define  E_MAKE_PACKET	   4
#define  E_ERR_CONNECT	   5
#define  E_SEND_PACKET	   6
#define  E_RECV_PACKET	   7
#define  E_RESOLVE_PACKET  8
#define  E_REVERSE_FAIL	   9
#define  E_NO_OLD_TRANS	   10
#define  E_TRANS_VOIDED	   11
#define  E_ERR_SWIPE	   12
#define  E_MEM_ERR		   13
#define  E_PINPAD_KEY	   14
#define  E_FILE_OPEN	   15
#define  E_FILE_SEEK	   16
#define  E_FILE_READ	   17
#define  E_FILE_WRITE	   18
#define  E_CHECK_MAC_VALUE 19
#define  E_TRANS_CANCEL	   20
#define	 E_MAC			   21
#define  E_SYS		       22
#define  E_FAILURE		   23
#define	 E_REVTIMEOUT	   24
#define	 E_PPNORESP		   25

#define  NO_DISP 26

#endif
