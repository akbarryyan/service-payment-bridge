#include "def.h"
#include "cJSON.h"

#include <coredef.h>
#include <stdio.h>
#include <string.h>

char* units[]    = {"", "One", "Two", "Three", "Four", "Five", "Six", "Seven", "Eight", "Nine"};
char* teen[]     = {"Ten", "Eleven", "Twelve", "Thirteen", "Fourteen", "Fifteen", "Sixteen", "Seventeen", "Eighteen", "Nineteen"};
char* tens[]     = {"", "", "Twenty", "Thirty", "Forty", "Fifty", "Sixty", "Seventy", "Eighty", "Ninety"};
char* suffixes[] = {"", "Thousand", "Million", "Billion"};

char TMP[48] = "";
char RESULT[20][128];

int CONTENT_IDX = 0;;

char WORDS_RESULTS[1024]    = "";
char SEQUENCE_RESULTS[1024] = "";

int buildJson(char *amt, char *words, char *sequence, char *data)
{
	//LogPrintNoRet("=> [AmtToMp3.c]->buildJson()");

	cJSON *root = NULL;
	cJSON *obj  = NULL;

	/// =====Root
	root = cJSON_CreateObject();
	if (root == NULL) {
		LogPrintNoRet("!!! cJSON_CreateObject(root) Error == NULL !!!");
		return 0;
	}
	/// =====Root

	/// =====Type
	obj = cJSON_AddStringToObject(root, "type", "TB");
	if (obj == NULL) {
		LogPrintNoRet("!!! cJSON_AddStringToObject(type) Error == NULL!!!");
		cJSON_Delete(root);
		return 0;
	}
	/// =====Type

	obj = NULL;

	/// =====Input
	obj = cJSON_AddStringToObject(root, "input", amt);
	if (obj == NULL) {
		LogPrintNoRet("!!! cJSON_AddStringToObject(input) Error == NULL!!!");
		cJSON_Delete(root);
		return 0;
	}
	/// =====Input

	obj = NULL;

	/// =====Words
	obj = cJSON_AddStringToObject(root, "output_english_words", words);
	if (obj == NULL) {
		LogPrintNoRet("!!! cJSON_AddStringToObject(words) Error == NULL!!!");
		cJSON_Delete(root);
		return 0;
	}
	/// =====Words

	obj = NULL;

	/// =====Sequence
	obj = cJSON_AddStringToObject(root, "output_english_sequence", sequence);
	if (obj == NULL) {
		LogPrintNoRet("!!! cJSON_AddStringToObject(sequence) Error == NULL!!!");
		cJSON_Delete(root);
		return 0;
	}
	/// =====Sequence

	obj = NULL;

	/// =====Language
	obj = cJSON_AddStringToObject(root, "language", "english");
	if (obj == NULL) {
		LogPrintNoRet("!!! cJSON_AddStringToObject(language) Error == NULL!!!");
		cJSON_Delete(root);
		return 0;
	}
	/// =====Language

	char *result = NULL;
	result = cJSON_PrintUnformatted(root);

	if (result == NULL) {
		LogPrintNoRet("!!! cJSON_PrintUnformatted(root) Error == NULL !!!");
		cJSON_Delete(root);
		return 0;
	}

	sprintf(data, "%s", result);
	//LogPrintInfo(data);

	memset(WORDS_RESULTS,    0, sizeof(WORDS_RESULTS));
	memset(SEQUENCE_RESULTS, 0, sizeof(SEQUENCE_RESULTS));

	return 1;
}

void convertThreeDigits(int num)
{
	//LogPrintNoRet("=> [AmtToMp3.c]->convertThreeDigits()");

    int hundred = num / 100;
    int ten     = (num / 10) % 10;
    int one     = num % 10;

	/*
	LogPrintNoRet("============");
	LogPrintWithRet(0, "hundred = ", hundred);
	LogPrintWithRet(0, "ten = ",     ten);
	LogPrintWithRet(0, "one = ",     one);
	LogPrintNoRet("============");
	*/

	char tmp[16]  = "";
	//char info[96] = "";

    if (hundred > 0) {
    	//sprintf(info, "%s Hundred ", units[hundred]);
    	//LogPrintInfo(info);

		sprintf(tmp, "%s Hundred ", units[hundred]);
		strcat(TMP, tmp);
    }

	memset(tmp,  0, sizeof(tmp));
	//memset(info, 0, sizeof(info));

    if (ten > 1)
	{
		if (hundred > 0) {
			strcat(TMP, "And ");
		}

    	//sprintf(info, "%s ", tens[ten]);
    	//LogPrintInfo(info);

		sprintf(tmp, "%s ", tens[ten]);
		strcat(TMP, tmp);

		memset(tmp, 0, sizeof(tmp));
		//memset(info, 0, sizeof(info));

        //sprintf(info, "%s ", units[one]);
    	//LogPrintInfo(info);

		sprintf(tmp, "%s ", units[one]);
		strcat(TMP, tmp);
    }
	else if (ten == 1)
	{
		if (hundred > 0) {
			strcat(TMP, "And ");
		}

		memset(tmp, 0, sizeof(tmp));
		//memset(info, 0, sizeof(info));

        //sprintf(info, "%s ", teen[one]);
    	//LogPrintInfo(info);

		sprintf(tmp, "%s ", teen[one]);
		strcat(TMP, tmp);
    }
	else if (one > 0)
	{
		if (hundred > 0) {
			strcat(TMP, "And ");
		}

		memset(tmp, 0, sizeof(tmp));
		//memset(info, 0, sizeof(info));

        //sprintf(info, "%s ", units[one]);
    	//LogPrintInfo(info);

		sprintf(tmp, "%s ", units[one]);
		strcat(TMP, tmp);
    }
}

