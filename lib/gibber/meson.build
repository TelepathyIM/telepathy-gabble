gibber_src = [
  'gibber-debug.c',
  'gibber-debug.h',
  'gibber-transport.c',
  'gibber-transport.h',
  'gibber-fd-transport.c',
  'gibber-fd-transport.h',
  'gibber-tcp-transport.c',
  'gibber-tcp-transport.h',
  'gibber-unix-transport.c',
  'gibber-unix-transport.h',
  'gibber-linklocal-transport.c',
  'gibber-linklocal-transport.h',
  'gibber-listener.c',
  'gibber-listener.h',
  'gibber-sockets.c',
  'gibber-sockets.h',
  'gibber-sockets-unix.h',
  'gibber-sockets-win32.h',
  'gibber-util.h',
  'gibber-util.c'
]

style_check_src += files(gibber_src)

marshal_list = 'gibber-signals-marshal.list'
marshal_srcs = custom_target('gibber-signals-marshal',
  input: gibber_src,
  output: marshal_list,
  command: [ python, meson.source_root()/'tools'/'gen_marshal_list.py',
    '_gibber_signals_marshal_',
    '@INPUT@',
    '@OUTPUT@' ]
)

gibber_marshals = gnome.genmarshal('gibber-signals-marshal',
  sources: [ marshal_srcs ],
  prefix: '_gibber_signals_marshal')

gibber_deps = [ glib_dep, gobj_dep, gio_dep ]
gibber_cflags = cc.get_supported_arguments(['-fno-strict-aliasing'])
gibber_cflags += '-DGLIB_VERSION_MIN_REQUIRED='+conf.get('GLIB_VERSION_MIN_REQUIRED')
gibber_cflags += '-DGLIB_VERSION_MAX_ALLOWED='+conf.get('GLIB_VERSION_MAX_ALLOWED')
gibber_cflags += '-D_GNU_SOURCE'
gibber_lib = static_library('gibber', gibber_src + gibber_marshals,
  include_directories: gabble_conf_inc,
  c_args: gibber_cflags,
  dependencies: gibber_deps)

gibber_dep = declare_dependency(
  include_directories: gabble_lib_inc,
  dependencies: gibber_deps,
  link_with: gibber_lib)

