#ifndef _tms__MD5__
#define _tms__MD5__

/* POINTER defines a generic pointer type */
typedef unsigned char * POINTER; 
  
/* UINT2 defines a two byte word */
//typedef unsigned short int UINT2; 
  
/* UINT4 defines a four byte word */
typedef unsigned long int UINT4; 
  
  
/* MD5 context. */
typedef struct { 
 UINT4 state[4];         /* state (ABCD) */
 UINT4 count[2];  /* number of bits, modulo 2^64 (lsb first) */
 unsigned char buffer[64];       /* input buffer */
} _tms_MD5_CTX; 
  
void _tms_MD5Init (_tms_MD5_CTX *context); 
void _tms_MD5InitUpdate (_tms_MD5_CTX *context, unsigned char *input, unsigned int inputLen); 
void _tms_MD5InitUpdaterString(_tms_MD5_CTX *context,const char *string); 
int _tms_MD5InitFileUpdateFile (_tms_MD5_CTX *context,char *filename); 
void _tms_MD5InitFinal (unsigned char digest[16], _tms_MD5_CTX *context); 
void _tms_MDString (char *string,unsigned char digest[16]); 
int _tms_MD5InitFile (char *filename,unsigned char digest[16]); 

int _tms_MD5File (char *filename,unsigned char digest[16]);

void _tms_MD5Final(unsigned char digest[16], _tms_MD5_CTX *context);
void _tms_MD5Update(_tms_MD5_CTX *context, unsigned char *input, unsigned int inputLen);
#endif
