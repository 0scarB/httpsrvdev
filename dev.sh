#!/bin/sh

set -eu

cc -ggdb -Wall -Werror -o cli lib.c cli.c
./cli $@

