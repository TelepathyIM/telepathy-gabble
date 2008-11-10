
"""
Test that <message>s with a chat state notification but no body don't create a
new text channel.
"""

from twisted.words.xish import domish

from gabbletest import exec_test

import ns

def test(q, bus, conn, stream):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged', args=[0, 1])

    # message without body
    m = domish.Element(('', 'message'))
    m['from'] = 'alice@foo.com'
    m['type'] = 'chat'
    m.addElement((ns.CHAT_STATES, 'composing'))
    stream.send(m)

    # message with body
    m = domish.Element(('', 'message'))
    m['from'] = 'bob@foo.com'
    m['type'] = 'chat'
    m.addElement((ns.CHAT_STATES, 'active'))
    m.addElement('body', content='hello')
    stream.send(m)

    # first message should be from Bob, not Alice
    event = q.expect('dbus-signal', signal='NewChannel')
    assert event.args[1] == u'org.freedesktop.Telepathy.Channel.Type.Text'
    jid = conn.InspectHandles(1, [event.args[3]])[0]
    assert jid == 'bob@foo.com'
    conn.Disconnect()
    q.expect('dbus-signal', signal='StatusChanged', args=[2, 1])

if __name__ == '__main__':
    exec_test(test)

