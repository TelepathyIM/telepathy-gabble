# Telepathy manager file
managerdir = $(datadir)/telepathy/managers
manager_DATA = gabble.manager

# Dbus service file
servicedir = $(DBUS_SERVICES_DIR)
service_in_files = org.freedesktop.Telepathy.ConnectionManager.gabble.service.in
service_DATA = $(service_in_files:.service.in=.service)

BUILT_FILES = $(service_DATA) $(manager_DATA)

CLEANFILES = $(BUILT_FILES)

EXTRA_DIST = $(service_in_files)

# Rule to make the service file with bindir expanded
$(service_DATA): $(service_in_files) Makefile
	@sed -e "s|\@bindir\@|$(bindir)|" $< > $@

$(manager_DATA): ../src/gabble-connection-manager.c ../src/write-mgr-file.c
	$(MAKE) -C ../src write-mgr-file
	../src/write-mgr-file > $@
