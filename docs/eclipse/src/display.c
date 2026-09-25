#include "def.h"

#include <coredef.h>
#include <struct.h>
#include <poslib.h>
#include <string.h>
#include <stdio.h>
#include <Q161Func_From_Devlib.h>

#define QRBMP "QRBMP.bmp"

volatile int  G_qrisReqPending = 0;
volatile long G_qrisReqAmount  = 0;
volatile int  G_qrisReplyReady = 0;
char          G_qrisReply[QRIS_MAX_LEN];

// Tingkat koreksi galat, mengikuti demo. Makin tinggi makin tahan kotor dan pantulan, tapi makin
// sedikit data yang muat pada versi yang sama.
#define QR_LEVEL	3

// Versi diserahkan ke pustakanya. Angka ini hanya nilai terkecil yang sah; pustaka menaikkannya
// sendiri sampai teksnya muat.
#define QR_VERSION	1

// Jarak kosong di tiap tepi layar, dalam piksel, supaya kode tidak menyentuh pinggiran.
#define QR_MARGIN	8

// 1 = tampilkan angka hasil pengukuran sebelum QR. Dipakai saat menyetel tata letaknya; nyalakan
// lagi bila suatu saat QR-nya meleset dari layar.
#define QR_DIAG		0

// Membaca lebar BMP hasil encode dari header berkasnya: BMP menyimpan lebar sebagai empat byte
// little-endian pada offset 18.
//
// Pengukuran ini yang menggantikan seluruh tebakan sebelumnya. Parameter "version" ternyata
// diabaikan QREncodeString -- diminta versi 1 untuk 300 karakter pun ia tetap mengembalikan 0
// lalu memakai versi yang benar-benar muat. Akibatnya jumlah modul tidak bisa dihitung dari
// parameter, dan satu-satunya sumber yang jujur adalah berkas hasilnya sendiri.
// Dibaca lewat ReadFile_Api, bukan fopen: fopen menarik fungsi sistem (open, read, lseek) yang
// tidak disediakan firmware ini, dan tautannya langsung gagal.
static int qrBmpWidth(void)
{
	unsigned char hdr[26];
	unsigned int len = sizeof(hdr);

	memset(hdr, 0, sizeof(hdr));
	if (ReadFile_Api(QRBMP, hdr, 0, &len) != 0 || len < sizeof(hdr))
		return 0;

	if (hdr[0] != 'B' || hdr[1] != 'M')
		return 0;

	return (int)hdr[18] | ((int)hdr[19] << 8) | ((int)hdr[20] << 16) | ((int)hdr[21] << 24);
}

