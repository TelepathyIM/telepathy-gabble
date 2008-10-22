"""
test OLPC search activity
"""

import dbus

from servicetest import call_async, EventPattern, tp_name_prefix
from gabbletest import (exec_test, make_result_iq, acknowledge_iq, sync_stream,
    elem)

from twisted.words.xish import domish, xpath
from twisted.words.protocols.jabber.client import IQ

from util import announce_gadget
import ns

def test(q, bus, conn, stream):
    conn.Connect()

    _, iq_event, disco_event = q.expect_many(
        EventPattern('dbus-signal', signal='StatusChanged', args=[0, 1]),
        EventPattern('stream-iq', to=None, query_ns='vcard-temp',
            query_name='vCard'),
        EventPattern('stream-iq', to='localhost', query_ns=ns.DISCO_ITEMS))

    acknowledge_iq(stream, iq_event.stanza)

    props = conn.GetAll(
            'org.laptop.Telepathy.Gadget',
            dbus_interface='org.freedesktop.DBus.Properties')
    assert props['GadgetAvailable'] == False

    announce_gadget(q, stream, disco_event.stanza)

    q.expect('dbus-signal', signal='GadgetDiscovered')

    props = conn.GetAll(
            'org.laptop.Telepathy.Gadget',
            dbus_interface='org.freedesktop.DBus.Properties')
    assert props['GadgetAvailable'] == True

    gadget_iface = dbus.Interface(conn, 'org.laptop.Telepathy.Gadget')

    call_async(q, gadget_iface, 'Publish', True)

    q.expect_many(
            EventPattern('stream-presence', presence_type='subscribe'),
            EventPattern('dbus-return', method='Publish'))

    # accept the request
    presence = elem('presence', to='test@localhost', from_='gadget.localhost',
        type='subscribed')
    stream.send(presence)

    # send a subscribe request
    presence = elem('presence', to='test@localhost', from_='gadget.localhost',
        type='subscribe')
    stream.send(presence)

    q.expect('stream-presence', presence_type='subscribed'),

    call_async(q, gadget_iface, 'Publish', False)

    q.expect_many(
            EventPattern('stream-presence', presence_type='unsubscribe'),
            EventPattern('stream-presence', presence_type='unsubscribed'),
            EventPattern('dbus-return', method='Publish'))

    # Gadget tries to subscribe but is refused now
    presence = elem('presence', to='test@localhost', from_='gadget.localhost',
        type='subscribe')
    stream.send(presence)

    q.expect('stream-presence', presence_type='unsubscribed'),

if __name__ == '__main__':
    exec_test(test)
