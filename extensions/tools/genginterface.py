#!/usr/bin/python

import sys
import os.path
import xml.dom.minidom

def cmdline_error():
    print """\
usage:
    gen-ginterface [OPTIONS] xmlfile classname
options:
    --include='<header.h>' (may be repeated)
    --include='"header.h"' (ditto)
        Include extra headers in the generated .c file
    --signal-marshal-prefix='prefix'
        Use the given prefix on generated signal marshallers (default is
        derived from class name). If this is given, classname-signals-marshal.h
        is not automatically included.
    --filename='BASENAME'
        Set the basename for the output files (default is derived from class
        name)
    --not-implemented-func='symbol'
        Set action when methods not implemented in the interface vtable are
        called. symbol must have signature
            void symbol (DBusGMethodInvocation *context)
        and return some sort of "not implemented" error via
            dbus_g_method_return_error (context, ...)
"""
    sys.exit(1)

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

class SignatureIter:
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
    def __iter__(self):
        return SignatureIter(self)


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
        return ("gint ", "G_TYPE_INT64","INT64", False)
    elif s == 't': #uint32
        return ("guint ", "G_TYPE_UINT64","UINT64", False)
    elif s == 'd': #double
        return ("gdouble ", "G_TYPE_DOUBLE","DOUBLE", False)
    elif s == 's': #string
        return ("gchar *", "G_TYPE_STRING", "STRING", True)
    elif s == 'g': #signature - FIXME
        return ("gchar *", "DBUS_TYPE_G_SIGNATURE", "STRING", True)
    elif s == 'o': #object path
        return ("gchar *", "DBUS_TYPE_G_OBJECT_PATH", "STRING", True)
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
    elif s[:2] == 'a(': #array of structs, recurse
        gtype = type_to_gtype(s[1:])[1]
        return ("GPtrArray *", "(dbus_g_type_get_collection (\"GPtrArray\", "+gtype+"))", "BOXED", True)
    elif s == 'a{ss}': #hash table of string to string
        return ("GHashTable *", "DBUS_TYPE_G_STRING_STRING_HASHTABLE", "BOXED", False)
    elif s[:2] == 'a{':  #some arbitrary hash tables
        if s[2] not in ('y', 'b', 'n', 'q', 'i', 'u', 's', 'o', 'g'):
            raise Exception, "can't index a hashtable off non-basic type " + s
        first = type_to_gtype(s[2])
        second = type_to_gtype(s[3:-1])
        return ("GHashTable *", "(dbus_g_type_get_map (\"GHashTable\", " + first[1] + ", " + second[1] + "))", "BOXED", False)
    elif s[:1] == '(': #struct
        gtype = "(dbus_g_type_get_struct (\"GValueArray\", "
        for subsig in Signature(s[1:-1]):
            gtype = gtype + type_to_gtype(subsig)[1] + ", "
        gtype = gtype + "G_TYPE_INVALID))"
        return ("GValueArray *", gtype, "BOXED", True)

    # we just don't know ..
    raise Exception, "don't know the GType for " + s


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

def signal_to_marshal_name(signal, prefix):
    glib_marshallers = ['VOID', 'BOOLEAN', 'CHAR', 'UCHAR', 'INT',
            'STRING', 'UINT', 'LONG', 'ULONG', 'ENUM', 'FLAGS', 'FLOAT',
            'DOUBLE', 'STRING', 'PARAM', 'BOXED', 'POINTER', 'OBJECT',
            'UINT_POINTER']

    mtype = signal_to_marshal_type(signal)
    if len(mtype):
        name = '_'.join(mtype)
    else:
        name = 'VOID'

    if name in glib_marshallers:
        return 'g_cclosure_marshal_VOID__' + name
    else:
        return prefix + '_marshal_VOID__' + name

def signal_to_gtype_list(signal):
    gtype=[]
    for i in signal.getElementsByTagName("arg"):
        name =i.getAttribute("name")
        type = i.getAttribute("type")
        gtype.append(type_to_gtype(type)[1])

    return gtype


def print_license(stream, filename, description, dom):
    stream.write(
"""/*
 * %s - %s
 *
""" % (filename, description))

    for c in dom.getElementsByTagName('tp:copyright'):
        # assume all child nodes are text
        stream.write(' * %s\n' % ''.join([n.data for n in c.childNodes
                                            if n.nodeType in (n.TEXT_NODE,
                                                n.CDATA_SECTION_NODE)]))

    stream.write("""\
 *
 * This file may be distributed under the same terms as the specification
 * from which it is generated.
 */

""")

def print_header_begin(stream, prefix):
    guardname = '__'+prefix.upper()+'_H__'
    stream.write ("#ifndef "+guardname+"\n")
    stream.write ("#define "+guardname+"\n\n")

    stream.write ("#include <glib-object.h>\n#include <dbus/dbus-glib.h>\n\n")
    stream.write ("G_BEGIN_DECLS\n\n")

