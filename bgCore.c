/* bgCore - loadable builtin to optimize bg_objects.sh  */

/* See Makefile for compilation details. */

#include "bg_objects.h"

#include <execute_cmd.h>

#include "bg_bashAPI.h"
#include "bg_json.h"
#include "bg_manifest.h"
#include "bg_import.h"
#include "bg_templates.h"


char* bgOptionGetOpt(WORD_LIST** args)
{
	char* param = (*args)->word->word;
	char* t = strstr(param,"=");

	// --longO=<optArg>;
	if (t) {
		return t+1;

	// -o <optArg>;
	} else if (*(param+1)=='\0') {
		if (!((*args)->next))
			return NULL;
		t = (*args)->next->word->word;
		(*args) = (*args)->next;
		return t;

	// -o<optArg>;
	} else {
		return param+2;
	}
}

void testAssertError(WORD_LIST* args)
{
	__bgtrace("testAssertError STARTING (%s)\n", WordList_toString(args));
	printf("testAssertError STARTING (%s)\n", WordList_toString(args));

	if (args && strcmp("segfault", args->word->word)) {
		char* f=NULL;
		*f='\0';
	}


	if (args)
		assertError(WordList_fromString("-v name",IFS,0), "this is a test error '%s'", "hooters");
	// SHELL_VAR* func = ShellFunc_find("myCode");
	// ShellFunc_execute(func, args);

	printf("testAssertError ENDING\n");
	__bgtrace("testAssertError ENDING\n");
}

