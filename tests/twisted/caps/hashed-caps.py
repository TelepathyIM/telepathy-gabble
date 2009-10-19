
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

from twisted.words.xish import xpath

from gabbletest import exec_test, make_result_iq, make_presence, sync_stream
from servicetest import sync_dbus, EventPattern
import constants as cs
import ns
from caps_helper import (
    compute_caps_hash, make_caps_disco_reply, fake_client_dataforms,
    )

caps_changed_flag = False

jingle_av_features = [
    ns.JINGLE_015,
    ns.JINGLE_015_AUDIO,
    ns.JINGLE_015_VIDEO,
    ns.GOOGLE_P2P,
    ]

def caps_changed_cb(dummy):
    # Workaround to bug 9980: do not raise an error but use a flag
    # https://bugs.freedesktop.org/show_bug.cgi?id=9980
    global caps_changed_flag
    caps_changed_flag = True

def test_hash(q, bus, conn, stream, contact, contact_handle, client):
    global caps_changed_flag

    presence = make_presence(contact, status='hello')
    stream.send(presence)

    q.expect_many(
        EventPattern('dbus-signal', signal='PresenceUpdate',
            args=[{contact_handle:
                (0L, {u'available': {'message': 'hello'}})}]),
        EventPattern('dbus-signal', signal='PresencesChanged',
            args=[{contact_handle:
                (2, u'available', 'hello')}]))

    # no special capabilities
    basic_caps = [(contact_handle, cs.CHANNEL_TYPE_TEXT, 3, 0)]
    assert conn.Capabilities.GetCapabilities([contact_handle]) == basic_caps

    # send updated presence with Jingle caps info
    presence = make_presence(contact, status='hello',
        caps={'node': client,
              'ver':  '0.1',
             })
    stream.send(presence)

    # Gabble looks up our capabilities
    event = q.expect('stream-iq', to=contact,
        query_ns='http://jabber.org/protocol/disco#info')
    query_node = xpath.queryForNodes('/iq/query', event.stanza)[0]
    assert query_node.attributes['node'] == \
        client + '#' + '0.1'

    # send good reply
    stream.send(make_caps_disco_reply(stream, event.stanza,
        jingle_av_features))

    # we can now do audio calls
    event = q.expect('dbus-signal', signal='CapabilitiesChanged')
    caps_changed_flag = False

    # send bogus presence
    caps = {
        'node': client,
        'ver':  'ceci=nest=pas=un=hash',
        'hash': 'sha-1',
        }
    presence = make_presence(contact, status='hello', caps=caps)
    stream.send(presence)

    # Gabble looks up our capabilities
    event = q.expect('stream-iq', to=contact,
        query_ns='http://jabber.org/protocol/disco#info')
    query_node = xpath.queryForNodes('/iq/query', event.stanza)[0]
    assert query_node.attributes['node'] == \
        client + '#' + caps['ver']

    # send bogus reply
    stream.send(make_caps_disco_reply(stream, event.stanza,
        ['http://jabber.org/protocol/bogus-feature']))

    # don't receive any D-Bus signal
    sync_dbus(bus, q, conn)
    sync_stream(q, stream)
    assert caps_changed_flag == False


    # send presence with empty caps
    presence = make_presence(contact, status='hello',
        caps={'node': client,
              'ver':  '0.0',
             })
    stream.send(presence)

    # Gabble looks up our capabilities
    event = q.expect('stream-iq', to=contact,
        query_ns='http://jabber.org/protocol/disco#info')
    query_node = xpath.queryForNodes('/iq/query', event.stanza)[0]
    assert query_node.attributes['node'] == \
        client + '#' + '0.0'

    # still don't receive any D-Bus signal
    sync_dbus(bus, q, conn)
    assert caps_changed_flag == False

    # send good reply
    result = make_result_iq(stream, event.stanza)
    query = result.firstChildElement()
    stream.send(result)

    # we can now do nothing
    event = q.expect('dbus-signal', signal='CapabilitiesChanged')
    assert caps_changed_flag == True
    caps_changed_flag = False


    # send correct presence
    ver = compute_caps_hash([], jingle_av_features, fake_client_dataforms)
    caps = {
        'node': client,
        'ver':  ver,
        'hash': 'sha-1',
        }
    presence = make_presence(contact, status='hello', caps=caps)
    stream.send(presence)

    # Gabble looks up our capabilities
    event = q.expect('stream-iq', to=contact,
        query_ns='http://jabber.org/protocol/disco#info')
    query_node = xpath.queryForNodes('/iq/query', event.stanza)[0]
    assert query_node.attributes['node'] == \
        client + '#' + caps['ver']

    # don't receive any D-Bus signal
    sync_dbus(bus, q, conn)
    assert caps_changed_flag == False

    # send good reply
    result = make_caps_disco_reply(stream, event.stanza, jingle_av_features,
        fake_client_dataforms)
    stream.send(result)

    # we can now do audio calls
    event = q.expect('dbus-signal', signal='CapabilitiesChanged',
    )
    assert caps_changed_flag == True
    caps_changed_flag = False

