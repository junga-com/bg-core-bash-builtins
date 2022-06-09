
#include "bg_bashAPI.h"

#include <execute_cmd.h>

#include "BGString.h"

jmp_buf assertErrorJmpPoint;

// When a builtin function calls assertError, it may or may not return from the assertError call. If the script has a Try/Catch
// block in the same PID as the builtin is running, then it will return and all the C functions on the stack has to return back up.
// I wonder if its possible to change this to do a long jump back to the bgCore() function and then unwind the stack using the
// begin_unwind_frame mechanism.
// If the assertError is not being caught, then our PID will be killed and we do not need to worry b/c it will not return
// If the assertError is being caught, it will kill any intermediate PIDs and then send the PID with the Try/Catch a SIG2. If this
// builtin code is running in that PID, the C code will continue to run until it returns and then the SIG2 handler will be called
// which installs a DEBUG handler which will skip code until it finds the Catch:
int assertError(WORD_LIST* opts, char* fmt, ...)
{
    WORD_LIST* args = NULL;

    // format the msg and make it the last item in args (we build args backwards)
    BGString msg;    BGString_init(&msg, 100);
    va_list vargs;   SH_VA_START (vargs, fmt);
    BGString_appendfv(&msg, "", fmt, vargs);
    args = make_word_list( make_word(msg.buf) , args);

    // now add the opts to the front
    args = WordList_join(opts, args);

    // and lastly add the funcname we are calling (b/c execute_shell_function burns the first arg)
    args = make_word_list( make_word("assertError") , args);

    __bgtrace("!!! assertError in builtin: %s\n", msg.buf);
    BGString_free(&msg);

    _bgtraceStack();

    SHELL_VAR* func = ShellFunc_find("assertError");
    execute_shell_function(func, args);

    // TODO: implement the run_unwind_frame mechanism
    // run_unwind_frame("bgAssertError")
    longjmp(assertErrorJmpPoint, 36);
    return 36;
}

void bgWarn(char* fmt, ...)
{
    // format the msg and make it the last item in args (we build args backwards)
    BGString msg;    BGString_init(&msg, 100);
    va_list vargs;   SH_VA_START (vargs, fmt);
    BGString_appendfv(&msg, "", fmt, vargs);

    __bgtrace("!!! WARNING in builtin: %s\n", msg.buf);
    BGString_free(&msg);

    __bgtrace("\t");
    _bgtraceStack();
}

SHELL_VAR* ShellVar_create(char* varname)
{
    if (shell_variables == global_variables)
        return bind_global_variable(varname, NULL, ASS_FORCE);
    else
        return make_local_variable(varname,0);
}

void ShellVar_setS(char* varname, char* value)
{
    if (valid_array_reference(varname,VA_NOEXPAND))
        assign_array_element(varname,value,0);
    else {
        SHELL_VAR* var = ShellVar_create(varname);
        bind_variable_value(var,value,0);
    }
}


SHELL_VAR* ShellVar_refCreate(char* varname)
{
    SHELL_VAR* var = ShellVar_create(varname);
    VSETATTR(var, att_nameref);
    return var;
}

SHELL_VAR* ShellVar_refCreateSet(char* varname, char* value)
{
    SHELL_VAR* var = ShellVar_create(varname);
    VSETATTR(var, att_nameref);
    bind_variable_value(var,value,0);
    return var;
}

SHELL_VAR* ShellVar_arrayCreate(char* varname)
{
    if (shell_variables == global_variables)
        return make_new_array_variable(varname);
    else
        return make_local_array_variable(varname,0);
}


// this does not use array_to_word_list() because the WORD_LIST used in arrays dont use the same allocation as in the rest
WORD_LIST* ShellVar_arrayToWordList(SHELL_VAR* var)
{
    WORD_LIST* ret=NULL;
    if (array_p(var)) {
        for (ARRAY_ELEMENT* el = (array_cell(var))->head->prev; el!=(array_cell(var))->head; el=el->prev ) {
            ret = make_word_list(make_word(el->value), ret);
        }
    } else if (assoc_p(var)) {

    } else {
        ret = make_word_list(make_word(ShellVar_get(var)), ret);
    }
    return ret;
}

