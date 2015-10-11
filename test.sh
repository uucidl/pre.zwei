#!/usr/bin/env bash
set -o pipefail

HERE="$(dirname ${0})"

set -e
"${HERE}"/build.sh | grep '^PROGRAM' | cut -f2 | \
    while read program ; do
        printf "running ${program}\n"
        ${program} || exit 1
    done
set +e

if [[ $? -ne 0 ]]; then
   exit 1
fi

##
# some reports

printf -- "# Balance Sheet\n"
"${HERE}"/scripts/balance.sh "${HERE}"

printf -- "\n# Features Summary\n"
"${HERE}"/scripts/features.sh --only-matches "${HERE}"

printf -- "\n# Todo Summary\n"
"${HERE}"/scripts/todo.sh --only-matches "${HERE}"

printf -- "\n# Features\n"
"${HERE}"/scripts/features.sh "${HERE}"

printf -- "\n# Issues\n"
"${HERE}"/scripts/todo.sh "${HERE}"
