
#include "bg_json.h"

#include <sys/stat.h>

#include "BGString.h"

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// JSON String escaping


// usage: jsonEscape <varname1> [...<varnameN>}
char* jsonEscape(char* s)
{
	char* sOut=xmalloc(strlen(s)*2+2);
	char* cOut=sOut;
	for (char* c=s; c && *c; c++) {
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
	*cOut++='\0';
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
// JSONType

int JSONType_isAValue(JSONType jt)
{
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


char* JSONTypeToString(JSONType jt)
{
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

void JSONToken_print(FILE* fd, JSONToken* this, char* label)
{
	fprintf(fd, "%stype='%s' value='%s'\n", label, JSONTypeToString(this->type), (JSONType_isBashObj(this->type))?  (((BashObj*)this->value)->name ) : this->value);
	fflush(fd);
}

char* JSONToken_ToString(JSONToken* this)
{
	static char buf[300];
	snprintf(buf,300, "%s(%s)", JSONTypeToString(this->type), JSONType_isBashObj(this->type)?"<objOrArray>":this->value);
	buf[300-1]='\0';
	return buf;
}

JSONToken* JSONToken_make(JSONType type, char* value)
{
	JSONToken* this = xmalloc(sizeof(JSONToken));
	this->type = type;
	this->value = savestring(value);
	return this;
}

JSONToken* JSONToken_maken(JSONType type, char* value, int len)
{
	JSONToken* this = xmalloc(sizeof(JSONToken));
	this->type = type;
	this->value = savestringn(value, len);
	return this;
}

JSONToken* JSONToken_makeObject(BashObj* pObj)
{
	JSONToken* this = xmalloc(sizeof(JSONToken));
	this->type = jt_object;
	this->value = (char*)pObj;
	return this;
}

JSONToken* JSONToken_makeArray(BashObj* pObj)
{
	JSONToken* this = xmalloc(sizeof(JSONToken));
	this->type = jt_array;
	this->value = (char*)pObj;
	return this;
}

JSONToken* JSONToken_makeError(JSONScanner* scanner, JSONToken* token, char* fmt, ...)
{
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
		snprintf(this->value, bytesLeft, "error: bgCore fromJSON: %s(%d:%d) ", scanner->filename, linePos,charPos);
		bytesLeft-=strlen(this->value);
	}

	if (token) {
		if (strlen(token->value) > 30)
			strcpy(token->value+25, " ... ");
		snprintf(temp, 500, "(error token:'%s'(%s)) ", JSONTypeToString(token->type), token->value);
		strncat(this->value, temp, bytesLeft);
		bytesLeft-=strlen(temp);
		JSONToken_free(token);
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

JSONToken* JSONToken_copy(JSONToken* that)
{
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
JSONScanner* JSONScanner_newFromFile(char* inFile)
{
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
	while ( (bytesRead = read(fdJSON, this->buf, STREAMCHUNK)>0) ) {
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

JSONToken* JSONScanner_getToken(JSONScanner* this)
{
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

static int depth = 0;

JSONToken* JSONScanner_getObject(JSONScanner* this, BashObj* pObj)
{
	depth++;
	JSONToken* token;
	while ((token = JSONScanner_getToken(this)) && !JSONToken_isDone(token) && token->type!=jt_objEnd) {
		// The first iteration should not have a comma but we will not call it an error it it does
		// also be tolerant to the caller not removing the jt_objStart token.
		if (token->type == jt_comma || token->type == jt_objStart) {
			JSONToken_free(token);
			token = JSONScanner_getToken(this);
		}

		// token should now be the name of an attribute.
		if (token->type != jt_string) {
			depth--;
			return JSONToken_makeError(this, token, "Expected a string");
		}
		JSONToken* name = token;
		bgtrace3(1,"%*s |name='%s'\n", depth*3,"",  JSONToken_ToString(token));

		// the : separator
		token = JSONScanner_getToken(this);
		if (token->type != jt_colon) {
			depth--;
			return JSONToken_makeError(this, token, "Expected a colon");
		}
		JSONToken_free(token);

		// the value of the attribute
		JSONToken* value = JSONScanner_getValue(this);
		if (!JSONType_isAValue(value->type)) {
			depth--;
			return JSONToken_makeError(this,value, "Unexpected token");
		}

		// check for some special bash object system variables for special processing
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
	if (token->type==jt_error)
		assertError(NULL, "Object_fromJSON parse error '%s'", token->value);
	depth--;
	return token;
}



JSONToken* JSONScanner_getValue(JSONScanner* this)
{
	depth++;
	JSONToken* jval;

	JSONToken* token = JSONScanner_getToken(this);
	bgtrace3(1,"%*sgetVal start token='%s'\n", depth*3,"",  JSONToken_ToString(token));
	if (token->type == jt_objStart) {
		JSONToken_free(token);
		BashObj* pObj = BashObj_makeNewObject("Object",NULL);
		jval = JSONToken_makeObject(pObj);
		JSONScanner_getObject(this, pObj);
		// while ((token = JSONScanner_getToken(this)) && !JSONToken_isDone(token) && token->type!=jt_objEnd) {
		//     // The first iteration should not have a comma but we will not call it an error it it does
		//     // also be tolerant to the caller not removing the jt_objStart token.
		//     if (token->type == jt_comma) {
		//         JSONToken_free(token);
		//         token = JSONScanner_getToken(this);
		//     }
		//
		//     // token should now be the name of an attribute.
		//     if (token->type != jt_string) {
		//         depth--;
		//         return JSONToken_makeError(this, token, "Expected a string");
		//     }
		//     JSONToken* name = token;
		//     bgtrace3(1,"%*s |name='%s'\n", depth*3,"",  JSONToken_ToString(token));
		//
		//     // the : separator
		//     token = JSONScanner_getToken(this);
		//     if (token->type != jt_colon) {
		//         depth--;
		//         return JSONToken_makeError(this, token, "Expected a colon");
		//     }
		//     JSONToken_free(token);
		//
		//     // the value of the attribute
		//     JSONToken* value = JSONScanner_getValue(this);
		//     if (!JSONType_isAValue(value->type)) {
		//         depth--;
		//         return JSONToken_makeError(this,value, "Unexpected token");
		//     }
		//
		//     // check for some special bash object system variables for special processing
		//     if (strcmp(name->value,"_OID")==0) {
		//         // // use _OID to update the objDictionary so that we can fixup relative objRefs
		//         // char* sessionOID = value;
		//         // objDictionary[sessionOID]=currentStack->pObj->name;
		//         // objDictionary[currentStack->pObj->name]=sessionOID;
		//
		//     } else if (strcmp(name->value,"_Ref")==0 || strcmp(name->value,"0")==0) {
		//         // ignore _Ref and "0" on restore
		//
		//     } else if (strcmp(name->value,"_CLASS")==0) {
		//         // oh right, you are supposed to be this kind of object
		//         BashObj_setClass(pObj, value->value);
		//
		//     } else {
		//         BashObj_setMemberValue(pObj, name->value, (JSONType_isBashObj(value->type)) ? (((BashObj*)value->value)->ref) : value->value );
		//     }
		//
		//     JSONToken_free(name);
		//     JSONToken_free(value);
		// }

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
// bgCore ConstructObjectFromJson <objVar>

int ConstructObjectFromJson(WORD_LIST* list)
{
//    begin_unwind_frame("bgCore");

	if (!list || list_length(list) < 2) {
		assertError(NULL,"Error - <objVar> and <restoreFile> are required arguments to ConstructObjectFromJson. See usage..\n\n");
		return (EX_USAGE);
	}

	char* objVar = list->word->word;
	list = list->next;
	SHELL_VAR* vObjVar = ShellVar_find(objVar);
	if (!vObjVar  && !valid_array_reference(objVar,VA_NOEXPAND))
		ShellVar_refCreate(objVar);
	if (vObjVar)
		VUNSETATTR(vObjVar, att_invisible);

	char* jsonFile = list->word->word;
	list = list->next;

	JSONScanner* scanner = JSONScanner_newFromFile(jsonFile);
	if (! scanner)
		return assertError(NULL,"could not open input file '%s' for reading\n", jsonFile);

	JSONToken* jValue = JSONScanner_getValue(scanner);
	if (jValue->type == jt_error) {
		fprintf(stderr, "%s\n", jValue->value);
		JSONScanner_free(scanner);
		JSONToken_free(jValue);
		return EXECUTION_FAILURE;

	} else if (!JSONType_isAValue(jValue->type)) {
		fprintf(stderr, "error: Expected a JSON value but got token='%s(%s)', \n", JSONTypeToString(jValue->type), jValue->value);
		JSONScanner_free(scanner);
		JSONToken_free(jValue);
		return EXECUTION_FAILURE;

	// we found an object
	} else if (JSONType_isBashObj(jValue->type) && !vObjVar) { // objVar is an array ref like v[sub]
		ShellVar_setS(objVar, ((BashObj*)jValue->value)->ref);
	} else if (JSONType_isBashObj(jValue->type) && nameref_p(vObjVar)) {
		ShellVar_set(vObjVar, ((BashObj*)jValue->value)->name);
	} else if (JSONType_isBashObj(jValue->type) && (array_p(vObjVar)) ) {
		ShellVar_arraySetI(vObjVar, 0, ((BashObj*)jValue->value)->ref);
	} else if (JSONType_isBashObj(jValue->type) && (assoc_p(vObjVar)) ) {
		ShellVar_assocSet(vObjVar, "0", ((BashObj*)jValue->value)->ref);
	} else if (JSONType_isBashObj(jValue->type) ) {
		ShellVar_set(vObjVar, ((BashObj*)jValue->value)->ref);

	// we found a primitive
	} else if (!vObjVar) { // objVar is an array ref like v[sub]
		ShellVar_setS(objVar, jValue->value);
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

//    discard_unwind_frame ("bgCore");
	JSONScanner_free(scanner);
	JSONToken_free(jValue);
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
	if (BashObj_isNull(pObj))
		return assertError(NULL,"Object::fromJSON() called on an invalid object. 'this' does not exist or is the null reference");

	JSONToken* endToken = JSONScanner_getObject(scanner, pObj);
	JSONScanner_free(scanner);
	JSONToken_free(endToken);
	return EXECUTION_SUCCESS;
}


int Object_toJSON(BashObj* this, ToJSONMode mode, int indentLevel)
{
	// this pattern allows objDictionary to be shared among this and any recursive call that it spawns
	// even though ShellVar_assocCreate will return an existing local var, it wont use one at a higher scope so we use ShellVar_find first
	SHELL_VAR* objDictionary = ShellVar_find("objDictionary");
	if (!objDictionary)
		objDictionary = ShellVar_assocCreate("objDictionary");


	// record that this object is being written to the json txt
	//objDictionary[${_this[_OID]}]="sessionOID_${#objDictionary[@]}"
	char* strCount = itos( ShellVar_assocSize(objDictionary) );
	char* sessionOID = save2string("sessionOID_", strCount);
	ShellVar_assocSet(objDictionary, this->vThis->name, sessionOID);
	xfree(strCount);

	char* tstr = ShellVar_assocGet(this->vCLASS, "defaultIndex");
	int defIdxSetting = (!tstr || strcmp(tstr,"off")!=0);

	char tOpen='{', tClose='}';
	//int labelsOn = 1;
	if (array_p(this->vThis)) {
		tOpen='['; tClose=']';
		//labelsOn = 0;
	}

	// start the object/array
	printf("%c", tOpen);
	indentLevel++;

	int attribCount = 0;
	if (assoc_p(this->vThis)) {
		AssocItr itr; AssocItr_init(&itr, assoc_cell(this->vThis));
		BUCKET_CONTENTS* bVar;
		while ((bVar=AssocItr_next(&itr))) {
			if (bVar->key[0]=='_' || (defIdxSetting && bVar->key[0]=='0' && bVar->key[1]=='\0'))
				continue;

			// print a newline to 'finish' the last attribute (or tOpen)
			// we do it this way so that we can write the ',' only if required
			printf("%s\n", ((attribCount++ == 0) ? "" : ","));

			// print the start of the line, up to the <value>
			printf("%*s", indentLevel*3, "");
			printf("\"%s\": ", bVar->key);

			char* value = (char*)bVar->data;
			if ( strncmp(value,"_bgclassCall",12)==0 ) {
				BashObj subObj; BashObj_initFromObjRef(&subObj,value);
				char* seenSessionID = ShellVar_assocGet(objDictionary, subObj.vThis->name);
				if (! seenSessionID)
					Object_toJSON(&subObj, mode, indentLevel);
				else {
					BGString newRef; BGString_init(&newRef, 100);
					BGString_copy(&newRef,"_bgclassCall");
					BGString_append(&newRef, seenSessionID, " ");
					BGString_append(&newRef, subObj.refClass, " ");
					BGString_append(&newRef, (subObj.superCallFlag)?"1":"0", " ");
					BGString_append(&newRef, " | ", "");
					printf("\"%s\"", newRef.buf);
					BGString_free(&newRef);
				}
			} else {
				char* escapedValue = jsonEscape(value);
				printf("\"%s\"", escapedValue);
				xfree(escapedValue);
			}
		}

		// now do the system attributes if called for
		AssocItr_init(&itr, assoc_cell(this->vThisSys));
		if (mode!=tj_real) while ((bVar=AssocItr_next(&itr))) {
			if (bVar->key[0]!='_' || strcmp(bVar->key,"_Ref")==0 )
				continue;

			// print a newline to 'finish' the last attribute (or tOpen)
			// we do it this way so that we can write the ',' only if required
			printf("%s\n", (attribCount++ == 0)?"":",");

			// print the start of the line, up to the <value>
			printf("%*s", indentLevel*3, "");
			printf("\"%s\": ", bVar->key);

			char* value = (char*)bVar->data;
			if ( strncmp(value,"_bgclassCall",12)==0 ) {
				BashObj subObj; BashObj_initFromObjRef(&subObj,value);
				char* seenSessionID = ShellVar_assocGet(objDictionary, subObj.vThis->name);
				if (! seenSessionID)
					Object_toJSON(&subObj, mode, indentLevel);
				else {
					BGString newRef; BGString_init(&newRef, 100);
					BGString_copy(&newRef,"_bgclassCall");
					BGString_append(&newRef, seenSessionID, " ");
					BGString_append(&newRef, subObj.refClass, " ");
					BGString_append(&newRef, (subObj.superCallFlag)?"1":"0", " ");
					BGString_append(&newRef, " | ", "");
					printf("\"%s\"", newRef.buf);
					BGString_free(&newRef);
				}
			} else if (strcmp(bVar->key,"_OID")==0) {
				printf("\"%s\"", sessionOID);
			} else {
				printf("\"%s\"", value);
			}
		}

	} else if (array_p(this->vThis)) {
		for (ARRAY_ELEMENT* el=ShellVar_arrayStart(this->vThis); el!=ShellVar_arrayEOL(this->vThis); el=el->next) {

			// print a newline to 'finish' the last attribute (or tOpen)
			// we do it this way so that we can write the ',' only if required
			printf("%s\n", (attribCount++ == 0)?"":",");

			// print the start of the line, up to the <value>
			printf("%*s", indentLevel*3, "");

			if ( strncmp(el->value,"_bgclassCall",12)==0 ) {
				BashObj subObj; BashObj_initFromObjRef(&subObj,el->value);
				char* seenSessionID = ShellVar_assocGet(objDictionary, subObj.vThis->name);
				if (! seenSessionID)
					Object_toJSON(&subObj, mode, indentLevel);
				else {
					BGString newRef; BGString_init(&newRef, 100);
					BGString_copy(&newRef,"_bgclassCall");
					BGString_append(&newRef, seenSessionID, " ");
					BGString_append(&newRef, subObj.refClass, " ");
					BGString_append(&newRef, (subObj.superCallFlag)?"1":"0", " ");
					BGString_append(&newRef, " | ", "");
					printf("\"%s\"", newRef.buf);
					BGString_free(&newRef);
				}
			} else {
				printf("\"%s\"", el->value);
			}
		}
	}

	indentLevel--;
	if (attribCount>0)
		printf("\n%*s", indentLevel*3,"");
	printf("%c", tClose);

	xfree(sessionOID);
	return EXECUTION_SUCCESS;
}