def test_two_clients(q, bus, conn, stream, contact1, contact2,
        contact_handle1, contact_handle2, client, broken_hash):
    global caps_changed_flag

    presence = make_presence(contact1, status='hello')
    stream.send(presence)

    event = q.expect_many(
        EventPattern('dbus-signal', signal='PresenceUpdate',
            args=[{contact_handle1:
                (0L, {u'available': {'message': 'hello'}})}]),
        EventPattern('dbus-signal', signal='PresencesChanged',
            args=[{contact_handle1:
                (2, u'available', 'hello')}]))

    presence = make_presence(contact2, status='hello')
    stream.send(presence)

    event = q.expect_many(
        EventPattern('dbus-signal', signal='PresenceUpdate',
            args=[{contact_handle2:
                (0L, {u'available': {'message': 'hello'}})}]),
        EventPattern('dbus-signal', signal='PresencesChanged',
            args=[{contact_handle2:
                (2, u'available', 'hello')}]))

    # no special capabilities
    basic_caps = [(contact_handle1, cs.CHANNEL_TYPE_TEXT, 3, 0)]
    assert conn.Capabilities.GetCapabilities([contact_handle1]) == basic_caps
    basic_caps = [(contact_handle2, cs.CHANNEL_TYPE_TEXT, 3, 0)]
    assert conn.Capabilities.GetCapabilities([contact_handle2]) == basic_caps

    # send updated presence with Jingle caps info
    ver = compute_caps_hash([], jingle_av_features, {})
    caps = {
        'node': client,
        'ver': ver,
        'hash': 'sha-1',
        }
    presence = make_presence(contact1, status='hello', caps=caps)
    stream.send(presence)
    presence = make_presence(contact2, status='hello', caps=caps)
    stream.send(presence)

    # Gabble looks up our capabilities
    event = q.expect('stream-iq', to=contact1,
        query_ns='http://jabber.org/protocol/disco#info')
    query_node = xpath.queryForNodes('/iq/query', event.stanza)[0]
    assert query_node.attributes['node'] == \
        client + '#' + ver

    # don't receive any D-Bus signal
    sync_dbus(bus, q, conn)
    assert caps_changed_flag == False

    result = make_caps_disco_reply(stream, event.stanza, jingle_av_features)

    if broken_hash:
        # make the hash break!
        query = result.firstChildElement()
        query.addElement('feature')['var'] = 'http://example.com/another-feature'

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
        sync_dbus(bus, q, conn)
        assert caps_changed_flag == False

        # send good reply
        result = make_caps_disco_reply(stream, event.stanza, jingle_av_features)
        stream.send(result)

    # we can now do audio calls with both contacts
    event = q.expect('dbus-signal', signal='CapabilitiesChanged',
        args=[[(contact_handle2, cs.CHANNEL_TYPE_STREAMED_MEDIA, 0, 3, 0,
            cs.MEDIA_CAP_AUDIO | cs.MEDIA_CAP_VIDEO)]])
    if not broken_hash:
        # if the first contact failed to provide a good hash, it does not
        # deserve its capabilities to be understood by Gabble!
        event = q.expect('dbus-signal', signal='CapabilitiesChanged',
            args=[[(contact_handle1, cs.CHANNEL_TYPE_STREAMED_MEDIA, 0, 3, 0,
                cs.MEDIA_CAP_AUDIO | cs.MEDIA_CAP_VIDEO)]])

    caps_changed_flag = False

    # don't receive any D-Bus signal
    sync_dbus(bus, q, conn)
    assert caps_changed_flag == False

def test(q, bus, conn, stream):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])

    # be notified when the signal CapabilitiesChanged is fired
    conn_caps_iface = dbus.Interface(conn, cs.CONN_IFACE_CAPS)
    conn_caps_iface.connect_to_signal('CapabilitiesChanged', caps_changed_cb)

    test_hash(q, bus, conn, stream, 'bob@foo.com/Foo', 2L, 'http://telepathy.freedesktop.org/fake-client')
    test_hash(q, bus, conn, stream, 'bob2@foo.com/Foo', 3L, 'http://telepathy.freedesktop.org/fake-client2')

    test_two_clients(q, bus, conn, stream, 'user1@example.com/Res',
            'user2@example.com/Res', 4L, 5L,
            'http://telepathy.freedesktop.org/fake-client3', 0)
    test_two_clients(q, bus, conn, stream, 'user3@example.com/Res',
            'user4@example.com/Res', 6L, 7L,
            'http://telepathy.freedesktop.org/fake-client4', 1)

if __name__ == '__main__':
    exec_test(test)
