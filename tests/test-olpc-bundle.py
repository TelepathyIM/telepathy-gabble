"""test OLPC bundle. We shouldn't announce OLPC features until we use the OLPC
interface"""
import base64
import errno
import os

import dbus

from servicetest import call_async, EventPattern
from gabbletest import exec_test, make_result_iq, acknowledge_iq

from twisted.words.xish import domish, xpath

NS_OLPC_BUDDY_PROPS_NOTIFY = "http://laptop.org/xmpp/buddy-properties+notify"
NS_OLPC_ACTIVITIES_NOTIFY = "http://laptop.org/xmpp/activities+notify"
NS_OLPC_CURRENT_ACTIVITY_NOTIFY = "http://laptop.org/xmpp/current-activity+notify"
NS_OLPC_ACTIVITY_PROPS_NOTIFY = "http://laptop.org/xmpp/activity-properties+notify"

olpc_features = set([NS_OLPC_BUDDY_PROPS_NOTIFY, NS_OLPC_ACTIVITIES_NOTIFY,
        NS_OLPC_CURRENT_ACTIVITY_NOTIFY, NS_OLPC_ACTIVITY_PROPS_NOTIFY])

def test(q, bus, conn, stream):
    conn.Connect()

    _, iq_event = q.expect_many(
        EventPattern('dbus-signal', signal='StatusChanged', args=[0, 1]),
        EventPattern('stream-iq', to=None, query_ns='vcard-temp',
            query_name='vCard'))

    acknowledge_iq(stream, iq_event.stanza)

    # send diso request
    m = domish.Element(('', 'iq'))
    m['from'] = 'alice@jabber.laptop.org'
    m['id'] = '1'
    query = m.addElement('query')
    query['xmlns'] = 'http://jabber.org/protocol/disco#info'
    stream.send(m)

    # wait for disco response
    event = q.expect('stream-iq', iq_type='result',
            query_ns='http://jabber.org/protocol/disco#info',
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

    c_node = c_nodes[0]
    assert c_node['ext'] == 'olpc'

    # send diso request
    m = domish.Element(('', 'iq'))
    m['from'] = 'alice@jabber.laptop.org'
    m['id'] = '2'
    query = m.addElement('query')
    query['xmlns'] = 'http://jabber.org/protocol/disco#info'
    stream.send(m)

    # wait for disco response
    event = q.expect('stream-iq', iq_type='result',
        query_ns='http://jabber.org/protocol/disco#info',
        to='alice@jabber.laptop.org')
    assert event.stanza['id'] == '2'

    # OLPC NS are now announced
    features = set([str(f['var']) for f in xpath.queryForNodes('/iq/query/feature',
        event.stanza)])

    assert olpc_features.issubset(features)

if __name__ == '__main__':
    exec_test(test)
