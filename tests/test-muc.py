
"""
Test MUC support.
"""

import dbus

from twisted.words.xish import domish, xpath

from gabbletest import go
from servicetest import call_async, lazy

def expect_connected(event, data):
    if event[0] != 'dbus-signal':
        return False

    if event[2] != 'StatusChanged':
        return False

    if event[3] != [0, 1]:
        return False

    # Need to call this asynchronously as it involves Gabble sending us a
    # query.
    call_async(data['handler'], data['conn_iface'], 'RequestHandles', 2,
        ['chat@conf.localhost'])
    return True

def expect_disco(event, data):
    if event[0] != 'stream-iq':
        return False

    iq = event[1]
    nodes = xpath.queryForNodes(
        "/iq/query[@xmlns='http://jabber.org/protocol/disco#info']", iq)

    if not nodes:
        return False

    assert iq['to'] == 'conf.localhost'

    query = nodes[0]
    feature = query.addElement('feature')
    feature['var'] = 'http://jabber.org/protocol/muc'

    iq['type'] = 'result'
    data['stream'].send(iq)
    return True

def expect_request_handles_return(event, data):
    assert event[0] == 'dbus-return'
    assert event[1] == 'RequestHandles'
    handles = event[2]

    call_async(data['handler'], data['conn_iface'], 'RequestChannel',
        'org.freedesktop.Telepathy.Channel.Type.Text', 2, handles[0], True)
    return True

@lazy
def expect_members_changed1(event, data):
    if event[0] != 'dbus-signal':
        return False

    if event[2] != 'MembersChanged':
        return False

    assert event[3] == [u'', [], [], [], [2], 0, 0]
    return True

def expect_presence(event, data):
    if event[0] != 'stream-presence':
        return False

    assert event[1]['to'] == 'chat@conf.localhost/test'

    # Send presence for other member of room.
    presence = domish.Element((None, 'presence'))
    presence['from'] = 'chat@conf.localhost/bob'
    x = presence.addElement(('http://jabber.org/protocol/muc#user', 'x'))
    item = x.addElement('item')
    item['affiliation'] = 'owner'
    item['role'] = 'moderator'
    data['stream'].send(presence)
    return True

def expect_members_changed2(event, data):
    if event[0] != 'dbus-signal':
        return False

    if event[2] != 'MembersChanged':
        return False

    assert event[3] == [u'', [3], [], [], [], 0, 0]
    assert data['conn_iface'].InspectHandles(1, [3]) == [
        'chat@conf.localhost/bob']

    # Send presence for own membership of room.
    presence = domish.Element((None, 'presence'))
    presence['from'] = 'chat@conf.localhost/test'
    x = presence.addElement(('http://jabber.org/protocol/muc#user', 'x'))
    item = x.addElement('item')
    item['affiliation'] = 'none'
    item['role'] = 'participant'
    data['stream'].send(presence)
    return True

def expect_request_channel_return(event, data):
    if event[0] != 'dbus-return':
        return False

    assert event[1] == 'RequestChannel'

    bus = data['conn']._bus
    data['text_chan'] = bus.get_object(
        data['conn']._named_service, event[2])

    message = domish.Element((None, 'message'))
    message['from'] = 'chat@conf.localhost/bob'
    message['type'] = 'groupchat'
    body = message.addElement('body', content='hello')
    data['stream'].send(message)
    return True

def expect_received(event, data):
    if event[0] != 'dbus-signal':
        return False

    if event[2] != 'Received':
        return False

    # sender: bob
    assert event[3][2] == 3
    # message type: normal
    assert event[3][3] == 0
    # flags: none
    assert event[3][4] == 0
    # body
    assert event[3][5] == 'hello'

    dbus.Interface(data['text_chan'],
        u'org.freedesktop.Telepathy.Channel.Type.Text').Send(0, 'goodbye')
    return True

def expect_message(event, data):
    if event[0] != 'stream-message':
        return False

    elem = event[1]
    assert elem.name == 'message'
    assert elem['type'] == 'groupchat'
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

