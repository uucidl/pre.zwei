#!/usr/bin/env bash

USAGE="${0} <top of the tree>"
TOP=${1:?${USAGE}}
ATOP=$(cd ${TOP} && pwd)

git --no-pager -C "${TOP}" grep --full-name -n -A 3 'TODO' -- src/* | while read line ; do
                                                                          if [ "${line}" == "--" ]; then
                                                                              printf "%s\n" "${line}";
                                                                          else
                                                                              printf "%s/%s\n" "${TOP}" "${line}"
                                                                          fi
                                                                      done
