tpgincludedir=$(includedir)/telepathy-1.0/telepathy-glib
genincludedir=$(tpgincludedir)/_gen

unstableincludedir=$(includedir)/telepathy-unstable/telepathy-glib
unstablegenincludedir=$(unstableincludedir)/_gen

EXTRA_DIST = extra-gtkdoc.h

# Also used to generate stable-interfaces.txt, which generates spec-gen.am
include stable-interfaces.mk

# This is a derived file but must be checked-in to Darcs anyway,
# for bootstrapping.
include _gen/spec-gen.am

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

lib_LTLIBRARIES = libtelepathy-glib.la libtelepathy-glib-unstable.la
noinst_LTLIBRARIES = libtelepathy-glib-internal.la

# libtelepathy-glib is just a shared version of libtelepathy-glib-internal.
# The static version is necessary because one of the tests wants to use
# internal symbols (_tp_debug) which are made invisible by GNU ld.
libtelepathy_glib_la_LDFLAGS = \
    -version-info $(LT_CURRENT):$(LT_REVISION):$(LT_AGE) \
    -export-symbols-regex '^tp_'
libtelepathy_glib_la_LIBADD = libtelepathy-glib-internal.la
libtelepathy_glib_la_SOURCES =

# interfaces which aren't stable enough for the shared library anyway
libtelepathy_glib_unstable_la_LDFLAGS = -static
libtelepathy_glib_unstable_la_LIBADD = libtelepathy-glib.la

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
    $(svc_headers) \
    text-mixin.h \
    util.h

nodist_libtelepathy_glib_internal_la_SOURCES = \
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

nodist_libtelepathy_glib_unstable_la_SOURCES = \
    _gen/unstable-signals-marshal.c \
    _gen/unstable-signals-marshal.h \
    _gen/unstable-signals-marshal.list \
    $(UNSTABLE_SPEC_GENERATED_SOURCES)

nodist_unstablegeninclude_HEADERS = \
    $(UNSTABLE_SPEC_GENERATED_HS) \
    _gen/unstable-svc.h \
    _gen/unstable-interfaces.h \
    _gen/unstable-enums.h
unstableinclude_HEADERS = \
    svc-unstable.h

BUILT_SOURCES = \
    $(nodist_libtelepathy_glib_internal_la_SOURCES) \
    $(nodist_geninclude_HEADERS) \
    $(nodist_libtelepathy_glib_unstable_la_SOURCES) \
    $(nodist_unstablegeninclude_HEADERS) \
    $(nodist_unstableinclude_HEADERS)

CLEANFILES = \
    $(BUILT_SOURCES) \
    _gen/spec-only-unstable.xsl \
    _gen/spec-only-stable.xsl \
    _gen/stable-spec.xml \
    _gen/unstable-spec.xml \
    _gen/stable-interfaces.txt

clean-local:
	-rmdir _gen

check_c_sources = \
    $(tpginclude_HEADERS) \
    $(libtelepathy_glib_la_SOURCES)
include $(top_srcdir)/check-coding-style.mk

libtelepathy_glib_internal_la_SOURCES = \
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

_gen/unstable-svc.h: Makefile.in
	for i in $(UNSTABLE_SPEC_INTERFACES); \
	do \
		echo "#include <telepathy-glib/_gen/svc-$$i.h>"; \
	done > $@

_gen/spec-only-stable.xsl: ../tools/identity.xsl _gen/stable-interfaces.txt \
		Makefile.am
	(\
	sed -e 's@</xsl:stylesheet>@@' $(srcdir)/../tools/identity.xsl; \
	while read input; \
	do \
		echo '  <xsl:template match="node[@name='"'/$$input'"']" priority="2">'; \
		echo '    <xsl:copy>'; \
		echo '      <xsl:apply-templates select="@*|node()"/>';\
		echo '    </xsl:copy>'; \
		echo '  </xsl:template>'; \
	done < _gen/stable-interfaces.txt; \
	echo '  <!-- ignore all other nodes -->'; \
	echo '  <xsl:template match="node" priority="1"/>'; \
	echo "</xsl:stylesheet>"; \
	) > $@

_gen/spec-only-unstable.xsl: ../tools/identity.xsl _gen/stable-interfaces.txt
	(\
	sed -e 's@</xsl:stylesheet>@@' $(srcdir)/../tools/identity.xsl; \
	while read input; \
	do \
		echo '  <xsl:template match="node[@name='"'/$$input'"']" priority="1"/>'; \
	done < _gen/stable-interfaces.txt; \
	echo "</xsl:stylesheet>"; \
	) > $@

_gen/stable-spec.xml: ../spec/all.xml \
	$(SPEC_INTERFACE_XMLS) _gen/spec-only-stable.xsl
	$(XSLTPROC) --xinclude $(XSLTPROCFLAGS) \
		_gen/spec-only-stable.xsl $(srcdir)/../spec/all.xml > $@
