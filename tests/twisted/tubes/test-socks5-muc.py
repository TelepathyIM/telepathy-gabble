"""Check if SOCKS5 relays are disabled in muc"""

import os

if os.name != 'posix':
    raise SystemExit(77)    # skipped on non-unix for now

from servicetest import call_async, EventPattern, EventProtocolClientFactory
from gabbletest import acknowledge_iq, make_muc_presence, exec_test
import constants as cs
import ns
from muctubeutil import get_muc_tubes_channel
from bytestream import BytestreamS5BRelay, create_from_si_offer, announce_socks5_proxy

from twisted.internet import reactor

def test(q, bus, conn, stream):
    conn.Connect()

    _, iq_event, disco_event = q.expect_many(
        EventPattern('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED]),
        EventPattern('stream-iq', to=None, query_ns='vcard-temp',
            query_name='vCard'),
        EventPattern('stream-iq', to='localhost', query_ns=ns.DISCO_ITEMS))

    acknowledge_iq(stream, iq_event.stanza)

    announce_socks5_proxy(q, stream, disco_event.stanza)

    room_handle, tubes_chan, tubes_iface = get_muc_tubes_channel(q, bus, conn,
        stream, 'chat@conf.localhost')

    # bob offers a stream tube
    stream_tube_id = 1

    presence = make_muc_presence('owner', 'moderator', 'chat@conf.localhost', 'bob')
    tubes = presence.addElement((ns.TUBES, 'tubes'))
    tube = tubes.addElement((None, 'tube'))
    tube['type'] = 'stream'
    tube['service'] = 'echo'
    tube['id'] = str(stream_tube_id)
    parameters = tube.addElement((None, 'parameters'))
    stream.send(presence)

    e = q.expect('dbus-signal', signal='NewChannels')
    channels = e.args[0]
    assert len(channels) == 1
    path, props = channels[0]
    assert props[cs.CHANNEL_TYPE] == cs.CHANNEL_TYPE_STREAM_TUBE

    tube_chan = bus.get_object(conn.bus_name, path)
    call_async(q, tubes_iface, 'AcceptStreamTube', stream_tube_id, 0, 0, '',
        byte_arrays=True)

    accept_return_event, _ = q.expect_many(
        EventPattern('dbus-return', method='AcceptStreamTube'),
        EventPattern('dbus-signal', signal='TubeStateChanged',
            args=[stream_tube_id, cs.TUBE_CHANNEL_STATE_OPEN]))

    unix_socket_adr = accept_return_event.value[0]

    factory = EventProtocolClientFactory(q)
    reactor.connectUNIX(unix_socket_adr, factory)

     # expect SI request
    e = q.expect('stream-iq', to='chat@conf.localhost/bob', query_ns=ns.SI,
        query_name='si')

    bytestream, profile = create_from_si_offer(stream, q, BytestreamS5BRelay, e.stanza,
        'chat@conf.localhost/bob')

    result, si = bytestream.create_si_reply(e.stanza, 'test@localhost/Resource')
    si.addElement((ns.TUBES, 'tube'))
    stream.send(result)

    # wait SOCKS5 init iq
    id, mode, si, hosts = bytestream._expect_socks5_init()
    for jid, host, port in hosts:
        # the proxy is not announced because we are in a muc
        assert jid != 'proxy.localhost'

if __name__ == '__main__':
    exec_test(test)