void QRCodeDispText(const char *text)
{
	#ifdef __LCDDISP__
		int ret, len, base, zoom, size, x, y, w, h, barH, field;
		char diag[32];
		struct _LCDINFO lcdInfo;

		if (text == NULL || *text == 0)
			return;

		memset(&lcdInfo, 0, sizeof(lcdInfo));
		ScrGetInfo_Api(&lcdInfo);

		// Terbukti di perangkat: 320x480 fisik, 320x456 terpakai. Selisih 24 piksel itu bilah
		// status di atas, dan QR harus digeser ke bawah sejauh itu supaya tidak tertimpa.
		w = lcdInfo.UseWigtgh > 0 ? lcdInfo.UseWigtgh : lcdInfo.Wigtgh;
		h = lcdInfo.UseHeigth > 0 ? lcdInfo.UseHeigth : lcdInfo.Heigth;
		barH = lcdInfo.Heigth - h;
		if (barH < 0)
			barH = 0;

		len = strlen(text);

		// Langkah 1: encode dengan zoom 1 semata-mata untuk mengukur. Lebar hasilnya sama dengan
		// jumlah modul ditambah bingkai kosong yang ditambahkan pustakanya sendiri.
		ret = QREncodeString(text, QR_VERSION, QR_LEVEL, QRBMP, 1);
		if (ret != 0) {
			LogPrintWithRet(1, "!!! QREncodeString(ukur) failed(%d) !!!", ret);
			return;
		}

		base = qrBmpWidth();
		if (base <= 0) {
			LogPrintWithRet(1, "!!! lebar BMP tidak terbaca !!!", base);
			return;
		}

		// Langkah 2: perbesar sebanyak yang masih muat. Pembagian bulat ke bawah memastikan
		// hasilnya tidak pernah melewati tepi, berapa pun panjang teksnya.
		field = (w < h ? w : h) - 2 * QR_MARGIN;
		zoom = field / base;
		if (zoom < 1)
			zoom = 1;

		ret = QREncodeString(text, QR_VERSION, QR_LEVEL, QRBMP, zoom);
		if (ret != 0) {
			LogPrintWithRet(1, "!!! QREncodeString(zoom=%d) failed(%d) !!!", zoom, ret);
			return;
		}

		// Diukur lagi, bukan dikalikan: zoom bisa saja diperlakukan lain oleh pustakanya, dan
		// posisi tengah hanya benar kalau ukurannya benar.
		size = qrBmpWidth();
		if (size <= 0)
			size = base * zoom;

		x = (w - size) / 2;
		y = barH + (h - size) / 2;
		if (x < 0) x = 0;
		if (y < barH) y = barH;

		MAINLOG_L1("QR %d karakter, base %d, zoom %d, %d px, di (%d,%d)",
				len, base, zoom, size, x, y);

		#if QR_DIAG
			memset(diag, 0, sizeof(diag));
			sprintf(diag, "len %d base %d", len, base);
			ScrCls_Api();
			ScrDisp_Api(LINE1, 0, diag, LDISP);
			memset(diag, 0, sizeof(diag));
			sprintf(diag, "zoom %d size %d", zoom, size);
			ScrDisp_Api(LINE2, 0, diag, LDISP);
			memset(diag, 0, sizeof(diag));
			sprintf(diag, "at %d,%d bar %d", x, y, barH);
			ScrDisp_Api(LINE3, 0, diag, LDISP);
			WaitAnyKey_Api(20);
		#else
			(void)diag;
		#endif

		ScrCls_Api();
		ScrDispImage_Api(QRBMP, x, y);
	#endif
}

void QRCodeDisp(void)
{
	QRCodeDispText("http://115.159.28.147:54321/hivemq.htm");
}

// Alur QRIS dinamis: merchant mengetik nominal, perangkat meminta string QRIS ke backend lewat
// MQTT, lalu menampilkannya sebagai kode QR. Perangkat sengaja tidak tahu dari mana string itu
// berasal -- backend boleh mengarangnya sendiri saat uji coba, atau memintanya ke gateway
// pembayaran nanti, tanpa firmware ini perlu diubah lagi.
void QrisDinamis(void)
{
	u8 amtBcd[6];
	long amt;
	int waited;
	char line[32];

	memset(amtBcd, 0, sizeof(amtBcd));

	ScrCls_Api();
	ScrDisp_Api(LINE1, 0, "Input amount:", LDISP);

	if (GetAmount(amtBcd) != 0) {
		Beep_Api(1);
		return;
	}

	amt = BcdToLong_Api(amtBcd, 6);
	if (amt <= 0) {
		ScrDisp_Api(LINE2, 0, "Amount invalid", LDISP);
		WaitAnyKey_Api(3);
		return;
	}

	memset(line, 0, sizeof(line));
	sprintf(line, "Rp %ld.%02ld", amt / 100, amt % 100);
	ScrCls_Api();
	ScrDisp_Api(LINE1, 0, line, LDISP);
	ScrDisp_Api(LINE2, 0, "Requesting QR...", LDISP);

	// Serahkan ke utas MQTT. Bendera balasan dibersihkan lebih dulu supaya balasan lama dari
	// permintaan sebelumnya tidak terbaca sebagai jawaban permintaan ini.
	G_qrisReplyReady = 0;
	G_qrisReqAmount  = amt;
	G_qrisReqPending = 1;

	for (waited = 0; waited < 150; waited++) {	// 150 x 200 ms = 30 detik
		if (G_qrisReplyReady)
			break;
		Delay_Api(200);
	}

	if (!G_qrisReplyReady) {
		ScrDisp_Api(LINE2, 0, "QR request timeout", LDISP);
		MAINLOG_L1("QrisDinamis: tidak ada balasan untuk %ld sen", amt);
		WaitAnyKey_Api(5);
		return;
	}

	QRCodeDispText(G_qrisReply);
	WaitAnyKey_Api(60);
}

