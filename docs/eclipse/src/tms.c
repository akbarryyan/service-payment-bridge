#include "def.h"
#include "httpDownload.h"

#include <coredef.h>
#include <struct.h>
#include <poslib.h>
#include <stdio.h>

static volatile int tms_download_flag = 0;
static int tmsPlayTimer = -1;
static int tmsTotleTime = TMSMAXTIME;

int fileFilter = -1;
int needUpdate = -1;
unsigned char updateAppName[32];

int tmsRestTimer()
{
	tmsPlayTimer = TimerSet_Api();
}
int tmsCheckOnceTimer()
{
	if(tmsPlayTimer == -1)
	{
		tmsRestTimer();
		return 0;
	}

	return TimerCheck_Api(tmsPlayTimer, TMSPLAYONCETIME*1000);
}

int tmsCheckUpdate()
{
	char tips[128];

	if (tmsCheckOnceTimer()) {
		if (tmsTotleTime == TMSMAXTIME) {
			tmsTotleTime -= TMSPLAYONCETIME;

			sprintf(tips, "New app found, update in %d seconds", tmsTotleTime);
			AppPlayTip(tips);
		} else {
			tmsTotleTime -= TMSPLAYONCETIME;

			if (tmsTotleTime <= 0) {
				return 0; // time is up, start updating
			}
			if (tmsTotleTime%10 == 0) {
				sprintf(tips, "%d Seconds to update", tmsTotleTime);
				AppPlayTip(tips);
			}
		}

		tmsRestTimer();
	}

	return 2;
}

int checkAppUpdate()
{
	int ret = -1;

	CheckAppFile();

	fileFilter = 3;
	folderFileDisplay("/app/ufs");

	LogPrintWithRet(0, "needUpdate = ", needUpdate);

	if (needUpdate == 1) {
		while (1) {
			ret = tmsCheckUpdate();
			if (ret == 0) { break; }
		}

		ret = WriteFile_Api(SAVE_UPDATE_FILE_NAME, updateAppName, 0, strlen(updateAppName));
		LogPrintWithRet(0, "=> [tms.c]->WriteFile_Api(): ", ret);

		AppPlayTip("start updating");

		ret = tmsUpdateFile_Api(TMS_FLAG_APP, updateAppName, NULL);
		LogPrintWithRet(0, "=> [tms.c]->tmsUpdateFile_Api(): ", ret);

		if (ret < 0) {
			ret = DelFile_Api(SAVE_UPDATE_FILE_NAME);
		}
	} else {
		return 2; //without app to update
	}

	return ret;
}

// type is to be defined for different download tasks.
// here we just use non-zero value to indicate a download action.
void set_tms_download_flag(int type)
{
	tms_download_flag = type;
}

void TMSThread(void)
{
	int ret;

	while (1) {
		while (tms_download_flag == 0) { Delay_Api(1000); }

		ret = httpDownload("https://support-f.vanstone.com.cn/resource/r_b6c481e47b1644aa9a5489510be834f1/UpdateData.zip", METHOD_GET, TMS_DOWN_FILE);//https://bharatpe-pos-poc.s3-us-west-2.amazonaws.com/urlConf.dat
		LogPrintWithRet(0, "httpDownload(): ", ret);

		if (ret > 0) {
			// Prepare for update after reboot, may need to prompt user
			ret = unzipDownFile(TMS_DOWN_FILE);
			LogPrintWithRet(0, "unzipDownFile(): ", ret);

			//or check update app when terminal power on
			ret = checkAppUpdate();
			LogPrintWithRet(0, "checkAppUpdate(): ", ret);

			if (ret == 2) {
				LogPrintNoRet("without new app to update");
			}
		}

		tms_download_flag = 0;
	}
}
