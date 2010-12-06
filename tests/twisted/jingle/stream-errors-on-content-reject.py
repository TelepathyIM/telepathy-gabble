"""
Test StreamError events when new content is rejected in-call.
"""

import dbus

from gabbletest import make_result_iq, sync_stream, exec_test
from servicetest import (
    make_channel_proxy, unwrap, EventPattern, assertEquals, assertLength)
from jingletest2 import JingleTest2, JingleProtocol031
import constants as cs

from twisted.words.xish import xpath

def _content_reject_predicate(event):
    reason = xpath.queryForNodes("/iq"
                               "/jingle[@action='content-reject']"
                               "/reason/failed-application",
                               event.stanza)

    return bool(reason)

def _start_audio_session(jp, q, bus, conn, stream, incoming):
    jt = JingleTest2(jp, conn, q, stream, 'test@localhost', 'foo@bar.com/Foo')
    jt.prepare()

    self_handle = conn.GetSelfHandle()
    remote_handle = conn.RequestHandles(cs.HT_CONTACT, [jt.peer])[0]

    if incoming:
        jt.incoming_call()
    else:
        ret = conn.Requests.CreateChannel(
            { cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_STREAMED_MEDIA,
              cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
              cs.TARGET_HANDLE: remote_handle,
              cs.INITIAL_AUDIO: True
              })

    nc, e = q.expect_many(
        EventPattern('dbus-signal', signal='NewChannel',
            predicate=lambda e: cs.CHANNEL_TYPE_CONTACT_LIST not in e.args),
        EventPattern('dbus-signal', signal='NewSessionHandler'))

    path = nc.args[0]

    media_chan = make_channel_proxy(conn, path, 'Channel.Interface.Group')
    media_iface = make_channel_proxy(conn, path, 'Channel.Type.StreamedMedia')

    # S-E was notified about new session handler, and calls Ready on it
    session_handler = make_channel_proxy(conn, e.args[0],
                                         'Media.SessionHandler')
    session_handler.Ready()

    nsh_event = q.expect('dbus-signal', signal='NewStreamHandler')

    # S-E gets notified about a newly-created stream
    stream_handler = make_channel_proxy(conn, nsh_event.args[0],
        'Media.StreamHandler')

    group_props = media_chan.GetAll(
        cs.CHANNEL_IFACE_GROUP, dbus_interface=dbus.PROPERTIES_IFACE)

    if incoming:
        assertEquals([remote_handle], group_props['Members'])
        assertEquals(unwrap(group_props['LocalPendingMembers']),
                     [(self_handle, remote_handle, cs.GC_REASON_INVITED, '')])
    else:
        assertEquals([self_handle], group_props['Members'])

    streams = media_chan.ListStreams(
            dbus_interface=cs.CHANNEL_TYPE_STREAMED_MEDIA)

    stream_id = streams[0][0]

    stream_handler.NewNativeCandidate("fake", jt.get_remote_transports_dbus())
    stream_handler.Ready(jt.dbusify_codecs([("FOO", 5, 8000, {})]))

    msg = u"None of the codecs are good for us, damn!"

    expected_events = []

    if incoming:
        stream_handler.StreamState(cs.MEDIA_STREAM_STATE_CONNECTED)
        stream_handler.SupportedCodecs(jt.get_audio_codecs_dbus())

        e = q.expect('stream-iq', predicate=jp.action_predicate('transport-info'))
        assertEquals(jt.peer, e.query['initiator'])
        content = xpath.queryForNodes('/iq/jingle/content', e.stanza)[0]
        assertEquals('initiator', content['creator'])

        stream.send(make_result_iq(stream, e.stanza))

        media_chan.AddMembers([self_handle], 'accepted')

        memb, acc, _, _, _ = q.expect_many(
            EventPattern('dbus-signal', signal='MembersChanged',
                         args=[u'', [self_handle], [], [], [], self_handle,
                               cs.GC_REASON_NONE]),
            EventPattern('stream-iq',
                         predicate=jp.action_predicate('session-accept')),
            EventPattern('dbus-signal', signal='SetStreamSending',
                         args=[True]),
            EventPattern('dbus-signal', signal='SetStreamPlaying',
                         args=[True]),
            EventPattern('dbus-signal', signal='StreamDirectionChanged',
                         args=[stream_id,
                               cs.MEDIA_STREAM_DIRECTION_BIDIRECTIONAL, 0]))

        stream.send(make_result_iq(stream, acc.stanza))

        active_event = jp.rtp_info_event("active")
        if active_event is not None:
            q.expect_many(active_event)

        members = media_chan.GetMembers()
        assert set(members) == set([self_handle, remote_handle]), members
    else:
        stream_handler.StreamState(cs.MEDIA_STREAM_STATE_CONNECTED)
        session_initiate = q.expect(
            'stream-iq',
            predicate=jp.action_predicate('session-initiate'))

        q.expect('dbus-signal', signal='MembersChanged', path=path,
                 args=['', [], [], [], [remote_handle], self_handle,
                       cs.GC_REASON_INVITED])

        jt.parse_session_initiate(session_initiate.query)
        stream.send(jp.xml(jp.ResultIq('test@localhost',
                                       session_initiate.stanza, [])))

        jt.accept()

        q.expect_many(
            EventPattern('stream-iq', iq_type='result'),
            # Call accepted
            EventPattern('dbus-signal', signal='MembersChanged',
                         args=['', [remote_handle], [], [], [], remote_handle,
                               cs.GC_REASON_NONE]),
            )
    return jt, media_iface

