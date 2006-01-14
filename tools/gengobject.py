#!/usr/bin/python

import sys
import os.path
import xml.dom.minidom

def cmdline_error():
    print "usage: gen-gobject xmlfile classname"
    sys.exit(1)

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

def type_to_gtype(s):
    if s == 'y': #byte
        return ("guchar", "G_TYPE_UCHAR","UCHAR", False)
    if s == 'b': #boolean
        return ("gboolean", "G_TYPE_BOOLEAN","BOOLEAN", False)
    if s == 'n': #int16
        return ("gint", "G_TYPE_INT","INT", False)
    if s == 'q': #uint16
        return ("guint", "G_TYPE_UINT","UINT", False)
    if s == 'i': #int32
        return ("gint", "G_TYPE_INT","INT", False)
    if s == 'u': #uint32
        return ("guint", "G_TYPE_UINT","INT", False)
    if s == 'x': #int64
        return ("gint", "G_TYPE_INT64","INT64", False)
    if s == 't': #uint32
        return ("guint", "G_TYPE_UINT64","UINT64", False)
    if s == 'd': #double
        return ("gdouble", "G_TYPE_DOUBLE","DOUBLE", False)
    if s == 's': #string
        return ("gchar *", "G_TYPE_STRING", "STRING", True)
    if s == 'o': #object path
        return ("gchar *", "DBUS_TYPE_G_OBJECT_PATH", "STRING", True)
    if s == 'as':  #array of strings
        return ("gchar **", "G_TYPE_STRV", "BOXED", True)
    if s == 'v':  #variant
        return ("GValue *", "G_TYPE_VALUE", "BOXED", True)
    if s[:3] == 'a{s':  # dict mapping of strings to any marshalable value
        return ("GHashTable *", "DBUS_TYPE_G_STRING_HASHTABLE", "BOXED", False)
    if s == 'ay': #byte array
        return ("GArray *", "DBUS_TYPE_G_BYTE_ARRAY", "BOXED", True)
    if s == 'au': #uint array
        return ("GArray *", "DBUS_TYPE_G_UINT_ARRAY", "BOXED", True)
    if s == 'ai': #int array
        return ("GArray *", "DBUS_TYPE_G_INT_ARRAY", "BOXED", True)
    if s == 'ax': #int64 array
        return ("GArray *", "DBUS_TYPE_G_INT64_ARRAY", "BOXED", True)
    if s == 'at': #uint64 array
        return ("GArray *", "DBUS_TYPE_G_UINT64_ARRAY", "BOXED", True)
    if s == 'ad': #double array
        return ("GArray *", "DBUS_TYPE_G_DOUBLE_ARRAY", "BOXED", True)
    if s == 'ab': #boolean array
        return ("GArray *", "DBUS_TYPE_G_BOOLEAN_ARRAY", "BOXED", True)

    # we just don't know ..
    return ("gpointer", "G_TYPE_BOXED", "BOXED", True)


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
    mtype=signal_to_marshal_type(signal)
    if len(mtype):
        return prefix+'_marshal_VOID__' + '_'.join(mtype)
    else:
        return prefix+'_marshal_VOID__VOID'

def signal_to_gtype_list(signal):
    gtype=[]
    for i in signal.getElementsByTagName("arg"):
        name =i.getAttribute("name")
        type = i.getAttribute("type")
        gtype.append(type_to_gtype(type)[1])

    return gtype


def print_license(stream, filename, description):
    stream.write(
"""/*
 * %s - %s
 * Copyright (C) 2005 Collabora Ltd.
 * Copyright (C) 2005 Nokia Corporation
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

""" % (filename, description))

def print_header_begin(stream, prefix):
    guardname = '__'+prefix.upper()+'_H__'
    stream.write ("#ifndef "+guardname+"\n")
    stream.write ("#define "+guardname+"\n\n")

    stream.write ("#include <glib-object.h>\n\n")
    stream.write ("G_BEGIN_DECLS\n\n")

def print_header_end(stream, prefix):
    guardname = '__'+prefix.upper()+'_H__'
    stream.write ("\nG_END_DECLS\n\n")
    stream.write ("#endif /* #ifndef "+guardname+"*/\n")

