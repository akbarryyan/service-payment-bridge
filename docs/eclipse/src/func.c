#include "def.h"
#include "func.h"
#include "EmvCommon.h"

#include <coredef.h>
#include <struct.h>
#include <poslib.h>
#include <string.h>
#include <Q161Func_From_Devlib.h>

#define		MAX_LCDWIDTH				21

int _IS_PORT_OPEN_ = 0;

struct _CtrlParam gCtrlParam;

int GetScanf(unsigned int mode, int Min, int Max, char *outBuf, unsigned int TimeOut, unsigned char StartRow, unsigned char EndRow, int howstr)
{
	return GetScanfEx_Api(mode, Min, Max, outBuf, TimeOut, StartRow, EndRow, howstr, MMI_NUMBER);
}

int GetScanfAndCheck(unsigned int mode, int Min, int Max, char *outBuf, unsigned int TimeOut, unsigned char StartRow, unsigned char EndRow, int howstr, int dmode)
{
	int ret ;
	G_InputFlag = 1;
	ret = GetScanfEx_Api(mode, Min, Max, outBuf, TimeOut, StartRow, EndRow, howstr, dmode);
	G_InputFlag = 0;
	return ret;
}

int GetAmount(u8 *pAmt)
{
	int ret;
	char buf[32], temp[32];

	memset(buf,  0, sizeof(buf));
	memset(temp, 0, sizeof(temp));

	G_InputFlag = 1;
	while (1)
	{
		memset(buf, 0, sizeof(buf));
		if(isSmallMainScreen())
			ret = GetScanf(MMI_POINT, 1, 12, buf, 60, LINE2, LINE2, MAX_LCDWIDTH);
		else
			ret = GetScanf(MMI_POINT, 1, 12, buf, 60, LINE4, LINE4, MAX_LCDWIDTH);

		if (ret == ENTER)
		{
			memset(temp, 0x30, 12 - buf[0]);
			strcpy(&temp[12 - buf[0]], &buf[1]);
			AscToBcd_Api(pAmt, temp, 12);
			G_InputFlag = 0;
			return 0;
		}
		else {
			G_InputFlag = 0;
			return -1;
		}
	}
}

void GetPanNumber()
{
	int tlvLen = 0, i,ret;
	u8 buf[64];
	u8 TEM[64];

	memset(buf, 0, sizeof(buf));
	memset(TEM, 0, sizeof(TEM));

	ret = Common_GetTLV_Api(0x5A, buf, &tlvLen);
	if (ret == 0)
	{
		BcdToAsc_Api(PosCom.stTrans.MainAcc, buf, tlvLen * 2);

		for (i = tlvLen * 2 - 1; i >= 0; --i)
		{
			if (PosCom.stTrans.MainAcc[i] == 'F' || PosCom.stTrans.MainAcc[i] == 'f')
				PosCom.stTrans.MainAcc[i] = 0;
			else
				break;
		}
	}
	else {
		LogPrintWithRet(1, "!!! Common_GetTLV_Api(0x5A) failed(%d) !!!", ret);

		ret = Common_GetTLV_Api(0x57, buf, &tlvLen);
		if (ret == 0)
		{
			BcdToAsc_Api(TEM, buf, tlvLen * 2);

			for (i = tlvLen * 2 - 1; i >= 0; --i)
			{
				if (TEM[i] == 'D') {
					TEM[i] = 0;
					memcpy(PosCom.stTrans.MainAcc, TEM, i);
					break;
				}
			}
		}
		else {
			LogPrintWithRet(1, "!!! Common_GetTLV_Api(0x57) failed(%d) !!!", ret);
		}
	}
}

