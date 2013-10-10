
"""
Test that <message>s with a chat state notification but no body don't create a
new text channel.
"""

from twisted.words.xish import domish

from gabbletest import exec_test
from servicetest import assertEquals
import constants as cs
import ns

def test(q, bus, conn, stream):
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
    event = q.expect('dbus-signal', signal='NewChannels')
    path, props = event.args[0][0]
    assertEquals(cs.CHANNEL_TYPE_TEXT, props[cs.CHANNEL_TYPE])
    jid = conn.inspect_contact_sync(props[cs.TARGET_HANDLE])
    assertEquals('bob@foo.com', jid)

if __name__ == '__main__':
    exec_test(test)
