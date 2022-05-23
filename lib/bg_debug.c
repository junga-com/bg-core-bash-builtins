
#include <stdarg.h>
#include <errno.h>
#include "bg_debug.h"
#include "builtins.h"



FILE* _bgtraceFD=NULL;
int bgtraceIndentLevel=0;

void bgtraceOn()
{
    if (!_bgtraceFD) {
        _bgtraceFD=fopen("/tmp/bgtrace.out","a+");
        if (!_bgtraceFD)
            fprintf(stderr, "FAILED to open trace file '/tmp/bgtrace.out' errno='%d'\n", errno);
        else
            fprintf(_bgtraceFD, "BASH bgObjects trace started\n");
    }
}


int _bgtrace(int level, char* fmt, ...)
{
    if (!_bgtraceFD || level>bgtraceLevel) return 1;
    va_list args;
    SH_VA_START (args, fmt);
    fprintf(_bgtraceFD, "%*s", bgtraceIndentLevel*3,"");
    vfprintf(_bgtraceFD, fmt, args);
    fflush(_bgtraceFD);
    return 1; // so that we can use bgtrace in condition
}
