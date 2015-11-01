#!/usr/bin/env bash
set -u

HERE="$(dirname ${0})"
AHERE="$(cd "${HERE}" ; pwd)"

source "${HERE}"/scripts/lib/user.sh

BUILD="${HERE}/builds"
ABUILD="${AHERE}"/builds
CONFIG=development
CONFIG_LIST=(development release)
PROFILING=false

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
        --profile-build)
            PROFILING=true
            shift
            ;;
        --help|-h)
            printf -- "Usage: %s [--profile-build][--config [%s]]\n" "${0}" "$(IFS="|" ; echo "${CONFIG_LIST[*]}")"
            unset IFS
            exit 1
            shift
            ;;
        *)
            printf -- "Unknown option: %s\n" "$1"
            exit 1
            break
            ;;
    esac
done

# @id 8413bd5dd3db97953791db977bac6bdd6d95852b
function must_compile_osx()
{
    local cflags
    local cxxflags
    local PLATFORM_SDK="/Applications/Xcode.app/Contents//Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX10.10.sdk/usr/include/"

    cflags=("${cflags[@]}" -mmacosx-version-min=10.8)
    cflags=("${cflags[@]}" -isystem "${PLATFORM_SDK}")
    cxxflags=("${cxxflags[@]}" -stdlib=libc++)

    cflags=("${cflags[@]}" "-DCOMPILER_LLVM")
    cflags=("${cflags[@]}" "-fno-rtti" "-fno-exceptions")
    cflags=("${cflags[@]}" "-isystem" "${HERE}"/include)
    cflags=("${cflags[@]}" "-Wall" "-Wextra" "-Wshorten-64-to-32" "-Werror")
    cflags=("${cflags[@]}" "-Wno-padded" "-Wno-unused-parameter")
    cflags=("${cflags[@]}" -g -gdwarf-3)
    cxxflags=("${cxxflags[@]}" "-std=c++11")

    if [[ "${CONFIG}" == "development" ]]; then
        cflags=("${cflags[@]}" -DZWEI_SLOW -DZWEI_INTERNAL)
    fi

    if [[ "${CONFIG}" == "release" ]]; then
        cflags=("${cflags[@]}" -O3)
    fi

    clang++ "${cflags[@]}" "${cxxflags[@]}" "$@"
    if [[ "$?" -ne 0 ]]; then
        exit 1
    fi
}

function must_compile_hammer()
{
    "${SCONS}" -C "${HERE}"/modules/hammer \
               prefix="/" DESTDIR="${ABUILD}" bindings=cpp \
               install
    if [[ "$?" -ne 0 ]]; then
        printf "error building hammer"
        exit 1
    fi
}

[ -d "${BUILD}" ] || mkdir -p "${BUILD}"

PROGRAM="${BUILD}"/zwei
LIBRARY="${BUILD}"/libzwei.dylib

if ${PROFILING}; then
    PS4='T $(date "+%s.%N")\011 '
    set -x
fi

SCONS=${SCONS:-$(which scons)}
[[ -x "${SCONS}" ]] || die "missing scons or SCONS env variable"

set -e
must_compile_hammer

must_compile_osx -fvisibility=hidden -shared \
                 "${HERE}"/src/spdr_unit.cpp \
                 "${HERE}"/src/zwei_lib_osx_unit.cpp \
                 -L"${BUILD}"/lib -lhammer \
                 -o "${LIBRARY}" \
    && printf "SHARED_LIBRARY\t%s\n" "${LIBRARY}"

must_compile_osx \
    "${HERE}"/src/spdr_unit.cpp \
    "${HERE}"/src/zwei_main_osx_unit.cpp \
    -o "${PROGRAM}" \
    && printf "PROGRAM\t%s\n" "${PROGRAM}"

(
    PROGRAM="${BUILD}"/shasum
    must_compile_osx \
        "${HERE}"/src/tests/shasum_unit.cpp \
        -DSHASUM_ASYNC=1 -DSHASUM_TRACING=0 \
        -o "${PROGRAM}" \
        && printf "PROGRAM\t%s\n" "${PROGRAM}"
)

(
    PROGRAM="${BUILD}"/shasum_sync
    must_compile_osx \
        "${HERE}"/src/tests/shasum_unit.cpp \
        -DSHASUM_ASYNC=0 -DSHASUM_TRACING=0 \
        -o "${PROGRAM}" \
        && printf "PROGRAM\t%s\n" "${PROGRAM}"
)
set +e

# beep
printf "\a"
set +x
