
#if !defined (_bg_templates_H_)
#define _bg_templates_H_

#include <stdio.h>

#include "bg_bashAPI.h"

typedef struct {
	BGRetVar* retVar;
	char* _objectContextES;
	char* user;
	char* group;
	char* permMode;
	char* policy;
	char* inputStr;
	char* statusVarName;
	int interactiveFlag;
	int mkdirFlag;
} TemplateExpandOptions;

typedef struct {
	char* packageOverride;
	char* manifestOverride;
	char* retVar;
} TemplateFindOptions;

extern int templateExpandStr(WORD_LIST* args);
extern char* templateExpandStrC(TemplateExpandOptions* pOpts, char* templateStr, char* scrContextForErrors);

extern int templateExpand(WORD_LIST* args);
extern int templateExpandC(TemplateExpandOptions* pOpts, char* srcTemplate, char* dstFilename);

extern int templateExpandFolderC(TemplateExpandOptions* pOpts, char* srcFolder, char* dstFolder);

extern int   templateFind(WORD_LIST* args);
extern char* templateFindC(TemplateFindOptions* pOpts, char* templateNameOrPath);


#endif /* _bg_templates_H_ */
