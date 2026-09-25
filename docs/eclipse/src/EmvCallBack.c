#include "def.h"
#include "func.h"
#include "EMV.h"
#include "EmvCommon.h"

#include <coredef.h>
#include <struct.h>
#include <poslib.h>
#include <string.h>

int CEmvWaitAppSel(int TryCnt, unsigned char *AppNameList, int AppNum)
{
	int iCurSel = 0, iPageNum = 0, iPageNo = 0, iCurItem = 0;
	u8  ucKeyIn = 0,cLine = 0;
	char scBuf[MAX_APPNAME_LEN + 1];
	
	memset(scBuf, 0, sizeof(scBuf));
 
	ScrCls_Api();
	ScrDisp_Api(LINE1, 0, "APP select", CDISP);

	LogPrintWithRet(0, "TryCnt = ", TryCnt);

	if (TryCnt != 0)
	{
		ScrDisp_Api(LINE5, 0, "APP refuse, PLS retry", CDISP);
		WaitAnyKey_Api(3);
	}

	iPageNum = (AppNum + MENU_LINES - 1) / MENU_LINES;
	iPageNo  = 0;
	iCurSel  = 0;

	while (1)
	{
		ScrClrLineRam_Api(LINE2, LINE5);

		iCurItem = iPageNo * MENU_LINES;
		for (cLine = LINE2; cLine < MENU_LINES + LINE2; cLine++)
		{
			memset(scBuf, 0, sizeof(scBuf));
			memcpy(scBuf, &AppNameList[iCurItem * MAX_APPNAME_LEN], MAX_APPNAME_LEN);

			if (iCurSel == iCurItem) {
				ScrDispRam_Api(cLine, 0, scBuf, LDISP|NOFDISP);
			} else {
				ScrDisp_Api(cLine, 0, scBuf, LDISP);
			}
			iCurItem++;

			if (iCurItem >= AppNum) { break; }
		}
		ScrBrush_Api();

		// process user input
		ucKeyIn = WaitAnyKey_Api(gCtrlParam.oprTimeoutValue);
		if (ucKeyIn == ENTER)
		{
			ScrCls_Api();
			ScrDisp_Api(LINE3, 0, "Processing...", CDISP);
			ScrDisp_Api(LINE4, 0, "Please Wait", CDISP);

			return iCurSel;
		}
		else if (ucKeyIn == ESC) {
			return ERR_USERCANCEL; // -5
		}
		else if (ucKeyIn == UP) {
			iCurSel = (iCurSel <= 0) ? AppNum - 1 : iCurSel - 1;
		}
		else if (ucKeyIn == DOWN) {
			iCurSel = (iCurSel >= AppNum - 1) ? 0 : iCurSel + 1;
		}
	}
}

void CEmvIoCtrl(unsigned char ioname, unsigned char *iovalue)
{
	switch(ioname)
	{
		case EMV_GET_POSENTRYMODE: 	    *iovalue = GetPosEntryMode(); 		  break;
		case EMV_GET_BATCHCAPTUREINFO:  *iovalue = GetPosBatchCaptureInfo();  break;
		case EMV_GET_ADVICESUPPORTINFO: *iovalue = GetPosAdviceSupportInfo(); break;
		default: break;
	}
}

int CEmvHandleBeforeGPO(void)
{
	return 0;
}

int CEmvInputAmt(unsigned char *AuthAmt, unsigned char *CashBackAmt)
{
	u8 Ret;

	if (PosCom.HaveInputAmt == 0) {
		ScrClrLine_Api(LINE2, LINE5);
		ScrDisp_Api(LINE2, 0, "PLS input AMT:", LDISP);

		Ret = GetAmount(PosCom.stTrans.TradeAmount);
		if (Ret != 0) { return 1; }

		PosCom.HaveInputAmt = 1;
	}

	if (CashBackAmt != NULL) {
		memset(CashBackAmt, 0, 6);
	} else {
		Ret = Common_SetTLV_Api(0x9F03, (unsigned char *)"\x00\x00\x00\x00\x00\x00", 6);
		if (Ret != 0) LogPrintWithRet(1, "!!! Common_SetTLV_Api(0x9F03) failed(%d) !!!", Ret);
	}

	memcpy(AuthAmt, PosCom.stTrans.TradeAmount, 6);

	ScrCls_Api();
	ScrDisp_Api(LINE3, 0, "Processing...", CDISP);
	ScrDisp_Api(LINE4, 0, "Please Wait", CDISP);

	return EMV_OK;
}

