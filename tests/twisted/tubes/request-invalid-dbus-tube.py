import dbus

from gabbletest import exec_test
import constants as cs

invalid_service_names = [ 'invalidServiceName'
                        , 'one ten hundred thousand million'
                        , 'me.is.it.you?.hello.you.sexy.sons.o.@#$%.heh'
                        , ':1.1'
                        , ''
                        ]

def test(q, bus, conn, stream):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])

    for invalid_service_name in invalid_service_names:
        try:
            conn.Requests.CreateChannel(
                    {cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_DBUS_TUBE,
                     cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
                     cs.TARGET_ID: 'alice@localhost',
                     cs.DBUS_TUBE_SERVICE_NAME: invalid_service_name
                })
        except dbus.DBusException, e:
            assert e.get_dbus_name() == cs.INVALID_ARGUMENT, \
                (e.get_dbus_name(), invalid_service_name)
        else:
            assert False

    # TODO: do the same with muc D-Bus tubes

if __name__ == '__main__':
    exec_test(test)
