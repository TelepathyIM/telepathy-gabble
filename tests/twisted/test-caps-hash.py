
"""
Test the verification string introduced in version 1.5 of XEP-0115

This test changes the caps several times:
- Initial presence to be online
- Change presence to handle audio calls, using XEP-0115-v1.3. Check that
  'CapabilitiesChanged' *is* fired
- Change presence *not* to handle audio calls, using XEP-0115-v1.5, but with a
  *bogus* hash. Check that 'CapabilitiesChanged' is *not* fired
- Change presence *not* to handle audio calls, using XEP-0115-v1.5, with a
  *good* hash. Check that 'CapabilitiesChanged' *is* fired
- Change presence to handle audio calls, using XEP-0115-v1.5, with a XEP-0128
  dataform. Check that 'CapabilitiesChanged' is fired
This is done for 2 contacts

Then, this test announce 2 contacts with the same hash.
- Gabble must ask only once for the hash and update the caps for both contacts
- When the caps advertised by the first contact does not match, Gabble asks
  the second and update only the caps of the second contact
"""

import dbus
import sys

from twisted.words.xish import domish, xpath

from gabbletest import exec_test, make_result_iq
from servicetest import call_async

gabble_service = 'org.freedesktop.Telepathy.ConnectionManager.gabble'
text = 'org.freedesktop.Telepathy.Channel.Type.Text'
sm = 'org.freedesktop.Telepathy.Channel.Type.StreamedMedia'
conn_iface = 'org.freedesktop.Telepathy.Connection'
caps_iface = 'org.freedesktop.Telepathy.Connection.Interface.Capabilities'

caps_changed_flag = 0

def caps_changed_cb(dummy):
    # Workaround to bug 9980: do not raise an error but use a flag
    # https://bugs.freedesktop.org/show_bug.cgi?id=9980
    global caps_changed_flag
    caps_changed_flag = 1

def make_presence(from_jid, type, status):
    presence = domish.Element((None, 'presence'))

    if from_jid is not None:
        presence['from'] = from_jid

    if type is not None:
        presence['type'] = type

    if status is not None:
        presence.addElement('status', content=status)

    return presence

def presence_add_caps(presence, ver, client, hash=None):
    c = presence.addElement(('http://jabber.org/protocol/caps', 'c'))
    c['node'] = client
    c['ver'] = ver
    if hash is not None:
        c['hash'] = hash
    return presence

def dbus_sync(bus, q, conn):
    # Dummy D-Bus method call
    call_async(q, conn, "InspectHandles", 1, [])

    event = q.expect('dbus-return', method='InspectHandles')

