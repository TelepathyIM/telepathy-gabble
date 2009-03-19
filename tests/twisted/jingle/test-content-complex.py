"""
Test everything related to contents
"""

from gabbletest import exec_test, make_result_iq, sync_stream, exec_tests
from servicetest import make_channel_proxy, unwrap, tp_path_prefix, \
        EventPattern
import gabbletest
import dbus
import time
from twisted.words.xish import xpath

from jingletest2 import *

import constants as cs

def worker(jp, q, bus, conn, stream):

    def make_stream_request(stream_type):
        media_iface.RequestStreams(remote_handle, [stream_type])

        e = q.expect('dbus-signal', signal='NewStreamHandler')
        stream_id = e.args[1]

        stream_handler = make_channel_proxy(conn, e.args[0], 'Media.StreamHandler')

        stream_handler.NewNativeCandidate("fake", jt2.get_remote_transports_dbus())
        stream_handler.Ready(jt2.get_audio_codecs_dbus())
        stream_handler.StreamState(cs.MEDIA_STREAM_STATE_CONNECTED)
        return (stream_handler, stream_id)


    jt2 = JingleTest2(jp, conn, q, stream, 'test@localhost', 'foo@bar.com/Foo')
    jt2.prepare()

    self_handle = conn.GetSelfHandle()
    remote_handle = conn.RequestHandles(cs.HT_CONTACT, ["foo@bar.com/Foo"])[0]

    # Remote end calls us
    # jt.incoming_call()
    node = jp.SetIq(jt2.peer, jt2.jid, [
        jp.Jingle(jt2.sid, jt2.peer, 'session-initiate', [
            jp.Content('stream1', 'initiator', 'both', [
                jp.Description('audio', [
                    jp.PayloadType(name, str(rate), str(id)) for
                        (name, id, rate) in jt2.audio_codecs ]),
            jp.TransportGoogleP2P() ]) ]) ])
    stream.send(jp.xml(node))

    # FIXME: these signals are not observable by real clients, since they
    #        happen before NewChannels.
    # The caller is in members
    e = q.expect('dbus-signal', signal='MembersChanged',
             args=[u'', [remote_handle], [], [], [], 0, 0])

    # We're pending because of remote_handle
    e = q.expect('dbus-signal', signal='MembersChanged',
             args=[u'', [], [], [self_handle], [], remote_handle,
                   cs.GC_REASON_INVITED])

    media_chan = make_channel_proxy(conn, tp_path_prefix + e.path, 'Channel.Interface.Group')
    signalling_iface = make_channel_proxy(conn, tp_path_prefix + e.path, 'Channel.Interface.MediaSignalling')
    media_iface = make_channel_proxy(conn, tp_path_prefix + e.path, 'Channel.Type.StreamedMedia')

    # S-E gets notified about new session handler, and calls Ready on it
    e = q.expect('dbus-signal', signal='NewSessionHandler')
    assert e.args[1] == 'rtp'

    session_handler = make_channel_proxy(conn, e.args[0], 'Media.SessionHandler')
    session_handler.Ready()

    media_chan.AddMembers([self_handle], 'accepted')

    # S-E gets notified about a newly-created stream
    e = q.expect('dbus-signal', signal='NewStreamHandler')
    id1 = e.args[1]

    stream_handler = make_channel_proxy(conn, e.args[0], 'Media.StreamHandler')

    # We are now in members too
    e = q.expect('dbus-signal', signal='MembersChanged',
             args=[u'', [self_handle], [], [], [], 0, 0])

    # we are now both in members
    members = media_chan.GetMembers()
    assert set(members) == set([self_handle, remote_handle]), members

    stream_handler.NewNativeCandidate("fake", jt2.get_remote_transports_dbus())
    stream_handler.Ready(jt2.get_audio_codecs_dbus())
    stream_handler.StreamState(cs.MEDIA_STREAM_STATE_CONNECTED)

    # First one is transport-info
    e = q.expect('stream-iq')
    assert jp.match_jingle_action(e.query, 'transport-info')
    assert e.query['initiator'] == 'foo@bar.com/Foo'

    # stream.send(gabbletest.make_result_iq(stream, e.stanza))
    stream.send(jp.xml(jp.ResultIq('test@localhost', e.stanza, [])))

    # S-E reports codec intersection, after which gabble can send acceptance
    stream_handler.SupportedCodecs(jt2.get_audio_codecs_dbus())

    # Second one is session-accept
    e = q.expect('stream-iq')
    assert jp.match_jingle_action(e.query, 'session-accept')

    # stream.send(gabbletest.make_result_iq(stream, e.stanza))
    stream.send(jp.xml(jp.ResultIq('test@localhost', e.stanza, [])))

    # Here starts the interesting part of this test
    # Remote end tries to create a content we can't handle
    node = jp.SetIq(jt2.peer, jt2.jid, [
        jp.Jingle(jt2.sid, jt2.peer, 'content-add', [
            jp.Content('bogus', 'initiator', 'both', [
                jp.Description('hologram', [
                    jp.PayloadType(name, str(rate), str(id)) for
                        (name, id, rate) in jt2.audio_codecs ]),
            jp.TransportGoogleP2P() ]) ]) ])
    stream.send(jp.xml(node))

    # In older Jingle, this is a separate namespace, which isn't
    # recognized, but it's a valid request, so it gets ackd and rejected
    if jp.dialect == 'jingle-v0.15':
        # Gabble should acknowledge content-add
        q.expect('stream-iq', iq_type='result')

        # .. and then send content-reject for the bogus content
        e = q.expect('stream-iq', iq_type='set', predicate=lambda x:
            xpath.queryForNodes("/iq/jingle[@action='content-reject']/content[@name='bogus']",
                x.stanza))

    # In new Jingle, this is a bogus subtype of recognized namespace,
    # so Gabble returns a bad request error
    else:
        q.expect('stream-iq', iq_type='error')


    # Remote end then tries to create content which we already have
    node = jp.SetIq(jt2.peer, jt2.jid, [
        jp.Jingle(jt2.sid, jt2.peer, 'content-add', [
            jp.Content('stream1', 'initiator', 'both', [
                jp.Description('audio', [
                    jp.PayloadType(name, str(rate), str(id)) for
                        (name, id, rate) in jt2.audio_codecs ]),
            jp.TransportGoogleP2P() ]) ]) ])
    stream.send(jp.xml(node))

    # Gabble should return error (content already exists)
    q.expect('stream-iq', iq_type='error')

    (stream_handler2, id2) = make_stream_request(cs.MEDIA_STREAM_TYPE_VIDEO)

    # Gabble should now send content-add
    e = q.expect('stream-iq', iq_type='set', predicate=lambda x:
        xpath.queryForNodes("/iq/jingle[@action='content-add']",
            x.stanza))

    c = e.query.firstChildElement()
    assert c['creator'] == 'responder', c['creator']

    stream.send(jp.xml(jp.ResultIq('test@localhost', e.stanza, [])))


    # Remote end rejects it
    node = jp.SetIq(jt2.peer, jt2.jid, [
        jp.Jingle(jt2.sid, jt2.peer, 'content-reject', [
            jp.Content(c['name'], c['creator'], c['senders'], []) ]) ])
    stream.send(jp.xml(node))

    # Gabble removes the stream
    q.expect('dbus-signal', signal='StreamRemoved',
        interface='org.freedesktop.Telepathy.Channel.Type.StreamedMedia')


    # We try to make the request again, and succeed
    (stream_handler3, id3) = make_stream_request(cs.MEDIA_STREAM_TYPE_VIDEO)

    # Gabble should again send content-add
    e = q.expect('stream-iq', iq_type='set', predicate=lambda x:
        xpath.queryForNodes("/iq/jingle[@action='content-add']",
            x.stanza))
    stream.send(jp.xml(jp.ResultIq('test@localhost', e.stanza, [])))
    c = e.query.firstChildElement()

    # Remote end finally accepts
    node = jp.SetIq(jt2.peer, jt2.jid, [
        jp.Jingle(jt2.sid, jt2.peer, 'content-accept', [
            jp.Content(c['name'], c['creator'], c['senders'], [
                jp.Description('video', [
                    jp.PayloadType(name, str(rate), str(id)) for
                        (name, id, rate) in jt2.audio_codecs ]),
            jp.TransportGoogleP2P() ]) ]) ])
    stream.send(jp.xml(node))

    # We get remote codecs
    e = q.expect('dbus-signal', signal='SetRemoteCodecs')

    # Now, both we and remote peer try to remove the content simultaneously
    media_iface.RemoveStreams([id3])
    node = jp.SetIq(jt2.peer, jt2.jid, [
        jp.Jingle(jt2.sid, jt2.peer, 'content-remove', [
            jp.Content(c['name'], c['creator'], c['senders'], []) ]) ])
    stream.send(jp.xml(node))

    # Gabble should ignore our content-remove and send it's own
    # (fixme: this could be racy)
    e = q.expect('stream-iq', iq_type='set', predicate=lambda x:
        xpath.queryForNodes("/iq/jingle[@action='content-remove']",
            x.stanza))

    # Now we want to remove the first stream
    media_iface.RemoveStreams([id1])

    # The remote peer still hasn't ackd the first stream removal, but since
    # gabble knows no streams will be left after the removal completes,
    # it will just terminate the session.

    e = q.expect('stream-iq', iq_type='set', predicate=lambda x:
        xpath.queryForNodes("/iq/jingle[@action='session-terminate']",
            x.stanza))

    conn.Disconnect()
    q.expect('dbus-signal', signal='StatusChanged', args=[2, 1])

    return True


def test015(q, bus, conn, stream):
    return worker(JingleProtocol015(), q, bus, conn, stream)

def test031(q, bus, conn, stream):
    return worker(JingleProtocol031(),q, bus, conn, stream)

if __name__ == '__main__':
    exec_test(test015)
    exec_test(test031)

