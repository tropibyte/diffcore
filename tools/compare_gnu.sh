#!/bin/sh
# Compares diffcore with GNU diff and GNU patch on pseudo-random file pairs.
# Linux (or any system with GNU diffutils and patch); not part of ctest.
#
#   tools/compare_gnu.sh [path/to/diffcore]      (default: build/dev/diffcore)
#
# The pairs come from `diffcore bench --save` over several sizes, change
# rates, vocabularies (few distinct lines means many equally short edit
# scripts) and fixed seeds; some lose their final newline. For each pair:
#   a. GNU patch applies diffcore's diff to OLD and gives NEW;
#   b. `diffcore patch` applies GNU `diff -u` output to OLD and gives NEW;
#   c. diffcore's diff has as many - and + lines as `diff --minimal -u`;
#   d. diffcore's diff is byte-identical to `diff -u` (counted, not
#      required: when several edit scripts are equally short, the two
#      programs may pick different ones).
# Exits with 1 if any of a-c fails.

set -eu

cli=${1:-build/dev/diffcore}
if [ ! -x "$cli" ]; then
    echo "no diffcore executable at $cli; build it or pass its path" >&2
    exit 2
fi
case "$cli" in
    /*) ;;
    *) cli="$(pwd)/$cli" ;;
esac

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work"

pairs=0
fail_a=0
fail_b=0
fail_c=0
identical=0

# "- count + count" of a unified diff on standard input.
changes() {
    awk 'NR > 2 && /^-/ { d++ } NR > 2 && /^\+/ { a++ } END { printf "-%d +%d\n", d, a }'
}

drop_final_newline() {
    if [ -s "$1" ]; then
        truncate -s -1 "$1"
    fi
}

for lines in 0 1 6 40 250 1500; do
    for rate in 0.02 0.1 0.3 0.6; do
        for vocab in 0 4 30; do
            for seed in 1 2 3 4 5; do
                "$cli" bench --lines "$lines" --change-rate "$rate" --vocab "$vocab" --seed "$seed" \
                    --save old new > /dev/null
                case "$seed" in
                    4) drop_final_newline old ;;
                    5) drop_final_newline new ;;
                esac
                pairs=$((pairs + 1))
                what="lines=$lines rate=$rate vocab=$vocab seed=$seed"

                status=0
                "$cli" diff --label a --label b old new > ours.diff || status=$?
                if [ "$status" -gt 1 ]; then
                    echo "diffcore diff failed ($status): $what" >&2
                    fail_a=$((fail_a + 1))
                    continue
                fi
                diff -u --label a --label b old new > gnu.diff || true
                diff --minimal -u --label a --label b old new > minimal.diff || true

                # a. GNU patch applies our diff.
                if [ -s ours.diff ]; then
                    if ! patch -s -f -o a.out old ours.diff > /dev/null 2>&1 || ! cmp -s a.out new; then
                        echo "a: GNU patch could not apply diffcore's diff: $what" >&2
                        fail_a=$((fail_a + 1))
                    fi
                elif ! cmp -s old new; then
                    echo "a: diffcore found no differences: $what" >&2
                    fail_a=$((fail_a + 1))
                fi

                # b. diffcore applies GNU's diff.
                if [ -s gnu.diff ]; then
                    if ! "$cli" patch -o b.out old gnu.diff > /dev/null 2>&1 || ! cmp -s b.out new; then
                        echo "b: diffcore patch could not apply GNU's diff: $what" >&2
                        fail_b=$((fail_b + 1))
                    fi
                fi

                # c. Same counts as a minimal GNU diff.
                if [ "$(changes < ours.diff)" != "$(changes < minimal.diff)" ]; then
                    echo "c: $(changes < ours.diff) vs diff --minimal $(changes < minimal.diff): $what" >&2
                    fail_c=$((fail_c + 1))
                fi

                # d. Byte-identical to diff -u.
                if cmp -s ours.diff gnu.diff; then
                    identical=$((identical + 1))
                fi
            done
        done
    done
done

diff --version | head -n 1
patch --version | head -n 1
echo "pairs compared:                                $pairs"
echo "a. GNU patch failed on diffcore's diff:        $fail_a"
echo "b. diffcore patch failed on GNU's diff:        $fail_b"
echo "c. +/- counts differ from diff --minimal:      $fail_c"
echo "d. byte-identical to diff -u:                  $identical of $pairs"

if [ $((fail_a + fail_b + fail_c)) -ne 0 ]; then
    exit 1
fi
