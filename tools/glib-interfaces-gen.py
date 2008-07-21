#!/usr/bin/python

from sys import argv, stdout, stderr
import xml.dom.minidom

from libglibcodegen import NS_TP, camelcase_to_upper, get_docstring, \
        get_descendant_text, get_by_path

class Generator(object):
    def __init__(self, prefix, implfile, declfile, dom):
        self.prefix = prefix + '_'
        self.impls = open(implfile, 'w')
        self.decls = open(declfile, 'w')
        self.spec = get_by_path(dom, "spec")[0]

    def __call__(self):
        for file in self.decls, self.impls:
            self.do_header(file)
        self.do_body()

    # Header
    def do_header(self, file):
        file.write('/* Generated from: ')
        file.write(get_descendant_text(get_by_path(self.spec, 'title')))
        version = get_by_path(self.spec, "version")
        if version:
            file.write(' version ' + get_descendant_text(version))
        file.write('\n\n')
        for copyright in get_by_path(self.spec, 'copyright'):
            stdout.write(get_descendant_text(copyright))
            stdout.write('\n')
        file.write('\n')
        file.write(get_descendant_text(get_by_path(self.spec, 'license')))
        file.write(get_descendant_text(get_by_path(self.spec, 'docstring')))
        file.write("""
 */

""")

    # Body
    def do_body(self):
        for iface in self.spec.getElementsByTagName('interface'):
            self.do_iface(iface)

    def do_iface(self, iface):
        parent_name = get_by_path(iface, '../@name')
        self.decls.write("""\
/**
 * %(IFACE_DEFINE)s:
 * 
 * The interface name "%(name)s"
 */
#define %(IFACE_DEFINE)s \\
"%(name)s"
""" % {'IFACE_DEFINE' : (self.prefix + 'IFACE_' + \
            parent_name).upper().replace('/', ''),
       'name' : iface.getAttribute('name')})

        self.decls.write("""
/**
 * %(IFACE_QUARK_DEFINE)s:
 * 
 * Expands to a call to a function that returns a quark for the interface \
name "%(name)s"
 */
#define %(IFACE_QUARK_DEFINE)s \\
  (%(iface_quark_func)s ())

GQuark %(iface_quark_func)s (void);

""" % {'IFACE_QUARK_DEFINE' : (self.prefix + 'IFACE_QUARK_' + \
            parent_name).upper().replace('/', ''),
       'iface_quark_func' : (self.prefix + 'iface_quark_' + \
            parent_name).lower().replace('/', ''),
       'name' : iface.getAttribute('name')})

        self.impls.write("""\
GQuark
%(iface_quark_func)s (void)
{
  static GQuark quark = 0;

  if (G_UNLIKELY (quark == 0))
    {
      quark = g_quark_from_static_string ("%(name)s");
    }

  return quark;
}

""" % {'iface_quark_func' : (self.prefix + 'iface_quark_' + \
            parent_name).lower().replace('/', ''),
       'name' : iface.getAttribute('name')})

if __name__ == '__main__':
    argv = argv[1:]
    Generator(argv[0], argv[1], argv[2], xml.dom.minidom.parse(argv[3]))()