void DispMainFace(void)
{
	if(isSmallMainScreen())
	{
		int i;
		char buf[24];
		char *p = App_Msg.Version;

		for(i=0; i<3; i++)
		{
			if(p++ == NULL)
				break;
			p = strstr(p, ".");
		}
		memset(buf, 0, sizeof(buf));
		strcpy(buf, "Soundbox ");
		if(p != NULL)
			strcat(buf, ++p);

		ScrFontSet_Api(1);
		ScrClsRam_Api();
		ScrDispRam_Api(LINE1,  0, "Welcome to use", CDISP);
		ScrDispRam_Api(LINE2, 0, buf, CDISP);
		ScrBrush_Api();
	}
	else
	{
		ScrFontSet_Api(5);
		ScrClsRam_Api();
		ScrDispRam_Api(LINE7,  0, "Welcome to Sound Box", CDISP);
		ScrDispRam_Api(LINE8,  0, "Aisino Q161Pro", CDISP);
		ScrDispRam_Api(LINE9,  0, "Demo App", CDISP);
		ScrDispRam_Api(LINE10, 0, App_Msg.Version, CDISP);
		ScrBrush_Api();
	}
}

int WaitEvent(void)
{
	u8 Key;
	int TimerId;

	TimerId = TimerSet_Api();
	while (1)
	{
		if (TimerCheck_Api(TimerId , 30 * 1000)) { return 0xfe; }

		Key = GetKey_Api();
		if (Key != 0) { return Key; }
	}

	return 0;
}

int ShowMenuItem(char *Title, const char *menu[], u8 ucLines, u8 ucStartKey, u8 ucEndKey, int IsShowX, u8 ucTimeOut)
{
	u8 IsShowTitle, cur_screen, OneScreenLines, Cur_Line, i, t;
	int nkey;
	char dispbuf[50];

	memset(dispbuf, 0, sizeof(dispbuf));

	if(isSmallMainScreen())
	{
		if (Title != NULL)
		{
			IsShowTitle = 1;
			OneScreenLines = 1;
		}
		else {
			IsShowTitle = 0;
			OneScreenLines = 2;
		}
	}
	else
	{
		if (Title != NULL)
		{
			IsShowTitle = 1;
			OneScreenLines = 12;
		}
		else {
			IsShowTitle = 0;
			OneScreenLines = 13;
		}
	}

	IsShowX -= 1;
	cur_screen = 0;

	while (1)
	{
		ScrClsRam_Api();

		if (IsShowTitle) ScrDisp_Api(LINE1, 0, Title, CDISP);

		Cur_Line = LINE1 + IsShowTitle;

		for (i = 0; i < OneScreenLines; i++)
		{
			t = i + cur_screen * OneScreenLines;
			if (t >= ucLines || menu[t] == NULL) break;

			memset(dispbuf, 0, sizeof(dispbuf));
			strcpy(dispbuf, menu[t]);
			ScrDispRam_Api(Cur_Line++, 0, dispbuf, FDISP);
		}
		ScrBrush_Api();

		nkey = WaitAnyKey_Api(ucTimeOut);
		LogPrintWithRet(0, "WaitAnyKey_Api(): ", nkey);

		switch(nkey)
		{
			case ESC:
			case TIMEOUT:
				return nkey;
			case UP:
				if(cur_screen > 0){
					cur_screen--;
				}else{
					Beep_Api(1);
				}
				break;
			case DOWN:
				if(t < (ucLines-1)){
					cur_screen++;
				}else{
					Beep_Api(1);
				}
				break;
			default:
				if ((nkey >= ucStartKey)&&(nkey <= ucEndKey)) return nkey;
				break;
		}
	}
}

