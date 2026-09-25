#include "def.h"
#include "func.h"
#include "EMV.h"
#include "EmvCommon.h"

#include ".\xgdlib.h"

#include <coredef.h>
#include <struct.h>
#include <poslib.h>
#include <Q161Func_From_Devlib.h>

int g_ucKerType = 0;

int PiccInit(void)
{
	if(PiccOpen_Api() != 0x00) return 1;
	return 0;
}

int PiccStop(void)
{
	if(PiccClose_Api() != 0x00) return 1;
	return 0;
}

int PiccCheck()
{
	u8 CardType[8], SerialNo[32];
	u8 ret;
	
	memset(CardType, 0, sizeof(CardType));
	memset(SerialNo, 0, sizeof(SerialNo));

	ret = PiccCheck_Api(0, CardType, SerialNo);

	if (ret != 0x00) { return -1; }
	else { return 0; }
}

int DetectCardEvent(u8 *CardData, u8 timeoutS)
{
	u8 key;
	unsigned int timerid;
		 
	timerid = TimerSet_Api();
	while (!TimerCheck_Api(timerid, timeoutS * 1000))
	{
		key = GetKey_Api();
		if (key == ESC) { return ESC; }
 
		if (!(int)CardData[0]) {
			if (PiccCheck() == 0x00) {
				return (int)CardData[0];
			} else {
				continue;
			}
		}
		else {
			int ret = IccDetect_Api(DEV_IC_NO);
			if (ret == 0x00) {
				return (int)CardData[0];
			}
			else {
				continue;
			}
		}
	}

    return TIMEOUT;
}

int GetCard()
{
	u8 CardData[256] = {0};
	int ret, event;

	CardData[0] = 0;

	if (PiccInit() != 0) { return -1; }

	AppPlayTip("Please tap card");

	initCtlsConfig(0x00);
	CTLPreProcess();

	while (1)
	{
		if(isSmallMainScreen())
		{
			ScrDisp_Api(LINE1, 0, "Sale", CDISP);
			ScrDisp_Api(LINE2, 0, "Tap card", CDISP);
		}
		else
		{
			ScrClrLine_Api(LINE2, LINE10);
			ScrDisp_Api(LINE3, 0, "Tap card", CDISP);
		}

		event = DetectCardEvent(CardData, 60);
		LogPrintWithRet(0, "DetectCardEvent(): ", event);

		switch (event)
		{
			case 0:
				ret = Common_SetIcCardType_Api(PEDPICCCARD, 0);
				LogPrintWithRet(0, "Common_SetIcCardType_Api(): ", ret);

				g_ucKerType = App_CommonSelKernel();
				LogPrintWithRet(0, "App_CommonSelKernel == ", g_ucKerType);

				if (g_ucKerType == TYPE_KER_PAYWAVE) { // 3
					Beep_Api(0);

					ret = App_PaywaveTrans();
					LogPrintWithRet(0, "App_PaywaveTrans(): ", ret);
				}
				else if (g_ucKerType == TYPE_KER_PAYPASS) { // 7
					Beep_Api(0);

					ret = App_PaypassTrans();
					LogPrintWithRet(0, "App_PaypassTrans(): ", ret);
				}
				else if (g_ucKerType == TYPE_KER_QUICS) { // 8
					Beep_Api(0);

					ret = App_QuicsTrans(); //proc_quics_trans_1
					LogPrintWithRet(0, "App_QuicsTrans(): ", ret);
				}
				else if (g_ucKerType == TYPE_KER_RUPAY) {
					Beep_Api(0);

					ret = App_RupayTrans();
					LogPrintWithRet(0, "App_RupayTrans(): ", ret);
				}
				else {
					AppPlayTip("no app match");
					ret = -1;
				}

				return ret;

			case ESC:
			case TIMEOUT:

			default:
				ret = -1;
				break;
		}
	}

	return ret;
}

