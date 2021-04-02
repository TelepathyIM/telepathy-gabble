gdata = configuration_data({
  'gabbletestsdir': gabbletestsdir,
  'abs_top_builddir': meson.build_root(),
  'abs_top_srcdir': meson.source_root(),
  'pluginexecdir': libexecdir,
  'libdir': libdir,
})

configure_file(
  input: 'run-gabble.sh.in',
  output: 'run-gabble.sh',
  configuration: gdata)

configure_file(
  input: 'gabble-debug.sh.in',
  output: 'exec-with-log.sh',
  configuration: gdata)

if get_option('install-tests')
  install_data('with-session-bus.sh',
    'exec-with-log.sh',
    'run-gabble.sh',
    install_mode: 'rwxr-xr-x',
    install_dir: twisted_dir/'tools')
endif

subdir('servicedir')
subdir('servicedir-uninstalled')
