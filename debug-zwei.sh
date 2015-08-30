#!/usr/bin/env bash
HERE="$(dirname ${0})"
source "${HERE}"/scripts/lib/user.sh

ZWEI_ARGS=(--root-dir "${HERE}")

if [[ $# -gt 0 ]]; then
    ZWEI_ARGS=(${@})
fi

LLDB=${LLDB:-$(which lldb)}
[ -x "${LLDB}" ] || die "missing lldb or LLDB env variable"

"${LLDB}" -o run "${HERE}"/builds/zwei -- --debug "${ZWEI_ARGS[@]}"
