"""
Test incoming call handling.
"""

from gabbletest import exec_test, make_result_iq, sync_stream, exec_tests
from servicetest import make_channel_proxy, unwrap, tp_path_prefix, \
        EventPattern
import gabbletest
import dbus
import time
from twisted.words.xish import xpath

from jingletest2 import *

def worker(jp, q, bus, conn, stream):

    jt2 = JingleTest2(jp, conn, q, stream, 'test@localhost', 'foo@bar.com/Foo')
    jt2.prepare()

    remote_handle = conn.RequestHandles(1, ["foo@bar.com/Foo"])[0]

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

    # we don't even need this here, because we've provided a very strict
    # predicate to expect_racy() so it won't get the wrong event
    # q.flush_past_events()

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

    media_chan.AddMembers([dbus.UInt32(1)], 'accepted')

    # S-E gets notified about a newly-created stream
    e = q.expect('dbus-signal', signal='NewStreamHandler')

    stream_handler = make_channel_proxy(conn, e.args[0], 'Media.StreamHandler')

    # We are now in members too
    e = q.expect_racy('dbus-signal', signal='MembersChanged',
             args=[u'', [1L], [], [], [], 0, 0])

    # we are now both in members
    members = media_chan.GetMembers()
    assert set(members) == set([1L, remote_handle]), members

    stream_handler.NewNativeCandidate("fake", jt2.get_remote_transports_dbus())
    stream_handler.Ready(jt2.get_audio_codecs_dbus())
    stream_handler.StreamState(2)

    # In gtalk4, first one will be transport-accept, telling us that GTalk
    # is ok with our choice of transports.
    if jp.dialect == 'gtalk-v0.4':
        e = q.expect_racy('stream-iq', predicate=lambda x:
            xpath.queryForNodes("/iq/session[@type='transport-accept']",
                x.stanza))

    # First one is transport-info
    e = q.expect('stream-iq')
    assert jp.match_jingle_action(e.query, 'transport-info')
    assert e.query['initiator'] == 'foo@bar.com/Foo'

    # stream.send(gabbletest.make_result_iq(stream, e.stanza))
    stream.send(jp.xml(jp.ResultIq('test@localhost', e.stanza, [])))

    # Set codec intersection so gabble can accept the session
    stream_handler.SupportedCodecs(jt2.get_audio_codecs_dbus())

    # Second one is session-accept
    e = q.expect('stream-iq')
    assert jp.match_jingle_action(e.query, 'session-accept')

    # stream.send(gabbletest.make_result_iq(stream, e.stanza))
    stream.send(jp.xml(jp.ResultIq('test@localhost', e.stanza, [])))

    # Connected! Blah, blah, ...

    # 'Nuff said
    # jt.remote_terminate()
    node = jp.SetIq(jt2.peer, jt2.jid, [
        jp.Jingle(jt2.sid, jt2.peer, 'session-terminate', []) ])
    stream.send(jp.xml(node))

    # Tests completed, close the connection

    e = q.expect('dbus-signal', signal='Close') #XXX - match against the path

    conn.Disconnect()
    q.expect('dbus-signal', signal='StatusChanged', args=[2, 1])

    return True


def test015(q, bus, conn, stream):
    return worker(JingleProtocol015(), q, bus, conn, stream)

def test031(q, bus, conn, stream):
    return worker(JingleProtocol031(),q, bus, conn, stream)

def testg3(q, bus, conn, stream):
    return worker(GtalkProtocol03(), q, bus, conn, stream)

def testg4(q, bus, conn, stream):
    return worker(GtalkProtocol04(), q, bus, conn, stream)

if __name__ == '__main__':
    exec_tests([testg3, testg4, test015, test031])

