
"""
Test workarounds for gtalk
"""

import dbus
import sys

from twisted.words.xish import domish

from servicetest import lazy, match
from gabbletest import go, acknowledge_iq

from twisted.words.protocols.jabber.client import IQ

def make_set_roster_iq(stream, user, contact, state, ask):
    iq = IQ(stream, 'set')
    query = iq.addElement(('jabber:iq:roster', 'query'))
    item = query.addElement('item')
    item['jid'] = contact
    item['subscription'] = state
    if ask:
        item['ask'] = 'subscribe'
    return iq


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

    # send empty roster item
    iq = make_set_roster_iq(data['stream'], 'test@localhost/Resource', item["jid"], "none", False)
    data['stream'].send(iq)
    return True

@match('stream-presence')
def expect_presence(event, data):
    if event.stanza['type'] != 'subscribe':
        return False


    # send pending roster item
    iq = make_set_roster_iq(data['stream'], 'test@localhost/Resource', event.to, "none", True)
    data['stream'].send(iq)

    # First error point, resetting from none+ask:subscribe to none, and back
    # In the real world this triggers bogus 'subscribe authorization rejected' messages

    # send pending roster item
    iq = make_set_roster_iq(data['stream'], 'test@localhost/Resource', event.to, "none", False)
    data['stream'].send(iq)

    # send pending roster item
    iq = make_set_roster_iq(data['stream'], 'test@localhost/Resource', event.to, "none", True)
    data['stream'].send(iq)

    # send accepted roster item
    iq = make_set_roster_iq(data['stream'], 'test@localhost/Resource', event.to, "to", False)
    data['stream'].send(iq)

    presence = domish.Element(('jabber:client', 'presence'))
    presence['from'] = 'bob@foo.com'
    presence['type'] = 'subscribed'
    data['stream'].send(presence)

    # Second error point, demoting from to to none+ask:subscribe, and back
    # In the real world this triggers multiple bogus 'subscribe authorization granted' messages
    # instead of just one

    # send pending roster item
    iq = make_set_roster_iq(data['stream'], 'test@localhost/Resource', event.to, "none", True)
    data['stream'].send(iq)

    # send accepted roster item
    iq = make_set_roster_iq(data['stream'], 'test@localhost/Resource', event.to, "to", False)
    data['stream'].send(iq)

    return True

@match('dbus-signal', signal='MembersChanged',
    args=['', [2], [], [], [], 0, 0])
def expect_members_changed1(event, data):
    assert(event.path.endswith('/known'))
    return True

@match('dbus-signal', signal='MembersChanged',
    args=['', [], [], [], [2], 0, 0])
def expect_members_changed2(event, data):
    assert(event.path.endswith('/subscribe'))
    return True

@match('dbus-signal', signal='MembersChanged',
    args=['', [2], [], [], [], 0, 0])
def expect_members_changed3(event, data):
    assert(event.path.endswith('/subscribe'))
    data['conn_iface'].Disconnect()
    return True

@lazy
@match('dbus-signal', signal='StatusChanged', args=[2, 1])
def expect_disconnected(event, data):
    sys.exit(0)
    return True

# If there's an assertion here, that means we've got a few MembersChanged
# signals too many (either from the first, or second point of error).
@match('dbus-signal', signal='MembersChanged')
def expect_members_changed_evil(event, data):
    assert(False)
    return True

if __name__ == '__main__':
    go()