void doAmt(int amt)
{
	//LogPrintNoRet("=> [AmtToMp3.c]->doAmt()");

    int i = 0;

    while (amt > 0) {
    	//char info[96] = "";

        int threeDigits = amt % 1000;

		memset(TMP, 0, sizeof(TMP));

        if (threeDigits != 0) {
            convertThreeDigits(threeDigits);

			if (strcmp(suffixes[i], "") == 0) {
				//sprintf(info, "%s ", suffixes[i]);
				//LogPrintInfo(info);

				strcat(TMP, " ");
			} else {
				//sprintf(info, "%s And ", suffixes[i]);
				//LogPrintInfo(info);

				char tmp[16] = "";
				sprintf(tmp, "%s And ", suffixes[i]);
				strcat(TMP, tmp);
			}
        }

		//LogPrintNoRet("====================");
		//LogPrintInfo(TMP);
		//LogPrintNoRet("====================");

		sprintf(RESULT[i], "%s", TMP);

        amt /= 1000;
        i++;
    }
	//LogPrintWithRet(0, "i = ", i);

    CONTENT_IDX = i;
}

int AmtToMp3(char *amt, char *data, char *words, const char *currency_s, const char *currency)
{
	//LogPrintNoRet("=> [AmtToMp3.c]->AmtToMp3()");

	int ret;

	int amt_ = 0;
	int amt_decimal = 0;

	int amt_len = strlen(amt);
	//LogPrintWithRet(0, "amt_len = ", amt_len);

	memset(RESULT, 0, 20 * 128);

	char *pos = NULL;
	pos = strstr(amt, ".");

	if (pos == NULL) {
		LogPrintNoRet("!!! ERROR, INVALID AMT !!!");
		return 0;
	}

	int len = amt_len - strlen(pos);
	//LogPrintWithRet(0, "len = ", len);

	char amt_data[12] = "";
	strncpy(amt_data, (const char *)amt, len);

	amt_ = atoi(amt_data);
	LogPrintWithRet(0, "amt_ = ", amt_);

	if (strcmp(pos, ".00") != 0)
	{
		char *posi	    = NULL;
		char *next_posi = NULL;

		posi = strtok(pos, ".");

		while (posi != NULL) {
			char tmp[2] = "";
			sprintf(tmp, "%s", posi);
			amt_decimal = atoi(tmp);

			posi = strtok(NULL, ".");
		}

		LogPrintWithRet(0, "amt_decimal = ", amt_decimal);
	}

	char results[512] = "";

	// Rupees Part
	if (amt_ == 0) {
		LogPrintNoRet("!!! ERROR, PARSE AMT FAILED !!!");
		return 0;
	}

	doAmt(amt_);

	for (int i = CONTENT_IDX; i >= 0; i--) {
		strcat(results, RESULT[i]);
	}

	char results_copy[512] = "";
	strncpy(results_copy, results, strlen(results) - 1);

	char currencys_tag[8] = "";
	sprintf(currencys_tag, "%ss", currency_s);

	//strcat(results_copy, "Rupees");
	strcat(results_copy, currencys_tag);

	// ==========
	char *pos1 = NULL;
	pos1 = strstr(results_copy, "  ");
	if (pos1 != NULL) {
		replaceStr(results_copy, "  ", " ");
	}
	// ==========

	/*
	char info[strlen(results_copy) + 16];
	memset(info, 0, sizeof(info));
	sprintf(info, "RESULTS: %s", results_copy);
	LogPrintInfo(info);
	*/

	strcat(WORDS_RESULTS, results_copy);

	int step = 0;
	char tmp[16] = "";

	char MP3_FILE_PART[32] = "";

	for (int i = 0; i < strlen(results_copy); ++i) {
		if (results_copy[i] == ' ') {
			strcat(MP3_FILE_PART, "english.");
			strcat(MP3_FILE_PART, tmp);
			strcat(MP3_FILE_PART, ".mp3");

			//LogPrintInfo(MP3_FILE_PART);

			strcat(SEQUENCE_RESULTS, MP3_FILE_PART);
			strcat(SEQUENCE_RESULTS, ",");

			memset(MP3_FILE_PART, 0, sizeof(MP3_FILE_PART));

			memset(tmp, 0, sizeof(tmp));
			step = 0;

		} else {
			tmp[step] = results_copy[i];
			step++;
		}
	}

	char currency_mp3[24] = "";
	sprintf(currency_mp3, "english.%s.mp3", currencys_tag);

	//strcat(SEQUENCE_RESULTS, "english.Rupees.mp3");
	strcat(SEQUENCE_RESULTS, currency_mp3);

	// Paisa Part
	if (amt_decimal != 0)
	{
		memset(RESULT, 0, 20 * 128);
		CONTENT_IDX = 0;

		doAmt(amt_decimal);

		memset(results, 0, sizeof(results));

		for (int i = CONTENT_IDX; i >= 0; i--) {
			strcat(results, RESULT[i]);
		}

		char results_copy[512] = "";
		strncpy(results_copy, results, strlen(results) - 1);

		//strcat(results_copy, "Paisa");
		strcat(results_copy, currency);
		strcat(results_copy, "s");

		// ========== FIX: When Paisa % 10 = 0, remove one more ' '
		char *pos = NULL;
		pos = strstr(results_copy, "  ");
		if (pos != NULL) {
			replaceStr(results_copy, "  ", " ");
		}
		// ==========

		/*
		char info[strlen(results_copy) + 16];
		memset(info, 0, sizeof(info));
		sprintf(info, "RESULTS: %s", results_copy);
		LogPrintInfo(info);
		*/

		strcat(WORDS_RESULTS, " And ");
		strcat(WORDS_RESULTS, results_copy);

		strcat(SEQUENCE_RESULTS, ",english.And.mp3,"); // Combine 'Rupees' part & 'Paisa' part

		int step = 0;
		char tmp[16] = "";

		char MP3_FILE_PART[32] = "";

		for (int i = 0; i < strlen(results_copy); ++i) {
			if (results_copy[i] == ' ') {
				strcat(MP3_FILE_PART, "english.");
				strcat(MP3_FILE_PART, tmp);
				strcat(MP3_FILE_PART, ".mp3");

				//LogPrintInfo(MP3_FILE_PART);

				strcat(SEQUENCE_RESULTS, MP3_FILE_PART);
				strcat(SEQUENCE_RESULTS, ",");

				memset(MP3_FILE_PART, 0, sizeof(MP3_FILE_PART));

				memset(tmp, 0, sizeof(tmp));
				step = 0;

			} else {
				tmp[step] = results_copy[i];
				step++;
			}
		}

		//strcat(SEQUENCE_RESULTS, "english.Paisa.mp3");
		strcat(SEQUENCE_RESULTS, "english.");
		strcat(SEQUENCE_RESULTS, currency);
		strcat(SEQUENCE_RESULTS, "s");
		strcat(SEQUENCE_RESULTS, ".mp3");
	}
	else
	{
		LogPrintNoRet("!!! AMT NO PAISA PART !!!");
	}

	strcat(WORDS_RESULTS, " Received");
	strcat(SEQUENCE_RESULTS, ",english.Received.mp3");

	/*
	char words_info[strlen(WORDS_RESULTS) + 16];
	memset(words_info, 0, sizeof(words_info));
	sprintf(words_info, "WORDS RESULTS: %s", WORDS_RESULTS);
	LogPrintInfo(words_info);
	*/

	memcpy(words, WORDS_RESULTS, strlen(WORDS_RESULTS));

	/*
	char seq_info[strlen(SEQUENCE_RESULTS) + 16];
	memset(seq_info, 0, sizeof(seq_info));
	sprintf(seq_info, "SEQUENCE RESULTS: %s", SEQUENCE_RESULTS);
	LogPrintInfo(seq_info);
	*/

	ret = buildJson(amt, WORDS_RESULTS, SEQUENCE_RESULTS, data);
	//LogPrintWithRet(0, "=> [AmtToMp3.c]->buildJson(): ", ret);

	return ret;
}

void replaceStr(char *data, const char *oldStr, const char *newStr)
{
	char *pos, tmp[24];
	int index = 0;

	int oldStrLen = strlen(oldStr);

	while ((pos = strstr(data, oldStr)) != NULL) {
		strcpy(tmp, data);
		index = pos - data;

		data[index] = '\0';

		strcat(data, newStr);
		strcat(data, tmp + index + oldStrLen);
	}
}
