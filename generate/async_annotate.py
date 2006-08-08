#!/usr/bin/python2.4

import sys

try:
    from elementtree.ElementTree import fromstring, tostring, ElementTree, Element
except ImportError:
    print "You need to install ElementTree (http://effbot.org/zone/element-index.htm)"
    sys.exit(1)

from xml.dom.minidom import parseString
from telepathy.server import *

import sys

def strip (element):
    if element.text:
        element.text = element.text.strip()
    if element.tail:
        element.tail = element.tail.strip()
    for child in element:
        strip (child)

class AsyncAnnotation:
    def __init__(self, line):
        self.cls, self.interface, self.method = line.strip().split()

def annotate(root, annotations):
    assert root.tag == 'node'
    annotations = [a for a in annotations if root.get('name') == '/' + a.cls]

    for interface_elt in root:
        if interface_elt.tag != 'interface':
            continue
        for method_elt in interface_elt:
            if method_elt.tag != 'method':
                continue
            for a in annotations:
                if (interface_elt.get('name') == a.interface
                    and method_elt.get('name') == a.method):
                    a_elt = Element('annotation',
                                    name='org.freedesktop.DBus.GLib.Async',
                                    value='')
                    method_elt.insert(0, a_elt)

if __name__ == '__main__':
    annotations = [AsyncAnnotation(line) for line in file(sys.argv[1])]

    root = ElementTree(file=sys.argv[2]).getroot()
    annotate(root, annotations)

    # pretty print
    strip(root)
    xml = tostring(root)
    dom = parseString(xml)

    output = file(sys.argv[3], 'w')
    output.write(dom.toprettyxml('  ', '\n'))
    output.close()
