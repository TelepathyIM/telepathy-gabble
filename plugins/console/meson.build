console_src = [
  'channel-manager.c',
  'channel-manager.h',
  'channel.c',
  'channel.h',
  'debug.c',
  'debug.h',
  'plugin.c',
  'plugin.h',
]
console_lib = shared_module('console', console_src,
  include_directories: [plugins_inc, gabble_conf_inc],
  install: get_option('plugins').enabled(),
  install_dir: pluginsdir,
  link_args: plugin_ldflags,
  dependencies: plugin_deps)

style_check_src += files(console_src)