def test_hash(q, bus, conn, stream, contact, contact_handle, client):
    global caps_changed_flag

    presence = make_presence(contact, None, 'hello')
    stream.send(presence)

    event = q.expect('dbus-signal', signal='PresenceUpdate',
        args=[{contact_handle: (0L, {u'available': {'message': 'hello'}})}])

    # no special capabilities
    basic_caps = [(contact_handle, text, 3, 0)]
    assert conn.Capabilities.GetCapabilities([contact_handle]) == basic_caps

    # send updated presence with Jingle caps info
    presence = make_presence(contact, None, 'hello')
    presence = presence_add_caps(presence, '0.1', client)
    stream.send(presence)

    # Gabble looks up our capabilities
    event = q.expect('stream-iq', to=contact,
        query_ns='http://jabber.org/protocol/disco#info')
    query_node = xpath.queryForNodes('/iq/query', event.stanza)[0]
    assert query_node.attributes['node'] == \
        client + '#' + '0.1'

    # send good reply
    result = make_result_iq(stream, event.stanza)
    query = result.firstChildElement()
    feature = query.addElement('feature')
    feature['var'] = 'http://jabber.org/protocol/jingle'
    feature = query.addElement('feature')
    feature['var'] = 'http://jabber.org/protocol/jingle/description/audio'
    feature = query.addElement('feature')
    feature['var'] = 'http://www.google.com/transport/p2p'
    stream.send(result)

    # we can now do audio calls
    event = q.expect('dbus-signal', signal='CapabilitiesChanged')
    caps_changed_flag = 0

    # send bogus presence
    presence = make_presence(contact, None, 'hello')
    c = presence.addElement(('http://jabber.org/protocol/caps', 'c'))
    c['node'] = client
    c['ver'] = 'KyuUmfhC34jP1sDjs489RjkJfsg=' # good hash
    c['ver'] = 'X' + c['ver'] # now the hash is broken
    c['hash'] = 'sha-1'
    stream.send(presence)

    # Gabble looks up our capabilities
    event = q.expect('stream-iq', to=contact,
        query_ns='http://jabber.org/protocol/disco#info')
    query_node = xpath.queryForNodes('/iq/query', event.stanza)[0]
    assert query_node.attributes['node'] == \
        client + '#' + c['ver']

    # send bogus reply
    result = make_result_iq(stream, event.stanza)
    query = result.firstChildElement()
    feature = query.addElement('feature')
    feature['var'] = 'http://jabber.org/protocol/bogus-feature'
    stream.send(result)

    # don't receive any D-Bus signal
    dbus_sync(bus, q, conn)
    assert caps_changed_flag == 0

    # send presence with empty caps
    presence = make_presence(contact, None, 'hello')
    presence = presence_add_caps(presence, '0.0', client)
    stream.send(presence)

    # Gabble looks up our capabilities
    event = q.expect('stream-iq', to=contact,
        query_ns='http://jabber.org/protocol/disco#info')
    query_node = xpath.queryForNodes('/iq/query', event.stanza)[0]
    assert query_node.attributes['node'] == \
        client + '#' + '0.0'

    # still don't receive any D-Bus signal
    dbus_sync(bus, q, conn)
    assert caps_changed_flag == 0

    # send good reply
    result = make_result_iq(stream, event.stanza)
    query = result.firstChildElement()
    stream.send(result)

    # we can now do nothing
    event = q.expect('dbus-signal', signal='CapabilitiesChanged')
    assert caps_changed_flag == 1
    caps_changed_flag = 0

    # send correct presence
    presence = make_presence(contact, None, 'hello')
    c = presence.addElement(('http://jabber.org/protocol/caps', 'c'))
    c['node'] = client
    c['ver'] = 'CzO+nkbflbxu1pgzOQSIi8gOyDc=' # good hash
    c['hash'] = 'sha-1'
    stream.send(presence)

    # Gabble looks up our capabilities
    event = q.expect('stream-iq', to=contact,
        query_ns='http://jabber.org/protocol/disco#info')
    query_node = xpath.queryForNodes('/iq/query', event.stanza)[0]
    assert query_node.attributes['node'] == \
        client + '#' + c['ver']

    # don't receive any D-Bus signal
    dbus_sync(bus, q, conn)
    assert caps_changed_flag == 0

    # send good reply
    result = make_result_iq(stream, event.stanza)
    query = result.firstChildElement()
    query['node'] = client + '#' + c['ver']
    feature = query.addElement('feature')
    feature['var'] = 'http://jabber.org/protocol/jingle'
    feature = query.addElement('feature')
    feature['var'] = 'http://jabber.org/protocol/jingle/description/audio'
    feature = query.addElement('feature')
    feature['var'] = 'http://www.google.com/transport/p2p'

    query.addRawXml("""
<x type='result' xmlns='jabber:x:data'>
<field var='FORM_TYPE' type='hidden'>
<value>urn:xmpp:dataforms:softwareinfo</value>
</field>
<field var='software'>
<value>A Fake Client with Twisted</value>
</field>
<field var='software_version'>
<value>5.11.2-svn-20080512</value>
</field>
<field var='os'>
<value>Debian GNU/Linux unstable (sid) unstable sid</value>
</field>
<field var='os_version'>
<value>2.6.24-1-amd64</value>
</field>
</x>
    """)
    stream.send(result)

    # we can now do audio calls
    event = q.expect('dbus-signal', signal='CapabilitiesChanged')
    assert caps_changed_flag == 1
    caps_changed_flag = 0

