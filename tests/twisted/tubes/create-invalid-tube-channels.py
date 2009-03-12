"""
Check that Gabble rejects invalid requests for tubes channels.
"""

import dbus
from dbus import PROPERTIES_IFACE

from servicetest import call_async
from gabbletest import exec_test
from constants import *

def is_tube(path, props):
    ct = props[CHANNEL_TYPE]
    return ct in [CHANNEL_TYPE_STREAM_TUBE, CHANNEL_TYPE_DBUS_TUBE]

def check_no_tubes(conn_props):
    channels = conn_props.Get(CONN_IFACE_REQUESTS, 'Channels')
    tube_channels = filter(is_tube, channels)
    assert len(tube_channels) == 0, tube_channels

def test(q, bus, conn, stream):
    conn.Connect()

    q.expect('dbus-signal', signal='StatusChanged', args=[0, 1]),

    conn_props = dbus.Interface(conn, PROPERTIES_IFACE)

    # Try to CreateChannel with unknown properties
    # Gabble must return an error
    call_async(q, conn.Requests, 'CreateChannel',
            {CHANNEL_TYPE: CHANNEL_TYPE_STREAM_TUBE,
             TARGET_HANDLE_TYPE: HT_CONTACT,
             TARGET_ID: "foo@example.com",
             'this.property.does.not.exist':
                'this.value.should.not.exist'
            });
    ret = q.expect('dbus-error', method='CreateChannel')

    check_no_tubes(conn_props)

    # Try to CreateChannel with missing properties ("Service")
    # Gabble must return an error
    call_async(q, conn.Requests, 'CreateChannel',
            {CHANNEL_TYPE: CHANNEL_TYPE_STREAM_TUBE,
             TARGET_HANDLE_TYPE: HT_CONTACT,
             TARGET_ID: "foo@example.com",
            });
    ret = q.expect('dbus-error', method='CreateChannel')

    check_no_tubes(conn_props)

    conn.Disconnect()

    q.expect('dbus-signal', signal='StatusChanged', args=[2, 1])

if __name__ == '__main__':
    exec_test(test)
