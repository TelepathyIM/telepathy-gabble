
"""
Test connecting to a server.
"""

import dbus
from twisted.words.xish import xpath

from servicetest import EventPattern
from gabbletest import go, exec_test

def test(q, bus, conn, stream):
    buddy_info_iface = dbus.Interface(conn, 'org.laptop.Telepathy.BuddyInfo')
    buddy_info_iface.SetProperties({'color': '#ff0000,#0000ff'})

    conn.Connect()
    # q.expect('dbus-signal', signal='StatusChanged', args=[0, 1])

    # buddy activities
    event = q.expect('stream-iq', iq_type='set', query_name='pubsub')

    # activity properties
    event = q.expect('stream-iq', iq_type='set', query_name='pubsub')

    # buddy properties
    event = q.expect('stream-iq', iq_type='set', query_name='pubsub')
    iq = event.stanza
    nodes = xpath.queryForNodes(
        "/iq[@type='set']/pubsub[@xmlns='http://jabber.org/protocol/pubsub']"
        "/publish[@node='http://laptop.org/xmpp/buddy-properties']", iq)
    assert nodes

    nodes = xpath.queryForNodes(
        "/publish/item"
        "/properties[@xmlns='http://laptop.org/xmpp/buddy-properties']"
        "/property",
        nodes[0])
    assert len(nodes) == 1
    assert nodes[0]['type'] == 'str'
    assert nodes[0]['name'] == 'color'
    text = str(nodes[0])
    assert text == '#ff0000,#0000ff', text

    iq['type'] = 'result'
    stream.send(iq)
    conn.Disconnect()

if __name__ == '__main__':
    exec_test(test)
