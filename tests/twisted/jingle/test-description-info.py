"""
Test emition and handling of codec update using description-info
"""

from gabbletest import exec_test, make_result_iq, sync_stream, exec_tests
from servicetest import make_channel_proxy, unwrap, tp_path_prefix, \
        EventPattern
import gabbletest
import dbus
import time
from twisted.words.xish import xpath

from jingletest2 import *

def test(q, bus, conn, stream):

    jp = JingleProtocol031()

    def make_stream_request(stream_type):
        media_iface.RequestStreams(remote_handle, [stream_type])

        e = q.expect('dbus-signal', signal='NewStreamHandler')
        stream_id = e.args[1]

        stream_handler = make_channel_proxy(conn, e.args[0], 'Media.StreamHandler')

        stream_handler.NewNativeCandidate("fake", jt2.get_remote_transports_dbus())
        stream_handler.Ready(jt2.get_audio_codecs_dbus())
        stream_handler.StreamState(2)
        return (stream_handler, stream_id)


    jt2 = JingleTest2(jp, conn, q, stream, 'test@localhost', 'foo@bar.com/Foo')
    jt2.prepare()

    remote_handle = conn.RequestHandles(1, ["foo@bar.com/Foo"])[0]

    # Remote end calls us
    node = jp.SetIq(jt2.peer, jt2.jid, [
        jp.Jingle(jt2.sid, jt2.peer, 'session-initiate', [
            jp.Content('stream1', 'initiator', 'both', [
                jp.Description('audio', [
                    jp.PayloadType(name, str(rate), str(id)) for
                        (name, id, rate) in jt2.audio_codecs ]),
            jp.TransportGoogleP2P() ]) ]) ])
    stream.send(jp.xml(node))

    # The caller is in members
    e = q.expect('dbus-signal', signal='MembersChanged',
             args=[u'', [remote_handle], [], [], [], 0, 0])

    # We're pending because of remote_handle
    e = q.expect('dbus-signal', signal='MembersChanged',
             args=[u'', [], [], [1L], [], remote_handle, 0])

    media_chan = make_channel_proxy(conn, tp_path_prefix + e.path, 'Channel.Interface.Group')
    signalling_iface = make_channel_proxy(conn, tp_path_prefix + e.path, 'Channel.Interface.MediaSignalling')
    media_iface = make_channel_proxy(conn, tp_path_prefix + e.path, 'Channel.Type.StreamedMedia')

    # S-E gets notified about new session handler, and calls Ready on it
    e = q.expect('dbus-signal', signal='NewSessionHandler')
    assert e.args[1] == 'rtp'

    session_handler = make_channel_proxy(conn, e.args[0], 'Media.SessionHandler')
    session_handler.Ready()

    media_chan.AddMembers([dbus.UInt32(1)], 'accepted')

    # S-E gets notified about a newly-created stream
    e = q.expect('dbus-signal', signal='NewStreamHandler')
    id1 = e.args[1]

    stream_handler = make_channel_proxy(conn, e.args[0], 'Media.StreamHandler')

    # We are now in members too
    e = q.expect('dbus-signal', signal='MembersChanged',
             args=[u'', [1L], [], [], [], 0, 0])

    # we are now both in members
    members = media_chan.GetMembers()
    assert set(members) == set([1L, remote_handle]), members

    stream_handler.NewNativeCandidate("fake", jt2.get_remote_transports_dbus())
    stream_handler.Ready(jt2.get_audio_codecs_dbus())
    stream_handler.StreamState(2)

    # First one is transport-info
    e = q.expect('stream-iq')
    assert jp.match_jingle_action(e.query, 'transport-info')
    assert e.query['initiator'] == 'foo@bar.com/Foo'

    stream.send(jp.xml(jp.ResultIq('test@localhost', e.stanza, [])))

    # S-E reports codec intersection, after which gabble can send acceptance
    stream_handler.SupportedCodecs(jt2.get_audio_codecs_dbus())

    # Second one is session-accept
    e = q.expect('stream-iq')
    assert jp.match_jingle_action(e.query, 'session-accept')

    # We decide we want to update our codec parameters
    jt2.audio_codecs = [ ('A', 3, 8000), ('B', 8, 8000), ('C', 0, 8000) ]
    stream_handler.CodecsUpdated(jt2.get_audio_codecs_dbus())

    e = q.expect('stream-iq', iq_type='set', predicate=lambda x:
        xpath.queryForNodes("/iq/jingle[@action='description-info']",
            x.stanza))
    assert xpath.queryForNodes("/iq/jingle/content[@name='stream1']/description/payload-type[@name='A']", e.stanza) is not None

    # Now the remote end decides to change something
    node = jp.SetIq(jt2.peer, jt2.jid, [
        jp.Jingle(jt2.sid, jt2.peer, 'description-info', [
            jp.Content('stream1', 'initiator', 'both', [
                jp.Description('audio', [
                    jp.PayloadType(name, str(rate), str(id)) for
                        (name, id, rate) in jt2.audio_codecs ]) ]) ]) ])
    stream.send(jp.xml(node))

    e = q.expect('dbus-signal', signal='SetRemoteCodecs')
    assert jt2.audio_codecs == [ (name, id, rate)
        for id, name, type, rate, channels, parameters in unwrap(e.args[0]) ]

    # We close the session by removing the stream
    media_iface.RemoveStreams([id1])

    e = q.expect('stream-iq', iq_type='set', predicate=lambda x:
        xpath.queryForNodes("/iq/jingle[@action='session-terminate']",
            x.stanza))

    conn.Disconnect()
    q.expect('dbus-signal', signal='StatusChanged', args=[2, 1])

    return True

if __name__ == '__main__':
    exec_test(test)

