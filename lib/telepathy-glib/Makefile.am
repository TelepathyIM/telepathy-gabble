tpgincludedir=$(includedir)/telepathy-1.0/telepathy-glib
genincludedir=$(tpgincludedir)/_gen

EXTRA_DIST = extra-gtkdoc.h

# This is a derived file but must be checked-in to Darcs anyway,
# for bootstrapping.
include spec-gen.am

STABLE_SPEC_GENERATED_SOURCES = \
    $(STABLE_SPEC_GENERATED_CS) \
    $(STABLE_SPEC_GENERATED_HS) \
    $(STABLE_SPEC_GENERATED_LISTS) \
    $(STABLE_SPEC_GLUE_HS)

UNSTABLE_SPEC_GENERATED_SOURCES = \
    $(UNSTABLE_SPEC_GENERATED_CS) \
    $(UNSTABLE_SPEC_GENERATED_HS) \
    $(UNSTABLE_SPEC_GENERATED_LISTS) \
    $(UNSTABLE_SPEC_GLUE_HS)

lib_LTLIBRARIES = libtelepathy-glib.la

# when it becomes stable enough to have a shared library:
#libtelepathy_glib_la_LDFLAGS = \
#    -version-info $(LT_CURRENT):$(LT_REVISION):$(LT_AGE) \
#    -export-symbols-regex '^tp_'
# but for now:
libtelepathy_glib_la_LDFLAGS = -static

if ENABLE_UNSTABLE
# interfaces which aren't stable enough for the shared library anyway
libtelepathy_glib_unstable_la_LDFLAGS = -static

lib_LTLIBRARIES += libtelepathy-glib-unstable.la
endif

tpginclude_HEADERS = \
    base-connection.h \
    base-connection-manager.h \
    channel-iface.h \
    channel-factory-iface.h \
    dbus.h \
    debug.h \
    debug-ansi.h \
    enums.h \
    errors.h \
    group-mixin.h \
    handle.h \
    handle-repo.h \
    handle-repo-static.h \
    handle-repo-dynamic.h \
    heap.h \
    interfaces.h \
    intset.h \
    properties-mixin.h \
    run.h \
    svc-channel.h \
    svc-connection.h \
    svc-connection-manager.h \
    svc-media-interfaces.h \
    svc-properties-interface.h \
    text-mixin.h \
    util.h

nodist_libtelepathy_glib_la_SOURCES = \
    _gen/signals-marshal.c \
    _gen/signals-marshal.h \
    _gen/signals-marshal.list \
    _gen/telepathy-errors.c \
    $(STABLE_SPEC_GENERATED_SOURCES)

nodist_geninclude_HEADERS = \
    _gen/telepathy-enums.h \
    _gen/telepathy-interfaces.h \
    _gen/telepathy-errors.h \
    $(STABLE_SPEC_GENERATED_HS)

BUILT_SOURCES = \
    $(nodist_libtelepathy_glib_la_SOURCES) \
    $(nodist_geninclude_HEADERS)

if ENABLE_UNSTABLE
nodist_libtelepathy_glib_unstable_la_SOURCES = \
    _gen/signals-marshal.c \
    _gen/signals-marshal.h \
    _gen/signals-marshal.list \
    _gen/telepathy-errors.c \
    $(UNSTABLE_SPEC_GENERATED_SOURCES)

nodist_geninclude_HEADERS += $(UNSTABLE_SPEC_GENERATED_HS)

BUILT_SOURCES += $(nodist_libtelepathy_glib_unstable_la_SOURCES)
endif

CLEANFILES = $(BUILT_SOURCES) _gen/.exists
clean-local:
	-rmdir _gen

check_c_sources = \
    $(tpginclude_HEADERS) \
    $(libtelepathy_glib_la_SOURCES)
include $(top_srcdir)/check-coding-style.mk

