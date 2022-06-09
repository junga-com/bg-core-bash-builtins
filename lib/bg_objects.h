
#if !defined (_bg_objects_H_)
#define _bg_objects_H_


#include "bg_bashAPI.h"

extern void onUnload_objects();


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// BashObjects MemberType

typedef enum {
	mt_unknown            = 0x01,  // we do not (yet) know
	mt_nullVar            = 0x02,  // last member of chain did not exist - syntax tells us a member variable is expected
	mt_nullMethod         = 0x03,  // last member of chain did not exist - syntax tells us a member function is expected
	mt_nullEither         = 0x04,  // last member of chain did not exist - syntax could be either a var or function
	mt_object             = 0x05,  // last member of chain is a member var that contains an <objRef>
	mt_primitive          = 0x06,  // last member of chain is a member var that does not contain an <objRef>
	mt_method             = 0x07,  // last member of chain is a member function
	mt_both               = 0x08,  // last member of chain is both a member variable and a member function
	mt_self               = 0x09,  // there was no chain. an object is being directly invoked
	mt_invalidExpression  = 0x0A   // could not parse completely because of a syntax error
} MemberType;

typedef enum {
	eo_defaultOp          = 0x10,
	eo_unset              = 0x20,
	eo_exists             = 0x30,
	eo_isA                = 0x40,
	eo_getType            = 0x50,
	eo_getOID             = 0x60,
	eo_getRef             = 0x70,
	eo_toString           = 0x80,
	eo_eqNew              = 0x90,
	eo_equal              = 0xA0,
	eo_plusEqual          = 0xB0,
	eo_dblColon           = 0xC0
} ObjExprOperators;

extern char* MemberTypeToString(MemberType mt, char* errorMsg, char* _rsvMemberValue);
extern ObjExprOperators ObjExprOpFromString(char* s);
#define CA(memberOp, memberType)    (memberOp+memberType)


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
extern SHELL_VAR* varNewHeapVar(char* attributes);

// construction on BashObj from existing instances
extern int      BashObj_init(           BashObj* pObj, char* name, char* refClass, char* hierarchyLevel);
extern int      BashObj_initFromContext(BashObj* pObj);
extern void     BashObj_initFromObjRef( BashObj* pObj, char* objRef);
extern BashObj* BashObj_copy(           BashObj* that);
extern BashObj* BashObj_find(           char* name, char* refClass, char* hierarchyLevel);

// making new instances
extern BashObj* ConstructObject(       WORD_LIST* args);
extern BashObj* BashObj_makeNewObject( char* _CLASS, SHELL_VAR* vObjVar, ...);
extern void     BashObj_setClass(      BashObj* pObj, char* newClassName);

extern void BashObj_setupMethodCallContextDone(BashObj* this);

extern char* BashObj_getMemberValue( BashObj* pObj, char* memberName);
extern int   BashObj_setMemberValue( BashObj* pObj, char* memberName, char* value);
extern char* BashObj_getMethod(      BashObj* pObj, char* methodName);
extern void  BashObj_unsetMember(    BashObj* pObj, char* memberName);

// reset pObj to point to its member obj
extern int   BashObj_gotoMemberObj(  BashObj* pObj, char* memberName, int allowOnDemandObjCreation, int* pErr);

extern void BashObj_dump(BashObj* pObj);

// internal -- these are used to set the transient ref and vmt members from the other object state
extern void BashObj_makeRef(BashObj* pObj);
extern void BashObj_makeVMT(BashObj* pObj);

typedef enum {sm_thisAndFriends, sm_membersOnly, sm_wholeShebang, sm_noThanks} BashObjectSetupMode;
extern int BashObj_setupMethodCallContext(BashObj* pObj, BashObjectSetupMode mode, char* _METHOD);

extern int BashClass_init(BashClass* pCls, char* className);
extern BashClass* BashClass_find(char* name);
extern int BashClass_isVMTDirty(BashClass* pCls, char* currentCacheNumStr);
extern void DeclareClassEnd(char* className);
extern BUCKET_CONTENTS* ObjMemberItr_next(ObjMemberItr* pI);
extern BUCKET_CONTENTS* ObjMemberItr_init(ObjMemberItr* pI, BashObj* pObj, ObjVarType type);
extern int _classUpdateVMT(char* className, int forceFlag);
extern int IsAnObjRef(WORD_LIST* args);
extern int IsAnObjRefS(char* str);
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
