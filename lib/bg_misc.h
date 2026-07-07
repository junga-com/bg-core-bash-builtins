
#if !defined (_bg_misc_H_)
#define _bg_misc_H_

#include <stdio.h>

// for safeSaveString
#include <stdarg.h>
#include <stddef.h>
#include <string.h>
void *xmalloc(size_t);

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Misc Functions

extern int isNumber(char* string);

// since its not a macro it wont evaluate x more than once
// x can be null
static inline char *safeSaveString(char *x) { x=x?x:""; return strcpy(xmalloc(strlen(x) + 1), x); }

extern char* savestringn(char* x, int n);
extern char* save2string(char* s1, char* s2);
extern char* save3string(char* s1, char* s2, char* s3);
extern char* save4string(char* s1, char* s2, char* s3, char* s4);

extern char* saveNstring(const char* s1, ...);

#define bg_savestring(...) saveNstring(__VA_ARGS__, NULL)

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

#define bgstr(s)              ( (s) ?(s) :"" )

// for fixed buffers only b/c sizeof(dst) must work 
#define bg_snprintf(dst, fmt, ...)                                      \
	do {                                                             \
		int _bg_n = snprintf((dst), sizeof(dst), (fmt), ##__VA_ARGS__); \
		if (_bg_n < 0 || (size_t)_bg_n >= sizeof(dst))               \
			assertError(NULL, "buffer overflow writing %s", #dst);   \
	} while (0)

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

// s is a writable null terminated string. Each call writes a null at the first whitespace position
// so that the s becomes shortenned and returns the the char* of the first non whitespace position after
// that position or the terminating null if there is no more text. If s points to null it return NULL
extern char* strConsumeNext(char* s);

#endif /* _bg_misc_H_ */
