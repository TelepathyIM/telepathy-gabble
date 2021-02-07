project(
  'Telepathy Gabble','c',
  license: 'LGPL2.1+',
  version: '0.19.0',
  meson_version: '>= 0.49.0',
  default_options: ['c_std=c99', 'warning_level=2', 'werror=true']
)

prefix = get_option('prefix')
datadir = prefix / get_option('datadir')
bindir = prefix / get_option('bindir')
libdir = prefix / get_option('libdir')
libexecdir = prefix / get_option('libexecdir')
localedir = prefix / get_option('localedir')
includedir = get_option('includedir')
include_subdir = 'telepathy-gabble-0'
includedir = includedir / include_subdir

libdir_suffix = 'telepathy'/'gabble-0'
gabblelibdir = libdir / libdir_suffix /'lib'
pluginsdir = libdir / libdir_suffix /'plugins'

# compiler flags
common_flags = [
  ## covered by warning_level=2
  #'-D_ALL_SOURCE',
  #'-D_GNU_SOURCE',
  '-DHAVE_CONFIG_H',
  ## not available till glib 2.56
  #'-DG_LOG_USE_STRUCTURED',
  '-DLOCALE_DIR="@0@"'.format(localedir)
]
add_project_arguments(common_flags, language: 'c')

# compiler specific flags
werr_cflags = []
warn_errs = [
  #  'all',
  #  'extra',
  'declaration-after-statement',
  'implicit-function-declaration',
  'shadow',
  'strict-prototypes',
  'missing-declarations',
  'missing-prototypes',
  'sign-compare',
  'nested-externs',
  'pointer-arith',
  'format',
  'format-security',
  'init-self'
]
no_warn_errs = [
  'missing-field-initializers',
  'cast-function-type',
  'unused-parameter'
]
foreach e: warn_errs
  werr_cflags += '-W@0@'.format(e)
endforeach

## covered by werror=true
#werr_cflags += '-Werror'

foreach e: no_warn_errs
  werr_cflags += '-Wno-@0@'.format(e)
  werr_cflags += '-Wno-error=@0@'.format(e)
endforeach

cc = meson.get_compiler('c')
add_project_arguments(cc.get_supported_arguments(werr_cflags), language: 'c')

# configuration
conf = configuration_data()
conf_h = ''
defines = []

check_h = [
  'arpa/inet.h',
  'arpa/nameser.h',
  'fcntl.h',
  'ifaddrs.h',
  'netdb.h',
  'netinet/in.h',
  'sys/ioctl.h',
  'sys/types.h',
  'sys/un.h',
  'unistd.h'
]

foreach h: check_h
  if cc.has_header(h)
    define = 'HAVE_' + h.underscorify().to_upper()
    defines += define
  endif
endforeach

# dependencies
glib_dep    = dependency('glib-2.0', version: '>= 2.32',
  fallback: ['glib', 'libglib_dep'])
gio_dep     = dependency('gio-2.0',  version: '>= 2.32',
  fallback: ['glib', 'libgio_dep'])
gobj_dep    = dependency('gobject-2.0',
  fallback: ['glib', 'gobj_dep'])
gmod_dep    = dependency('gmodule-2.0',
  fallback: ['glib', 'gmod_dep'])
libdbus_dep = dependency('dbus-1',  version: '>= 1.1.0')
dbus_glib_dep = dependency('dbus-glib-1', version: '>= 0.82')
tp_glib_dep = dependency('telepathy-glib', version: '>= 0.23.2')
libsoup_dep = dependency('libsoup-2.4', version: '>= 2.42', required: get_option('google-relay'))
libnice_dep = dependency('nice', version: '>= 0.0.11', required: get_option('voip'))

conf.set('GLIB_VERSION_MIN_REQUIRED', 'GLIB_VERSION_2_32', description: 'Ignore post 2.32 deprecations')
conf.set('GLIB_VERSION_MAX_ALLOWED', 'GLIB_VERSION_2_34', description: 'Prevent post 2.34 APIs')

conf.set('TP_VERSION_MIN_REQUIRED', 'TP_VERSION_0_24', description: 'Ignore post 0.24 deprecations')
conf.set('TP_VERSION_MAX_ALLOWED', 'TP_VERSION_0_24', description: 'Prevent post 0.24 APIs')
conf.set('TP_SEAL_ENABLE', 1, description: 'Prevent to use sealed variables')
conf.set('TP_DISABLE_SINGLE_INCLUDE', 1, description: 'Disable single header include')

# locally sourced wocky goes to gabble-specific subdir
wocky_dep = dependency('wocky', version: '>= 0.18', fallback: ['wocky', 'wocky_dep'],
  default_options: [
    'libdir-suffix='+libdir_suffix/'lib',
    'install-headers='+include_subdir,
    'google-relay=@0@'.format(get_option('google-relay'))
  ])

gabble_deps = [
  glib_dep,
  gio_dep,
  gobj_dep,
  gmod_dep,
  libdbus_dep,
  dbus_glib_dep,
  tp_glib_dep,
  wocky_dep,
]

