"""
Tests Contact Search channels to a simulated XEP-0055 service, without
extensibility via Data Forms
"""

import dbus

from twisted.words.protocols.jabber.client import IQ

from gabbletest import exec_test, sync_stream
from servicetest import call_async, unwrap, make_channel_proxy, EventPattern
from search_helper import call_create, answer_field_query, make_search, send_results

from pprint import pformat

import constants as cs
import ns

g_jid = 'guybrush.threepwood@lucasarts.example.com'
f_jid = 'freddiet@pgwodehouse.example.com'
g_results = (g_jid, 'Guybrush', 'Threepwood', 'Fancy Pants')
f_results = (f_jid, 'Frederick', 'Threepwood', 'Freddie')
results = { g_jid: g_results, f_jid: f_results }

def test(q, bus, conn, stream):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged',
        args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])

    requests = dbus.Interface(conn, cs.CONN_IFACE_REQUESTS)

    for f in [complete_search, cancelled_while_in_progress]:
        f(q, bus, conn, requests, stream, 'jud.localhost')

def complete_search(q, bus, conn, requests, stream, server):
    call_create(q, requests, server)

    # the channel is not yet in Requests.Channels as it's not ready yet
    channels = conn.Get(cs.CONN_IFACE_REQUESTS, 'Channels',
        dbus_interface=cs.PROPERTIES_IFACE)
    for path, props in channels:
        assert props[cs.CHANNEL_TYPE] != cs.CHANNEL_TYPE_CONTACT_SEARCH

    ret, nc_sig = answer_field_query(q, stream, server)

    path, props = ret.value
    props = unwrap(props)

    expected_search_keys = ['email', 'nickname', 'x-n-family', 'x-n-given']

    assert props[cs.CONTACT_SEARCH_SERVER] == server, pformat(props)
    assert sorted(props[cs.CONTACT_SEARCH_ASK]) == expected_search_keys, \
        pformat(props)
    assert cs.CONTACT_SEARCH_STATE not in props, pformat(props)

    # check that channel is listed in Requests.Channels
    channels = conn.Get(cs.CONN_IFACE_REQUESTS, 'Channels',
        dbus_interface=cs.PROPERTIES_IFACE)
    assert (path, props) in channels

    c = make_channel_proxy(conn, path, 'Channel')
    c_props = dbus.Interface(c, cs.PROPERTIES_IFACE)
    c_search = dbus.Interface(c, cs.CHANNEL_TYPE_CONTACT_SEARCH)

    state = c_props.Get(cs.CHANNEL_TYPE_CONTACT_SEARCH, 'SearchState')
    assert state == cs.SEARCH_NOT_STARTED, state

    # We make a search.
    iq = make_search(q, c_search, c_props, server, { 'x-n-family': 'Threepwood' })
    query = iq.firstChildElement()
    i = 0
    for field in query.elements():
        assert field.name == 'last', field.toXml()
        assert field.children[0] == u'Threepwood', field.children[0]
        i += 1
    assert i == 1, query

    # Server sends the results of the search.
    send_results(stream, iq, results.values())

    r = q.expect('dbus-signal', signal='SearchResultReceived')
    infos = r.args[0]

    handles = infos.keys()
    jids = conn.InspectHandles(cs.HT_CONTACT, handles)
    assert set(jids) == set(results.keys())

    for handle, id in zip(handles, jids):
        i = infos[handle]
        r = results[id]
        i_ = pformat(unwrap(i))
        assert ("x-telepathy-identifier", [], [r[0]]) in i, i_
        assert ("n", [], [r[2], r[1], "", "", ""])    in i, i_
        assert ("nickname", [], [r[3]])               in i, i_
        assert ("email", [], [r[0]])                  in i, i_
        assert ("x-n-family", [], [r[2]]) in i, i_
        assert ("x-n-given", [], [r[1]]) in i, i_

        assert len(i) == 6, i_

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
    for h in handles:
        call_async(q, conn, 'InspectHandles', cs.HT_CONTACT, [h])
        q.expect('dbus-error', method='InspectHandles')

def cancelled_while_in_progress(q, bus, conn, requests, stream, server):
    call_create(q, requests, server)

    ret, _ = answer_field_query(q, stream, server)
    path, props = ret.value

    c = make_channel_proxy(conn, path, 'Channel')
    c_props = dbus.Interface(c, cs.PROPERTIES_IFACE)
    c_search = dbus.Interface(c, cs.CHANNEL_TYPE_CONTACT_SEARCH)

    iq = make_search(q, c_search, c_props, server, { 'x-n-family': 'Threepwood' })

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

    send_results(stream, iq, results.values())

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
