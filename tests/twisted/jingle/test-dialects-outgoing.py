"""
Test outgoing call handling, using all three variations of RequestChannel.
"""

import dbus
from twisted.words.xish import xpath

from servicetest import (
    make_channel_proxy, EventPattern, call_async,
    assertEquals, assertContains, assertLength,
    )
import constants as cs
from jingletest2 import JingleTest2, test_all_dialects

# There are various deprecated APIs for requesting calls, documented at
# <http://telepathy.freedesktop.org/wiki/Requesting StreamedMedia channels>.
# These are ordered from most recent to most deprecated.
REQUEST_ANONYMOUS = 1
REQUEST_ANONYMOUS_AND_ADD = 2
REQUEST_NONYMOUS = 3

def request_anonymous(jp, q, bus, conn, stream):
    worker(jp, q, bus, conn, stream, REQUEST_ANONYMOUS)

def request_anonymous_and_add(jp, q, bus, conn, stream):
    worker(jp, q, bus, conn, stream, REQUEST_ANONYMOUS_AND_ADD)

def request_nonymous(jp, q, bus, conn, stream):
    worker(jp, q, bus, conn, stream, REQUEST_NONYMOUS)

def worker(jp, q, bus, conn, stream, variant):
    jt2 = JingleTest2(jp, conn, q, stream, 'test@localhost', 'foo@bar.com/Foo')
    jt2.prepare()

    self_handle = conn.GetSelfHandle()
    remote_handle = conn.RequestHandles(1, ["foo@bar.com/Foo"])[0]

    if variant == REQUEST_NONYMOUS:
        call_async( q, conn, 'RequestChannel', cs.CHANNEL_TYPE_STREAMED_MEDIA,
            cs.HT_CONTACT, remote_handle, True)
    else:
        call_async( q, conn, 'RequestChannel', cs.CHANNEL_TYPE_STREAMED_MEDIA,
            cs.HT_NONE, 0, True)

    ret, old_sig, new_sig = q.expect_many(
        EventPattern('dbus-return', method='RequestChannel'),
        EventPattern('dbus-signal', signal='NewChannel'),
        EventPattern('dbus-signal', signal='NewChannels'),
        )
    path = ret.value[0]

    if variant == REQUEST_NONYMOUS:
        assertEquals( [path, cs.CHANNEL_TYPE_STREAMED_MEDIA, cs.HT_CONTACT,
            remote_handle, True], old_sig.args)
    else:
        assertEquals( [path, cs.CHANNEL_TYPE_STREAMED_MEDIA, cs.HT_NONE, 0,
            True], old_sig.args)

    assertLength(1, new_sig.args)
    assertLength(1, new_sig.args[0])       # one channel
    assertLength(2, new_sig.args[0][0])    # two struct members
    emitted_props = new_sig.args[0][0][1]

    assertEquals(
        cs.CHANNEL_TYPE_STREAMED_MEDIA, emitted_props[cs.CHANNEL_TYPE])

    if variant == REQUEST_NONYMOUS:
        assertEquals(remote_handle, emitted_props[cs.TARGET_HANDLE])
        assertEquals(cs.HT_CONTACT, emitted_props[cs.TARGET_HANDLE_TYPE])
        assertEquals('foo@bar.com', emitted_props[cs.TARGET_ID])
    else:
        assertEquals(0, emitted_props[cs.TARGET_HANDLE])
        assertEquals(cs.HT_NONE, emitted_props[cs.TARGET_HANDLE_TYPE])
        assertEquals('', emitted_props[cs.TARGET_ID])

    assertEquals(True, emitted_props[cs.REQUESTED])
    assertEquals(self_handle, emitted_props[cs.INITIATOR_HANDLE])
    assertEquals('test@localhost', emitted_props[cs.INITIATOR_ID])

    signalling_iface = make_channel_proxy(conn, path, 'Channel.Interface.MediaSignalling')
    media_iface = make_channel_proxy(conn, path, 'Channel.Type.StreamedMedia')
    group_iface = make_channel_proxy(conn, path, 'Channel.Interface.Group')

    # Exercise basic Channel Properties from spec 0.17.7
    channel_props = group_iface.GetAll(
        cs.CHANNEL, dbus_interface=dbus.PROPERTIES_IFACE)

    assertEquals(cs.CHANNEL_TYPE_STREAMED_MEDIA,
        channel_props.get('ChannelType'))

    if variant == REQUEST_NONYMOUS:
        assertEquals(remote_handle, channel_props['TargetHandle'])
        assertEquals(cs.HT_CONTACT, channel_props['TargetHandleType'])
        assertEquals('foo@bar.com', channel_props['TargetID'])
        assertEquals((cs.HT_CONTACT, remote_handle),
            media_iface.GetHandle(dbus_interface=cs.CHANNEL))
    else:
        assertEquals(0, channel_props['TargetHandle'])
        assertEquals(cs.HT_NONE, channel_props['TargetHandleType'])
        assertEquals('', channel_props['TargetID'])
        assertEquals((cs.HT_NONE, 0),
            media_iface.GetHandle(dbus_interface=cs.CHANNEL))

    for interface in [
            cs.CHANNEL_IFACE_GROUP, cs.CHANNEL_IFACE_MEDIA_SIGNALLING,
            cs.TP_AWKWARD_PROPERTIES, cs.CHANNEL_IFACE_HOLD]:
        assertContains(interface, channel_props['Interfaces'])

    assertEquals(True, channel_props['Requested'])
    assertEquals('test@localhost', channel_props['InitiatorID'])
    assertEquals(conn.GetSelfHandle(), channel_props['InitiatorHandle'])

    # Exercise Group Properties from spec 0.17.6 (in a basic way)
    group_props = group_iface.GetAll(
        cs.CHANNEL_IFACE_GROUP, dbus_interface=dbus.PROPERTIES_IFACE)

    assertEquals([self_handle], group_props['Members'])
    assertEquals([], group_props['LocalPendingMembers'])

    if variant == REQUEST_NONYMOUS:
        # In this variant, they're meant to be in RP even though we've sent
        # nothing
        assertEquals([remote_handle], group_props['RemotePendingMembers'])
    else:
        # The channel's anonymous...
        assertEquals([], group_props['RemotePendingMembers'])

        if variant == REQUEST_ANONYMOUS_AND_ADD:
            # but we should be allowed to add the peer.
            group_iface.AddMembers([remote_handle], 'I love backwards compat')

    assertContains('HandleOwners', group_props)
    assertContains('GroupFlags', group_props)

    assertEquals([], media_iface.ListStreams())
    streams = media_iface.RequestStreams(remote_handle,
        [cs.MEDIA_STREAM_TYPE_AUDIO])
    assertEquals(streams, media_iface.ListStreams())
    assertLength(1, streams)

    # streams[0][0] is the stream identifier, which in principle we can't
    # make any assertion about (although in practice it's probably 1)

    assertEquals((
        remote_handle,
        cs.MEDIA_STREAM_TYPE_AUDIO,
        # We haven't connected yet
        cs.MEDIA_STREAM_STATE_DISCONNECTED,
        # In Gabble, requested streams start off bidirectional
        cs.MEDIA_STREAM_DIRECTION_BIDIRECTIONAL,
        0),
        streams[0][1:])

    # S-E gets notified about new session handler, and calls Ready on it
    e = q.expect('dbus-signal', signal='NewSessionHandler')
    assert e.args[1] == 'rtp'

    session_handler = make_channel_proxy(conn, e.args[0], 'Media.SessionHandler')
    session_handler.Ready()

    e = q.expect('dbus-signal', signal='NewStreamHandler')

    stream_handler = make_channel_proxy(conn, e.args[0], 'Media.StreamHandler')

    stream_handler.NewNativeCandidate("fake", jt2.get_remote_transports_dbus())
    stream_handler.Ready(jt2.get_audio_codecs_dbus())
    stream_handler.StreamState(cs.MEDIA_STREAM_STATE_CONNECTED)

    sh_props = stream_handler.GetAll(
        cs.STREAM_HANDLER, dbus_interface=dbus.PROPERTIES_IFACE)
    assertEquals('gtalk-p2p', sh_props['NATTraversal'])
    assertEquals(True, sh_props['CreatedLocally'])

    session_initiate = q.expect('stream-iq', predicate=lambda e:
        jp.match_jingle_action(e.query, 'session-initiate'))
    jt2.set_sid_from_initiate(session_initiate.query)

    stream.send(jp.xml(jp.ResultIq('test@localhost', session_initiate.stanza,
        [])))

    if jp.dialect == 'gtalk-v0.4':
        node = jp.SetIq(jt2.peer, jt2.jid, [
            jp.Jingle(jt2.sid, jt2.peer, 'transport-accept', [
                jp.TransportGoogleP2P() ]) ])
        stream.send(jp.xml(node))

    # FIXME: expect transport-info, then if we're gtalk3, send
    # candidates, and check that gabble resends transport-info as
    # candidates
    jt2.accept()

    q.expect_many(
        EventPattern('stream-iq', iq_type='result'),
        # Call accepted
        EventPattern('dbus-signal', signal='MembersChanged',
            args=['', [remote_handle], [], [], [], remote_handle,
                  cs.GC_REASON_NONE]),
        )

    # Time passes ... afterwards we close the chan

    group_iface.RemoveMembers([self_handle], 'closed')

    # Make sure gabble sends proper terminate action
    if jp.dialect.startswith('gtalk'):
        terminate = EventPattern('stream-iq', predicate=lambda x:
            xpath.queryForNodes("/iq/session[@type='terminate']",
                x.stanza))
    else:
        terminate = EventPattern('stream-iq', predicate=lambda x:
            xpath.queryForNodes("/iq/jingle[@action='session-terminate']",
                x.stanza))

    mc_event, _, _ = q.expect_many(
        EventPattern('dbus-signal', signal='MembersChanged'),
        EventPattern('dbus-signal', signal='Close'),
        terminate,
        )
    # Check that we're the actor
    assertEquals(self_handle, mc_event.args[5])

    conn.Disconnect()
    q.expect('dbus-signal', signal='StatusChanged', args=[2, 1])

if __name__ == '__main__':
    test_all_dialects(request_anonymous)
    test_all_dialects(request_anonymous_and_add)
    test_all_dialects(request_nonymous)
