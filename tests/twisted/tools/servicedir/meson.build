configure_file(
  input: 'tmp-session-bus.conf.in',
  output: 'tmp-session-bus.conf',
  configuration: gdata)

configure_file(
  input: 'gabble.service.in',
  output: 'org.freedesktop.Telepathy.ConnectionManager.gabble.service',
  configuration: gdata)

if get_option('install-tests')
  install_data('tmp-session-bus.conf',
    'org.freedesktop.Telepathy.ConnectionManager.gabble.service',
    install_mode: 'rw-r--r--',
    install_dir: twisted_dir/'tools'/'servicedir')
endif


