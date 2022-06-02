
echo "here in test.sh"


bgtrace "From foo.sh Script Global Scope"
builtin bgCore ShellContext_dump

function foo()
{
	bgtrace "From foo.sh Script Function Scope"
	builtin bgCore ShellContext_dump
}
foo
