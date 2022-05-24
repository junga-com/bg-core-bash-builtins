#!/usr/bin/env bash
source /usr/lib/bg_core.sh
import bg_objects.sh  ;$L1;$L2

DeclareClass Animal
Animal::speak() {
	echo "the animal says..."
}


function foo()
{
	builtin bgCore import "PackageProject.sh"	;$L1;$L2
	type -t PackageProject::makePackage || echo NOPE

	local -A foo=()
	ConstructObject Object foo[bar]
	printfVars foo --plain foo

	#enable -f bgCore.so bgCore 2>/dev/null

	# builtin bgCore ConstructObject Animal animal
	# echo "result=$?"
	# $animal.speak
	#
	# builtin bgCore manifestGet "$@"
}
foo "$@"
