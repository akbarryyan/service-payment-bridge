#include "def.h"
#include "httpDownload.h"

#include <coredef.h>
#include <struct.h>
#include <poslib.h>
#include <stdio.h>
#include <string.h>
#include <Q161Func_From_Devlib.h>

//int wirelessPdpOpen_lib(void);
int wirelessPppOpen_Api(unsigned char *pucApn, unsigned char *pucUserName, unsigned char *pucPassword) ;
//0-success   others-failed
int wirelessCheckPppDial_Api(void); //int wirelessCheckPdpDial_lib(int timeout);
int wirelessPppClose_Api(void) ;//int wirelessPdpRelease_lib(void);
int wirelessSocketCreate_Api(int nProtocol);
int wirelessSocketClose_Api(int sockid);
int wirelessTcpConnect_Api(int sockid, char *pucIP, char *pucPort, int timeout);//Q161
//int wirelessTcpConnect_lib(int sockid, char *pucIP, unsigned int uiPort); //Q181
int wirelessSend_Api(int sockid, unsigned char *pucdata, unsigned int iLen);
int wirelessRecv_Api(int sockid, unsigned char *pucdata, unsigned int iLen, unsigned int uiTimeOut);

//int wirelessSslSetTlsVer_lib(int ver);
int wirelessSetSslVer_Api(unsigned char ucVer) ;
void wirelessSslDefault_Api(void);
int wirelessSendSslFile_Api (unsigned char ucType, unsigned char *pucData, int iLen);
int wirelessSetSslMode_Api(unsigned char  ucMode);
int wirelessSslSocketCreate_Api(void);
int wirelessSslSocketClose_Api(int sockid);
//timeout: ms   return :0-success <0-failed
int wirelessSslConnect_Api(int sockid, unsigned char *pucDestIP, char *pucPort, int timeout);  //for Q161
//int wirelessSslConnect_lib(int sockid, char *pucDestIP, unsigned short pucDestPort); //for Q181
int wirelessSslSend_Api(int sockid, unsigned char *pucdata, unsigned int iLen);
int wirelessSslRecv_Api(int sockid, unsigned char *pucdata, unsigned int iLen, unsigned int uiTimeOut);
int wirelessSetDNS_Api(unsigned char *pucDNS1, unsigned char *pucDNS2 );


typedef struct {
	int valid;
	int socket;
	int ssl;
} NET_SOCKET_PRI;

// FIXME: What if the caller doesn't close the socket??
#define	MAX_SOCKETS	10
#define CERTI_LEN 1024 * 2

static NET_SOCKET_PRI sockets[MAX_SOCKETS];

void net_init(void)
{
	for (int i = 0; i < MAX_SOCKETS; i++) {
		memset(&sockets[i], 0, sizeof(NET_SOCKET_PRI));
	}
}

int getCertificate(char *cerName, unsigned char *cer)
{
	int Cerlen, Ret;
	u8 CerBuf[CERTI_LEN];

	char info[64] = "";

	Cerlen = GetFileSize_Api(cerName);
	if(Cerlen <= 0)
	{
		sprintf(info,"get certificate err or not exist: %s", cerName);
		LogPrintInfo(info);
		return -1;
	}

	memset(CerBuf, 0 , sizeof(CerBuf));
	memset(info, 0, sizeof(info));

	if(Cerlen > CERTI_LEN)
	{
		sprintf(info, "%s, is too large", cerName);
		LogPrintInfo(info);
		return -2;
	}

	Ret = ReadFile_Api(cerName, CerBuf, 0, (unsigned int *)&Cerlen);
	LogPrintWithRet(0, "=> [network.c]->ReadFile_Api(): ", Ret);

	memset(info, 0, sizeof(info));
	if(Ret != 0)
	{
		sprintf(info, "read: %s, failed", cerName);
		LogPrintInfo(info);
		return -3;
	}

	memcpy(cer, CerBuf, strlen(CerBuf));

	return 0;
}

