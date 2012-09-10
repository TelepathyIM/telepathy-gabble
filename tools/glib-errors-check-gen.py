#!/usr/bin/python

import sys
import xml.dom.minidom

from libglibcodegen import NS_TP, get_docstring, get_descendant_text

class Generator(object):
    def __init__(self, dom):
        self.dom = dom
        self.errors = self.dom.getElementsByTagNameNS(NS_TP, 'errors')[0]

    def __call__(self):

        print '{'
        print '  GEnumClass *klass;'
        print '  GEnumValue *value_by_name;'
        print '  GEnumValue *value_by_nick;'
        print ''
        print '  g_type_init ();'
        print '  klass = g_type_class_ref (TP_TYPE_ERROR);'

        for error in self.errors.getElementsByTagNameNS(NS_TP, 'error'):
            ns = error.parentNode.getAttribute('namespace')
            nick = error.getAttribute('name').replace(' ', '')
            enum = ('TP_ERROR_' +
                    error.getAttribute('name').replace(' ', '_').replace('.', '_').upper())
            s = ('TP_ERROR_STR_' +
                 error.getAttribute('name').replace(' ', '_').replace('.', '_').upper())

            print ''
            print '  /* %s.%s */' % (ns, nick)
            print ('  value_by_name = g_enum_get_value_by_name (klass, "%s");'
                    % enum)
            print ('  value_by_nick = g_enum_get_value_by_nick (klass, "%s");'
                    % nick)
            print ('  g_assert (value_by_name != NULL);')
            print ('  g_assert (value_by_nick != NULL);')
            print ('  g_assert_cmpint (value_by_name->value, ==, %s);'
                    % enum)
            print ('  g_assert_cmpint (value_by_nick->value, ==, %s);'
                    % enum)
            print ('  g_assert_cmpstr (value_by_name->value_name, ==, "%s");'
                    % enum)
            print ('  g_assert_cmpstr (value_by_nick->value_name, ==, "%s");'
                    % enum)
            print ('  g_assert_cmpstr (value_by_name->value_nick, ==, "%s");'
                    % nick)
            print ('  g_assert_cmpstr (value_by_nick->value_nick, ==, "%s");'
                    % nick)
            print ('  g_assert_cmpstr (%s, ==, TP_ERROR_PREFIX ".%s");'
                    % (s, nick))

        print '}'

if __name__ == '__main__':
    argv = sys.argv[1:]
    Generator(xml.dom.minidom.parse(argv[0]))()
