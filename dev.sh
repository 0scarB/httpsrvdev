#!/bin/sh

set -eu

project_dir="$( dirname "$( realpath "$0" )" )"

cc -ggdb -DDEV -Wall -Werror \
    -o "$project_dir/cli" \
    "$project_dir/lib.c" "$project_dir/cli.c"
"$project_dir/cli" $@

