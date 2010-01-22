"""
Test basic outgoing and incoming call handling
"""

import dbus
from dbus.exceptions import DBusException

from twisted.words.xish import xpath

from gabbletest import exec_test
from servicetest import (
    make_channel_proxy, wrap_channel,
    EventPattern, call_async,
    assertEquals, assertContains, assertLength, assertNotEquals
    )
import constants as cs
from jingletest2 import JingleTest2, test_all_dialects
import ns

from gabbletest import make_muc_presence
from mucutil import join_muc_and_check

muc = "muji@test"

def run_incoming_test(jp, q, bus, conn, stream):
    conn.Connect()

    q.expect('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])
    _, _, test_handle, bob_handle = \
        join_muc_and_check(q, bus, conn, stream, muc)

    presence = make_muc_presence('owner', 'moderator', muc, 'bob')
    muji = presence.addElement((ns.MUJI, 'muji'))

    stream.send(presence)

    q.expect_many(EventPattern('dbus-signal', signal='NewChannels'))

def run_outgoing_test(jp, q, bus, conn, stream):
    conn.Connect()

    q.expect('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])

    call_async (q, conn.Requests, 'CreateChannel',
        { cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_CALL,
          cs.TARGET_HANDLE_TYPE: cs.HT_ROOM,
          cs.TARGET_ID: muc,
          cs.CALL_INITIAL_AUDIO: True,
         }, byte_arrays = True)


    stream.send(make_muc_presence('none', 'participant', muc, 'test'))

    q.expect_many (EventPattern ('dbus-return', method='CreateChannel'))

if __name__ == '__main__':
    test_all_dialects(run_outgoing_test)
    test_all_dialects(run_incoming_test)

