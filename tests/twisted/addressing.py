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
        "xmpp:EITAN@example.COM/resource")

    assertEquals("xmpp:eitan@example.com", normalized_uri)

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
    conn.Connect()

    event = q.expect('stream-iq', query_ns=ns.ROSTER)
    event.stanza['type'] = 'result'

    buddies = ['amy@foo.com', 'bob@foo.com', 'che@foo.com']
    not_normal_buddies = ['AMY@foo.com', 'bob@FOO.com', 'che@foo.com/resource']

    for buddy in buddies:
        item = event.query.addElement('item')
        item['jid'] = buddy
        item['subscription'] = 'both'

    stream.send(event.stanza)

    contacts = conn.Addressing.GetContactsByVCardField(
        "X-JABBER", not_normal_buddies[:2] + ['bad!jid'] + not_normal_buddies[2:], [])

    req_addresses = []
    addresses = []

    for c in contacts.values():
        assertContains(cs.CONN_IFACE_ADDRESSING + '/requested-address', c.keys())
        req_addresses.append(c[cs.CONN_IFACE_ADDRESSING + '/requested-address'][1])
        assertEquals("X-JABBER", c[cs.CONN_IFACE_ADDRESSING + '/requested-address'][0])
        assertContains(cs.CONN_IFACE_ADDRESSING + '/addresses', c.keys())
        assertContains('x-jabber', c[cs.CONN_IFACE_ADDRESSING + '/addresses'].keys())
        addresses.append(c[cs.CONN_IFACE_ADDRESSING + '/addresses']['x-jabber'])

    assertSameSets(buddies, addresses)
    assertSameSets(not_normal_buddies, req_addresses)

    schemes = ["xmpp", "XMPP", "http"]

    get_uris = [a + ":" + b for a,b in zip(schemes, not_normal_buddies)]

    contacts = conn.Addressing.GetContactsByURI(get_uris, [])

    # Only two of the schemes we provided are valid.
    assertEquals(2, len(contacts))

    for c in contacts.values():
        assertContains(cs.CONN_IFACE_ADDRESSING + '/requested-uri', c.keys())
        req_uri = c[cs.CONN_IFACE_ADDRESSING + '/requested-uri']
        assertDoesNotContain("http", req_uri)
        assertContains(cs.CONN_IFACE_ADDRESSING + '/uris', c.keys())
        assertContains(req_uri.lower(), c[cs.CONN_IFACE_ADDRESSING + '/uris'])

if __name__ == '__main__':
    exec_test(test_protocol)
    exec_test(test_connection)
