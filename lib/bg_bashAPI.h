
#if !defined (_bg_bashAPI_H_)
#define _bg_bashAPI_H_

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdarg.h>
#include<setjmp.h>

// this replaces loadables.h
#include <builtins.h>
#include <shell.h>
#include <common.h>

// everyone likes misc stuff
#include "bg_misc.h"
#include "bg_debug.h"
#include "bg_ini.h"



/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// NOTES:
//    * builtins.h : defines the data structures and functions to create a loadable plugin
//                   but also brings in sys/types.h or unistd.h based on _MINIX
//    * common.h  has interesing functions parse_and_execute, evalstring(string, from_file, flags), pushd.def,setattr.def,shopt,set functions, find_shell_builtin, JOB_CONTROL
//                set_dollar_vars_changed, builtin_error and other error reporting
//    * shell.h   mostly brings in a lot of other headers.
//                parser_remaining_input() seems interesting WRT assertError unwinding the stack
//                shell state variables
//    *    general.h : savestring, strchr, strrchr, xmalloc. whitespace,
//    *    subst.h   : manipulate strings of code and word lists.
//    *    externs.h : print_word_list, print_command, make_command_string, set -x support, maybe_make_restricted, get_current_user_info,
//                     parser stuff, locale stuff, generic list functions, string manipulation find_string_in_alist strip_leading etc,
//                     sh_modcase,get_clk_tck,getcwd,inttostr family, sh_makepath, netopen, gethostname, sh_canonpath, sh_regmatch,
//                     string quoting family (sh_single_quote),strncasecmp,strftime,STRINGLIST *strlist_create, sh_mktmpname,
//                     get_new_window_size, zgetline family (read from stdin), match_pattern_char
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// make this function from source.def avaialable
extern int source_builtin (WORD_LIST *);

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Error handling

// the top level builtin function needs to setjmp(assertErrorJmpPoint) like...
// if ((exitCode = setjmp(assertErrorJmpPoint))) {
//    // true condition is returning from the longjmp inside assertError()
//    return exitCode;
// } else {
//    // false condition is the normal builtin code
//    ...
// }

typedef struct {
	jmp_buf jmpBuf;
	char* name;
} CallFrame;

extern CallFrame* callFrames_push();                 // at the start of each builtin call this
extern void callFrames_pop();                      // at the end of the success path call this
extern void callFrames_longjump(int exitCode);     // at the start of the error path (in assertError or equivalent) call this
extern int callFrames_getPos();

extern int assertError(WORD_LIST* opts, char* fmt, ...);
extern void bgWarn(char* fmt, ...);



/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ShellVar
// Make an API over existing bash SHELL_VAR stuff from the POV of re-coding bash function in C for performance

// Suffix Meaning
//    <function>S   : trailing S means the parameter is specified as a string (instead of a SHELL_VAR or int index)
//    <function>I   : trailing I means the parameter is specified as a int (e.g. array[int])
//    <function>El  : operate on an element of an array or assoc instead of the array or assoc var itself
//    <function>Global : ignore local vars by this name and operate only on global vars
//    <function>createSet : this is short for create and set as opposed to creating an uninitialized var

// var context/scope functions
#define ShellAtGlobalScope()                     (shell_variables==global_variables)
#define ShellPushFunctionScope(name)             push_var_context(name, VC_FUNCENV, temporary_env)
#define ShellPopFunctionScope()                  pop_var_context()

// shell function functions
#define ShellFunc_find(funcname)                 find_function(funcname)
extern SHELL_VAR* ShellFunc_findWithSuffix(char* funcname, char* suffix);
extern int ShellFunc_executeS(WORD_LIST* args);
extern int ShellFunc_execute(SHELL_VAR* func, WORD_LIST* args);


