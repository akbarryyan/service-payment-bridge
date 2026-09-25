#ifndef __HTTPDOWNLOAD_H__
#define	__HTTPDOWNLOAD_H__

// The following macros can be configured to adapt field requirements
#define	MAX_DOMAIN_LENGTH	128
#define	MAX_PATH_LENGTH		128
#define	CONNECT_TIMEOUT		10
#define	RECEIVE_TIMEOUT		30
#define	RECEIVE_BUF_SIZE	2048
#define	RECEIVE_RANGE		2048	// Content range requested by every HTTP

//#define	DEBUG_MSG

// Erorr codes
// HTTP error will be reflected by error codes of -1xx/-2xx/-3xx/-4xx/-5xx
#define	UNSUPPORTED_URL		-1
#define	URL_DOMAIN_TOO_LONG	-2
#define	URL_PATH_TOO_LONG	-3
#define	CONNECT_ERROR		-4
#define	SEND_ERROR			-5
#define	RECEIVE_ERROR		-6

// HTTP methods
#define	METHOD_GET	0
#define	METHOD_POST	1

/*
 * Can only support the following url format:
 * 	http://domain[:port]/path[?request]
 * 	https://domain[:port]/path[?request]
 * 	domain[:port]/path[?request] (will default to http)
 */
int httpDownload(char *url, int method, char *file);

#endif
