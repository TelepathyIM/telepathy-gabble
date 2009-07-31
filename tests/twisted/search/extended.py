"""
Tests Contact Search channels to a simulated XEP-0055 service, with
extensibility via Data Forms
"""

import dbus

from twisted.words.xish import xpath

from gabbletest import exec_test
from servicetest import call_async, unwrap, make_channel_proxy, EventPattern
from search_helper import call_create, answer_extended_field_query, make_search, send_results_extended

from pprint import pformat

import constants as cs
import ns

server = 'jud.localhost'

g_jid = 'guybrush.threepwood@lucasarts.example.com'
f_jid = 'freddiet@pgwodehouse.example.com'

g_results = { 'jid': g_jid, 'first': 'Guybrush', 'last': 'Threepwood',
    'nick': 'Fancy Pants', 'x-gender': 'Male', 'email': g_jid }
f_results = { 'jid': f_jid, 'first': 'Frederick', 'last': 'Threepwood',
    'nick': 'Freddie', 'x-gender': 'Male', 'email': f_jid }

def test(q, bus, conn, stream):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged',
        args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])

    requests = dbus.Interface(conn, cs.CONN_IFACE_REQUESTS)

    for f in [complete_search, complete_search2]:
        f(q, bus, conn, requests, stream)

def do_one_search(q, bus, conn, requests, stream, fields, expected_search_keys,
    terms, results):

    call_create(q, requests, server)

    ret, nc_sig = answer_extended_field_query(q, stream, server, fields)

    path, props = ret.value
    props = unwrap(props)

    assert props[cs.CONTACT_SEARCH_SERVER] == server, pformat(props)
    assert sorted(props[cs.CONTACT_SEARCH_ASK]) == expected_search_keys, \
        sorted(props[cs.CONTACT_SEARCH_ASK])
    assert cs.CONTACT_SEARCH_STATE not in props, pformat(props)

    c = make_channel_proxy(conn, path, 'Channel')
    c_props = dbus.Interface(c, cs.PROPERTIES_IFACE)
    c_search = dbus.Interface(c, cs.CHANNEL_TYPE_CONTACT_SEARCH)

    state = c_props.Get(cs.CHANNEL_TYPE_CONTACT_SEARCH, 'SearchState')
    assert state == cs.SEARCH_NOT_STARTED, state

    # We make a search.
    iq = make_search(q, c_search, c_props, server, terms)
    query = iq.firstChildElement()
    fields = xpath.queryForNodes(
        '/iq/query[@xmlns="%s"]/x[@xmlns="%s"][@type="submit"]/field'
        % (ns.SEARCH, ns.X_DATA), iq)
    assert fields is not None

    # check FORM_TYPE
    f = fields[0]
    assert f['type'] == 'hidden'
    assert f['var'] == 'FORM_TYPE'
    value = f.firstChildElement()
    assert value.name == 'value'
    assert value.children[0] == ns.SEARCH

    # extract search fields
    search_fields = []
    for f in fields[1:]:
        value = f.firstChildElement()
        assert value.name == 'value'
        search_fields.append((f['var'], value.children[0]))

    assert len(search_fields) == 1

    # Server sends the results of the search.
    send_results_extended(stream, iq, results)

    return search_fields, c, c_search, c_props

def search_done(q, c, c_search, c_props):
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

def complete_search(q, bus, conn, requests, stream):
    fields = [('first', 'text-single', 'Given Name', []),
        ('last', 'text-single', 'Family Name', []),
        ('x-gender', 'list-single', 'Gender', [('male', 'Male'), ('female', 'Female')])]

    expected_search_keys = ['x-gender', 'x-n-family', 'x-n-given']

    terms = { 'x-n-family': 'Threepwood' }

    results = [g_results, f_results]

    search_fields, chan, c_search, c_props = do_one_search (q, bus, conn, requests, stream,
        fields, expected_search_keys, terms, results)

    # FIXME: this is currently broken because 2 fields are mapped with
    # 'x-n-family': "last" and "family"; Gabble uses the last one inserted to
    # the hash table...
    #assert ('last', 'Threepwood') in search_fields, search_fields

    # get results
    r1 = q.expect('dbus-signal', signal='SearchResultReceived')
    r2 = q.expect('dbus-signal', signal='SearchResultReceived')

    g_handle, g_info = r1.args
    f_handle, f_info = r2.args

    jids = conn.InspectHandles(cs.HT_CONTACT, [g_handle, f_handle])
    assert jids == [g_jid, f_jid], jids

    for i, r in [(g_info, g_results), (f_info, f_results)]:
        i_ = pformat(unwrap(i))
        assert ("x-telepathy-identifier", [], [r['jid']]) in i, i_
        assert ("n", [], [r['last'], r['first'], "", "", ""])    in i, i_
        assert ("nickname", [], [r['nick']]) in i, i_
        assert ("email", [], [r['email']]) in i, i_

        assert len(i) == 4, i_

    search_done(q, chan, c_search, c_props)

    # Check that now the channel has gone away the handles have become invalid.
    for h in g_handle, f_handle:
        call_async(q, conn, 'InspectHandles', cs.HT_CONTACT, [h])
        q.expect('dbus-error', method='InspectHandles')

def complete_search2(q, bus, conn, requests, stream):
    # uses other, dataform specific, fields
    fields = [('given', 'text-single', 'Name', []),
        ('family', 'text-single', 'Family Name', []),
        ('nickname', 'text-single', 'Nickname', [])]

    expected_search_keys = ['nickname', 'x-n-family', 'x-n-given']

    terms = { 'x-n-family': 'Threepwood' }

    results = [g_results, f_results]

    search_fields, chan, c_search, c_props = do_one_search (q, bus, conn, requests, stream,
        fields, expected_search_keys, terms, results)

    assert ('family', 'Threepwood') in search_fields, search_fields

    # get results
    r1 = q.expect('dbus-signal', signal='SearchResultReceived')
    r2 = q.expect('dbus-signal', signal='SearchResultReceived')

    g_handle, g_info = r1.args
    f_handle, f_info = r2.args

    jids = conn.InspectHandles(cs.HT_CONTACT, [g_handle, f_handle])
    assert jids == [g_jid, f_jid], jids

    for i, r in [(g_info, g_results), (f_info, f_results)]:
        i_ = pformat(unwrap(i))
        assert ("x-telepathy-identifier", [], [r['jid']]) in i, i_
        assert ("n", [], [r['last'], r['first'], "", "", ""])    in i, i_
        assert ("nickname", [], [r['nick']]) in i, i_
        assert ("email", [], [r['email']]) in i, i_

        assert len(i) == 4, i_

    search_done(q, chan, c_search, c_props)

if __name__ == '__main__':
    exec_test(test)
