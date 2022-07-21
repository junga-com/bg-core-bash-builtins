
#include "bg_templates.h"

#include "bg_bashAPI.h"
#include "BGString.h"


typedef struct {
	char* expr;
	int required;
	char* varname;
	char* objExpr;
	char* configSect;
	char* configName;
	char* defaultVal;
	int badSyntax;
} TemplateExpression;

void TemplateExpression_init(TemplateExpression* pExpr)
{
	memset(pExpr, 0, sizeof(*pExpr));
}

void TemplateExpression_free(TemplateExpression* pExpr)
{
	xfree(pExpr->expr);
	xfree(pExpr->varname);
	xfree(pExpr->objExpr);
	xfree(pExpr->configSect);
	xfree(pExpr->configName);
	xfree(pExpr->defaultVal);
	TemplateExpression_init(pExpr);
}

extern char* _parseTemplateExpr(TemplateExpression* pExpr, char* pStart);
extern void _evaluateTemplateExpr(TemplateExpression* pExpr, BGString* pOut);



int templateExpandStr(WORD_LIST* args)
{
	TemplateExpandOptions tOpts = {0};
	while (args && *args->word->word=='-') {
		char* optArg;
		if (BGRetVar_initFromOpts(&tOpts.retVar, &args));
		else if ((optArg=BGCheckOpt("-s", &args)));
		else if ((optArg=BGCheckOpt("-o*|--objCtx*", &args))) tOpts._objectContextES = optArg;
		else assertError(NULL, "unknown option '%s' passed to templateExpandStr", args->word->word);
		args = args->next;
	}

	char* templateStr         = WordList_shift(&args);
	char* scrContextForErrors = WordList_shift(&args);

	char* value = templateExpandStrC(&tOpts, templateStr, scrContextForErrors);

	// the default for the C form of this function is to simply return value from the function but for the bash compatible form
	// we need the default destination to write it to std out
	if (!tOpts.retVar)
		printf("%s\n",value);

	xfree(value);
	return 0;
}


char* templateExpandStrC(TemplateExpandOptions* pOpts, char* templateStr, char* scrContextForErrors)
{
	if (!templateStr)
		return NULL;

	char* pS = templateStr;

	BGString sOut; BGString_init(&sOut, strlen(templateStr)*5/4);

	TemplateExpression tExpr; TemplateExpression_init(&tExpr);

	while (*pS) {
		char* pE = strchr(pS, '%');
		if (!pE) pE = pS + strlen(pS);
		BGString_appendn(&sOut, pS, (pE-pS), "");
		pS = pE;

		if (*pS == '%') {
			pS = _parseTemplateExpr(&tExpr, pS);
			_evaluateTemplateExpr(&tExpr, &sOut);
			TemplateExpression_free(&tExpr);
		}
	}
	return sOut.buf;
}


char* _parseTemplateExpr(TemplateExpression* pExpr, char* pStart)
{
	char* pS = pStart;
	if (!pS || *pS!='%')
		assertError(NULL, "logic error in how _parseTemplateExpr was called. pS should point to a '%' char");

	TemplateExpression_init(pExpr);

	pS++;
	if (*pS == '+') {
		pExpr->required = 1;
		pS++;
	}

	char* pE = pS;

	if (strncmp(pS, "config[", 7)==0) {
		pS+=7;
		pE = pS; while (*pE && *pE!=']' && *pE!=':' && *pE!='%') pE++;
		if (*pE==']') {
			pExpr->configSect = savestringn(pS,(pE-pS));
			pS = pE+1;
			pE = pS; while (*pE && *pE!=':' && *pE!='%') pE++;
			pExpr->configName = savestringn(pS,(pE-pS));
			pS = pE;
		} else {
			pExpr->badSyntax = 2;
			pS = pE;
		}
	} else if (*pS == '$')  {
		// TODO: call a function from bg_objects.c to consume the expression so that it can contain :
		pE = pS; while (*pE && *pE!=':' && *pE!='%') pE++;
		pExpr->objExpr = savestringn(pS,(pE-pS));
		pS = pE;
	} else {
		pE = pS; while (*pE && *pE!=':' && *pE!='%') pE++;
		pExpr->varname = savestringn(pS,(pE-pS));
		pS = pE;
	}

	if (*pS!='%' && *pS!=':')
		pExpr->badSyntax = 3;

	if (*pS==':') {
		pS++;
		pE = pS; while (*pE && *pE!='%') pE++;
		pExpr->defaultVal = savestringn(pS,(pE-pS));
		pS = pE;
	}

	if (*pE!='%')
		pExpr->badSyntax = 4;

	if (*pE) pE++;
	pExpr->expr = savestringn(pStart, (pE-pStart));
	return pE;
}

void _evaluateTemplateExpr(TemplateExpression* pExpr, BGString* pOut)
{
	char* value = NULL;
	if (pExpr->varname) {
		SHELL_VAR* var = ShellVar_find(pExpr->varname);
		value = ShellVar_get(var);

	} else if (pExpr->configSect) {
		char* iniParamGetC(IniScheme* pScheme, char* iniFilenameSpec, char* targetSection, char* targetName, char* defValue);
		value = configGetC(NULL, pExpr->configSect, pExpr->configName, NULL);

	} else if (pExpr->objExpr) {
		assertError(NULL,"object syntaxt in template vars is not yet implemented in the C builtin");

	} else
		assertError(NULL, "_evaluateTemplateExpr: logic error.  none of (varname,configSect,objExpr) are set in pExpr");

	if (!value) {
		if (!value && pExpr->required)
			assertError(NULL,"The required templated variable '%s' does not exist", pExpr->varname);

		if (pExpr->defaultVal && *pExpr->defaultVal=='$') {
			char* defRef = pExpr->defaultVal +1;
			SHELL_VAR* defVar = ShellVar_find(defRef);
			value = ShellVar_get(defVar);
		} else {
			value = pExpr->defaultVal;
		}
	}

	BGString_append(pOut, (value)?value:"", "");
}
