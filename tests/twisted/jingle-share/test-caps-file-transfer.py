import dbus

from twisted.words.xish import xpath

from servicetest import assertEquals
from gabbletest import exec_test, make_result_iq, sync_stream, make_presence
import constants as cs

from caps_helper import compute_caps_hash, \
    text_fixed_properties, text_allowed_properties, \
    stream_tube_fixed_properties, stream_tube_allowed_properties, \
    dbus_tube_fixed_properties, dbus_tube_allowed_properties, \
    ft_fixed_properties, ft_allowed_properties

import ns

def test_ft_caps_from_contact(q, bus, conn, stream, contact, contact_handle, client):

    conn_caps_iface = dbus.Interface(conn, cs.CONN_IFACE_CONTACT_CAPS)
    conn_contacts_iface = dbus.Interface(conn, cs.CONN_IFACE_CONTACTS)

    # send presence with no FT cap
    presence = make_presence(contact, status='hello')
    c = presence.addElement((ns.CAPS, 'c'))
    c['node'] = client
    c['ver'] = compute_caps_hash([], [], {})
    c['ext'] = ""
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
    c['ext'] = "share-v1"
    c['ver'] = compute_caps_hash([], [], {})
    stream.send(presence)

    # Gabble looks up our capabilities
    event = q.expect('stream-iq', to=contact, query_ns=ns.DISCO_INFO)
    query_node = xpath.queryForNodes('/iq/query', event.stanza)[0]
    assert query_node.attributes['node'] == \
        client + '#' + c['ext']

    # send good reply
    result = make_result_iq(stream, event.stanza)
    query = result.firstChildElement()
    query['node'] = client + '#' + c['ext']
    feature = query.addElement('feature')
    feature['var'] = ns.GOOGLE_FEAT_SHARE
    stream.send(result)


    generic_ft_caps = dbus.Dictionary({contact_handle:
            [(text_fixed_properties, text_allowed_properties),
             (ft_fixed_properties, ft_allowed_properties)]})

    event = q.expect('dbus-signal', signal='ContactCapabilitiesChanged')
    assert len(event.args) == 1
    assert event.args[0] == generic_ft_caps

    caps = conn_caps_iface.GetContactCapabilities([contact_handle])
    assert caps == generic_ft_caps, caps
    # test again, to check GetContactCapabilities does not have side effect
    caps = conn_caps_iface.GetContactCapabilities([contact_handle])
    assert caps == generic_ft_caps, caps
    # check the Contacts interface give the same caps
    caps_via_contacts_iface = conn_contacts_iface.GetContactAttributes(
            [contact_handle], [cs.CONN_IFACE_CONTACT_CAPS], False) \
            [contact_handle][cs.ATTR_CONTACT_CAPABILITIES]
    assert caps_via_contacts_iface == caps[contact_handle], \
                                    caps_via_contacts_iface

def test(q, bus, conn, stream):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])

    client = 'http://telepathy.freedesktop.org/fake-client'

    test_ft_caps_from_contact(q, bus, conn, stream, 'bilbo1@foo.com/Foo',
        2L, client)

    # our own capabilities, formerly tested here, are now in
    # tests/twisted/caps/advertise-contact-capabilities.py


generic_ft_caps = [(text_fixed_properties, text_allowed_properties),
                   (stream_tube_fixed_properties, \
                        stream_tube_allowed_properties),
                   (dbus_tube_fixed_properties, dbus_tube_allowed_properties),
                   (ft_fixed_properties, ft_allowed_properties)]

generic_caps = [(text_fixed_properties, text_allowed_properties),
                   (stream_tube_fixed_properties, \
                        stream_tube_allowed_properties),
                   (dbus_tube_fixed_properties, dbus_tube_allowed_properties)]

def check_contact_caps(conn, handle, with_ft):
    conn_caps_iface = dbus.Interface(conn, cs.CONN_IFACE_CONTACT_CAPS)
    conn_contacts_iface = dbus.Interface(conn, cs.CONN_IFACE_CONTACTS)

    if with_ft:
        expected_caps = dbus.Dictionary({handle: generic_ft_caps})
    else:
        expected_caps = dbus.Dictionary({handle: generic_caps})

    caps = conn_caps_iface.GetContactCapabilities([handle])
    assert caps == expected_caps, caps
    # check the Contacts interface give the same caps
    caps_via_contacts_iface = conn_contacts_iface.GetContactAttributes(
            [handle], [cs.CONN_IFACE_CONTACT_CAPS], False) \
            [handle][cs.ATTR_CONTACT_CAPABILITIES]
    assert caps_via_contacts_iface == caps[handle], \
        caps_via_contacts_iface


def test2(q, bus, connections, streams):

    for i, conn in enumerate(connections):
        path = conn.object.__dbus_object_path__
        print "Connecting %s" % path
        conn.Connect()
        q.expect('dbus-signal', signal='StatusChanged', path=path,
            args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])
        print "DONE CONNECTING %s" % path

    conn1 = connections[0]
    conn2 = connections[1]
    conn1_handle = conn1.Properties.Get(cs.CONN, 'SelfHandle')
    conn1_jid = conn1.InspectHandles(cs.HT_CONTACT, [conn1_handle])[0]
    conn2_handle = conn2.Properties.Get(cs.CONN, 'SelfHandle')
    conn2_jid = conn2.InspectHandles(cs.HT_CONTACT, [conn2_handle])[0]
    handle1 = conn2.RequestHandles(cs.HT_CONTACT, [conn1_jid])[0]
    handle2 = conn1.RequestHandles(cs.HT_CONTACT, [conn2_jid])[0]

    q.expect('dbus-signal', signal='ContactCapabilitiesChanged',
             path=conn1.object.__dbus_object_path__)
    q.expect('dbus-signal', signal='ContactCapabilitiesChanged',
             path=conn2.object.__dbus_object_path__)

    check_contact_caps (conn1, handle2, False)
    check_contact_caps (conn2, handle1, False)

    caps_iface = dbus.Interface(conn1, cs.CONN_IFACE_CONTACT_CAPS)
    caps_iface.UpdateCapabilities([("self",
                                    [ft_fixed_properties],
                                    dbus.Array([], signature="s"))])

    q.expect('dbus-signal', signal='ContactCapabilitiesChanged',
             path=conn1.object.__dbus_object_path__,
             args=[{conn1_handle:generic_ft_caps}])
    q.expect('dbus-signal', signal='ContactCapabilitiesChanged',
             path=conn2.object.__dbus_object_path__,
             args=[{handle1:generic_ft_caps}])

    check_contact_caps (conn2, handle1, True)

    caps_iface = dbus.Interface(conn2, cs.CONN_IFACE_CONTACT_CAPS)
    caps_iface.UpdateCapabilities([("self",
                                    [ft_fixed_properties],
                                    dbus.Array([], signature="s"))])

    q.expect('dbus-signal', signal='ContactCapabilitiesChanged',
             path=conn2.object.__dbus_object_path__,
             args=[{conn2_handle:generic_ft_caps}])

    q.expect('dbus-signal', signal='ContactCapabilitiesChanged',
             path=conn1.object.__dbus_object_path__,
             args=[{handle2:generic_ft_caps}])

    check_contact_caps (conn1, handle2, True)


if __name__ == '__main__':
    exec_test(test)
    exec_test(test2, num_instances=2)
