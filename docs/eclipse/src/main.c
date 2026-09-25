#include "def.h"

#include <coredef.h>
#include <struct.h>
#include <poslib.h>
#include <stdio.h>
#include <string.h>
#include <Q161Func_From_Devlib.h>

const APP_MSG App_Msg =
{
	"Q161ProDemo",
	"Q161ProDemoApp",
	"V1.05.11.260511",
	"Aisino",
	__DATE__ " " __TIME__,
	"",
	0,
	0,
	0,
	"1002406171530"
};

void InitSys(void)
{
	int ret;
	unsigned char bp[32];

	initParam(); // Load parameters

	LogPrintNoRet("========== SYS_PARAM ==========");
	LogPrintNoRet(G_sys_param.sn);
	LogPrintNoRet(G_sys_param.mqtt_server);
	LogPrintNoRet(G_sys_param.mqtt_port);
	LogPrintNoRet(G_sys_param.mqtt_client_id);
	LogPrintNoRet(G_sys_param.mqtt_topic);
	LogPrintWithRet(0, "SSL: ", G_sys_param.mqtt_ssl);
	LogPrintWithRet(0, "QOS: ", G_sys_param.mqtt_qos);
	LogPrintWithRet(0, "KEEP-ALIVE: ", G_sys_param.mqtt_keepalive);
	LogPrintWithRet(0, "SOUND-LEVEL: ", G_sys_param.sound_level);
	LogPrintNoRet("===============================");

	initDeviceType(); //initializes device type

	if(G_sys_param.commode == TRANCOMM_WIFI)
	{
		NetModuleOper_Api(GPRS, 0);
		NetModuleOper_Api(WIFI, 1);

		ret = wifiOpen_Api();
		LogPrintWithRet(0, "wifiOpen_Api(): ", ret);
	}
	else
	{
		NetModuleOper_Api(GPRS, 1);
		NetModuleOper_Api(WIFI, 0);
	}

	net_init();   // Network initialization
	initMqttOs(); // MQTT client initialization

	memset(bp, 0, sizeof(bp));
	ret = sysReadBPVersion_Api(bp);

	char info[48] = "";
	sprintf(info, "FW = %d, BP = %s", ret, bp);
	LogPrintInfo(info);

	memset(bp, 0, sizeof(bp));
	ret = sysReadVerInfo_Api(4, bp);

	memset(info, 0, sizeof(info));
	sprintf(info, "LIB = %d", ret);
	LogPrintInfo(info);

	// check if any app to update
	/*ret = checkAppUpdate();
	LogPrintWithRet(0, "checkAppUpdate(): ", ret);

	if (ret < 0) { set_tms_download_flag(1); }*/

	// =============== CONTACTLESS TXN
	ret = Common_Init_Api();  if (ret != 0) LogPrintWithRet(1, "!!! Common_Init_Api() failed(%d) !!!", ret);
	ret = PayPass_Init_Api(); if (ret != 0) LogPrintWithRet(1, "!!! PayPass_Init_Api() failed(%d) !!!", ret);
	ret = PayWave_Init_Api(); if (ret != 0) LogPrintWithRet(1, "!!! PayWave_Init_Api() failed(%d) !!!", ret);
	ret = qUICS_Init_Api(); if (ret != 0) LogPrintWithRet(1, "!!! qUICS_Init_Api() failed(%d) !!!", ret);
	ret = RUPAY_Init_Api(); if (ret != 0) LogPrintWithRet(1, "!!! RUPAY_Init_Api() failed(%d) !!!", ret);

	PayPassAddAppExp(0);
	PayWaveAddAppExp();
	QuicsAddAppExp();
	// ===============

	// =============== CONTACT TXN
	ret = EMV_Init_Api();
	if (ret != 0) LogPrintWithRet(1, "!!! EMV_Init_Api() failed(%d) !!!", ret);

	EmvAddAppExp();
	// ===============

	secscrOpen_Api();
	secscrCls_Api();
	secscrSetAttrib_Api(4,1);  //brush automatically for secscrPrint_Api
	secscrSetAttrib_Api(7,0);  //display text in UTF-8 format
	secscrSetBackLightMode_Api(1, 500); //keep back ligth for 5s, then turn it off

	fibo_thread_create(MenuThread, "mainMenu", 128 * 1024, NULL, 24);

	if(isSmallMainScreen())
		SetMyPowerMenu();

	//Common_DbgEN_Api(1);
}

