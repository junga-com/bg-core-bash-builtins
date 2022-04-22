#!/usr/bin/env bash

gcc -fPIC -DHAVE_CONFIG_H -DSHELL  -g -O2 -Wno-parentheses -Wno-format-security -I. -I../include -I/usr/include -I/usr/include/bash -I/usr/include/bash/include -I/usr/include/bash/builtins   -c -o bgObjects.o bgObjects.c
gcc -shared -Wl,-soname,bgObjects  -o bgObjects bgObjects.o
