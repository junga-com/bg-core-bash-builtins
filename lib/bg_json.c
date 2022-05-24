
#include "bg_json.h"

#include <sys/stat.h>


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
    static char buf[100];
    snprintf(buf,100, "%s(%s)", JSONTypeToString(this->type), JSONType_isBashObj(this->type)?"<objOrArray>":this->value);
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
