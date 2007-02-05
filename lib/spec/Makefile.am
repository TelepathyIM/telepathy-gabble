include spec-gen.am

EXTRA_DIST = \
    all.xml \
    errors.xml \
    $(SPEC_INTERFACE_XMLS)

# This is a derived file but must be checked-in to Darcs anyway, for
# bootstrapping.
spec-gen.am: Makefile.am all.xml $(SPEC_INTERFACE_XMLS) \
	../tools/ls-interfaces.xsl
	$(XSLTPROC) $(XSLTPROCFLAGS) --xinclude \
		$(srcdir)/../tools/ls-interfaces.xsl \
		$(srcdir)/../spec/all.xml > spec-gen.tmp
	@echo Interfaces to be generated are:
	@cat spec-gen.tmp ; echo
	@echo end of interfaces list
	(\
	echo SPEC_INTERFACES = `cat spec-gen.tmp` && \
	echo SPEC_INTERFACE_XMLS = `sed -e 's,[A-Za-z_][A-Za-z_]*,&.xml,g' spec-gen.tmp` \
	) > spec-gen.am
	rm -f spec-gen.tmp
