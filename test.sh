#!/usr/bin/env sh
HERE="$(dirname ${0})"

"${HERE}"/build.sh | grep '^PROGRAM' | cut -f2 | while read program ; do
                                                     printf "running ${program}\n"
                                                     ${program} || exit 1
                                                 done
