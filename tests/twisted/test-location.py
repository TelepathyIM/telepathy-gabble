import dbus
import time
import datetime

from gabbletest import exec_test, make_result_iq, elem
from servicetest import call_async, EventPattern, assertEquals, assertLength

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

    date = dbus.Int64(time.time())
    date_str = datetime.datetime.utcfromtimestamp(date).strftime('%FT%H:%M:%SZ')

    # set a Location
    conn.Location.SetLocation({
        'lat': dbus.Double(0.0, variant_level=1),
        'lon': 0.0,
        'language': 'en',
        'timestamp': date,
        'country': 'Congo',
        # Gabble silently ignores unknown keys
        'badger': 'mushroom'})

    geoloc_iq_set_event = EventPattern('stream-iq', predicate=lambda x:
        xpath.queryForNodes("/iq/pubsub/publish/item/geoloc", x.stanza))

    event = q.expect_many(geoloc_iq_set_event)[0]
    geoloc = xpath.queryForNodes("/iq/pubsub/publish/item/geoloc", event.stanza)[0]
    assertEquals(geoloc.getAttribute((ns.XML, 'lang')), 'en')
    lon = xpath.queryForNodes('/geoloc/lon', geoloc)[0]
    assertEquals(float(str(lon)), 0.0)
    lat = xpath.queryForNodes('/geoloc/lat', geoloc)[0]
    assertEquals(float(str(lat)), 0.0)
    timestamp = xpath.queryForNodes('/geoloc/timestamp', geoloc)[0]
    assertEquals(str(timestamp), date_str)
    country = xpath.queryForNodes('/geoloc/country', geoloc)[0]
    assertEquals(str(country), 'Congo')

    # Request Bob's location
    bob_handle = conn.RequestHandles(1, ['bob@foo.com'])[0]
    call_async(q, conn.Location, 'GetLocations', [bob_handle])

    # Gabble sends a pubsub query
    event = q.expect('stream-iq', iq_type='get',
        query_ns=ns.PUBSUB)

    # GetLocations doesn't wait for the reply
    e = q.expect('dbus-return', method='GetLocations')
    locations = e.value[0]
    # Location isn't known yet
    assertLength(0, locations)

    # reply with Bob's location
    result = make_result_iq(stream, event.stanza)
    result['from'] = 'bob@foo.com'
    query = result.firstChildElement()
    geoloc = query.addElement((ns.GEOLOC, 'geoloc'))
    geoloc['xml:lang'] = 'en'
    geoloc.addElement('lat', content='1.25')
    geoloc.addElement('lon', content='5.5')
    geoloc.addElement('country', content='Belgium')
    geoloc.addElement('timestamp', content=date_str)
    # invalid element, will be discarded by Gabble
    geoloc.addElement('badger', content='mushroom')
    stream.send(result)

    update_event = q.expect('dbus-signal', signal='LocationUpdated')

    handle, location = update_event.args
    assertEquals(handle, bob_handle)

    assertLength(5, location)
    assertEquals(location['language'], 'en')
    assertEquals(location['lat'], 1.25)
    assertEquals(location['lon'], 5.5)
    assertEquals(location['country'], 'Belgium')
    assertEquals(location['timestamp'], date)

    q.forbid_events([geoloc_iq_set_event])

    # Get location again, Gabble doesn't send a query any more and return the known
    # location
    locations = conn.Location.GetLocations([bob_handle])
    assertLength(1, locations)
    assertEquals(locations[bob_handle], location)

    # check that Contacts interface supports location
    assert conn.Contacts.GetContactAttributes([bob_handle],
        [cs.CONN_IFACE_LOCATION], False) == { bob_handle:
            { cs.CONN_IFACE_LOCATION + '/location': location,
              'org.freedesktop.Telepathy.Connection/contact-id': 'bob@foo.com'}}

    # Try to set our location by passing a valid with an invalid type (lat is
    # supposed to be a double)
    try:
        conn.Location.SetLocation({'lat': 'pony'})
    except dbus.DBusException, e:
        assertEquals(e.get_dbus_name(), cs.INVALID_ARGUMENT)
    else:
        assert False

    # Bob updates his location
    message = elem('message', from_='bob@foo.com')(
        elem((ns.PUBSUB + "#event"), 'event')(
            elem('items', node=ns.GEOLOC)(
                elem('item', id='12345')(
                    elem(ns.GEOLOC, 'geoloc')(
                        elem ('country') (u'France')
                    )
                )
            )
        )
    )
    stream.send(message)

    update_event = q.expect('dbus-signal', signal='LocationUpdated')
    handle, location = update_event.args
    assertEquals(handle, bob_handle)
    assertLength(1, location)
    assertEquals(location['country'], 'France')

if __name__ == '__main__':
    exec_test(test)
