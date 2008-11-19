"""Test stream initiation fallback."""

import base64
import errno
import os

import dbus
from dbus.connection import Connection
from dbus.lowlevel import SignalMessage

from servicetest import call_async, EventPattern, tp_name_prefix, watch_tube_signals, EventProtocolClientFactory
from gabbletest import exec_test, acknowledge_iq

from twisted.words.xish import domish, xpath
from twisted.internet.protocol import Factory, Protocol
from twisted.internet import reactor
from twisted.words.protocols.jabber.client import IQ

from gabbleconfig import HAVE_DBUS_TUBES

NS_TUBES = 'http://telepathy.freedesktop.org/xmpp/tubes'
NS_SI = 'http://jabber.org/protocol/si'
NS_FEATURE_NEG = 'http://jabber.org/protocol/feature-neg'
NS_IBB = 'http://jabber.org/protocol/ibb'
NS_X_DATA = 'jabber:x:data'
NS_BYTESTREAMS = 'http://jabber.org/protocol/bytestreams'

class Echo(Protocol):
    def dataReceived(self, data):
        self.transport.write(data)

def set_up_echo():
    factory = Factory()
    factory.protocol = Echo
    try:
        os.remove(os.getcwd() + '/stream')
    except OSError, e:
        if e.errno != errno.ENOENT:
            raise
    reactor.listenUNIX(os.getcwd() + '/stream', factory)

