#!/usr/bin/env bash
set -u

HERE="$(dirname ${0})"
AHERE="$(cd "${HERE}" ; pwd)"

os=${os:-osx}

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
	--os)
	    shift
	    if [[ $# -eq 0 ]]; then
		printf "expected an os\n"
		exit 1
	    fi
	    os=${1}
	    shift
	    ;;
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
            printf -- "Usage: %s [--os <linux,osx*>][--profile-build][--config [%s]]\n" "${0}" "$(IFS="|" ; echo "${CONFIG_LIST[*]}")"
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

function must_compile_osx_cxx()
{
    local cflags
    local cxxflags
    local PLATFORM_SDK="/Applications/Xcode.app/Contents//Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX10.10.sdk/usr/include/"

    cflags=("${cflags[@]}" -mmacosx-version-min=10.8)
    cflags=("${cflags[@]}" -isystem "${PLATFORM_SDK}")
    cxxflags=("${cxxflags[@]}" -stdlib=libc++)

    cflags=("${cflags[@]}" "-DCOMPILER_LLVM")
    cxxflags=("${cflags[@]}" "-fno-rtti" "-fno-exceptions")
    cflags=("${cflags[@]}" "-isystem" "${HERE}"/include)
    cflags=("${cflags[@]}" "-Wall" "-Wextra" "-Wshorten-64-to-32" "-Werror")
    cflags=("${cflags[@]}" "-Wno-padded" "-Wno-unused-parameter")
    cflags=("${cflags[@]}" -g -gdwarf-3 -fsanitize=address)
    cxxflags=("${cxxflags[@]}" "-std=c++11")

    if [[ "${CONFIG}" == "development" ]]; then
        cflags=("${cflags[@]}" -DZWEI_SLOW -DZWEI_INTERNAL)
    fi

    if [[ "${CONFIG}" == "release" ]]; then
        cflags=("${cflags[@]}" -O3 -DZWEI_SLOW=0 -DZWEI_INTERNAL=0)
    fi

    clang++ "${cflags[@]}" "${cxxflags[@]}" "$@"
    if [[ "$?" -ne 0 ]]; then
        exit 1
    fi
}

function must_compile_linux_cxx()
{
    local cflags
    local cxxflags
    cflags=("${cflags[@]}" "-DCOMPILER_GCC")
    cxxflags=("${cflags[@]}" "-fno-rtti" "-fno-exceptions")
    cflags=("${cflags[@]}" "-isystem" "${HERE}"/include)
    cflags=("${cflags[@]}" "-Wall" "-Wextra" "-Werror")
    cflags=("${cflags[@]}" "-Wno-padded" "-Wno-unused-parameter")
    cflags=("${cflags[@]}" -g -gdwarf-3 -fsanitize=address)
    cxxflags=("${cxxflags[@]}" "-std=c++11")

    if [[ "${CONFIG}" == "development" ]]; then
        cflags=("${cflags[@]}" -DZWEI_SLOW -DZWEI_INTERNAL)
    fi

    if [[ "${CONFIG}" == "release" ]]; then
        cflags=("${cflags[@]}" -O3 -DZWEI_SLOW=0 -DZWEI_INTERNAL=0)
    fi

    g++ "${cflags[@]}" "${cxxflags[@]}" "$@" -ldl
    if [[ "$?" -ne 0 ]]; then
        exit 1
    fi
}

function must_compile_osx_c()
{
    local cflags
    local cxxflags
    local PLATFORM_SDK="/Applications/Xcode.app/Contents//Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX10.10.sdk/usr/include/"

    cflags=("${cflags[@]}" -mmacosx-version-min=10.8)
    cflags=("${cflags[@]}" -isystem "${PLATFORM_SDK}")

    cflags=("${cflags[@]}" "-DCOMPILER_LLVM")
    cflags=("${cflags[@]}" "-isystem" "${HERE}"/include)
    cflags=("${cflags[@]}" "-Wall" "-Wextra" "-Wshorten-64-to-32" "-Werror")
    cflags=("${cflags[@]}" "-Wno-padded" "-Wno-unused-parameter")
    cflags=("${cflags[@]}" -g -gdwarf-3 -fsanitize=address)

    if [[ "${CONFIG}" == "development" ]]; then
        cflags=("${cflags[@]}" -DZWEI_SLOW -DZWEI_INTERNAL)
    fi

    if [[ "${CONFIG}" == "release" ]]; then
        cflags=("${cflags[@]}" -O3 -DZWEI_SLOW=0 -DZWEI_INTERNAL=0)
    fi

    clang "${cflags[@]}" "$@"
    if [[ "$?" -ne 0 ]]; then
        exit 1
    fi
}

function must_compile_linux_c()
{
    local cflags
    cflags=("${cflags[@]}" "-DCOMPILER_GCC")
    cflags=("${cflags[@]}" "-isystem" "${HERE}"/include)
    cflags=("${cflags[@]}" "-Wall" "-Wextra" "-Werror")
    cflags=("${cflags[@]}" "-Wno-padded" "-Wno-unused-parameter")
    cflags=("${cflags[@]}" -g -gdwarf-3 -fsanitize=address)

    if [[ "${CONFIG}" == "development" ]]; then
        cflags=("${cflags[@]}" -DZWEI_SLOW -DZWEI_INTERNAL)
    fi

    if [[ "${CONFIG}" == "release" ]]; then
        cflags=("${cflags[@]}" -O3 -DZWEI_SLOW=0 -DZWEI_INTERNAL=0)
    fi

    gcc "${cflags[@]}" "$@" -ldl
    if [[ "$?" -ne 0 ]]; then
        exit 1
    fi
}

function compile() {
    case "${os}" in
	osx) must_compile_osx_cxx "$@" ;;
	linux) must_compile_linux_cxx "$@" ;;
	*) die "Unknown os: '${os}'" ;;
    esac
}

