#!/bin/bash

# directories to be formatted (recursive search)
DIRS="Oros/include Oros/src tests"
FORMAT_CMD="clang-format -style=file:scripts/clang_style -i"

echo "- clang-format"

# format C++
for D in ${DIRS}; do
    for F in `find ${D}/. -type f \( -iname \*.hpp -o -iname \*.inl -o -iname \*.cpp \)`; do
	echo ${F}
	${FORMAT_CMD} ${F}
    done
done

# format cmake files
echo "- cmake-format"

if command -v cmake-format &> /dev/null; then
    cmake-format -i CMakeLists.txt Oros/CMakeLists.txt tests/CMakeLists.txt tests/ui_test/CMakeLists.txt tests/unittests/CMakeLists.txt
else
    echo "cmake-format not found, skipping cmake formatting."
fi
