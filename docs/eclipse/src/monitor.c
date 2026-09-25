#include "def.h"

#include <coredef.h>
#include <struct.h>
#include <poslib.h>

enum BATSTATUE {
	BAT_NORMAL,    // normal
	BAT_LOW,       // low power
	BAT_OFF,       // It needs to be shut down
	BAT_FULL,      // is filled
	BAT_CHARG,     // Charging
	BAT_ERROR,     // fault
};

static int signalPlayTimer = -1;
static unsigned short signalCount = 0;

static int batChargePlayTimer = -1;
static int batStatue = BAT_NORMAL;

int SingalProc()
{
	int Ret, ret;
	char *strength, *err;

	if(signalPlayTimer == -1) {
		signalPlayTimer = TimerSet_Api();
	}
	if(TimerCheck_Api(signalPlayTimer, 3000)) {
		// 3 seconds to get a signal, 5 minutes broadcast once
		Ret = CommSignalProc_Api(0, 0, 0, 1);
		if(Ret <= 1 && signalCount>100) {
			AppPlayTip("signal is poor");
			signalCount = 0;
		} else {
			signalCount++;
		}
		signalPlayTimer = TimerSet_Api();
	}

}

void PlayPowrValue(int type)
{
	int Ret, isElect;
	char temp[32];

	Ret = BatChargeProc_Api(0, 0, 0, 0, BATER_WARM, &isElect); // Immediate power acquisition
	LogPrintWithRet(0, "=> [monitor.c]->BatChargeProc_Api(): ", Ret);

	if(Ret == 0xaa) // charging
	{
		AppPlayTip("charging");
	}
	else if(Ret == -2)
	{
		AppPlayTip("battery fault");
	}
	else
	{
		if(isElect == 1)
		{
			AppPlayTip("The battery is full, please unplug");
		}
		else
		{
			if(type) {
				memset(temp, 0, sizeof(temp));
				sprintf(temp, "electricity %d percent", Ret);
				AppPlayTip(temp);

				if(Ret <=  BATER_POWR_OFF)
				{
					AppPlayTip(",,,The battery is too low, and it's shutting down");
					Delay_Api(7000);
					SysPowerOff_Api();
				}
				else if(Ret <= BATER_WARM)
					AppPlayTip(",,,Low battery please charge");
			}
		}
	}
}

int BatChargeProc()
{
	int Ret, isElect, timers,batFullTimes;
	char sTime[16];
	int hour = 0;

	if(batChargePlayTimer==-1)
	{
		batChargePlayTimer = TimerSet_Api();
	}

	Ret = BatChargeProc_Api(1, 0, 0, 0, BATER_WARM, &isElect);
	LogPrintWithRet(0, "=> [monitor.c]->BatChargeProc_Api(): ", Ret);

	if(Ret == 0xaa) // charging
	{
		Ret = BAT_CHARG;
	}
	else if(Ret == -2 || Ret == -1)
	{
		//Ret = BAT_ERROR;
		return -1;
	}
	else
	{
		if(isElect == 1)
			Ret = BAT_FULL;
		else
		{
			if(Ret <= BATER_POWR_OFF)
				Ret = BAT_OFF;
			else if(Ret <= BATER_WARM)
				Ret = BAT_LOW;
			else Ret = BAT_NORMAL;
		}
	}
	if(Ret != batStatue)
	{
		batStatue = Ret;
		if(batStatue != BAT_NORMAL)
		{
			PlayPowrValue(0);
			batChargePlayTimer = TimerSet_Api();
		}
	}
	if(batStatue == BAT_FULL)
		timers = 120*1000;
	else
	{
//		batFullTimes = 0;
		timers = 300*1000;
	}
	if(TimerCheck_Api(batChargePlayTimer, timers))
	{
		if(batStatue == BAT_LOW || batStatue == BAT_OFF ||
			batStatue == BAT_FULL || batStatue == BAT_ERROR)
		{
			if(batStatue == BAT_FULL)
			{
				batFullTimes++;

				memset(sTime, 0, sizeof(sTime));
				GetSysTime_Api(sTime);

				hour = BcdToLong_Api(&sTime[4], 1);

				//They broadcast every two minutes between 8 a.m. and 22 p.m
				if(!(hour>=8 && hour<=22))
				{
					batChargePlayTimer = TimerSet_Api();
					return -1;
				}
			}
			PlayPowrValue(0);
		}
		batChargePlayTimer = TimerSet_Api();
	}
}

void MonitorThread(void)
{
	while (1) {
		//SingalProc();
		//BatChargeProc();
		Delay_Api(1000);
	}
}
