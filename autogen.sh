#!/bin/sh
set -e

if test -z "$MAKE"
then
	MAKE=make
fi

( cd extensions && \
	TOP_SRCDIR=.. sh tools/update-spec-gen-am.sh _gen/spec-gen.am _gen )

autoreconf -i

run_configure=true
for arg in $*; do
    case $arg in
        --no-configure)
            run_configure=false
            ;;
        *)
            ;;
    esac
done

if test $run_configure = true; then
    ./configure "$@"
fi
