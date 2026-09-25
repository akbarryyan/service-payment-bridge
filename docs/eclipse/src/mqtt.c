#include "def.h"

#include <coredef.h>
#include <struct.h>
#include <poslib.h>
#include <string.h>

#include <MQTTClient.h>
#include <MQTTFreeRTOS.h>
#include <cJSON.h>
#include <Q161Func_From_Devlib.h>

int MENU_THREAD_INITIALIZED = 0;

void initMqttOs(void)
{
	MQTTOS os;
	memset(&os, 0, sizeof(os));

	os.timerSet = (unsigned long(*)())TimerSet_Api;
	MQTTClientOSInit(os);
}

static void onDefaultMessageArrived(MessageData* md)
{
	// Topic
	if (md->topicName->lenstring.data == NULL || md->topicName->lenstring.len <= 0) {
		LogPrintInfo("!!! DEFAULT MSG TOPIC ERROR !!!");
		return;
	}
	LogPrintInfo("DEFAULT MSG ARRIVED TOPIC:\n");
	LogPrintInfo(md->topicName->lenstring.data);

	// Data
	if (md->message->payload == NULL || md->message->payloadlen <= 0) {
		LogPrintInfo("!!! DEFAULT MSG ERROR !!!");
		return;
	}

	LogPrintWithRet(0, "DEFAULT MSG ARRIVED MSG LEN: ", md->message->payloadlen);

	char data[md->message->payloadlen + 1];
	memset(data, 0, sizeof(data));
	memcpy(data, md->message->payload, md->message->payloadlen);

	LogPrintInfo("DEFAULT MSG ARRIVED MSG:\n");
	LogPrintInfo(data);

	memset(data, 0, sizeof(data));
}

static void onTopicMessageArrived(MessageData* md)
{
	ScrBackLight_Api(30);

	if (md->message->payload == NULL || md->message->payloadlen <= 0) {
		LogPrintInfo("MESSAGE ERROR == NULL || LEN <= 0");
		return;
	}

	int data_len = md->message->payloadlen;
	LogPrintWithRet(0, "data_len = ", data_len);

	char buf[data_len + 2];
	memset(buf, 0, sizeof(buf));

	memcpy(buf, md->message->payload, data_len);
	LogPrintInfo(buf);

	// Balasan QRIS dinamis: "QR:" diikuti string yang harus ditampilkan sebagai kode QR.
	// Diperiksa lebih dulu karena payload QRIS bisa saja memuat ".mp3" secara kebetulan.
	// Ditaruh ke penyangga global lalu ditandai; utas menu yang menunggu akan mengambilnya.
	if (strncmp(buf, QRIS_REPLY_PREFIX, strlen(QRIS_REPLY_PREFIX)) == 0)
	{
		char *qr = buf + strlen(QRIS_REPLY_PREFIX);

		memset(G_qrisReply, 0, sizeof(G_qrisReply));
		strncpy(G_qrisReply, qr, sizeof(G_qrisReply) - 1);
		G_qrisReplyReady = 1;

		MAINLOG_L1("balasan QRIS diterima, %d karakter", (int)strlen(G_qrisReply));
		return;
	}

	// Payload yang memuat ".mp3" adalah daftar path berkas audio dipisah "+", dikirim Payment
	// API. Selain itu diperlakukan sebagai teks biasa dan dibacakan TTS seperti bawaan demo.
	if (strstr(buf, ".mp3") != NULL)
	{
		char *pStart = buf;
		char *pSep;
		char one[128];

		while (pStart != NULL && *pStart != 0)
		{
			int ret, tries = 0;

			memset(one, 0, sizeof(one));

			pSep = strchr(pStart, '+');
			if (pSep != NULL) {
				int n = pSep - pStart;
				if (n > (int)sizeof(one) - 1)
					n = sizeof(one) - 1;
				memcpy(one, pStart, n);
				pStart = pSep + 1;
			} else {
				strncpy(one, pStart, sizeof(one) - 1);
				pStart = NULL;
			}

			// -3 berarti audio masih sibuk memutar berkas sebelumnya. Tanpa pengulangan,
			// berkas kedua dan seterusnya akan terbuang. Batas 150 x 200 ms (30 detik)
			// mencegah satu berkas rusak menggantung selamanya -- vendor sendiri memakai
			// while(1) tanpa batas di PlayMP3File.
			do {
				ret = audioFilePlayPath_Api(one);
				if (ret != -3)
					break;
				Delay_Api(200);
			} while (++tries < 150);

			MAINLOG_L1("audioFilePlayPath_Api(%s) = %d, tries=%d", one, ret, tries);
		}
	}
	else
	{
		AppPlayTip(buf);
	}

	secscrOpen_Api();
	secscrCls_Api();

	secscrSetAttrib_Api(4, 1);
	secscrSetBackLightMode_Api(1, 300);

	secscrPrint_Api(0, 0, 0, buf);
}