function compile_c() {
    case "${os}" in
	osx) must_compile_osx_c "$@" ;;
	linux) must_compile_linux_c "$@" ;;
	*) die "Unknown os: '${os}'" ;;
    esac
}

function must_compile_hammer()
{
    local HAMMER_DIR
    local OUT
    local OUTDIR

    HAMMER_DIR="${HERE}"/modules/hammer
    if false; then
	[[ -x "${SCONS}" ]] || die "missing scons or SCONS env variable"
	"${SCONS}" -C "${HAMMER_DIR}" \
		   prefix="/" DESTDIR="${ABUILD}" bindings=cpp \
		   install
    else
	compile_c -c -I"${HERE}" -D_GNU_SOURCE "${HERE}"/src/hammer_unit.c -o "${BUILD}"/hammer.o
	[[ -d "${BUILD}"/lib ]] || mkdir "${BUILD}"/lib
	OUT="${BUILD}"/lib/libhammer.a
	ar r "${OUT}" "${BUILD}"/hammer.o
	ranlib "${OUT}"
	printf "STATIC_LIBRARY\t%s\n" "${OUT}"
	OUTDIR="${BUILD}"/include/hammer

	[[ -d "${OUTDIR}" ]] || mkdir -p "${OUTDIR}"
	for hf in allocator.h compiler_specifics.h glue.h hammer.h; do
	    cp "${HAMMER_DIR}"/src/"${hf}" "${OUTDIR}"
	done
	printf "HEADERS\t%s\n" "${OUTDIR}"
    fi
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

set -e
must_compile_hammer

(
    PROGRAM="${BUILD}"/tests
    compile \
	"${HERE}"/src/tests/tests_unit.cpp \
	-o "${PROGRAM}" \
	&& printf "PROGRAM\t%s\n" "${PROGRAM}"
)

[[ "${os}" != "osx" ]] || (must_compile_osx \
    -DZWEI_API_DLL=zw_dll_exported \
    -fvisibility=hidden -shared \
    "${HERE}"/src/spdr_unit.cpp \
    "${HERE}"/src/zwei_lib_osx_unit.cpp \
    -L"${BUILD}"/lib -lhammer \
    -o "${LIBRARY}" \
    && printf "SHARED_LIBRARY\t%s\n" "${LIBRARY}")

[[ "${os}" != "osx" ]] || (must_compile_osx \
    "${HERE}"/src/spdr_unit.cpp \
    "${HERE}"/src/zwei_main_osx_unit.cpp \
    -o "${PROGRAM}" \
    && printf "PROGRAM\t%s\n" "${PROGRAM}")

[[ "${os}" != "linux" ]] || \
    (must_compile_linux_cxx \
	 "${HERE}"/src/spdr_unit.cpp \
	 -Wno-error \
	 -c -o "${BUILD}"/spdr.o \
	 &&
	 must_compile_linux_cxx \
	     "${BUILD}"/spdr.o \
	     "${HERE}"/src/zwei_main_linux_unit.cpp \
	     -L"${BUILD}"/lib -lhammer \
	     -o "$PROGRAM" \
	     && printf "PROGRAM\t%s\n" "${PROGRAM}")

(
    PROGRAM="${BUILD}"/shasum
    [[ "${os}" != "osx" ]] || (must_compile_osx \
        "${HERE}"/src/tests/shasum_unit.cpp \
        -DSHASUM_ASYNC=1 -DSHASUM_TRACING=0 \
        -o "${PROGRAM}" \
        && printf "PROGRAM\t%s\n" "${PROGRAM}")
)


(
    PROGRAM="${BUILD}"/shasum_sync
    [[ "{$os}" != "osx" ]] || (must_compile_osx \
        "${HERE}"/src/tests/shasum_unit.cpp \
        -DSHASUM_ASYNC=0 -DSHASUM_TRACING=0 \
        -o "${PROGRAM}" \
        && printf "PROGRAM\t%s\n" "${PROGRAM}")
)

(
    PROGRAM="${BUILD}"/socks_rfc1928
    compile \
        "${HERE}"/src/socks_rfc1928.cpp \
        -DSOCKS_RFC1928_MAIN=1 \
        -o "${PROGRAM}" \
	&& printf "PROGRAM\t%s\n" "${PROGRAM}"
)

(
    PROGRAM="${BUILD}"/sftp
    compile \
        "${HERE}"/src/sftp.cpp \
        -DSFTP_MAIN=1 \
        -DSFTP_API=extern \
        -o "${PROGRAM}" \
	&& printf "PROGRAM\t%s\n" "${PROGRAM}"
)
set +e

# beep
printf "\a"
set +x
