"""
Test outgoing call using ICE-UDP transport mechanism.
"""

from gabbletest import exec_test, sync_stream
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
    jp.features.remove(ns.GOOGLE_P2P)
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

    # The 'nat-traversal' tp property should be "ice-udp"
    hrggh = chan.ListProperties(dbus_interface=cs.TP_AWKWARD_PROPERTIES)
    id = [x for x, name, _, _ in hrggh if name == 'nat-traversal'][0]
    nrgrg = chan.GetProperties([id], dbus_interface=cs.TP_AWKWARD_PROPERTIES)
    _, nat_traversal = nrgrg[0]
    assertEquals('ice-udp', nat_traversal)

    session_handler = make_channel_proxy(conn, e.args[0], 'Media.SessionHandler')
    session_handler.Ready()

    e = q.expect('dbus-signal', signal='NewStreamHandler')

    stream_handler = make_channel_proxy(conn, e.args[0], 'Media.StreamHandler')

    stream_handler.NewNativeCandidate("fake", jt2.get_remote_transports_dbus())
    stream_handler.Ready(jt2.get_audio_codecs_dbus())
    stream_handler.StreamState(2)

    e = q.expect('stream-iq', predicate=jp.action_predicate('session-initiate'))
    # The session-initiate "MUST include a <transport/> child element qualified
    # by the [ice-udp] namespace"
    node = xpath.queryForNodes("/iq/jingle/content/transport[@xmlns='%s']" %
        ns.JINGLE_TRANSPORT_ICEUDP, e.stanza)[0]
    jt2.parse_session_initiate(e.query)

    # ...which SHOULD contain the higher-priority ICE candidates. We supplied
    # one candidate, so...
    assertEquals('username', node['ufrag'])
    assertEquals('password', node['pwd'])
    node = [ x for x in node.children if type(x) != unicode ][0]
    assertEquals('candidate', node.name)
    assert node['foundation'] is not None

    stream.send(jp.xml(jp.ResultIq('test@localhost', e.stanza, [])))

    # Make sure that it doesn't send a duplicate of our one ICE candidate here.
    ti_event = [
        EventPattern('stream-iq',
            predicate=jp.action_predicate('transport-info'))
        ]
    q.forbid_events(ti_event)
    sync_stream(q, stream)
    q.unforbid_events(ti_event)

    # XEP-0166 6.4 Negotiation: "The allowable negotiations include:
    # Exchanging particular transport candidates via the transport-info action."
    candidate = (
        "192.168.0.69", # host
        668, # port
        0, # protocol = TP_MEDIA_STREAM_BASE_PROTO_UDP
        "RTP", # protocol subtype
        "AVP", # profile
        1.0, # preference
        0, # transport type = TP_MEDIA_STREAM_TRANSPORT_TYPE_LOCAL,
        "username",
        "password" )
    transport = jp.TransportIceUdp([candidate])

    node = jp.SetIq(jt2.peer, jt2.jid, [
        jp.Jingle(jt2.sid, jt2.peer, 'transport-info', [
            jp.Content('stream1', 'initiator', 'both', [
                transport]) ]) ])
    stream.send(jp.xml(node))

    candidate_e, result_e =  q.expect_many(
        EventPattern('dbus-signal',  signal='AddRemoteCandidate'),
        EventPattern('stream-iq', iq_type='result'))

    fake_, (returned_candidate,) = candidate_e.args
    assertEquals(candidate, returned_candidate[1:])

    candidate = (
        "192.168.0.69", # host
        670, # port
        0, # protocol = TP_MEDIA_STREAM_BASE_PROTO_UDP
        "RTP", # protocol subtype
        "AVP", # profile
        1.0, # preference
        0, # transport type = TP_MEDIA_STREAM_TRANSPORT_TYPE_LOCAL,
        "username",
        "password" )
    transport = jp.TransportIceUdp([candidate])

    # It is also valid to send transports in the accept.
    # This is what pidgin does.
    node = jp.SetIq(jt2.peer, jt2.jid, [
        jp.Jingle(jt2.sid, jt2.peer, 'session-accept', [
            jp.Content('stream1', 'initiator', 'both', [
                jp.Description('audio', [
                    jp.PayloadType(name, str(rate), str(id)) for
                        (name, id, rate) in jt2.audio_codecs ]),
                transport ]) ]) ])
    stream.send(jp.xml(node))

    candidate_e, result_e = q.expect_many(
        EventPattern('dbus-signal',  signal='AddRemoteCandidate'),
        EventPattern('stream-iq', iq_type='result'))

    fake_, (returned_candidate,) = candidate_e.args
    assertEquals(candidate, returned_candidate[1:])

    chan.Close()
    e = q.expect('stream-iq',
        predicate=jp.action_predicate('session-terminate'))

def test031(q, bus, conn, stream):
    return worker(JingleProtocol031(),q, bus, conn, stream)

if __name__ == '__main__':
    exec_test(test031)
