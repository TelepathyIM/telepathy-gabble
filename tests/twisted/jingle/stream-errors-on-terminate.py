"""
Test StreamError events and on session terminate, both directions.
"""

import dbus

from gabbletest import make_result_iq, sync_stream, exec_test
from servicetest import (
    make_channel_proxy, unwrap, EventPattern, assertEquals, assertLength)
from jingletest2 import JingleTest2, JingleProtocol031
import constants as cs

from twisted.words.xish import xpath

def _session_terminate_predicate(event, msg):
    reason = xpath.queryForNodes("/iq"
                               "/jingle[@action='session-terminate']"
                               "/reason/failed-application",
                               event.stanza)
    reason_text = xpath.queryForString("/iq/jingle/reason/text",
                                       event.stanza)

    return reason is not None and reason_text == msg

def _test_terminate_reason(jp, q, bus, conn, stream, incoming):
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
        q.expect('dbus-signal', signal='SetRemoteCodecs')
        stream_handler.Error(cs.MEDIA_STREAM_ERROR_CODEC_NEGOTIATION_FAILED,
                             msg)
        expected_events = [EventPattern(
                "stream-iq", iq_type="set",
                predicate=lambda x: _session_terminate_predicate(x, msg))]
        rejector = self_handle
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
        jt.terminate('failed-application', msg)
        rejector = remote_handle

    expected_events += [
        EventPattern('dbus-signal', signal='StreamError',
                     interface=cs.CHANNEL_TYPE_STREAMED_MEDIA, path=path,
                     args=[stream_id,
                           cs.MEDIA_STREAM_ERROR_CODEC_NEGOTIATION_FAILED,
                           msg]),
        EventPattern('dbus-signal', signal='MembersChanged',
                     interface=cs.CHANNEL_IFACE_GROUP, path=path,
                     args=[msg, [], [self_handle, remote_handle], [], [],
                      rejector, cs.GC_REASON_ERROR])]


    q.expect_many(*expected_events)

    q.expect('dbus-signal', signal='Closed', path=path)

def test_terminate_outgoing(jp, q, bus, conn, stream):
    _test_terminate_reason(jp, q, bus, conn, stream, False)

def test_terminate_incoming(jp, q, bus, conn, stream):
    _test_terminate_reason(jp, q, bus, conn, stream, True)

if __name__ == '__main__':
    for f in (test_terminate_incoming, test_terminate_outgoing):
        exec_test(
            lambda q, b, c, s: f(JingleProtocol031(), q, b, c, s))
