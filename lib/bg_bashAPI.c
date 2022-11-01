
#include "bg_bashAPI.h"

#include <execute_cmd.h>
#include <errno.h>
#include <sys/stat.h>

#include "BGString.h"
#include "bg_json.h"

static SHELL_VAR* hash_lookup(char* name, HASH_TABLE* hashed_vars)
{
	BUCKET_CONTENTS *bucket;
	bucket = hash_search(name, hashed_vars, 0);
	return (bucket ? (SHELL_VAR *)bucket->data : NULL);
}


CallFrame* callFrames = NULL;

int callFramesAllocSize = 0;
int callFramesPos = 0;

int callFrames_getPos()
{
	return callFramesPos;
}

CallFrame* callFrames_push()
{
	if (callFramesAllocSize <= 0) {
		callFramesAllocSize = 10;
		callFrames = xmalloc(callFramesAllocSize*sizeof(CallFrame));
	}
	if (callFramesAllocSize < (callFramesPos+1)) {
		callFramesAllocSize = 2*callFramesAllocSize;
		callFrames = xrealloc(callFrames, callFramesAllocSize*sizeof(CallFrame));
	}
	CallFrame* pFrm = &(callFrames[callFramesPos++]);
	pFrm->name = saprintf("bgCore-%d", callFramesPos-1);
	begin_unwind_frame(pFrm->name);
	return pFrm;
}
void callFrames_pop()
{
	if (callFramesPos<=0) {
		__bgtrace("!!! bgCore BUILTIN logic ERROR. trying to pop the CallFrame (aka jmpPoint) but none on the stack #########################################\n ");
		exit(205);
	}

	CallFrame* pFrm = &(callFrames[--callFramesPos]);
	discard_unwind_frame(pFrm->name);
	xfree(pFrm->name);
}

void callFrames_longjump(int exitCode)
{
	if (callFramesPos > 0) {
		__bgtrace("!!! LONGJMPing at '%d'\n", callFramesPos-1);

		CallFrame* pFrm = &(callFrames[--callFramesPos]);
		run_unwind_frame(pFrm->name);
		xfree(pFrm->name);

		longjmp(pFrm->jmpBuf, exitCode);
		__bgtrace("!!! WOOPS !!!! jump didnt jump!\n");
	}
	__bgtrace("!!! bgCore BUILTIN logic ERROR. trying to jump but none on the stack #########################################\n ");
	exit(205);
}


// This C function invokes the bash function assertError.  It may or may not return from the assertError bash function call. If the
// script has a Try/Catch block in the same PID as the builtin is running, then it will return and all the C functions on the stack
// have end so that the top level builtin function can return. We use setjmp/longjmp to resume at that top level builtin function
// and the unwind_prot.c module to clean up any resources.
//
// If the assertError is not being caught, then our PID will be killed and we do not need to worry b/c it will not return
// If the assertError is being caught, it will kill any intermediate PIDs and then send the PID with the Try/Catch a SIG2. If this
// builtin code is running in that PID, the C code will continue to run until it returns and then the SIG2 handler will be called
// which installs a DEBUG handler which will skip code until it finds the Catch:
int assertError(WORD_LIST* opts, char* fmt, ...)
{
	WORD_LIST* args = NULL;

	// executing_line_number()
	// shell_variables->name

	// format the msg and make it the last item in args (we build args backwards)
	BGString msg;    BGString_init(&msg, 100);
	va_list vargs;   SH_VA_START (vargs, fmt);
	BGString_appendfv(&msg, "", fmt, vargs);
	args = WordList_unshift(args, msg.buf);

	// now add the opts to the front
	args = WordList_join(opts, args);

	__bgtrace("!!! assertError in builtin: %s\n", msg.buf);
	BGString_free(&msg);

	_bgtraceStack();

	SHELL_VAR* func = ShellFunc_find("assertError");
	if (func)
		ShellFunc_execute(func, args);
	else
		__bgtrace("!!! ERROR: throwing an assertError from C builtin code but the 'assertError' shell function is not found");
	WordList_free(args);

	callFrames_longjump(36);
	return 36;
}

void bgWarn(char* fmt, ...)
{
	BGString msg;    BGString_init(&msg, 100);
	va_list vargs;   SH_VA_START (vargs, fmt);
	BGString_appendfv(&msg, "", fmt, vargs);

	__bgtrace("!!! WARNING in builtin: %s\n", msg.buf);
	BGString_free(&msg);

	__bgtrace("\t");
	_bgtraceStack();
}

static sighandler_t oldSIGSEGV = NULL;

void bgOnRecoverableFault(int sig_num)
{
	bgWarn("SEGFAUlT (%d) caught", sig_num);
	//assertError(NULL, "SEGFAUlT (%d) caught", sig_num);
	oldSIGSEGV(sig_num);
}

void assertError_init()
{
	oldSIGSEGV = signal(SIGSEGV, bgOnRecoverableFault); // <-- this one is for segmentation fault
}

void assertError_done()
{
	if (oldSIGSEGV)
		signal(SIGSEGV, oldSIGSEGV);
	oldSIGSEGV = NULL;
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
			ret = WordList_unshift(ret, el->value);
		}
	} else if (assoc_p(var)) {

	} else {
		ret = WordList_unshift(ret, ShellVar_get(var));
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
		assertError(NULL,"ShellFunc_execute called without a function name in args");

	char* funcname = args->word->word;
	SHELL_VAR* func = ShellFunc_find(funcname);
	if (!func)
		assertError(NULL,"ShellFunc_execute: could not find function '%s'",funcname);

	ShellVar_unsetS("catch_errorCode");

	int ret = execute_shell_function(func, args);

	char* exceptionTest = ShellVar_getS("catch_errorCode");
	if ( exceptionTest && *exceptionTest ) {
		__bgtrace("!!! ShellFunc_execute: detected assertError, errorCode='%s' descr='%s'\n",  ShellVar_getS("catch_errorCode"), ShellVar_getS("catch_errorDescription"));
		callFrames_longjump(36);
		return 34;
	}
	return ret;
}

