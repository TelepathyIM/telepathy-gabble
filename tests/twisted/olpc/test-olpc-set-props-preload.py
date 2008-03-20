
"""
Test connecting to a server.
"""

import dbus
from twisted.words.xish import xpath

from servicetest import match
from gabbletest import go

@match('dbus-signal', signal='StatusChanged', args=[1, 1])
def expect_connecting(event, data):
    return True

@match('stream-authenticated')
def expect_authenticated(event, data):
    return True

@match('dbus-signal', signal='StatusChanged', args=[0, 1])
def expect_connected(event, data):
    return True

@match('stream-iq')
def expect_transmit_properties(event, data):
    iq = event.stanza
    nodes = xpath.queryForNodes(
        "/iq[@type='set']/pubsub[@xmlns='http://jabber.org/protocol/pubsub']"
        "/publish[@node='http://laptop.org/xmpp/buddy-properties']", iq)
    if not nodes:
        return False

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
    data['stream'].send(iq)
    data['conn_iface'].Disconnect()
    return True

@match('dbus-signal', signal='StatusChanged', args=[2, 1])
def expect_disconnected(event, data):
    return True

def start(data):
    data['buddy_info_iface'] = dbus.Interface(data['conn'],
                                              'org.laptop.Telepathy.BuddyInfo')
    data['buddy_info_iface'].SetProperties({'color': '#ff0000,#0000ff'})
    data['conn_iface'].Connect()

if __name__ == '__main__':
    go(start=start)
