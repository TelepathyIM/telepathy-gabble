"""
Tests Contact Search channels to a simulated XEP-0055 service, without
extensibility via Data Forms
"""

import dbus

from twisted.words.protocols.jabber.client import IQ

from gabbletest import exec_test
from servicetest import call_async, unwrap, make_channel_proxy, EventPattern

from pprint import pformat

import constants as cs
import ns

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

    iq_event = q.expect('stream-iq', to=server, query_ns=ns.SEARCH)
    iq = iq_event.stanza

    result = IQ(stream, "result")
    result["id"] = iq["id"]
    query = result.addElement((ns.SEARCH, 'query'))
    query.addElement("instructions", content="cybar?")
    for f in ["first", "last", "nick", "email"]:
        query.addElement(f)
    stream.send(result)

    ret = q.expect('dbus-return', method='CreateChannel')
    sig = q.expect('dbus-signal', signal='NewChannels')

    path, props = ret.value
    props = unwrap(props)

    expected_search_keys = ['email', 'nickname', 'x-n-family', 'x-n-given']

    assert props[cs.CONTACT_SEARCH_SERVER] == server, pformat(props)
    assert sorted(props[cs.CONTACT_SEARCH_ASK]) == expected_search_keys, \
        pformat(props)
    assert cs.CONTACT_SEARCH_STATE not in props, pformat(props)

    c = make_channel_proxy(conn, path, 'Channel')
    c_props = dbus.Interface(c, cs.PROPERTIES_IFACE)
    c_search = dbus.Interface(c, cs.CHANNEL_TYPE_CONTACT_SEARCH)

    state = c_props.Get(cs.CHANNEL_TYPE_CONTACT_SEARCH, 'SearchState')
    assert state == cs.SEARCH_NOT_STARTED, state

    terms = { 'x-n-family': 'Thomspon' }
    call_async(q, c_search, 'Search', terms)

    _, ssc_event, iq_event = q.expect_many(
        EventPattern('dbus-return', method='Search'),
        EventPattern('dbus-signal', signal='SearchStateChanged'),
        EventPattern('stream-iq', to=server, query_ns=ns.SEARCH),
        )

    assert ssc_event.args[0] == cs.SEARCH_IN_PROGRESS

    state = c_props.Get(cs.CHANNEL_TYPE_CONTACT_SEARCH, 'SearchState')
    assert state == cs.SEARCH_IN_PROGRESS, state

    iq = iq_event.stanza
    query = iq.firstChildElement()
    i = 0
    for field in query.elements():
        assert field.name == 'last', field.toXml()
        assert field.children[0] == u'Thomspon', field.children[0]
        i += 1
    assert i == 1, query

    c.Close()

    q.expect_many(
        EventPattern('dbus-signal', signal='Closed'),
        EventPattern('dbus-signal', signal='ChannelClosed'),
        )

    conn.Disconnect()
    q.expect('dbus-signal', signal='StatusChanged', args=[2, 1])

if __name__ == '__main__':
    exec_test(test)