conf.set_quoted('PACKAGE_NAME', meson.project_name())
conf.set_quoted('PACKAGE_VERSION', meson.project_version())
conf.set_quoted('PACKAGE_STRING', '@0@ @1@'.format(
  meson.project_name(),
  meson.project_version()
  )
)
conf.set_quoted('VERSION', meson.project_version())
defines += '_GNU_SOURCE'
defines += '_ALL_SOURCE'

if get_option('debug')
  defines += 'ENABLE_DEBUG'
endif

if get_option('plugins').enabled()
  defines += 'ENABLE_PLUGINS'
endif

if get_option('google-relay')
  defines += 'ENABLE_GOOGLE_RELAY'
  gabble_deps += libsoup_dep
endif

if get_option('voip').enabled()
  defines += 'ENABLE_VOIP'
  gabble_deps += libnice_dep
endif

if get_option('file-transfer').enabled()
  defines += 'ENABLE_FILE_TRANSFER'
endif

jingle_file_transfer = (get_option('voip').enabled() and get_option('file-transfer').enabled())
if jingle_file_transfer
  defines += 'ENABLE_JINGLE_FILE_TRANSFER'
endif

foreach define: defines
  conf.set(define, 1)
  conf_h += '#define @0@ 1\n'.format(define)
endforeach

#if get_option('gibber').enabled()
  extra_h = '''
#include <stdio.h>
#include <stdlib.h>
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
'''

  if cc.check_header('sys/socket.h', prefix: conf_h + extra_h)
    conf.set('HAVE_SYS_SOCKET_H', 1)
    extra_h += '#include <sys/socket.h>\n'
  endif

  if cc.check_header('net/if.h', prefix: conf_h + extra_h)
    conf.set('HAVE_NET_IF_H', 1)
    extra_h += '#include <net/if.h>'
  endif

  extra_h += '''
#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif
#ifdef HAVE_ARPA_NAMESER_H
#include <arpa/nameser.h>
endif
#ifdef HAVE_NETDB_H
#include <netdb.h>
#endif
'''

  if cc.check_header('resolv.h', prefix: conf_h + extra_h)
    conf.set('HAVE_RESOLV_H', 1)
  endif
#endif # gibber.enabled

if get_option('ca-certificates') != ''
  conf.set_quoted('GTLS_SYSTEM_CA_CERTIFICATES', get_option('ca-certificates'))
endif

if get_option('fatal-criticals').enabled()
  conf.set('ENABLE_FATAL_CRITICALS', 1)
endif

if host_machine.system() == 'windows'
  conf.set('_WIN32_WINNT', '0x0501',
    description: 'following flag is requied to make getnameinfo work')
endif


client_type='pc'
if get_option('is-a-phone')
  client_type='phone'
endif
conf.set_quoted('CLIENT_TYPE',client_type)

config_h = configure_file(
  output: 'config.h',
  configuration: conf
)

gabble_conf_inc = include_directories('.')

gnome = import('gnome')
pymod = import('python')
python = pymod.find_installation()
xsltproc = find_program('xsltproc')
shell = find_program('sh')

style_check_src = []
subdir('lib')
subdir('extensions')
subdir('src')
subdir('plugins')
subdir('gabble')
subdir('data')
subdir('tests')

if get_option('code-style-check')
  run_target('check', command: [
    shell, meson.source_root()/'tools'/'check-c-style.sh',
    style_check_src
  ])
endif

output = '''
Configure summary:

        Compiler....................:  @0@
        Compiler Flags..............:  @1@
        Prefix......................:  @2@
        Coding style checks.........:  @3@
        Python tests................:  @4@
        Install unit tests..........:  @5@

    Features:

        Client type.................:  "@6@"
        Plugin support..............:  @7@
        Plugin headers installed....:  @7@
        Google relay support........:  @8@
        File transfer support.......:  @9@
        Jingle file transfer support:  @10@
        VoIP support................:  @11@

'''.format(
  cc.get_id(),
  cc.cmd_array(),
  prefix,
  get_option('code-style-check'),
  python.found(),
  get_option('install-tests'),
  client_type,
  get_option('plugins').enabled(),
  get_option('google-relay'),
  get_option('file-transfer').enabled(),
  jingle_file_transfer,
  get_option('voip').enabled()
  )
message(output)
#summary({
#  'compiler': cc.get_id(),
#  'prefix': prefix,
#  'Coding style checks': get_option('code-style-check'),
#  'Python tests': python.found(),
#  'Install unit tests': get_option('install-tests')
#  }, section: 'Configuration')
#summary({
#  'Client type': client_type,
#  'Plugin support': get_option('plugins').enabled(),
#  'Google relay support': get_option('google-relay'),
#  'File transfer support': get_option('file-transfer').enabled(),
#  'Jingle file transfer support': (get_option('voip').enabled() and get_option('file-transfer').enabled()),
#  'VoIP support': get_option('voip').enabled()
#  }, section: 'Features')

