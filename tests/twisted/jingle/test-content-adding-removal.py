"""
Test content adding and removal during the session. We start
session with only one stream, then add one more, then remove
the first one and lastly remove the second stream, which
closes the session.
"""

from gabbletest import make_result_iq, sync_stream
from servicetest import (
    wrap_channel, make_channel_proxy, assertEquals, EventPattern)
from jingletest2 import (
    JingleTest2, test_dialects, JingleProtocol031, JingleProtocol015,
    )
import constants as cs

def gabble_terminates(jp, q, bus, conn, stream):
    test(jp, q, bus, conn, stream, False)

def peer_terminates(jp, q, bus, conn, stream):
    test(jp, q, bus, conn, stream, True)

def test(jp, q, bus, conn, stream, peer_removes_final_content):
    jt = JingleTest2(jp, conn, q, stream, 'test@localhost', 'foo@bar.com/Foo')
    jt.prepare()

    handle = conn.RequestHandles(cs.HT_CONTACT, [jt.peer])[0]
    path = conn.RequestChannel(
        cs.CHANNEL_TYPE_STREAMED_MEDIA, cs.HT_CONTACT, handle, True)

    chan = wrap_channel(bus.get_object(conn.bus_name, path), 'StreamedMedia')

    chan.StreamedMedia.RequestStreams(handle, [cs.MEDIA_STREAM_TYPE_AUDIO])

    # S-E gets notified about new session handler, and calls Ready on it
    e = q.expect('dbus-signal', signal='NewSessionHandler')
    assert e.args[1] == 'rtp'

    session_handler = make_channel_proxy(conn, e.args[0], 'Media.SessionHandler')
    session_handler.Ready()

    e = q.expect('dbus-signal', signal='NewStreamHandler')
    stream_id = e.args[1]

    stream_handler = make_channel_proxy(conn, e.args[0], 'Media.StreamHandler')

    stream_handler.NewNativeCandidate("fake", jt.get_remote_transports_dbus())

    # Before sending the initiate, request another stream

    chan.StreamedMedia.RequestStreams(handle, [cs.MEDIA_STREAM_TYPE_VIDEO])

    e = q.expect('dbus-signal', signal='NewStreamHandler')
    stream_id2 = e.args[1]

    stream_handler2 = make_channel_proxy(conn, e.args[0], 'Media.StreamHandler')
    stream_handler2.NewNativeCandidate("fake", jt.get_remote_transports_dbus())

    # We set both streams as ready, which will trigger the session initiate
    stream_handler.Ready(jt.get_audio_codecs_dbus())
    stream_handler.StreamState(cs.MEDIA_STREAM_STATE_CONNECTED)
    stream_handler2.Ready(jt.get_audio_codecs_dbus())
    stream_handler2.StreamState(cs.MEDIA_STREAM_STATE_CONNECTED)

    # We changed our mind locally, don't want video
    chan.StreamedMedia.RemoveStreams([stream_id2])

    e = q.expect('stream-iq', predicate=jp.action_predicate('session-initiate'))
    stream.send(make_result_iq(stream, e.stanza))

    jt.parse_session_initiate(e.query)

    # Gabble sends content-remove for the video stream...
    e2 = q.expect('stream-iq', predicate=jp.action_predicate('content-remove'))

    # ...but before the peer notices, they accept the call.
    jt.accept()

    # Only now the remote end removes the video stream; if gabble mistakenly
    # marked it as accepted on session acceptance, it'll crash right about
    # now. If it's good, stream will be really removed, and
    # we can proceed.
    stream.send(make_result_iq(stream, e2.stanza))

    q.expect('dbus-signal', signal='StreamRemoved')

    # Actually, we *do* want video!
    chan.StreamedMedia.RequestStreams(handle, [cs.MEDIA_STREAM_TYPE_VIDEO])

    e = q.expect('dbus-signal', signal='NewStreamHandler')
    stream2_id = e.args[1]

    stream_handler2 = make_channel_proxy(conn, e.args[0], 'Media.StreamHandler')

    stream_handler2.NewNativeCandidate("fake", jt.get_remote_transports_dbus())
    stream_handler2.Ready(jt.get_audio_codecs_dbus())
    stream_handler2.StreamState(cs.MEDIA_STREAM_STATE_CONNECTED)

    e = q.expect('stream-iq', predicate=jp.action_predicate('content-add'))
    c = e.query.firstChildElement()
    assertEquals('initiator', c['creator'])
    stream.send(make_result_iq(stream, e.stanza))

    # Peer accepts
    jt.content_accept(e.query, 'video')

    # Let's start sending and receiving video!
    q.expect_many(
        EventPattern('dbus-signal', signal='SetStreamPlaying', args=[True]),
        EventPattern('dbus-signal', signal='SetStreamSending', args=[True]),
        )

    # Now, the call draws to a close.
    # We first remove the original stream
    chan.StreamedMedia.RemoveStreams([stream_id])

    e = q.expect('stream-iq', predicate=jp.action_predicate('content-remove'))
    content_remove_ack = make_result_iq(stream, e.stanza)

    if peer_removes_final_content:
        # The peer removes the final countdo^W content. From a footnote (!) in
        # XEP 0166:
        #     If the content-remove results in zero content definitions for the
        #     session, the entity that receives the content-remove SHOULD send
        #     a session-terminate action to the other party (since a session
        #     with no content definitions is void).
        # So, Gabble should respond to the content-remove with a
        # session-terminate.
        node = jp.SetIq(jt.peer, jt.jid, [
            jp.Jingle(jt.sid, jt.peer, 'content-remove', [
                jp.Content(c['name'], c['creator'], c['senders'], []) ]) ])
        stream.send(jp.xml(node))
    else:
        # The Telepathy client removes the second stream; Gabble should
        # terminate the session rather than sending a content-remove.
        chan.StreamedMedia.RemoveStreams([stream2_id])

    st, closed = q.expect_many(
        EventPattern('stream-iq',
            predicate=jp.action_predicate('session-terminate')),
        # Gabble shouldn't wait for the peer to ack the terminate before
        # considering the call finished.
        EventPattern('dbus-signal', signal='Closed', path=path))

    # Only now does the peer ack the content-remove. This serves as a
    # regression test for contents outliving the session; if the content didn't
    # die properly, this crashed Gabble.
    stream.send(content_remove_ack)
    sync_stream(q, stream)

    # The peer can ack the terminate too, just for completeness.
    stream.send(make_result_iq(stream, st.stanza))

if __name__ == '__main__':
    test_dialects(gabble_terminates, [JingleProtocol015, JingleProtocol031])
    test_dialects(peer_terminates, [JingleProtocol015, JingleProtocol031])

