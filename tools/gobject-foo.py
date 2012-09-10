#!/usr/bin/python

# gobject-foo.py: generate standard GObject type macros etc.
#
# The master copy of this program is in the telepathy-glib repository -
# please make any changes there.
#
# Copyright (C) 2007-2010 Collabora Ltd. <http://www.collabora.co.uk/>
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

def gobject_header(head, tail, as_interface=False):
    out = []
    o = out.append

    name = head + '_' + tail
    MixedCase = name.replace('_', '')
    lower_case = name.lower()
    UPPER_CASE = name.upper()

    gtype = head.upper() + '_TYPE_' + tail.upper()

    o("typedef struct _%s %s;" % (MixedCase, MixedCase))

    if as_interface:
        o("typedef struct _%sInterface %sInterface;" % (MixedCase, MixedCase))
    else:
        o("typedef struct _%sClass %sClass;" % (MixedCase, MixedCase))
        o("typedef struct _%sPrivate %sPrivate;" % (MixedCase, MixedCase))

    o("")
    o("GType %s_get_type (void);" % lower_case)
    o("")

    o("#define %s \\" % gtype)
    o("  (%s_get_type ())" % lower_case)

    o("#define %s(obj) \\" % UPPER_CASE)
    o("  (G_TYPE_CHECK_INSTANCE_CAST ((obj), %s, \\" % gtype)
    o("                               %s))" % MixedCase)

    if not as_interface:
        o("#define %s_CLASS(klass) \\" % UPPER_CASE)
        o("  (G_TYPE_CHECK_CLASS_CAST ((klass), %s, \\" % gtype)
        o("                            %sClass))" % MixedCase)

    o("#define %s_IS_%s(obj) \\" % (head.upper(), tail.upper()))
    o("  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), %s))" % gtype)

    if as_interface:
        o("#define %s_GET_IFACE(obj) \\" % UPPER_CASE)
        o("  (G_TYPE_INSTANCE_GET_INTERFACE ((obj), %s, \\" % gtype)
        o("                                  %sInterface))" % MixedCase)
    else:
        o("#define %s_IS_%s_CLASS(klass) \\" % (head.upper(), tail.upper()))
        o("  (G_TYPE_CHECK_CLASS_TYPE ((klass), %s))" % gtype)

        o("#define %s_GET_CLASS(obj) \\" % UPPER_CASE)
        o("  (G_TYPE_INSTANCE_GET_CLASS ((obj), %s, \\" % gtype)
        o("                              %sClass))" % MixedCase)

    return out

if __name__ == '__main__':
    import sys
    from getopt import gnu_getopt

    options, argv = gnu_getopt(sys.argv[1:], '', ['interface'])

    as_interface = False

    for opt, val in options:
        if opt == '--interface':
            as_interface = True

    head, tail = argv

    print '\n'.join(gobject_header(head, tail, as_interface=as_interface))