int bgCoreNestCount = 0;

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// bgCore <cmd> ....
// This is the entry point builtin function. It dispatches the call to the specific function based on the first command word
int bgCore_builtin(WORD_LIST* list)
{
	bgCoreNestCount++;
	int ret = EXECUTION_FAILURE;

	char* label = ""; if (!label);
	bgtrace0(1,"###############################################################################################################\n");
	bgtrace2(1,"### %d START %s (bgCore)\n", callFrames_getPos()+1, label=WordList_toString(list));
	bgtracePush();
	if (!list || !list->word) {
		fprintf(stderr, "Error - <cmd> is a required argument. See usage..\n\n");
		builtin_usage();
		bgtracePop();
		bgtrace1(1,"### END-NOARGS %s\n",label);
		bgCoreNestCount--;
		return (EX_USAGE);
	}

	// if anything calls assertError(), it will either terminate the PID and the rest of the builtin will not run, or if there is
	// a Try: / Catch: block in bash that is catching the error, it will longjump back to here and continue to execute the true
	// condition which will exit back to the parser flow.
	CallFrame* callFrame = callFrames_push();
	if (setjmp(callFrame->jmpBuf)) {
		// assertError() was called .. we just need to end without doing anything so that the bash assertError mechanism can take
		// over
		__bgtrace("!!! caught the LONGJMP '%d'\n", bgCoreNestCount);
		bgtracePop();
		bgtrace2(1,"### %d END-JMP %s\n", callFrames_getPos()+1, label);
		ret = (EXECUTION_FAILURE);
	}

	else {
		// normal setjmp path...

		// bgCore ping
		if (strcmp("ping", list->word->word)==0) {
			ret = EXECUTION_SUCCESS;
		}


		// ### Objects ###############################################################################################################

		// bgCore IsAnObjRef
		else if (strcmp("IsAnObjRef", list->word->word)==0) {
			ret = IsAnObjRef(list->next) ? 0 : 1;
		}

		// bgCore ConstructObject
		else if (strcmp("ConstructObject", list->word->word)==0) {
			BashObj* pObj = ConstructObject(list->next);
			BashObj_free(pObj);
			ret = EXECUTION_SUCCESS;
		}


		// bgCore DeclareClassEnd
		else if (strcmp("DeclareClassEnd", list->word->word)==0) {
			list=list->next;
			DeclareClassEnd(list->word->word);
			ret = EXECUTION_SUCCESS;
		}

		// bgCore _bgclassCall <oid> <refClass> <hierarchyLevel> |<objSyntaxStart> [<p1,2> ... <pN>]
		else if (strcmp("_bgclassCall", list->word->word)==0) {
			// __bgtrace("\n#### %s\n", WordList_toString(list));
			// _bgtraceStack();
			ret = _bgclassCall(list->next);
		}

		// bgCore _classUpdateVMT [-f|--force] <className>
		else if (strcmp("_classUpdateVMT", list->word->word)==0) {
			list=list->next;
			int forceFlag=0;
			while (list && *list->word->word=='-') {
				char* optArg;
				if ((optArg=BGCheckOpt("-f|--force", &list)))
					forceFlag = 1;
				list = list->next;
			}
			if (!list)
				assertError(NULL, "<className> is a required argument to _classUpdateVMT");

			ret = _classUpdateVMT(list->word->word, forceFlag);
		}


		// bgCore Object_getIndexes [--sys|--real|--all]
		else if (strcmp("Object_getIndexes", list->word->word)==0 || strcmp("Object::getIndexes", list->word->word)==0 || strcmp("Object::getAttributes", list->word->word)==0) {
			list = list->next;
			ToJSONMode mode = tj_real;
			BGRetVar* retVar = BGRetVar_new();
			retVar->delim = "\n";
			while (list && (*list->word->word == '-' || *(list->word->word) == '+')) {
				char* param = list->word->word;
				if      (strcmp("--"      , param)==0) { list=list->next; break; }
				else if (strcmp("--all"   , param)==0) { mode        = tj_all; }
				else if (strcmp("--sys"   , param)==0) { mode        = tj_sys; }
				else if (strcmp("--real"  , param)==0) { mode        = tj_real; }
				else if ((BGRetVar_initFromOpts(&retVar, &list)));
				else                                   { assertError(NULL, "invalid option '%s'\n", list->word->word); }
				list = (list) ? list->next : NULL;
			}

			BashObj this; BashObj_initFromContext(&this);

			WORD_LIST* indexList = Object_getIndexes(&this, mode);
			outputValues(retVar, indexList);

			xfree(retVar);
			ret = EXECUTION_SUCCESS;
		}



		// ### JSON ##################################################################################################################

		// bgCore Object_fromJSON
		else if (strcmp("Object_fromJSON", list->word->word)==0 || strcmp("Object::fromJSON", list->word->word)==0) {
			ret = Object_fromJSON(list->next);
		}

		// bgCore Object_toJSON
		else if (strcmp("Object_toJSON", list->word->word)==0 || strcmp("Object::toJSON", list->word->word)==0) {
			list = list->next;
			ToJSONMode mode = tj_real;
			int indentLevel = 0;
			while (list && *list->word->word == '-') {
				char* param = list->word->word;
				if      (strcmp("--all"   , param)==0) { mode        = tj_all; }
				else if (strcmp("--sys"   , param)==0) { mode        = tj_sys; }
				else if (strcmp("--indent", param)==0) { indentLevel = atol(bgOptionGetOpt(&list)); }
				else                                   { assertError(NULL, "invalid option '%s'\n", list->word->word); }
				list = (list) ? list->next : NULL;
			}

			BashObj this; BashObj_initFromContext(&this);

			ret = Object_toJSON(&this, mode, indentLevel);
			printf("\n");
		}


		// bgCore arrayToJSON <inVar> <outVar>
		else if (strcmp("arrayToJSON", list->word->word)==0 || strcmp("Object::toJSON", list->word->word)==0) {
			list = list->next;
			SHELL_VAR* vInVar  = ShellVar_find(list->word->word); list = list->next;
			BGRetVar retVar; BGRetVar_initFromVarname(&retVar, list->word->word);
			char* jsonValue = ShellVar_toJSON(vInVar, 0);
			outputValue(&retVar, jsonValue);
			xfree(jsonValue);
		}

		// bgCore ConstructObjectFromJson
		else if (strcmp("ConstructObjectFromJson", list->word->word)==0) {
			ret = ConstructObjectFromJson(list->next);
		}


		// ### MISC ##################################################################################################################


		// bgCore varOutput
		else if (strcmp("varOutput", list->word->word)==0 || strcmp("outputValue", list->word->word)==0) {
			list=list->next;
			BGRetVar* retVar = BGRetVar_new();
			while (list && (*(list->word->word) == '-' || *(list->word->word) == '+')) {
				if (strcmp("--",list->word->word)==0) { list=list->next; break; }
				else if ((BGRetVar_initFromOpts(&retVar, &list)));
				else break; //assertError(NULL, "invalid option '%s'\n", list->word->word);
				list = (list) ? list->next : NULL;
			}
			outputValues(retVar, list);
			if (retVar)
				xfree(retVar);
			ret = EXECUTION_SUCCESS;
		}


		// bgCore findInLibPaths
		else if (strcmp("findInLibPaths", list->word->word)==0) {
			list=list->next;
			char* foundPath = findInLibPaths(list->word->word);
			list=list->next;
			if (list)
				ShellVar_setS(list->word->word, foundPath);
			else
				printf("%s\n",foundPath);
			ret = (foundPath && strcmp("",foundPath)!=0);
		}

		// OBSOLETE? import gets its own builtin instead of reusing "bgCore import..."
		// bgCore import <scriptName>
		else if (strcmp("import", list->word->word)==0) {
			list = list->next;
			char* param = (list) ? list->word->word : NULL;
			int importFlags = 0;
			while (param && *param == '-') {
				param = (*(param+1)=='-') ? param+2 : param+1;
				switch (*param) {
				  case 'd': importFlags |= im_devOnlyFlag    ; break;
				  case 'f': importFlags |= im_forceFlag      ; break;
				  case 'e': importFlags |= im_stopOnErrorFlag; break;
				  case 'q': importFlags |= im_quietFlag      ; break;
				  case 'g': importFlags |= im_getPathFlag    ; break;
				}
				list = list->next;
				param = (list) ? list->word->word : NULL;
			}
			if (!list)
				assertError(NULL,"<scriptname> is a required argument to import\n");
			char* scriptName = list->word->word;
			list = list->next;

			char* scriptPath = NULL;
			ret = importBashLibrary(scriptName, importFlags, &scriptPath);
			if (importFlags&im_getPathFlag) {
				if (list)
					ShellVar_setS(list->word->word, scriptPath);
				else
					printf("%s\n",scriptPath);
			}
			if (scriptPath) xfree(scriptPath);
		}


		// bgCore manifestGet [-p|--pkg=<pkgMatch>] [-o|--output='$n'] <assetTypeMatch> <assetNameMatch>
		else if (strcmp("manifestGet", list->word->word)==0) {
			list = list->next;
			char* param = (list) ? list->word->word : NULL;
			char* pkgMatch = NULL;
			char* outputStr = "$0";
			char* manifestFile = NULL;
			while (param && *param == '-') {
				param = (*(param+1)=='-') ? param+2 : param+1;
				switch (*param) {
				  case 'p': pkgMatch     = bgOptionGetOpt(&list); break;
				  case 'o': outputStr    = bgOptionGetOpt(&list); break;
				  case 'm': manifestFile = bgOptionGetOpt(&list); break;
				}
				list = list->next;
				param = (list) ? list->word->word : NULL;
			}
			ManifestRecord target; ManifestRecord_assign(&target, NULL,NULL,NULL,NULL);
			target.pkgName = pkgMatch;
			if (list) {
				target.assetType = bgMakeAnchoredRegEx(list->word->word);
				list = list->next;
			}
			if (list) {
				target.assetName = bgMakeAnchoredRegEx(list->word->word);
				list = list->next;
			}

			// when outputStr is not null, it will print results to stdout
			ManifestRecord foundManRec = manifestGet(manifestFile, outputStr, &target, NULL);
			ManifestRecord_free(&foundManRec);
			if (target.assetType) xfree(target.assetType);
			if (target.assetName) xfree(target.assetName);

			ret = EXECUTION_SUCCESS;
		}


		else if (strcmp("pathGetCommon", list->word->word)==0) {
			list=list->next;
			char* retVar = NULL;
			char* optArg;
			while (list && (*(list->word->word) == '-' || *(list->word->word) == '+')) {
				if (strcmp("--",list->word->word)==0) { list=list->next; break; }
				if ((optArg=BGCheckOpt("-R*|--retVar=*", &list)))
					retVar = optArg;
				else assertError(NULL, "invalid option '%s'\n", list->word->word);
				list = (list) ? list->next : NULL;
			}

			char* retVal = pathGetCommon(list);

			if (retVar)
				ShellVar_setS(retVar, retVal);
			else
				printf("%s\n", retVal);
			ret = EXECUTION_SUCCESS;
		}

		// bgCore fsExpandFiles
		else if (strcmp("fsExpandFiles", list->word->word)==0) {
			list = list->next;
			ret = fsExpandFiles(list);
		}

		// bgCore templateExpandStr
		else if (strcmp("templateExpandStr", list->word->word)==0) {
			list = list->next;
			ret = templateExpandStr(list);
		}

		// bgCore templateExpand
		else if (strcmp("templateExpand", list->word->word)==0) {
			list = list->next;
			ret = templateExpand(list);
		}

		// bgCore templateFind
		else if (strcmp("templateFind", list->word->word)==0) {
			list = list->next;
			ret = templateFind(list);
		}

		// ### ini ###############################################################################################################

		// bgCore iniParamGet
		else if (strcmp("iniParamGet", list->word->word)==0) {
			list = list->next;
			ret = iniParamGet(list);
		}

		// bgCore iniParamSet
		else if (strcmp("iniParamSet", list->word->word)==0) {
			list = list->next;
			ret = iniParamSet(list);
		}


		// ### Debugging and tests ##################################################################################################################

		// bgCore ShellContext_dump
		else if (strcmp("ShellContext_dump", list->word->word)==0) {
			list=list->next;
			ShellContext_dump(shell_variables, (list!=NULL));
			//ShellContext_dump(global_variables, (list!=NULL));
			ret = EXECUTION_SUCCESS;
		}

		// bgCore scopeToJSON <retVar> <stackPositionFromGlobal>
		// returns a variabl scope (aka context) as a single JSON object (no type info -- just hierarchal name,values)
		else if (strcmp("scopeToJSON", list->word->word)==0) {
			list=list->next;
			BGRetVar retVar; BGRetVar_initFromVarname(&retVar, list->word->word);
			list=list->next;
			int stackPositionFromGlobal = atoi(list->word->word);

			// TODO: this algorithm may not handle temporary scopes for assignments before a command (see two places).
			//       It might be OK if temp vars are always merged into the function scope by this point
			VAR_CONTEXT* cntx = global_variables;
			for (int i=0; i<stackPositionFromGlobal; i++)
				cntx = (cntx && cntx->up)?cntx->up:cntx;

			char* jsonTxt = ShellContext_toJSON(cntx);
			outputValue(&retVar, jsonTxt);
			xfree(jsonTxt);
			ret = EXECUTION_SUCCESS;
		}

		// bgCore dbgVars <retVar> <stackPositionFromGlobal>
		// returns a variable scope (aka context) as and array of variable objects (with type information)
		else if (strcmp("dbgVars", list->word->word)==0) {
			list=list->next;
			BGRetVar retVar; BGRetVar_initFromVarname(&retVar, list->word->word);
			list=list->next;
			int stackPositionFromGlobal = atoi((list)?list->word->word:"0");

			char* jsonTxt = ShellContext_dumpJSON(stackPositionFromGlobal, DJ_DOHIERACHY);
			outputValue(&retVar, jsonTxt);
			xfree(jsonTxt);
			ret = EXECUTION_SUCCESS;
		}

		// bgCore testAssertError
		else if (strcmp("testAssertError", list->word->word)==0) {
			testAssertError(list->next);
			ret = EXECUTION_SUCCESS;
		}

		// bgCore transTest
		else if (strcmp("transTest", list->word->word)==0) {
			list = list->next;

			SHELL_VAR* vAssoc = ShellVar_find(list->word->word);
			AssocSortedItr itr = {0}; AssocSortedItr_init(&itr, assoc_cell(vAssoc));
			BUCKET_CONTENTS* pEl;
			while ((pEl = AssocSortedItr_next(&itr))) {
				printf("%s\n",pEl->key);
			}
			AssocSortedItr_free(&itr);
		}


		else {
			assertError(NULL, "error: command not recognized cmd='%s'\n", (list && list->word)?list->word->word:"");
		}

		callFrames_pop();
		bgtracePop();
		bgtrace2(1,"### %d END-NORM %s\n\n", callFrames_getPos()+1, label);
	}

	bgCoreNestCount--;
	assertError_done();
	return ret;
}


