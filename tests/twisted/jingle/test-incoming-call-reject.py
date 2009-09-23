"""
Test incoming call handling - reject a call because we're busy, and for no
reason.
"""

from twisted.words.xish import xpath

from servicetest import make_channel_proxy, EventPattern, call_async
from jingletest2 import JingleTest2, test_all_dialects

import constants as cs

def test_busy(jp, q, bus, conn, stream):
    test(jp, q, bus, conn, stream, True)

def test_no_reason(jp, q, bus, conn, stream):
    test(jp, q, bus, conn, stream, False)

def test(jp, q, bus, conn, stream, busy):
    remote_jid = 'foo@bar.com/Foo'
    jt = JingleTest2(jp, conn, q, stream, 'test@localhost', remote_jid)

    jt.prepare()

    self_handle = conn.GetSelfHandle()
    remote_handle = conn.RequestHandles(cs.HT_CONTACT, [remote_jid])[0]

    # Remote end calls us
    jt.incoming_call()

    # FIXME: these signals are not observable by real clients, since they
    #        happen before NewChannels.
    # The caller is in members
    e = q.expect('dbus-signal', signal='MembersChanged',
             args=[u'', [remote_handle], [], [], [], 0, 0])

    # We're pending because of remote_handle
    e = q.expect('dbus-signal', signal='MembersChanged',
             args=[u'', [], [], [self_handle], [], remote_handle,
                   cs.GC_REASON_INVITED])

    # S-E gets notified about new session handler, and calls Ready on it
    e = q.expect('dbus-signal', signal='NewSessionHandler')
    assert e.args[1] == 'rtp'

    session_handler = make_channel_proxy(conn, e.args[0], 'Media.SessionHandler')
    session_handler.Ready()

    media_chan = make_channel_proxy(conn, e.path, 'Channel.Interface.Group')

    # Exercise channel properties
    channel_props = media_chan.GetAll(cs.CHANNEL,
            dbus_interface=cs.PROPERTIES_IFACE)
    assert channel_props['TargetHandle'] == remote_handle
    assert channel_props['TargetHandleType'] == 1
    assert channel_props['TargetID'] == 'foo@bar.com'
    assert channel_props['Requested'] == False
    assert channel_props['InitiatorID'] == 'foo@bar.com'
    assert channel_props['InitiatorHandle'] == remote_handle

    if busy:
        # First, try using a reason that doesn't make any sense
        call_async(q, media_chan, 'RemoveMembersWithReason',
            [self_handle], "what kind of a reason is Separated?!",
            cs.GC_REASON_SEPARATED)
        e = q.expect('dbus-error', method='RemoveMembersWithReason')
        assert e.error.get_dbus_name() == cs.INVALID_ARGUMENT

        # Now try a more sensible reason.
        media_chan.RemoveMembersWithReason([self_handle],
            "which part of 'Do Not Disturb' don't you understand?",
            cs.GC_REASON_BUSY)
    else:
        media_chan.RemoveMembers([self_handle], 'rejected')

    iq, mc, _ = q.expect_many(
        EventPattern('stream-iq',
            predicate=jp.action_predicate('session-terminate')),
        EventPattern('dbus-signal', signal='MembersChanged'),
        EventPattern('dbus-signal', signal='Closed'),
        )

    _, added, removed, lp, rp, actor, reason = mc.args
    assert added == [], added
    assert set(removed) == set([self_handle, remote_handle]), \
        (removed, self_handle, remote_handle)
    assert lp == [], lp
    assert rp == [], rp
    assert actor == self_handle, (actor, self_handle)
    if busy:
        assert reason == cs.GC_REASON_BUSY, reason
    else:
        assert reason == cs.GC_REASON_NONE, reason

    if jp.is_modern_jingle():
        jingle = iq.query
        if busy:
            r = "/jingle/reason/busy"
        else:
            r = "/jingle/reason/cancel"
        assert xpath.queryForNodes(r, jingle) is not None, (jingle.toXml(), r)

if __name__ == '__main__':
    test_all_dialects(test_busy)
    test_all_dialects(test_no_reason)

