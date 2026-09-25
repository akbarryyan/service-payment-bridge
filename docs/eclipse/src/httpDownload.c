#include "def.h"
#include "httpDownload.h"

#include <coredef.h>
#include <struct.h>
#include <poslib.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define	PROTOCOL_HTTP  0
#define	PROTOCOL_HTTPS 1

typedef struct {
	int protocol;
	char domain[MAX_DOMAIN_LENGTH];
	char path[MAX_PATH_LENGTH];
	char port[8];
} URL_ENTRY;

static int parseURL(char *url, URL_ENTRY *entry)
{
	LogPrintNoRet("=> [httpDownload.c]->parseURL()");

	char *p, *p2;

	memset(entry, 0, sizeof(URL_ENTRY));

	// Protocol analysis
	p = strstr(url, "://");
	if (p == NULL) {
		entry->protocol = PROTOCOL_HTTP;
		p = url;
	}
	else {
		if (((p - url) == 5) && (memcmp(url, "https", 5) == 0))
			entry->protocol = PROTOCOL_HTTPS;
		else if (((p - url) == 4) && (memcmp(url, "http", 4) == 0))
			entry->protocol = PROTOCOL_HTTP;
		else
			return UNSUPPORTED_URL;

		p += 3;
	}

	// Domain analysis (now p points to the beginning of domain name)
	p2 = strchr(p, '/');
	if (p2 == NULL) {
		// Format as http://domain
		if (strlen(p) >= MAX_DOMAIN_LENGTH)
			return URL_DOMAIN_TOO_LONG;

		strcpy(entry->domain, p);
	}
	else {
		if ((p2 - p) >= MAX_DOMAIN_LENGTH)
			return URL_DOMAIN_TOO_LONG;

		memcpy(entry->domain, p, p2 - p);
		entry->domain[p2 - p] = 0;
	}

	// Path analysis (now p2 is either NULL or pointing to the begining of path)
	if (p2 == NULL)
		strcpy(entry->path, "/");
	else {
		if (strlen(p2) >= MAX_PATH_LENGTH)
			return URL_PATH_TOO_LONG;

		strcpy(entry->path, p2);
	}

	// Port analysis (may be part of domain)
	p2 = strchr(entry->domain, ':');
	if (p2 == NULL) {
		if (entry->protocol == PROTOCOL_HTTP)
			strcpy(entry->port, "80");
		else if (entry->protocol == PROTOCOL_HTTPS)
			strcpy(entry->port, "443");
	}
	else {
		p2[0] = 0;	// cut the ":port" from domain

		strcpy(entry->port, p2 + 1);
	}

	return 0;
}

static void *s;
static int dev_connect(URL_ENTRY *entry, int timeout)
{
	int err;

	if (entry->protocol == PROTOCOL_HTTP)
		s = net_connect(NULL, entry->domain, entry->port, timeout * 1000, 0, &err);
	else
		s = net_connect(NULL, entry->domain, entry->port, timeout * 1000, 1, &err);

	if (s == NULL)
		return CONNECT_ERROR;

	return 0;
}

static int dev_send(unsigned char *buf, int length)
{
	int ret;

	ret = net_write(s, buf, length, 0);
	if (ret < 0)
		return SEND_ERROR;

	return 0;
}

// Return bytes of received, or error
static int dev_recv(unsigned char *buf, int max, int timeout)
{
	int ret;

	ret = net_read(s, buf, max, timeout * 1000);
	if (ret < 0)
		return RECEIVE_ERROR;

	return ret;
}

static int dev_disconnect(void)
{
	return net_close(s);
}

// For appending, offset is at the end of current file size.
// Make sure this works for your device
static int dev_savefile(char *filename, unsigned char *buf, int start, int len)
{
	LogPrintNoRet("=> [httoDownload.c]->Savefile()");

	int ret = WriteFile_Api(filename, buf, start, len);
	LogPrintWithRet(0, "=> [httpDownload.c]->WriteFile_Api(): ", ret);

	return 0;
}

// Remove the space in the begining and end of the str
static char *trim(char *str)
{
	int length, start, end;

	length = strlen(str);

	for (start = 0; start < length; start++) {
		if (str[start] != ' ')
			break;
	}

	for (end = length - 1; end > start; end--) {
		if (str[end] != ' ')
			break;
	}

	str[end + 1] = 0;

	return str + start;
}

