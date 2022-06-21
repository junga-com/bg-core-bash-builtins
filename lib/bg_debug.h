
#if !defined (_bg_debug_H_)
#define _bg_debug_H_

#include <config.h>
#include <stdio.h>

// to embed a breakpoint for debugging
#include <signal.h>
//use this-> raise(SIGINT);




/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// bgtrace

// 0 == no bgtrace
// 1 == minmal bgtrace
// 2 == more tracing
// 9 == all tracing
#ifndef bgtraceLevel
#define bgtraceLevel 9
#endif

#if bgtraceLevel > 0
#   define bgtrace0(level, fmt)                _bgtrace(level,fmt)
#   define bgtrace1(level, fmt,p1)             _bgtrace(level,fmt,p1)
#   define bgtrace2(level, fmt,p1,p2)          _bgtrace(level,fmt,p1,p2)
#   define bgtrace3(level, fmt,p1,p2,p3)       _bgtrace(level,fmt,p1,p2,p3)
#   define bgtrace4(level, fmt,p1,p2,p3,p4)    _bgtrace(level,fmt,p1,p2,p3,p4)
#   define bgtrace5(level, fmt,p1,p2,p3,p4,p5) _bgtrace(level,fmt,p1,p2,p3,p4,p5)
#   define bgtracePush() bgtraceIndentLevel++
#   define bgtracePop()  bgtraceIndentLevel--
#	define bgtraceStack()                       _bgtraceStack()
#else
#   define bgtrace0(level,fmt)
#   define bgtrace1(level,fmt,p1)
#   define bgtrace2(level,fmt,p1,p2)
#   define bgtrace3(level,fmt,p1,p2,p3)
#   define bgtrace4(level,fmt,p1,p2,p3,p4)
#   define bgtrace5(level,fmt,p1,p2,p3,p4,p5)
#   define bgtracePush()
#   define bgtracePop()
#	define bgtraceStack()
#endif


extern FILE* _bgtraceFD;
extern int bgtraceIndentLevel;
extern void bgtraceOn();
extern int _bgtrace(int level, char* fmt, ...);
extern int __bgtrace(char* fmt, ...);
extern void _bgtraceStack();

#endif /* _bg_debug_H_ */
