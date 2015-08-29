#!/usr/bin/env sh
HERE="$(dirname ${0})"
source "${HERE}"/lib/user.sh

# formats source code

USAGE="${0} <top of the tree>"
TOP=${1:?${USAGE}}

CLANG_FORMAT=${CLANG_FORMAT:-$(which clang-format)}
[ -x "${CLANG_FORMAT}" ] || die "missing CLANG_FORMAT env variable"

function find_all_sources() {
    find "${TOP}"/include \( \
                  -name "*.hpp" \
                  -o -name "*.hh" \
                  -o -name "*.h" \
    \) -print
    find "${TOP}"/src \( \
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
find_all_sources | grep -v '.*' | while read f; do
    "${CLANG_FORMAT}" -i "${f}"
done
