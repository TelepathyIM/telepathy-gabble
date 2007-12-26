
"""
Test receiving delayed (offline) messages on a text channel.
"""

import datetime

from twisted.words.xish import domish

from gabbletest import exec_test

def test(q, bus, conn, stream):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged', args=[0, 1])

    m = domish.Element(('', 'message'))
    m['from'] = 'foo@bar.com'
    m['type'] = 'chat'
    m.addElement('body', content='hello')

    # add timestamp information
    x = m.addElement(('jabber:x:delay', 'x'))
    x['stamp'] = '20070517T16:15:01'

    stream.send(m)

    event = q.expect('dbus-signal', signal='NewChannel')
    assert event.args[1] == u'org.freedesktop.Telepathy.Channel.Type.Text'
    # check that handle type == contact handle
    assert event.args[2] == 1
    jid = conn.InspectHandles(1, [event.args[3]])[0]
    assert jid == 'foo@bar.com'

    event = q.expect('dbus-signal', signal='Received')
    assert (str(datetime.datetime.utcfromtimestamp(event.args[1]))
        == '2007-05-17 16:15:01')
    assert event.args[5] == 'hello'

    conn.Disconnect()
    q.expect('dbus-signal', signal='StatusChanged', args=[2, 1])

if __name__ == '__main__':
    exec_test(test)

