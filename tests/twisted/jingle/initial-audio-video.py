"""
Tests outgoing calls created with InitialAudio and/or InitialVideo, and
exposing the initial contents of incoming calls as values of InitialAudio and
InitialVideo
"""

import operator

from servicetest import (
    assertContains, assertEquals, assertLength,
    wrap_channel, EventPattern, call_async, make_channel_proxy)

from jingletest2 import JingleTest2, test_all_dialects

import constants as cs

def outgoing(jp, q, bus, conn, stream):
    remote_jid = 'flames@cold.mountain/beyond'
    jt = JingleTest2(jp, conn, q, stream, 'test@localhost', remote_jid)
    jt.prepare()

    self_handle = conn.GetSelfHandle()
    remote_handle = conn.RequestHandles(cs.HT_CONTACT, [remote_jid])[0]

    rccs = conn.Properties.Get(cs.CONN_IFACE_REQUESTS, 'RequestableChannelClasses')
    media_classes = [ rcc for rcc in rccs
        if rcc[0][cs.CHANNEL_TYPE] == cs.CHANNEL_TYPE_STREAMED_MEDIA ]

    assertLength(1, media_classes)
    fixed, allowed = media_classes[0]
    assertContains(cs.INITIAL_AUDIO, allowed)
    assertContains(cs.INITIAL_VIDEO, allowed)

    check_neither(q, conn, bus, stream, remote_handle)
    check_iav(jt, q, conn, bus, stream, remote_handle, True, False)
    check_iav(jt, q, conn, bus, stream, remote_handle, False, True)
    check_iav(jt, q, conn, bus, stream, remote_handle, True, True)

def check_neither(q, conn, bus, stream, remote_handle):
    """
    Make a channel without specifying InitialAudio or InitialVideo; check
    that it's announced with both False, and that they're both present and
    false in GetAll().
    """

    path, props = conn.Requests.CreateChannel({
        cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_STREAMED_MEDIA,
        cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
        cs.TARGET_HANDLE: remote_handle})

    assertContains((cs.INITIAL_AUDIO, False), props.items())
    assertContains((cs.INITIAL_VIDEO, False), props.items())

    chan = wrap_channel(bus.get_object(conn.bus_name, path),
        cs.CHANNEL_TYPE_STREAMED_MEDIA, ['MediaSignalling'])
    props = chan.Properties.GetAll(cs.CHANNEL_TYPE_STREAMED_MEDIA + '.FUTURE')
    assertContains(('InitialAudio', False), props.items())
    assertContains(('InitialVideo', False), props.items())

    # We shouldn't have started a session yet, so there shouldn't be any
    # session handlers. Strictly speaking, there could be a session handler
    # with no stream handlers, but...
    session_handlers = chan.MediaSignalling.GetSessionHandlers()
    assertLength(0, session_handlers)

def check_iav(jt, q, conn, bus, stream, remote_handle, initial_audio,
              initial_video):
    """
    Make a channel and check that its InitialAudio and InitialVideo properties
    come out correctly.
    """

    call_async(q, conn.Requests, 'CreateChannel', {
        cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_STREAMED_MEDIA,
        cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
        cs.TARGET_HANDLE: remote_handle,
        cs.INITIAL_AUDIO: initial_audio,
        cs.INITIAL_VIDEO: initial_video,
        })
    if initial_video and (not jt.jp.can_do_video()
            or (not initial_audio and not jt.jp.can_do_video_only ())):
        # Some protocols can't do video
        event = q.expect('dbus-error', method='CreateChannel')
        assertEquals(cs.NOT_CAPABLE, event.error.get_dbus_name())
    else:
        path, props = q.expect('dbus-return', method='CreateChannel').value

        assertContains((cs.INITIAL_AUDIO, initial_audio), props.items())
        assertContains((cs.INITIAL_VIDEO, initial_video), props.items())

        chan = wrap_channel(bus.get_object(conn.bus_name, path),
            cs.CHANNEL_TYPE_STREAMED_MEDIA, ['MediaSignalling'])
        props = chan.Properties.GetAll(cs.CHANNEL_TYPE_STREAMED_MEDIA + '.FUTURE')
        assertContains(('InitialAudio', initial_audio), props.items())
        assertContains(('InitialVideo', initial_video), props.items())

        session_handlers = chan.MediaSignalling.GetSessionHandlers()

        assertLength(1, session_handlers)
        path, type = session_handlers[0]
        assertEquals('rtp', type)
        session_handler = make_channel_proxy(conn, path, 'Media.SessionHandler')
        session_handler.Ready()

        stream_handler_paths = []
        stream_handler_types = []

        for x in [initial_audio, initial_video]:
            if x:
                e = q.expect('dbus-signal', signal='NewStreamHandler')
                stream_handler_paths.append(e.args[0])
                stream_handler_types.append(e.args[2])

        if initial_audio:
            assertContains(cs.MEDIA_STREAM_TYPE_AUDIO, stream_handler_types)

        if initial_video:
            assertContains(cs.MEDIA_STREAM_TYPE_VIDEO, stream_handler_types)

        for x in xrange (0, len(stream_handler_paths)):
            p = stream_handler_paths[x]
            t = stream_handler_types[x]
            sh = make_channel_proxy(conn, p, 'Media.StreamHandler')
            sh.NewNativeCandidate("fake", jt.get_remote_transports_dbus())
            if t == cs.MEDIA_STREAM_TYPE_AUDIO:
                sh.Ready(jt.get_audio_codecs_dbus())
            else:
                sh.Ready(jt.get_video_codecs_dbus())
            sh.StreamState(cs.MEDIA_STREAM_STATE_CONNECTED)

        e = q.expect('stream-iq',
            predicate=jt.jp.action_predicate('session-initiate'))
        jt.parse_session_initiate (e.query)

        jt.accept()

        events = reduce(operator.concat,
            [ [ EventPattern('dbus-signal', signal='SetRemoteCodecs', path=p),
                EventPattern('dbus-signal', signal='SetStreamPlaying', path=p),
              ] for p in stream_handler_paths
            ], [])
        q.expect_many(*events)

        chan.Close()

def incoming(jp, q, bus, conn, stream):
    remote_jid = 'skinny.fists@heaven/antennas'
    jt = JingleTest2(jp, conn, q, stream, 'test@localhost', remote_jid)
    jt.prepare()

    self_handle = conn.GetSelfHandle()
    remote_handle = conn.RequestHandles(cs.HT_CONTACT, [remote_jid])[0]

    for a, v in [(True, False), (False, True), (True, True)]:
        if v and not jp.can_do_video():
            continue
        if not a and v and not jp.can_do_video_only():
            continue

        jt.incoming_call(audio=a, video=v)
        e = q.expect('dbus-signal', signal='NewChannels')
        chans = e.args[0]
        assertLength(1, chans)

        path, props = chans[0]

        assertEquals(cs.CHANNEL_TYPE_STREAMED_MEDIA, props[cs.CHANNEL_TYPE])
        assertEquals(a, props[cs.INITIAL_AUDIO])
        assertEquals(v, props[cs.INITIAL_VIDEO])

        chan = wrap_channel(bus.get_object(conn.bus_name, path),
            cs.CHANNEL_TYPE_STREAMED_MEDIA)
        chan.Close()


if __name__ == '__main__':
    test_all_dialects(outgoing)
    test_all_dialects(incoming)