void MenuThread()
{
	int Result = 0;

	while (1)
	{
		DispMainFace();

		Result = WaitEvent();

		if (Result == 0xfe) { continue; }

		if (Result != 0)
		{
			switch (Result)
			{
				case ENTER:
					if(isSmallMainScreen())
						SelectMainMenuForSmallDisplay();
					else
						SelectMainMenuForLargeDisplay();
					break;
				default: break;
			}
		}
	}
}

void SelectMainMenuForSmallDisplay(void)
{
	int nSelcItem = 1, num;

	char *pszTitle = "Menu";
	const char *pszItems[] =
	{
		"1.Amt Display(BackScr)",
		"2.Tran By Tap Card",
		"3.Tran By Insert Card",
		"4.RemoteUpgrade",
		"5.Settings"
	};

	while (1)
	{
		num = sizeof(pszItems) / sizeof(char *);
		nSelcItem = ShowMenuItem(pszTitle, pszItems, num, DIGITAL1, DIGITAL0 + num, 0, 60);
		LogPrintWithRet(0, "ShowMenuItem(): ", nSelcItem);

		switch (nSelcItem)
		{
			case DIGITAL1:
				#ifdef __SECCODEDISP__
					secscrOpen_Api();
					secscrCls_Api();

				    secscrSetAttrib_Api(4, 1);
				    secscrSetBackLightMode_Api(1, 300);

				    secscrPrint_Api(0, 0, 0, "1234567.89");

				    WaitAnyKey_Api(3);

				    secscrCls_Api();
				    secscrClose_Api();
				#endif
				break;

			case DIGITAL2: TransTapCard();    break;
			case DIGITAL3: TransInsertCard(); break;

			case DIGITAL4:
				TmsRemoteUpgrade();
				break;

			case DIGITAL5:
				SelectSettingsMenu();
				break;

			case ESC:
				Beep_Api(1);
				return;

			default: break;
		}
	}
}

void SelectMainMenuForLargeDisplay(void)
{
	int nSelcItem = 1, num;

	char *pszTitle = "Menu";
	const char *pszItems[] =
	{
		"1.Amt Display(BackScr)",
		"2.Tran By Tap Card",
		"3.Tran By Insert Card",
		"4.QRIS Dinamis",
		"5.RemoteUpgrade",
		"6.Settings"
	};

	while (1)
	{
		num = sizeof(pszItems) / sizeof(char *);

		nSelcItem = ShowMenuItem(pszTitle, pszItems, num, DIGITAL1, DIGITAL0 + num, 0, 60);
		LogPrintWithRet(0, "ShowMenuItem(): ", nSelcItem);

		switch (nSelcItem)
		{
			case DIGITAL1:
				#ifdef __SECCODEDISP__
					secscrOpen_Api();
					secscrCls_Api();

				    secscrSetAttrib_Api(4, 1);
				    secscrSetBackLightMode_Api(1, 300);

				    secscrPrint_Api(0, 0, 0, "1234567.89");

				    WaitAnyKey_Api(3);

				    secscrCls_Api();
				    secscrClose_Api();
				#endif
				break;

			case DIGITAL2: TransTapCard();    break;
			case DIGITAL3: TransInsertCard(); break;

			case DIGITAL4:
				QrisDinamis();
				break;

			case DIGITAL5:
				TmsRemoteUpgrade();
				break;

			case DIGITAL6:
				SelectSettingsMenu();
				break;

			case ESC:
				Beep_Api(1);
				return;

			default: break;
		}
	}
}


