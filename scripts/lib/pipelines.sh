function prepend_top_grep_pipeline() {
    local TOP="${1}"
    while read line ; do
        if [ "${line}" == "--" ]; then
            printf "%s\n" "${line}";
        else
            printf "%s/%s\n" "${TOP}" "${line}"
        fi
    done
}

function onlymatches_pipeline() {
    local ON="${1}"
    local REGEX="${2}"
    if [[ "${ON}" -ne 0 ]]; then
        grep -o -e "${REGEX}"
    else
        tee
    fi
}