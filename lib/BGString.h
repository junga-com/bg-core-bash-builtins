
#if !defined (_BGString_H_)
#define _BGString_H_

#include <stdarg.h>
#include <stdio.h>

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// BGString
// auto growing string buffer that it null terminated but also can contain nulls in the string to make a string list
// that can be iterated. A typical senario is getting a whitespace separated string from a SHELL_VAR, turning whitespace
// to nulls and then iterating the words.
typedef struct {
	char*  buf;
	int    len;
	size_t allocatedLen;
	char*  itr;
	int    indentLevel;
	char*  lineEnd;
	char*  sep;
} BGString;

extern void BGString_init(BGString* pStr, int allocatedLen);
extern void BGString_initFromStr(BGString* pStr, char* s);
extern void BGString_initFromAllocatedStr(BGString* pStr, char* s);
extern void BGString_free(BGString* pStr);

extern int  BGString_readln(BGString* pStr, FILE* fd);
extern int  BGString_writeln(BGString* pStr, FILE* fd);

extern void BGString_appendf( BGString* pStr, char* separator, char* fmt, ...);
extern void BGString_appendfv(BGString* pStr, char* separator, char* fmt, va_list args);
extern void BGString_appendn( BGString* pStr, char* separator, char* s, int sLen);
extern void BGString_append(  BGString* pStr, char* separator, char* s);
extern void BGString_copy(BGString* pStr, char* s);
extern void BGString_replaceWhitespaceWithNulls(BGString* pStr);
extern void BGString_replaceChar(BGString* pStr, char toReplace, char withThis);
extern char* BGString_nextWord(BGString* pStr);

#endif // _BGString_H_
