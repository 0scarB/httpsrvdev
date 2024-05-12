#!/bin/sh

set -eu

cc -ggdb -DDEV -Wall -Werror -o cli lib.c cli.c
./cli $@