def _start_audio_session_outgoing(jp, q, bus, conn, stream):
    return _start_audio_session(jp, q, bus, conn, stream, False)

def _start_audio_session_incoming(jp, q, bus, conn, stream):
    return _start_audio_session(jp, q, bus, conn, stream, True)

def _remote_content_add(jp, q, bus, conn, stream, initiate_call_func):
    jt, chan = initiate_call_func(jp, q, bus, conn, stream)

    video_codecs = [
        jp.PayloadType(name, str(rate), str(id), parameters) \
            for (name, id, rate, parameters) in jt.video_codecs]

    node = jp.SetIq(jt.peer, jt.jid, [
            jp.Jingle(jt.sid, jt.peer, 'content-add', [
                    jp.Content(
                        'videostream', 'initiator', 'both',
                        jp.Description('video', video_codecs),
                        jp.TransportGoogleP2P()) ]) ])
    stream.send(jp.xml(node))

    _, nsh = q.expect_many(
        EventPattern('dbus-signal', signal='StreamAdded'),
        EventPattern('dbus-signal', signal='NewStreamHandler'))

    stream_handler_path, stream_id, media_type, direction = nsh.args

    video_handler = make_channel_proxy(conn, stream_handler_path,
                                       'Media.StreamHandler')

    video_handler.NewNativeCandidate("fake",
                                     jt.get_remote_transports_dbus())
    video_handler.Ready(jt.dbusify_codecs([("FOO", 5, 8000, {})]))

    msg = u"None of the codecs are good for us, damn!"

    video_handler.Error(cs.MEDIA_STREAM_ERROR_CODEC_NEGOTIATION_FAILED, msg)

    q.expect_many(
        EventPattern('dbus-signal', signal='StreamError',
                     args=[stream_id,
                           cs.MEDIA_STREAM_ERROR_CODEC_NEGOTIATION_FAILED,
                           msg]),
        EventPattern('stream-iq', predicate=_content_reject_predicate))

def _local_content_add(jp, q, bus, conn, stream, initiate_call_func):
    jt, chan = initiate_call_func(jp, q, bus, conn, stream)

    remote_handle = conn.RequestHandles(cs.HT_CONTACT, [jt.peer])[0]

    chan.RequestStreams(remote_handle, [cs.MEDIA_STREAM_TYPE_VIDEO])

    nsh = q.expect('dbus-signal', signal='NewStreamHandler')
    stream_handler_path, stream_id, media_type, direction = nsh.args
    video_handler = make_channel_proxy(conn, stream_handler_path,
                                       'Media.StreamHandler')

    video_handler.NewNativeCandidate("fake", jt.get_remote_transports_dbus())
    video_handler.Ready(jt.get_audio_codecs_dbus())
    video_handler.StreamState(cs.MEDIA_STREAM_STATE_CONNECTED)

    e = q.expect('stream-iq', predicate=jp.action_predicate('content-add'))
    c = e.query.firstChildElement()
    stream.send(make_result_iq(stream, e.stanza))

    node = jp.SetIq(jt.peer, jt.jid, [
            jp.Jingle(jt.sid, jt.peer, 'content-reject', [
                    ('reason', None, {}, [
                            ('failed-application', None, {}, [])]),
                    jp.Content(c['name'], c['creator'], c['senders']) ]) ])
    stream.send(jp.xml(node))

    q.expect('dbus-signal', signal='StreamError',
             args=[stream_id,
                   cs.MEDIA_STREAM_ERROR_CODEC_NEGOTIATION_FAILED,
                   ""]),

def test_remote_content_add_incoming(jp, q, bus, conn, stream):
    _remote_content_add(jp, q, bus, conn, stream,
                        _start_audio_session_incoming)

def test_remote_content_add_outgoing(jp, q, bus, conn, stream):
    _remote_content_add(jp, q, bus, conn, stream,
                        _start_audio_session_outgoing)

def test_local_content_add_incoming(jp, q, bus, conn, stream):
    _local_content_add(jp, q, bus, conn, stream, _start_audio_session_incoming)

def test_local_content_add_outgoing(jp, q, bus, conn, stream):
    _local_content_add(jp, q, bus, conn, stream, _start_audio_session_outgoing)

if __name__ == '__main__':
    for f in (test_local_content_add_incoming,
              test_local_content_add_outgoing,
              test_remote_content_add_incoming,
              test_remote_content_add_outgoing):
        exec_test(
            lambda q, b, c, s: f(JingleProtocol031(), q, b, c, s))
