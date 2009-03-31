
"""
Test MUC properties support.
"""

from twisted.words.xish import xpath

from gabbletest import (
    exec_test, make_result_iq, acknowledge_iq, make_muc_presence)
from servicetest import call_async, wrap_channel, EventPattern

import constants
import ns

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

def test(q, bus, conn, stream):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged', args=[0, 1])
    # Need to call this asynchronously as it involves Gabble sending us a
    # query.
    call_async(q, conn, 'RequestHandles', 2, ['chat@conf.localhost'])

    event = q.expect('stream-iq', to='conf.localhost', query_ns=ns.DISCO_INFO)
    result = make_result_iq(stream, event.stanza)
    feature = result.firstChildElement().addElement('feature')
    feature['var'] = 'http://jabber.org/protocol/muc'
    stream.send(result)

    event = q.expect('dbus-return', method='RequestHandles')
    handles = event.value[0]
    call_async(q, conn, 'RequestChannel', constants.CHANNEL_TYPE_TEXT, 2,
        handles[0], True)

    q.expect('stream-presence', to='chat@conf.localhost/test')

    # Send presence for own membership of room.
    stream.send(
        make_muc_presence('owner', 'moderator', 'chat@conf.localhost', 'test'))

    iq, ret = q.expect_many(
        EventPattern('stream-iq', to='chat@conf.localhost', iq_type='get',
            query_ns=ns.MUC_OWNER),
        EventPattern('dbus-return', method='RequestChannel'))
    handle_muc_get_iq(stream, iq.stanza)

    text_chan = wrap_channel(
        bus.get_object(conn.bus_name, ret.value[0]), 'Text')

    props = dict([(name, id)
        for id, name, sig, flags in text_chan.TpProperties.ListProperties()])
    call_async(q, text_chan.TpProperties, 'SetProperties',
        [(props['password'], 'foo'), (props['password-required'], True)])

    event = q.expect('stream-iq', to='chat@conf.localhost', iq_type='get',
        query_ns=ns.MUC_OWNER)
    handle_muc_get_iq(stream, event.stanza)

    event = q.expect('stream-iq', to='chat@conf.localhost', iq_type='set',
        query_ns=ns.MUC_OWNER)
    fields = xpath.queryForNodes('/iq/query/x/field', event.stanza)
    form = {}
    for field in fields:
        values = xpath.queryForNodes('/field/value', field)
        form[field['var']] = [str(v) for v in values]
    assert form == {'password': ['foo'], 'password_protected': ['1'],
            'muc#roomconfig_presencebroadcast' :
            ['moderator', 'participant', 'visitor']}
    acknowledge_iq(stream, event.stanza)

    event = q.expect('dbus-signal', signal='PropertiesChanged')
    assert event.args == [[(props['password'], 'foo'),
        (props['password-required'], True)]]

    q.expect('dbus-return', method='SetProperties', value=())

    conn.Disconnect()
    q.expect('dbus-signal', signal='StatusChanged', args=[2, 1])

if __name__ == '__main__':
    exec_test(test)

