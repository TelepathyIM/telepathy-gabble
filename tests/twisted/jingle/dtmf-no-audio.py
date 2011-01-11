"""
Test inability to send DTMF events on a video-only call.
"""

from twisted.words.xish import xpath

from gabbletest import make_result_iq
from servicetest import (call_async,
    wrap_channel, make_channel_proxy, EventPattern, sync_dbus)
import constants as cs

from jingletest2 import JingleTest2, test_all_dialects

def test(jp, q, bus, conn, stream):
    if not jp.can_do_video_only():
        return

    remote_jid = 'foo@bar.com/Foo'
    jt = JingleTest2(jp, conn, q, stream, 'test@localhost', remote_jid)
    jt.prepare()

    self_handle = conn.GetSelfHandle()
    handle = conn.RequestHandles(cs.HT_CONTACT, [remote_jid])[0]

    chan_path = conn.RequestChannel(cs.CHANNEL_TYPE_STREAMED_MEDIA,
            cs.HT_CONTACT, handle, True)
    chan = wrap_channel(bus.get_object(conn.bus_name, chan_path),
            'StreamedMedia', ['MediaSignalling', 'Group', 'CallState', 'DTMF'])
    chan_props = chan.Properties.GetAll(cs.CHANNEL)
    assert cs.CHANNEL_IFACE_DTMF in chan_props['Interfaces'], \
        chan_props['Interfaces']

    chan.StreamedMedia.RequestStreams(handle, [cs.MEDIA_STREAM_TYPE_VIDEO])

    # S-E gets notified about new session handler, and calls Ready on it
    e = q.expect('dbus-signal', signal='NewSessionHandler')
    assert e.args[1] == 'rtp'

    session_handler = make_channel_proxy(conn, e.args[0], 'Media.SessionHandler')
    session_handler.Ready()

    e = q.expect('dbus-signal', signal='NewStreamHandler')
    video_path = e.args[0]
    stream_handler = make_channel_proxy(conn, video_path, 'Media.StreamHandler')

    stream_handler.NewNativeCandidate("fake", jt.get_remote_transports_dbus())
    stream_handler.Ready(jt.get_video_codecs_dbus())
    stream_handler.StreamState(cs.MEDIA_STREAM_STATE_CONNECTED)

    e = q.expect('stream-iq', predicate=jp.action_predicate('session-initiate'))
    stream.send(make_result_iq(stream, e.stanza))

    jt.parse_session_initiate(e.query)

    jt.accept()

    # Gabble tells s-e to start sending
    q.expect('dbus-signal', signal='SetStreamSending', args=[True],
            path=video_path)

    # We don't actually have an audio stream, so this is a non-starter.
    call_async(q, chan.DTMF, 'StartTone', 666, 3)
    q.expect('dbus-error', method='StartTone', name=cs.NOT_AVAILABLE)
    call_async(q, chan.DTMF, 'MultipleTones', '**666##')
    q.expect('dbus-error', method='MultipleTones', name=cs.NOT_AVAILABLE)

    # We can still stop all the tones that are playing (a no-op).
    call_async(q, chan.DTMF, 'StopTone', 666)
    q.expect('dbus-return', method='StopTone')

    chan.Group.RemoveMembers([self_handle], 'closed')
    e = q.expect('dbus-signal', signal='Closed', path=chan_path)

if __name__ == '__main__':
    test_all_dialects(test)
