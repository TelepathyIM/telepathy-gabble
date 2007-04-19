#!/bin/sh
set -e

autoreconf -i

./configure "$@"
