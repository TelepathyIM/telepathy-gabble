
"""
Test that Gabble responds to disco#info queries.
"""

from twisted.words.xish import domish

from gabbletest import exec_test
import constants as cs

def test(q, bus, conn, stream):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])

    m = domish.Element((None, 'iq'))
    m['from'] = 'foo@bar.com'
    m['id'] = '1'
    m['type'] = 'get'
    m.addElement(('http://jabber.org/protocol/disco#info', 'query'))
    stream.send(m)

    event = q.expect('stream-iq', iq_type='result', to='foo@bar.com')
    elem = event.stanza
    assert elem['id'] == '1'

if __name__ == '__main__':
    exec_test(test)
