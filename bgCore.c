/* bgCore - loadable builtin to optimize bg_objects.sh  */

/* See Makefile for compilation details. */

#include "bg_objects.h"

#include <execute_cmd.h>

#include "bg_bashAPI.h"
#include "bg_json.h"
#include "bg_manifest.h"
#include "bg_import.h"


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

    if (args)
        assertError(WordList_fromString("-v name",IFS,0), "this is a test error '%s'", "hooters");
    // SHELL_VAR* func = ShellFunc_find("myCode");
    // args = make_word_list( make_word("thisfunc") ,args);
    // execute_shell_function(func, args);

    printf("testAssertError ENDING\n");
    __bgtrace("testAssertError ENDING\n");
}



/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// bgCore <cmd> ....
// This is the entry point builtin function. It dispatches the call to the specific function based on the first command word
int bgCore_builtin(WORD_LIST* list)
{
    int ret = EXECUTION_FAILURE;

    char* label = ""; if (!label);
    bgtrace1(1,"### STRing bgCore_builtin(%s)\n", label=WordList_toString(list)); bgtracePush();
    if (!list || !list->word) {
        fprintf(stderr, "Error - <cmd> is a required argument. See usage..\n\n");
        builtin_usage();
        bgtracePop(); bgtrace1(1,"### ENDING bgCore_builtin(%s)\n",label);
        return (EX_USAGE);
    }

    // if anything calls assertError(), it will either terminate the PID and the rest of the builtin will not run, or if there is
    // a Try: / Catch: block in bash that is catching the error, it will longjump back to here and continue to execute the true
    // condition which will exit back to the parser flow.
    if (setjmp(assertErrorJmpPoint)) {
        // assertError() was called .. we just need to end without doing anything so that the bash assertError mechanism can take
        // over
        bgtracePop(); bgtrace1(1,"### ENDING bgCore_builtin(%s) Returning from assertError()\n",label);
        return (EXECUTION_FAILURE);

    } else {
        // normal setjmp path...

        // bgCore ping
        if (strcmp(list->word->word, "ping")==0) {
            bgtracePop(); bgtrace1(1,"### ENDING bgCore_builtin(%s)\n",label);
            return EXECUTION_SUCCESS;
        }



        // ### Objects ###############################################################################################################

        // bgCore ConstructObject
        if (strcmp(list->word->word, "ConstructObject")==0) {
            BashObj* pObj = ConstructObject(list->next);
            xfree(pObj);
            bgtracePop(); bgtrace1(1,"### ENDING bgCore_builtin(%s)\n",label);
            return EXECUTION_SUCCESS;
        }

        // bgCore DeclareClassEnd
        if (strcmp(list->word->word, "DeclareClassEnd")==0) {
            list=list->next;
            DeclareClassEnd(list->word->word);
            bgtracePop(); bgtrace1(1,"### ENDING bgCore_builtin(%s)\n",label);
            return EXECUTION_SUCCESS;
        }

        // bgCore _bgclassCall <oid> <refClass> <hierarchyLevel> |<objSyntaxStart> [<p1,2> ... <pN>]
        if (strcmp(list->word->word, "_bgclassCall")==0) {
            ret = _bgclassCall(list->next);
            bgtracePop(); bgtrace1(1,"### ENDING bgCore_builtin(%s)\n",label);
            return ret;
        }

        // bgCore _classUpdateVMT [-f|--force] <className>
        if (strcmp(list->word->word, "_classUpdateVMT")==0) {
            list=list->next; if (!list) return (EX_USAGE);
            int forceFlag=0;
            if (strcmp(list->word->word,"-f")==0 || strcmp(list->word->word,"--force")==0) {
                list=list->next; if (!list) return (EX_USAGE);
                forceFlag=1;
            }
    //        begin_unwind_frame("bgCore");
            int result = _classUpdateVMT(list->word->word, forceFlag);
    //        discard_unwind_frame("bgCore");
            bgtracePop(); bgtrace1(1,"### ENDING bgCore_builtin(%s)\n",label);
            return result;
        }


        // bgCore Object_getIndexes [--sys|--real|--all]
        if (strcmp(list->word->word, "Object_getIndexes")==0 || strcmp(list->word->word, "Object::getIndexes")==0 || strcmp(list->word->word, "Object::getAttributes")==0) {
            list = list->next;
            ToJSONMode mode = tj_real;
            BGRetVar* retVar = BGRetVar_new();
            retVar->delim = "\n";
            while (list && *list->word->word == '-') {
                char* param = list->word->word;
                if      (strcmp("--all"   , param)==0) { mode        = tj_all; }
                else if (strcmp("--sys"   , param)==0) { mode        = tj_sys; }
                else if (strcmp("--real"  , param)==0) { mode        = tj_real; }
                else if ((retVar=BGRetVar_initFromOpts(retVar, &list)));
                else                                   { return assertError(NULL, "invalid option '%s'\n", list->word->word); }
                if (list)
                    list = list->next;
            }

            BashObj this;
            if (BashObj_initFromContext(&this)==EXECUTION_FAILURE)
                return EXECUTION_FAILURE;

            WORD_LIST* indexList = Object_getIndexes(&this, mode);

            outputValues(retVar, indexList);
            if (retVar)
                xfree(retVar);
            bgtracePop(); bgtrace1(1,"### ENDING bgCore_builtin(%s)\n",label);
            return EXECUTION_SUCCESS;
        }



        // ### JSON ##################################################################################################################

        // bgCore Object_fromJSON
        if (strcmp(list->word->word, "Object_fromJSON")==0 || strcmp(list->word->word, "Object::fromJSON")==0) {
            ret = Object_fromJSON(list->next);
            bgtracePop(); bgtrace1(1,"### ENDING bgCore_builtin(%s)\n",label);
            return ret;
        }

        // bgCore Object_toJSON
        if (strcmp(list->word->word, "Object_toJSON")==0 || strcmp(list->word->word, "Object::toJSON")==0) {
            list = list->next;
            ToJSONMode mode = tj_real;
            int indentLevel = 0;
            while (list && *list->word->word == '-') {
                char* param = list->word->word;
                if      (strcmp("--all"   , param)==0) { mode        = tj_all; }
                else if (strcmp("--sys"   , param)==0) { mode        = tj_sys; }
                else if (strcmp("--indent", param)==0) { indentLevel = atol(bgOptionGetOpt(&list)); }
                else                                   { return assertError(NULL, "invalid option '%s'\n", list->word->word); }
                list = list->next;
            }

            BashObj this;
            if (BashObj_initFromContext(&this)==EXECUTION_FAILURE)
                return EXECUTION_FAILURE;

            ret = Object_toJSON(&this, mode, indentLevel);
            printf("\n");

            bgtracePop(); bgtrace1(1,"### ENDING bgCore_builtin(%s)\n",label);
            return ret;
        }

        // bgCore ConstructObjectFromJson
        if (strcmp(list->word->word, "ConstructObjectFromJson")==0) {
            ret = ConstructObjectFromJson(list->next);
            bgtracePop(); bgtrace1(1,"### ENDING bgCore_builtin(%s)\n",label);
            return ret;
        }


        // ### MISC ##################################################################################################################


        // bgCore ShellContext_dump
        if (strcmp(list->word->word, "ShellContext_dump")==0) {
            list=list->next;
            ShellContext_dump(shell_variables, (list!=NULL));
            //ShellContext_dump(global_variables, (list!=NULL));
            bgtracePop(); bgtrace1(1,"### ENDING bgCore_builtin(%s)\n",label);
            return ret;
        }



        // bgCore findInLibPaths
        if (strcmp(list->word->word, "findInLibPaths")==0) {
            list=list->next;
            char* foundPath = findInLibPaths(list->word->word);
            list=list->next;
            if (list)
                ShellVar_setS(list->word->word, foundPath);
            else
                printf("%s\n",foundPath);
            bgtracePop(); bgtrace1(1,"### ENDING bgCore_builtin(%s)\n",label);
            return ret;
        }

        // bgCore import <scriptName>
        if (strcmp(list->word->word, "import")==0) {
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
                return assertError(NULL,"<scriptname> is a required argument to import\n");
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

            bgtracePop(); bgtrace1(1,"### ENDING bgCore_builtin(%s)\n",label);
            return ret;
        }

        // bgCore manifestGet [-p|--pkg=<pkgMatch>] [-o|--output='$n'] <assetTypeMatch> <assetNameMatch>
        if (strcmp(list->word->word, "manifestGet")==0) {
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

            bgtracePop(); bgtrace1(1,"### ENDING bgCore_builtin(%s)\n",label);
            return EXECUTION_SUCCESS;
        }

        // ### Debugging and tests ##################################################################################################################

        // bgCore testAssertError
        if (strcmp(list->word->word, "testAssertError")==0) {
            testAssertError(list->next);
            bgtracePop(); bgtrace1(1,"### ENDING bgCore_builtin(%s)\n",label);
            return EXECUTION_SUCCESS;
        }

        // bgCore transTest
        if (strcmp(list->word->word, "transTest")==0) {
            printf("START '%s'\n",WordList_toString(list));
            list = list->next;
            while (list && *list->word->word=='-') {
                printf("###ONE\n");
                char* optArg;
                char* param = list->word->word;
                if ((optArg=BGCheckOpt("-f*|--file=*", &list)))
                    printf("   HIT '%s' is '%s'\n", param,optArg);
                else if ((optArg=BGCheckOpt("-a|--append", &list)))
                    printf("   HIT '%s' is '%s'\n", param,optArg);
                list = list->next;
            }
            printf("remainder = '%s'\n",WordList_toString(list));

            bgtracePop(); bgtrace1(1,"### ENDING bgCore_builtin(%s)\n",label);
            return EXECUTION_SUCCESS;
        }

        assertError(NULL, "error: command not recognized cmd='%s'\n", (list && list->word)?list->word->word:"");
        return (EXECUTION_FAILURE);
    }

    return (EXECUTION_FAILURE);
}


/* Called when `bgCore' is enabled and loaded from the shared object.  If this
   function returns 0, the load fails. */
int bgCore_builtin_load (char* name)
{
    bgtraceOn();
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
    char* label = ""; if (!label);
    bgtrace1(1,"### STR import_builtin(%s)\n", label=WordList_toString(args)); bgtracePush();

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

    int ret = importBashLibrary(scriptName, importFlags, &scriptPath);

    if (importFlags&im_getPathFlag) {
        if (args)
            ShellVar_setS(args->word->word, scriptPath);
        else
            printf("%s\n",scriptPath);
    }
    if (scriptPath) xfree(scriptPath);

     bgtracePop(); bgtrace1(2,"### END import_builtin(%s)\n", label);
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
