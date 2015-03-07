#!/usr/bin/env sh
HERE="$(dirname ${0})"
BUILD="${HERE}/builds"
[ -d "${BUILD}" ] || mkdir -p "${BUILD}"


function compile_clang()
{
    local cflags=()
    local cxxflags=()

    cflags=("${cflags[@]}" "-fno-rtti" "-fno-exceptions")
    cflags=("${cflags[@]}" "-isystem" "${HERE}"/include)
    cflags=("${cflags[@]}" "-Wall" "-Wextra" "-Werror")
    cflags=("${cflags[@]}" "-Wno-padded" "-Wno-unused-parameter" "-Wno-conversion")
    cflags=("${cflags[@]}" -g -gdwarf-4)
    cxxflags=("${cflags[@]}" "-std=c++11")

    clang++ "${cflags[@]}" "${cxxflags[@]}" "$@"
}

compile_clang "${HERE}"/src/zwei_osx.cpp -o "${BUILD}"/zwei && \
    find "${BUILD}" -type f -depth 1 | xargs printf "PROGRAM\t%s\n"