void *net_connect(void* attch, const char *host,const char *port, int timerOutMs, int ssl, int *errCode)
{
	char info[strlen(host) + strlen(port) + 32];
	memset(info, 0, sizeof(info));
	sprintf(info, "host: %s, port: %s, ssl: %d", host, port, ssl);
	LogPrintInfo(info);

	int ret, sock, i;
	u8 CerBuf[CERTI_LEN];
	
	if(G_sys_param.commode == TRANCOMM_WIFI)
	{
		// Find an empty socket slot
		for (i = 0; i < MAX_SOCKETS; i++) {
			if (sockets[i].valid == 0) { break; }
		}

		if (i >= MAX_SOCKETS) {
			*errCode = -3;
			return NULL;
		}

		if(ssl == 0)
		{
			// >=0 success  , others -failed
			sock = wifiSocketCreate_Api(0); //0-TCP   1-UDP
			LogPrintWithRet(0, "=> [network.c]->wifiSocketCreate_Api(0): sock = ", sock);

			if (sock < 0) {
				*errCode = -4;
				return NULL;
			}

			ret = wifiTCPConnect_Api(sock, host, port, 60 * 1000);
			LogPrintWithRet(0, "=> [network.c]->wifiTCPConnect_Api(): ", ret);

			if (ret != 0) {
				*errCode = -5;

				ret =  wifiSocketClose_Api(sock);
				LogPrintWithRet(0, "=> [network.c]->wifiSocketClose_Api(): ", ret);

				return NULL;
			}
		}
		else
		{
			sock = wifiSSLSocketCreate_Api(); //>=0 -success
			MAINLOG_L1("wifiSSLSocketCreate_Api  sock:%d", sock);
			if(sock < 0)
				return -4;

			if(ssl == 1)
			{
				WifiespSetSSLConfig_Api(sock,0);
			}
			else
			{
				//noted: the second parameter value need to be right for WifiespSetSSLConfig_Api . 0-not verify
				//and WifiespDownloadCrt_Api should be used if certificates are necessary

				ret = WifiespSetSSLConfig_Api(sock,3);
				MAINLOG_L1("WifiespSetSSLConfig_Api   ret : %d", ret);
				
				memset(CerBuf, 0 , sizeof(CerBuf));
				getCertificate(FILE_CERT_ROOT, CerBuf);
				ret = WifiespDownloadCrt_Api(0, CerBuf, strlen(CerBuf));
				MAINLOG_L1("WifiespDownloadCrt_Api %s : ret-%d strlen-%d", FILE_CERT_ROOT, ret, strlen(CerBuf));

				memset(CerBuf, 0 , sizeof(CerBuf));
				getCertificate(FILE_CERT_CHAIN, CerBuf);
				ret = WifiespDownloadCrt_Api(1, CerBuf, strlen(CerBuf));
				MAINLOG_L1("WifiespDownloadCrt_Api %s : ret-%d strlen-%d", FILE_CERT_CHAIN, ret, strlen(CerBuf));

				memset(CerBuf, 0 , sizeof(CerBuf));
				getCertificate(FILE_CERT_PRIVATE, CerBuf);
				ret = WifiespDownloadCrt_Api(2, CerBuf, strlen(CerBuf));
				MAINLOG_L1("WifiespDownloadCrt_Api %s : ret-%d strlen-%d", FILE_CERT_PRIVATE, ret, strlen(CerBuf));
			}

			//for the server with one IP, multiple domain name and multiple certificates, SNI need to be set.
			//ret = WifiSetSocketSNI_Api(sock, host);
			//MAINLOG_L1("WifiSetSocketSNI_Api:%d  host:%s", ret, host);

			ret = wifiSSLConnect_Api(sock, host, port, 60*1000);
			MAINLOG_L1("wifiSSLConnect_Api:%d host:%s port:%s", ret, host, port);
			if(ret != 0)
			{
				*errCode = -5;
				ret =  wifiSSLSocketClose_Api(sock);
				MAINLOG_L1("wifiSSLSocketClose_Api:%d", ret);
				return NULL;
			}
		}

		sockets[i].valid  = 1;
		sockets[i].ssl    = ssl;
		sockets[i].socket = sock;

		*errCode = 0;
		return &sockets[i];

	}
	else
	{

		int timeid = TimerSet_Api();
		while (1)
		{
			ret = wirelessCheckPppDial_Api();

			if (ret == 0) { break; }
			else {
				LogPrintWithRet(1, "!!! wirelessCheckPppDial_Api() failed(%d) !!!", ret);

				ret = wirelessPppOpen_Api(NULL, NULL, NULL);
				LogPrintWithRet(0, "wirelessPppOpen_Api(NULL): ", ret);

				Delay_Api(1000);
			}

			if (TimerCheck_Api(timeid , timerOutMs) == 1) {
				*errCode = -1;
				return NULL;
			}
		}

		#ifdef __SETDNS__
			ret =  wirelessSetDNS_Api("8.8.8.8", "8.8.4.4" );
			LogPrintWithRet(0, "wirelessSetDNS_Api(): " , ret);
		#endif

		// Find an empty socket slot
		for (i = 0; i < MAX_SOCKETS; i++) {
			if (sockets[i].valid == 0) { break; }
		}

		if (i >= MAX_SOCKETS) {
			*errCode = -3;
			return NULL;
		}

		if (ssl == 0) {
			sock = wirelessSocketCreate_Api(0);
			LogPrintWithRet(0, "wirelessSocketCreate_Api(0): sock = ", sock);

			if (sock < 0) {
				*errCode = -4;
				return NULL;
			}
		}
		else {
			if (ssl == 2)
			{
				ret = wirelessSetSslMode_Api(1);
				LogPrintWithRet(0, "wirelessSetSslMode_Api(1): ", ret);

				// ca.pem
				memset(CerBuf, 0 , sizeof(CerBuf));
				ret = getCertificate(FILE_CERT_ROOT, CerBuf);
				LogPrintWithRet(0, "getCertificate(ca.pem): ", ret);

				ret = wirelessSendSslFile_Api(0, CerBuf, strlen((char *)CerBuf));
				LogPrintWithRet(0, "wirelessSendSslFile_Api(2): ", ret);

				// pri.key
				memset(CerBuf, 0 , sizeof(CerBuf));
				ret = getCertificate(FILE_CERT_PRIVATE, CerBuf);
				LogPrintWithRet(0, "getCertificate(pri.key): ", ret);

				ret = wirelessSendSslFile_Api(1, CerBuf, strlen((char *)CerBuf));
				LogPrintWithRet(0, "wirelessSendSslFile_Api(1): ", ret);

				// cli.crt
				memset(CerBuf, 0 , sizeof(CerBuf));
				ret = getCertificate(FILE_CERT_CHAIN, CerBuf);
				LogPrintWithRet(0, "getCertificate(cli.crt): ", ret);

				ret = wirelessSendSslFile_Api(2, CerBuf, strlen((char *)CerBuf));
				LogPrintWithRet(0, "wirelessSendSslFile_Api(0): ", ret);
			}
			else {
				ret = wirelessSetSslMode_Api(0);
				LogPrintWithRet(0, "wirelessSetSslMode_Api(0): ", ret);
			}

			ret = wirelessSetSslVer_Api(4);
			LogPrintWithRet(0, "wirelessSetSslVer_Api(4): ", ret);

			sock = wirelessSslSocketCreate_Api();
			LogPrintWithRet(0, "wirelessSslSocketCreate_Api(): sockId = ", sock);

			if (sock == -1) {
				*errCode = -4;
				return NULL;
			}
		}

		if (ssl == 0) {
			ret = wirelessTcpConnect_Api(sock, (char *)host, port, 10 * 1000);
		} else {
			ret = wirelessSslConnect_Api(sock, (char *)host, port, 10 * 1000);
		}

		LogPrintWithRet(0, "(TCP/SSL CONNECT)RET = ", ret);

		if (ret != 0)
		{
			if (ssl == 0) {
				ret = wirelessSocketClose_Api(sock);
			} else {
				ret = wirelessSslSocketClose_Api(sock);
			}

			LogPrintWithRet(0, "(TCP/SSL SOCKET CLOSE)RET = ", ret);

			*errCode = -5;
			return NULL;
		}

		sockets[i].valid  = 1;
		sockets[i].ssl    = ssl;
		sockets[i].socket = sock;

		*errCode = 0;

		return &sockets[i];
	}
}

