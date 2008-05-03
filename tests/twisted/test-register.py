
"""
Test registration.
"""

from gabbletest import exec_test, make_result_iq, acknowledge_iq

from twisted.words.xish import xpath

def test(q, bus, conn, stream):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged', args=[1, 1])

    event = q.expect('stream-iq', query_ns='jabber:iq:register')
    result = make_result_iq(stream, event.stanza)
    query = result.firstChildElement()
    query.addElement('username')
    query.addElement('password')
    stream.send(result)

    event = q.expect('stream-iq')
    iq = event.stanza
    assert xpath.queryForString('/iq/query/username', iq) == 'test'
    assert xpath.queryForString('/iq/query/password', iq) == 'pass'
    acknowledge_iq(stream, iq)

    q.expect('dbus-signal', signal='StatusChanged', args=[0, 1])
    conn.Disconnect()
    q.expect('dbus-signal', signal='StatusChanged', args=[2, 1])

if __name__ == '__main__':
    exec_test(test, {'register': True})

