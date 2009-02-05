"""
Test outgoing call using ICE-UDP transport mechanism.
"""

from gabbletest import exec_test
from servicetest import (
    wrap_channel, make_channel_proxy, EventPattern, call_async,
    assertEquals)
import gabbletest
import dbus
import time
from twisted.words.xish import xpath
import ns
import constants as cs

from jingletest2 import *

def worker(jp, q, bus, conn, stream):
    jp.features.append(ns.JINGLE_TRANSPORT_ICEUDP)
    jt2 = JingleTest2(jp, conn, q, stream, 'test@localhost', 'foo@bar.com/Foo')
    jt2.prepare()

    remote_handle = conn.RequestHandles(cs.HT_CONTACT, ["foo@bar.com/Foo"])[0]

    call_async(q, conn, 'RequestChannel', cs.CHANNEL_TYPE_STREAMED_MEDIA, 0, 0,
        True)

    ret, old_sig, new_sig = q.expect_many(
        EventPattern('dbus-return', method='RequestChannel'),
        EventPattern('dbus-signal', signal='NewChannel'),
        EventPattern('dbus-signal', signal='NewChannels'),
        )
    path = ret.value[0]

    chan = wrap_channel(bus.get_object(conn.bus_name, path), 'StreamedMedia')

    chan.StreamedMedia.RequestStreams(remote_handle, [cs.MEDIA_STREAM_TYPE_AUDIO])

    # S-E gets notified about new session handler, and calls Ready on it
    e = q.expect('dbus-signal', signal='NewSessionHandler')
    assert e.args[1] == 'rtp'

    # Gabble changes nat-traversal property to "ice-udp"
    q.expect('dbus-signal', signal="PropertiesChanged", args=[[(0, 'ice-udp')]])

    session_handler = make_channel_proxy(conn, e.args[0], 'Media.SessionHandler')
    session_handler.Ready()

    e = q.expect('dbus-signal', signal='NewStreamHandler')

    stream_handler = make_channel_proxy(conn, e.args[0], 'Media.StreamHandler')

    stream_handler.NewNativeCandidate("fake", jt2.get_remote_transports_dbus())
    stream_handler.Ready(jt2.get_audio_codecs_dbus())
    stream_handler.StreamState(2)

    e = q.expect('stream-iq', predicate=jp.action_predicate('session-initiate'))
    assert xpath.queryForNodes("/iq/jingle/content/transport[@xmlns='%s']" %
        ns.JINGLE_TRANSPORT_ICEUDP, e.stanza)
    jt2.parse_session_initiate(e.query)

    stream.send(jp.xml(jp.ResultIq('test@localhost', e.stanza, [])))

    e = q.expect('stream-iq', predicate=jp.action_predicate('transport-info'))
    node = xpath.queryForNodes("/iq/jingle/content/transport[@xmlns='%s']" %
        ns.JINGLE_TRANSPORT_ICEUDP, e.stanza)[0]

    assertEquals('username', node['ufrag'])
    assertEquals('password', node['pwd'])
    node = [ x for x in node.children if type(x) != unicode ][0]
    assertEquals('candidate', node.name)
    assert node['foundation'] is not None

    node = jp.SetIq(jt2.peer, jt2.jid, [
        jp.Jingle(jt2.sid, jt2.peer, 'session-accept', [
            jp.Content('stream1', 'initiator', 'both', [
                jp.Description('audio', [
                    jp.PayloadType(name, str(rate), str(id)) for
                        (name, id, rate) in jt2.audio_codecs ]),
            jp.TransportIceUdp() ]) ]) ])
    stream.send(jp.xml(node))

    q.expect('stream-iq', iq_type='result')

    chan.Close()
    e = q.expect('stream-iq',
        predicate=jp.action_predicate('session-terminate'))

    conn.Disconnect()
    q.expect('dbus-signal', signal='StatusChanged', args=[2, 1])

def test031(q, bus, conn, stream):
    return worker(JingleProtocol031(),q, bus, conn, stream)

if __name__ == '__main__':
    exec_test(test031)
