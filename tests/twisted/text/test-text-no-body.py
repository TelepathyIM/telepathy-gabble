
"""
Test that <message>s with a chat state notification but no body don't create a
new text channel.
"""

import dbus

from twisted.words.xish import domish

from gabbletest import go

def expect_connected(event, data):
    if event.type != 'dbus-signal':
        return False

    if event.signal != 'StatusChanged':
        return False

    if event.args != [0, 1]:
        return False

    # message without body
    m = domish.Element(('', 'message'))
    m['from'] = 'alice@foo.com'
    m['type'] = 'chat'
    m.addElement(('http://jabber.org/protocol/chatstates', 'composing'))
    data['stream'].send(m)

    # message with body
    m = domish.Element(('', 'message'))
    m['from'] = 'bob@foo.com'
    m['type'] = 'chat'
    m.addElement(('http://jabber.org/protocol/chatstates', 'composing'))
    m.addElement('body', content='hello')
    data['stream'].send(m)
    return True

def expect_new_channel(event, data):
    if event.type != 'dbus-signal':
        return False

    if event.signal != 'NewChannel':
        return False

    if event.args[1] != u'org.freedesktop.Telepathy.Channel.Type.Text':
        return False

    jid = data['conn_iface'].InspectHandles(1, [event.args[3]])[0]
    assert jid == 'bob@foo.com'
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

