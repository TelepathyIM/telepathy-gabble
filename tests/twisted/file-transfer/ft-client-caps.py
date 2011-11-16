
"""
Test FT capabilities with Connection.Interface.ContactCapabilities

1. Receive presence and caps from contacts and check that
GetContactCapabilities works correctly and that ContactCapabilitiesChanged is
correctly received. Also check that GetContactAttributes gives the same
results.

- no FT cap at all
- FT caps without metadata extension
- FT caps with metadata extension
- 1 FT cap with a service name
- 2 FT caps with service names
- 1 FT cap again, to test whether the caps cache works with FT services

2. Test UpdateCapabilities and test that a presence stanza is sent to the
contacts, test that the D-Bus signal ContactCapabilitiesChanged is fired for
the self handle, ask Gabble for its caps with an iq request, check the reply
is correct, and ask Gabble for its caps using D-Bus method
GetContactCapabilities. Also check that GetContactAttributes gives the same
results.

Ensure that just a Requested=True channel class in a client filter doesn't
make a FT service advertised as a cap.

- no FT cap at all
- 1 FT cap with no service name
- 1 Requested=True FT cap with service name
- 1 FT cap with service name
- 1 FT cap with service name + 1 FT cap with no service name
- 2 FT caps with service names
- 1 FT cap with service name again, just for fun

"""

import dbus

from twisted.words.xish import xpath

from servicetest import assertEquals, assertLength, assertContains,\
        assertDoesNotContain, sync_dbus
from gabbletest import exec_test, make_result_iq, sync_stream, make_presence
import constants as cs

from caps_helper import compute_caps_hash, text_fixed_properties,\
    text_allowed_properties, stream_tube_fixed_properties, stream_tube_allowed_properties,\
    dbus_tube_fixed_properties, dbus_tube_allowed_properties, receive_presence_and_ask_caps,\
    caps_contain, ft_fixed_properties, ft_allowed_properties, ft_allowed_properties_with_metadata, \
    presence_and_disco
import ns

def dict_union(a, b):
    return dbus.Dictionary(a.items() + b.items(), signature='sv')

no_service_fixed_properties = {
    cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
    cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_FILE_TRANSFER,
    }
bidir_daap_fixed_properties = dict_union(no_service_fixed_properties, {
        cs.FT_SERVICE_NAME: 'daap'
        })
outgoing_daap_fixed_properties = dict_union(bidir_daap_fixed_properties, {
        cs.REQUESTED : True,
        })
incoming_daap_fixed_properties = dict_union(bidir_daap_fixed_properties, {
        cs.REQUESTED : False,
        })
http_fixed_properties = dict_union(no_service_fixed_properties, {
        cs.FT_SERVICE_NAME: 'http',
        })
xiangqi_fixed_properties = dict_union(no_service_fixed_properties, {
        cs.FT_SERVICE_NAME: 'com.example.Xiangqi',
        })
go_fixed_properties = dict_union(no_service_fixed_properties, {
        cs.FT_SERVICE_NAME: 'com.example.Go',
        })

client = 'http://telepathy.freedesktop.org/another-fake-client'

def assertSameElements(a, b):
    assertEquals(sorted(a), sorted(b))

def receive_caps(q, conn, stream, contact, contact_handle, features,
                 expected_caps, expect_disco=True, expect_ccc=True):

    caps = {'node': client,
            'ver': compute_caps_hash([], features, {}),
            'hash': 'sha-1'}

    presence_and_disco(q, conn, stream, contact, expect_disco,
                       client, caps, features, initial=False)

    if expect_ccc:
        event = q.expect('dbus-signal', signal='ContactCapabilitiesChanged')
        announced_ccs, = event.args
        assertSameElements(expected_caps, announced_ccs[contact_handle])
    else:
        # Make sure Gabble's got the caps
        sync_stream(q, stream)

    caps = conn.ContactCapabilities.GetContactCapabilities([contact_handle])
    assertSameElements(expected_caps, caps[contact_handle])

    # test again, to check GetContactCapabilities does not have side effect
    caps = conn.ContactCapabilities.GetContactCapabilities([contact_handle])
    assertSameElements(expected_caps, caps[contact_handle])

    # check the Contacts interface give the same caps
    caps_via_contacts_iface = conn.Contacts.GetContactAttributes(
            [contact_handle], [cs.CONN_IFACE_CONTACT_CAPS], False) \
            [contact_handle][cs.ATTR_CONTACT_CAPABILITIES]
    assertSameElements(caps[contact_handle], caps_via_contacts_iface)

