#!/usr/bin/env bash
source /usr/lib/bg_core.sh



function foo()
{
	import bg_objects.sh ;$L1;$L2
	import Project.sh ;$L1;$L2
	import PackageProject.sh ;$L1;$L2
	import bg_json.sh ;$L1;$L2

	local -n sub; ConstructObjectFromJson sub ../.bglocal/run/bg-core.3423746.json

	printfVars sub
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
