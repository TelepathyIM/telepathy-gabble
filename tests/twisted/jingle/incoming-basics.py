"""
Test incoming call handling.
"""

import dbus

from gabbletest import make_result_iq
from servicetest import (
    make_channel_proxy, unwrap, EventPattern, assertEquals, assertLength)
from jingletest2 import JingleTest2, test_all_dialects
import constants as cs

from twisted.words.xish import xpath

def test(jp, q, bus, conn, stream):
    jt = JingleTest2(jp, conn, q, stream, 'test@localhost', 'foo@bar.com/Foo')
    jt.prepare()

    self_handle = conn.GetSelfHandle()
    remote_handle = conn.RequestHandles(cs.HT_CONTACT, ["foo@bar.com/Foo"])[0]

    # Remote end calls us
    jt.incoming_call()

    # If this is a Jingle dialect that supports it, Gabble should send a
    # <ringing/> notification when it gets the session-initiate until Telepathy
    # has a way for the UI to do this.
    # https://bugs.freedesktop.org/show_bug.cgi?id=21964
    ringing_event = jp.rtp_info_event_list("ringing")

    if jp.dialect == 'gtalk-v0.4':
        # With gtalk4, apparently we have to send transport-accept immediately,
        # not even just before we send our transport-info. wjt tested this, and
        # indeed if we don't send this for incoming calls, the call never
        # connects.
        ta_event = [
            EventPattern('stream-iq', predicate=lambda x:
                xpath.queryForNodes("/iq/session[@type='transport-accept']",
                    x.stanza)),
            ]
    else:
        ta_event = []

    nc, e = q.expect_many(
        EventPattern('dbus-signal', signal='NewChannel'),
        EventPattern('dbus-signal', signal='NewSessionHandler'),
        *(ringing_event + ta_event)
        )[0:2]
    path, ct, ht, h, _ = nc.args

    assert ct == cs.CHANNEL_TYPE_STREAMED_MEDIA, ct
    assert ht == cs.HT_CONTACT, ht
    assert h == remote_handle, h

    media_chan = make_channel_proxy(conn, path, 'Channel.Interface.Group')
    media_iface = make_channel_proxy(conn, path, 'Channel.Type.StreamedMedia')

    # S-E was notified about new session handler, and calls Ready on it
    assert e.args[1] == 'rtp'
    session_handler = make_channel_proxy(conn, e.args[0], 'Media.SessionHandler')
    session_handler.Ready()

    nsh_event = q.expect('dbus-signal', signal='NewStreamHandler')

    # S-E gets notified about a newly-created stream
    stream_handler = make_channel_proxy(conn, nsh_event.args[0],
        'Media.StreamHandler')

    streams = media_iface.ListStreams()
    assertLength(1, streams)

    stream_id, stream_handle, stream_type, _, stream_direction, pending_flags =\
        streams[0]
    assertEquals(remote_handle, stream_handle)
    assertEquals(cs.MEDIA_STREAM_TYPE_AUDIO, stream_type)
    assertEquals(cs.MEDIA_STREAM_DIRECTION_RECEIVE, stream_direction)
    assertEquals(cs.MEDIA_STREAM_PENDING_LOCAL_SEND, pending_flags)

    # Exercise channel properties
    channel_props = media_chan.GetAll(
        cs.CHANNEL, dbus_interface=dbus.PROPERTIES_IFACE)
    assert channel_props['TargetHandle'] == remote_handle
    assert channel_props['TargetHandleType'] == cs.HT_CONTACT
    assert media_chan.GetHandle(dbus_interface=cs.CHANNEL) == (cs.HT_CONTACT,
            remote_handle)
    assert channel_props['TargetID'] == 'foo@bar.com'
    assert channel_props['InitiatorID'] == 'foo@bar.com'
    assert channel_props['InitiatorHandle'] == remote_handle
    assert channel_props['Requested'] == False

    group_props = media_chan.GetAll(
        cs.CHANNEL_IFACE_GROUP, dbus_interface=dbus.PROPERTIES_IFACE)

    assert group_props['SelfHandle'] == self_handle, \
        (group_props['SelfHandle'], self_handle)

    flags = group_props['GroupFlags']
    assert flags & cs.GF_PROPERTIES, flags
    # Changing members in any way other than adding or removing yourself is
    # meaningless for incoming calls, and the flags need not be sent to change
    # your own membership.
    assert not flags & cs.GF_CAN_ADD, flags
    assert not flags & cs.GF_CAN_REMOVE, flags
    assert not flags & cs.GF_CAN_RESCIND, flags

    assert group_props['Members'] == [remote_handle], group_props['Members']
    assert group_props['RemotePendingMembers'] == [], \
        group_props['RemotePendingMembers']
    # We're local pending because remote_handle invited us.
    assert group_props['LocalPendingMembers'] == \
        [(self_handle, remote_handle, cs.GC_REASON_INVITED, '')], \
        unwrap(group_props['LocalPendingMembers'])

    streams = media_chan.ListStreams(
            dbus_interface=cs.CHANNEL_TYPE_STREAMED_MEDIA)
    assert len(streams) == 1, streams
    assert len(streams[0]) == 6, streams[0]
    # streams[0][0] is the stream identifier, which in principle we can't
    # make any assertion about (although in practice it's probably 1)
    assert streams[0][1] == remote_handle, (streams[0], remote_handle)
    assert streams[0][2] == cs.MEDIA_STREAM_TYPE_AUDIO, streams[0]
    # We haven't connected yet
    assert streams[0][3] == cs.MEDIA_STREAM_STATE_DISCONNECTED, streams[0]
    # In Gabble, incoming streams start off with remote send enabled, and
    # local send requested
    assert streams[0][4] == cs.MEDIA_STREAM_DIRECTION_RECEIVE, streams[0]
    assert streams[0][5] == cs.MEDIA_STREAM_PENDING_LOCAL_SEND, streams[0]

    # Connectivity checks happen before we have accepted the call
    stream_handler.NewNativeCandidate("fake", jt.get_remote_transports_dbus())
    stream_handler.Ready(jt.get_audio_codecs_dbus())
    stream_handler.StreamState(cs.MEDIA_STREAM_STATE_CONNECTED)
    stream_handler.SupportedCodecs(jt.get_audio_codecs_dbus())

    # peer gets the transport
    e = q.expect('stream-iq', predicate=jp.action_predicate('transport-info'))
    assertEquals('foo@bar.com/Foo', e.query['initiator'])

    if jp.dialect in ['jingle-v0.15', 'jingle-v0.31']:
        content = xpath.queryForNodes('/iq/jingle/content', e.stanza)[0]
        assertEquals('initiator', content['creator'])

    stream.send(make_result_iq(stream, e.stanza))

    # At last, accept the call
    media_chan.AddMembers([self_handle], 'accepted')

    # Call is accepted, we become a member, and the stream that was pending
    # local send is now sending.
    memb, acc, _, _, _ = q.expect_many(
        EventPattern('dbus-signal', signal='MembersChanged',
            args=[u'', [self_handle], [], [], [], self_handle,
                  cs.GC_REASON_NONE]),
        EventPattern('stream-iq',
            predicate=jp.action_predicate('session-accept')),
        EventPattern('dbus-signal', signal='SetStreamSending', args=[True]),
        EventPattern('dbus-signal', signal='SetStreamPlaying', args=[True]),
        EventPattern('dbus-signal', signal='StreamDirectionChanged',
            args=[stream_id, cs.MEDIA_STREAM_DIRECTION_BIDIRECTIONAL, 0]),
        )

    stream.send(make_result_iq(stream, acc.stanza))

    # Also, if this is a Jingle dialect that supports it, Gabble should send an
    # <active/> notification when the session-accept is acked (until the
    # Telepathy spec lets the UI say it's not ringing any more).
    active_event = jp.rtp_info_event("active")
    if active_event is not None:
        q.expect_many(active_event)

    # we are now both in members
    members = media_chan.GetMembers()
    assert set(members) == set([self_handle, remote_handle]), members

    # Connected! Blah, blah, ...

    # 'Nuff said
    jt.terminate()
    q.expect('dbus-signal', signal='Closed', path=path)

if __name__ == '__main__':
    test_all_dialects(test)
