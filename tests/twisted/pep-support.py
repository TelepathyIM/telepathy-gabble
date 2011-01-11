import dbus

from gabbletest import exec_test, GoogleXmlStream, acknowledge_iq, BaseXmlStream,\
    sync_stream
from servicetest import call_async, EventPattern

from twisted.words.protocols.jabber.client import IQ
from twisted.words.xish import xpath
import constants as cs
import ns


# PEP supports is advertised in Server's disco which is wrong but that's what
# old ejabberd used to do.
class PepInServerDiscoXmlStream(BaseXmlStream):
    version = (1, 0)

    def _cb_disco_iq(self, iq):
        # Advertise PEP support in server disco rather than when discoing our
        # bare JID
        nodes = xpath.queryForNodes(
            "/iq/query[@xmlns='http://jabber.org/protocol/disco#info']",
            iq)
        query = nodes[0]
        identity = query.addElement('identity')
        identity['category'] = 'pubsub'
        identity['type'] = 'pep'

        iq['type'] = 'result'
        iq['from'] = 'localhost'
        self.send(iq)

    def _cb_bare_jid_disco_iq(self, iq):
        # Additionally, Prosody 0.6.1 doesn't like us discoing our own bare
        # JID, and responds with an error which doesn't have the 'from'
        # attribute. Wocky used to discard this, but now tolerates it.
        result = IQ(self, 'error')
        result['id'] = iq['id']
        error = result.addElement((None, 'error'))
        error['type'] = 'cancel'
        error.addElement((ns.STANZA, 'service-unavailable'))
        self.send(result)

def test_legacy(q, bus, conn, stream):
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
    call_async(q, conn.Location, 'SetLocation', {
        'lat': 0.0,
        'lon': 0.0})

    q.expect('dbus-error', name=cs.NOT_IMPLEMENTED)

#PEP is advertised using the right protocol
def test_pep(q, bus, conn, stream):
    call_async(q, conn.Location, 'SetLocation', {
        'lat': 0.0,
        'lon': 0.0})

    geoloc_iq_set_event = EventPattern('stream-iq', predicate=lambda x:
        xpath.queryForNodes("/iq/pubsub/publish/item/geoloc", x.stanza))

    event = q.expect_many(geoloc_iq_set_event)[0]

    acknowledge_iq(stream, event.stanza)
    q.expect('dbus-return', method='SetLocation')

if __name__ == '__main__':
    exec_test(test_legacy, protocol=PepInServerDiscoXmlStream)
    exec_test(test_no_pep, protocol=GoogleXmlStream)
    exec_test(test_pep)
