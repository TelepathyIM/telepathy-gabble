
"""
Test receiving delayed (offline) messages on a text channel.
"""

import datetime

from twisted.words.xish import domish

from gabbletest import exec_test
from servicetest import assertEquals
import constants as cs

def test(q, bus, conn, stream):
    m = domish.Element((None, 'message'))
    m['from'] = 'foo@bar.com'
    m['type'] = 'chat'
    m.addElement('body', content='hello')

    # add timestamp information
    x = m.addElement(('jabber:x:delay', 'x'))
    x['stamp'] = '20070517T16:15:01'

    stream.send(m)

    event = q.expect('dbus-signal', signal='NewChannel')
    path, props = event.args
    assertEquals(cs.CHANNEL_TYPE_TEXT, props[cs.CHANNEL_TYPE])
    assertEquals(cs.HT_CONTACT, props[cs.TARGET_HANDLE_TYPE])
    jid = conn.inspect_contact_sync(props[cs.TARGET_HANDLE])
    assertEquals('foo@bar.com', jid)

    message_received = q.expect('dbus-signal', signal='MessageReceived')

    message = message_received.args[0]
    header = message[0]
    message_sent_timestamp = header['message-sent']
    assert str(datetime.datetime.utcfromtimestamp(message_sent_timestamp)
        == '2007-05-17 16:15:01'), header
    message_received_timestamp = header['message-received']
    assert message_received_timestamp > message_sent_timestamp, header

    assert message[1]['content'] == 'hello', message

if __name__ == '__main__':
    exec_test(test)