// Partial download
// Return content length downloaded or error
static int __httpDownload(char *url, int method, char *filename, int start, int end)
{
	LogPrintNoRet("=> [httpDownload.c]->__httpDownload()");

	URL_ENTRY entry;
	int ret;
	int length, total, offset;
	int chunked_total = 0;
	int payload_flag;
	unsigned char buf[RECEIVE_BUF_SIZE];
	char *p, *p1, *p2;
	int reblen = RECEIVE_BUF_SIZE;

	ret = parseURL(url, &entry);

relocate_301:
	if (ret != 0)
		return ret;

	ret = dev_connect(&entry, CONNECT_TIMEOUT);
	if (ret != 0)
		return ret;
		
	memset(buf, 0, reblen);

	// Prepare the HTTP request
	if (method == METHOD_GET)
		strcpy((char *)buf, "GET ");
	else
		strcpy((char *)buf, "POST ");

	if (end < 0)
		sprintf((char *)buf, "%s %s HTTP/1.1\r\nHost:%s\r\nRange:bytes=%d-\r\n\r\n",
				(method == METHOD_GET) ? "GET" : "POST",
						entry.path,
						entry.domain,
						start);
	else
		sprintf((char *)buf, "%s %s HTTP/1.1\r\nHost:%s\r\nRange:bytes=%d-%d\r\n\r\n",
				(method == METHOD_GET) ? "GET" : "POST",
						entry.path,
						entry.domain,
						start, end);

	ret = dev_send(buf, strlen((char *)buf));
	if (ret != 0) {
		dev_disconnect();

		return ret;
	}

	memset(buf, 0, reblen);
	// Check HTTP status ("HTTP/1.1 200 OK\r\n")
	length = 0;
	while (1) {
		ret = dev_recv(buf + length, RECEIVE_BUF_SIZE - 1 - length, RECEIVE_TIMEOUT);

		if (ret <= 0) {
			dev_disconnect();
			return ret;
		}

		length += ret;
		buf[length] = 0;

		#ifdef DEBUG_MSG
			LogPrintNoRet("===============================");
			LogPrintInfo(buf);
			LogPrintNoRet("===============================");
		#endif

		// Check "\r\n" after HTTP status line
		p = strstr((char *)buf, "\r\n");
		if (p == NULL)
			// Not received yet, continue receiving
			continue;

		//p is end of status line(before \r\n)
		p[0] = 0;
		p += 2;
		length -= (p - (char *)buf);
		// Now p points to the start of header, length is the length
		// of header already received.

		// An ' ' should exist before response code "200"
		p1 = strchr((char *)buf, ' ');
		if (p1 == NULL) {
			dev_disconnect();

			return RECEIVE_ERROR;
		}

		p1[4] = 0;
		ret = atoi(p1 + 1);
		if ((ret != 200) && (ret != 301) && (ret != 206)) {
			dev_disconnect();

			return -ret;
		}

		break;
	}

	// Check HTTP headers
	// Find headers "Content-Length: xxx" or "Transfer-Encoding: chunked" for 200
	// payload_flag:
	// 	1	- total size known, receive all along
	// 	2	- payload is chunked, receive each chunk one by one
	// 	0	- no payload length info, error
	// Find header "Location: xxx" for 301
	payload_flag = 0; ////p is end of status line(after \r\n)
	while (1) {
		p1 = strstr(p, "\r\n");  //p: after HTTP status line(after \r\n)
		if (p1 == NULL) {
			// Header not complete, move to the
			// begining of buf, continue receiving.
			int i;

			for (i = 0; i < length; i++)
				buf[i] = p[i];

			p = (char *)buf;

			ret = dev_recv(buf + length, RECEIVE_BUF_SIZE - 1 - length, RECEIVE_TIMEOUT);
			if (ret <= 0) {
				dev_disconnect();

				return RECEIVE_ERROR;
			}

			length += ret;
			buf[length] = 0;

			#ifdef DEBUG_MSG
				LogPrintNoRet("===============================");
				LogPrintInfo(buf);
				LogPrintNoRet("===============================");
			#endif

			continue;
		}

		if (p1 == p) {
			// No more header, just break;
			p += 2;
			length -= 2;

			break;
		}

		p1[0] = 0;

		p2 = p;		// p2 points to current header  //p: after HTTP status line(after \r\n) //p1: next \r\n
		p = p1 + 2;	// p points to the next header
		length -= (p - p2);

		p1 = strchr(p2, ':');
		if (p1 == NULL) {
			dev_disconnect();
			return RECEIVE_ERROR;
		}

		p1[0] = 0;
		p2 = trim(p2);
		if (strcasecmp(p2, "Location") == 0) {
			ret = parseURL(trim(p1 + 1), &entry);
			goto relocate_301;
		}
		else if (strcasecmp(p2, "Content-Length") == 0) {
			payload_flag = 1;
			total = atoi(trim(p1 + 1));
		}
		else if (strcasecmp(p2, "Transfer-Encoding") == 0) {
			if (strcmp(trim(p1 + 1), "chunked") == 0)
				payload_flag = 2;
		}
	}

	//p is after hearder now
	if (payload_flag == 1) {
		// Save the already-received payload first
		if (length > 0)
			dev_savefile(filename, (unsigned char *)p, start, length);

		offset = length;

		while (offset < total) {
			length = total - offset;

			if (length > RECEIVE_BUF_SIZE)
				length = RECEIVE_BUF_SIZE;

			length = dev_recv(buf, length, RECEIVE_TIMEOUT);
			if (length <= 0) {
				dev_disconnect();

				return RECEIVE_ERROR;
			}

			#ifdef DEBUG_MSG
				LogPrintNoRet("===============================");
				LogPrintInfo(buf);
				LogPrintNoRet("===============================");
			#endif

			dev_savefile(filename, buf, start + offset, length);

			offset += length;
		}
	}
	else if (payload_flag == 2) {	// Chunked
		offset = 0;
		chunked_total = 0;
		while (1) {
			p1 = strstr(p, "\r\n");
			if (p1 == NULL) {
				// chunk size not complete, move to the
				// begining of the buffer, and continue
				// receiving.
				int i;

				for (i = 0; i < length; i++)
					buf[i] = p[i];

				p = (char *)buf;

				ret = dev_recv(buf + length, RECEIVE_BUF_SIZE - 1 - length, RECEIVE_TIMEOUT);
				if (ret <= 0) {
					dev_disconnect();

					return RECEIVE_ERROR;
				}

				length += ret;
				buf[length] = 0;

				#ifdef DEBUG_MSG
					LogPrintNoRet("===============================");
					LogPrintInfo(buf);
					LogPrintNoRet("===============================");
				#endif

				continue;
			}

			p1[0] = 0;
			p1 += 2;
			length -= (p1 - p);

			sscanf(p, "%x", &total);
			if (total == 0)
				break;

			chunked_total += total;

			while (length < total) {
				dev_savefile(filename, (unsigned char *)p1, start + offset, length);

				offset += length;
				total -= length;

				length = total;
				if (length > RECEIVE_BUF_SIZE - 1)
					length = RECEIVE_BUF_SIZE - 1;

				length = dev_recv(buf, length, RECEIVE_TIMEOUT);
				if (length <= 0) {
					dev_disconnect();

					return RECEIVE_ERROR;
				}

				buf[length] = 0;
				p1 = (char *)buf;

				#ifdef DEBUG_MSG
					LogPrintNoRet("===============================");
					LogPrintInfo(buf);
					LogPrintNoRet("===============================\n");
				#endif
			}

			dev_savefile(filename, (unsigned char *)p1, start + offset, total);

			offset += total;
			length -= total;
			p = p1 + total;

			// After chunk messaged, there should be "\r\n"
			if (length < 2) {
				// \r\n not complete, move to the beginning
				// of buffer, continue receiving.
				int i;

				for (i = 0; i < length; i++)
					buf[i] = p[i];

				p = (char *)buf;

				ret = dev_recv(buf + length, RECEIVE_BUF_SIZE - 1 - length, RECEIVE_TIMEOUT);
				if (ret <= 0) {
					dev_disconnect();
					return RECEIVE_ERROR;
				}

				length += ret;
				buf[length] = 0;

				#ifdef DEBUG_MSG
					LogPrintNoRet("===============================");
					LogPrintInfo(buf);
					LogPrintNoRet("===============================\n");
				#endif
			}

			if ((p[0] != '\r') || (p[1] != '\n')) {
				dev_disconnect();
				return RECEIVE_ERROR;
			}

			p += 2;
			length -= 2;
		}
	}
	else {
		dev_disconnect();
		return RECEIVE_ERROR;
	}

	dev_disconnect();

	if (payload_flag == 1)
		return total;
	else
		return chunked_total;

	return 0;
}

int httpDownload(char *url, int method, char *filename)
{
	LogPrintNoRet("=> [httpDownload.c]->httpDownload()");

	int ret;
	int total;

#if 0
	// Receive all payload within one request, small files can be used in this way
	return __httpDownload(url, method, filename, 0, -1);
#else
	// Receive payload by range, will ensure large file download stability
	total = 0;
	while (1) {
		ret = __httpDownload(url, method, filename, total, total + RECEIVE_RANGE - 1);
		if (ret == -416)
			// Requested range beyond file size, just finish
			break;

		if (ret < 0)
			return ret;

		total += ret;

		// If received payload is less than requested Range, this is the last range.
		// If received payload is more than requested Range, the server does not support Range
		if (ret != RECEIVE_RANGE)
			break;
	}

	return total;
#endif
}
