"""
Test DTMF events.
"""

from twisted.words.xish import xpath

from gabbletest import make_result_iq
from servicetest import (call_async,
    wrap_channel, make_channel_proxy, EventPattern, sync_dbus)
import constants as cs

from jingletest2 import JingleTest2, test_all_dialects

def test(jp, q, bus, conn, stream):
    # this test uses multiple streams
    if not jp.is_modern_jingle():
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

    jt.accept()

    # Gabble tells s-e to start sending
    q.expect('dbus-signal', signal='SetStreamSending', args=[True],
            path=audio_path)

    chan.StreamedMedia.RequestStreams(handle, [cs.MEDIA_STREAM_TYPE_AUDIO])

    e = q.expect('dbus-signal', signal='NewStreamHandler')
    audio2_path = e.args[0]

    # The Stream_ID is specified to be ignored; we use 666 here.
    call_async(q, chan.DTMF, 'StartTone', 666, 3)
    q.expect_many(
            EventPattern('dbus-signal', signal='StartTelephonyEvent',
                path=audio2_path),
            EventPattern('dbus-signal', signal='StartTelephonyEvent',
                path=audio_path),
            EventPattern('dbus-signal', signal='SendingTones', args=['3'],
                path=chan_path),
            EventPattern('dbus-return', method='StartTone'),
            )

    call_async(q, chan.DTMF, 'StopTone', 666)
    q.expect_many(
            EventPattern('dbus-signal', signal='StopTelephonyEvent',
                path=audio_path),
            EventPattern('dbus-signal', signal='StopTelephonyEvent',
                path=audio2_path),
            EventPattern('dbus-signal', signal='StoppedTones', args=[True],
                path=chan_path),
            EventPattern('dbus-return', method='StopTone'),
            )

    call_async(q, chan.DTMF, 'MultipleTones', '123')
    q.expect_many(
            EventPattern('dbus-signal', signal='StartTelephonyEvent',
                path=audio_path, args=[1]),
            EventPattern('dbus-signal', signal='StartTelephonyEvent',
                path=audio2_path, args=[1]),
            EventPattern('dbus-signal', signal='SendingTones', args=['123'],
                path=chan_path),
            EventPattern('dbus-return', method='MultipleTones'),
            )
    q.expect_many(
            EventPattern('dbus-signal', signal='StopTelephonyEvent',
                path=audio_path),
            EventPattern('dbus-signal', signal='StopTelephonyEvent',
                path=audio2_path),
            )
    q.expect_many(
            EventPattern('dbus-signal', signal='StartTelephonyEvent',
                path=audio_path, args=[2]),
            EventPattern('dbus-signal', signal='StartTelephonyEvent',
                path=audio2_path, args=[2]),
            )
    q.expect_many(
            EventPattern('dbus-signal', signal='StopTelephonyEvent',
                path=audio_path),
            EventPattern('dbus-signal', signal='StopTelephonyEvent',
                path=audio2_path),
            )
    q.expect_many(
            EventPattern('dbus-signal', signal='StartTelephonyEvent',
                path=audio_path, args=[3]),
            EventPattern('dbus-signal', signal='StartTelephonyEvent',
                path=audio2_path, args=[3]),
            )
    q.expect_many(
            EventPattern('dbus-signal', signal='StopTelephonyEvent',
                path=audio_path),
            EventPattern('dbus-signal', signal='StopTelephonyEvent',
                path=audio2_path),
            EventPattern('dbus-signal', signal='StoppedTones', args=[False],
                path=chan_path),
            )

    forbidden = [EventPattern('dbus-signal', signal='StartTelephonyEvent',
        args=[9])]
    q.forbid_events(forbidden)

    # This is technically a race condition, but this dialstring is almost
    # certainly long enough that the Python script will win the race, i.e.
    # cancel before Gabble processes the whole dialstring.
    call_async(q, chan.DTMF, 'MultipleTones',
            '1,1' * 100)
    q.expect('dbus-return', method='MultipleTones')
    call_async(q, chan.DTMF, 'MultipleTones', '9')
    q.expect('dbus-error', method='MultipleTones',
            name=cs.SERVICE_BUSY)
    call_async(q, chan.DTMF, 'StartTone', 666, 9)
    q.expect('dbus-error', method='StartTone', name=cs.SERVICE_BUSY)
    call_async(q, chan.DTMF, 'StopTone', 666)
    q.expect_many(
            EventPattern('dbus-signal', signal='StopTelephonyEvent',
                path=audio_path),
            EventPattern('dbus-signal', signal='StopTelephonyEvent',
                path=audio2_path),
            EventPattern('dbus-signal', signal='StoppedTones', args=[True],
                path=chan_path),
            EventPattern('dbus-return', method='StopTone'),
            )

    q.unforbid_events(forbidden)

    chan.Group.RemoveMembers([self_handle], 'closed')
    e = q.expect('dbus-signal', signal='Closed', path=chan_path)

if __name__ == '__main__':
    test_all_dialects(test)