void SelectVolumMenu(void)
{
	LogPrintNoRet("=> [display.c]->SelectSettingsMenu()");

	int nSelcItem = 1, ret;

	char *pszTitle = "Settings";
	const char *pszItems[] = {
		"1.Volum up" ,
		"2.Volum down"
	};

	while(1)
	{
		nSelcItem = ShowMenuItem(pszTitle, pszItems, sizeof(pszItems)/sizeof(char *), DIGITAL1, DIGITAL4, 0, 60);
		LogPrintWithRet(0, "=> [display.c]->ShowMenuItem(): ", nSelcItem);

		switch (nSelcItem)
		{
			case DIGITAL1:
				LogPrintWithRet(0, "G_sys_param.sound_level = ", G_sys_param.sound_level);

				if (G_sys_param.sound_level >= 5)
					AppPlayTip("This is the maximum volume");
				else {
					G_sys_param.sound_level++;
					AppPlayTip("Volume up");
					saveParam();
				}
				break;

			case DIGITAL2:
				LogPrintWithRet(0, "G_sys_param.sound_level = ", G_sys_param.sound_level);

				if (G_sys_param.sound_level <= 1)
					AppPlayTip("This is the minimum volume");
				else {
					G_sys_param.sound_level--;
					AppPlayTip("Volume down");
					saveParam();
				}
				break;

			case ESC: return;

			default: break;
		}
	}
}

void SelectAPDULogMenu(void)
{
	LogPrintNoRet("=> [display.c]->SelectSettingsMenu()");

	int nSelcItem = 1, ret;

	char *pszTitle = "Settings";
	const char *pszItems[] = {
		"1.Enable APDU log",
		"2.Disable APDU log",
		"3.Output APDU log"
	};

	while(1)
	{
		nSelcItem = ShowMenuItem(pszTitle, pszItems, sizeof(pszItems)/sizeof(char *), DIGITAL1, DIGITAL4, 0, 60);
		LogPrintWithRet(0, "=> [display.c]->ShowMenuItem(): ", nSelcItem);

		switch (nSelcItem)
		{
			case DIGITAL1:
				Common_DbgEN_Api(1);
				break;

			case DIGITAL2:
				Common_DbgEN_Api(0);
				break;

			case DIGITAL3:
				PortOpenTest();
				OutputLog("ComLog.dat");
				break;

			case ESC: return;

			default: break;
		}
	}
}

void SelectSettingsMenu(void)
{
	LogPrintNoRet("=> [display.c]->SelectSettingsMenu()");

	int nSelcItem = 1, ret;

	char *pszTitle = "Settings";
	const char *pszItems[] = {
		"1.Volum",
		"2.COMM mode",
		"3.Scan code",
		"4.Output Vtms log",
		"5.Output APDU log"
	};

	while(1)
	{
		nSelcItem = ShowMenuItem(pszTitle, pszItems, sizeof(pszItems)/sizeof(char *), DIGITAL1, DIGITAL4, 0, 60);
		LogPrintWithRet(0, "=> [display.c]->ShowMenuItem(): ", nSelcItem);

		switch (nSelcItem)
		{
			case DIGITAL1:
				SelectVolumMenu();
				break;

			case DIGITAL2:
				SetCommMode();
				break;

			case DIGITAL3:
				ScanCodeTest();
				break;

			case DIGITAL4:
				SetVtmsLogOutputFlag();
				break;

			case DIGITAL5:
				SelectAPDULogMenu();
				break;

			case ESC: return;

			default: break;
		}
	}
}

void clearSmallScreen()
{
	secscrOpen_Api();
	secscrCls_Api();

	// ===== FIXME: An Temporarily Solution for solve the invalid "secscrCls_Api()"
	secscrSetAttrib_Api(4, 1);
	secscrPrint_Api(0, 0, 0, " ");
	// =====
}

