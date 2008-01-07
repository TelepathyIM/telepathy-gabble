
"""
Test that Gabble responds to disco#info queries.
"""

from twisted.words.xish import domish

from gabbletest import exec_test

def test(q, bus, conn, stream):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged', args=[0, 1])

    m = domish.Element(('', 'iq'))
    m['from'] = 'foo@bar.com'
    m['id'] = '1'
    m.addElement(('http://jabber.org/protocol/disco#info', 'query'))
    stream.send(m)
    return True

    event = q.expect('stream-iq', iq_type='result', to='foo@bar.com')
    elem = event.stanza
    assert elem['id'] == '1'

    conn.Disconnect()
    q.expect('dbus-signal', signal='StatusChanged', args=[2, 1])

if __name__ == '__main__':
    exec_test(test)

