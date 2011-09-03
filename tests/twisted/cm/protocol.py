"""
Test Gabble's o.T.Protocol implementation
"""

import dbus
from servicetest import unwrap, tp_path_prefix, assertEquals
from gabbletest import exec_test, call_async
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

    # org.freedesktop.Telepathy.Protocol.Interface.Presence
    expected_status = {'available': (cs.PRESENCE_AVAILABLE,     True,  True),
                       'dnd'      : (cs.PRESENCE_BUSY,          True,  True),
                       'unknown'  : (cs.PRESENCE_UNKNOWN,       False, False),
                       'away'     : (cs.PRESENCE_AWAY,          True,  True),
                       'xa'       : (cs.PRESENCE_EXTENDED_AWAY, True,  True),
                       'chat'     : (cs.PRESENCE_AVAILABLE,     True,  True),
                       'error'    : (cs.PRESENCE_ERROR,         False, False),
                       'offline'  : (cs.PRESENCE_OFFLINE,       False, False),
                       'testaway' : (cs.PRESENCE_AWAY,          False, False),
                       'testbusy' : (cs.PRESENCE_BUSY,          True,  False),
                       'hidden'   : (cs.PRESENCE_HIDDEN,        True,  True)}

    presences = proto_prop_iface.Get(cs.PROTOCOL_IFACE_PRESENCES, 'Statuses');
    assertEquals(expected_status, unwrap(presences))

    # (Only) 'account' is mandatory for IdentifyAccount()
    call_async(q, proto_iface, 'IdentifyAccount', {})
    q.expect('dbus-error', method='IdentifyAccount', name=cs.INVALID_ARGUMENT)

    test_params = { 'account': 'test@localhost' }
    acc_name = unwrap(proto_iface.IdentifyAccount(test_params))
    assertEquals(test_params['account'], acc_name)

    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged', args=[cs.CONN_STATUS_CONNECTING, cs.CSR_REQUESTED])
    q.expect('stream-authenticated')
    q.expect('dbus-signal', signal='PresencesChanged',
        args=[{1L: (cs.PRESENCE_AVAILABLE, 'available', '')}])
    q.expect('dbus-signal', signal='StatusChanged', args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])
    return


if __name__ == '__main__':
    exec_test(test, do_connect=False)

