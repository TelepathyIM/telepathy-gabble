
"""
Test GetAvatarTokens() and GetKnownAvatarTokens().
"""

import dbus

from servicetest import tp_name_prefix, lazy, match, unwrap
from gabbletest import go
from twisted.words.xish import domish
import time

@lazy
@match('dbus-signal', signal='StatusChanged', args=[0, 1])
def expect_connected(event, data):
    return True

def avatars_iface(proxy):
    return dbus.Interface(proxy, tp_name_prefix +
        '.Connection.Interface.Avatars')

def make_presence(jid, sha1sum):
    p = domish.Element((None, 'presence'))
    p['from'] = jid
    p['to'] = 'test@localhost/Resource'
    x = domish.Element(('vcard-temp:x:update', 'x'))
    p.addChild(x)
    x.addElement('photo', content=sha1sum)
    return p

@match('stream-iq', query_ns='jabber:iq:roster')
def expect_roster_iq(event, data):
    event.stanza['type'] = 'result'
    item = event.query.addElement('item')
    item['jid'] = 'amy@foo.com'
    item['subscription'] = 'both'

    item = event.query.addElement('item')
    item['jid'] = 'bob@foo.com'
    item['subscription'] = 'both'

    item = event.query.addElement('item')
    item['jid'] = 'che@foo.com'
    item['subscription'] = 'both'

    data['stream'].send(event.stanza)

    data['stream'].send(make_presence('amy@foo.com', 'SHA1SUM-FOR-AMY'))
    data['stream'].send(make_presence('bob@foo.com', 'SHA1SUM-FOR-BOB'))
    data['stream'].send(make_presence('che@foo.com', None))

    return True

@match('dbus-signal', signal='AvatarUpdated')
def expect_avatar_updated(event, data):
    handles = data['conn_iface'].RequestHandles(1, [
        'amy@foo.com', 'bob@foo.com', 'che@foo.com', 'daf@foo.com' ])

    data['avatars_iface'] = avatars_iface(data['conn'])
    tokens = unwrap(data['avatars_iface'].GetAvatarTokens(handles))

    assert tokens == ['SHA1SUM-FOR-AMY', 'SHA1SUM-FOR-BOB', '', '']

    tokens = unwrap(data['avatars_iface'].GetKnownAvatarTokens(handles))
    tokens = list(tokens.items())
    tokens.sort()
    assert tokens == [(2, 'SHA1SUM-FOR-AMY'), (3, 'SHA1SUM-FOR-BOB'), (4, u'')]

    data['conn_iface'].Disconnect()
    return True


@match('dbus-signal', signal='StatusChanged', args=[2, 1])
def expect_disconnected(event, data):
    return True

if __name__ == '__main__':
    go()