static unsigned char pSendBuf[1024];
static unsigned char pReadBuf[1024];
void mQTTMainThread(void)
{
	u8 Key;
	int ret, err;

	Network n;
	MQTTClient c;
	MQTTPacket_connectData data = MQTTPacket_connectData_initializer;

	AppPlayTipFile(CONNECTED_SERVER, "Connecting to server");
	Delay_Api(5000);

	memset(&c, 0, sizeof(MQTTClient));
	MQTTClientInit(&c, &n, 20000, pSendBuf, 1024, pReadBuf, 1024);

	c.defaultMessageHandler = onDefaultMessageArrived;

	n.mqttconnect = net_connect;
	n.mqttclose   = net_close;
	n.mqttread    = net_read;
	n.mqttwrite   = net_write;

	n.netContext = n.mqttconnect(NULL, G_sys_param.mqtt_server, G_sys_param.mqtt_port, 60000, G_sys_param.mqtt_ssl, &err);
	if (n.netContext == NULL) {
		LogPrintNoRet("n.netContext == NULL");
		return;
	}

	data.willFlag          = 0;
	data.MQTTVersion 	   = 4; // 3.1.1
	data.clientID.cstring  = G_sys_param.mqtt_client_id;
	data.username.cstring  = "";
	data.password.cstring  = "";
	data.keepAliveInterval = G_sys_param.mqtt_keepalive;
	// cleansession 0 supaya broker menyimpan sesi perangkat ini dan mengantre pesan QoS 1
	// selama soundbox mati atau kehilangan jaringan, lalu mengirimkannya saat tersambung
	// kembali. Bergantung pada clientID yang stabil -- clientId-<SN>, diturunkan dari
	// perangkat keras, jadi tetap sama setiap boot.
	data.cleansession      = 0;

	ret = MQTTConnect(&c, &data);
	if (ret != 0)
	{
		LogPrintWithRet(1, "!!! MQTTConnect() failed(%d) !!!", ret);

		MQTTDisconnect(&c);
		n.mqttclose(n.netContext);

		return;
	}

	ret = MQTTSubscribe(&c, G_sys_param.mqtt_topic, G_sys_param.mqtt_qos, onTopicMessageArrived);
	if (ret != 0)
	{
		LogPrintWithRet(1, "!!! MQTTSubscribe() failed(%d) !!!", ret);

		MQTTDisconnect(&c);
		n.mqttclose(n.netContext);

		return;
	}

	AppPlayTipFile(SERVER_CONNECTED, "Server Connected");
	Delay_Api(5000);
	//AppPlayTip("Press Enter to show menu");

	LogPrintNoRet("===================== MQTT SERVER CONNECTED =====================");

	while (1)
	{
		ret = MQTTYield(&c, 500);
		if (ret < 0) {
			LogPrintWithRet(1, "!!! MQTTYield() failed(%d) !!!", ret);
			break;
		}

		// Permintaan QRIS dari utas menu diterbitkan di sini, bukan di utas itu sendiri:
		// MQTTClient hanya boleh disentuh satu utas. Formatnya "<merchant id>|<nominal sen>",
		// teks polos seperti payload lain di jalur ini.
		if (G_qrisReqPending)
		{
			char req[64];
			MQTTMessage msg;

			G_qrisReqPending = 0;

			memset(req, 0, sizeof(req));
			snprintf(req, sizeof(req), "%s|%ld", MQTT_MERCHANT_ID, G_qrisReqAmount);

			memset(&msg, 0, sizeof(msg));
			msg.qos        = QOS1;
			msg.retained   = 0;
			msg.payload    = req;
			msg.payloadlen = strlen(req);

			ret = MQTTPublish(&c, QRIS_REQUEST_TOPIC, &msg);
			MAINLOG_L1("MQTTPublish(%s, %s) = %d", QRIS_REQUEST_TOPIC, req, ret);

			if (ret < 0) {
				LogPrintWithRet(1, "!!! MQTTPublish() failed(%d) !!!", ret);
				break;
			}
		}

		Delay_Api(10);
	}

	MQTTDisconnect(&c);
	n.mqttclose(n.netContext);

	AppPlayTip("Server connection lost");
	LogPrintInfo("!!! SERVER CONNECTION LOST !!!");

	ScrBackLight_Api(30);
}
