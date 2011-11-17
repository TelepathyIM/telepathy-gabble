"""
Test Gabble's different addressing interfaces.
"""

import dbus
from servicetest import unwrap, tp_path_prefix, assertEquals, ProxyWrapper, \
    assertContains, assertSameSets, assertDoesNotContain
from gabbletest import exec_test, call_async
import constants as cs
import ns
import time

def test_protocol(q, bus, conn, stream):
    proto = ProxyWrapper(
        bus.get_object(cs.CM + '.gabble',
                       tp_path_prefix + '/ConnectionManager/gabble/jabber'),
        cs.PROTOCOL, {"Addressing" : cs.PROTOCOL_IFACE_ADDRESSING})

    # AddressableVCardFields and AddressableURISchemes

    addr_props = proto.Properties.GetAll(cs.PROTOCOL_IFACE_ADDRESSING)

    assertEquals(["x-jabber"], addr_props["AddressableVCardFields"])

    assertEquals(["xmpp"], addr_props["AddressableURISchemes"])

    # NormalizeVCardAddress

    normalized_address = proto.Addressing.NormalizeVCardAddress(
        "X-JABBER", "eitan@EXAMPLE.com/somewhere")

    assertEquals("eitan@example.com", normalized_address)

    call_async(q, proto.Addressing, "NormalizeVCardAddress",
               "X-WEIRD-FIELD", "eitan@example.com")

    q.expect('dbus-error', method="NormalizeVCardAddress",
             name=cs.NOT_IMPLEMENTED)

    call_async(q, proto.Addressing, "NormalizeVCardAddress",
               "X-JABBER", "eitan!example.com")

    q.expect('dbus-error', method="NormalizeVCardAddress",
             name=cs.INVALID_ARGUMENT)

    # NormalizeContactURI

    normalized_uri = proto.Addressing.NormalizeContactURI(
        "xmpp:EITAN?@example.COM/resource")

    assertEquals("xmpp:eitan%3F@example.com", normalized_uri)

    normalized_uri = proto.Addressing.NormalizeContactURI(
        "xmpp:EITAN?@example.COM/resourc?e")

    assertEquals("xmpp:eitan%3F@example.com", normalized_uri)

    call_async(q, proto.Addressing, "NormalizeContactURI",
               "Something that is far from a URI")

    q.expect('dbus-error', method="NormalizeContactURI",
             name=cs.INVALID_ARGUMENT)

    call_async(q, proto.Addressing, "NormalizeContactURI",
               "http://this@isnotawebbrowser")

    q.expect('dbus-error', method="NormalizeContactURI",
             name=cs.NOT_IMPLEMENTED)

    call_async(q, proto.Addressing, "NormalizeContactURI",
               "xmpp:something")

    q.expect('dbus-error', method="NormalizeContactURI",
             name=cs.INVALID_ARGUMENT)

def test_connection(q, bus, conn, stream):
    event = q.expect('stream-iq', query_ns=ns.ROSTER)
    event.stanza['type'] = 'result'

    normalized_buddies = ['amy@foo.com', 'bob@foo.com', 'che@foo.com']
    buddies = ['AMY@foo.com', 'bob@FOO.com', 'che@foo.com/resource']

    for buddy in normalized_buddies:
        item = event.query.addElement('item')
        item['jid'] = buddy
        item['subscription'] = 'both'

    stream.send(event.stanza)

    requested, attributes = conn.Addressing.GetContactsByVCardField(
        "X-JABBER", buddies[:2] + ['bad!jid'] + buddies[2:], [])

    addresses = []

    assertEquals(3, len(attributes))
    assertEquals(3, len(requested))

    for attr in attributes.values():
        assertContains(cs.CONN_IFACE_ADDRESSING + '/addresses', attr.keys())
        assertContains('x-jabber', attr[cs.CONN_IFACE_ADDRESSING + '/addresses'].keys())
        addresses.append(attr[cs.CONN_IFACE_ADDRESSING + '/addresses']['x-jabber'])

    assertSameSets(normalized_buddies, addresses)
    assertSameSets(buddies, requested.keys());

    normalized_buddies = ['amy%3F@foo.com', 'bob@foo.com', 'che@foo.com']
    buddies = ['AMY?@foo.com', 'bob@FOO.com', 'che@foo.com/resource']

    normalized_schemes = ["xmpp", "xmpp", "http"]
    schemes = ["xmpp", "XMPP", "http"]
    valid_schemes = ["xmpp", "XMPP"]

    request_uris = [a + ":" + b for a, b in zip(schemes, buddies)]
    valid_request_uris = [a + ":" + b for a, b in zip(valid_schemes, buddies)]
    normalized_request_uris = [a + ":" + b for a, b in zip(normalized_schemes, normalized_buddies)]

    requested, attributes = conn.Addressing.GetContactsByURI(request_uris, [])

    assertEquals(2, len(attributes))
    assertEquals(2, len(requested))

    for attr in attributes.values():
        assertContains(attr[cs.CONN_IFACE_ADDRESSING + '/uris'][0], normalized_request_uris)
        assertContains(cs.CONN_IFACE_ADDRESSING + '/uris', attr.keys())

    assertSameSets(valid_request_uris, requested.keys())

if __name__ == '__main__':
    exec_test(test_protocol)
    exec_test(test_connection)
