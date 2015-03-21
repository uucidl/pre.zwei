#!/usr/bin/env sh
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

"${HERE}"/scripts/balance.sh "${HERE}"
"${HERE}"/scripts/todo.sh "${HERE}"
