
#include "bg_ini.h"

#include <errno.h>
#include <regex.h>
#include <sys/stat.h>

#include "bg_bashAPI.h"

extern char* parserFindEndOfValue(char* pS);

int iniParamGet(WORD_LIST* args)
{
	IniScheme iniScheme = {0};
	while (args && *args->word->word=='-') {
		char* optArg;
		if ((optArg=BGCheckOpt("-R*|--retVar=*", &args)))
			iniScheme.retVar = optArg;
		else if ((optArg=BGCheckOpt("-d|--delim", &args)))
			iniScheme.delim = optArg;
		else if ((optArg=BGCheckOpt("-a|--addIfNotFound", &args)))
			iniScheme.addIfNotFound = 1;
		else if ((optArg=BGCheckOpt("-t|--expandAsTemplate", &args)))
			iniScheme.expandAsTemplate = 1;
		else if ((optArg=BGCheckOpt("-x|--noSectionPad", &args)))
			iniScheme.noSectionPad = 1;
		else if ((optArg=BGCheckOpt("-v|--schema", &args)))
			iniScheme.schema = optArg;
		args = args->next;
	}

	char* iniFilename   = (args) ? args->word->word : NULL; args = (args) ? args->next : NULL;
	char* targetSection = (args) ? args->word->word : NULL; args = (args) ? args->next : NULL;
	char* targetName    = (args) ? args->word->word : NULL; args = (args) ? args->next : NULL;
	char* defValue      = (args) ? args->word->word : NULL; args = (args) ? args->next : NULL;

	char* value = iniParamGetC(&iniScheme, iniFilename, targetSection, targetName, defValue);

	return value!=NULL;
}

char* iniParamGetC(IniScheme* pScheme, char* iniFilename, char* targetSection, char* targetName, char* defValue)
{
	IniScheme defScheme = {0};
	if (!pScheme)
		pScheme = &defScheme;
	FILE* iniFileFD = fopen(iniFilename, "r");
	if (!iniFileFD)
		return NULL;

	char* targetValue = NULL;
	char* curSect = savestring("");
	int inSect = (!targetSection || *targetSection=='\0' || (*(targetSection+1)=='\0' && (*targetSection=='.') ) );
	size_t bufSize = 500;
	char* buf =  xmalloc(bufSize);
	while (freadline(iniFileFD, buf, &bufSize) > -1) {
		char* pS = buf;
		char* pE = buf;
		while (*pS && whitespace(*pS)) pS++;

		if (*pS == '#' || *pS == '\0') {
			// skip comments and blank lines
		} else if (*pS == '[') {
			// change to new Section
			pS++; while (*pS && whitespace(*pS)) pS++;
			pE = pS; while (*pE && ! whitespace(*pE) && *pE != ']') pE++;
			xfree(curSect);
			curSect = savestringn(pS, (pE-pS));
			inSect = (strcmp(curSect, targetSection)==0);
		} else if (inSect) {
			// process a setting line
			pE = pS; while (*pE && ! whitespace(*pE) && *pE != '=') pE++;
			if (*pE=='\0')
				continue; // invalid setting line (no =)

			if (strncmp(pS,targetName, (pE-pS))==0 && (pE-pS)==strlen(targetName)) {
				pS = pE; while (*pS && whitespace(*pS) && *pS != '=') pS++;
				if (*pS != '=')
					continue; // invalid setting line (no =)
				pS++; while (*pS && whitespace(*pS)) pS++;

				pE = parserFindEndOfValue(pS);
				if ( (*pS=='"' && (pE>pS) && *(pE-1)=='"') || (*pS=='\'' && (pE>pS) && *(pE-1)=='\'') ) {
					pS++;
					pE--;
				}
				targetValue = savestringn(pS, (pE-pS));
				break;
			}
		}
	}

	if (!targetValue)
		targetValue = savestring((defValue)?defValue:"");

	fclose(iniFileFD);
	xfree(buf);
	return targetValue;
}


char* parserFindEndOfValue(char* pS)
{
	char* pE = pS;
	if (*pS == '"') {
		pE++;
		while (*pE && ! (*pE=='"' && *(pE-1)!='\\') ) pE++;
	} else if (*pS == '\'') {
		pE++;
		while (*pE && ! (*pE=='\'' && *(pE-1)!='\\') ) pE++;
	} else if (*pS != '\0') {
		pE++;
		while (*pE && *pE!='#' ) pE++;
		while ((pE>pS) && whitespace(*(pE-1)) ) pE--;
	}
	return pE;
}
