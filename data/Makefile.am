# Telepathy manager file
managerdir = $(datadir)/telepathy/managers
manager_DATA = gabble.manager

# Dbus service file
servicedir = $(DBUS_SERVICES_DIR)
service_in_files = org.freedesktop.Telepathy.ConnectionManager.gabble.service.in
service_DATA = $(service_in_files:.service.in=.service)

# Rule to make the service file with bindir expanded
$(service_DATA): $(service_in_files) Makefile
	@sed -e "s|\@bindir\@|$(bindir)|" $< > $@
