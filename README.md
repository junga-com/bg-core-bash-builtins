This project provides the bgObject loadable builtin for bash

### DESCRIPTION:
The bg-core package includes the bg_objects.sh bash script library that enables using object oriented programming techniques in
bash scripts. The default implementation uses only bash functions to implement the OO features. That works but an object oriented
method call adds about 20ms (on an Intel 11th Gen Core i7-1165G7) of overhead compared to a normal bash function call. Enabling
this loadable builtin will redirect several bg_objects.sh functions to the "bgObject <cmd> [<arg1>...<argN>]" builtin command which
eliminates most of that overhead. There will still be about 1/2 a ms (0.0005sec) overhead so normal bash functions should be prefered
for any low level script features that may be called many times in a script.

The bg_object.sh library  will automatically enable and use this builtin if it is installed (or bg-dev virtually installed) on the
host.  It will attempt to "enable -f bgObjects.so bgObjects" but will fallback to the bash script implementation if the builtin is
not found in any of the paths in BASH_LOADABLES_PATH. The path /usr/lib/bash will be searched if BASH_LOADABLES_PATH is not set.

### EXAMPLE OO SCRIPT:
Example OO Bash...

	$ cat /tmp/test.sh
	#!/usr/bin/env bash
	source /usr/lib/bg_core.sh
	import bg_objects.sh  ;$L1;$L2

	DeclareClass Animal
	function Animal::__construct() {
		this[name]="${1:-anonymous}"
	}
	function Animal::speak() {
		echo "${this[name]} says 'generic animal sound'"
	}

	DeclareClass Dog : Animal
	function Dog::speak() {
		echo "${this[name]} says 'woof'"
	}

	DeclareClass Cat : Animal
	function Cat::speak() {
		echo "${this[name]} says 'meow'"
	}

	declare -A spot;     ConstructObject Dog spot     "Spot"
	declare -A whiskers; ConstructObject Cat whiskers "Whiskers"


	$spot.speak
	$whiskers.speak

	$ bash /tmp/test.sh
	Spot says 'woof'
	Whiskers says 'meow'


### BUILDING:
run ...
   <projRoot>$ ./configure && make

The ./configure script ..
   1. tries to install build dependencies which are mainly gnu compiler/linker and the bash-builtins package that has the headers
      and example Makefile configured for this machine's architecture.
   2. makes a list of each *.c file in the top level project folder which are each assumed to be builtins
   3. creates the Makefile in the root project folder by copying /usr/lib/bash/Makefile.inc and replacing its example tagets with
      targets that build the builtins found in this project in step 2.

The output <builtin>.so file(s) will be placed in the project's ./bin/ folder.


### TESTING:
You can test the builtin by enabling it in your
interactive shell.
   <projRoot>$ bash # create a new interactive shell to test in case a bug in your builtin exits the shell.
   <projRoot>$ enable -f bin/<builtin>.so <builtin>
   <projRoot>$ <builtin> ....
   <projRoot>$ exit  # go back to your base shell when done testing

If you are using the bg-dev tools to virtually install this project, the project's bin/ folder will be in the BASH_LOADABLES_PATH
so you do not have to include any path like ... "enable -f <builtin>.so <builtin>". This also allows you to test the builtin from
any installed or virtually installed script (probably from a different project).


### INSTALLING:
run ...
   <projRoot>$ make install

It will copy the <builtin>.so files from this project into your host's /usr/lib/bin/ folder. You may need to set BASH_LOADABLES_PATH
in a bash startup file (e.g. /etc/bash.bashrc) to include /usr/lib/bin/ if it does not already. It is a ':' separated list of folders.


### BUILDING A PACKAGE:
You can use the bg-dev command from the bg-dev package to create a package for this project.
run ...
   <projRoot>$ sudo apt install bg-dev
   <projRoot>$ bg-dev buildPkg [deb|rpm]

The package file will be placed in the project root folder
