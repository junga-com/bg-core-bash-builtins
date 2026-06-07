#!/bin/bash

echo "here in test.sh"

enable -f ./bin/bgCore.so bgCore

cd ../bg-dev

builtin bgCore manifestGet "plugin" "PackageAsset:.*"
echo "here in test.sh"
