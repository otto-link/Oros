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

# format markdown files
echo "- mdformat"

if command -v mdformat &> /dev/null; then
    MD_DIRS="docs"
    for D in ${MD_DIRS}; do
        if [ -d "${D}" ]; then
            for F in `find ${D} -type f -iname \*.md`; do
                echo ${F}
                mdformat ${F}
            done
        fi
    done
    if [ -f "README.md" ]; then
        echo "README.md"
        mdformat README.md
    fi
else
    echo "mdformat not found, skipping markdown formatting."
fi

