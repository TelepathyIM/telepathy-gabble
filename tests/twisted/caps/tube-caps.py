
"""
Test tubes capabilities with Connection.Interface.ContactCapabilities

1. Receive presence and caps from contacts and check that
GetContactCapabilities works correctly and that ContactCapabilitiesChanged is
correctly received. Also check that GetContactAttributes gives the same
results.

- no tube cap at all
- 1 stream tube cap
- 1 D-Bus tube cap
- 1 stream tube + 1 D-Bus tube caps
- 2 stream tube + 2 D-Bus tube caps
- 1 stream tube + 1 D-Bus tube caps, again, to test whether the caps cache
  works with tubes

2. Test UpdateCapabilities and test that a presence stanza is sent to the
contacts, test that the D-Bus signal ContactCapabilitiesChanged is fired for
the self handle, ask Gabble for its caps with an iq request, check the reply
is correct, and ask Gabble for its caps using D-Bus method
GetContactCapabilities. Also check that GetContactAttributes gives the same
results.

Ensure that just a Requested=True channel class in a client filter doesn't
make a tube service advertised as a cap.

- no tube cap at all
- 1 Requested=True stream tube cap
- 1 stream tube cap
- 1 D-Bus tube cap + 1 Requested=True cap
- 1 stream tube + 1 D-Bus tube caps + 1 Requested=True cap
- 2 stream tube + 2 D-Bus tube caps
- 1 stream tube + 1 D-Bus tube caps, again, just for the fun

"""

import dbus

from twisted.words.xish import xpath

from servicetest import assertEquals, assertLength, assertContains,\
        assertDoesNotContain
from gabbletest import exec_test, make_result_iq, sync_stream, make_presence
import constants as cs

from caps_helper import compute_caps_hash, text_fixed_properties,\
    text_allowed_properties, stream_tube_fixed_properties, stream_tube_allowed_properties,\
    dbus_tube_fixed_properties, dbus_tube_allowed_properties, receive_presence_and_ask_caps,\
    caps_contain, ft_fixed_properties, ft_allowed_properties, get_contacts_capabilities_sync
import ns

specialized_tube_allowed_properties = dbus.Array([cs.TARGET_HANDLE,
    cs.TARGET_ID])

bidir_daap_fixed_properties = dbus.Dictionary({
    cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
    cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_STREAM_TUBE,
    cs.STREAM_TUBE_SERVICE: 'daap'
    })
outgoing_daap_fixed_properties = dbus.Dictionary({
    cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
    cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_STREAM_TUBE,
    cs.REQUESTED : True,
    cs.STREAM_TUBE_SERVICE: 'daap'
    })
incoming_daap_fixed_properties = dbus.Dictionary({
    cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
    cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_STREAM_TUBE,
    cs.REQUESTED : False,
    cs.STREAM_TUBE_SERVICE: 'daap'
    })
http_fixed_properties = dbus.Dictionary({
    cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
    cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_STREAM_TUBE,
    cs.STREAM_TUBE_SERVICE: 'http'
    })

xiangqi_fixed_properties = dbus.Dictionary({
    cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
    cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_DBUS_TUBE,
    cs.DBUS_TUBE_SERVICE_NAME: 'com.example.Xiangqi'
    })

go_fixed_properties = dbus.Dictionary({
    cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
    cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_DBUS_TUBE,
    cs.DBUS_TUBE_SERVICE_NAME: 'com.example.Go'
    })

client = 'http://telepathy.freedesktop.org/fake-client'

from servicetest import sort_d
def assertSameElements(a, b):
    assertEquals(sort_d(a), sort_d(b))

