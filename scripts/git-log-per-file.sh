#!/usr/bin/env bash
HERE="$(dirname ${0})"

USAGE="${0} <top of the tree>"
TOP=${1:?${USAGE}}

git ls-files --full-name "${TOP}" \
    | uniq \
    | while read filepath;
do
    printf "%s\t%s\n" \
      "$(git log -1 --pretty=format:"%aI %h %<(79,trunc)%s" -n1 -- "${TOP}/$filepath")" \
      "$filepath" 
done