int EmvGetCard(u8 mode, u8 type)
{
	u8 CardData[256];
	int ret, event;

	memset(CardData, 0, sizeof(CardData));
	CardData[0] = 1;

	AppPlayTip("Please insert card");

	if (mode & MASK_INCARDNO_ICC) {
		if (type & CARD_EMVSIMPLE) {
			type = CARD_EMVSIMPLE;
		}
	}

	LogPrintWithRet(0, "mode = ", (int)mode);
	LogPrintWithRet(0, "type = ", (int)type);

	while (1)
	{
		ScrClrLine_Api(LINE2, LINE10);
		if(isSmallMainScreen())
			ScrDisp_Api(LINE2, 0, "Insert card", CDISP);
		else
			ScrDisp_Api(LINE3, 0, "Insert card", CDISP);

		event = DetectCardEvent(CardData, 60);
		LogPrintWithRet(0, "DetectCardEvent(): ", event);

		switch (event)
		{
			case 1:
				Beep_Api(0);
				PosCom.stTrans.EntryMode[0] = PAN_ICCARD;

				memset(CardData, 0, sizeof(CardData));

				ret = EmvCardProc(PosCom.stTrans.Trans_id, type, CardData);
				LogPrintWithRet(0, "EmvCardProc(): ", ret);

				if (ret != 0)
				{
					Beep_Api(1);

					if ((ret == ERR_ICCRESET) || (ret == ERR_NOAPP) || (ret == E_NEED_FALLBACK)) // -20, -4, 91
					{
						if (ret == ERR_NOAPP) {
							AppPlayTip("No app match");
						}
						else {
							if (mode & MASK_INCARDNO_MAGCARD)
							{
								AppPlayTip("Insert card error");

								PosCom.stTrans.IccFallBack = 1;
								mode &= ~MASK_INCARDNO_ICC;
								mode &= ~MASK_INCARDNO_PICC;
								mode |= MASK_INCARDNO_MAGCARD;
							}
							else {
								AppPlayTip("Transaction failed");
								mode = 0;
							}
						}

						break;
					}
					else {
						return ret;
					}
				}

				// todo trans with server online

				return ret;

			case ESC:
			case TIMEOUT:

			default:
				ret = -1;
				break;
		}

		return ret;
	}
}

void TransTapCard()
{
	int ret = 0, amt, tipline;
	unsigned char tmp[32], buf[12];

	if(isSmallMainScreen())
		tipline = LINE2;
	else
		tipline = LINE3;

	memset(&PosCom, 0, sizeof(PosCom));

	ScrCls_Api();
	if(isSmallMainScreen())
		ScrDisp_Api(LINE1, 0, "Input amount:", LDISP);
	else
	{
		ScrDisp_Api(LINE1, 0, "Sale", CDISP);
		ScrDisp_Api(LINE3, 0, "Please input amount:", LDISP);
	}

	AppPlayTip("Please input amount");

	if (GetAmount(PosCom.stTrans.TradeAmount) != 0)
	{
		Beep_Api(1);
		return;
	}

	AppPlayTip("Amount Confirmed");

	ret = GetCard();
	LogPrintWithRet(0, "GetCard(): ", ret);

	if ((ret != 0) || (DispCardNo() != 0))
	{
		Beep_Api(1);

		ScrCls_Api();
		ScrDisp_Api(LINE1, 0, "Sale", CDISP);
		ScrDisp_Api(tipline, 0, "Transaction failed.", CDISP);
		AppPlayTip("Transaction failed.");

		return;
	}

	ScrCls_Api();
	ScrDisp_Api(LINE1, 0, "Sale",          CDISP);
	ScrDisp_Api(tipline, 0, "Connecting...", CDISP);

	Delay_Api(2000);

	ScrDisp_Api(tipline, 0, "Sending MSG...", CDISP);
	Delay_Api(2000);
	ScrDisp_Api(tipline, 0, "Receiving MSG...", CDISP);

	Delay_Api(2000);

	if (ret == 0)
	{
		amt = BcdToLong_Api(PosCom.stTrans.TradeAmount, 6);

		memset(buf, 0, sizeof(buf));
		memset(tmp, 0, sizeof(tmp));

		sprintf(buf, "%d.%02d", amt / 100, amt % 100);
		sprintf(tmp, "Paid: %s", buf);

		ScrCls_Api();
		ScrDisp_Api(LINE1, 0, "Sale", CDISP);
		if(isSmallMainScreen())
			ScrDisp_Api(LINE2, 0, tmp,    CDISP);
		else
			ScrDisp_Api(LINE5, 0, tmp,    CDISP);

		#ifdef __SECCODEDISP__
			secscrOpen_Api();
			secscrCls_Api();

			secscrSetAttrib_Api(4, 1);
			secscrSetBackLightMode_Api(1, 300);

			secscrPrint_Api(0, 0, 0, buf);
		#endif

		char data[1024] = "";
		char words[256] = "";

		ret = AmtToMp3(buf, data, words, "Dollar", "Cent");
		if (ret) {
			AppPlayTip(words);
		}

		WaitAnyKey_Api(5);
	}

	clearSmallScreen();

	PiccStop();
}

