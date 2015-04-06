#!/usr/bin/env sh
HERE="$(dirname ${0})"
source "${HERE}"/scripts/lib/user.sh

CLANG_FORMAT=${CLANG_FORMAT:-$(which clang-format)}
[ -x "${CLANG_FORMAT}" ] || die "missing CLANG_FORMAT env variable"

function find_all_sources() {
    find src \( \
                  -name "*.cpp" -o -name "*.hpp" \
                  -name "*.cc" -o -name "*.hh" \
                  -name "*.c" -o -name "*.h" \
                  -o -name "*.js" \
    \) -print
}

find_all_sources | while read f; do
                       "${CLANG_FORMAT}" -i "${f}"
                   done