SHELL_VAR* ShellVar_assocCreate(char* varname)
{
    if (shell_variables == global_variables)
        return make_new_assoc_variable(varname);
    else
        return make_local_assoc_variable(varname, 0);
}



SHELL_VAR* ShellFunc_findWithSuffix(char* funcname, char* suffix)
{
    char* buf = save2string(funcname, suffix);
    SHELL_VAR* vVar = find_function(buf);
    xfree(buf);
    return vVar;
}

// the first arg is the shell function name
int ShellFunc_executeS(WORD_LIST* args)
{
    if (!args)
        return assertError(NULL,"ShellFunc_execute called without a function name in args");

    char* funcname = args->word->word;
    SHELL_VAR* func = ShellFunc_find(funcname);
    if (!func)
        return assertError(NULL,"ShellFunc_execute: could not find function '%s'",funcname);

    ShellVar_unsetS("catch_errorCode");
    int ret = execute_shell_function(func, args);
    if (ShellVar_findGlobal("catch_errorCode"))
        return 34;
    return ret;
}

int ShellFunc_execute(SHELL_VAR* func, WORD_LIST* args)
{
    if (!args)
        return assertError(NULL,"ShellFunc_execute called with empty args");

    ShellVar_unsetS("catch_errorCode");
    int ret = execute_shell_function(func, args);
    if (ShellVar_findGlobal("catch_errorCode"))
        return 34;
    return ret;
}





SHELL_VAR* ShellVar_find(char* varname)
{
    SHELL_VAR* retVal = find_variable(varname);
    // if varname is an uninitialized nameref, find_variable(varname) returns NULL because it follows the nameref even when its
    // invisible
    if (!retVal)
        retVal = find_variable_noref(varname);
    return retVal;
}

SHELL_VAR* ShellVar_findWithSuffix(char* varname, char* suffix)
{
    char* buf = xmalloc(strlen(varname)+strlen(suffix)+1);
    strcpy(buf, varname);
    strcat(buf,suffix);
    SHELL_VAR* vVar = find_variable(buf);
    xfree(buf);
    return vVar;
}

SHELL_VAR* ShellVar_findUpVar(char* varname)
{
    if (ShellAtGlobalScope())
        assertError(NULL, "can not use ShellVar_findUpVar() function at global scope (only use it in a function)");

    // note that we pass in shell_variables->down so that we skip variables declared in the current function's scope
    SHELL_VAR* var = var_lookup(varname, shell_variables->down);

    // if its a nameref that is already set, follow it to the real variable
    if (var && nameref_p(var) && (!invisible_p(var)) )
        var = find_variable_nameref(var);
    return var;
}


void ShellVar_assocCopyElements(HASH_TABLE* dest, HASH_TABLE* source)
{
    register int i;
    BUCKET_CONTENTS *item;
    if (source != 0 && HASH_ENTRIES (source) != 0) {
        for (i = 0; i < source->nbuckets; i++) {
            for (item = hash_items (i, source); item; item = item->next) {
                assoc_insert(dest, savestring(item->key), item->data);
            }
        }
    }
}


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// WordList
// native WORD_LIST functions are mostly in make_cmd.h

char* WordList_shift(WORD_LIST** list)
{
    WORD_LIST* front = (*list);
    (*list) = (*list)->next;
    front->next = NULL;
    char* retVal = savestring(front->word->word);
    WordList_free(front);
    return retVal;
}

void WordList_shiftFree(WORD_LIST** list, int count)
{
    WORD_LIST* front = (*list);
    while (count-- > 0 && (*list)) {
        WORD_LIST* tmp = (*list);
        (*list) = (*list)->next;
        if (count <= 0)
            tmp->next = NULL;
    }
    WordList_free(front);
}

void WordList_freeUpTo(WORD_LIST** list, WORD_LIST* stop)
{
    WORD_LIST* tmp = (*list);
    int loopProtector = 100;
    while (tmp->next != stop && loopProtector-- > 0)
        tmp = tmp->next;
    if (loopProtector <=0)
        assertError(NULL, "WordList_freeUpTo: 'stop' is not within 100 elements of the list 'list'");
    tmp->next = NULL;
    WordList_free((*list));
    (*list) = NULL;
}