int net_close(void *netContext)
{
	int ret;
	NET_SOCKET_PRI *sock = (NET_SOCKET_PRI *)netContext;

	if (sock == NULL) { return -1; }
	if (sock->valid == 0) { return 0; }

	if(G_sys_param.commode == TRANCOMM_WIFI)
	{
		if (sock->ssl == 0)
		{
			ret = wifiTCPClose_Api(sock->socket);
			LogPrintWithRet(0, "wifiTCPClose_Api(): ", ret);

			ret =  wifiSocketClose_Api(sock->socket);
			LogPrintWithRet(0, "wifiSocketClose_Api(): ", ret);
		}
		else
		{
			ret = wifiSSLSocketClose_Api(sock->socket);
			MAINLOG_L1("wifiSSLSocketClose_Api:%d", ret);
		}
	}
	else
	{
		if (sock->ssl == 0) {
			ret = wirelessSocketClose_Api(sock->socket);
			LogPrintWithRet(0, "wirelessSocketClose_Api(): ", ret);
		} else {
			ret = wirelessSslSocketClose_Api(sock->socket);
			LogPrintWithRet(0, "wirelessSslSocketClose_Api(): ", ret);
		}
	}

	sock->valid = 0;

	return 0;
}

int net_read(void *netContext, unsigned char* recvBuf, int needLen, int timeOutMs)
{
	int ret;
	NET_SOCKET_PRI *sock = (NET_SOCKET_PRI *)netContext;

	if (sock == NULL) { return -1; }
	if (sock->valid == 0) { return -1; }

	if(G_sys_param.commode == TRANCOMM_WIFI)
	{
		if (sock->ssl == 0)
			ret = wifiRecv_Api(sock->socket, recvBuf, 1, timeOutMs);
		else
			ret = wifiSSLRecv_Api(sock->socket, recvBuf, 1, timeOutMs);

		if (ret == 0) { return 0; }
		else if (ret < 0)
		{
			LogPrintWithRet(0, "wifiRecv_Api: ", ret);
			return -1;
		}
		
		if (needLen == 1)
			return 1;

		if (sock->ssl == 0)
			ret = wifiRecv_Api(sock->socket, recvBuf + 1, needLen - 1, 10);
		else
			ret = wifiSSLRecv_Api(sock->socket, recvBuf + 1, needLen - 1, 10);

		if(ret < 0)
		{
			MAINLOG_L1("wifixxRecv_Api error 2:%d  needlen:%d", ret, needLen - 1);
			return -1;
		}

		return ret + 1;
	}
	else
	{
		if (sock->ssl == 0)
			ret = wirelessRecv_Api(sock->socket, recvBuf, 1, timeOutMs);
		else
			ret = wirelessSslRecv_Api(sock->socket, recvBuf, 1, timeOutMs);

		if (ret == 0)
			return 0;

		if (ret != 1)
			return -1;

		if (needLen == 1)
			return 1;

		if (sock->ssl == 0)
			ret = wirelessRecv_Api(sock->socket, recvBuf + 1, needLen - 1, 10);
		else
			ret = wirelessSslRecv_Api(sock->socket, recvBuf + 1, needLen - 1, 10);

		if(ret < 0)
			return -1;

		return ret + 1;
	}
}

