"""test OLPC bundle. We shouldn't announce OLPC features until we use the OLPC
interface"""
import dbus

from servicetest import call_async, EventPattern
from gabbletest import exec_test, acknowledge_iq

from twisted.words.xish import domish, xpath
import ns
import constants as cs

olpc_features = set([ns.OLPC_BUDDY_PROPS_NOTIFY, ns.OLPC_ACTIVITIES_NOTIFY,
        ns.OLPC_CURRENT_ACTIVITY_NOTIFY, ns.OLPC_ACTIVITY_PROPS_NOTIFY])

def test(q, bus, conn, stream):
    conn.Connect()

    _, iq_event = q.expect_many(
        EventPattern('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED]),
        EventPattern('stream-iq', to=None, query_ns='vcard-temp',
            query_name='vCard'))

    acknowledge_iq(stream, iq_event.stanza)

    # send diso request
    m = domish.Element((None, 'iq'))
    m['from'] = 'alice@jabber.laptop.org'
    m['id'] = '1'
    m['type'] = 'get'
    query = m.addElement('query')
    query['xmlns'] = ns.DISCO_INFO
    stream.send(m)

    # wait for disco response
    event = q.expect('stream-iq', iq_type='result',
            query_ns=ns.DISCO_INFO,
            to='alice@jabber.laptop.org')

    features = set([str(f['var']) for f in xpath.queryForNodes('/iq/query/feature',
        event.stanza)])

    # OLPC NS aren't announced
    assert len(olpc_features.intersection(features)) == 0

    # Use OLPC interface
    buddy_info_iface = dbus.Interface(conn, 'org.laptop.Telepathy.BuddyInfo')
    call_async(q, buddy_info_iface, 'SetProperties',
            {'color': '#ff0000,#0000ff'})

    # wait for <presence> stanza
    event = q.expect('stream-presence')
    c_nodes = xpath.queryForNodes('/presence/c', event.stanza)
    assert c_nodes is not None
    assert len(c_nodes) == 1

    # send diso request
    m = domish.Element((None, 'iq'))
    m['from'] = 'alice@jabber.laptop.org'
    m['id'] = '2'
    m['type'] = 'get'
    query = m.addElement('query')
    query['xmlns'] = ns.DISCO_INFO
    stream.send(m)

    # wait for disco response
    event = q.expect('stream-iq', iq_type='result',
        query_ns=ns.DISCO_INFO,
        to='alice@jabber.laptop.org')
    assert event.stanza['id'] == '2'

    # OLPC NS are now announced
    features = set([str(f['var']) for f in xpath.queryForNodes('/iq/query/feature',
        event.stanza)])

    assert olpc_features.issubset(features)

if __name__ == '__main__':
    exec_test(test)
