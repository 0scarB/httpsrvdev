#!/bin/sh

set -eu

cc -ggdb -Wall -Werror -DTEST \
    -o test \
    httpsrvdev_lib.c test_framework.c
./test

