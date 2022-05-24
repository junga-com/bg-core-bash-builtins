#!/usr/bin/env bash
source /usr/lib/bg_core.sh
import bg_objects.sh  ;$L1;$L2

DeclareClass Animal
Animal::speak() {
	echo "the animal says..."
}

function foo()
{
	#enable -f bgCore.so bgCore 2>/dev/null

	builtin bgCore ConstructObject Animal animal
	echo "result=$?"
	$animal.speak

	builtin bgCore manifestGet "$@"
}
foo "$@"
