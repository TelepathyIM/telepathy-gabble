"""
Test outgoing call handling.
"""

import dbus
from twisted.words.xish import xpath

from servicetest import make_channel_proxy, EventPattern, call_async
import constants as cs
from jingletest2 import JingleTest2, test_all_dialects

def worker(jp, q, bus, conn, stream):

    jt2 = JingleTest2(jp, conn, q, stream, 'test@localhost', 'foo@bar.com/Foo')
    jt2.prepare()

    remote_handle = conn.RequestHandles(1, ["foo@bar.com/Foo"])[0]
    call_async(
        q, conn, 'RequestChannel', cs.CHANNEL_TYPE_STREAMED_MEDIA, 0, 0, True)

    ret, old_sig, new_sig = q.expect_many(
        EventPattern('dbus-return', method='RequestChannel'),
        EventPattern('dbus-signal', signal='NewChannel'),
        EventPattern('dbus-signal', signal='NewChannels'),
        )
    path = ret.value[0]

    signalling_iface = make_channel_proxy(conn, path, 'Channel.Interface.MediaSignalling')
    media_iface = make_channel_proxy(conn, path, 'Channel.Type.StreamedMedia')
    group_iface = make_channel_proxy(conn, path, 'Channel.Interface.Group')

    media_iface.RequestStreams(remote_handle, [0]) # 0 == MEDIA_STREAM_TYPE_AUDIO

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

    e = q.expect('stream-iq')
    if jp.dialect.startswith('gtalk'):
        assert e.query.name == 'session'
        assert e.query['type'] == 'initiate'
        jt2.sid = e.query['id']
    else:
        assert e.query.name == 'jingle'
        assert e.query['action'] == 'session-initiate'
        jt2.sid = e.query['sid']

    # stream.send(gabbletest.make_result_iq(stream, e.stanza))
    stream.send(jp.xml(jp.ResultIq('test@localhost', e.stanza, [])))

    if jp.dialect == 'gtalk-v0.4':
        node = jp.SetIq(jt2.peer, jt2.jid, [
            jp.Jingle(jt2.sid, jt2.peer, 'transport-accept', [
                jp.TransportGoogleP2P() ]) ])
        stream.send(jp.xml(node))

    # FIXME: expect transport-info, then if we're gtalk3, send
    # candidates, and check that gabble resends transport-info as
    # candidates
    # jt.outgoing_call_reply(e.query['sid'], True)
    node = jp.SetIq(jt2.peer, jt2.jid, [
        jp.Jingle(jt2.sid, jt2.peer, 'session-accept', [
            jp.Content('stream1', 'initiator', 'both', [
                jp.Description('audio', [
                    jp.PayloadType(name, str(rate), str(id)) for
                        (name, id, rate) in jt2.audio_codecs ]),
            jp.TransportGoogleP2P() ]) ]) ])
    stream.send(jp.xml(node))

    q.expect('stream-iq', iq_type='result')

    # Time passes ... afterwards we close the chan

    group_iface.RemoveMembers([dbus.UInt32(1)], 'closed')

    # Make sure gabble sends proper terminate action
    if jp.dialect.startswith('gtalk'):
        terminate = EventPattern('stream-iq', predicate=lambda x:
            xpath.queryForNodes("/iq/session[@type='terminate']",
                x.stanza))
    else:
        terminate = EventPattern('stream-iq', predicate=lambda x:
            xpath.queryForNodes("/iq/jingle[@action='session-terminate']",
                x.stanza))

    q.expect_many(
        EventPattern('dbus-signal', signal='Close'),
        terminate,
        )

    conn.Disconnect()
    q.expect('dbus-signal', signal='StatusChanged', args=[2, 1])

    return True


if __name__ == '__main__':
    test_all_dialects(worker)

