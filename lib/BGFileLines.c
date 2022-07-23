
#include "BGFileLines.h"

#include "bg_bashAPI.h"

void BGFileLines_init(BGFileLines* pBuf, size_t bufSize)
{
	pBuf->allocatedLen = bufSize;
	pBuf->buf = xmalloc(pBuf->allocatedLen);
	pBuf->len = 0;
	pBuf->lastEOLLen = 0;
	pBuf->savedChar = '\0';
}

int  BGFileLines_readChunk(BGFileLines* pBuf, FILE* fd)
{
	// if there is a partial line at the end of the buffer, move it to the front, otherwise set the len to zero to start the next chunk
	if (pBuf->lastEOLLen < pBuf->len) {
		pBuf->buf[pBuf->lastEOLLen] = pBuf->savedChar;
		memmove(pBuf->buf, &(pBuf->buf[pBuf->lastEOLLen]), (pBuf->len - pBuf->lastEOLLen));
		pBuf->len = (pBuf->len - pBuf->lastEOLLen);
		pBuf->lastEOLLen = 0;
		pBuf->buf[pBuf->len] = '\0';
	} else {
		pBuf->len = 0;
		pBuf->lastEOLLen = 0;
	}

	// read another chunk
	int maxReadSize = pBuf->allocatedLen - pBuf->len -1;
	int bytesRead = fread(pBuf->buf + pBuf->len, 1, maxReadSize, fd);
	pBuf->len += bytesRead;
	pBuf->buf[pBuf->len] = '\0';

	// set pS to one past the \n of the last full line or to the null at the end of the buffer if there is no full line.
	char* pS = strrchr(pBuf->buf, '\n');
	pS = ( (pS) ? pS+1 : (pBuf->buf+pBuf->len) );

	// if there is a partial line at the end of th buffer, save and then replace the first char of that line with a null so that
	// the caller wont start processing that line until its complete in the next iteration
	pBuf->lastEOLLen = pS - pBuf->buf;
	if (pBuf->lastEOLLen < pBuf->len) {
		pBuf->savedChar = pBuf->buf[pBuf->lastEOLLen];
		pBuf->buf[pBuf->lastEOLLen] = '\0';
	}

	// the return value is a boolean indicating if there is data remaining to be processed.
	return (pBuf->len > 0);
}
