"""
A simple smoke-test for C.I.SimplePresence

FIXME: test C.I.Presence too
"""

from twisted.words.xish import domish

from gabbletest import exec_test

def test(q, bus, conn, stream):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged', args=[0, 1])

    amy_handle = conn.RequestHandles(1, ['amy@foo.com'])[0]

    event = q.expect('stream-iq', query_ns='jabber:iq:roster')
    event.stanza['type'] = 'result'

    item = event.query.addElement('item')
    item['jid'] = 'amy@foo.com'
    item['subscription'] = 'both'

    stream.send(event.stanza)

    presence = domish.Element((None, 'presence'))
    presence['from'] = 'amy@foo.com'
    show = presence.addElement((None, 'show'))
    show.addContent('away')
    status = presence.addElement((None, 'status'))
    status.addContent('At the pub')
    stream.send(presence)

    event = q.expect('dbus-signal', signal='PresencesChanged')
    assert event.args[0] == { amy_handle: (3, 'away', 'At the pub') }

    presence = domish.Element((None, 'presence'))
    presence['from'] = 'amy@foo.com'
    show = presence.addElement((None, 'show'))
    show.addContent('chat')
    status = presence.addElement((None, 'status'))
    status.addContent('I may have been drinking')
    stream.send(presence)

    event = q.expect('dbus-signal', signal='PresencesChanged')
    assert event.args[0] == { amy_handle: (2, 'chat', 'I may have been drinking') }

    conn.Disconnect()
    q.expect('dbus-signal', signal='StatusChanged', args=[2, 1])

if __name__ == '__main__':
    exec_test(test)

