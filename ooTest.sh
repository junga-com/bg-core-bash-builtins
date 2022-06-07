#!/usr/bin/env bash
source /usr/lib/bg_core.sh
import bg_objects.sh  ;$L1;$L2


# DeclareClass Animal
# Animal::speak() {
# 	echo "the animal says..."
# }


function foo()
{
	import Project.sh ;$L1;$L2
	import bg_json.sh ;$L1;$L2


	local -n proj; ConstructObject Project proj .
#	printfVars --noObjects proj

	$proj.toJSON > /tmp/proj.json
}
foo "$@"