def test_ft_caps_from_contact(q, bus, conn, stream, contact):
    contact_handle = conn.RequestHandles(cs.HT_CONTACT, [contact])[0]

    # Check that we don't crash if we haven't seen any caps/presence for this
    # contact yet.
    caps = conn.ContactCapabilities.GetContactCapabilities([contact_handle])

    basic_caps = [(text_fixed_properties, text_allowed_properties)]

    # Since we don't know their caps, they should be omitted from the dict,
    # rather than present with no caps, but all contacts have text chat caps.
    assertEquals(basic_caps, caps[contact_handle])

    # send presence with no FT cap
    # We don't expect ContactCapabilitiesChanged to be emitted here: we always
    # assume people can do text channels.
    receive_caps(q, conn, stream, contact, contact_handle, [], basic_caps,
        expect_ccc=False)

    # send presence with no mention of metadata
    no_metadata_ft_caps = [
        (text_fixed_properties, text_allowed_properties),
        (ft_fixed_properties, ft_allowed_properties)
        ]
    receive_caps(q, conn, stream, contact, contact_handle,
        [ns.FILE_TRANSFER], no_metadata_ft_caps)

    # send presence with generic FT caps including metadata from now on
    generic_ft_caps = [
        (text_fixed_properties, text_allowed_properties),
        (ft_fixed_properties, ft_allowed_properties_with_metadata)
        ]
    generic_ft_features = [ns.FILE_TRANSFER, ns.TP_FT_METADATA]
    receive_caps(q, conn, stream, contact, contact_handle,
        generic_ft_features, generic_ft_caps)

    # send presence with 1 FT cap with a service
    daap_caps = generic_ft_caps + [
        (bidir_daap_fixed_properties, ft_allowed_properties + [cs.FT_METADATA])]
    receive_caps(q, conn, stream, contact, contact_handle,
        generic_ft_features + [ns.TP_FT_METADATA + '#daap'], daap_caps)

    # send presence with 2 FT caps
    daap_xiangqi_caps = daap_caps + [
        (xiangqi_fixed_properties, ft_allowed_properties + [cs.FT_METADATA])]
    receive_caps(q, conn, stream, contact, contact_handle,
        generic_ft_features + [ns.TP_FT_METADATA + '#com.example.Xiangqi',
         ns.TP_FT_METADATA + '#daap',
        ], daap_xiangqi_caps)

    # send presence with 1 FT cap again
    # Gabble does not look up our capabilities because of the cache
    receive_caps(q, conn, stream, contact, contact_handle,
        generic_ft_features + [ns.TP_FT_METADATA + '#daap'], daap_caps,
        expect_disco=False)

def advertise_caps(q, bus, conn, stream, filters, expected_features, unexpected_features,
                   expected_caps):
    # make sure nothing from a previous update is still running
    sync_dbus(bus, q, conn)

    self_handle = conn.GetSelfHandle()
    ret_caps = conn.ContactCapabilities.UpdateCapabilities(
            [(cs.CLIENT + '.Foo', filters, [])])

    # Expect Gabble to reply with the correct caps
    event, namespaces, _, signaled_caps = receive_presence_and_ask_caps(q, stream)

    assertSameElements(expected_caps, signaled_caps[self_handle])

    assertContains(ns.TP_FT_METADATA, namespaces)

    for var in expected_features:
        assertContains(var, namespaces)

    for var in unexpected_features:
        assertDoesNotContain(var, namespaces)

    # Check our own caps
    caps = conn.ContactCapabilities.GetContactCapabilities([self_handle])
    assertSameElements(expected_caps, caps[self_handle])

    # check the Contacts interface give the same caps
    caps_via_contacts_iface = conn.Contacts.GetContactAttributes(
            [self_handle], [cs.CONN_IFACE_CONTACT_CAPS], False) \
            [self_handle][cs.ATTR_CONTACT_CAPABILITIES]
    assertSameElements(caps[self_handle], caps_via_contacts_iface)

