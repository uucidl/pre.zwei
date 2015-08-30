#!/usr/bin/env bash
HERE="$(dirname ${0})"
source "${HERE}"/lib/user.sh

# generates source code

USAGE="${0} <top of the tree>"
TOP=${1:?${USAGE}}

## nothing to do
