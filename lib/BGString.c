
#include "BGString.h"

#include "bg_bashAPI.h"

void BGString_init(BGString* pStr, int allocatedLen)
{
	pStr->len=0;
	pStr->allocatedLen=allocatedLen;
	pStr->buf=xmalloc(pStr->allocatedLen);
	pStr->buf[pStr->len]='\0';
	pStr->itr=NULL;
	pStr->indentLevel = 0;
	pStr->lineEnd = "\n";
	pStr->sep = ",";
}
void BGString_initFromStr(BGString* pStr, char* s)
{
	pStr->len=bgstrlen(s);
	pStr->allocatedLen=pStr->len+1;
	pStr->buf=xmalloc(pStr->allocatedLen);
	strcpy(bgstr(pStr->buf), s);
	pStr->itr=NULL;
	pStr->indentLevel = 0;
	pStr->lineEnd = "\n";
	pStr->sep = ",";
}
void BGString_initFromAllocatedStr(BGString* pStr, char* s)
{
	pStr->len=strlen(s);
	pStr->allocatedLen=pStr->len+1;
	pStr->buf = s;
	pStr->itr=NULL;
	pStr->indentLevel = 0;
	pStr->lineEnd = "\n";
	pStr->sep = ",";
}
void BGString_grow(BGString* pStr, int newAllocatedLen)
{
	if (newAllocatedLen <= 0)
		newAllocatedLen = pStr->allocatedLen*2;
	if (newAllocatedLen <= pStr->allocatedLen)
		return;

	// save the itr position as an offset
	int itrPos=(pStr->itr) ? (pStr->itr -pStr->buf) : -1;

	pStr->allocatedLen=newAllocatedLen;
	pStr->buf=xrealloc(pStr->buf,pStr->allocatedLen);

	// restore the saved itr position
	pStr->itr=(itrPos>-1) ? pStr->buf+itrPos : NULL;
}
void BGString_free(BGString* pStr)
{
	if (pStr->buf) {
		xfree(pStr->buf);
		pStr->buf=NULL;
		pStr->len=0;
		pStr->allocatedLen=0;
		pStr->itr=NULL;
		pStr->indentLevel = 0;
		pStr->lineEnd = "\n";
		pStr->sep = ",";
	}
}

int BGString_readln(BGString* pStr, FILE* fd)
{
	int readResult = freadline(fd, &(pStr->buf), &(pStr->allocatedLen));
	if (readResult>=0) {
		pStr->len = readResult;
		return 1;
	} else {
		pStr->len = 0;
		return 0;
	}
}

int BGString_writeln(BGString* pStr, FILE* fd)
{
	int bytesWritten = 0;
	if (pStr->len>0) {
		pStr->buf[pStr->len] = '\n';
		bytesWritten = fwrite(pStr->buf, 1, pStr->len+1, fd);
		pStr->buf[pStr->len] = '\0';
	} else {
		bytesWritten = fwrite("\n", 1, 1, fd);
	}
	return bytesWritten;
}


void BGString_appendf(BGString* pStr, char* separator, char* fmt, ...)
{
	va_list args;
	SH_VA_START(args, fmt);
	BGString_appendfv(pStr, separator, fmt, args);
}

void BGString_appendfv(BGString* pStr, char* separator, char* fmt, va_list args)
{
	if (!fmt || !*fmt)
		return;

	int separatorLen=(pStr->len>0) ? strlen((separator)?separator:"") : 0;
	if ( (pStr->len + separatorLen + strlen(fmt)) > pStr->allocatedLen )
		BGString_grow(pStr, pStr->len + separatorLen + strlen(fmt));

	if (pStr->len > 0 && separatorLen>0) {
		strcpy(pStr->buf+pStr->len, (separator)?separator:"");
		pStr->len+=separatorLen;
	}

	va_list args2;
	va_copy(args2,args);
	int spaceLeft = (pStr->allocatedLen-pStr->len);
	int sLen = vsnprintf( (pStr->buf+pStr->len), spaceLeft, fmt, args);
	if (sLen >= spaceLeft) {
		BGString_grow(pStr, pStr->len + sLen + 1);
		spaceLeft = (pStr->allocatedLen-pStr->len);
		sLen = vsnprintf( (pStr->buf+pStr->len), spaceLeft, fmt, args2);
	}
	pStr->len += sLen;
}


void BGString_appendn(BGString* pStr, char* separator, char* s, int sLen)
{
	if (!s || !*s)
		return;
	int separatorLen=(pStr->len>0) ? strlen((separator)?separator:"") : 0;

	if (pStr->len+sLen+separatorLen+1 > pStr->allocatedLen)
		BGString_grow(pStr, pStr->allocatedLen * 2 + sLen+separatorLen);

	if (pStr->len > 0) {
		strcpy(pStr->buf+pStr->len, (separator)?separator:"");
		pStr->len+=separatorLen;
	}
	strncpy(pStr->buf+pStr->len, s, sLen);
	pStr->len+=sLen;
	pStr->buf[pStr->len]='\0';
}
void BGString_append(BGString* pStr, char* separator, char* s)
{
	if (!s || !*s)
		return;
	BGString_appendn(pStr, separator, s, strlen(s));
}
void BGString_copy(BGString* pStr, char* s)
{
	pStr->itr = NULL;
	*pStr->buf = '\0';
	pStr->len = 0;
	if (!s || !*s)
		return;
	BGString_appendn(pStr,"", s, strlen(s));
}
void BGString_replaceWhitespaceWithNulls(BGString* pStr)
{
	for (register int i=0; i<pStr->len; i++)
		if (whitespace(pStr->buf[i]))
			pStr->buf[i]='\0';
}
void BGString_replaceChar(BGString* pStr, char toReplace, char withThis)
{
	for (register int i=0; i<pStr->len; i++)
		if (pStr->buf[i] == toReplace)
			pStr->buf[i]=withThis;
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