def test_ft_caps_to_contact(q, bus, conn, stream):
    self_handle = conn.GetSelfHandle()

    basic_caps = [
        (text_fixed_properties, text_allowed_properties),
        (stream_tube_fixed_properties, stream_tube_allowed_properties),
        (dbus_tube_fixed_properties, dbus_tube_allowed_properties),
        ]
    simple_ft_caps = basic_caps + [
        (ft_fixed_properties, ft_allowed_properties_with_metadata),
        ]
    daap_caps = simple_ft_caps + [
        (bidir_daap_fixed_properties, ft_allowed_properties + [cs.FT_METADATA]),
        ]
    xiangqi_caps = simple_ft_caps + [
        (xiangqi_fixed_properties, ft_allowed_properties + [cs.FT_METADATA]),
        ]
    xiangqi_go_caps = xiangqi_caps + [
        (go_fixed_properties, ft_allowed_properties + [cs.FT_METADATA]),
        ]
    go_caps = simple_ft_caps + [
        (go_fixed_properties, ft_allowed_properties + [cs.FT_METADATA]),
        ]

    #
    # Check our own caps; we should have no FT caps
    #
    caps = conn.ContactCapabilities.GetContactCapabilities([self_handle])
    assertEquals(basic_caps, caps[self_handle])

    # check the Contacts interface give the same caps
    caps_via_contacts_iface = conn.Contacts.GetContactAttributes(
            [self_handle], [cs.CONN_IFACE_CONTACT_CAPS], False) \
            [self_handle][cs.ATTR_CONTACT_CAPABILITIES]
    assertEquals(caps[self_handle], caps_via_contacts_iface)

    #
    # Advertise nothing
    #
    conn.ContactCapabilities.UpdateCapabilities(
            [(cs.CLIENT + '.Foo', {}, [])])

    # Check our own caps
    caps = conn.ContactCapabilities.GetContactCapabilities([self_handle])
    assertLength(1, caps)
    assertEquals(basic_caps, caps[self_handle])

    # check the Contacts interface give the same caps
    caps_via_contacts_iface = conn.Contacts.GetContactAttributes(
            [self_handle], [cs.CONN_IFACE_CONTACT_CAPS], False) \
            [self_handle][cs.ATTR_CONTACT_CAPABILITIES]
    assertEquals(caps[self_handle], caps_via_contacts_iface)

    sync_stream(q, stream)

    #
    # Advertise FT but with no service name
    #
    conn.ContactCapabilities.UpdateCapabilities(
            [(cs.CLIENT + '.Foo', [no_service_fixed_properties], [])])

    # Check our own caps
    caps = conn.ContactCapabilities.GetContactCapabilities([self_handle])
    assertLength(1, caps)
    assertEquals(simple_ft_caps, caps[self_handle])

    # check the Contacts interface give the same caps
    caps_via_contacts_iface = conn.Contacts.GetContactAttributes(
            [self_handle], [cs.CONN_IFACE_CONTACT_CAPS], False) \
            [self_handle][cs.ATTR_CONTACT_CAPABILITIES]
    assertEquals(caps[self_handle], caps_via_contacts_iface)

    sync_stream(q, stream)

    #
    # Advertise a Requested=True FT cap
    #
    conn.ContactCapabilities.UpdateCapabilities(
            [(cs.CLIENT + '.Foo', [outgoing_daap_fixed_properties], [])])

    # Check our own caps
    caps = conn.ContactCapabilities.GetContactCapabilities([self_handle])
    assertLength(1, caps)
    assertEquals(simple_ft_caps, caps[self_handle])

    # check the Contacts interface give the same caps
    caps_via_contacts_iface = conn.Contacts.GetContactAttributes(
            [self_handle], [cs.CONN_IFACE_CONTACT_CAPS], False) \
            [self_handle][cs.ATTR_CONTACT_CAPABILITIES]
    assertEquals(caps[self_handle], caps_via_contacts_iface)

    advertise_caps(q, bus, conn, stream,
        [bidir_daap_fixed_properties],
        [ns.TP_FT_METADATA + '#daap'],
        [ns.TP_FT_METADATA + '#http',
         ns.TP_FT_METADATA + '#com.example.Go',
         ns.TP_FT_METADATA + '#com.example.Xiangqi',
        ],
        daap_caps)

    advertise_caps(q, bus, conn, stream,
        [xiangqi_fixed_properties, no_service_fixed_properties],
        [ns.TP_FT_METADATA + '#com.example.Xiangqi'],
        [ns.TP_FT_METADATA + '#daap',
         ns.TP_FT_METADATA + '#http',
         ns.TP_FT_METADATA + '#com.example.Go',
        ],
        xiangqi_caps)

    advertise_caps(q, bus, conn, stream,
        [xiangqi_fixed_properties, go_fixed_properties],
        [ns.TP_FT_METADATA + '#com.example.Xiangqi',
         ns.TP_FT_METADATA + '#com.example.Go',
        ],
        [ns.TP_FT_METADATA + '#http',
         ns.TP_FT_METADATA + '#daap',
        ],
        xiangqi_go_caps)

    advertise_caps(q, bus, conn, stream,
        [go_fixed_properties],
        [ns.TP_FT_METADATA + '#com.example.Go',
        ],
        [ns.TP_FT_METADATA + '#http',
         ns.TP_FT_METADATA + '#daap',
         ns.TP_FT_METADATA + '#com.example.Xiangqi',
        ],
        go_caps)

def test(q, bus, conn, stream):
    test_ft_caps_from_contact(q, bus, conn, stream, 'bilbo1@foo.com/Foo')

    test_ft_caps_to_contact(q, bus, conn, stream)

if __name__ == '__main__':
    exec_test(test)
