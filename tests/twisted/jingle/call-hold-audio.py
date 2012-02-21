"""
Test the Hold API.
"""

import dbus
from dbus.exceptions import DBusException
from functools import partial
from servicetest import call_async, EventPattern, assertEquals, assertLength
from jingletest2 import test_all_dialects
from gabbletest import sync_stream
from call_helper import CallTest, run_call_test
import constants as cs

class CallHoldAudioTest(CallTest):

    def initiate(self):
        CallTest.initiate(self)

        q = self.q
        jp = self.jp
        cstream = self.audio_stream
        chan = self.chan

        recv_state = cstream.GetAll(cs.CALL_STREAM_IFACE_MEDIA,
                dbus_interface=dbus.PROPERTIES_IFACE)["ReceivingState"]
        send_state = cstream.GetAll(cs.CALL_STREAM_IFACE_MEDIA,
                dbus_interface=dbus.PROPERTIES_IFACE)["SendingState"]
        assertEquals (cs.CALL_STREAM_FLOW_STATE_STOPPED, recv_state)
        assertEquals (cs.CALL_STREAM_FLOW_STATE_STOPPED, send_state)

        # These are 0- (for old dialects) or 1- (for new dialects) element lists
        # that can be splatted into expect_many with *
        self.hold_event = jp.rtp_info_event_list("hold")
        self.unhold_event = jp.rtp_info_event_list("unhold")
    
        # Before we have accepted any streams, GetHoldState returns Unheld and
        # unhold is a no-op.
        assertEquals((cs.HS_UNHELD, cs.HSR_REQUESTED), chan.Hold.GetHoldState())
        chan.Hold.RequestHold(False)
    
        q.forbid_events(self.hold_event)
        q.forbid_events(self.unhold_event)
    
        assertEquals((cs.HS_UNHELD, cs.HSR_REQUESTED), chan.Hold.GetHoldState())
        chan.Hold.RequestHold(False)

        # Before we have any streams, RequestHold(True) should work; because
        # there are no streams, it should take effect at once. It certainly
        # should't send anything to the peer.
        q.forbid_events(self.hold_event)
        q.forbid_events(self.unhold_event)

        call_async(q, chan.Hold, 'RequestHold', True)
        q.expect('dbus-signal', signal='HoldStateChanged',
                args=[cs.HS_PENDING_HOLD, cs.HSR_REQUESTED])
        q.expect('dbus-signal', signal='HoldStateChanged',
                args=[cs.HS_HELD, cs.HSR_REQUESTED])
        assertEquals((cs.HS_HELD, cs.HSR_REQUESTED), chan.Hold.GetHoldState())
    
        # If we unhold, it should succeed immediately again, because there are
        # no resources to reclaim.
        call_async(q, chan.Hold, 'RequestHold', False)
        q.expect('dbus-signal', signal='HoldStateChanged',
                args=[cs.HS_PENDING_UNHOLD, cs.HSR_REQUESTED])
        q.expect('dbus-signal', signal='HoldStateChanged',
                args=[cs.HS_UNHELD, cs.HSR_REQUESTED])
        assertEquals((cs.HS_UNHELD, cs.HSR_REQUESTED), chan.Hold.GetHoldState())
    
        # Put the call back on hold ...
        call_async(q, chan.Hold, 'RequestHold', True)
        q.expect('dbus-signal', signal='HoldStateChanged',
                args=[cs.HS_PENDING_HOLD, cs.HSR_REQUESTED])
        q.expect('dbus-signal', signal='HoldStateChanged',
                args=[cs.HS_HELD, cs.HSR_REQUESTED])
        assertEquals((cs.HS_HELD, cs.HSR_REQUESTED), chan.Hold.GetHoldState())


    def connect(self):
        assertEquals((cs.HS_HELD, cs.HSR_REQUESTED),
                self.chan.Hold.GetHoldState())
        assertEquals((cs.HS_HELD, cs.HSR_REQUESTED),
                self.chan.Hold.GetHoldState())
        CallTest.connect(self, expect_after_si=self.hold_event)


    def accept_outgoing(self):
        # We are on hold, no states to complete here
        self.check_channel_state(cs.CALL_STATE_PENDING_INITIATOR)
        self.chan.Accept(dbus_interface=cs.CHANNEL_TYPE_CALL)
        self.check_channel_state(cs.CALL_STATE_INITIALISING)


    def pickup(self):
        CallTest.pickup(self, held=True)

        q = self.q
        stream = self.stream
        chan = self.chan
        cstream = self.audio_stream
    
        assertEquals((cs.HS_HELD, cs.HSR_REQUESTED), chan.Hold.GetHoldState())
    
        recv_state = cstream.Get(cs.CALL_STREAM_IFACE_MEDIA, "ReceivingState",
                dbus_interface=dbus.PROPERTIES_IFACE)
        assertEquals (cs.CALL_STREAM_FLOW_STATE_STOPPED, recv_state)
        send_state = cstream.Get(cs.CALL_STREAM_IFACE_MEDIA, "SendingState",
                dbus_interface=dbus.PROPERTIES_IFACE)
        assertEquals (cs.CALL_STREAM_FLOW_STATE_STOPPED, send_state)
    
    
        # Now we decide we do actually want to speak to them, and unhold.
        # Ensure that if Gabble sent the <unhold/> stanza too early it's already
        # arrived.
        sync_stream(q, stream)
        q.forbid_events(self.unhold_event)
    
        call_async(q, chan.Hold, 'RequestHold', False)
        q.expect_many(
            EventPattern('dbus-signal', signal='HoldStateChanged',
                args=[cs.HS_PENDING_UNHOLD, cs.HSR_REQUESTED]),
            EventPattern('dbus-signal', signal='ReceivingStateChanged',
                args = [cs.CALL_STREAM_FLOW_STATE_PENDING_START],
                interface = cs.CALL_STREAM_IFACE_MEDIA),
            EventPattern('dbus-signal', signal='SendingStateChanged',
                args = [cs.CALL_STREAM_FLOW_STATE_PENDING_START],
                interface = cs.CALL_STREAM_IFACE_MEDIA),
            EventPattern('dbus-return', method='RequestHold', value=()),
            )
    
        # Ensure that if Gabble sent the <unhold/> stanza too early it's already
        # arrived.
        sync_stream(q, stream)
        q.unforbid_events(self.unhold_event)
    
        cstream.CompleteReceivingStateChange(
                cs.CALL_STREAM_FLOW_STATE_STARTED,
                dbus_interface = cs.CALL_STREAM_IFACE_MEDIA)
        cstream.CompleteSendingStateChange(
                cs.CALL_STREAM_FLOW_STATE_STARTED,
                dbus_interface = cs.CALL_STREAM_IFACE_MEDIA)
    
        q.expect_many(
            EventPattern('dbus-signal', signal='HoldStateChanged',
                args=[cs.HS_UNHELD, cs.HSR_REQUESTED]),
            EventPattern('dbus-signal', signal='SendingStateChanged',
                args = [cs.CALL_STREAM_FLOW_STATE_STARTED],
                interface = cs.CALL_STREAM_IFACE_MEDIA),
            EventPattern('dbus-signal', signal='ReceivingStateChanged',
                args = [cs.CALL_STREAM_FLOW_STATE_STARTED],
                interface = cs.CALL_STREAM_IFACE_MEDIA),
            *self.unhold_event
            )
    
    
        # Hooray! Now let's check that Hold works properly once the call's fully
        # established.
    
        # ---- Test 1: GetHoldState returns unheld and unhold is a no-op ----
    
        hold_state = chan.Hold.GetHoldState()
        assert hold_state[0] == cs.HS_UNHELD, hold_state
        chan.Hold.RequestHold(False)
    
        # ---- Test 2: successful hold ----
    
        call_async(q, chan.Hold, 'RequestHold', True)
        q.expect_many(
            EventPattern('dbus-signal', signal='HoldStateChanged',
                args=[cs.HS_PENDING_HOLD, cs.HSR_REQUESTED]),
            EventPattern('dbus-signal', signal='SendingStateChanged',
                args = [cs.CALL_STREAM_FLOW_STATE_PENDING_STOP],
                interface = cs.CALL_STREAM_IFACE_MEDIA),
            EventPattern('dbus-signal', signal='ReceivingStateChanged',
                args = [cs.CALL_STREAM_FLOW_STATE_PENDING_STOP],
                interface = cs.CALL_STREAM_IFACE_MEDIA),
            EventPattern('dbus-return', method='RequestHold', value=()),
            *self.hold_event
            )
    
        cstream.CompleteReceivingStateChange(
                cs.CALL_STREAM_FLOW_STATE_STOPPED,
                dbus_interface = cs.CALL_STREAM_IFACE_MEDIA)
        cstream.CompleteSendingStateChange(
                cs.CALL_STREAM_FLOW_STATE_STOPPED,
                dbus_interface = cs.CALL_STREAM_IFACE_MEDIA)
        q.expect_many(
            EventPattern('dbus-signal', signal='HoldStateChanged',
                args=[cs.HS_HELD, cs.HSR_REQUESTED]),
            EventPattern('dbus-signal', signal='SendingStateChanged',
                args = [cs.CALL_STREAM_FLOW_STATE_STOPPED],
                interface = cs.CALL_STREAM_IFACE_MEDIA),
            EventPattern('dbus-signal', signal='ReceivingStateChanged',
                args = [cs.CALL_STREAM_FLOW_STATE_STOPPED],
                interface = cs.CALL_STREAM_IFACE_MEDIA),
            )
    
        # ---- Test 3: GetHoldState returns held and hold is a no-op ----
    
        hold_state = chan.Hold.GetHoldState()
        assert hold_state[0] == cs.HS_HELD, hold_state
        chan.Hold.RequestHold(True)
    
        # ---- Test 4: successful unhold ----
    
        q.forbid_events(self.unhold_event)
    
        call_async(q, chan.Hold, 'RequestHold', False)
        q.expect_many(
            EventPattern('dbus-signal', signal='HoldStateChanged',
                args=[cs.HS_PENDING_UNHOLD, cs.HSR_REQUESTED]),
            EventPattern('dbus-signal', signal='ReceivingStateChanged',
                args = [cs.CALL_STREAM_FLOW_STATE_PENDING_START],
                interface = cs.CALL_STREAM_IFACE_MEDIA),
            EventPattern('dbus-signal', signal='SendingStateChanged',
                args = [cs.CALL_STREAM_FLOW_STATE_PENDING_START],
                interface = cs.CALL_STREAM_IFACE_MEDIA),
            EventPattern('dbus-return', method='RequestHold', value=()),
            )
    
        # Ensure that if Gabble sent the <unhold/> stanza too early it's already
        # arrived.
        sync_stream(q, stream)
        q.unforbid_events(self.unhold_event)
    
        cstream.CompleteReceivingStateChange(
                cs.CALL_STREAM_FLOW_STATE_STARTED,
                dbus_interface = cs.CALL_STREAM_IFACE_MEDIA)
        cstream.CompleteSendingStateChange(
                cs.CALL_STREAM_FLOW_STATE_STARTED,
                dbus_interface = cs.CALL_STREAM_IFACE_MEDIA)
        q.expect_many(
            EventPattern('dbus-signal', signal='HoldStateChanged',
                args=[cs.HS_UNHELD, cs.HSR_REQUESTED]),
            EventPattern('dbus-signal', signal='SendingStateChanged',
                args = [cs.CALL_STREAM_FLOW_STATE_STARTED],
                interface = cs.CALL_STREAM_IFACE_MEDIA),
            EventPattern('dbus-signal', signal='ReceivingStateChanged',
                args = [cs.CALL_STREAM_FLOW_STATE_STARTED],
                interface = cs.CALL_STREAM_IFACE_MEDIA),
            *self.unhold_event
            )
    
        # ---- Test 5: GetHoldState returns False and unhold is a no-op ----
    
        hold_state = chan.Hold.GetHoldState()
        assert hold_state[0] == cs.HS_UNHELD, hold_state
        chan.Hold.RequestHold(False)
    
        # ---- Test 6: 3 parallel calls to hold ----
    
        hold_state = chan.Hold.GetHoldState()
        assert hold_state[0] == cs.HS_UNHELD, hold_state
    
        call_async(q, chan.Hold, 'RequestHold', True)
        call_async(q, chan.Hold, 'RequestHold', True)
        call_async(q, chan.Hold, 'RequestHold', True)
        q.expect_many(
            EventPattern('dbus-signal', signal='HoldStateChanged',
                args=[cs.HS_PENDING_HOLD, cs.HSR_REQUESTED]),
            EventPattern('dbus-signal', signal='SendingStateChanged',
                args = [cs.CALL_STREAM_FLOW_STATE_PENDING_STOP],
                interface = cs.CALL_STREAM_IFACE_MEDIA),
            EventPattern('dbus-signal', signal='ReceivingStateChanged',
                args = [cs.CALL_STREAM_FLOW_STATE_PENDING_STOP],
                interface = cs.CALL_STREAM_IFACE_MEDIA),
            EventPattern('dbus-return', method='RequestHold', value=()),
            *self.hold_event
            )
    
        cstream.CompleteReceivingStateChange(
                cs.CALL_STREAM_FLOW_STATE_STOPPED,
                dbus_interface = cs.CALL_STREAM_IFACE_MEDIA)
        cstream.CompleteSendingStateChange(
                cs.CALL_STREAM_FLOW_STATE_STOPPED,
                dbus_interface = cs.CALL_STREAM_IFACE_MEDIA)
        q.expect_many(
            EventPattern('dbus-signal', signal='SendingStateChanged',
                args = [cs.CALL_STREAM_FLOW_STATE_STOPPED],
                interface = cs.CALL_STREAM_IFACE_MEDIA),
            EventPattern('dbus-signal', signal='ReceivingStateChanged',
                args = [cs.CALL_STREAM_FLOW_STATE_STOPPED],
                interface = cs.CALL_STREAM_IFACE_MEDIA),
            EventPattern('dbus-signal', signal='HoldStateChanged',
                args=[cs.HS_HELD, cs.HSR_REQUESTED]),
            )
    
        # ---- Test 7: 3 parallel calls to unhold ----
    
        q.forbid_events(self.unhold_event)
    
        call_async(q, chan.Hold, 'RequestHold', False)
        call_async(q, chan.Hold, 'RequestHold', False)
        call_async(q, chan.Hold, 'RequestHold', False)
        q.expect_many(
            EventPattern('dbus-signal', signal='HoldStateChanged',
                args=[cs.HS_PENDING_UNHOLD, cs.HSR_REQUESTED]),
            EventPattern('dbus-signal', signal='SendingStateChanged',
                args = [cs.CALL_STREAM_FLOW_STATE_PENDING_START],
                interface = cs.CALL_STREAM_IFACE_MEDIA),
            EventPattern('dbus-signal', signal='ReceivingStateChanged',
                args = [cs.CALL_STREAM_FLOW_STATE_PENDING_START],
                interface = cs.CALL_STREAM_IFACE_MEDIA),
            EventPattern('dbus-return', method='RequestHold', value=()),
            )
    
        # Ensure that if Gabble sent the <unhold/> stanza too early it's already
        # arrived.
        sync_stream(q, stream)
        q.unforbid_events(self.unhold_event)
    
        cstream.CompleteReceivingStateChange(
                cs.CALL_STREAM_FLOW_STATE_STARTED,
                dbus_interface = cs.CALL_STREAM_IFACE_MEDIA)
        cstream.CompleteSendingStateChange(
                cs.CALL_STREAM_FLOW_STATE_STARTED,
                dbus_interface = cs.CALL_STREAM_IFACE_MEDIA)
        q.expect_many(
            EventPattern('dbus-signal', signal='HoldStateChanged',
                args=[cs.HS_UNHELD, cs.HSR_REQUESTED]),
            EventPattern('dbus-signal', signal='SendingStateChanged',
                args = [cs.CALL_STREAM_FLOW_STATE_STARTED],
                interface = cs.CALL_STREAM_IFACE_MEDIA),
            EventPattern('dbus-signal', signal='ReceivingStateChanged',
                args = [cs.CALL_STREAM_FLOW_STATE_STARTED],
                interface = cs.CALL_STREAM_IFACE_MEDIA),
            *self.unhold_event
            )
    
        # ---- Test 8: hold, then change our minds before s-e has responded ----
    
        hold_state = chan.Hold.GetHoldState()
        assert hold_state[0] == cs.HS_UNHELD, hold_state
    
        call_async(q, chan.Hold, 'RequestHold', True)
        q.expect_many(
            EventPattern('dbus-signal', signal='HoldStateChanged',
                args=[cs.HS_PENDING_HOLD, cs.HSR_REQUESTED]),
            EventPattern('dbus-signal', signal='SendingStateChanged',
                args = [cs.CALL_STREAM_FLOW_STATE_PENDING_STOP],
                interface = cs.CALL_STREAM_IFACE_MEDIA),
            EventPattern('dbus-signal', signal='ReceivingStateChanged',
                args = [cs.CALL_STREAM_FLOW_STATE_PENDING_STOP],
                interface = cs.CALL_STREAM_IFACE_MEDIA),
            *self.hold_event
            )
    
        q.forbid_events(self.unhold_event)
    
        call_async(q, chan.Hold, 'RequestHold', False)
        q.expect_many(
            EventPattern('dbus-signal', signal='HoldStateChanged',
                args=[cs.HS_PENDING_UNHOLD, cs.HSR_REQUESTED]),
            EventPattern('dbus-signal', signal='SendingStateChanged',
                args = [cs.CALL_STREAM_FLOW_STATE_PENDING_START],
                interface = cs.CALL_STREAM_IFACE_MEDIA),
            EventPattern('dbus-signal', signal='ReceivingStateChanged',
                args = [cs.CALL_STREAM_FLOW_STATE_PENDING_START],
                interface = cs.CALL_STREAM_IFACE_MEDIA),
            # Gabble shouldn't send <unhold/> here because s-e might have
            # already relinquished the audio hardware.
            )
    
        sync_stream(q, stream)
        q.unforbid_events(self.unhold_event)
    
        try:
            cstream.CompleteReceivingStateChange(
                    cs.CALL_STREAM_FLOW_STATE_STOPPED,
                    dbus_interface = cs.CALL_STREAM_IFACE_MEDIA)
        except dbus.DBusException, e:
            assertEquals (cs.INVALID_ARGUMENT, e.get_dbus_name ())
    
        try:
            cstream.CompleteSendingStateChange(
                    cs.CALL_STREAM_FLOW_STATE_STOPPED,
                    dbus_interface = cs.CALL_STREAM_IFACE_MEDIA)
        except dbus.DBusException, e:
            assertEquals (cs.INVALID_ARGUMENT, e.get_dbus_name ())
    
    
        cstream.CompleteReceivingStateChange(
                cs.CALL_STREAM_FLOW_STATE_STARTED,
                dbus_interface = cs.CALL_STREAM_IFACE_MEDIA)
        cstream.CompleteSendingStateChange(
                cs.CALL_STREAM_FLOW_STATE_STARTED,
                dbus_interface = cs.CALL_STREAM_IFACE_MEDIA)
        q.expect_many(
            EventPattern('dbus-signal', signal='HoldStateChanged',
                args=[cs.HS_UNHELD, cs.HSR_REQUESTED]),
            EventPattern('dbus-signal', signal='SendingStateChanged',
                args = [cs.CALL_STREAM_FLOW_STATE_STARTED],
                interface = cs.CALL_STREAM_IFACE_MEDIA),
            EventPattern('dbus-signal', signal='ReceivingStateChanged',
                args = [cs.CALL_STREAM_FLOW_STATE_STARTED],
                interface = cs.CALL_STREAM_IFACE_MEDIA),
            *self.unhold_event
            )
    
        hold_state = chan.Hold.GetHoldState()
        assert hold_state[0] == cs.HS_UNHELD, hold_state
    
        # ---- Test 9: unhold, then change our minds before s-e has responded --
    
        # Go to state "held" first
        call_async(q, chan.Hold, 'RequestHold', True)
        q.expect_many(
            EventPattern('dbus-signal', signal='HoldStateChanged',
                args=[cs.HS_PENDING_HOLD, cs.HSR_REQUESTED]),
            EventPattern('dbus-signal', signal='SendingStateChanged',
                args = [cs.CALL_STREAM_FLOW_STATE_PENDING_STOP],
                interface = cs.CALL_STREAM_IFACE_MEDIA),
            EventPattern('dbus-signal', signal='ReceivingStateChanged',
                args = [cs.CALL_STREAM_FLOW_STATE_PENDING_STOP],
                interface = cs.CALL_STREAM_IFACE_MEDIA),
            EventPattern('dbus-return', method='RequestHold', value=()),
            *self.hold_event
            )
    
        cstream.CompleteReceivingStateChange(
                cs.CALL_STREAM_FLOW_STATE_STOPPED,
                dbus_interface = cs.CALL_STREAM_IFACE_MEDIA)
        cstream.CompleteSendingStateChange(
                cs.CALL_STREAM_FLOW_STATE_STOPPED,
                dbus_interface = cs.CALL_STREAM_IFACE_MEDIA)
        q.expect_many(
            EventPattern('dbus-signal', signal='SendingStateChanged',
                args = [cs.CALL_STREAM_FLOW_STATE_STOPPED],
                interface = cs.CALL_STREAM_IFACE_MEDIA),
            EventPattern('dbus-signal', signal='ReceivingStateChanged',
                args = [cs.CALL_STREAM_FLOW_STATE_STOPPED],
                interface = cs.CALL_STREAM_IFACE_MEDIA),
            EventPattern('dbus-signal', signal='HoldStateChanged',
                args=[cs.HS_HELD, cs.HSR_REQUESTED]),
            )
    
        # Actually do test 9
    
        hold_state = chan.Hold.GetHoldState()
        assert hold_state[0] == cs.HS_HELD, hold_state
    
        # Check that Gabble doesn't send another <hold/>, or send <unhold/>
        # before we change our minds.
        q.forbid_events(self.unhold_event + self.hold_event)
    
        call_async(q, chan.Hold, 'RequestHold', False)
        q.expect_many(
            EventPattern('dbus-signal', signal='HoldStateChanged',
                args=[cs.HS_PENDING_UNHOLD, cs.HSR_REQUESTED]),
            EventPattern('dbus-signal', signal='SendingStateChanged',
                args = [cs.CALL_STREAM_FLOW_STATE_PENDING_START],
                interface = cs.CALL_STREAM_IFACE_MEDIA),
            EventPattern('dbus-signal', signal='ReceivingStateChanged',
                args = [cs.CALL_STREAM_FLOW_STATE_PENDING_START],
                interface = cs.CALL_STREAM_IFACE_MEDIA),
            )
    
        call_async(q, chan.Hold, 'RequestHold', True)
        q.expect_many(
            EventPattern('dbus-signal', signal='HoldStateChanged',
                args=[cs.HS_PENDING_HOLD, cs.HSR_REQUESTED]),
            EventPattern('dbus-signal', signal='SendingStateChanged',
                args = [cs.CALL_STREAM_FLOW_STATE_PENDING_STOP],
                interface = cs.CALL_STREAM_IFACE_MEDIA),
            EventPattern('dbus-signal', signal='ReceivingStateChanged',
                args = [cs.CALL_STREAM_FLOW_STATE_PENDING_STOP],
                interface = cs.CALL_STREAM_IFACE_MEDIA),
            )
    
    
        cstream.CompleteReceivingStateChange(
                cs.CALL_STREAM_FLOW_STATE_STOPPED,
                dbus_interface = cs.CALL_STREAM_IFACE_MEDIA)
        cstream.CompleteSendingStateChange(
                cs.CALL_STREAM_FLOW_STATE_STOPPED,
                dbus_interface = cs.CALL_STREAM_IFACE_MEDIA)
        q.expect_many(
            EventPattern('dbus-signal', signal='SendingStateChanged',
                args = [cs.CALL_STREAM_FLOW_STATE_STOPPED],
                interface = cs.CALL_STREAM_IFACE_MEDIA),
            EventPattern('dbus-signal', signal='ReceivingStateChanged',
                args = [cs.CALL_STREAM_FLOW_STATE_STOPPED],
                interface = cs.CALL_STREAM_IFACE_MEDIA),
            EventPattern('dbus-signal', signal='HoldStateChanged',
                args=[cs.HS_HELD, cs.HSR_REQUESTED]),
            )
    
    
        hold_state = chan.Hold.GetHoldState()
        assert hold_state[0] == cs.HS_HELD, hold_state
    
        sync_stream(q, stream)
        q.unforbid_events(self.unhold_event + self.hold_event)
    
        # ---- Test 10: attempting to unhold fails in the sending bit ----
    
        # Check that Gabble doesn't send another <hold/>, or send <unhold/> even
        # though unholding fails.
        q.forbid_events(self.unhold_event + self.hold_event)
    
        call_async(q, chan.Hold, 'RequestHold', False)
        q.expect_many(
            EventPattern('dbus-signal', signal='HoldStateChanged',
                args=[cs.HS_PENDING_UNHOLD, cs.HSR_REQUESTED]),
            EventPattern('dbus-signal', signal='SendingStateChanged',
                args = [cs.CALL_STREAM_FLOW_STATE_PENDING_START],
                interface = cs.CALL_STREAM_IFACE_MEDIA),
            EventPattern('dbus-signal', signal='ReceivingStateChanged',
                args = [cs.CALL_STREAM_FLOW_STATE_PENDING_START],
                interface = cs.CALL_STREAM_IFACE_MEDIA), 
            EventPattern('dbus-return', method='RequestHold', value=()),
            )
    
        cstream.ReportSendingFailure(0, "", "", 
                dbus_interface = cs.CALL_STREAM_IFACE_MEDIA)
    
        q.expect_many(
            EventPattern('dbus-signal', signal='HoldStateChanged',
                args=[cs.HS_PENDING_HOLD, cs.HSR_RESOURCE_NOT_AVAILABLE]),
            EventPattern('dbus-signal', signal='SendingStateChanged',
                args = [cs.CALL_STREAM_FLOW_STATE_STOPPED],
                interface = cs.CALL_STREAM_IFACE_MEDIA),
            EventPattern('dbus-signal', signal='ReceivingStateChanged',
                args = [cs.CALL_STREAM_FLOW_STATE_PENDING_STOP],
                interface = cs.CALL_STREAM_IFACE_MEDIA), 
            )
    
        cstream.CompleteReceivingStateChange(
                cs.CALL_STREAM_FLOW_STATE_STOPPED,
                dbus_interface = cs.CALL_STREAM_IFACE_MEDIA)
        q.expect_many(
            EventPattern('dbus-signal', signal='HoldStateChanged',
                args=[cs.HS_HELD, cs.HSR_RESOURCE_NOT_AVAILABLE]),
            EventPattern('dbus-signal', signal='ReceivingStateChanged',
                args = [cs.CALL_STREAM_FLOW_STATE_STOPPED],
                interface = cs.CALL_STREAM_IFACE_MEDIA),
            )
    
        # ---- Test 11: attempting to unhold fails in the receiving bit ----
    
        
        call_async(q, chan.Hold, 'RequestHold', False)
        q.expect_many(
            EventPattern('dbus-signal', signal='HoldStateChanged',
                args=[cs.HS_PENDING_UNHOLD, cs.HSR_REQUESTED]),
            EventPattern('dbus-signal', signal='SendingStateChanged',
                args = [cs.CALL_STREAM_FLOW_STATE_PENDING_START],
                interface = cs.CALL_STREAM_IFACE_MEDIA),
            EventPattern('dbus-signal', signal='ReceivingStateChanged',
                args = [cs.CALL_STREAM_FLOW_STATE_PENDING_START],
                interface = cs.CALL_STREAM_IFACE_MEDIA), 
            EventPattern('dbus-return', method='RequestHold', value=()),
            )
    
        cstream.ReportReceivingFailure(0, "", "",
                dbus_interface = cs.CALL_STREAM_IFACE_MEDIA)
    
        q.expect_many(
            EventPattern('dbus-signal', signal='HoldStateChanged',
                args=[cs.HS_PENDING_HOLD, cs.HSR_RESOURCE_NOT_AVAILABLE]),
            EventPattern('dbus-signal', signal='SendingStateChanged',
                args = [cs.CALL_STREAM_FLOW_STATE_PENDING_STOP],
                interface = cs.CALL_STREAM_IFACE_MEDIA),
            EventPattern('dbus-signal', signal='ReceivingStateChanged',
                args = [cs.CALL_STREAM_FLOW_STATE_STOPPED],
                interface = cs.CALL_STREAM_IFACE_MEDIA), 
            )

        cstream.CompleteSendingStateChange(
                cs.CALL_STREAM_FLOW_STATE_STOPPED,
                dbus_interface = cs.CALL_STREAM_IFACE_MEDIA)
        q.expect_many(
            EventPattern('dbus-signal', signal='HoldStateChanged',
                args=[cs.HS_HELD, cs.HSR_RESOURCE_NOT_AVAILABLE]),
            EventPattern('dbus-signal', signal='SendingStateChanged',
                args = [cs.CALL_STREAM_FLOW_STATE_STOPPED],
                interface = cs.CALL_STREAM_IFACE_MEDIA),
            )
    
        sync_stream(q, stream)
        q.unforbid_events(self.unhold_event + self.hold_event)
    
        # ---- Test 12: when we successfully unhold, the peer gets <unhold/> ---
    
        q.forbid_events(self.unhold_event)
    
        call_async(q, chan.Hold, 'RequestHold', False)
        q.expect_many(
            EventPattern('dbus-signal', signal='HoldStateChanged',
                args=[cs.HS_PENDING_UNHOLD, cs.HSR_REQUESTED]),
            EventPattern('dbus-signal', signal='SendingStateChanged',
                args = [cs.CALL_STREAM_FLOW_STATE_PENDING_START],
                interface = cs.CALL_STREAM_IFACE_MEDIA),
            EventPattern('dbus-signal', signal='ReceivingStateChanged',
                args = [cs.CALL_STREAM_FLOW_STATE_PENDING_START],
                interface = cs.CALL_STREAM_IFACE_MEDIA),
            EventPattern('dbus-return', method='RequestHold', value=()),
            )
    
        # Ensure that if Gabble sent the <unhold/> stanza too early it's already
        # arrived.
        sync_stream(q, stream)
        q.unforbid_events(self.unhold_event)
    
        cstream.CompleteReceivingStateChange(
                cs.CALL_STREAM_FLOW_STATE_STARTED,
                dbus_interface = cs.CALL_STREAM_IFACE_MEDIA)
        cstream.CompleteSendingStateChange(
                cs.CALL_STREAM_FLOW_STATE_STARTED,
                dbus_interface = cs.CALL_STREAM_IFACE_MEDIA)
        q.expect_many(
            EventPattern('dbus-signal', signal='HoldStateChanged',
                args=[cs.HS_UNHELD, cs.HSR_REQUESTED]),
            EventPattern('dbus-signal', signal='SendingStateChanged',
                args = [cs.CALL_STREAM_FLOW_STATE_STARTED],
                interface = cs.CALL_STREAM_IFACE_MEDIA),
            EventPattern('dbus-signal', signal='ReceivingStateChanged',
                args = [cs.CALL_STREAM_FLOW_STATE_STARTED],
                interface = cs.CALL_STREAM_IFACE_MEDIA),
            *self.unhold_event
            )


if __name__ == '__main__':
    test_all_dialects(partial(run_call_test, klass=CallHoldAudioTest,
                incoming=False))