WORD_LIST* WordList_copy(WORD_LIST* src)
{
    WORD_LIST* dst = (src) ? make_word_list(make_word(src->word->word),NULL) : NULL;
    WORD_LIST* tail = dst;
    src = (src) ? src->next : NULL;
    while (src) {
        tail->next = make_word_list(make_word(src->word->word),NULL);
        tail = tail->next;
        src = src->next;
    }
    return dst;
}

WORD_LIST* WordList_copyR(WORD_LIST* src)
{
    WORD_LIST* dst = NULL;
    while (src) {
        dst = make_word_list(make_word(src->word->word),dst);
        src = src->next;
    }
    return dst;
}

WORD_LIST* WordList_join(WORD_LIST* args1, WORD_LIST* args2)
{
    if (!args1)
        return args2;
    if (!args2)
        return args1;

    WORD_LIST* t=args1; while (t && t->next) t=t->next;
    t->next = args2;

    return args1;
}



/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// AssocItr

void AssocItr_init(AssocItr* pI, HASH_TABLE* pTbl)
{
    pI->table = pTbl;
    pI->position=0;
    pI->item=NULL;
    pI->item = NULL; //AssocItr_next(pI);
}

BUCKET_CONTENTS* AssocItr_first(AssocItr* pI, HASH_TABLE* pTbl)
{
    AssocItr_init(pI, pTbl);
    return AssocItr_next(pI);
}

BUCKET_CONTENTS* AssocItr_next(AssocItr* pI)
{
    // if we are not done iterating the current bucket, just do that
    if (pI->item && pI->item->next) {
        pI->item = pI->item->next;
    // else, goto the start of the linked list of the next non-empty bucket
    } else if (HASH_ENTRIES (pI->table) != 0 && pI->table && pI->position < pI->table->nbuckets) {
        while ((pI->position < pI->table->nbuckets) && 0==(pI->item=hash_items(pI->position, pI->table))) pI->position++;
        pI->position++;
    // else, nothing is left
    } else {
        pI->item = NULL;
    }
    return pI->item;
}

BUCKET_CONTENTS* AssocItr_peek(AssocItr* pI)
{
    // if we are not done iterating the current bucket, just do that
    if (pI->item && pI->item->next) {
        return pI->item->next;
    // else, goto the start of the linked list of the next non-empty bucket
    } else if (HASH_ENTRIES (pI->table) != 0 && pI->table && pI->position < pI->table->nbuckets) {
        int pos2 = pI->position;
        BUCKET_CONTENTS* item = NULL;
        while ((pos2 < pI->table->nbuckets) && 0==(item=hash_items(pos2, pI->table))) pos2++;
        return item;
    }
    return NULL;
}


char* vcFlagsToString(VAR_CONTEXT* vc)
{
    // #define VC_HASLOCAL	0x01
    // #define VC_HASTMPVAR	0x02
    // #define VC_FUNCENV	0x04	/* also function if name != NULL */
    // #define VC_BLTNENV	0x08	/* builtin_env */
    // #define VC_TEMPENV	0x10	/* temporary_env */
    // #define VC_TEMPFLAGS	(VC_FUNCENV|VC_BLTNENV|VC_TEMPENV)
    // #define vc_istempenv(vc)	(((vc)->flags & (VC_TEMPFLAGS)) == VC_TEMPENV)
    // #define vc_istempscope(vc)	(((vc)->flags & (VC_TEMPENV|VC_BLTNENV)) != 0)

    BGString retVal;
    BGString_init(&retVal, 500);
    BGString_appendf(&retVal, "", "(%0.2X) ", vc->flags);

    if (vc->flags&VC_HASLOCAL)  BGString_append(&retVal,"VC_HASLOCAL",",");
    if (vc->flags&VC_HASTMPVAR) BGString_append(&retVal,"VC_HASTMPVAR",",");
    if (vc->flags&VC_FUNCENV)   BGString_append(&retVal,"VC_FUNCENV",",");
    if (vc->flags&VC_BLTNENV)   BGString_append(&retVal,"VC_BLTNENV",",");
    if (vc->flags&VC_TEMPENV)   BGString_append(&retVal,"VC_TEMPENV",",");

    if (vc_istempenv(vc))   BGString_append(&retVal,"vc_istempenv",",");
    if (vc_istempscope(vc)) BGString_append(&retVal,"vc_istempscope",",");
    return retVal.buf;
}