void CEmvVerifyPINOK(void)
{
	return;
} 

int CEmvGetSignature(void)
{ 
	return EMV_OK;
}

int CEmvGetOnlinePwd(int iTryFlag, int iRemainCnt, unsigned char *pszPlainPin)
{
	u8	buf[50];
	char szAmount[15];
	int	i, iret, tlvLen;

	memset(buf, 0, sizeof(buf));
	memset(szAmount, 0, sizeof(szAmount));

	tlvLen = 0;

	iret = Common_GetTLV_Api(0x9F02, buf, &tlvLen);
	if (iret == 0) {
		memcpy(PosCom.stTrans.TradeAmount, buf, 6);
	} else {
		LogPrintWithRet(1, "!!! Common_GetTLV_Api(0x9F02) failed(%d) !!!", iret);
	}

	tlvLen = 0;
	iret = Common_GetTLV_Api(0x5A, buf, &tlvLen);
	if (iret == 0)
	{
		BcdToAsc_Api(PosCom.stTrans.MainAcc, buf, tlvLen * 2);

		for (i = tlvLen * 2 - 1; i >= 0; --i)
		{
			if (PosCom.stTrans.MainAcc[i] == 'F' || PosCom.stTrans.MainAcc[i] == 'f') {
				PosCom.stTrans.MainAcc[i] = 0;
			} else {
				break;
			}
		}
	}
	else {
		LogPrintWithRet(1, "!!! Common_GetTLV_Api(0x5a) failed(%d) !!!", iret);
	}

	iret = Common_GetTLV_Api(0x57, buf, &tlvLen);
	if (iret == 0)
	{
		FormBcdToAsc((char *)PosCom.Track2, (u8*)buf, tlvLen * 2);

		PosCom.Track2Len = tlvLen * 2;
		if (PosCom.Track2[PosCom.Track2Len -1] == 0x3f) {
			PosCom.Track2Len--;
		}

		PosCom.Track2[PosCom.Track2Len] = 0;
		GetCardFromTrack((unsigned char *)PosCom.stTrans.MainAcc, PosCom.Track2, PosCom.Track3);
	}
	else {
		LogPrintWithRet(1, "!!! Common_GetTLV_Api(0x57) failed(%d) !!!", iret);
	}

	iret = EnterPIN(0);
	if (iret == 0)
	{
		if (PosCom.stTrans.EntryMode[1] == PIN_HAVE_INPUT) {
			PosCom.HaveInputPin = 1;
			return EMV_OK;
		}
		else {
			return ERR_NOPIN; // -12
		}
	}
	else if (iret == ESC) {
		return ERR_USERCANCEL;
	}
	else {
		return ERR_NOPINPAD; // -11
	}
}

// AuthAmt: 6byte/BCD, bcdTotalAmt: 6byte/BCD
void CEmvGetAllAmt(unsigned char *PANData, int PANDataLen, unsigned char *AuthAmt, unsigned char *bcdTotalAmt)
{
	int i = 0;
	//u32 BefAmount;

	LOG_STRC tLog;
	char PanDataTem[22];

	memset(&tLog,       0, LOG_SIZE);
	memset(PanDataTem,  0, sizeof(PanDataTem));
	memset(bcdTotalAmt, 0, 6);

	BcdToAsc_Api(PanDataTem, PANData, PANDataLen * 2);

	if ((PANData == NULL) || (PANDataLen <= 0)) {
		memcpy(bcdTotalAmt, AuthAmt, 6);
		return;
	}

	for (i = 0; i < gCtrlParam.iTransNum; i++)
	{
		if (ReadLog(&tLog, i) == 0)
		{
			if (!strcmp(tLog.MainAcc, PanDataTem))
			{
				if ((tLog.ucRecFalg != RECORDVOID) && (tLog.EntryMode[0] == PAN_ICCARD))
				{
					BcdAdd_Api(bcdTotalAmt, tLog.TradeAmount, 6);
				}
			}
		}
	}
}

void CEmvAdviceProc(void)
{
	// todo
}

int CEmvReferProc(void)
{
	return REFER_DENIAL; // 0x02
}