def print_header_end(stream, prefix):
    guardname = '__'+prefix.upper()+'_H__'
    stream.write ("\nG_END_DECLS\n\n")
    stream.write ("#endif /* #ifndef "+guardname+"*/\n")

def print_class_declaration(stream, prefix, classname, methods):
    stream.write ("""\
/**
 * %(classname)s:
 *
 * Dummy typedef representing any implementation of this interface.
 */
typedef struct _%(classname)s %(classname)s;

/**
 * %(classname)sClass:
 *
 * The class of %(classname)s.
 */
typedef struct _%(classname)sClass %(classname)sClass;

""" % locals())

    stream.write(
"""
GType %(prefix)s_get_type (void);

""" % {'prefix':prefix,'uprefix':prefix.upper()})

    macro_prefix = prefix.upper().split('_',1)
    gtype = '_TYPE_'.join(macro_prefix)

    stream.write(
"""/* TYPE MACROS */
#define %(type)s \\
  (%(prefix)s_get_type ())
#define %(main)s_%(sub)s(obj) \\
  (G_TYPE_CHECK_INSTANCE_CAST((obj), %(type)s, %(name)s))
#define %(main)s_IS_%(sub)s(obj) \\
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), %(type)s))
#define %(main)s_%(sub)s_GET_CLASS(obj) \\
  (G_TYPE_INSTANCE_GET_INTERFACE ((obj), %(type)s, %(name)sClass))

""" % {"main":macro_prefix[0], "sub":macro_prefix[1], "type":gtype, "name":classname, "prefix":prefix})
 

def signal_emit_stub(signal):
    # for signal: org.freedesktop.Telepathy.Thing::StuffHappened (s, u)
    # emit: void tp_svc_thing_emit_stuff_happened (gpointer instance,
    #           const char *arg, guint arg2)
    dbus_name = signal.getAttributeNode("name").nodeValue
    c_emitter_name = prefix + '_emit_' + camelcase_to_lower(dbus_name)
    c_signal_const_name = 'SIGNAL_' + dbus_name

    macro_prefix = prefix.upper().split('_',1)

    decl = 'void ' + c_emitter_name + ' (gpointer instance'
    args = ''
    argdoc = ''

    for i in signal.getElementsByTagName("arg"):
        name = i.getAttribute("name")
        type = i.getAttribute("type")
        info = type_to_gtype(type)
        gtype = info[0]
        if gtype[3]:
            gtype = 'const ' + gtype
        decl += ',\n    ' + gtype + ' ' + name
        args += ', ' + name
        argdoc += ' * @' + name + ': FIXME: document args in genginterface\n'
    decl += ')'

    doc = ("""\
/**
 * %s:
 * @instance: An object implementing this interface
%s *
 * Emit the %s D-Bus signal from @instance with the given arguments.
 */
""" % (c_emitter_name, argdoc, dbus_name))

    header = decl + ';\n\n'
    body = doc + decl + ('\n{\n'
                   '  g_assert (%s_IS_%s (instance));\n'
                   '  g_signal_emit (instance, signals[%s], 0%s);\n'
                   '}\n\n'
                   % (macro_prefix[0], macro_prefix[1], c_signal_const_name,
                      args))

    return header, body


def print_class_definition(stream, prefix, classname, methods):
    stream.write ("struct _%sClass {\n" % classname)
    stream.write ("    GObjectClass parent_class;\n")

    for method in methods:
        dbus_method_name = method.getAttributeNode("name").nodeValue
        lc_method_name = camelcase_to_lower(dbus_method_name)
        c_impl_name = prefix + '_' + lc_method_name + '_impl'
        stream.write('    %s %s;\n' % (c_impl_name, lc_method_name))

    stream.write ("};\n\n")


def cmp_by_name(node1, node2):
    return cmp(node1.getAttributeNode("name").nodeValue,
               node2.getAttributeNode("name").nodeValue)


