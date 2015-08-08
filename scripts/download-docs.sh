#!/usr/bin/env bash

# downloads all docs from source code

USAGE="${0} <top of the tree>"
TOP=${1:?${USAGE}}

grep --no-filename --recursive -e '\[http.*\]' "${TOP}"/src/* | \
    grep --only-matching \
     -e 'https://[^]]*' \
     -e 'http://[^]]*' \
    | \
    (
        cd "${TOP}"/docs
        while read url; do
            printf "Downloading %s\n" "${url}"
            curl '-#' -L -O "${url}" || exit 1
        done
    )
