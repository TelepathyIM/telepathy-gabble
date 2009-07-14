"""
Tests outgoing calls timing out if they're not answered.
"""

from twisted.words.xish import xpath

from servicetest import make_channel_proxy, EventPattern, sync_dbus
from jingletest2 import JingleTest2, test_all_dialects

import constants as cs

def test(jp, q, bus, conn, stream):
    remote_jid = 'daf@example.com/misc'
    jt = JingleTest2(jp, conn, q, stream, 'test@localhost', remote_jid)

    jt.prepare()

    self_handle = conn.GetSelfHandle()
    remote_handle = conn.RequestHandles(cs.HT_CONTACT, [remote_jid])[0]
    path, _ = conn.Requests.CreateChannel({
        cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_STREAMED_MEDIA,
        cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
        cs.TARGET_HANDLE: remote_handle})
    media_iface = make_channel_proxy(conn, path, 'Channel.Type.StreamedMedia')
    media_iface.RequestStreams(remote_handle, [cs.MEDIA_STREAM_TYPE_AUDIO])

    e = q.expect('dbus-signal', signal='NewSessionHandler')
    assert e.args[1] == 'rtp'
    session_handler = make_channel_proxy(conn, e.args[0], 'Media.SessionHandler')
    session_handler.Ready()

    e = q.expect('dbus-signal', signal='NewStreamHandler')
    stream_handler = make_channel_proxy(conn, e.args[0], 'Media.StreamHandler')
    stream_handler.NewNativeCandidate("fake", jt.get_remote_transports_dbus())
    stream_handler.Ready(jt.get_audio_codecs_dbus())
    stream_handler.StreamState(cs.MEDIA_STREAM_STATE_CONNECTED)

    q.expect('stream-iq', predicate=jp.action_predicate('session-initiate'))

    # Ensure we've got all the MembersChanged signals at the start of the call
    # out of the way.
    sync_dbus(bus, q, conn)

    # daf doesn't answer; we expect the call to end.
    mc, t = q.expect_many(
        EventPattern('dbus-signal', signal='MembersChanged'),
        EventPattern('stream-iq',
            predicate=jp.action_predicate('session-terminate')),
        )

    _, added, removed, lp, rp, actor, reason = mc.args
    assert added == [], added
    assert set(removed) == set([self_handle, remote_handle]), \
        (removed, self_handle, remote_handle)
    assert lp == [], lp
    assert rp == [], rp
    # It's not clear whether the actor should be self_handle (we gave up),
    # remote_handle (the other guy didn't pick up), or neither. So I'll make no
    # assertions.
    assert reason == cs.GC_REASON_NO_ANSWER, reason

    if jp.dialect == 'jingle-v0.31':
        # Check we sent <reason><timeout/></reason> to daf
        jingle = t.query
        assert xpath.queryForNodes("/jingle/reason/timeout", jingle) is not None, \
            jingle.toXml()

if __name__ == '__main__':
    test_all_dialects(test)