#define MAX(a, b) (((a) < (b))? (b) : (a))

void ShellContext_dump(VAR_CONTEXT* startCntx, int includeVars)
{
    // typedef struct var_context {
    //   char *name;		/* empty or NULL means global context */
    //   int scope;		/* 0 means global context */
    //   int flags;
    //   struct var_context *up;	/* previous function calls */
    //   struct var_context *down;	/* down towards global context */
    //   HASH_TABLE *table;		/* variables at this scope */
    // } VAR_CONTEXT;
    int maxCntxNameLen = 0;
    for (VAR_CONTEXT* cntx=startCntx; cntx;  cntx=cntx->down) {
        maxCntxNameLen = MAX(maxCntxNameLen, (cntx->name)?strlen(cntx->name):0 );
    }

    char* label = "VAR_CONTEXT";
    for (VAR_CONTEXT* cntx=startCntx; cntx;  cntx=cntx->down) {
        char* flagStr = vcFlagsToString(cntx);
        __bgtrace("%11s: '%-*s' scope='%d'(%s) up='%-4s'  varCount=%3d  flags='%s'\n",
            label,
            maxCntxNameLen, cntx->name,
            cntx->scope,
            (cntx==global_variables)?"GBL":((cntx==shell_variables)?"STR":"mid"),
            (cntx->up)?"HAS":"null",
            HASH_ENTRIES(cntx->table),
            flagStr);
        if (includeVars) {
            AssocItr itr; AssocItr_init(&itr, cntx->table);
            BUCKET_CONTENTS* bVar;
            int maxVarnameLen = 0;
            int count = (includeVars==1)?20:includeVars;
            while ((count-->0) && (bVar = AssocItr_next(&itr))) {
                maxVarnameLen = MAX(maxVarnameLen, strlen(bVar->key));
            }
            AssocItr_init(&itr, cntx->table);
            count = (includeVars==1)?20:includeVars;
            while ((count-->0) && (bVar = AssocItr_next(&itr))) {
                // typedef struct variable {
                //   char *name;			/* Symbol that the user types. */
                //   char *value;			/* Value that is returned. */
                //   char *exportstr;		/* String for the environment. */
                //   sh_var_value_func_t *dynamic_value;	/* Function called to return a `dynamic'
                // 				   value for a variable, like $SECONDS
                // 				   or $RANDOM. */
                //   sh_var_assign_func_t *assign_func; /* Function called when this `special
                // 				   variable' is assigned a value in
                // 				   bind_variable. */
                //   int attributes;		/* export, readonly, array, invisible... */
                //   int context;			/* Which context this variable belongs to. */
                // } SHELL_VAR;

                SHELL_VAR* pVar = (SHELL_VAR*)bVar->data;
                char* flagStr = ShellVarFlagsToString(pVar->attributes);
                __bgtrace("   %*s%-*s : %s\n",
                    maxCntxNameLen,"",
                    maxVarnameLen, bVar->key,
                    flagStr
                );
                xfree(flagStr);
            }
        }
        label = "";
        xfree(flagStr);
    }
}


char* BGCheckOpt(char* spec, WORD_LIST** pArgs)
{
    if (!(*pArgs))
        return NULL;
    char* p = spec;
    char* param = (*pArgs)->word->word;
    int slen;
    do {
        char* s = p;
        if (*s != '-' && *s != '+')
            return 0;
        char* e = s;
        while (e && *e && (strchr("=*|",*e)==NULL) ) e++;
        switch (*e) {
            // match with an argument (signified with <opt>* or <opt>=)
            case '=':
            case '*':
                slen = (e-s);
                if (strncmp(param,s, slen)==0) {
                    if (param[slen]!='\0') {
                        if (param[slen]=='=')
                            slen++;
                        return param + slen;
                    } else {
                        (*pArgs) = (*pArgs)->next;
                        return (*pArgs) ? (*pArgs)->word->word : "";
                    }
                }
                break;
            // match without an argument ( no *, nor =)
            case '\0':
            case '|':
                if (strncmp(param,s, (e-s) )==0 ) {
                    return "true";
                }
                break;
        }
        for (p = e; *p && (strchr("=*|",*p)); p++);
    } while (p && (*p=='-'));
    return 0;
}