def test(q, bus, conn, stream):
    set_up_echo()

    conn.Connect()

    _, vcard_event, roster_event = q.expect_many(
        EventPattern('dbus-signal', signal='StatusChanged', args=[0, 1]),
        EventPattern('stream-iq', to=None, query_ns='vcard-temp',
            query_name='vCard'),
        EventPattern('stream-iq', query_ns='jabber:iq:roster'))

    acknowledge_iq(stream, vcard_event.stanza)

    roster = roster_event.stanza
    roster['type'] = 'result'
    item = roster_event.query.addElement('item')
    item['jid'] = 'bob@localhost'
    item['subscription'] = 'both'
    stream.send(roster)

    presence = domish.Element(('jabber:client', 'presence'))
    presence['from'] = 'bob@localhost/Bob'
    presence['to'] = 'test@localhost/Resource'
    c = presence.addElement('c')
    c['xmlns'] = 'http://jabber.org/protocol/caps'
    c['node'] = 'http://example.com/ICantBelieveItsNotTelepathy'
    c['ver'] = '1.2.3'
    stream.send(presence)

    event = q.expect('stream-iq', iq_type='get',
        query_ns='http://jabber.org/protocol/disco#info',
        to='bob@localhost/Bob')
    result = event.stanza
    result['type'] = 'result'
    assert event.query['node'] == \
        'http://example.com/ICantBelieveItsNotTelepathy#1.2.3'
    feature = event.query.addElement('feature')
    feature['var'] = NS_TUBES
    stream.send(result)

    requestotron = dbus.Interface(conn,
            'org.freedesktop.Telepathy.Connection.Interface.Requests')
    bob_handle = conn.RequestHandles(1, ['bob@localhost'])[0]

    # Offer a tube
    call_async(q, requestotron, 'CreateChannel',
            {'org.freedesktop.Telepathy.Channel.ChannelType':
                'org.freedesktop.Telepathy.Channel.Type.StreamTube.DRAFT',
             'org.freedesktop.Telepathy.Channel.TargetHandleType':
                1,
             'org.freedesktop.Telepathy.Channel.TargetHandle':
                bob_handle,
             'org.freedesktop.Telepathy.Channel.Type.StreamTube.DRAFT.Service':
                'echo',
             'org.freedesktop.Telepathy.Channel.Interface.Tube.DRAFT.Parameters':
                dbus.Dictionary({'foo': 'bar'}, signature='sv'),
            })
    ret, _, _ = q.expect_many(
        EventPattern('dbus-return', method='CreateChannel'),
        EventPattern('dbus-signal', signal='NewChannel'),
        EventPattern('dbus-signal', signal='NewChannels'),
        )

    chan_path = ret.value[0]
    channels = filter(lambda x:
      x[1] == "org.freedesktop.Telepathy.Channel.Type.StreamTube.DRAFT" and
      x[0] == chan_path,
      conn.ListChannels())
    tube_chan = bus.get_object(conn.bus_name, channels[0][0])
    tube_iface = dbus.Interface(tube_chan,
        tp_name_prefix + '.Channel.Type.StreamTube.DRAFT')

    path = os.getcwd() + '/stream'
    call_async(q, tube_iface, 'OfferStreamTube',
        0, dbus.ByteArray(path), 0, "")

    event = q.expect('stream-message')
    message = event.stanza
    tube_nodes = xpath.queryForNodes('/message/tube[@xmlns="%s"]' % NS_TUBES,
        message)
    assert tube_nodes is not None
    assert len(tube_nodes) == 1
    tube = tube_nodes[0]

    assert tube['service'] == 'echo'
    assert tube['type'] == 'stream'
    assert not tube.hasAttribute('initiator')
    stream_tube_id = long(tube['id'])

    # The CM is the server, so fake a client wanting to talk to it
    iq = IQ(stream, 'set')
    iq['to'] = 'test@localhost/Resource'
    iq['from'] = 'bob@localhost/Bob'
    si = iq.addElement((NS_SI, 'si'))
    si['id'] = 'alpha'
    si['profile'] = NS_TUBES
    feature = si.addElement((NS_FEATURE_NEG, 'feature'))
    x = feature.addElement((NS_X_DATA, 'x'))
    x['type'] = 'form'
    field = x.addElement((None, 'field'))
    field['var'] = 'stream-method'
    field['type'] = 'list-multiple'
    option = field.addElement((None, 'option'))
    value = option.addElement((None, 'value'))
    value.addContent(NS_BYTESTREAMS)
    option = field.addElement((None, 'option'))
    value = option.addElement((None, 'value'))
    value.addContent(NS_IBB)

    stream_node = si.addElement((NS_TUBES, 'stream'))
    stream_node['tube'] = str(stream_tube_id)
    stream.send(iq)

    si_reply_event, _ = q.expect_many(
            EventPattern('stream-iq', iq_type='result'),
            EventPattern('dbus-signal', signal='TubeChannelStateChanged',
                args=[2])) # 2 == OPEN
    iq = si_reply_event.stanza
    si = xpath.queryForNodes('/iq/si[@xmlns="%s"]' % NS_SI,
        iq)[0]
    value = xpath.queryForNodes('/si/feature/x/field/value', si)
    assert len(value) == 1
    proto = value[0]
    assert str(proto) == NS_BYTESTREAMS
    tube = xpath.queryForNodes('/si/tube[@xmlns="%s"]' % NS_TUBES, si)
    assert len(tube) == 1

    q.expect('dbus-signal', signal='StreamTubeNewConnection',
        args=[bob_handle])

    # Send the non-working streamhost
    iq = IQ(stream, 'set')
    iq['to'] = 'test@localhost/Resource'
    iq['from'] = 'bob@localhost/Bob'
    query = iq.addElement((NS_BYTESTREAMS, 'query'))
    query['sid'] = 'alpha'
    query['mode'] = 'tcp'
    streamhost = query.addElement('streamhost')
    streamhost['jid'] = 'bob@localhost/Bob'
    streamhost['host'] = 'invalid.invalid'
    streamhost['port'] = '1234'
    stream.send(iq)

    event = q.expect('stream-iq', iq_type='set', to='bob@localhost/Bob')
    iq = event.stanza
    open = xpath.queryForNodes('/iq/open', iq)[0]
    assert open.uri == NS_IBB
    assert open['sid'] == 'alpha'

    result = IQ(stream, 'result')
    result['id'] = iq['id']
    result['from'] = iq['to']
    result['to'] = 'test@localhost/Resource'

    stream.send(result)

    # have the fake client send us some data
    message = domish.Element(('jabber:client', 'message'))
    message['to'] = 'test@localhost/Resource'
    message['from'] = 'bob@localhost/Bob'
    data_node = message.addElement((NS_IBB, 'data'))
    data_node['sid'] = 'alpha'
    data_node['seq'] = '0'
    data_node.addContent(base64.b64encode('hello world'))
    stream.send(message)

    event = q.expect('stream-message', to='bob@localhost/Bob')
    message = event.stanza

    data_nodes = xpath.queryForNodes('/message/data[@xmlns="%s"]' % NS_IBB,
        message)
    assert data_nodes is not None
    assert len(data_nodes) == 1
    ibb_data = data_nodes[0]
    assert ibb_data['sid'] == 'alpha'
    binary = base64.b64decode(str(ibb_data))
    assert binary == 'hello world'


    # Accepting a tube
    message = domish.Element(('jabber:client', 'message'))
    message['to'] = 'test@localhost/Resource'
    message['from'] = 'bob@localhost/Bob'
    message['id'] = 'msg-id'
    tube = message.addElement((NS_TUBES, 'tube'))
    tube['type'] = 'stream'
    tube['id'] = '42'
    tube['service'] = 'foo-service'
    parameters = tube.addElement((None, 'parameters'))
    parameter = parameters.addElement((None, 'parameter'))
    parameter['type'] = 'str'
    parameter['name'] = 'foo'
    parameter.addContent('bar')

    stream.send (message)

    event = q.expect('dbus-signal', signal='NewTube')
    id = event.args[0]
    initiator = event.args[1]
    type = event.args[2]
    service = event.args[3]
    parameters = event.args[4]
    state = event.args[5]

    assert id == 42
    initiator_jid = conn.InspectHandles(1, [initiator])[0]
    assert initiator_jid == 'bob@localhost'
    assert type == 1 # Stream tube
    assert service == 'foo-service'
    assert parameters == {'foo': 'bar'}
    assert state == 0 # local pending

    # accept the tube
    channels = filter(lambda x:
      x[1] == "org.freedesktop.Telepathy.Channel.Type.StreamTube.DRAFT" and
      '42' in x[0],
      conn.ListChannels())
    assert len(channels) == 1

    tube_chan = bus.get_object(conn.bus_name, channels[0][0])
    tube_iface = dbus.Interface(tube_chan,
        tp_name_prefix + '.Channel.Type.StreamTube.DRAFT')

    call_async(q, tube_iface, 'AcceptStreamTube', 0, 0, '')

    event = q.expect('dbus-return', method='AcceptStreamTube')
    path2 = event.value[0]
    path2 = ''.join([chr(c) for c in path2])

    factory = EventProtocolClientFactory(q)
    reactor.connectUNIX(path2, factory)

    event = q.expect('stream-iq', iq_type='set', to='bob@localhost/Bob')
    iq = event.stanza
    si_nodes = xpath.queryForNodes('/iq/si', iq)
    assert si_nodes is not None
    assert len(si_nodes) == 1
    si = si_nodes[0]
    assert si['profile'] == NS_TUBES
    dbus_stream_id = si['id']

    feature = xpath.queryForNodes('/si/feature', si)[0]
    x = xpath.queryForNodes('/feature/x', feature)[0]
    assert x['type'] == 'form'
    field = xpath.queryForNodes('/x/field', x)[0]
    assert field['var'] == 'stream-method'
    assert field['type'] == 'list-multiple'
    value = xpath.queryForNodes('/field/option/value', field)[0]
    assert str(value) == NS_BYTESTREAMS
    value = xpath.queryForNodes('/field/option/value', field)[1]
    assert str(value) == NS_IBB

    result = IQ(stream, 'result')
    result['id'] = iq['id']
    result['from'] = iq['to']
    result['to'] = 'test@localhost/Resource'
    res_si = result.addElement((NS_SI, 'si'))
    res_feature = res_si.addElement((NS_FEATURE_NEG, 'feature'))
    res_x = res_feature.addElement((NS_X_DATA, 'x'))
    res_x['type'] = 'submit'
    res_field = res_x.addElement((None, 'field'))
    res_field['var'] = 'stream-method'
    res_value = res_field.addElement((None, 'value'))
    res_value.addContent(NS_BYTESTREAMS)
    res_value = res_field.addElement((None, 'value'))
    res_value.addContent(NS_IBB)

    stream.send(result)

    event = q.expect('stream-iq', iq_type='set', to='bob@localhost/Bob')
    iq = event.stanza
    query = xpath.queryForNodes('/iq/query', iq)[0]
    assert query.uri == NS_BYTESTREAMS
    sid = query['sid']
    streamhost = xpath.queryForNodes('/iq/query/streamhost', iq)[0]
    assert streamhost

    iq = IQ(stream, 'set')
    iq['to'] = 'test@localhost/Resource'
    iq['from'] = 'bob@localhost/Bob'
    open = iq.addElement((NS_IBB, 'open'))
    open['sid'] = sid
    open['block-size'] = '4096'
    stream.send(iq)

    q.expect('stream-iq', iq_type='result')

    return

if __name__ == '__main__':
    exec_test(test)
