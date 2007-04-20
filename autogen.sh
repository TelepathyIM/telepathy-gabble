#!/bin/sh
set -e

if test -z "$MAKE"
then
	MAKE=make
fi

( cd extensions && \
	TOP_SRCDIR=.. sh tools/update-spec-gen-am.sh _gen/spec-gen.am _gen )

autoreconf -i

./configure "$@"
