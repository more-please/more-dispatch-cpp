#!/bin/bash

set -e

cd `dirname $0`

for f in `find -E . -regex ".*\.(h|cpp)" | grep -v CMake`; do
	echo "Formatting $f"
	clang-format -style=file -i $f
done
