
#include "bg_objects.h"

#include <regex.h>
#include <execute_cmd.h>

#include "BGString.h"
#include "bg_import.h"


void assertObjExpressionError()
{
	assertError(NULL,"");
}

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
}

ObjExprOperators ObjExprOpFromString(char* s)
{
	if (strcmp(""          ,s)==0)   return eo_defaultOp;
	if (strcmp(".unset"    ,s)==0)   return eo_unset;
	if (strcmp(".exists"   ,s)==0)   return eo_exists;
	if (strcmp(".isA"      ,s)==0)   return eo_isA;
	if (strcmp(".getType"  ,s)==0)   return eo_getType;
	if (strcmp(".getOID"   ,s)==0)   return eo_getOID;
	if (strcmp(".getRef"   ,s)==0)   return eo_getRef;
	if (strcmp(".toString" ,s)==0)   return eo_toString;
	if (strcmp("=new"      ,s)==0)   return eo_eqNew;
	if (strcmp("="         ,s)==0)   return eo_equal;
	if (strcmp("+="        ,s)==0)   return eo_plusEqual;
	if (strcmp("::"        ,s)==0)   return eo_dblColon;
	assertError(NULL, "unknown object expression operator '%s'",s);
	return eo_defaultOp; // never reached
}



char* extractOID(char* objRef)
{
	if (!objRef || strlen(objRef)<=13)
		return NULL;
	char* oid=objRef+12;
	while (*oid && whitespace(*oid)) oid++;
	char* oidEnd=oid;
	while (*oidEnd && !whitespace(*oidEnd)) oidEnd++;
	return savestringn(oid, (oidEnd-oid));
}

// '_bgclassCall <oid> <className> <hierarchyLevel> | '
int BashObjRef_init(BashObjRef* pRef, char* objRefStr)
{
	// skip over the '_bgclassCall' token
	char* ps=objRefStr;
	if (strncmp(ps,"_bgclassCall",12)!=0)
		return 0; // assertError(NULL,"BashObjRef_init: malformed <objRef> does not start with '_bgclassCall'\n\tobjRef='%s'\n", objRefStr);
	ps+=12;
	while (ps && *ps && whitespace(*ps)) ps++;

	// ps should be pointing at the start of the <oid> now
	char* pe=ps;
	while (pe && *pe && !whitespace(*pe)) pe++;
	if ((pe-ps) > sizeof(pRef->oid)-1)
		return 0; // assertError(NULL,"BashObjRef_init: oid name is too large (>%d)\n\tobjRef='%s'\n", sizeof(pRef->oid)-1, objRefStr);
	strncpy(pRef->oid, ps, pe-ps); pRef->oid[pe-ps]='\0';
	while (pe && *pe && whitespace(*pe)) pe++;

	// ps should be pointing at the start of <className> now
	ps=pe;
	while (pe && *pe && !whitespace(*pe)) pe++;
	if ((pe-ps) > sizeof(pRef->className)-1)
		return 0; // assertError(NULL,"BashObjRef_init: className is too large (>%d)\n\tobjRef='%s'\n", sizeof(pRef->className)-1, objRefStr);
	strncpy(pRef->className, ps, pe-ps); pRef->className[pe-ps]='\0';
	while (pe && *pe && whitespace(*pe)) pe++;

	// ps should be pointing at the start of <hierarchyLevel> now
	ps=pe;
	pRef->superCallFlag=strtol(ps,&pe,10);
	while (pe && *pe && whitespace(*pe)) pe++;

	// should be pointing at the | character now.
	ps=pe;
	if (*ps != '|')
		return 0; // assertError(NULL,"BashObjRef_init: malformed <objRef> missing the '|' character at '%s'.\n\tobjRef='%s'\n", ps, objRefStr);
	return 1;
}


SHELL_VAR* assertClassExists(char* className, int* pErr)
{
	if (!className) {
		if (pErr) *pErr=1;
		assertError(NULL, "className is empty\n");
		return NULL;
	}

	SHELL_VAR* vClass = ShellVar_find(className);

	int trig = 0;
	if (!vClass) {
		trig = 1;
		bgtrace1(0,"STR assertClassExists '%s'\n",className); bgtracePush();
		char* classLibName = save2string(className, ".sh");
		int ret = importBashLibrary(classLibName, 0, NULL);
		bgtrace1(0,"import exitcode='%d'\n",ret); if (ret);
		xfree(classLibName);

		vClass = ShellVar_find(className);
		if (!vClass) {
			if (pErr) *pErr=1;
			assertError(NULL, "Class still does not exist nor does a library with its name exists.\n\tclassName='%s'\n", className);
			bgtracePop(); bgtrace1(0,"END assertClassExists '%s'\n",className);
			return NULL;
		}
	}
	if (!assoc_p(vClass) && !array_p(vClass)) {
		if (pErr) *pErr=1;
		assertError(NULL, "Error - <refClass> (%s) is not an array\n", className);
		if (trig)
			{bgtracePop(); bgtrace1(0,"END assertClassExists '%s'\n",className);}
		return NULL;
	}
	if (pErr) *pErr=0;
	if (trig)
		{bgtracePop(); bgtrace1(0,"END assertClassExists '%s'\n",className);}
	return vClass;
}


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// BashObj

