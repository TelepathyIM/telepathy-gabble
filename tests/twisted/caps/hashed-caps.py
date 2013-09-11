# coding=utf-8
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

from gabbletest import (
    exec_test, make_result_iq, make_presence, sync_stream, elem,
)
from servicetest import sync_dbus, EventPattern, assertLength, assertEquals
import constants as cs
import ns
from caps_helper import (
    compute_caps_hash, make_caps_disco_reply, send_disco_reply,
    fake_client_dataforms, assert_rccs_callable, assert_rccs_not_callable)

from config import VOIP_ENABLED

if not VOIP_ENABLED:
    print "NOTE: built with --disable-voip"
    raise SystemExit(77)

some_identities = [
    'client/pc/fr/le gabble',
    'client/pc/en/gabble',
    ]
jingle_av_features = [
    ns.JINGLE_015,
    ns.JINGLE_015_AUDIO,
    ns.JINGLE_015_VIDEO,
    ns.GOOGLE_P2P,
    ]

def test_hash(q, bus, conn, stream, contact, contact_handle, client):
    presence = make_presence(contact, status='hello')
    stream.send(presence)

    q.expect('dbus-signal', signal='PresencesChanged',
            args=[{contact_handle:
                (2, u'available', 'hello')}])

    # no special capabilities
    basic_caps = [(contact_handle, cs.CHANNEL_TYPE_TEXT, 3, 0)]
    assert conn.Capabilities.GetCapabilities([contact_handle]) == basic_caps
    for rcc in conn.ContactCapabilities.GetContactCapabilities([contact_handle])[contact_handle]:
        assertEquals(cs.CHANNEL_TYPE_TEXT, rcc[0].get(cs.CHANNEL_TYPE))

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
    send_disco_reply(stream, event.stanza, [], jingle_av_features)

    # we can now do audio calls
    old, new = q.expect_many(
        EventPattern('dbus-signal', signal='CapabilitiesChanged'),
        EventPattern('dbus-signal', signal='ContactCapabilitiesChanged'),
        )

    caps_diff = old.args[0]
    media_diff = [c for c in caps_diff
                    if c[1] == cs.CHANNEL_TYPE_STREAMED_MEDIA][0]
    assert media_diff[5] & cs.MEDIA_CAP_AUDIO, media_diff[5]

    assert_rccs_callable(new.args[0][contact_handle])
    assertEquals(new.args[0],
            conn.ContactCapabilities.GetContactCapabilities([contact_handle]))

    # Send presence without any capabilities. XEP-0115 §8.4 Caps Optimization
    # says “receivers of presence notifications MUST NOT expect an annotation
    # on every presence notification they receive”, so the contact should still
    # be media-capable afterwards.
    stream.send(make_presence(contact, status='very capable'))
    q.expect('dbus-signal', signal='PresencesChanged',
        args=[{contact_handle: (2, u'available', 'very capable')}])
    ye_olde_caps = conn.Capabilities.GetCapabilities([contact_handle])
    assertLength(1, [c for c in ye_olde_caps
                       if c[1] == cs.CHANNEL_TYPE_STREAMED_MEDIA and
                          c[3] & cs.MEDIA_CAP_AUDIO])
    # still exactly the same capabilities
    assertEquals(new.args[0],
            conn.ContactCapabilities.GetContactCapabilities([contact_handle]))

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
    send_disco_reply(stream, event.stanza, [],
        ['http://jabber.org/protocol/bogus-feature'])

    # don't receive any D-Bus signal
    forbidden = [
        EventPattern('dbus-signal', signal='CapabilitiesChanged'),
        EventPattern('dbus-signal', signal='ContactCapabilitiesChanged'),
        ]
    q.forbid_events(forbidden)
    sync_dbus(bus, q, conn)
    sync_stream(q, stream)

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

    # send good reply
    q.unforbid_events(forbidden)
    result = make_result_iq(stream, event.stanza)
    query = result.firstChildElement()
    stream.send(result)

    # we can now do nothing
    old, new = q.expect_many(
        EventPattern('dbus-signal', signal='CapabilitiesChanged'),
        EventPattern('dbus-signal', signal='ContactCapabilitiesChanged'),
        )
    for rcc in new.args[0][contact_handle]:
        assertEquals(cs.CHANNEL_TYPE_TEXT, rcc[0].get(cs.CHANNEL_TYPE))
    assert_rccs_not_callable(new.args[0][contact_handle])
    assertEquals(new.args[0],
            conn.ContactCapabilities.GetContactCapabilities([contact_handle]))

    # send correct presence
    ver = compute_caps_hash(some_identities, jingle_av_features, fake_client_dataforms)
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
    q.forbid_events(forbidden)
    sync_dbus(bus, q, conn)
    q.unforbid_events(forbidden)

    # send good reply
    send_disco_reply(
        stream, event.stanza, some_identities, jingle_av_features, fake_client_dataforms)

    # we can now do audio calls
    old, new = q.expect_many(
        EventPattern('dbus-signal', signal='CapabilitiesChanged'),
        EventPattern('dbus-signal', signal='ContactCapabilitiesChanged'),
        )
    assert_rccs_callable(new.args[0][contact_handle])
    assertEquals(new.args[0],
            conn.ContactCapabilities.GetContactCapabilities([contact_handle]))

