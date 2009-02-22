QUIET_GEN = $(Q:@=@echo '  GEN   '$@;)

noinst_LTLIBRARIES = libgibber.la

BUILT_SOURCES = \
  gibber-signals-marshal.list \
  gibber-signals-marshal.h \
  gibber-signals-marshal.c

IMPORTED_SOURCES =                \
  asyncns.c                       \
  asyncns.h

OUR_SOURCES =                     \
  gibber-debug.c                  \
  gibber-debug.h                  \
  gibber-transport.c              \
  gibber-transport.h              \
  gibber-fd-transport.c           \
  gibber-fd-transport.h           \
  gibber-tcp-transport.c          \
  gibber-tcp-transport.h          \
  gibber-unix-transport.c         \
  gibber-unix-transport.h         \
  gibber-linklocal-transport.c    \
  gibber-linklocal-transport.h    \
  gibber-listener.c               \
  gibber-listener.h               \
  gibber-resolver.c               \
  gibber-resolver.h               \
  gibber-linklocal-transport.c    \
  gibber-linklocal-transport.h    \
  gibber-util.h                   \
  gibber-util.c

libgibber_la_SOURCES = $(OUR_SOURCES) $(BUILT_SOURCES) $(IMPORTED_SOURCES)

# Coding style checks
check_c_sources = \
    $(OUR_SOURCES)

include $(top_srcdir)/tools/check-coding-style.mk
check-local: check-coding-style

CLEANFILES=$(BUILT_SOURCES)
dist-hook:
	$(shell for x in $(BUILT_SOURCES); do rm -f $(distdir)/$$x ; done)

gibber-signals-marshal.list: $(OUR_SOURCES) Makefile.am
	$(QUIET_GEN)( cd $(srcdir) && \
	sed -n -e 's/.*_gibber_signals_marshal_\([[:upper:][:digit:]]*__[[:upper:][:digit:]_]*\).*/\1/p' \
	$(OUR_SOURCES) ) \
	| sed -e 's/__/:/' -e 'y/_/,/' | sort -u > $@.tmp
	if cmp -s $@.tmp $@; then \
		rm $@.tmp; \
		touch $@; \
	else \
		mv $@.tmp $@; \
	fi

%-signals-marshal.h: %-signals-marshal.list Makefile.am
	$(QUIET_GEN)glib-genmarshal --header --prefix=_$(subst -,_,$*)_signals_marshal $< > $@

%-signals-marshal.c: %-signals-marshal.list Makefile.am
	$(QUIET_GEN){ echo '#include "$*-signals-marshal.h"' && \
	glib-genmarshal --body --prefix=_$(subst -,_,$*)_signals_marshal $< ; \
	} > $@


AM_CFLAGS = $(ERROR_CFLAGS) $(GCOV_CFLAGS) @GLIB_CFLAGS@

AM_LDFLAGS = $(GCOV_LIBS) @GLIB_LIBS@ @RESOLV_LIBS@

# rules for making the glib enum objects
%-enumtypes.h: %.h Makefile.in
	$(QUIET_GEN)glib-mkenums \
	--fhead "#ifndef __$(shell echo $* | tr [:lower:]- [:upper:]_)_ENUM_TYPES_H__\n#define __$(shell echo $* | tr [:lower:]- [:upper:]_)_ENUM_TYPES_H__\n\n#include <glib-object.h>\n\nG_BEGIN_DECLS\n" \
	--fprod "/* enumerations from \"@filename@\" */\n" \
	--vhead "GType @enum_name@_get_type (void);\n#define $(shell echo $* | tr [:lower:]- [:upper:]_ | sed 's/_.*//')_TYPE_@ENUMSHORT@ (@enum_name@_get_type())\n"         \
	--ftail "G_END_DECLS\n\n#endif /* __$(shell echo $* | tr [:lower:]- [:upper:]_)_ENUM_TYPES_H__ */" \
	$< > $@

%-enumtypes.c: %.h Makefile.in
	$(QUIET_GEN)glib-mkenums \
	--fhead "#include <$*.h>\n#include <$*-enumtypes.h>" \
	--fprod "\n/* enumerations from \"@filename@\" */" \
	--vhead "GType\n@enum_name@_get_type (void)\n{\n  static GType etype = 0;\n  if (etype == 0) {\n    static const G@Type@Value values[] = {"     \
	--vprod "      { @VALUENAME@, \"@VALUENAME@\", \"@VALUENAME@\" }," \
	--vtail "      { 0, NULL, NULL }\n    };\n    etype = g_@type@_register_static (\"@EnumName@\", values);\n  }\n  return etype;\n}\n" \
	$< > $@
