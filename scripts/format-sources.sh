#!/usr/bin/env bash
HERE="$(dirname ${0})"
source "${HERE}"/lib/user.sh

# formats source code

USAGE="${0} <top of the tree>"
TOP=${1:?${USAGE}}

CLANG_FORMAT=${CLANG_FORMAT:-$(which clang-format)}
[ -x "${CLANG_FORMAT}" ] || die "missing CLANG_FORMAT env variable"

function find_all_sources() {
    [[ -d "${TOP}/include" ]] && find "${TOP}"/include \( \
                  -name "*.hpp" \
                  -o -name "*.hh" \
                  -o -name "*.h" \
    \) -print
    [[ -d "${TOP}/src" ]] && find "${TOP}"/src \( \
                  -name "*.cpp" -o -name "*.hpp" \
                  -o -name "*.cc" -o -name "*.hh" \
                  -o -name "*.c" -o -name "*.h" \
                  -o -name "*.js" \
    \) -print
    [ -d "${TOP}"/examples ] && find "${TOP}"/examples \( \
                  -name "*.cpp" -o -name "*.hpp" \
                  -o -name "*.cc" -o -name "*.hh" \
                  -o -name "*.c" -o -name "*.h" \
                  -o -name "*.js" \
    \) -print
}

set -e
find_all_sources | grep -v -e '\.rl\..*$' |
    while read f; do
        "${CLANG_FORMAT}" -style=file -i "${f}"
done
