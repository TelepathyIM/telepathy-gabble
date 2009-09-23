"""
Test exposing incoming <hold/>, <ringing/> and <active/> notifications via the
CallState interface.
"""

from twisted.words.xish import xpath

from gabbletest import make_result_iq
from servicetest import (
    wrap_channel, make_channel_proxy, EventPattern, sync_dbus)
import ns
import constants as cs

from jingletest2 import JingleTest2, test_all_dialects

def test(jp, q, bus, conn, stream):
    remote_jid = 'foo@bar.com/Foo'
    jt = JingleTest2(jp, conn, q, stream, 'test@localhost', remote_jid)

    jt.prepare()

    self_handle = conn.GetSelfHandle()
    handle = conn.RequestHandles(cs.HT_CONTACT, [remote_jid])[0]

    path = conn.RequestChannel(cs.CHANNEL_TYPE_STREAMED_MEDIA, cs.HT_CONTACT,
        handle, True)

    chan = wrap_channel(bus.get_object(conn.bus_name, path), 'StreamedMedia',
        ['MediaSignalling', 'Group', 'CallState'])
    chan_props = chan.Properties.GetAll(cs.CHANNEL)
    assert cs.CHANNEL_IFACE_CALL_STATE in chan_props['Interfaces'], \
        chan_props['Interfaces']

    call_states = chan.CallState.GetCallStates()
    assert call_states == { handle: 0 } or \
        call_states == {}, call_states

    chan.StreamedMedia.RequestStreams(handle, [cs.MEDIA_STREAM_TYPE_AUDIO])

    # S-E gets notified about new session handler, and calls Ready on it
    e = q.expect('dbus-signal', signal='NewSessionHandler')
    assert e.args[1] == 'rtp'

    session_handler = make_channel_proxy(conn, e.args[0], 'Media.SessionHandler')
    session_handler.Ready()

    e = q.expect('dbus-signal', signal='NewStreamHandler')
    audio_path = e.args[0]
    stream_handler = make_channel_proxy(conn, audio_path, 'Media.StreamHandler')

    stream_handler.NewNativeCandidate("fake", jt.get_remote_transports_dbus())
    stream_handler.Ready(jt.get_audio_codecs_dbus())
    stream_handler.StreamState(cs.MEDIA_STREAM_STATE_CONNECTED)

    e = q.expect('stream-iq', predicate=jp.action_predicate('session-initiate'))
    stream.send(make_result_iq(stream, e.stanza))

    jt.parse_session_initiate(e.query)

    if jp.is_modern_jingle():
        # The other person's client starts ringing, and tells us so!
        node = jp.SetIq(jt.peer, jt.jid, [
            jp.Jingle(jt.sid, jt.jid, 'session-info', [
                ('ringing', ns.JINGLE_RTP_INFO_1, {}, []) ]) ])
        stream.send(jp.xml(node))

    # If this is an old Jingle dialect, Gabble should treat the
    # session-initiate ack as ringing notification; if it's modern Jingle, we
    # just sent a ringing notification.
    q.expect('dbus-signal', signal='CallStateChanged',
            args=[handle, cs.CALL_STATE_RINGING])

    call_states = chan.CallState.GetCallStates()
    assert call_states == { handle: cs.CALL_STATE_RINGING }, call_states

    if jp.is_modern_jingle():
        # We're waiting in a queue, so the other person's client tells us we're on
        # hold. Gabble should ack the IQ, and set the call state to Ringing | Held.
        # Also, Gabble certainly shouldn't tell s-e to start sending. (Although it
        # might tell it not to; we don't mind.)
        node = jp.SetIq(jt.peer, jt.jid, [
            jp.Jingle(jt.sid, jt.jid, 'session-info', [
                ('hold', ns.JINGLE_RTP_INFO_1, {}, []) ]) ])
        stream.send(jp.xml(node))

        forbidden = [
            EventPattern('dbus-signal', signal='SetStreamSending', args=[True],
                path=audio_path),
                ]
        q.forbid_events(forbidden)

        q.expect_many(
            EventPattern('stream-iq', iq_type='result', iq_id=node[2]['id']),
            EventPattern('dbus-signal', signal='CallStateChanged',
                args=[handle, cs.CALL_STATE_RINGING | cs.CALL_STATE_HELD]),
            )

        call_states = chan.CallState.GetCallStates()
        assert call_states == { handle: cs.CALL_STATE_RINGING | cs.CALL_STATE_HELD }, call_states

        # We're at the head of a queue, so the other person's client tells us we're
        # no longer on hold. The call centre phone's still ringing, though. s-e
        # still shouldn't start sending.
        node = jp.SetIq(jt.peer, jt.jid, [
            jp.Jingle(jt.sid, jt.jid, 'session-info', [
                ('unhold', ns.JINGLE_RTP_INFO_1, {}, []) ]) ])
        stream.send(jp.xml(node))

        q.expect_many(
            EventPattern('stream-iq', iq_type='result', iq_id=node[2]['id']),
            EventPattern('dbus-signal', signal='CallStateChanged',
                args=[handle, cs.CALL_STATE_RINGING]),
            )

        call_states = chan.CallState.GetCallStates()
        assert call_states == { handle: cs.CALL_STATE_RINGING }, call_states

        sync_dbus(bus, q, conn)
        q.unforbid_events(forbidden)

    jt.accept()

    if jp.is_modern_jingle():
        # The other person's client decides it's not ringing any more
        node = jp.SetIq(jt.peer, jt.jid, [
            jp.Jingle(jt.sid, jt.jid, 'session-info', [
                ('active', ns.JINGLE_RTP_INFO_1, {}, []) ]) ])
        stream.send(jp.xml(node))

    # Gabble tells s-e to start sending, and removes the Ringing flag, either
    # because we got <active/> or because of the session-accept in ye olde
    # Jingle.
    q.expect_many(
        EventPattern('dbus-signal', signal='SetStreamSending', args=[True],
            path=audio_path),
        EventPattern('dbus-signal', signal='CallStateChanged',
            args=[ handle, 0 ]),
        )

    call_states = chan.CallState.GetCallStates()
    assert call_states == { handle: 0 } or call_states == {}, call_states

    # The rest of the test concerns things we only support in the glorious
    # modern Jingle future.
    if not jp.is_modern_jingle():
        return

    # The other person puts us on hold.  Gabble should ack the session-info IQ,
    # tell s-e to stop sending on the held stream, and set the call state.
    node = jp.SetIq(jt.peer, jt.jid, [
        jp.Jingle(jt.sid, jt.jid, 'session-info', [
            ('hold', ns.JINGLE_RTP_INFO_1, {}, []) ]) ])
    stream.send(jp.xml(node))

    q.expect_many(
        EventPattern('stream-iq', iq_type='result', iq_id=node[2]['id']),
        EventPattern('dbus-signal', signal='SetStreamSending', args=[False],
            path=audio_path),
        EventPattern('dbus-signal', signal='CallStateChanged',
            args=[handle, cs.CALL_STATE_HELD]),
        )

    call_states = chan.CallState.GetCallStates()
    assert call_states == { handle: cs.CALL_STATE_HELD }, call_states

    # The peer pings us with an empty session-info; Gabble should just ack it.
    node = jp.SetIq(jt.peer, jt.jid, [
        jp.Jingle(jt.sid, jt.jid, 'session-info', [])])
    stream.send(jp.xml(node))

    q.expect('stream-iq', iq_type='result', iq_id=node[2]['id'])

    call_states = chan.CallState.GetCallStates()
    assert call_states == { handle: cs.CALL_STATE_HELD }, call_states

    # The peer sends us some unknown-namespaced misc in a session-info; Gabble
    # should nak it with <unsupported-info/>
    node = jp.SetIq(jt.peer, jt.jid, [
        jp.Jingle(jt.sid, jt.jid, 'session-info', [
            ('boiling', 'com.example.Kettle', {}, []) ]) ])
    stream.send(jp.xml(node))

    e = q.expect('stream-iq', iq_type='error', iq_id=node[2]['id'])
    xpath.queryForNodes("/jingle/error/unsupported-info", e.query)

    call_states = chan.CallState.GetCallStates()
    assert call_states == { handle: cs.CALL_STATE_HELD }, call_states

    # The other person unholds us; Gabble should ack the session-info IQ, tell
    # s-e to start sending on the now-active stream, and set the call state.
    node = jp.SetIq(jt.peer, jt.jid, [
        jp.Jingle(jt.sid, jt.jid, 'session-info', [
            ('active', ns.JINGLE_RTP_INFO_1, {}, []) ]) ])
    stream.send(jp.xml(node))

    q.expect_many(
        EventPattern('stream-iq', iq_type='result', iq_id=node[2]['id']),
        EventPattern('dbus-signal', signal='SetStreamSending', args=[True],
            path=audio_path),
        EventPattern('dbus-signal', signal='CallStateChanged',
            args=[handle, 0]),
        )

    call_states = chan.CallState.GetCallStates()
    assert call_states == { handle: 0 } or call_states == {}, call_states

    # Okay, let's get a second stream involved!

    chan.StreamedMedia.RequestStreams(handle, [cs.MEDIA_STREAM_TYPE_VIDEO])

    e = q.expect('dbus-signal', signal='NewStreamHandler')
    video_path = e.args[0]
    stream_handler2 = make_channel_proxy(conn, video_path, 'Media.StreamHandler')

    stream_handler2.NewNativeCandidate("fake", jt.get_remote_transports_dbus())
    stream_handler2.Ready(jt.get_video_codecs_dbus())
    stream_handler2.StreamState(cs.MEDIA_STREAM_STATE_CONNECTED)

    e = q.expect('stream-iq', predicate=jp.action_predicate('content-add'))
    stream.send(make_result_iq(stream, e.stanza))

    jt.content_accept(e.query, 'video')

    q.expect('dbus-signal', signal='SetStreamSending', args=[True],
        path=video_path)

    call_states = chan.CallState.GetCallStates()
    assert call_states == { handle: 0 } or call_states == {}, call_states

    # The other person puts us on hold.  Gabble should ack the session-info IQ,
    # tell s-e to stop sending on both streams, and set the call state.
    node = jp.SetIq(jt.peer, jt.jid, [
        jp.Jingle(jt.sid, jt.jid, 'session-info', [
            ('hold', ns.JINGLE_RTP_INFO_1, {}, []) ]) ])
    stream.send(jp.xml(node))

    q.expect_many(
        EventPattern('stream-iq', iq_type='result', iq_id=node[2]['id']),
        EventPattern('dbus-signal', signal='SetStreamSending', args=[False],
            path=audio_path),
        EventPattern('dbus-signal', signal='SetStreamSending', args=[False],
            path=video_path),
        EventPattern('dbus-signal', signal='CallStateChanged',
            args=[handle, cs.CALL_STATE_HELD]),
        )

    call_states = chan.CallState.GetCallStates()
    assert call_states == { handle: cs.CALL_STATE_HELD }, call_states

    # Now the other person sets the audio stream to mute. We can't represent
    # mute yet, but Gabble shouldn't take this to mean the call is active, as
    # one stream being muted doesn't change the fact that the call's on hold.
    # FIXME: hardcoded stream id
    node = jp.SetIq(jt.peer, jt.jid, [
        jp.Jingle(jt.sid, jt.jid, 'session-info', [
            ('mute', ns.JINGLE_RTP_INFO_1,
             {'name': 'stream1', 'creator': 'initiator'}, []) ]) ])
    stream.send(jp.xml(node))

    forbidden = [
        EventPattern('dbus-signal', signal='SetStreamSending', args=[True],
            path=audio_path),
        EventPattern('dbus-signal', signal='CallStateChanged',
            args=[ handle, 0 ]),
            ]
    q.forbid_events(forbidden)

    q.expect('stream-iq', iq_type='result', iq_id=node[2]['id'])

    call_states = chan.CallState.GetCallStates()
    assert call_states == { handle: cs.CALL_STATE_HELD }, call_states

    sync_dbus(bus, q, conn)
    q.unforbid_events(forbidden)

    # That'll do, pig.

    chan.Group.RemoveMembers([self_handle], 'closed')
    e = q.expect('dbus-signal', signal='Close') #XXX - match against the path

if __name__ == '__main__':
    test_all_dialects(test)