// plain var functions
extern SHELL_VAR* ShellVar_find(char* varname);
extern SHELL_VAR* ShellVar_findUpVar(char* varname);
#define ShellVar_findLocal(varname)             hash_lookup(varname, shell_variables);
#define ShellVar_findGlobal(varname)            find_global_variable(varname)
extern SHELL_VAR* ShellVar_findWithSuffix(char* varname, char* suffix);
// create will return an existing variable or create a new local or global var depending on if there is a function context or not
extern SHELL_VAR* ShellVar_create(char* varname);
#define ShellVar_createGlobal(varname)          bind_global_variable(varname, NULL, ASS_FORCE)
#define ShellVar_createGlobalSet(varname,value) bind_global_variable(varname, value, ASS_FORCE)
#define ShellVar_createSet(varname, value)      bind_variable_value(ShellVar_create(varname), value, 0)
#define ShellVar_unset(var)                     unbind_variable(var->name)
#define ShellVar_unsetS(varname)                unbind_variable(varname)
#define ShellVar_get(var)                       get_variable_value(var)
#define ShellVar_getS(varname)                  get_variable_value(ShellVar_find(varname))
#define ShellVar_set(var,value)                 bind_variable_value(var,value,0)
extern void ShellVar_setS(char* varname, char* value);

// nameref var functions
// note that ShellVar_find will return the non-ref SHELL_VAR refered to by varname. ShellVar_refFind allows us to manipulate the ref
#define ShellVar_refFind(varname)              find_variable_noref(varname)
// create will return an existing variable or create a new one
extern SHELL_VAR* ShellVar_refCreate(char* varname);
extern SHELL_VAR* ShellVar_refCreateSet(char* varname, char* value);
#define ShellVar_refUnsetS(varname)            unbind_variable_noref(varname)
#define ShellVar_refUnset(var)                 unbind_variable_noref(var->name)


// array var functions
// create will return an existing variable or create a new one
extern SHELL_VAR* ShellVar_arrayCreate(char* varname);
#define ShellVar_arrayCreateGlobal(varname)    make_new_array_variable(varname)
#define ShellVar_arrayUnset(var)               unbind_variable(var->name)
#define ShellVar_arrayUnsetI(var, index)       array_dispose_element(array_remove(array_cell(var), index))
#define ShellVar_arrayUnsetS(varname, indexStr) array_dispose_element(array_remove(array_cell(varname), array_expand_index(varname, indexStr, strlen(indexStr), 0)))
#define ShellVar_arrayGetI(var,indexInt)       array_reference(array_cell(var), indexInt)
#define ShellVar_arrayGetS(var,indexStr)       array_reference(array_cell(var), array_expand_index(var, indexStr, strlen(indexStr), 0))
#define ShellVar_arraySetI(var,indexInt,value) bind_array_element(var, indexInt, value, 0)
#define ShellVar_arraySetS(var,indexStr,value) bind_array_element(var, ShellVar_arrayStrToIndex(var,indexStr), value, 0)
#define ShellVar_arrayPush(var,value)          bind_array_element(var, 1+array_max_index(array_cell(var)), value, 0)
#define ShellVar_arrayAppend(var,value)        bind_array_element(var, 1+array_max_index(array_cell(var)), value, 0)
#define ShellVar_arrayCopy(dst,src)            do { array_dispose(array_cell(dst)); var_setarray(dst, array_copy(array_cell(src))); } while(0)
#define ShellVar_arrayClear(var)               array_flush(array_cell(var))
#define ShellVar_arrayStrToIndex(var,indexStr) ((isNumber(indexStr)) ? atoi(indexStr) : 1+array_max_index(array_cell(var)))
// this does not use array_to_word_list() because the WORD_LIST used in arrays dont use the same allocation as in the rest
extern WORD_LIST* ShellVar_arrayToWordList(SHELL_VAR* var);
// array iteration macros...
#define ShellVar_arrayStart(var)   (array_cell(var))->head->next
#define ShellVar_arrayEnd(var)     (array_cell(var))->head->prev
#define ShellVar_arrayEOL(var)     (array_cell(var))->head
// for (ARRAY_ELEMENT* el = ShellVar_arrayStart(var); el!=ShellVar_arrayEOL(var); el=el->next ) {
//     bgtrace2(0,"index='%d'  value='%s'\n", el->ind, el->value);

