/* bgObjects - loadable builtin to optimize bg_objects.sh  */

/* See Makefile for compilation details. */


#include <config.h>

#if defined (HAVE_UNISTD_H)
#  include <unistd.h>
#endif
#include "bashansi.h"
#include <stdio.h>
#include <errno.h>
#include <regex.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

// to embed a breakpoint for debugging
#include <signal.h>
//use this-> raise(SIGINT);


#include "loadables.h"
#include "variables.h"
//#include "execute_cmd.h"
#include <stdarg.h>
#include <execute_cmd.h>

#if !defined (errno)
extern int errno;
#endif

typedef struct _BashObj {
  char name[255];
  char ref[300];
  SHELL_VAR* vThis;
  SHELL_VAR* vThisSys;
  SHELL_VAR* vCLASS;
  SHELL_VAR* vVMT;

  char* refClass;
  int superCallFlag;
} BashObj;

typedef enum {
    jt_object,
    jt_array,
    jt_objStart,
    jt_arrayStart,
    jt_objEnd,
    jt_arrayEnd,
    jt_value,
    jt_string,
    jt_number,
    jt_true,
    jt_false,
    jt_null,
    jt_comma,
    jt_colon,
    jt_error,
    jt_eof
} JSONType;

typedef struct {
    JSONType type;
    char* value;
} JSONToken;

typedef struct {
    char* buf;
    size_t bufAllocSize;
    size_t length;
    char* pos;
    char* end;
    char* filename;
} JSONScanner;


extern int _classUpdateVMT(char* className, int forceFlag);
extern void DeclareClassEnd(char* className);
extern BashObj* ConstructObject(WORD_LIST* args);
extern JSONToken* JSONScanner_getValue(JSONScanner* this);
extern int BashObj_setupMethodCallContext(BashObj* pObj);



/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Misc Functions

int isNumber(char* string) {
    if (string == 0 || *string=='\0')
      return 0;

    errno = 0;
    char *ep;
    strtoimax (string, &ep, 10);
    if (errno || ep == string)
      return 0;	/* errno is set on overflow or underflow */

    // if there are characters left over, its not a valid number
    if (*ep != '\0')
        return 0;

    return (1);
}

char* savestringn(char* x, int n)
{
    char *p=(char *)strncpy (xmalloc (1 + n), (x),n);
    p[n]='\0';
    return p;
}

char* save2string(char* s1, char* s2)
{
    char* p = xmalloc(strlen(s1) + strlen(s2 +1));
    strcpy(p, s1);
    strcat(p, s2);
    return p;
}

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
SHELL_VAR* ShellVar_findOrCreate(char* varname) {
    SHELL_VAR* var = find_variable(varname);
    if (!var)
        var = ShellVar_create(varname);
    return var;
}
#define ShellVar_unset(var)                     unbind_variable(var->name)
#define ShellVar_unsetS(varname)                unbind_variable(varname)
#define ShellVar_get(var)                       get_variable_value(var)
#define ShellVar_set(var,value)                 bind_variable_value(var,value,0)

// note that ShellVar_find will return the non-ref SHELL_VAR refered to by varname. ShellVar_refFind allows us to manipulate the ref
#define ShellVar_refFind(varname)              find_variable_noref(varname)
SHELL_VAR* ShellVar_refCreate(char* varname) {
    SHELL_VAR* var = make_local_variable(varname,0);
    VSETATTR(var, att_nameref);
    return var;
}
SHELL_VAR* ShellVar_refCreateSet(char* varname, char* value) {
    SHELL_VAR* var = make_local_variable(varname,0);
    VSETATTR(var, att_nameref);
    bind_variable_value(var,value,0);
    return var;
}
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
WORD_LIST* ShellVar_arrayToWordList(SHELL_VAR* var) {
    WORD_LIST* ret=NULL;
    for (ARRAY_ELEMENT* el = (array_cell(var))->lastref->prev; el!=(array_cell(var))->head; el=el->prev ) {
        ret = make_word_list(make_word(el->value), ret);
    }
    return ret;
}

#define ShellVar_assocCreate(varname)          make_local_assoc_variable(varname, 0)
#define ShellVar_assocCreateGlobal(varname)    make_new_assoc_variable(varname)
#define ShellVar_assocUnset(var)               unbind_variable(var->name)
#define ShellVar_assocUnsetEl(var)             assoc_remove(assoc_cell(var->name))
#define ShellVar_assocGet(var,indexStr)        assoc_reference(assoc_cell(var), indexStr)
#define ShellVar_assocSet(var,indexStr,value)  assoc_insert(assoc_cell(var), savestring(indexStr), value)
#define ShellVar_assocSize(var)                assoc_num_elements(assoc_cell(var))

SHELL_VAR* ShellVar_findWithSuffix(char* varname, char* suffix) {
    char* buf = xmalloc(strlen(varname)+strlen(suffix)+1);
    strcpy(buf, varname);
    strcat(buf,suffix);
    SHELL_VAR* vVar = find_variable(buf);
    xfree(buf);
    return vVar;
}


