"""
Test outgoing call handling. This tests the case when the
remote party rejects our call because they're busy.
"""

from gabbletest import make_result_iq
from servicetest import make_channel_proxy, assertEquals
import constants as cs
from jingletest2 import JingleTest2, test_all_dialects

def _test(jp, q, bus, conn, stream,
          jingle_reason, group_change_reason, stream_error):
    remote_jid = 'foo@bar.com/Foo'
    jt = JingleTest2(jp, conn, q, stream, 'test@localhost', remote_jid)

    jt.prepare()

    self_handle = conn.GetSelfHandle()
    remote_handle = conn.RequestHandles(cs.HT_CONTACT, [remote_jid])[0]

    path = conn.RequestChannel(cs.CHANNEL_TYPE_STREAMED_MEDIA,
        cs.HT_CONTACT, remote_handle, True)

    signalling_iface = make_channel_proxy(conn, path, 'Channel.Interface.MediaSignalling')
    media_iface = make_channel_proxy(conn, path, 'Channel.Type.StreamedMedia')

    media_iface.RequestStreams(remote_handle, [cs.MEDIA_STREAM_TYPE_AUDIO])

    # S-E gets notified about new session handler, and calls Ready on it
    e = q.expect('dbus-signal', signal='NewSessionHandler')
    assert e.args[1] == 'rtp'

    session_handler = make_channel_proxy(conn, e.args[0], 'Media.SessionHandler')
    session_handler.Ready()


    e = q.expect('dbus-signal', signal='NewStreamHandler')

    stream_handler = make_channel_proxy(conn, e.args[0], 'Media.StreamHandler')

    stream_handler.NewNativeCandidate("fake", jt.get_remote_transports_dbus())
    stream_handler.Ready(jt.get_audio_codecs_dbus())
    stream_handler.StreamState(cs.MEDIA_STREAM_STATE_CONNECTED)

    e = q.expect('stream-iq', predicate=jp.action_predicate('session-initiate'))
    stream.send(make_result_iq(stream, e.stanza))

    text = u"begone!"

    jt.parse_session_initiate(e.query)
    jt.terminate(reason=jingle_reason, text=text)

    mc = q.expect('dbus-signal', signal='MembersChanged')
    message, added, removed, lp, rp, actor, reason = mc.args
    assert added == [], added
    assert set(removed) == set([self_handle, remote_handle]), \
        (removed, self_handle, remote_handle)
    assert lp == [], lp
    assert rp == [], rp
    assert actor == remote_handle, (actor, remote_handle)
    if jp.is_modern_jingle():
        assertEquals(text, message)
        assertEquals(group_change_reason, reason)

    if jp.is_modern_jingle() and stream_error:
        se = q.expect('dbus-signal', signal='StreamError')
        assertEquals(stream_error, se.args[1])

    q.expect('dbus-signal', signal='Close') #XXX - match against the path

def test_busy(jp, q, bus, conn, stream):
    _test(jp, q, bus, conn, stream, "busy", cs.GC_REASON_BUSY, None)

def test_codec_fail(jp, q, bus, conn, stream):
    _test(jp, q, bus, conn, stream, "failed-application", cs.GC_REASON_ERROR,
          cs.MEDIA_STREAM_ERROR_CODEC_NEGOTIATION_FAILED)

if __name__ == '__main__':
    test_all_dialects(test_busy)
    test_all_dialects(test_codec_fail)
