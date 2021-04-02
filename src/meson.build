
# handwritten sources
gabble_sources = [
  'addressing-util.h',
  'addressing-util.c',
  'auth-manager.h',
  'auth-manager.c',
  'bytestream-factory.h',
  'bytestream-factory.c',
  'bytestream-ibb.h',
  'bytestream-ibb.c',
  'bytestream-iface.h',
  'bytestream-iface.c',
  'bytestream-muc.h',
  'bytestream-muc.c',
  'bytestream-multiple.h',
  'bytestream-multiple.c',
  'bytestream-socks5.h',
  'bytestream-socks5.c',
  'capabilities.c',
  'caps-hash.h',
  'caps-hash.c',
  'caps-channel-manager.c',
  'conn-addressing.h',
  'conn-addressing.c',
  'conn-aliasing.h',
  'conn-aliasing.c',
  'conn-avatars.h',
  'conn-avatars.c',
  'conn-client-types.h',
  'conn-client-types.c',
  'conn-contact-info.h',
  'conn-contact-info.c',
  'conn-location.h',
  'conn-location.c',
  'conn-olpc.h',
  'conn-olpc.c',
  'conn-power-saving.h',
  'conn-power-saving.c',
  'conn-presence.h',
  'conn-presence.c',
  'conn-sidecars.h',
  'conn-sidecars.c',
  'conn-util.h',
  'conn-util.c',
  'conn-mail-notif.h',
  'conn-mail-notif.c',
  'connection.h',
  'connection.c',
  'connection-manager.h',
  'connection-manager.c',
  'debug.h',
  'debug.c',
  'disco.h',
  'disco.c',
  'error.c',
  'error.h',
  'gabble.c',
  'gabble.h',
  'im-channel.h',
  'im-channel.c',
  'im-factory.h',
  'im-factory.c',
  'message-util.h',
  'message-util.c',
  'muc-channel.h',
  'muc-channel.c',
  'muc-factory.h',
  'muc-factory.c',
  'muc-tube-dbus.h',
  'muc-tube-dbus.c',
  'muc-tube-stream.h',
  'muc-tube-stream.c',
  'namespaces.h',
  'olpc-activity.h',
  'olpc-activity.c',
  'plugin-loader.h',
  'plugin-loader.c',
  'presence.h',
  'presence.c',
  'presence-cache.h',
  'presence-cache.c',
  'protocol.h',
  'protocol.c',
  'private-tubes-factory.h',
  'private-tubes-factory.c',
  'request-pipeline.h',
  'request-pipeline.c',
  'roster.h',
  'roster.c',
  'roster-cache.h',
  'roster-cache.c',
  'room-config.h',
  'room-config.c',
  'roomlist-channel.h',
  'roomlist-channel.c',
  'roomlist-manager.h',
  'roomlist-manager.c',
  'search-channel.h',
  'search-channel.c',
  'search-manager.h',
  'search-manager.c',
  'server-sasl-channel.h',
  'server-sasl-channel.c',
  'server-tls-channel.h',
  'server-tls-channel.c',
  'server-tls-manager.h',
  'server-tls-manager.c',
  'sidecar.c',
  'tls-certificate.h',
  'tls-certificate.c',
  'tube-iface.h',
  'tube-iface.c',
  'tube-dbus.h',
  'tube-dbus.c',
  'tube-stream.h',
  'tube-stream.c',
  'types.h',
  'util.h',
  'util.c',
  'vcard-manager.h',
  'vcard-manager.c',
]
if get_option('file-transfer').enabled()
  gabble_sources += [
    'ft-channel.c',
    'ft-channel.h',
    'ft-manager.c',
    'ft-manager.h',
  ]
endif

if get_option('voip').enabled()
  gabble_sources += [
    'base-call-channel.h',
    'base-call-channel.c',
    'call-content.h',
    'call-content.c',
    'call-channel.h',
    'call-channel.c',
    'call-muc-channel.h',
    'call-muc-channel.c',
    'call-member.h',
    'call-member.c',
    'call-member-content.h',
    'call-member-content.c',
    'call-stream.h',
    'call-stream.c',
    'jingle-share.h',
    'jingle-share.c',
    'jingle-mint.h',
    'jingle-mint.c',
    'jingle-tp-util.h',
    'jingle-tp-util.c',
    'media-factory.h',
    'media-factory.c',
  ]
endif

if jingle_file_transfer
  gabble_sources += [
    'gtalk-file-collection.c',
    'gtalk-file-collection.h',
  ]
endif

enumtype_sources = [
  'connection.h',
  'room-config.h',
  'presence.h'
]

gabble_plugins_src = [
  'capabilities.c',
  'caps-channel-manager.c',
  'debug.c',
  'error.c',
  'plugin.c',
  'plugin-connection.c',
  'sidecar.c'
]

gabble_cflags = [
  '-DPLUGIN_DIR="'+pluginsdir+'"',
  '-DG_LOG_DOMAIN="gabble"',
]
gabble_ldflags = cc.get_supported_link_arguments(['-Wl,--no-undefined'])

gabble_plugins_lib = library('gabble-plugins', gabble_plugins_src,
  version: meson.project_version(),
  dependencies: gabble_deps,
  include_directories: [ gabble_conf_inc ],
  link_args: gabble_ldflags,
  c_args: gabble_cflags,
  install_dir: gabblelibdir,
  install: get_option('plugins').enabled()
)

gabble_plugins_dep = declare_dependency(
  link_with: gabble_plugins_lib,
  dependencies: gabble_deps,
  include_directories: gabble_conf_inc,
)

gabble_lib_dep = gabble_deps
gabble_lib_dep += gibber_dep
gabble_lib_dep += exts_dep

marshal_list = 'gabble-signals-marshal.list'
marshal_srcs = custom_target('gabble-signals-marshal',
  input: gabble_sources,
  output: marshal_list,
  command: [ python, meson.source_root()/'tools'/'gen_marshal_list.py', 'gabble_marshal_', '@INPUT@', '@OUTPUT@' ])

gabble_marshals = gnome.genmarshal('gabble-signals-marshal',
  sources: [ marshal_srcs ],
  prefix: 'gabble_marshal')

gabble_enums = gnome.mkenums_simple('gabble-enumtypes',
  body_prefix: '#include "config.h"',
  sources: enumtype_sources)

gabble_lib = static_library('gabble', gabble_sources + gabble_marshals + gabble_enums,
  c_args: gabble_cflags,
  include_directories: [ gabble_conf_inc ],
  dependencies: gabble_lib_dep)

write_mgr_file = executable('write-mgr-file', 'write-mgr-file.c',
  dependencies: gabble_deps,
  include_directories: [ gabble_conf_inc ],
  link_with: [ gabble_lib, gabble_plugins_lib ],
  install: false)

telepathy_gabble = executable('telepathy-gabble', 'main.c',
  dependencies: gabble_deps,
  include_directories: [ gabble_conf_inc ],
  link_with: [ gabble_lib, gabble_plugins_lib ],
  export_dynamic: true,
  install_dir: libexecdir,
  install_rpath: gabblelibdir,
  install: true)

if get_option('code-style-check')
  check_c_sources = [
    'main.c',
    'write-mgr-file.c',
    gabble_sources,
  ]
  foreach file: gabble_plugins_src
    if file not in check_c_sources
      check_c_sources += file
    endif
  endforeach
  style_check_src += files(check_c_sources)
endif

gabble_src_inc = include_directories('.')
