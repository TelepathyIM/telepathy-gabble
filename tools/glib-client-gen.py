#!/usr/bin/python

# glib-client-gen.py: "I Can't Believe It's Not dbus-binding-tool"
#
# Generate GLib client wrappers from the Telepathy specification.
# The master copy of this program is in the telepathy-glib repository -
# please make any changes there.
#
# Copyright (C) 2006-2008 Collabora Ltd. <http://www.collabora.co.uk/>
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
import os.path
import xml.dom.minidom
from getopt import gnu_getopt

from libtpcodegen import file_set_contents
from libglibcodegen import Signature, type_to_gtype, cmp_by_name, \
        get_docstring, xml_escape, get_deprecated


NS_TP = "http://telepathy.freedesktop.org/wiki/DbusSpec#extensions-v0"

class Generator(object):

    def __init__(self, dom, prefix, basename, opts):
        self.dom = dom
        self.__header = []
        self.__body = []
        self.__docs = []

        self.prefix_lc = prefix.lower()
        self.prefix_uc = prefix.upper()
        self.prefix_mc = prefix.replace('_', '')
        self.basename = basename
        self.group = opts.get('--group', None)
        self.iface_quark_prefix = opts.get('--iface-quark-prefix', None)
        self.tp_proxy_api = tuple(map(int,
                opts.get('--tp-proxy-api', '0').split('.')))
        self.proxy_cls = opts.get('--subclass', 'TpProxy') + ' *'
        self.proxy_arg = opts.get('--subclass', 'void') + ' *'
        self.proxy_assert = opts.get('--subclass-assert', 'TP_IS_PROXY')
        self.proxy_doc = ('A #%s or subclass'
            % opts.get('--subclass', 'TpProxy'))
        if self.proxy_arg == 'void *':
            self.proxy_arg = 'gpointer '

        self.reentrant_symbols = set()
        try:
            filename = opts['--generate-reentrant']
            with open(filename, 'r') as f:
                for line in f.readlines():
                    self.reentrant_symbols.add(line.strip())
        except KeyError:
            pass

        self.deprecate_reentrant = opts.get('--deprecate-reentrant', None)
        self.deprecation_attribute = opts.get('--deprecation-attribute',
                'G_GNUC_DEPRECATED')

        self.guard = opts.get('--guard', None)

    def h(self, s):
        if isinstance(s, unicode):
            s = s.encode('utf-8')
        self.__header.append(s)

    def b(self, s):
        if isinstance(s, unicode):
            s = s.encode('utf-8')
        self.__body.append(s)

    def d(self, s):
        if isinstance(s, unicode):
            s = s.encode('utf-8')
        self.__docs.append(s)

    def get_iface_quark(self):
        assert self.iface_dbus is not None
        assert self.iface_uc is not None
        if self.iface_quark_prefix is None:
            return 'g_quark_from_static_string (\"%s\")' % self.iface_dbus
        else:
            return '%s_%s' % (self.iface_quark_prefix, self.iface_uc)

    def do_signal(self, iface, signal):
        iface_lc = iface.lower()

        member = signal.getAttribute('name')
        member_lc = signal.getAttribute('tp:name-for-bindings')
        if member != member_lc.replace('_', ''):
            raise AssertionError('Signal %s tp:name-for-bindings (%s) does '
                    'not match' % (member, member_lc))
        member_lc = member_lc.lower()
        member_uc = member_lc.upper()

        arg_count = 0
        args = []
        out_args = []

        for arg in signal.getElementsByTagName('arg'):
            name = arg.getAttribute('name')
            type = arg.getAttribute('type')
            tp_type = arg.getAttribute('tp:type')

            if not name:
                name = 'arg%u' % arg_count
                arg_count += 1
            else:
                name = 'arg_%s' % name

            info = type_to_gtype(type)
            args.append((name, info, tp_type, arg))

        callback_name = ('%s_%s_signal_callback_%s'
                         % (self.prefix_lc, iface_lc, member_lc))
        collect_name = ('_%s_%s_collect_args_of_%s'
                        % (self.prefix_lc, iface_lc, member_lc))
        invoke_name = ('_%s_%s_invoke_callback_for_%s'
                       % (self.prefix_lc, iface_lc, member_lc))

        # Example:
        #
        # typedef void (*tp_cli_connection_signal_callback_new_channel)
        #   (TpConnection *proxy, const gchar *arg_object_path,
        #   const gchar *arg_channel_type, guint arg_handle_type,
        #   guint arg_handle, gboolean arg_suppress_handler,
        #   gpointer user_data, GObject *weak_object);

        self.d('/**')
        self.d(' * %s:' % callback_name)
        self.d(' * @proxy: The proxy on which %s_%s_connect_to_%s ()'
               % (self.prefix_lc, iface_lc, member_lc))
        self.d(' *  was called')

        for arg in args:
            name, info, tp_type, elt = arg
            ctype, gtype, marshaller, pointer = info

            docs = get_docstring(elt) or '(Undocumented)'

            if ctype == 'guint ' and tp_type != '':
                docs +=  ' (#%s)' % ('Tp' + tp_type.replace('_', ''))

            self.d(' * @%s: %s' % (name, xml_escape(docs)))

        self.d(' * @user_data: User-supplied data')
        self.d(' * @weak_object: User-supplied weakly referenced object')
        self.d(' *')
        self.d(' * Represents the signature of a callback for the signal %s.'
               % member)
        self.d(' */')
        self.d('')

        self.h('typedef void (*%s) (%sproxy,'
               % (callback_name, self.proxy_cls))

        for arg in args:
            name, info, tp_type, elt = arg
            ctype, gtype, marshaller, pointer = info

            const = pointer and 'const ' or ''

            self.h('    %s%s%s,' % (const, ctype, name))

        self.h('    gpointer user_data, GObject *weak_object);')

        if args:
            self.b('static void')
            self.b('%s (DBusGProxy *proxy G_GNUC_UNUSED,' % collect_name)

            for arg in args:
                name, info, tp_type, elt = arg
                ctype, gtype, marshaller, pointer = info

                const = pointer and 'const ' or ''

                self.b('    %s%s%s,' % (const, ctype, name))

            self.b('    TpProxySignalConnection *sc)')
            self.b('{')
            self.b('  GValueArray *args = g_value_array_new (%d);' % len(args))
            self.b('  GValue blank = { 0 };')
            self.b('  guint i;')
            self.b('')
            self.b('  g_value_init (&blank, G_TYPE_INT);')
            self.b('')
            self.b('  for (i = 0; i < %d; i++)' % len(args))
            self.b('    g_value_array_append (args, &blank);')
            self.b('')

            for i, arg in enumerate(args):
                name, info, tp_type, elt = arg
                ctype, gtype, marshaller, pointer = info

                self.b('  g_value_unset (args->values + %d);' % i)
                self.b('  g_value_init (args->values + %d, %s);' % (i, gtype))

                if gtype == 'G_TYPE_STRING':
                    self.b('  g_value_set_string (args->values + %d, %s);'
                           % (i, name))
                elif marshaller == 'BOXED':
                    self.b('  g_value_set_boxed (args->values + %d, %s);'
                           % (i, name))
                elif gtype == 'G_TYPE_UCHAR':
                    self.b('  g_value_set_uchar (args->values + %d, %s);'
                           % (i, name))
                elif gtype == 'G_TYPE_BOOLEAN':
                    self.b('  g_value_set_boolean (args->values + %d, %s);'
                           % (i, name))
                elif gtype == 'G_TYPE_INT':
                    self.b('  g_value_set_int (args->values + %d, %s);'
                           % (i, name))
                elif gtype == 'G_TYPE_UINT':
                    self.b('  g_value_set_uint (args->values + %d, %s);'
                           % (i, name))
                elif gtype == 'G_TYPE_INT64':
                    self.b('  g_value_set_int (args->values + %d, %s);'
                           % (i, name))
                elif gtype == 'G_TYPE_UINT64':
                    self.b('  g_value_set_uint64 (args->values + %d, %s);'
                           % (i, name))
                elif gtype == 'G_TYPE_DOUBLE':
                    self.b('  g_value_set_double (args->values + %d, %s);'
                           % (i, name))
                else:
                    assert False, ("Don't know how to put %s in a GValue"
                                   % gtype)
                self.b('')

            self.b('  tp_proxy_signal_connection_v0_take_results (sc, args);')
            self.b('}')

        self.b('static void')
        self.b('%s (TpProxy *tpproxy,' % invoke_name)
        self.b('    GError *error G_GNUC_UNUSED,')
        self.b('    GValueArray *args,')
        self.b('    GCallback generic_callback,')
        self.b('    gpointer user_data,')
        self.b('    GObject *weak_object)')
        self.b('{')
        self.b('  %s callback =' % callback_name)
        self.b('      (%s) generic_callback;' % callback_name)
        self.b('')
        self.b('  if (callback != NULL)')
        self.b('    callback (g_object_ref (tpproxy),')

        # FIXME: factor out into a function
        for i, arg in enumerate(args):
            name, info, tp_type, elt = arg
            ctype, gtype, marshaller, pointer = info

            if marshaller == 'BOXED':
                self.b('      g_value_get_boxed (args->values + %d),' % i)
            elif gtype == 'G_TYPE_STRING':
                self.b('      g_value_get_string (args->values + %d),' % i)
            elif gtype == 'G_TYPE_UCHAR':
                self.b('      g_value_get_uchar (args->values + %d),' % i)
            elif gtype == 'G_TYPE_BOOLEAN':
                self.b('      g_value_get_boolean (args->values + %d),' % i)
            elif gtype == 'G_TYPE_UINT':
                self.b('      g_value_get_uint (args->values + %d),' % i)
            elif gtype == 'G_TYPE_INT':
                self.b('      g_value_get_int (args->values + %d),' % i)
            elif gtype == 'G_TYPE_UINT64':
                self.b('      g_value_get_uint64 (args->values + %d),' % i)
            elif gtype == 'G_TYPE_INT64':
                self.b('      g_value_get_int64 (args->values + %d),' % i)
            elif gtype == 'G_TYPE_DOUBLE':
                self.b('      g_value_get_double (args->values + %d),' % i)
            else:
                assert False, "Don't know how to get %s from a GValue" % gtype

        self.b('      user_data,')
        self.b('      weak_object);')
        self.b('')

        if len(args) > 0:
            self.b('  g_value_array_free (args);')
        else:
            self.b('  if (args != NULL)')
            self.b('    g_value_array_free (args);')
            self.b('')

        self.b('  g_object_unref (tpproxy);')
        self.b('}')

        # Example:
        #
        # TpProxySignalConnection *
        #   tp_cli_connection_connect_to_new_channel
        #   (TpConnection *proxy,
        #   tp_cli_connection_signal_callback_new_channel callback,
        #   gpointer user_data,
        #   GDestroyNotify destroy);
        #
        # destroy is invoked when the signal becomes disconnected. This
        # is either because the signal has been disconnected explicitly
        # by the user, because the TpProxy has become invalid and
        # emitted the 'invalidated' signal, or because the weakly referenced
        # object has gone away.

        self.d('/**')
        self.d(' * %s_%s_connect_to_%s:'
               % (self.prefix_lc, iface_lc, member_lc))
        self.d(' * @proxy: %s' % self.proxy_doc)
        self.d(' * @callback: Callback to be called when the signal is')
        self.d(' *   received')
        self.d(' * @user_data: User-supplied data for the callback')
        self.d(' * @destroy: Destructor for the user-supplied data, which')
        self.d(' *   will be called when this signal is disconnected, or')
        self.d(' *   before this function returns %NULL')
        self.d(' * @weak_object: A #GObject which will be weakly referenced; ')
        self.d(' *   if it is destroyed, this callback will automatically be')
        self.d(' *   disconnected')
        self.d(' * @error: If not %NULL, used to raise an error if %NULL is')
        self.d(' *   returned')
        self.d(' *')
        self.d(' * Connect a handler to the signal %s.' % member)
        self.d(' *')
        self.d(' * %s' % xml_escape(get_docstring(signal) or '(Undocumented)'))
        self.d(' *')
        self.d(' * Returns: a #TpProxySignalConnection containing all of the')
        self.d(' * above, which can be used to disconnect the signal; or')
        self.d(' * %NULL if the proxy does not have the desired interface')
        self.d(' * or has become invalid.')
        self.d(' */')
        self.d('')

        self.h('TpProxySignalConnection *%s_%s_connect_to_%s (%sproxy,'
               % (self.prefix_lc, iface_lc, member_lc, self.proxy_arg))
        self.h('    %s callback,' % callback_name)
        self.h('    gpointer user_data,')
        self.h('    GDestroyNotify destroy,')
        self.h('    GObject *weak_object,')
        self.h('    GError **error);')
        self.h('')

        self.b('TpProxySignalConnection *')
        self.b('%s_%s_connect_to_%s (%sproxy,'
               % (self.prefix_lc, iface_lc, member_lc, self.proxy_arg))
        self.b('    %s callback,' % callback_name)
        self.b('    gpointer user_data,')
        self.b('    GDestroyNotify destroy,')
        self.b('    GObject *weak_object,')
        self.b('    GError **error)')
        self.b('{')
        self.b('  GType expected_types[%d] = {' % (len(args) + 1))

        for arg in args:
            name, info, tp_type, elt = arg
            ctype, gtype, marshaller, pointer = info

            self.b('      %s,' % gtype)

        self.b('      G_TYPE_INVALID };')
        self.b('')
        self.b('  g_return_val_if_fail (%s (proxy), NULL);'
               % self.proxy_assert)
        self.b('  g_return_val_if_fail (callback != NULL, NULL);')
        self.b('')
        self.b('  return tp_proxy_signal_connection_v0_new ((TpProxy *) proxy,')
        self.b('      %s, \"%s\",' % (self.get_iface_quark(), member))
        self.b('      expected_types,')

        if args:
            self.b('      G_CALLBACK (%s),' % collect_name)
        else:
            self.b('      NULL, /* no args => no collector function */')

        self.b('      %s,' % invoke_name)
        self.b('      G_CALLBACK (callback), user_data, destroy,')
        self.b('      weak_object, error);')
        self.b('}')
        self.b('')

    def do_method(self, iface, method):
        iface_lc = iface.lower()

        member = method.getAttribute('name')
        member_lc = method.getAttribute('tp:name-for-bindings')
        if member != member_lc.replace('_', ''):
            raise AssertionError('Method %s tp:name-for-bindings (%s) does '
                    'not match' % (member, member_lc))
        member_lc = member_lc.lower()
        member_uc = member_lc.upper()

        in_count = 0
        ret_count = 0
        in_args = []
        out_args = []

        for arg in method.getElementsByTagName('arg'):
            name = arg.getAttribute('name')
            direction = arg.getAttribute('direction')
            type = arg.getAttribute('type')
            tp_type = arg.getAttribute('tp:type')

            if direction != 'out':
                if not name:
                    name = 'in%u' % in_count
                    in_count += 1
                else:
                    name = 'in_%s' % name
            else:
                if not name:
                    name = 'out%u' % ret_count
                    ret_count += 1
                else:
                    name = 'out_%s' % name

            info = type_to_gtype(type)
            if direction != 'out':
                in_args.append((name, info, tp_type, arg))
            else:
                out_args.append((name, info, tp_type, arg))

        # Async reply callback type

        # Example:
        # void (*tp_cli_properties_interface_callback_for_get_properties)
        #   (TpProxy *proxy,
        #       const GPtrArray *out0,
        #       const GError *error,
        #       gpointer user_data,
        #       GObject *weak_object);

        self.d('/**')
        self.d(' * %s_%s_callback_for_%s:'
               % (self.prefix_lc, iface_lc, member_lc))
        self.d(' * @proxy: the proxy on which the call was made')

        for arg in out_args:
            name, info, tp_type, elt = arg
            ctype, gtype, marshaller, pointer = info

            docs = xml_escape(get_docstring(elt) or '(Undocumented)')

            if ctype == 'guint ' and tp_type != '':
                docs +=  ' (#%s)' % ('Tp' + tp_type.replace('_', ''))

            self.d(' * @%s: Used to return an \'out\' argument if @error is '
                   '%%NULL: %s'
                   % (name, docs))

        self.d(' * @error: %NULL on success, or an error on failure')
        self.d(' * @user_data: user-supplied data')
        self.d(' * @weak_object: user-supplied object')
        self.d(' *')
        self.d(' * Signature of the callback called when a %s method call'
               % member)
        self.d(' * succeeds or fails.')

        deprecated = method.getElementsByTagName('tp:deprecated')
        if deprecated:
            d = deprecated[0]
            self.d(' *')
            self.d(' * Deprecated: %s' % xml_escape(get_deprecated(d)))

        self.d(' */')
        self.d('')

        callback_name = '%s_%s_callback_for_%s' % (self.prefix_lc, iface_lc,
                                                   member_lc)

        self.h('typedef void (*%s) (%sproxy,'
               % (callback_name, self.proxy_cls))

        for arg in out_args:
            name, info, tp_type, elt = arg
            ctype, gtype, marshaller, pointer = info
            const = pointer and 'const ' or ''

            self.h('    %s%s%s,' % (const, ctype, name))

        self.h('    const GError *error, gpointer user_data,')
        self.h('    GObject *weak_object);')
        self.h('')

        # Async callback implementation

        invoke_callback = '_%s_%s_invoke_callback_%s' % (self.prefix_lc,
                                                         iface_lc,
                                                         member_lc)

        collect_callback = '_%s_%s_collect_callback_%s' % (self.prefix_lc,
                                                           iface_lc,
                                                           member_lc)

        # The callback called by dbus-glib; this ends the call and collects
        # the results into a GValueArray.
        self.b('static void')
        self.b('%s (DBusGProxy *proxy,' % collect_callback)
        self.b('    DBusGProxyCall *call,')
        self.b('    gpointer user_data)')
        self.b('{')
        self.b('  GError *error = NULL;')

        if len(out_args) > 0:
            self.b('  GValueArray *args;')
            self.b('  GValue blank = { 0 };')
            self.b('  guint i;')

            for arg in out_args:
                name, info, tp_type, elt = arg
                ctype, gtype, marshaller, pointer = info

                # "We handle variants specially; the caller is expected to
                # have already allocated storage for them". Thanks,
                # dbus-glib...
                if gtype == 'G_TYPE_VALUE':
                    self.b('  GValue *%s = g_new0 (GValue, 1);' % name)
                else:
                    self.b('  %s%s;' % (ctype, name))

        self.b('')
        self.b('  dbus_g_proxy_end_call (proxy, call, &error,')

        for arg in out_args:
            name, info, tp_type, elt = arg
            ctype, gtype, marshaller, pointer = info

            if gtype == 'G_TYPE_VALUE':
                self.b('      %s, %s,' % (gtype, name))
            else:
                self.b('      %s, &%s,' % (gtype, name))

        self.b('      G_TYPE_INVALID);')

        if len(out_args) == 0:
            self.b('  tp_proxy_pending_call_v0_take_results (user_data, error,'
                   'NULL);')
        else:
            self.b('')
            self.b('  if (error != NULL)')
            self.b('    {')
            self.b('      tp_proxy_pending_call_v0_take_results (user_data, error,')
            self.b('          NULL);')

            for arg in out_args:
                name, info, tp_type, elt = arg
                ctype, gtype, marshaller, pointer = info
                if gtype == 'G_TYPE_VALUE':
                    self.b('      g_free (%s);' % name)

            self.b('      return;')
            self.b('    }')
            self.b('')
            self.b('  args = g_value_array_new (%d);' % len(out_args))
            self.b('  g_value_init (&blank, G_TYPE_INT);')
            self.b('')
            self.b('  for (i = 0; i < %d; i++)' % len(out_args))
            self.b('    g_value_array_append (args, &blank);')

            for i, arg in enumerate(out_args):
                name, info, tp_type, elt = arg
                ctype, gtype, marshaller, pointer = info

                self.b('')
                self.b('  g_value_unset (args->values + %d);' % i)
                self.b('  g_value_init (args->values + %d, %s);' % (i, gtype))

                if gtype == 'G_TYPE_STRING':
                    self.b('  g_value_take_string (args->values + %d, %s);'
                           % (i, name))
                elif marshaller == 'BOXED':
                    self.b('  g_value_take_boxed (args->values + %d, %s);'
                            % (i, name))
                elif gtype == 'G_TYPE_UCHAR':
                    self.b('  g_value_set_uchar (args->values + %d, %s);'
                            % (i, name))
                elif gtype == 'G_TYPE_BOOLEAN':
                    self.b('  g_value_set_boolean (args->values + %d, %s);'
                            % (i, name))
                elif gtype == 'G_TYPE_INT':
                    self.b('  g_value_set_int (args->values + %d, %s);'
                            % (i, name))
                elif gtype == 'G_TYPE_UINT':
                    self.b('  g_value_set_uint (args->values + %d, %s);'
                            % (i, name))
                elif gtype == 'G_TYPE_INT64':
                    self.b('  g_value_set_int (args->values + %d, %s);'
                            % (i, name))
                elif gtype == 'G_TYPE_UINT64':
                    self.b('  g_value_set_uint (args->values + %d, %s);'
                            % (i, name))
                elif gtype == 'G_TYPE_DOUBLE':
                    self.b('  g_value_set_double (args->values + %d, %s);'
                            % (i, name))
                else:
                    assert False, ("Don't know how to put %s in a GValue"
                                   % gtype)

            self.b('  tp_proxy_pending_call_v0_take_results (user_data, '
                   'NULL, args);')

        self.b('}')

        self.b('static void')
        self.b('%s (TpProxy *self,' % invoke_callback)
        self.b('    GError *error,')
        self.b('    GValueArray *args,')
        self.b('    GCallback generic_callback,')
        self.b('    gpointer user_data,')
        self.b('    GObject *weak_object)')
        self.b('{')
        self.b('  %s callback = (%s) generic_callback;'
               % (callback_name, callback_name))
        self.b('')
        self.b('  if (error != NULL)')
        self.b('    {')
        self.b('      callback ((%s) self,' % self.proxy_cls)

        for arg in out_args:
            name, info, tp_type, elt = arg
            ctype, gtype, marshaller, pointer = info

            if marshaller == 'BOXED' or pointer:
                self.b('          NULL,')
            elif gtype == 'G_TYPE_DOUBLE':
                self.b('          0.0,')
            else:
                self.b('          0,')

        self.b('          error, user_data, weak_object);')
        self.b('      g_error_free (error);')
        self.b('      return;')
        self.b('    }')

        self.b('  callback ((%s) self,' % self.proxy_cls)

        # FIXME: factor out into a function
        for i, arg in enumerate(out_args):
            name, info, tp_type, elt = arg
            ctype, gtype, marshaller, pointer = info

            if marshaller == 'BOXED':
                self.b('      g_value_get_boxed (args->values + %d),' % i)
            elif gtype == 'G_TYPE_STRING':
                self.b('      g_value_get_string (args->values + %d),' % i)
            elif gtype == 'G_TYPE_UCHAR':
                self.b('      g_value_get_uchar (args->values + %d),' % i)
            elif gtype == 'G_TYPE_BOOLEAN':
                self.b('      g_value_get_boolean (args->values + %d),' % i)
            elif gtype == 'G_TYPE_UINT':
                self.b('      g_value_get_uint (args->values + %d),' % i)
            elif gtype == 'G_TYPE_INT':
                self.b('      g_value_get_int (args->values + %d),' % i)
            elif gtype == 'G_TYPE_UINT64':
                self.b('      g_value_get_uint64 (args->values + %d),' % i)
            elif gtype == 'G_TYPE_INT64':
                self.b('      g_value_get_int64 (args->values + %d),' % i)
            elif gtype == 'G_TYPE_DOUBLE':
                self.b('      g_value_get_double (args->values + %d),' % i)
            else:
                assert False, "Don't know how to get %s from a GValue" % gtype

        self.b('      error, user_data, weak_object);')
        self.b('')

        if len(out_args) > 0:
            self.b('  g_value_array_free (args);')
        else:
            self.b('  if (args != NULL)')
            self.b('    g_value_array_free (args);')

        self.b('}')
        self.b('')

        # Async stub

        # Example:
        # TpProxyPendingCall *
        #   tp_cli_properties_interface_call_get_properties
        #   (gpointer proxy,
        #   gint timeout_ms,
        #   const GArray *in_properties,
        #   tp_cli_properties_interface_callback_for_get_properties callback,
        #   gpointer user_data,
        #   GDestroyNotify *destructor);

        self.h('TpProxyPendingCall *%s_%s_call_%s (%sproxy,'
               % (self.prefix_lc, iface_lc, member_lc, self.proxy_arg))
        self.h('    gint timeout_ms,')

        self.d('/**')
        self.d(' * %s_%s_call_%s:'
               % (self.prefix_lc, iface_lc, member_lc))
        self.d(' * @proxy: the #TpProxy')
        self.d(' * @timeout_ms: the timeout in milliseconds, or -1 to use the')
        self.d(' *   default')

        for arg in in_args:
            name, info, tp_type, elt = arg
            ctype, gtype, marshaller, pointer = info

            docs = xml_escape(get_docstring(elt) or '(Undocumented)')

            if ctype == 'guint ' and tp_type != '':
                docs +=  ' (#%s)' % ('Tp' + tp_type.replace('_', ''))

            self.d(' * @%s: Used to pass an \'in\' argument: %s'
                   % (name, docs))

        self.d(' * @callback: called when the method call succeeds or fails;')
        self.d(' *   may be %NULL to make a "fire and forget" call with no ')
        self.d(' *   reply tracking')
        self.d(' * @user_data: user-supplied data passed to the callback;')
        self.d(' *   must be %NULL if @callback is %NULL')
        self.d(' * @destroy: called with the user_data as argument, after the')
        self.d(' *   call has succeeded, failed or been cancelled;')
        self.d(' *   must be %NULL if @callback is %NULL')
        self.d(' * @weak_object: If not %NULL, a #GObject which will be ')
        self.d(' *   weakly referenced; if it is destroyed, this call ')
        self.d(' *   will automatically be cancelled. Must be %NULL if ')
        self.d(' *   @callback is %NULL')
        self.d(' *')
        self.d(' * Start a %s method call.' % member)
        self.d(' *')
        self.d(' * %s' % xml_escape(get_docstring(method) or '(Undocumented)'))
        self.d(' *')
        self.d(' * Returns: a #TpProxyPendingCall representing the call in')
        self.d(' *  progress. It is borrowed from the object, and will become')
        self.d(' *  invalid when the callback is called, the call is')
        self.d(' *  cancelled or the #TpProxy becomes invalid.')

        deprecated = method.getElementsByTagName('tp:deprecated')
        if deprecated:
            d = deprecated[0]
            self.d(' *')
            self.d(' * Deprecated: %s' % xml_escape(get_deprecated(d)))

        self.d(' */')
        self.d('')

        self.b('TpProxyPendingCall *\n%s_%s_call_%s (%sproxy,'
               % (self.prefix_lc, iface_lc, member_lc, self.proxy_arg))
        self.b('    gint timeout_ms,')

        for arg in in_args:
            name, info, tp_type, elt = arg
            ctype, gtype, marshaller, pointer = info

            const = pointer and 'const ' or ''

            self.h('    %s%s%s,' % (const, ctype, name))
            self.b('    %s%s%s,' % (const, ctype, name))

        self.h('    %s callback,' % callback_name)
        self.h('    gpointer user_data,')
        self.h('    GDestroyNotify destroy,')
        self.h('    GObject *weak_object);')
        self.h('')

        self.b('    %s callback,' % callback_name)
        self.b('    gpointer user_data,')
        self.b('    GDestroyNotify destroy,')
        self.b('    GObject *weak_object)')
        self.b('{')
        self.b('  GError *error = NULL;')
        self.b('  GQuark interface = %s;' % self.get_iface_quark())
        self.b('  DBusGProxy *iface;')
        self.b('')
        self.b('  g_return_val_if_fail (%s (proxy), NULL);'
               % self.proxy_assert)
        self.b('  g_return_val_if_fail (callback != NULL || '
               'user_data == NULL, NULL);')
        self.b('  g_return_val_if_fail (callback != NULL || '
               'destroy == NULL, NULL);')
        self.b('  g_return_val_if_fail (callback != NULL || '
               'weak_object == NULL, NULL);')
        self.b('')
        self.b('  G_GNUC_BEGIN_IGNORE_DEPRECATIONS')
        self.b('  iface = tp_proxy_borrow_interface_by_id (')
        self.b('      (TpProxy *) proxy,')
        self.b('      interface, &error);')
        self.b('  G_GNUC_END_IGNORE_DEPRECATIONS')
        self.b('')
        self.b('  if (iface == NULL)')
        self.b('    {')
        self.b('      if (callback != NULL)')
        self.b('        callback (proxy,')

        for arg in out_args:
            name, info, tp_type, elt = arg
            ctype, gtype, marshaller, pointer = info

            if pointer:
                self.b('            NULL,')
            else:
                self.b('            0,')

        self.b('            error, user_data, weak_object);')
        self.b('')
        self.b('      if (destroy != NULL)')
        self.b('        destroy (user_data);')
        self.b('')
        self.b('      g_error_free (error);')
        self.b('      return NULL;')
        self.b('    }')
        self.b('')
        self.b('  if (callback == NULL)')
        self.b('    {')
        self.b('      dbus_g_proxy_call_no_reply (iface, "%s",' % member)

        for arg in in_args:
            name, info, tp_type, elt = arg
            ctype, gtype, marshaller, pointer = info

            const = pointer and 'const ' or ''

            self.b('          %s, %s,' % (gtype, name))

        self.b('          G_TYPE_INVALID);')
        self.b('      return NULL;')
        self.b('    }')
        self.b('  else')
        self.b('    {')
        self.b('      TpProxyPendingCall *data;')
        self.b('')
        self.b('      data = tp_proxy_pending_call_v0_new ((TpProxy *) proxy,')
        self.b('          interface, "%s", iface,' % member)
        self.b('          %s,' % invoke_callback)
        self.b('          G_CALLBACK (callback), user_data, destroy,')
        self.b('          weak_object, FALSE);')
        self.b('      tp_proxy_pending_call_v0_take_pending_call (data,')
        self.b('          dbus_g_proxy_begin_call_with_timeout (iface,')
        self.b('              "%s",' % member)
        self.b('              %s,' % collect_callback)
        self.b('              data,')
        self.b('              tp_proxy_pending_call_v0_completed,')
        self.b('              timeout_ms,')

        for arg in in_args:
            name, info, tp_type, elt = arg
            ctype, gtype, marshaller, pointer = info

            const = pointer and 'const ' or ''

            self.b('              %s, %s,' % (gtype, name))

        self.b('              G_TYPE_INVALID));')
        self.b('')
        self.b('      return data;')
        self.b('    }')
        self.b('}')
        self.b('')

        self.do_method_reentrant(method, iface_lc, member, member_lc,
                                 in_args, out_args, collect_callback)

        # leave a gap for the end of the method
        self.d('')
        self.b('')
        self.h('')

    def do_method_reentrant(self, method, iface_lc, member, member_lc, in_args,
            out_args, collect_callback):
        # Reentrant blocking calls
        # Example:
        # gboolean tp_cli_properties_interface_run_get_properties
        #   (gpointer proxy,
        #       gint timeout_ms,
        #       const GArray *in_properties,
        #       GPtrArray **out0,
        #       GError **error,
        #       GMainLoop **loop);

        run_method_name = '%s_%s_run_%s' % (self.prefix_lc, iface_lc, member_lc)
        if run_method_name not in self.reentrant_symbols:
            return

        self.b('typedef struct {')
        self.b('    GMainLoop *loop;')
        self.b('    GError **error;')

        for arg in out_args:
            name, info, tp_type, elt = arg
            ctype, gtype, marshaller, pointer = info

            self.b('    %s*%s;' % (ctype, name))

        self.b('    unsigned success:1;')
        self.b('    unsigned completed:1;')
        self.b('} _%s_%s_run_state_%s;'
               % (self.prefix_lc, iface_lc, member_lc))

        reentrant_invoke = '_%s_%s_finish_running_%s' % (self.prefix_lc,
                                                         iface_lc,
                                                         member_lc)

        self.b('static void')
        self.b('%s (TpProxy *self G_GNUC_UNUSED,' % reentrant_invoke)
        self.b('    GError *error,')
        self.b('    GValueArray *args,')
        self.b('    GCallback unused G_GNUC_UNUSED,')
        self.b('    gpointer user_data G_GNUC_UNUSED,')
        self.b('    GObject *unused2 G_GNUC_UNUSED)')
        self.b('{')
        self.b('  _%s_%s_run_state_%s *state = user_data;'
               % (self.prefix_lc, iface_lc, member_lc))
        self.b('')
        self.b('  state->success = (error == NULL);')
        self.b('  state->completed = TRUE;')
        self.b('  g_main_loop_quit (state->loop);')
        self.b('')
        self.b('  if (error != NULL)')
        self.b('    {')
        self.b('      if (state->error != NULL)')
        self.b('        *state->error = error;')
        self.b('      else')
        self.b('        g_error_free (error);')
        self.b('')
        self.b('      return;')
        self.b('    }')
        self.b('')

        for i, arg in enumerate(out_args):
            name, info, tp_type, elt = arg
            ctype, gtype, marshaller, pointer = info

            self.b('  if (state->%s != NULL)' % name)
            if marshaller == 'BOXED':
                self.b('    *state->%s = g_value_dup_boxed ('
                       'args->values + %d);' % (name, i))
            elif marshaller == 'STRING':
                self.b('    *state->%s = g_value_dup_string '
                       '(args->values + %d);' % (name, i))
            elif marshaller in ('UCHAR', 'BOOLEAN', 'INT', 'UINT',
                    'INT64', 'UINT64', 'DOUBLE'):
                self.b('    *state->%s = g_value_get_%s (args->values + %d);'
                       % (name, marshaller.lower(), i))
            else:
                assert False, "Don't know how to copy %s" % gtype

            self.b('')

        if len(out_args) > 0:
            self.b('  g_value_array_free (args);')
        else:
            self.b('  if (args != NULL)')
            self.b('    g_value_array_free (args);')

        self.b('}')
        self.b('')

        if self.deprecate_reentrant:
            self.h('#ifndef %s' % self.deprecate_reentrant)

        self.h('gboolean %s (%sproxy,'
               % (run_method_name, self.proxy_arg))
        self.h('    gint timeout_ms,')

        self.d('/**')
        self.d(' * %s:' % run_method_name)
        self.d(' * @proxy: %s' % self.proxy_doc)
        self.d(' * @timeout_ms: Timeout in milliseconds, or -1 for default')

        for arg in in_args:
            name, info, tp_type, elt = arg
            ctype, gtype, marshaller, pointer = info

            docs = xml_escape(get_docstring(elt) or '(Undocumented)')

            if ctype == 'guint ' and tp_type != '':
                docs +=  ' (#%s)' % ('Tp' + tp_type.replace('_', ''))

            self.d(' * @%s: Used to pass an \'in\' argument: %s'
                   % (name, docs))

        for arg in out_args:
            name, info, tp_type, elt = arg
            ctype, gtype, marshaller, pointer = info

            self.d(' * @%s: Used to return an \'out\' argument if %%TRUE is '
                   'returned: %s'
                   % (name, xml_escape(get_docstring(elt) or '(Undocumented)')))

        self.d(' * @error: If not %NULL, used to return errors if %FALSE ')
        self.d(' *  is returned')
        self.d(' * @loop: If not %NULL, set before re-entering ')
        self.d(' *  the main loop, to point to a #GMainLoop ')
        self.d(' *  which can be used to cancel this call with ')
        self.d(' *  g_main_loop_quit(), causing a return of ')
        self.d(' *  %FALSE with @error set to %TP_DBUS_ERROR_CANCELLED')
        self.d(' *')
        self.d(' * Call the method %s and run the main loop' % member)
        self.d(' * until it returns. Before calling this method, you must')
        self.d(' * add a reference to any borrowed objects you need to keep,')
        self.d(' * and generally ensure that everything is in a consistent')
        self.d(' * state.')
        self.d(' *')
        self.d(' * %s' % xml_escape(get_docstring(method) or '(Undocumented)'))
        self.d(' *')
        self.d(' * Returns: TRUE on success, FALSE and sets @error on error')

        deprecated = method.getElementsByTagName('tp:deprecated')
        if deprecated:
            d = deprecated[0]
            self.d(' *')
            self.d(' * Deprecated: %s' % xml_escape(get_deprecated(d)))

        self.d(' */')
        self.d('')

        self.b('gboolean\n%s (%sproxy,'
               % (run_method_name, self.proxy_arg))
        self.b('    gint timeout_ms,')

        for arg in in_args:
            name, info, tp_type, elt = arg
            ctype, gtype, marshaller, pointer = info

            const = pointer and 'const ' or ''

            self.h('    %s%s%s,' % (const, ctype, name))
            self.b('    %s%s%s,' % (const, ctype, name))

        for arg in out_args:
            name, info, tp_type, elt = arg
            ctype, gtype, marshaller, pointer = info

            self.h('    %s*%s,' % (ctype, name))
            self.b('    %s*%s,' % (ctype, name))

        self.h('    GError **error,')

        if self.deprecate_reentrant:
            self.h('    GMainLoop **loop) %s;' % self.deprecation_attribute)
            self.h('#endif /* not %s */' % self.deprecate_reentrant)
        else:
            self.h('    GMainLoop **loop);')

        self.h('')

        self.b('    GError **error,')
        self.b('    GMainLoop **loop)')
        self.b('{')
        self.b('  DBusGProxy *iface;')
        self.b('  GQuark interface = %s;' % self.get_iface_quark())
        self.b('  TpProxyPendingCall *pc;')
        self.b('  _%s_%s_run_state_%s state = {'
               % (self.prefix_lc, iface_lc, member_lc))
        self.b('      NULL /* loop */, error,')

        for arg in out_args:
            name, info, tp_type, elt = arg

            self.b('    %s,' % name)

        self.b('      FALSE /* completed */, FALSE /* success */ };')
        self.b('')
        self.b('  g_return_val_if_fail (%s (proxy), FALSE);'
               % self.proxy_assert)
        self.b('')
        self.b('  G_GNUC_BEGIN_IGNORE_DEPRECATIONS')
        self.b('  iface = tp_proxy_borrow_interface_by_id')
        self.b('       ((TpProxy *) proxy, interface, error);')
        self.b('  G_GNUC_END_IGNORE_DEPRECATIONS')
        self.b('')
        self.b('  if (iface == NULL)')
        self.b('    return FALSE;')
        self.b('')
        self.b('  state.loop = g_main_loop_new (NULL, FALSE);')
        self.b('')
        self.b('  pc = tp_proxy_pending_call_v0_new ((TpProxy *) proxy,')
        self.b('      interface, "%s", iface,' % member)
        self.b('      %s,' % reentrant_invoke)
        self.b('      NULL, &state, NULL, NULL, TRUE);')
        self.b('')
        self.b('  if (loop != NULL)')
        self.b('    *loop = state.loop;')
        self.b('')
        self.b('  tp_proxy_pending_call_v0_take_pending_call (pc,')
        self.b('      dbus_g_proxy_begin_call_with_timeout (iface,')
        self.b('          "%s",' % member)
        self.b('          %s,' % collect_callback)
        self.b('          pc,')
        self.b('          tp_proxy_pending_call_v0_completed,')
        self.b('          timeout_ms,')

        for arg in in_args:
            name, info, tp_type, elt = arg
            ctype, gtype, marshaller, pointer = info

            const = pointer and 'const ' or ''

            self.b('              %s, %s,' % (gtype, name))

        self.b('          G_TYPE_INVALID));')
        self.b('')
        self.b('  if (!state.completed)')
        self.b('    g_main_loop_run (state.loop);')
        self.b('')
        self.b('  if (!state.completed)')
        self.b('    tp_proxy_pending_call_cancel (pc);')
        self.b('')
        self.b('  if (loop != NULL)')
        self.b('    *loop = NULL;')
        self.b('')
        self.b('  g_main_loop_unref (state.loop);')
        self.b('')
        self.b('  return state.success;')
        self.b('}')
        self.b('')

    def do_signal_add(self, signal):
        marshaller_items = []
        gtypes = []

        for i in signal.getElementsByTagName('arg'):
            name = i.getAttribute('name')
            type = i.getAttribute('type')
            info = type_to_gtype(type)
            # type, GType, STRING, is a pointer
            gtypes.append(info[1])

        self.b('  dbus_g_proxy_add_signal (proxy, "%s",'
               % signal.getAttribute('name'))
        for gtype in gtypes:
            self.b('      %s,' % gtype)
        self.b('      G_TYPE_INVALID);')

    def do_interface(self, node):
        ifaces = node.getElementsByTagName('interface')
        assert len(ifaces) == 1
        iface = ifaces[0]
        name = node.getAttribute('name').replace('/', '')

        self.iface = name
        self.iface_lc = name.lower()
        self.iface_uc = name.upper()
        self.iface_mc = name.replace('_', '')
        self.iface_dbus = iface.getAttribute('name')

        signals = node.getElementsByTagName('signal')
        methods = node.getElementsByTagName('method')

        if signals:
            self.b('static inline void')
            self.b('%s_add_signals_for_%s (DBusGProxy *proxy)'
                    % (self.prefix_lc, name.lower()))
            self.b('{')

            if self.tp_proxy_api >= (0, 7, 6):
                self.b('  if (!tp_proxy_dbus_g_proxy_claim_for_signal_adding '
                       '(proxy))')
                self.b('    return;')

            for signal in signals:
                self.do_signal_add(signal)

            self.b('}')
            self.b('')
            self.b('')

        for signal in signals:
            self.do_signal(name, signal)

        for method in methods:
            self.do_method(name, method)

        self.iface_dbus = None

    def __call__(self):

        if self.guard is not None:
            self.h('#ifndef %s' % self.guard)
            self.h('#define %s' % self.guard)
            self.h('')

        self.h('G_BEGIN_DECLS')
        self.h('')

        self.b('/* We don\'t want gtkdoc scanning this file, it\'ll get')
        self.b(' * confused by seeing function definitions, so mark it as: */')
        self.b('/*<private_header>*/')
        self.b('')

        nodes = self.dom.getElementsByTagName('node')
        nodes.sort(cmp_by_name)

        for node in nodes:
            self.do_interface(node)

        if self.group is not None:

            self.b('/*')
            self.b(' * %s_%s_add_signals:' % (self.prefix_lc, self.group))
            self.b(' * @self: the #TpProxy')
            self.b(' * @quark: a quark whose string value is the interface')
            self.b(' *   name whose signals should be added')
            self.b(' * @proxy: the D-Bus proxy to which to add the signals')
            self.b(' * @unused: not used for anything')
            self.b(' *')
            self.b(' * Tell dbus-glib that @proxy has the signatures of all')
            self.b(' * signals on the given interface, if it\'s one we')
            self.b(' * support.')
            self.b(' *')
            self.b(' * This function should be used as a signal handler for')
            self.b(' * #TpProxy::interface-added.')
            self.b(' */')
            self.b('static void')
            self.b('%s_%s_add_signals (TpProxy *self G_GNUC_UNUSED,'
                    % (self.prefix_lc, self.group))
            self.b('    guint quark,')
            self.b('    DBusGProxy *proxy,')
            self.b('    gpointer unused G_GNUC_UNUSED)')

            self.b('{')

            for node in nodes:
                iface = node.getElementsByTagName('interface')[0]
                self.iface_dbus = iface.getAttribute('name')
                signals = node.getElementsByTagName('signal')
                if not signals:
                    continue
                name = node.getAttribute('name').replace('/', '').lower()
                self.iface_uc = name.upper()
                self.b('  if (quark == %s)' % self.get_iface_quark())
                self.b('    %s_add_signals_for_%s (proxy);'
                       % (self.prefix_lc, name))

            self.b('}')
            self.b('')

        self.h('G_END_DECLS')
        self.h('')

        if self.guard is not None:
            self.h('#endif /* defined (%s) */' % self.guard)
            self.h('')

        file_set_contents(self.basename + '.h', '\n'.join(self.__header))
        file_set_contents(self.basename + '-body.h', '\n'.join(self.__body))
        file_set_contents(self.basename + '-gtk-doc.h', '\n'.join(self.__docs))

def types_to_gtypes(types):
    return [type_to_gtype(t)[1] for t in types]


if __name__ == '__main__':
    options, argv = gnu_getopt(sys.argv[1:], '',
                               ['group=', 'subclass=', 'subclass-assert=',
                                'iface-quark-prefix=', 'tp-proxy-api=',
                                'generate-reentrant=', 'deprecate-reentrant=',
                                'deprecation-attribute=', 'guard='])

    opts = {}

    for option, value in options:
        opts[option] = value

    dom = xml.dom.minidom.parse(argv[0])

    Generator(dom, argv[1], argv[2], opts)()
