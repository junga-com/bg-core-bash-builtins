#!/usr/bin/env bash
source /usr/lib/bg_core.sh
import bg_objects.sh  ;$L1;$L2

function myCode()
{
	echo "STR mycode"
	assertError "this is my error"
	echo "END mycode"
}

function tryCatchTest()
{
	local name="myName"
	local foo=(a b c d e f g )
#	Try:
		echo "tryCatch b4 exception"
		builtin bgCore testAssertError "$@"
		echo "tryCatch after exception"
#	Catch: && {
#		echo "catch block"
#	}
	echo "moving on..."
}
tryCatchTest "$@"
exit

# DeclareClass Animal
# Animal::speak() {
# 	echo "the animal says..."
# }


bgtrace "From Main Script Global Scope"
builtin bgCore ShellContext_dump

function foo()
{
	bgtrace "From Main Script Function Scope"
	builtin bgCore ShellContext_dump

	source foo.sh
	import foo.sh

	# bgtrace "From Main Script Global Scope"
	# source foo.sh

	# local -A proj; ConstructObject Object proj
	# declare -p proj
	# echo "HI"
	# printfVars --plain --noObjects proj

	#local -n foo; ConstructObject Object foo

	# builtin bgCore import "PackageProject.sh"	;$L1;$L2
	# type -t PackageProject::makePackage || echo NOPE


	#enable -f bgCore.so bgCore 2>/dev/null

	# builtin bgCore ConstructObject Animal animal
	# echo "result=$?"
	# $animal.speak
	#
	# builtin bgCore manifestGet "$@"
}
foo "$@"
# i=0
# while true; do
# 	foo "$@"
# 	if ((i>0)); then
# 		break;
# 	fi
# done
