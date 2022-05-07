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

#include "loadables.h"
#include "variables.h"
//#include "execute_cmd.h"
#include <stdarg.h>

#if !defined (errno)
extern int errno;
#endif

extern int _classUpdateVMT(char* className, int forceFlag);

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

char* savestringn(x,n)
    char* x;
    int n;
{
    char *p=(char *)strncpy (xmalloc (1 + n), (x),n);
    p[n]='\0';
    return p;
}




SHELL_VAR* find_variableWithSuffix(char* varname, char* suffix) {
    char* buf = xmalloc(strlen(varname)+strlen(suffix)+1);
    strcpy(buf, varname);
    strcat(buf,suffix);
    SHELL_VAR* vVar = find_variable(buf);
    xfree(buf);
    return vVar;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// bgtrace

FILE* _bgclassCallTraceFD=NULL;

void bgtraceStatus() {
    if (_bgclassCallTraceFD)
        printf("bgObjects tracing is ON\n");
    else
        printf("bgObjects tracing is OFF\n");
}

void bgtraceOn(char* location) {
    if (!_bgclassCallTraceFD) {
        _bgclassCallTraceFD=fopen("/tmp/bgtrace.out","a+");
        if (!_bgclassCallTraceFD)
            fprintf(stderr, "FAILED to open trace file '/tmp/bgtrace.out' errno='%d'\n", errno);
        else
            fprintf(_bgclassCallTraceFD, "BASH bgObjects trace started (%s)\n",location);
    }
}

void bgtraceOff() {
    if (_bgclassCallTraceFD) {
        fprintf(_bgclassCallTraceFD, "BASH bgObjects trace ended\n");
        fclose(_bgclassCallTraceFD);
        _bgclassCallTraceFD=NULL;
    }
}

void traceCntr(WORD_LIST *args) {
    if (args && strcmp(args->word->word, "on")==0) {
        bgtraceOn("traceCntr");
    } else if (args && strcmp(args->word->word, "off")==0) {
        bgtraceOff();
    } else {
        bgtraceStatus();
    }
}

int bgtrace(char* fmt, ...) {
    if (!_bgclassCallTraceFD) return 1;
    va_list args;
    SH_VA_START (args, fmt);
    vfprintf (_bgclassCallTraceFD, fmt, args);
    fflush(_bgclassCallTraceFD);
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
    va_list args;
    SH_VA_START (args, fmt);

    char* outputMsg=xmalloc(512);
    vsnprintf(outputMsg, 511, fmt, args);

    // local _rsvMemberType="$_rsvMemberType"
    char* temp = MemberTypeToString(mt_invalidExpression,outputMsg,NULL);
    bind_variable_value(make_local_variable("_rsvMemberType",0), temp, 0);
    xfree(temp);
    xfree(outputMsg);

    //builtin_error(fmt, p1, p2, p3, p4, p5);
    run_unwind_frame ("bgObjects");
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
int BashObjRef_init(BashObjRef* pRef, char* objRefStr) {

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

SHELL_VAR* assertClassExists(char* className, int* pErr) {
    if (!className) {
        if (pErr) *pErr=1;
        setErrorMsg("className is empty\n");
        return NULL;
    }
    SHELL_VAR* vClass = find_variable(className);
    if (!vClass) {
        // TODO: try to load a library '<className>.sh'
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

typedef struct {
  char name[255];
  char ref[300];
  SHELL_VAR* vThis;
  SHELL_VAR* vThisSys;
  SHELL_VAR* vCLASS;
  SHELL_VAR* vVMT;

  char* refClass;
  int superCallFlag;
} BashObj;

void BashObj_makeRef(BashObj* pObj) {
    // '_bgclassCall <oid> <class> 0 | '
    //  12  + 8 + 1 + strlen(<oid>) + strlen(<class>)
    strcpy(pObj->ref,"_bgclassCall ");
    strcat(pObj->ref,pObj->name);
    strcat(pObj->ref," ");
    strcat(pObj->ref,pObj->vCLASS->name);
    strcat(pObj->ref," 0 | ");
}

int BashObj_Init(BashObj* pObj, char* name, char* refClass, char* hierarchyLevel) {
    if (strlen(name) >200) return setErrorMsg("BashObj_Init: error: name is too long (>200)\nname='%s'\n", name);
    int errFlag=0;

    pObj->refClass=refClass;
    pObj->superCallFlag = (hierarchyLevel && strcmp(hierarchyLevel, "0")!=0);

    pObj->vThis=find_variable(name);
    if (!(pObj->vThis))
        return setErrorMsg("Error - <oid> (%s) does not exist \n", name);
    if (!assoc_p(pObj->vThis) && !array_p(pObj->vThis))
        return setErrorMsg ("Error - <oid> (%s) is not an array\n", name);

    // an object instance may or may not have a separate associative array for its system variables. If not, system vars are mixed
    // in with the object members but we can tell them apart because system vars must begin with an '_'.  [0] can also be a system
    // var but it is special because its functionality depends on putting it in the pThis array. The class attribute [defaultIndex]
    // determines if pThis[0] is a system var or a memberVar
    strcpy(pObj->name,name); strcat(pObj->name,"_sys");
    pObj->vThisSys=find_variable(pObj->name);
    if (!pObj->vThisSys)
        pObj->vThisSys=pObj->vThis;

    // lookup the vCLASS array
    char* _CLASS = assoc_reference(assoc_cell(pObj->vThisSys), "_CLASS");
    if (!_CLASS)
        return setErrorMsg("Error - malformed object instance. missing the _CLASS system variable. instance='%s'\n", pObj->vThisSys->name);
    pObj->vCLASS = assertClassExists(_CLASS, &errFlag); if (errFlag!=0) return EXECUTION_FAILURE;

    // lookup the VMT array taking into account that an obj instance may have a separate VMT unique to it (_this[_VMT]) and if this
    // is a $super.<method>... call, it is the VMT of the baseClass of the calling <objRef>'s refClass.
    if (!pObj->superCallFlag) {
        // pObj->vVMT = "${_this[_VMT]:-${_this[_CLASS]}_vmt}"
        char* vmtName = assoc_reference(assoc_cell(pObj->vThisSys), "_VMT");
        if (!vmtName) {
            strcpy(pObj->name, pObj->vCLASS->name); strcat(pObj->name, "_vmt");
            vmtName=pObj->name;
        }
        pObj->vVMT=find_variable(vmtName);
    } else {
        // we get here when we are processing a $super.<method> call. $super is only available in methods and the refClass in the
        // $super <objRef> is the class of the method that is running and hierarchyLevel is !="0". We need to set the VMT to the
        // baseClass of the the refClass from the $super <objRef>. If there are no base classes left, we set the VMT to empty_vmt
        // to indicate that there is nother left to call.
        // pObj->vVMT = "${refClass[baseClass]}_vmt"
        SHELL_VAR* vRefClass=assertClassExists(pObj->refClass, &errFlag); if (errFlag!=0) return EXECUTION_FAILURE;
        char* refBaseClass=assoc_reference(assoc_cell(vRefClass), "baseClass");
        if (refBaseClass) {
            pObj->vVMT = find_variableWithSuffix(refBaseClass, "_vmt");
        } else {
            pObj->vVMT = find_variable("empty_vmt");
        }
    }
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
    if (!BashObj_Init(pObj, name, refClass, hierarchyLevel)) {
        xfree(pObj);
        return NULL;
    }
    return pObj;
}

// c implementation of the bash library function
// returns "heap_A_XXXXXXXXX" where X's are random chars
// TODO: add support for passing in attributes like the bash version does
char* varNewHeapVar(char* attributes) {
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
    return buf;
}

BashObj* BashObj_makeNewObject(char* _CLASS, ...) {
    //TODO: DeclareClassEnd "${_CLASS%%::*}"
    _classUpdateVMT(_CLASS, 0);

    BashObj* newObj = xmalloc(sizeof(BashObj));
    newObj->refClass=NULL;
    newObj->superCallFlag=0;

	newObj->vCLASS = assertClassExists(_CLASS, NULL);
    if (!newObj->vCLASS)
        return NULL;

    newObj->vVMT = find_variableWithSuffix(_CLASS,"_vmt");

    // TODO: ### support dynamic base class implemented construction

    // TODO: deal with the destination var? or maybe in C we do that at a higher level?

    char* oidAttributes = assoc_reference( assoc_cell(newObj->vCLASS),"oidAttributes");
    int isNumericArray=0;
    for (char* c=oidAttributes; c && *c; c++)
        if (*c=='a')
            isNumericArray=1;

    char* _OID = varNewHeapVar((isNumericArray)?"a":"A");
    strcpy(newObj->name, _OID);

    char* _OID_sys = xmalloc(strlen(_OID)+4+1);
    strcpy(_OID_sys, _OID); strcat(_OID_sys, "_sys");

    if (isNumericArray)
        newObj->vThis = make_new_array_variable(_OID);
    else
        newObj->vThis = make_new_assoc_variable(_OID);
    newObj->vThisSys = make_new_assoc_variable(_OID_sys);

    BashObj_makeRef(newObj);

    assoc_insert (assoc_cell (newObj->vThisSys), savestring("_OID"), _OID);
    assoc_insert (assoc_cell (newObj->vThisSys), savestring("_CLASS"), _CLASS);
    assoc_insert (assoc_cell (newObj->vThisSys), savestring("_Ref"), newObj->ref);
    assoc_insert (assoc_cell (newObj->vThisSys), savestring("0"), newObj->ref);
    char* defIdxSetting = assoc_reference( assoc_cell(newObj->vCLASS),"defaultIndex");
    if (defIdxSetting && strcmp(defIdxSetting,"on")==0)
        assoc_insert (assoc_cell (newObj->vThis), savestring ("0"), newObj->ref);

    // constructor context
    // local -n class="$_CLASS"
	// local -n newTarget="$_CLASS"
    // local -n this="$_OID"

    xfree(_OID);
    xfree(_OID_sys);
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
        return assoc_reference(assoc_cell(pObj->vThisSys), memberName);
    }else if (array_p(pObj->vThis)) {
        if (!isNumber(memberName)) {
            //setErrorMsg("memberName is not a valid numeric array index\n\tmemberName='%s'\n", memberName);
            return NULL;
        }
        return array_reference(array_cell(pObj->vThis),    array_expand_index(pObj->vThis, memberName, strlen(memberName), 0));
    }else {
        return assoc_reference(assoc_cell(pObj->vThis),    memberName);
    }
}

int BashObj_setMemberValue(BashObj* pObj, char* memberName, char* value) {
    if (*memberName == '_'){
        assoc_insert (assoc_cell (pObj->vThisSys), savestring(memberName), value);
    }else if (array_p(pObj->vThis)) {
        arrayind_t index = (isNumber(memberName))
            ? atoi(memberName)
            : 1+array_max_index(array_cell(pObj->vThis));
        bind_array_element(pObj->vThis, index, value, 0);
    }else {
        assoc_insert (assoc_cell (pObj->vThis),    savestring(memberName), value);
    }
    return EXECUTION_SUCCESS;
}

void BashObj_setClass(BashObj* pObj, char* newClassName) {
bgtrace("BashObj_setClass(%s, %s)\n", pObj->name, newClassName);
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
    char* defIdxSetting = assoc_reference( assoc_cell(pObj->vCLASS),"defaultIndex");
    if (defIdxSetting && strcmp(defIdxSetting,"on")==0)
        assoc_insert(assoc_cell (pObj->vThis), savestring ("0"), pObj->ref);
    else
        assoc_remove( assoc_cell(pObj->vThis), "0" );

    _classUpdateVMT(newClassName, 0);
bgtrace("BashObj_setClass(%s, %s)\n", pObj->name, newClassName);

	// # invoke the _onClassSet methods that exist for any Class in this hierarchy
	// local -n static="$_CLASS"
	// local -n _VMT="${_this[_VMT]:-${_this[_CLASS]}_vmt}"
	// local _cname; for _cname in ${class[classHierarchy]}; do
	// 	type -t $_cname::_onClassSet &>/dev/null && $_cname::_onClassSet "$@"
	// done
}


char* BashObj_getMethod(BashObj* pObj, char* methodName) {
    if (strncmp(methodName,"_method::",9)==0 || strncmp(methodName,"_static::",9)==0)
        return assoc_reference(assoc_cell(pObj->vVMT), methodName);
    else {
        if (strlen(methodName)>200) {
            setErrorMsg("BashObj_getMethod: methodName is too long (>200). \n\tmethodName='%s'\n", methodName);
            return NULL;
        }
        char buf[255]; strcpy(buf, "_method::"); strcat(buf,methodName);
        return assoc_reference(assoc_cell(pObj->vVMT), buf);
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
        BashObj* subObj=BashObj_makeNewObject("Object");
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
    if (!BashObj_Init(&memberObj, oRef.oid, pObj->refClass, pObj->superCallFlag ? "1":"0")) {
        if (pErr) *pErr=EXECUTION_FAILURE;
        return 0;
    }
    memcpy(pObj,&memberObj, sizeof(*pObj));
    return 1;
}

// Make the local vars for the method execution environment
int BashObj_setupMethodCallContext(BashObj* pObj)
{
    // local _OID="$oid"
    SHELL_VAR* _OID=make_local_variable("_OID",0);
    _OID = bind_variable_value(_OID,pObj->vThis->name, 0);

    // local -n this="$oid"
    SHELL_VAR* this=make_local_variable("this",0);
    VSETATTR(this, att_nameref);
    this = bind_variable_value(this,pObj->vThis->name, 0);

    // local _OID_sys="$oid"
    SHELL_VAR* _OID_sys=make_local_variable("_OID_sys",0);
    _OID_sys = bind_variable_value(_OID_sys,pObj->vThisSys->name, 0);

    // local -n _this="$oid"
    SHELL_VAR* _this=make_local_variable("_this",0);
    VSETATTR(_this, att_nameref);
    _this = bind_variable_value(_this,pObj->vThisSys->name, 0);

    // local -n static="$_CLASS"
    SHELL_VAR* vStatic=make_local_variable("static",0);
    VSETATTR(vStatic, att_nameref);
    vStatic = bind_variable_value(vStatic,pObj->vCLASS->name, 0);

    // local _CLASS="$refClass"
    // TODO: maybe this should not be here because it should be the refClass from the objRef that invokes the method
    SHELL_VAR* _CLASS=make_local_variable("_CLASS",0);
    _CLASS = bind_variable_value(_CLASS, pObj->refClass, 0);

    // local -n _VMT="$oid"
    SHELL_VAR* _VMT=make_local_variable("_VMT",0);
    VSETATTR(_VMT, att_nameref);
    _VMT = bind_variable_value(_VMT,pObj->vVMT->name, 0);

    // local _hierarchLevel="$1"
    bind_variable_value(make_local_variable("_hierarchLevel",0), (pObj->superCallFlag)?"1":"0", 0);


    return EXECUTION_SUCCESS;
}

void assoc_copyElements(HASH_TABLE* dest, HASH_TABLE* source) {
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
// BashClass

typedef struct {
    SHELL_VAR* vClass;
    SHELL_VAR* vVMT;
} BashClass;

int BashClass_init(BashClass* pCls, char* className)
{
    pCls->vClass = find_variable(className);
    if (!pCls->vClass)
        return setErrorMsg("bultin _classUpdateVMT: global class array does not exist for class '%s'\n", className);
    if (!assoc_p(pCls->vClass))
        return setErrorMsg("bultin _classUpdateVMT: global class array not an -A array for class '%s'\n", className);
    if (invisible_p(pCls->vClass)) {
        VUNSETATTR (pCls->vClass, att_invisible);
    }

    pCls->vVMT = find_variableWithSuffix(className, "_vmt");
    if (!pCls->vVMT)
        return setErrorMsg("bultin _classUpdateVMT: global class vmt array does not exist for class '%s_vmt'\n", className);
    if (!assoc_p(pCls->vVMT))
        return setErrorMsg("bultin _classUpdateVMT: global class vmt array is not an -A array for class '%s'\n", className);
    if (invisible_p(pCls->vVMT)) {
        VUNSETATTR (pCls->vVMT, att_invisible);
    }
    return EXECUTION_SUCCESS;
}

int BashClass_isVMTDirty(BashClass* pCls, char* currentCacheNumStr) {
    char* baseClassNameVmtCacheNum = assoc_reference(assoc_cell(pCls->vClass),"vmtCacheNum");
    return (!baseClassNameVmtCacheNum || strcmp(baseClassNameVmtCacheNum, currentCacheNumStr)!=0);
}

void DeclareClassEnd(char* className)
{
    char* tstr = xmalloc(strlen(className)+10);
    strcpy(tstr, className);
    strcat(tstr, "_initData");
    SHELL_VAR* vInitData = find_variable(tstr);
    if (!vInitData)
        return;
    xfree(tstr);

//    char* baseClass = SHELL_VAR
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
bgtrace("_classUpdateVMT(%s)\n", className);

    if (!className || !*className)
        return EXECUTION_FAILURE;

    int classNameLen=strlen(className);
    BashClass class; BashClass_init(&class,className);
//bgtrace("#### %s: baseClass='%s'\n",className,assoc_reference(assoc_cell(class.vClass), "baseClass"));
bgtrace("#### %s: '%s'\n", className, string_list(assoc_keys_to_word_list(assoc_cell(class.vClass)) ) );

    // # force resets the vmtCacheNum of this entire class hierarchy so that the algorithm will rescan them all during this run
    if (forceFlag) {
        BGString classHierarchy; BGString_initFromStr(&classHierarchy, assoc_reference(assoc_cell(class.vClass), "classHierarchy") );
        BGString_replaceWhitespaceWithNulls(&classHierarchy);
        char* oneClass;
        while (oneClass=BGString_nextWord(&classHierarchy)) {
            SHELL_VAR* vOneClass = find_variable(oneClass);
            if (vOneClass) {
                assoc_insert(assoc_cell(vOneClass), savestring("vmtCacheNum"), "-1");
            }
        }
        BGString_free(&classHierarchy);
    }


    // local currentCacheNum; importCntr getRevNumber currentCacheNum
    int currentCacheNum=0;
    SHELL_VAR* vImportedLibraries=find_variable("_importedLibraries");
    if (vImportedLibraries)
        currentCacheNum+=(vImportedLibraries) ? assoc_num_elements(assoc_cell(vImportedLibraries)) : 0;
    SHELL_VAR* vImportedLibrariesBumpAdj=find_variable("_importedLibrariesBumpAdj");
    if (vImportedLibrariesBumpAdj)
        currentCacheNum+=evalexp(value_cell(vImportedLibrariesBumpAdj),0,0);
    char* currentCacheNumStr = itos(currentCacheNum);


    // if [ "${class[vmtCacheNum]}" != "$currentCacheNum" ]; then
    // char* classVmtCacheNum = assoc_reference(assoc_cell(class.vClass),"vmtCacheNum");
    // if (!classVmtCacheNum || strcmp(classVmtCacheNum, currentCacheNumStr)!=0) {
    if (BashClass_isVMTDirty(&class, currentCacheNumStr)) {
bgtrace("2 _classUpdateVMT(%s)\n", className);
       // class[vmtCacheNum]="$currentCacheNum"
        assoc_insert(assoc_cell(class.vClass), savestring("vmtCacheNum"), currentCacheNumStr);

        // vmt=()
         assoc_flush(assoc_cell(class.vVMT));

        // # init the vmt with the contents of the base class vmt (Object does not have a baseClassName)
        // each baseclass will have _classUpdateVMT(baseClassName) called to recursively make everything uptodate.
        char* baseClassName = assoc_reference(assoc_cell(class.vClass), "baseClass");
bgtrace("3 baseClassName='%s'    class.vClass->name='%s' \n", baseClassName, class.vClass->name);
bgtrace("4 '%s'\n", string_list(assoc_keys_to_word_list(assoc_cell(class.vClass)) ) );
        if (baseClassName && *baseClassName) {
bgtrace("   _classUpdateVMT doing '%s'\n", baseClassName);
            BashClass baseClass; BashClass_init(&baseClass,baseClassName);

            // # update the base class vmt if needed
            // if  [ "${baseClassName[vmtCacheNum]}" != "$currentCacheNum" ]; then
            char* baseClassNameVmtCacheNum = assoc_reference(assoc_cell(baseClass.vClass),"vmtCacheNum");
            if (!baseClassNameVmtCacheNum || strcmp(baseClassNameVmtCacheNum, currentCacheNumStr)!=0) {
                // recursive call...
                _classUpdateVMT(baseClassName,0);
            }
            // # copy the whole base class VMT into our VMT (static and non-static)
            assoc_copyElements(assoc_cell(class.vVMT), assoc_cell(baseClass.vVMT));
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
                assoc_insert(assoc_cell(class.vVMT), methodBase, funcList[i]->name);
                BGString_append(&methodsStrList, funcList[i]->name, "\n");
            }
            if (strncmp(funcList[i]->name, prefix2,prefix2Len)==0) {
                count++;
                char* staticBase=xmalloc(strlen(funcList[i]->name)+7+1);
                strcpy(staticBase, "_static");
                strcat(staticBase, funcList[i]->name+prefix2Len-2);
                assoc_insert(assoc_cell(class.vVMT), staticBase, funcList[i]->name);
                BGString_append(&staticMethodsStrList, funcList[i]->name, "\n");
            }
        }
        assoc_insert(assoc_cell(class.vClass), savestring("methods"),       methodsStrList.buf);
        assoc_insert(assoc_cell(class.vClass), savestring("staticMethods"), staticMethodsStrList.buf);
        BGString_free(&methodsStrList);
        BGString_free(&staticMethodsStrList);
        xfree(prefix1);
        xfree(prefix2);

        // 	_classScanForClassMethods "$className" class[methods]
        // 	local _mname; for _mname in ${class[methods]}; do
        // 		vmt[_method::${_mname#*::}]="$_mname"
        // 	done
        //
        // 	# add the static methods of this class
        // 	_classScanForStaticMethods "$className" class[staticMethods]
        // 	local _mname; for _mname in ${class[staticMethods]}; do
        // 		vmt[_static::${_mname##*::}]="$_mname"
        // 	done
        //
        // 	#bgtraceVars className vmt
    }

    xfree(currentCacheNumStr);
    return EXECUTION_SUCCESS;
}


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// CMD: _bgclassCall

int _bgclassCall(WORD_LIST* list)
{
    begin_unwind_frame ("bgObjects");

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
    char* refHierachy=list->word->word;
    list = list->next;

    BashObj objInstance;
    if (!BashObj_Init(&objInstance, oid, refClass, refHierachy))
        return EXECUTION_FAILURE;

    // at this point the <objRef> components (except the |) have been removed so its a good time to record the _memberExpression
    // that is used as context in errors. The '|' from the <objRef> may be stuck to the first position so remove that if it is
    char* _memberExpression = string_list(list);
    add_unwind_protect(xfree,_memberExpression);
    char* pStart=_memberExpression;
    if (*pStart == '|') pStart++;
    while (pStart && *pStart && whitespace(*pStart)) pStart++;
    if (pStart> _memberExpression)
        memmove(_memberExpression, pStart, strlen(pStart)+1);
    bind_variable_value(make_local_variable("_memberExpression",0), _memberExpression, 0);


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

    //bgtrace("\n01234567890123456789\n%s\n",objSyntaxStart);


    // These variables are static so that we only compile the pattern once
    //                   ((                            opWBr                                         )(  Br         )|( opNBr  ))(firstArg)?$
    static char* pattern="((\\.unset|\\.exists|\\.isA|\\.getType|\\.getOID|\\.getRef|\\.toString|=new|)([[:space:]]|$)|([+]?=|::))(.*)?$";
    static regex_t regex;
    static int regexInit;
    if (!regexInit) {
        if (regcomp (&regex, pattern, REG_EXTENDED)) // |REG_ICASE
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
    if (regexec (&regex, objSyntaxStart, regex.re_nsub + 1, matches, 0))
        return setErrorMsg("error: invalid object expression (%s) did not match regex='%s'\n", objSyntaxStart, pattern);


    // the _memberOp can be in the 2(opWBr) or 4(opNBr) position
    char* _memberOp;
    if (matches[2].rm_so<matches[2].rm_eo)
        _memberOp=savestringn(objSyntaxStart+matches[2].rm_so,(matches[2].rm_eo-matches[2].rm_so));
    else if (matches[4].rm_so<matches[4].rm_eo)
        _memberOp=savestringn(objSyntaxStart+matches[4].rm_so,(matches[4].rm_eo-matches[4].rm_so));
    else
        _memberOp=savestring("");
    add_unwind_protect(xfree,_memberOp);


    // _chainedObjOrMember is the beginning of objSyntaxStart up to the start of the match (start of op)
    //int objSyntaxStartLen=strlen(objSyntaxStart); // to be able to trace the whole thing including \0 we place
    char* _chainedObjOrMember=objSyntaxStart;
    bind_variable_value(make_local_variable("_chainedObjOrMember",0), savestringn(_chainedObjOrMember, matches[0].rm_so), 0);
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

    // if (_bgclassCallTraceFD) {
    //     for (int i=0; i<objSyntaxStartLen; i++)
    //         bgtrace("%c",(objSyntaxStart[i]=='\0')?'_':objSyntaxStart[i]);
    //     bgtrace("\n");
    // }

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
            BashObj staticObj; BashObj_Init(&staticObj, objInstance.vCLASS->name, NULL,NULL);
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
    add_unwind_protect(xfree,_rsvMemberName);
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
    add_unwind_protect(xfree,_rsvMemberTypeStr);

    // bgtrace("   _memberOp        ='%s'\n", (_memberOp)?_memberOp:"");
    // bgtrace("   _rsvMemberType   ='%s'\n", (_rsvMemberTypeStr)?_rsvMemberTypeStr:"");
    // bgtrace("   _rsvMemberName   ='%s'\n", (_rsvMemberName)?_rsvMemberName:"");
    // bgtrace("   _rsvMemberValue  ='%s'\n", (_rsvMemberValue)?_rsvMemberValue:"");
    // bgtrace("   _memberExpression='%s'\n", (_memberExpression)?_memberExpression:"");

    //[[ "$_rsvMemberType" =~ ^invalidExpression ]] && assertObjExpressionError ...
    //case ${_memberOp:-defaultOp}:${_rsvMemberType} in

    // setup the method environment for the rest of _bgclassCall to use and in case it invokes a method
    BashObj_setupMethodCallContext(&objInstance);

    /* This block creates local vars that the bash function _bgclassCall will use in deciding what to do  */

    // local _rsvOID="$_rsvOID"
    bind_variable_value(make_local_variable("_rsvOID",0), objInstance.name, 0);

    // local _memberOp="$_memberOp"
    bind_variable_value(make_local_variable("_memberOp",0), _memberOp, 0);

    // local _rsvMemberName="$_rsvMemberName"
    bind_variable_value(make_local_variable("_rsvMemberName",0), _rsvMemberName, 0);

    // local _rsvMemberType="$_rsvMemberType"
    bind_variable_value(make_local_variable("_rsvMemberType",0), _rsvMemberTypeStr, 0);

    bind_variable_value(make_local_variable("_resultCode",0), "", 0);

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
        bind_variable_value(make_local_variable("_METHOD",0), (_METHOD)?_METHOD:"", 0);

        // make local namerefs to the oid's of any object members
        if (assoc_p(objInstance.vThis)) {
            ObjMemberItr i;
            for (BUCKET_CONTENTS *item=ObjMemberItr_init(&i,&objInstance, ovt_both); item; item=ObjMemberItr_next(&i)) {
                if (strcmp(item->key,"0")!=0 && strcmp(item->key,"_Ref")!=0 && item->data ) {
                    if (strncmp(item->data, "_bgclassCall",12)==0) {
                        //bgtraceOn(); bgtrace("!!! item->key='%s'\n", item->key);
                        char* memOid = extractOID(item->data);
                        SHELL_VAR* vMemOid=make_local_variable(item->key,0);
                        VSETATTR(vMemOid, att_nameref);
                        vMemOid = bind_variable_value(vMemOid,memOid, 0);
                        xfree(memOid);
                    } else if (strncmp(item->data, "heap_",5)==0) {
                        SHELL_VAR* vMemOid=make_local_variable(item->key,0);
                        VSETATTR(vMemOid, att_nameref);
                        vMemOid = bind_variable_value(vMemOid,item->data, 0);
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

        bind_variable_value(make_local_variable("super",0), superObjRef.buf, 0);
        BGString_free(&superObjRef);
    }


    // cleanup before we leave
    xfree(_rsvMemberTypeStr);
    xfree(_rsvMemberName);_rsvMemberName=NULL;
    xfree(_memberExpression);_memberExpression=NULL;
    xfree(_memberOp); _memberOp=NULL;
    discard_unwind_frame ("bgObjects");

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
void SWList_print(SimpleWordList* list) {
    while (list) {
        fprintf(_bgclassCallTraceFD, "'%s' ",list->word);
        list=list->next;
    }
    fprintf(_bgclassCallTraceFD, "\n");
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
void BGObjectStack_print(BGObjectStack* list) {
    while (list) {
        fprintf(_bgclassCallTraceFD, "'%s' ",list->pObj->name);
        list=list->next;
    }
    fprintf(_bgclassCallTraceFD, "\n");
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
    begin_unwind_frame ("bgObjects");

    if (!list || !list->word) {
        setErrorMsg("Error - <objVar> is a required argument to restoreObject. See usage..\n\n");
        builtin_usage();
        return (EX_USAGE);
    }

    BashObj* scope = BashObj_find(list->word->word, NULL,NULL);
    if (!scope) {
        return setErrorMsg("Error - <objVar> (%s) does not exist\n\n", list->word->word);
    }

    //SHELL_VAR* vCurrentStack=find_variable("currentStack");
    BGObjectStack* currentStack=NULL;

    currentStack = BGObjectStack_unshift(currentStack, scope);

    char* ifs_chars = getifs();
    char *line=NULL, *startToken=NULL, *sepStart=NULL;
    size_t lineAllocSize=0;
    while (zgetline(0, &line, &lineAllocSize, '\n', 0)>0) {
        // bgtrace("        01234567890123456789012345678901234567890\n");
        // bgtrace("line = '%s'\n", line);
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
            BashObj* subObj = BashObj_makeNewObject(className);
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
    discard_unwind_frame ("bgObjects");
    return EXECUTION_SUCCESS;
}


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// JSONScanner
// methods defined below
typedef struct {
    char* buf;
    size_t length;
    char* pos;
    char* end;
    char* filename;
} JSONScanner;


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// JSONType
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
typedef struct {
    JSONType type;
    char* value;
} JSONToken;

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
JSONScanner* JSONScanner_new(char* inFile) {
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
//JSONToken_print(_bgclassCallTraceFD, token, "");


JSONToken* JSONScanner_getValue(JSONScanner* this) {
//bgtraceOn(NULL);
    static int depth = 0;
    depth++;
    JSONToken* jval;

    JSONToken* token = JSONScanner_getToken(this);
    bgtrace("%*sgetVal start token='%s'\n", depth*3,"",  JSONToken_ToString(token));
    if (token->type == jt_objStart) {
        JSONToken_free(token);
        BashObj* pObj = BashObj_makeNewObject("Object");
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
            bgtrace("%*s |name='%s'\n", depth*3,"",  JSONToken_ToString(token));

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
        BashObj* pObj = BashObj_makeNewObject("Array");
        jval = JSONToken_makeObject(pObj);
        int index=0;
        JSONToken* element;
        while ((element = JSONScanner_getValue(this)) && !JSONToken_isDone(element) && element->type!=jt_arrayEnd) {
            if (element->type == jt_comma) {
                JSONToken_free(element);
                element = JSONScanner_getValue(this);
            }

            char* indexStr = itos(index++);
            bgtrace("%*s |element[%s]='%s'\n", depth*3,"", indexStr,  JSONToken_ToString(element));

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
int ConstructObjectFromJson(WORD_LIST* list) {
    begin_unwind_frame("bgObjects");

    if (!list || list_length(list) < 2) {
        setErrorMsg("Error - <objVar> and <restoreFile> are required arguments to ConstructObjectFromJson. See usage..\n\n");
        return (EX_USAGE);
    }

    char* objVar = list->word->word;
    list = list->next;
    SHELL_VAR* vObjVar = find_variable(objVar);
    if (!vObjVar) {
        vObjVar = make_local_variable(objVar,0);
        VSETATTR(vObjVar, att_nameref);
    } else {
    }
    VUNSETATTR(vObjVar, att_invisible);

    char* jsonFile = list->word->word;
    list = list->next;

    JSONScanner* scanner = JSONScanner_new(jsonFile);
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
        bind_variable_value(vObjVar,((BashObj*)jValue->value)->name, 0);
    } else if (JSONType_isBashObj(jValue->type) && (array_p(vObjVar)) ) {
        bind_array_element(vObjVar,0, ((BashObj*)jValue->value)->ref, 0);
    } else if (JSONType_isBashObj(jValue->type) && (assoc_p(vObjVar)) ) {
        assoc_insert(assoc_cell(vObjVar), savestring("0"), ((BashObj*)jValue->value)->ref);
    } else if (JSONType_isBashObj(jValue->type) ) {
        bind_variable_value(vObjVar,((BashObj*)jValue->value)->ref, 0);

    // we found a primitive
    } else if (nameref_p(vObjVar)) {
        char* transName = varNewHeapVar("");
        bind_variable_value(make_local_variable(transName,0), jValue->value, 0);
        bind_variable_value(vObjVar,jValue->value, 0);
    } else if (array_p(vObjVar)) {
        bind_array_element(vObjVar,0, jValue->value, 0);
    } else if (assoc_p(vObjVar)) {
        assoc_insert(assoc_cell(vObjVar), savestring("0"), jValue->value);
    } else {
        bind_variable_value(vObjVar,jValue->value, 0);
    }

    discard_unwind_frame ("bgObjects");
    return EXECUTION_SUCCESS;
}



/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// bgObjects <cmd> ....
// This is the entry point builtin function. It dispatches the call to the specific function based on the first command word
int bgObjects_builtin (WORD_LIST* list)
{
    if (!list || !list->word) {
        printf ("Error - <cmd> is a required argument. See usage..\n\n");
        builtin_usage();
        return (EX_USAGE);
    }

    // bgObjects _bgclassCall <oid> <refClass> <hierarchyLevel> |<objSyntaxStart> [<p1,2> ... <pN>]
    if (strcmp(list->word->word, "_bgclassCall")==0) {
        return _bgclassCall(list->next);
    }

    // bgObjects _classUpdateVMT [-f|--force] <className>
    if (strcmp(list->word->word, "_classUpdateVMT")==0) {
        list=list->next; if (!list) return (EX_USAGE);
        int forceFlag=0;
        if (strcmp(list->word->word,"-f")==0 || strcmp(list->word->word,"--force")==0) {
            list=list->next; if (!list) return (EX_USAGE);
            forceFlag=1;
        }
        begin_unwind_frame("bgObjects");
        int result = _classUpdateVMT(list->word->word, forceFlag);
        discard_unwind_frame("bgObjects");
        return result;
    }

    // bgObjects restoreObject
    if (strcmp(list->word->word, "restoreObject")==0) {
        return restoreObject(list->next);
    }


    // bgObjects ConstructObjectFromJson
    if (strcmp(list->word->word, "ConstructObjectFromJson")==0) {
        return ConstructObjectFromJson(list->next);
    }


    // bgObjects trace on|off|status
    if (strcmp(list->word->word, "trace")==0) {
        traceCntr(list->next);
        return EXECUTION_SUCCESS;
    }

    fprintf(stderr, "error: command not recognized cmd='%s'\n", (list && list->word)?list->word->word:"");
    return (EX_USAGE);
}


/* Called when `bgObjects' is enabled and loaded from the shared object.  If this
   function returns 0, the load fails. */
int bgObjects_builtin_load (char* name)
{
    //bgtraceOn();
    return (1);
}

/* Called when `bgObjects' is disabled. */
void bgObjects_builtin_unload (char* name)
{
    bgtraceOff();
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
