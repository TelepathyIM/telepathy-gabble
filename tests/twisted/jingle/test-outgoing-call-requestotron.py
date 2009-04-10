
"""
Test making outgoing call using CreateChannel. This tests the happy scenario
when the remote party accepts the call.
"""

import dbus

from gabbletest import exec_test, sync_stream
from servicetest import (
    make_channel_proxy, call_async, EventPattern,
    assertEquals, assertContains, assertDoesNotContain, assertLength,
    )
import jingletest
import gabbletest

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

    handle = conn.RequestHandles(cs.HT_CONTACT, [jt.remote_jid])[0]

    call_async(q, conn.Requests, 'CreateChannel',
            { cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_STREAMED_MEDIA,
              cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
              cs.TARGET_HANDLE: handle,
              })

    ret, old_sig, new_sig = q.expect_many(
        EventPattern('dbus-return', method='CreateChannel'),
        EventPattern('dbus-signal', signal='NewChannel'),
        EventPattern('dbus-signal', signal='NewChannels'),
        )

    path = ret.value[0]

    sig_path, sig_ct, sig_ht, sig_h, sig_sh = old_sig.args

    assertEquals(path, sig_path)
    assertEquals(cs.CHANNEL_TYPE_STREAMED_MEDIA, sig_ct)
    assertEquals(cs.HT_CONTACT, sig_ht)
    assertEquals(handle, sig_h)
    assertEquals(True, sig_sh) # suppress handler

    assertLength(1, new_sig.args)
    assertLength(1, new_sig.args[0])        # one channel
    assertLength(2, new_sig.args[0][0])     # two struct members
    assertEquals(path, new_sig.args[0][0][0])

    emitted_props = new_sig.args[0][0][1]

    assertEquals(cs.CHANNEL_TYPE_STREAMED_MEDIA, emitted_props[cs.CHANNEL_TYPE])
    assertEquals(cs.HT_CONTACT, emitted_props[cs.TARGET_HANDLE_TYPE])
    assertEquals(handle, emitted_props[cs.TARGET_HANDLE])
    assertEquals('foo@bar.com', emitted_props[cs.TARGET_ID])
    assertEquals(True, emitted_props[cs.REQUESTED])
    assertEquals(self_handle, emitted_props[cs.INITIATOR_HANDLE])
    assertEquals('test@localhost', emitted_props[cs.INITIATOR_ID])

    signalling_iface = make_channel_proxy(conn, path, 'Channel.Interface.MediaSignalling')
    media_iface = make_channel_proxy(conn, path, 'Channel.Type.StreamedMedia')
    group_iface = make_channel_proxy(conn, path, 'Channel.Interface.Group')

    # Exercise basic Channel Properties from spec 0.17.7
    channel_props = group_iface.GetAll(
        cs.CHANNEL, dbus_interface=dbus.PROPERTIES_IFACE)
    assertEquals(handle, channel_props.get('TargetHandle'))
    assertEquals(cs.HT_CONTACT, channel_props.get('TargetHandleType'))
    assertEquals((cs.HT_CONTACT, handle),
        media_iface.GetHandle(dbus_interface=cs.CHANNEL))
    assertEquals(cs.CHANNEL_TYPE_STREAMED_MEDIA,
        channel_props.get('ChannelType'))
    assertContains(cs.CHANNEL_IFACE_GROUP, channel_props.get('Interfaces'))
    assertContains(cs.CHANNEL_IFACE_MEDIA_SIGNALLING,
        channel_props.get('Interfaces'))
    assertContains(cs.TP_AWKWARD_PROPERTIES, channel_props.get('Interfaces'))
    assertContains(cs.CHANNEL_IFACE_HOLD, channel_props.get('Interfaces'))
    assertEquals('foo@bar.com', channel_props['TargetID'])
    assertEquals(True, channel_props['Requested'])
    assertEquals('test@localhost', channel_props['InitiatorID'])
    assertEquals(conn.GetSelfHandle(), channel_props['InitiatorHandle'])

    # Exercise Group Properties
    group_props = group_iface.GetAll(
        cs.CHANNEL_IFACE_GROUP, dbus_interface=dbus.PROPERTIES_IFACE)
    assertContains('HandleOwners', group_props)
    assertEquals([self_handle], group_props['Members'])
    assertEquals([], group_props['LocalPendingMembers'])
    assertEquals([], group_props['RemotePendingMembers'])

    expected_flags = cs.GF_PROPERTIES
    assertEquals(expected_flags, group_props['GroupFlags'])

    # The remote contact shouldn't be in remote pending yet (nor should it be
    # in members!)
    assertDoesNotContain(handle, group_props['RemotePendingMembers'])
    assertDoesNotContain(handle, group_props['Members'])

    list_streams_result = media_iface.ListStreams()
    assertLength(0, list_streams_result)

    streams = media_iface.RequestStreams(handle, [cs.MEDIA_STREAM_TYPE_AUDIO])

    list_streams_result = media_iface.ListStreams()
    assertEquals(list_streams_result, streams)

    assertLength(1, streams)
    assertLength(6, streams[0])
    # streams[0][0] is the stream identifier, which in principle we can't
    # make any assertion about (although in practice it's probably 1)
    assertEquals(handle, streams[0][1])
    assertEquals(cs.MEDIA_STREAM_TYPE_AUDIO, streams[0][2])
    # We haven't connected yet
    assertEquals(cs.MEDIA_STREAM_STATE_DISCONNECTED, streams[0][3])
    # In Gabble, requested streams start off bidirectional
    assertEquals(cs.MEDIA_STREAM_DIRECTION_BIDIRECTIONAL, streams[0][4])
    assertEquals(0, streams[0][5])

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

    # When we actually send XML to the peer, they should pop up in remote
    # pending.
    e, _ = q.expect_many(
        EventPattern('stream-iq', predicate=lambda e:
            e.query is not None and e.query.name == 'jingle' and \
            e.query['action'] == 'session-initiate'
        ),
        EventPattern('dbus-signal', signal='MembersChanged',
            args=["", [], [], [], [handle], self_handle, cs.GC_REASON_INVITED]),
        )
    stream.send(gabbletest.make_result_iq(stream, e.stanza))

    # Check the Group interface's properties again!
    group_props = group_iface.GetAll(
        cs.CHANNEL_IFACE_GROUP, dbus_interface=dbus.PROPERTIES_IFACE)
    assertContains('HandleOwners', group_props)
    assertEquals([self_handle], group_props['Members'])
    assertEquals([], group_props['LocalPendingMembers'])
    assertEquals([handle], group_props['RemotePendingMembers'])

    expected_flags = cs.GF_PROPERTIES
    assertEquals(expected_flags, group_props['GroupFlags'])

    jt.outgoing_call_reply(e.query['sid'], True)

    q.expect('stream-iq', iq_type='result')

    # Time passes ... afterwards we close the chan

    group_iface.RemoveMembers([self_handle], 'closed')

    # Test completed, close the connection

    e = q.expect('dbus-signal', signal='Close') #XXX - match against the path

    conn.Disconnect()
    q.expect('dbus-signal', signal='StatusChanged', args=[2, 1])

    return True


if __name__ == '__main__':
    exec_test(test)

