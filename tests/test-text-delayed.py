
"""
Test receiving delayed (offline) messages on a text channel.
"""

import datetime

import dbus

from twisted.internet import glib2reactor
glib2reactor.install()

from twisted.words.xish import domish

from gabbletest import conn_iface, go

def expect_connected(event, data):
    if event[0] != 'dbus-signal':
        return False

    if event[2] != 'StatusChanged':
        return False

    if event[3] != [0, 1]:
        return False

    m = domish.Element(('', 'message'))
    m['from'] = 'foo@bar.com'
    m['type'] = 'chat'
    m.addElement('body', content='hello')

    # add timestamp information
    x = m.addElement(('jabber:x:delay', 'x'))
    x['stamp'] = '20070517T16:15:01'

    data['stream'].send(m)
    return True

def expect_new_channel(event, data):
    if event[0] != 'dbus-signal':
        return False

    if event[2] != 'NewChannel':
        return False

    bus = data['conn']._bus
    data['text_chan'] = bus.get_object(
        data['conn']._named_service, event[3][0])

    if event[3][1] != u'org.freedesktop.Telepathy.Channel.Type.Text':
        return False

    # check that handle type == contact handle
    assert event[3][2] == 1

    jid = conn_iface(data['conn']).InspectHandles(1, [event[3][3]])[0]
    assert jid == 'foo@bar.com'
    return True

def expect_conn_received(event, data):
    if event[0] != 'dbus-signal':
        return False

    if event[2] != 'Received':
        return False

    assert (str(datetime.datetime.utcfromtimestamp(event[3][1]))
        == '2007-05-17 16:15:01')
    assert event[3][5] == 'hello'

    data['conn'].Disconnect()
    return True

def expect_disconnected(event, data):
    if event[0] != 'dbus-signal':
        return False

    if event[2] != 'StatusChanged':
        return False

    if event[3] != [2, 1]:
        return False

    return True

if __name__ == '__main__':
    go()

