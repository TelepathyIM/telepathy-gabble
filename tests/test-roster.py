
"""
Test basic roster functionality.
"""

import dbus

from twisted.internet import glib2reactor
glib2reactor.install()

from twisted.words.xish import xpath

from gabbletest import conn_iface, go

def expect_connected(event, data):
    if event[0] != 'dbus-signal':
        return False

    if event[2] != 'StatusChanged':
        return False

    if event[3] != [0, 1]:
        return False

    return True

def expect_roster_iq(event, data):
    if event[0] != 'stream-iq':
        return False

    iq = event[1]
    nodes = xpath.queryForNodes("/iq/query[@xmlns='jabber:iq:roster']", iq)

    if not nodes:
        return False

    assert len(nodes) == 1
    iq['type'] = 'result'
    query = nodes[0]

    item = query.addElement('item')
    item['jid'] = 'amy@foo.com'
    item['subscription'] = 'both'

    item = query.addElement('item')
    item['jid'] = 'bob@foo.com'
    item['subscription'] = 'from'

    item = query.addElement('item')
    item['jid'] = 'che@foo.com'
    item['subscription'] = 'to'

    data['stream'].send(iq)
    return True

def _expect_contact_list_channel(event, data, name, contacts):
    if event[0] != 'dbus-signal':
        return False

    if event[2] != 'NewChannel':
        return False

    path, type, handle_type, handle, suppress_handler = event[3]

    if type != u'org.freedesktop.Telepathy.Channel.Type.ContactList':
        return False

    chan_name = conn_iface(
        data['conn']).InspectHandles(handle_type, [handle])[0]
    assert chan_name == name
    chan = data['conn']._bus.get_object(data['conn']._named_service, path)
    group_iface = dbus.Interface(chan,
        u'org.freedesktop.Telepathy.Channel.Interface.Group')
    chan_contacts = conn_iface(
        data['conn']).InspectHandles(1, group_iface.GetMembers())
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
        conn_iface(data['conn']).Disconnect()
        return True
    else:
        return False

def expect_disconnected(event, data):
    if event[0] != 'dbus-signal':
        return False

    if event[2] != 'StatusChanged':
        return False

    if event[3] != [2, 1]:
        return False

    return True

if __name__ == '__main__':
    go()

