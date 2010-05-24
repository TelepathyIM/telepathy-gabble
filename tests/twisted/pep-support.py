import dbus

from gabbletest import exec_test, GoogleXmlStream, acknowledge_iq, BaseXmlStream,\
    sync_stream
from servicetest import call_async, EventPattern

from twisted.words.xish import xpath
import constants as cs
import ns


# PEP supports is advertised in Server's disco which is wrong but that's what
# old ejabberd used to do.
def test_legacy(q, bus, conn, stream):
    conn.Connect()

    q.expect_many(
        EventPattern('stream-iq', iq_type='set',
            query_ns=ns.PUBSUB),
        EventPattern('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED]),
        )

    call_async(q, conn.Location, 'SetLocation', {
        'lat': 0.0,
        'lon': 0.0})

    geoloc_iq_set_event = EventPattern('stream-iq', predicate=lambda x:
        xpath.queryForNodes("/iq/pubsub/publish/item/geoloc", x.stanza))

    event = q.expect_many(geoloc_iq_set_event)[0]

    acknowledge_iq(stream, event.stanza)
    q.expect('dbus-return', method='SetLocation')

# PEP is not supported.
def test_no_pep(q, bus, conn, stream):
    conn.Connect()

    q.expect_many(
        EventPattern('stream-iq', iq_type='set',
            query_ns=ns.PUBSUB),
        EventPattern('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED]),
        )

    try:
        conn.Location.SetLocation({'lat': 0.0, 'lon': 0.0})
    except dbus.DBusException:
        pass
    else:
        assert False, "Should have had an error!"

#PEP is advertised using the right protocol
def test_pep(q, bus, conn, stream):
    conn.Connect()

    _, _, e = q.expect_many(
        EventPattern('stream-iq', iq_type='set',
            query_ns=ns.PUBSUB),
        EventPattern('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED]),
        EventPattern('stream-iq', iq_type='get', to='test@localhost',
            query_ns=ns.DISCO_INFO)
        )

    iq = e.stanza
    nodes = xpath.queryForNodes(
    "/iq/query[@xmlns='http://jabber.org/protocol/disco#info']", iq)
    query = nodes[0]
    identity = query.addElement('identity')
    identity['category'] = 'pubsub'
    identity['type'] = 'pep'

    iq['type'] = 'result'
    iq['from'] = 'test@localhost'
    stream.send(iq)

    sync_stream(q, stream)

    call_async(q, conn.Location, 'SetLocation', {
        'lat': 0.0,
        'lon': 0.0})

    geoloc_iq_set_event = EventPattern('stream-iq', predicate=lambda x:
        xpath.queryForNodes("/iq/pubsub/publish/item/geoloc", x.stanza))

    event = q.expect_many(geoloc_iq_set_event)[0]

    acknowledge_iq(stream, event.stanza)
    q.expect('dbus-return', method='SetLocation')

class MyXmppStream(BaseXmlStream):
    version = (1, 0)

    def _cb_disco_iq(self, iq):
        if iq.getAttribute('to') == 'localhost':
            iq['type'] = 'result'
            iq['from'] = 'localhost'
            self.send(iq)

if __name__ == '__main__':
    exec_test(test_legacy)
    exec_test(test_no_pep, protocol=GoogleXmlStream)
    exec_test(test_pep, protocol=MyXmppStream)
