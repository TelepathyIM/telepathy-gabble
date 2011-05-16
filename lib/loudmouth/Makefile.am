noinst_LTLIBRARIES = libloudmouth.la

BUILT_SOURCES = \
  loudmouth-signals-marshal.list \
  loudmouth-signals-marshal.h \
  loudmouth-signals-marshal.c

OUR_SOURCES =                     \
  loudmouth.h                     \
  lm-types.h                      \
  lm-message.c                    \
  lm-message.h                    \
  lm-message-node.c               \
  lm-message-node.h               \
  lm-message-handler.c            \
  lm-message-handler.h            \
  lm-connection.c                 \
  lm-connection.h

libloudmouth_la_SOURCES = $(OUR_SOURCES) $(BUILT_SOURCES)
libloudmouth_la_LIBADD = @WOCKY_LIBS@
# filter out any -flags as we only need to include .a and .la files here:
# this is necessary because: 
# @@ substitutions are filtered out by automake when constructing *_DEPENDENCIES
# *_LIBADD contains the link flags, dynamic libs and static libs we want
# *_DEPENDENCIES _MUST NOT_ contain the link flags and dynamic libs 
# *_DEPENDENCIES _SHOULD_ contain static libs so we rebuild when they change
libloudmouth_la_DEPENDENCIES = \
	$(filter-out -%, $(libloudmouth_la_LIBADD))

# Coding style checks
check_c_sources = \
    $(OUR_SOURCES)

include $(top_srcdir)/tools/check-coding-style.mk
check-local: check-coding-style

CLEANFILES=$(BUILT_SOURCES)
dist-hook:
	$(shell for x in $(BUILT_SOURCES); do rm -f $(distdir)/$$x ; done)

loudmouth-signals-marshal.list: $(OUR_SOURCES) Makefile.am
	$(AM_V_GEN)( cd $(srcdir) && \
	sed -n -e 's/.*_loudmouth_signals_marshal_\([[:upper:][:digit:]]*__[[:upper:][:digit:]_]*\).*/\1/p' \
	$(OUR_SOURCES) ) \
	| sed -e 's/__/:/' -e 'y/_/,/' | sort -u > $@.tmp
	@if cmp -s $@.tmp $@; then \
		rm $@.tmp; \
		touch $@; \
	else \
		mv $@.tmp $@; \
	fi

%-signals-marshal.h: %-signals-marshal.list Makefile.am
	$(AM_V_GEN)glib-genmarshal --header --prefix=_$(subst -,_,$*)_signals_marshal $< > $@

%-signals-marshal.c: %-signals-marshal.list Makefile.am
	$(AM_V_GEN){ echo '#include "$*-signals-marshal.h"' && \
	glib-genmarshal --body --prefix=_$(subst -,_,$*)_signals_marshal $< ; \
	} > $@


AM_CFLAGS = $(ERROR_CFLAGS) $(GCOV_CFLAGS) @GLIB_CFLAGS@ @WOCKY_CFLAGS@

AM_LDFLAGS = $(GCOV_LIBS) @GLIB_LIBS@ @WOCKY_LIBS@
