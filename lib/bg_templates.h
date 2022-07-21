
#if !defined (_bg_templates_H_)
#define _bg_templates_H_

#include <stdio.h>

#include "bg_bashAPI.h"

typedef struct {
	BGRetVar* retVar;
	char* _objectContextES;
} TemplateExpandOptions;

extern int templateExpandStr(WORD_LIST* args);

extern char* templateExpandStrC(TemplateExpandOptions* pOpts, char* templateStr, char* scrContextForErrors);

#endif /* _bg_templates_H_ */
