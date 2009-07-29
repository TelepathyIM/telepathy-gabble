"""
Check that Gabble rejects invalid requests for tubes channels.
"""

import dbus

from servicetest import call_async
from gabbletest import exec_test
import constants as cs

def is_tube(path, props):
    ct = props[cs.CHANNEL_TYPE]
    return ct in [cs.CHANNEL_TYPE_STREAM_TUBE, cs.CHANNEL_TYPE_DBUS_TUBE]

def check_no_tubes(conn_props):
    channels = conn_props.Get(cs.CONN_IFACE_REQUESTS, 'Channels')
    tube_channels = filter(is_tube, channels)
    assert len(tube_channels) == 0, tube_channels

def test(q, bus, conn, stream):
    conn.Connect()

    q.expect('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])

    conn_props = dbus.Interface(conn, cs.PROPERTIES_IFACE)

    # Try to CreateChannel with unknown properties
    # Gabble must return an error
    call_async(q, conn.Requests, 'CreateChannel',
            {cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_STREAM_TUBE,
             cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
             cs.TARGET_ID: "foo@example.com",
             'this.property.does.not.exist': 'this.value.should.not.exist'
            })
    ret = q.expect('dbus-error', method='CreateChannel')

    check_no_tubes(conn_props)

    # Try to CreateChannel with missing properties ("Service")
    # Gabble must return an error
    call_async(q, conn.Requests, 'CreateChannel',
            {cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_STREAM_TUBE,
             cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
             cs.TARGET_ID: "foo@example.com",
            })
    ret = q.expect('dbus-error', method='CreateChannel')

    check_no_tubes(conn_props)

if __name__ == '__main__':
    exec_test(test)
