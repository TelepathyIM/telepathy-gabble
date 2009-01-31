"""
Test incoming call handling.
"""

from gabbletest import exec_test, make_result_iq, sync_stream
from servicetest import make_channel_proxy, unwrap, tp_path_prefix, \
        EventPattern, sync_dbus
import jingletest
import gabbletest
import dbus
import time


def test(q, bus, conn, stream):
    jt = jingletest.JingleTest(stream, 'test@localhost', 'foo@bar.com/Foo')

    # If we need to override remote caps, feats, codecs or caps,
    # this is a good time to do it

    # Connecting
    conn.Connect()

    q.expect_many(
            EventPattern('dbus-signal', signal='StatusChanged', args=[1, 1]),
            EventPattern('stream-authenticated'),
            EventPattern('dbus-signal', signal='PresenceUpdate',
                args=[{1L: (0L, {u'available': {}})}]),
            EventPattern('dbus-signal', signal='StatusChanged', args=[0, 1]),
            )

    # We need remote end's presence for capabilities
    jt.send_remote_presence()

    # Gabble doesn't trust it, so makes a disco
    event = q.expect('stream-iq', query_ns='http://jabber.org/protocol/disco#info',
             to='foo@bar.com/Foo')

    jt.send_remote_disco_reply(event.stanza)

    # Force Gabble to process the caps before calling RequestChannel
    sync_stream(q, stream)

    remote_handle = conn.RequestHandles(1, ["foo@bar.com/Foo"])[0]

    # Remote end calls us
    jt.incoming_call()

    # The caller is in members
    e = q.expect('dbus-signal', signal='MembersChanged',
             args=[u'', [remote_handle], [], [], [], 0, 0])

    # We're pending because of remote_handle
    e = q.expect('dbus-signal', signal='MembersChanged',
             args=[u'', [], [], [1L], [], remote_handle, 0])

    media_chan = make_channel_proxy(conn, tp_path_prefix + e.path, 'Channel.Interface.Group')

    # S-E gets notified about new session handler, and calls Ready on it
    e = q.expect('dbus-signal', signal='NewSessionHandler')
    assert e.args[1] == 'rtp'

    session_handler = make_channel_proxy(conn, e.args[0], 'Media.SessionHandler')
    session_handler.Ready()


    # S-E gets notified about a newly-created stream
    e = q.expect('dbus-signal', signal='NewStreamHandler')

    stream_handler = make_channel_proxy(conn, e.args[0], 'Media.StreamHandler')

    # Exercise channel properties
    channel_props = media_chan.GetAll(
            'org.freedesktop.Telepathy.Channel',
            dbus_interface=dbus.PROPERTIES_IFACE)
    assert channel_props['TargetHandle'] == remote_handle
    assert channel_props['TargetHandleType'] == 1
    assert channel_props['TargetID'] == 'foo@bar.com'
    assert channel_props['InitiatorID'] == 'foo@bar.com'
    assert channel_props['InitiatorHandle'] == remote_handle
    assert channel_props['Requested'] == False

    # Connectivity checks happen before we have accepted the call
    stream_handler.NewNativeCandidate("fake", jt.get_remote_transports_dbus())
    stream_handler.Ready(jt.get_audio_codecs_dbus())
    stream_handler.StreamState(2)
    stream_handler.SupportedCodecs(jt.get_audio_codecs_dbus())

    # peer gets the transport
    e = q.expect('stream-iq')
    assert e.query.name == 'jingle'
    assert e.query['action'] == 'transport-info'
    assert e.query['initiator'] == 'foo@bar.com/Foo'

    stream.send(gabbletest.make_result_iq(stream, e.stanza))

    # Make sure everything's processed
    sync_stream(q, stream)
    sync_dbus(bus, q, conn)

    # At last, accept the call
    media_chan.AddMembers([dbus.UInt32(1)], 'accepted')

    # Call is accepted and we're in members too
    memb, acc = q.expect_many(
        EventPattern('dbus-signal', signal='MembersChanged', args=[u'', [1L],
            [], [], [], 0, 0]),
        EventPattern('stream-iq',
            predicate=lambda e: (e.query.name == 'jingle' and
                e.query['action'] == 'session-accept')))

    # we are now both in members
    members = media_chan.GetMembers()
    assert set(members) == set([1L, remote_handle]), members

    stream.send(gabbletest.make_result_iq(stream, e.stanza))

    # Connected! Blah, blah, ...

    # 'Nuff said
    jt.remote_terminate()


    # Tests completed, close the connection

    e = q.expect('dbus-signal', signal='Closed') #XXX - match against the path

    conn.Disconnect()
    q.expect('dbus-signal', signal='StatusChanged', args=[2, 1])

    return True


if __name__ == '__main__':
    exec_test(test)

