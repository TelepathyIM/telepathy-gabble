
"""
Test receiving delayed (offline) messages on a text channel.
"""

import datetime

from twisted.words.xish import domish

from gabbletest import exec_test
from servicetest import EventPattern
import constants as cs

def test(q, bus, conn, stream):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])

    m = domish.Element((None, 'message'))
    m['from'] = 'foo@bar.com'
    m['type'] = 'chat'
    m.addElement('body', content='hello')

    # add timestamp information
    x = m.addElement(('jabber:x:delay', 'x'))
    x['stamp'] = '20070517T16:15:01'

    stream.send(m)

    event = q.expect('dbus-signal', signal='NewChannel')
    assert event.args[1] == cs.CHANNEL_TYPE_TEXT
    assert event.args[2] == cs.HT_CONTACT
    jid = conn.InspectHandles(cs.HT_CONTACT, [event.args[3]])[0]
    assert jid == 'foo@bar.com'

    received, message_received = q.expect_many(
        EventPattern('dbus-signal', signal='Received'),
        EventPattern('dbus-signal', signal='MessageReceived'),
        )

    assert (str(datetime.datetime.utcfromtimestamp(received.args[1]))
        == '2007-05-17 16:15:01')
    assert received.args[5] == 'hello'

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
