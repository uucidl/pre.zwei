#!/usr/bin/env bash
HERE="$(dirname ${0})"
ZWEI_ARGS=(--root-dir "${HERE}")

if [[ $# -gt 0 ]]; then
    ZWEI_ARGS=(${@})
fi

lldb -o run "${HERE}"/builds/zwei -- "${ZWEI_ARGS[@]}"
