Old unchanged files tend to work yet may not been up to date with respect to a
project as a whole. Therefore look for old, unchanged files, and modernize
them every now and then:

$(scripts/git-log-per-file.sh . | sort -r)


