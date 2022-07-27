
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

extern char* resaveWithQuotes(char* s1, int reallocFlag);

extern char*   bgMakeAnchoredRegEx(char* expr);
extern ssize_t freadline(FILE* file, char** pBuf, size_t* pBufAllocSize);
extern int     matchFilter(char* filter, char* value);
extern void    hexDump(char *desc, void *addr, int len);

// 'sa' stands for allocated string. caller should xfree() the returned string when finished with it.
extern char* saprintf(char* fmt, ...);

#define bgstrcmp(s1,s2)        ((!s1)?-1: (!s2)?1: strcmp(s1,s2))
#define bgstrncmp(s1,s2,len)   ((!s1)?-1: (!s2)?1: strncmp(s1,s2,len))
#define bgstrlen(s)            ((!s)?0:strlen(s))

#define bgstr(s)              ( (s) ?s :"" )

extern char* bgstrpbrk(char* s, char* delims);

// file stuff
extern int   fsExists(const char* file);

#define cp_removeSrc 0x01
#define cp_mkdir     0x02

// flags:
//     cp_mkdir      if the destination folder tree does not exist, create all the parents as needed
//     cp_removeSrc  after succesfull cp, remove the src file
extern void  fsCopy(const char* src, const char* dst, int flags);

extern char* mktempC(char* template);

#endif /* _bg_misc_H_ */