_gen/unstable-spec.xml: ../spec/all.xml \
	$(SPEC_INTERFACE_XMLS) _gen/spec-only-unstable.xsl
	$(XSLTPROC) --xinclude $(XSLTPROCFLAGS) \
		_gen/spec-only-unstable.xsl $(srcdir)/../spec/all.xml > $@

_gen/telepathy-enums.h: _gen/stable-spec.xml \
	../tools/c-constants-generator.xsl
	$(XSLTPROC) --xinclude $(XSLTPROCFLAGS) \
		$(srcdir)/../tools/c-constants-generator.xsl \
		_gen/stable-spec.xml > $@

_gen/unstable-enums.h: _gen/unstable-spec.xml \
	../tools/c-constants-generator.xsl
	$(XSLTPROC) --xinclude $(XSLTPROCFLAGS) \
		$(srcdir)/../tools/c-constants-generator.xsl \
		_gen/unstable-spec.xml > $@

_gen/telepathy-interfaces.h: _gen/stable-spec.xml \
	../tools/c-interfaces-generator.xsl
	$(XSLTPROC) --xinclude $(XSLTPROCFLAGS) \
		$(srcdir)/../tools/c-interfaces-generator.xsl \
		_gen/stable-spec.xml > $@

_gen/unstable-interfaces.h: _gen/unstable-spec.xml \
	../tools/c-interfaces-generator.xsl
	$(XSLTPROC) --xinclude $(XSLTPROCFLAGS) \
		$(srcdir)/../tools/c-interfaces-generator.xsl \
		_gen/unstable-spec.xml > $@

_gen/telepathy-errors.c: ../spec/errors.xml \
	../tools/glib-errors-enum-body.xsl
	$(XSLTPROC) $(XSLTPROCFLAGS) \
		$(srcdir)/../tools/glib-errors-enum-body.xsl \
		$< > $@

_gen/telepathy-errors.h: ../spec/errors.xml \
	../tools/glib-errors-enum-header.xsl
	$(XSLTPROC) $(XSLTPROCFLAGS) \
		$(srcdir)/../tools/glib-errors-enum-header.xsl \
		$< > $@

_gen/%.xml: ../spec/%.xml ../tools/spec-to-introspect.xsl
	$(XSLTPROC) $(XSLTPROCFLAGS) \
		$(srcdir)/../tools/spec-to-introspect.xsl $< \
		| $(DROP_NAMESPACE) > $@

_gen/async-%.xml: _gen/%.xml ../tools/make-all-async.xsl
	$(XSLTPROC) $(XSLTPROCFLAGS) $(srcdir)/../tools/make-all-async.xsl $< > $@

_gen/svc-%-glue.h: _gen/async-%.xml
	$(DBUS_BINDING_TOOL) --mode=glib-server --output=$@ \
		--prefix=tp_svc_`echo $* | tr A-Z a-z` $<

_gen/svc-%.c _gen/svc-%.h _gen/svc-%-signals-marshal.list: ../spec/%.xml ../tools/genginterface.py _gen/stable-interfaces.txt Makefile.am
	if grep '^$*$$' _gen/stable-interfaces.txt; \
	then \
		sigprefix=_tp; \
		sigheader=signals-marshal.h; \
	else \
		sigprefix=_tp_unst; \
		sigheader=unstable-signals-marshal.h; \
	fi; \
	$(PYTHON) $(srcdir)/../tools/genginterface.py \
		--filename=_gen/svc-$* --signal-marshal-prefix=$$sigprefix \
		--include='<telepathy-glib/dbus.h>' \
		--include="\"$$sigheader\"" \
		--not-implemented-func='tp_dbus_g_method_return_not_implemented' \
		$< TpSvc`echo $* | tr -d _`

_gen/signals-marshal.list: signals-marshal.list $(STABLE_SPEC_GENERATED_LISTS)
	sort $^ | uniq > $@

_gen/signals-marshal.h: _gen/signals-marshal.list
	$(GLIB_GENMARSHAL) --header --prefix=_tp_marshal $< > $@

_gen/signals-marshal.c: _gen/signals-marshal.list
	$(GLIB_GENMARSHAL) --body --prefix=_tp_marshal $< > $@

_gen/unstable-signals-marshal.list: $(UNSTABLE_SPEC_GENERATED_LISTS)
	sort $^ | uniq > $@

_gen/unstable-signals-marshal.h: _gen/unstable-signals-marshal.list
	$(GLIB_GENMARSHAL) --header --prefix=_tp_unst_marshal $< > $@

_gen/unstable-signals-marshal.c: _gen/unstable-signals-marshal.list
	$(GLIB_GENMARSHAL) --body --prefix=_tp_unst_marshal $< > $@
