
"""
Test MUC support.
"""

import dbus

from twisted.words.xish import domish, xpath

from gabbletest import go
from servicetest import call_async, lazy, match

@match('dbus-signal', signal='StatusChanged', args=[0, 1])
def expect_connected(event, data):
    # Need to call this asynchronously as it involves Gabble sending us a
    # query.
    call_async(data['test'], data['conn_iface'], 'RequestHandles', 2,
        ['chat@conf.localhost'])
    return True

@match('stream-iq', to='conf.localhost',
    query_ns='http://jabber.org/protocol/disco#info')
def expect_disco(event, data):
    feature = event.query.addElement('feature')
    feature['var'] = 'http://jabber.org/protocol/muc'

    event.stanza['type'] = 'result'
    data['stream'].send(event.stanza)
    return True

def expect_request_handles_return(event, data):
    assert event.type == 'dbus-return'
    assert event.method == 'RequestHandles'
    handles = event.value[0]

    call_async(data['test'], data['conn_iface'], 'RequestChannel',
        'org.freedesktop.Telepathy.Channel.Type.Text', 2, handles[0], True)
    return True

@lazy
@match('dbus-signal', signal='MembersChanged')
def expect_members_changed1(event, data):
    assert event.args == [u'', [], [], [], [2], 0, 0]
    return True

@match('stream-presence')
def expect_presence(event, data):
    assert event.stanza['to'] == 'chat@conf.localhost/test'

    # Send presence for other member of room.
    presence = domish.Element((None, 'presence'))
    presence['from'] = 'chat@conf.localhost/bob'
    x = presence.addElement(('http://jabber.org/protocol/muc#user', 'x'))
    item = x.addElement('item')
    item['affiliation'] = 'owner'
    item['role'] = 'moderator'
    data['stream'].send(presence)
    return True

@match('dbus-signal', signal='MembersChanged')
def expect_members_changed2(event, data):
    assert event.args == [u'', [3], [], [], [], 0, 0]
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

@match('dbus-return', method='RequestChannel')
def expect_request_channel_return(event, data):
    bus = data['conn']._bus
    data['text_chan'] = bus.get_object(
        data['conn']._named_service, event.value[0])

    message = domish.Element((None, 'message'))
    message['from'] = 'chat@conf.localhost/bob'
    message['type'] = 'groupchat'
    body = message.addElement('body', content='hello')
    data['stream'].send(message)
    return True

@match('dbus-signal', signal='Received')
def expect_received(event, data):
    # sender: bob
    assert event.args[2] == 3
    # message type: normal
    assert event.args[3] == 0
    # flags: none
    assert event.args[4] == 0
    # body
    assert event.args[5] == 'hello'

    dbus.Interface(data['text_chan'],
        u'org.freedesktop.Telepathy.Channel.Type.Text').Send(0, 'goodbye')
    return True

@match('stream-message')
def expect_message(event, data):
    elem = event.stanza
    assert elem.name == 'message'
    assert elem['type'] == 'groupchat'
    body = list(event.stanza.elements())[0]
    assert body.name == 'body'
    assert body.children[0] == u'goodbye'

    data['conn'].Disconnect()
    return True

@match('dbus-signal', signal='StatusChanged', args=[2, 1])
def expect_disconnected(event, data):
    return True

if __name__ == '__main__':
    go()

