
"""
Test subscribing to a contact's presence.
"""

import dbus

from twisted.words.xish import domish

from servicetest import lazy, match
from gabbletest import go, acknowledge_iq

@match('stream-iq', query_ns='jabber:iq:roster')
def expect_roster_iq(event, data):
    # send back empty roster
    event.stanza['type'] = 'result'
    data['stream'].send(event.stanza)
    return True

@match('dbus-signal', signal='NewChannel')
def expect_new_channel(event, data):
    path, type, handle_type, handle, suppress_handler = event.args

    if type != u'org.freedesktop.Telepathy.Channel.Type.ContactList':
        return False

    chan_name = data['conn_iface'].InspectHandles(handle_type, [handle])[0]

    if chan_name != 'subscribe':
        return False

    # request subscription
    chan = data['conn']._bus.get_object(data['conn']._named_service, path)
    group_iface = dbus.Interface(chan,
        u'org.freedesktop.Telepathy.Channel.Interface.Group')
    assert group_iface.GetMembers() == []
    handle = data['conn_iface'].RequestHandles(1, ['bob@foo.com'])[0]
    group_iface.AddMembers([handle], '')
    return True

@match('stream-iq', iq_type='set', query_ns='jabber:iq:roster')
def expect_roster_set_iq(event, data):
    item = event.query.firstChildElement()
    assert item["jid"] == 'bob@foo.com'

    acknowledge_iq(data['stream'], event.stanza)
    return True

@match('stream-presence')
def expect_presence(event, data):
    if event.stanza['type'] != 'subscribe':
        return False

    presence = domish.Element(('jabber:client', 'presence'))
    presence['from'] = 'bob@foo.com'
    presence['type'] = 'subscribed'
    data['stream'].send(presence)
    return True

@lazy
@match('dbus-signal', signal='MembersChanged',
    args=['', [2], [], [], [], 0, 0])
def expect_members_changed(event, data):
    return True

@match('stream-presence')
def expect_presence_ack(event, data):
    data['conn_iface'].Disconnect()
    return True

@match('dbus-signal', signal='StatusChanged', args=[2, 1])
def expect_disconnected(event, data):
    return True

if __name__ == '__main__':
    go()

