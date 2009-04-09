"""
Test incoming call handling.
"""

from servicetest import make_channel_proxy, tp_path_prefix
import constants as cs
from jingletest2 import JingleTest2, test_all_dialects

from twisted.words.xish import xpath

def worker(jp, q, bus, conn, stream):

    jt2 = JingleTest2(jp, conn, q, stream, 'test@localhost', 'foo@bar.com/Foo')
    jt2.prepare()

    self_handle = conn.GetSelfHandle()
    remote_handle = conn.RequestHandles(cs.HT_CONTACT, ["foo@bar.com/Foo"])[0]

    # Remote end calls us
    jt2.incoming_call()

    # we don't even need this here, because we've provided a very strict
    # predicate to expect_racy() so it won't get the wrong event
    # q.flush_past_events()

    # FIXME: these signals are not observable by real clients, since they
    #        happen before NewChannels.
    # The caller is in members
    e = q.expect('dbus-signal', signal='MembersChanged',
             args=[u'', [remote_handle], [], [], [], 0, 0])

    # We're pending because of remote_handle
    e = q.expect('dbus-signal', signal='MembersChanged',
             args=[u'', [], [], [1L], [], remote_handle, cs.GC_REASON_INVITED])

    media_chan = make_channel_proxy(conn, tp_path_prefix + e.path, 'Channel.Interface.Group')


    # S-E gets notified about new session handler, and calls Ready on it
    e = q.expect('dbus-signal', signal='NewSessionHandler')
    assert e.args[1] == 'rtp'

    session_handler = make_channel_proxy(conn, e.args[0], 'Media.SessionHandler')
    session_handler.Ready()

    media_chan.AddMembers([self_handle], 'accepted')

    # S-E gets notified about a newly-created stream
    e = q.expect('dbus-signal', signal='NewStreamHandler')

    stream_handler = make_channel_proxy(conn, e.args[0], 'Media.StreamHandler')

    # We are now in members too
    e = q.expect_racy('dbus-signal', signal='MembersChanged',
             args=[u'', [self_handle], [], [], [], self_handle, 0])

    # we are now both in members
    members = media_chan.GetMembers()
    assert set(members) == set([self_handle, remote_handle]), members

    stream_handler.NewNativeCandidate("fake", jt2.get_remote_transports_dbus())
    stream_handler.Ready(jt2.get_audio_codecs_dbus())
    stream_handler.StreamState(cs.MEDIA_STREAM_STATE_CONNECTED)

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


if __name__ == '__main__':
    test_all_dialects(worker)

