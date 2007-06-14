
"""
Test basic roster functionality.
"""

import dbus

from twisted.words.xish import xpath

from servicetest import lazy, match
from gabbletest import go

@lazy
@match('dbus-signal', signal='StatusChanged', args=[0, 1])
def expect_connected(event, data):
    return True

@match('stream-iq', query_ns='jabber:iq:roster')
def expect_roster_iq(event, data):
    event.stanza['type'] = 'result'
    item = event.query.addElement('item')
    item['jid'] = 'amy@foo.com'
    item['subscription'] = 'both'

    item = event.query.addElement('item')
    item['jid'] = 'bob@foo.com'
    item['subscription'] = 'from'

    item = event.query.addElement('item')
    item['jid'] = 'che@foo.com'
    item['subscription'] = 'to'

    data['stream'].send(event.stanza)
    return True

@match('dbus-signal', signal='NewChannel')
def _expect_contact_list_channel(event, data, name, contacts):
    path, type, handle_type, handle, suppress_handler = event.args

    if type != u'org.freedesktop.Telepathy.Channel.Type.ContactList':
        return False

    chan_name = data['conn_iface'].InspectHandles(handle_type, [handle])[0]
    assert chan_name == name
    chan = data['conn']._bus.get_object(data['conn']._named_service, path)
    group_iface = dbus.Interface(chan,
        u'org.freedesktop.Telepathy.Channel.Interface.Group')
    chan_contacts = data['conn_iface'].InspectHandles(1,
        group_iface.GetMembers())
    assert chan_contacts == contacts
    return True

def expect_contact_list_publish(event, data):
    return _expect_contact_list_channel(event, data, 'publish',
        ['amy@foo.com', 'bob@foo.com'])

def expect_contact_list_subscribe(event, data):
    return _expect_contact_list_channel(event, data, 'subscribe',
        ['amy@foo.com', 'che@foo.com'])

def expect_contact_list_known(event, data):
    if _expect_contact_list_channel(event, data, 'known',
            ['amy@foo.com', 'bob@foo.com', 'che@foo.com']):
        data['conn_iface'].Disconnect()
        return True
    else:
        return False

@match('dbus-signal', signal='StatusChanged', args=[2, 1])
def expect_disconnected(event, data):
    return True

if __name__ == '__main__':
    go()

