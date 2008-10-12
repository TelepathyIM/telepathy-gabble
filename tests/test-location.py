
from gabbletest import exec_test, make_result_iq
from servicetest import call_async

def test(q, bus, conn, stream):
    # hack
    import dbus
    conn.interfaces['Location'] = \
        dbus.Interface(conn, 'org.freedesktop.Telepathy.Location')

    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged', args=[0, 1])

    # discard activities request
    q.expect('stream-iq', iq_type='set',
        query_ns='http://jabber.org/protocol/pubsub')

    conn.Location.SetLocation({
        'lat': dbus.Double(0.0, variant_level=1), 'lon': 0.0})

    event = q.expect('stream-iq', iq_type='set',
        query_ns='http://jabber.org/protocol/pubsub')

    handle = conn.RequestHandles(1, ['bob@foo.com'])[0]
    call_async(q, conn.Location, 'GetLocations', [handle])

    # XXX this is made up
    event = q.expect('stream-iq', iq_type='get',
        query_ns='http://jabber.org/protocol/pubsub')
    result = make_result_iq(stream, event.stanza)
    result['from'] = 'bob@foo.com'
    query = result.firstChildElement()
    geoloc = query.addElement(('http://jabber.org/protocol/geoloc', 'geoloc'))
    geoloc.addElement('lat', content='1.234')
    geoloc.addElement('lon', content='5.678')
    stream.send(result)

    q.expect('dbus-return', method='GetLocations')

    conn.Disconnect()
    q.expect('dbus-signal', signal='StatusChanged', args=[2, 1])
    return True

if __name__ == '__main__':
    exec_test(test)