int EnterPIN(u8 flag)
{
	int ret;
	u8 DesFlag;
	char dispbuf[32];

	memset(dispbuf, 0, sizeof(dispbuf));

	if(isSmallMainScreen())
	{
		ScrClrLine_Api(LINE2, LINE2);
		if (flag != 0)
			ScrDisp_Api(LINE2, 0, "Wrong, again:", LDISP);
		else
			ScrDisp_Api(LINE2, 0, "Input PIN:", LDISP);
	}
	else
	{
		ScrClrLine_Api(LINE2, LINE5);
		if (flag != 0)
			ScrDisp_Api(LINE2, 0, "Password is wrong, please input again:", LDISP);
		else
			ScrDisp_Api(LINE2, 0, "Please input PIN:", LDISP);
	}

	if (gCtrlParam.DesType == 1)
		DesFlag = 0x01;
	else
		DesFlag = 0x03;

	ret = PEDGetPwd_Api(gCtrlParam.PinKeyIndes, 4, 8, PosCom.stTrans.MainAcc, PosCom.sPIN, DesFlag);
	if (ret != 0) {
		LogPrintWithRet(1, "!!! PEDGetPwd_Api() failed(%d) !!!", ret);
	}

	if (memcmp(PosCom.sPIN, "\0\0\0\0\0\0\0\0", 8) == 0)
		PosCom.stTrans.EntryMode[1] = PIN_NOT_INPUT;
	else
		PosCom.stTrans.EntryMode[1] = PIN_HAVE_INPUT;

	return 0;
}

int GetCardNoFromTrack2Data(char *cardNo, u8 *track2Data)
{
	char tmp[MCARDNO_MAX_LEN + 2], *p = NULL;
	u32 len;

	memset(tmp, 0, sizeof(tmp));

	len = MCARDNO_MAX_LEN + 1 < track2Data[0]  *2 ? MCARDNO_MAX_LEN + 1 : track2Data[0] * 2;

	FormBcdToAsc(tmp, track2Data+1, len);

	tmp[len] = '\0';
	p = strchr(tmp, '=');

	if (p != NULL)
	{
		*p = 0;
		strcpy(cardNo, tmp);
		return TRUE;
	}

	return FALSE;
}

int DispCardNo()
{
	int iRet;
	u8 pTrackBuf[256] = {0};

	LogPrintInfo("========== GET EMV TRACK DATA START ==========");
	iRet = GetEmvTrackData(pTrackBuf);
	LogPrintInfo("=========== GET EMV TRACK DATA END ===========");

	if (iRet == 0)
	{
		LogPrintInfo("========== GET CARD NO FROM TRACK 2 DATA START ==========");
		iRet = GetCardNoFromTrack2Data(PosCom.stTrans.MainAcc, pTrackBuf);
		LogPrintInfo("=========== GET CARD NO FROM TRACK 2 DATA END ===========");

		LogPrintWithRet(0, "GetCardNoFromTrack2Data(): ", iRet);
		if(isSmallMainScreen())
		{
			ScrClrLine_Api(LINE1, LINE2);
			ScrDispRam_Api(LINE1, 0, "PLS confirm:", 		 LDISP);
			ScrDispRam_Api(LINE2, 0, PosCom.stTrans.MainAcc, RDISP);
			ScrBrush_Api();
		}
		else
		{
			ScrClrLine_Api(LINE2, LINE10);
			ScrDispRam_Api(LINE3, 0, "PLS confirm:", 		 LDISP);
			ScrDispRam_Api(LINE4, 0, PosCom.stTrans.MainAcc, RDISP);
			ScrDispRam_Api(LINE6, 0, "ENTER to continue",    RDISP);
			ScrBrush_Api();
		}
	}
	else {
		LogPrintWithRet(1, "!!! GetEmvmTrackData() failed(%d) !!!", iRet);
	}

	KBFlush_Api();

	iRet = WaitEnterAndEscKey_Api(30);
	if (iRet != ENTER) return ERR_NOTACCEPT; // -8

	return 0;
}

void RemoveTailChars(char* pString, char cRemove)
{
	int nLen = 0;

	nLen = strlen(pString);
	while (nLen)
	{
		nLen--;
		if (pString[nLen] == cRemove) pString[nLen] = 0;
		else break;
	}
}

