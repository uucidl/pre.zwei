#!/usr/bin/env bash

# generates source code

USAGE="${0} <top of the tree>"
TOP=${1:?${USAGE}}

function die() {
    msg=$1
    printf "%s\n" "$msg" >&2
    exit 1
}

RAGEL=${RAGEL:-$(which ragel)}
[ -x "${RAGEL}" ] || die "missing ragel or RAGEL env variable"


"${RAGEL}" "${TOP}"/src/adhoc_parser.rl.c -G2 -o "${TOP}"/src-generated/adhoc_parser.c