def test_two_clients(q, bus, conn, stream, contact1, contact2,
        contact_handle1, contact_handle2, client, broken_hash):

    presence = make_presence(contact1, status='hello')
    stream.send(presence)

    q.expect('dbus-signal', signal='PresencesChanged',
            args=[{contact_handle1:
                (2, u'available', 'hello')}])

    presence = make_presence(contact2, status='hello')
    stream.send(presence)

    q.expect('dbus-signal', signal='PresencesChanged',
            args=[{contact_handle2:
                (2, u'available', 'hello')}])

    # no special capabilities
    basic_caps = [(contact_handle1, cs.CHANNEL_TYPE_TEXT, 3, 0)]
    assert conn.Capabilities.GetCapabilities([contact_handle1]) == basic_caps
    basic_caps = [(contact_handle2, cs.CHANNEL_TYPE_TEXT, 3, 0)]
    assert conn.Capabilities.GetCapabilities([contact_handle2]) == basic_caps

    for h in (contact_handle1, contact_handle2):
        for rcc in conn.ContactCapabilities.GetContactCapabilities([h])[h]:
            assertEquals(cs.CHANNEL_TYPE_TEXT, rcc[0].get(cs.CHANNEL_TYPE))

    # send updated presence with Jingle caps info
    ver = compute_caps_hash(some_identities, jingle_av_features, {})
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
    forbidden = [
        EventPattern('dbus-signal', signal='CapabilitiesChanged'),
        EventPattern('dbus-signal', signal='ContactCapabilitiesChanged'),
        ]
    q.forbid_events(forbidden)
    sync_dbus(bus, q, conn)
    q.unforbid_events(forbidden)

    result = make_caps_disco_reply(
        stream, event.stanza, some_identities, jingle_av_features)

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
        q.forbid_events(forbidden)
        sync_dbus(bus, q, conn)
        q.unforbid_events(forbidden)

        # send good reply
        send_disco_reply(stream, event.stanza, some_identities, jingle_av_features)

    # we can now do audio calls
    old, new = q.expect_many(
        EventPattern('dbus-signal', signal='CapabilitiesChanged',
            args=[[(contact_handle2, cs.CHANNEL_TYPE_STREAMED_MEDIA, 0, 3, 0,
                cs.MEDIA_CAP_AUDIO | cs.MEDIA_CAP_VIDEO)]]),
        EventPattern('dbus-signal', signal='ContactCapabilitiesChanged',
            predicate=lambda e: contact_handle2 in e.args[0]),
        )
    assert_rccs_callable(new.args[0][contact_handle2])

    if not broken_hash:
        # if the first contact failed to provide a good hash, it does not
        # deserve its capabilities to be understood by Gabble!
        old, new = q.expect_many(
            EventPattern('dbus-signal', signal='CapabilitiesChanged',
                args=[[(contact_handle1, cs.CHANNEL_TYPE_STREAMED_MEDIA, 0, 3, 0,
                    cs.MEDIA_CAP_AUDIO | cs.MEDIA_CAP_VIDEO)]]),
            EventPattern('dbus-signal', signal='ContactCapabilitiesChanged',
                predicate=lambda e: contact_handle1 in e.args[0]),
            )
        assert_rccs_callable(new.args[0][contact_handle1])

    # don't receive any further signals
    q.forbid_events(forbidden)
    sync_dbus(bus, q, conn)
    q.unforbid_events(forbidden)

def test_39464(q, bus, conn, stream):
    """
    Regression test for an issue where a form with no type='' attribute on the
    <x/> node would crash Gabble.
    """
    client = 'fake:qutim'
    hash = 'blahblah'
    contact = 'bug-39464@example.com/foo'
    caps = {
        'node': client,
        'ver': hash,
        'hash': 'sha-1',
        }
    presence = make_presence(contact, status='hello', caps=caps)
    stream.send(presence)

    # Gabble looks up our capabilities
    event = q.expect('stream-iq', to=contact, query_ns=ns.DISCO_INFO)

    # Send a reply with a form without a type=''
    result = make_result_iq(stream, event.stanza, add_query_node=False)
    result.addChild(
        elem(ns.DISCO_INFO, 'query', node='%s#%s' % (client, hash))(
          # NB. no type='' attribute
          elem(ns.X_DATA, 'x')
        )
      )
    stream.send(result)
    # We don't really care what Gabble does, as long as it doesn't crash.
    sync_stream(q, stream)

def test(q, bus, conn, stream):
    test_hash(q, bus, conn, stream, 'bob@foo.com/Foo', 2L, 'http://telepathy.freedesktop.org/fake-client')
    test_hash(q, bus, conn, stream, 'bob2@foo.com/Foo', 3L, 'http://telepathy.freedesktop.org/fake-client2')

    test_two_clients(q, bus, conn, stream, 'user1@example.com/Res',
            'user2@example.com/Res', 4L, 5L,
            'http://telepathy.freedesktop.org/fake-client3', 0)
    test_two_clients(q, bus, conn, stream, 'user3@example.com/Res',
            'user4@example.com/Res', 6L, 7L,
            'http://telepathy.freedesktop.org/fake-client4', 1)

    test_39464(q, bus, conn, stream)

if __name__ == '__main__':
    exec_test(test)
