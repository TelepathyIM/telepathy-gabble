#!/usr/bin/python

import sys
import xml.dom.minidom
from string import ascii_letters, digits


from libglibcodegen import signal_to_marshal_name


NS_TP = "http://telepathy.freedesktop.org/wiki/DbusSpec#extensions-v0"

class Generator(object):

    def __init__(self, dom, prefix):
        self.dom = dom
        self.marshallers = {}
        self.prefix = prefix

    def do_signal(self, signal):
        marshaller = signal_to_marshal_name(signal, self.prefix)

        assert '__' in marshaller
        rhs = marshaller.split('__', 1)[1].split('_')

        self.marshallers[marshaller] = rhs

    def __call__(self):
        signals = self.dom.getElementsByTagName('signal')

        for signal in signals:
            self.do_signal(signal)

        print('void')
        print('%s_register_dbus_glib_marshallers (void)' % self.prefix)
        print('{')

        all = list(self.marshallers.keys())
        all.sort()
        for marshaller in all:
            rhs = self.marshallers[marshaller]

            print('  dbus_g_object_register_marshaller (')
            print('      g_cclosure_marshal_generic,')
            print('      G_TYPE_NONE,       /* return */')
            for type in rhs:
                print('      G_TYPE_%s,' % type.replace('VOID', 'NONE'))
            print('      G_TYPE_INVALID);')

        print('}')


def types_to_gtypes(types):
    return [type_to_gtype(t)[1] for t in types]

if __name__ == '__main__':
    argv = sys.argv[1:]
    dom = xml.dom.minidom.parse(argv[0])

    Generator(dom, argv[1])()
