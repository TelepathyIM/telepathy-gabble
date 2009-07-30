"""
Tests Contact Search channels to a simulated XEP-0055 service, without
extensibility via Data Forms
"""

import dbus

from twisted.words.protocols.jabber.client import IQ

from gabbletest import exec_test, sync_stream
from servicetest import call_async, unwrap, make_channel_proxy, EventPattern

from pprint import pformat

import constants as cs
import ns

g_jid = 'guybrush.threepwood@lucasarts.example.com'
f_jid = 'freddiet@pgwodehouse.example.com'
g_results = (g_jid, 'Guybrush', 'Threepwood', 'Fancy Pants')
f_results = (f_jid, 'Frederick', 'Threepwood', 'Freddie')
results = [g_results, f_results]


def test(q, bus, conn, stream):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged',
        args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])

    requests = dbus.Interface(conn, cs.CONN_IFACE_REQUESTS)

    for f in [complete_search, cancelled_while_in_progress]:
        f(q, bus, conn, requests, stream, 'jud.localhost')

    conn.Disconnect()
    q.expect('dbus-signal', signal='StatusChanged', args=[2, 1])

def call_create(q, requests, server):
    request = dbus.Dictionary(
        {
            cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_CONTACT_SEARCH,
            cs.CONTACT_SEARCH_SERVER: server,
        }, signature='sv')
    call_async(q, requests, 'CreateChannel', request)

def answer_field_query(q, stream, server):
    # Gabble asks the server what search fields it supports
    iq_event = q.expect('stream-iq', to=server, query_ns=ns.SEARCH)
    iq = iq_event.stanza

    # The server says it supports all the fields in unextended XEP 0055
    result = IQ(stream, "result")
    result["id"] = iq["id"]
    query = result.addElement((ns.SEARCH, 'query'))
    query.addElement("instructions", content="cybar?")
    for f in ["first", "last", "nick", "email"]:
        query.addElement(f)
    stream.send(result)

    ret = q.expect('dbus-return', method='CreateChannel')
    nc_sig = q.expect('dbus-signal', signal='NewChannels')

    return (ret, nc_sig)

def make_search(q, c_search, c_props, server):
    terms = { 'x-n-family': 'Threepwood' }
    call_async(q, c_search, 'Search', terms)

    _, ssc_event, iq_event = q.expect_many(
        EventPattern('dbus-return', method='Search'),
        EventPattern('dbus-signal', signal='SearchStateChanged'),
        EventPattern('stream-iq', to=server, query_ns=ns.SEARCH),
        )

    assert ssc_event.args[0] == cs.SEARCH_IN_PROGRESS

    state = c_props.Get(cs.CHANNEL_TYPE_CONTACT_SEARCH, 'SearchState')
    assert state == cs.SEARCH_IN_PROGRESS, state

    return iq_event.stanza

def send_results(stream, iq):
    result = IQ(stream, 'result')
    result['id'] = iq['id']
    query = result.addElement((ns.SEARCH, 'query'))
    for jid, first, last, nick in results:
        item = query.addElement('item')
        item['jid'] = jid
        item.addElement('first', content=first)
        item.addElement('last', content=last)
        item.addElement('nick', content=nick)
        item.addElement('email', content=jid)
    stream.send(result)

def complete_search(q, bus, conn, requests, stream, server):
    call_create(q, requests, server)

    ret, nc_sig = answer_field_query(q, stream, server)

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

    # We make a search.
    iq = make_search(q, c_search, c_props, server)
    query = iq.firstChildElement()
    i = 0
    for field in query.elements():
        assert field.name == 'last', field.toXml()
        assert field.children[0] == u'Threepwood', field.children[0]
        i += 1
    assert i == 1, query

    # Server sends the results of the search.
    send_results(stream, iq)

    r1 = q.expect('dbus-signal', signal='SearchResultReceived')
    r2 = q.expect('dbus-signal', signal='SearchResultReceived')

    g_handle, g_info = r1.args
    f_handle, f_info = r2.args

    jids = conn.InspectHandles(cs.HT_CONTACT, [g_handle, f_handle])
    assert jids == [g_jid, f_jid], jids

    for i, r in [(g_info, g_results), (f_info, f_results)]:
        i_ = pformat(unwrap(i))
        assert ("x-telepathy-identifier", [], [r[0]]) in i, i_
        assert ("n", [], [r[2], r[1], "", "", ""])    in i, i_
        assert ("nickname", [], [r[3]])               in i, i_
        assert ("email", [], [r[0]])                  in i, i_

        assert len(i) == 4, i_

    ssc = q.expect('dbus-signal', signal='SearchStateChanged')
    assert ssc.args[0] == cs.SEARCH_COMPLETED, ssc.args

    # We call Stop after the search has completed; it should succeed, but leave
    # the channel in state Completed rather than changing it to Failed for
    # reason Cancelled.
    call_async(q, c_search, 'Stop')
    event = q.expect('dbus-return', method='Stop')
    state = c_props.Get(cs.CHANNEL_TYPE_CONTACT_SEARCH, 'SearchState')
    assert state == cs.SEARCH_COMPLETED, (state, cs.SEARCH_COMPLETED)

    c.Close()

    q.expect_many(
        EventPattern('dbus-signal', signal='Closed'),
        EventPattern('dbus-signal', signal='ChannelClosed'),
        )

    # Check that now the channel has gone away the handles have become invalid.
    for h in g_handle, f_handle:
        call_async(q, conn, 'InspectHandles', cs.HT_CONTACT, [h])
        q.expect('dbus-error', method='InspectHandles')

def cancelled_while_in_progress(q, bus, conn, requests, stream, server):
    call_create(q, requests, server)

    ret, _ = answer_field_query(q, stream, server)
    path, props = ret.value

    c = make_channel_proxy(conn, path, 'Channel')
    c_props = dbus.Interface(c, cs.PROPERTIES_IFACE)
    c_search = dbus.Interface(c, cs.CHANNEL_TYPE_CONTACT_SEARCH)

    iq = make_search(q, c_search, c_props, server)

    # Before the server sends back the results, the client cancels the search.
    call_async(q, c_search, 'Stop')
    ret, ssc = q.expect_many(
        EventPattern('dbus-return', method='Stop'),
        EventPattern('dbus-signal', signal='SearchStateChanged'),
        )

    assert ssc.args[0] == cs.SEARCH_FAILED, ssc.args
    assert ssc.args[1] == cs.CANCELLED, ssc.args

    state = c_props.Get(cs.CHANNEL_TYPE_CONTACT_SEARCH, 'SearchState')
    assert state == cs.SEARCH_FAILED, (state, cs.SEARCH_FAILED)

    # Now the server sends us the results; SearchResultReceived shouldn't fire
    search_result_received_event = EventPattern('dbus-signal', signal='SearchResultReceived')
    q.forbid_events([search_result_received_event])

    send_results(stream, iq)

    # Make sure Gabble's received the results.
    sync_stream(q, stream)

    # Hooray! We survived. Now let's call Stop again; it should succeed but do
    # nothing.
    search_state_changed_event = EventPattern('dbus-signal', signal='SearchStateChanged')
    q.forbid_events([search_state_changed_event])

    call_async(q, c_search, 'Stop')
    ssc = q.expect('dbus-return', method='Stop')

    c.Close()
    q.unforbid_events([search_result_received_event, search_state_changed_event])

if __name__ == '__main__':
    exec_test(test)
