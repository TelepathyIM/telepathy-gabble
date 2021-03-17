import dbus
import time
import datetime

from gabbletest import (
    exec_test, elem, acknowledge_iq, send_error_reply, sync_stream,
    make_result_iq, disconnect_conn,
)
from servicetest import (
    call_async, EventPattern,
    assertEquals, assertLength, assertContains,
)

from twisted.words.xish import xpath
import constants as cs
import ns

Rich_Presence_Access_Control_Type_Publish_List = 1

def get_location(conn, contact):
    h2asv = conn.Contacts.GetContactAttributes([contact], [cs.CONN_IFACE_LOCATION], False)
    return h2asv[contact].get(cs.ATTR_LOCATION)

def test(q, bus, conn, stream):
    # we don't yet know we have PEP
    assertEquals(0, conn.Get(cs.CONN_IFACE_LOCATION,
        "SupportedLocationFeatures", dbus_interface=cs.PROPERTIES_IFACE))

    conn.Connect()

    # discard activities request and status change
    q.expect_many(
        EventPattern('stream-iq', iq_type='set',
            query_ns=ns.PUBSUB),
        EventPattern('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED]),
        )

    # we now know we have PEP
    assertEquals(cs.LOCATION_FEATURE_CAN_SET, conn.Get(cs.CONN_IFACE_LOCATION,
        "SupportedLocationFeatures", dbus_interface=cs.PROPERTIES_IFACE))

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
    except dbus.DBusException as e:
        pass
    else:
        assert False, "Should have had an error!"

    # Bad type
    bad_access_control = dbus.String("This should not be a string")
    try:
        conn.Set (cs.CONN_IFACE_LOCATION, 'LocationAccessControl', bad_access_control,
            dbus_interface =cs.PROPERTIES_IFACE)
    except dbus.DBusException as e:
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
    except dbus.DBusException as e:
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
    except dbus.DBusException as e:
        assert e.get_dbus_name() == cs.PERMISSION_DENIED, e.get_dbus_name()
    else:
        assert False, "Should have had an error!"

    date = dbus.Int64(time.time())
    date_str = datetime.datetime.utcfromtimestamp(date).strftime('%FT%H:%M:%SZ')

    # set a Location
    call_async(q, conn.Location, 'SetLocation', {
        'lat': dbus.Double(0.0, variant_level=1),
        'lon': 0.0,
        'language': 'en',
        'timestamp': date,
        'country': 'Congo',
        'accuracy': 1.4,
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
    lat = xpath.queryForNodes('/geoloc/accuracy', geoloc)[0]
    assertEquals(float(str(lat)), 1.4)

    acknowledge_iq(stream, event.stanza)
    q.expect('dbus-return', method='SetLocation')

    # Server refuses to set Location
    call_async(q, conn.Location, 'SetLocation', {
        'lat': 0.0,
        'lon': 0.0})

    geoloc_iq_set_event = EventPattern('stream-iq', predicate=lambda x:
        xpath.queryForNodes("/iq/pubsub/publish/item/geoloc", x.stanza))
    event = q.expect_many(geoloc_iq_set_event)[0]

    send_error_reply(stream, event.stanza)
    q.expect('dbus-error', method='SetLocation')

    # Request Bob's location
    bob_handle = conn.get_contact_handle_sync('bob@foo.com')

    # Gabble should not send a pubsub query. The point of PEP is that we don't
    # have to do this.
    pubsub_get_pattern = EventPattern('stream-iq', iq_type='get',
        query_ns=ns.PUBSUB)
    q.forbid_events([ pubsub_get_pattern ])

    location = get_location(conn, bob_handle)
    # Location isn't known yet
    assertEquals(None, location)

    # Sync the XMPP stream to ensure Gabble hasn't sent a query.
    sync_stream(q, stream)

    # Bob updates his location
    message = elem('message', from_='bob@foo.com')(
        elem((ns.PUBSUB_EVENT), 'event')(
            elem('items', node=ns.GEOLOC)(
                elem('item', id='12345')(
                    elem(ns.GEOLOC, 'geoloc', attrs={'xml:lang': 'en'})(
                        elem('lat')(u'1.25'),
                        elem('lon')(u'5.5'),
                        elem('country')(u'Belgium'),
                        elem('accuracy')(u'2.3'),
                        elem('timestamp')(date_str),
                        # invalid element, will be ignored by Gabble
                        elem('badger')(u'mushroom'),
                    )
                )
            )
        )
    )
    stream.send(message)

    update_event = q.expect('dbus-signal', signal='LocationUpdated')

    handle, location = update_event.args
    assertEquals(bob_handle, handle)

    assertLength(6, location)
    assertEquals(location['language'], 'en')
    assertEquals(location['lat'], 1.25)
    assertEquals(location['lon'], 5.5)
    assertEquals(location['country'], 'Belgium')
    assertEquals(location['accuracy'], 2.3)
    assertEquals(location['timestamp'], date)

    # Get location again; Gabble should return the cached location
    loc = get_location(conn, bob_handle)
    assertEquals(loc, location)

    charles_handle = conn.get_contact_handle_sync('charles@foo.com')

    # check that Contacts interface supports location
    attributes = conn.Contacts.GetContactAttributes(
        [bob_handle, charles_handle], [cs.CONN_IFACE_LOCATION], False)
    assertLength(2, attributes)
    assertContains(bob_handle, attributes)
    assertContains(charles_handle, attributes)

    assertEquals(
        { cs.CONN_IFACE_LOCATION + '/location': location,
          cs.CONN + '/contact-id': 'bob@foo.com'},
        attributes[bob_handle])

    assertEquals(
        { cs.CONN + '/contact-id': 'charles@foo.com'},
        attributes[charles_handle])

    # Try to set our location by passing a valid with an invalid type (lat is
    # supposed to be a double)

    q.forbid_events([geoloc_iq_set_event])

    try:
        conn.Location.SetLocation({'lat': 'pony'})
    except dbus.DBusException as e:
        assertEquals(e.get_dbus_name(), cs.INVALID_ARGUMENT)
    else:
        assert False

    # Bob updates his location again
    message = elem('message', from_='bob@foo.com')(
        elem((ns.PUBSUB_EVENT), 'event')(
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

    # Now we test explicitly retrieving Bob's location, so we should not forbid
    # such queries. :)
    q.unforbid_events([ pubsub_get_pattern ])

    call_async(q, conn.Location, 'RequestLocation', bob_handle)
    e = q.expect('stream-iq', iq_type='get', query_ns=ns.PUBSUB,
        to='bob@foo.com')

    # Hey, while we weren't looking Bob moved abroad!
    result = make_result_iq(stream, e.stanza)
    result['from'] = 'bob@foo.com'
    pubsub_node = result.firstChildElement()
    pubsub_node.addChild(
      elem('items', node=ns.GEOLOC)(
        elem('item', id='12345')(
          elem(ns.GEOLOC, 'geoloc')(
            elem ('country') (u'Chad')
          )
        )
      )
    )
    stream.send(result)

    ret = q.expect('dbus-return', method='RequestLocation')
    location, = ret.value
    assertLength(1, location)
    assertEquals(location['country'], 'Chad')

    # Let's ask again; this time Bob's server hates us for some reason.
    call_async(q, conn.Location, 'RequestLocation', bob_handle)
    e = q.expect('stream-iq', iq_type='get', query_ns=ns.PUBSUB,
        to='bob@foo.com')
    send_error_reply(stream, e.stanza,
        elem('error', type='auth')(
          elem(ns.STANZA, 'forbidden')
        ))
    e = q.expect('dbus-error', method='RequestLocation')
    assertEquals(cs.PERMISSION_DENIED, e.name)

    # FIXME: maybe we should check that the cache gets invalidated in this
    # case? We should also test whether or not the cache is invalidated
    # properly if the contact clears their PEP node.

    # Let's ask a final time, and disconnect while we're doing so, to make sure
    # this doesn't break Gabble or Wocky.
    call_async(q, conn.Location, 'RequestLocation', bob_handle)
    e = q.expect('stream-iq', iq_type='get', query_ns=ns.PUBSUB,
        to='bob@foo.com')
    # Tasty argument unpacking. disconnect_conn returns two lists, one for
    # expeced_before=[] and one for expected_after=[...]
    _, (e, ) = disconnect_conn(q, conn, stream,
        expected_after=[EventPattern('dbus-error', method='RequestLocation')])
    assertEquals(cs.CANCELLED, e.name)

if __name__ == '__main__':
    exec_test(test, do_connect=False)
