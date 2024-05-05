#!/bin/sh

set -eu

cc -o cli lib.c cli.c
./cli

