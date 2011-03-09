# -*- coding: utf-8 -*-

"""Feature test for ContactInfo.SupportedFields
"""

# Copyright © 2010 Collabora Ltd.
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

from servicetest import (EventPattern, assertEquals, call_async)
from gabbletest import (exec_test, GoogleXmlStream)
import constants as cs

PARAMS_EXACT = cs.CONTACT_INFO_FIELD_FLAG_PARAMETERS_EXACT
OVERWRITTEN_BY_NICKNAME = cs.CONTACT_INFO_FIELD_FLAG_OVERWRITTEN_BY_NICKNAME
UNLIMITED = 0xffffffffL

def types(s):
    ret = ['type=%s' % t for t in s.split()]
    ret.sort()
    return ret

def check_google_props(props):
    assertEquals(cs.CONTACT_INFO_FLAG_CAN_SET, props['ContactInfoFlags'])
    sf = props['SupportedFields']
    sf.sort()
    for f in sf:
        f[1].sort()     # type-parameters
    assertEquals([
        ('fn', [], PARAMS_EXACT | OVERWRITTEN_BY_NICKNAME, 1),
        ('n', [], PARAMS_EXACT, 1),
        ], sf)

def check_normal_props(props):
    assertEquals(cs.CONTACT_INFO_FLAG_CAN_SET, props['ContactInfoFlags'])
    sf = props['SupportedFields']
    sf.sort()
    for f in sf:
        f[1].sort()     # type-parameters
    assertEquals([
        ('adr', types('home work postal parcel dom intl pref'), 0, UNLIMITED),
        ('bday', [], PARAMS_EXACT, UNLIMITED),
        ('email', types('home work internet pref x400'), 0, UNLIMITED),
        ('fn', [], PARAMS_EXACT, 1),
        ('geo', [], PARAMS_EXACT, 1),
        ('label', types('home work postal parcel dom intl pref'), 0,
            UNLIMITED),
        ('mailer', [], PARAMS_EXACT, UNLIMITED),
        ('n', [], PARAMS_EXACT, 1),
        ('nickname', [], PARAMS_EXACT | OVERWRITTEN_BY_NICKNAME, UNLIMITED),
        ('note', [], PARAMS_EXACT, UNLIMITED),
        ('org', [], PARAMS_EXACT, UNLIMITED),
        ('prodid', [], PARAMS_EXACT, UNLIMITED),
        ('rev', [], PARAMS_EXACT, UNLIMITED),
        ('role', [], PARAMS_EXACT, UNLIMITED),
        ('sort-string', [], PARAMS_EXACT, UNLIMITED),
        ('tel', types('home work voice fax pager msg cell video bbs '
            'modem isdn pcs pref'), 0, UNLIMITED),
        ('title', [], PARAMS_EXACT, UNLIMITED),
        ('tz', [], PARAMS_EXACT, UNLIMITED),
        ('uid', [], PARAMS_EXACT, UNLIMITED),
        ('url', [], PARAMS_EXACT, UNLIMITED),
        ('x-desc', [], PARAMS_EXACT, UNLIMITED),
        ('x-jabber', [], PARAMS_EXACT, UNLIMITED),
        ], sf)

def test_google(q, bus, conn, stream):
    test(q, bus, conn, stream, is_google=True)

def test(q, bus, conn, stream, is_google=False):
    props = conn.GetAll(cs.CONN_IFACE_CONTACT_INFO,
            dbus_interface=cs.PROPERTIES_IFACE)
    check_normal_props(props)

    conn.Connect()

    q.expect('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])

    props = conn.GetAll(cs.CONN_IFACE_CONTACT_INFO,
            dbus_interface=cs.PROPERTIES_IFACE)

    if is_google:
        check_google_props(props)

        # on a Google server, we can't use most vCard fields
        call_async(q, conn.ContactInfo, 'SetContactInfo',
                [('x-jabber', [], ['wee.ninja@collabora.co.uk'])])
        q.expect('dbus-error', method='SetContactInfo',
                name=cs.INVALID_ARGUMENT)
    else:
        check_normal_props(props)

if __name__ == '__main__':
    exec_test(test, do_connect=False)
    exec_test(test_google, protocol=GoogleXmlStream, do_connect=False)
