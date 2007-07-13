
"""
Test support for the HANDLE_OWNERS_NOT_AVAILABLE group flag, and calling
GetHandleOwners on MUC members.

By default, MUC channels should have the flag set. The flag should be unset
when presence is received that includes the MUC JID's owner JID.
"""

import dbus

from twisted.words.xish import domish

from gabbletest import go, make_result_iq
from servicetest import call_async, lazy, match

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

def expect_request_handles_return(event, data):
    assert event.type == 'dbus-return'
    assert event.method == 'RequestHandles'
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
    # Send presence for anonymous other member of room.
    presence = domish.Element((None, 'presence'))
    presence['from'] = 'chat@conf.localhost/bob'
    x = presence.addElement(('http://jabber.org/protocol/muc#user', 'x'))
    item = x.addElement('item')
    item['affiliation'] = 'owner'
    item['role'] = 'moderator'
    data['stream'].send(presence)
    return True

@match('dbus-signal', signal='MembersChanged',
    args=[u'', [3], [], [], [], 0, 0])
def expect_members_changed2(event, data):
    assert data['conn_iface'].InspectHandles(1, [3]) == [
        'chat@conf.localhost/bob']

    # Send presence for nonymous other member of room.
    presence = domish.Element((None, 'presence'))
    presence['from'] = 'chat@conf.localhost/che'
    x = presence.addElement(('http://jabber.org/protocol/muc#user', 'x'))
    item = x.addElement('item')
    item['affiliation'] = 'none'
    item['role'] = 'participant'
    item['jid'] = 'che@foo.com'
    data['stream'].send(presence)
    return True

@match('dbus-signal', signal='MembersChanged')
def expect_members_changed3(event, data):
    assert event.args == [u'', [4], [], [], [], 0, 0]
    assert data['conn_iface'].InspectHandles(1, [4]) == [
        'chat@conf.localhost/che']

    # Send presence for own membership of room.
    presence = domish.Element((None, 'presence'))
    presence['from'] = 'chat@conf.localhost/test'
    x = presence.addElement(('http://jabber.org/protocol/muc#user', 'x'))
    item = x.addElement('item')
    item['affiliation'] = 'none'
    item['role'] = 'participant'
    data['stream'].send(presence)
    return True

@match('dbus-signal', signal='GroupFlagsChanged')
def expect_group_flags_changed(event, data):
    # Since we received MUC presence that contains an owner JID, the
    # OWNERS_NOT_AVAILABLE flag should be removed.
    assert event.args == [0, 1024]
    return True

@match('dbus-return', method='RequestChannel')
def expect_request_channel_return(event, data):
    # Check that GetHandleOwners works.
    bus = data['conn']._bus
    chan = bus.get_object(data['conn']._named_service, event.value[0])
    group = dbus.Interface(chan,
        'org.freedesktop.Telepathy.Channel.Interface.Group')
    assert group.GetHandleOwners([4]) == [5]
    assert data['conn_iface'].InspectHandles(1, [5]) == ['che@foo.com']

    data['conn_iface'].Disconnect()
    return True

@match('dbus-signal', signal='StatusChanged', args=[2, 1])
def expect_disconnected(event, data):
    return True

if __name__ == '__main__':
    go()

