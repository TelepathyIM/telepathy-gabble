tools_dir = meson.source_root()/'tools'

exts_src = [
  'extensions.c',
  'extensions.h'
]

style_check_src += files(exts_src)

xml_sources = [
  'all.xml',
  'Connection_Interface_Gabble_Decloak.xml',
  'Gabble_Plugin_Console.xml',
  'Gabble_Plugin_Gateways.xml',
  'Gabble_Plugin_Test.xml',
  'OLPC_Activity_Properties.xml',
  'OLPC_Buddy_Info.xml',
]
# Generated stuff

all_xml = custom_target('all.xml',
  input: 'all.xml',
  output: 'all.xml',
  capture: true,
  command: [ python, tools_dir/'xincludator.py', '@INPUT@' ],
  depend_files: xml_sources + [tools_dir/'xincludator.py'])

exts_html = custom_target('extensions.html',
  input: [ tools_dir/'doc-generator.xsl', all_xml ],
  output: 'extensions.html',
  command: [
    xsltproc,
    '--nonet',
    '--novalid',
    '--param', 'allow-undefined-interfaces', 'true()',
    '@INPUT@',
    '-o', '@OUTPUT@'
  ])

# support the legacy. At least for some time
run_command(shell, '-c', 'cd '+meson.current_build_dir()+' && ln -Tfs . _gen', check: true)

svc_src = custom_target('svc',
  input: all_xml,
  output: ['svc.c', 'svc.h', 'svc-gtk-doc.h'],
  command: [
    python, tools_dir/'glib-ginterface-gen.py',
    '--filename='+'extensions'/'svc',
    '--include=<telepathy-glib/telepathy-glib.h>',
    '--allow-unstable',
    '--not-implemented-func=tp_dbus_g_method_return_not_implemented',
    '@INPUT@',
    'Gabble_Svc_'
    ],
  depend_files: [ tools_dir/'glib-ginterface-gen.py'])

enums_src = custom_target('enums',
  input: all_xml,
  output: ['enums.h', 'enums-gtk-doc.h'],
  command: [ python, tools_dir/'c-constants-gen.py', 'Gabble', '@INPUT@', 'extensions'/'enums' ],
  depend_files: [ tools_dir/'c-constants-gen.py' ])

interfaces_src = custom_target('interfaces',
  input: all_xml,
  output: [ 'interfaces.h', 'interfaces-body.h', 'interfaces-gtk-doc.h' ],
  command: [ python, tools_dir/'glib-interfaces-gen.py', 'Gabble', '@OUTPUT1@', '@OUTPUT0@', '@INPUT@' ],
  depend_files: [ tools_dir/'glib-interfaces-gen.py'])

gtypes_src = custom_target('gtypes',
  input: all_xml,
  output: [ 'gtypes.h', 'gtypes-body.h', 'gtypes-gtk-doc.h'],
  command: [ python, tools_dir/'glib-gtypes-generator.py', '@INPUT@', 'extensions'/'gtypes', 'Gabble' ])

exts_deps = [glib_dep, gobj_dep, dbus_glib_dep, tp_glib_dep]
exgen_src = [svc_src, enums_src, interfaces_src, gtypes_src]
exts_srcs = exts_src + exgen_src
exts_la = static_library('gabble-extensions', exts_srcs,
  include_directories: [ gabble_conf_inc ],
  dependencies: exts_deps)

exts_dep = declare_dependency(
  link_with: exts_la,
  sources: exgen_src,
  #include_directories: include_directories(meson.current_build_dir()),
  dependencies: exts_deps)
