
#if !defined (_bg_misc_H_)
#define _bg_misc_H_

#include <stdio.h>


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Misc Functions

extern int isNumber(char* string);
extern char* savestringn(char* x, int n);
extern char* save2string(char* s1, char* s2);
extern char* save3string(char* s1, char* s2, char* s3);
extern char* save4string(char* s1, char* s2, char* s3, char* s4);
extern char* bgMakeAnchoredRegEx(char* expr);
extern size_t freadline(FILE* file, char* buf, size_t* pBufAllocSize);
extern int matchFilter(char* filter, char* value);
extern void hexDump(char *desc, void *addr, int len);
extern int fsExists(const char* file);

// 'sa' stands for allocated string. caller should xfree() the returned string when finished with it.
extern char* saprintf(char* fmt, ...);

#endif /* _bg_misc_H_ */