void outputValue(BGRetVar* retVar, char* value)
{
    if (!retVar || retVar->type==rt_echo) {
        printf("%s%s",value, (retVar && retVar->delim) ? retVar->delim : "\n");
        return;
    }

    char* delim = (retVar && retVar->delim) ? retVar->delim : ifs_firstchar(NULL);

    switch (retVar->type) {
        case rt_arrayRef:
        case rt_simple:
            if (!retVar->appendFlag)
                if (retVar->type==rt_arrayRef)
                    ShellVar_setS(retVar->arrayRef, value);
                else
                    ShellVar_set(retVar->var, value);
            else {
                char* oldval = ShellVar_get(retVar->var);
                BGString newVal; BGString_init(&newVal,strlen(oldval)+ strlen(delim) + strlen(value) + 1 );
                BGString_copy(&newVal, oldval);
                BGString_append(&newVal, value, delim);
                if (retVar->type==rt_arrayRef)
                    ShellVar_setS(retVar->arrayRef, newVal.buf);
                else if (array_p(retVar->var))
                    ShellVar_arraySetI(retVar->var, 0, newVal.buf);
                else if (assoc_p(retVar->var))
                    ShellVar_assocSet(retVar->var, "0", newVal.buf);
                else
                    ShellVar_set(retVar->var, newVal.buf);
                BGString_free(&newVal);
            }
            break;
        case rt_array:
            if (!retVar->appendFlag)
                ShellVar_arrayClear(retVar->var);
            ShellVar_arrayPush(retVar->var, value);
            break;
        case rt_set:
            if (!retVar->appendFlag)
                ShellVar_assocClear(retVar->var);
            // we use the assoc array as a set by putting our value(s) in the index and the value of the index can be "" (or anything)
            ShellVar_assocSet(retVar->var, value, "");
            break;
        case rt_echo: break;
        case rt_noop: break;
    }
}

void outputValues(BGRetVar* retVar, WORD_LIST* values)
{
    char* ifsChar = NULL;
    char* delim = (retVar && retVar->delim) ? retVar->delim : (ifsChar=ifs_firstchar(NULL));

    if (!retVar || retVar->type==rt_echo) {
        for (WORD_LIST* lp=values; lp; lp=lp->next) {
            printf("%s%s", lp->word->word, (lp->next)?delim:"");
        }
        printf("\n");
        xfree(ifsChar);
        return;
    }

    char* oldval;
    switch (retVar->type) {
        case rt_arrayRef:
        case rt_simple:
            oldval = (retVar->appendFlag) ? ShellVar_get(retVar->var) : "";
            int allocSize = 1 + strlen(oldval);
            int delimLen = strlen(delim);
            for (WORD_LIST* lp=values; lp; lp=lp->next)
                allocSize += (strlen(lp->word->word) + ((lp->next)?delimLen:0));

            BGString newVal; BGString_init(&newVal, allocSize);
            BGString_copy(&newVal, oldval);
            for (WORD_LIST* lp=values; lp; lp=lp->next)
                BGString_append(&newVal, lp->word->word, delim);
            if (retVar->type==rt_arrayRef)
                ShellVar_setS(retVar->arrayRef, newVal.buf);
            else if (array_p(retVar->var))
                ShellVar_arraySetI(retVar->var, 0, newVal.buf);
            else if (assoc_p(retVar->var))
                ShellVar_assocSet(retVar->var, "0", newVal.buf);
            else
                ShellVar_set(retVar->var, newVal.buf);
            BGString_free(&newVal);
            break;
        case rt_array:
            if (!retVar->appendFlag)
                ShellVar_arrayClear(retVar->var);
            for (WORD_LIST* lp=values; lp; lp=lp->next)
                ShellVar_arrayPush(retVar->var, lp->word->word);
            break;
        case rt_set:
            if (!retVar->appendFlag)
                ShellVar_assocClear(retVar->var);
            // we use the assoc array as a set by putting our value(s) in the index and the value of the index can be "" (or anything)
            for (WORD_LIST* lp=values; lp; lp=lp->next)
                ShellVar_assocSet(retVar->var, lp->word->word, "");
            break;
        case rt_echo: break;
        case rt_noop: break;
    }
    xfree(ifsChar);
}


