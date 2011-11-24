"""
Test Gabble's different addressing interfaces.
"""

import dbus
from servicetest import unwrap, tp_path_prefix, assertEquals, ProxyWrapper
from gabbletest import exec_test, call_async
import constants as cs
import time

def test(q, bus, conn, stream):
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

if __name__ == '__main__':
    exec_test(test)
