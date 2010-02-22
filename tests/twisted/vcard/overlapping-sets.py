
import base64

from twisted.words.xish import xpath

import constants as cs
from servicetest import EventPattern, call_async, sync_dbus
from gabbletest import (
    acknowledge_iq, exec_test, expect_and_handle_get_vcard, make_result_iq,
    sync_stream)
import ns

def test(q, bus, conn, stream):
    conn.Connect()

    # Initial vCard request. Respond only after we call SetAliases().
    vcard_get_event = q.expect('stream-iq', iq_type='get', to=None,
        query_ns=ns.VCARD_TEMP, query_name='vCard')
    sync_stream(q, stream)

    handle = conn.GetSelfHandle()
    call_async(q, conn.Aliasing, 'SetAliases', {handle: 'Some Guy'})
    sync_dbus(bus, q, conn)
    acknowledge_iq(stream, vcard_get_event.stanza)

    # Gabble sets a new vCard with our nickname.
    vcard_set_event = q.expect('stream-iq', iq_type='set',
        query_ns=ns.VCARD_TEMP, query_name='vCard')

    # Before the server replies, the user sets their avatar
    call_async(q, conn.Avatars, 'SetAvatar', 'hello', 'image/png')
    sync_dbus(bus, q, conn)
    # This acknowledgement is for the nickname
    acknowledge_iq(stream, vcard_set_event.stanza)

    # This sets the avatar
    vcard_set_event = q.expect('stream-iq', iq_type='set',
        query_ns=ns.VCARD_TEMP, query_name='vCard')
    # Before the server replies, the user sets their ContactInfo
    call_async(q, conn.ContactInfo, 'SetContactInfo',
               [(u'fn', [], [u'Bob']),
                (u'n', [], [u'', u'Bob', u'', u'', u'']),
                (u'nickname', [], [u'bob'])])
    sync_dbus(bus, q, conn)
    # This acknowledgement is for the avatar; SetAvatar won't happen
    # until this has
    acknowledge_iq(stream, vcard_set_event.stanza)
    q.expect('dbus-return', method='SetAvatar')

    # This sets the ContactInfo
    vcard_set_event = q.expect('stream-iq', iq_type='set',
        query_ns=ns.VCARD_TEMP, query_name='vCard')
    # This acknowledgement is for the ContactInfo; SetContactInfo won't happen
    # until this has
    acknowledge_iq(stream, vcard_set_event.stanza)
    q.expect('dbus-return', method='SetContactInfo')

    # Now Gabble gets disconnected.
    sync_stream(q, stream)
    conn.Disconnect()
    q.expect('dbus-signal', signal='StatusChanged', args=[2, 1])

if __name__ == '__main__':
    exec_test(test)
