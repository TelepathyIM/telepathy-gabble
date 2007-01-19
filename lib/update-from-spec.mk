#! /usr/bin/make -f

# To be run from inside _spec.

TELEPATHY_SPEC = $(CURDIR)/../../../telepathy-spec

# Exclude ChannelInterfaceDTMF for now, it causes odd behaviour in the
# camel-case-to-lower-case algorithms and we don't need it yet.

INTERFACES = \
	     ChannelInterfaceGroup \
	     ChannelInterfaceHold \
	     ChannelInterfaceMediaSignalling \
	     ChannelInterfacePassword \
	     ChannelInterfaceTransfer \
	     ChannelTypeContactList \
	     ChannelTypeContactSearch \
	     ChannelTypeRoomList \
	     ChannelTypeStreamedMedia \
	     ChannelTypeText \
	     Channel \
	     ConnectionInterfaceAliasing \
	     ConnectionInterfaceAvatars \
	     ConnectionInterfaceCapabilities \
	     ConnectionInterfaceContactInfo \
	     ConnectionInterfaceForwarding \
	     ConnectionInterfacePresence \
	     ConnectionInterfacePrivacy \
	     ConnectionInterfaceRenaming \
	     ConnectionManager \
	     Connection \
	     MediaSessionHandler \
	     MediaStreamHandler \
	     Properties

INTERFACES_LC = $(shell cat $(INTERFACES:%=$(TELEPATHY_SPEC)/tmp/%.name.lower))
INTERFACES_DASH = $(subst _,-,$(INTERFACES_LC))
INTERFACES_UC = $(shell cat $(INTERFACES:%=$(TELEPATHY_SPEC)/tmp/%.name.upper))

GLUE = $(INTERFACES_DASH:%=svc-%-glue.h)
SIGNALS_MARSHAL_LISTS = $(INTERFACES_DASH:%=svc-%-signals-marshal.list)
IMPLS = $(INTERFACES_DASH:%=svc-%.c)
HEADERS = $(INTERFACES_DASH:%=telepathy-glib/svc-%.h)

SIMPLE_HEADERS = telepathy-glib/_spec/telepathy-enums.h \
	         telepathy-glib/_spec/telepathy-errors.h \
	         telepathy-glib/_spec/telepathy-interfaces.h

GENERATED_FILES = $(SIMPLE_HEADERS) $(GLUE) $(IMPLS) $(HEADERS) $(SIGNALS_MARSHAL_LISTS) Makefile.am.gen

update: prepare $(GENERATED_FILES) check

check:
	@cat ../signals-marshal.list $(SIGNALS_MARSHAL_LISTS) \
		| sort | uniq | diff ../signals-marshal.list - \
		|| echo "*** Some signals are missing from signals-marshal.list"

Makefile.am.gen: ../update-from-spec.mk
	@echo "Making _spec/Makefile.am.gen"
	@(\
	echo "# Generated from Makefile.update-from-spec";\
	echo "noinst_LTLIBRARIES = libtelepathy-glib-_spec.la";\
	echo "tpginclude_HEADERS = $(HEADERS) $(SIMPLE_HEADERS)";\
	echo "libtelepathy_glib__spec_la_SOURCES = $(GLUE) $(IMPLS)";\
	echo "SIGNALS_MARSHAL_LISTS = $(SIGNALS_MARSHAL_LISTS)";\
	) > $@

prepare:
	@{ test -n $(TELEPATHY_SPEC) && test -d $(TELEPATHY_SPEC) \
		&& test -f $(TELEPATHY_SPEC)/spec/all.xml; } \
		|| { echo "TELEPATHY_SPEC must be specified"; exit 1; }
	$(MAKE) -C $(TELEPATHY_SPEC)

$(GLUE) $(IMPLS) $(SIGNALS_MARSHAL_LISTS): %: $(TELEPATHY_SPEC)/c/ginterfaces/%
	cp $< $@

$(HEADERS): telepathy-glib/%: $(TELEPATHY_SPEC)/c/ginterfaces/%
	@install -d telepathy-glib
	cp $< $@

$(SIMPLE_HEADERS): telepathy-glib/_spec/%: $(TELEPATHY_SPEC)/c/%
	@install -d telepathy-glib/_spec
	cp $< $@
