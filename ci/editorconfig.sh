#!/bin/sh
set -eu
for f in $(git ls-tree -r HEAD --name-only); do
    if [ "$f" = "${f%.out}" ]  &&
        [ "$f" = "${f%.data}" ] &&
        [ "$f" = "${f%.png}" ] &&
        [ "$(dirname "$f")" != "src/test/regress/output" ]
    then
        # Trim trailing whitespace
        sed -i".backup" -e 's/[[:space:]]*$//' "./$f"
        rm "./$f.backup"
        # Add final newline if not there
        if [ -n "$(tail -c1 "$f")" ]; then
            echo >> "$f"
        fi
    fi
done