def print_simple_class_defn(stream, prefix, classname):
    stream.write ("typedef struct _%s %s;\n" % (classname,classname))
    stream.write ("typedef struct _%sClass %sClass;\n\n" % (classname,classname))
    stream.write ("struct _%sClass {\n" % classname)
    stream.write ("    GObjectClass parent_class;\n")
    stream.write ("};\n\n")

    stream.write ("struct _%s {\n" % classname)
    stream.write ("    GObject parent;\n")
    stream.write ("};\n")

    stream.write(
"""
GType %(prefix)s_get_type(void);

""" % {'prefix':prefix,'uprefix':prefix.upper()})

    macro_prefix = prefix.upper().split('_',1)
    gtype = '_TYPE_'.join(macro_prefix)

    stream.write(
"""/* TYPE MACROS */
#define %(type)s \\
  (%(prefix)s_get_type())
#define %(main)s_%(sub)s(obj) \\
  (G_TYPE_CHECK_INSTANCE_CAST((obj), %(type)s, %(name)s))
#define %(main)s_%(sub)s_CLASS(klass) \\
  (G_TYPE_CHECK_CLASS_CAST((klass), %(type)s, %(name)sClass))
#define %(main)s_IS_%(sub)s(obj) \\
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), %(type)s))
#define %(main)s_IS_%(sub)s_CLASS(klass) \\
  (G_TYPE_CHECK_CLASS_TYPE((klass), %(type)s))
#define %(main)s_%(sub)s_GET_CLASS(obj) \\
  (G_TYPE_INSTANCE_GET_CLASS ((obj), %(type)s, %(name)sClass))

""" % {"main":macro_prefix[0], "sub":macro_prefix[1], "type":gtype, "name":classname, "prefix":prefix})

