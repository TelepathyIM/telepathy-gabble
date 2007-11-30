
"""
Test MUC properties support.
"""

import dbus

from twisted.words.xish import domish, xpath

from gabbletest import go, make_result_iq, acknowledge_iq
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
    # Send presence for own membership of room.
    presence = domish.Element((None, 'presence'))
    presence['from'] = 'chat@conf.localhost/test'
    x = presence.addElement(('http://jabber.org/protocol/muc#user', 'x'))
    item = x.addElement('item')
    item['affiliation'] = 'owner'
    item['role'] = 'moderator'
    data['stream'].send(presence)
    return True

@match('dbus-signal', signal='MembersChanged',
    args=[u'', [2], [], [], [], 0, 0])
def expect_members_changed2(event, data):
    return True

@match('dbus-return', method='RequestChannel')
def expect_request_channel_return(event, data):
    bus = data['conn']._bus
    data['text_chan'] = bus.get_object(
        data['conn']._named_service, event.value[0])

    props_iface = dbus.Interface(data['text_chan'],
        'org.freedesktop.Telepathy.Properties')
    props = dict([(name, id)
        for id, name, sig, flags in props_iface.ListProperties()])
    call_async(data['test'], props_iface, 'SetProperties',
        [(props['password'], 'foo'), (props['password-required'], True)])

    data['props_iface'] = props_iface
    data['props'] = props
    return True

def add_field(elem, type, var, value):
    field = elem.addElement('field')
    field['type'] = type
    field['var'] = var
    value = field.addElement('value', content=value)

def handle_muc_get_iq(stream, stanza):
    iq = make_result_iq(stream, stanza)
    query = iq.firstChildElement()
    x = query.addElement(('jabber:x:data', 'x'))
    x['type'] = 'form'
    add_field(x, 'text', 'password', '')
    add_field(x, 'boolean', 'password_protected', '0')

    # add a multi values setting
    field = x.addElement('field')
    field['type'] = 'list-multi'
    field['var'] = 'muc#roomconfig_presencebroadcast'
    for v in ['moderator', 'participant', 'visitor']:
        field.addElement('value', content=v)
        field.addElement('option', content=v)

    stream.send(iq)
    return True

@match('stream-iq', to='chat@conf.localhost', iq_type='get',
    query_ns='http://jabber.org/protocol/muc#owner')
def expect_muc_get_iq1(event, data):
    handle_muc_get_iq(data['stream'], event.stanza)
    return True

@match('stream-iq', to='chat@conf.localhost', iq_type='get',
    query_ns='http://jabber.org/protocol/muc#owner')
def expect_muc_get_iq2(event, data):
    handle_muc_get_iq(data['stream'], event.stanza)
    return True

@match('stream-iq', to='chat@conf.localhost', iq_type='set',
    query_ns='http://jabber.org/protocol/muc#owner')
def expect_muc_set_iq(event, data):
    fields = xpath.queryForNodes('/iq/query/x/field', event.stanza)
    form = {}
    for field in fields:
        values = xpath.queryForNodes('/field/value', field)
        form[field['var']] = [str(v) for v in values]
    assert form == {'password': ['foo'], 'password_protected': ['1'],
            'muc#roomconfig_presencebroadcast' :
            ['moderator', 'participant', 'visitor']}
    acknowledge_iq(data['stream'], event.stanza)
    return True

@match('dbus-signal', signal='PropertiesChanged')
def expect_properties_changed(event, data):
    assert event.args == [[(data['props']['password'], 'foo'),
        (data['props']['password-required'], True)]]
    return True

@match('dbus-return', method='SetProperties', value=())
def expect_set_properties_return(event, data):
    data['conn_iface'].Disconnect()
    return True

@match('dbus-signal', signal='StatusChanged', args=[2, 1])
def expect_disconnected(event, data):
    return True

if __name__ == '__main__':
    go()

