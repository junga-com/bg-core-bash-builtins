#!/usr/bin/env bash
source /usr/lib/bg_core.sh

function ut_Map()
{
	import bg_objects.sh ;$L1;$L2
	local a2; ConstructObject Map a2
	$a2.getOID
	printfVars --noObjects "$($a2.getOID)"{,_sys}
	local -n a2OID; $a2.getOID a2OID
	printfVars a2
	a2OID=([one]=1 [two]=2 [three]=3)
	printfVars --noObjects a2OID
	$a2.getIndexes
}
ut_Map
exit

function foo()
{
	import bg_objects.sh ;$L1;$L2
	local -n foo; ConstructObject Object foo
	foo[one]=1
	foo[two]=2
	foo[three]=3
	local str
	local -a array
	local -A set
	$foo.getIndexes "$@"

	printfVars str array set
}

function top()
{
	echo "top: b4"
	Try:
		foo "$@"
	Catch: && {
		echo "top: caught exception"
	}
	echo "top:after"
}

foo "$@"
#top "$@"
