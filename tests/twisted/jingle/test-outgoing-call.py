
"""
Test outgoing call handling. This tests the happy scenario
when the remote party accepts the call.
"""

from gabbletest import exec_test, sync_stream
from servicetest import (
    assertContains, assertEquals, assertLength, make_channel_proxy,
    call_async, EventPattern)
import jingletest
import gabbletest
import dbus

import constants as cs

def test(q, bus, conn, stream):
    jt = jingletest.JingleTest(stream, 'test@localhost', 'foo@bar.com/Foo')

    # If we need to override remote caps, feats, codecs or caps,
    # this is a good time to do it

    # Connecting
    conn.Connect()

    q.expect('dbus-signal', signal='StatusChanged', args=[1, 1])

    q.expect('stream-authenticated')
    q.expect('dbus-signal', signal='PresenceUpdate',
        args=[{1L: (0L, {u'available': {}})}])
    q.expect('dbus-signal', signal='StatusChanged', args=[0, 1])

    self_handle = conn.GetSelfHandle()

    # We need remote end's presence for capabilities
    jt.send_remote_presence()

    # Gabble doesn't trust it, so makes a disco
    event = q.expect('stream-iq', query_ns='http://jabber.org/protocol/disco#info',
             to='foo@bar.com/Foo')

    jt.send_remote_disco_reply(event.stanza)

    # Force Gabble to process the caps before calling RequestChannel
    sync_stream(q, stream)

    self_handle = conn.GetSelfHandle()
    handle = conn.RequestHandles(cs.HT_CONTACT, [jt.remote_jid])[0]

    call_async(
        q, conn, 'RequestChannel', cs.CHANNEL_TYPE_STREAMED_MEDIA, 0, 0, True)

    ret, old_sig, new_sig = q.expect_many(
        EventPattern('dbus-return', method='RequestChannel'),
        EventPattern('dbus-signal', signal='NewChannel'),
        EventPattern('dbus-signal', signal='NewChannels'),
        )

    path = ret.value[0]
    assertEquals(
        [path, cs.CHANNEL_TYPE_STREAMED_MEDIA, 0, 0, True], old_sig.args)

    assertLength(1, new_sig.args)
    assertLength(1, new_sig.args[0])       # one channel
    assertLength(2, new_sig.args[0][0])    # two struct members
    emitted_props = new_sig.args[0][0][1]

    assertEquals(
        cs.CHANNEL_TYPE_STREAMED_MEDIA, emitted_props[cs.CHANNEL_TYPE])
    assertEquals(0, emitted_props[cs.TARGET_HANDLE_TYPE])
    assertEquals(0, emitted_props[cs.TARGET_HANDLE])
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

    assertEquals(0, channel_props['TargetHandle'])
    assertEquals(0, channel_props['TargetHandleType'])
    assertEquals((0, 0), media_iface.GetHandle(dbus_interface=cs.CHANNEL))
    assertEquals(cs.CHANNEL_TYPE_STREAMED_MEDIA,
        channel_props.get('ChannelType'))

    for interface in [
            cs.CHANNEL_IFACE_GROUP, cs.CHANNEL_IFACE_MEDIA_SIGNALLING,
            cs.TP_AWKWARD_PROPERTIES, cs.CHANNEL_IFACE_HOLD]:
        assertContains(interface, channel_props['Interfaces'])

    assertEquals('', channel_props['TargetID'])
    assertEquals(True, channel_props['Requested'])
    assertEquals('test@localhost', channel_props['InitiatorID'])
    assertEquals(conn.GetSelfHandle(), channel_props['InitiatorHandle'])

    # Exercise Group Properties from spec 0.17.6 (in a basic way)
    group_props = group_iface.GetAll(
        cs.CHANNEL_IFACE_GROUP, dbus_interface=dbus.PROPERTIES_IFACE)

    for name in [
            'HandleOwners', 'Members', 'LocalPendingMembers',
            'RemotePendingMembers', 'GroupFlags']:
        assertContains(name, group_props)

    assertEquals([], media_iface.ListStreams())
    streams = media_iface.RequestStreams(handle, [cs.MEDIA_STREAM_TYPE_AUDIO])
    assertEquals(streams, media_iface.ListStreams())
    assertLength(1, streams)

    # streams[0][0] is the stream identifier, which in principle we can't
    # make any assertion about (although in practice it's probably 1)

    assertEquals((
        handle,
        cs.MEDIA_STREAM_TYPE_AUDIO,
        # We haven't connected yet
        cs.MEDIA_STREAM_STATE_DISCONNECTED,
        # In Gabble, requested streams start off bidirectional
        cs.MEDIA_STREAM_DIRECTION_BIDIRECTIONAL,
        0),
        streams[0][1:])

    # S-E gets notified about new session handler, and calls Ready on it
    e = q.expect('dbus-signal', signal='NewSessionHandler')
    assertEquals('rtp', e.args[1])

    session_handler = make_channel_proxy(conn, e.args[0], 'Media.SessionHandler')
    session_handler.Ready()

    e = q.expect('dbus-signal', signal='NewStreamHandler')

    stream_handler = make_channel_proxy(conn, e.args[0], 'Media.StreamHandler')

    stream_handler.NewNativeCandidate("fake", jt.get_remote_transports_dbus())
    stream_handler.Ready(jt.get_audio_codecs_dbus())
    stream_handler.StreamState(cs.MEDIA_STREAM_STATE_CONNECTED)

    sh_props = stream_handler.GetAll(
        cs.STREAM_HANDLER, dbus_interface=dbus.PROPERTIES_IFACE)
    assertEquals('gtalk-p2p', sh_props['NATTraversal'])
    assertEquals(True, sh_props['CreatedLocally'])

    e = q.expect('stream-iq')
    assertEquals('jingle', e.query.name)
    assertEquals('session-initiate', e.query['action'])
    stream.send(gabbletest.make_result_iq(stream, e.stanza))

    jt.outgoing_call_reply(e.query['sid'], True)

    q.expect('stream-iq', iq_type='result')

    # Call accepted
    q.expect('dbus-signal', signal='MembersChanged',
        args=['', [handle], [], [], [], handle, cs.GC_REASON_NONE])

    # Time passes ... afterwards we close the chan

    group_iface.RemoveMembers([self_handle], 'closed')

    # Check that we're the actor
    e = q.expect('dbus-signal', signal='MembersChanged')
    assertEquals(self_handle, e.args[5])

    # Test completed, close the connection

    e = q.expect('dbus-signal', signal='Close') #XXX - match against the path

    conn.Disconnect()
    q.expect('dbus-signal', signal='StatusChanged', args=[2, 1])

    return True


if __name__ == '__main__':
    exec_test(test)

