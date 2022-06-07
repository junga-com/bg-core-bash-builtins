
#if !defined (_bg_objects_H_)
#define _bg_objects_H_


#include "bg_bashAPI.h"

extern void onUnload_objects();


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

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// BashObjRef

// OBSOLETE? the BashObj struct has refClass and superCallFlag. Maybe embrace the Ref vars as one way to init the BashObj
typedef struct {
  char oid[255];
  char className[255];
  int superCallFlag;
} BashObjRef;

extern char* extractOID(char* objRef);

extern int BashObjRef_init(BashObjRef* pRef, char* objRefStr);


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// BashObj

typedef struct _BashObj {
  char name[255]; // maybe get rid of this in favor of vThis->name
  char ref[300];
  SHELL_VAR* vThis;
  SHELL_VAR* vThisSys;
  SHELL_VAR* vCLASS;
  SHELL_VAR* vVMT;

  // objRef state
  char* refClass;
  int superCallFlag;

  // internal state
  HASH_TABLE* namerefMembers; // members for which we created a nameref in the method context (used to incrementally add during ctors)
} BashObj;

// This describes what set of member attributes we are interested in.
//     tj_real : real attributes are the logical members of the object that the programmer puts in the class/object
//     tj_sys  : sys attributes are the attributes managed and required by the object system. A developer can add more sys attributes.
typedef enum {tj_all,tj_sys,tj_real} ToJSONMode;


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
extern int BashObj_initFromContext(BashObj* pObj);
extern void BashObj_initFromObjRef(BashObj* pObj, char* objRef);
extern void BashObj_setupMethodCallContextDone(BashObj* this);
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

typedef enum {sm_thisAndFriends, sm_membersOnly, sm_wholeShebang, sm_noThanks} BashObjectSetupMode;
extern int BashObj_setupMethodCallContext(BashObj* pObj, BashObjectSetupMode mode, char* _METHOD);

extern int BashClass_init(BashClass* pCls, char* className);
extern BashClass* BashClass_find(char* name);
extern int BashClass_isVMTDirty(BashClass* pCls, char* currentCacheNumStr);
extern void DeclareClassEnd(char* className);
extern BUCKET_CONTENTS* ObjMemberItr_next(ObjMemberItr* pI);
extern BUCKET_CONTENTS* ObjMemberItr_init(ObjMemberItr* pI, BashObj* pObj, ObjVarType type);
extern int _classUpdateVMT(char* className, int forceFlag);
extern int _bgclassCall(WORD_LIST* list);
#define BashObj_isNull(pObj) (!pObj || strcmp(ShellVar_get(pObj->vThis),"assertThisRefError")==0)



// Object Methods implemented in C
extern WORD_LIST* Object_getIndexes(BashObj* pObj, ToJSONMode mode);

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
