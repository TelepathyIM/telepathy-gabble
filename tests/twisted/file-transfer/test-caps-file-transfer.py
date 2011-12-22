import dbus

from twisted.words.xish import xpath

from servicetest import assertEquals
from gabbletest import exec_test, make_result_iq, sync_stream, make_presence
import constants as cs

from caps_helper import compute_caps_hash, text_fixed_properties,\
    text_allowed_properties, stream_tube_fixed_properties,\
    stream_tube_allowed_properties, dbus_tube_fixed_properties,\
    dbus_tube_allowed_properties, ft_fixed_properties, ft_allowed_properties

import ns

from config import FILE_TRANSFER_ENABLED

if not FILE_TRANSFER_ENABLED:
    print "NOTE: built with --disable-file-transfer"
    raise SystemExit(77)

def test_ft_caps_from_contact(q, bus, conn, stream, contact, contact_handle, client):

    conn_caps_iface = dbus.Interface(conn, cs.CONN_IFACE_CONTACT_CAPS)
    conn_contacts_iface = dbus.Interface(conn, cs.CONN_IFACE_CONTACTS)

    # send presence with no FT cap
    presence = make_presence(contact, status='hello')
    c = presence.addElement((ns.CAPS, 'c'))
    c['node'] = client
    c['ver'] = compute_caps_hash([], [], {})
    c['hash'] = 'sha-1'
    stream.send(presence)

    # Gabble looks up our capabilities
    event = q.expect('stream-iq', to=contact, query_ns=ns.DISCO_INFO)
    query_node = xpath.queryForNodes('/iq/query', event.stanza)[0]
    assert query_node.attributes['node'] == \
        client + '#' + c['ver']

    # send good reply
    result = make_result_iq(stream, event.stanza)
    query = result.firstChildElement()
    query['node'] = client + '#' + c['ver']
    stream.send(result)

    # no change in ContactCapabilities, so no signal ContactCapabilitiesChanged
    sync_stream(q, stream)

    # no special capabilities
    basic_caps = dbus.Dictionary({contact_handle:
            [(text_fixed_properties, text_allowed_properties)]})
    caps = conn_caps_iface.GetContactCapabilities([contact_handle])
    assert caps == basic_caps, caps
    # test again, to check GetContactCapabilities does not have side effect
    caps = conn_caps_iface.GetContactCapabilities([contact_handle])
    assert caps == basic_caps, caps
    # check the Contacts interface give the same caps
    caps_via_contacts_iface = conn_contacts_iface.GetContactAttributes(
            [contact_handle], [cs.CONN_IFACE_CONTACT_CAPS], False) \
            [contact_handle][cs.ATTR_CONTACT_CAPABILITIES]
    assert caps_via_contacts_iface == caps[contact_handle], \
                                    caps_via_contacts_iface

    # send presence with ft capa
    presence = make_presence(contact, status='hello')
    c = presence.addElement((ns.CAPS, 'c'))
    c['node'] = client
    c['ver'] = compute_caps_hash([], [ns.FILE_TRANSFER], {})
    c['hash'] = 'sha-1'
    stream.send(presence)

    # Gabble looks up our capabilities
    event = q.expect('stream-iq', to=contact, query_ns=ns.DISCO_INFO)
    query_node = xpath.queryForNodes('/iq/query', event.stanza)[0]
    assert query_node.attributes['node'] == \
        client + '#' + c['ver']

    # send good reply
    result = make_result_iq(stream, event.stanza)
    query = result.firstChildElement()
    query['node'] = client + '#' + c['ver']
    feature = query.addElement('feature')
    feature['var'] = ns.FILE_TRANSFER
    stream.send(result)

    generic_tubes_caps = dbus.Dictionary({contact_handle:
            [(text_fixed_properties, text_allowed_properties),
             (ft_fixed_properties, ft_allowed_properties)]})

    event = q.expect('dbus-signal', signal='ContactCapabilitiesChanged')
    assert len(event.args) == 1
    assert event.args[0] == generic_tubes_caps

    caps = conn_caps_iface.GetContactCapabilities([contact_handle])
    assert caps == generic_tubes_caps, caps
    # test again, to check GetContactCapabilities does not have side effect
    caps = conn_caps_iface.GetContactCapabilities([contact_handle])
    assert caps == generic_tubes_caps, caps
    # check the Contacts interface give the same caps
    caps_via_contacts_iface = conn_contacts_iface.GetContactAttributes(
            [contact_handle], [cs.CONN_IFACE_CONTACT_CAPS], False) \
            [contact_handle][cs.ATTR_CONTACT_CAPABILITIES]
    assert caps_via_contacts_iface == caps[contact_handle], \
                                    caps_via_contacts_iface

def test(q, bus, conn, stream):
    client = 'http://telepathy.freedesktop.org/fake-ft-client'

    test_ft_caps_from_contact(q, bus, conn, stream, 'bilbo1@foo.com/Foo',
        2L, client)

    # our own capabilities, formerly tested here, are now in
    # tests/twisted/caps/advertise-contact-capabilities.py

if __name__ == '__main__':
    exec_test(test)
