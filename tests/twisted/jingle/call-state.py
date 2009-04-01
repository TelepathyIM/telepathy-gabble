"""
Test exposing incoming <hold/> and <active/> notifications via the CallState
interface.
"""

from twisted.words.xish import xpath

from gabbletest import make_result_iq, sync_stream
from servicetest import wrap_channel, make_channel_proxy, call_async, \
    EventPattern
import ns
import constants as cs

from jingletest2 import JingleTest2, test_all_dialects

def test(jp, q, bus, conn, stream):
    # We can only get call state notifications on modern jingle.
    # TODO: but if we fake Hold by changing senders="", we could check for that
    # here.
    if not jp.is_modern_jingle():
        return

    remote_jid = 'foo@bar.com/Foo'
    jt = JingleTest2(jp, conn, q, stream, 'test@localhost', remote_jid)

    jt.prepare()

    self_handle = conn.GetSelfHandle()
    handle = conn.RequestHandles(cs.HT_CONTACT, [remote_jid])[0]

    path = conn.RequestChannel(cs.CHANNEL_TYPE_STREAMED_MEDIA, cs.HT_CONTACT,
        handle, True)

    chan = wrap_channel(bus.get_object(conn.bus_name, path), 'StreamedMedia',
        ['MediaSignalling', 'Group', 'CallState'])
    chan_props = chan.Properties.GetAll(cs.CHANNEL)
    assert cs.CHANNEL_IFACE_CALL_STATE in chan_props['Interfaces'], \
        chan_props['Interfaces']

    chan.StreamedMedia.RequestStreams(handle, [cs.MEDIA_STREAM_TYPE_AUDIO])

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

    e = q.expect('stream-iq', predicate=lambda e:
        jp.match_jingle_action(e.query, 'session-initiate'))
    stream.send(make_result_iq(stream, e.stanza))

    jt.set_sid_from_initiate(e.query)
    jt.accept()

    # Various misc happens; among other things, Gabble tells s-e to start
    # sending.
    q.expect('dbus-signal', signal='SetStreamSending', args=[True])

    call_states = chan.CallState.GetCallStates()
    assert call_states == { handle: 0 } or call_states == {}, call_states

    # The other person puts us on hold.  Gabble should ack the session-info IQ,
    # tell s-e to stop sending on the held stream, and set the call state.
    node = jp.SetIq(jt.peer, jt.jid, [
        jp.Jingle(jt.sid, jt.jid, 'session-info', [
            ('hold', ns.JINGLE_RTP_INFO_1, {}, []) ]) ])
    stream.send(jp.xml(node))

    q.expect_many(
        EventPattern('stream-iq', iq_type='result', iq_id=node[2]['id']),
        EventPattern('dbus-signal', signal='SetStreamSending', args=[False]),
        EventPattern('dbus-signal', signal='CallStateChanged',
            args=[handle, cs.CALL_STATE_HELD]),
        )

    call_states = chan.CallState.GetCallStates()
    assert call_states == { handle: cs.CALL_STATE_HELD }, call_states

    # The peer pings us with an empty session-info; Gabble should just ack it.
    node = jp.SetIq(jt.peer, jt.jid, [
        jp.Jingle(jt.sid, jt.jid, 'session-info', [])])
    stream.send(jp.xml(node))

    q.expect('stream-iq', iq_type='result', iq_id=node[2]['id'])

    call_states = chan.CallState.GetCallStates()
    assert call_states == { handle: cs.CALL_STATE_HELD }, call_states

    # The peer sends us some unknown-namespaced misc in a session-info; Gabble
    # should nak it with <unsupported-info/>
    node = jp.SetIq(jt.peer, jt.jid, [
        jp.Jingle(jt.sid, jt.jid, 'session-info', [
            ('boiling', 'com.example.Kettle', {}, []) ]) ])
    stream.send(jp.xml(node))

    e = q.expect('stream-iq', iq_type='error', iq_id=node[2]['id'])
    xpath.queryForNodes("/jingle/error/unsupported-info", e.query)

    call_states = chan.CallState.GetCallStates()
    assert call_states == { handle: cs.CALL_STATE_HELD }, call_states

    # The other person unholds us; Gabble should ack the session-info IQ, tell
    # s-e to start sending on the now-active stream, and set the call state.
    node = jp.SetIq(jt.peer, jt.jid, [
        jp.Jingle(jt.sid, jt.jid, 'session-info', [
            ('active', ns.JINGLE_RTP_INFO_1, {}, []) ]) ])
    stream.send(jp.xml(node))

    q.expect_many(
        EventPattern('stream-iq', iq_type='result', iq_id=node[2]['id']),
        EventPattern('dbus-signal', signal='SetStreamSending', args=[True]),
        EventPattern('dbus-signal', signal='CallStateChanged',
            args=[handle, 0]),
        )

    call_states = chan.CallState.GetCallStates()
    assert call_states == { handle: 0 } or call_states == {}, call_states

    # Test completed, close the connection

    chan.Group.RemoveMembers([self_handle], 'closed')
    e = q.expect('dbus-signal', signal='Close') #XXX - match against the path

    conn.Disconnect()
    q.expect('dbus-signal', signal='StatusChanged', args=[2, 1])

if __name__ == '__main__':
    test_all_dialects(test)

