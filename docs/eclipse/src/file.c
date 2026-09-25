#include "def.h"
#include "func.h"

#include <coredef.h>
#include <struct.h>
#include <poslib.h>
#include <string.h>

#define ZIPPATH "/ext/"

u8* filename(u8* file)
{
	return file;
}

void filegetlistcbtesting(const char *pchDirName, uint32 size, uint8 filetype, void *arg)
{
	char *pChar;
	int ret, num;

	//MAINLOG_L1("[%s] -%s- Line=%d: size = %d,filetype=%d,pchDirName:%s, arg=%s\r\n", filename(__FILE__), __FUNCTION__, __LINE__,size, filetype, pchDirName, arg);

	//delete all mp3 files
	if (fileFilter == 1) {
		pChar = strstr(pchDirName, ".mp3");
		if (pChar != NULL) {
			ret = DelFile_Api(pchDirName);
		}
	}

	//delete all .img files
	if (fileFilter == 2) {
		pChar = strstr(pchDirName,".img");
		if (pChar != NULL) {
			ret = DelFile_Api(pchDirName);
		}
	}

	//search app to update
	if (fileFilter == 3) {
		pChar = strstr(pchDirName,".img");
		if (pChar != NULL) {
			num = 9;
			memset(updateAppName, 0, sizeof(updateAppName));
			memcpy(updateAppName, pchDirName, strlen(pchDirName));

			LogPrintInfo(pchDirName);

			needUpdate = 1;
		}
	}
}

void folderFileDisplay(unsigned char *filePath)
{
	LogPrintNoRet("=> [file.c]->folderFileDisplay()");

	int iRet = -1;
	uint8 *rP = NULL;

	iRet = fileGetFileListCB_Api(filePath, filegetlistcbtesting, rP);
}

int unzipDownFile(unsigned char *fileName){
	int ret;

	ret = fileunZip_Api(fileName, ZIPPATH);
	if(ret != 0){
		return -1;
	}
	DelFile_Api(fileName);//delete zip after unzip it

	return 0;
}

void CheckAppFile()
{
	int ret;
	unsigned char buf[32] = "";

	ret = GetFileSize_Api(SAVE_UPDATE_FILE_NAME);
	LogPrintWithRet(0, "=> [file.c]->GetFileSize_Api('/ext/ifUpdate'): ", ret);

	if (ret > 0)
	{
		ret = ReadFile_Api(SAVE_UPDATE_FILE_NAME, buf, 0, (unsigned int *)&ret);

		if (ret == 0) {
			LogPrintInfo(buf);

			ret = DelFile_Api(buf);
			ret = DelFile_Api(SAVE_UPDATE_FILE_NAME);
		}
	}
}

int ReadLog( LOG_STRC *pLog, int logNo )
{
	u32 off, Len;

	if (gCtrlParam.iTransNum == 0) { return 1; }

	if (logNo == LAST_REC_LOG) {
		off = (gCtrlParam.iTransNum - 1) * LOG_SIZE;
	}
	else {
		off = logNo * LOG_SIZE;
	}

	Len = LOG_SIZE;
	if (ReadFile_Api(RECORDLOG, (unsigned char *)pLog, off, &Len) == 0)
	{
		if (Len == LOG_SIZE) { return 0; }
	}
	else {
		return E_MEM_ERR; // 13
	}

	return 0;
}
