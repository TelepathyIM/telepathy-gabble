EXTRA_DIST = gabble.service.in

managerdir = $(datadir)/telepathy/managers
manager_DATA = gabble.manager

servicedir = $(datadir)/dbus-1/services
service_DATA = org.freedesktop.Telepathy.ConnectionManager.gabble.service

org.freedesktop.Telepathy.ConnectionManager.gabble.service: gabble.service.in \
		Makefile
	@sed -e "s|\@libexecdir\@|$(libexecdir)|" $< > $@

CLEANFILES = $(service_DATA) $(manager_DATA)

$(service_DATA): $(service_in_files) Makefile
	sed -e "s|[@]libexecdir[@]|$(libexecdir)|" $< > $@

$(manager_DATA): ../src/gabble-connection-manager.c ../src/write-mgr-file.c
	$(MAKE) -C ../src write-mgr-file
	../src/write-mgr-file > $@
