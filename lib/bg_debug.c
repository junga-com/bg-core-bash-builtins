
#include "bg_debug.h"

#include <stdarg.h>
#include <stdc.h>
#include <errno.h>

#include <sys/types.h>
#include <unistd.h>

FILE* _bgtraceFD=NULL;
int bgtraceIndentLevel=0;

void bgtraceOn()
{
    if (!_bgtraceFD) {
        _bgtraceFD=fopen("/tmp/bgtrace.out","a+");
        if (!_bgtraceFD)
            fprintf(stderr, "FAILED to open trace file '/tmp/bgtrace.out' errno='%d'\n", errno);
        else
            fprintf(_bgtraceFD, "BASH bgCore trace started\n");
    }
}

extern pid_t dollar_dollar_pid;

int _bgtrace(int level, char* fmt, ...)
{
    if (!_bgtraceFD || level>bgtraceLevel) return 1;
//    if (dollar_dollar_pid != getpid()) return 1;
    va_list args;
    SH_VA_START (args, fmt);
    fprintf(_bgtraceFD, "%*s", bgtraceIndentLevel*3,"");
    vfprintf(_bgtraceFD, fmt, args);
    fflush(_bgtraceFD);
    return 1; // so that we can use bgtrace in condition
}
