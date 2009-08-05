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

def test(q, bus, conn, stream):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged',
        args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])

    requests = dbus.Interface(conn, cs.CONN_IFACE_REQUESTS)

    for f in [complete_search, complete_search2, openfire_search]:
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
    fields_sent = xpath.queryForNodes(
        '/iq/query[@xmlns="%s"]/x[@xmlns="%s"][@type="submit"]/field'
        % (ns.SEARCH, ns.X_DATA), iq)
    assert fields_sent is not None

    # check FORM_TYPE
    f = fields_sent[0]
    assert f['type'] == 'hidden'
    assert f['var'] == 'FORM_TYPE'
    value = f.firstChildElement()
    assert value.name == 'value'
    assert value.children[0] == ns.SEARCH

    # extract search fields
    search_fields = []
    for f in fields_sent[1:]:
        value = f.firstChildElement()
        assert value.name == 'value'
        search_fields.append((f['var'], value.children[0]))

    # Server sends the results of the search.
    send_results_extended(stream, iq, results, fields)

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

    g_results = { 'jid': g_jid, 'first': 'Guybrush', 'last': 'Threepwood',
        'nick': 'Fancy Pants', 'x-gender': 'Male', 'email': g_jid }
    f_results = { 'jid': f_jid, 'first': 'Frederick', 'last': 'Threepwood',
        'nick': 'Freddie', 'x-gender': 'Male', 'email': f_jid }

    results = { g_jid: g_results, f_jid: f_results }

    search_fields, chan, c_search, c_props = do_one_search (q, bus, conn, requests, stream,
        fields, expected_search_keys, terms, results.values())

    assert len(search_fields) == 1
    assert ('last', 'Threepwood') in search_fields, search_fields

    e = q.expect('dbus-signal', signal='SearchResultReceived')
    infos = e.args[0]

    handles = infos.keys()
    jids = conn.InspectHandles(cs.HT_CONTACT, handles)
    assert set(jids) == set(results.keys())

    for handle, id in zip(handles, jids):
        i = infos[handle]
        r = results[id]
        i_ = pformat(unwrap(i))
        assert ("x-telepathy-identifier", [], [r['jid']]) in i, i_
        assert ("n", [], [r['last'], r['first'], "", "", ""])    in i, i_
        assert ("nickname", [], [r['nick']]) in i, i_
        assert ("email", [], [r['email']]) in i, i_
        assert ("x-gender", [], [r['x-gender']]) in i, i_
        assert ("x-n-family", [], [r['last']]) in i, i_
        assert ("x-n-given", [], [r['first']]) in i, i_

        assert len(i) == 7, i_

    search_done(q, chan, c_search, c_props)

    # Check that now the channel has gone away the handles have become invalid.
    for h in handles:
        call_async(q, conn, 'InspectHandles', cs.HT_CONTACT, [h])
        q.expect('dbus-error', method='InspectHandles')

def complete_search2(q, bus, conn, requests, stream):
    # uses other, dataform specific, fields
    fields = [('given', 'text-single', 'Name', []),
        ('family', 'text-single', 'Family Name', []),
        ('nickname', 'text-single', 'Nickname', [])]

    expected_search_keys = ['nickname', 'x-n-family', 'x-n-given']

    terms = { 'x-n-family': 'Threepwood' }

    g_results = { 'jid': g_jid, 'given': 'Guybrush', 'family': 'Threepwood',
        'nickname': 'Fancy Pants', 'email': g_jid }
    f_results = { 'jid': f_jid, 'given': 'Frederick', 'family': 'Threepwood',
        'nickname': 'Freddie', 'email': f_jid }

    results = { g_jid: g_results, f_jid: f_results }

    search_fields, chan, c_search, c_props = do_one_search (q, bus, conn, requests, stream,
        fields, expected_search_keys, terms, results.values())

    assert len(search_fields) == 1
    assert ('family', 'Threepwood') in search_fields, search_fields

    e = q.expect('dbus-signal', signal='SearchResultReceived')
    infos = e.args[0]

    handles = infos.keys()
    jids = conn.InspectHandles(cs.HT_CONTACT, handles)
    assert set(jids) == set(results.keys())

    for handle, id in zip(handles, jids):
        i = infos[handle]
        r = results[id]
        i_ = pformat(unwrap(i))
        assert ("x-telepathy-identifier", [], [r['jid']]) in i, i_
        assert ("n", [], [r['family'], r['given'], "", "", ""])    in i, i_
        assert ("nickname", [], [r['nickname']]) in i, i_
        assert ("email", [], [r['email']]) in i, i_
        assert ("x-n-family", [], [r['family']]) in i, i_
        assert ("x-n-given", [], [r['given']]) in i, i_

        assert len(i) == 6, i_

    search_done(q, chan, c_search, c_props)

def openfire_search(q, bus, conn, requests, stream):
    # Openfire only supports one text field and a bunch on checkbox
    fields = [('search', 'text-single', 'Search', []),
        ('Username', 'boolean', 'Username', []),
        ('Name', 'boolean', 'Name', []),
        ('Email', 'boolean', 'Email', [])]

    expected_search_keys = ['']

    terms = { '': '*badger*' }

    jid = 'badger@mushroom.org'
    results = {jid : { 'jid': jid, 'Name': 'Badger Badger', 'Email': jid, 'Username': 'badger'}}

    search_fields, chan, c_search, c_props = do_one_search (q, bus, conn, requests, stream,
        fields, expected_search_keys, terms, results.values())

    assert len(search_fields) == 4
    assert ('search', '*badger*') in search_fields, search_fields
    assert ('Username', '1') in search_fields, search_fields
    assert ('Name', '1') in search_fields, search_fields
    assert ('Email', '1') in search_fields, search_fields

    r = q.expect('dbus-signal', signal='SearchResultReceived')
    infos = r.args[0]

    handles = infos.keys()
    jids = conn.InspectHandles(cs.HT_CONTACT, handles)
    assert set(jids) == set(results.keys())

    for handle, id in zip(handles, jids):
        i = infos[handle]
        r = results[id]
        i_ = pformat(unwrap(i))
        assert ("x-telepathy-identifier", [], [r['jid']]) in i, i_
        assert ("fn", [], [r['Name']]) in i, i_
        assert ("email", [], [r['Email']]) in i, i_

        assert len(i) == 3

if __name__ == '__main__':
    exec_test(test)
