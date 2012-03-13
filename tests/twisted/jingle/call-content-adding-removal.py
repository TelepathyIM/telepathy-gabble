"""
Test content adding and removal during the session. We start
session with only one stream, then add one more, then remove
the first one and lastly remove the second stream, which
closes the session.
"""

from functools import partial
from servicetest import call_async, assertEquals, assertLength, EventPattern
from gabbletest import make_result_iq, sync_stream
from jingletest2 import test_dialects, JingleProtocol015, JingleProtocol031
from call_helper import CallTest, run_call_test
import constants as cs
import dbus

class CallContentAddingRemovalTest(CallTest):

    # A/V
    initial_audio = True
    initial_video = True

    def pickup(self):
        peer_removes_final_content = self.params['peer-removes-final-content']

        # Remove video content before remote pick the call
        self.video_content.Remove()
        e = self.q.expect('dbus-signal', signal='ContentRemoved')
        assertEquals(e.args[1][0], self.self_handle)
        assertEquals(e.args[1][1], cs.CALL_STATE_CHANGE_REASON_USER_REQUESTED)
        assertEquals(e.args[1][2], '')

        self.initial_video = False
        self.video_content = None
        self.video_content_name = None
        self.video_stream = None

        # ...but before the peer notices, they accept the call.
        CallTest.pickup(self)

        # Gabble sends content-remove for the video stream...
        e = self.q.expect('stream-iq',
                predicate=self.jp.action_predicate('content-remove'))

        # Only now the remote end removes the video stream; if gabble mistakenly
        # marked it as accepted on session acceptance, it'll crash right about
        # now. If it's good, stream will be really removed, and
        # we can proceed.
        self.stream.send(make_result_iq(self.stream, e.stanza))
    
        # Actually, we *do* want video!
        content_path = self.chan.AddContent(
            "video1", cs.CALL_MEDIA_TYPE_VIDEO,
            cs.MEDIA_STREAM_DIRECTION_BIDIRECTIONAL,
            dbus_interface=cs.CHANNEL_TYPE_CALL);
        self.q.expect('dbus-signal', signal='ContentAdded')

        self.store_content(content_path, initial=False, incoming=False)

        md = self.jt2.get_call_video_md_dbus()
        self.check_and_accept_offer(self.video_content, md)

        candidates = self.jt2.get_call_remote_transports_dbus()
        self.video_stream.AddCandidates(candidates,
                dbus_interface=cs.CALL_STREAM_IFACE_MEDIA)

        e = self.q.expect('stream-iq',
                predicate=self.jp.action_predicate('content-add'))
        c = e.query.firstChildElement()
        assertEquals('initiator', c['creator'])

        endpoints = self.video_stream.Get(cs.CALL_STREAM_IFACE_MEDIA,
                "Endpoints", dbus_interface=dbus.PROPERTIES_IFACE)
        assertLength(1, endpoints)

        endpoint = self.bus.get_object(self.conn.bus_name, endpoints[0])
        self.enable_endpoint(endpoint)

        # Now, the call draws to a close.
        # We first remove the original stream
        self.audio_content.Remove()
        self.initial_audio = False
        self.audio_content = None
        self.audio_content_name = None
        self.audio_stream = None
    
        e = self.q.expect('stream-iq',
                predicate=self.jp.action_predicate('content-remove'))
        content_remove_ack = make_result_iq(self.stream, e.stanza)
    
        if peer_removes_final_content:
            # The peer removes the final countdo content. From a footnote (!) in
            # XEP 0166:
            #  If the content-remove results in zero content definitions for the
            #  session, the entity that receives the content-remove SHOULD send
            #  a session-terminate action to the other party (since a session
            #  with no content definitions is void).
            # So, Gabble should respond to the content-remove with a
            # session-terminate.
            node = self.jp.SetIq(self.jt2.peer, self.jt2.jid, [
                self.jp.Jingle(self.jt2.sid, self.jt2.peer, 'content-remove', [
                    self.jp.Content(c['name'], c['creator'], c['senders']) ]) ])
            self.stream.send(self.jp.xml(node))

        else:
            # The Telepathy client removes the second stream; Gabble should
            # terminate the session rather than sending a content-remove.
            self.video_content.Remove()
            self.initial_video = False
            self.video_content = None
            self.video_content_name = None
            self.video_stream = None

        st, ended = self.q.expect_many(
            EventPattern('stream-iq',
                predicate=self.jp.action_predicate('session-terminate')),
            # Gabble shouldn't wait for the peer to ack the terminate before
            # considering the call finished.
            EventPattern('dbus-signal', signal='CallStateChanged'))
        assertEquals(ended.args[0], cs.CALL_STATE_ENDED)
    
        # Only now does the peer ack the content-remove. This serves as a
        # regression test for contents outliving the session; if the content did
        # did't die properly, this crashed Gabble.
        self.stream.send(content_remove_ack)
        sync_stream(self.q, self.stream)
 
        # The peer can ack the terminate too, just for completeness.
        self.stream.send(make_result_iq(self.stream, st.stanza))

    def hangup(self):
        pass

if __name__ == '__main__':
    dialects = [JingleProtocol015, JingleProtocol031]
    test_dialects(
            partial(run_call_test, klass=CallContentAddingRemovalTest,
                incoming=False, params={'peer-removes-final-content': True}),
            dialects)
    test_dialects(
            partial(run_call_test, klass=CallContentAddingRemovalTest,
                incoming=False, params={'peer-removes-final-content': False}),
            dialects)
