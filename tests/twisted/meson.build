local_twisted_tests = [
  'addressing.py',
  'avatar-requirements.py',
  'caps_helper.py',
  'client-types.py',
  'console.py',
  'dataforms.py',
  'gateways.py',
  'last-activity.py',
  'mail-notification.py',
  'pep-support.py',
  'plugin-channel-managers.py',
  'power-save.py',
  'pubsub.py',
  'servicetest.py',
  'sidecar-own-caps.py',
  'sidecars.py',
  'test-debug.py',
  'test-fallback-socks5-proxy.py',
  'test-location.py',
  'test-register.py',
  'version.py',
]
twisted_tests = files(local_twisted_tests)
subdirs = [
  'caps',
  'cm',
  'connect',
  'muc',
  'olpc',
  'presence',
  'roster',
  'sasl',
  'search',
  'text',
  'tls',
  'tubes',
  'vcard',
]

if get_option('voip').enabled()
  subdirs += 'jingle'
endif

if get_option('file-transfer').enabled()
  subdirs += 'file-transfer'
endif

if jingle_file_transfer
  subdirs += 'jingle-share'
endif

foreach dir: subdirs
  subdir(dir)
endforeach

cdata = configuration_data({
  'PACKAGE_STRING': conf.get('PACKAGE_STRING'),
  'CLIENT_TYPE': client_type,
  'PLUGINS_ENABLED': get_option('plugins').enabled() ? 'True' : 'False',
  'GOOGLE_RELAY_ENABLED': get_option('google-relay') ? 'True' : 'False',
  'FILE_TRANSFER_ENABLED': get_option('file-transfer').enabled() ? 'True' : 'False',
  'VOIP_ENABLED': get_option('voip').enabled() ? 'True' : 'False',
  'JINGLE_FILE_TRANSFER_ENABLED': jingle_file_transfer ? 'True' : 'False'
})
configure_file(input: 'config.py.in',
  output: 'config.py',
  configuration: cdata)

twisted_dir = gabbletestsdir / 'twisted'
gabble_debug_src = [ 'main-debug.c', 'test-resolver.c', 'test-resolver.c']
tp_gabble_debug = executable('telepathy-gabble-debug',
  gabble_debug_src, enums_src, interfaces_src, gtypes_src,
  link_with: [ gabble_lib, gabble_plugins_lib ],
  include_directories: [ gabble_conf_inc, gabble_src_inc ],
  export_dynamic: true,
  dependencies: gabble_deps,
  install_dir: twisted_dir,
  install: get_option('install-tests')
  )
style_check_src += files(gabble_debug_src)

subdir('tools')

py = find_program('python3', 'python')
configure_file(
  input: 'run-test.sh.in',
  output: 'run-test.sh',
  configuration: {
    'TEST_PYTHON': py.path(),
    'gabbletestsdir': gabbletestsdir,
  })

# Install files in each directory. They could be tests, pristine data files,
# scripts or built source
if get_option('install-tests')
  install_data([
      local_twisted_tests,
      'config.py',
      'run-test.sh'
    ],
    install_dir: twisted_dir)
  foreach dir: subdirs
    install_subdir(dir,
      exclude_files: 'meson.build',
      install_dir: twisted_dir)
  endforeach
endif


has_twisted = run_command(python, '-c', 'import twisted')
has_pydbus = run_command(python, '-c', 'import dbus')

if has_twisted.returncode() == 0 and has_pydbus.returncode() == 0
  foreach t: twisted_tests
    test('@0@'.format(t),
        python,
	args: [t],
        env: ['PYTHONPATH=@0@:@1@'.format(meson.current_source_dir(),meson.current_build_dir())],
        suite: 'twisted')
  endforeach
else
  message('''Configured without Twisted test support. To enable them,
ensure that these Python modules are available:
 • twisted.words.xish.domish
 • twisted.words.protocols.jabber
 • twisted.internet.reactor
 * dbus
and then re-configure meson build.''')
endif
