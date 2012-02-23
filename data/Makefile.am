EXTRA_DIST = gabble.service.in

managerdir = $(datadir)/telepathy/managers
manager_DATA = gabble.manager

servicedir = $(datadir)/dbus-1/services
service_DATA = org.freedesktop.Telepathy.ConnectionManager.gabble.service

# We don't use the full filename for the .in because > 99 character filenames
# in tarballs are non-portable (and automake 1.8 doesn't let us build
# non-archaic tarballs)
org.freedesktop.Telepathy.ConnectionManager.gabble.service: gabble.service.in \
		Makefile
	$(AM_V_GEN)sed -e "s|[@]libexecdir[@]|$(libexecdir)|" $< > $@

CLEANFILES = $(service_DATA) $(manager_DATA)

$(manager_DATA): ../src/write-mgr-file.c ../src/protocol.c ../src/protocol.h
	@$(MAKE) -C ../src write-mgr-file$(EXEEXT)
	$(AM_V_GEN)../src/write-mgr-file$(EXEEXT) > $@
