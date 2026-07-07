
#include "bg_debug.h"

#include <stdarg.h>
#include <stdc.h>
#include <errno.h>
#include <execinfo.h>

#include <sys/types.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <strings.h>

#include "bg_bashAPI.h"

char* _bgtraceFile="";
FILE* _bgtraceFD=NULL;
int bgtraceIndentLevel=0;



static void bg_segv_handler(int sig)
{
	void *frames[64];
	int n;

	int outFD = _bgtraceFD ? fileno(_bgtraceFD) : STDERR_FILENO;

	const char msg[] = "\nBG builtin caught SIGSEGV; stack trace follows:\n";
	(void)!write(outFD, msg, sizeof(msg)-1);

	n = backtrace(frames, 64);
	backtrace_symbols_fd(frames, n, outFD);

	signal(sig, SIG_DFL);
	raise(sig);
}

void bgtraceOn()
{
	if (!_bgtraceFD) {
		const char* home = getenv("HOME");
		const char* tracePath = "/tmp/bgtrace.out";
		char pathBuf[PATH_MAX];

		if (home && *home) {
			snprintf(pathBuf, sizeof(pathBuf), "%s/.bgtrace.out", home);
			tracePath = pathBuf;
		}

		_bgtraceFD = fopen(tracePath, "a+");

		if (!_bgtraceFD)
			fprintf(stderr, "FAILED to open trace file '%s' errno='%d'\n", tracePath, errno);
		else {
			_bgtraceFile = bg_savestring(tracePath)
			bgtrace0(0, "BASH bgCore trace started\n");
		}
	}

	// install a segfault handler to bgtrace a stack trace
	const char* inhibitFlag = getenv("bgInhibitDebugSegHandling");
	if (!inhibitFlag ||strcasecmp(inhibitFlag,"yes")!=0) {
		struct sigaction sa = {0};
		sa.sa_handler = bg_segv_handler;
		sigemptyset(&sa.sa_mask);
		sa.sa_flags = SA_RESETHAND | SA_NODEFER;
		sigaction(SIGSEGV, &sa, NULL);
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
	if (_bgtraceFD == NULL)
		return 0;
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
