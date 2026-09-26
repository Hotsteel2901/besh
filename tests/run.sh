#!/bin/sh
# ================================================================
# besh differential + besh-only test runner
#
#   tests/cases/*        every file is run under both bash and besh;
#                        stdout+stderr are diffed.  A case passes only
#                        when the two shells agree byte for byte.
#   tests/only/*         besh-only cases.  bash cannot serve as a
#                        reference for zsh-inspired features
#                        (command_not_found_handler, fc, compgen …),
#                        so each file carries its expected output after
#                        a line containing exactly `--- expect ---`.
#
# Usage:  tests/run.sh          run everything
#         tests/run.sh -v       also print the diff for failures
#         BESH=./besh tests/run.sh
#
# Exits non-zero if any case fails.
# ================================================================
set -u

here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=$(dirname -- "$here")
BESH=${BESH:-$root/besh}
VERBOSE=0
[ "${1:-}" = "-v" ] && VERBOSE=1

if [ ! -x "$BESH" ]; then
    echo "tests: $BESH not found — run 'make' first" >&2
    exit 2
fi

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

pass=0
fail=0
failed=""

diffcase() {
    name=$1
    file=$here/cases/$name
    bash "$file" > "$tmp/a.$name" 2>&1
    "$BESH" "$file" > "$tmp/b.$name" 2>&1
    if cmp -s "$tmp/a.$name" "$tmp/b.$name"; then
        pass=$((pass + 1))
    else
        fail=$((fail + 1))
        failed="$failed $name"
        echo "FAIL(diff)  $name"
        [ "$VERBOSE" = 1 ] && diff -u "$tmp/a.$name" "$tmp/b.$name" | sed 's/^/    /'
    fi
}

onlycase() {
    name=$1
    file=$here/only/$name
    # split the file at the `--- expect ---` marker
    expected=$(awk '/^-+ *expect *-+$/{f=1;next} f' "$file")
    body=$(awk '/^-+ *expect *-+$/{exit} {print}' "$file")
    printf '%s\n' "$body" > "$tmp/body.$name"
    "$BESH" "$tmp/body.$name" > "$tmp/got.$name" 2>&1
    if [ "$(cat "$tmp/got.$name")" = "$expected" ]; then
        pass=$((pass + 1))
    else
        fail=$((fail + 1))
        failed="$failed $name"
        echo "FAIL(only)  $name"
        if [ "$VERBOSE" = 1 ]; then
            echo "    --- expected ---"
            printf '%s\n' "$expected" | sed 's/^/    /'
            echo "    --- got ---"
            sed 's/^/    /' "$tmp/got.$name"
        fi
    fi
}

echo "=== differential (bash vs besh) ==="
for f in "$here"/cases/*; do
    [ -f "$f" ] || continue
    diffcase "$(basename "$f")"
done

if [ -d "$here/only" ]; then
    echo "=== besh-only (zsh-inspired features) ==="
    for f in "$here"/only/*; do
        [ -f "$f" ] || continue
        onlycase "$(basename "$f")"
    done
fi

echo
echo "PASS=$pass FAIL=$fail"
[ -n "$failed" ] && echo "FAILED:$failed"
[ "$fail" -eq 0 ]
