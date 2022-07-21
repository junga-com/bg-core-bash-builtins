
#if !defined (_bg_ini_H_)
#define _bg_ini_H_

#include <stdio.h>

#include "bg_bashAPI.h"

typedef struct {
	char* retVar;
	char* delim;
	int addIfNotFound;
	int expandAsTemplate;
	char* schema;
	int exitCode;

	char* statusVarName;  // for operations that can change the inin file, this var will be set to "changed" if it does so
	char* resultsVarName; // for set operations, this returns one of {nochange,changedExistingSetting,addedToExistingSection,addedSectionAndSetting}
	int mkdirFlag;
	char* comment;
	char* sectComment;
	char* commentsStyle;
	char* quoteMode;
	char* sectPad;
	char* paramPad;
} IniScheme;

extern void IniScheme_init(IniScheme* pScheme);
extern void IniScheme_validate(IniScheme* pScheme);

// pass NULL for pScheme to use the default scheme
extern char* iniParamGetC(IniScheme* pScheme, char* iniFilename, char* targetSection, char* targetName, char* defValue);
extern int   iniParamGet( WORD_LIST* args);

extern void  iniParamSetC(IniScheme* pScheme, char* iniFilenameSpec, char* targetSection, char* targetName, char* targetValue);
extern int   iniParamSet( WORD_LIST* args);

extern char* configGetC(IniScheme* pScheme, char* targetSection, char* targetName, char* defValue);
extern int   configGet(WORD_LIST* args);

extern void iniValidate(char* schema, char* value);

#endif /* _bg_ini_H_ */
