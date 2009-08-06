import dbus

from gabbletest import exec_test, make_result_iq
from servicetest import call_async, EventPattern, assertEquals

from twisted.words.xish import xpath
import constants as cs
import ns

Rich_Presence_Access_Control_Type_Publish_List = 1

def test(q, bus, conn, stream):
    conn.Connect()

    # discard activities request and status change
    q.expect_many(
        EventPattern('stream-iq', iq_type='set',
            query_ns=ns.PUBSUB),
        EventPattern('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED]),
        )

    # check location properties

    access_control_types = conn.Get(
            cs.CONN_IFACE_LOCATION, "LocationAccessControlTypes",
            dbus_interface=cs.PROPERTIES_IFACE)
    # only one access control is implemented in Gabble at the moment:
    assert len(access_control_types) == 1, access_control_types
    assert access_control_types[0] == \
        Rich_Presence_Access_Control_Type_Publish_List

    access_control = conn.Get(
            cs.CONN_IFACE_LOCATION, "LocationAccessControl",
            dbus_interface=cs.PROPERTIES_IFACE)
    assert len(access_control) == 2, access_control
    assert access_control[0] == \
        Rich_Presence_Access_Control_Type_Publish_List

    properties = conn.GetAll(
            cs.CONN_IFACE_LOCATION,
            dbus_interface=cs.PROPERTIES_IFACE)

    assert properties.get('LocationAccessControlTypes') == access_control_types
    assert properties.get('LocationAccessControl') == access_control

    # Test setting the properties

    # Enum out of range
    bad_access_control = dbus.Struct([dbus.UInt32(99),
            dbus.UInt32(0, variant_level=1)],
            signature=dbus.Signature('uv'))
    try:
        conn.Set (cs.CONN_IFACE_LOCATION, 'LocationAccessControl', bad_access_control,
            dbus_interface =cs.PROPERTIES_IFACE)
    except dbus.DBusException, e:
        pass
    else:
        assert False, "Should have had an error!"

    # Bad type
    bad_access_control = dbus.String("This should not be a string")
    try:
        conn.Set (cs.CONN_IFACE_LOCATION, 'LocationAccessControl', bad_access_control,
            dbus_interface =cs.PROPERTIES_IFACE)
    except dbus.DBusException, e:
        assert e.get_dbus_name() == cs.INVALID_ARGUMENT, e.get_dbus_name()
    else:
        assert False, "Should have had an error!"

    # Bad type
    bad_access_control = dbus.Struct([dbus.String("bad"), dbus.String("!"),
            dbus.UInt32(0, variant_level=1)],
            signature=dbus.Signature('ssv'))
    try:
        conn.Set (cs.CONN_IFACE_LOCATION, 'LocationAccessControl', bad_access_control,
            dbus_interface =cs.PROPERTIES_IFACE)
    except dbus.DBusException, e:
        assert e.get_dbus_name() == cs.INVALID_ARGUMENT, e.get_dbus_name()
    else:
        assert False, "Should have had an error!"

    # Correct
    conn.Set (cs.CONN_IFACE_LOCATION, 'LocationAccessControl', access_control,
        dbus_interface =cs.PROPERTIES_IFACE)

    # LocationAccessControlTypes is read-only, check Gabble return the
    # PermissionDenied error
    try:
        conn.Set (cs.CONN_IFACE_LOCATION, 'LocationAccessControlTypes',
            access_control_types,
            dbus_interface =cs.PROPERTIES_IFACE)
    except dbus.DBusException, e:
        assert e.get_dbus_name() == cs.PERMISSION_DENIED, e.get_dbus_name()
    else:
        assert False, "Should have had an error!"

    conn.Location.SetLocation({
        'lat': dbus.Double(0.0, variant_level=1),
        'lon': 0.0,
        'language': 'en'})

    event = q.expect('stream-iq', predicate=lambda x:
        xpath.queryForNodes("/iq/pubsub/publish/item/geoloc", x.stanza))
    geoloc = xpath.queryForNodes("/iq/pubsub/publish/item/geoloc", event.stanza)[0]
    assertEquals(geoloc.getAttribute((ns.XML, 'lang')), 'en')

    handle = conn.RequestHandles(1, ['bob@foo.com'])[0]
    call_async(q, conn.Location, 'GetLocations', [handle])

    event = q.expect('stream-iq', iq_type='get',
        query_ns=ns.PUBSUB)
    result = make_result_iq(stream, event.stanza)
    result['from'] = 'bob@foo.com'
    query = result.firstChildElement()
    geoloc = query.addElement((ns.GEOLOC, 'geoloc'))
    geoloc['xml:lang'] = 'en'
    geoloc.addElement('lat', content='1.234')
    geoloc.addElement('lon', content='5.678')
    stream.send(result)

    _, update_event = q.expect_many(
        EventPattern('dbus-return', method='GetLocations'),
        EventPattern('dbus-signal', signal='LocationUpdated'))

    handle, location = update_event.args
    assertEquals(location['language'], 'en')

    # Get location again, only GetLocation should get fired
    handle = conn.RequestHandles(1, ['bob@foo.com'])[0]
    call_async(q, conn.Location, 'GetLocations', [handle])

    q.expect('dbus-return', method='GetLocations')

if __name__ == '__main__':
    exec_test(test)
