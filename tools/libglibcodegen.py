"""Library code for GLib/D-Bus-related code generation.

The master copy of this library is in the telepathy-glib repository -
please make any changes there.
"""

# Copyright (C) 2006-2008 Collabora Limited
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


from libtpcodegen import NS_TP, \
                         Signature, \
                         cmp_by_name, \
                         escape_as_identifier, \
                         get_by_path, \
                         get_descendant_text, \
                         get_docstring, \
                         xml_escape, \
                         get_deprecated

def dbus_gutils_wincaps_to_uscore(s):
    """Bug-for-bug compatible Python port of _dbus_gutils_wincaps_to_uscore
    which gets sequences of capital letters wrong in the same way.
    (e.g. in Telepathy, SendDTMF -> send_dt_mf)
    """
    ret = ''
    for c in s:
        if c >= 'A' and c <= 'Z':
            length = len(ret)
            if length > 0 and (length < 2 or ret[length-2] != '_'):
                ret += '_'
            ret += c.lower()
        else:
            ret += c
    return ret

def type_to_gtype(s):
    if s == 'y': #byte
        return ("guchar ", "G_TYPE_UCHAR","UCHAR", False)
    elif s == 'b': #boolean
        return ("gboolean ", "G_TYPE_BOOLEAN","BOOLEAN", False)
    elif s == 'n': #int16
        return ("gint ", "G_TYPE_INT","INT", False)
    elif s == 'q': #uint16
        return ("guint ", "G_TYPE_UINT","UINT", False)
    elif s == 'i': #int32
        return ("gint ", "G_TYPE_INT","INT", False)
    elif s == 'u': #uint32
        return ("guint ", "G_TYPE_UINT","UINT", False)
    elif s == 'x': #int64
        return ("gint64 ", "G_TYPE_INT64","INT64", False)
    elif s == 't': #uint64
        return ("guint64 ", "G_TYPE_UINT64","UINT64", False)
    elif s == 'd': #double
        return ("gdouble ", "G_TYPE_DOUBLE","DOUBLE", False)
    elif s == 's': #string
        return ("gchar *", "G_TYPE_STRING", "STRING", True)
    elif s == 'g': #signature - FIXME
        return ("gchar *", "DBUS_TYPE_G_SIGNATURE", "STRING", True)
    elif s == 'o': #object path
        return ("gchar *", "DBUS_TYPE_G_OBJECT_PATH", "BOXED", True)
    elif s == 'v':  #variant
        return ("GValue *", "G_TYPE_VALUE", "BOXED", True)
    elif s == 'as':  #array of strings
        return ("gchar **", "G_TYPE_STRV", "BOXED", True)
    elif s == 'ay': #byte array
        return ("GArray *",
            "dbus_g_type_get_collection (\"GArray\", G_TYPE_UCHAR)", "BOXED",
            True)
    elif s == 'au': #uint array
        return ("GArray *", "DBUS_TYPE_G_UINT_ARRAY", "BOXED", True)
    elif s == 'ai': #int array
        return ("GArray *", "DBUS_TYPE_G_INT_ARRAY", "BOXED", True)
    elif s == 'ax': #int64 array
        return ("GArray *", "DBUS_TYPE_G_INT64_ARRAY", "BOXED", True)
    elif s == 'at': #uint64 array
        return ("GArray *", "DBUS_TYPE_G_UINT64_ARRAY", "BOXED", True)
    elif s == 'ad': #double array
        return ("GArray *", "DBUS_TYPE_G_DOUBLE_ARRAY", "BOXED", True)
    elif s == 'ab': #boolean array
        return ("GArray *", "DBUS_TYPE_G_BOOLEAN_ARRAY", "BOXED", True)
    elif s == 'ao': #object path array
        return ("GPtrArray *",
                'dbus_g_type_get_collection ("GPtrArray",'
                ' DBUS_TYPE_G_OBJECT_PATH)',
                "BOXED", True)
    elif s == 'a{ss}': #hash table of string to string
        return ("GHashTable *", "DBUS_TYPE_G_STRING_STRING_HASHTABLE", "BOXED", False)
    elif s[:2] == 'a{':  #some arbitrary hash tables
        if s[2] not in ('y', 'b', 'n', 'q', 'i', 'u', 's', 'o', 'g'):
            raise Exception("can't index a hashtable off non-basic type " + s)
        first = type_to_gtype(s[2])
        second = type_to_gtype(s[3:-1])
        return ("GHashTable *", "(dbus_g_type_get_map (\"GHashTable\", " + first[1] + ", " + second[1] + "))", "BOXED", False)
    elif s[:2] in ('a(', 'aa'): # array of structs or arrays, recurse
        gtype = type_to_gtype(s[1:])[1]
        return ("GPtrArray *", "(dbus_g_type_get_collection (\"GPtrArray\", "+gtype+"))", "BOXED", True)
    elif s[:1] == '(': #struct
        gtype = "(dbus_g_type_get_struct (\"GValueArray\", "
        for subsig in Signature(s[1:-1]):
            gtype = gtype + type_to_gtype(subsig)[1] + ", "
        gtype = gtype + "G_TYPE_INVALID))"
        return ("GValueArray *", gtype, "BOXED", True)

    # we just don't know ..
    raise Exception("don't know the GType for " + s)

