ACLOCAL_AMFLAGS = -I m4

SUBDIRS = src data m4 generate tests

dist-hook:
	chmod a+w ${distdir}/ChangeLog
	darcs changes >${distdir}/ChangeLog