int AppMain(int argc, char **argv)
{
	int ret, signal_lost_count = 0, mobile_network_registered = 0, wifiTZflag = 0, simTZflag = 0;

	SystemInit_Api(argc, argv);

	SetApiCoreLogLevel(1);
	EnableLogPortPrint();

	InitSys();

	AppPlayTipFile(BOOT_TIP_FILE, "Welcome to SoundBox");
	Delay_Api(5000);

	DispMainFace();
	ScrBackLight_Api(30);

	while (1)
	{
		if(G_sys_param.commode == TRANCOMM_WIFI)
		{
			ret = wifiGetLinkStatus_Api();
			LogPrintWithRet(0, "WIFI STATUS == ", ret);

			if (ret == 5) { //AP not connected
				if(G_InputFlag == 0) //if GetScanfEx_Api is called during set wifi password, playing tip will effect input
				{
					if((strlen(G_sys_param.wifiSsid) == 0) && (strlen(G_sys_param.wifiPwd) == 0))
					{
						AppPlayTip("Configure Wifi first!");
						Delay_Api(10000);
					}
					else
					{
						if(wifiAPConnect_Api(G_sys_param.wifiSsid, G_sys_param.wifiPwd) != 0)
						{
							AppPlayTip("Connect AP failed !");
							Delay_Api(5000);
						}
					}
				}
				else
				{
					Delay_Api(3000);
				}				
				continue;
			}
			else if (ret == -6300) { // not open
				ret = wifiOpen_Api();
				LogPrintWithRet(0, "wifiOpen_Api(): ", ret);

				if (ret != 0) {
					AppPlayTip("WiFi open failed!");
					Delay_Api(5000);
					continue;
				}
			}
			else if (ret == -6302) { // check status failed
				AppPlayTip("Checking WiFi connection in progress");

				Delay_Api(5000);
				continue;
			}

			if(wifiTZflag == 0)
			{
				char recv[513];

				//synchronize time into WIFI chip after restart, otherwise wifiSSLConnect_Api may return -6306 even though terminal date/time is right by GetTime_Api; date/time in wifi chip is different from terminal's.
				//remember to synchronize time before the first connection with WIFI;
				WifiespSetTimezone_Api(32);  //GMT+8
				//ret = wifiSetTime_Api();   //by Alibaba Cloud NTP server, use wifiCtrl_Api to set a reliable NTP server if it not work for some countries.
				ret = wifiCtrl_Api(1, "edu.ntp.org.cn", 14, recv, 512); //set a reliable NTP server
				MAINLOG_L1("wifiCtrl_Api:%d", ret);
				if(ret == 0)
					wifiTZflag = 1;
			}

			mQTTMainThread();

		}
		else
		{
			ret = NetLinkCheck_Api(GPRS);
			LogPrintWithRet(0, "GPRS STATUS == ",ret);

			if (ret == 2) {
				AppPlayTip("Please insert sim card and restart device");
				Delay_Api(10 * 1000);
				continue;
			}
			else if (ret == 1)
			{
				if (signal_lost_count > 10) {
					AppPlayTip("Cannot register mobile network, try restart device");
					Delay_Api(10000);
					continue;
				}

				AppPlayTip("Mobile network registration in progress");
				Delay_Api(3000);

				signal_lost_count++;
				mobile_network_registered = 0;

				continue;

			} else {
				signal_lost_count = 0;

				if (mobile_network_registered == 0) {
					mobile_network_registered = 1;
					AppPlayTip("Mobile network connected");
				}
			}

			if(simTZflag == 0)
			{
				ret = sysSetTimezone_Api(32); //GMT+8
				MAINLOG_L1("sysSetTimezone_Api:%d", ret);
				if(ret == 0)
					simTZflag = 1;
			}

			mQTTMainThread();
		}

		Delay_Api(5000);
	}

	return 0;
}
