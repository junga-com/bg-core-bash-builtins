
#include "BGString.h"

#include <config.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>

// we should be able to get rid of this include if we try
#include "../loadables.h"

void BGString_init(BGString* pStr, int allocatedLen)
{
    pStr->len=0;
    pStr->allocatedLen=allocatedLen;
    pStr->buf=xmalloc(pStr->allocatedLen);
    pStr->buf[pStr->len]='\0';
    pStr->itr=NULL;
}
void BGString_initFromStr(BGString* pStr, char* s)
{
    pStr->len=strlen(s);
    pStr->allocatedLen=pStr->len+1;
    pStr->buf=xmalloc(pStr->allocatedLen);
    strcpy(pStr->buf, s);
    pStr->itr=NULL;
}
void BGString_initFromAllocatedStr(BGString* pStr, char* s)
{
    pStr->len=strlen(s);
    pStr->allocatedLen=pStr->len+1;
    pStr->buf = s;
    pStr->itr=NULL;
}
void BGString_free(BGString* pStr)
{
    if (pStr->buf) {
        xfree(pStr->buf);
        pStr->buf=NULL;
        pStr->len=0;
        pStr->allocatedLen=0;
        pStr->itr=NULL;
    }
}
void BGString_appendn(BGString* pStr, char* s, int sLen, char* separator)
{
    if (!s || !*s)
        return;
    int separatorLen=(pStr->len>0) ? strlen((separator)?separator:"") : 0;
    if (pStr->len+sLen+separatorLen+1 > pStr->allocatedLen) {
        int itrPos=(pStr->itr) ? (pStr->itr -pStr->buf) : -1;
        pStr->allocatedLen=pStr->allocatedLen * 2 + sLen+separatorLen;
        char* temp=xmalloc(pStr->allocatedLen);
        memcpy(temp, pStr->buf, pStr->len+1);
        xfree(pStr->buf);
        pStr->buf=temp;
        pStr->itr=(itrPos>-1) ? pStr->buf+itrPos : NULL;
    }
    if (pStr->len > 0) {
        strcpy(pStr->buf+pStr->len, (separator)?separator:"");
        pStr->len+=separatorLen;
    }
    strncpy(pStr->buf+pStr->len, s, sLen);
    pStr->len+=sLen;
    pStr->buf[pStr->len]='\0';
}
void BGString_append(BGString* pStr, char* s, char* separator)
{
    if (!s || !*s)
        return;
    BGString_appendn(pStr, s, strlen(s), separator);
}
void BGString_copy(BGString* pStr, char* s)
{
    pStr->itr = NULL;
    *pStr->buf = '\0';
    pStr->len = 0;
    if (!s || !*s)
        return;
    BGString_appendn(pStr, s, strlen(s), "");
}
void BGString_replaceWhitespaceWithNulls(BGString* pStr)
{
    for (register int i=0; i<pStr->len; i++)
        if (whitespace(pStr->buf[i]))
            pStr->buf[i]='\0';
}
char* BGString_nextWord(BGString* pStr)
{
    char* pEnd=pStr->buf + pStr->len;
    if (!pStr->itr) {
        pStr->itr=pStr->buf;
        while (pStr->itr < pEnd && *pStr->itr=='\0') pStr->itr++;
        return pStr->itr;
    } else if (pStr->itr >= pEnd) {
        return NULL;
    } else {
        pStr->itr+=strlen(pStr->itr);
        // if the original string list had consequtive whitespace, we have to skip over consequtive nulls
        while (pStr->itr < pEnd && *pStr->itr=='\0') pStr->itr++;
        return (pStr->itr < pEnd) ? pStr->itr : NULL;
    }
}
