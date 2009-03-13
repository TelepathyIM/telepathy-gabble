import dbus

from gabbletest import exec_test
from constants import *

invalid_service_names = [ 'invalidServiceName'
                        , 'one ten hundred thousand million'
                        , 'me.is.it.you?.hello.you.sexy.sons.o.@#$%.heh'
                        , ':1.1'
                        , ''
                        ]

def test(q, bus, conn, stream):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged', args=[0L, 1L])

    for invalid_service_name in invalid_service_names:
        try:
            conn.Requests.CreateChannel(
                    {CHANNEL_TYPE: CHANNEL_TYPE_DBUS_TUBE,
                     TARGET_HANDLE_TYPE: HT_CONTACT,
                     TARGET_ID: 'alice@localhost',
                     DBUS_TUBE_SERVICE_NAME: invalid_service_name
                });
        except dbus.DBusException, e:
            assert e.get_dbus_name() == INVALID_ARGUMENT, \
                (e.get_dbus_name(), invalid_service_name)
        else:
            assert False

    # TODO: do the same with muc D-Bus tubes

if __name__ == '__main__':
    exec_test(test)
