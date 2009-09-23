
"""
Test handling of Error() call on stream handler.

This tests a regression in which MembersChanged was emitted with reason other
than GC_REASON_ERROR.
"""

from servicetest import EventPattern, assertEquals, make_channel_proxy
from jingletest2 import JingleTest2, test_all_dialects
import constants as cs

MEDIA_STREAM_ERROR_CONNECTION_FAILED = 3

def test(jp, q, bus, conn, stream):
    jt = JingleTest2(jp, conn, q, stream, 'test@localhost', 'foo@bar.com/Foo')
    jt.prepare()
    self_handle = conn.GetSelfHandle()
    remote_handle = conn.RequestHandles(cs.HT_CONTACT, ["foo@bar.com/Foo"])[0]

    # Ring ring!
    jt.incoming_call()
    new_channel, new_session_handler = q.expect_many(
        EventPattern('dbus-signal', signal='NewChannel'),
        EventPattern('dbus-signal', signal='NewSessionHandler'))
    assertEquals(cs.CHANNEL_TYPE_STREAMED_MEDIA, new_channel.args[1])
    assertEquals(cs.HT_CONTACT, new_channel.args[2])
    assertEquals(remote_handle, new_channel.args[3])
    assertEquals('rtp', new_session_handler.args[1])

    # Client calls Ready on new session handler.
    session_handler = make_channel_proxy(
        conn, new_session_handler.args[0], 'Media.SessionHandler')
    session_handler.Ready()

    # Client gets notified about a newly created stream...
    new_stream_handler = q.expect('dbus-signal', signal='NewStreamHandler')
    stream_handler = make_channel_proxy(
        conn, new_stream_handler.args[0], 'Media.StreamHandler')
    # ...but something goes wrong.
    stream_handler.Error(MEDIA_STREAM_ERROR_CONNECTION_FAILED, "o noes")

    # Bye bye members.
    q.expect('dbus-signal', signal='MembersChanged',
        args=[u'', [], [self_handle, remote_handle], [], [], self_handle,
            cs.GC_REASON_ERROR])
    # Bye bye stream.
    q.expect('dbus-signal', signal='Close')
    q.expect('dbus-signal', signal='StreamRemoved')
    # Bye bye channel.
    q.expect('dbus-signal', signal='Closed')
    q.expect('dbus-signal', signal='ChannelClosed')

if __name__ == '__main__':
    test_all_dialects(test)
