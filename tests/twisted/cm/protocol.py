"""
Test Gabble's o.T.Protocol implementation
"""

import dbus
from servicetest import unwrap, tp_path_prefix, assertEquals
from gabbletest import exec_test
import constants as cs
import time

def test(q, bus, conn, stream):
    cm = bus.get_object(cs.CM + '.gabble',
        tp_path_prefix + '/ConnectionManager/gabble')
    cm_iface = dbus.Interface(cm, cs.CM)
    cm_prop_iface = dbus.Interface(cm, cs.PROPERTIES_IFACE)

    protocols = unwrap(cm_prop_iface.Get(cs.CM, 'Protocols'))
    assertEquals(set(['jabber']), set(protocols.keys()))

    protocol_names = unwrap(cm_iface.ListProtocols())
    assertEquals(set(['jabber']), set(protocol_names))

    cm_params = cm_iface.GetParameters('jabber')
    jabber_props = protocols['jabber']
    jabber_params = jabber_props[cs.PROTOCOL + '.Parameters']
    assertEquals(cm_params, jabber_params)

    proto = bus.get_object(cm.bus_name, cm.object_path + '/jabber')
    proto_iface = dbus.Interface(proto, cs.PROTOCOL)
    proto_prop_iface = dbus.Interface(proto, cs.PROPERTIES_IFACE)
    proto_props = unwrap(proto_prop_iface.GetAll(cs.PROTOCOL))

    for key in ['Parameters', 'Interfaces', 'ConnectionInterfaces',
      'RequestableChannelClasses', u'VCardField', u'EnglishName', u'Icon']:
        a = jabber_props[cs.PROTOCOL + '.' + key]
        b = proto_props[key]
        assertEquals(a, b)

    assertEquals('foo@mit.edu',
        unwrap(proto_iface.NormalizeContact('foo@MIT.Edu/Telepathy')))

    # (Only) 'account' is mandatory for IdentifyAccount()
    try:
        proto_iface.IdentifyAccount({})
    except dbus.DBusException, e:
        assertEquals(cs.INVALID_ARGUMENT, e.get_dbus_name())
    else:
        raise AssertionError("IdentifyAccount({}) should've returned error but didn't")

    test_params = { 'account': 'test@localhost' }
    acc_name = unwrap(proto_iface.IdentifyAccount(test_params))
    assertEquals(test_params['account'], acc_name)

    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged', args=[cs.CONN_STATUS_CONNECTING, cs.CSR_REQUESTED])
    q.expect('stream-authenticated')
    q.expect('dbus-signal', signal='PresenceUpdate',
        args=[{1L: (0L, {u'available': {}})}])
    q.expect('dbus-signal', signal='StatusChanged', args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])
    return


if __name__ == '__main__':
    exec_test(test)