/* Called when `bgCore' is enabled and loaded from the shared object.  If this
   function returns 0, the load fails. */
int bgCore_builtin_load (char* name)
{
	bgtraceOn();
	assertError_init();
	_bgtrace(1,"LOAD ############################################################################################\n");
	return (1);
}

/* Called when `bgCore' is disabled. */
void bgCore_builtin_unload (char* name)
{
	onUnload_objects();
}

char *_bgclassCall_doc[] = {
	"Invoke a method or operator on a bash object instance.",
	"",
	"The bg_objects.sh style of object oriented bash uses a syntax that invokes _bgclassCall.",
	(char *)NULL
};

struct builtin bgCore_struct = {
	"bgCore",			/* builtin name */
	bgCore_builtin,		/* function implementing the builtin */
	BUILTIN_ENABLED,		/* initial flags for builtin */
	_bgclassCall_doc,			/* array of long documentation strings. */
	"bgCore <oid> <className> <hierarchyLevel> '|' <objectSyntaxToExecute>",			/* usage synopsis; becomes short_doc */
	0				/* reserved for internal use */
};






// ###############################################################################################################################

int import_builtin(WORD_LIST* args)
{
	int ret = EXECUTION_FAILURE;

	char* label = ""; if (!label);
	bgtrace0(1,"###############################################################################################################\n");
	bgtrace2(1,"### %d START import %s \n", callFrames_getPos()+1, label=WordList_toString(args));
	bgtracePush();

	CallFrame* callFrame = callFrames_push();
	if (setjmp(callFrame->jmpBuf)) {
		bgtracePop();
		bgtrace2(1,"### %d END-JMP %s\n", callFrames_getPos()+1, label);
		ret = (EXECUTION_FAILURE);

	} else {
		char* param = (args) ? args->word->word : NULL;
		int importFlags = 0;
		while (param && *param == '-') {
			param = (*(param+1)=='-') ? param+2 : param+1;
			switch (*param) {
			  case 'd': importFlags |= im_devOnlyFlag    ; break;
			  case 'f': importFlags |= im_forceFlag      ; break;
			  case 'e': importFlags |= im_stopOnErrorFlag; break;
			  case 'q': importFlags |= im_quietFlag      ; break;
			  case 'g': importFlags |= im_getPathFlag    ; break;
			}
			args = args->next;
			param = (args) ? args->word->word : NULL;
		}
		if (!args)
			return assertError(NULL,"<scriptname> is a required argument to import\n");
		char* scriptName = args->word->word;
		args = args->next;

		char* scriptPath = NULL;

		ret = importBashLibrary(scriptName, importFlags, &scriptPath);

		if (importFlags&im_getPathFlag) {
			if (args)
				ShellVar_setS(args->word->word, scriptPath);
			else
				printf("%s\n",scriptPath);
		}
		if (scriptPath) xfree(scriptPath);


		callFrames_pop();
 		bgtracePop();
 		bgtrace2(1,"### %d END-NORM import %s\n\n", callFrames_getPos()+1, label);
	}

	return ret;
}


/* Called when `bgCore' is enabled and loaded from the shared object.  If this
   function returns 0, the load fails. */
int import_builtin_load (char* name)
{
	return (1);
}

/* Called when `import' is disabled. */
void import_builtin_unload (char* name)
{
	onUnload_objects();
}

char *import_doc[] = {
	"Source a bash library with idempotency.",
	"",
	"import can be used instead of 'source' to include a bash script library with idempotency which means it works correctly if"
	"there are complicated dependency relationships between library scripts and scripts that use them",
	(char *)NULL
};

struct builtin import_struct = {
	"import",			/* builtin name */
	import_builtin,		/* function implementing the builtin */
	BUILTIN_ENABLED,		/* initial flags for builtin */
	import_doc,			/* array of long documentation strings. */
	"import <scriptName> ;$L1;$L2",			/* usage synopsis; becomes short_doc */
	0				/* reserved for internal use */
};
