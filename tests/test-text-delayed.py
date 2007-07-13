
"""
Test receiving delayed (offline) messages on a text channel.
"""

import datetime

from twisted.words.xish import domish

from gabbletest import go

def expect_connected(event, data):
    if event.type != 'dbus-signal':
        return False

    if event.signal != 'StatusChanged':
        return False

    if event.args != [0, 1]:
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
    if event.type != 'dbus-signal':
        return False

    if event.signal != 'NewChannel':
        return False

    bus = data['conn']._bus
    data['text_chan'] = bus.get_object(
        data['conn']._named_service, event.args[0])

    if event.args[1] != u'org.freedesktop.Telepathy.Channel.Type.Text':
        return False

    # check that handle type == contact handle
    assert event.args[2] == 1

    jid = data['conn_iface'].InspectHandles(1, [event.args[3]])[0]
    assert jid == 'foo@bar.com'
    return True

def expect_conn_received(event, data):
    if event.type != 'dbus-signal':
        return False

    if event.signal != 'Received':
        return False

    assert (str(datetime.datetime.utcfromtimestamp(event.args[1]))
        == '2007-05-17 16:15:01')
    assert event.args[5] == 'hello'

    data['conn_iface'].Disconnect()
    return True

def expect_disconnected(event, data):
    if event.type != 'dbus-signal':
        return False

    if event.signal != 'StatusChanged':
        return False

    if event.args != [2, 1]:
        return False

    return True

if __name__ == '__main__':
    go()

