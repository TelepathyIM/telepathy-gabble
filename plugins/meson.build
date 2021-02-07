
plugin_deps = gabble_deps + [ exts_dep, gabble_plugins_dep ]
plugins_inc = include_directories('.')
plugin_ldflags = cc.get_supported_link_arguments(['-Wl,--no-undefined'])

test_src = [
  'test.c',
  'test.h'
]

test_lib = shared_module('plugin-test', test_src,
  install: get_option('install-tests') and get_option('plugins').enabled(),
  install_dir: pluginsdir,
  include_directories: [plugins_inc, gabble_conf_inc],
  link_args: plugin_ldflags,
  dependencies: plugin_deps)

gateways_src = [
  'gateways.c',
  'gateways.h'
]

gateways_lib = shared_module('gateways', gateways_src,
  install: get_option('plugins').enabled(),
  install_dir: pluginsdir,
  include_directories: [plugins_inc, gabble_conf_inc],
  link_args: plugin_ldflags,
  dependencies: plugin_deps)

subdir('console')
if get_option('plugins').enabled()
  install_data('telepathy-gabble-xmpp-console',
    install_dir: bindir)
endif

plugins = [ gateways_lib, console_lib ]

if get_option('install-tests')
  plugins += test_lib
endif

style_check_src += files(test_src + gateways_src)
