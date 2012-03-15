"""
Test support for creating and retrieving 1-1 tubes with EnsureChannel
"""

import dbus

from servicetest import call_async, EventPattern, tp_name_prefix, unwrap
from gabbletest import exec_test, acknowledge_iq
import constants as cs
import ns

from twisted.words.xish import domish

import tubetestutil as t

sample_parameters = dbus.Dictionary({
    's': 'hello',
    'ay': dbus.ByteArray('hello'),
    'u': dbus.UInt32(123),
    'i': dbus.Int32(-123),
    }, signature='sv')


def test(q, bus, conn, stream):
    conn.Connect()

    properties = conn.GetAll(
        cs.CONN_IFACE_REQUESTS, dbus_interface=cs.PROPERTIES_IFACE)
    assert properties.get('Channels') == [], properties['Channels']
    assert ({cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_STREAM_TUBE,
             cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
             },
             [cs.TARGET_HANDLE, cs.TARGET_ID, cs.STREAM_TUBE_SERVICE]
             ) in properties.get('RequestableChannelClasses'),\
                     unwrap(properties['RequestableChannelClasses'])

    _, vcard_event, roster_event = q.expect_many(
        EventPattern('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED]),
        EventPattern('stream-iq', to=None, query_ns='vcard-temp',
            query_name='vCard'),
        EventPattern('stream-iq', query_ns=ns.ROSTER))

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
    feature['var'] = ns.TUBES
    stream.send(result)

    bob_handle = conn.RequestHandles(1, ['bob@localhost'])[0]

    def new_chan_predicate(e):
        types = []
        for _, props in e.args[0]:
            types.append(props[cs.CHANNEL_TYPE])

        return cs.CHANNEL_TYPE_STREAM_TUBE in types

    call_async(q, conn.Requests, 'CreateChannel',
            { cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_STREAM_TUBE,
              cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
              cs.TARGET_HANDLE: bob_handle,
              cs.STREAM_TUBE_SERVICE: 'the.service',
              })

    ret, _ = q.expect_many(
        EventPattern('dbus-return', method='CreateChannel'),
        EventPattern('dbus-signal', signal='NewChannels',
                     predicate=new_chan_predicate),
        )


    chan_path, props = ret.value

    # Ensure a tube to the same person; check it's the same one.
#    call_async(q, conn.Requests, 'EnsureChannel',
#            { cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_STREAM_TUBE,
#              cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
#              cs.TARGET_HANDLE: bob_handle,
#              cs.STREAM_TUBE_SERVICE: 'the.service',
#              })

#    ret = q.expect('dbus-return', method='EnsureChannel')
#    yours, ensured_path, _ = ret.value

#    assert ensured_path == chan_path, (ensured_path, chan_path)
#    assert not yours

    chan = bus.get_object(conn.bus_name, chan_path)
    chan.Close()

    # Now let's try ensuring a new tube.
    call_async(q, conn.Requests, 'EnsureChannel',
            { cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_STREAM_TUBE,
              cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
              cs.TARGET_HANDLE: bob_handle,
              cs.STREAM_TUBE_SERVICE: 'the.service',
              })

    ret, new_sig = q.expect_many(
        EventPattern('dbus-return', method='EnsureChannel'),
        EventPattern('dbus-signal', signal='NewChannels',
                     predicate=new_chan_predicate),
        )

    yours, path, props = ret.value
    assert yours

    emitted_props = new_sig.args[0][0][1]
    assert props == emitted_props, (props, emitted_props)

    chan = bus.get_object(conn.bus_name, path)
    chan.Close()

if __name__ == '__main__':
    exec_test(test, do_connect=False)
