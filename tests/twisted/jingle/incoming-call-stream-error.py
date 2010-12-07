
"""
Test handling of Error() call on stream handler.

This tests a regression in which MembersChanged was emitted with reason other
than GC_REASON_ERROR.
"""

from servicetest import EventPattern, assertEquals, make_channel_proxy
from jingletest2 import JingleTest2, test_all_dialects
import constants as cs

from twisted.words.xish import xpath

def _session_terminate_predicate(event, reason, msg, jp):
    matches = jp.match_jingle_action(event.query, 'session-terminate')

    if matches and jp.is_modern_jingle():
        reason = xpath.queryForNodes("/iq"
                                     "/jingle[@action='session-terminate']"
                                     "/reason/%s" % reason,
                                     event.stanza)
        reason_text = xpath.queryForString("/iq/jingle/reason/text",
                                           event.stanza)

        return bool(reason) and reason_text == msg

    return matches

def _test(jp, q, bus, conn, stream,
          jingle_reason, group_change_reason, stream_error):
    jt = JingleTest2(jp, conn, q, stream, 'test@localhost', 'foo@bar.com/Foo')
    jt.prepare()
    self_handle = conn.GetSelfHandle()
    remote_handle = conn.RequestHandles(cs.HT_CONTACT, ["foo@bar.com/Foo"])[0]

    # Ring ring!
    jt.incoming_call()
    new_channel, new_session_handler = q.expect_many(
        EventPattern('dbus-signal', signal='NewChannel',
            predicate=lambda e: cs.CHANNEL_TYPE_CONTACT_LIST not in e.args),
        EventPattern('dbus-signal', signal='NewSessionHandler'))
    assertEquals(cs.CHANNEL_TYPE_STREAMED_MEDIA, new_channel.args[1])
    assertEquals(cs.HT_CONTACT, new_channel.args[2])
    assertEquals(remote_handle, new_channel.args[3])
    assertEquals('rtp', new_session_handler.args[1])

    channel_path = new_channel.args[0]

    # Client calls Ready on new session handler.
    session_handler = make_channel_proxy(
        conn, new_session_handler.args[0], 'Media.SessionHandler')
    session_handler.Ready()

    # Client gets notified about a newly created stream...
    new_stream_handler = q.expect('dbus-signal', signal='NewStreamHandler')
    stream_id = new_stream_handler.args[1]
    stream_handler = make_channel_proxy(
        conn, new_stream_handler.args[0], 'Media.StreamHandler')
    stream_handler.NewNativeCandidate("fake", jt.get_remote_transports_dbus())
    stream_handler.Ready(jt.dbusify_codecs([("FOO", 5, 8000, {})]))

    q.expect('dbus-signal', signal='SetRemoteCodecs')

    msg = u"o noes"

    # ...but something goes wrong.
    stream_handler.Error(stream_error, msg)

    q.expect("stream-iq", iq_type="set",
             predicate=lambda x: _session_terminate_predicate(x, jingle_reason,
                                                              msg, jp))
    # Bye bye members.
    mc = q.expect('dbus-signal', signal='MembersChanged',
                  interface=cs.CHANNEL_IFACE_GROUP, path=channel_path,
                  args=[msg, [], [self_handle, remote_handle], [],
                        [], self_handle, group_change_reason])

    q.expect('dbus-signal', signal='StreamError',
             interface=cs.CHANNEL_TYPE_STREAMED_MEDIA,
             args=[stream_id, stream_error, msg])

    # Bye bye stream
    q.expect('dbus-signal', signal='Close')
    q.expect('dbus-signal', signal='StreamRemoved')

    # Bye bye channel.
    q.expect('dbus-signal', signal='Closed')
    q.expect('dbus-signal', signal='ChannelClosed')

def test_connection_error(jp, q, bus, conn, stream):
    _test(jp, q, bus, conn, stream, "connectivity-error", cs.GC_REASON_ERROR,
          cs.MEDIA_STREAM_ERROR_NETWORK_ERROR)

def test_codec_negotiation_fail(jp, q, bus, conn, stream):
    _test(jp, q, bus, conn, stream, "failed-application", cs.GC_REASON_ERROR,
          cs.MEDIA_STREAM_ERROR_CODEC_NEGOTIATION_FAILED)

if __name__ == '__main__':
    test_all_dialects(test_connection_error)
    test_all_dialects(test_codec_negotiation_fail)
