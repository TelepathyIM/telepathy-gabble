
"""
Test text channel.
"""

import dbus

from twisted.words.xish import domish

from gabbletest import go

def expect_connected(event, data):
    if event[0] != 'dbus-signal':
        return False

    if event[2] != 'StatusChanged':
        return False

    if event[3] != [0, 1]:
        return False

    # <message type="chat"><body>hello</body</message>
    m = domish.Element(('', 'message'))
    m['from'] = 'foo@bar.com'
    m['type'] = 'chat'
    m.addElement('body', content='hello')
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

    jid = data['conn_iface'].InspectHandles(1, [event[3][3]])[0]
    assert jid == 'foo@bar.com'
    return True

def expect_conn_received(event, data):
    if event[0] != 'dbus-signal':
        return False

    if event[2] != 'Received':
        return False

    # message type: normal
    assert event[3][3] == 0
    # flags: none
    assert event[3][4] == 0
    # body
    assert event[3][5] == 'hello'

    dbus.Interface(data['text_chan'],
        u'org.freedesktop.Telepathy.Channel.Type.Text').Send(0, 'goodbye')
    return True

def expect_srv_received(event, data):
    if event[0] != 'stream-message':
        return False

    elem = event[1]
    assert elem.name == 'message'
    assert elem['type'] == 'chat'
    body = list(event[1].elements())[0]
    assert body.name == 'body'
    assert body.children[0] == u'goodbye'

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

