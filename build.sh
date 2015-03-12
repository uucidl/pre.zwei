#!/usr/bin/env sh
HERE="$(dirname ${0})"
BUILD="${HERE}/builds"
[ -d "${BUILD}" ] || mkdir -p "${BUILD}"


function compile_osx()
{
    local cflags=()
    local cxxflags=()
    local PLATFORM_SDK="/Applications/Xcode.app/Contents//Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX10.10.sdk/usr/include/"

    cflags=("${cflags[@]}" -mmacosx-version-min=10.8)
    cflags=("${cflags[@]}" -isystem "${PLATFORM_SDK}")
    cxxflags=("${cxxflags[@]}" -stdlib=libc++)

    cflags=("${cflags[@]}" "-fno-rtti" "-fno-exceptions")
    cflags=("${cflags[@]}" "-isystem" "${HERE}"/include)
    cflags=("${cflags[@]}" "-Wall" "-Wextra" "-Wshorten-64-to-32" "-Werror")
    cflags=("${cflags[@]}" "-Wno-padded" "-Wno-unused-parameter")
    cflags=("${cflags[@]}" -g -gdwarf-3)
    cxxflags=("${cxxflags[@]}" "-std=c++11")

    cflags=("${cflags[@]}" -DZWEI_SLOW)

    clang++ "${cflags[@]}" "${cxxflags[@]}" "$@"
}

compile_osx "${HERE}"/src/zwei_osx.cpp -o "${BUILD}"/zwei && \
    find "${BUILD}" -type f -depth 1 | xargs printf "PROGRAM\t%s\n"
