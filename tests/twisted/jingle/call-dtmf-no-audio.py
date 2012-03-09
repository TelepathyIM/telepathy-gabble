"""
Test DMTF events in a Call channel
"""

from functools import partial
from servicetest import call_async
from jingletest2 import test_all_dialects
from call_helper import CallTest, run_call_test

from config import VOIP_ENABLED

if not VOIP_ENABLED:
    print "NOTE: built with --disable-voip"
    raise SystemExit(77)

class CallDtmfNoAudioTest(CallTest):

    # We want vieo only channel
    initial_audio = False
    initial_video = True

    def pickup(self):
        CallTest.pickup(self)

        # Check the DTMF method does not exist
        call_async(self.q, self.video_content.DTMF, 'StartTone', 3)
        self.q.expect('dbus-error', method='StartTone')

if __name__ == '__main__':
    test_all_dialects(
            partial(run_call_test, klass=CallDtmfNoAudioTest, incoming=False))
