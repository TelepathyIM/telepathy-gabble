"""
Tests making two connections to the same account, to see if Gabble correctly
picks random resources when none has been specified. (If they get the same
resource, the second call to RequestConnection will fail, since they'll try to
have the same object path.
"""

import dbus

from servicetest import tp_name_prefix, tp_path_prefix, wrap_connection

if __name__ == '__main__':
    bus = dbus.SessionBus()

    name = 'gabble'
    proto = 'jabber'

    cm = bus.get_object(
        tp_name_prefix + '.ConnectionManager.%s' % name,
        tp_path_prefix + '/ConnectionManager/%s' % name)
    cm_iface = dbus.Interface(cm, tp_name_prefix + '.ConnectionManager')

    params = {
        'account': 'test@localhost',
        'password': 'pass',
        'server': 'localhost',
        'port': dbus.UInt32(4242),
        }

    # Create two connections with the same account and no specified resource.
    connection_name, connection_path = cm_iface.RequestConnection(
        proto, params)
    conn1 = wrap_connection(bus.get_object(connection_name, connection_path))

    connection_name, connection_path = cm_iface.RequestConnection(
        proto, params)
    conn2 = wrap_connection(bus.get_object(connection_name, connection_path))

    # Cool. Let's get rid of them.
    try:
        conn1.Connect()
        conn1.Disconnect()
    except dbus.DBusException, e:
        pass

    try:
        conn2.Connect()
        conn2.Disconnect()
    except dbus.DBusException, e:
        pass
