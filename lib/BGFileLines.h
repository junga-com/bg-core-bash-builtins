
#if !defined (_BGFileLines_H_)
#define _BGFileLines_H_

#include <stdarg.h>
#include <stdio.h>

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// BGFileLines
// This is a buffer for efficiently processing a text file in chunks when you dont want to assume that the entire file will fit in
// memory.
// Each chunk will consist of as many lines as will fit in the allocated buffer but will not include any partially read lines so
// that line processing semantics will still hold.


typedef struct {
	char* buf;
	size_t allocatedLen;
	int len;
	int lastEOLLen;
	char savedChar;
} BGFileLines;

extern void BGFileLines_init(BGFileLines* pBuf, size_t bufSize);
extern int  BGFileLines_readChunk(BGFileLines* pBuf, FILE* fd);


#endif // _BGFileLines_H_
