"""
Test that we can accept streams added after the call has been accepted.
"""

from servicetest import (
    make_channel_proxy, EventPattern, sync_dbus, call_async,
    assertEquals,
    )
from gabbletest import exec_test, make_result_iq, sync_stream
import constants as cs

from jingletest2 import JingleProtocol031, JingleTest2

def test(q, bus, conn, stream):
    remote_jid = 'foo@bar.com/Foo'
    jp = JingleProtocol031()
    jt2 = JingleTest2(jp, conn, q, stream, 'test@localhost', remote_jid)
    jt2.prepare()

    self_handle = conn.GetSelfHandle()
    remote_handle = conn.RequestHandles(cs.HT_CONTACT, [remote_jid])[0]

    # Remote end calls us
    node = jp.SetIq(jt2.peer, jt2.jid, [
        jp.Jingle(jt2.sid, jt2.peer, 'session-initiate', [
            jp.Content('audiostream', 'initiator', 'both', [
                jp.Description('audio', [
                    jp.PayloadType(name, str(rate), str(id)) for
                        (name, id, rate) in jt2.audio_codecs ]),
            jp.TransportGoogleP2P() ]) ]) ])
    stream.send(jp.xml(node))

    nc = q.expect('dbus-signal', signal='NewChannel')
    path, ct, ht, h, sh = nc.args
    assert ct == cs.CHANNEL_TYPE_STREAMED_MEDIA, ct
    assert ht == cs.HT_CONTACT, ht
    assert h == remote_handle, h

    group = make_channel_proxy(conn, path, 'Channel.Interface.Group')
    sm = make_channel_proxy(conn, path, 'Channel.Type.StreamedMedia')
    ms = make_channel_proxy(conn, path, 'Channel.Interface.MediaSignalling')

    streams = sm.ListStreams()
    assert len(streams) == 1, streams
    audio_stream_id, h, media_type, state, direction, pending = streams[0]
    assert h == remote_handle, (h, remote_handle)
    assert media_type == cs.MEDIA_STREAM_TYPE_AUDIO, media_type
    assert state == cs.MEDIA_STREAM_STATE_DISCONNECTED, state
    # FIXME: This turns out to be Bidirectional; wjt thinks this sounds wrong
    #        since the stream is (we hope) pending local send.
    #assert direction == cs.MEDIA_STREAM_DIRECTION_RECEIVE, direction
    assert pending == cs.MEDIA_STREAM_PENDING_LOCAL_SEND, pending

    session_handlers = ms.GetSessionHandlers()
    assert len(session_handlers) == 1, session_handlers
    session_handler = make_channel_proxy(conn, session_handlers[0][0],
        'Media.SessionHandler')
    session_handler.Ready()

    nsh = q.expect('dbus-signal', signal='NewStreamHandler')
    stream_handler_path, stream_id, media_type, direction = nsh.args
    assert stream_id == audio_stream_id, (stream_id, audio_stream_id)
    assert media_type == cs.MEDIA_STREAM_TYPE_AUDIO, media_type
    # FIXME: As above
    #assert direction == cs.MEDIA_STREAM_DIRECTION_RECEIVE, direction

    stream_handler = make_channel_proxy(conn, stream_handler_path,
        'Media.StreamHandler')

    stream_handler.NewNativeCandidate("fake", jt2.get_remote_transports_dbus())
    stream_handler.Ready(jt2.get_audio_codecs_dbus())
    stream_handler.StreamState(cs.MEDIA_STREAM_STATE_CONNECTED)
    stream_handler.SupportedCodecs(jt2.get_audio_codecs_dbus())

    # peer gets the transport
    e = q.expect('stream-iq', predicate=jp.action_predicate('transport-info'))
    assertEquals(remote_jid, e.query['initiator'])

    stream.send(make_result_iq(stream, e.stanza))

    # Make sure all the above's happened.
    sync_stream(q, stream)
    sync_dbus(bus, q, conn)

    # At last, accept the call
    group.AddMembers([self_handle], 'accepted')

    # Call is accepted, we become a member, and the stream that was pending
    # local send is now sending.
    memb, acc, _, _, _,  = q.expect_many(
        EventPattern('dbus-signal', signal='MembersChanged',
            args=[u'', [self_handle], [], [], [],
                        self_handle,
                        cs.GC_REASON_NONE]),
        EventPattern('stream-iq',
            predicate=lambda e: (e.query.name == 'jingle' and
                e.query['action'] == 'session-accept')),
        EventPattern('dbus-signal', signal='SetStreamSending', args=[True]),
        EventPattern('dbus-signal', signal='SetStreamPlaying', args=[True]),
        EventPattern('dbus-signal', signal='StreamDirectionChanged',
            args=[audio_stream_id, cs.MEDIA_STREAM_DIRECTION_BIDIRECTIONAL, 0]),
        )

    # Respond to session-accept
    # FIXME: wjt thinks Gabble should accept the content-add without this.
    stream.send(jp.xml(jp.ResultIq('test@localhost', acc.stanza, [])))

    # Foo would like to gaze upon our beautiful complexion
    node = jp.SetIq(jt2.peer, jt2.jid, [
        jp.Jingle(jt2.sid, jt2.peer, 'content-add', [
            jp.Content('videostream', 'initiator', 'both', [
                jp.Description('video', [
                    jp.PayloadType(name, str(rate), str(id)) for
                        (name, id, rate) in jt2.video_codecs ]),
            jp.TransportGoogleP2P() ]) ]) ])
    stream.send(jp.xml(node))

    added, nsh = q.expect_many(
        EventPattern('dbus-signal', signal='StreamAdded'),
        EventPattern('dbus-signal', signal='NewStreamHandler'),
        )

    video_stream_id, h, type = added.args
    assert h == remote_handle, (h, remote_handle)
    assert type == cs.MEDIA_STREAM_TYPE_VIDEO, type

    stream_handler_path, stream_id, media_type, direction = nsh.args
    assert stream_id == video_stream_id, (stream_id, video_stream_id)
    assert media_type == cs.MEDIA_STREAM_TYPE_VIDEO, type
    # FIXME: As above
    #assert direction == cs.MEDIA_STREAM_DIRECTION_RECEIVE, direction

    video_handler = make_channel_proxy(conn, stream_handler_path,
        'Media.StreamHandler')

    video_handler.NewNativeCandidate("fake", jt2.get_remote_transports_dbus())
    video_handler.Ready(jt2.get_video_codecs_dbus())
    video_handler.StreamState(cs.MEDIA_STREAM_STATE_CONNECTED)
    video_handler.SupportedCodecs(jt2.get_video_codecs_dbus())

    ti, _, _, _ = q.expect_many(
        # Peer gets the transport
        EventPattern('stream-iq',
            predicate=jp.action_predicate('transport-info')),
        # Gabble tells the peer we accepted
        EventPattern('stream-iq',
            predicate=jp.action_predicate('content-accept')),
        EventPattern('dbus-signal', signal='SetStreamPlaying', args=[True]),
        # It's not entirely clear that this *needs* to fire here...
        EventPattern('dbus-signal', signal='SetStreamSending', args=[False]),
        )
    assertEquals(remote_jid, ti.query['initiator'])

    stream.send(make_result_iq(stream, e.stanza))

    # Okay, so now the stream's playing but not sending, and we should be still
    # pending local send:
    streams = sm.ListStreams()
    assert len(streams) == 2, streams
    video_streams = [s for s in streams if s[2] == cs.MEDIA_STREAM_TYPE_VIDEO]
    assert len(video_streams) == 1, streams
    stream_id, h, _, state, direction, pending = video_streams[0]
    assert stream_id == video_stream_id, (stream_id, video_stream_id)
    assert h == remote_handle, (h, remote_handle)
    assert state == cs.MEDIA_STREAM_STATE_CONNECTED, state
    assert direction == cs.MEDIA_STREAM_DIRECTION_RECEIVE, direction
    assert pending == cs.MEDIA_STREAM_PENDING_LOCAL_SEND, pending

    # Let's accept the stream; the direction should change, and we should be
    # told to start sending.
    call_async(q, sm, 'RequestStreamDirection', video_stream_id,
        cs.MEDIA_STREAM_DIRECTION_BIDIRECTIONAL)

    # The stream's direction should change, and we should be told to start
    # playing.
    q.expect_many(
        EventPattern('dbus-signal', signal='StreamDirectionChanged',
            args=[video_stream_id, cs.MEDIA_STREAM_DIRECTION_BIDIRECTIONAL, 0]),
        EventPattern('dbus-signal', signal='SetStreamSending', args=[True]),
        )

    # That'll do, pig. That'll do.

if __name__ == '__main__':
    exec_test(test)