int MatchTrack2AndPan(u8 *pTrack2, u8 *pPan)
{
	int  i = 0;
	char szTemp[19 + 1], sTrack[256], sPan[256];

	memset(szTemp, 0, sizeof(szTemp));
	memset(sTrack, 0, sizeof(sTrack));
	memset(sPan,   0, sizeof(sPan));

	//track2
	BcdToAsc_Api(sTrack, &pTrack2[1], (u16)(pTrack2[0] * 2));

	RemoveTailChars(sTrack, 'F'); // erase padded 'F' chars

	for (i = 0; sTrack[i] != '\0'; i++) // convert 'D' to '='
	{
		if (sTrack[i] == 'D')
		{
			sTrack[i] = '=';
			break;
		}
	}

	for (i = 0; i < 19 && sTrack[i] != '\0'; i++)
	{
		if (sTrack[i] == '=') break;
		szTemp[i] = sTrack[i];
	}
	szTemp[i] = 0;

	//pan
	BcdToAsc_Api(sPan, &pPan[1], (int)(pPan[0]*2));

	RemoveTailChars(sPan, 'F'); // erase padded 'F' chars

	if (strcmp(szTemp, sPan) == 0)
		return 0;
	else
		return 1;
}

// ==================== Log Port Print ====================
void EnableLogPortPrint()
{
	int ret = portOpen_Api(USB_PORT_NUM, NULL);

	if (ret != 0) {
		MAINLOG_L1("!!! Port Open Failed(%d) !!!", ret);
	} else {
		ret = portFlushBuf_Api(USB_PORT_NUM);

		if (ret != 0) {
			MAINLOG_L1("!!! Port Flush Failed(%d) !!!", ret);
		} else {
			_IS_PORT_OPEN_ = 1;
		}
	}
}

void LogPrintNoRet(char *data)
{
	portFlushBuf_Api(USB_PORT_NUM);

	int len = strlen(data) + 2;

	char buf[len];
	memset(buf, 0, sizeof(buf));

	sprintf(buf, "%s", data);

	MAINLOG_L1(buf);
	portFlushBuf_Api(USB_PORT_NUM);

	memset(buf, 0, sizeof(buf));
	sprintf(buf, "%s\n", data);

	if (_IS_PORT_OPEN_) {
		portSends_Api(USB_PORT_NUM, buf, strlen(buf));
	}

	portFlushBuf_Api(USB_PORT_NUM);
}

void LogPrintWithRet(int log_style, char *data, int ret)
{
	portFlushBuf_Api(USB_PORT_NUM);

	int len = strlen(data) + 16;

	char buf[len];
	memset(buf, 0, sizeof(buf));

	if (log_style == 0) { // Normal, ret at END
		sprintf(buf, "%s%d", data, ret);
		MAINLOG_L1(buf);

		portFlushBuf_Api(USB_PORT_NUM);
		memset(buf, 0, sizeof(buf));

		sprintf(buf, "%s%d\n", data, ret);

	} else { // UnNormal, ret at OTHER POS.
		sprintf(buf, data, ret);
		MAINLOG_L1(buf);

		portFlushBuf_Api(USB_PORT_NUM);
		memset(buf, 0, sizeof(buf));

		sprintf(buf, data, ret);
		strcat(buf, "\n");
	}

	if (_IS_PORT_OPEN_) {
		portSends_Api(USB_PORT_NUM, buf, strlen(buf));
	}

	portFlushBuf_Api(USB_PORT_NUM);
}

void LogPrintInfo(char *data)
{
	portFlushBuf_Api(USB_PORT_NUM);

	int len = strlen(data) + 2;

	char buf[len];
	memset(buf, 0, sizeof(buf));

	sprintf(buf, "%s", data);
	MAINLOG_L1(buf);

	portFlushBuf_Api(USB_PORT_NUM);
	memset(buf, 0, sizeof(buf));

	sprintf(buf, "%s\n", data);

	if (_IS_PORT_OPEN_) {
		portSends_Api(USB_PORT_NUM, buf, strlen(buf));
	}
	portFlushBuf_Api(USB_PORT_NUM);
}
// ==================== Log Port Print ====================

