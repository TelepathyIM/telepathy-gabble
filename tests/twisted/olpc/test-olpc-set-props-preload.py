
"""
Test connecting to a server.
"""

import dbus
from twisted.words.xish import xpath

from gabbletest import exec_test
import ns

def test(q, bus, conn, stream):
    buddy_info_iface = dbus.Interface(conn, 'org.laptop.Telepathy.BuddyInfo')
    buddy_info_iface.SetProperties({'color': '#ff0000,#0000ff'})

    conn.Connect()

    # buddy activities
    event = q.expect('stream-iq', iq_type='set', query_name='pubsub')
    assert xpath.queryForNodes(
        "/iq[@type='set']/pubsub[@xmlns='%s']"
        "/publish[@node='%s']" % (ns.PUBSUB, ns.OLPC_ACTIVITIES), event.stanza)

    # activity properties
    event = q.expect('stream-iq', iq_type='set', query_name='pubsub')
    assert xpath.queryForNodes(
        "/iq[@type='set']/pubsub[@xmlns='%s']"
        "/publish[@node='%s']" % (ns.PUBSUB, ns.OLPC_ACTIVITY_PROPS),
        event.stanza)

    # buddy properties
    event = q.expect('stream-iq', iq_type='set', query_name='pubsub')
    iq = event.stanza
    nodes = xpath.queryForNodes(
        "/iq[@type='set']/pubsub[@xmlns='%s']"
        "/publish[@node='%s']" % (ns.PUBSUB, ns.OLPC_BUDDY_PROPS), iq)
    assert nodes

    nodes = xpath.queryForNodes(
        "/publish/item"
        "/properties[@xmlns='%s']"
        "/property" % (ns.OLPC_BUDDY_PROPS),
        nodes[0])
    assert len(nodes) == 1
    assert nodes[0]['type'] == 'str'
    assert nodes[0]['name'] == 'color'
    text = str(nodes[0])
    assert text == '#ff0000,#0000ff', text

    iq['type'] = 'result'
    stream.send(iq)

if __name__ == '__main__':
    exec_test(test)
