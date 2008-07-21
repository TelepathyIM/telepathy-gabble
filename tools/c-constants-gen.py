#!/usr/bin/python

from sys import argv, stdout, stderr
import xml.dom.minidom

from libglibcodegen import NS_TP, camelcase_to_upper, get_docstring, \
        get_descendant_text, get_by_path

class Generator(object):
    def __init__(self, prefix, dom):
        self.prefix = prefix + '_'
        self.spec = get_by_path(dom, "spec")[0]

    def __call__(self):
        self.do_header()
        self.do_body()
        self.do_footer()

    # Header
    def do_header(self):
        stdout.write('/* Generated from ')
        stdout.write(get_descendant_text(get_by_path(self.spec, 'title')))
        version = get_by_path(self.spec, "version")
        if version:
            stdout.write(', version ' + get_descendant_text(version))
        stdout.write('\n\n')
        for copyright in get_by_path(self.spec, 'copyright'):
            stdout.write(get_descendant_text(copyright))
            stdout.write('\n')
        stdout.write(get_descendant_text(get_by_path(self.spec, 'license')))
        stdout.write('\n')
        stdout.write(get_descendant_text(get_by_path(self.spec, 'docstring')))
        stdout.write("""
 */

#ifdef __cplusplus
extern "C" {
#endif
\n""")

    # Body
    def do_body(self):
        for elem in self.spec.getElementsByTagNameNS(NS_TP, '*'):
            if elem.localName == 'flags':
                self.do_flags(elem)
            elif elem.localName == 'enum':
                self.do_enum(elem)

    def do_flags(self, flags):
        name = flags.getAttribute('plural') or flags.getAttribute('name')
        value_prefix = flags.getAttribute('singular') or \
                       flags.getAttribute('value-prefix') or \
                       flags.getAttribute('name')
        stdout.write("""\
/**
 *
%s:
""" % (self.prefix + name).replace('_', ''))
        for flag in get_by_path(flags, 'flag'):
            self.do_gtkdoc(flag, value_prefix)
        stdout.write(' *\n')
        docstrings = get_by_path(flags, 'docstring')
        if docstrings:
            stdout.write("""\
 * <![CDATA[%s]]>
 *
""" % get_descendant_text(docstrings).replace('\n', ' '))
        stdout.write("""\
 * Bitfield/set of flags generated from the Telepathy specification.
 */
typedef enum {
""")
        for flag in get_by_path(flags, 'flag'):
            self.do_val(flag, value_prefix)
        stdout.write("""\
} %s;

""" % (self.prefix + name).replace('_', ''))

    def do_enum(self, enum):
        name = enum.getAttribute('singular') or enum.getAttribute('name')
        value_prefix = enum.getAttribute('singular') or \
                       enum.getAttribute('value-prefix') or \
                       enum.getAttribute('name')
        name_plural = enum.getAttribute('plural') or \
                      enum.getAttribute('name') + 's'
        stdout.write("""\
/**
 *
%s:
""" % (self.prefix + name).replace('_', ''))
        vals = get_by_path(enum, 'enumvalue')
        for val in vals:
            self.do_gtkdoc(val, value_prefix)
        stdout.write(' *\n')
        docstrings = get_by_path(enum, 'docstring')
        if docstrings:
            stdout.write("""\
 * <![CDATA[%s]]>
 *
""" % get_descendant_text(docstrings).replace('\n', ' '))
        stdout.write("""\
 * Bitfield/set of flags generated from the Telepathy specification.
 */
typedef enum {
""")
        for val in vals:
            self.do_val(val, value_prefix)
        stdout.write("""\
} %(mixed-name)s;

/**
 * NUM_%(upper-plural)s:
 *
 * 1 higher than the highest valid value of #%(mixed-name)s.
 */
#define NUM_%(upper-plural)s (%(last-val)s+1)

""" % {'mixed-name' : (self.prefix + name).replace('_', ''),
       'upper-plural' : (self.prefix + name_plural).upper(),
       'last-val' : vals[-1].getAttribute('value')})

    def do_val(self, val, value_prefix):
        name = val.getAttribute('name')
        suffix = val.getAttribute('suffix')
        use_name = (self.prefix + value_prefix + '_' + \
                (suffix or name)).upper()
        assert not (name and suffix) or name == suffix, \
                'Flag/enumvalue name %s != suffix %s' % (name, suffix)
        stdout.write('    %s = %s,\n' % (use_name, val.getAttribute('value')))

    def do_gtkdoc(self, node, value_prefix):
        stdout.write(' * @')
        stdout.write((self.prefix + value_prefix + '_' +
            node.getAttribute('suffix')).upper())
        stdout.write(': <![CDATA[')
        docstring = get_by_path(node, 'docstring')
        stdout.write(get_descendant_text(docstring).replace('\n', ' '))
        stdout.write(']]>\n')

    # Footer
    def do_footer(self):
        stdout.write("""
#ifdef __cplusplus
}
#endif
""")

if __name__ == '__main__':
    argv = argv[1:]
    Generator(argv[0], xml.dom.minidom.parse(argv[1]))()
