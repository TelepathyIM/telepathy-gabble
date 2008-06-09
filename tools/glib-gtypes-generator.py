#!/usr/bin/python

# Generate GLib GInterfaces from the Telepathy specification.
# The master copy of this program is in the telepathy-glib repository -
# please make any changes there.
#
# Copyright (C) 2006, 2007 Collabora Limited
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

import sys
import xml.dom.minidom

from libglibcodegen import escape_as_identifier, \
                           get_docstring, \
                           NS_TP, \
                           Signature, \
                           type_to_gtype, \
                           xml_escape


def types_to_gtypes(types):
    return [type_to_gtype(t)[1] for t in types]


class GTypesGenerator(object):
    def __init__(self, dom, output, mixed_case_prefix):
        self.dom = dom
        self.Prefix = mixed_case_prefix
        self.PREFIX_ = self.Prefix.upper() + '_'
        self.prefix_ = self.Prefix.lower() + '_'

        self.header = open(output + '.h', 'w')
        self.body = open(output + '-body.h', 'w')

        for f in (self.header, self.body):
            f.write('/* Auto-generated, do not edit.\n *\n'
                    ' * This file may be distributed under the same terms\n'
                    ' * as the specification from which it was generated.\n'
                    ' */\n\n')

        self.need_mappings = {}
        self.need_structs = {}
        self.need_arrays = {}

    def do_mapping_header(self, mapping):
        members = mapping.getElementsByTagNameNS(NS_TP, 'member')
        assert len(members) == 2

        impl_sig = ''.join([elt.getAttribute('type')
                            for elt in members])

        esc_impl_sig = escape_as_identifier(impl_sig)

        name = (self.PREFIX_ + 'HASH_TYPE_' +
                mapping.getAttribute('name').upper())
        impl = self.prefix_ + 'type_dbus_hash_' + esc_impl_sig

        docstring = get_docstring(mapping) or '(Undocumented)'

        self.header.write('/**\n * %s:\n *\n' % name)
        self.header.write(' * %s\n' % xml_escape(docstring))
        self.header.write(' *\n')
        self.header.write(' * This macro expands to a call to a function\n')
        self.header.write(' * that returns the #GType of a #GHashTable\n')
        self.header.write(' * appropriate for representing a D-Bus\n')
        self.header.write(' * dictionary of signature\n')
        self.header.write(' * <literal>a{%s}</literal>.\n' % impl_sig)
        self.header.write(' *\n')

        key, value = members

        self.header.write(' * Keys (D-Bus type <literal>%s</literal>,\n'
                          % key.getAttribute('type'))
        tp_type = key.getAttributeNS(NS_TP, 'type')
        if tp_type:
            self.header.write(' * type <literal>%s</literal>,\n' % tp_type)
        self.header.write(' * named <literal>%s</literal>):\n'
                          % key.getAttribute('name'))
        docstring = get_docstring(key) or '(Undocumented)'
        self.header.write(' * %s\n' % xml_escape(docstring))
        self.header.write(' *\n')

        self.header.write(' * Values (D-Bus type <literal>%s</literal>,\n'
                          % value.getAttribute('type'))
        tp_type = value.getAttributeNS(NS_TP, 'type')
        if tp_type:
            self.header.write(' * type <literal>%s</literal>,\n' % tp_type)
        self.header.write(' * named <literal>%s</literal>):\n'
                          % value.getAttribute('name'))
        docstring = get_docstring(value) or '(Undocumented)'
        self.header.write(' * %s\n' % xml_escape(docstring))
        self.header.write(' *\n')

        self.header.write(' */\n')

        self.header.write('#define %s (%s ())\n\n' % (name, impl))
        self.need_mappings[impl_sig] = esc_impl_sig

    def do_struct_header(self, struct):
        members = struct.getElementsByTagNameNS(NS_TP, 'member')
        impl_sig = ''.join([elt.getAttribute('type') for elt in members])
        esc_impl_sig = escape_as_identifier(impl_sig)

        name = (self.PREFIX_ + 'STRUCT_TYPE_' +
                struct.getAttribute('name').upper())
        impl = self.prefix_ + 'type_dbus_struct_' + esc_impl_sig
        docstring = struct.getElementsByTagNameNS(NS_TP, 'docstring')
        if docstring:
            docstring = docstring[0].toprettyxml()
            if docstring.startswith('<tp:docstring>'):
                docstring = docstring[14:]
            if docstring.endswith('</tp:docstring>\n'):
                docstring = docstring[:-16]
            if docstring.strip() in ('<tp:docstring/>', ''):
                docstring = '(Undocumented)'
        else:
            docstring = '(Undocumented)'
        self.header.write('/**\n * %s:\n\n' % name)
        self.header.write(' * %s\n' % xml_escape(docstring))
        self.header.write(' *\n')
        self.header.write(' * This macro expands to a call to a function\n')
        self.header.write(' * that returns the #GType of a #GValueArray\n')
        self.header.write(' * appropriate for representing a D-Bus struct\n')
        self.header.write(' * with signature <literal>(%s)</literal>.\n'
                          % impl_sig)
        self.header.write(' *\n')

        for i, member in enumerate(members):
            self.header.write(' * Member %d (D-Bus type '
                              '<literal>%s</literal>,\n'
                              % (i, member.getAttribute('type')))
            tp_type = member.getAttributeNS(NS_TP, 'type')
            if tp_type:
                self.header.write(' * type <literal>%s</literal>,\n' % tp_type)
            self.header.write(' * named <literal>%s</literal>):\n'
                              % member.getAttribute('name'))
            docstring = get_docstring(member) or '(Undocumented)'
            self.header.write(' * %s\n' % xml_escape(docstring))
            self.header.write(' *\n')

        self.header.write(' */\n')
        self.header.write('#define %s (%s ())\n\n' % (name, impl))

        array_name = struct.getAttribute('array-name')
        if array_name != '':
            array_name = (self.PREFIX_ + 'ARRAY_TYPE_' + array_name.upper())
            impl = self.prefix_ + 'type_dbus_array_' + esc_impl_sig
            self.header.write('/**\n * %s:\n\n' % array_name)
            self.header.write(' * Expands to a call to a function\n')
            self.header.write(' * that returns the #GType of a #GPtrArray\n')
            self.header.write(' * of #%s.\n' % name)
            self.header.write(' */\n')
            self.header.write('#define %s (%s ())\n\n' % (array_name, impl))
            self.need_arrays[impl_sig] = esc_impl_sig

        self.need_structs[impl_sig] = esc_impl_sig

    def __call__(self):
        mappings = self.dom.getElementsByTagNameNS(NS_TP, 'mapping')
        structs = self.dom.getElementsByTagNameNS(NS_TP, 'struct')

        for mapping in mappings:
            self.do_mapping_header(mapping)

        for sig in self.need_mappings:
            self.header.write('GType %stype_dbus_hash_%s (void);\n\n' %
                              (self.prefix_, self.need_mappings[sig]))
            self.body.write('GType\n%stype_dbus_hash_%s (void)\n{\n' %
                              (self.prefix_, self.need_mappings[sig]))
            self.body.write('  static GType t = 0;\n\n')
            self.body.write('  if (G_UNLIKELY (t == 0))\n')
            # FIXME: translate sig into two GTypes
            items = tuple(Signature(sig))
            gtypes = types_to_gtypes(items)
            self.body.write('    t = dbus_g_type_get_map ("GHashTable", '
                            '%s, %s);\n' % (gtypes[0], gtypes[1]))
            self.body.write('  return t;\n')
            self.body.write('}\n\n')

        for struct in structs:
            self.do_struct_header(struct)

        for sig in self.need_structs:
            self.header.write('GType %stype_dbus_struct_%s (void);\n\n' %
                              (self.prefix_, self.need_structs[sig]))
            self.body.write('GType\n%stype_dbus_struct_%s (void)\n{\n' %
                              (self.prefix_, self.need_structs[sig]))
            self.body.write('  static GType t = 0;\n\n')
            self.body.write('  if (G_UNLIKELY (t == 0))\n')
            self.body.write('    t = dbus_g_type_get_struct ("GValueArray",\n')
            items = tuple(Signature(sig))
            gtypes = types_to_gtypes(items)
            for gtype in gtypes:
                self.body.write('        %s,\n' % gtype)
            self.body.write('        G_TYPE_INVALID);\n')
            self.body.write('  return t;\n')
            self.body.write('}\n\n')

        for sig in self.need_arrays:
            self.header.write('GType %stype_dbus_array_%s (void);\n\n' %
                              (self.prefix_, self.need_structs[sig]))
            self.body.write('GType\n%stype_dbus_array_%s (void)\n{\n' %
                              (self.prefix_, self.need_structs[sig]))
            self.body.write('  static GType t = 0;\n\n')
            self.body.write('  if (G_UNLIKELY (t == 0))\n')
            self.body.write('    t = dbus_g_type_get_collection ("GPtrArray", '
                            '%stype_dbus_struct_%s ());\n' %
                            (self.prefix_, self.need_structs[sig]))
            self.body.write('  return t;\n')
            self.body.write('}\n\n')

if __name__ == '__main__':
    argv = sys.argv[1:]

    dom = xml.dom.minidom.parse(argv[0])

    GTypesGenerator(dom, argv[1], argv[2])()
