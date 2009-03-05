"""
Test emition and handling of codec update using description-info
"""

from gabbletest import exec_test, make_result_iq, sync_stream, exec_tests
from servicetest import make_channel_proxy, unwrap, tp_path_prefix, \
        EventPattern, call_async
import gabbletest
import dbus
import time
from twisted.words.xish import xpath

from jingletest2 import *

import constants as cs

def extract_params(payload_type):
    ret = {}
    for node in payload_type.elements():
        assert node.name == 'parameter'
        ret[node['name']] = node['value']
    return ret

def test(q, bus, conn, stream):

    jp = JingleProtocol031()

    def make_stream_request(stream_type):
        media_iface.RequestStreams(remote_handle, [stream_type])

        e = q.expect('dbus-signal', signal='NewStreamHandler')
        stream_id = e.args[1]

        stream_handler = make_channel_proxy(conn, e.args[0], 'Media.StreamHandler')

        stream_handler.NewNativeCandidate("fake", jt2.get_remote_transports_dbus())
        stream_handler.Ready(jt2.get_audio_codecs_dbus())
        stream_handler.StreamState(2)
        return (stream_handler, stream_id)


    jt2 = JingleTest2(jp, conn, q, stream, 'test@localhost', 'foo@bar.com/Foo')
    jt2.prepare()

    remote_handle = conn.RequestHandles(1, ["foo@bar.com/Foo"])[0]

    # Remote end calls us
    node = jp.SetIq(jt2.peer, jt2.jid, [
        jp.Jingle(jt2.sid, jt2.peer, 'session-initiate', [
            jp.Content('stream1', 'initiator', 'both', [
                jp.Description('audio', [
                    jp.PayloadType(name, str(rate), str(id)) for
                        (name, id, rate) in jt2.audio_codecs ]),
            jp.TransportGoogleP2P() ]) ]) ])
    stream.send(jp.xml(node))

    # The caller is in members
    e = q.expect('dbus-signal', signal='MembersChanged',
             args=[u'', [remote_handle], [], [], [], 0, 0])

    # We're pending because of remote_handle
    e = q.expect('dbus-signal', signal='MembersChanged',
             args=[u'', [], [], [1L], [], remote_handle, 0])

    media_chan = make_channel_proxy(conn, tp_path_prefix + e.path, 'Channel.Interface.Group')
    signalling_iface = make_channel_proxy(conn, tp_path_prefix + e.path, 'Channel.Interface.MediaSignalling')
    media_iface = make_channel_proxy(conn, tp_path_prefix + e.path, 'Channel.Type.StreamedMedia')

    # S-E gets notified about new session handler, and calls Ready on it
    e = q.expect('dbus-signal', signal='NewSessionHandler')
    assert e.args[1] == 'rtp'

    session_handler = make_channel_proxy(conn, e.args[0], 'Media.SessionHandler')
    session_handler.Ready()

    media_chan.AddMembers([dbus.UInt32(1)], 'accepted')

    # S-E gets notified about a newly-created stream
    e = q.expect('dbus-signal', signal='NewStreamHandler')
    id1 = e.args[1]

    stream_handler = make_channel_proxy(conn, e.args[0], 'Media.StreamHandler')

    # We are now in members too
    e = q.expect('dbus-signal', signal='MembersChanged',
             args=[u'', [1L], [], [], [], 0, 0])

    # we are now both in members
    members = media_chan.GetMembers()
    assert set(members) == set([1L, remote_handle]), members

    local_codecs = [('GSM', 3, 8000, {}),
                    ('PCMA', 8, 8000, {'helix':'woo yay'}),
                    ('PCMU', 0, 8000, {}) ]
    local_codecs_dbus = jt2.dbusify_codecs_with_params(local_codecs)

    stream_handler.NewNativeCandidate("fake", jt2.get_remote_transports_dbus())
    stream_handler.Ready(local_codecs_dbus)
    stream_handler.StreamState(2)

    # First IQ is transport-info; also, we expect to be told what codecs the
    # other end wants.
    e, src = q.expect_many(
        EventPattern('stream-iq'),
        EventPattern('dbus-signal', signal='SetRemoteCodecs')
        )
    assert jp.match_jingle_action(e.query, 'transport-info')
    assert e.query['initiator'] == 'foo@bar.com/Foo'

    assert jt2.audio_codecs == [ (name, id, rate)
        for id, name, type, rate, channels, parameters in unwrap(src.args[0]) ], \
        (jt2.audio_codecs, unwrap(src.args[0]))

    stream.send(jp.xml(jp.ResultIq('test@localhost', e.stanza, [])))

    # S-E reports codec intersection, after which gabble can send acceptance
    stream_handler.SupportedCodecs(local_codecs_dbus)

    # Second one is session-accept
    e = q.expect('stream-iq')
    assert jp.match_jingle_action(e.query, 'session-accept')

    # farstream is buggy, and tells tp-fs to tell Gabble to change the third
    # codec's clockrate. This isn't legal, so Gabble says no.
    new_codecs = [ ('GSM', 3, 8000),
                   ('PCMA', 8, 8000),
                   ('PCMU', 0, 4000) ]
    call_async(q, stream_handler, 'CodecsUpdated',
        jt2.dbusify_codecs(new_codecs))
    event = q.expect('dbus-error', method='CodecsUpdated')
    assert event.error.get_dbus_name() == cs.INVALID_ARGUMENT, \
        event.error.get_dbus_name()

    # With its tail between its legs, tp-fs decides it wants to add some
    # parameters to the first two codecs, not changing the third.
    new_codecs = [ ('GSM', 3, 8000, {'type': 'banana'}),
                   ('PCMA', 8, 8000, {'helix': 'BUFFERING'}),
                   ('PCMU', 0, 8000, {}) ]
    stream_handler.CodecsUpdated(jt2.dbusify_codecs_with_params(new_codecs))

    e = q.expect('stream-iq', iq_type='set', predicate=lambda x:
        xpath.queryForNodes("/iq/jingle[@action='description-info']",
            x.stanza))
    payload_types = xpath.queryForNodes(
        "/iq/jingle/content[@name='stream1']/description/payload-type", e.stanza)
    # Gabble SHOULD only include the changed codecs in description-info
    assert len(payload_types) == 2, payload_types

    payload_types_tupled = [ (pt['name'], int(pt['id']), int(pt['clockrate']),
                              extract_params(pt))
                             for pt in payload_types ]
    assert sorted(payload_types_tupled) == sorted(new_codecs[0:2]), \
        (payload_types_tupled, new_codecs[0:2])

    # The remote end decides it wants to change the number of channels in the
    # third codec. This is not meant to happen, so Gabble should send it an IQ
    # error back.
    node = jp.SetIq(jt2.peer, jt2.jid, [
        jp.Jingle(jt2.sid, jt2.peer, 'description-info', [
            jp.Content('stream1', 'initiator', 'both', [
                jp.Description('audio', [
                    jp.PayloadType('PCMU', '1600', '0') ]) ]) ]) ])
    stream.send(jp.xml(node))
    q.expect('stream-iq', iq_type='error',
        predicate=lambda x: x.stanza['id'] == node[2]['id'])

    # Instead, the remote end decides to add a parameter to the third codec.
    new_codecs = [ ('GSM', 3, 8000, {}),
                   ('PCMA', 8, 8000, {}),
                   ('PCMU', 0, 8000, {'choppy': 'false'}),
                 ]
    # As per the XEP, it only sends the ones which have changed.
    c = new_codecs[2]
    node = jp.SetIq(jt2.peer, jt2.jid, [
        jp.Jingle(jt2.sid, jt2.peer, 'description-info', [
            jp.Content('stream1', 'initiator', 'both', [
                jp.Description('audio', [
                    jp.PayloadType(c[0], str(c[2]), str(c[1]), c[3])
                ]) ]) ]) ])
    stream.send(jp.xml(node))

    # Gabble should patch its idea of the remote codecs with the update it just
    # got, and emit SetRemoteCodecs for them all.
    e = q.expect('dbus-signal', signal='SetRemoteCodecs')
    new_codecs_dbus = unwrap(jt2.dbusify_codecs_with_params(new_codecs))
    announced = unwrap(e.args[0])
    assert new_codecs_dbus == announced, (new_codecs_dbus, announced)

    # We close the session by removing the stream
    media_iface.RemoveStreams([id1])

    e = q.expect('stream-iq', iq_type='set', predicate=lambda x:
        xpath.queryForNodes("/iq/jingle[@action='session-terminate']",
            x.stanza))

    conn.Disconnect()
    q.expect('dbus-signal', signal='StatusChanged', args=[2, 1])

    return True

if __name__ == '__main__':
    exec_test(test)