def receive_caps(q, conn, stream, contact, contact_handle, features,
                 expected_caps, expect_disco=True, expect_ccc=True):
    presence = make_presence(contact, status='hello')
    c = presence.addElement((ns.CAPS, 'c'))
    c['node'] = client
    c['ver'] = compute_caps_hash([],
        features if features is not None else [],
        {})
    c['hash'] = 'sha-1'
    stream.send(presence)

    if expect_disco:
        # Gabble looks up our capabilities
        event = q.expect('stream-iq', to=contact, query_ns=ns.DISCO_INFO)
        query_node = xpath.queryForNodes('/iq/query', event.stanza)[0]
        assert query_node.attributes['node'] == \
            client + '#' + c['ver']

        # send good reply
        result = make_result_iq(stream, event.stanza)
        query = result.firstChildElement()
        query['node'] = client + '#' + c['ver']

        for f in features:
            feature = query.addElement('feature')
            feature['var'] = f

        stream.send(result)

    if expect_ccc:
        event = q.expect('dbus-signal', signal='ContactCapabilitiesChanged')
        announced_ccs, = event.args
        assertSameElements(expected_caps, announced_ccs)
    else:
        # Make sure Gabble's got the caps
        sync_stream(q, stream)

    caps = get_contacts_capabilities_sync(conn, [contact_handle])
    assertSameElements(expected_caps, caps)

    # test again, to check GetContactCapabilities does not have side effect
    caps = get_contacts_capabilities_sync(conn, [contact_handle])
    assertSameElements(expected_caps, caps)

    # check the Contacts interface give the same caps
    caps_via_contacts_iface = conn.Contacts.GetContactAttributes(
            [contact_handle], [cs.CONN_IFACE_CONTACT_CAPS], False) \
            [contact_handle][cs.ATTR_CONTACT_CAPABILITIES]
    assertSameElements(caps[contact_handle], caps_via_contacts_iface)

def test_tube_caps_from_contact(q, bus, conn, stream, contact):
    contact_handle = conn.get_contact_handle_sync(contact)

    # Check that we don't crash if we haven't seen any caps/presence for this
    # contact yet.
    caps = get_contacts_capabilities_sync(conn, [contact_handle])

    basic_caps = dbus.Dictionary({contact_handle:
            [(text_fixed_properties, text_allowed_properties)]})

    # Since we don't know their caps, they should be omitted from the dict,
    # rather than present with no caps, but all contacts have text chat caps.
    assertEquals(basic_caps, caps)

    # send presence with no tube cap
    # We don't expect ContactCapabilitiesChanged to be emitted here: we always
    # assume people can do text channels.
    receive_caps(q, conn, stream, contact, contact_handle, [], basic_caps,
        expect_ccc=False)

    # send presence with generic tubes caps
    generic_tubes_caps = dbus.Dictionary({contact_handle:
            [(text_fixed_properties, text_allowed_properties),
             (stream_tube_fixed_properties, stream_tube_allowed_properties),
             (dbus_tube_fixed_properties, dbus_tube_allowed_properties)]})
    receive_caps(q, conn, stream, contact, contact_handle, [ns.TUBES],
        generic_tubes_caps)

    # send presence with 1 stream tube cap
    daap_caps = dbus.Dictionary({contact_handle:
            [(text_fixed_properties, text_allowed_properties),
             (stream_tube_fixed_properties, stream_tube_allowed_properties),
             (dbus_tube_fixed_properties, dbus_tube_allowed_properties),
             (incoming_daap_fixed_properties, specialized_tube_allowed_properties)]})
    receive_caps(q, conn, stream, contact, contact_handle,
        [ns.TUBES + '/stream#daap'], daap_caps)

    # send presence with 1 D-Bus tube cap
    xiangqi_caps = dbus.Dictionary({contact_handle:
            [(text_fixed_properties, text_allowed_properties),
             (stream_tube_fixed_properties, stream_tube_allowed_properties),
             (dbus_tube_fixed_properties, dbus_tube_allowed_properties),
            (xiangqi_fixed_properties, specialized_tube_allowed_properties)]})
    receive_caps(q, conn, stream, contact, contact_handle,
        [ns.TUBES + '/dbus#com.example.Xiangqi'], xiangqi_caps)

    # send presence with both D-Bus and stream tube caps
    daap_xiangqi_caps = dbus.Dictionary({contact_handle:
        [(text_fixed_properties, text_allowed_properties),
        (stream_tube_fixed_properties, stream_tube_allowed_properties),
        (dbus_tube_fixed_properties, dbus_tube_allowed_properties),
        (incoming_daap_fixed_properties, specialized_tube_allowed_properties),
        (xiangqi_fixed_properties, specialized_tube_allowed_properties)]})
    receive_caps(q, conn, stream, contact, contact_handle,
        [ns.TUBES + '/dbus#com.example.Xiangqi',
         ns.TUBES + '/stream#daap',
        ], daap_xiangqi_caps)

    # send presence with 4 tube caps
    all_tubes_caps = dbus.Dictionary({contact_handle:
        [(text_fixed_properties, text_allowed_properties),
        (stream_tube_fixed_properties, stream_tube_allowed_properties),
        (dbus_tube_fixed_properties, dbus_tube_allowed_properties),
        (incoming_daap_fixed_properties, specialized_tube_allowed_properties),
        (http_fixed_properties, specialized_tube_allowed_properties),
        (xiangqi_fixed_properties, specialized_tube_allowed_properties),
        (go_fixed_properties, specialized_tube_allowed_properties)]})
    receive_caps(q, conn, stream, contact, contact_handle,
        [ns.TUBES + '/dbus#com.example.Xiangqi',
         ns.TUBES + '/dbus#com.example.Go',
         ns.TUBES + '/stream#daap',
         ns.TUBES + '/stream#http',
        ], all_tubes_caps)

    # send presence with both D-Bus and stream tube caps
    # Gabble does not look up our capabilities because of the cache
    receive_caps(q, conn, stream, contact, contact_handle,
        [ns.TUBES + '/dbus#com.example.Xiangqi',
         ns.TUBES + '/stream#daap',
        ], daap_xiangqi_caps, expect_disco=False)