def do_method(method):
    # DoStuff (s -> u)
    dbus_method_name = method.getAttributeNode("name").nodeValue
    lc_method_name = camelcase_to_lower(dbus_method_name)
    # void tp_svc_thing_do_stuff (TpSvcThing *, const char *,
    #                             DBusGMethodInvocation *);
    c_method_name = prefix + '_' + lc_method_name
    # typedef void (*tp_svc_thing_do_stuff_impl) (TpSvcThing *, const char *,
    #                                             DBusGMethodInvocation *);
    c_impl_name = prefix + '_' + lc_method_name + '_impl'
    # void tp_svc_thing_return_from_do_stuff (DBusGMethodInvocation *, guint);
    ret_method_name = prefix + '_return_from_' + lc_method_name

    ret_count=0

    header = ''
    body = ''

    c_decl = "static void\n"
    method_decl = "typedef void (*" + c_impl_name + ') ('
    ret_decl = 'void\n'
    ret_body = '{\n  dbus_g_method_return (dbus_context'
    arg_doc = ''
    ret_arg_doc = ''

    tmp = c_method_name+' ('
    pad = ' ' * len(tmp)
    c_decl += tmp+classname+' *self'

    method_pad = ' ' * len(method_decl)
    method_decl += classname + ' *self'
    args = 'self'

    tmp = ret_method_name+' ('
    ret_pad = ' ' * len(tmp)
    ret_decl += tmp+'DBusGMethodInvocation *dbus_context'

    for i in method.getElementsByTagName("arg"):
        name =i.getAttribute("name")
        direction = i.getAttribute("direction")
        type = i.getAttribute("type")

        if not name and direction == "out":
            if ret_count==0:
                name = "ret"
            else:
                name = "ret"+str(ret_count)
            ret_count += 1

        gtype = type_to_gtype(type)[0]
        if type_to_gtype(type)[3]:
            gtype="const "+gtype
        if direction != "out":
            c_decl +=",\n"+pad+gtype+name
            method_decl +=",\n"+method_pad+gtype+name
            args += ', '+name
            arg_doc += (' * @' + name
                    + ': FIXME: document args in genginterface\n')
        else:
            ret_decl += ",\n"+ret_pad+gtype+name
            ret_body += ', '+name
            ret_arg_doc += (' * @' + name
                    + ': FIXME: document args in genginterface\n')

    c_decl += ",\n"+pad+"DBusGMethodInvocation *context)"
    method_decl += ",\n"+method_pad+"DBusGMethodInvocation *context);\n"
    args += ', context'

    ret_doc = ("""\
/**
 * %s:
 * @dbus_context: The D-Bus method invocation context
%s *
 * Return successfully by calling dbus_g_method_return (@dbus_context,
 * ...). This inline function is just a type-safe wrapper for
 * dbus_g_method_return.
 */
""" % (ret_method_name, ret_arg_doc))

    interface = method.parentNode.getAttribute("name");
    ret_decl += ')\n'
    ret_body += ');\n}\n'
    header += (ret_doc + 'static inline\n/**/\n' + ret_decl + ';\n'
            + 'static inline ' + ret_decl + ret_body)
    body += (
"""
/**
 * %(c_impl_name)s
 * @self: The object implementing this interface
%(arg_doc)s * @context: The D-Bus invocation context to use to return values
 *           or throw an error.
 *
 * Signature of an implementation of D-Bus method %(dbus_method_name)s
 * on interface %(interface)s
 */
""" % locals())

    body += c_decl+"\n{\n"
    body += "  %s impl = (%s_GET_CLASS (self)->%s);\n" % (
            c_impl_name, prefix.upper(), lc_method_name)
    body += "  if (impl)\n"
    body += "    (impl) (%s);\n" % args
    body += "  else\n"
    if not_implemented_func:
        body += "    %s (context);\n" % not_implemented_func
    else:
        # this seems as appropriate an error as any
        body += """\
    {
      GError e = { DBUS_GERROR, DBUS_GERROR_UNKNOWN_METHOD,
          "Method not implemented" };

      dbus_g_method_return_error (context, &e);
    }
"""
    body += "}\n\n"

    dg_method_name = prefix + '_' + dbus_gutils_wincaps_to_uscore(dbus_method_name)
    if dg_method_name != c_method_name:
        body += ("""\
#define %(dg_method_name)s %(c_method_name)s

""" % {'dg_method_name': dg_method_name, 'c_method_name': c_method_name })

    method_decl += 'void %s_implement_%s (%sClass *klass, %s impl);\n\n' \
                   % (prefix, lc_method_name, classname, c_impl_name)

    body += ("""\
/**
 * %s_implement_%s:
 * @klass: A class whose instances implement this interface
 * @impl: A callback used to implement the %s method
 *
 * Register an implementation for the %s method in the vtable of an
 * implementation of this interface. To be called from the interface
 * init function.
 */
""" % (prefix, lc_method_name, dbus_method_name, dbus_method_name))
    body += 'void\n%s_implement_%s (%sClass *klass, %s impl)\n{\n'\
            % (prefix, lc_method_name, classname, c_impl_name)
    body += '  klass->%s = impl;\n' % lc_method_name
    body += '}\n\n'

    return (method_decl, header, body)

