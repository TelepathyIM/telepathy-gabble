
"""
Test outgoing call handling. This tests the happy scenario
when the remote party accepts the call.
"""

from gabbletest import exec_test, sync_stream
from servicetest import make_channel_proxy, call_async, EventPattern
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

    call_async(q, conn, 'RequestChannel',
        'org.freedesktop.Telepathy.Channel.Type.StreamedMedia', 0, 0, True)

    ret, old_sig, new_sig = q.expect_many(
        EventPattern('dbus-return', method='RequestChannel'),
        EventPattern('dbus-signal', signal='NewChannel'),
        EventPattern('dbus-signal', signal='NewChannels'),
        )

    path = ret.value[0]
    assert old_sig.args[0] == path, (old_sig.args[0], path)
    assert old_sig.args[1] == u'org.freedesktop.Telepathy.Channel.Type.StreamedMedia',\
            old_sig.args[1]
    assert old_sig.args[2] == 0, old_sig.args[2]
    assert old_sig.args[3] == 0, old_sig.args[3]
    assert old_sig.args[4] == True      # suppress handler

    assert len(new_sig.args) == 1
    assert len(new_sig.args[0]) == 1        # one channel
    assert len(new_sig.args[0][0]) == 2     # two struct members
    assert new_sig.args[0][0][0] == path
    emitted_props = new_sig.args[0][0][1]

    assert emitted_props['org.freedesktop.Telepathy.Channel.ChannelType'] ==\
            'org.freedesktop.Telepathy.Channel.Type.StreamedMedia'
    assert emitted_props['org.freedesktop.Telepathy.Channel.'
            'TargetHandleType'] == 0
    assert emitted_props['org.freedesktop.Telepathy.Channel.TargetHandle'] ==\
            0
    assert emitted_props['org.freedesktop.Telepathy.Channel.TargetID'] == ''
    assert emitted_props['org.freedesktop.Telepathy.Channel.Requested'] \
            == True
    assert emitted_props['org.freedesktop.Telepathy.Channel.InitiatorHandle'] \
            == self_handle
    assert emitted_props['org.freedesktop.Telepathy.Channel.InitiatorID'] \
            == 'test@localhost'

    signalling_iface = make_channel_proxy(conn, path, 'Channel.Interface.MediaSignalling')
    media_iface = make_channel_proxy(conn, path, 'Channel.Type.StreamedMedia')
    group_iface = make_channel_proxy(conn, path, 'Channel.Interface.Group')

    # Exercise basic Channel Properties from spec 0.17.7
    channel_props = group_iface.GetAll(
            'org.freedesktop.Telepathy.Channel',
            dbus_interface=dbus.PROPERTIES_IFACE)
    assert channel_props.get('TargetHandle') == 0, \
            channel_props.get('TargetHandle')
    assert channel_props.get('TargetHandleType') == 0,\
            channel_props.get('TargetHandleType')
    assert media_iface.GetHandle(dbus_interface=cs.CHANNEL) == (0, 0)
    assert channel_props.get('ChannelType') == \
            'org.freedesktop.Telepathy.Channel.Type.StreamedMedia',\
            channel_props.get('ChannelType')
    assert 'org.freedesktop.Telepathy.Channel.Interface.Group' in \
            channel_props.get('Interfaces', ()), \
            channel_props.get('Interfaces')
    assert 'org.freedesktop.Telepathy.Channel.Interface.MediaSignalling' in \
            channel_props.get('Interfaces', ()), \
            channel_props.get('Interfaces')
    assert 'org.freedesktop.Telepathy.Properties' in \
            channel_props.get('Interfaces', ()), \
            channel_props.get('Interfaces')
    assert 'org.freedesktop.Telepathy.Channel.Interface.Hold' in \
            channel_props.get('Interfaces', ()), \
            channel_props.get('Interfaces')
    assert channel_props['TargetID'] == '', channel_props
    assert channel_props['Requested'] == True
    assert channel_props['InitiatorID'] == 'test@localhost'
    assert channel_props['InitiatorHandle'] == conn.GetSelfHandle()

    # Exercise Group Properties from spec 0.17.6 (in a basic way)
    group_props = group_iface.GetAll(
            'org.freedesktop.Telepathy.Channel.Interface.Group',
            dbus_interface=dbus.PROPERTIES_IFACE)
    assert 'HandleOwners' in group_props, group_props
    assert 'Members' in group_props, group_props
    assert 'LocalPendingMembers' in group_props, group_props
    assert 'RemotePendingMembers' in group_props, group_props
    assert 'GroupFlags' in group_props, group_props

    list_streams_result = media_iface.ListStreams()
    assert len(list_streams_result) == 0, list_streams_result

    streams = media_iface.RequestStreams(handle,
            [cs.MEDIA_STREAM_TYPE_AUDIO])

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

    sh_props = stream_handler.GetAll(
            'org.freedesktop.Telepathy.Media.StreamHandler',
            dbus_interface=dbus.PROPERTIES_IFACE)
    assert sh_props['NATTraversal'] == 'gtalk-p2p'
    assert sh_props['CreatedLocally'] == True

    e = q.expect('stream-iq')
    assert e.query.name == 'jingle'
    assert e.query['action'] == 'session-initiate'
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
    assert e.args[5] == self_handle

    # Test completed, close the connection

    e = q.expect('dbus-signal', signal='Close') #XXX - match against the path

    conn.Disconnect()
    q.expect('dbus-signal', signal='StatusChanged', args=[2, 1])

    return True


if __name__ == '__main__':
    exec_test(test)