void BashObj_makeRef(BashObj* pObj)
{
	// '_bgclassCall <oid> <class> 0 | '
	//  12  + 8 + 1 + strlen(<oid>) + strlen(<class>)
	strcpy(pObj->ref,"_bgclassCall ");
	strcat(pObj->ref,pObj->vThis->name);
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
	if (strlen(name) >200) return assertError(NULL,"BashObj_init: name is too long (>200)\nname='%s'\n", name);
	int errFlag=0;

	pObj->refClass=refClass;
	pObj->superCallFlag = (hierarchyLevel && strcmp(hierarchyLevel, "0")!=0);

	pObj->vThis=ShellVar_find(name);
	if (!(pObj->vThis))
		return assertError(NULL,"BashObj_init: <oid>(%s) does not exist \n", name);
	if (!assoc_p(pObj->vThis) && !array_p(pObj->vThis)) {
		char* objRefString = ShellVar_get(pObj->vThis);
		BashObjRef oRef;
		if (!BashObjRef_init(&oRef, objRefString))
			return assertError(NULL,"Error - <oid> (%s) is not an array nor does it contain an <objRef> string\n", name);
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
		return assertError(NULL,"Error - malformed object instance. missing the _CLASS system variable. instance='%s'\n", pObj->vThisSys->name);
	pObj->vCLASS = assertClassExists(_CLASS, &errFlag); if (errFlag!=0) return EXECUTION_FAILURE;

	// lookup the VMT array taking into account that an obj instance may have a separate VMT unique to it (_this[_VMT]) and if this
	// is a $super.<method>... call, it is the VMT of the baseClass of the calling <objRef>'s refClass.
	BashObj_makeVMT(pObj);
	if (!pObj->vVMT)
		return assertError(NULL,"Error - _VMT (virtual method table) is missing.\n\tobj='%s'\n\tclass='%s'\n\trefClass='%s'\n\tsuperCallFlag='%d'\n", name, _CLASS, pObj->refClass,pObj->superCallFlag);

	// we used pObj->name as a buffer to try lookups for several different names but now lets put the actual name in it
	strcpy(pObj->name, pObj->vThis->name);

	pObj->namerefMembers = NULL;

	BashObj_makeRef(pObj);
	return 1;
}

// this is handy for the start of object methods implemented in C
// its sort of like the opposite of BashObj_setupMethodCallContext
int BashObj_initFromContext(BashObj* pObj)
{
	pObj->vThis = ShellVar_find("this");
	pObj->vThisSys = ShellVar_find("_this");
	pObj->vCLASS = ShellVar_find("class");
	pObj->vVMT = ShellVar_find("_VMT");
	pObj->refClass = ShellVar_getS("_CLASS");
	pObj->superCallFlag = atol(ShellVar_getS("_hierarchLevel"));
	strncpy(pObj->ref , ShellVar_assocGet(pObj->vThisSys, "_Ref"), sizeof(pObj->ref));
	strncpy(pObj->name, pObj->vThis->name                        , sizeof(pObj->name));
	pObj->namerefMembers = NULL;
	return EXECUTION_SUCCESS;
}

void BashObj_initFromObjRef(BashObj* pObj, char* objRefStr)
{
	// skip over the '_bgclassCall' token
	char* ps=objRefStr;
	if (!ps || strncmp(ps,"_bgclassCall",12)!=0)
		assertError(NULL,"BashObj_initFromObjRef: malformed <objRef> does not start with '_bgclassCall'\n\tobjRef='%s'\n", objRefStr);
	ps+=12;
	while (*ps && whitespace(*ps)) ps++;

	// ps should be pointing at the start of the <oid> now
	char* pe=ps;
	while (*pe && !whitespace(*pe)) pe++;
	char* oid = savestringn(ps, pe-ps);
	while (pe && *pe && whitespace(*pe)) pe++;

	// ps should be pointing at the start of <className> now
	ps=pe;
	while (pe && *pe && !whitespace(*pe)) pe++;
	char* refClass = savestringn(ps, pe-ps);
	while (pe && *pe && whitespace(*pe)) pe++;

	// ps should be pointing at the start of <hierarchyLevel> now
	ps=pe;
	int superCallFlag=strtol(ps,&pe,10);
	while (pe && *pe && whitespace(*pe)) pe++;

	// should be pointing at the | character now.
	ps=pe;
	if (*ps != '|')
		assertError(NULL,"BashObj_initFromObjRef: malformed <objRef> missing the '|' character at '%s'.\n\tobjRef='%s'\n", ps, objRefStr);

	BashObj_init(pObj, oid, refClass, (superCallFlag)?"1":"0");

	xfree(oid);
	xfree(refClass);
}

void BashObj_setupMethodCallContextDone(BashObj* this)
{
//__bgtrace("!!! %p : FREEING namerefMembers in BashObj_setupMethodCallContextDone\n", this->namerefMembers);
	xfree(this->namerefMembers);
	this->namerefMembers = NULL;
}


BashObj* BashObj_copy(BashObj* that)
{
	BashObj* this = xmalloc(sizeof(BashObj));
	strcpy(this->name, that->name );
	strcpy(this->ref , that->ref  );
	this->vThis   = that->vThis     ;
	this->vThisSys= that->vThisSys  ;
	this->vCLASS  = that->vCLASS    ;
	this->vVMT    = that->vVMT      ;

	this->refClass = that->refClass;
	this->superCallFlag = that->superCallFlag;
	this->namerefMembers = NULL;
	return this;
}

BashObj* BashObj_find(char* name, char* refClass, char* hierarchyLevel)
{
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
SHELL_VAR* varNewHeapVar(char* attributes)
{
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
		retVal = ShellVar_arrayCreateGlobal(buf);
	else
		retVal = ShellVar_assocCreateGlobal(buf);

	xfree(buf);
	return retVal;
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
	WordList_free(args);
	if (vTmpObjVar)
		ShellVar_unset(vTmpObjVar);
	bgtracePop();
	bgtrace1(1, "END BashObj_makeNewObject '%s'\n",_CLASS);
	return pObj;
}

typedef struct _mchunk {
	size_t prevSize;
	size_t size;
} mchunk;


BashObj* ConstructObject(WORD_LIST* args)
{
	char* label; if (!label);
	bgtrace1(1,"START ConstructObject(%s)\n", label  = WordList_toString(args));
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
	bgtrace1(1,"dynData='%s'\n",dynData);

	DeclareClassEnd(_CLASS);

	SHELL_VAR* vClass = assertClassExists(_CLASS, NULL);
	if (!vClass) {
		assertError(NULL,"Class ('%s') does not exist", _CLASS);
		bgtracePop(); bgtrace1(1,"END ConstructObject(%s) \n",label);
		return NULL;
	}

	// ### support dynamic base class implemented construction
	char* tstr = save2string(_CLASS, "::ConstructObject");
	SHELL_VAR* fDynCtor = find_function(tstr);
	if (fDynCtor) {
		bgtrace1(1,"dyn ctor exists ...'%s'\n",tstr);
		char* objVar = args->word->word;
		WORD_LIST* dArgs = WordList_copy(args);
		dArgs = WordList_unshift(dArgs, (dynData)?dynData:"");
		// if the class ctor returns true, we are done, otherwise it wants us to continue constructing the object
		// 34 means it threw an exception (and its being caught) so we should end right away
		int result = ShellFunc_execute(fDynCtor, dArgs);
		WordList_free(dArgs);
		if (result == 0) {
			xfree(dynData);
			xfree(tstr); tstr = NULL;
			bgtracePop(); bgtrace1(1,"END ConstructObject(%s) \n", label);
			return BashObj_find(objVar, NULL,NULL);
		} else if (result == 34) {
			xfree(dynData);
			xfree(tstr); tstr = NULL;
			bgtracePop(); bgtrace1(1,"END ConstructObject(%s) \n", label);
			return NULL;
		}
	}
	xfree(dynData);
	xfree(tstr); tstr=NULL;

	// get some information from the class about how we construct an instance
	char* oidAttributes = ShellVar_assocGet(vClass, "oidAttributes");
	int isNumericArray = (oidAttributes && strstr(oidAttributes, "a")) ? 1 : 0;

	// ### deal with the return variable
	if (!args) {
		assertError(NULL,"<objVar> is a required argument to ConstructObject");
		bgtracePop(); bgtrace1(1, "END ConstructObject(%s) \n",label);
		return NULL;
	}
	char* _objRefVar = args->word->word;
	args = args->next;

	SHELL_VAR* vObjVar = ShellVar_find(_objRefVar);

	BashObj* newObj = xmalloc(sizeof(BashObj));
	*newObj->name='\0';
	*newObj->ref='\0';
	newObj->vCLASS = vClass;
	newObj->refClass=NULL;
	newObj->superCallFlag=0;
	newObj->namerefMembers = NULL;

	// this is the case of 'local obj=$(NewObject ...)' where obj gets set to an objRef to a heap_ array that does not exist in its
	// process space but can be restored from the tmp file that NewObject creates
	// The first time _bgclassCall is invoked with that objRef, it will call us with the OID of the ref to create it
	if (!vObjVar && !valid_array_reference(_objRefVar,VA_NOEXPAND)  && strncasecmp(_objRefVar, "heap_a", 6)==0) {
		bgtrace1(1,"objVar:(%s) non-existent heap var\n", _objRefVar);
		strcpy(newObj->name, _objRefVar);
		if (isNumericArray)
			newObj->vThis = ShellVar_arrayCreateGlobal(_objRefVar);
		else
			newObj->vThis = ShellVar_assocCreateGlobal(_objRefVar);
		char* tstr = save2string(newObj->vThis->name,"_sys");
		newObj->vThisSys = ShellVar_assocCreateGlobal(tstr);
		xfree(tstr); tstr = NULL;
		BashObj_makeRef(newObj);
//        ConstructObjectFromJson
		bgtracePop();
		bgtrace1(1,"END ConstructObject(%s) \n",label);
		return NULL;

	// its an unitialized -n nameRef
	} else if (vObjVar && nameref_p(vObjVar) && invisible_p(vObjVar)) {
		bgtrace1(1,"objVar:(%s) its an unitialized -n nameRef\n", _objRefVar);
		newObj->vThis = varNewHeapVar((oidAttributes)?oidAttributes:"A");
		ShellVar_set(vObjVar, newObj->vThis->name);

		tstr = save2string(newObj->vThis->name,"_sys");
		newObj->vThisSys = ShellVar_assocCreateGlobal(tstr);
		xfree(tstr);  tstr = NULL;
		BashObj_makeRef(newObj);

	// its an 'A' (associative) array that we can use as our object and the _this array
	// we dont create a separate <oid>_sys array because we cant create it in the same scope as <oid>_sys in the bash implementation
	} else if (vObjVar && assoc_p(vObjVar)) {
		bgtrace1(1,"objVar:(%s) its an 'A' (associative) array\n", _objRefVar);
		newObj->vThis = vObjVar;
		newObj->vThisSys = newObj->vThis;
		BashObj_makeRef(newObj);

	// its an 'a' (numeric) array that we can use as our object
	// Note that this case is problematic and maybe should assert an error because there is no way to create the _sys array in the
	// same scope that the caller created the OID array. We create a "${_OID}_sys" global array but that polutes the global namespace
	// and can collide with other object instances.
	} else if (vObjVar && assoc_p(vObjVar)) {
		bgtrace1(1,"objVar:(%s) its an 'a' (numeric) array\n", _objRefVar);
		newObj->vThis = vObjVar;

		tstr = save2string(newObj->vThis->name,"_sys");
		newObj->vThisSys = ShellVar_assocCreateGlobal(tstr);
		xfree(tstr);  tstr = NULL;
		BashObj_makeRef(newObj);

	// its a plain string variable or an array reference or does not exist
	} else {
		bgtrace1(1,"objVar:(%s) its a plain string variable or an array reference or does not exist\n", _objRefVar);
		newObj->vThis = varNewHeapVar((oidAttributes)?oidAttributes:"A");
		BashObj_makeRef(newObj);

		tstr = save2string(newObj->vThis->name,"_sys");
		newObj->vThisSys = ShellVar_assocCreateGlobal(tstr);
		xfree(tstr);  tstr = NULL;

		// if _objRefVar is an array reference (v[idx]) vObjVar will not be found but we can still set it like a normal var
		if (valid_array_reference(_objRefVar,VA_NOEXPAND)) {
			ShellVar_setS(_objRefVar, newObj->ref);
		} else if (vObjVar) {
			ShellVar_set(vObjVar, newObj->ref);
		}
		else {
			// ShellVar_createSet did not work (./unitTests/bg_objects.sh.ut run expressionErrors:)
			ShellVar_createGlobalSet(_objRefVar, newObj->ref);
			//ShellVar_createSet(_objRefVar, newObj->ref);
		}
	}

	strcpy(newObj->name, newObj->vThis->name);

	ShellVar_assocSet(newObj->vThisSys, "_OID"  , newObj->vThis->name);
	ShellVar_assocSet(newObj->vThisSys, "_CLASS", newObj->vCLASS->name);
	ShellVar_assocSet(newObj->vThisSys, "_Ref"  , newObj->ref);
	ShellVar_assocSet(newObj->vThisSys, "0"     , newObj->ref);

	BashObj_makeVMT(newObj);

	char* defIdxSetting = ShellVar_assocGet(newObj->vCLASS, "defaultIndex");
	if (!defIdxSetting || strcmp(defIdxSetting,"off")!=0)
		ShellVar_assocSet(newObj->vThis, "0", newObj->ref);

	_classUpdateVMT(_CLASS, 0);

	int needsPopping = 0;
	if (ShellAtGlobalScope()) {
		needsPopping = 1;
		ShellPushFunctionScope("ConstructObject");
		bgtrace2(2, "!!!### pushing var ctx for 'ConstructObject %s %s'\n",_CLASS, _objRefVar);
	}

	int hasCtorThrownException = 0;

	BashObj_setupMethodCallContext(newObj, sm_wholeShebang, NULL);

	// concept borrowed from jscript. "newTarget" is the final class being constructed. static and _CLASS will be the specific class
	// of the ctor being executed but newTarget will always be the final class that the instance will be in the end.
	ShellVar_refCreateSet("newTarget",_CLASS);
	ShellVar_refCreateSet("prototype", "primeThePump"); // so that unset has a local to unset

	// call each class ctor in the hierarchy in order from Object to _CLASS
	char* classHierarchy = ShellVar_assocGet(newObj->vCLASS, "classHierarchy");
	bgtrace1(1,"classHierarchy='%s'\n", classHierarchy);
	WORD_LIST* hierarchyList = list_string(classHierarchy, " \t\n",0);
	for (WORD_LIST* _cname=hierarchyList; !hasCtorThrownException && _cname; _cname=_cname->next ) {

		ShellVar_refUnsetS("static");
		ShellVar_refCreateSet("static", _cname->word->word);
		SHELL_VAR* vProto = ShellVar_findWithSuffix(_cname->word->word, "_prototype");
		if (vProto) {
			ShellVar_refUnsetS("prototype");
			ShellVar_refCreateSet("prototype", vProto->name);
			ShellVar_assocCopyElements(assoc_cell(newObj->vThis), assoc_cell(vProto));
		}
		SHELL_VAR* vCtor = ShellFunc_findWithSuffix(_cname->word->word, "::__construct");
		if (vCtor) {
			bgtrace1(1,"CTOR calling  '%s::__construct' FOUND\n", _cname->word->word);
			int result;
			if ((result=ShellFunc_execute(vCtor, args)) != 0) {
				hasCtorThrownException = 1;
				bgtrace1(1,"CTOR returned false exit code = '%d'\n", result);
			}
			// this will incrementally make new namerefs for any members added by the ctor
			BashObj_setupMethodCallContext(newObj, sm_membersOnly, NULL);
		} else {
			bgtrace1(1,"CTOR skipping '%s::__construct' does not exist\n", _cname->word->word);
		}
	}
	ShellVar_refUnsetS("prototype");


	// call each class post ctor in the hierarchy in order from Object to _CLASS
	for (WORD_LIST* _cname=hierarchyList; !hasCtorThrownException && _cname; _cname=_cname->next ) {
		SHELL_VAR* vPostCtor = ShellFunc_findWithSuffix(_cname->word->word, "::postConstruct");
		if (vPostCtor) {
			bgtrace1(1,"PTOR calling  '%s::postConstruct' FOUND\n", _cname->word->word);
			if (ShellFunc_execute(vPostCtor, args) != 0)
				hasCtorThrownException = 1;
			// this will incrementally make new namerefs for any members added by the ctor
			BashObj_setupMethodCallContext(newObj, sm_membersOnly, NULL);
		} else {
			bgtrace1(1,"PTOR skipping '%s::postConstruct' does not exist\n", _cname->word->word);
		}
	}

	if (needsPopping) {
		ShellPopFunctionScope();
		bgtrace2(2,"!!!### popping var ctx for 'ConstructObject %s %s'\n\n",_CLASS, _objRefVar);
	}

//__bgtrace("!!! %p : FREEING namerefMembers in ConstructObject\n", newObj->namerefMembers);
	BashObj_setupMethodCallContextDone(newObj);
	WordList_free(hierarchyList);

	bgtracePop();
	bgtrace1(1,"END ConstructObject(%s) \n",label);
	return newObj;
}

void BashObj_dump(BashObj* pObj)
{
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

char* BashObj_getMemberValue(BashObj* pObj, char* memberName)
{
	if (*memberName == '_') {
		return ShellVar_assocGet(pObj->vThisSys, memberName);
	}else if (array_p(pObj->vThis)) {
		if (!isNumber(memberName)) {
			//assertError(NULL,"memberName is not a valid numeric array index\n\tmemberName='%s'\n", memberName);
			return NULL;
		}
		return ShellVar_arrayGetS(pObj->vThis, memberName);
	}else {
		return ShellVar_assocGet(pObj->vThis, memberName);
	}
}

int BashObj_setMemberValue(BashObj* pObj, char* memberName, char* value)
{
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

void BashObj_unsetMember(BashObj* pObj, char* memberName)
{
	if (!memberName)
		return;
	if (*memberName == '_')
		ShellVar_assocUnsetEl(pObj->vThisSys, memberName);
	else if (assoc_p(pObj->vThis))
		ShellVar_assocUnsetEl(pObj->vThis   , memberName);
	else if (array_p(pObj->vThis))
		ShellVar_arrayUnsetS( pObj->vThis   , memberName);
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
	if (!defIdxSetting || strcmp(defIdxSetting,"off")!=0)
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


char* BashObj_getMethod(BashObj* pObj, char* methodName)
{
	if (strncmp(methodName,"_method::",9)==0 || strncmp(methodName,"_static::",9)==0)
		return ShellVar_assocGet(pObj->vVMT, methodName);
	else {
		if (strlen(methodName)>200) {
			assertError(NULL,"BashObj_getMethod: methodName is too long (>200). \n\tmethodName='%s'\n", methodName);
			return NULL;
		}
		char buf[255]; strcpy(buf, "_method::"); strcat(buf,methodName);
		return ShellVar_assocGet(pObj->vVMT, buf);
	}
}


int BashObj_gotoMemberObj(BashObj* pObj, char* memberName, int allowOnDemandObjCreation, int* pErr)
{
	if (pErr) *pErr=EXECUTION_SUCCESS;
	char* memberVal=BashObj_getMemberValue(pObj, memberName);
	if (!memberVal) {
		if (!allowOnDemandObjCreation) {
			assertError(NULL,"MemberName does not exist and allowOnDemandObjCreation is false\n\tmemberName='%s'\n", memberName);
			if (pErr) *pErr=EXECUTION_FAILURE;
			return 0;
		}
		if (array_p(pObj->vThis) && isNumber(memberName)) {
			assertError(NULL,"MemberName is not a valid index for a Numeric array\n\tmemberName='%s'\n", memberName);
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
		assertError(NULL,"A primitive member can not be dereferenced with MemberName\n\tmemberName='%s'\n", memberName);
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
int BashObj_setupMethodCallContext(BashObj* pObj, BashObjectSetupMode mode, char* _METHOD)
{
	if (mode == sm_thisAndFriends || mode == sm_wholeShebang) {
		ShellVar_refCreateSet( "this"           , pObj->vThis->name             );
		ShellVar_refCreateSet( "_this"          , pObj->vThisSys->name          );
		ShellVar_refCreateSet( "static"         , pObj->vCLASS->name            );
		ShellVar_refCreateSet( "refClass"       , (pObj->refClass) ? pObj->refClass : pObj->vCLASS->name );
		ShellVar_refCreateSet( "class"          , pObj->vCLASS->name            );
		ShellVar_refCreateSet( "_VMT"           , pObj->vVMT->name              );

		ShellVar_createSet(    "_OID"           , pObj->vThis->name             );
		ShellVar_createSet(    "_OID_sys"       , pObj->vThisSys->name          );
		ShellVar_createSet(    "_CLASS"         , pObj->refClass                );
		ShellVar_createSet(    "_hierarchLevel" , (pObj->superCallFlag)?"1":"0" );
	}
	if (mode == sm_membersOnly || mode == sm_wholeShebang) {
		if (assoc_p(pObj->vThis)) {
			if (! pObj->namerefMembers) {
				pObj->namerefMembers = hash_create(0);
//__bgtrace("!!! %p : making namerefMembers\n", pObj->namerefMembers);
			}
			ObjMemberItr i;
			for (BUCKET_CONTENTS* item=ObjMemberItr_init(&i,pObj, ovt_both); item; item=ObjMemberItr_next(&i)) {
				if (strcmp(item->key,"0")!=0 && strcmp(item->key,"_Ref")!=0 && item->data ) {
					if (!assoc_reference(pObj->namerefMembers, item->key)) {
						if (strncmp(item->data, "_bgclassCall",12)==0) {
							bgtrace1(3,"!!! item->key='%s'\n", item->key);
							char* memOid = extractOID(item->data);
							ShellVar_refCreateSet(item->key, memOid);
							xfree(memOid);
							assoc_insert(pObj->namerefMembers, item->key, "");
						} else if (strncmp(item->data, "heap_",5)==0) {
							ShellVar_refCreateSet(item->key, item->data);
							assoc_insert(pObj->namerefMembers, item->key, "");
						}
					}
				}
			}
		}
	}

	if (_METHOD) {
		// make the $super.<method> <objRef>
		// '_bgclassCall <pObj> <classFrom_METHOD> 1 |'
		// _METHOD=<className>::<method>
		BGString superObjRef;  BGString_init(&superObjRef, 50);
		BGString_append(&superObjRef, "_bgclassCall ",NULL);
		BGString_append(&superObjRef, pObj->vThis->name,NULL);
		char* mClass=_METHOD;
		char* mClassEnd=mClass; while (*mClassEnd && (*mClassEnd!=':' || *(mClassEnd+1)!=':') ) mClassEnd++;
		BGString_appendn(&superObjRef, mClass, (mClassEnd-mClass)," ");
		BGString_append(&superObjRef, " 1 | ",NULL);

		ShellVar_createSet("super", superObjRef.buf);
		BGString_free(&superObjRef);
	}
	return EXECUTION_SUCCESS;
}


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// BashClass

int BashClass_init(BashClass* pCls, char* className)
{
	pCls->vClass = ShellVar_find(className);
	if (!pCls->vClass)
		return assertError(NULL,"bultin _classUpdateVMT: global class array does not exist for class '%s'\n", className);
	if (!assoc_p(pCls->vClass))
		return assertError(NULL,"bultin _classUpdateVMT: global class array not an -A array for class '%s'\n", className);
	if (invisible_p(pCls->vClass)) {
		VUNSETATTR (pCls->vClass, att_invisible);
	}

	pCls->vVMT = ShellVar_findWithSuffix(className, "_vmt");
	if (!pCls->vVMT)
		return assertError(NULL,"bultin _classUpdateVMT: global class vmt array does not exist for class '%s_vmt'\n", className);
	if (!assoc_p(pCls->vVMT))
		return assertError(NULL,"bultin _classUpdateVMT: global class vmt array is not an -A array for class '%s'\n", className);
	if (invisible_p(pCls->vVMT)) {
		VUNSETATTR (pCls->vVMT, att_invisible);
	}
	return EXECUTION_SUCCESS;
}

BashClass* BashClass_find(char* name)
{
	BashClass* pClass = xmalloc(sizeof(BashClass));
	if (!BashClass_init(pClass, name)) {
		xfree(pClass);
		return NULL;
	}
	return pClass;
}


int BashClass_isVMTDirty(BashClass* pCls, char* currentCacheNumStr)
{
	char* baseClassNameVmtCacheNum = ShellVar_assocGet(pCls->vClass, "vmtCacheNum");
	return (!baseClassNameVmtCacheNum || strcmp(baseClassNameVmtCacheNum, currentCacheNumStr)!=0);
}

void DeclareClassEnd(char* className)
{
	SHELL_VAR* vDelayedData = ShellVar_findWithSuffix(className, "_initData");
	if (!vDelayedData)
		return;
	bgtrace1(1,"DOing DeclareClassEnd(%s)\n", className); bgtracePush();

	// note that its most efficient to build WORD_LIST backward. ShellVar_arrayToWordList(),p3,p2,p1
	// copy the init data and remove vDelayedData so that a recursive call wont re-enter this section
	WORD_LIST* constructionArgs = ShellVar_arrayToWordList(vDelayedData);
	ShellVar_arrayUnset(vDelayedData);

	// we need the baseClass arg as well as passing it to the ctor
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
		if (s-pendingClassCtors==1 && whitespace(*(s-1))) s--;
		memmove(s, e, strlen(e)+1);
	}

	// ensure that our base class is realized because we will need it
	if (baseClass) {
		assertClassExists(baseClass, NULL);
		DeclareClassEnd(baseClass);
	}

	// ConstructObject Class "$className" "$className" "$baseClass" "${initData[@]}"
	BashObj* pObj = ConstructObject(constructionArgs);
	xfree(pObj);

	char* tstr = save2string("static::",className);
	SHELL_VAR* vClassDynCtor = ShellFunc_findWithSuffix(tstr, "::__construct");
	xfree(tstr);
	if (vClassDynCtor) {
		bgtrace1(1,"STOR calling  '%s' \n", vClassDynCtor->name);
		ShellVar_refCreateSet("newClass",className);
		BashObj* pNewClass = BashObj_find(className, NULL,0);
		BashObj_setupMethodCallContext(pNewClass, sm_wholeShebang, NULL);
//__bgtrace("!!! %p : FREEING namerefMembers in DeclareClassEnd\n", pNewClass->namerefMembers);
		BashObj_setupMethodCallContextDone(pNewClass);
		xfree(pNewClass);
		ShellVar_refUnsetS("static");
		ShellVar_refCreateSet("static", className);
		ShellFunc_execute(vClassDynCtor, constructionArgs->next->next);
	} else {
		bgtrace1(1,"STOR skipping 'static::%s::__construct' (not found) \n",className);
	}

	WordList_free(constructionArgs);
	bgtracePop(); bgtrace1(1,"END DeclareClassEnd(%s)\n", className);
}


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ObjMemberItr

BUCKET_CONTENTS* ObjMemberItr_next(ObjMemberItr* pI)
{
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

BUCKET_CONTENTS* ObjMemberItr_init(ObjMemberItr* pI, BashObj* pObj, ObjVarType type)
{
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

	// # forceFlag will reset the vmtCacheNum of this entire class hierarchy so that the algorithm will rescan them all during this run
	if (forceFlag) {
		BGString classHierarchy; BGString_initFromStr(&classHierarchy, ShellVar_assocGet(class.vClass, "classHierarchy") );
		BGString_replaceWhitespaceWithNulls(&classHierarchy);
		char* oneClass;
		while ( (oneClass=BGString_nextWord(&classHierarchy)) ) {
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
		char* prefix1=xmalloc(classNameLen+3);  strcpy(prefix1, className);  strcat(prefix1, "::");                             int prefix1Len=strlen(prefix1);
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
				xfree(methodBase);
			}
			if (strncmp(funcList[i]->name, prefix2,prefix2Len)==0) {
				count++;
				char* staticBase=xmalloc(strlen(funcList[i]->name)+7+1);
				strcpy(staticBase, "_static");
				strcat(staticBase, funcList[i]->name+prefix2Len-2);
				ShellVar_assocSet(class.vVMT, staticBase, funcList[i]->name);
				BGString_append(&staticMethodsStrList, funcList[i]->name, "\n");
				xfree(staticBase);
			}
		}
		ShellVar_assocSet(class.vClass, "methods"      , methodsStrList.buf      );
		ShellVar_assocSet(class.vClass, "staticMethods", staticMethodsStrList.buf);
		xfree(funcList);
		BGString_free(&methodsStrList);
		BGString_free(&staticMethodsStrList);
		xfree(prefix1);
		xfree(prefix2);
	}

	xfree(currentCacheNumStr);
	return EXECUTION_SUCCESS;
}


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// CMD: IsAnObjRef

// this returns the C notion of false(0) and true(1)
int IsAnObjRef(WORD_LIST* args)
{
	if (!args) return 0;
	if (strcmp("_bgclassCall",args->word->word)==0) {
		args = args->next;
		if (!args) return 0;
		//char* oid = args->word->word;

		args = args->next;
		if (!args) return 0;
		//char* class = args->word->word;

		args = args->next;
		if (!args) return 0;
		char* superFlag = args->word->word;
		if ((*superFlag != '0') && (*superFlag != '1')) return 0;

		args = args->next;
		if (!args) return 0;
		if (*args->word->word == '|') return 1;

		return 0;
	}

	return IsAnObjRefS(args->word->word);
}

// this returns the C notion of false(0) and true(1)
int IsAnObjRefS(char* str)
{
	if (strncmp("_bgclassCall",str,12)!=0)
		return 0;
	char* s = str + 12;
	if (!whitespace(*s))
		return 0;
	while (*s && whitespace(*s)) s++;

	// pointing at <oid>
	while (*s && !whitespace(*s)) s++;
	while (*s && whitespace(*s)) s++;

	// pointing at <className>
	while (*s && !whitespace(*s)) s++;
	while (*s && whitespace(*s)) s++;

	// pointing at <superFlag>
	if (*s!='0' && *s!='1')
		return 0;
	while (*s && !whitespace(*s)) s++;
	while (*s && whitespace(*s)) s++;

	// pointing at |
	if (*s!='|')
		return 0;

	return 1;
}


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// CMD: _bgclassCall

regex_t regex;

int _bgclassCall(WORD_LIST* list)
{
	bgtrace1(1,"starting _bgclassCall\n", WordList_toString(list));
//    begin_unwind_frame ("bgCore");

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
			return assertError(NULL,"Syntax error. the 4th argument must start with the pipe ('|') character. See usage..\n\n");
		}
	}
	list = list->next;

	// if the objSyntaxStart was not in the 4th position, it is the 5th but its alright if it does not exist
	if (!objSyntaxStart && list && list->word) {
		objSyntaxStart=list->word->word;
		list = list->next;
	}

#   if bgtraceLevel >= 2
	bgtrace0(2,"01234567890123456789\n");
	bgtrace1(2,"%s\n",objSyntaxStart);
#   endif


	// These variables are static so that we only compile the pattern once
	//                   ((                            opWBr                                         )(  Br         )|( opNBr  ))(firstArg)?$
	static char* pattern="((\\.unset|\\.exists|\\.isA|\\.getType|\\.getOID|\\.getRef|\\.toString|=new|)([[:space:]]|$)|([+]?=|::))(.*)?$";
	static int regexInit;
	if (!regexInit) {
		if (regcomp(&regex, pattern, REG_EXTENDED)) // |REG_ICASE
			return assertError(NULL,"error: invalid regex pattern (%s)\n", pattern);
	}

	// [[ "$objSyntaxStart" =~ $reExp ]] || assertError(NULL, "invalid object expression"
	regmatch_t *matches = malloc(sizeof(regmatch_t) * (regex.re_nsub + 1));
	if (regexec(&regex, objSyntaxStart, regex.re_nsub + 1, matches, 0))
		return assertError(NULL,"error: invalid object expression (%s) did not match regex='%s'\n", objSyntaxStart, pattern);

	// matches[0] is the whole match (op to EOL) ((opWBr)(Br)|(opNBr))(firstArg)?$
	// matches[1] is ((opWBr)(Br)|(opNBr))
	// matches[2] is (opWBr)
	// matches[3] is (Br)
	// matches[4] is (opNBr)
	// matches[5] is (firstArg)
#   if bgtraceLevel >= 3
	for (int i=0; i<=regex.re_nsub; i++) {
		bgtrace3(3,"  matches[%d]:  %d to %d\n", i, matches[i].rm_so, matches[i].rm_eo);
	}
#   endif


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
	char* tstr = savestringn(_chainedObjOrMember, matches[0].rm_so);
	ShellVar_createSet("_chainedObjOrMember", tstr);
	xfree(tstr);
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
		bgtrace0(2,"");
		for (int i=0; i<objSyntaxStartLen; i++)
			__bgtrace("%c",(objSyntaxStart[i]=='\0')?'_':objSyntaxStart[i]);
		__bgtrace("\n");
#   endif

	// put the expression args into args[] and also 'set -- "${args[@]}"'
	// some operators dont require a space between the operator and the first arg so if match[5] is not empty, create
	// a new list head with it
	SHELL_VAR* vArgs = make_local_array_variable("_argsV",0);
	WORD_LIST* methodArgs = WordList_copy(list);
	int argsNonEmpty=0;
	if (matches[5].rm_so<matches[5].rm_eo) {
		argsNonEmpty=1;
		methodArgs = WordList_unshift(methodArgs, savestringn(objSyntaxStart+matches[5].rm_so,(matches[5].rm_eo-matches[5].rm_so)) );
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
	xfree(matches);

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
				return assertError(NULL,"error: '%s' is not a member object of '%s' but it is being dereferenced as if it is. dereference='%s'\n", pCurPart, objInstance.name, pNextPart);
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

	// setup the quick method environment needed for generic operators
	BashObj_setupMethodCallContext(&objInstance, sm_thisAndFriends, NULL);

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
		assign_compound_array_list(vArgs, list, 0);

		_rsvMemberType=mt_method;
		_rsvMemberTypeStr = MemberTypeToString(_rsvMemberType, NULL, _rsvMemberValue);
		xfree(_memberOp); _memberOp=savestring("");
	}

	ObjExprOperators memberOp = ObjExprOpFromString(_memberOp);


	// local variables used in the switch cases...
	int        exitCode = 0;
	char*      value    = NULL;
	WORD_LIST* args     = NULL;
	char*      _METHOD  = NULL;
	SHELL_VAR* vMethod  = NULL;
	char*      _CLASS   = NULL;
	char*      retVal   = "";
	BashObjRef objRef;
	BGRetVar   retVar;

	bgtrace2(1, "_bgclassCall op:type='%s:%s'\n", (_memberOp) ? _memberOp : "defaultOp", _rsvMemberTypeStr);


	switch (CA(memberOp,_rsvMemberType)) {

		//########################################################################################################################
		// eo_defaultOp

		case CA(eo_defaultOp,mt_nullVar) :
			exitCode = 1;
		break;
		case CA(eo_defaultOp,mt_nullMethod) :
			assertError(NULL, "method not found '%s'", _rsvMemberName);
		break;
		case CA(eo_defaultOp,mt_nullEither) :
			assertError(NULL, "niether method nor member variable found in this object '%s'",_rsvMemberName);
		break;

		case CA(eo_defaultOp,mt_primitive) :
			value = ShellVar_assocGet(objInstance.vThis, _rsvMemberName);
			BGRetVar_initFromVarname(&retVar, (methodArgs) ? methodArgs->word->word : "");
			outputValue(&retVar, value);
		break;

		case CA(eo_defaultOp,mt_object) :
			args = WordList_unshift(methodArgs, "|.toString");
			args = WordList_unshift(args, "0");
			args = WordList_unshift(args, objInstance.vCLASS->name);
			args = WordList_unshift(args, objInstance.vThis->name);
			_bgclassCall(args);
			WordList_freeUpTo(&args, methodArgs);
		break;

		case CA(eo_defaultOp,mt_method) :
			_classUpdateVMT(objInstance.vCLASS->name ,0);

			if (virtOverride) {
				// $obj.<class>::<method> ...
				// in the virtual override syntax, the complete <class>::<method> name is in the syntax so we dont look it up in the VMT
				// local _METHOD="$_rsvMemberName"
				_METHOD=_rsvMemberName;
				vMethod = ShellFunc_find(_METHOD);
				if (!vMethod)
					assertError(NULL, "overriden method does not exist '%s'", _METHOD);
			} else {
				// normal method call
				_METHOD = BashObj_getMethod(&objInstance, _rsvMemberName);
				if (!_METHOD && !objInstance.superCallFlag) {
					assertError(NULL, "method (%s) not found in class ('%s')", _rsvMemberName, objInstance.vCLASS->name);
				}
				vMethod = ShellFunc_find(_METHOD);
				if (!vMethod)
					assertError(NULL, "this method was found in the VMT (%s) but does not exist. This must be a logic error in _bgclassCall. '%s'", objInstance.vVMT->name, _METHOD);
			}
			ShellVar_createSet("_METHOD", (_METHOD)?_METHOD:"");

			// setup the rest of the method environment now that we know we will be invoking a shell method.
			BashObj_setupMethodCallContext(&objInstance, sm_membersOnly, _METHOD);

			SHELL_VAR* vOnEnterFunc = ShellFunc_find("objOnEnterMethod");
			if (vOnEnterFunc) {
				ShellFunc_execute(vOnEnterFunc, methodArgs);
			}
			exitCode = ShellFunc_execute(vMethod, methodArgs);
		break;


		//########################################################################################################################
		// eo_dblColon (::)

		// $obj::<method> p1 p2
		// static call where obj can be a Class object or an instance.
		case CA(eo_dblColon,mt_self) :
			if (strcmp("Class",objInstance.vCLASS->name)==0 )
				_CLASS=ShellVar_assocGet(objInstance.vThis, "name");
			else
				_CLASS=objInstance.vCLASS->name;

			SHELL_VAR* vStatic = ShellVar_find(_CLASS);
			if (!vStatic)
				assertError(NULL, "Class '$s' not found while executing static method call");
			if (!assoc_p(vStatic))
				assertError(NULL, "Bad Class '$s' is not a global associative array while executing static method call");

			_classUpdateVMT(_CLASS,0);

			// this is a static call so unset the this pointer vars. Our state array is in 'static', not 'this'
			ShellVar_refUnsetS("this");
			ShellVar_refUnsetS("_this");
			ShellVar_unsetS("_OID");
			ShellVar_unsetS("_OID_sys");

			ShellVar_refUnsetS("static");
			ShellVar_refCreateSet("static", _CLASS);

			ShellVar_refUnsetS("_VMT");
			char* vmtName = save2string(_CLASS, "_vmt");
			ShellVar_refCreateSet("_VMT", vmtName);

			if (!methodArgs)
				assertError(NULL, "object syntax error. $obj::<methodname> is missing <methodname>");

			char* _METHOD_key = save2string("_static::",methodArgs->word->word);
			_METHOD = ShellVar_assocGetS(vmtName, _METHOD_key);
			if (!_METHOD)
				assertError(NULL, "'%s' is not a member function of the Class '%s'", _METHOD_key, _CLASS);

			vMethod = ShellFunc_find(_METHOD);
			if (!vMethod)
				assertError(NULL, "The method '%s', found in VMT '%s[%s]' does not exist.", _METHOD, vmtName, _METHOD_key);


			AssocItr i;
			for (BUCKET_CONTENTS* item=AssocItr_first(&i,assoc_cell(vStatic)); item; item=AssocItr_next(&i)) {
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

			exitCode = ShellFunc_execute(vMethod, methodArgs);

			xfree(_METHOD_key);
			xfree(vmtName);
		break;


		case CA(eo_dblColon,mt_unknown) :
		case CA(eo_dblColon,mt_nullVar) :
		case CA(eo_dblColon,mt_nullMethod) :
		case CA(eo_dblColon,mt_nullEither) :
		case CA(eo_dblColon,mt_object) :
		case CA(eo_dblColon,mt_primitive) :
		case CA(eo_dblColon,mt_method) :
		case CA(eo_dblColon,mt_both) :
		case CA(eo_dblColon,mt_invalidExpression) :
			assertError(NULL, "Invalid use of the :: operator in object expression.");
		break;


		//########################################################################################################################
		// eo_unset (.unset)

		case CA(eo_unset,mt_self) :
			// unset on the base object is a noop -- maybe it should assert?
			exitCode = 0;
		break;

		case CA(eo_unset,mt_primitive) :
			BashObj_unsetMember(&objInstance, _rsvMemberName);
			exitCode = 0;
		break;

		case CA(eo_unset,mt_nullVar) :
		case CA(eo_unset,mt_nullMethod) :
		case CA(eo_unset,mt_nullEither) :
		case CA(eo_unset,mt_method) :
			exitCode = 0;
		break;

		case CA(eo_unset,mt_object) :
			args = WordList_unshift(NULL, _rsvMemberValue);
			args = WordList_unshift(args, "DeleteObject");
			ShellFunc_executeS(args);
			WordList_free(args);
			BashObj_unsetMember(&objInstance, _rsvMemberName);
			exitCode = 0;
		break;


		//########################################################################################################################
		// eo_exists (.exists)

		case CA(eo_exists,0x00) ... CA(eo_exists,0x0F) :
			switch (_rsvMemberType) {
				case mt_object :
				case mt_primitive :
				case mt_method :
				case mt_both :
				case mt_self :
					exitCode = 0;
					break;
				default:
					exitCode = 1;
			}
		break;


		//########################################################################################################################
		// eo_isA (.isA)

		case CA(eo_isA,0x00) ... CA(eo_isA,0x0F) :
			if (!methodArgs)
				assertError(NULL, "<type> is a required argument to <obExpr>.isA <type>");
			char* classTblLookup;
			switch (_rsvMemberType) {
				case mt_self :
					classTblLookup = save3string(objInstance.vCLASS->name, ",", methodArgs->word->word);
					exitCode = (NULL!=ShellVar_assocGetS("_classIsAMap", classTblLookup)) ? 0 : 1;
					xfree(classTblLookup);
				break;
				case mt_object :
					BashObjRef_init(&objRef, _rsvMemberValue);
					char* classTblLookup = save3string(objRef.className, ",", methodArgs->word->word);
					exitCode = (NULL!=ShellVar_assocGetS("_classIsAMap", classTblLookup)) ? 0 : 1;
					xfree(classTblLookup);
				break;
				default:
					exitCode = (strcmp(methodArgs->word->word,_rsvMemberTypeStr)==0) ? 0 : 1;
			}
		break;


		//########################################################################################################################
		// eo_getType (.getType)

		case CA(eo_getType,0x00) ... CA(eo_getType,0x0F) :
			switch (_rsvMemberType) {
				case mt_self   : retVal = objInstance.vCLASS->name; break;
				case mt_object :
					BashObjRef_init(&objRef, _rsvMemberValue);
					retVal = objRef.className;
				break;
				default:
					tstr = strstr(_rsvMemberTypeStr, ":either");
					if (tstr)
						*tstr = '\0';
					retVal = _rsvMemberTypeStr;
					break;
			}
			BGRetVar_initFromVarname(&retVar, (methodArgs) ? methodArgs->word->word : "");
			outputValue(&retVar, retVal);
			exitCode = 0;
		break;


		//########################################################################################################################
		// eo_getOID (.getOID)

		case CA(eo_getOID,0x00) ... CA(eo_getOID,0x0F) :
			switch (_rsvMemberType) {
				case mt_object : BashObjRef_init(&objRef, _rsvMemberValue);
								 retVal = objRef.oid;
								 break;
				case mt_self   : retVal = objInstance.vThis->name; break;
				default:         retVal = NULL;
			}
			if (retVal) {
				BGRetVar_initFromVarname(&retVar, (methodArgs) ? methodArgs->word->word : "");
				outputValue(&retVar, retVal);
				exitCode = 0;
			} else
				exitCode = 1;
		break;


		//########################################################################################################################
		// eo_getRef (.getRef)

		case CA(eo_getRef,0x00) ... CA(eo_getRef,0x0F) :
			switch (_rsvMemberType) {
				case mt_self   : retVal = objInstance.ref; break;
				case mt_object : retVal = _rsvMemberValue; break;
				default:         retVal = NULL;
			}
			if (retVal) {
				BGRetVar_initFromVarname(&retVar, (methodArgs) ? methodArgs->word->word : "");
				outputValue(&retVar, retVal);
				exitCode = 0;
			} else
				exitCode = 1;
		break;


		//########################################################################################################################
		// eo_toString (.toString)

		case CA(eo_toString,mt_self) :
			args = WordList_unshift(methodArgs, "Object::toString");
			ShellFunc_executeS(args);
			WordList_freeUpTo(&args, methodArgs);
		break;

		case CA(eo_toString,mt_object) :
			BashObjRef_init(&objRef, _rsvMemberValue);
			args = WordList_unshift(methodArgs, "|.toString");
			args = WordList_unshift(args, "0");
			args = WordList_unshift(args, objRef.className);
			args = WordList_unshift(args, objRef.oid);
			_bgclassCall(args);
			WordList_freeUpTo(&args, methodArgs);
		break;

		case CA(eo_toString,mt_primitive) :
			args = WordList_unshift(methodArgs, _rsvMemberValue);
			args = WordList_unshift(args, "--value");
			args = WordList_unshift(args, _rsvMemberName);
			args = WordList_unshift(args, "--name");
			args = WordList_unshift(args, "Primitive::toString");
			ShellFunc_executeS(args);
			WordList_freeUpTo(&args, methodArgs);
		break;

		case CA(eo_toString,mt_nullVar) :
		case CA(eo_toString,mt_nullMethod) :
		case CA(eo_toString,mt_nullEither) :
			tstr = strstr(_rsvMemberTypeStr, ":either");
			if (tstr)
				*tstr = '\0';
			args = WordList_unshift(methodArgs, _rsvMemberTypeStr);
			args = WordList_unshift(args, "--value");
			args = WordList_unshift(args, _rsvMemberName);
			args = WordList_unshift(args, "--name");
			args = WordList_unshift(args, "Primitive::toString");
			ShellFunc_executeS(args);
			WordList_freeUpTo(&args, methodArgs);
		break;

		case CA(eo_toString,mt_method) :
			_classUpdateVMT(_CLASS,0);
			retVal = save3string("method<",_rsvMemberValue,"()>");
			args = WordList_unshift(methodArgs, retVal);
			args = WordList_unshift(args, "--value");
			args = WordList_unshift(args, _rsvMemberName);
			args = WordList_unshift(args, "--name");
			args = WordList_unshift(args, "Primitive::toString");
			ShellFunc_executeS(args);
			WordList_freeUpTo(&args, methodArgs);
			xfree(retVal);
		break;


		//########################################################################################################################
		// eo_eqNew (=new)

		case CA(eo_eqNew,0x00) ... CA(eo_eqNew,0x0F) :
			if (_rsvMemberType==mt_self)
				assertError(NULL, "direct object assignment (as opposed to member variable assignment) is not yet supported");
			char* newClassName = (methodArgs) ? WordList_shift(&methodArgs)  : savestring("Object");
			char* instanceName = save4string(objInstance.vThis->name,"[",_rsvMemberName,"]");
			methodArgs = WordList_unshift(methodArgs, instanceName);
			methodArgs = WordList_unshift(methodArgs, newClassName);
			ConstructObject(methodArgs);
			xfree(instanceName);
			xfree(newClassName);
		break;


		//########################################################################################################################
		// eo_plusEqual (+=)

		case CA(eo_plusEqual,0x00) ... CA(eo_plusEqual,0x0F) :
			if (_rsvMemberType==mt_self)
				assertError(NULL, "+= direct object assignment (as opposed to member variable assignment) is not yet supported");
			BGString buf; BGString_initFromStr(&buf, BashObj_getMemberValue(&objInstance, _rsvMemberName));
			retVal = WordList_toString(methodArgs);
			BGString_append(&buf, retVal, "");
			BashObj_setMemberValue(&objInstance, _rsvMemberName, buf.buf);
			xfree(retVal);
			BGString_free(&buf);
		break;


		//########################################################################################################################
		// eo_equal (=)

		case CA(eo_equal,0x00) ... CA(eo_equal,0x0F) :
			if (_rsvMemberType==mt_self)
				assertError(NULL, "+= direct object assignment (as opposed to member variable assignment) is not yet supported");
			retVal = WordList_toString(methodArgs);
			BashObj_setMemberValue(&objInstance, _rsvMemberName, retVal);
			xfree(retVal);
		break;
	}


	// cleanup before we leave
//__bgtrace("!!! %p : FREEING namerefMembers in _bgclassCall\n", objInstance.namerefMembers);
	BashObj_setupMethodCallContextDone(&objInstance);
	WordList_free(methodArgs);
	xfree(_rsvMemberTypeStr);
	xfree(_rsvMemberName);_rsvMemberName=NULL;
	xfree(_memberExpression);_memberExpression=NULL;
	xfree(_memberOp); _memberOp=NULL;
//    discard_unwind_frame ("bgCore");

	return (exitCode);
}



WORD_LIST* Object_getIndexes(BashObj* pObj, ToJSONMode mode)
{
	WORD_LIST* realIdx = NULL;
	WORD_LIST* sysIdx  = NULL;

	char* tstr = ShellVar_assocGet(pObj->vCLASS, "defaultIndex");
	int defIdxSetting = (!tstr || strcmp(tstr,"off")!=0);

	if (assoc_p(pObj->vThis)) {
		AssocItr itr; AssocItr_init(&itr, assoc_cell(pObj->vThis));
		BUCKET_CONTENTS* bVar;
		while ((bVar=AssocItr_next(&itr))) {
			if (defIdxSetting && bVar->key[0]=='0' && bVar->key[1]=='\0')
				continue;
			if (bVar->key[0]!='_') {
				if (mode!=tj_sys) // i.e. real or all
					realIdx = WordList_unshift(realIdx, bVar->key);
			} else {
				// i.e. sys or all
				if (mode!=tj_real && strcmp("0",bVar->key)!=0 && strcmp("_Ref",bVar->key)!=0)
					sysIdx = WordList_unshift(sysIdx, bVar->key);
			}
		}
	} else if (array_p(pObj->vThis) && mode!=tj_sys) {
		for (ARRAY_ELEMENT* el = ShellVar_arrayStart(pObj->vThis); el!=ShellVar_arrayEOL(pObj->vThis); el=el->next ) {
			char* strInd = saprintf("%d", el->ind);
			realIdx = WordList_unshift(realIdx, strInd);
			xfree(strInd);
		}
	}

	// TODO: consider if we should sort the lists

	if ((pObj->vThis!=pObj->vThisSys) && (mode!=tj_real)) {
		AssocItr itr; AssocItr_init(&itr, assoc_cell(pObj->vThisSys));
		BUCKET_CONTENTS* bVar;
		while ((bVar=AssocItr_next(&itr)))
			if (strcmp("0",bVar->key)!=0 && strcmp("_Ref",bVar->key)!=0)
				sysIdx = WordList_unshift(sysIdx, bVar->key);
	}

	realIdx = WordList_reverse(realIdx);
	sysIdx  = WordList_reverse(sysIdx);

	switch (mode) {
		case tj_real: return realIdx;
		case tj_sys:  return sysIdx;
		case tj_all:  return WordList_join(realIdx, sysIdx);
	}
	return NULL; // should never be reached -- needed to shutup compiler warning
}



void onUnload_objects()
{
	regfree(&regex);
}


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// BGObjectStack

BGObjectStack* BGObjectStack_unshift(BGObjectStack* list, BashObj* pObj)
{
	BGObjectStack* newHead=xmalloc(sizeof(BGObjectStack));
	newHead->next = list;
	newHead->pObj=pObj;
	return newHead;
}
BashObj* BGObjectStack_shift(BGObjectStack** pList)
{
	if (!pList || !*pList)
		return NULL;
	BashObj* pObj=(*pList)->pObj;
	BGObjectStack* toFree=*pList;
	*pList=((*pList)->next);
	xfree(toFree);
	return pObj;
}
void BGObjectStack_free(BGObjectStack** pList)
{
	while (pList && *pList) {
		BGObjectStack* toFree=*pList;
		*pList=((*pList)->next);
		xfree(toFree);
	}
}
