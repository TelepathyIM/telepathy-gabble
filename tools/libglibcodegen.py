"""Library code for GLib/D-Bus-related code generation.

The master copy of this library is in the telepathy-glib repository -
please make any changes there.
"""

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


from string import ascii_letters, digits


NS_TP = "http://telepathy.freedesktop.org/wiki/DbusSpec#extensions-v0"

_ASCII_ALNUM = ascii_letters + digits


def camelcase_to_lower(s):
    out ="";
    out += s[0].lower()
    last_upper=False
    if s[0].isupper():
        last_upper=True
    for i in range(1,len(s)):
        if s[i].isupper():
            if last_upper:
                if (i+1) < len(s) and  s[i+1].islower():
                    out += "_" + s[i].lower()
                else:
                    out += s[i].lower()
            else:
                out += "_" + s[i].lower()
            last_upper=True
        else:
            out += s[i]
            last_upper=False
    return out


def camelcase_to_upper(s):
    return camelcase_to_lower(s).upper()


def cmp_by_name(node1, node2):
    return cmp(node1.getAttributeNode("name").nodeValue,
               node2.getAttributeNode("name").nodeValue)


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


def escape_as_identifier(identifier):
    """Escape the given string to be a valid D-Bus object path or service
    name component, using a reversible encoding to ensure uniqueness.

    The reversible encoding is as follows:

    * The empty string becomes '_'
    * Otherwise, each non-alphanumeric character is replaced by '_' plus
      two lower-case hex digits; the same replacement is carried out on
      the first character, if it's a digit
    """
    # '' -> '_'
    if not identifier:
        return '_'

    # A bit of a fast path for strings which are already OK.
    # We deliberately omit '_' because, for reversibility, that must also
    # be escaped.
    if (identifier.strip(_ASCII_ALNUM) == '' and
        identifier[0] in ascii_letters):
        return identifier

    # The first character may not be a digit
    if identifier[0] not in ascii_letters:
        ret = ['_%02x' % ord(identifier[0])]
    else:
        ret = [identifier[0]]

    # Subsequent characters may be digits or ASCII letters
    for c in identifier[1:]:
        if c in _ASCII_ALNUM:
            ret.append(c)
        else:
            ret.append('_%02x' % ord(c))

    return ''.join(ret)


def get_docstring(element):
    docstring = None
    for x in element.childNodes:
        if x.namespaceURI == NS_TP and x.localName == 'docstring':
            docstring = x
    if docstring is not None:
        docstring = docstring.toxml().replace('\n', ' ').strip()
        if docstring.startswith('<tp:docstring>'):
            docstring = docstring[14:].lstrip()
        if docstring.endswith('</tp:docstring>'):
            docstring = docstring[:-15].rstrip()
        if docstring in ('<tp:docstring/>', ''):
            docstring = ''
    return docstring


def signal_to_marshal_type(signal):
    """
    return a list of strings indicating the marshalling type for this signal.
    """

    mtype=[]
    for i in signal.getElementsByTagName("arg"):
        name =i.getAttribute("name")
        type = i.getAttribute("type")
        mtype.append(type_to_gtype(type)[2])

    return mtype


_glib_marshallers = ['VOID', 'BOOLEAN', 'CHAR', 'UCHAR', 'INT',
        'STRING', 'UINT', 'LONG', 'ULONG', 'ENUM', 'FLAGS', 'FLOAT',
        'DOUBLE', 'STRING', 'PARAM', 'BOXED', 'POINTER', 'OBJECT',
        'UINT_POINTER']


def signal_to_marshal_name(signal, prefix):

    mtype = signal_to_marshal_type(signal)
    if len(mtype):
        name = '_'.join(mtype)
    else:
        name = 'VOID'

    if name in _glib_marshallers:
        return 'g_cclosure_marshal_VOID__' + name
    else:
        return prefix + '_marshal_VOID__' + name


def method_to_glue_marshal_name(method, prefix):

    mtype = []
    for i in method.getElementsByTagName("arg"):
        if i.getAttribute("direction") != "out":
            type = i.getAttribute("type")
            mtype.append(type_to_gtype(type)[2])

    mtype.append('POINTER')

    name = '_'.join(mtype)

    if name in _glib_marshallers:
        return 'g_cclosure_marshal_VOID__' + name
    else:
        return prefix + '_marshal_VOID__' + name


class _SignatureIter:
    """Iterator over a D-Bus signature. Copied from dbus-python 0.71 so we
    can run genginterface in a limited environment with only Python
    (like Scratchbox).
    """
    def __init__(self, string):
        self.remaining = string

    def next(self):
        if self.remaining == '':
            raise StopIteration

        signature = self.remaining
        block_depth = 0
        block_type = None
        end = len(signature)

        for marker in range(0, end):
            cur_sig = signature[marker]

            if cur_sig == 'a':
                pass
            elif cur_sig == '{' or cur_sig == '(':
                if block_type == None:
                    block_type = cur_sig

                if block_type == cur_sig:
                    block_depth = block_depth + 1

            elif cur_sig == '}':
                if block_type == '{':
                    block_depth = block_depth - 1

                if block_depth == 0:
                    end = marker
                    break

            elif cur_sig == ')':
                if block_type == '(':
                    block_depth = block_depth - 1

                if block_depth == 0:
                    end = marker
                    break

            else:
                if block_depth == 0:
                    end = marker
                    break

        end = end + 1
        self.remaining = signature[end:]
        return Signature(signature[0:end])


class Signature(str):
    """A string, iteration over which is by D-Bus single complete types
    rather than characters.
    """
    def __iter__(self):
        return _SignatureIter(self)


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
        return ("GPtrArray *", "DBUS_TYPE_G_OBJECT_ARRAY", "BOXED", True)
    elif s == 'a{ss}': #hash table of string to string
        return ("GHashTable *", "DBUS_TYPE_G_STRING_STRING_HASHTABLE", "BOXED", False)
    elif s[:2] == 'a{':  #some arbitrary hash tables
        if s[2] not in ('y', 'b', 'n', 'q', 'i', 'u', 's', 'o', 'g'):
            raise Exception, "can't index a hashtable off non-basic type " + s
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
    raise Exception, "don't know the GType for " + s


def xml_escape(s):
    s = s.replace('&', '&amp;').replace("'", '&apos;').replace('"', '&quot;')
    return s.replace('<', '&lt;').replace('>', '&gt;')
