#!/usr/bin/env bash
source /usr/lib/bg_core.sh
import bg_objects.sh  ;$L1;$L2

DeclareClass Animal
Animal::speak() {
	echo "the animal says..."
}

function foo()
{
	#enable -f bgObjects.so bgObjects 2>/dev/null

	builtin bgObjects ConstructObject Animal animal
	echo "result=$?"
	$animal.speak

	builtin bgObjects manifestGet "$@"
}
foo "$@"
