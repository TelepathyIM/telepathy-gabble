
"""
Test support for aliases in MUCs.
"""

import dbus

from twisted.words.xish import domish

from gabbletest import go, make_result_iq
from servicetest import call_async, lazy, match, tp_name_prefix

def aliasing_iface(proxy):
    return dbus.Interface(proxy, tp_name_prefix +
        '.Connection.Interface.Aliasing')

@match('dbus-signal', signal='StatusChanged', args=[0, 1])
def expect_connected(event, data):
    # Need to call this asynchronously as it involves Gabble sending us a
    # query.
    call_async(data['test'], data['conn_iface'], 'RequestHandles', 2,
        ['chat@conf.localhost'])
    return True

@match('stream-iq', to='conf.localhost',
    query_ns='http://jabber.org/protocol/disco#info')
def expect_disco(event, data):
    result = make_result_iq(data['stream'], event.stanza)
    feature = result.firstChildElement().addElement('feature')
    feature['var'] = 'http://jabber.org/protocol/muc'
    data['stream'].send(result)
    return True

@match('dbus-return', method='RequestHandles')
def expect_request_handles_return(event, data):
    handles = event.value[0]

    call_async(data['test'], data['conn_iface'], 'RequestChannel',
        'org.freedesktop.Telepathy.Channel.Type.Text', 2, handles[0], True)
    return True

@lazy
@match('dbus-signal', signal='MembersChanged',
    args=[u'', [], [], [], [2], 0, 0])
def expect_members_changed1(event, data):
    return True

@match('stream-presence', to='chat@conf.localhost/test')
def expect_presence(event, data):
    # Send presence for other member of room.
    presence = domish.Element((None, 'presence'))
    presence['from'] = 'chat@conf.localhost/bob'
    x = presence.addElement(('http://jabber.org/protocol/muc#user', 'x'))
    item = x.addElement('item')
    item['affiliation'] = 'owner'
    item['role'] = 'moderator'
    data['stream'].send(presence)

    # Send presence for own membership of room.
    presence = domish.Element((None, 'presence'))
    presence['from'] = 'chat@conf.localhost/test'
    x = presence.addElement(('http://jabber.org/protocol/muc#user', 'x'))
    item = x.addElement('item')
    item['affiliation'] = 'none'
    item['role'] = 'participant'
    data['stream'].send(presence)
    return True

@match('dbus-signal', signal='MembersChanged',
    args=[u'', [2, 3], [], [], [], 0, 0])
def expect_members_changed2(event, data):
    assert data['conn_iface'].InspectHandles(1, [2]) == [
        'chat@conf.localhost/test']
    assert data['conn_iface'].InspectHandles(1, [3]) == [
        'chat@conf.localhost/bob']

    return True

@match('dbus-return', method='RequestChannel')
def expect_request_channel_return(event, data):
    assert aliasing_iface(data['conn']).RequestAliases([3]) == ['bob']
    data['conn_iface'].Disconnect()
    return True

@match('dbus-signal', signal='StatusChanged', args=[2, 1])
def expect_disconnected(event, data):
    return True

if __name__ == '__main__':
    go()

