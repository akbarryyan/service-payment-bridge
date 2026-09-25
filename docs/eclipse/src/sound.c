#include "def.h"

#include <coredef.h>
#include <struct.h>
#include <poslib.h>

void AppPlayTip(char *tip)
{
#ifdef __CHECKTYPESTATUS__
	while(1)
	{
		if(G_InputFlag == 1)
		{
			Delay_Api(500);
			continue;
		}
		break;
	}
#endif
	PlaySound_Api((unsigned char *)tip, G_sys_param.sound_level, 0);
}

// Memutar berkas audio di path apa pun, dengan TTS sebagai jaring pengaman.
//
// Berbeda dari PlayMP3File yang selalu menambahkan awalan "/app/ufs/", fungsi ini menerima path
// utuh, sehingga berkas di /ext/ bisa dipakai langsung. Disalin dari Q181 varian Z, tempat pola
// ini sudah terbukti; dua tundaannya bukan hiasan:
//
//   - Saat boot, sistem berkas belum tentu siap dan audioFilePlayPath_Api menjawab -2 ("berkas
//     tidak ada") untuk berkas yang sebenarnya ada. Karena itu ditunggu dulu sampai terlihat,
//     maksimal 5 detik, supaya berkas yang memang tidak ada tidak menahan boot lebih lama.
//   - -3 berarti perangkat audio sedang dipakai; itu bukan kegagalan, cukup diulang.
void AppPlayTipFile(char *path, char *fallbackTip)
{
	int ret = -1;
	int size = 0;
	int tries = 0;

	do {
		size = (int)GetFileSize_Api(path);
		if (size > 0)
			break;
		Delay_Api(200);
	} while (++tries < 25);

	MAINLOG_L1("AppPlayTipFile(%s) size=%d, tunggu=%d", path, size, tries);

	if (size > 0) {
		tries = 0;

		do {
			ret = audioFilePlayPath_Api(path);
			if (ret != -3)
				break;
			Delay_Api(200);
		} while (++tries < 150);

		MAINLOG_L1("audioFilePlayPath_Api(%s) = %d, tries=%d", path, ret, tries);
	}

	// -1 berkas rusak, -2 berkas tidak ada. Tetap bersuara supaya perangkat menunjukkan dirinya
	// hidup alih-alih diam sepenuhnya.
	if (ret != 0 && fallbackTip != 0)
		AppPlayTip(fallbackTip);
}

void PlayMP3File(char *audioFileName)
{
	int ret;
	char filePath[16];

	memset(filePath, 0, sizeof(filePath));
	sprintf(filePath, "/app/ufs/%s", audioFileName);

	LogPrintInfo(filePath);

	while(1){
#ifdef __CHECKTYPESTATUS__
		while(1)
		{
			if(G_InputFlag == 1)
			{
				Delay_Api(500);
				continue;
			}
			break;
		}
#endif
		ret = audioFilePlayPath_Api(filePath);

		if (ret==0){
			break;
		}
		else if (ret==-1) {
			LogPrintNoRet("Play failed, please check if the file is damaged");
			break;
		}
		else if (ret==-2) {
			LogPrintNoRet("File is not present");
			break;
		}
		else if (ret==-3) {
			LogPrintNoRet("TSS is occupied");
		}
	}
}
