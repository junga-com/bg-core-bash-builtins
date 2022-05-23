
#if !defined (_bg_json_H_)
#define _bg_json_H_

#include "bg_objects.h"

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

extern JSONToken* JSONScanner_getValue(JSONScanner* this);

extern char* jsonEscape(char* s);
extern void jsonUnescape(char* s);

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// JSONType

#define JSONType_isBashObj(jt) (jt==jt_object || jt==jt_array)
extern int JSONType_isAValue(JSONType jt);
extern char* JSONTypeToString(JSONType jt);

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// JSONToken

#define JSONToken_isDone(this) ((this->type == jt_eof) || (this->type == jt_error))
#define JSONToken_free(t) do { if (t) { xfree(t->value); xfree(t); }; } while(0)

extern void JSONToken_print(FILE* fd, JSONToken* this, char* label);
extern char* JSONToken_ToString(JSONToken* this);
extern JSONToken* JSONToken_make(JSONType type, char* value);
extern JSONToken* JSONToken_maken(JSONType type, char* value, int len);
extern JSONToken* JSONToken_makeObject(BashObj* pObj);
extern JSONToken* JSONToken_makeArray(BashObj* pObj);
extern JSONToken* JSONToken_makeError(JSONScanner* scanner, JSONToken* token, char* fmt, ...);
extern JSONToken* JSONToken_copy(JSONToken* that);

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// JSONScanner
// struct defined above

extern JSONScanner* JSONScanner_newFromFile(char* inFile);
extern JSONScanner* JSONScanner_newFromStream(int fdJSON);
extern JSONToken* JSONScanner_getToken(JSONScanner* this);
extern JSONToken* JSONScanner_getObject(JSONScanner* this, BashObj* pObj);
extern JSONToken* JSONScanner_getValue(JSONScanner* this);


#endif // _bg_json_H_