def advertise_caps(q, conn, stream, filters, expected_features, unexpected_features,
                   expected_caps):
    self_handle = conn.Properties.Get(cs.CONN, "SelfHandle")
    ret_caps = conn.ContactCapabilities.UpdateCapabilities(
            [(cs.CLIENT + '.Foo', filters, [])])

    # Expect Gabble to reply with the correct caps
    event, namespaces, _, signaled_caps = receive_presence_and_ask_caps(q, stream)

    assertSameElements(expected_caps, signaled_caps)

    assertContains(ns.TUBES, namespaces)

    for var in expected_features:
        assertContains(var, namespaces)

    for var in unexpected_features:
        assertDoesNotContain(var, namespaces)

    # Check our own caps
    caps = get_contacts_capabilities_sync(conn, [self_handle])
    assertSameElements(expected_caps, caps)

    # check the Contacts interface give the same caps
    caps_via_contacts_iface = conn.Contacts.GetContactAttributes(
            [self_handle], [cs.CONN_IFACE_CONTACT_CAPS], False) \
            [self_handle][cs.ATTR_CONTACT_CAPABILITIES]
    assertSameElements(caps[self_handle], caps_via_contacts_iface)

def test_tube_caps_to_contact(q, bus, conn, stream):
    self_handle = conn.Properties.Get(cs.CONN, "SelfHandle")

    basic_caps = dbus.Dictionary({self_handle:
        [(text_fixed_properties, text_allowed_properties),
         (stream_tube_fixed_properties, stream_tube_allowed_properties),
         (dbus_tube_fixed_properties, dbus_tube_allowed_properties),
         ]})
    daap_caps = dbus.Dictionary({self_handle:
        [(text_fixed_properties, text_allowed_properties),
        (stream_tube_fixed_properties, stream_tube_allowed_properties),
        (dbus_tube_fixed_properties, dbus_tube_allowed_properties),
        (incoming_daap_fixed_properties, specialized_tube_allowed_properties),
        (ft_fixed_properties, ft_allowed_properties)]})
    xiangqi_caps = dbus.Dictionary({self_handle:
        [(text_fixed_properties, text_allowed_properties),
        (stream_tube_fixed_properties, stream_tube_allowed_properties),
        (dbus_tube_fixed_properties, dbus_tube_allowed_properties),
        (xiangqi_fixed_properties, specialized_tube_allowed_properties),
        (ft_fixed_properties, ft_allowed_properties)]})
    daap_xiangqi_caps = dbus.Dictionary({self_handle:
        [(text_fixed_properties, text_allowed_properties),
        (stream_tube_fixed_properties, stream_tube_allowed_properties),
        (dbus_tube_fixed_properties, dbus_tube_allowed_properties),
        (incoming_daap_fixed_properties, specialized_tube_allowed_properties),
        (xiangqi_fixed_properties, specialized_tube_allowed_properties),
        (ft_fixed_properties, ft_allowed_properties)]})
    all_tubes_caps = dbus.Dictionary({self_handle:
        [(text_fixed_properties, text_allowed_properties),
        (stream_tube_fixed_properties, stream_tube_allowed_properties),
        (dbus_tube_fixed_properties, dbus_tube_allowed_properties),
        (incoming_daap_fixed_properties, specialized_tube_allowed_properties),
        (http_fixed_properties, specialized_tube_allowed_properties),
        (xiangqi_fixed_properties, specialized_tube_allowed_properties),
        (go_fixed_properties, specialized_tube_allowed_properties),
        (ft_fixed_properties, ft_allowed_properties)]})

    # Check our own caps
    caps = get_contacts_capabilities_sync(conn, [self_handle])
    assertEquals(basic_caps, caps)

    # check the Contacts interface give the same caps
    caps_via_contacts_iface = conn.Contacts.GetContactAttributes(
            [self_handle], [cs.CONN_IFACE_CONTACT_CAPS], False) \
            [self_handle][cs.ATTR_CONTACT_CAPABILITIES]
    assertEquals(caps[self_handle], caps_via_contacts_iface)

    # Advertise nothing
    conn.ContactCapabilities.UpdateCapabilities(
            [(cs.CLIENT + '.Foo', {}, [])])

    # Check our own caps
    caps = get_contacts_capabilities_sync(conn, [self_handle])
    assertLength(1, caps)
    assertEquals(basic_caps, caps)

    # check the Contacts interface give the same caps
    caps_via_contacts_iface = conn.Contacts.GetContactAttributes(
            [self_handle], [cs.CONN_IFACE_CONTACT_CAPS], False) \
            [self_handle][cs.ATTR_CONTACT_CAPABILITIES]
    assertEquals(caps[self_handle], caps_via_contacts_iface)

    sync_stream(q, stream)

    # Advertise a Requested=True tube cap
    conn.ContactCapabilities.UpdateCapabilities(
            [(cs.CLIENT + '.Foo', [outgoing_daap_fixed_properties], [])])

    # Check our own caps
    caps = get_contacts_capabilities_sync(conn, [self_handle])
    assertLength(1, caps)
    assertEquals(basic_caps, caps)

    # check the Contacts interface give the same caps
    caps_via_contacts_iface = conn.Contacts.GetContactAttributes(
            [self_handle], [cs.CONN_IFACE_CONTACT_CAPS], False) \
            [self_handle][cs.ATTR_CONTACT_CAPABILITIES]
    assertEquals(caps[self_handle], caps_via_contacts_iface)

    sync_stream(q, stream)

    advertise_caps(q, conn, stream,
        [bidir_daap_fixed_properties],
        [ns.TUBES + '/stream#daap'],
        [ns.TUBES + '/stream#http',
         ns.TUBES + '/dbus#com.example.Go',
         ns.TUBES + '/dbus#com.example.Xiangqi',
        ],
        daap_caps)

    advertise_caps(q, conn, stream,
        [outgoing_daap_fixed_properties, xiangqi_fixed_properties],
        [ns.TUBES + '/dbus#com.example.Xiangqi'],
        [ns.TUBES + '/stream#daap',
         ns.TUBES + '/stream#http',
         ns.TUBES + '/dbus#com.example.Go',
        ],
        xiangqi_caps)

    advertise_caps(q, conn, stream,
        [incoming_daap_fixed_properties, outgoing_daap_fixed_properties,
         xiangqi_fixed_properties],
        [ns.TUBES + '/dbus#com.example.Xiangqi',
         ns.TUBES + '/stream#daap',
        ],
        [ns.TUBES + '/stream#http',
         ns.TUBES + '/dbus#com.example.Go',
        ],
        daap_xiangqi_caps)

    advertise_caps(q, conn, stream,
        [incoming_daap_fixed_properties, http_fixed_properties,
         go_fixed_properties, xiangqi_fixed_properties],
        [ns.TUBES + '/dbus#com.example.Xiangqi',
         ns.TUBES + '/stream#daap',
         ns.TUBES + '/stream#http',
         ns.TUBES + '/dbus#com.example.Go',
        ],
        [],
        all_tubes_caps)

    # test daap + xiangqi again for some reason
    advertise_caps(q, conn, stream,
        [incoming_daap_fixed_properties, xiangqi_fixed_properties],
        [ns.TUBES + '/dbus#com.example.Xiangqi',
         ns.TUBES + '/stream#daap',
        ],
        [ns.TUBES + '/stream#http',
         ns.TUBES + '/dbus#com.example.Go',
        ],
        daap_xiangqi_caps)

def test(q, bus, conn, stream):
    test_tube_caps_from_contact(q, bus, conn, stream, 'bilbo1@foo.com/Foo')

    test_tube_caps_to_contact(q, bus, conn, stream)

if __name__ == '__main__':
    exec_test(test)
