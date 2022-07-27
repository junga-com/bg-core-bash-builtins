
#include "bg_templates.h"

#include <time.h>

#include "bg_bashAPI.h"
#include "BGString.h"
#include "BGFileLines.h"
#include "bg_manifest.h"

typedef struct {
	char* expr;
	int required;
	char* varname;
	char* objExpr;
	char* configSect;
	char* configName;
	char* defaultVal;
	int literalPercent;
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

	DefaultOpts(TemplateExpandOptions, pOpts);

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


int templateExpand(WORD_LIST* args)
{
	TemplateExpandOptions tOpts = {0};
	char* srcTemplate = NULL;
	char* dstFilename = NULL;
	while (args && *args->word->word=='-') {
		char* optArg;
		if (0);
		else if ((optArg=BGCheckOpt("-p|--mkdir", &args)))             tOpts.mkdirFlag      = 1;
		else if ((optArg=BGCheckOpt("-u*|--user*", &args)))            tOpts.user           = optArg;
		else if ((optArg=BGCheckOpt("-g*|--group*", &args)))           tOpts.group          = optArg;
		else if ((optArg=BGCheckOpt("--perm*", &args)))                tOpts.permMode       = optArg;
		else if ((optArg=BGCheckOpt("--policy*", &args)))              tOpts.policy         = optArg;
		else if ((optArg=BGCheckOpt("-s*", &args)))                    tOpts.inputStr       = optArg;
		else if ((optArg=BGCheckOpt("-f*|--file*", &args)))            srcTemplate          = optArg;
		else if ((optArg=BGCheckOpt("-d*|--destination*", &args)))     dstFilename          = optArg;
		else if ((optArg=BGCheckOpt("-S*|--statusVar*",  &args)))      tOpts.statusVarName  = optArg;
		else if ((optArg=BGCheckOpt("--interactive",  &args)))         tOpts.interactiveFlag= 1;
		else if ((optArg=BGCheckOpt("-o*|--objCtx*", &args)))          tOpts._objectContextES = optArg;
		else assertError(NULL, "unknown option '%s' passed to templateExpand", args->word->word);
		args = args->next;
	}

	if (!srcTemplate && ! tOpts.inputStr)
		srcTemplate = WordList_shift(&args);
	if (!dstFilename)
		dstFilename = WordList_shift(&args);

	// user can specify '-' or '--' for outputFilename to indicate output should go to stdout which is the default if its not
	// specified so set dstFilename to NULL so we dont have to keep checking for - or --
	if (dstFilename && ( strcmp("-",dstFilename)==0 || strcmp("--",dstFilename)==0 ) )
		dstFilename = NULL;

	// the remainder of args are context variable assignment statements.
	if (args) {
		// this assumes that we are called from the templateExpand shell function which provides a function scope to store these
		// variable assignments
		while (args) {
			//do_assignment(args->word->word);
			do_assignment_no_expand(args->word->word);
			args = args->next;
		}
	}

	templateExpandC(&tOpts, srcTemplate, dstFilename);

	return 0;
}


int templateExpandC(TemplateExpandOptions* pOpts, char* srcTemplate, char* dstFilename)
{
	DefaultOpts(TemplateExpandOptions, pOpts);

	bgtrace2(1, "templateExpandC(pOpts, '%s' , '%s')\n", srcTemplate, dstFilename);
	if (!pOpts->inputStr && !srcTemplate)
		assertError(NULL, "templateExpandC: niether a template string nor template name was specified. ");

	char* srcTemplatePath = NULL;
	int inputFileStatus = 0;
	if (srcTemplate) {
		srcTemplatePath = templateFindC(NULL, srcTemplate);
		if (!srcTemplatePath)
			assertError(NULL, "templateExpandC: the template name '%s' is not found on this host.",srcTemplate);
		ShellVar_setS("_contextETV", srcTemplate);
		inputFileStatus  = file_status(srcTemplatePath);
	}


	// if srcTemplatePath is a folder, redirect to templateExpandFolderC
	if (dstFilename && srcTemplatePath && inputFileStatus&FS_DIRECTORY ) {
		bgtrace0(2, "templateExpand redirecting to templateExpandFolderC\n");
		return templateExpandFolderC(pOpts, srcTemplatePath, dstFilename);
	}


	// detect the case where srcTemplatePath is a binary file which will be copied without expansion. This supports the folder template
	// feature.
	if (dstFilename && srcTemplatePath && inputFileStatus&FS_EXISTS && !(inputFileStatus&FS_DIRECTORY) ) {
		char* cmd = saprintf("[[ ! \"$(file -ib \"%s\")\" =~ ^text ]]", srcTemplatePath);
		if (0==evalstring(cmd, NULL, SEVAL_NOHIST|SEVAL_NONINT|SEVAL_NOHISTEXP|SEVAL_NOLONGJMP)) {
			bgtrace0(2, "templateExpand redirecting to binary file fsCopy\n");
			fsCopy(srcTemplatePath, dstFilename, (pOpts->mkdirFlag) ? cp_mkdir : 0);
			return EXECUTION_SUCCESS;
		}
	}

	bgtrace1(2, "templateExpand did not redirect. template source is '%s' \n", (pOpts->inputStr)?"string data":srcTemplatePath);

	// TODO: we need to figure out the transient var scope first...
	// # add templateFile to var context for use in templates
	// local -x templateFile="$srcTemplate"
	ShellPushFunctionScope("templateExpandC");
	begin_unwind_frame("templateExpandC");
	add_unwind_protect(pop_var_context, NULL);
	ShellVar_createSet("templateFile", srcTemplate);

	FILE* outFD = stdout;
	char* outFilename = NULL;
	char* tmpFilename = NULL;
	if (dstFilename) {
		// if dstFilename contains a '%' expand it. This is particularly usefull when expanding folder templates
		if (strchr(dstFilename, '%'))
			outFilename = templateExpandStrC(NULL, dstFilename, NULL);
		else
			outFilename = savestring(dstFilename);
		add_unwind_protect(xfree, outFilename);

		tmpFilename = mktempC("/tmp/bgTempExp.XXXXXXXXXX");
		add_unwind_protect(xfree, tmpFilename);
		outFD = fopen(tmpFilename, "w");
		add_unwind_protect(unlink, tmpFilename);
	}

	// if input is a string
	if (pOpts->inputStr) {
		ShellVar_setS("_contextETV", "string");
		char* expandedStr = templateExpandStrC(NULL, pOpts->inputStr, NULL);
		int len = strlen(expandedStr);
		if (fwrite(expandedStr, 1, len, outFD) != len)
			assertError(NULL, "templateExpandC: failed to write the entire expanded string to the destination file");
		if (outFD!=stdout)
			fclose(outFD);


	// if input is a filename to read
	} else {
		FILE* inFD = fopen(srcTemplatePath, "r");
		if (!inFD)
			assertError(NULL,"templateExpandC: could not open template file for reading '%s'", srcTemplatePath);

		TemplateExpression tExpr; TemplateExpression_init(&tExpr);
		BGString sOut; BGString_init(&sOut, 5000);
		BGFileLines buf; BGFileLines_init(&buf, 5000);
		while (BGFileLines_readChunk(&buf, inFD)) {
			char* pS = buf.buf;

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

			// write this chunk of expanded content to the outFD
			if (fwrite(sOut.buf, 1, sOut.len, outFD) != sOut.len)
				assertError(NULL, "templateExpandC: failed to write the entire expanded string to the destination file");
			sOut.len = 0;
		}
		if (outFD!=stdout)
			fclose(outFD);
	}

	// if writing to a temp file, resolve it
	if (tmpFilename) {
		fsReplaceIfDifferent(tmpFilename, outFilename, cp_removeSrc | ((pOpts->mkdirFlag)?cp_mkdir:0));
		xfree(tmpFilename); tmpFilename=NULL;
		xfree(outFilename); outFilename=NULL;
	}
	ShellPopFunctionScope();
	discard_unwind_frame("templateExpandC");
	return 1;
}

// this 'C' function is a wrapper over the shell function because the shell version is probably just as fast as a C implementation
// this exists just to make it easier to invoke form other C builtin code
int templateExpandFolderC(TemplateExpandOptions* pOpts, char* srcFolder, char* dstFolder)
{
	DefaultOpts(TemplateExpandOptions, pOpts);

	SHELL_VAR* vTemplateExpandFolder = ShellFunc_find("templateExpandFolder");
	WORD_LIST* args = NULL;
	args = WordList_unshift(args, dstFolder);
	args = WordList_unshift(args, srcFolder);
	args = WordList_unshift(args, "templateExpandFolder");
	int retVal = ShellFunc_execute(vTemplateExpandFolder, args);
	WordList_free(args);
	return retVal;
}



static int _templateAssetPkgOrder(char* pkgName, char* owningPkgName)
{
	if (strcmp("domainadmin",        pkgName)==0) return 4;
	if (strcmp("localadmin",         pkgName)==0) return 3;
	if (strcmp(bgstr(owningPkgName), pkgName)==0)  return 2;
	return 1;
}


int templateFind(WORD_LIST* args)
{
	TemplateFindOptions tOpts = {0};
	while (args && *args->word->word=='-') {
		char* optArg;
		if (0);
		else if ((optArg=BGCheckOpt("-R*|--retVar*", &args)))          tOpts.retVar               = optArg;
		else if ((optArg=BGCheckOpt("-p*|--pkg*", &args)))             tOpts.packageOverride      = optArg;
		else if ((optArg=BGCheckOpt("--manifest*", &args)))            tOpts.manifestOverride     = optArg;
		else assertError(NULL, "unknown option '%s' passed to templateFindC", args->word->word);
		args = args->next;
	}

	char* templateName = WordList_shift(&args);

	char* templatePath = templateFindC(&tOpts, templateName);

	if (!tOpts.retVar)
		printf("%s\n", bgstr(templatePath));

	return (templatePath != NULL) ? EXECUTION_SUCCESS : EXECUTION_FAILURE;
}

char* templateFindC(TemplateFindOptions* pOpts, char* templateNameOrPath)
{
	DefaultOpts(TemplateFindOptions, pOpts);

	if (pOpts->manifestOverride)
		assertError(NULL, "templateFindC: the --maifest=<overridePath> option in this function is not yet implemented");

	if (!templateNameOrPath)
		return NULL;

	// an absolute path can not be an assetName so it has already been translated (or the user is using an absolute path)
	// some functions run the templateName through this function so that they can accept either an assetName or an absolute path
	if (*templateNameOrPath=='/')
		return savestring(templateNameOrPath);

	ManifestRecord target; ManifestRecord_assign(&target, NULL,"template(.folder)?", templateNameOrPath,NULL);
	ManifestRecord* results = manifestGetC(NULL, NULL, &target, NULL);

	// a template assetName can have multiple records from different packages (pkgName could be 'domainadmin' or 'localadmin' too)
	// we use the one from the highest in this list
	// Order:
	//   4 domainadmin
	//   3 localadmin
	//   2 <packageName>
	//   1 (unknown packages)
	//   0 ""

	char* retPath = NULL;
	int retPathPkgRank = -1;
	for (int i=0; results && results[i].assetType; i++) {
		int pkgRank = _templateAssetPkgOrder(results[i].pkgName, pOpts->packageOverride);
		if (pkgRank>retPathPkgRank) {
			retPathPkgRank = pkgRank;
			retPath = results[i].assetPath;
		}
	}

	// support the user expanding local, non system files as long as they are not valid system template names (i.e. found in manifest)
	// if no system template was found, then <templateName> might refer to a local file or folder template.
	// SECURITY: we only fall back to this if <templateName> is not a system template name so that a user can not override an
	//           installed template by using a path to a local template
	if (!retPath && fsExists(templateNameOrPath)) {
		// TODO: convert this to an asbsolute path.
		retPath = templateNameOrPath;
	}

	if (retPath)
		retPath = savestring(retPath);
	xfree(results);

	if (pOpts->retVar) {
		SHELL_VAR* vRetVar = ShellVar_findUpVar(pOpts->retVar);
		ShellVar_set(vRetVar, bgstr(retPath));
	}

	return retPath;
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

	if (*pS=='/' || *pS=='\\') {
		pE = pS;
		while (*pE=='/' || *pE=='\\') {
			pExpr->literalPercent++;
			pE++;
		}
		if (*pE!='%')
			pExpr->badSyntax = 5;
		pS = pE;

	} else if (strncmp(pS, "config[", 7)==0) {
		pS+=7;
		//pE = pS; while (*pE && *pE!=']' && *pE!=':' && *pE!='%') pE++;
		pE = bgstrpbrk(pS, "%:]\n\0");
		if (*pE==']') {
			pExpr->configSect = savestringn(pS,(pE-pS));
			pS = pE+1;
			pE = pS; while (*pE && *pE!=':' && *pE!='%') pE++;
			pExpr->configName = savestringn(pS,(pE-pS));
		} else {
			pExpr->badSyntax = 2;
		}
		pS = pE;

	} else if (*pS == '$')  {
		// TODO: call a function from bg_objects.c to consume the expression so that it can contain :
		// consider using parse_string() here
		//pE = pS; while (*pE && *pE!=':' && *pE!='%') pE++;
		pE = bgstrpbrk(pS, "%:\n\0");
		pExpr->objExpr = savestringn(pS,(pE-pS));
		pS = pE;

	} else {
		//pE = pS; while (*pE && *pE!=':' && *pE!='%') pE++;
		pE = bgstrpbrk(pS, "%:\n\0");
		pExpr->varname = savestringn(pS,(pE-pS));
		pS = pE;
	}

	if (*pS!='%' && *pS!=':')
		pExpr->badSyntax = 3;

	if (*pS==':') {
		pS++;
		//pE = pS; while (*pE && *pE!='%') pE++;
		pE = bgstrpbrk(pS, "%\n\0");
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
	char* allocatedValue = NULL;
	if (pExpr->literalPercent>0) {
		value = allocatedValue = xmalloc(pExpr->literalPercent+1);
		char* pS = value;
		for (int i=0; i<pExpr->literalPercent; i++)
			*pS++ = '%';
		*pS = '\0';

	} else if (strcmp("now",bgstr(pExpr->varname))==0) {
		time_t nowEpoch = time(NULL);
		struct tm nowTime; localtime_r(&nowEpoch, &nowTime);
		char* format = NULL;
		if ((!pExpr->defaultVal) || !(*pExpr->defaultVal) || strcasecmp("RFC5322", pExpr->defaultVal)==0)
			format = "%a, %d %b %Y %T %z";
		else {
			format = pExpr->defaultVal;
			for (char* s=format; s && *s; s++)
				if (*s=='^')
					*s = '%';
		}
		value = allocatedValue = malloc(200);
		*value = '\0';
		strftime(value, 200, format, &nowTime);

	} else if (pExpr->varname) {
		SHELL_VAR* var = ShellVar_find(pExpr->varname);
		value = ShellVar_get(var);

	} else if (pExpr->configSect) {
		char* iniParamGetC(IniScheme* pScheme, char* iniFilenameSpec, char* targetSection, char* targetName, char* defValue);
		value = allocatedValue = configGetC(NULL, pExpr->configSect, pExpr->configName, NULL);

	} else if (pExpr->objExpr) {
		assertError(NULL,"object syntaxt in template vars is not yet implemented in the C builtin");

	} else
		assertError(NULL, "_evaluateTemplateExpr: logic error.  none of (varname,configSect,objExpr) are set in pExpr");

	if (!value) {
		if (!value && pExpr->badSyntax) {
			value = pExpr->expr;
		}

		if (!value && pExpr->required) {
			assertError(WordList_fromString("--errorClass=assertTemplateError --errorFn=templateEvaluateVarToken -v context:_contextETV", IFS,0),"The required template variable '%s' does not exist", pExpr->varname);
		}

		if (!value && pExpr->defaultVal && *pExpr->defaultVal=='$') {
			char* defRef = pExpr->defaultVal +1;
			SHELL_VAR* defVar = ShellVar_find(defRef);
			value = ShellVar_get(defVar);
		}

		if (!value) {
			value = pExpr->defaultVal;
		}
	}

	BGString_append(pOut, (value)?value:"", "");
	xfree(allocatedValue);
}
