#!/usr/bin/env bash
source /usr/lib/bg_core.sh

#bgCore testAssertError

# echo wasup
# import bg_objects.sh ;$L1;$L2
# declare -n proj
# ConstructObject Project proj /home/bobg/github/bg-CreateCommonSandbox/bg-pkgrepo-deb/
# #ConstructObject Project proj
# $proj.toString
# echo hey there
# exit


function manifestTest()
{
	bgCore manifestGet "$@"
}
# manifestTest "$@"
# exit

function namerefTest()
{
	local aVar
	local -n bVar="$1"
	bVar=(one two three)
	declare -p aVar bVar
}
# declare -a aVar; namerefTest "aVar"; printfVars aVar "" ""
# declare -a bVar; namerefTest "bVar"; printfVars bVar "" ""
# declare -a cVar; namerefTest "cVar"; printfVars cVar "" ""
# exit

# bgCore transTest
# exit

# declare -a iniFiles=(one)
# fsExpandFiles -f -A iniFiles foo/goo /etc/bgsys.conf -true -true
# printfVars iniFiles
# exit


function builtinTest()
{
	import bg_template.sh ;$L1;$L2
	bgtimerStart
	bgCore  "$@"
	# bgtimerLapPrint "c impl"
	# "$@"
	# bgtimerLapPrint "bash impl"
}
builtinTest "$@"
exit

function static::Object::testFoo()
{
	echo "static be stylin"
}
import bg_objects.sh  ;$L1;$L2
Try:
	declare -n obj; ConstructObject Object obj
	_bgclassCall obj Object 0 :.toString
Catch: && {
	echo "caught it"
	PrintException
}
exit


function ut_()
{
	local a2; ConstructObject Stack a2
	printfVars a2
	$a2.push one
	$a2.push two
	$a2.push three
	$a2.getSize
	printfVars a2

	for ((i=0; i<$($a2.getSize); i++)); do $a2.peek $i; done

	local element
	while $a2.pop element; do printfVars element; done
}
ut_ "$@"
exit

DeclareClass Animal
Animal::speak() {
	assertError "DONT SPEAK!"
	echo "the animal says..."
}



function foo()
{
	# import Project.sh ;$L1;$L2
	# import bg_json.sh ;$L1;$L2

	local -n obj; ConstructObject Stack obj
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
