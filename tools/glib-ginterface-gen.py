#!/usr/bin/python

# glib-ginterface-gen.py: service-side interface generator
#
# Generate dbus-glib 0.x service GInterfaces from the Telepathy specification.
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
import os.path
import xml.dom.minidom

from libtpcodegen import file_set_contents, key_by_name, u, get_emits_changed
from libglibcodegen import (Signature, type_to_gtype,
        NS_TP, dbus_gutils_wincaps_to_uscore, value_getter,
        GDBusInterfaceInfo)


NS_TP = "http://telepathy.freedesktop.org/wiki/DbusSpec#extensions-v0"

class Generator(object):

    def __init__(self, dom, prefix, basename, signal_marshal_prefix,
                 headers, end_headers, not_implemented_func,
                 allow_havoc, allow_single_include):
        self.dom = dom
        self.__header = []
        self.__body = []
        self.__docs = []

        assert prefix.endswith('_')
        assert not signal_marshal_prefix.endswith('_')

        # The main_prefix, sub_prefix thing is to get:
        # FOO_ -> (FOO_, _)
        # FOO_SVC_ -> (FOO_, _SVC_)
        # but
        # FOO_BAR/ -> (FOO_BAR_, _)
        # FOO_BAR/SVC_ -> (FOO_BAR_, _SVC_)

        if '/' in prefix:
            main_prefix, sub_prefix = prefix.upper().split('/', 1)
            prefix = prefix.replace('/', '_')
        else:
            main_prefix, sub_prefix = prefix.upper().split('_', 1)

        self.MAIN_PREFIX_ = main_prefix + '_'
        self._SUB_PREFIX_ = '_' + sub_prefix

        self.Prefix_ = prefix
        self.Prefix = prefix.replace('_', '')
        self.prefix_ = prefix.lower()
        self.PREFIX_ = prefix.upper()

        self.basename = basename
        self.signal_marshal_prefix = signal_marshal_prefix
        self.headers = headers
        self.end_headers = end_headers
        self.not_implemented_func = not_implemented_func
        self.allow_havoc = allow_havoc
        self.allow_single_include = allow_single_include

    def h(self, s):
        self.__header.append(s)

    def b(self, s):
        self.__body.append(s)

    def d(self, s):
        self.__docs.append(s)

    def do_node(self, node):
        node_name = node.getAttribute('name').replace('/', '')
        node_name_mixed = self.node_name_mixed = node_name.replace('_', '')
        node_name_lc = self.node_name_lc = node_name.lower()
        node_name_uc = self.node_name_uc = node_name.upper()

        interfaces = node.getElementsByTagName('interface')
        assert len(interfaces) == 1, interfaces
        interface = interfaces[0]
        self.iface_name = interface.getAttribute('name')

        tmp = interface.getAttribute('tp:implement-service')
        if tmp == "no":
            return

        tmp = interface.getAttribute('tp:causes-havoc')
        if tmp and not self.allow_havoc:
            raise AssertionError('%s is %s' % (self.iface_name, tmp))

        iface_emits_changed = get_emits_changed(interface)

        methods = interface.getElementsByTagName('method')
        signals = interface.getElementsByTagName('signal')
        properties = interface.getElementsByTagName('property')

        self.b('struct _%s%sClass {' % (self.Prefix, node_name_mixed))
        self.b('    GTypeInterface parent_class;')
        for method in methods:
            self.b('    %s %s;' % self.get_method_impl_names(method))
        self.b('};')
        self.b('')

        if signals:
            self.b('enum {')
            for signal in signals:
                self.b('    %s,' % self.get_signal_const_entry(signal))
            self.b('    N_%s_SIGNALS' % node_name_uc)
            self.b('};')
            self.b('static guint %s_signals[N_%s_SIGNALS] = {0};'
                   % (node_name_lc, node_name_uc))
            self.b('')

        self.b('static void %s%s_base_init (gpointer klass);'
               % (self.prefix_, node_name_lc))
        self.b('')

        self.b('GType')
        self.b('%s%s_get_type (void)'
               % (self.prefix_, node_name_lc))
        self.b('{')
        self.b('  static GType type = 0;')
        self.b('')
        self.b('  if (G_UNLIKELY (type == 0))')
        self.b('    {')
        self.b('      static const GTypeInfo info = {')
        self.b('        sizeof (%s%sClass),' % (self.Prefix, node_name_mixed))
        self.b('        %s%s_base_init, /* base_init */'
               % (self.prefix_, node_name_lc))
        self.b('        NULL, /* base_finalize */')
        self.b('        NULL, /* class_init */')
        self.b('        NULL, /* class_finalize */')
        self.b('        NULL, /* class_data */')
        self.b('        0,')
        self.b('        0, /* n_preallocs */')
        self.b('        NULL /* instance_init */')
        self.b('      };')
        self.b('')
        self.b('      type = g_type_register_static (G_TYPE_INTERFACE,')
        self.b('          "%s%s", &info, 0);' % (self.Prefix, node_name_mixed))
        self.b('    }')
        self.b('')
        self.b('  return type;')
        self.b('}')
        self.b('')

        self.d('/**')
        self.d(' * %s%s:' % (self.Prefix, node_name_mixed))
        self.d(' *')
        self.d(' * Dummy typedef representing any implementation of this '
               'interface.')
        self.d(' */')

        self.h('typedef struct _%s%s %s%s;'
               % (self.Prefix, node_name_mixed, self.Prefix, node_name_mixed))
        self.h('')

        self.d('/**')
        self.d(' * %s%sClass:' % (self.Prefix, node_name_mixed))
        self.d(' *')
        self.d(' * The class of %s%s.' % (self.Prefix, node_name_mixed))

        if methods:
            self.d(' *')
            self.d(' * In a full implementation of this interface (i.e. all')
            self.d(' * methods implemented), the interface initialization')
            self.d(' * function used in G_IMPLEMENT_INTERFACE() would')
            self.d(' * typically look like this:')
            self.d(' *')
            self.d(' * <programlisting>')
            self.d(' * static void')
            self.d(' * implement_%s (gpointer klass,' % self.node_name_lc)
            self.d(' *     gpointer unused G_GNUC_UNUSED)')
            self.d(' * {')
            self.d(' * #define IMPLEMENT(x) %s%s_implement_&num;&num;x (\\'
                   % (self.prefix_, self.node_name_lc))
            self.d(' *   klass, my_object_&num;&num;x)')

            for method in methods:
                class_member_name = method.getAttribute('tp:name-for-bindings')
                class_member_name = class_member_name.lower()
                self.d(' *   IMPLEMENT (%s);' % class_member_name)

            self.d(' * #undef IMPLEMENT')
            self.d(' * }')
            self.d(' * </programlisting>')
        else:
            self.d(' * This interface has no D-Bus methods, so an')
            self.d(' * implementation can typically pass %NULL to')
            self.d(' * G_IMPLEMENT_INTERFACE() as the interface')
            self.d(' * initialization function.')

        self.d(' */')
        self.d('')

        self.h('typedef struct _%s%sClass %s%sClass;'
               % (self.Prefix, node_name_mixed, self.Prefix, node_name_mixed))
        self.h('')
        self.h('GType %s%s_get_type (void);'
               % (self.prefix_, node_name_lc))

        gtype = self.current_gtype = \
                self.MAIN_PREFIX_ + 'TYPE' + self._SUB_PREFIX_ + node_name_uc
        classname = self.Prefix + node_name_mixed

        self.h('#define %s \\\n  (%s%s_get_type ())'
               % (gtype, self.prefix_, node_name_lc))
        self.h('#define %s%s(obj) \\\n'
               '  (G_TYPE_CHECK_INSTANCE_CAST((obj), %s, %s))'
               % (self.PREFIX_, node_name_uc, gtype, classname))
        self.h('#define %sIS%s%s(obj) \\\n'
               '  (G_TYPE_CHECK_INSTANCE_TYPE((obj), %s))'
               % (self.MAIN_PREFIX_, self._SUB_PREFIX_, node_name_uc, gtype))
        self.h('#define %s%s_GET_CLASS(obj) \\\n'
               '  (G_TYPE_INSTANCE_GET_INTERFACE((obj), %s, %sClass))'
               % (self.PREFIX_, node_name_uc, gtype, classname))
        self.h('')

        base_init_code = []
        method_call_code = []

        for method in methods:
            self.do_method(method, method_call_code)

        signal_table = [
            'static const gchar * const _gsignals_%s[] = {' %
                self.node_name_lc
        ]

        for signal in signals:
            # we rely on this being in the same order as the interface info
            self.do_signal(signal, in_base_init=base_init_code,
                    in_signal_table=signal_table)

        signal_table.append('  NULL')
        signal_table.append('};')
        signal_table.append('')
        for line in signal_table:
            self.b(line)

        # e.g. _interface_info_connection_interface_contact_info1
        for line in GDBusInterfaceInfo(node_name, interface,
                '_interface_info_%s' % node_name_lc).to_lines(linkage='static'):
            self.b(line)

        self.b('')
        self.b('static void')
        self.b('_method_call_%s (GDBusConnection *connection,' % node_name_lc)
        self.b('    const gchar *sender,')
        self.b('    const gchar *object_path,')
        self.b('    const gchar *interface_name,')
        self.b('    const gchar *method_name,')
        self.b('    GVariant *parameters,')
        self.b('    GDBusMethodInvocation *invocation,')
        self.b('    gpointer user_data)')
        self.b('{')

        for line in method_call_code:
            self.b(line)

        # Deliberately not using self.not_implemented_func here so that callers
        # can distinguish between "you called Protocol.NormalizeContact() but
        # that isn't implemented here" and "you called Protocol.Badger()
        # which isn't even in the spec" if required.
        self.b('  g_dbus_method_invocation_return_error (invocation,')
        self.b('       G_DBUS_ERROR,')
        self.b('       G_DBUS_ERROR_UNKNOWN_METHOD,')
        self.b('       "Method not implemented");')
        self.b('}')
        self.b('')
        self.b('static const GDBusInterfaceVTable _vtable_%s = {' %
                node_name_lc)
        self.b('  _method_call_%s,' % node_name_lc)
        self.b('  NULL, /* get property */')
        self.b('  NULL /* set property */')
        self.b('};')
        self.b('')
        self.b('static const TpSvcInterfaceInfo _tp_interface_info_%s = {' %
                node_name_lc)
        self.b('  -1,')
        self.b('  (GDBusInterfaceInfo *) &_interface_info_%s,' % node_name_lc)
        self.b('  (GDBusInterfaceVTable *) &_vtable_%s,' % node_name_lc)
        self.b('  (gchar **) _gsignals_%s' % node_name_lc)
        self.b('  /* _future is implicitly zero-filled */')
        self.b('};')
        self.b('')
        self.b('static inline void')
        self.b('%s%s_base_init_once (gpointer klass G_GNUC_UNUSED)'
               % (self.prefix_, node_name_lc))
        self.b('{')

        if properties:
            self.b('  static TpDBusPropertiesMixinPropInfo properties[%d] = {'
                   % (len(properties) + 1))

            for m in properties:
                access = m.getAttribute('access')
                assert access in ('read', 'write', 'readwrite')

                if access == 'read':
                    flags = 'TP_DBUS_PROPERTIES_MIXIN_FLAG_READ'
                elif access == 'write':
                    flags = 'TP_DBUS_PROPERTIES_MIXIN_FLAG_WRITE'
                else:
                    flags = ('TP_DBUS_PROPERTIES_MIXIN_FLAG_READ | '
                             'TP_DBUS_PROPERTIES_MIXIN_FLAG_WRITE')

                prop_emits_changed = get_emits_changed(m)

                if prop_emits_changed is None:
                    prop_emits_changed = iface_emits_changed

                if prop_emits_changed == 'true':
                    flags += ' | TP_DBUS_PROPERTIES_MIXIN_FLAG_EMITS_CHANGED'
                elif prop_emits_changed == 'invalidates':
                    flags += ' | TP_DBUS_PROPERTIES_MIXIN_FLAG_EMITS_INVALIDATED'

                self.b('      { 0, %s, "%s", 0, NULL, NULL }, /* %s */'
                       % (flags, m.getAttribute('type'), m.getAttribute('name')))

            self.b('      { 0, 0, NULL, 0, NULL, NULL }')
            self.b('  };')
            self.b('  static TpDBusPropertiesMixinIfaceInfo interface =')
            self.b('      { 0, properties, NULL, NULL };')
            self.b('')

        if properties:
            self.b('  interface.dbus_interface = g_quark_from_static_string '
                   '("%s");' % self.iface_name)

            for i, m in enumerate(properties):
                self.b('  properties[%d].name = g_quark_from_static_string ("%s");'
                       % (i, m.getAttribute('name')))
                self.b('  properties[%d].type = %s;'
                           % (i, type_to_gtype(m.getAttribute('type'))[1]))

            self.b('  tp_svc_interface_set_dbus_properties_info (%s, &interface);'
                   % self.current_gtype)

            self.b('')

        self.b('  tp_svc_interface_set_dbus_interface_info (%s,'
               % (self.current_gtype))
        self.b('      &_tp_interface_info_%s);' % node_name_lc)

        for s in base_init_code:
            self.b(s)
        self.b('}')

        self.b('static void')
        self.b('%s%s_base_init (gpointer klass)'
               % (self.prefix_, node_name_lc))
        self.b('{')
        self.b('  static gboolean initialized = FALSE;')
        self.b('')
        self.b('  if (!initialized)')
        self.b('    {')
        self.b('      initialized = TRUE;')
        self.b('      %s%s_base_init_once (klass);'
               % (self.prefix_, node_name_lc))
        self.b('    }')
        # insert anything we need to do per implementation here
        self.b('}')

        self.h('')

        self.node_name_mixed = None
        self.node_name_lc = None
        self.node_name_uc = None

    def get_method_impl_names(self, method):
        dbus_method_name = method.getAttribute('name')

        class_member_name = method.getAttribute('tp:name-for-bindings')
        if dbus_method_name != class_member_name.replace('_', ''):
            raise AssertionError('Method %s tp:name-for-bindings (%s) does '
                    'not match' % (dbus_method_name, class_member_name))
        class_member_name = class_member_name.lower()

        stub_name = (self.prefix_ + self.node_name_lc + '_' +
                     class_member_name)
        return (stub_name + '_impl', class_member_name + '_cb')

    def do_method(self, method, method_call_code):
        assert self.node_name_mixed is not None

        # Examples refer to Thing.DoStuff (su) -> ii

        # DoStuff
        dbus_method_name = method.getAttribute('name')
        # do_stuff
        class_member_name = method.getAttribute('tp:name-for-bindings')
        if dbus_method_name != class_member_name.replace('_', ''):
            raise AssertionError('Method %s tp:name-for-bindings (%s) does '
                    'not match' % (dbus_method_name, class_member_name))
        class_member_name = class_member_name.lower()

        # tp_svc_thing_do_stuff (signature of GDBusInterfaceMethodCallFunc)
        stub_name = (self.prefix_ + self.node_name_lc + '_' +
                     class_member_name)
        # typedef void (*tp_svc_thing_do_stuff_impl) (TpSvcThing *,
        #   const char *, guint, GDBusMethodInvocation);
        impl_name = stub_name + '_impl'
        # void tp_svc_thing_return_from_do_stuff (GDBusMethodInvocation *,
        #   gint, gint);
        ret_name = (self.prefix_ + self.node_name_lc + '_return_from_' +
                    class_member_name)

        # Gather arguments
        in_args = []
        in_arg_value_getters = []
        out_args = []
        for i in method.getElementsByTagName('arg'):
            name = i.getAttribute('name')
            direction = i.getAttribute('direction') or 'in'
            dtype = i.getAttribute('type')

            assert direction in ('in', 'out')

            if name:
                name = direction + '_' + name
            elif direction == 'in':
                name = direction + str(len(in_args))
            else:
                name = direction + str(len(out_args))

            ctype, gtype, marshaller, pointer = type_to_gtype(dtype)

            if pointer:
                ctype = 'const ' + ctype

            struct = (ctype, name)

            if direction == 'in':
                in_args.append((ctype, name))
                in_arg_value_getters.append(value_getter(gtype, marshaller))
            else:
                out_args.append((gtype, ctype, name))

        # bits of _method_call_myiface
        method_call_code.extend([
            '  if (g_strcmp0 (method_name, "%s") == 0)' % dbus_method_name,
            '    {',
            '      %s (connection, sender, object_path, interface_name, ' %
                stub_name,
            '          method_name, parameters, invocation, user_data);',
            '      return;',
            '    }',
            ''
        ])

        # Implementation type declaration (in header, docs separated)
        self.d('/**')
        self.d(' * %s:' % impl_name)
        self.d(' * @self: The object implementing this interface')
        for (ctype, name) in in_args:
            self.d(' * @%s: %s (FIXME, generate documentation)'
                   % (name, ctype))
        self.d(' * @invocation: Used to return values or throw an error')
        self.d(' *')
        self.d(' * The signature of an implementation of the D-Bus method')
        self.d(' * %s on interface %s.' % (dbus_method_name, self.iface_name))
        self.d(' */')

        self.h('typedef void (*%s) (%s%s *self,'
          % (impl_name, self.Prefix, self.node_name_mixed))
        for (ctype, name) in in_args:
            self.h('    %s%s,' % (ctype, name))
        self.h('    GDBusMethodInvocation *invocation);')

        # Stub definition (in body only - it's static)
        self.b('static void')
        self.b('%s (GDBusConnection *connection,' % stub_name)
        self.b('    const gchar *sender,')
        self.b('    const gchar *object_path,')
        self.b('    const gchar *interface_name,')
        self.b('    const gchar *method_name,')
        self.b('    GVariant *parameters,')
        self.b('    GDBusMethodInvocation *invocation,')
        self.b('    gpointer user_data)')
        self.b('{')
        self.b('  %s%s *self = %s%s (user_data);'
                % (self.Prefix, self.node_name_mixed, self.PREFIX_,
                    self.node_name_uc))
        self.b('  %s%sClass *cls = %s%s_GET_CLASS (self);'
          % (self.Prefix, self.node_name_mixed, self.PREFIX_,
              self.node_name_uc))
        self.b('  %s impl = cls->%s_cb;' % (impl_name, class_member_name))
        self.b('')
        self.b('  if (impl != NULL)')
        tmp = ['self'] + [name for (ctype, name) in in_args] + ['invocation']
        self.b('    {')

        if in_args:
            self.b('      GValue args_val = G_VALUE_INIT;')
            self.b('      GValueArray *va;')
            self.b('')
            self.b('      dbus_g_value_parse_g_variant (parameters, &args_val);')
            self.b('      va = g_value_get_boxed (&args_val);')
            self.b('')

        self.b('      (impl) (self,')

        for i, getter in enumerate(in_arg_value_getters):
            self.b('          %s (va->values + %d),' % (getter, i))

        self.b('          invocation);')

        if in_args:
            self.b('      g_value_unset (&args_val);')

        self.b('    }')
        self.b('  else')
        self.b('    {')
        if self.not_implemented_func:
            self.b('      %s (invocation);' % self.not_implemented_func)
        else:
            self.b('      g_dbus_method_invocation_return_error (invocation,')
            self.b('           G_DBUS_ERROR,')
            self.b('           G_DBUS_ERROR_UNKNOWN_METHOD,')
            self.b('           "Method not implemented");')
        self.b('    }')
        self.b('}')
        self.b('')

        # Implementation registration (in both header and body)
        self.h('void %s%s_implement_%s (%s%sClass *klass, %s impl);'
               % (self.prefix_, self.node_name_lc, class_member_name,
                  self.Prefix, self.node_name_mixed, impl_name))

        self.d('/**')
        self.d(' * %s%s_implement_%s:'
               % (self.prefix_, self.node_name_lc, class_member_name))
        self.d(' * @klass: A class whose instances implement this interface')
        self.d(' * @impl: A callback used to implement the %s D-Bus method'
               % dbus_method_name)
        self.d(' *')
        self.d(' * Register an implementation for the %s method in the vtable'
               % dbus_method_name)
        self.d(' * of an implementation of this interface. To be called from')
        self.d(' * the interface init function.')
        self.d(' */')

        self.b('void')
        self.b('%s%s_implement_%s (%s%sClass *klass, %s impl)'
               % (self.prefix_, self.node_name_lc, class_member_name,
                  self.Prefix, self.node_name_mixed, impl_name))
        self.b('{')
        self.b('  klass->%s_cb = impl;' % class_member_name)
        self.b('}')
        self.b('')

        # Return convenience function
        self.d('/**')
        self.d(' * %s:' % ret_name)
        self.d(' * @invocation: The D-Bus method invocation context')
        for (gtype, ctype, name) in out_args:
            self.d(' * @%s: %s (FIXME, generate documentation)'
                   % (name, ctype))
        self.d(' *')
        self.d(' * Return successfully by calling g_dbus_method_invocation_return_value().')
        self.d(' */')
        self.d('')

        tmp = (['GDBusMethodInvocation *invocation'] +
               [ctype + name for (gtype, ctype, name) in out_args])
        self.h(('void %s (' % ret_name) + (',\n    '.join(tmp)) + ');')

        self.b('void')
        self.b(('%s (' % ret_name) + (',\n    '.join(tmp)) + ')')
        self.b('{')
        self.b('  GValueArray *tmp = tp_value_array_build (%d,' % len(out_args))

        for (gtype, ctype, name) in out_args:
            self.b('      %s, %s,' % (gtype, name))

        self.b('      G_TYPE_INVALID);')
        self.b('  GValue args_val = G_VALUE_INIT;')
        self.b('')

        self.b('  g_value_init (&args_val, '
                'dbus_g_type_get_struct ("GValueArray",')

        for (gtype, ctype, name) in out_args:
            self.b('      %s,' % gtype)

        self.b('      G_TYPE_INVALID));')

        self.b('  g_value_take_boxed (&args_val, tmp);')

        self.b('  g_dbus_method_invocation_return_value (invocation,')
        self.b('      /* consume floating ref */')
        self.b('      dbus_g_value_build_g_variant (&args_val));')
        self.b('  g_value_unset (&args_val);')
        self.b('}')
        self.b('')

    def get_signal_const_entry(self, signal):
        assert self.node_name_uc is not None
        return ('SIGNAL_%s_%s'
                % (self.node_name_uc, signal.getAttribute('name')))

    def do_signal(self, signal, in_base_init, in_signal_table):
        assert self.node_name_mixed is not None

        # for signal: Thing::StuffHappened (s, u)
        # we want to emit:
        # void tp_svc_thing_emit_stuff_happened (gpointer instance,
        #    const char *arg0, guint arg1);

        dbus_name = signal.getAttribute('name')

        ugly_name = signal.getAttribute('tp:name-for-bindings')
        if dbus_name != ugly_name.replace('_', ''):
            raise AssertionError('Signal %s tp:name-for-bindings (%s) does '
                    'not match' % (dbus_name, ugly_name))

        stub_name = (self.prefix_ + self.node_name_lc + '_emit_' +
                     ugly_name.lower())

        const_name = self.get_signal_const_entry(signal)

        # Gather arguments
        args = []
        for i in signal.getElementsByTagName('arg'):
            name = i.getAttribute('name')
            dtype = i.getAttribute('type')
            tp_type = i.getAttribute('tp:type')

            if name:
                name = 'arg_' + name
            else:
                name = 'arg' + str(len(args))

            ctype, gtype, marshaller, pointer = type_to_gtype(dtype)

            if pointer:
                ctype = 'const ' + ctype

            struct = (ctype, name, gtype)
            args.append(struct)

        tmp = (['gpointer instance'] +
               [ctype + name for (ctype, name, gtype) in args])

        self.h(('void %s (' % stub_name) + (',\n    '.join(tmp)) + ');')

        # FIXME: emit docs

        self.d('/**')
        self.d(' * %s:' % stub_name)
        self.d(' * @instance: The object implementing this interface')
        for (ctype, name, gtype) in args:
            self.d(' * @%s: %s (FIXME, generate documentation)'
                   % (name, ctype))
        self.d(' *')
        self.d(' * Type-safe wrapper around g_signal_emit to emit the')
        self.d(' * %s signal on interface %s.'
               % (dbus_name, self.iface_name))
        self.d(' */')

        self.b('void')
        self.b(('%s (' % stub_name) + (',\n    '.join(tmp)) + ')')
        self.b('{')
        self.b('  g_assert (instance != NULL);')
        self.b('  g_assert (G_TYPE_CHECK_INSTANCE_TYPE (instance, %s));'
               % (self.current_gtype))
        tmp = (['instance', '%s_signals[%s]' % (self.node_name_lc, const_name),
                '0'] + [name for (ctype, name, gtype) in args])
        self.b('  g_signal_emit (' + ',\n      '.join(tmp) + ');')
        self.b('}')
        self.b('')

        signal_name = dbus_gutils_wincaps_to_uscore(dbus_name).replace('_',
                '-')

        self.d('/**')
        self.d(' * %s%s::%s:'
                % (self.Prefix, self.node_name_mixed, signal_name))
        self.d(' * @self: an object')
        for (ctype, name, gtype) in args:
            self.d(' * @%s: %s (FIXME, generate documentation)'
                   % (name, ctype))
        self.d(' *')
        self.d(' * The %s D-Bus signal is emitted whenever '
                'this GObject signal is.' % dbus_name)
        self.d(' */')
        self.d('')

        in_base_init.append('  %s_signals[%s] ='
                            % (self.node_name_lc, const_name))
        in_base_init.append('  g_signal_new ("%s",' % signal_name)
        in_base_init.append('      G_OBJECT_CLASS_TYPE (klass),')
        in_base_init.append('      G_SIGNAL_RUN_LAST|G_SIGNAL_DETAILED,')
        in_base_init.append('      0,')
        in_base_init.append('      NULL, NULL,')
        in_base_init.append('      g_cclosure_marshal_generic,')
        in_base_init.append('      G_TYPE_NONE,')
        tmp = ['%d' % len(args)] + [gtype for (ctype, name, gtype) in args]
        in_base_init.append('      %s);' % ',\n      '.join(tmp))
        in_base_init.append('')

        in_signal_table.append('    "%s",' % signal_name)

    def have_properties(self, nodes):
        for node in nodes:
            interface =  node.getElementsByTagName('interface')[0]
            if interface.getElementsByTagName('property'):
                return True
        return False

    def __call__(self):
        nodes = self.dom.getElementsByTagName('node')
        nodes.sort(key=key_by_name)

        self.h('#include <glib-object.h>')
        self.h('#include <gio/gio.h>')
        self.h('#include <dbus/dbus-glib.h>')

        self.h('')
        self.h('G_BEGIN_DECLS')
        self.h('')

        self.b('#include "%s.h"' % self.basename)
        self.b('')

        if self.allow_single_include:
            self.b('#include <telepathy-glib/core-svc-interface.h>')
            self.b('#include <telepathy-glib/dbus.h>')
            self.b('#include <telepathy-glib/dbus-properties-mixin.h>')
            self.b('#include <telepathy-glib/util.h>')
        else:
            self.b('#include <telepathy-glib/telepathy-glib.h>')
        self.b('')

        for header in self.headers:
            self.b('#include %s' % header)
        self.b('')

        for node in nodes:
            self.do_node(node)

        self.h('')
        self.h('G_END_DECLS')

        self.b('')
        for header in self.end_headers:
            self.b('#include %s' % header)

        self.h('')
        self.b('')
        file_set_contents(self.basename + '.h', u('\n').join(self.__header).encode('utf-8'))
        file_set_contents(self.basename + '.c', u('\n').join(self.__body).encode('utf-8'))
        file_set_contents(self.basename + '-gtk-doc.h', u('\n').join(self.__docs).encode('utf-8'))

