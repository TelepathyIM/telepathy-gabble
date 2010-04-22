"""
A simple smoke-test for XEP-0186 invisibility

"""

from twisted.words.xish import domish

from gabbletest import exec_test, make_presence, XmppXmlStream
from servicetest import EventPattern, assertEquals, assertNotEquals
import ns
import constants as cs
from twisted.words.xish import domish, xpath

class InvisibleListXmlStream(XmppXmlStream):
    def _cb_disco_iq(self, iq):
        if iq.getAttribute('to') == 'localhost':
            nodes = xpath.queryForNodes(
                "/iq/query[@xmlns='http://jabber.org/protocol/disco#info']",
                iq)
            query = nodes[0]
            feature = query.addElement('feature')
            feature['var'] = ns.INVISIBLE
            feature = query.addElement('feature')
            feature['var'] = ns.PRIVACY

            iq['type'] = 'result'
            iq['from'] = iq['to']
            del iq['to']

            self.send(iq)

def test_invisible_on_connect(q, bus, conn, stream):
    props = conn.Properties.GetAll(cs.CONN_IFACE_SIMPLE_PRESENCE)
    assertNotEquals({}, props['Statuses'])
    conn.SimplePresence.SetPresence("hidden", "")

    conn.Connect()

    q.expect('stream-iq', query_name='invisible')

    q.expect('dbus-signal', signal='StatusChanged',
             args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])

def test(q, bus, conn, stream):
    conn.Connect()

    q.expect('dbus-signal', signal='StatusChanged',
             args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])

    assert ("hidden" in conn.Get(cs.CONN_IFACE_SIMPLE_PRESENCE, "Statuses",
                                 dbus_interface=cs.PROPERTIES_IFACE).keys())

    conn.SimplePresence.SetPresence("hidden", "")

    q.expect('stream-iq', query_name='invisible')

    conn.SimplePresence.SetPresence("away", "")

    q.expect('stream-iq', query_name='visible')

if __name__ == '__main__':
    exec_test(test_invisible_on_connect, protocol=InvisibleListXmlStream)
    exec_test(test, protocol=InvisibleListXmlStream)