libtelepathy_glib_la_SOURCES = \
    base-connection.c \
    base-connection-manager.c \
    dbus.c \
    debug.c \
    internal-debug.h \
    errors.c \
    group-mixin.c \
    handle.c \
    internal-handle-repo.h \
    handle-repo.c \
    handle-repo-dynamic.c \
    handle-repo-static.c \
    handle-set.c \
    heap.c \
    intset.c \
    channel-iface.c \
    channel-factory-iface.c \
    properties-mixin.c \
    run.c \
    signals-marshal.list \
    text-mixin.c \
    util.c

AM_CFLAGS = $(ERROR_CFLAGS) @DBUS_CFLAGS@ @GLIB_CFLAGS@ @HANDLE_LEAK_DEBUG_CFLAGS@ @COVERAGE_CFLAGS@ -I.. -I$(srcdir)/..
AM_LDFLAGS = @DBUS_LIBS@ @GLIB_LIBS@

# Generated stuff

DROP_NAMESPACE = sed -e 's@xmlns:tp="http://telepathy\.freedesktop\.org/wiki/DbusSpec.extensions-v0"@@g'
XSLTPROCFLAGS = --nonet --novalid

_gen/.exists:
	$(INSTALL) -d _gen
	touch $@

_gen/telepathy-enums.h: ../spec/all.xml \
	$(SPEC_INTERFACE_XMLS) ../tools/c-constants-generator.xsl _gen/.exists
	$(XSLTPROC) --xinclude $(XSLTPROCFLAGS) \
		$(srcdir)/../tools/c-constants-generator.xsl \
		$(srcdir)/../spec/all.xml > $@

_gen/telepathy-interfaces.h: ../spec/all.xml \
	$(SPEC_INTERFACE_XMLS) ../tools/c-interfaces-generator.xsl _gen/.exists
	$(XSLTPROC) --xinclude $(XSLTPROCFLAGS) \
		$(srcdir)/../tools/c-interfaces-generator.xsl \
		$(srcdir)/../spec/all.xml > $@

_gen/telepathy-errors.c: ../spec/errors.xml \
	../tools/glib-errors-enum-body.xsl _gen/.exists
	$(XSLTPROC) $(XSLTPROCFLAGS) \
		$(srcdir)/../tools/glib-errors-enum-body.xsl \
		$< > $@

_gen/telepathy-errors.h: ../spec/errors.xml \
	../tools/glib-errors-enum-header.xsl _gen/.exists
	$(XSLTPROC) $(XSLTPROCFLAGS) \
		$(srcdir)/../tools/glib-errors-enum-header.xsl \
		$< > $@

_gen/%.xml: ../spec/%.xml ../tools/spec-to-introspect.xsl _gen/.exists
	$(XSLTPROC) $(XSLTPROCFLAGS) \
		$(srcdir)/../tools/spec-to-introspect.xsl $< \
		| $(DROP_NAMESPACE) > $@

_gen/async-%.xml: _gen/%.xml ../tools/make-all-async.xsl _gen/.exists
	$(XSLTPROC) $(XSLTPROCFLAGS) $(srcdir)/../tools/make-all-async.xsl $< > $@

_gen/svc-%-glue.h: _gen/async-%.xml _gen/.exists
	$(DBUS_BINDING_TOOL) --mode=glib-server --output=$@ \
		--prefix=tp_svc_`echo $* | tr A-Z a-z` $<

_gen/svc-%.c _gen/svc-%.h _gen/svc-%-signals-marshal.list: ../spec/%.xml ../tools/genginterface.py _gen/.exists
	$(PYTHON) $(srcdir)/../tools/genginterface.py $< \
	TpSvc`echo $* | tr -d _` _gen/svc-$* _tp

_gen/signals-marshal.list: signals-marshal.list $(SPEC_GENERATED_LISTS)
	sort $^ | uniq > $@

_gen/signals-marshal.h: _gen/signals-marshal.list _gen/.exists
	$(GLIB_GENMARSHAL) --header --prefix=_tp_marshal $< > $@

_gen/signals-marshal.c: _gen/signals-marshal.list _gen/.exists
	$(GLIB_GENMARSHAL) --body --prefix=_tp_marshal $< > $@
