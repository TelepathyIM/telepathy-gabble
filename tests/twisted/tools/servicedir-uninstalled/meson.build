configure_file(
  input: 'tmp-session-bus.conf.in',
  output: 'tmp-session-bus.conf',
  configuration: gdata)

configure_file(
  input: 'gabble.service.in',
  output: 'org.freedesktop.Telepathy.ConnectionManager.gabble.service',
  configuration: gdata)

service_file = meson.current_build_dir()/'org.freedesktop.Telepathy.ConnectionManager.gabble.service'
service_conf = meson.current_build_dir()/'tmp-session-bus.conf'
