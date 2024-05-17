#!/bin/sh

set -eu

project_dir="$( dirname "$( realpath "$0" )" )"

cc -ggdb -DDEV -Wall -Werror \
    -o "$project_dir/httpsrvdev-dev" \
    "$project_dir/httpsrvdev_lib.c" "$project_dir/httpsrvdev_cli.c"

# TODO: Fix overflow detected when optimizations are turned on!!!!!!
cc -Wall -Werror -Wno-unused-result \
    -o "$project_dir/httpsrvdev" \
    "$project_dir/httpsrvdev_lib.c" "$project_dir/httpsrvdev_cli.c"

"$project_dir/httpsrvdev-dev" $@

