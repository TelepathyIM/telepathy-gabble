"""
Test emition and handling of codec update using description-info
"""

from gabbletest import exec_test, sync_stream
from servicetest import (
    wrap_channel, assertEquals, make_channel_proxy, unwrap, EventPattern,
    call_async)
from jingletest2 import JingleTest2, JingleProtocol031
import constants as cs

from twisted.words.xish import xpath


def extract_params(payload_type):
    ret = {}
    for node in payload_type.elements():
        assert node.name == 'parameter'
        ret[node['name']] = node['value']
    return ret

def early_description_info(q, bus, conn, stream):
    test(q, bus, conn, stream, send_early_description_info=True)

def test(q, bus, conn, stream, send_early_description_info=False):
    jp = JingleProtocol031()
    jt2 = JingleTest2(jp, conn, q, stream, 'test@localhost', 'foo@bar.com/Foo')
    jt2.prepare()

    self_handle = conn.GetSelfHandle()
    remote_handle = conn.RequestHandles(cs.HT_CONTACT, ["foo@bar.com/Foo"])[0]

    # Remote end calls us
    jt2.incoming_call()

    # FIXME: these signals are not observable by real clients, since they
    #        happen before NewChannels.
    # The caller is in members
    e = q.expect('dbus-signal', signal='MembersChanged',
             args=[u'', [remote_handle], [], [], [], 0, 0])

    # We're pending because of remote_handle
    e = q.expect('dbus-signal', signal='MembersChanged',
             args=[u'', [], [], [self_handle], [], remote_handle,
                   cs.GC_REASON_INVITED])

    chan = wrap_channel(bus.get_object(conn.bus_name,  e.path),
        'StreamedMedia')

    # S-E gets notified about new session handler, and calls Ready on it
    e = q.expect('dbus-signal', signal='NewSessionHandler')
    assert e.args[1] == 'rtp'

    if send_early_description_info:
        """
        Regression test for a bug where Gabble would crash if you sent it
        description-info before calling Ready() on the relevant StreamHandler,
        and then for a bug where Gabble would never accept the call if a
        description-info was received before all StreamHandlers were Ready().
        """
        node = jp.SetIq(jt2.peer, jt2.jid, [
            jp.Jingle(jt2.sid, jt2.peer, 'description-info', [
                jp.Content('stream1', 'initiator', 'both', [
                    jp.Description('audio', [ ]) ]) ]) ])
        stream.send(jp.xml(node))

        sync_stream(q, stream)

    session_handler = make_channel_proxy(conn, e.args[0], 'Media.SessionHandler')
    session_handler.Ready()

    chan.Group.AddMembers([self_handle], 'accepted')

    # S-E gets notified about a newly-created stream
    e = q.expect('dbus-signal', signal='NewStreamHandler')
    id1 = e.args[1]

    stream_handler = make_channel_proxy(conn, e.args[0], 'Media.StreamHandler')

    # We are now in members too
    e = q.expect('dbus-signal', signal='MembersChanged',
             args=[u'', [self_handle], [], [], [], self_handle,
                   cs.GC_REASON_NONE])

    # we are now both in members
    members = chan.Group.GetMembers()
    assert set(members) == set([self_handle, remote_handle]), members

    local_codecs = [('GSM', 3, 8000, {}),
                    ('PCMA', 8, 8000, {'helix':'woo yay'}),
                    ('PCMU', 0, 8000, {}) ]
    local_codecs_dbus = jt2.dbusify_codecs_with_params(local_codecs)

    stream_handler.NewNativeCandidate("fake", jt2.get_remote_transports_dbus())
    stream_handler.Ready(local_codecs_dbus)
    stream_handler.StreamState(cs.MEDIA_STREAM_STATE_CONNECTED)

    stream_handler.CodecsUpdated(local_codecs_dbus)

    local_codecs = [('GSM', 3, 8000, {}),
                    ('PCMA', 8, 8000, {'gstreamer':'rock on'}),
                    ('PCMU', 0, 8000, {}) ]
    local_codecs_dbus = jt2.dbusify_codecs_with_params(local_codecs)
    stream_handler.CodecsUpdated(local_codecs_dbus)


    # First IQ is transport-info; also, we expect to be told what codecs the
    # other end wants.
    e, src = q.expect_many(
        EventPattern('stream-iq',
            predicate=jp.action_predicate('transport-info')),
        EventPattern('dbus-signal', signal='SetRemoteCodecs')
        )
    assertEquals('foo@bar.com/Foo', e.query['initiator'])

    assert jt2.audio_codecs == [ (name, id, rate)
        for id, name, type, rate, channels, parameters in unwrap(src.args[0]) ], \
        (jt2.audio_codecs, unwrap(src.args[0]))

    stream.send(jp.xml(jp.ResultIq('test@localhost', e.stanza, [])))

    # S-E reports codec intersection, after which gabble can send acceptance
    stream_handler.SupportedCodecs(local_codecs_dbus)

    # Second one is session-accept
    e = q.expect('stream-iq', predicate=jp.action_predicate('session-accept'))

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

    audio_content = jt2.audio_names[0]

    e = q.expect('stream-iq', iq_type='set', predicate=lambda x:
        xpath.queryForNodes("/iq/jingle[@action='description-info']",
            x.stanza))
    payload_types = xpath.queryForNodes(
        "/iq/jingle/content[@name='%s']/description/payload-type"
            % audio_content,
        e.stanza)
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
            jp.Content(audio_content, 'initiator', 'both', [
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
            jp.Content(audio_content, 'initiator', 'both', [
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
    chan.StreamedMedia.RemoveStreams([id1])

    e = q.expect('stream-iq', iq_type='set', predicate=lambda x:
        xpath.queryForNodes("/iq/jingle[@action='session-terminate']",
            x.stanza))

if __name__ == '__main__':
    exec_test(test)
    exec_test(early_description_info)

