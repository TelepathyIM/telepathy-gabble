
import base64

from twisted.words.xish import xpath

import constants as cs
from servicetest import (EventPattern, call_async, sync_dbus, assertEquals,
        assertLength)
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
    call_async(q, conn.Aliasing, 'SetAliases', {handle: 'Robert the Bruce'})
    sync_dbus(bus, q, conn)
    acknowledge_iq(stream, vcard_get_event.stanza)

    # Gabble sets a new vCard with our nickname.
    vcard_set_event = q.expect('stream-iq', iq_type='set',
        query_ns=ns.VCARD_TEMP, query_name='vCard')
    assertEquals('Robert the Bruce', xpath.queryForString('/iq/vCard/NICKNAME',
        vcard_set_event.stanza))
    assertEquals(None, xpath.queryForNodes('/iq/vCard/PHOTO',
        vcard_set_event.stanza))
    assertEquals(None, xpath.queryForNodes('/iq/vCard/FN',
        vcard_set_event.stanza))
    assertEquals(None, xpath.queryForNodes('/iq/vCard/N',
        vcard_set_event.stanza))

    # Before the server replies, the user sets their avatar
    call_async(q, conn.Avatars, 'SetAvatar', 'hello', 'image/png')
    sync_dbus(bus, q, conn)
    # This acknowledgement is for the nickname
    acknowledge_iq(stream, vcard_set_event.stanza)

    hello_binval = base64.b64encode('hello')

    # This sets the avatar
    vcard_set_event = q.expect('stream-iq', iq_type='set',
        query_ns=ns.VCARD_TEMP, query_name='vCard')
    assertEquals('Robert the Bruce', xpath.queryForString('/iq/vCard/NICKNAME',
        vcard_set_event.stanza))
    assertLength(1, xpath.queryForNodes('/iq/vCard/PHOTO',
        vcard_set_event.stanza))
    assertEquals('image/png', xpath.queryForString('/iq/vCard/PHOTO/TYPE',
        vcard_set_event.stanza))
    assertEquals(hello_binval, xpath.queryForString('/iq/vCard/PHOTO/BINVAL',
        vcard_set_event.stanza))
    assertEquals(None, xpath.queryForNodes('/iq/vCard/FN',
        vcard_set_event.stanza))
    assertEquals(None, xpath.queryForNodes('/iq/vCard/N',
        vcard_set_event.stanza))

    # Before the server replies, the user sets their ContactInfo
    call_async(q, conn.ContactInfo, 'SetContactInfo',
               [(u'fn', [], [u'King Robert I']),
                (u'n', [], [u'de Brus', u'Robert', u'', u'King', u'']),
                (u'nickname', [], [u'Bob'])])
    sync_dbus(bus, q, conn)
    # This acknowledgement is for the avatar; SetAvatar won't happen
    # until this has
    acknowledge_iq(stream, vcard_set_event.stanza)
    q.expect('dbus-return', method='SetAvatar')

    # This sets the ContactInfo
    vcard_set_event = q.expect('stream-iq', iq_type='set',
        query_ns=ns.VCARD_TEMP, query_name='vCard')
    assertEquals('Bob', xpath.queryForString('/iq/vCard/NICKNAME',
        vcard_set_event.stanza))
    assertLength(1, xpath.queryForNodes('/iq/vCard/PHOTO',
        vcard_set_event.stanza))
    assertEquals('image/png', xpath.queryForString('/iq/vCard/PHOTO/TYPE',
        vcard_set_event.stanza))
    assertEquals(hello_binval, xpath.queryForString('/iq/vCard/PHOTO/BINVAL',
        vcard_set_event.stanza))
    assertLength(1, xpath.queryForNodes('/iq/vCard/N',
        vcard_set_event.stanza))
    assertEquals('Robert', xpath.queryForString('/iq/vCard/N/GIVEN',
        vcard_set_event.stanza))
    assertEquals('de Brus', xpath.queryForString('/iq/vCard/N/FAMILY',
        vcard_set_event.stanza))
    assertEquals('King', xpath.queryForString('/iq/vCard/N/PREFIX',
        vcard_set_event.stanza))
    assertEquals('King Robert I', xpath.queryForString('/iq/vCard/FN',
        vcard_set_event.stanza))

    # Before the server replies, the user unsets their avatar
    call_async(q, conn.Avatars, 'SetAvatar', '', '')
    sync_dbus(bus, q, conn)

    # This acknowledgement is for the ContactInfo; SetContactInfo won't happen
    # until this has
    acknowledge_iq(stream, vcard_set_event.stanza)
    q.expect('dbus-return', method='SetContactInfo')

    vcard_set_event = q.expect('stream-iq', iq_type='set',
        query_ns=ns.VCARD_TEMP, query_name='vCard')
    assertEquals('Bob', xpath.queryForString('/iq/vCard/NICKNAME',
        vcard_set_event.stanza))
    assertEquals(None, xpath.queryForNodes('/iq/vCard/PHOTO',
        vcard_set_event.stanza))
    assertLength(1, xpath.queryForNodes('/iq/vCard/N',
        vcard_set_event.stanza))
    assertEquals('Robert', xpath.queryForString('/iq/vCard/N/GIVEN',
        vcard_set_event.stanza))
    assertEquals('de Brus', xpath.queryForString('/iq/vCard/N/FAMILY',
        vcard_set_event.stanza))
    assertEquals('King', xpath.queryForString('/iq/vCard/N/PREFIX',
        vcard_set_event.stanza))
    assertEquals('King Robert I', xpath.queryForString('/iq/vCard/FN',
        vcard_set_event.stanza))

    # This acknowledgement is for the avatar; SetAvatar won't finish
    # until this is received
    acknowledge_iq(stream, vcard_set_event.stanza)
    q.expect('dbus-return', method='SetAvatar')

    # Now Gabble gets disconnected.
    sync_stream(q, stream)
    conn.Disconnect()
    q.expect('dbus-signal', signal='StatusChanged', args=[2, 1])

if __name__ == '__main__':
    exec_test(test)