if __name__ == '__main__':
    from getopt import gnu_getopt

    options, argv = gnu_getopt(sys.argv[1:], '',
                               ['filename=', 'signal-marshal-prefix=',
                                'include=',
                                'not-implemented-func='])

    try:
        classname = argv[1]
    except IndexError:
        cmdline_error()

    prefix = camelcase_to_lower(classname)

    basename = prefix.replace('_', '-')
    signal_marshal_prefix = prefix
    headers = []
    not_implemented_func = ''

    for option, value in options:
        if option == '--filename':
            basename = value
        elif option == '--signal-marshal-prefix':
            signal_marshal_prefix = value
        elif option == '--include':
            if value[0] not in '<"':
                value = '"%s"' % value
            headers.append(value)
        elif option == '--not-implemented-func':
            not_implemented_func = value

    outname_header = basename + ".h"
    outname_body = basename + ".c"
    outname_signal_marshal = basename + "-signals-marshal.list"

    header=open(outname_header,'w')
    body=open(outname_body, 'w')

    signal_marshal=open(outname_signal_marshal, 'w')

    try:
        dom = xml.dom.minidom.parse(argv[0])
    except IndexError:
        cmdline_error()

    signals = dom.getElementsByTagName("signal")
    signals.sort(cmp_by_name)
    methods = dom.getElementsByTagName("method")
    methods.sort(cmp_by_name)

    print_license(header, outname_header, "Header for " + classname, dom)
    print_license(body, outname_body, "Source for " + classname, dom)
    print_header_begin(header,prefix)

    print_class_declaration(header, prefix, classname, methods)

    # include my own header first, to ensure self-contained
    body.write(
"""#include "%s"

""" % outname_header)

    # required headers
    body.write(
"""#include <stdio.h>
#include <stdlib.h>

""")

    for h in headers:
        body.write('#include %s\n' % h)
    body.write('\n')

    if signal_marshal_prefix == prefix:
        body.write('#include "%s-signals-marshal.h"\n' % basename)
        # else assume the signal marshallers are declared in one of the headers

    body.write('const DBusGObjectInfo dbus_glib_%s_object_info;\n'
            % prefix)

    print_class_definition(body, prefix, classname, methods)

    if signals:
        body.write('enum {\n')
        for signal in signals:
            dbus_name = signal.getAttributeNode("name").nodeValue
            body.write('    SIGNAL_%s,\n' % (dbus_name))
        body.write('    N_SIGNALS\n};\nstatic guint signals[N_SIGNALS] = {0};\n\n')

    gtypename = '_TYPE_'.join(prefix.upper().split('_',1))

    body.write(
"""
static void
%(prefix)s_base_init (gpointer klass)
{
  static gboolean initialized = FALSE;

  if (!initialized)
    {
      initialized = TRUE;
""" % {'classname':classname, 'gtypename':gtypename, 'prefix':prefix, 'uprefix':prefix.upper()})

    header.write("\n")

    marshallers = {}
    for signal in signals:
        dbus_name = signal.getAttributeNode("name").nodeValue
        gtypelist = signal_to_gtype_list(signal)
        marshal_name = signal_to_marshal_name(signal, signal_marshal_prefix)

        body.write(
"""
      signals[SIGNAL_%s] =
      g_signal_new ("%s",
                    G_OBJECT_CLASS_TYPE (klass),
                    G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                    0,
                    NULL, NULL,
                    %s,
                    G_TYPE_NONE, %s);
""" % (dbus_name,
       (dbus_gutils_wincaps_to_uscore(dbus_name)).replace('_','-'),
       marshal_name,
       ', '.join([str(len(gtypelist))] + gtypelist)))

        if not marshal_name.startswith('g_cclosure_marshal_VOID__'):
            mtype = signal_to_marshal_type(signal)
            assert(len(mtype))
            marshallers[','.join(mtype)] = True

    for marshaller in marshallers:
        signal_marshal.write("VOID:"+marshaller+"\n")

    body.write(
"""
      dbus_g_object_type_install_info (%(prefix)s_get_type (), &dbus_glib_%(prefix)s_object_info);
    }
}

GType
%(prefix)s_get_type (void)
{
  static GType type = 0;

  if (G_UNLIKELY (type == 0)) {
    static const GTypeInfo info = {
      sizeof (%(classname)sClass),
      %(prefix)s_base_init, /* base_init */
      NULL, /* base_finalize */
      NULL, /* class_init */
      NULL, /* class_finalize */
      NULL, /* class_data */
      0,
      0, /* n_preallocs */
      NULL /* instance_init */
    };

    type = g_type_register_static (G_TYPE_INTERFACE, "%(classname)s", &info, 0);
  }

  return type;
}

""" % {'classname':classname,'prefix':prefix, 'uprefix':prefix.upper()})

    for method in methods:
        m, h, b = do_method(method)
        header.write(m + '\n')
        header.write(h)
        body.write(b)

    for signal in signals:
        h, b = signal_emit_stub(signal)
        header.write(h)
        body.write(b)

    header.write('\n')

    body.write("""\
#include "%s-glue.h"

""" % (basename))

    print_header_end(header,prefix)
    header.close()
    body.close()