def test_two_clients(q, bus, conn, stream, contact1, contact2,
        contact_handle1, contact_handle2, client, broken_hash):
    global caps_changed_flag

    presence = make_presence(contact1, None, 'hello')
    stream.send(presence)

    event = q.expect('dbus-signal', signal='PresenceUpdate',
        args=[{contact_handle1: (0L, {u'available': {'message': 'hello'}})}])

    presence = make_presence(contact2, None, 'hello')
    stream.send(presence)

    event = q.expect('dbus-signal', signal='PresenceUpdate',
        args=[{contact_handle2: (0L, {u'available': {'message': 'hello'}})}])

    # no special capabilities
    basic_caps = [(contact_handle1, text, 3, 0)]
    assert conn.Capabilities.GetCapabilities([contact_handle1]) == basic_caps
    basic_caps = [(contact_handle2, text, 3, 0)]
    assert conn.Capabilities.GetCapabilities([contact_handle2]) == basic_caps

    # send updated presence with Jingle caps info
    presence = make_presence(contact1, None, 'hello')
    ver = 'JpaYgiKL0y4fUOCTwN3WLGpaftM='
    presence = presence_add_caps(presence, ver, client,
            hash='sha-1')
    stream.send(presence)
    presence = make_presence(contact2, None, 'hello')
    presence = presence_add_caps(presence, ver, client,
            hash='sha-1')
    stream.send(presence)

    # Gabble looks up our capabilities
    event = q.expect('stream-iq', to=contact1,
        query_ns='http://jabber.org/protocol/disco#info')
    query_node = xpath.queryForNodes('/iq/query', event.stanza)[0]
    assert query_node.attributes['node'] == \
        client + '#' + ver

    # don't receive any D-Bus signal
    dbus_sync(bus, q, conn)
    assert caps_changed_flag == 0

    # send good reply
    result = make_result_iq(stream, event.stanza)
    query = result.firstChildElement()
    feature = query.addElement('feature')
    feature['var'] = 'http://jabber.org/protocol/jingle'
    feature = query.addElement('feature')
    feature['var'] = 'http://jabber.org/protocol/jingle/description/audio'
    feature = query.addElement('feature')
    feature['var'] = 'http://www.google.com/transport/p2p'
    if broken_hash:
        # make the hash break!
        feature = query.addElement('feature')
        feature['var'] = 'http://broken-feature'
    stream.send(result)

    if broken_hash:
        # Gabble looks up our capabilities again because the first contact
        # failed to provide a valid hash
        event = q.expect('stream-iq', to=contact2,
            query_ns='http://jabber.org/protocol/disco#info')
        query_node = xpath.queryForNodes('/iq/query', event.stanza)[0]
        assert query_node.attributes['node'] == \
            client + '#' + ver

        # don't receive any D-Bus signal
        dbus_sync(bus, q, conn)
        assert caps_changed_flag == 0

        # send good reply
        result = make_result_iq(stream, event.stanza)
        query = result.firstChildElement()
        feature = query.addElement('feature')
        feature['var'] = 'http://jabber.org/protocol/jingle'
        feature = query.addElement('feature')
        feature['var'] = 'http://jabber.org/protocol/jingle/description/audio'
        feature = query.addElement('feature')
        feature['var'] = 'http://www.google.com/transport/p2p'
        stream.send(result)

    # we can now do audio calls with both contacts
    event = q.expect('dbus-signal', signal='CapabilitiesChanged',
        args=[[(contact_handle2, sm, 0, 3, 0, 1)]])#  what are the good values?!
    if not broken_hash:
        # if the first contact failed to provide a good hash, it does not
        # deserve its capabilities to be understood by Gabble!
        event = q.expect('dbus-signal', signal='CapabilitiesChanged',
            args=[[(contact_handle1, sm, 0, 3, 0, 1)]])#  what are the good values?!

    caps_changed_flag = 0

    # don't receive any D-Bus signal
    dbus_sync(bus, q, conn)
    assert caps_changed_flag == 0

def test(q, bus, conn, stream):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged', args=[0, 1])

    # be notified when the signal CapabilitiesChanged is fired
    conn_caps_iface = dbus.Interface(conn, caps_iface)
    conn_caps_iface.connect_to_signal('CapabilitiesChanged', caps_changed_cb)

    test_hash(q, bus, conn, stream, 'bob@foo.com/Foo', 2L, 'http://telepathy.freedesktop.org/fake-client')
    test_hash(q, bus, conn, stream, 'bob2@foo.com/Foo', 3L, 'http://telepathy.freedesktop.org/fake-client2')

    test_two_clients(q, bus, conn, stream, 'user1@example.com/Res',
            'user2@example.com/Res', 4L, 5L,
            'http://telepathy.freedesktop.org/fake-client3', 0)
    test_two_clients(q, bus, conn, stream, 'user3@example.com/Res',
            'user4@example.com/Res', 6L, 7L,
            'http://telepathy.freedesktop.org/fake-client4', 1)

    conn.Disconnect()
    q.expect('dbus-signal', signal='StatusChanged', args=[2, 1])


if __name__ == '__main__':
    exec_test(test)