// assoc var functions
// create will return an existing variable or create a new one
extern SHELL_VAR* ShellVar_assocCreate(char* varname);
#define ShellVar_assocCreateGlobal(varname)    make_new_assoc_variable(varname)
#define ShellVar_assocUnset(var)               unbind_variable(var->name)
#define ShellVar_assocUnsetEl(var,indexStr)    assoc_remove(assoc_cell(var), indexStr)
#define ShellVar_assocGet(var,indexStr)        assoc_reference(assoc_cell(var), indexStr)
#define ShellVar_assocGetS(varname,indexStr)   assoc_reference(assoc_cell(ShellVar_find(varname)), indexStr)
#define ShellVar_assocSet(var,indexStr,value)  do { VUNSETATTR(var, att_invisible);  assoc_insert(assoc_cell(var), savestring(indexStr), value); } while(0)
#define ShellVar_assocSize(var)                assoc_num_elements(assoc_cell(var))
#define ShellVar_assocClear(var)               assoc_flush(assoc_cell(var))
extern void ShellVar_assocCopyElements(HASH_TABLE* dest, HASH_TABLE* source);

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// WordList
// native WORD_LIST functions are mostly in make_cmd.h

extern char* WordList_shift(     WORD_LIST** list);
extern void  WordList_shiftFree( WORD_LIST** list, int count);
extern void  WordList_freeUpTo(  WORD_LIST** list, WORD_LIST* stop);
#define      WordList_unshift(   list, word)    make_word_list(make_word(word), list)

extern void  WordList_push(     WORD_LIST** list, char* word);
extern char* WordList_pop(      WORD_LIST** list);


#define      WordList_toString(  list)          string_list(list)
#define      WordList_fromString(contents, seperators, quotedFlag)    list_string(contents, seperators, quotedFlag)
#define      WordList_reverse(   list)          REVERSE_LIST(list, WORD_LIST*)
#define      WordList_free(      list)          do { if (list) dispose_words(list); list=NULL; } while(0)
#define IFS " \t\n\0"

extern WORD_LIST* WordList_copy(WORD_LIST* src);
extern WORD_LIST* WordList_copyR(WORD_LIST* src);
extern WORD_LIST* WordList_join(WORD_LIST* args1, WORD_LIST* args2);


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// AssocItr

typedef struct {
	HASH_TABLE* table;
	BUCKET_CONTENTS* item;
	int position;
} AssocItr;

// two patterns:
//   AssocItr itr;
//   for (BUCKET_CONTENTS* item=AssocItr_first(&itr,assoc_cell(var)); item; item=AssocItr_next(&itr) )
// or..
//   AssocItr itr; AssocItr_init(&itr,assoc_cell(var));
//   BUCKET_CONTENTS* bVar;
//   while (bVar=AssocItr_next(&itr) )

extern void             AssocItr_init(AssocItr* pI, HASH_TABLE* pTbl);
extern BUCKET_CONTENTS* AssocItr_first(AssocItr* pI, HASH_TABLE* pTbl);
extern BUCKET_CONTENTS* AssocItr_next(AssocItr* pI);
extern BUCKET_CONTENTS* AssocItr_peek(AssocItr* pI);

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ArrayItr (there is no ArrayItr -- use this pattern)
// for (ARRAY_ELEMENT* el = ShellVar_arrayStart(var); el!=ShellVar_arrayEOL(var); el=el->next ) {
//     bgtrace2(0,"index='%d'  value='%s'\n", el->ind, el->value);



/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// BGRetVar
//    This supports a function that returns it result in a flexible way. See man(3) bgOptions_DoOutputVarOpts

typedef enum {rt_simple, rt_array, rt_set, rt_echo, rt_arrayRef, rt_noop} BGRetType;

typedef struct {
	SHELL_VAR* var;
	char* arrayRef;
	BGRetType type;
	int appendFlag;
	char* delim;
} BGRetVar;

extern void      BGRetVar_init(BGRetVar* retVar);
extern void      BGRetVar_initFromVarname(BGRetVar* retVar, char* varname);
extern BGRetVar* BGRetVar_new();
extern int       BGRetVar_initFromOpts(BGRetVar** retVar, WORD_LIST** pArgs);
extern void      outputValue(BGRetVar* retVar, char* value);
extern void      outputValues(BGRetVar* retVar, WORD_LIST* values);

extern char* BGCheckOpt(char* spec, WORD_LIST** pArgs);


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// file functions
//

extern int fsExpandFiles(WORD_LIST* args);

extern char* pathGetCommon(WORD_LIST* paths);

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Debugging
//
extern char* ShellVarFlagsToString(int flags);
extern void ShellContext_dump(VAR_CONTEXT* varCntx, int includeVars);



/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Misc
//
extern SHELL_VAR* varNewHeapVar(char* attributes);


#endif // _bg_bashAPI_H_
