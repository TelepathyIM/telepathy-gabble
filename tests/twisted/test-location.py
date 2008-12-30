from gabbletest import exec_test, make_result_iq
from servicetest import call_async, EventPattern

from twisted.words.xish import domish, xpath

location_iface = \
    'org.freedesktop.Telepathy.Connection.Interface.Location.DRAFT'

def test(q, bus, conn, stream):
    # hack
    import dbus
    conn.interfaces['Location'] = \
        dbus.Interface(conn, location_iface)

    conn.Connect()
    q.expect_many(
        EventPattern('stream-iq', iq_type='set', query_ns='http://jabber.org/protocol/pubsub'),
        EventPattern('dbus-signal', signal='StatusChanged', args=[0, 1]))
    #q.expect('dbus-signal', signal='StatusChanged', args=[0, 1])

    # check location properties
    #properties = conn.GetAll(
    #        location_iface,
    #        dbus_interface='org.freedesktop.DBus.Properties')

    #assert properties.get('LocationAccessControlTypes') is not None
    #assert properties.get('LocationAccessControl') is not None

    # discard activities request
    #q.expect('stream-iq', iq_type='set',
    #    query_ns='http://jabber.org/protocol/pubsub')

    conn.Location.SetLocation({
        'lat': dbus.Double(0.0, variant_level=1), 'lon': 0.0})

    event = q.expect('stream-iq', predicate=lambda x: 
        xpath.queryForNodes("/iq/pubsub/publish/item/geoloc", x.stanza))

    handle = conn.RequestHandles(1, ['bob@foo.com'])[0]
    call_async(q, conn.Location, 'GetLocations', [handle])

    event = q.expect('stream-iq', iq_type='get',
        query_ns='http://jabber.org/protocol/pubsub')
    result = make_result_iq(stream, event.stanza)
    result['from'] = 'bob@foo.com'
    query = result.firstChildElement()
    geoloc = query.addElement(('http://jabber.org/protocol/geoloc', 'geoloc'))
    geoloc.addElement('lat', content='1.234')
    geoloc.addElement('lon', content='5.678')
    stream.send(result)

    q.expect_many(
        EventPattern('dbus-return', method='GetLocations'),
        EventPattern('dbus-signal', signal='LocationUpdated'))

    # Get location again, only GetLocation should get fired
    handle = conn.RequestHandles(1, ['bob@foo.com'])[0]
    call_async(q, conn.Location, 'GetLocations', [handle])

    q.expect('dbus-return', method='GetLocations')

    conn.Disconnect()
    q.expect('dbus-signal', signal='StatusChanged', args=[2, 1])
    return True

if __name__ == '__main__':
    exec_test(test)