void TransInsertCard()
{
	int ret = 0, amt, tipline;
	unsigned char tmp[32], buf[12];

	if(isSmallMainScreen())
		tipline = LINE2;
	else
		tipline = LINE3;

	memset(&PosCom, 0, sizeof(PosCom));

	ScrCls_Api();
	ScrDisp_Api(LINE1, 0, "Sale", CDISP);
	ScrDisp_Api(tipline, 0, "Input amount:", LDISP);

	AppPlayTip("Please input amount");

	if (GetAmount(PosCom.stTrans.TradeAmount) != 0)
	{
		Beep_Api(1);
		return;
	}

	AppPlayTip("Amount Confirmed");

	ret = EmvGetCard(MASK_INCARDNO_ICC | MASK_INCARDNO_PICC, CARD_EMVFULLNOCASH | CARD_QPASSONLINE);
	LogPrintWithRet(0, "EmvGetCard(): ", ret);

	if ((ret != 0) || (DispCardNo() != 0))
	{
		Beep_Api(1);

		ScrCls_Api();
		ScrDisp_Api(LINE1, 0, "Sale", CDISP);
		ScrDisp_Api(tipline, 0, "Transaction failed.", CDISP);

		AppPlayTip("Transaction failed.");

		return;
	}

	ScrCls_Api();
	ScrDisp_Api(LINE1, 0, "Sale",          CDISP);
	ScrDisp_Api(tipline, 0, "Connecting...", CDISP);

	Delay_Api(2000);

	ScrDisp_Api(tipline, 0, "Sending MSG...", CDISP);
	Delay_Api(2000);
	ScrDisp_Api(tipline, 0, "Receiving MSG...", CDISP);

	Delay_Api(2000);

	// send data, receive data
	//if(PosCom.stTrans.EntryMode[0] == PAN_PAYWAVE) ret = PaywaveTransComplete();

	if (ret == 0)
	{
		amt = BcdToLong_Api(PosCom.stTrans.TradeAmount, 6);

		memset(buf, 0, sizeof(buf));
		memset(tmp, 0, sizeof(tmp));

		sprintf(buf, "%d.%02d", amt / 100, amt % 100);
		sprintf(tmp, "Paid: %s", buf);

		ScrCls_Api();
		ScrDisp_Api(LINE1, 0, "Sale", CDISP);
		if(isSmallMainScreen())
			ScrDisp_Api(tipline, 0, tmp,    CDISP);
		else
			ScrDisp_Api(LINE5, 0, tmp,    CDISP);

		#ifdef __SECCODEDISP__
			secscrOpen_Api();
			secscrCls_Api();

			secscrSetAttrib_Api(4, 1);
			secscrSetBackLightMode_Api(1, 300);

			secscrPrint_Api(0, 0, 0, buf);
		#endif

		char data[1024] = "";
		char words[256] = "";

		ret = AmtToMp3(buf, data, words, "Dollar", "Cent");
		if (ret) {
			AppPlayTip(words);
		}

		WaitAnyKey_Api(5);
	}

	clearSmallScreen();

	PiccStop();
}

void UnzipMp3()
{
	int ret;

	AppPlayTip("Processing");

	ret = fileunZip_Api("/ext/mp3.zip", "/ext/");
	LogPrintWithRet(0, "=> [Card.c]->fileunZip_Api(): ", ret);

	AppPlayTip("Done");
}
