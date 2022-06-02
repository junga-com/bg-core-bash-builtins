
#if !defined (_bg_misc_H_)
#define _bg_misc_H_

#include <stdio.h>


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Misc Functions

extern int isNumber(char* string);
extern char* savestringn(char* x, int n);
extern char* save2string(char* s1, char* s2);
extern char* bgMakeAnchoredRegEx(char* expr);
extern size_t freadline(FILE* file, char* buf, size_t* pBufAllocSize);
extern int matchFilter(char* filter, char* value);
extern void hexDump(char *desc, void *addr, int len);
extern int fsExists(const char* file);


#endif /* _bg_misc_H_ */