def cmdline_error():
    print("""\
usage:
    gen-ginterface [OPTIONS] xmlfile Prefix_
options:
    --include='<header.h>' (may be repeated)
    --include='"header.h"' (ditto)
    --include-end='"header.h"' (ditto)
        Include extra headers in the generated .c file
    --signal-marshal-prefix='prefix'
        Use the given prefix on generated signal marshallers (default is
        prefix.lower()).
    --filename='BASENAME'
        Set the basename for the output files (default is prefix.lower()
        + 'ginterfaces')
    --not-implemented-func='symbol'
        Set action when methods not implemented in the interface vtable are
        called. symbol must have signature
            void symbol (GDBusMethodInvocation *invocation)
        and return some sort of "not implemented" error via
            e.g. g_dbus_method_invocation_return_error
""")
    sys.exit(1)


if __name__ == '__main__':
    from getopt import gnu_getopt

    options, argv = gnu_getopt(sys.argv[1:], '',
                               ['filename=', 'signal-marshal-prefix=',
                                'include=', 'include-end=',
                                'allow-unstable',
                                'not-implemented-func=',
                                "allow-single-include"])

    try:
        prefix = argv[1]
    except IndexError:
        cmdline_error()

    basename = prefix.lower() + 'ginterfaces'
    signal_marshal_prefix = prefix.lower().rstrip('_')
    headers = []
    end_headers = []
    not_implemented_func = ''
    allow_havoc = False
    allow_single_include = False

    for option, value in options:
        if option == '--filename':
            basename = value
        elif option == '--signal-marshal-prefix':
            signal_marshal_prefix = value
        elif option == '--include':
            if value[0] not in '<"':
                value = '"%s"' % value
            headers.append(value)
        elif option == '--include-end':
            if value[0] not in '<"':
                value = '"%s"' % value
            end_headers.append(value)
        elif option == '--not-implemented-func':
            not_implemented_func = value
        elif option == '--allow-unstable':
            allow_havoc = True
        elif option == '--allow-single-include':
            allow_single_include = True

    try:
        dom = xml.dom.minidom.parse(argv[0])
    except IndexError:
        cmdline_error()

    Generator(dom, prefix, basename, signal_marshal_prefix, headers,
              end_headers, not_implemented_func, allow_havoc,
              allow_single_include)()
