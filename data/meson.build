
managerdir = datadir/'telepathy'/'managers'
servicedir = datadir/'dbus-1'/'services'

gabble_mgr = custom_target('gabble.manager',
  output: 'gabble.manager',
  command: [ write_mgr_file ],
  capture: true,
  install: true,
  install_dir: managerdir)

gabble_srv = configure_file(
  input: 'gabble.service.in',
  output: 'org.freedesktop.Telepathy.ConnectionManager.gabble.service',
  configuration: { 'libexecdir': libexecdir })

install_data(gabble_srv,
  install_dir: servicedir)

