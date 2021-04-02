gabble_headers = [
  'capabilities.h',
  'capabilities-set.h',
  'caps-channel-manager.h',
  'caps-hash.h',
  'plugin-connection.h',
  'error.h',
  'gabble.h',
  'namespaces.h',
  'plugin.h',
  'sidecar.h',
  'types.h']

style_check_src += files(gabble_headers)

if get_option('plugins').enabled()
  install_headers(gabble_headers,
    subdir: include_subdir/'gabble')
  pkg = import('pkgconfig')
  pkg.generate(gabble_plugins_lib,
    install_dir: libdir/'pkgconfig',
    name: 'Telepathy-Gabble',
    filebase: 'telepathy-gabble',
    subdirs: include_subdir
  )
endif
