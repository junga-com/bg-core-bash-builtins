
#include "bg_misc.h"

#include <errno.h>
#include <regex.h>

#include "bg_bashAPI.h"


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Misc Functions

int isNumber(char* string) {
    if (string == 0 || *string=='\0')
      return 0;

    errno = 0;
    char *ep;
    strtoimax (string, &ep, 10);
    if (errno || ep == string)
      return 0;	/* errno is set on overflow or underflow */

    // if there are characters left over, its not a valid number
    if (*ep != '\0')
        return 0;

    return (1);
}

char* savestringn(char* x, int n)
{
    char *p=(char *)strncpy (xmalloc (1 + n), (x),n);
    p[n]='\0';
    return p;
}

char* save2string(char* s1, char* s2)
{
    char* p = xmalloc(strlen(s1) + strlen(s2 +1));
    strcpy(p, s1);
    strcat(p, s2);
    return p;
}


char* bgMakeAnchoredRegEx(char* expr)
{
    char* s = xmalloc(strlen(expr)+3);
    *s = '\0';
    if (*expr != '^')
        strcat(s, "^");
    strcat(s, expr);
    if (expr[strlen(expr)-1] != '$')
        strcat(s, "$");
    return s;
}



// reads one \n terminated line from <file> into <buf> and returns the number of bytes moved into <buf>
// the terminating \n is not included in the <buf> string. The <buf> will always be null terminated.
// The return value will be equal to strlen(buf).
// <buf> should be allocated with a malloc or equivalent function and pBufAllocSize contain the size of the
// allocation. If <buf> is not large enough to hold the entire line, xremalloc will be used to increase its
// size and pBufAllocSize will be updated to reflect the new allocation size.
size_t freadline(FILE* file, char* buf, size_t* pBufAllocSize)
{
    buf[0]='\0';
    char* readResult = fgets(buf, *pBufAllocSize,file);
    size_t readLen = strlen(buf);
    while (readResult && (buf[readLen-1] != '\n')) {
        *pBufAllocSize *= 2;
        buf = xrealloc(buf, *pBufAllocSize);

        readResult = fgets(buf+readLen, *pBufAllocSize/2,file);
        readLen = strlen(buf+readLen)+readLen;
    }
    if (buf[readLen-1] == '\n') {
        buf[readLen-1] = '\0';
        readLen--;
    }
    return readLen;
}

// returns true(1) if <value> matches <filter> or if <filter> is null
// returns false(0) if <filter> is specified and <value> does not match it.
// Params:
//    <filter> : NULL, "", or "^$" means any <value> matches without testing the <value>.
//               otherwise <filter> is a regex that applies to <value>.
//               <filter> is automatically anchored meaning that it will have a leading '^' and trailing '$' added to it if it does
//               not already have them.
//    <value>  : the value to match against <filter>
int matchFilter(char* filter, char* value)
{
    if (!filter || !*filter || strcmp(filter,"^$")==0)
        return 1;

    regex_t regex;
    if (regcomp(&regex, filter, REG_EXTENDED)) {
        fprintf(stderr, "error: invalid regex filter (%s)\n", filter);
        return 1;
    }
    return regexec(&regex, value, 0, NULL, 0) == 0;
}