void BGRetVar_init(BGRetVar* this)
{
    this->var = NULL;
    this->arrayRef = NULL;
    this->type = rt_echo;
    this->appendFlag = 0;
    this->delim = NULL;
}

void BGRetVar_initFromVarname(BGRetVar* this, char* varname)
{
    if (!varname || !(*varname)) {
        this->type = rt_echo;
        this->var = NULL;
        this->arrayRef = NULL;
    } else if (valid_array_reference(varname, VA_NOEXPAND)) {
        this->type = rt_arrayRef;
        this->var = NULL;
        this->arrayRef = varname;
    } else {
        this->type = rt_simple;
        this->var = ShellVar_findUpVar(varname);
        this->arrayRef = NULL;
    }
    this->appendFlag = 0;
    this->delim = NULL;
}

BGRetVar* BGRetVar_new()
{
    BGRetVar* this = xmalloc(sizeof(*this));
    this->var = NULL;
    this->arrayRef = NULL;
    this->type = rt_echo;
    this->appendFlag = 0;
    this->delim = NULL;
    return this;
}

int BGRetVar_initFromOpts(BGRetVar** retVar, WORD_LIST** pArgs)
{
    if (! (*pArgs))
        return 0;

    char* optArg = NULL;
    int found = 0;

    if        ((optArg=BGCheckOpt("-a|--append", pArgs))) {
        if (!(*retVar)) (*retVar) = BGRetVar_new();
        found = 1;
        (*retVar)->appendFlag = 1;
    } else if ((optArg=BGCheckOpt("+a|++append", pArgs))) {
        if (!(*retVar)) (*retVar) = BGRetVar_new();
        found = 1;
        (*retVar)->appendFlag = 0;

    } else if ((optArg=BGCheckOpt("-d*|--delim=*|--retVar=*", pArgs))) {
        if (!(*retVar)) (*retVar) = BGRetVar_new();
        found = 1;
        (*retVar)->delim = optArg;

    } else if ((optArg=BGCheckOpt("-1|", pArgs))) {
        if (!(*retVar)) (*retVar) = BGRetVar_new();
        found = 1;
        (*retVar)->delim = "\n";

    } else if ((optArg=BGCheckOpt("+1|", pArgs))) {
        if (!(*retVar)) (*retVar) = BGRetVar_new();
        found = 1;
        (*retVar)->delim = " ";

    } else if ((optArg=BGCheckOpt("-R*|--string=*|--retVar=*", pArgs))) {
        if (!(*retVar)) (*retVar) = BGRetVar_new();
        found = 1;
        if (*optArg=='\0') {
            (*retVar)->type = rt_noop;
            bgWarn("BGRetVar_initFromOpts(): Return variable -R '%s' is empty. nothing will be returned\n\targs='%s'", optArg, WordList_toString(*pArgs));
        } else if (valid_array_reference(optArg, VA_NOEXPAND)) {
            (*retVar)->type = rt_arrayRef;
            (*retVar)->arrayRef = optArg;
        } else {
            (*retVar)->type = rt_simple;
            (*retVar)->var = ShellVar_findUpVar(optArg);
            if (!(*retVar)->var) {
                bgWarn("BGRetVar_initFromOpts(): Return variable -R '%s' does not exist in the calling scope\n\targs='%s'", optArg, WordList_toString(*pArgs));
                (*retVar)->var = ShellVar_createGlobal(optArg);
            }
        }

    } else if ((optArg=BGCheckOpt("-A*|--array=*|--retArray=*", pArgs))) {
        if (!(*retVar)) (*retVar) = BGRetVar_new();
        found = 1;
        (*retVar)->type = rt_array;
        (*retVar)->var = ShellVar_findUpVar(optArg);
        if (*optArg=='\0') {
            (*retVar)->type = rt_noop;
            bgWarn("BGRetVar_initFromOpts(): Return variable -A '%s' is empty. nothing will be returned\n\targs='%s'", optArg, WordList_toString(*pArgs));
        } else if (!(*retVar)->var) {
            bgWarn("BGRetVar_initFromOpts(): Return variable -A '%s' does not exist in the calling scope\n\targs='%s'", optArg, WordList_toString(*pArgs));
            (*retVar)->var = ShellVar_arrayCreateGlobal(optArg);
        }
        if (assoc_p((*retVar)->var))
            assertError(NULL, "BGRetVar_initFromOpts(): Return variable -A '%s' is expected to be an array or a simple variable that can be converted to an array but it is an associative array which is not compatible. Use -S*|--set=* if you intend to return values in the indexes of an associative array\n",optArg);
        else if (!array_p((*retVar)->var))
            convert_var_to_array((*retVar)->var);

    } else if ((optArg=BGCheckOpt("-S*|--set=*", pArgs))) {
        if (!(*retVar)) (*retVar) = BGRetVar_new();
        found = 1;
        (*retVar)->type = rt_set;
        (*retVar)->var = ShellVar_findUpVar(optArg);
        if (*optArg=='\0') {
            (*retVar)->type = rt_noop;
            bgWarn("BGRetVar_initFromOpts(): Return variable -S '%s' is empty. nothing will be returned\n\targs='%s'", optArg, WordList_toString(*pArgs));
        } else if (!(*retVar)->var) {
            bgWarn("BGRetVar_initFromOpts(): Return variable -S '%s' does not exist in the calling scope\n\targs='%s'", optArg, WordList_toString(*pArgs));
            (*retVar)->var = ShellVar_assocCreateGlobal(optArg);
        }
        if (!assoc_p((*retVar)->var))
            assertError(NULL, "BGRetVar_initFromOpts(): Return variable '%s' is expected to be an associative array (local -A)\n",optArg);

    } else if ((optArg=BGCheckOpt("-e|--echo", pArgs))) {
        if (!(*retVar)) (*retVar) = BGRetVar_new();
        found = 1;
        (*retVar)->type = rt_echo;
    }

    return found;
}



