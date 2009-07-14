"""
Test usage of ICE-UDP in incoming calls.
"""

from twisted.words.xish import xpath

from gabbletest import exec_test
from servicetest import (
    make_channel_proxy, wrap_channel, assertEquals, EventPattern, assertLength,
    )
import ns
import constants as cs

from jingletest2 import *

def worker(jp, q, bus, conn, stream):
    jp.features.append(ns.JINGLE_TRANSPORT_ICEUDP)
    jt2 = JingleTest2(jp, conn, q, stream, 'test@localhost', 'foo@bar.com/Foo')
    jt2.prepare()

    remote_handle = conn.RequestHandles(cs.HT_CONTACT, ["foo@bar.com/Foo"])[0]

    # Remote end calls us
    node = jp.SetIq(jt2.peer, jt2.jid, [
        jp.Jingle(jt2.sid, jt2.peer, 'session-initiate', [
            jp.Content('stream1', 'initiator', 'both', [
                jp.Description('audio', [
                    jp.PayloadType(name, str(rate), str(id)) for
                        (name, id, rate) in jt2.audio_codecs ]),
            jp.TransportIceUdp() ]) ]) ])
    stream.send(jp.xml(node))

    nc, e = q.expect_many(
        EventPattern('dbus-signal', signal='NewChannel'),
        EventPattern('dbus-signal', signal='NewSessionHandler'),
        )
    path = nc.args[0]

    chan = wrap_channel(bus.get_object(conn.bus_name, path), 'StreamedMedia')

    hrggh = chan.ListProperties(dbus_interface=cs.TP_AWKWARD_PROPERTIES)
    id = [x for x, name, _, _ in hrggh if name == 'nat-traversal'][0]
    nrgrg = chan.GetProperties([id], dbus_interface=cs.TP_AWKWARD_PROPERTIES)
    _, nat_traversal = nrgrg[0]
    assertEquals('ice-udp', nat_traversal)

    session_handler = make_channel_proxy(conn, e.args[0], 'Media.SessionHandler')
    session_handler.Ready()

    chan.Group.AddMembers([conn.GetSelfHandle()], 'accepted')

    # S-E gets notified about a newly-created stream
    e = q.expect('dbus-signal', signal='NewStreamHandler')

    stream_handler = make_channel_proxy(conn, e.args[0], 'Media.StreamHandler')

    stream_handler.NewNativeCandidate("fake", jt2.get_remote_transports_dbus())
    stream_handler.Ready(jt2.get_audio_codecs_dbus())
    stream_handler.StreamState(2)

    # First one is transport-info
    e = q.expect('stream-iq', predicate=jp.action_predicate('transport-info'))
    transport_stanza = xpath.queryForNodes(
        "/iq/jingle/content/transport[@xmlns='%s']"
        % ns.JINGLE_TRANSPORT_ICEUDP, e.stanza)[0]

    # username
    assertEquals(transport_stanza['ufrag'], jt2.remote_transports[0][7])
    # password
    assertEquals(transport_stanza['pwd'], jt2.remote_transports[0][8])

    children = list(transport_stanza.elements())
    assertLength(1, children)

    stream.send(jp.xml(jp.ResultIq('test@localhost', e.stanza, [])))

    # Set codec intersection so gabble can accept the session
    stream_handler.SupportedCodecs(jt2.get_audio_codecs_dbus())

    # Second one is session-accept
    e = q.expect('stream-iq', predicate=jp.action_predicate('session-accept'))
    assert xpath.queryForNodes("/iq/jingle/content/transport[@xmlns='%s']" %
        ns.JINGLE_TRANSPORT_ICEUDP, e.stanza)

    stream.send(jp.xml(jp.ResultIq('test@localhost', e.stanza, [])))

    # Connected! Blah, blah, ...

    jt2.terminate()

    e = q.expect('dbus-signal', signal='Close')

def test031(q, bus, conn, stream):
    return worker(JingleProtocol031(),q, bus, conn, stream)

if __name__ == '__main__':
    exec_test(test031)
