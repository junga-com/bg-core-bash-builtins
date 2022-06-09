#!/usr/bin/env bash
source /usr/lib/bg_core.sh
function static::Object::testFoo()
{
	echo "static be stylin"
}
import bg_objects.sh  ;$L1;$L2

if IsAnObjRef "$@"; then
	echo it is an objRef
else
	echo "!NOPE!"
fi
exit

#DeclareClass Animal
Animal::speak() {
	echo "the animal says..."
}

function foo()
{
	# import Project.sh ;$L1;$L2
	# import bg_json.sh ;$L1;$L2

	local -n obj; ConstructObject Object obj
	obj[foo]="ello world"
	obj[bar]="wasup"
	$obj.array=new Array
	printfVars obj[0]
	# local -a indexes; $obj.getIndexes -A indexes
	# printfVars --noObjects indexes
}
foo "$@"
