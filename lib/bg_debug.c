
#include "bg_debug.h"

#include <stdarg.h>
#include <stdc.h>
#include <errno.h>
#include <execinfo.h>

#include <sys/types.h>
#include <unistd.h>

#include "bg_bashAPI.h"

FILE* _bgtraceFD=NULL;
int bgtraceIndentLevel=0;

void bgtraceOn()
{
	if (!_bgtraceFD) {
		_bgtraceFD=fopen("/tmp/bgtrace.out","a+");
		if (!_bgtraceFD)
			fprintf(stderr, "FAILED to open trace file '/tmp/bgtrace.out' errno='%d'\n", errno);
		else
			bgtrace0(0, "BASH bgCore trace started\n");
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

int __bgtrace(char* fmt, ...)
{
	va_list args;
	SH_VA_START (args, fmt);
	vfprintf(_bgtraceFD, fmt, args);
	fflush(_bgtraceFD);
	return 1; // so that we can use bgtrace in condition
}


void _bgtraceStack()
{
	SHELL_VAR* vFuncname = ShellVar_findGlobal("FUNCNAME");
	SHELL_VAR* vSource = ShellVar_findGlobal("BASH_SOURCE");
	SHELL_VAR* vLineno = ShellVar_findGlobal("BASH_LINENO");

	char* firstLineno = ShellVar_getS("LINENO");

	ARRAY_ELEMENT* elFuncname=ShellVar_arrayStart(vFuncname);
	ARRAY_ELEMENT* elSource=ShellVar_arrayStart(vSource);
	ARRAY_ELEMENT* elLineno=ShellVar_arrayStart(vLineno);

	int maxSourceLen=0;
	for (ARRAY_ELEMENT* el = ShellVar_arrayStart(vSource);  el != ShellVar_arrayEOL(vSource); el = el->next) {
		char* t = strrchr(elSource->value,'/');
		int len = strlen(t?t:elSource->value);
		maxSourceLen = (len>maxSourceLen)? len : maxSourceLen;
	}

	__bgtrace("C stack trace:\n");
	void *array[100];
	int size = backtrace (array, 10);
	char **strings = backtrace_symbols (array, size);
	if (strings != NULL) {
		for (int i = 0; i < size; i++) {
			char* t = strrchr(strings[i], '/');
			if (strstr(strings[i], "bgCore.so"))
				__bgtrace("   %s\n", t?t+1:strings[i]);
		}
	}
	free (strings);

	__bgtrace("Shell Stack Trace: \n");
	int first=1;
	while ( elFuncname!=ShellVar_arrayEOL(vFuncname)) {
		char* sLineno = (first) ? firstLineno : elLineno->value;
		char* t = strrchr(elSource->value,'/');
		__bgtrace("   %*s:%-4s: %s\n", -maxSourceLen, t?t+1:elSource->value, sLineno, elFuncname->value);

		elFuncname=elFuncname->next;
		elSource=elSource->next;
		if (!first) elLineno=elLineno->next;
		first=0;
	}

	__bgtrace("\n");
}
