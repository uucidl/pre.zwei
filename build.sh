#!/usr/bin/env sh
set -u

HERE="$(dirname ${0})"
BUILD="${HERE}/builds"

CONFIG=development
CONFIG_LIST=(development release)

function validate_config () {
    local found_success=1;

    for c in ${CONFIG_LIST[@]}; do
        if [[ ${c} == ${CONFIG} ]]; then
            found_success=0
        fi
    done

    return ${found_success}
}

while [ "$#" -gt 0 ]; do
    case $1 in
        --config)
            CONFIG=${2:?"build config expected"}
            validate_config
            if [[ $? -ne 0 ]]; then
                printf "expected a valid configuration (one of %s), got %s\n" "${CONFIG_LIST[*]}" "${CONFIG}"
                exit 1
            fi
            shift
            shift
            ;;
        --help|-h)
            printf -- "Usage: %s [--config [%s]]\n" "${0}" "$(IFS="|" ; echo "${CONFIG_LIST[*]}")"
            unset IFS
            exit 1
            shift
            ;;
        *)
            break
            ;;
    esac
done


function compile_osx()
{
    local cflags
    local cxxflags
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

    if [[ "${CONFIG}" == "development" ]]; then
        cflags=("${cflags[@]}" -DZWEI_SLOW)
    fi

    if [[ "${CONFIG}" == "release" ]]; then
        cflags=("${cflags[@]}" -O3)
    fi

    clang++ "${cflags[@]}" "${cxxflags[@]}" "$@"
}

[ -d "${BUILD}" ] || mkdir -p "${BUILD}"

PROGRAM="${BUILD}"/zwei

compile_osx "${HERE}"/src/zwei_osx.cpp -o "${PROGRAM}" \
    && printf "PROGRAM\t%s\n" "${PROGRAM}"

# beep
printf "\a"
