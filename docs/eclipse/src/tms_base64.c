#include "../inc/tms_base64.h"

const char BASE_CODE[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";  //base��

//���룬������Ҫ������ַ���ָ�룬������ŵ�λ�ã������ִ����ȵ�4/3����Ҫ������ַ������� 
//->���ؽ������  
int _tms_fnBase64Encode(char *lpString, char *lpBuffer, int sLen) 
//���������ģ������������ĳ���  
{   register int vLen = 0;  //�Ĵ����ֲ�����������  
    while(sLen > 0)      //���������ַ���  
    {  
		*lpBuffer++ = BASE_CODE[(lpString[0] >> 2 ) & 0x3F];   //������λ����00111111�Ƿ�ֹ������Լ�    
        if(sLen > 2) //��3���ַ�  
        {  
			*lpBuffer++ = BASE_CODE[((lpString[0] & 3) << 4) | ((lpString[1] >> 4) & 0x0f)];    //*lpBuffer++ = BASE_CODE[((lpString[0] & 3) << 4) | (lpString[1] >> 4)];   
			*lpBuffer++ = BASE_CODE[((lpString[1] & 0xF) << 2) | ((lpString[2] >> 6) & 0x03)]; //*lpBuffer++ = BASE_CODE[((lpString[1] & 0xF) << 2) | (lpString[2] >> 6)];  
            *lpBuffer++ = BASE_CODE[lpString[2] & 0x3F];                               
        }else  
        {   switch(sLen)    //׷�ӡ�=��  
            {   
				case 1:  
                    *lpBuffer ++ = BASE_CODE[(lpString[0] & 3) << 4 ];  
                    *lpBuffer ++ = '=';  
                    *lpBuffer ++ = '=';  
                    break;  
                case 2:  
                    *lpBuffer ++ = BASE_CODE[((lpString[0] & 3) << 4) | (lpString[1] >> 4)];  
                    *lpBuffer ++ = BASE_CODE[((lpString[1] & 0x0F) << 2) | (lpString[2] >> 6)];  
                    *lpBuffer ++ = '=';  
                    break;  
            }  
        }  
        lpString += 3;  
        sLen -= 3;  
        vLen +=4;  
    }  
    *lpBuffer = 0;  
    return vLen;  
}  

//�Ӻ��� - ȡ���ĵ�����  
char _tms_GetCharIndex(char c) //������������ʡȥ�������ù��̣�����  
{   if((c >= 'A') && (c <= 'Z'))  
    {   return c - 'A';  
    }else if((c >= 'a') && (c <= 'z'))  
    {   return c - 'a' + 26;  
    }else if((c >= '0') && (c <= '9'))  
    {   return c - '0' + 52;  
    }else if(c == '+')  
    {   return 62;  
    }else if(c == '/')  
    {   return 63;  
    }else if(c == '=')  
    {   return 0;  
    }  
    return 0;  
}  

//���룬��������������ģ����ĳ���  
int _tms_fnBase64Decode(char *lpString, char *lpSrc, int sLen)   //���뺯��  
{   static char lpCode[4];  
    register int vLen = 0; 
	int su_num = 0;

    if(sLen % 4)        //Base64���볤�ȱض���4�ı���������'='  
    {   lpString[0] = '\0';  
        return -1;  
    }  

	if(lpSrc[sLen-1] == '=')
	{
		su_num++;
		if(lpSrc[sLen-2] == '=')
			su_num++;
	}

    while(sLen > 2)      //���������ַ�������  
    {   lpCode[0] = _tms_GetCharIndex(lpSrc[0]);  
        lpCode[1] = _tms_GetCharIndex(lpSrc[1]);  
        lpCode[2] = _tms_GetCharIndex(lpSrc[2]);  
        lpCode[3] = _tms_GetCharIndex(lpSrc[3]);  

        *lpString++ = (lpCode[0] << 2) | (lpCode[1] >> 4);  
        *lpString++ = (lpCode[1] << 4) | (lpCode[2] >> 2);  
        *lpString++ = (lpCode[2] << 6) | (lpCode[3]);  

        lpSrc += 4;  
        sLen -= 4;  
        vLen += 3;  
    } 
	vLen -= su_num;
    return vLen;  
}  
/*
void main() //�����������Ժ���  
{   char s[] = "a", str[32];  
    int l = strlen(s);  
    int i = fnBase64Encode(s, str, l);  
    printf("fnBase64Encode(\"%s\", str, %d) = \"%s\";\n", s, l, str);  
    l = _tms_fnBase64Decode(s, str, i);  
    printf("_tms_fnBase64Decode(\"%s\", s, %d) = \"%s\";\n", str, i, s);  
}  
*/
