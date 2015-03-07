#!/usr/bin/env bash
HERE="$(dirname ${0})"
lldb -o run "${HERE}"/builds/zwei -- --root-dir "${HERE}"
