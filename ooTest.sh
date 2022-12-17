#!/usr/bin/env bash

source /usr/lib/bg_core.sh

# foo="hi"
# varContextToJSON
# #bgCore dbgVars "" 3
# exit

trap 'echo "EXIT one"
echo "EXIT two"' EXIT

echo $$
bgtraceBreak
bgCore ping

function f1()
{
	echo "i am f1"
}

function f2()
{
	local a
	echo "i am f2"
	f1
}

function f3()
{
	local b=1
	local ary=("one", "two", "three")
	local -A assoc=( ["one"]=1, ["two"]=2, ["three"]=3)
	bgCore dbgVars "" 3
	echo "i am f3"
	f2
}

function foo()
{
	local c
	f3
	echo foo1
	echo foo2
}

echo "before"
foo
#var="$(foo)"
echo "after"
exit

echo $$
count=0
while true; do
	if (( (count++ % 15) == 0 )); then
		echo "still going... $count"
	fi
	sleep 1
done
exit


import bg_objects.sh ;$L1;$L2
declare -n obj; ConstructObject Object obj
$obj.foo=5
$obj.array=new Array

$obj.toString
$obj.foo.toString
$obj.foo.toString --title
$obj.foo.toString --title=
$obj.foo.toString --title=foo
$obj.foo.toString --title=DifferentLable
$obj.array.toString
$obj.array.toString --title
$obj.array[0]="hiya"
$obj.array.toString
$obj.getIndexes.toString
$obj.getIndexes.toString
$obj.dontExist.toString --title
$obj.dontExist.toString
$obj[dontExist].toString
$obj.::dontExist.toString
$obj.Object::dontExist.toString
exit


function a1()
{
	local myvar="a1"
	a2
	echo "myvar='$myvar'"
}

function a2()
{
	local myvar="a2"
	a3
	echo "myvar='$myvar'"
}

function a3()
{
	local myvar="a3"
#	bgCore testAssertError segfault
	echo "myvarA3='$myvar'"
}

bgtraceBreak
a1
exit

# import bg_template.sh  ;$L1;$L2
# templateExpandStr "Hello $USER. Now is %now:^H:%M:^S% good%bye"
# exit


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
