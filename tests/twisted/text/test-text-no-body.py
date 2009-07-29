
"""
Test that <message>s with a chat state notification but no body don't create a
new text channel.
"""

from twisted.words.xish import domish

from gabbletest import exec_test
import constants as cs
import ns

def test(q, bus, conn, stream):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])

    # message without body
    m = domish.Element((None, 'message'))
    m['from'] = 'alice@foo.com'
    m['type'] = 'chat'
    m.addElement((ns.CHAT_STATES, 'composing'))
    stream.send(m)

    # message with body
    m = domish.Element((None, 'message'))
    m['from'] = 'bob@foo.com'
    m['type'] = 'chat'
    m.addElement((ns.CHAT_STATES, 'active'))
    m.addElement('body', content='hello')
    stream.send(m)

    # first message should be from Bob, not Alice
    event = q.expect('dbus-signal', signal='NewChannel')
    assert event.args[1] == cs.CHANNEL_TYPE_TEXT
    jid = conn.InspectHandles(cs.HT_CONTACT, [event.args[3]])[0]
    assert jid == 'bob@foo.com'

if __name__ == '__main__':
    exec_test(test)
