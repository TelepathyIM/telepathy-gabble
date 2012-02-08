"""
Test DMTF events in a Call channel
"""

import dbus
from dbus.exceptions import DBusException
from functools import partial
from servicetest import call_async, EventPattern, assertEquals
from jingletest2 import test_all_dialects
from call_helper import CallTest, run_call_test
import constants as cs

class CallDtmfTest(CallTest):

    def test_dtmf(self):
        content = self.audio_content
        q = self.q

        # The Stream_ID is specified to be ignored; we use 666 here.
        assertEquals(False, content.Get(cs.CALL_CONTENT_IFACE_DTMF,
                    'CurrentlySendingTones',
                    dbus_interface=dbus.PROPERTIES_IFACE));
    
        call_async(q, content.DTMF, 'StartTone', 3)
        q.expect_many(
                EventPattern('dbus-signal', signal='SendingTones', args=['3']),
                EventPattern('dbus-signal', signal='DTMFChangeRequested',
                    args = [cs.CALL_SENDING_STATE_PENDING_SEND, 3]),
                EventPattern('dbus-return', method='StartTone'),
                )
    
        assertEquals(True, content.Get(cs.CALL_CONTENT_IFACE_DTMF,
                    'CurrentlySendingTones',
                    dbus_interface=dbus.PROPERTIES_IFACE));
    
        content.Media.AcknowledgeDTMFChange(3, cs.CALL_SENDING_STATE_SENDING)
    
        call_async(q, content.DTMF, 'StopTone')
        q.expect_many(
                EventPattern('dbus-signal', signal='DTMFChangeRequested',
                    args = [cs.CALL_SENDING_STATE_PENDING_STOP_SENDING, 3]),
                EventPattern('dbus-return', method='StopTone'),
                )
    
        call_async(q, content.Media, 'AcknowledgeDTMFChange', 3,
                cs.CALL_SENDING_STATE_NONE)
        q.expect_many(
            EventPattern('dbus-signal', signal='StoppedTones', args=[True]),
            EventPattern('dbus-return', method='AcknowledgeDTMFChange'),
            )
    
        assertEquals(False, content.Get(cs.CALL_CONTENT_IFACE_DTMF,
                    'CurrentlySendingTones',
                    dbus_interface=dbus.PROPERTIES_IFACE));
    
        call_async(q, content.DTMF, 'MultipleTones', '123')
        q.expect_many(
            EventPattern('dbus-signal', signal='SendingTones', args=['123']),
            EventPattern('dbus-return', method='MultipleTones'),
            EventPattern('dbus-signal', signal='DTMFChangeRequested',
                args = [cs.CALL_SENDING_STATE_PENDING_SEND, 1]),
            )
        content.Media.AcknowledgeDTMFChange(1, cs.CALL_SENDING_STATE_SENDING)
    
        q.expect('dbus-signal', signal='DTMFChangeRequested',
                args = [cs.CALL_SENDING_STATE_PENDING_STOP_SENDING, 1])
        content.Media.AcknowledgeDTMFChange(1, cs.CALL_SENDING_STATE_NONE)
    
        q.expect_many(
            EventPattern('dbus-signal', signal='SendingTones', args=['23']),
            EventPattern('dbus-signal', signal='DTMFChangeRequested',
                args = [cs.CALL_SENDING_STATE_PENDING_SEND, 2]),
            )
        content.Media.AcknowledgeDTMFChange(2, cs.CALL_SENDING_STATE_SENDING)
        q.expect('dbus-signal', signal='DTMFChangeRequested',
                args = [cs.CALL_SENDING_STATE_PENDING_STOP_SENDING, 2])
        content.Media.AcknowledgeDTMFChange(2, cs.CALL_SENDING_STATE_NONE)
    
        q.expect_many(
            EventPattern('dbus-signal', signal='SendingTones', args=['3']),
            EventPattern('dbus-signal', signal='DTMFChangeRequested',
                args = [cs.CALL_SENDING_STATE_PENDING_SEND, 3]),
            )
        content.Media.AcknowledgeDTMFChange(3, cs.CALL_SENDING_STATE_SENDING)
        q.expect('dbus-signal', signal='DTMFChangeRequested',
                args = [cs.CALL_SENDING_STATE_PENDING_STOP_SENDING, 3])
        content.Media.AcknowledgeDTMFChange(3, cs.CALL_SENDING_STATE_NONE)
    
        q.expect_many(
            EventPattern('dbus-signal', signal='StoppedTones', args=[False])
            )
    
        call_async(q, content.DTMF, 'MultipleTones',
                '1,1' * 100)
        q.expect_many(
            EventPattern('dbus-signal', signal='SendingTones'),
            EventPattern('dbus-signal', signal='DTMFChangeRequested',
                args = [cs.CALL_SENDING_STATE_PENDING_SEND, 1]),
            EventPattern('dbus-return', method='MultipleTones'),
            )
        call_async(q, content.DTMF, 'MultipleTones', '9')
        q.expect('dbus-error', method='MultipleTones',
                name=cs.SERVICE_BUSY)
        call_async(q, content.DTMF, 'StartTone', 9)
        q.expect('dbus-error', method='StartTone', name=cs.SERVICE_BUSY)
    
        call_async(q, content.DTMF, 'StopTone')
        q.expect_many(
            EventPattern('dbus-signal', signal='DTMFChangeRequested',
                args = [cs.CALL_SENDING_STATE_PENDING_STOP_SENDING, 1]),
            EventPattern('dbus-return', method='StopTone'),
            )
        call_async(q, content.Media, 'AcknowledgeDTMFChange',
                1, cs.CALL_SENDING_STATE_NONE)
        q.expect_many(
            EventPattern('dbus-signal', signal='StoppedTones', args=[True]),
            EventPattern('dbus-return', method='AcknowledgeDTMFChange'),
            )
    
        call_async(q, content.DTMF, 'MultipleTones', '1w2')
        q.expect_many(
            EventPattern('dbus-signal', signal='SendingTones', args=['1w2']),
            EventPattern('dbus-signal', signal='DTMFChangeRequested',
                args = [cs.CALL_SENDING_STATE_PENDING_SEND, 1]),
            EventPattern('dbus-return', method='MultipleTones'),
            )
    
        content.Media.AcknowledgeDTMFChange(1, cs.CALL_SENDING_STATE_SENDING)
    
        q.expect('dbus-signal', signal='DTMFChangeRequested',
                args = [cs.CALL_SENDING_STATE_PENDING_STOP_SENDING, 1])
    
        call_async(q, content.Media, 'AcknowledgeDTMFChange', 1,
                cs.CALL_SENDING_STATE_NONE)
        q.expect_many(
            EventPattern('dbus-signal', signal='TonesDeferred', args=['2']),
            EventPattern('dbus-signal', signal='StoppedTones', args=[False]),
            EventPattern('dbus-return', method='AcknowledgeDTMFChange'),
            )
        assertEquals('2', content.Get(cs.CALL_CONTENT_IFACE_DTMF,
                    'DeferredTones', dbus_interface=dbus.PROPERTIES_IFACE));
    
        call_async(q, content.DTMF, 'StartTone', 7)
        q.expect_many(
            EventPattern('dbus-signal', signal='SendingTones', args=['7']),
            EventPattern('dbus-signal', signal='DTMFChangeRequested',
                args = [cs.CALL_SENDING_STATE_PENDING_SEND, 7]),
            EventPattern('dbus-return', method='StartTone'),
            )
    
        # Checked that DeferredTones is properly reset
        assertEquals('', content.Get(cs.CALL_CONTENT_IFACE_DTMF,
                    'DeferredTones', dbus_interface=dbus.PROPERTIES_IFACE));
        

    def pickup(self):
        CallTest.pickup(self)
        self.test_dtmf()

if __name__ == '__main__':
    test_all_dialects(
            partial(run_call_test, klass=CallDtmfTest, incoming=False))