void MyPowerCtrlMenu(void)
{
	int nSelcItem = 1, num;

	char *pszTitle = "POWER CTRL";
	const char *pszItems[] =
	{
		"1.PowerStand",
		"2.Power Off",
		"3.Reboot"
	};

	num = sizeof(pszItems) / sizeof(char *);
	nSelcItem = ShowMenuItem(pszTitle, pszItems, num, DIGITAL1, DIGITAL0 + num, 0, 60);

	switch (nSelcItem)
	{
		case DIGITAL1:
			SysPowerStand_Api();
			break;
		case DIGITAL2:
			SysPowerOff_Api();
			break;
		case DIGITAL3:
			SysPowerReBoot_Api();
			break;
		case ESC:
			Beep_Api(1);
			return;
		default: break;
	}
	ScrCls_Api();
}

void SetMyPowerMenu()
{
	u8 params[8];
	memset(params , 0 , sizeof(params));

	params[0] = 2;
	params[1] = 5;
	params[2] = 2;
	params[3] = (((u32)&MyPowerCtrlMenu) & 0xff000000)>>24;
	params[4] = (((u32)&MyPowerCtrlMenu) & 0x00ff0000)>>16;
	params[5] = (((u32)&MyPowerCtrlMenu) & 0x0000ff00)>> 8;
	params[6] = (((u32)&MyPowerCtrlMenu) & 0x000000ff)>> 0;
	SysConfig_Api(params , sizeof(params));
}

int isSmallMainScreen()
{
	if(G_scrtype == SCR_BLWH_128X96 || G_scrtype == SCR_BLWH_128X32)
		return 1;
	else
		return 0;
}

int ScanCodeTest()
{
	int  ret, RLen, TimerId;
	unsigned char RevBuf[256];

	ScrCls_Api();
	ScrDisp_Api(LINE1, 0, "Scanner test", CDISP);
	ret = ScanOpen_Api();
	MAINLOG_L1("ScanOpen_Api:%d", ret);
	if(ret != 0)
	{
		ScrCls_Api();
		ErrorPrompt("Init failed, No camera ?", 3);
		return -1;
	}

	ScrDisp_Api(LINE3, 0, "Scanning...", CDISP);
	TimerId = TimerSet_Api();
	while(1)
	{
		if(TimerCheck_Api(TimerId, 60 * 1000))
		{
			ScrCls_Api();
			ErrorPrompt("Receive timeout", 3);
			ScanClose_Api();
			return -1;
		}

		if(GetKey_Api() == ESC)
		{
			ScanClose_Api();
			return -1;
		}

		memset(RevBuf, 0, sizeof(RevBuf));
		ret = ScanGetData_Api(RevBuf, 30*1000);
		if(ret != 0)
		{
			ErrorPrompt("Read failed", 3);
			ScanClose_Api();
			return -1;
		}
		RLen = strlen(RevBuf);
		if(RLen >= 1)
		{
			break;
		}
	}

	ScanClose_Api();
	ScrCls_Api();
	ScrDisp_Api(LINE1, 0, "Scanner test", CDISP);
	ScrDisp_Api(LINE2, 0, "Scan ok!", FDISP|CDISP);
	ScrDisp_Api(LINE3, 0, RevBuf, FDISP|LDISP);
	WaitAnyKey_Api(60);
	return 0;
}

#define	__COM_PORT__	12
void PortOpenTest()
{
	int ret;

	ret = portOpen_lib(__COM_PORT__, NULL);
	MAINLOG_L1("portOpen_lib: %d", ret);
	if(ret != 0)
		return;

	ret = portFlushBuf_lib(__COM_PORT__);
	MAINLOG_L1("portFlushBuf_lib: %d", ret);
}

//OutputLog("ComLog.dat");
void OutputLog(char *FileName)
{
	char TempBuf[1024*2];
	int Len=0, ret=0, loc=0;

	//MAINLOG_L1("GetFileSize_Api: %d", GetFileSize_Api(FileName));
	memset(TempBuf, 0, sizeof(TempBuf));
	while(1)
	{
		Len = 1024;
		memset(TempBuf, 0, sizeof(TempBuf));
		ret = ReadFile_Api(FileName, TempBuf, loc, &Len);
		if (ret == 0 || ret == 2)
		{
			portSends_lib(__COM_PORT__, TempBuf, Len);
			if(ret == 2)
				break;
			loc += Len;
		}
		else
			break;
	}
}


