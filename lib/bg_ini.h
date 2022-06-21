
#if !defined (_bg_ini_H_)
#define _bg_ini_H_

#include <stdio.h>

#include "bg_bashAPI.h"

typedef struct {
	char* retVar;
	char* delim;
	int addIfNotFound;
	int expandAsTemplate;
	int noSectionPad;
	char* schema;
} IniScheme;

extern int iniParamGet(WORD_LIST* args);

// pass NULL for pScheme to use the default scheme
extern char* iniParamGetC(IniScheme* pScheme, char* iniFilename, char* targetSection, char* targetName, char* defValue);


#endif /* _bg_ini_H_ */
