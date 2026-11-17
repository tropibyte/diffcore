#!/bin/sh
# Regenerates the expected outputs in tests/data/golden: for each NAME.old /
# NAME.new pair, NAME.diff is `diff -u --label a --label b` from GNU diff
# (the committed files come from GNU diffutils 3.10). Names starting with
# "u<N>_" use N lines of context instead of 3.
#
# Add a case by writing NAME.old and NAME.new and running this script. Pick
# inputs with only one minimal edit script (distinct lines near the
# changes), so the expected output does not depend on which of several
# equally short scripts a diff implementation happens to choose.

set -eu

dir=$(cd "$(dirname "$0")/../tests/data/golden" && pwd)
diff --version | head -n 1

for old in "$dir"/*.old; do
    name=$(basename "$old" .old)
    new="$dir/$name.new"
    case "$name" in
        u[0-9]*_*)
            context=${name%%_*}
            context=${context#u}
            ;;
        *)
            context=3
            ;;
    esac
    status=0
    diff -U "$context" --label a --label b "$old" "$new" > "$dir/$name.diff" || status=$?
    if [ "$status" -ne 1 ]; then
        echo "$name: expected diff to exit with 1, got $status" >&2
        exit 1
    fi
    echo "$name.diff"
done