int ShellFunc_execute(SHELL_VAR* func, WORD_LIST* args)
{
	WORD_LIST* argsWithFN = WordList_unshift(args, func->name);

	ShellVar_unsetS("catch_errorCode");

	int ret = execute_shell_function(func, argsWithFN);

	WordList_freeUpTo(&argsWithFN, args);
	char* exceptionTest = ShellVar_getS("catch_errorCode");
	if ( exceptionTest && *exceptionTest ) {
		__bgtrace("!!! ShellFunc_execute: detected assertError, errorCode='%s' descr='%s'\n",  ShellVar_getS("catch_errorCode"), ShellVar_getS("catch_errorDescription"));
		callFrames_longjump(36);
		return 34;
	}
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

	// if this varname exists in the current function scope, warn the user in the bgtrace output.
	// this C builtin implementation can correctly handle that case but if the bgCore builtin is not installed this would be a
	// silent error so warning gives the script author a chance to avoid the naming conflict.
	if (hash_lookup(varname, shell_variables->table))
		__bgtrace("WARNING: The variable name '%s' was passed to the function '%s' as a pass by reference argument but the function declares a local variable by that same name. The bgCore loadable builtin is currently loaded so this is working correctly but if it had not been loaded it would have resulted in the reference variable silently not being set. You should consider renaming the local variables of this function so that a conflict like this is not likely.\n", varname, shell_variables->name);

	// note that we pass in shell_variables->down so that we skip variables declared in the current function's scope
	SHELL_VAR* var = var_lookup(varname, shell_variables->down);

	// if its a nameref that is already set, follow it to the real variable
	char* refs[NAMEREF_MAX];
	refs[0] = varname;
	for (int count=1; count <= NAMEREF_MAX && var && nameref_p(var) && (!invisible_p(var)); count++) {
		refs[count] = nameref_cell(var);
		for (int i=0; i<count; i++)
			if (strcmp(refs[count], refs[i])==0) {
				char* refDesc = varname;
				for (int j=0; j<count; j++) {
					char* tmp = refDesc;
					refDesc = saprintf("%s%s%s", refDesc, (refDesc)?"->":"", refs[j]);
					xfree(tmp);
				}
				internal_error("Nameref circular reference. %s \n", refDesc);
			}
		var = var_lookup(refs[count], shell_variables->down);
	}

	// if it was not found, create it
	if (!var) {
		// it might be better to create the var specifically in the shell_variables->down scope but the bash implementation can not
		// do that so for now, lets create it in the global scope. If the bgCore builtin becomes more widely available we could change
		// this.
		__bgtrace("WARNING: The variable name '%s' was passed to the function '%s' as a pass by reference argument but that variable does not exist in the calling scope or any scope above that so its being created in the global scope.\n", varname, shell_variables->name);
		var = ShellVar_createGlobal(varname);
	}

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

// shift off the first word without freeing the node
char* WordList_shift(WORD_LIST** list)
{
	char* retVal = NULL;
	if (list && *list) {
		retVal = savestring((*list)->word->word);
		(*list) = (*list)->next;
	}
	return retVal;
}

void WordList_shiftFree(WORD_LIST** list, int count)
{
	WORD_LIST* front = (list) ? (*list) : NULL;
	while (count-- > 0 && list && (*list)) {
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

WORD_LIST* WordList_unshiftQ(WORD_LIST* list, char* word)
{
	char* wq = resaveWithQuotes(word,0);
	list = WordList_unshift(list, wq);
	xfree(wq);
	return list;
}


void  WordList_push(WORD_LIST** list, char* word)
{
	WORD_LIST* tmp = (*list);
	while (tmp && tmp->next)
		tmp = tmp->next;
	if (tmp)
		tmp->next = WordList_unshift(NULL, word);
	else
		(*list) = WordList_unshift(NULL, word);
}

char* WordList_pop(WORD_LIST** list)
{
	WORD_LIST* tmp = *list;
	while (tmp && tmp->next && tmp->next->next)
		tmp = tmp->next;
	char* ret = NULL;
	if (!tmp) {
	 	return ret;
	} else if (tmp->next) {
		ret = tmp->next->word->word;
		WordList_free(tmp->next);
	} else {
		ret = (*list)->word->word;
		WordList_free((*list));
	}
	return ret;
}


WORD_LIST* WordList_copy(WORD_LIST* src)
{
	WORD_LIST* dst = (src) ? WordList_unshift(NULL, src->word->word) : NULL;
	WORD_LIST* tail = dst;
	src = (src) ? src->next : NULL;
	while (src) {
		tail->next = WordList_unshift(NULL, src->word->word);
		tail = tail->next;
		src = src->next;
	}
	return dst;
}

WORD_LIST* WordList_copyR(WORD_LIST* src)
{
	WORD_LIST* dst = NULL;
	while (src) {
		dst = WordList_unshift(dst, src->word->word);
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
// WordDesc
// native WORD_DESC functions are mostly in make_cmd.h

WORD_DESC* WordDesc_surroundWord(WORD_DESC* w, char chStart, char chEnd)
{
	// "bob"
	// 0123456789
	//          .
	char* t = w->word;
	int len = bgstrlen(t);
	w->word = xmalloc(len+3); if (!w->word) assertError(NULL, "xmalloc failed in WordDesc_surroundWord");
	*w->word = chStart;
	if (t) {
		strcpy(w->word+1, t);
	}
	*(w->word+len+1) = chEnd;
	*(w->word+len+2) = '\0';
	xfree(t);
	return w;
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


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// AssocSortedItr

int AssocSortedItr_qsortComp(BUCKET_CONTENTS** p1, BUCKET_CONTENTS** p2)
{
	if (!*p1 && !*p2) return 0;
	if (!*p1)        return 1;
	if (!*p2)        return -1;
	return strcmp((*p1)->key,(*p2)->key);
}

// note that pI must be initialized to null like AssocSortedItr* myItr = {0}; before calling this init
void AssocSortedItr_init(AssocSortedItr* pI, HASH_TABLE* pTbl)
{
	if (pI->allocSize>0) {
		xfree(pI->allItems);
	}
	pI->table = pTbl;
	pI->allItems=NULL;
	pI->allocSize=(pTbl)?pTbl->nentries:0;
	pI->position=0;

	if (pI->allocSize > 0) {
		pI->allItems = xmalloc(pI->allocSize * sizeof(BUCKET_CONTENTS*));
		int copiedCount = 0;
		BUCKET_CONTENTS** pDestArray = pI->allItems;
		for (int i=0; i<pTbl->nbuckets; i++) {
			BUCKET_CONTENTS* pItem = pTbl->bucket_array[i];
			while (pItem) {
				if (++copiedCount > pTbl->nentries)
					assertError(NULL, "AssocSortedItr_init: logic error. found more entries than pTbl->nentries so our memory vector is not big enough");
				*pDestArray++ = pItem;
				pItem = pItem->next;
			}
		}
		qsort(pI->allItems,pI->allocSize,sizeof(BUCKET_CONTENTS*), (QSFUNC *)AssocSortedItr_qsortComp);
	}
}

void AssocSortedItr_free(AssocSortedItr* pI)
{
	AssocSortedItr_init(pI,NULL);
}


BUCKET_CONTENTS* AssocSortedItr_first(AssocSortedItr* pI, HASH_TABLE* pTbl)
{
	AssocSortedItr_init(pI, pTbl);
	return AssocSortedItr_next(pI);
}

BUCKET_CONTENTS* AssocSortedItr_next(AssocSortedItr* pI)
{
	return (pI->position < pI->allocSize)? pI->allItems[pI->position++] : NULL;
}

BUCKET_CONTENTS* AssocSortedItr_peek(AssocSortedItr* pI)
{
	return (pI->position < pI->allocSize)? pI->allItems[pI->position] : NULL;
}



/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Misc


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

	if (vc->flags&VC_HASLOCAL)  BGString_append(&retVal, ",", "VC_HASLOCAL");
	if (vc->flags&VC_HASTMPVAR) BGString_append(&retVal, ",", "VC_HASTMPVAR");
	if (vc->flags&VC_FUNCENV)   BGString_append(&retVal, ",", "VC_FUNCENV");
	if (vc->flags&VC_BLTNENV)   BGString_append(&retVal, ",", "VC_BLTNENV");
	if (vc->flags&VC_TEMPENV)   BGString_append(&retVal, ",", "VC_TEMPENV");

	if (vc_istempenv(vc))   BGString_append(&retVal, ",", "vc_istempenv");
	if (vc_istempscope(vc)) BGString_append(&retVal, ",", "vc_istempscope");
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
	int paramLen = (param)?strlen(param):0;
	int slen;
	do {
		char* s = p;
		if (*s != '-' && *s != '+')
			return 0;
		char* e = s;
		while (e && *e && (strchr("=*|",*e)==NULL) ) e++;
		slen = (e-s);
		switch (*e) {
			// match with an argument (signified with <opt>* or <opt>=)
			case '=':
			case '*':
				// -f<filename> || --file=<filename> || -f <filename> || --file <filename>
				if ( paramLen>=slen && (param[slen]=='=' || param[slen]=='\0' || slen==2) && strncmp(param,s, slen)==0) {
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
				if (paramLen==slen && strncmp(param,s, slen)==0 ) {
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

	char* ifsChar = NULL;
	char* delim = (retVar && retVar->delim) ? retVar->delim : (ifsChar=ifs_firstchar(NULL));

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
				BGString_append(&newVal, delim, value);
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
		case rt_echo: break; // this is handled at top , the same as if retVar is null
		case rt_noop: break;
	}
	xfree(ifsChar);
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
				BGString_append(&newVal, delim, lp->word->word);
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

BGRetType ShellVar_getType(SHELL_VAR* var)
{
	if (array_p(var))
		return rt_array;
	else if (assoc_p(var))
		return rt_set;
	else
		return rt_simple;
}

char* BGRetType_toString(BGRetType rt)
{
	switch (rt) {
		case rt_echo:      return "rt_echo";
		case rt_simple:    return "rt_simple";
		case rt_array:     return "rt_array";
		case rt_set:       return "rt_set";
		case rt_arrayRef:  return "rt_arrayRef";
		case rt_noop:      return "rt_noop";
	}
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
		this->var = ShellVar_findUpVar(varname);
		this->arrayRef = NULL;
		this->type = ShellVar_getType(this->var);
		if (this->type != rt_simple)
			bgWarn("BGRetVar_initFromVarname : varname was found to be an array. This function expected to return a simple value. varname='%s'", varname);
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
			(*retVar)->var = ShellVar_findUpVar(optArg);
			if (!(*retVar)->var) {
				bgWarn("BGRetVar_initFromOpts(): Return variable -R '%s' does not exist in the calling scope\n\targs='%s'", optArg, WordList_toString(*pArgs));
				(*retVar)->var = ShellVar_createGlobal(optArg);
			}
			(*retVar)->type = ShellVar_getType((*retVar)->var);
			if ((*retVar)->type != rt_simple)
				bgWarn("BGRetVar_initFromOpts(): Return variable -R '%s' in the calling scope is an array (use -A or -S for arrays)\n\targs='%s'", optArg, WordList_toString(*pArgs));
		}

	} else if ((optArg=BGCheckOpt("-A*|--array=*|--retArray=*", pArgs))) {
		if (!(*retVar)) (*retVar) = BGRetVar_new();
		found = 1;
		if (*optArg=='\0') {
			(*retVar)->type = rt_noop;
			bgWarn("BGRetVar_initFromOpts(): Return variable -A '%s' is empty. nothing will be returned\n\targs='%s'", optArg, WordList_toString(*pArgs));
			return 1;
		}
		(*retVar)->var = ShellVar_findUpVar(optArg);
		if (!(*retVar)->var) {
			bgWarn("BGRetVar_initFromOpts(): Return variable -A '%s' does not exist in the calling scope\n\targs='%s'", optArg, WordList_toString(*pArgs));
			(*retVar)->var = ShellVar_arrayCreateGlobal(optArg);
		}
		(*retVar)->type = ShellVar_getType((*retVar)->var);
		if ((*retVar)->type == rt_simple) {
			convert_var_to_array((*retVar)->var);
			(*retVar)->type = rt_array;
		} else if ((*retVar)->type != rt_array)
			assertError(NULL, "BGRetVar_initFromOpts(): Return variable -A '%s' (%s) is expected to be an array or a simple variable that can be converted to an array but it is an associative array which is not compatible. Use -S*|--retSet=* if you intend to return values in the indexes of an associative array\n",optArg, BGRetType_toString((*retVar)->type));

	} else if ((optArg=BGCheckOpt("-S*|--retSet=*", pArgs))) {
		if (!(*retVar)) (*retVar) = BGRetVar_new();
		found = 1;
		if (*optArg=='\0') {
			(*retVar)->type = rt_noop;
			bgWarn("BGRetVar_initFromOpts(): Return variable -S '%s' is empty. nothing will be returned\n\targs='%s'", optArg, WordList_toString(*pArgs));
		}
		(*retVar)->var = ShellVar_findUpVar(optArg);
		if (!(*retVar)->var) {
			bgWarn("BGRetVar_initFromOpts(): Return variable -S '%s' does not exist in the calling scope\n\targs='%s'", optArg, WordList_toString(*pArgs));
			(*retVar)->var = ShellVar_assocCreateGlobal(optArg);
		}
		(*retVar)->type = ShellVar_getType((*retVar)->var);
		if ((*retVar)->type != rt_set)
			assertError(NULL, "BGRetVar_initFromOpts(): Return variable '%s' is expected to be an associative array (local -A)\n",optArg);

	} else if ((optArg=BGCheckOpt("-e|--echo", pArgs))) {
		if (!(*retVar)) (*retVar) = BGRetVar_new();
		found = 1;
		(*retVar)->type = rt_echo;
	}

	return found;
}

// sometimes a function calls outputValue in a loop to return multiple values incrementally. It should call this before the loop
// so that it will support the appendFlag correctly. If appendFlag is not initially set, this function will clear the var and then
// set appendFlag so that outputValue will add to the results instead of replacing them
void BGRetVar_startOutput(BGRetVar* retVar)
{
	if (!retVar || retVar->appendFlag)
		return;

	retVar->appendFlag = 1;

	switch (retVar->type) {
		case rt_arrayRef:
			ShellVar_setS(retVar->arrayRef, "");
			break;
		case rt_simple:
			ShellVar_set(retVar->var, "");
			break;
		case rt_array:
			ShellVar_arrayClear(retVar->var);
			break;
		case rt_set:
			ShellVar_assocClear(retVar->var);
			break;
		case rt_echo: break; // this is handled at top , the same as if retVar is null
		case rt_noop: break;
	}
}


char* ShellVarFlagsToString(int flags) {
	BGString retVal;
	BGString_init(&retVal, 500);

	if (flags&att_exported)   BGString_append(&retVal, ",", "exported");
	if (flags&att_readonly)   BGString_append(&retVal, ",", "readonly");
	if (flags&att_array)      BGString_append(&retVal, ",", "array");
	if (flags&att_function)   BGString_append(&retVal, ",", "function");
	if (flags&att_integer)    BGString_append(&retVal, ",", "integer");
	if (flags&att_local)      BGString_append(&retVal, ",", "local");
	if (flags&att_assoc)      BGString_append(&retVal, ",", "assoc");
	if (flags&att_trace)      BGString_append(&retVal, ",", "trace");
	if (flags&att_uppercase)  BGString_append(&retVal, ",", "uppercase");
	if (flags&att_lowercase)  BGString_append(&retVal, ",", "lowercase");
	if (flags&att_capcase)    BGString_append(&retVal, ",", "capcase");
	if (flags&att_nameref)    BGString_append(&retVal, ",", "nameref");
	if (flags&att_invisible)  BGString_append(&retVal, ",", "invisible");
	if (flags&att_nounset)    BGString_append(&retVal, ",", "nounset");
	if (flags&att_noassign)   BGString_append(&retVal, ",", "noassign");
	if (flags&att_imported)   BGString_append(&retVal, ",", "imported");
	if (flags&att_special)    BGString_append(&retVal, ",", "special");
	if (flags&att_nofree)     BGString_append(&retVal, ",", "nofree");
	if (flags&att_regenerate) BGString_append(&retVal, ",", "regenerate");
	if (flags&att_tempvar)    BGString_append(&retVal, ",", "tempvar");
	if (flags&att_propagate)  BGString_append(&retVal, ",", "propagate");
	return retVal.buf;
}

// Params:
//    findStartingPoints : (WORD_LIST*) list of glob expressions to expand
//    retVar             : (BGRetVar*)  specification for how the results are returned. If NULL, then results are returned as a
//                                      WORD_LIST* return value of the function. If not null, the function return value will be NULL
//    fsef_prefixToRemove: (char*)      a glob pattern that will be removed from the start of each returned path. If NULL, paths
//                                      will be returned as is. e.g. "*/" will remove all folder components of the path leaving only
//                                      the filename.
//    flags              : (int)        &'d combination of flags.
//                                      ef_force           : if no matching files are found, return "/dev/null"
//                                      ef_recurse         : return matching files in sub folders also
//                                      ef_filesOnly       : only return matching files
//                                      ef_foldersOnly     : only return matching folders
//                                      ef_alreadyExpanded : internal flag used by fsExpandFiles to indicate that findStartingPoints
//                                                           has already been expanded so not to waste effort doing it again.
//    findOpts           : (WORD_LIST*) native find options (the real options that must come first on the cmd line)
//                                      -H|-L|-P|-D*|-O*
//    findGlobalExpressions:(WORD_LIST*)native find expressions that are global and therefore must come fist in the expression section.
//                                      -mindepth <n>|-maxdepth <n>|-depth|-d|-ignore_readdir_race|-noignore_readdir_race|-mount|-xdev|-noleaf
//    findExpressions    : (WORD_LIST*) native find match expressions (to further restrict the returned set of files)
//    _gitIgnorePath     : (char*)      determines whether the expressions from a .gitignore files are used to exclude the files
//                                      that git ignores from consideration.
//                                      NULL : do not exclude the git ignored files
//                                      "<glean>" : use the .gitignore file if found in the ussual place
//                                      <pathToGitIgnore> : any other string is interpretted as the path to the .gitignore file to use.
//    excludePaths       : (WORD_LIST*) a list of files or folders to exclude. Can contain glob characters.
WORD_LIST* fsExpandFilesC(
	WORD_LIST* findStartingPoints,
	BGRetVar* retVar,
	char* fsef_prefixToRemove,
	int flags,
	WORD_LIST* findOpts,
	WORD_LIST* findGlobalExpressions,
	WORD_LIST* findExpressions,
	char* _gitIgnorePath,
	WORD_LIST* excludePaths,
	int* pFound)
{
	int forceFlag   = flags & ef_force;
	int recurseFlag = flags & ef_recurse;

	WORD_LIST* fTypeOpt = NULL;
	if (flags&ef_filesOnly) {
		fTypeOpt = WordList_unshift(fTypeOpt, "f");
		fTypeOpt = WordList_unshift(fTypeOpt, "-type");
	} else if (flags&ef_foldersOnly) {
		fTypeOpt = WordList_unshift(fTypeOpt, "d");
		fTypeOpt = WordList_unshift(fTypeOpt, "-type");
	}

	if (!(flags&ef_alreadyExpanded)) {
		WORD_LIST* tList = findStartingPoints;
		findStartingPoints = expand_words(findStartingPoints);
		WordList_free(tList);
	}

	WORD_LIST* recursiveOpt = (recurseFlag) ? NULL : WordList_fromString("-maxdepth 0", IFS,0);


	// if the user supplied more than 1 find test expression, it may contain 'or' logic so enclose it in () so that we can treat it
	// like one and'd filter criteria
	if (findExpressions && findExpressions->next) {
		findExpressions = WordList_unshift(findExpressions, "'('");
		WordList_push(&findExpressions, "')'");
	}

	// calculate the commonPrefix which is the the effective root folder of the entire operation
	char* commonPrefix = NULL;
	if (_gitIgnorePath || excludePaths) {
		char* tmp = pathGetCommon(findStartingPoints);
		if (tmp && *tmp!='\0')
			commonPrefix = save2string(tmp, "/");
		xfree(tmp);
	}
	commonPrefix = (commonPrefix) ?commonPrefix : savestring("./");

	// add the gitignore contents to the excludePaths if called for
	if (_gitIgnorePath) {
		if (strcmp(_gitIgnorePath, "<glean>"))
			_gitIgnorePath = save2string(commonPrefix, ".gitignore");
		else
			_gitIgnorePath = savestring(_gitIgnorePath);
		excludePaths = WordList_unshift(excludePaths, ".git");
		excludePaths = WordList_unshift(excludePaths, ".bglocal");
		FILE* fGitIgnore = fopen(_gitIgnorePath, "r");
		if (fGitIgnore) {
			size_t bufSize = 100;
			char* buf =  xmalloc(bufSize);
			while (freadline(fGitIgnore, &buf, &bufSize) > -1) {
				char* pS = buf;
				while (*pS && whitespace(*pS)) pS++;
				// skip comments and blank lines
				if (*pS != '#' && *pS != '\0') {
					excludePaths = WordList_unshift(excludePaths, pS);
				}
			}
			xfree(buf);
		}
		xfree(_gitIgnorePath);
	}

	// build the findPruneExpr if there are any excludePaths
	WORD_LIST* findPruneExpr = NULL;
	if (excludePaths) {
		WORD_LIST* _folderEntries = NULL;
		WORD_LIST* _anyEntries = NULL;
		for (WORD_LIST* excludePath=excludePaths; excludePath; excludePath=excludePath->next) {
			char* path = savestring(excludePath->word->word);
			int isDir = 0;
			char* _expr = "-name"; for (char* t=path; *t; t++) if (*t=='/') { _expr="-path"; break;}
			if (path-(strrchr(path,'/')) == strlen(path)-1 ) {
				path[strlen(path)-1] = '\0';
				isDir=1;
			}
			if (path && *path=='/') {
				char* tstr = path;
				path=save2string(commonPrefix, tstr);
				xfree(tstr);
			}
			WORD_LIST** pList =( (isDir) ? &_folderEntries : &_anyEntries);
			path = resaveWithQuotes(path, 1);
			*pList = WordList_unshift(*pList, path);
			*pList = WordList_unshift(*pList, _expr);
			*pList = WordList_unshift(*pList, "-o");

			xfree(path);
		}

		//    "(" "(" -type d "(" -false "${_folderEntries[@]}" ")" ")" -o  "(" -false "${_anyEntries[@]}" ")"  ")" -prune -o
		//     (-  (--         (---------------------------------) --)       (------------------------------)   -)
		findPruneExpr = WordList_join(NULL,             WordList_fromString("'(' '(' -type d '(' -false"  ,IFS,0));
		findPruneExpr = WordList_join(findPruneExpr,    _folderEntries);
		findPruneExpr = WordList_join(findPruneExpr,    WordList_fromString("')' ')' -o '(' -false"       ,IFS,0));
		findPruneExpr = WordList_join(findPruneExpr,    _anyEntries);
		findPruneExpr = WordList_join(findPruneExpr,    WordList_fromString("')' ')' -prune -o "        ,IFS,0));
	}

	xfree(commonPrefix);

	// remove starting points that do not exist so that the 'find' util does not complain about them
	// add quotes to the ones that are left now that they are expanded
	WORD_LIST** ppLast = &findStartingPoints;
	for (WORD_LIST* p=findStartingPoints; p; ) {
		if (!fsExists(p->word->word)) {
			WORD_LIST* tmpP = p;
			p = p->next;
			*ppLast = p;
			tmpP->next = NULL;
			WordList_free(tmpP);
		} else {
			ppLast = &(p->next);
			WordDesc_surroundWord(p->word,'"','"');
			p=p->next;
		}
	}

	// if none of the starting points exist this invocation will match nothing, but there is nothing we can set findStartingPoints
	// to so that find would exit cleanly without displaying an error so we return here
	if (!findStartingPoints) {
		if (retVar) {
			BGRetVar_startOutput(retVar);
			if (flags&ef_force)
				outputValue(retVar, "/dev/null");
		}
		return NULL;
	}


	// buffer to read result file(s)
	size_t bufSize = 100;
	char* buf =  xmalloc(bufSize);

	// now compose the arguments to the find cmd and redirect output to tmp files
	char* tmpFilename = mktempC("/tmp/bgCore.out.XXXXXXXXXX");
	char* errOut = mktempC("/tmp/bgCore.errOut.XXXXXXXXXX");
	WORD_LIST* findArgs = NULL;
	findArgs = WordList_join(findArgs, findOpts);
	findArgs = WordList_join(findArgs, findStartingPoints);
	findArgs = WordList_join(findArgs, recursiveOpt);
	findArgs = WordList_join(findArgs, findGlobalExpressions);
	findArgs = WordList_join(findArgs, findPruneExpr);
	findArgs = WordList_join(findArgs, fTypeOpt);
	findArgs = WordList_join(findArgs, findExpressions);
	//WordList_push(&findArgs, "-print0");
	WordList_push(&findArgs, ">");
	WordList_push(&findArgs, tmpFilename);
	WordList_push(&findArgs, "2>");
	WordList_push(&findArgs, errOut);

	// execute the cmdline. parse_and_execute will free the cmdline string
	findArgs = WordList_unshift(findArgs, "find");
	char* cmdline = WordList_toString(findArgs);
	WordList_free(findArgs);

	if (0!=parse_and_execute(cmdline, "bgCore", SEVAL_NOFREE | SEVAL_NOHIST | SEVAL_RESETLINE | SEVAL_NOHISTEXP | SEVAL_NONINT)) {
		__bgtrace("bgfind cmdline='%s'\n", cmdline);
		FILE* errOutFD = fopen(errOut, "r");
		while (freadline(errOutFD, &buf, &bufSize) > -1) {
			__bgtrace("   err: %s\n", buf);
		}
		assertError(NULL, "gnu find failed. \n\tcmdline='%s'\n", cmdline);
	}
	xfree(cmdline);

	// if fsef_prefixToRemove is set, create a tmpVar to have BASH manipulate the value
	SHELL_VAR* tmpVar = NULL;
	SHELL_VAR* vPrefixToRemove = NULL;
	char* prefixCmd = NULL;
	if (fsef_prefixToRemove) {
		tmpVar = varNewHeapVar("");
		vPrefixToRemove = varNewHeapVar("");
		ShellVar_set(vPrefixToRemove, fsef_prefixToRemove);
		prefixCmd = saprintf("%s=${%s/#$%s}", tmpVar->name, tmpVar->name, vPrefixToRemove->name);
	}

	// now read the results from the tmp file
	FILE* tmpFile = fopen(tmpFilename, "r");
	int found = 0;
	BGRetVar_startOutput(retVar);
	WORD_LIST* resultList = NULL;
	while (freadline(tmpFile, &buf, &bufSize) > -1) {
		if (fsef_prefixToRemove) {
			ShellVar_set(tmpVar, buf);
			parse_and_execute(prefixCmd, "bgCore", SEVAL_NOFREE | SEVAL_NOHIST | SEVAL_RESETLINE | SEVAL_NOHISTEXP | SEVAL_NONINT);
			strcpy(buf, ShellVar_get(tmpVar));
		}
		if (retVar)
			outputValue(retVar, buf);
		else
			resultList = WordList_unshift(resultList, buf);
		found = 1;
	}
	xfree(buf);

	// TODO: rm the tmp files
	unlink(tmpFilename);
	unlink(errOut);
	xfree(tmpFilename);
	xfree(errOut);

	resultList = WordList_reverse(resultList);

	if (!found && forceFlag)
		outputValue(retVar, "/dev/null");

	if (pFound) *pFound = found;

	return resultList;
}

int fsExpandFiles(WORD_LIST* args)
{
	// This cmd, like the gnu find util, has 3 sections of parameters instead of the normal 2 (options + positional)
	//    Section 1: normal options. ends when '--' or a token without a leading '-' is encountered
	//    Section 2: starting paths. ends when a token with a leading '-' is encountered.
	//    Section 3: criteria and action 'script'

	// Section 1: process real options
	int flags = ef_alreadyExpanded;
	//int findCmdLineCompat = 0;
	char* fsef_prefixToRemove = NULL;
	char* _gitIgnorePath = NULL;
	WORD_LIST* excludePaths = NULL;
	WORD_LIST* findOpts = NULL;
	BGRetVar* retVar = BGRetVar_new();
	while (args && (*(args->word->word) == '-' || *(args->word->word) == '+')) {
		char* optArg;
		char* param = args->word->word;
		if       (strcmp("--",args->word->word)==0)                 { args=args->next; break; }
		else if ((optArg=BGCheckOpt("--findCmdLineCompat", &args))) ;//findCmdLineCompat = 1;
		else if ((optArg=BGCheckOpt("--force|-f", &args)))          flags = flags | ef_force;
		// aliases for -type d|f
		else if ((optArg=BGCheckOpt("-F", &args)))                  flags = flags | ef_filesOnly;
		else if ((optArg=BGCheckOpt("-D|-d", &args)))               flags = flags | ef_foldersOnly;
		// -R means recurse (dont limit the depth) and +R means dont recurse (apply the find criteria only to the supplied paths)
		else if ((optArg=BGCheckOpt("-R", &args)))                  flags = flags | ef_recurse;
		else if ((optArg=BGCheckOpt("+R", &args)))                  flags = flags & (!ef_recurse);
		// modify the output
		else if ((optArg=BGCheckOpt("--basename|-b", &args)))       fsef_prefixToRemove = "*/";
		else if ((optArg=BGCheckOpt("-B*", &args)))                 fsef_prefixToRemove = optArg;
		// should we exclude patterns from gitignore?
		else if ((optArg=BGCheckOpt("--gitignore", &args)))         _gitIgnorePath = "<glean>";
		else if ((optArg=BGCheckOpt("--gitignore*", &args)))        _gitIgnorePath = (strcmp(optArg,"")==0) ? "<glean>" : optArg;
		// exclude some results
		else if ((optArg=BGCheckOpt("--exclude*", &args)))          excludePaths = WordList_unshiftQ(excludePaths, optArg);
		// native find (GNU utility) 'real' options (as opposed to find criteria and actions that also start with '-')
		else if ((optArg=BGCheckOpt("-H|-L|-P", &args)))            findOpts = WordList_unshift(findOpts, optArg);
		else if ((optArg=BGCheckOpt("-D*|-O*", &args)))             {findOpts = WordList_unshift(findOpts, optArg);findOpts = WordList_unshift(findOpts, param);}
		else if ((BGRetVar_initFromOpts(&retVar, &args)))           ;
		else break; //assertError(NULL, "invalid option '%s'\n", args->word->word);
		args = (args) ? args->next : NULL;
	}

	// Section 2: these are the <fileSpec> positional parameters. Bash will expand any wild cards if they match any paths but will
	// leave them in if they dont match any. We cant pass any non existing paths to find or it will fail with an error. find will
	// interpret each of these as a starting point but if +R is specified, we will give find the -maxdepth 0 global option that
	// will cause it to only apply the test expressions to the starting points and do no directory traversal.
	WORD_LIST* findStartingPoints = NULL;
	while (args && (*(args->word->word) != '-' && *(args->word->word) != '(')) {
		findStartingPoints = WordList_unshift(findStartingPoints, args->word->word);
		args = (args) ? args->next : NULL;
	}
	findStartingPoints = WordList_reverse(findStartingPoints);

	// Section3: /bin/find expressions section. The rest of the command line is interpretted as the find expression
	WORD_LIST* findGlobalExpressions = NULL;
	WORD_LIST* findExpressions = NULL;
	while (args) {
		char* optArg;
		char* param = args->word->word;

		// 'global options'
		if ((optArg=BGCheckOpt("-maxdepth|-mindepth", &args))) {
			flags = flags | ef_recurse;
			findGlobalExpressions = WordList_unshift(findGlobalExpressions, param);
			args = (args) ? args->next : NULL;
			if (!args)
				assertError(NULL, "-maxdepth|-mindepth must be followed by a value on the command line to fsExpandFiles");
			findGlobalExpressions = WordList_unshift(findGlobalExpressions, args->word->word);
		}
		else if ((optArg=BGCheckOpt("-depth|-d|-ignore_readdir_race|-noignore_readdir_race|-mount|-xdev|-noleaf", &args)))
			findGlobalExpressions = WordList_unshift(findGlobalExpressions, param);
		else if ((optArg=BGCheckOpt("-help|-version", &args))) {
			assertError(NULL, "This global 'find' expression is not supported by fsExpandFiles (%s)", param);
		}

		// actions. most actions are not supported b/c we are all about the returned list (-quit is alright though)
		else if ((optArg=BGCheckOpt("-delete|-exec|-execdir|-fls|-fprint|-fprint0|-fprintf|-ls|-ok|-okdir|-print|-print0|-printf", &args)))
			assertError(NULL, "Action 'find' options are not allowed by fsExpandFiles. ($1)" );

		else if ((optArg=BGCheckOpt("-prune", &args))) {
			if (flags & (ef_filesOnly|ef_foldersOnly))
			 	assertError(NULL, "-prune can not be used with either the -D (directories only) or -F (files only) options. You can however use -type d|f in your expression with -prune ");
			findExpressions = WordList_unshift(findExpressions, args->word->word);
		} else {
			// assume any other is a valid test expression, operator or positional option. If not, find will fail
			findExpressions = WordList_unshiftQ(findExpressions, args->word->word);
		}

		args = (args) ? args->next : NULL;
	}
	findGlobalExpressions = WordList_reverse(findGlobalExpressions);
	findExpressions = WordList_reverse(findExpressions);


	int found = 0;

	fsExpandFilesC(
		findStartingPoints,
		retVar,
		fsef_prefixToRemove,
		flags,
		findOpts,
		findGlobalExpressions,
		findExpressions,
		_gitIgnorePath,
		excludePaths,
		&found
	);

	if (retVar)
		xfree(retVar);

	return (found) ? EXECUTION_SUCCESS : EXECUTION_FAILURE;
}



// usage: pathGetCommon <path1> <path2>
// compare the two paths from left to right and return the prefix that is common to both.
// put another way, it returns the folder with the longest path that is a parent of both paths
// Example:
//    p1 = /var/lib/foo/data
//    p2 = /var/lib/bar/five/fee
//   out = /var/lib/
char* pathGetCommon(WORD_LIST* paths)
{
	if (!paths)
		return savestring("");

	// init prefix with the first path. We assume that the last component of the path is a folder even if there is no trailing '/'
	// if it is, infact a filename, it will be removed unless all paths are equal to that filename path which is not valid input.
	char* prefix = savestring(paths->word->word);
	int prefixLen = strlen(prefix);
	while (prefixLen>0 && prefix[prefixLen-1] == '/') prefixLen--;

	// now iterate over the rest of the pathes, each time shortening prefix if needed until its empty or matches some part of path
	for (WORD_LIST* tmp=paths->next; prefixLen>0 && tmp; tmp=tmp->next) {
		char* tword = tmp->word->word;
		int tlen = strlen(tword);

		while (prefixLen>0 && (strncmp(tword, prefix, prefixLen)!=0  || (prefixLen!=tlen && tword[prefixLen] != '/'))) {
			while (prefixLen>0 && prefix[prefixLen-1] != '/') prefixLen--; // move to the first / moving backwards
			while (prefixLen>0 && prefix[prefixLen-1] == '/') prefixLen--; // move off the '/' or '//'
		}
	}

	// the only time this function returns prefix with a trailing '/' is if all pathes are absolute but have no other common path.
	if (prefixLen==0) {
		int allAbs = 1;
		for (WORD_LIST* tmp=paths; allAbs && tmp; tmp=tmp->next)
			if (*(tmp->word->word)!='/')
				allAbs = 0;
		if (allAbs) {
			prefixLen++;
		}
	}

	prefix[prefixLen] = '\0';

	return prefix;
}


// c implementation of the bash library function
// returns a new global SHELL_VAR with a randon name like "heap_A_XXXXXXXXX" where X's are random chars
// The new var is given the attributes specified in <attributes>
SHELL_VAR* varNewHeapVar(char* attributes)
{
    // TODO: use char* mktempC(<template>) to create the varname

	static char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
	int charsetSize = (int) (sizeof(charset) -1);

	int length=16+strlen(attributes);
	char* buf = xmalloc(length+1);
	if (!buf)
		return NULL;
	strcpy(buf, "heap_");
	strcat(buf, attributes);
	strcat(buf, "_");

	for (int n=strlen(buf);n < length;n++) {
		buf[n] = charset[rand() % charsetSize];
	}

	buf[length] = '\0';

	SHELL_VAR* retVal;

	if (!attributes || !strcasestr(attributes,"a"))
		retVal = ShellVar_createGlobal(buf);
	else if (strstr(attributes, "a"))
//		retVal = ShellVar_arrayCreateGlobal(buf);
		retVal = make_new_array_variable(buf);
	else
//		retVal = ShellVar_assocCreateGlobal(buf);
		retVal = make_new_assoc_variable(buf);
	xfree(buf);
	return retVal;
}


// this is not a good implementation of this concept. We need a fsPipeToFile and fsReplaceIfDifferent that will use either the
// overwrite content or the mv file preserving the overwritten file's attributes methods and using sudo when needed.
int fsReplaceIfDifferent(char* srcFilename, char* destFilename, int flags)
{
	FILE* srcFD = fopen(srcFilename, "r");
	if (!srcFD)
		assertError(NULL, "fsReplaceIfDifferent: could not open srcFilename(%s) for reading. ",srcFilename);
	FILE* destFD = fopen(destFilename, "r");

	// if destFilename does not exist, it will be created (so changedFlag is true)
	int changedFlag = (destFD==NULL);

	BGString srcBuf, destBuf;
	BGString_init(&srcBuf, 100);
	BGString_init(&destBuf, 100);

	// next, if destFile exists, see if the content is different
	if (!changedFlag) {
		int srcDone = 0;
		int destDone = 0;
		while (!changedFlag && !srcDone && !destDone) {
			srcDone  =  ! BGString_readln(&srcBuf,  srcFD);
			destDone  = ! BGString_readln(&destBuf, destFD);
			changedFlag = strcmp(srcBuf.buf, destBuf.buf)!=0;
		}
		fclose(srcFD);
		fclose(destFD);

		changedFlag = changedFlag || (srcDone!=destDone);
	}

	if (changedFlag) {
		fsCopy(srcFilename, destFilename, flags);
	} else {
		if (flags&cp_removeSrc) {
			if (remove(srcFilename)!=0)
				assertError(NULL, "fsCopy: could not remove file '%s'. %s", srcFilename, strerror(errno));
		}
	}
	return changedFlag;
}
