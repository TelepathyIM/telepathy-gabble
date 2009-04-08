
"""
Test making outgoing call using CreateChannel. This tests the happy scenario
when the remote party accepts the call.
"""

import dbus

from gabbletest import exec_test, sync_stream
from servicetest import make_channel_proxy, call_async, EventPattern
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

    handle = conn.RequestHandles(1, [jt.remote_jid])[0]

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

    assert sig_path == path, (sig_path, path)
    assert sig_ct == cs.CHANNEL_TYPE_STREAMED_MEDIA, sig_ct
    assert sig_ht == 1, sig_ht      # HandleType = Contact
    assert sig_h == handle, sig_h
    assert sig_sh == True           # suppress handler

    assert len(new_sig.args) == 1
    assert len(new_sig.args[0]) == 1        # one channel
    assert len(new_sig.args[0][0]) == 2     # two struct members
    assert new_sig.args[0][0][0] == path
    emitted_props = new_sig.args[0][0][1]

    assert emitted_props[cs.CHANNEL_TYPE] == cs.CHANNEL_TYPE_STREAMED_MEDIA
    assert emitted_props[cs.TARGET_HANDLE_TYPE] == cs.HT_CONTACT
    assert emitted_props[cs.TARGET_HANDLE] == handle
    assert emitted_props[cs.TARGET_ID] == 'foo@bar.com', emitted_props
    assert emitted_props[cs.REQUESTED] == True
    assert emitted_props[cs.INITIATOR_HANDLE] == self_handle
    assert emitted_props[cs.INITIATOR_ID] == 'test@localhost'

    signalling_iface = make_channel_proxy(conn, path, 'Channel.Interface.MediaSignalling')
    media_iface = make_channel_proxy(conn, path, 'Channel.Type.StreamedMedia')
    group_iface = make_channel_proxy(conn, path, 'Channel.Interface.Group')

    # Exercise basic Channel Properties from spec 0.17.7
    channel_props = group_iface.GetAll(
        cs.CHANNEL, dbus_interface=dbus.PROPERTIES_IFACE)
    assert channel_props.get('TargetHandle') == handle, \
            channel_props.get('TargetHandle')
    assert channel_props.get('TargetHandleType') == 1,\
            channel_props.get('TargetHandleType')
    assert media_iface.GetHandle(dbus_interface=cs.CHANNEL) == (cs.HT_CONTACT,
            handle)
    assert channel_props.get('ChannelType') == \
            cs.CHANNEL_TYPE_STREAMED_MEDIA,\
            channel_props.get('ChannelType')
    assert cs.CHANNEL_IFACE_GROUP in \
            channel_props.get('Interfaces', ()), \
            channel_props.get('Interfaces')
    assert cs.CHANNEL_IFACE_MEDIA_SIGNALLING in \
            channel_props.get('Interfaces', ()), \
            channel_props.get('Interfaces')
    assert cs.TP_AWKWARD_PROPERTIES in \
            channel_props.get('Interfaces', ()), \
            channel_props.get('Interfaces')
    assert cs.CHANNEL_IFACE_HOLD in \
            channel_props.get('Interfaces', ()), \
            channel_props.get('Interfaces')
    assert channel_props['TargetID'] == 'foo@bar.com', channel_props
    assert channel_props['Requested'] == True
    assert channel_props['InitiatorID'] == 'test@localhost'
    assert channel_props['InitiatorHandle'] == conn.GetSelfHandle()

    # Exercise Group Properties from spec 0.17.6 (in a basic way)
    group_props = group_iface.GetAll(
        cs.CHANNEL_IFACE_GROUP, dbus_interface=dbus.PROPERTIES_IFACE)
    assert 'HandleOwners' in group_props, group_props
    assert 'Members' in group_props, group_props
    assert 'LocalPendingMembers' in group_props, group_props
    assert 'RemotePendingMembers' in group_props, group_props
    assert 'GroupFlags' in group_props, group_props

    # The remote contact shouldn't be in remote pending yet (nor should it be
    # in members!)
    assert handle not in group_props['RemotePendingMembers'], group_props
    assert handle not in group_props['Members'], group_props

    list_streams_result = media_iface.ListStreams()
    assert len(list_streams_result) == 0, list_streams_result

    streams = media_iface.RequestStreams(handle, [cs.MEDIA_STREAM_TYPE_AUDIO])

    list_streams_result = media_iface.ListStreams()
    assert streams == list_streams_result, (streams, list_streams_result)

    assert len(streams) == 1, streams
    assert len(streams[0]) == 6, streams[0]
    # streams[0][0] is the stream identifier, which in principle we can't
    # make any assertion about (although in practice it's probably 1)
    assert streams[0][1] == handle, (streams[0], handle)
    assert streams[0][2] == cs.MEDIA_STREAM_TYPE_AUDIO, streams[0]
    # We haven't connected yet
    assert streams[0][3] == cs.MEDIA_STREAM_STATE_DISCONNECTED, streams[0]
    # In Gabble, requested streams start off bidirectional
    assert streams[0][4] == cs.MEDIA_STREAM_DIRECTION_BIDIRECTIONAL, streams[0]
    assert streams[0][5] == 0, streams[0]

    # S-E gets notified about new session handler, and calls Ready on it
    e = q.expect('dbus-signal', signal='NewSessionHandler')
    assert e.args[1] == 'rtp'

    session_handler = make_channel_proxy(conn, e.args[0], 'Media.SessionHandler')
    session_handler.Ready()

    e = q.expect('dbus-signal', signal='NewStreamHandler')

    stream_handler = make_channel_proxy(conn, e.args[0], 'Media.StreamHandler')

    stream_handler.NewNativeCandidate("fake", jt.get_remote_transports_dbus())
    stream_handler.Ready(jt.get_audio_codecs_dbus())
    stream_handler.StreamState(cs.MEDIA_STREAM_STATE_CONNECTED)

    e = q.expect('stream-iq')
    assert e.query.name == 'jingle'
    assert e.query['action'] == 'session-initiate'
    stream.send(gabbletest.make_result_iq(stream, e.stanza))

    jt.outgoing_call_reply(e.query['sid'], True)

    q.expect('stream-iq', iq_type='result')

    # Time passes ... afterwards we close the chan

    group_iface.RemoveMembers([dbus.UInt32(1)], 'closed')

    # Test completed, close the connection

    e = q.expect('dbus-signal', signal='Close') #XXX - match against the path

    conn.Disconnect()
    q.expect('dbus-signal', signal='StatusChanged', args=[2, 1])

    return True


if __name__ == '__main__':
    exec_test(test)

