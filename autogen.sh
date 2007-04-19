#!/bin/sh
set -e

gtkdocize

( cd lib/spec && sh ../tools/update-spec-gen-am.sh spec-gen.am )
make -C lib/telepathy-glib -f stable-interfaces.mk _gen/stable-interfaces.txt
( cd lib/telepathy-glib && sh ../tools/update-spec-gen-am.sh spec-gen.am _gen _gen/stable-interfaces.txt )

autoreconf -i

./configure --enable-gtk-doc --enable-handle-leak-debug "$@"