def value_getter(gtype, marshaller):
    if marshaller == 'BOXED':
        return 'g_value_get_boxed'
    elif gtype == 'G_TYPE_STRING':
        return 'g_value_get_string'
    elif gtype == 'G_TYPE_UCHAR':
        return 'g_value_get_uchar'
    elif gtype == 'G_TYPE_BOOLEAN':
        return 'g_value_get_boolean'
    elif gtype == 'G_TYPE_UINT':
        return 'g_value_get_uint'
    elif gtype == 'G_TYPE_INT':
        return 'g_value_get_int'
    elif gtype == 'G_TYPE_UINT64':
        return 'g_value_get_uint64'
    elif gtype == 'G_TYPE_INT64':
        return 'g_value_get_int64'
    elif gtype == 'G_TYPE_DOUBLE':
        return 'g_value_get_double'
    else:
        raise AssertionError("Don't know how to get %s from a GValue" % marshaller)

class GDBusInterfaceInfo(object):
    def __init__(self, ugly_name, iface_element, c_name):
        self.ugly_name = ugly_name
        self.mixed_name = ugly_name.replace('_', '')
        self.lc_name = ugly_name.lower()
        self.uc_name = ugly_name.upper()
        self.c_name = c_name
        self.iface_element = iface_element

        self.method_elements = iface_element.getElementsByTagName('method')
        self.signal_elements = iface_element.getElementsByTagName('signal')
        self.property_elements = iface_element.getElementsByTagName('property')

    def do_methods(self):
        method_args = [
        ]
        method_in_arg_pointers = [
        ]
        method_out_arg_pointers = [
        ]
        methods = [
        ]
        method_pointers = [
            'static const GDBusMethodInfo *const method_pointers_%s[] = {'
                % self.c_name,
        ]

        for meth in self.method_elements:
            lc_name = meth.getAttribute('tp:name-for-bindings')
            if meth.getAttribute('name') != lc_name.replace('_', ''):
                raise AssertionError('Method %s tp:name-for-bindings (%s) '
                        'does not match' %
                        (meth.getAttribute('name'), lc_name))
            lc_name = lc_name.lower()

            c_name = 'method_%s_%s' % (self.c_name, lc_name)

            method_in_arg_pointers.append('static const GDBusArgInfo *const '
                    'method_in_arg_pointers_%s_%s[] = {' %
                    (self.c_name, lc_name))
            method_out_arg_pointers.append('static const GDBusArgInfo *const '
                    'method_out_arg_pointers_%s_%s[] = {'
                    % (self.c_name, lc_name))

            for i, arg in enumerate(meth.getElementsByTagName('arg')):
                name = arg.getAttribute('name')
                if not name:
                    name = 'arg%d' % i

                method_args.append('static const GDBusArgInfo '
                        'method_arg_%s_%s_%d = {' % (self.c_name, lc_name, i))
                method_args.append('  -1, /* refcount */')
                method_args.append('  "%s",' % name)
                method_args.append('  "%s",' % arg.getAttribute('type'))
                method_args.append('  NULL /* annotations */')
                method_args.append('};')

                if arg.getAttribute('direction') == 'out':
                    method_out_arg_pointers.append('  &method_arg_%s_%s_%d,' %
                            (self.c_name, lc_name, i))
                else:
                    method_in_arg_pointers.append('  &method_arg_%s_%s_%d,' %
                            (self.c_name, lc_name, i))

            method_in_arg_pointers.append('  NULL')
            method_in_arg_pointers.append('};')
            method_out_arg_pointers.append('  NULL')
            method_out_arg_pointers.append('};')

            methods.append('static const GDBusMethodInfo %s = {' % c_name)
            methods.append('  -1, /* refcount */')
            methods.append('  "%s",' % meth.getAttribute("name"))
            methods.append('  (GDBusArgInfo **) method_in_arg_pointers_%s_%s,'
                    % (self.c_name, lc_name))
            methods.append('  (GDBusArgInfo **) method_out_arg_pointers_%s_%s,'
                    % (self.c_name, lc_name))
            methods.append('  NULL /* annotations */')
            methods.append('};')

            method_pointers.append('  &%s,' % c_name)

        method_pointers.append('  NULL')
        method_pointers.append('};')

        return (method_args + method_in_arg_pointers +
                method_out_arg_pointers + methods + method_pointers)

    def do_signals(self):
        signal_args = [
        ]
        signal_arg_pointers = [
        ]
        signals = [
        ]
        signal_pointers = [
            'static const GDBusSignalInfo *const signal_pointers_%s[] = {'
                % self.c_name,
        ]

        for sig in self.signal_elements:
            lc_name = sig.getAttribute('tp:name-for-bindings')
            if sig.getAttribute('name') != lc_name.replace('_', ''):
                raise AssertionError('Signal %s tp:name-for-bindings (%s) '
                        'does not match' %
                        (sig.getAttribute('name'), lc_name))
            lc_name = lc_name.lower()

            c_name = 'signal_%s_%s' % (self.c_name, lc_name)

            signal_arg_pointers.append('static const GDBusArgInfo *const '
                    'signal_arg_pointers_%s_%s[] = {' % (self.c_name, lc_name))

            for i, arg in enumerate(sig.getElementsByTagName('arg')):
                name = arg.getAttribute('name')
                if not name:
                    name = 'arg%d' % i

                signal_args.append('static const GDBusArgInfo '
                        'signal_arg_%s_%s_%d = {' % (self.c_name, lc_name, i))
                signal_args.append('  -1, /* refcount */')
                signal_args.append('  "%s",' % name)
                signal_args.append('  "%s",' % arg.getAttribute('type'))
                signal_args.append('  NULL /* annotations */')
                signal_args.append('};')

                signal_arg_pointers.append('  &signal_arg_%s_%s_%d,' %
                        (self.c_name, lc_name, i))

            signal_arg_pointers.append('  NULL')
            signal_arg_pointers.append('};')

            signals.append('static const GDBusSignalInfo %s = {' % c_name)
            signals.append('  -1, /* refcount */')
            signals.append('  "%s",' % sig.getAttribute("name"))
            signals.append('  (GDBusArgInfo **) signal_arg_pointers_%s_%s,'
                    % (self.c_name, lc_name))
            signals.append('  NULL /* annotations */')
            signals.append('};')

            signal_pointers.append('  &%s,' % c_name)

        signal_pointers.append('  NULL')
        signal_pointers.append('};')

        return signal_args + signal_arg_pointers + signals + signal_pointers

    def do_properties(self):
        properties = [
        ]
        property_pointers = [
            'static const GDBusPropertyInfo *const property_pointers_%s[] = {'
                % self.c_name,
        ]

        for prop in self.property_elements:
            access = prop.getAttribute('access')
            flags = {
                'read': 'G_DBUS_PROPERTY_INFO_FLAGS_READABLE',
                'write': 'G_DBUS_PROPERTY_INFO_FLAGS_WRITABLE',
                'readwrite':
                    'G_DBUS_PROPERTY_INFO_FLAGS_READABLE | '
                    'G_DBUS_PROPERTY_INFO_FLAGS_WRITABLE',
            }[access]

            lc_name = prop.getAttribute('tp:name-for-bindings')
            if prop.getAttribute('name') != lc_name.replace('_', ''):
                raise AssertionError('Property %s tp:name-for-bindings (%s) '
                        'does not match' %
                        (prop.getAttribute('name'), lc_name))
            lc_name = lc_name.lower()

            c_name = 'property_%s_%s' % (self.c_name, lc_name)

            properties.append('static const GDBusPropertyInfo %s = {' % c_name)
            properties.append('  -1, /* refcount */')
            properties.append('  "%s",' % prop.getAttribute("name"))
            properties.append('  "%s",' % prop.getAttribute("type"))
            properties.append('  %s,' % flags)
            # FIXME: add annotations?
            properties.append('  NULL /* annotations */')
            properties.append('};')

            property_pointers.append('  &%s,' % c_name)

        property_pointers.append('  NULL')
        property_pointers.append('};')

        return properties + property_pointers

    def to_lines(self, linkage='static'):
        return (self.do_methods() +
                self.do_signals() +
                self.do_properties() + [
            '%s const GDBusInterfaceInfo %s = {' % (linkage, self.c_name),
            '  -1, /* refcount */',
            '  "%s",' % self.iface_element.getAttribute('name'),
            '  (GDBusMethodInfo **) method_pointers_%s,' % self.c_name,
            '  (GDBusSignalInfo **) signal_pointers_%s,' % self.c_name,
            '  (GDBusPropertyInfo **) property_pointers_%s,' % self.c_name,
            '  NULL /* annotations */',
            '};'
        ])
