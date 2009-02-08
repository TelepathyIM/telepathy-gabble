"""
Tests Contact Search channels to a simulated XEP-0055 service, without
extensibility via Data Forms
"""

import dbus

from gabbletest import exec_test
from servicetest import call_async, unwrap

import constants as cs

server = 'jud.localhost'

def test(q, bus, conn, stream):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged', args=[0, 1])

    requests = dbus.Interface(conn, cs.CONN_IFACE_REQUESTS)
    request = dbus.Dictionary(
        {
            cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_CONTACT_SEARCH,
            cs.CONTACT_SEARCH_SERVER: server,
        }, signature='sv')
    call_async(q, requests, 'CreateChannel', request)

    # Gabble should disco the server here.

    ret = q.expect('dbus-return', method='CreateChannel')
    sig = q.expect('dbus-signal', signal='NewChannels')

    path, props = ret.value

    conn.Disconnect()
    q.expect('dbus-signal', signal='StatusChanged', args=[2, 1])

if __name__ == '__main__':
    exec_test(test)
