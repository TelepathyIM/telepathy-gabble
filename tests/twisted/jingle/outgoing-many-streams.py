
"""
Test making outgoing call using CreateChannel. This tests the happy scenario
when the remote party accepts the call.
"""

import dbus

from gabbletest import exec_test, sync_stream
from servicetest import (
    make_channel_proxy, call_async, EventPattern)
import jingletest
import gabbletest

import constants as cs

def test(q, bus, conn, stream):
    jt = jingletest.JingleTest(stream, 'test@localhost', 'foo@bar.com/Foo')

    # If we need to override remote caps, feats, codecs or caps,
    # this is a good time to do it

    # Connecting
    conn.Connect()

    q.expect('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTING, cs.CSR_REQUESTED])

    q.expect('stream-authenticated')
    q.expect('dbus-signal', signal='PresenceUpdate',
        args=[{1L: (0L, {u'available': {}})}])
    q.expect('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])

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

    assert sig_path == path, (sig_path, path)
    assert sig_ct == cs.CHANNEL_TYPE_STREAMED_MEDIA, sig_ct
    assert sig_ht == cs.HT_CONTACT, sig_ht
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
    assert emitted_props[cs.INITIATOR_ID]  == 'test@localhost'

    signalling_iface = make_channel_proxy(conn, path, 'Channel.Interface.MediaSignalling')
    media_iface = make_channel_proxy(conn, path, 'Channel.Type.StreamedMedia')
    group_iface = make_channel_proxy(conn, path, 'Channel.Interface.Group')

    # Exercise basic Channel Properties from spec 0.17.7
    channel_props = group_iface.GetAll(cs.CHANNEL,
        dbus_interface=dbus.PROPERTIES_IFACE)
    assert channel_props.get('TargetHandle') == handle, \
            channel_props.get('TargetHandle')
    assert channel_props.get('TargetHandleType') == cs.HT_CONTACT,\
            channel_props.get('TargetHandleType')
    assert media_iface.GetHandle(dbus_interface=cs.CHANNEL) == (cs.HT_CONTACT,
            handle)
    assert channel_props.get('ChannelType') == cs.CHANNEL_TYPE_STREAMED_MEDIA,\
            channel_props.get('ChannelType')

    interfaces = channel_props['Interfaces']
    for i in [cs.CHANNEL_IFACE_GROUP, cs.CHANNEL_IFACE_MEDIA_SIGNALLING,
              cs.TP_AWKWARD_PROPERTIES, cs.CHANNEL_IFACE_HOLD]:
        assert i in interfaces, (i, interfaces)

    assert channel_props['TargetID'] == 'foo@bar.com', channel_props
    assert channel_props['Requested'] == True
    assert channel_props['InitiatorID'] == 'test@localhost'
    assert channel_props['InitiatorHandle'] == self_handle

    # Exercise Group Properties from spec 0.17.6 (in a basic way)
    group_props = group_iface.GetAll(cs.CHANNEL_IFACE_GROUP,
            dbus_interface=dbus.PROPERTIES_IFACE)
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

    # Asking for 4 audio and 3 video streams is pathological, but we claim to
    # support up to 99 streams, so we should test a decent number of them.
    #
    # More practically, the success of this test implies that the simpler case
    # of one audio stream and one video stream should easily work.
    streams = media_iface.RequestStreams(handle,
            [cs.MEDIA_STREAM_TYPE_AUDIO,
                cs.MEDIA_STREAM_TYPE_VIDEO,
                cs.MEDIA_STREAM_TYPE_VIDEO,
                cs.MEDIA_STREAM_TYPE_AUDIO,
                cs.MEDIA_STREAM_TYPE_AUDIO,
                cs.MEDIA_STREAM_TYPE_VIDEO,
                cs.MEDIA_STREAM_TYPE_AUDIO])
    assert len(streams) == 7, streams

    streams_by_id = {}

    for s in streams:
        streams_by_id[s[0]] = s

        assert len(s) == 6, s
        assert s[1] == handle, (s, handle)
        assert s[2] in (cs.MEDIA_STREAM_TYPE_AUDIO,
                cs.MEDIA_STREAM_TYPE_VIDEO), s
        # We haven't connected yet
        assert s[3] == cs.MEDIA_STREAM_STATE_DISCONNECTED, s
        # In Gabble, requested streams start off bidirectional
        assert s[4] == cs.MEDIA_STREAM_DIRECTION_BIDIRECTIONAL, s
        assert s[5] == 0, s # no pending send

    # the streams should all have unique IDs
    stream_ids = streams_by_id.keys()
    assert len(stream_ids) == 7

    # the streams should come out in the same order as the requests
    assert streams[0][2] == cs.MEDIA_STREAM_TYPE_AUDIO, streams[0]
    assert streams[1][2] == cs.MEDIA_STREAM_TYPE_VIDEO, streams[0]
    assert streams[2][2] == cs.MEDIA_STREAM_TYPE_VIDEO, streams[0]
    assert streams[3][2] == cs.MEDIA_STREAM_TYPE_AUDIO, streams[0]
    assert streams[4][2] == cs.MEDIA_STREAM_TYPE_AUDIO, streams[0]
    assert streams[5][2] == cs.MEDIA_STREAM_TYPE_VIDEO, streams[0]
    assert streams[6][2] == cs.MEDIA_STREAM_TYPE_AUDIO, streams[0]

    # The ListStreams() result must be the streams we got from RequestStreams,
    # but this time the order is unimportant
    list_streams_result = media_iface.ListStreams()
    listed_streams_by_id = {}

    for s in list_streams_result:
        listed_streams_by_id[s[0]] = s

    assert listed_streams_by_id == streams_by_id, (listed_streams_by_id,
            streams_by_id)

    # S-E gets notified about new session handler, and calls Ready on it
    e = q.expect('dbus-signal', signal='NewSessionHandler')
    assert e.args[1] == 'rtp'

    session_handler_path = e.args[0]
    session_handler = make_channel_proxy(conn, session_handler_path,
            'Media.SessionHandler')
    session_handler.Ready()

    stream_handler_paths = []

    # give all 7 streams some candidates
    for i in xrange(7):
        e = q.expect('dbus-signal', signal='NewStreamHandler')
        stream_handler_paths.append(e.args[0])
        stream_handler = make_channel_proxy(conn, e.args[0],
                'Media.StreamHandler')
        stream_handler.NewNativeCandidate("fake",
                jt.get_remote_transports_dbus())
        stream_handler.Ready(jt.get_audio_codecs_dbus())
        stream_handler.StreamState(cs.MEDIA_STREAM_STATE_CONNECTED)

    e = q.expect('stream-iq')
    assert e.query.name == 'jingle'
    assert e.query['action'] == 'session-initiate'
    stream.send(gabbletest.make_result_iq(stream, e.stanza))

    jt.outgoing_call_reply(e.query['sid'], True)

    q.expect('stream-iq', iq_type='result')

    # Time passes ... afterwards we close the chan

    group_iface.RemoveMembers([self_handle], 'closed')

    # Everything closes
    closes = [ EventPattern('dbus-signal', signal='Close',
                   path=stream_handler_paths[i])
               for i in range(0,7) ]
    removeds = [ EventPattern('dbus-signal', signal='StreamRemoved',
                     args=[stream_ids[i]], path=path)
               for i in range(0,7) ]
    q.expect_many(
        EventPattern('dbus-signal', signal='ChannelClosed', args=[path]),
        *(closes + removeds)
    )

if __name__ == '__main__':
    exec_test(test)
