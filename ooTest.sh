#!/usr/bin/env bash
source /usr/lib/bg_core.sh
function static::Object::testFoo()
{
	echo "static be stylin"
}
import bg_objects.sh  ;$L1;$L2

DeclareClass Animal
Animal::speak() {
	assertError "DONT SPEAK!"
	echo "the animal says..."
}

function foo()
{
	# import Project.sh ;$L1;$L2
	# import bg_json.sh ;$L1;$L2

	local -n obj; ConstructObject Animal obj
	obj[foo]="ello world"
	obj[bar]="wasup"
	$obj.array=new Array
	echo "b4"
	Try:
		$obj.speak
	Catch: && {
		echo "CAUGHT!"
	}
	echo "after"
	printfVars obj[0]
	# local -a indexes; $obj.getIndexes -A indexes
	# printfVars --noObjects indexes
}
foo "$@"
