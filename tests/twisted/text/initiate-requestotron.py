"""
Test text channel initiated by me, using Requests.
"""

import dbus

from gabbletest import exec_test
from servicetest import call_async, EventPattern
import constants as cs

def test(q, bus, conn, stream):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])

    self_handle = conn.GetSelfHandle()

    jid = 'foo@bar.com'
    foo_handle = conn.RequestHandles(cs.HT_CONTACT, [jid])[0]

    properties = conn.GetAll(
        cs.CONN_IFACE_REQUESTS, dbus_interface=dbus.PROPERTIES_IFACE)
    assert properties.get('Channels') == [], properties['Channels']
    assert ({cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_TEXT,
             cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
             },
             [cs.TARGET_HANDLE, cs.TARGET_ID],
             ) in properties.get('RequestableChannelClasses'),\
                     properties['RequestableChannelClasses']

    call_async(q, conn.Requests, 'CreateChannel',
            { cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_TEXT,
              cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
              cs.TARGET_HANDLE: foo_handle,
              })

    ret, old_sig, new_sig = q.expect_many(
        EventPattern('dbus-return', method='CreateChannel'),
        EventPattern('dbus-signal', signal='NewChannel'),
        EventPattern('dbus-signal', signal='NewChannels'),
        )

    assert len(ret.value) == 2
    emitted_props = ret.value[1]
    assert emitted_props[cs.CHANNEL_TYPE] == cs.CHANNEL_TYPE_TEXT
    assert emitted_props[cs.TARGET_HANDLE_TYPE] == cs.HT_CONTACT
    assert emitted_props[cs.TARGET_HANDLE] == foo_handle
    assert emitted_props[cs.TARGET_ID] == jid
    assert emitted_props[cs.REQUESTED] == True
    assert emitted_props[cs.INITIATOR_HANDLE] == self_handle
    assert emitted_props[cs.INITIATOR_ID] == 'test@localhost'

    assert old_sig.args[0] == ret.value[0]
    assert old_sig.args[1] == cs.CHANNEL_TYPE_TEXT
    assert old_sig.args[2] == cs.HT_CONTACT
    assert old_sig.args[3] == foo_handle
    assert old_sig.args[4] == True      # suppress handler

    assert len(new_sig.args) == 1
    assert len(new_sig.args[0]) == 1        # one channel
    assert len(new_sig.args[0][0]) == 2     # two struct members
    assert new_sig.args[0][0][0] == ret.value[0]
    assert new_sig.args[0][0][1] == ret.value[1]

    properties = conn.GetAll(
        cs.CONN_IFACE_REQUESTS, dbus_interface=dbus.PROPERTIES_IFACE)

    assert new_sig.args[0][0] in properties['Channels'], \
            (new_sig.args[0][0], properties['Channels'])

if __name__ == '__main__':
    exec_test(test)
