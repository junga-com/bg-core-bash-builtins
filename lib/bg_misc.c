
#include "bg_misc.h"

#include <errno.h>
#include <regex.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <sys/sendfile.h>

#define _GNU_SOURCE 1
#include <string.h>

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
	char* p = xmalloc(strlen(s1) + strlen(s2)+1);
	strcpy(p, s1);
	strcat(p, s2);
	return p;
}

char* save3string(char* s1, char* s2, char* s3)
{
	char* p = xmalloc(strlen(s1) + strlen(s2) + strlen(s3) +1);
	strcpy(p, s1);
	strcat(p, s2);
	strcat(p, s3);
	return p;
}

char* save4string(char* s1, char* s2, char* s3, char* s4)
{
	char* p = xmalloc(strlen(s1) + strlen(s2) + strlen(s3) + strlen(s4) +1);
	strcpy(p, s1);
	strcat(p, s2);
	strcat(p, s3);
	strcat(p, s4);
	return p;
}

char* resaveWithQuotes(char* s1, int reallocFlag)
{
	int s1Len = strlen(s1);
	if (reallocFlag) {
		s1 = xrealloc(s1, s1Len + 3);
		memmove(s1+1, s1, s1Len);
	} else {
		char* tmp = s1;
		s1 = xmalloc(s1Len + 3);
		strcpy(s1+1, tmp);
	}
	s1[0] = '\'';
	s1[s1Len+1] = '\'';
	s1[s1Len+2] = '\0';
	return s1;
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
ssize_t freadline(FILE* file, char** pBuf, size_t* pBufAllocSize)
{
	if (!pBuf || !pBufAllocSize)
		assertError(NULL, "freadline C function called with null value for either <pBuf> or <pBufAllocSize>");
	if (*pBuf==NULL || (*pBufAllocSize == 0)) {
		*pBufAllocSize = 100;
		*pBuf = xmalloc(*pBufAllocSize);
	}

	(*pBuf)[0]='\0';
	if (!file)
		return -1;
	char* readResult = fgets(*pBuf, *pBufAllocSize,file);
	if (!readResult)
		return -1;
	size_t readLen = strlen(*pBuf);
	while (readResult && ((*pBuf)[readLen-1] != '\n')) {
		*pBufAllocSize *= 2;
		*pBuf = xrealloc(*pBuf, *pBufAllocSize);

		readResult = fgets((*pBuf)+readLen, *pBufAllocSize/2,file);
		readLen = strlen((*pBuf)+readLen)+readLen;
	}
	if (readLen>0 && (*pBuf)[readLen-1] == '\n') {
		(*pBuf)[readLen-1] = '\0';
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


void hexDump(char *desc, void *addr, int len)
{
	int i;
	unsigned char buff[17];
	unsigned char *pc = (unsigned char*)addr;

	// Output description if given.
	if (desc != NULL)
		__bgtrace("%s:\n", desc);

	// Process every byte in the data.
	for (i = 0; i < len; i++) {
		// Multiple of 16 means new line (with line offset).

		if ((i % 16) == 0) {
			// Just don't print ASCII for the zeroth line.
			if (i != 0)
				__bgtrace("  %s\n", buff);

			// Output the offset.
			__bgtrace("  %04x ", i);
		}

		// Now the hex code for the specific character.
		__bgtrace(" %02x", pc[i]);

		// And store a printable ASCII character for later.
		if ((pc[i] < 0x20) || (pc[i] > 0x7e)) {
			buff[i % 16] = '.';
		} else {
			buff[i % 16] = pc[i];
		}

		buff[(i % 16) + 1] = '\0';
	}

	// Pad out last line if not exactly 16 characters.
	while ((i % 16) != 0) {
		__bgtrace("   ");
		i++;
	}

	// And print the final ASCII bit.
	__bgtrace("  %s\n", buff);
}

char* bgstrpbrk(char* s, char* delims)
{
	char* retVar = strpbrk(s, delims);
	if (!retVar)
		retVar = s + strlen(s);
	return retVar;
}


int fsExists(const char* file) {
	struct stat buf;
	return (stat(file, &buf) == 0);
}

#define fsCopyChunkSize (4096*1000)

void fsCopy(const char* src, const char* dst, int flags) {
	int srcFD = open(src, O_RDONLY);
	if (srcFD<=0)
		assertError(NULL, "fsCopy could not open source file for reading. filename='%s'", src);
	int dstFD = open(dst, O_WRONLY|O_CREAT|O_TRUNC, 0777);
	if (dstFD<=0) {
		if (flags&cp_mkdir) {
			char* parentFolder = savestring(dst);
			char* p = parentFolder; while (*p=='/') p++;
			struct stat sb;
			while ((p = strchr (p, '/'))) {
				*p = '\0';
				if (stat(parentFolder, &sb) != 0)
					mkdir(parentFolder, 0777);
				*p = '/';
				while (*p=='/') p++;
			}
			xfree(parentFolder);
			dstFD = open(dst, O_WRONLY|O_CREAT|O_TRUNC, 0777);
		}

		if (dstFD<=0) {
			close(srcFD);
			assertError(NULL, "fsCopy could not open destination file for writing. filename='%s'", dst);
		}
	}
	while (sendfile(dstFD, srcFD, NULL, fsCopyChunkSize));
	close(srcFD);
	close(dstFD);

	if (flags&cp_removeSrc) {
		if (remove(src)!=0)
			assertError(NULL, "fsCopy: could not remove file '%s'. %s", src, strerror(errno));
	}
}

char* saprintf(char* fmt, ...)
{
	if (!fmt || !(*fmt))
		return savestring("");

	// we make an initial guess for the allocSize. If its too small, vsnprintf will tell us so we can then allocate the exact size.
	// if our initial guess is too large, we waste some memory but we avoid having to call vsnprintf twice.

	size_t allocSize = 2 * strlen(fmt);
	char* buf = xmalloc(allocSize);

	va_list args;
	SH_VA_START(args, fmt);
	va_list args2;
	va_copy(args2,args);

	int actualSize = vsnprintf( buf, allocSize, fmt, args);
	if (actualSize >= allocSize) {
		allocSize = actualSize + 1;
		buf = xrealloc(buf, allocSize);
		actualSize = vsnprintf( buf, allocSize, fmt, args2);
		if (actualSize >= allocSize)
			assertError(NULL, "logic error. allocSize should have been large enough but something went wrong.\n");
	}
	return buf;
}


char* mktempC(char* template)
{
	char* fn = savestring((template)?template:"/tmp/tmp.XXXXXXXXXX");
	char* pS = strchr(fn, 'X');
	if (!pS) pS=fn; // strchrnull is not available. in this case it can be the start fn b/c *fn != 'X'

	char* pE = pS;
	while (*pE=='X') pE++;

	int randoCount = (pE - pS);

	if (randoCount<3) {
		xfree(fn);
		assertError(NULL,"template passed to mktemp must contain at least 3 'X'. template='%s'", template);
	}

	static char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
	int charsetSize = (int) (sizeof(charset) -1);

	FILE* urandFD = fopen("/dev/urandom","r");
	if ((!urandFD) || (!fread(pS, 1,randoCount, urandFD)))
		assertError(NULL, "mktempC: could not access /dev/urandom");
	for (int n=0;n < randoCount;n++,pS++) {
		//__bgtrace("  rand='%d' [%d] -> '%c'\n",*pS, abs(*pS) % charsetSize, charset[abs(*pS) % charsetSize]);
		*pS = charset[abs(*pS) % charsetSize];
	}
	fclose(urandFD);

	//__bgtrace("mktempC='%s'\n", fn);
	return fn;
}