int CEmvGetHolderPwd(int iTryFlag, int iRemainCnt, unsigned char *pszPlainPin)
{
	u8	buf[50];
	char szAmount[15];
	int	i, iret, tlvLen;

	memset(buf, 0, sizeof(buf));
	memset(szAmount, 0, sizeof(szAmount));

	tlvLen = 0;
	iret = Common_GetTLV_Api(0x9F02, buf, &tlvLen);

	if (iret == 0) {
		memcpy(PosCom.stTrans.TradeAmount, buf, 6);
	} else {
		LogPrintWithRet(1, "!!! Common_GetTLV_Api(0x9F02) failed(%d) !!!", iret);
	}

	tlvLen = 0;
	iret = Common_GetTLV_Api(0x5A, buf, &tlvLen);

	if (iret == 0)
	{
		BcdToAsc_Api(PosCom.stTrans.MainAcc, buf, tlvLen * 2);

		for (i = tlvLen * 2 - 1; i >= 0; --i)
		{
			if (PosCom.stTrans.MainAcc[i] == 'F' || PosCom.stTrans.MainAcc[i] == 'f') {
				PosCom.stTrans.MainAcc[i] = 0;
			} else {
				break;
			}
		}
	} else {
		LogPrintWithRet(1, "!!! Common_GetTLV_Api(0x5a) failed(%d) !!!", iret);
	}

	iret = Common_GetTLV_Api(0x57, buf, &tlvLen);
	if (iret == 0)
	{
		FormBcdToAsc((char *)PosCom.Track2, (u8*)buf, tlvLen * 2);

		PosCom.Track2Len = tlvLen * 2;
		if (PosCom.Track2[PosCom.Track2Len -1] == 0x3f) {
			PosCom.Track2Len--;
		}

		PosCom.Track2[PosCom.Track2Len] = 0;
		GetCardFromTrack((unsigned char *)PosCom.stTrans.MainAcc, PosCom.Track2, PosCom.Track3);
	}
	else {
		LogPrintWithRet(1, "!!! Common_GetTLV_Api(0x57) failed(%d) !!!", iret);
	}

	if (pszPlainPin == NULL)
	{
		iret = EnterPIN(0);
		if (iret == 0)
		{
			if (PosCom.stTrans.EntryMode[1] == PIN_HAVE_INPUT) {
				PosCom.HaveInputPin = 1;
				return EMV_OK;
			} else {
				return ERR_NOPIN;
			}
		}
		else if (iret == ESC) {
			return ERR_USERCANCEL;
		}
		else {
			return ERR_NOPINPAD;
		}
	}

	ScrClrLine_Api(0, 7);
	FormatAmtToDisp_Api(szAmount, PosCom.stTrans.TradeAmount, 0);

	if (iTryFlag == 0)
	{
		if (PosCom.stTrans.Trans_id != POS_QUE) {
			strcat(szAmount, "YUAN");
			ScrDisp_Api(LINE1, 0, szAmount, CDISP);
		}
	}
	else {
		ScrDisp_Api(LINE1, 0, "PWD wrong,PLS retry", CDISP);
	}

	if (iRemainCnt == 1) {
		ScrDisp_Api(LINE4, 0, "residue times:1", CDISP);
		ScrDisp_Api(LINE5, 0, "LAST ENTER PIN",  CDISP);
		WaitAnyKey_Api(3);
	}

	ScrClrLine_Api(2,7); 
	ScrDisp_Api(LINE2, 0, "PLS input offline PWD", CDISP);
	ScrDisp_Api(LINE3, 0, "no PWD PLS continue",   CDISP);

	G_InputFlag = 1;
	while (1)
	{
		memset(buf, 0, sizeof(buf));
		if (GetScanf(MMI_NUMBER|MMI_PWD, 0, 12, (char*)buf, 30, LINE4, LINE4, 16) == ENTER)
		{
			if (buf[0]==0 ) {
				G_InputFlag = 0;
				return ERR_NOPIN;
			}
			else if (buf[0] < 4 ) {
				continue;					
			}

			sprintf((char *)pszPlainPin, "%.12s", buf + 1);

			Delay_Api(200);
			PosCom.HaveInputPin = 1;
			G_InputFlag = 0;
			return EMV_OK;
		}
		else {
			G_InputFlag = 0;
			return ERR_USERCANCEL;
		}
	}
}
