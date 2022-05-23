
#if !defined (_BGString_H_)
#define _BGString_H_


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

extern void BGString_init(BGString* pStr, int allocatedLen);
extern void BGString_initFromStr(BGString* pStr, char* s);
extern void BGString_initFromAllocatedStr(BGString* pStr, char* s);
extern void BGString_free(BGString* pStr);
extern void BGString_appendn(BGString* pStr, char* s, int sLen, char* separator);
extern void BGString_append(BGString* pStr, char* s, char* separator);
extern void BGString_copy(BGString* pStr, char* s);
extern void BGString_replaceWhitespaceWithNulls(BGString* pStr);
extern char* BGString_nextWord(BGString* pStr);

#endif // _BGString_H_
