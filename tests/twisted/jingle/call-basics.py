"""
Test basic outgoing and incoming call handling
"""

import config

if not config.CHANNEL_TYPE_CALL_ENABLED:
    print "NOTE: built with --disable-channel-type-call"
    raise SystemExit(77)

import dbus
from dbus.exceptions import DBusException

from functools import partial
from servicetest import EventPattern, assertEquals, assertContains, assertLength
from call_helper import CallTest, run_call_test
from jingletest2 import test_all_dialects
import constants as cs
import ns

from config import VOIP_ENABLED

if not VOIP_ENABLED:
    print "NOTE: built with --disable-voip"
    raise SystemExit(77)

class CallBasicsTest(CallTest):

    def test_connect_disconnect_endpoint(self):
        endpoints = self.audio_stream.Get(cs.CALL_STREAM_IFACE_MEDIA,
                "Endpoints", dbus_interface=dbus.PROPERTIES_IFACE)
        assertLength(1, endpoints)

        endpoint = self.bus.get_object(self.conn.bus_name, endpoints[0])

        # Lets try disconnecting one
        endpoint.SetEndpointState(1, cs.CALL_STREAM_ENDPOINT_STATE_CONNECTING,
                dbus_interface=cs.CALL_STREAM_ENDPOINT)
        ret = self.q.expect_many(
                EventPattern('dbus-signal', signal='EndpointStateChanged'),
                EventPattern('dbus-signal', signal='CallStateChanged'))
        assertEquals(1, ret[0].args[0])
        assertEquals(cs.CALL_STREAM_ENDPOINT_STATE_CONNECTING, ret[0].args[1])
        assertEquals(cs.CALL_STATE_ACCEPTED, ret[1].args[0])

        state = endpoint.Get(cs.CALL_STREAM_ENDPOINT,
                "EndpointState",  dbus_interface=dbus.PROPERTIES_IFACE)
        assertEquals(cs.CALL_STREAM_ENDPOINT_STATE_CONNECTING, state[1])

        # And reconnecting it
        endpoint.SetEndpointState(1,
                cs.CALL_STREAM_ENDPOINT_STATE_FULLY_CONNECTED,
                dbus_interface=cs.CALL_STREAM_ENDPOINT)

        ret = self.q.expect_many(
                EventPattern('dbus-signal', signal='EndpointStateChanged'),
                EventPattern('dbus-signal', signal='CallStateChanged'))
        assertEquals(1,ret[0].args[0])
        assertEquals(cs.CALL_STREAM_ENDPOINT_STATE_FULLY_CONNECTED,
                ret[0].args[1])
        assertEquals(cs.CALL_STATE_ACTIVE, ret[1].args[0])

        state = endpoint.Get(cs.CALL_STREAM_ENDPOINT,
                "EndpointState",  dbus_interface=dbus.PROPERTIES_IFACE)
        assertEquals(cs.CALL_STREAM_ENDPOINT_STATE_FULLY_CONNECTED, state[1])

        # All Direction should still be both now
        stream_props = self.audio_stream.GetAll(cs.CALL_STREAM,
                dbus_interface = dbus.PROPERTIES_IFACE)
        assertEquals({self.peer_handle: cs.CALL_SENDING_STATE_SENDING},
                stream_props["RemoteMembers"])
        assertEquals(cs.CALL_SENDING_STATE_SENDING,
                stream_props["LocalSendingState"])


    def test_content_addition(self):
        path = self.chan.AddContent("Webcam", cs.CALL_MEDIA_TYPE_VIDEO,
                                    cs.MEDIA_STREAM_DIRECTION_BIDIRECTIONAL,
                                    dbus_interface=cs.CHANNEL_TYPE_CALL)
        content = self.bus.get_object(self.conn.bus_name, path)
        content_properties = content.GetAll(cs.CALL_CONTENT,
                dbus_interface=dbus.PROPERTIES_IFACE)

        assertEquals(cs.CALL_DISPOSITION_NONE,
                content_properties["Disposition"])
        #assertEquals(self_handle, content_properties["Creator"])
        assertContains("Webcam", content_properties["Name"])

        md = self.jt2.get_call_video_md_dbus()
        self.check_and_accept_offer(content, md)

        cstream = self.bus.get_object(self.conn.bus_name,
                content_properties["Streams"][0])
        candidates = self.jt2.get_call_remote_transports_dbus()
        cstream.AddCandidates(candidates,
                dbus_interface=cs.CALL_STREAM_IFACE_MEDIA)

        self.q.expect('stream-iq',
                predicate=self.jp.action_predicate('content-add'))

        content.Remove(dbus_interface=cs.CALL_CONTENT)
        self.q.expect('stream-iq',
                predicate=self.jp.action_predicate('content-remove'))


    def pickup(self):
        jt2 = self.jt2
        jp = self.jp
        q = self.q
        cstream = self.audio_stream
        remote_handle = self.peer_handle
        can_change_direction = self.can_change_direction
        incoming = self.incoming

        # We pickup the call as we need active state to run this test
        CallTest.pickup(self)

        self.test_connect_disconnect_endpoint()

        # FIXME: This should eventually be break down in smaller test methods

        # Turn sending off and on again

        # but first, let's try direction changes requested by the other side

        if can_change_direction:
            content_name = jt2.audio_names[0]
            if incoming:
                jt2.content_modify(content_name, "initiator", "initiator")
            else:
                jt2.content_modify(content_name, "initiator", "responder")
            o = q.expect('dbus-signal', signal='LocalSendingStateChanged')
            assertEquals(cs.CALL_SENDING_STATE_PENDING_STOP_SENDING, o.args[0])

            cstream.SetSending(False,
                    dbus_interface = cs.CALL_STREAM)
            o = q.expect('dbus-signal', signal='SendingStateChanged')
            assertEquals(cs.CALL_STREAM_FLOW_STATE_PENDING_STOP, o.args[0])

            cstream.CompleteSendingStateChange(
                    cs.CALL_STREAM_FLOW_STATE_STOPPED,
                    dbus_interface = cs.CALL_STREAM_IFACE_MEDIA)
            o = q.expect('dbus-signal', signal='SendingStateChanged',
                    interface = cs.CALL_STREAM_IFACE_MEDIA)
            assertEquals(cs.CALL_STREAM_FLOW_STATE_STOPPED, o.args[0])

            jt2.content_modify(content_name, "initiator", "both")
            o = q.expect('dbus-signal', signal='LocalSendingStateChanged')
            assertEquals(cs.CALL_SENDING_STATE_PENDING_SEND, o.args[0])

            cstream.SetSending(True,
                    dbus_interface = cs.CALL_STREAM)

            ret = q.expect_many(
                    EventPattern('dbus-signal', signal='SendingStateChanged'),
                    EventPattern('dbus-signal',
                        signal='LocalSendingStateChanged'))
            assertEquals(cs.CALL_STREAM_FLOW_STATE_PENDING_START,
                    ret[0].args[0])
            assertEquals(cs.CALL_SENDING_STATE_SENDING, ret[1].args[0])

            cstream.CompleteSendingStateChange(
                    cs.CALL_STREAM_FLOW_STATE_STARTED,
                    dbus_interface = cs.CALL_STREAM_IFACE_MEDIA)
            o = q.expect('dbus-signal', signal='SendingStateChanged',
                    interface = cs.CALL_STREAM_IFACE_MEDIA)
            assertEquals(cs.CALL_STREAM_FLOW_STATE_STARTED, o.args[0])

            stream_props = cstream.GetAll(cs.CALL_STREAM,
                    dbus_interface = dbus.PROPERTIES_IFACE)
            assertEquals({remote_handle: cs.CALL_SENDING_STATE_SENDING},
                    stream_props["RemoteMembers"])
            assertEquals(cs.CALL_SENDING_STATE_SENDING,
                    stream_props["LocalSendingState"])

        cstream.SetSending(False,
                dbus_interface = cs.CALL_STREAM)
        ret = q.expect_many(
                EventPattern('dbus-signal', signal='SendingStateChanged'),
                EventPattern('dbus-signal', signal='LocalSendingStateChanged'))
        assertEquals(cs.CALL_STREAM_FLOW_STATE_PENDING_STOP, ret[0].args[0])
        assertEquals(cs.CALL_SENDING_STATE_NONE, ret[1].args[0])

        cstream.CompleteSendingStateChange(
                cs.CALL_STREAM_FLOW_STATE_STOPPED,
                dbus_interface = cs.CALL_STREAM_IFACE_MEDIA)
        o = q.expect('dbus-signal', signal='SendingStateChanged',
                interface = cs.CALL_STREAM_IFACE_MEDIA)
        assertEquals(cs.CALL_STREAM_FLOW_STATE_STOPPED, o.args[0])

        stream_props = cstream.GetAll(cs.CALL_STREAM,
                dbus_interface = dbus.PROPERTIES_IFACE)
        assertEquals({remote_handle: cs.CALL_SENDING_STATE_SENDING},
                stream_props["RemoteMembers"])
        assertEquals(cs.CALL_SENDING_STATE_NONE,
                stream_props["LocalSendingState"])

        # If possible, test the other side asking us to start then stop sending

        if can_change_direction:
            jt2.content_modify(content_name, "initiator", "both")
            o = q.expect('dbus-signal', signal='LocalSendingStateChanged')
            assertEquals(cs.CALL_SENDING_STATE_PENDING_SEND, o.args[0])

            if incoming:
                jt2.content_modify(content_name, "initiator", "initiator")
            else:
                jt2.content_modify(content_name, "initiator", "responder")
            o = q.expect('dbus-signal', signal='LocalSendingStateChanged')
            assertEquals(cs.CALL_SENDING_STATE_NONE, o.args[0])

            jt2.content_modify(content_name, "initiator", "both")
            o = q.expect('dbus-signal', signal='LocalSendingStateChanged')
            assertEquals(cs.CALL_SENDING_STATE_PENDING_SEND, o.args[0])


        cstream.SetSending(True, dbus_interface = cs.CALL_STREAM)

        ret = q.expect_many(
                EventPattern('dbus-signal', signal='SendingStateChanged'),
                EventPattern('dbus-signal', signal='LocalSendingStateChanged'))
        assertEquals(cs.CALL_STREAM_FLOW_STATE_PENDING_START, ret[0].args[0])
        assertEquals(cs.CALL_SENDING_STATE_SENDING, ret[1].args[0])

        cstream.CompleteSendingStateChange(
                cs.CALL_STREAM_FLOW_STATE_STARTED,
                dbus_interface = cs.CALL_STREAM_IFACE_MEDIA)

        stream_props = cstream.GetAll(cs.CALL_STREAM,
                dbus_interface = dbus.PROPERTIES_IFACE)
        assertEquals({remote_handle: cs.CALL_SENDING_STATE_SENDING},
                stream_props["RemoteMembers"])
        assertEquals(cs.CALL_SENDING_STATE_SENDING,
                stream_props["LocalSendingState"])

        # Turn receiving off and on again

        try:
            cstream.RequestReceiving(remote_handle, False,
                    dbus_interface = cs.CALL_STREAM)
            assert can_change_direction
        except dbus.DBusException, e:
            assertEquals(cs.NOT_CAPABLE, e.get_dbus_name())
            assert not can_change_direction

        if can_change_direction:
            ret = q.expect_many(
                    EventPattern('dbus-signal', signal='ReceivingStateChanged'),
                    EventPattern('dbus-signal', signal='RemoteMembersChanged'),
                    EventPattern('stream-iq',
                        predicate=jp.action_predicate('content-modify')))
            assertEquals(cs.CALL_STREAM_FLOW_STATE_PENDING_STOP, ret[0].args[0])
            assert ret[1].args[0].has_key(remote_handle)

            cstream.CompleteReceivingStateChange(
                    cs.CALL_STREAM_FLOW_STATE_STOPPED,
                    dbus_interface = cs.CALL_STREAM_IFACE_MEDIA)
            o = q.expect('dbus-signal', signal='ReceivingStateChanged',
                    interface = cs.CALL_STREAM_IFACE_MEDIA)
            assertEquals(cs.CALL_STREAM_FLOW_STATE_STOPPED, o.args[0])

            stream_props = cstream.GetAll(cs.CALL_STREAM,
                    dbus_interface = dbus.PROPERTIES_IFACE)
            assertEquals({remote_handle: cs.CALL_SENDING_STATE_NONE},
                    stream_props["RemoteMembers"])
            assertEquals(cs.CALL_SENDING_STATE_SENDING,
                    stream_props["LocalSendingState"])
        else:
            stream_props = cstream.GetAll(cs.CALL_STREAM,
                    dbus_interface = dbus.PROPERTIES_IFACE)
            assertEquals({remote_handle: cs.CALL_SENDING_STATE_SENDING},
                    stream_props["RemoteMembers"])
            assertEquals(cs.CALL_SENDING_STATE_SENDING,
                    stream_props["LocalSendingState"])

        try:
            cstream.RequestReceiving(remote_handle, True,
                    dbus_interface = cs.CALL_STREAM)
            assert can_change_direction
        except dbus.DBusException, e:
            assertEquals(cs.NOT_CAPABLE, e.get_dbus_name())
            assert not can_change_direction

        if can_change_direction:
            ret = q.expect_many(
                    EventPattern('dbus-signal', signal='ReceivingStateChanged'),
                    EventPattern('dbus-signal', signal='RemoteMembersChanged'),
                    EventPattern('stream-iq',
                        predicate=jp.action_predicate('content-modify')))
            assertEquals(cs.CALL_STREAM_FLOW_STATE_PENDING_START,
                    ret[0].args[0])
            assert ret[1].args[0].has_key(remote_handle)
            assertEquals(cs.CALL_SENDING_STATE_PENDING_SEND,
                    ret[1].args[0][remote_handle])

            cstream.CompleteReceivingStateChange(
                    cs.CALL_STREAM_FLOW_STATE_STARTED,
                    dbus_interface = cs.CALL_STREAM_IFACE_MEDIA)
            ret = q.expect_many(
                    EventPattern('dbus-signal', signal='ReceivingStateChanged'),
                    EventPattern('dbus-signal', signal='RemoteMembersChanged'))
            assertEquals(cs.CALL_STREAM_FLOW_STATE_STARTED, ret[0].args[0])
            assert ret[1].args[0].has_key(remote_handle)
            assertEquals(cs.CALL_SENDING_STATE_SENDING,
                    ret[1].args[0][remote_handle])

        stream_props = cstream.GetAll(cs.CALL_STREAM,
                dbus_interface = dbus.PROPERTIES_IFACE)
        assertEquals({remote_handle: cs.CALL_SENDING_STATE_SENDING},
                stream_props["RemoteMembers"])
        assertEquals(cs.CALL_SENDING_STATE_SENDING,
                stream_props["LocalSendingState"])

        if can_change_direction:
            if incoming:
                jt2.content_modify(content_name, "initiator", "responder")
            else:
                jt2.content_modify(content_name, "initiator", "initiator")

            ret = q.expect_many(
                    EventPattern('dbus-signal', signal='ReceivingStateChanged'),
                    EventPattern('dbus-signal', signal='RemoteMembersChanged'))
            assertEquals(cs.CALL_STREAM_FLOW_STATE_PENDING_STOP, ret[0].args[0])
            assert ret[1].args[0].has_key(remote_handle)
            assertEquals(cs.CALL_SENDING_STATE_NONE,
                    ret[1].args[0][remote_handle])

            cstream.CompleteReceivingStateChange(
                    cs.CALL_STREAM_FLOW_STATE_STOPPED,
                    dbus_interface = cs.CALL_STREAM_IFACE_MEDIA)
            o = q.expect('dbus-signal', signal='ReceivingStateChanged')
            assertEquals(cs.CALL_STREAM_FLOW_STATE_STOPPED, o.args[0])

            jt2.content_modify(content_name, "initiator", "both")
            ret = q.expect_many(
                    EventPattern('dbus-signal', signal='ReceivingStateChanged'),
                    EventPattern('dbus-signal', signal='RemoteMembersChanged'))
            assertEquals(cs.CALL_STREAM_FLOW_STATE_PENDING_START,
                    ret[0].args[0])
            assert ret[1].args[0].has_key(remote_handle)
            assertEquals(cs.CALL_SENDING_STATE_SENDING,
                    ret[1].args[0][remote_handle])

            cstream.CompleteReceivingStateChange(
                    cs.CALL_STREAM_FLOW_STATE_STARTED,
                    dbus_interface = cs.CALL_STREAM_IFACE_MEDIA)
            o = q.expect('dbus-signal', signal='ReceivingStateChanged')
            assertEquals(cs.CALL_STREAM_FLOW_STATE_STARTED, o.args[0])

        try:
            self.test_content_addition()
        except DBusException, e:
            assertEquals(cs.NOT_AVAILABLE, e.get_dbus_name())
            assert not jp.can_do_video()


if __name__ == '__main__':
    test_all_dialects(
            partial(run_call_test, klass=CallBasicsTest, incoming=True))
    test_all_dialects(
            partial(run_call_test, klass=CallBasicsTest, incoming=False))