int net_write(void *netContext, unsigned char* sendBuf, int sendLen, int timeOutMs)
{
	int ret;
	NET_SOCKET_PRI *sock = (NET_SOCKET_PRI *)netContext;

	if (sock == NULL) { return -1; }
	if (sock->valid == 0) { return -1; }

	if(G_sys_param.commode == TRANCOMM_WIFI)
	{
		if (sock->ssl == 0)
		{
			ret = wifiSend_Api(sock->socket, sendBuf, sendLen, timeOutMs);
			LogPrintWithRet(0, "=> [network.c]->wifiSend_Api(): ", ret);

			if (ret == 0) {
				return sendLen;
			} else {
				return -1;
			}
		}
		else
		{
			ret = wifiSSLSend_Api(sock->socket, sendBuf, sendLen, 10000); //timeout 不能是0， 是0的话  返回-6311  WIFI_SENDDATA_FAIL_1
			MAINLOG_L1("wifiSSLSend_Api :%d  socket:%d timeout:%d" , ret, sock->socket, timeOutMs);
			if(ret == 0)
				return sendLen;
			else
				return -1;
		}
	}
	else
	{
		if (sock->ssl == 0)
			return wirelessSend_Api(sock->socket, sendBuf, sendLen);
		else
			return wirelessSslSend_Api(sock->socket, sendBuf, sendLen);
	}
}