char* ShellVarFlagsToString(int flags) {
    BGString retVal;
    BGString_init(&retVal, 500);
    BGString_appendf(&retVal, "", "(%0.2X) ", flags);

    if (flags&att_exported)   BGString_append(&retVal, "exported", ",");
    if (flags&att_readonly)   BGString_append(&retVal, "readonly", ",");
    if (flags&att_array)      BGString_append(&retVal, "array", ",");
    if (flags&att_function)   BGString_append(&retVal, "function", ",");
    if (flags&att_integer)    BGString_append(&retVal, "integer", ",");
    if (flags&att_local)      BGString_append(&retVal, "local", ",");
    if (flags&att_assoc)      BGString_append(&retVal, "assoc", ",");
    if (flags&att_trace)      BGString_append(&retVal, "trace", ",");
    if (flags&att_uppercase)  BGString_append(&retVal, "uppercase", ",");
    if (flags&att_lowercase)  BGString_append(&retVal, "lowercase", ",");
    if (flags&att_capcase)    BGString_append(&retVal, "capcase", ",");
    if (flags&att_nameref)    BGString_append(&retVal, "nameref", ",");
    if (flags&att_invisible)  BGString_append(&retVal, "invisible", ",");
    if (flags&att_nounset)    BGString_append(&retVal, "nounset", ",");
    if (flags&att_noassign)   BGString_append(&retVal, "noassign", ",");
    if (flags&att_imported)   BGString_append(&retVal, "imported", ",");
    if (flags&att_special)    BGString_append(&retVal, "special", ",");
    if (flags&att_nofree)     BGString_append(&retVal, "nofree", ",");
    if (flags&att_regenerate) BGString_append(&retVal, "regenerate", ",");
    if (flags&att_tempvar)    BGString_append(&retVal, "tempvar", ",");
    if (flags&att_propagate)  BGString_append(&retVal, "propagate", ",");
    return retVal.buf;
}
