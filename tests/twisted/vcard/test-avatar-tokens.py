
"""
Test GetAvatarTokens() and GetKnownAvatarTokens().
"""

from twisted.words.xish import domish

from servicetest import unwrap, EventPattern
from gabbletest import exec_test, make_result_iq
import ns
import constants as cs

def make_presence(jid, sha1sum):
    p = domish.Element((None, 'presence'))
    p['from'] = jid
    p['to'] = 'test@localhost/Resource'
    x = p.addElement((ns.VCARD_TEMP_UPDATE, 'x'))
    x.addElement('photo', content=sha1sum)
    return p

def test(q, bus, conn, stream):
    conn.Connect()
    _, event = q.expect_many(
        EventPattern('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED]),
        EventPattern('stream-iq', to=None, query_ns=ns.ROSTER,
            query_name='query'))

    result = make_result_iq(stream, event.stanza)
    item = result.addElement('item')
    item['jid'] = 'amy@foo.com'
    item['subscription'] = 'both'

    item = result.addElement('item')
    item['jid'] = 'bob@foo.com'
    item['subscription'] = 'both'

    item = result.addElement('item')
    item['jid'] = 'che@foo.com'
    item['subscription'] = 'both'
    stream.send(result)

    stream.send(make_presence('amy@foo.com', 'SHA1SUM-FOR-AMY'))
    stream.send(make_presence('bob@foo.com', 'SHA1SUM-FOR-BOB'))
    stream.send(make_presence('che@foo.com', None))

    q.expect('dbus-signal', signal='AvatarUpdated')
    handles = conn.RequestHandles(1, [
        'amy@foo.com', 'bob@foo.com', 'che@foo.com', 'daf@foo.com' ])

    tokens = unwrap(conn.Avatars.GetAvatarTokens(handles))
    assert tokens == ['SHA1SUM-FOR-AMY', 'SHA1SUM-FOR-BOB', '', '']

    tokens = unwrap(conn.Avatars.GetKnownAvatarTokens(handles))
    tokens = sorted(tokens.items())
    assert tokens == [(2, 'SHA1SUM-FOR-AMY'), (3, 'SHA1SUM-FOR-BOB'), (4, u'')]

if __name__ == '__main__':
    exec_test(test)
