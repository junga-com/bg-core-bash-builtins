/* bgCore - loadable builtin to optimize bg_objects.sh  */

/* See Makefile for compilation details. */

#include "bg_objects.h"

#include "bg_bashAPI.h"
#include "bg_json.h"
#include "bg_manifest.h"
#include "bg_import.h"





/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// bgCore ConstructObjectFromJson <objVar>
int ConstructObjectFromJson(WORD_LIST* list)
{
//    begin_unwind_frame("bgCore");

    if (!list || list_length(list) < 2) {
        setErrorMsg("Error - <objVar> and <restoreFile> are required arguments to ConstructObjectFromJson. See usage..\n\n");
        return (EX_USAGE);
    }

    char* objVar = list->word->word;
    list = list->next;
    SHELL_VAR* vObjVar = ShellVar_find(objVar);
    if (!vObjVar) {
        ShellVar_refCreate(objVar);
    }
    VUNSETATTR(vObjVar, att_invisible);

    char* jsonFile = list->word->word;
    list = list->next;

    JSONScanner* scanner = JSONScanner_newFromFile(jsonFile);
    if (! scanner)
        return setErrorMsg("could not open input file '%s' for reading\n", jsonFile);

    JSONToken* jValue = JSONScanner_getValue(scanner);
    if (jValue->type == jt_error) {
        fprintf(stderr, "%s\n", jValue->value);
        return EXECUTION_FAILURE;

    } else if (!JSONType_isAValue(jValue->type)) {
        fprintf(stderr, "error: Expected a JSON value but got token='%s(%s)', \n", JSONTypeToString(jValue->type), jValue->value);
        return EXECUTION_FAILURE;

    // we found an object
    } else if (JSONType_isBashObj(jValue->type) && nameref_p(vObjVar)) {
        ShellVar_set(vObjVar, ((BashObj*)jValue->value)->name);
    } else if (JSONType_isBashObj(jValue->type) && (array_p(vObjVar)) ) {
        ShellVar_arraySetI(vObjVar, 0, ((BashObj*)jValue->value)->ref);
    } else if (JSONType_isBashObj(jValue->type) && (assoc_p(vObjVar)) ) {
        ShellVar_assocSet(vObjVar, "0", ((BashObj*)jValue->value)->ref);
    } else if (JSONType_isBashObj(jValue->type) ) {
        ShellVar_set(vObjVar, ((BashObj*)jValue->value)->ref);

    // we found a primitive
    } else if (nameref_p(vObjVar)) {
        // the user provided a nameref for objVar but the json data value is not an object or array so we create a heap_ var for
        // the simple value which the nameref can point to
        SHELL_VAR* transObjVar = varNewHeapVar("");
        ShellVar_set(vObjVar, transObjVar->name);
    } else if (array_p(vObjVar)) {
        ShellVar_arraySetI(vObjVar, 0, jValue->value);
    } else if (assoc_p(vObjVar)) {
        ShellVar_assocSet(vObjVar, "0", jValue->value);
    } else {
        ShellVar_set(vObjVar, jValue->value);
    }

//    discard_unwind_frame ("bgCore");
    return EXECUTION_SUCCESS;
}

int Object_fromJSON(WORD_LIST* args)
{
    JSONScanner* scanner;
    if (args)
        scanner = JSONScanner_newFromFile(args->word->word);
    else
        scanner = JSONScanner_newFromStream(1);

    BashObj* pObj = BashObj_find("this", NULL,NULL);
    //JSONToken* endToken = JSONScanner_getObject(scanner, pObj);
    JSONScanner_getObject(scanner, pObj);
    return EXECUTION_SUCCESS;
}

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


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// bgCore <cmd> ....
// This is the entry point builtin function. It dispatches the call to the specific function based on the first command word
int bgCore_builtin(WORD_LIST* list)
{
    bgtrace1(1,"### STRing bgCore_builtin(%s)\n", WordList_toString(list));
    bgtracePush();
    if (!list || !list->word) {
        printf ("Error - <cmd> is a required argument. See usage..\n\n");
        builtin_usage();
        bgtracePop();
        bgtrace0(1,"### ENDING bgCore_builtin()\n");
        return (EX_USAGE);
    }

    // bgCore _bgclassCall <oid> <refClass> <hierarchyLevel> |<objSyntaxStart> [<p1,2> ... <pN>]
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

        bgtracePop();
        bgtrace0(1,"### ENDING 1 bgCore_builtin()\n");
        return EXECUTION_SUCCESS;
    }


    // bgCore _bgclassCall <oid> <refClass> <hierarchyLevel> |<objSyntaxStart> [<p1,2> ... <pN>]
    if (strcmp(list->word->word, "_bgclassCall")==0) {
        int ret = _bgclassCall(list->next);
        bgtracePop();
        bgtrace0(1,"### ENDING 1 bgCore_builtin()\n");
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
        bgtracePop();
        bgtrace0(1,"### ENDING 2 bgCore_builtin()\n");
        return result;
    }

    // bgCore Object_fromJSON
    if (strcmp(list->word->word, "Object_fromJSON")==0) {
        int ret = Object_fromJSON(list->next);
        bgtracePop();
        bgtrace0(1,"### ENDING 3 bgCore_builtin()\n");
        return ret;
    }


    // bgCore ConstructObject
    if (strcmp(list->word->word, "ConstructObject")==0) {
        ConstructObject(list->next);
        bgtracePop();
        bgtrace0(1,"### ENDING 3.5 bgCore_builtin()\n");
        return EXECUTION_SUCCESS;
    }



    // bgCore ConstructObjectFromJson
    if (strcmp(list->word->word, "ConstructObjectFromJson")==0) {
        int ret = ConstructObjectFromJson(list->next);
        bgtracePop();
        bgtrace0(1,"### ENDING 4 bgCore_builtin()\n");
        return ret;
    }

    fprintf(stderr, "error: command not recognized cmd='%s'\n", (list && list->word)?list->word->word:"");
    bgtracePop();
    bgtrace0(1,"### ENDING 6 bgCore_builtin()\n");
    return (EX_USAGE);
}


/* Called when `bgCore' is enabled and loaded from the shared object.  If this
   function returns 0, the load fails. */
int bgCore_builtin_load (char* name)
{
    bgtraceOn();
    _bgtrace(0,"LOAD ############################################################################################\n");
    return (1);
}

/* Called when `bgCore' is disabled. */
void bgCore_builtin_unload (char* name)
{
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
