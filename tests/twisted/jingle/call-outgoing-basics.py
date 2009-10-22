"""
Test basic outgoing call handling, using CreateChannel
"""

import dbus
from twisted.words.xish import xpath

from gabbletest import exec_test
from servicetest import (
    make_channel_proxy, wrap_channel,
    EventPattern, call_async,
    assertEquals, assertContains, assertLength,
    )
import constants as cs
from jingletest2 import JingleTest2, test_all_dialects

def run_test(jp, q, bus, conn, stream):
    jt2 = JingleTest2(jp, conn, q, stream, 'test@localhost', 'foo@bar.com/Foo')
    jt2.prepare()

    self_handle = conn.GetSelfHandle()
    remote_handle = conn.RequestHandles(1, ["foo@bar.com/Foo"])[0]

    # Ensure a channel that doesn't exist yet.
    call_async(q, conn.Requests, 'CreateChannel',
            { cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_CALL,
              cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
              cs.TARGET_HANDLE: remote_handle,
              cs.CALL_INITIAL_AUDIO: True,
            })

    ret, signal = q.expect_many(
        EventPattern('dbus-return', method='CreateChannel'),
        EventPattern('dbus-signal', signal='NewChannels'),
    )

    assertLength(1, signal.args)
    assertLength(1, signal.args[0])       # one channel
    assertLength(2, signal.args[0][0])    # two struct members
    emitted_props = signal.args[0][0][1]

    assertEquals(
        cs.CHANNEL_TYPE_CALL, emitted_props[cs.CHANNEL_TYPE])

    assertEquals(remote_handle, emitted_props[cs.TARGET_HANDLE])
    assertEquals(cs.HT_CONTACT, emitted_props[cs.TARGET_HANDLE_TYPE])
    assertEquals('foo@bar.com', emitted_props[cs.TARGET_ID])

    assertEquals(True, emitted_props[cs.REQUESTED])
    assertEquals(self_handle, emitted_props[cs.INITIATOR_HANDLE])
    assertEquals('test@localhost', emitted_props[cs.INITIATOR_ID])

    assertEquals(True, emitted_props[cs.CALL_INITIAL_AUDIO])
    assertEquals(False, emitted_props[cs.CALL_INITIAL_VIDEO])

if __name__ == '__main__':
    test_all_dialects(run_test)
