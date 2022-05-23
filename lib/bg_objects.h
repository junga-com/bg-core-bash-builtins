
#if !defined (_bg_objects_H_)
#define _bg_objects_H_

#include "bg_bashAPI.h"

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

extern char* MemberTypeToString(MemberType mt, char* errorMsg, char* _rsvMemberValue);

extern int setErrorMsg(char* fmt, ...);

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// BashObjRef

typedef struct {
  char oid[255];
  char className[255];
  int superCallFlag;
} BashObjRef;

extern char* extractOID(char* objRef);

extern int BashObjRef_init(BashObjRef* pRef, char* objRefStr);

// TODO: where is BashObjRef_free?

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// BashObj

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

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// BashClass

typedef struct {
    SHELL_VAR* vClass;
    SHELL_VAR* vVMT;
} BashClass;

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


extern SHELL_VAR* assertClassExists(char* className, int* pErr);
extern void BashObj_makeRef(BashObj* pObj);
extern void BashObj_makeVMT(BashObj* pObj);
extern int BashObj_init(BashObj* pObj, char* name, char* refClass, char* hierarchyLevel);
extern BashObj* BashObj_copy(BashObj* that);
extern BashObj* BashObj_find(char* name, char* refClass, char* hierarchyLevel);
extern SHELL_VAR* varNewHeapVar(char* attributes);
extern BashObj* BashObj_makeNewObject(char* _CLASS, SHELL_VAR* vObjVar, ...);
extern BashObj* ConstructObject(WORD_LIST* args);
extern void BashObj_dump(BashObj* pObj);
extern char* BashObj_getMemberValue(BashObj* pObj, char* memberName);
extern int BashObj_setMemberValue(BashObj* pObj, char* memberName, char* value);
extern void BashObj_setClass(BashObj* pObj, char* newClassName);
extern char* BashObj_getMethod(BashObj* pObj, char* methodName);
extern int BashObj_gotoMemberObj(BashObj* pObj, char* memberName, int allowOnDemandObjCreation, int* pErr);
extern int BashObj_setupMethodCallContext(BashObj* pObj);
extern int BashClass_init(BashClass* pCls, char* className);
extern BashClass* BashClass_find(char* name);
extern int BashClass_isVMTDirty(BashClass* pCls, char* currentCacheNumStr);
extern void DeclareClassEnd(char* className);
extern BUCKET_CONTENTS* ObjMemberItr_next(ObjMemberItr* pI);
extern BUCKET_CONTENTS* ObjMemberItr_init(ObjMemberItr* pI, BashObj* pObj, ObjVarType type);
extern int _classUpdateVMT(char* className, int forceFlag);
extern int _bgclassCall(WORD_LIST* list);

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// BGObjectStack

typedef struct _BGObjectStack {
  struct _BGObjectStack* next;
  BashObj* pObj;
} BGObjectStack;

extern BGObjectStack* BGObjectStack_unshift(BGObjectStack* list, BashObj* pObj);
extern BashObj* BGObjectStack_shift(BGObjectStack** pList);
extern void BGObjectStack_free(BGObjectStack** pList);


#endif // _bg_objects_H_
