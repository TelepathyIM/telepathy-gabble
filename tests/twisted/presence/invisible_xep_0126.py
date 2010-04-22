"""
A simple smoke-test for XEP-0126 invisibility

"""

from twisted.words.xish import domish

from gabbletest import exec_test, make_presence, XmppXmlStream
from servicetest import EventPattern, assertNotEquals
import ns
import constants as cs
from twisted.words.xish import domish, xpath

class PrivacyListXmlStream(XmppXmlStream):
    def _cb_disco_iq(self, iq):
        if iq.getAttribute('to') == 'localhost':
            nodes = xpath.queryForNodes(
                "/iq/query[@xmlns='http://jabber.org/protocol/disco#info']",
                iq)
            query = nodes[0]
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

    create_list, set_active, _ = q.expect_many(
        EventPattern('stream-iq', query_ns=ns.PRIVACY, iq_type='set'),
        EventPattern('stream-iq', query_ns=ns.PRIVACY, iq_type='set'),
        EventPattern('dbus-signal', signal='StatusChanged',
             args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED]))

    assertNotEquals (xpath.queryForNodes('/query/list/item/presence-out',
                                         create_list.query), [])

def test(q, bus, conn, stream):
    conn.Connect()

    event, _ = q.expect_many(
        EventPattern('stream-iq', query_ns=ns.PRIVACY, iq_type='set'),
        EventPattern('dbus-signal', signal='StatusChanged',
             args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED]))

    assert ("hidden" in conn.Get(cs.CONN_IFACE_SIMPLE_PRESENCE, "Statuses",
                                 dbus_interface=cs.PROPERTIES_IFACE).keys())

    conn.SimplePresence.SetPresence("hidden", "")

    p1, event, p2 = q.expect_many(
        EventPattern('stream-presence', presence_type='unavailable'),
        EventPattern('stream-iq', query_ns=ns.PRIVACY, iq_type='set'),
        EventPattern('stream-presence'))

    active = xpath.queryForNodes('//active', event.query)[0]
    assert (active.compareAttribute('name', 'invisible'))

    conn.SimplePresence.SetPresence("away", "")

    event, p2 = q.expect_many(
        EventPattern('stream-iq', query_ns=ns.PRIVACY, iq_type='set'),
        EventPattern('stream-presence'))

    active = xpath.queryForNodes('//active', event.query)[0]
    assert (not active.compareAttribute('name', 'invisible'))

if __name__ == '__main__':
    exec_test(test, protocol=PrivacyListXmlStream)
    exec_test(test_invisible_on_connect, protocol=PrivacyListXmlStream)