void ShellVar_assocCopyElements(HASH_TABLE* dest, HASH_TABLE* source) {
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

#define WordList_toString(list)   string_list(list)

// this preserves the head so that if something else will free the list, it will free the node we add also
void WordList_unshift(WORD_LIST* list, char* str)
{
    WORD_DESC* newWord = alloc_word_desc();
    newWord->word = list->word->word;
    newWord->flags = list->word->flags;
    WORD_LIST* wl = make_word_list(newWord, list->next);
    list->next = wl;
    list->word->word = savestring(str);
    make_word_flags(list->word, str);
}

char* WordList_shift(WORD_LIST* list)
{
    char* ret = savestring(list->word->word);
    WORD_LIST* temp = list;
    list = list->next;
    temp->next = NULL;
    dispose_words(temp);
    return ret;
}


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// AssocItr

typedef struct {
    SHELL_VAR* vVar;
    HASH_TABLE* table;
    BUCKET_CONTENTS* item;
    int position;
} AssocItr;

BUCKET_CONTENTS* AssocItr_next(AssocItr* pI) {
    // first iterate the current bucket linked list if we are not at the end
    if (pI->item && pI->item->next) {
        pI->item = pI->item->next;

    // next, goto the start linked list of the next non-empty bucket
    } else if (HASH_ENTRIES (pI->table) != 0 && pI->table && pI->position < pI->table->nbuckets) {
        while ((pI->position < pI->table->nbuckets) && 0==(pI->item=hash_items(pI->position, pI->table))) pI->position++;
        pI->position++;
    } else {
        pI->item = NULL;
    }
    return pI->item;
}
BUCKET_CONTENTS* AssocItr_init(AssocItr* pI, SHELL_VAR* vVar) {
    pI->vVar=vVar;
    pI->table = assoc_cell(pI->vVar);
    pI->position=0;
    pI->item=NULL;
    pI->item = AssocItr_next(pI);
    return pI->item;
}


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// bgtrace

// 0 == no bgtrace
// 1 == minmal bgtrace
// 2 == more tracing
// 9 == all tracing
#define bgtraceLevel 0

#if bgtraceLevel > 0
#   define bgtrace0(level, fmt)                _bgtrace(level,fmt)
#   define bgtrace1(level, fmt,p1)             _bgtrace(level,fmt,p1)
#   define bgtrace2(level, fmt,p1,p2)          _bgtrace(level,fmt,p1,p2)
#   define bgtrace3(level, fmt,p1,p2,p3)       _bgtrace(level,fmt,p1,p2,p3)
#   define bgtrace4(level, fmt,p1,p2,p3,p4)    _bgtrace(level,fmt,p1,p2,p3,p4)
#   define bgtrace5(level, fmt,p1,p2,p3,p4,p5) _bgtrace(level,fmt,p1,p2,p3,p4,p5)
#   define bgtracePush() bgtraceIndentLevel++
#   define bgtracePop()  bgtraceIndentLevel--
#else
#   define bgtrace0(level,fmt)
#   define bgtrace1(level,fmt,p1)
#   define bgtrace2(level,fmt,p1,p2)
#   define bgtrace3(level,fmt,p1,p2,p3)
#   define bgtrace4(level,fmt,p1,p2,p3,p4)
#   define bgtrace5(level,fmt,p1,p2,p3,p4,p5)
#   define bgtracePush()
#   define bgtracePop()
#endif

FILE* _bgtraceFD=NULL;
static int bgtraceIndentLevel=0;
void bgtraceOn()
{
    if (!_bgtraceFD) {
        _bgtraceFD=fopen("/tmp/bgtrace.out","a+");
        if (!_bgtraceFD)
            fprintf(stderr, "FAILED to open trace file '/tmp/bgtrace.out' errno='%d'\n", errno);
        else
            fprintf(_bgtraceFD, "BASH bgObjects trace started\n");
    }
}
int _bgtrace(int level, char* fmt, ...) {
    if (!_bgtraceFD || level>bgtraceLevel) return 1;
    va_list args;
    SH_VA_START (args, fmt);
    fprintf(_bgtraceFD, "%*s", bgtraceIndentLevel*3,"");
    vfprintf(_bgtraceFD, fmt, args);
    fflush(_bgtraceFD);
    return 1; // so that we can use bgtrace in condition
}


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// BGString
// auto growing string buffer that it null terminated but also can contain nulls in the string to make a string list
// that can be iterated. A typical senario is getting a whitespace separated string from a SHELL_VAR, turning whitespace
// to nulls and then iterating the words.
typedef struct {
    char* buf;
    int len;
    int allocatedLen;
    char* itr;
} BGString;

void BGString_init(BGString* pStr, int allocatedLen) {
    pStr->len=0;
    pStr->allocatedLen=allocatedLen;
    pStr->buf=xmalloc(pStr->allocatedLen);
    pStr->buf[pStr->len]='\0';
    pStr->itr=NULL;
}
void BGString_initFromStr(BGString* pStr, char* s) {
    pStr->len=strlen(s);
    pStr->allocatedLen=pStr->len+1;
    pStr->buf=xmalloc(pStr->allocatedLen);
    strcpy(pStr->buf, s);
    pStr->itr=NULL;
}
void BGString_initFromAllocatedStr(BGString* pStr, char* s) {
    pStr->len=strlen(s);
    pStr->allocatedLen=pStr->len+1;
    pStr->buf = s;
    pStr->itr=NULL;
}
void BGString_free(BGString* pStr) {
    if (pStr->buf) {
        xfree(pStr->buf);
        pStr->buf=NULL;
        pStr->len=0;
        pStr->allocatedLen=0;
        pStr->itr=NULL;
    }
}
void BGString_appendn(BGString* pStr, char* s, int sLen, char* separator) {
    if (!s || !*s)
        return;
    int separatorLen=(pStr->len>0) ? strlen((separator)?separator:"") : 0;
    if (pStr->len+sLen+separatorLen+1 > pStr->allocatedLen) {
        int itrPos=(pStr->itr) ? (pStr->itr -pStr->buf) : -1;
        pStr->allocatedLen=pStr->allocatedLen * 2 + sLen+separatorLen;
        char* temp=xmalloc(pStr->allocatedLen);
        memcpy(temp, pStr->buf, pStr->len+1);
        xfree(pStr->buf);
        pStr->buf=temp;
        pStr->itr=(itrPos>-1) ? pStr->buf+itrPos : NULL;
    }
    if (pStr->len > 0) {
        strcpy(pStr->buf+pStr->len, (separator)?separator:"");
        pStr->len+=separatorLen;
    }
    strncpy(pStr->buf+pStr->len, s, sLen);
    pStr->len+=sLen;
    pStr->buf[pStr->len]='\0';
}
void BGString_append(BGString* pStr, char* s, char* separator) {
    if (!s || !*s)
        return;
    BGString_appendn(pStr, s, strlen(s), separator);
}
void BGString_copy(BGString* pStr, char* s) {
    pStr->itr = NULL;
    *pStr->buf = '\0';
    pStr->len = 0;
    if (!s || !*s)
        return;
    BGString_appendn(pStr, s, strlen(s), "");
}
void BGString_replaceWhitespaceWithNulls(BGString* pStr) {
    for (register int i=0; i<pStr->len; i++)
        if (whitespace(pStr->buf[i]))
            pStr->buf[i]='\0';
}
char* BGString_nextWord(BGString* pStr) {
    char* pEnd=pStr->buf + pStr->len;
    if (!pStr->itr) {
        pStr->itr=pStr->buf;
        while (pStr->itr < pEnd && *pStr->itr=='\0') pStr->itr++;
        return pStr->itr;
    } else if (pStr->itr >= pEnd) {
        return NULL;
    } else {
        pStr->itr+=strlen(pStr->itr);
        // if the original string list had consequtive whitespace, we have to skip over consequtive nulls
        while (pStr->itr < pEnd && *pStr->itr=='\0') pStr->itr++;
        return (pStr->itr < pEnd) ? pStr->itr : NULL;
    }
}




/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// BashObjects MemberType

typedef enum {
    mt_unknown,            // we do not (yet) know
    mt_nullVar,            // last member of chain did not exist - syntax tells us a member variable is expected
    mt_nullMethod,         // last member of chain did not exist - syntax tells us a member function is expected
    mt_nullEither,         // last member of chain did not exist - syntax could be either a var or function
    mt_object,             // last member of chain is a member var that contains an <objRef>
    mt_primitive,          // last member of chain is a member var that does not contain an <objRef>
    mt_method,             // last member of chain is a member function
    mt_both,               // last member of chain is both a member variable and a member function
    mt_self,               // there was no chain. an object is being directly invoked
    mt_invalidExpression   // could not parse completely because of a syntax error
} MemberType;

char* MemberTypeToString(MemberType mt, char* errorMsg, char* _rsvMemberValue)
{
    char* temp=NULL;
    switch (mt) {
        case mt_nullVar:     return savestring("null:memberVar");
        case mt_nullMethod:  return savestring("null:method");
        case mt_nullEither:  return savestring("null:either");
        case mt_object:
            if (!_rsvMemberValue)
                return savestring("object");
            char* memberClass=_rsvMemberValue+12;
            while (*memberClass && whitespace(*memberClass)) memberClass++;
            while (*memberClass && !whitespace(*memberClass)) memberClass++;
            while (*memberClass && whitespace(*memberClass)) memberClass++;
            char* memberClassEnd=memberClass;
            while (*memberClassEnd && !whitespace(*memberClassEnd)) memberClassEnd++;
            // object:<className>\0
            // 012345678901234567890
            char* temp=xmalloc(8+(memberClassEnd-memberClass));
            strcpy(temp, "object:");
            strncat(temp, memberClass, (memberClassEnd-memberClass));
            temp[7+(memberClassEnd-memberClass)]='\0';
            return temp;
        case mt_primitive:   return savestring("primitive");
        case mt_method:      return savestring("method");
        case mt_both:        return savestring("both");
        case mt_self:        return savestring("self");
        case mt_invalidExpression:
            temp=xmalloc(19+((errorMsg)?strlen(errorMsg):0));
            strcpy(temp,"invalidExpression");
            if (errorMsg) {
                strcat(temp,":");
                strcat(temp,errorMsg);
            }
            return temp;
        default: return "UNKNOWN MemberType";
    }
    return temp;
};

int setErrorMsg(char* fmt, ...)
{
    _bgtrace(0, "!!!ERROR: \n");
    va_list args;
    SH_VA_START (args, fmt);

    char* outputMsg=xmalloc(512);
    vsnprintf(outputMsg, 511, fmt, args);

    // local _rsvMemberType="$_rsvMemberType"
    char* temp = MemberTypeToString(mt_invalidExpression,outputMsg,NULL);
    ShellVar_createSet("_rsvMemberType", temp);

    _bgtrace(0,"   %s\n",temp);

    xfree(temp);
    xfree(outputMsg);

    //builtin_error(fmt, p1, p2, p3, p4, p5);
//    run_unwind_frame ("bgObjects");
    return (EXECUTION_FAILURE);
}


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// BashObjRef

typedef struct {
  char oid[255];
  char className[255];
  int superCallFlag;
} BashObjRef;

char* extractOID(char* objRef) {
    if (!objRef || strlen(objRef)<=13)
        return NULL;
    char* oid=objRef+12;
    while (*oid && whitespace(*oid)) oid++;
    char* oidEnd=oid;
    while (*oidEnd && !whitespace(*oidEnd)) oidEnd++;
    return savestringn(oid, (oidEnd-oid));
}

// TODO: where is BashObjRef_free?

// '_bgclassCall <oid> <className> <hierarchyLevel> | '
int BashObjRef_init(BashObjRef* pRef, char* objRefStr)
{
    // skip over the '_bgclassCall' token
    char* ps=objRefStr;
    if (strncmp(ps,"_bgclassCall",12)!=0)
        return 0; // setErrorMsg("BashObjRef_init: malformed <objRef> does not start with '_bgclassCall'\n\tobjRef='%s'\n", objRefStr);
    ps+=12;
    while (ps && *ps && whitespace(*ps)) ps++;

    // ps should be pointing at the start of the <oid> now
    char* pe=ps;
    while (pe && *pe && !whitespace(*pe)) pe++;
    if ((pe-ps) > sizeof(pRef->oid)-1)
        return 0; // setErrorMsg("BashObjRef_init: oid name is too large (>%d)\n\tobjRef='%s'\n", sizeof(pRef->oid)-1, objRefStr);
    strncpy(pRef->oid, ps, pe-ps); pRef->oid[pe-ps]='\0';
    while (pe && *pe && whitespace(*pe)) pe++;

    // ps should be pointing at the start of <className> now
    ps=pe;
    while (pe && *pe && !whitespace(*pe)) pe++;
    if ((pe-ps) > sizeof(pRef->className)-1)
        return 0; // setErrorMsg("BashObjRef_init: className is too large (>%d)\n\tobjRef='%s'\n", sizeof(pRef->className)-1, objRefStr);
    strncpy(pRef->className, ps, pe-ps); pRef->className[pe-ps]='\0';
    while (pe && *pe && whitespace(*pe)) pe++;

    // ps should be pointing at the start of <hierarchyLevel> now
    ps=pe;
    pRef->superCallFlag=strtol(ps,&pe,10);
    while (pe && *pe && whitespace(*pe)) pe++;

    // should be pointing at the | character now.
    ps=pe;
    if (*ps != '|')
        return 0; // setErrorMsg("BashObjRef_init: malformed <objRef> missing the '|' character at '%s'.\n\tobjRef='%s'\n", ps, objRefStr);
    return 1;
}


// reads one \n terminated line from <file> into <buf> and returns the number of bytes moved into <buf>
// the terminating \n is not included in the <buf> string. The <buf> will always be null terminated.
// The return value will be equal to strlen(buf).
// <buf> should be allocated with a malloc or equivalent function and pBufAllocSize contain the size of the
// allocation. If <buf> is not large enough to hold the entire line, xremalloc will be used to increase its
// size and pBufAllocSize will be updated to reflect the new allocation size.
size_t freadline(FILE* file, char* buf, size_t* pBufAllocSize)
{
    buf[0]='\0';
    char* readResult = fgets(buf, *pBufAllocSize,file);
    size_t readLen = strlen(buf);
    while (readResult && (buf[readLen-1] != '\n')) {
        *pBufAllocSize *= 2;
        buf = xrealloc(buf, *pBufAllocSize);

        readResult = fgets(buf+readLen, *pBufAllocSize/2,file);
        readLen = strlen(buf+readLen)+readLen;
    }
    if (buf[readLen-1] == '\n') {
        buf[readLen-1] = '\0';
        readLen--;
    }
    return readLen;
}

// returns true(1) if <value> matches <filter> or if <filter> is null
// returns false(0) if <filter> is specified and <value> does not match it.
// Params:
//    <filter> : NULL, "", or "^$" means any <value> matches without testing the <value>.
//               otherwise <filter> is a regex that applies to <value>.
//               <filter> is automatically anchored meaning that it will have a leading '^' and trailing '$' added to it if it does
//               not already have them.
//    <value>  : the value to match against <filter>
int matchFilter(char* filter, char* value) {
    if (!filter || !*filter || strcmp(filter,"^$")==0)
        return 1;

    regex_t regex;
    if (regcomp(&regex, filter, REG_EXTENDED)) {
        fprintf(stderr, "error: invalid regex filter (%s)\n", filter);
        return 1;
    }
    return regexec(&regex, value, 0, NULL, 0) == 0;

    // int leadingFlag = 0;
    // if (*filter == '*') {
    //     leadingFlag = 1;
    //     filter++;
    // }
    //
    // int filterLen = strlen(filter);
    // int trailingFlag = 0;
    // if (filter[filterLen-1] == '*') {
    //     trailingFlag = 1;
    //     filterLen--;
    // }
    //
    // if (leadingFlag+trailingFlag == 0)
    //     return (strcmp(filter, value)==0);
    // else if (leadingFlag) {
    //     char* t = strstr(value, filter);
    //     if (t && strlen(t) != filterLen)
    //         t = NULL;
    //     return (t != NULL);
    // } else
    //     return (strncmp(filter, value, filterLen)==0);
}

typedef enum {mf_pkgName, mf_assetType, mf_assetName, mf_assetPath} ManifestField;
typedef struct {
    char* line;
    char* pkgName;
    char* assetType;
    char* assetName;
    char* assetPath;
    int alloced;
} ManifestRecord;
typedef int (*ManifestFilterFn)(ManifestRecord* rec, ManifestRecord* target);
ManifestRecord* ManifestRecord_assign(ManifestRecord* ret, char* pkgName, char* assetType, char* assetName, char* assetPath)
{
    ret->line      = NULL;
    ret->pkgName   = pkgName;
    ret->assetType = assetType;
    ret->assetName = assetName;
    ret->assetPath = assetPath;
    ret->alloced = 0;
    return ret;
}
ManifestRecord* ManifestRecord_new(char* pkgName, char* assetType, char* assetName, char* assetPath)
{
    ManifestRecord* ret = xmalloc(sizeof(ManifestRecord));
    return ManifestRecord_assign(ret, pkgName, assetType, assetName, assetPath);
}
ManifestRecord* ManifestRecord_newFromLine(char* line)
{
    ManifestRecord* ret = xmalloc(sizeof(ManifestRecord));
    ret->line = savestring(line);
    BGString parser; BGString_initFromAllocatedStr(&parser, line);
    BGString_replaceWhitespaceWithNulls(&parser);
    ret->pkgName =   savestring(BGString_nextWord(&parser));
    ret->assetType = savestring(BGString_nextWord(&parser));
    ret->assetName = savestring(BGString_nextWord(&parser));
    ret->assetPath = savestring(BGString_nextWord(&parser));
    ret->alloced = 1;
    // we do not free parser b/c the caller owns char* line and we never call append so we did not change the allocation.
    return ret;
}
ManifestRecord* ManifestRecord_save(ManifestRecord* dst, ManifestRecord* src)
{
    dst->line      = savestring(src->line);
    dst->pkgName   = savestring(src->pkgName);
    dst->assetType = savestring(src->assetType);
    dst->assetName = savestring(src->assetName);
    dst->assetPath = savestring(src->assetPath);
    dst->alloced = 1;
    return dst;
}

void ManifestRecord_free(ManifestRecord* rec)
{
    if (rec->alloced) {
        xfree(rec->line);      rec->line      = NULL;
        xfree(rec->pkgName);   rec->pkgName   = NULL;
        xfree(rec->assetType); rec->assetType = NULL;
        xfree(rec->assetName); rec->assetName = NULL;
        xfree(rec->assetPath); rec->assetPath = NULL;
        rec->alloced = 0;
    }
}

char* manifestExpandOutStr(ManifestRecord* rec, char* outputStr)
{
    BGString retVal;
    BGString_init(&retVal, 500);
    char* p = outputStr;
    while (p && *p) {
        char* p2 = p;
        while (*p2 && *p2 != '$')
            p2++;
        if (*p2 == '$') {
            BGString_appendn(&retVal,p, (p2-p),"");
            p = p2+1;
            switch (*p) {
                case '0': BGString_append(&retVal,rec->line     , ""); break;
                case '1': BGString_append(&retVal,rec->pkgName  , ""); break;
                case '2': BGString_append(&retVal,rec->assetType, ""); break;
                case '3': BGString_append(&retVal,rec->assetName, ""); break;
                case '4': BGString_append(&retVal,rec->assetPath, ""); break;
                default:
                    BGString_appendn(&retVal,p2,2,"");
                    break;
            }
            p++;
        } else {
            BGString_append(&retVal,p,"");
            p = p2;
        }
    }
    return retVal.buf;
}

// Pass in a record <target> containing match criteria and it returns the first record in the hostmanifest file that matches the
// criteria or one with all null fields if no record matches.
ManifestRecord manifestGet(char* manFile, char* outputStr, ManifestRecord* target, ManifestFilterFn filterFn)
{
    ManifestRecord retVal; ManifestRecord_assign(&retVal, NULL,NULL,NULL,NULL);

    // if the caller did not specify any filters, we interpret it as there can not be any match.
    if (!target->pkgName && !target->assetType && !target->assetName && !target->assetPath && !filterFn)
        return retVal;

    if (!manFile)
        manFile = ShellVar_get(ShellVar_find("bgVinstalledManifest"));
    if (!manFile || strcmp(manFile,"")==0)
        manFile = "/var/lib/bg-core/manifest";
    FILE* manFileFD = fopen(manFile, "r");
    if (!manFileFD)
        return retVal;

    // bg-core\0       awkDataSchema\0   plugins\0         /usr/lib/plugins.awkDataSchema\0
    // '--<pkgName>'   '--<assetType>'   '--<assetName>'   '--<assetPath >'
    size_t bufSize = 500;
    char* buf =  xmalloc(bufSize);
    ManifestRecord rec;    ManifestRecord_assign(&rec,    NULL,NULL,NULL,NULL);
    BGString parser; BGString_init(&parser, 500);
    while (freadline(manFileFD, buf, &bufSize)) {
        rec.line = buf;

        // make a copy of buf in parser which modifies and consumes it.
        BGString_copy(&parser, buf);
        BGString_replaceWhitespaceWithNulls(&parser);
        rec.pkgName = BGString_nextWord(&parser);
        if (!matchFilter(target->pkgName ,rec.pkgName))
            continue;

        rec.assetType = BGString_nextWord(&parser);
        if (!matchFilter(target->assetType ,rec.assetType))
            continue;

        rec.assetName = BGString_nextWord(&parser);
        if (!matchFilter(target->assetName ,rec.assetName))
            continue;

        rec.assetPath = BGString_nextWord(&parser);
        if (!matchFilter(target->assetPath ,rec.assetPath))
            continue;

        if (filterFn && (!(*filterFn)(&rec, target)) ) {
            continue;
        }

        // we get here only when all the specified filters matched the record so break the loop to return the result
        // save the first match to return.
        // TODO: change this to return a dynamic array of ManifestRecord's whcih the caller must free.
        if (!retVal.assetType)
            ManifestRecord_save(&retVal, &rec);

        // TODO: change this to respect the standard output options instead of only printing to stdout
        if (outputStr) {
            char* outLine = manifestExpandOutStr(&rec, outputStr);
            printf("%s\n",outLine);
        }
    }

    xfree(buf);
    BGString_free(&parser);
    return retVal;
}

#define im_forceFlag       0x01
#define im_stopOnErrorFlag 0x02
#define im_quietFlag       0x04
#define im_getPathFlag     0x08

int importManifestCriteria(ManifestRecord* rec, ManifestRecord* target)
{
    // remove any leading paths
    char* target_assetName = target->assetName;
    for (char* t = target_assetName; t && *t; t++)
        if (*t == '/' && *(t+1)!='\0')
            target_assetName = t+1;

    // find the last '.' or '\0' if there is none
    char* ext = target_assetName + strlen(target_assetName);
    for (char* t = ext; t && *t; t++)
        if (*t == '.')
            ext = t;

    // assetName is without path and without ext
    char* target_assetNameWOExt = savestringn(target_assetName, ext-target_assetName);

    // remove any leading paths
    char* rec_pathWOFolders = rec->assetPath;
    for (char* t = rec_pathWOFolders; t && *t; t++)
        if (*t == '/' && *(t+1)!='\0')
            rec_pathWOFolders = t+1;


    int matched = 0;
    if (strncmp(rec->assetType, "lib.script.bash", 15)==0 && (strcmp(rec->assetName,target_assetNameWOExt)==0 || strcmp(rec->assetName,target_assetName)==0) )
        matched = 1;
    if (!matched && strcmp(rec_pathWOFolders,target_assetName)==0 && (strncmp(rec->assetType, "plugin", 15)==0 || strncmp(rec->assetType, "unitTest", 15)==0))
        matched = 1;
    xfree(target_assetNameWOExt);
    return matched;
}

int importBashLibrary(char* scriptName, int flags, char** retVar)
{
    SHELL_VAR* vImportedLibraries = ShellVar_find("_importedLibraries");
    if (!vImportedLibraries) {
        vImportedLibraries = ShellVar_assocCreateGlobal("vImportedLibraries");
    }
    SHELL_VAR* vL1 = ShellVar_findOrCreate("L1");
    SHELL_VAR* vL2 = ShellVar_findOrCreate("L2");
    ShellVar_set(vL1,"");
    ShellVar_set(vL2,"");
    char* lookupName = save2string("lib:",scriptName);
    if (flags&im_forceFlag==0 && flags&im_getPathFlag==0 && strcmp(ShellVar_assocGet(vImportedLibraries, lookupName),"")!=0) {
        return EXECUTION_SUCCESS;
    }

    ManifestRecord targetRec;
    ManifestRecord foundManRec = manifestGet(NULL, NULL, ManifestRecord_assign(&targetRec, scriptName,NULL,NULL,NULL), importManifestCriteria);
    if (!foundManRec.assetPath) {
        // TODO: the bash import function falls back to searching the bgVinstalledPaths ENV var and /usr/lib/ when its not found in the manifest
        //       but that might be phased out so not sure if we ever need to implement it is C. It is usefull during development, though if updating the manifest is not perfect.
        ShellVar_set(vL1,"assertError import could not find bash script library in host manifest");
        xfree(lookupName);
        return 0;
    }

    // if we are only asked to get the path, do that and return
    if (retVar && *retVar) {
        *retVar = savestring(foundManRec.assetPath);
        xfree(foundManRec.assetPath);
        xfree(lookupName);
        return 1;
    }

    // TODO: SECURITY:  the bash import function checks if [ "$bgSourceOnlyUnchangable" ] && [ -w "$foundManRec.assetPath" ]

    ShellVar_assocSet(vImportedLibraries, lookupName, foundManRec.assetPath);


    ManifestRecord_free(&foundManRec);
    xfree(lookupName);
    return 0;
}

SHELL_VAR* assertClassExists(char* className, int* pErr) {
    if (!className) {
        if (pErr) *pErr=1;
        setErrorMsg("className is empty\n");
        return NULL;
    }
    SHELL_VAR* vClass = ShellVar_find(className);
    if (!vClass) {
        // sh_builtin_func_t* sourceBltn = find_shell_builtin("source");
        // int result = execute_builtin (sourceBltn, , flags, 0);
        SHELL_VAR* vImportFn = ShellVar_find("import");
        if (!vImportFn) {
            setErrorMsg("could not find shell function import");
            return NULL;
        }
        WORD_LIST* args = NULL;
        char* classLibName = save2string(className, ".sh");
        args = make_word_list(make_word(classLibName), args);
        xfree(classLibName);
        execute_shell_function(vImportFn, args);
        dispose_words(args);


        if (pErr) *pErr=1;
        setErrorMsg("Class does not exist\n\tclassName='%s'\n", className);
        return NULL;
    }
    if (!assoc_p(vClass) && !array_p(vClass)) {
        if (pErr) *pErr=1;
        setErrorMsg("Error - <refClass> (%s) is not an array\n", className);
        return NULL;
    }
    if (pErr) *pErr=0;
    return vClass;
}


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// BashObj

void BashObj_makeRef(BashObj* pObj)
{
    // '_bgclassCall <oid> <class> 0 | '
    //  12  + 8 + 1 + strlen(<oid>) + strlen(<class>)
    strcpy(pObj->ref,"_bgclassCall ");
    strcat(pObj->ref,pObj->name);
    strcat(pObj->ref," ");
    strcat(pObj->ref,pObj->vCLASS->name);
    strcat(pObj->ref," 0 | ");
}

void BashObj_makeVMT(BashObj* pObj)
{
    if (!pObj->superCallFlag) {
        // pObj->vVMT = "${_this[_VMT]:-${_this[_CLASS]}_vmt}"
        char* vmtName = (pObj->vThisSys)?ShellVar_assocGet(pObj->vThisSys, "_VMT"):NULL;
        if (!vmtName) {
            vmtName = save2string(pObj->vCLASS->name, "_vmt");
            pObj->vVMT=ShellVar_find(vmtName);
            xfree(vmtName);
        } else {
            pObj->vVMT=ShellVar_find(vmtName);
        }
    } else {
        // we get here when we are processing a $super.<method> call. $super is only available in methods and the refClass in the
        // $super <objRef> is the class of the method that is running and hierarchyLevel is !="0". We need to set the VMT to the
        // baseClass of the the refClass from the $super <objRef>. If there are no base classes left, we set the VMT to empty_vmt
        // to indicate that there is nother left to call.
        // pObj->vVMT = "${refClass[baseClass]}_vmt"
        int errFlag = 0;
        SHELL_VAR* vRefClass = assertClassExists(pObj->refClass, &errFlag);
        if (errFlag!=0)
            return;// EXECUTION_FAILURE;
        char* refBaseClass = ShellVar_assocGet(vRefClass, "baseClass");
        if (refBaseClass) {
            pObj->vVMT = ShellVar_findWithSuffix(refBaseClass, "_vmt");
        } else {
            pObj->vVMT = ShellVar_find("empty_vmt");
        }
    }
}


int BashObj_init(BashObj* pObj, char* name, char* refClass, char* hierarchyLevel)
{
    if (strlen(name) >200) return setErrorMsg("BashObj_init: error: name is too long (>200)\nname='%s'\n", name);
    int errFlag=0;

    pObj->refClass=refClass;
    pObj->superCallFlag = (hierarchyLevel && strcmp(hierarchyLevel, "0")!=0);

    pObj->vThis=ShellVar_find(name);
    if (!(pObj->vThis))
        return setErrorMsg("Error - <oid> (%s) does not exist \n", name);
    if (!assoc_p(pObj->vThis) && !array_p(pObj->vThis)) {
        char* objRefString = ShellVar_get(pObj->vThis);
        BashObjRef oRef;
        if (!BashObjRef_init(&oRef, objRefString))
            return setErrorMsg ("Error - <oid> (%s) is not an array nor does it contain an <objRef> string\n", name);
        pObj->vThis=ShellVar_find(oRef.oid);
    }

    // an object instance may or may not have a separate associative array for its system variables. If not, system vars are mixed
    // in with the object members but we can tell them apart because system vars must begin with an '_'.  [0] can also be a system
    // var but it is special because its functionality depends on putting it in the pThis array. The class attribute [defaultIndex]
    // determines if pThis[0] is a system var or a memberVar
    strcpy(pObj->name,pObj->vThis->name); strcat(pObj->name,"_sys");
    pObj->vThisSys=ShellVar_find(pObj->name);
    if (!pObj->vThisSys)
        pObj->vThisSys=pObj->vThis;

    // lookup the vCLASS array
    char* _CLASS = ShellVar_assocGet(pObj->vThisSys, "_CLASS");
    if (!_CLASS)
        return setErrorMsg("Error - malformed object instance. missing the _CLASS system variable. instance='%s'\n", pObj->vThisSys->name);
    pObj->vCLASS = assertClassExists(_CLASS, &errFlag); if (errFlag!=0) return EXECUTION_FAILURE;

    // lookup the VMT array taking into account that an obj instance may have a separate VMT unique to it (_this[_VMT]) and if this
    // is a $super.<method>... call, it is the VMT of the baseClass of the calling <objRef>'s refClass.
    BashObj_makeVMT(pObj);
    if (!pObj->vVMT)
        return setErrorMsg("Error - _VMT (virtual method table) is missing.\n\tobj='%s'\n\tclass='%s'\n\trefClass='%s'\n\tsuperCallFlag='%d'\n", name, _CLASS, pObj->refClass,pObj->superCallFlag);

    // we used pObj->name as a buffer to try lookups for several different names but now lets put the actual name in it
    strcpy(pObj->name, pObj->vThis->name);

    BashObj_makeRef(pObj);
    return 1;
}

BashObj* BashObj_copy(BashObj* that) {
    BashObj* this = xmalloc(sizeof(BashObj));
    strcpy(this->name, that->name );
    strcpy(this->ref , that->ref  );
    this->vThis   = that->vThis     ;
    this->vThisSys= that->vThisSys  ;
    this->vCLASS  = that->vCLASS    ;
    this->vVMT    = that->vVMT      ;

    this->refClass = that->refClass;
    this->superCallFlag = that->superCallFlag;
    return this;
}

BashObj* BashObj_find(char* name, char* refClass, char* hierarchyLevel) {
    BashObj* pObj = xmalloc(sizeof(BashObj));
    if (!BashObj_init(pObj, name, refClass, hierarchyLevel)) {
        xfree(pObj);
        return NULL;
    }
    return pObj;
}

// c implementation of the bash library function
// returns "heap_A_XXXXXXXXX" where X's are random chars
// TODO: add support for passing in attributes like the bash version does
SHELL_VAR* varNewHeapVar(char* attributes) {
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

    if (!attributes || !strcasestr(attributes,"a"))
        return ShellVar_createGlobal(buf);
    else if (strstr(attributes, "a"))
        return ShellVar_arrayCreateGlobal(buf);
    else
        return ShellVar_assocCreateGlobal(buf);
}

// this is a convenience function for C code to call ConstructObject which converts the C args to a WORD_LIST
// vObjVar can be NULL if the caller only needs the BashObj* and does not need an objvar SHELL_VAR set.
BashObj* BashObj_makeNewObject(char* _CLASS, SHELL_VAR* vObjVar, ...)
{
    bgtrace1(1,"START BashObj_makeNewObject '%s'\n",_CLASS);
    bgtracePush();
    SHELL_VAR* vTmpObjVar = NULL;
    if (!vObjVar)
        vObjVar = vTmpObjVar = varNewHeapVar("");

    // build args in reverse order...
    WORD_LIST* args = make_word_list(make_word(vObjVar->name), NULL);
    args = make_word_list(make_word(_CLASS), args);
    BashObj* pObj = ConstructObject(args);
    dispose_words(args);
    if (vTmpObjVar)
        ShellVar_unset(vTmpObjVar);
    bgtracePop();
    bgtrace1(1, "END BashObj_makeNewObject '%s'\n",_CLASS);
    return pObj;
}

BashObj* ConstructObject(WORD_LIST* args)
{
    bgtrace1(1,"START ConstructObject \n", WordList_toString(args));
    bgtracePush();
    char* _CLASS = args->word->word;
    args = args->next;

    // If the _CLASS word is in the form "<classname>::<data>" its a dynamic construction call that delegates the construction to
    // a user defined <classname>::ConstructObject function
    // TODO: what should we do if it has the data but the function does not exist or vica versa
    char* dynData = strstr(_CLASS, "::");
    if (dynData) {
        char* s = dynData;
        dynData = savestring(s+2);
        *s = '\0'; // now _CLASS has just the <classname>\0
        bgtrace1(1,"dynData='%s'\n",dynData);
        bgtrace1(1,"_CLASS='%s'\n",_CLASS);
    }

    DeclareClassEnd(_CLASS);

    SHELL_VAR* vClass = assertClassExists(_CLASS, NULL);
    if (!vClass) {
        setErrorMsg("Class ('%s') does not exist", _CLASS);
        bgtracePop();
        bgtrace0(1,"END 1 ConstructObject \n");
        return NULL;
    }

    // ### support dynamic base class implemented construction
    char* tstr = save2string(_CLASS, "::ConstructObject");
    SHELL_VAR* fDynCtor = find_function(tstr);
    if (fDynCtor) {
        bgtrace1(1,"dyn ctor exists ...'%s'\n",tstr);
        char* objVar = args->word->word;
        WordList_unshift(args, (dynData)?dynData:"");
        // if the class ctor returns true, we are done, otherwise it wants us to continue constructing the object
        if (execute_shell_function(fDynCtor, args) == 0) {
            xfree(dynData);
            bgtracePop();
            bgtrace0(1,"END 2 ConstructObject \n");
            return BashObj_find(objVar, NULL,NULL);
        }
    }
    xfree(dynData);
    xfree(tstr); tstr=NULL;

    // get some information from the class about how we construct an instance
    char* oidAttributes = ShellVar_assocGet(vClass, "oidAttributes");
    int isNumericArray = (oidAttributes && strstr(oidAttributes, "a")) ? 1 : 0;

    // ### deal with the return variable
    if (!args) {
        setErrorMsg("<objVar> is a required argument to ConstructObject");
        bgtracePop();
        bgtrace0(1, "END 3 ConstructObject \n");
        return NULL;
    }
    char* _objRefVar = args->word->word;
    args = args->next;

    SHELL_VAR* vObjVar = ShellVar_find(_objRefVar);

    BashObj* newObj = xmalloc(sizeof(BashObj));
    *newObj->name='\n';
    *newObj->ref='\n';
    newObj->vCLASS = vClass;
    newObj->refClass=NULL;
    newObj->superCallFlag=0;

    // this is the case of 'local obj=$(NewObject ...)' where obj gets set to an objRef to a heap_ array that does not exist in its
    // process space but can be restored from the tmp file that NewObject creates
    // The first time _bgclassCall is invoked with that objRef, it will call us with the OID of the ref to create it
    if (!vObjVar && strncasecmp(_objRefVar, "heap_a", 6)==0) {
        strcpy(newObj->name, _objRefVar);
        if (isNumericArray)
            newObj->vThis = ShellVar_arrayCreateGlobal(_objRefVar);
        else
            newObj->vThis = ShellVar_assocCreateGlobal(_objRefVar);
        char* tstr = save2string(newObj->vThis->name,"_sys");
        newObj->vThisSys = ShellVar_assocCreateGlobal(tstr);
        xfree(tstr);
        BashObj_makeRef(newObj);
//        ConstructObjectFromJson
        bgtracePop();
        bgtrace0(1,"END 4 ConstructObject \n");
        return NULL;

    // its an unitialized -n nameRef
    } else if (vObjVar && nameref_p(vObjVar) && invisible_p(vObjVar)) {
        newObj->vThis = varNewHeapVar((oidAttributes)?oidAttributes:"A");
        ShellVar_set(vObjVar, newObj->vThis->name);

        tstr = save2string(newObj->vThis->name,"_sys");
        newObj->vThisSys = ShellVar_assocCreateGlobal(tstr);
        xfree(tstr);
        BashObj_makeRef(newObj);

    // its an 'A' (associative) array that we can use as our object and the _this array
    // we dont create a separate <oid>_sys array because we cant create it in the same scope as <oid>_sys in the bash implementation
    } else if (vObjVar && assoc_p(vObjVar)) {
        newObj->vThis = vObjVar;
        newObj->vThisSys = newObj->vThis;
        BashObj_makeRef(newObj);

    // its an 'a' (numeric) array that we can use as our object
	// Note that this case is problematic and maybe should assert an error because there is no way to create the _sys array in the
	// same scope that the caller created the OID array. We create a "${_OID}_sys" global array but that polutes the global namespace
	// and can collide with other object instances.
    } else if (vObjVar && assoc_p(vObjVar)) {
        newObj->vThis = vObjVar;

        tstr = save2string(newObj->vThis->name,"_sys");
        newObj->vThisSys = ShellVar_assocCreateGlobal(tstr);
        xfree(tstr);
        BashObj_makeRef(newObj);

    // its a plain string variable
    } else {
        newObj->vThis = varNewHeapVar((oidAttributes)?oidAttributes:"A");
        strcpy(newObj->name, newObj->vThis->name);
        BashObj_makeRef(newObj);
        if (vObjVar)
            ShellVar_set(vObjVar, newObj->ref);
        else
            ShellVar_createSet(_objRefVar, newObj->ref);

        tstr = save2string(newObj->vThis->name,"_sys");
        newObj->vThisSys = ShellVar_assocCreateGlobal(tstr);
        xfree(tstr);
    }

    strcpy(newObj->name, newObj->vThis->name);

    ShellVar_assocSet(newObj->vThisSys, "_OID"  , newObj->vThis->name);
    ShellVar_assocSet(newObj->vThisSys, "_CLASS", newObj->vCLASS->name);
    ShellVar_assocSet(newObj->vThisSys, "_Ref"  , newObj->ref);
    ShellVar_assocSet(newObj->vThisSys, "0"     , newObj->ref);

    BashObj_makeVMT(newObj);

    char* defIdxSetting = ShellVar_assocGet(newObj->vCLASS, "defaultIndex");
    if (defIdxSetting && strcmp(defIdxSetting,"on")==0)
        ShellVar_assocSet(newObj->vThis, "0", newObj->ref);

    _classUpdateVMT(_CLASS, 0);

    BashObj_setupMethodCallContext(newObj);
    ShellVar_refCreateSet("newtarget",_CLASS);
    ShellVar_refCreateSet("prototype", "primeThePump"); // so that unset has a local to unset

    // call each class ctor in the hierarchy in order from Object to _CLASS
    char* classHierarchy = ShellVar_assocGet(newObj->vCLASS, "classHierarchy");
    WORD_LIST* hierarchyList = list_string(classHierarchy, " \t\n",0);
    for (WORD_LIST* _cname=hierarchyList; _cname; _cname=_cname->next ) {
        ShellVar_refUnsetS("static");
        ShellVar_refCreateSet("static", _cname->word->word);
        SHELL_VAR* vProto = ShellVar_findWithSuffix(_cname->word->word, "_prototype");
        if (vProto) {
            ShellVar_refUnsetS("prototype");
            ShellVar_refCreateSet("prototype", vProto->name);
            ShellVar_assocCopyElements(assoc_cell(newObj->vThis), assoc_cell(vProto));
        }
        SHELL_VAR* vCtor = ShellVar_findWithSuffix(_cname->word->word, "::__construct");
        if (vCtor)
            execute_shell_function(vCtor, args);
    }

    ShellVar_refUnsetS("prototype");

    // call each class post ctor in the hierarchy in order from Object to _CLASS
    for (WORD_LIST* _cname=hierarchyList; _cname; _cname=_cname->next ) {
        SHELL_VAR* vPostCtor = ShellVar_findWithSuffix(_cname->word->word, "::__construct");
        if (vPostCtor)
            execute_shell_function(vPostCtor, args);
    }

    dispose_words(hierarchyList);

    bgtracePop();
    bgtrace0(1,"END 6 ConstructObject \n");
    return newObj;
}

void BashObj_dump(BashObj* pObj) {
    printf("BashObj: '%s'\n", (pObj)?pObj->name:"<nonexistent>");
    if (!pObj)
        return;
    printf("   vThis:    %s\n", (pObj->vThis)?pObj->vThis->name:"<nonexistent>");
    printf("   vThisSys: %s\n", (pObj->vThisSys)?pObj->vThisSys->name:"<nonexistent>");
    printf("   vCLASS:   %s\n", (pObj->vCLASS)?pObj->vCLASS->name:"<nonexistent>");
    printf("   vVMT:     %s\n", (pObj->vVMT)?pObj->vVMT->name:"<nonexistent>");

    printf("   refClass: %s\n", pObj->refClass);
    printf("   superCallFlag: %d\n", pObj->superCallFlag);
}

char* BashObj_getMemberValue(BashObj* pObj, char* memberName) {
    if (*memberName == '_') {
        return ShellVar_assocGet(pObj->vThisSys, memberName);
    }else if (array_p(pObj->vThis)) {
        if (!isNumber(memberName)) {
            //setErrorMsg("memberName is not a valid numeric array index\n\tmemberName='%s'\n", memberName);
            return NULL;
        }
        return ShellVar_arrayGetS(pObj->vThis, memberName);
    }else {
        return ShellVar_assocGet(pObj->vThis, memberName);
    }
}

int BashObj_setMemberValue(BashObj* pObj, char* memberName, char* value) {
    if (*memberName == '_'){
        assoc_insert (assoc_cell (pObj->vThisSys), savestring(memberName), value);
    }else if (array_p(pObj->vThis)) {
        arrayind_t index = ShellVar_arrayStrToIndex(pObj->vThis, memberName);
        ShellVar_arraySetI(pObj->vThis, index, value);
    }else {
        assoc_insert (assoc_cell (pObj->vThis),    savestring(memberName), value);
    }
    return EXECUTION_SUCCESS;
}

void BashObj_setClass(BashObj* pObj, char* newClassName)
{
    bgtrace2(1,"BashObj_setClass(%s, %s)\n", pObj->name, newClassName);
    // $class.isDerivedFrom "${oldStatic[name]}" || assertError -v this -v originalClass:oldStatic[name] -v newClass:newClassName "Can not set class to one that is not a descendant to the original class"

    // if its already newClassName, there is nothing to do
    if (strcmp(pObj->vCLASS->name, newClassName)==0)
        return;

    SHELL_VAR* vNewClass = assertClassExists(newClassName, NULL);
    if (!vNewClass)
        return;

    pObj->vCLASS = vNewClass;
    BashObj_makeRef(pObj);

    assoc_insert (assoc_cell(pObj->vThisSys), savestring("_CLASS"), vNewClass->name);
    assoc_insert (assoc_cell(pObj->vThisSys), savestring("_Ref"), pObj->ref);
    assoc_insert (assoc_cell(pObj->vThisSys), savestring("0"), pObj->ref);
    char* defIdxSetting = ShellVar_assocGet(pObj->vCLASS, "defaultIndex");
    if (defIdxSetting && strcmp(defIdxSetting,"on")==0)
        ShellVar_assocSet(pObj->vThis, "0", pObj->ref);
    else
        assoc_remove( assoc_cell(pObj->vThis), "0" );

    _classUpdateVMT(newClassName, 0);

	// # invoke the _onClassSet methods that exist for any Class in this hierarchy
	// local -n static="$_CLASS"
	// local -n _VMT="${_this[_VMT]:-${_this[_CLASS]}_vmt}"
	// local _cname; for _cname in ${class[classHierarchy]}; do
	// 	type -t $_cname::_onClassSet &>/dev/null && $_cname::_onClassSet "$@"
	// done
}


char* BashObj_getMethod(BashObj* pObj, char* methodName) {
    if (strncmp(methodName,"_method::",9)==0 || strncmp(methodName,"_static::",9)==0)
        return ShellVar_assocGet(pObj->vVMT, methodName);
    else {
        if (strlen(methodName)>200) {
            setErrorMsg("BashObj_getMethod: methodName is too long (>200). \n\tmethodName='%s'\n", methodName);
            return NULL;
        }
        char buf[255]; strcpy(buf, "_method::"); strcat(buf,methodName);
        return ShellVar_assocGet(pObj->vVMT, buf);
    }
}


int BashObj_gotoMemberObj(BashObj* pObj, char* memberName, int allowOnDemandObjCreation, int* pErr) {
    if (pErr) *pErr=EXECUTION_SUCCESS;
    char* memberVal=BashObj_getMemberValue(pObj, memberName);
    if (!memberVal) {
        if (!allowOnDemandObjCreation) {
            setErrorMsg("MemberName does not exist and allowOnDemandObjCreation is false\n\tmemberName='%s'\n", memberName);
            if (pErr) *pErr=EXECUTION_FAILURE;
            return 0;
        }
        if (array_p(pObj->vThis) && isNumber(memberName)) {
            setErrorMsg("MemberName is not a valid index for a Numeric array\n\tmemberName='%s'\n", memberName);
            if (pErr) *pErr=EXECUTION_FAILURE;
            return 0;
        }
        BashObj* subObj=BashObj_makeNewObject("Object",NULL);
        BashObj_setMemberValue(pObj, memberName, subObj->ref);
        memcpy(pObj,subObj, sizeof(*pObj));
        xfree(subObj);
        return 1;
    }
    BashObjRef oRef;
    BashObj memberObj;
    if (!BashObjRef_init(&oRef, memberVal)) {
        setErrorMsg("A primitive member can not be dereferenced with MemberName\n\tmemberName='%s'\n", memberName);
        if (pErr) *pErr=EXECUTION_FAILURE;
        return 0;
    }
    if (!BashObj_init(&memberObj, oRef.oid, pObj->refClass, pObj->superCallFlag ? "1":"0")) {
        if (pErr) *pErr=EXECUTION_FAILURE;
        return 0;
    }
    memcpy(pObj,&memberObj, sizeof(*pObj));
    return 1;
}

// Make the local vars for the method execution environment
int BashObj_setupMethodCallContext(BashObj* pObj)
{
    ShellVar_refCreateSet( "this"           , pObj->vThis->name             );
    ShellVar_refCreateSet( "_this"          , pObj->vThisSys->name          );
    ShellVar_refCreateSet( "static"         , pObj->vCLASS->name            );
    ShellVar_refCreateSet( "class"          , pObj->vCLASS->name            );
    ShellVar_refCreateSet( "_VMT"           , pObj->vVMT->name              );

    ShellVar_createSet(    "_OID"           , pObj->vThis->name             );
    ShellVar_createSet(    "_OID_sys"       , pObj->vThisSys->name          );
    ShellVar_createSet(    "_CLASS"         , pObj->refClass                );
    ShellVar_createSet(    "_hierarchLevel" , (pObj->superCallFlag)?"1":"0" );
    return EXECUTION_SUCCESS;
}



/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// BashClass

typedef struct {
    SHELL_VAR* vClass;
    SHELL_VAR* vVMT;
} BashClass;

int BashClass_init(BashClass* pCls, char* className)
{
    pCls->vClass = ShellVar_find(className);
    if (!pCls->vClass)
        return setErrorMsg("bultin _classUpdateVMT: global class array does not exist for class '%s'\n", className);
    if (!assoc_p(pCls->vClass))
        return setErrorMsg("bultin _classUpdateVMT: global class array not an -A array for class '%s'\n", className);
    if (invisible_p(pCls->vClass)) {
        VUNSETATTR (pCls->vClass, att_invisible);
    }

    pCls->vVMT = ShellVar_findWithSuffix(className, "_vmt");
    if (!pCls->vVMT)
        return setErrorMsg("bultin _classUpdateVMT: global class vmt array does not exist for class '%s_vmt'\n", className);
    if (!assoc_p(pCls->vVMT))
        return setErrorMsg("bultin _classUpdateVMT: global class vmt array is not an -A array for class '%s'\n", className);
    if (invisible_p(pCls->vVMT)) {
        VUNSETATTR (pCls->vVMT, att_invisible);
    }
    return EXECUTION_SUCCESS;
}

BashClass* BashClass_find(char* name) {
    BashClass* pClass = xmalloc(sizeof(BashClass));
    if (!BashClass_init(pClass, name)) {
        xfree(pClass);
        return NULL;
    }
    return pClass;
}


int BashClass_isVMTDirty(BashClass* pCls, char* currentCacheNumStr) {
    char* baseClassNameVmtCacheNum = ShellVar_assocGet(pCls->vClass, "vmtCacheNum");
    return (!baseClassNameVmtCacheNum || strcmp(baseClassNameVmtCacheNum, currentCacheNumStr)!=0);
}

void DeclareClassEnd(char* className)
{
    char* tstr = xmalloc(strlen(className)+10);
    strcpy(tstr, className);
    strcat(tstr, "_initData");
    SHELL_VAR* vDelayedData = ShellVar_find(tstr);
    xfree(tstr);
    if (!vDelayedData)
        return;

    // note that its most efficient to build WORD_LIST backward. ShellVar_arrayToWordList(),p3,p2,p1

    // copy the init data and remove vDelayedData so that a recursive call wont re-enter this section
    WORD_LIST* constructionArgs = ShellVar_arrayToWordList(vDelayedData);
    ShellVar_arrayUnset(vDelayedData);

    // we need the baseClass as well as passing to the ctor
    char* baseClass = (constructionArgs)?constructionArgs->word->word:NULL;

    constructionArgs = make_word_list(make_word(className), constructionArgs); // <className>  1st arg to the Class __constructor
    constructionArgs = make_word_list(make_word(className), constructionArgs); // <objVar>     2nd arg to ConstructObject
    constructionArgs = make_word_list(make_word("Class")  , constructionArgs); // <className>  1st arg to ConstructObject

    //Class[pendingClassCtors]="${Class[pendingClassCtors]// $className }"
    SHELL_VAR* vClassClass = ShellVar_find("Class");
    char* pendingClassCtors = ShellVar_assocGet(vClassClass, "pendingClassCtors");
    char* s = strstr(pendingClassCtors, className);
    if (s) {
        char* e = s;
        while (*e && !whitespace(*e)) e++;
        while (*e && whitespace(*e)) e++;
        memmove(s, e, strlen(e)+1);
    }

    // ensure that our base class is realized because we will need it
    if (baseClass)
        DeclareClassEnd(baseClass);

    // ConstructObject Class "$className" "$className" "$baseClass" "${initData[@]}"
    ConstructObject(constructionArgs);

    tstr = save2string("static::",className);
    SHELL_VAR* vClassDynCtor = ShellVar_findWithSuffix(tstr, "::__construct");
    xfree(tstr);
    if (vClassDynCtor) {
        execute_shell_function(vClassDynCtor, constructionArgs->next->next);
    }
    dispose_words(constructionArgs);
}



/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ObjMemberItr

typedef enum { ovt_normal, ovt_sys, ovt_both } ObjVarType;
typedef struct {
    BashObj* pObj;
    ObjVarType type;
    HASH_TABLE* table;
    BUCKET_CONTENTS* item;
    int position;
} ObjMemberItr;

BUCKET_CONTENTS* ObjMemberItr_next(ObjMemberItr* pI) {
    // first iterate the current bucket linked list if we are not at the end
    if (pI->item && pI->item->next) {
        pI->item = pI->item->next;

    // next, goto the start linked list of the next non-empty bucket
    } else if (HASH_ENTRIES (pI->table) != 0 && pI->table && pI->position < pI->table->nbuckets) {
        while ((pI->position < pI->table->nbuckets) && 0==(pI->item=hash_items(pI->position, pI->table))) pI->position++;
        pI->position++;
    } else {
        pI->item = NULL;
    }

    // if we are at the end of vThis and vThisSys is different and we are not just iterating normal members, go to the start of the
    // vThisSys table
    if (!pI->item && pI->table != assoc_cell(pI->pObj->vThisSys) && pI->type!=ovt_normal) {
        pI->table = assoc_cell(pI->pObj->vThisSys);
        pI->position=0;
        while ((pI->position < pI->table->nbuckets) && 0==(pI->item=hash_items(pI->position, pI->table))) pI->position++;
        pI->position++;
    }
    return pI->item;
}
BUCKET_CONTENTS* ObjMemberItr_init(ObjMemberItr* pI, BashObj* pObj, ObjVarType type) {
    pI->pObj=pObj;
    pI->type=type;
    pI->table = (type!=ovt_sys) ? assoc_cell(pI->pObj->vThis) : assoc_cell(pI->pObj->vThisSys);
    pI->position=0;
    pI->item=NULL;
    pI->item = ObjMemberItr_next(pI);
    return pI->item;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// CMD: _classUpdateVMT

int _classUpdateVMT(char* className, int forceFlag)
{
    if (!className || !*className)
        return EXECUTION_FAILURE;

    int classNameLen=strlen(className);
    BashClass class; BashClass_init(&class,className);

    // # force resets the vmtCacheNum of this entire class hierarchy so that the algorithm will rescan them all during this run
    if (forceFlag) {
        BGString classHierarchy; BGString_initFromStr(&classHierarchy, ShellVar_assocGet(class.vClass, "classHierarchy") );
        BGString_replaceWhitespaceWithNulls(&classHierarchy);
        char* oneClass;
        while (oneClass=BGString_nextWord(&classHierarchy)) {
            SHELL_VAR* vOneClass = ShellVar_find(oneClass);
            if (vOneClass) {
                ShellVar_assocSet(vOneClass, "vmtCacheNum", "-1");
            }
        }
        BGString_free(&classHierarchy);
    }

    // local currentCacheNum; importCntr getRevNumber currentCacheNum
    int currentCacheNum=0;
    SHELL_VAR* vImportedLibraries=ShellVar_find("_importedLibraries");
    if (vImportedLibraries)
        currentCacheNum+=(vImportedLibraries) ? ShellVar_assocSize(vImportedLibraries) : 0;
    SHELL_VAR* vImportedLibrariesBumpAdj=ShellVar_find("_importedLibrariesBumpAdj");
    if (vImportedLibrariesBumpAdj)
        currentCacheNum+=evalexp(ShellVar_get(vImportedLibrariesBumpAdj),0,0);
    char* currentCacheNumStr = itos(currentCacheNum);


    // if [ "${class[vmtCacheNum]}" != "$currentCacheNum" ]; then
    // char* classVmtCacheNum = ShellVar_assocGet(class.vClass, "vmtCacheNum");
    // if (!classVmtCacheNum || strcmp(classVmtCacheNum, currentCacheNumStr)!=0) {
    if (BashClass_isVMTDirty(&class, currentCacheNumStr)) {
        bgtrace1(1,"doing _classUpdateVMT(%s)\n", className);

        // class[vmtCacheNum]="$currentCacheNum"
        ShellVar_assocSet(class.vClass, "vmtCacheNum", currentCacheNumStr);

        // vmt=()
         assoc_flush(assoc_cell(class.vVMT));

        // # init the vmt with the contents of the base class vmt (Object does not have a baseClassName)
        // each baseclass will have _classUpdateVMT(baseClassName) called to recursively make everything uptodate.
        char* baseClassName = ShellVar_assocGet(class.vClass, "baseClass");
        if (baseClassName && *baseClassName) {
            BashClass baseClass; BashClass_init(&baseClass,baseClassName);

            // # update the base class vmt if needed
            // if  [ "${baseClassName[vmtCacheNum]}" != "$currentCacheNum" ]; then
            char* baseClassNameVmtCacheNum = ShellVar_assocGet(baseClass.vClass, "vmtCacheNum");
            if (!baseClassNameVmtCacheNum || strcmp(baseClassNameVmtCacheNum, currentCacheNumStr)!=0) {
                // recursive call...
                _classUpdateVMT(baseClassName,0);
            }
            // # copy the whole base class VMT into our VMT (static and non-static)
            ShellVar_assocCopyElements(assoc_cell(class.vVMT), assoc_cell(baseClass.vVMT));
        }

        // # add the methods of this class
        // functions that start with <className>:: or static::<className>::
        SHELL_VAR **funcList = all_visible_functions();
        char* prefix1=xmalloc(classNameLen+3); strcpy(prefix1, className); strcat(prefix1, "::"); int prefix1Len=strlen(prefix1);
        char* prefix2=xmalloc(classNameLen+11); strcpy(prefix2, "static::"); strcat(prefix2, className); strcat(prefix2, "::"); int prefix2Len=strlen(prefix2);

        BGString methodsStrList;       BGString_init(&methodsStrList,       1024);
        BGString staticMethodsStrList; BGString_init(&staticMethodsStrList, 1024);
        int count=0;
        int total=0;
        for (int i=0; funcList[i]; i++) {
            total++;
            if (strncmp(funcList[i]->name, prefix1,prefix1Len)==0) {
                count++;
                char* methodBase=xmalloc(strlen(funcList[i]->name)+7+1);
                strcpy(methodBase, "_method");
                strcat(methodBase, funcList[i]->name+prefix1Len-2);
                ShellVar_assocSet(class.vVMT, methodBase, funcList[i]->name);
                BGString_append(&methodsStrList, funcList[i]->name, "\n");
            }
            if (strncmp(funcList[i]->name, prefix2,prefix2Len)==0) {
                count++;
                char* staticBase=xmalloc(strlen(funcList[i]->name)+7+1);
                strcpy(staticBase, "_static");
                strcat(staticBase, funcList[i]->name+prefix2Len-2);
                ShellVar_assocSet(class.vVMT, staticBase, funcList[i]->name);
                BGString_append(&staticMethodsStrList, funcList[i]->name, "\n");
            }
        }
        ShellVar_assocSet(class.vClass, "methods"      , methodsStrList.buf      );
        ShellVar_assocSet(class.vClass, "staticMethods", staticMethodsStrList.buf);
        BGString_free(&methodsStrList);
        BGString_free(&staticMethodsStrList);
        xfree(prefix1);
        xfree(prefix2);
    }

    xfree(currentCacheNumStr);
    return EXECUTION_SUCCESS;
}


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// CMD: _bgclassCall

int _bgclassCall(WORD_LIST* list)
{
    bgtrace1(1,"starting _bgclassCall\n", WordList_toString(list));
//    begin_unwind_frame ("bgObjects");

    if (!list || list_length(list) < 3) {
        printf ("Error - not enough arguments (%d). See usage..\n\n", (list)?list_length(list):0);
        return (EX_USAGE);
    }

    // first arg is the OID which is the name of the bash array that represents the object instance
    char* oid=list->word->word;
    list = list->next;

    // second arg is the className of the <objRef> being used to reference the OID. This may be different from the actual class
    // its only used when hierarchyLevel is not 0 indicating a super reference call
    char* refClass=list->word->word;
    list = list->next;

    // third arg is the hierarchyLevel of the <objRef> being used to reference the OID
    char* refHierarchy=list->word->word;
    list = list->next;

    BashObj objInstance;
    if (!BashObj_init(&objInstance, oid, refClass, refHierarchy))
        return EXECUTION_FAILURE;

    // at this point the <objRef> components (except the |) have been removed so its a good time to record the _memberExpression
    // that is used as context in errors. The '|' from the <objRef> may be stuck to the first position so remove that if it is
    char* _memberExpression = string_list(list);
//    add_unwind_protect(xfree,_memberExpression);
    char* pStart=_memberExpression;
    if (*pStart == '|') pStart++;
    while (pStart && *pStart && whitespace(*pStart)) pStart++;
    if (pStart> _memberExpression)
        memmove(_memberExpression, pStart, strlen(pStart)+1);
    ShellVar_createSet("_memberExpression", _memberExpression);


    // forth arg is the pipe character used to delimit the start of the object syntax. It typically should be alone in the 4th
    // position but sometimes the objRef looses its trailing space and it gets stuck to the first term of the expression.
    // objSyntaxStart will be either the 4th or 5th position and if it starts with "| " it will be incremente past the pipe and whitespace
    char* objSyntaxStart=NULL;
    if (strcmp(list->word->word,"|")!=0) { // list->word->word is not a '|' by itself
        if (strncmp(list->word->word,"|",1)==0) {
            objSyntaxStart=list->word->word +1; // +1 to start after the '|'
            while (*objSyntaxStart && whitespace(*objSyntaxStart)) objSyntaxStart++;
        } else {
            return setErrorMsg("Syntax error. the 4th argument must start with the pipe ('|') character. See usage..\n\n");
        }
    }
    list = list->next;

    // if the objSyntaxStart was not in the 4th position, it is the 5th but its alright if it does not exist
    if (!objSyntaxStart && list && list->word) {
        objSyntaxStart=list->word->word;
        list = list->next;
    }

    bgtrace1(2,"\n01234567890123456789\n%s\n",objSyntaxStart);


    // These variables are static so that we only compile the pattern once
    //                   ((                            opWBr                                         )(  Br         )|( opNBr  ))(firstArg)?$
    static char* pattern="((\\.unset|\\.exists|\\.isA|\\.getType|\\.getOID|\\.getRef|\\.toString|=new|)([[:space:]]|$)|([+]?=|::))(.*)?$";
    static regex_t regex;
    static int regexInit;
    if (!regexInit) {
        if (regcomp(&regex, pattern, REG_EXTENDED)) // |REG_ICASE
            return setErrorMsg("error: invalid regex pattern (%s)\n", pattern);
    }
    // // matches[0] is the whole match (op to EOL) ((opWBr)(Br)|(opNBr))(firstArg)?$
    // // matches[1] is ((opWBr)(Br)|(opNBr))
    // // matches[2] is (opWBr)
    // // matches[3] is (Br)
    // // matches[4] is (opNBr)
    // // matches[5] is (firstArg)
    // for (int i=0; i<=regex.re_nsub; i++) {
    //     printf("  matches[%d]:  %d to %d\n", i, matches[i].rm_so, matches[i].rm_eo);
    // }


    // [[ "$objSyntaxStart" =~ $reExp ]] || setErrorMsg "invalid object expression"
    regmatch_t *matches = (regmatch_t *)malloc (sizeof (regmatch_t) * (regex.re_nsub + 1));
    if (regexec(&regex, objSyntaxStart, regex.re_nsub + 1, matches, 0))
        return setErrorMsg("error: invalid object expression (%s) did not match regex='%s'\n", objSyntaxStart, pattern);


    // the _memberOp can be in the 2(opWBr) or 4(opNBr) position
    char* _memberOp;
    if (matches[2].rm_so<matches[2].rm_eo)
        _memberOp=savestringn(objSyntaxStart+matches[2].rm_so,(matches[2].rm_eo-matches[2].rm_so));
    else if (matches[4].rm_so<matches[4].rm_eo)
        _memberOp=savestringn(objSyntaxStart+matches[4].rm_so,(matches[4].rm_eo-matches[4].rm_so));
    else
        _memberOp=savestring("");
//    add_unwind_protect(xfree,_memberOp);


    // _chainedObjOrMember is the beginning of objSyntaxStart up to the start of the match (start of op)
#   if bgtraceLevel >= 2
    int objSyntaxStartLen=strlen(objSyntaxStart); // to be able to trace the whole thing including \0 we place
#   endif
    char* _chainedObjOrMember=objSyntaxStart;
    // TODO: MEMLEAK: is the following savestringn a leak?
    ShellVar_createSet("_chainedObjOrMember", savestringn(_chainedObjOrMember, matches[0].rm_so));
    char* _chainedObjOrMemberEnd=objSyntaxStart + matches[0].rm_so;
    char saveCh=*_chainedObjOrMemberEnd;
    *_chainedObjOrMemberEnd='\0';

    // move past the initial '.' or '[' delimeter
    if (_chainedObjOrMember[0]=='.' || _chainedObjOrMember[0]=='[')
        _chainedObjOrMember+=1;

    // replace the '.' and '[' separators with \0 so we can iterate the parts
    for (int i=0; i<matches[0].rm_so; i++)
        if (_chainedObjOrMember[i]=='.' || _chainedObjOrMember[i]=='[')
            _chainedObjOrMember[i]='\0';

#   if bgtraceLevel >= 2
        for (int i=0; i<objSyntaxStartLen; i++)
            bgtrace1(2,"%c",(objSyntaxStart[i]=='\0')?'_':objSyntaxStart[i]);
        bgtrace0(2,"\n");
#   endif

    // put the expression args into args[] and also 'set -- "${args[@]}"'
    // some operators dont require a space between the operator and the first arg so if match[5] is not empty, create
    // a new list head with it
    SHELL_VAR* vArgs = make_local_array_variable("_argsV",0);
    int argsNonEmpty=0;
    if (matches[5].rm_so<matches[5].rm_eo) {
        argsNonEmpty=1;
        WORD_LIST argsList;
        argsList.next=list;
        argsList.word = xmalloc(sizeof(*argsList.word));
        argsList.word->word = savestringn(objSyntaxStart+matches[5].rm_so,(matches[5].rm_eo-matches[5].rm_so));
        argsList.word->flags=0;

        remember_args(&argsList,1);
        assign_compound_array_list (vArgs, &argsList, 0);
        xfree(argsList.word->word);
        xfree(argsList.word);
    } else {
        if (list)
            argsNonEmpty=1;
        remember_args(list,1);
        assign_compound_array_list (vArgs, list, 0);
    }

    // local allowOnDemandObjCreation; [[ "${_memberOp:-defaultOp}" =~ ^(defaultOp|=new|\+=|=|::)$ ]] && allowOnDemandObjCreation="-f"
    int allowOnDemandObjCreation = (
               strcmp(_memberOp, "=new")==0
            || strcmp(_memberOp, "+="  )==0
            || strcmp(_memberOp, "="   )==0
            || strcmp(_memberOp, "::"  )
        );

    // This block process the part of the expression that is before the recognized operator to figure out
    // which object instance (objInstance) and which member of that instance (_rsvMemberName) the operator
    // (_memberOp) is acting on.
    //
    // objInstance starts off as the <oid> from the <objRef> passed in and then we iterate all but the last
    // part of _chainedObjOrMember. Each iteration replaces the contents of objInstance to point to the member
    // object that pCurPart identifies (i.e. objInstance=objInstance[pCurPart]). If objInstance[pCurPart] does
    // not contain an <objRef> it stops with an error or creates an Object at objInstance[pCurPart].
    //
    // The last pCurPart becomes _rsvMemberName and it does not need to be an <objRef> or even exist in objInstance
    // at all because some operators like =new will bring it into existance.
    //
    // if _chainedObjOrMember has no parts objInstance will be the <oid> from the <objRef> passed in, _rsvMemberName
    // will be NULL and _rsvMemberType will be mt_self indicating that the operator must operate on the <oid> itself
    char* _rsvMemberName=NULL;
    char* _rsvMemberValue=NULL;
    MemberType _rsvMemberTypeFromSyntax=mt_unknown;
    MemberType _rsvMemberType=mt_unknown;
    char* pCurPart=_chainedObjOrMember;
    while (pCurPart<(_chainedObjOrMemberEnd-1)) {
        // record the new pNextPart now becuase we might remove a trailing ']' from pCurPart which would mess it up
        char* pNextPart=pCurPart+strlen(pCurPart)+1;  // '.' and '[' have been replaced with '\0'
        pNextPart=(pNextPart>_chainedObjOrMemberEnd)?_chainedObjOrMemberEnd:pNextPart;
        int isLastPart=(pNextPart<(_chainedObjOrMemberEnd-1))?0:1;

        // examine the syntax
        if (pCurPart[strlen(pCurPart)-1] == ']') {
            _rsvMemberTypeFromSyntax=mt_primitive;
            pCurPart[strlen(pCurPart)-1]='\0';
        } else if (*pCurPart == ':') {
            _rsvMemberTypeFromSyntax=mt_method;
            while (pCurPart && *pCurPart && *pCurPart==':') pCurPart++;
        } else {
            _rsvMemberTypeFromSyntax=mt_unknown;
        }

        int localErrno=EXECUTION_SUCCESS;
        if (strcmp(pCurPart,"static")==0) {
            BashObj staticObj; BashObj_init(&staticObj, objInstance.vCLASS->name, NULL,NULL);
            memcpy(&objInstance, &staticObj, sizeof(objInstance));
            pCurPart=pNextPart;
        } else {
            // descend into the next member object if its not the last part
            if (!isLastPart && !BashObj_gotoMemberObj(&objInstance, pCurPart, allowOnDemandObjCreation, &localErrno)) {
                if (localErrno==EXECUTION_FAILURE)
                    return EXECUTION_FAILURE;
                return setErrorMsg("error: '%s' is not a member object of '%s' but it is being dereferenced as if it is. dereference='%s'\n", pCurPart, objInstance.name, pNextPart);
            }
        }

        if (isLastPart) break;
        pCurPart=pNextPart;
    }

    // the loop iterated all but the last part and objInstance is now the object for which the last part is a member of
    _rsvMemberName=savestring(pCurPart);
//    add_unwind_protect(xfree,_rsvMemberName);
    *_chainedObjOrMemberEnd=saveCh;

    // if its empty
    if (!_rsvMemberName || strlen(_rsvMemberName)==0) {
        _rsvMemberType=mt_self;

    // if syntax says its a variable
    } else if (_rsvMemberTypeFromSyntax==mt_primitive) {
        _rsvMemberValue=BashObj_getMemberValue(&objInstance, _rsvMemberName);
        _rsvMemberType=(!_rsvMemberValue) \
                        ?mt_nullVar \
                        :(strncmp(_rsvMemberValue,"_bgclassCall",12)==0) \
                            ?mt_object \
                            :mt_primitive;

    // if syntax says its a method
    } else if (_rsvMemberTypeFromSyntax==mt_method) {
        _rsvMemberValue=BashObj_getMethod(&objInstance, _rsvMemberName);
        _rsvMemberType=(_rsvMemberValue)?mt_method:mt_nullMethod;

    // syntax says it could be either
    } else {
        char* tMeth=BashObj_getMethod(&objInstance, _rsvMemberName);
        _rsvMemberValue=BashObj_getMemberValue(&objInstance, _rsvMemberName);
        // only var exists so its a var
        if (!tMeth && _rsvMemberValue) {
            _rsvMemberType=(strncmp(_rsvMemberValue,"_bgclassCall",12)==0) \
                            ?mt_object \
                            :mt_primitive;
        // only method exists so its a method
        } else if (tMeth && !_rsvMemberValue) {
            _rsvMemberValue=tMeth;
            _rsvMemberType=mt_method;
        // at this point its a tie -- either they both exist or niether exist so if its the default operator and there are some
        // arguments, it looks more like a method
        } else if (strcmp(_memberOp,"")==0 && argsNonEmpty) {
            _rsvMemberType=mt_nullMethod;
        // niether exist so its nullEither
        } else if (!tMeth && !_rsvMemberValue) {
            _rsvMemberType=mt_nullEither;
        // both exist
        } else { // both exist
            _rsvMemberType=mt_both;
        }
    }

    char* _rsvMemberTypeStr = MemberTypeToString(_rsvMemberType, NULL, _rsvMemberValue);
//    add_unwind_protect(xfree,_rsvMemberTypeStr);

    bgtrace1(2,"   _memberOp        ='%s'\n", (_memberOp)?_memberOp:"");
    bgtrace1(2,"   _rsvMemberType   ='%s'\n", (_rsvMemberTypeStr)?_rsvMemberTypeStr:"");
    bgtrace1(2,"   _rsvMemberName   ='%s'\n", (_rsvMemberName)?_rsvMemberName:"");
    bgtrace1(2,"   _rsvMemberValue  ='%s'\n", (_rsvMemberValue)?_rsvMemberValue:"");
    bgtrace1(2,"   _memberExpression='%s'\n", (_memberExpression)?_memberExpression:"");

    //[[ "$_rsvMemberType" =~ ^invalidExpression ]] && assertObjExpressionError ...
    //case ${_memberOp:-defaultOp}:${_rsvMemberType} in

    // setup the method environment for the rest of _bgclassCall to use and in case it invokes a method
    BashObj_setupMethodCallContext(&objInstance);

    /* This block creates local vars that the bash function _bgclassCall will use in deciding what to do  */

    // local _rsvOID="$_rsvOID"
    ShellVar_createSet("_rsvOID", objInstance.name);

    // local _memberOp="$_memberOp"
    ShellVar_createSet("_memberOp", _memberOp);

    // local _rsvMemberName="$_rsvMemberName"
    ShellVar_createSet("_rsvMemberName", _rsvMemberName);

    // local _rsvMemberType="$_rsvMemberType"
    ShellVar_createSet("_rsvMemberType", _rsvMemberTypeStr);

    ShellVar_createSet("_resultCode", "");

    // # fixup the :: override syntax
    int virtOverride=0;
    if (strcmp(_memberOp,"::")==0 && strncmp(_rsvMemberTypeStr,"null:",5)==0) {
        virtOverride=1;
        // _rsvMemberName="${_rsvMemberName}::$1"
        char* t=_rsvMemberName;
        char* d1 = get_dollar_var_value(1);
        _rsvMemberName = xmalloc(strlen(t)+strlen(d1)+3);
        strcpy(_rsvMemberName,t);
        strcat(_rsvMemberName, "::");
        strcat(_rsvMemberName, d1);
        xfree(t);

        // shift  -- we know in this case that $1 came from the expression and had to be insterted into vArgs and dollar so list
        // is $@ without it
        remember_args(list,1);
        assign_compound_array_list (vArgs, list, 0);

        _rsvMemberType=mt_method;
        xfree(_memberOp); _memberOp=savestring("");
    }


    if (_rsvMemberType==mt_method && strcmp(_memberOp,"")==0) {

        _classUpdateVMT(objInstance.vCLASS->name ,0);

        // local _METHOD
        // if [[ "${_rsvMemberName}" =~ .:: ]]; then
        //     _METHOD="${_rsvMemberName}"
        // else
        //     _METHOD="${_VMT[_method::${_rsvMemberName}]}"
        // fi
        char* _METHOD=NULL;
        if (virtOverride) {
            // local _METHOD="$_rsvMemberName"
            _METHOD=_rsvMemberName;
        } else {
            _METHOD = BashObj_getMethod(&objInstance, _rsvMemberName);
            if (!_METHOD && objInstance.superCallFlag) {
                // do noop -- for he time being, the bash function detects and handles this case if _METHOD is empty
            }
        }
        ShellVar_createSet("_METHOD", (_METHOD)?_METHOD:"");

        // make local namerefs to the oid's of any object members
        if (assoc_p(objInstance.vThis)) {
            ObjMemberItr i;
            for (BUCKET_CONTENTS* item=ObjMemberItr_init(&i,&objInstance, ovt_both); item; item=ObjMemberItr_next(&i)) {
                if (strcmp(item->key,"0")!=0 && strcmp(item->key,"_Ref")!=0 && item->data ) {
                    if (strncmp(item->data, "_bgclassCall",12)==0) {
                        bgtrace1(3,"!!! item->key='%s'\n", item->key);
                        char* memOid = extractOID(item->data);
                        ShellVar_refCreateSet(item->key, memOid);
                        xfree(memOid);
                    } else if (strncmp(item->data, "heap_",5)==0) {
                        ShellVar_refCreateSet(item->key, item->data);
                    }
                }
            }
        }

        // make the $super.<method> <objRef>
        // '_bgclassCall <objInstance> <classFrom_METHOD> 1 |'
        // _METHOD=<className>::<method>
        BGString superObjRef;  BGString_init(&superObjRef, 500);
        BGString_append(&superObjRef, "_bgclassCall ",NULL);
        BGString_append(&superObjRef, objInstance.vThis->name,NULL);
        char* mClass=_METHOD;
        char* mClassEnd=mClass; while (*mClassEnd && (*mClassEnd!=':' || *(mClassEnd+1)!=':') ) mClassEnd++;
        BGString_appendn(&superObjRef, mClass, (mClassEnd-mClass)," ");
        BGString_append(&superObjRef, " 1 | ",NULL);

        ShellVar_createSet("super", superObjRef.buf);
        BGString_free(&superObjRef);
    }


    // cleanup before we leave
    xfree(_rsvMemberTypeStr);
    xfree(_rsvMemberName);_rsvMemberName=NULL;
    xfree(_memberExpression);_memberExpression=NULL;
    xfree(_memberOp); _memberOp=NULL;
//    discard_unwind_frame ("bgObjects");

    return (EXECUTION_SUCCESS);
}




/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// JSON String escaping


// usage: jsonEscape <varname1> [...<varnameN>}
char* jsonEscape(char* s)
{
    char* sOut=xmalloc(strlen(s)*2+2);
    char* cOut=sOut;
    for (char* c=s; c && *c;) {
        switch (*c) {
            case '\\':
            case '"':
            case '/':
            case '\b':
            case '\f':
            case '\n':
            case '\r':
            case '\t':
                *cOut++='\\';
                break;
        }
        *cOut++=*c;
    }
    return sOut;
}


// usage: jsonUnescape <varname1> [...<varnameN>}
void jsonUnescape(char* s)
{
    if (!s || !*s)
        return;
    int slen=strlen(s);
    for (char* c=s; c && *c; c++,slen--) {
        if (*c=='\\') {
            memmove(c,c+1,slen);
            slen--;
            switch (*c) {
                case 'b': *c = '\b'; break;
                case 'f': *c = '\f'; break;
                case 'n': *c = '\n'; break;
                case 'r': *c = '\r'; break;
                case 't': *c = '\t'; break;
                case '\\':
                case '"':
                case '/':
                default:
                    break;
            }
        }
    }
}


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// SimpleWordList

typedef struct _SimpleWordList {
  struct _SimpleWordList* next;
  char* word;
} SimpleWordList;

SimpleWordList* SWList_unshift(SimpleWordList* list, char* word) {
    SimpleWordList* newHead=xmalloc(sizeof(SimpleWordList));
    newHead->next = list;
    newHead->word=savestring(word);
    return newHead;
}
// caller needs to free the returned char* eventually
char* SWList_shift(SimpleWordList** pList) {
    if (!pList || !*pList)
        return NULL;
    char* s=(*pList)->word;
    SimpleWordList* toFree=*pList;
    *pList=((*pList)->next);
    xfree(toFree);
    return s;
}
void SWList_free(SimpleWordList** pList) {
    while (pList && *pList) {
        SimpleWordList* toFree=*pList;
        *pList=((*pList)->next);
        xfree(toFree->word);
        xfree(toFree);
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// BGObjectStack

typedef struct _BGObjectStack {
  struct _BGObjectStack* next;
  BashObj* pObj;
} BGObjectStack;

BGObjectStack* BGObjectStack_unshift(BGObjectStack* list, BashObj* pObj) {
    BGObjectStack* newHead=xmalloc(sizeof(BGObjectStack));
    newHead->next = list;
    newHead->pObj=pObj;
    return newHead;
}
BashObj* BGObjectStack_shift(BGObjectStack** pList) {
    if (!pList || !*pList)
        return NULL;
    BashObj* pObj=(*pList)->pObj;
    BGObjectStack* toFree=*pList;
    *pList=((*pList)->next);
    xfree(toFree);
    return pObj;
}
void BGObjectStack_free(BGObjectStack** pList) {
    while (pList && *pList) {
        BGObjectStack* toFree=*pList;
        *pList=((*pList)->next);
        xfree(toFree);
    }
}


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// CMD: restoreObject

// restoreObject <objVar>
// reads attribute stream from stdin to restore the state of <objVar>
int restoreObject(WORD_LIST* list) {
//    begin_unwind_frame ("bgObjects");

    if (!list || !list->word) {
        setErrorMsg("Error - <objVar> is a required argument to restoreObject. See usage..\n\n");
        builtin_usage();
        return (EX_USAGE);
    }

    BashObj* scope = BashObj_find(list->word->word, NULL,NULL);
    if (!scope) {
        return setErrorMsg("Error - <objVar> (%s) does not exist\n\n", list->word->word);
    }

    //SHELL_VAR* vCurrentStack=ShellVar_find("currentStack");
    BGObjectStack* currentStack=NULL;

    currentStack = BGObjectStack_unshift(currentStack, scope);

    char* ifs_chars = getifs();
    char *line=NULL, *startToken=NULL, *sepStart=NULL;
    size_t lineAllocSize=0;
    while (zgetline(0, &line, &lineAllocSize, '\n', 0)>0) {
        bgtrace0(2,"        01234567890123456789012345678901234567890\n");
        bgtrace1(2,"line = '%s'\n", line);
        startToken=line;
        // TODO: protect the lines below from running out of input
        char *valType=NULL, *relName=NULL, *jpath=NULL, *value=NULL;
        if (*startToken) valType = get_word_from_string(&startToken, ifs_chars, &sepStart);
        if (*startToken) relName = get_word_from_string(&startToken, ifs_chars, &sepStart);
        if (*startToken) jpath   = get_word_from_string(&startToken, ifs_chars, &sepStart);
        if (*startToken) value   = get_word_from_string(&startToken, ifs_chars, &sepStart);

        // Note that relName=="<arrayEl>" when current is an Array (aka json 'list'). BashObj_setMemberValue handles that because
        // when array_p(vThis) but !isNumber(relName), it will append to the end of the array

        //[ "$valType" == "!ERROR" ] && assertError -v objRefVar -v file "jsonAwk returned an error reading restoration file"
        if (strcmp(valType,"!ERROR")==0) {
            return setErrorMsg("error: Object::fromJSON: awk script ended with non-zero exit code\n");
        }

        // value="${value//%20/ }"
        for (char* c=value; c && *c;) {
            if (strncmp(c,"%20",3)==0) {
                *c++=' ';
                memmove(c,c+2, strlen(c+2)+1);
            } else
                c++;
        }

        jsonUnescape(value);

        char* className=NULL;
        if (strcmp(valType,"startObject")==0) {
            className="Object";
        } else if (strcmp(valType,"startList")==0) {
            className="Array";
        }
        if (className) {
            // ConstructObject "$className" currentStack[0]
            BashObj* subObj = BashObj_makeNewObject(className, NULL);
            BashObj_setMemberValue(currentStack->pObj, relName, subObj->ref);
            currentStack = BGObjectStack_unshift(currentStack, subObj);

        } else if (strcmp(valType,"endObject")==0 || strcmp(valType,"endList")==0) {
            BashObj* temp = BGObjectStack_shift(&currentStack);
            xfree(temp);

        } else if (strcmp(valType,"tObject")==0 || strcmp(valType,"tArray")==0) {
            // there is nothing we need to do for these values type because the start* and end* types encompasses them

        } else if (strcmp(relName,"_OID")==0) {
            // // use _OID to update the objDictionary so that we can fixup relative objRefs
            // char* sessionOID = value;
            // objDictionary[sessionOID]=currentStack->pObj->name;
            // objDictionary[currentStack->pObj->name]=sessionOID;

        } else if (strcmp(relName,"_Ref")==0 || strcmp(relName,"0")==0) {
            // ignore _Ref and "0" on restore

        } else if (strcmp(relName,"_CLASS")==0) {
            BashObj_setClass(currentStack->pObj, value);

        } else {
            // *)	if [[ "$value" =~ _bgclassCall.*sessionOID_[0-9]+ ]]; then
            //         :
            //     fi
            BashObj_setMemberValue(currentStack->pObj,relName, value);
        }

        xfree(valType);
        xfree(relName);
        xfree(jpath);
        xfree(value);
    }
    xfree(line);
//    discard_unwind_frame ("bgObjects");
    return EXECUTION_SUCCESS;
}



/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// JSONType

#define JSONType_isBashObj(jt) (jt==jt_object || jt==jt_array)

int JSONType_isAValue(JSONType jt) {
    switch (jt) {
        case jt_object:
        case jt_array:
        case jt_string:
        case jt_number:
        case jt_true:
        case jt_false:
        case jt_null:
            return 1;
        default:
            return 0;
    }
    return 0;
}

char* JSONTypeToString(JSONType jt) {
    switch (jt) {
        case jt_object    : return "jt_object";
        case jt_array     : return "jt_array";
        case jt_objStart  : return "jt_objStart";
        case jt_arrayStart: return "jt_arrayStart";
        case jt_objEnd    : return "jt_objEnd";
        case jt_arrayEnd  : return "jt_arrayEnd";
        case jt_value     : return "jt_value";
        case jt_string    : return "jt_string";
        case jt_number    : return "jt_number";
        case jt_true      : return "jt_true";
        case jt_false     : return "jt_false";
        case jt_null      : return "jt_null";
        case jt_comma     : return "jt_comma";
        case jt_colon     : return "jt_colon";
        case jt_error     : return "jt_error";
        case jt_eof       : return "jt_eof";
    }
    return "unknown JSONType";
}


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// JSONToken

#define JSONToken_isDone(this) ((this->type == jt_eof) || (this->type == jt_error))
#define JSONToken_free(t) do { if (t) { xfree(t->value); xfree(t); }; } while(0)

void JSONToken_print(FILE* fd, JSONToken* this, char* label) {
    fprintf(fd, "%stype='%s' value='%s'\n", label, JSONTypeToString(this->type), (JSONType_isBashObj(this->type))?  (((BashObj*)this->value)->name ) : this->value);
    fflush(fd);
}

char* JSONToken_ToString(JSONToken* this) {
    static char buf[100];
    snprintf(buf,100, "%s(%s)", JSONTypeToString(this->type), JSONType_isBashObj(this->type)?"<objOrArray>":this->value);
    return buf;
}

JSONToken* JSONToken_make(JSONType type, char* value) {
    JSONToken* this = xmalloc(sizeof(JSONToken));
    this->type = type;
    this->value = savestring(value);
    return this;
}
JSONToken* JSONToken_maken(JSONType type, char* value, int len) {
    JSONToken* this = xmalloc(sizeof(JSONToken));
    this->type = type;
    this->value = savestringn(value, len);
    return this;
}
JSONToken* JSONToken_makeObject(BashObj* pObj) {
    JSONToken* this = xmalloc(sizeof(JSONToken));
    this->type = jt_object;
    this->value = (char*)pObj;
    return this;
}
JSONToken* JSONToken_makeArray(BashObj* pObj) {
    JSONToken* this = xmalloc(sizeof(JSONToken));
    this->type = jt_array;
    this->value = (char*)pObj;
    return this;
}
JSONToken* JSONToken_makeError(JSONScanner* scanner, JSONToken* token, char* fmt, ...) {
    if (token && token->type==jt_error)
        return token;

    JSONToken* this = xmalloc(sizeof(JSONToken));
    this->type = jt_error;
    char* temp = xmalloc(500);

    size_t bytesLeft=500;
    this->value = xmalloc(bytesLeft+1);
    *this->value = '\n';

    if (scanner) {
        int linePos=1, charPos=1;
        for (register char* pos=scanner->buf; pos<scanner->pos; pos++, charPos++) {
            if (*pos == '\n') {
                linePos++;
                charPos=1;
            }
        }
        snprintf(this->value, bytesLeft, "error: bgObjects fromJSON: %s(%d:%d) ", scanner->filename, linePos,charPos);
        bytesLeft-=strlen(this->value);
    }

    if (token) {
        if (strlen(token->value) > 30)
            strcpy(token->value+25, " ... ");
        snprintf(temp, 500, "(error token:'%s'(%s)) ", JSONTypeToString(token->type), token->value);
        strncat(this->value, temp, bytesLeft);
        bytesLeft-=strlen(temp);
    }

    va_list args;
    SH_VA_START (args, fmt);
    vsnprintf(temp, 500, fmt, args);
    strncat(this->value, temp, bytesLeft);
    bytesLeft-=strlen(temp);

    int len = strlen(this->value);
    if (len>=500)
        len = 499;
    this->value[len]='\n';
    this->value[len+1]='\0';


    xfree(temp);
    return this;
}
JSONToken* JSONToken_copy(JSONToken* that) {
    JSONToken* this = xmalloc(sizeof(JSONToken));
    this->type = that->type;
    if (this->type==jt_object || this->type==jt_array)
        this->value = (char*)BashObj_copy((BashObj*)that->value);
    else
        this->value = savestring(that->value);
    return this;
}




/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// JSONScanner
// struct defined above
JSONScanner* JSONScanner_newFromFile(char* inFile) {
    struct stat finfo;
    if (stat(inFile, &finfo)!=0)
        return NULL;

    size_t fileLen = (size_t) finfo.st_size;

    // if we dont have a sane fileLen, the file is invalid. This checks for empty and files that are too large
    if (fileLen<=0 || fileLen != finfo.st_size || fileLen+1 < fileLen)
        return NULL;

    FILE* f = fopen(inFile, "r");
    if (!f)
        return NULL;

    JSONScanner* this = xmalloc(sizeof(JSONScanner));
    this->buf = xmalloc(fileLen+1);
    if (!this->buf) {
        xfree(this);
        return NULL;
    }
    this->filename = inFile;
    this->length = fileLen;
    this->pos = this->buf;
    this->end = this->buf+this->length;

    if (fileLen != fread(this->buf, 1, fileLen, f) ) {
        xfree(this->buf);
        xfree(this);
        return NULL;
    }
    *this->end = '\0';
    fclose(f);

    // advance over leading whitespace to the real start of data
    while (spctabnl(*this->pos) && this->pos<this->end) this->pos++;

    return this;
}

#define STREAMCHUNK 4080

JSONScanner* JSONScanner_newFromStream(int fdJSON)
{
    JSONScanner* this = xmalloc(sizeof(JSONScanner));

    // read the entire stream at once. We assume that the caller is piping in a reasonable size JSON data stream that we can read in
    // at once. If this is not true for some reason we would have to make the buffer scanners smart enough to ask for more data
    // whenever they reach the end of the buffer. I suspect that wont be needed, though.
    this->length = 0;
    this->bufAllocSize = STREAMCHUNK;
    this->buf = xmalloc(this->bufAllocSize);
    if (!this->buf) {xfree(this);return NULL;}
    ssize_t bytesRead;
    while (bytesRead = read(fdJSON, this->buf, STREAMCHUNK)>0) {
        this->length += bytesRead;
        if (bytesRead==STREAMCHUNK) {
            this->bufAllocSize += STREAMCHUNK;
            this->buf = xrealloc(this->buf, this->bufAllocSize);
            if (!this->buf) {xfree(this);return NULL;}
        }
    }

    this->filename = "<stream>";
    this->pos = this->buf;
    this->end = this->buf+this->length;
    *this->end = '\0';
    return this;
}

JSONToken* JSONScanner_getToken(JSONScanner* this) {
    while ( this->pos < this->end && (*this->pos==' ' || *this->pos=='\t' || *this->pos=='\n' || *this->pos=='\r') ) this->pos++;
    if (this->pos >= this->end)
        return JSONToken_make(jt_eof, "EOF");
    char* end;
    switch (*this->pos++) {
        case '{': return JSONToken_make(jt_objStart,    "{");
        case '}': return JSONToken_make(jt_objEnd,      "}");
        case '[': return JSONToken_make(jt_arrayStart,  "[");
        case ']': return JSONToken_make(jt_arrayEnd,    "]");
        case ',': return JSONToken_make(jt_comma,       ",");
        case ':': return JSONToken_make(jt_colon,       ":");

        case '"':
            end = this->pos;
            while (end < this->end && *end!='"') {
                end++;
                if (*end=='"' && *(end-1)=='\\')
                    end++;
            }
            if (*end!='"')
                return JSONToken_makeError(this,NULL, "Unterminated string");
            JSONToken* ret = JSONToken_maken(jt_string, this->pos, (end-this->pos));
            this->pos = end+1;
            return ret;

        case '0' ... '9':
        case '-':
            end = this->pos--;
            while (end < this->end && ISDIGIT(*end)) end++;
            if (*end=='.') {
                end++;
                if (!ISDIGIT(*end))
                    return JSONToken_makeError(this,NULL, "Invalid number");
                while (end < this->end && ISDIGIT(*end)) end++;
            }
            if (*end=='E' || *end=='e') {
                end++;
                if (*end=='+' || *end=='-') end++;
                if (!ISDIGIT(*end))
                    return JSONToken_makeError(this,NULL, "Invalid exponent in number");
                while (end < this->end && ISDIGIT(*end)) end++;
            }
            ret = JSONToken_maken(jt_string, this->pos, (end-this->pos));
            this->pos = end+1;
            return ret;

        default:
            this->pos--;
            if (strncmp(this->pos,"null",4)==0)
                return JSONToken_make(jt_null, "<null>");
            else if (strncmp(this->pos,"true",4)==0)
                return JSONToken_make(jt_true, "<true>");
            else if (strncmp(this->pos,"false",5)==0)
                return JSONToken_make(jt_false, "<false>");
            else
                return JSONToken_makeError(this,NULL, "Unknown character '%c'", *this->pos);
    }
    return NULL;
}
//JSONToken_print(_bgtraceFD, token, "");


JSONToken* JSONScanner_getObject(JSONScanner* this, BashObj* pObj)
{
    JSONToken* token;
    while ((token = JSONScanner_getToken(this)) && !JSONToken_isDone(token) && token->type!=jt_objEnd) {
        // The first iteration should not have a comma but we will not call it an error it it does
        // also be tolerant to the caller not removing the jt_objStart token.
        if (token->type == jt_comma || token->type == jt_objStart) {
            JSONToken_free(token);
            token = JSONScanner_getToken(this);
        }
        if (token->type != jt_string) {
            return JSONToken_makeError(this, token, "Expected a string");
        }
        JSONToken* name = token;

        token = JSONScanner_getToken(this);
        if (token->type != jt_colon) {
            return JSONToken_makeError(this, token, "Expected a colon");
        }
        JSONToken_free(token);

        JSONToken* value = JSONScanner_getValue(this);
        if (!JSONType_isAValue(value->type)) {
            return JSONToken_makeError(this,value, "Unexpected token");
        }

        if (strcmp(name->value,"_OID")==0) {
            // // TODO: use _OID to update the objDictionary so that we can fixup relative objRefs
            // char* sessionOID = value;
            // objDictionary[sessionOID]=currentStack->pObj->name;
            // objDictionary[currentStack->pObj->name]=sessionOID;

        } else if (strcmp(name->value,"_Ref")==0 || strcmp(name->value,"0")==0) {
            // ignore _Ref and "0" on restore

        } else if (strcmp(name->value,"_CLASS")==0) {
            // oh right, you are supposed to be this kind of object
            BashObj_setClass(pObj, value->value);

        } else {
            BashObj_setMemberValue(pObj, name->value, (JSONType_isBashObj(value->type)) ? (((BashObj*)value->value)->ref) : value->value );
        }

        JSONToken_free(name);
        JSONToken_free(value);
    }
    return token;
}



JSONToken* JSONScanner_getValue(JSONScanner* this)
{
    static int depth = 0;
    depth++;
    JSONToken* jval;

    JSONToken* token = JSONScanner_getToken(this);
    bgtrace3(1,"%*sgetVal start token='%s'\n", depth*3,"",  JSONToken_ToString(token));
    if (token->type == jt_objStart) {
        JSONToken_free(token);
        BashObj* pObj = BashObj_makeNewObject("Object",NULL);
        jval = JSONToken_makeObject(pObj);
        while ((token = JSONScanner_getToken(this)) && !JSONToken_isDone(token) && token->type!=jt_objEnd) {
            if (token->type == jt_comma) {
                JSONToken_free(token);
                token = JSONScanner_getToken(this);
            }
            if (token->type != jt_string) {
                depth--;
                return JSONToken_makeError(this, token, "Expected a string");
            }
            JSONToken* name = token;
            bgtrace3(1,"%*s |name='%s'\n", depth*3,"",  JSONToken_ToString(token));

            token = JSONScanner_getToken(this);
            if (token->type != jt_colon) {
                depth--;
                return JSONToken_makeError(this, token, "Expected a colon");
            }
            JSONToken_free(token);

            JSONToken* value = JSONScanner_getValue(this);
            if (!JSONType_isAValue(value->type)) {
                depth--;
                return JSONToken_makeError(this,value, "Unexpected token");
            }

            if (strcmp(name->value,"_OID")==0) {
                // // use _OID to update the objDictionary so that we can fixup relative objRefs
                // char* sessionOID = value;
                // objDictionary[sessionOID]=currentStack->pObj->name;
                // objDictionary[currentStack->pObj->name]=sessionOID;

            } else if (strcmp(name->value,"_Ref")==0 || strcmp(name->value,"0")==0) {
                // ignore _Ref and "0" on restore

            } else if (strcmp(name->value,"_CLASS")==0) {
                BashObj_setClass(pObj, value->value);

            } else {
                BashObj_setMemberValue(pObj, name->value, (JSONType_isBashObj(value->type)) ? (((BashObj*)value->value)->ref) : value->value );
            }

            JSONToken_free(name);
            JSONToken_free(value);
        }

    } else if (token->type==jt_arrayStart) {
        BashObj* pObj = BashObj_makeNewObject("Array", NULL);
        jval = JSONToken_makeObject(pObj);
        int index=0;
        JSONToken* element;
        while ((element = JSONScanner_getValue(this)) && !JSONToken_isDone(element) && element->type!=jt_arrayEnd) {
            if (element->type == jt_comma) {
                JSONToken_free(element);
                element = JSONScanner_getValue(this);
            }

            char* indexStr = itos(index++);
            bgtrace4(1,"%*s |element[%s]='%s'\n", depth*3,"", indexStr,  JSONToken_ToString(element));

            if (JSONType_isBashObj(element->type))
                BashObj_setMemberValue(pObj, indexStr, (((BashObj*)element->value)->ref) );
            else if (JSONType_isAValue(element->type))
                BashObj_setMemberValue(pObj, indexStr, element->value );

            else {
                depth--;
                return JSONToken_makeError(this,element, "Unexpected token");
            }

            xfree(indexStr);
            JSONToken_free(element);
        }

    } else {
        jval = JSONToken_copy(token);
    }

    JSONToken_free(token);
    depth--;
    return jval;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// bgObjects ConstructObjectFromJson <objVar>
int ConstructObjectFromJson(WORD_LIST* list)
{
//    begin_unwind_frame("bgObjects");

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

//    discard_unwind_frame ("bgObjects");
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

char* bgMakeAnchoredRegEx(char* expr)
{
    char* s = xmalloc(strlen(expr)+3);
    *s = '\0';
    if (*expr != '^')
        strcat(s, "^");
    strcat(s, expr);
    if (expr[strlen(expr)-1] != '$')
        strcat(s, "$");
    return s;
}


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// bgObjects <cmd> ....
// This is the entry point builtin function. It dispatches the call to the specific function based on the first command word
int bgObjects_builtin(WORD_LIST* list)
{
    bgtrace1(1,"### STRing bgObjects_builtin(%s)\n", WordList_toString(list));
    bgtracePush();
    if (!list || !list->word) {
        printf ("Error - <cmd> is a required argument. See usage..\n\n");
        builtin_usage();
        bgtracePop();
        bgtrace0(1,"### ENDING bgObjects_builtin()\n");
        return (EX_USAGE);
    }

    // bgObjects _bgclassCall <oid> <refClass> <hierarchyLevel> |<objSyntaxStart> [<p1,2> ... <pN>]
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
        bgtrace0(1,"### ENDING 1 bgObjects_builtin()\n");
        return EXECUTION_SUCCESS;
    }


    // bgObjects _bgclassCall <oid> <refClass> <hierarchyLevel> |<objSyntaxStart> [<p1,2> ... <pN>]
    if (strcmp(list->word->word, "_bgclassCall")==0) {
        int ret = _bgclassCall(list->next);
        bgtracePop();
        bgtrace0(1,"### ENDING 1 bgObjects_builtin()\n");
        return ret;
    }

    // bgObjects _classUpdateVMT [-f|--force] <className>
    if (strcmp(list->word->word, "_classUpdateVMT")==0) {
        list=list->next; if (!list) return (EX_USAGE);
        int forceFlag=0;
        if (strcmp(list->word->word,"-f")==0 || strcmp(list->word->word,"--force")==0) {
            list=list->next; if (!list) return (EX_USAGE);
            forceFlag=1;
        }
//        begin_unwind_frame("bgObjects");
        int result = _classUpdateVMT(list->word->word, forceFlag);
//        discard_unwind_frame("bgObjects");
        bgtracePop();
        bgtrace0(1,"### ENDING 2 bgObjects_builtin()\n");
        return result;
    }

    // bgObjects Object_fromJSON
    if (strcmp(list->word->word, "Object_fromJSON")==0) {
        int ret = Object_fromJSON(list->next);
        bgtracePop();
        bgtrace0(1,"### ENDING 3 bgObjects_builtin()\n");
        return ret;
    }


    // bgObjects ConstructObject
    if (strcmp(list->word->word, "ConstructObject")==0) {
        ConstructObject(list->next);
        bgtracePop();
        bgtrace0(1,"### ENDING 3.5 bgObjects_builtin()\n");
        return EXECUTION_SUCCESS;
    }



    // bgObjects ConstructObjectFromJson
    if (strcmp(list->word->word, "ConstructObjectFromJson")==0) {
        int ret = ConstructObjectFromJson(list->next);
        bgtracePop();
        bgtrace0(1,"### ENDING 4 bgObjects_builtin()\n");
        return ret;
    }

    fprintf(stderr, "error: command not recognized cmd='%s'\n", (list && list->word)?list->word->word:"");
    bgtracePop();
    bgtrace0(1,"### ENDING 6 bgObjects_builtin()\n");
    return (EX_USAGE);
}


/* Called when `bgObjects' is enabled and loaded from the shared object.  If this
   function returns 0, the load fails. */
int bgObjects_builtin_load (char* name)
{
    bgtraceOn();
    _bgtrace(0,"LOAD ############################################################################################\n");
    return (1);
}

/* Called when `bgObjects' is disabled. */
void bgObjects_builtin_unload (char* name)
{
}

char *_bgclassCall_doc[] = {
	"Invoke a method or operator on a bash object instance.",
	"",
	"The bg_objects.sh style of object oriented bash uses a syntax that invokes _bgclassCall.",
	(char *)NULL
};

struct builtin bgObjects_struct = {
	"bgObjects",			/* builtin name */
	bgObjects_builtin,		/* function implementing the builtin */
	BUILTIN_ENABLED,		/* initial flags for builtin */
	_bgclassCall_doc,			/* array of long documentation strings. */
	"bgObjects <oid> <className> <hierarchyLevel> '|' <objectSyntaxToExecute>",			/* usage synopsis; becomes short_doc */
	0				/* reserved for internal use */
};
