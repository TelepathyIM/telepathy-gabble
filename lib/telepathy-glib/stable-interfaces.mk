svc_headers = \
    svc-channel.h \
    svc-connection.h \
    svc-connection-manager.h \
    svc-media-interfaces.h \
    svc-properties-interface.h

extract_interfaces_expr = \
's@^.include <telepathy-glib/_gen/svc-\([a-zA-Z_]*\)\.h>$$@\1@p'

_gen/stable-interfaces.txt: $(svc_headers)
	test -d _gen || mkdir _gen
	sed -ne $(extract_interfaces_expr) $^ > $@
