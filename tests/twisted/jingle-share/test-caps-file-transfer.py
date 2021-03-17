import dbus

from twisted.words.xish import xpath

from servicetest import EventPattern
from gabbletest import exec_test
import constants as cs

from caps_helper import \
    text_fixed_properties, text_allowed_properties, \
    stream_tube_fixed_properties, stream_tube_allowed_properties, \
    dbus_tube_fixed_properties, dbus_tube_allowed_properties, \
    ft_fixed_properties, ft_allowed_properties_with_metadata,\
    get_contacts_capabilities_sync

import ns
from jingleshareutils import test_ft_caps_from_contact

from config import FILE_TRANSFER_ENABLED

if not FILE_TRANSFER_ENABLED:
    print("NOTE: built with --disable-file-transfer")
    raise SystemExit(77)

def test(q, bus, conn, stream):
    client = 'http://telepathy.freedesktop.org/fake-client'

    test_ft_caps_from_contact(q, bus, conn, stream, 'bilbo1@foo.com/Foo',
        2, client)

    # our own capabilities, formerly tested here, are now in
    # tests/twisted/caps/advertise-contact-capabilities.py


generic_ft_caps = [(text_fixed_properties, text_allowed_properties),
                   (stream_tube_fixed_properties, \
                        stream_tube_allowed_properties),
                   (dbus_tube_fixed_properties, dbus_tube_allowed_properties),
                   (ft_fixed_properties, ft_allowed_properties_with_metadata)]

generic_caps = [(text_fixed_properties, text_allowed_properties),
                   (stream_tube_fixed_properties, \
                        stream_tube_allowed_properties),
                   (dbus_tube_fixed_properties, dbus_tube_allowed_properties)]

def check_contact_caps(conn, handle, with_ft):
    conn_contacts_iface = dbus.Interface(conn, cs.CONN_IFACE_CONTACTS)

    if with_ft:
        expected_caps = dbus.Dictionary({handle: generic_ft_caps})
    else:
        expected_caps = dbus.Dictionary({handle: generic_caps})

    caps = get_contacts_capabilities_sync(conn, [handle])
    assert caps == expected_caps, caps
    # check the Contacts interface give the same caps
    caps_via_contacts_iface = conn_contacts_iface.GetContactAttributes(
            [handle], [cs.CONN_IFACE_CONTACT_CAPS], False) \
            [handle][cs.ATTR_CONTACT_CAPABILITIES]
    assert caps_via_contacts_iface == caps[handle], \
        caps_via_contacts_iface


def test2(q, bus, connections, streams):
    conn1, conn2 = connections
    stream1, stream2 = streams
    conn1_handle = conn1.Properties.Get(cs.CONN, 'SelfHandle')
    conn1_jid = conn1.inspect_contact_sync(conn1_handle)
    conn2_handle = conn2.Properties.Get(cs.CONN, 'SelfHandle')
    conn2_jid = conn2.inspect_contact_sync(conn2_handle)
    handle1 = conn2.get_contact_handle_sync(conn1_jid)
    handle2 = conn1.get_contact_handle_sync(conn2_jid)

    q.expect_many(EventPattern('dbus-signal',
                               signal='ContactCapabilitiesChanged',
                               path=conn1.object.object_path),
                  EventPattern('dbus-signal',
                               signal='ContactCapabilitiesChanged',
                               path=conn2.object.object_path))

    check_contact_caps (conn1, handle2, False)
    check_contact_caps (conn2, handle1, False)

    caps_iface = dbus.Interface(conn1, cs.CONN_IFACE_CONTACT_CAPS)
    caps_iface.UpdateCapabilities([("self",
                                    [ft_fixed_properties],
                                    dbus.Array([], signature="s"))])

    _, presence, disco, _ = \
        q.expect_many(EventPattern('dbus-signal',
                                   signal='ContactCapabilitiesChanged',
                                   path=conn1.object.object_path,
                                   args=[{conn1_handle:generic_ft_caps}]),
                      EventPattern('stream-presence', stream=stream1),
                      EventPattern('stream-iq', stream=stream1,
                                   query_ns=ns.DISCO_INFO,
                                   iq_type = 'result'),
                      EventPattern('dbus-signal',
                                   signal='ContactCapabilitiesChanged',
                                   path=conn2.object.object_path,
                                   args=[{handle1:generic_ft_caps}]))

    presence_c = xpath.queryForNodes('/presence/c', presence.stanza)[0]
    assert "share-v1" in presence_c.attributes['ext']

    conn1_ver = presence_c.attributes['ver']

    found_share = False
    for feature in xpath.queryForNodes('/iq/query/feature', disco.stanza):
        if feature.attributes['var'] == ns.GOOGLE_FEAT_SHARE:
            found_share = True
    assert found_share

    check_contact_caps (conn2, handle1, True)

    caps_iface = dbus.Interface(conn2, cs.CONN_IFACE_CONTACT_CAPS)
    caps_iface.UpdateCapabilities([("self",
                                    [ft_fixed_properties],
                                    dbus.Array([], signature="s"))])

    _, presence, _ = \
        q.expect_many(EventPattern('dbus-signal',
                                   signal='ContactCapabilitiesChanged',
                                   path=conn2.object.object_path,
                                   args=[{conn2_handle:generic_ft_caps}]),
                      EventPattern('stream-presence', stream=stream2),
                      EventPattern('dbus-signal',
                                   signal='ContactCapabilitiesChanged',
                                   path=conn1.object.object_path,
                                   args=[{handle2:generic_ft_caps}]))

    presence_c = xpath.queryForNodes('/presence/c', presence.stanza)[0]
    assert "share-v1" in presence_c.attributes['ext']

    # We will have the same capabilities on both sides, so we can't check for
    # a cap disco since the hash will be the same, so we need to make sure the
    # hash is indeed the same
    assert presence_c.attributes['ver'] == conn1_ver

    found_share = False
    for feature in xpath.queryForNodes('/iq/query/feature', disco.stanza):
        if feature.attributes['var'] == ns.GOOGLE_FEAT_SHARE:
            found_share = True
    assert found_share

    check_contact_caps (conn1, handle2, True)


if __name__ == '__main__':
    exec_test(test)
    exec_test(test2, num_instances=2)
