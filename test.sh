#!/usr/bin/env bash
set -o pipefail
set -e

HERE="$(dirname ${0})"

"${HERE}"/build.sh | grep '^PROGRAM' | cut -f2 | \
    while read program ; do
        printf "running ${program}\n"
        ${program} || exit 1
    done

if [[ $? -ne 0 ]]; then
   exit 1
fi

##
# some reports

printf -- "# Balance Sheet\n"
"${HERE}"/scripts/balance.sh "${HERE}"

printf -- "\n# Features\n"
"${HERE}"/scripts/features.sh --only-matches "${HERE}"

printf -- "\n# Issues\n"
"${HERE}"/scripts/todo.sh "${HERE}"
