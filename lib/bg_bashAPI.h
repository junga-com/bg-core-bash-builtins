
#if !defined (_bg_bashAPI_H_)
#define _bg_bashAPI_H_

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdarg.h>

// this replaces loadables.h
#include <builtins.h>
#include "shell.h"
#include "common.h"

// everyone likes misc stuff
#include "bg_misc.h"
#include "bg_debug.h"



/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// NOTES:
//    * builtins.h : defines the data structures and functions to create a loadable plugin
//                   but also brings in sys/types.h or unistd.h based on _MINIX
//    * common.h  has interesing functions parse_and_execute, pushd.def,setattr.def,shopt,set functions, find_shell_builtin, JOB_CONTROL
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



/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ShellVar
// Make an API over existing bash SHELL_VAR stuff from the POV of re-coding bash function in C for performance

// Suffix Meaning
//    <function>S   : trailing S means the parameter is specified as a string (instead of a SHELL_VAR or int index)
//    <function>I   : trailing I means the parameter is specified as a int (e.g. array[int])
//    <function>El  : operate on an element of an array or assoc instead of the array or assoc var itself
//    <function>Global : ignore local vars by this name and operate only on global vars
//    <function>createSet : this is short for create and set as opposed to creating an uninitialized var

#define ShellVar_find(varname)                  find_variable(varname)
#define ShellVar_findGlobal(varname)            find_global_variable(varname)
#define ShellVar_create(varname)                make_local_variable(varname,0)
#define ShellVar_createGlobal(varname)          bind_global_variable(varname, NULL, ASS_FORCE)
#define ShellVar_createGlobalSet(varname,value) bind_global_variable(varname, value, ASS_FORCE)
#define ShellVar_createSet(varname, value)      bind_variable_value(make_local_variable(varname,0), value, 0)
extern SHELL_VAR* ShellVar_findOrCreate(char* varname);
#define ShellVar_unset(var)                     unbind_variable(var->name)
#define ShellVar_unsetS(varname)                unbind_variable(varname)
#define ShellVar_get(var)                       get_variable_value(var)
#define ShellVar_set(var,value)                 bind_variable_value(var,value,0)

// note that ShellVar_find will return the non-ref SHELL_VAR refered to by varname. ShellVar_refFind allows us to manipulate the ref
#define ShellVar_refFind(varname)              find_variable_noref(varname)
extern SHELL_VAR* ShellVar_refCreate(char* varname);
extern SHELL_VAR* ShellVar_refCreateSet(char* varname, char* value);
#define ShellVar_refUnsetS(varname)            unbind_variable_noref(varname)
#define ShellVar_refUnset(var)                 unbind_variable_noref(var->name)


#define ShellVar_arrayCreate(varname)          make_local_array_variable(varname,0)
#define ShellVar_arrayCreateGlobal(varname)    make_new_array_variable(varname)
#define ShellVar_arrayUnset(var)               unbind_variable(var->name)
#define ShellVar_arrayUnsetI(var, index)       array_dispose_element(array_remove(array_cell(var), index))
#define ShellVar_arrayUnsetS(var, indexStr)    array_dispose_element(array_remove(array_cell(var), array_expand_index(var, indexStr, strlen(indexStr), 0)))
#define ShellVar_arrayGetI(var,indexInt)       array_reference(array_cell(var), indexInt)
#define ShellVar_arrayGetS(var,indexStr)       array_reference(array_cell(var), array_expand_index(var, indexStr, strlen(indexStr), 0))
#define ShellVar_arraySetI(var,indexInt,value) bind_array_element(var, indexInt, value, 0)
#define ShellVar_arraySetS(var,indexStr,value) bind_array_element(var, ShellVar_arrayStrToIndex(var,indexStr), value, 0)
#define ShellVar_arrayAppend(var,value)        bind_array_element(var, 1+array_max_index(array_cell(var)), value, 0)
#define ShellVar_arrayCopy(dst,src)            do { array_dispose(array_cell(dst)); var_setarray(dst, array_copy(array_cell(src))); } while(0)
#define ShellVar_arrayStrToIndex(var,indexStr) ((isNumber(indexStr)) ? atoi(indexStr) : 1+array_max_index(array_cell(var)))

// this does not use array_to_word_list() because the WORD_LIST used in arrays dont use the same allocation as in the rest
extern WORD_LIST* ShellVar_arrayToWordList(SHELL_VAR* var);

#define ShellVar_assocCreate(varname)          make_local_assoc_variable(varname, 0)
#define ShellVar_assocCreateGlobal(varname)    make_new_assoc_variable(varname)
#define ShellVar_assocUnset(var)               unbind_variable(var->name)
#define ShellVar_assocUnsetEl(var)             assoc_remove(assoc_cell(var->name))
#define ShellVar_assocGet(var,indexStr)        assoc_reference(assoc_cell(var), indexStr)
#define ShellVar_assocSet(var,indexStr,value)  assoc_insert(assoc_cell(var), savestring(indexStr), value)
#define ShellVar_assocSize(var)                assoc_num_elements(assoc_cell(var))

extern SHELL_VAR* ShellVar_findWithSuffix(char* varname, char* suffix);
extern void ShellVar_assocCopyElements(HASH_TABLE* dest, HASH_TABLE* source);



/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// WordList
// native WORD_LIST functions are mostly in make_cmd.h

#define WordList_toString(list)   string_list(list)

// this preserves the head so that if something else will free the list, it will free the node we add also
extern void WordList_unshift(WORD_LIST* list, char* str);
extern char* WordList_shift(WORD_LIST* list);


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// AssocItr

typedef struct {
    SHELL_VAR* vVar;
    HASH_TABLE* table;
    BUCKET_CONTENTS* item;
    int position;
} AssocItr;

extern BUCKET_CONTENTS* AssocItr_next(AssocItr* pI);
extern BUCKET_CONTENTS* AssocItr_init(AssocItr* pI, SHELL_VAR* vVar);


#endif // _bg_bashAPI_H_