if __name__ == '__main__':
    try:
        classname = sys.argv[2]
    except IndexError:
        cmdline_error()

    prefix = camelcase_to_lower(classname)
    basename = prefix.replace('_','-')
    outname_header = basename + ".h"
    outname_body = basename + ".c"
    outname_signal_marshal = basename + "-signals-marshal.list"

    header=open(outname_header,'w')
    body=open(outname_body, 'w')
    signal_marshal=open(outname_signal_marshal, 'w')

    try:
        dom = xml.dom.minidom.parse(sys.argv[1])
    except IndexError:
        cmdline_error()

    signals = dom.getElementsByTagName("signal")
    signals.sort()
    methods = dom.getElementsByTagName("method")
    methods.sort()

    print_license(header, outname_header, "Header for " + classname)
    print_license(body, outname_body, "Source for " + classname)
    print_header_begin(header,prefix)

    print_simple_class_defn(header, prefix, classname)

    body.write(
"""#include <dbus/dbus-glib.h>
#include <stdio.h>
#include <stdlib.h>

#include "%s"
#include "%s-signals-marshal.h"

#include "%s-glue.h"

""" % (outname_header, basename, basename))

    body.write(
"""G_DEFINE_TYPE(%(classname)s, %(prefix)s, G_TYPE_OBJECT)

""" % {'classname':classname, 'prefix':prefix})

    body.write("/* signal enum */\nenum\n{\n")
    for signal in signals:
        dbus_name = signal.getAttributeNode("name").nodeValue
        body.write("    %s,\n" % camelcase_to_upper(dbus_name) )
    body.write("    LAST_SIGNAL\n};\n\n")
    body.write("static guint signals[LAST_SIGNAL] = {0};\n\n")

    gtypename = '_TYPE_'.join(prefix.upper().split('_',1))

    body.write("""/* private structure */
typedef struct _%(classname)sPrivate %(classname)sPrivate;

struct _%(classname)sPrivate
{
  gboolean dispose_has_run;
};

#define %(uprefix)s_GET_PRIVATE(o) \
    (G_TYPE_INSTANCE_GET_PRIVATE ((o), %(gtypename)s, %(classname)sPrivate))
""" % {'classname':classname, 'uprefix':prefix.upper(), 'gtypename':gtypename})

    body.write(
"""
static void
%(prefix)s_init (%(classname)s *obj)
{
  %(classname)sPrivate *priv = %(uprefix)s_GET_PRIVATE (obj);

  /* allocate any data required by the object here */
}

static void %(prefix)s_dispose (GObject *object);
static void %(prefix)s_finalize (GObject *object);

static void
%(prefix)s_class_init (%(classname)sClass *%(prefix)s_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (%(prefix)s_class);

  g_type_class_add_private (%(prefix)s_class, sizeof (%(classname)sPrivate));

  object_class->dispose = %(prefix)s_dispose;
  object_class->finalize = %(prefix)s_finalize;
""" % {"prefix":prefix, "classname":classname, 'uprefix':prefix.upper()})

    for signal in signals:
        mtype = signal_to_marshal_type(signal)
        if len(mtype):
            signal_marshal.write("VOID:"+','.join(mtype)+"\n")
        else:
            signal_marshal.write("VOID:VOID\n")

    header.write("\n")

    for signal in signals:
        dbus_name = signal.getAttributeNode("name").nodeValue
        gtypelist=signal_to_gtype_list(signal)

        body.write(
"""
  signals[%s] =
    g_signal_new ("%s",
                  G_OBJECT_CLASS_TYPE (%s_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  %s,
                  G_TYPE_NONE, %s);
""" % (camelcase_to_upper(dbus_name),
            camelcase_to_lower(dbus_name).replace('_','-'),
            prefix,
            signal_to_marshal_name(signal,prefix), ', '.join([str(len(gtypelist))] + gtypelist)))

    body.write(
"""
  dbus_g_object_type_install_info (G_TYPE_FROM_CLASS (%(prefix)s_class), &dbus_glib_%(prefix)s_object_info);
}

void
%(prefix)s_dispose (GObject *object)
{
  %(classname)s *self = %(uprefix)s (object);
  %(classname)sPrivate *priv = %(uprefix)s_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  /* release any references held by the object here */

  if (G_OBJECT_CLASS (%(prefix)s_parent_class)->dispose)
    G_OBJECT_CLASS (%(prefix)s_parent_class)->dispose (object);
}

void
%(prefix)s_finalize (GObject *object)
{
  %(classname)s *self = %(uprefix)s (object);
  %(classname)sPrivate *priv = %(uprefix)s_GET_PRIVATE (self);

  /* free any data held directly by the object here */

  G_OBJECT_CLASS (%(prefix)s_parent_class)->finalize (object);
}


""" % {'classname':classname,'prefix':prefix, 'uprefix':prefix.upper()})

    for method in methods:
        dbus_method_name = method.getAttributeNode("name").nodeValue
        c_method_name = prefix + '_' + camelcase_to_lower(dbus_method_name)
        c_decl = 'gboolean '+c_method_name+' ('+classname+' *obj'
        async=False
        ret_count=0

        for i in method.getElementsByTagName("annotation"):
            if i.getAttribute("name") == "org.freedesktop.DBus.GLib.Async":
                async=True

        for i in method.getElementsByTagName("arg"):
            name =i.getAttribute("name")
            direction = i.getAttribute("direction")
            type = i.getAttribute("type")

            if async and direction=="out":
                continue

            if not name and direction == "out":
                if ret_count==0:
                    name = "ret"
                else:
                    name = "ret"+str(ret_count)
                ret_count += 1

            gtype = type_to_gtype(type)[0]
            if direction =="out":
                gtype+='*'
            else:
                if type_to_gtype(type)[3]:
                    gtype="const "+gtype
            c_decl +=", "+gtype+" "+name
        if async:
            c_decl += ", DBusGMethodInvocation *context)"
        else:
            c_decl += ", GError **error)"

        interface = method.parentNode.getAttribute("name");
        header.write(c_decl+";\n")
        body.write(
"""
/**
 * %(c_method_name)s
 *
 * Implements DBus method %(method)s
 * on interface %(interface)s
 *""" % {'c_method_name':c_method_name, 'method':dbus_method_name, 'interface':interface})
        if async:
            body.write(
"""
 * @context: The DBUS invocation context to use to return values
 *           or throw an error.
 */
""")
        else:
            body.write(
"""
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
""")
        body.write(c_decl+"\n{\n  return TRUE;\n}\n\n")

    header.write('\n')

    print_header_end(header,prefix)
    header.close()
    body.close()