/*
uint8 pssltmpp[93] = {
	0x10, 0x5B, 0x00, 0x04, 0x4D, 0x51, 0x54, 0x54, 0x04, 0x82, 0x02, 0x58, 0x00, 0x34, 0x4D, 0x73,
	0x77, 0x69, 0x70, 0x65, 0x2D, 0x53, 0x6F, 0x75, 0x6E, 0x64, 0x62, 0x6F, 0x78, 0x2D, 0x37, 0x35,
	0x37, 0x66, 0x39, 0x66, 0x61, 0x35, 0x2D, 0x38, 0x35, 0x63, 0x33, 0x2D, 0x34, 0x34, 0x38, 0x31,
	0x2D, 0x39, 0x38, 0x37, 0x33, 0x2D, 0x38, 0x30, 0x64, 0x66, 0x33, 0x30, 0x35, 0x39, 0x63, 0x34,
	0x32, 0x33, 0x00, 0x19, 0x3F, 0x53, 0x44, 0x4B, 0x3D, 0x50, 0x79, 0x74, 0x68, 0x6F, 0x6E, 0x26,
	0x56, 0x65, 0x72, 0x73, 0x69, 0x6F, 0x6E, 0x3D, 0x31, 0x2E, 0x34, 0x2E, 0x39
};

void DR_SSL_testi(void)
{
	//int32 iRet;
	int ret;
	//MAINLOG_L1("[%s] -%s- Line=%d:application thread enter, param 0x%x\r\n", filename(__FILE__), __FUNCTION__, __LINE__, param);
	int timeid = 0;

	timeid = TimerSet_Api();
	while(1)
	{
		ret = wirelessCheckPppDial_Api();
		MAINLOG_L1("wirelessCheckPppDial_Api = %d" , ret );
		if(ret == 0)
			break;
		else{
			ret = wirelessPppOpen_Api(NULL, NULL, NULL);
			Delay_Api(1000);
		}
		if(TimerCheck_Api(timeid , 60*1000) == 1)
		{
			return ;
		}
	}
	//ret =  wirelessSetDNS_Api("114.114.114.114", "8.8.8.8" );
	//MAINLOG_L1("wirelessSetDNS_Api = %d" , ret);

    ret = wirelessSetSslMode_Api((unsigned char)1);
    MAINLOG_L1("wirelessSetSslMode_Api :%d", ret);

	ret = wirelessSendSslFile_Api(2, TEST_CA_FILE, strlen((char *)TEST_CA_FILE));
	MAINLOG_L1("wirelessSendSslFile_Api 1:%d %d", ret, strlen((char *)TEST_CA_FILE));
	ret = wirelessSendSslFile_Api(1, TEST_CLIENT_KEY_FILE, strlen((char *)TEST_CLIENT_KEY_FILE));
	MAINLOG_L1("wirelessSendSslFile_Api 2:%d %d", ret, strlen((char *)TEST_CLIENT_KEY_FILE));
	ret = wirelessSendSslFile_Api(0, TEST_CLIENT_CRT_FILE, strlen((char *)TEST_CLIENT_CRT_FILE));
	MAINLOG_L1("wirelessSendSslFile_Api 3:%d %d", ret, strlen((char *)TEST_CLIENT_CRT_FILE));


	wirelessSslDefault_Api();
	wirelessSetSslVer_Api((unsigned char)0); //wirelessSslSetTlsVer_Api(0);

	Delay_Api(2000);

    int sock = wirelessSslSocketCreate_Api();
    if (sock == -1)
    {
    	MAINLOG_L1("[%s] -%s- Line=%d:<ERR> create ssl sock failed\r\n");
        return;//fibo_thread_delete();
    }

    MAINLOG_L1(":::fibossl fibo_ssl_sock_create %x\r\n", sock);

	ret = wirelessSslConnect_Api(sock,  "a14bkgef0ojrzw-ats.iot.us-west-2.amazonaws.com", "8883", 60*1000);

	MAINLOG_L1(":::fibossl wirelessSslConnect_Api %d\r\n", ret);

    ret = wirelessSslSend_Api(sock, pssltmpp, sizeof(pssltmpp));
	MAINLOG_L1(":::fibossl sys_sock_send %d\r\n", ret);

    ret = wirelessSslRecv_Api(sock, buf, 1, 1000);
	MAINLOG_L1(":::fibossl sys_sock_recv %d\r\n", ret);
	ret = wirelessSslRecv_Api(sock, buf, 32, 10000);
	MAINLOG_L1(":::fibossl sys_sock_recv %d\r\n", ret);

	ret = wirelessSslGetErrcode_Api();
	MAINLOG_L1(":::fibo_get_ssl_errcode, ret = %d\r\n", ret);

	uint32 port = 6500;

	MAINLOG_L1(":::wifiTCPConnect_Api, iRet = %d\r\n", iRet);
}
*/
