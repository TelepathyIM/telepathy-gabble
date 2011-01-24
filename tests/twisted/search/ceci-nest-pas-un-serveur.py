"""
Tests requesting search channels to, and performing contact searches against,
fake servers which are broken in various ways.
"""

import dbus

from twisted.words.protocols.jabber.client import IQ
from twisted.words.xish import domish

from gabbletest import exec_test, send_error_reply, make_result_iq
from servicetest import (
    call_async, unwrap, make_channel_proxy, EventPattern, assertDBusError
    )

from pprint import pformat

import constants as cs
import ns

def call_create(q, conn, server):
    """
    Calls CreateChannel for the given contact search server, and returns the IQ
    stanza received by the server.
    """

    request = dbus.Dictionary(
        {
            cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_CONTACT_SEARCH,
            cs.CONTACT_SEARCH_SERVER: server,
        }, signature='sv')
    call_async(q, conn.Requests, 'CreateChannel', request)

    iq_event = q.expect('stream-iq', to=server, query_ns=ns.SEARCH)
    return iq_event.stanza

def not_a_search_server(q, stream, conn):
    iq = call_create(q, conn, 'notajud.localhost')

    e = domish.Element((None, 'error'))
    e['type'] = 'cancel'
    e.addElement((ns.STANZA, 'service-unavailable'))
    send_error_reply(stream, iq, e)

    event = q.expect('dbus-error', method='CreateChannel')
    assertDBusError(cs.NOT_AVAILABLE, event.error)

def returns_invalid_fields(q, stream, conn):
    iq = call_create(q, conn, 'broken.localhost')

    result = make_result_iq(stream, iq)
    query = result.firstChildElement()
    for f in ["first", "shoe-size", "nick", "star-sign"]:
        query.addElement(f)
    stream.send(result)

    event = q.expect('dbus-error', method='CreateChannel')
    assertDBusError(cs.NOT_AVAILABLE, event.error)

def returns_error_from_search(q, stream, conn):
    server = 'nofunforyou.localhost'
    iq = call_create(q, conn, server)

    result = make_result_iq(stream, iq)
    query = result.firstChildElement()
    query.addElement("first")
    stream.send(result)

    event = q.expect('dbus-return', method='CreateChannel')
    c = make_channel_proxy(conn, event.value[0], 'Channel')
    c_search = dbus.Interface(c, cs.CHANNEL_TYPE_CONTACT_SEARCH)

    call_async(q, c_search, 'Search', {'x-n-given': 'World of Goo'})
    iq_event, _ = q.expect_many(
        EventPattern('stream-iq', to=server, query_ns=ns.SEARCH),
        EventPattern('dbus-signal', signal='SearchStateChanged'),
        )

    iq = iq_event.stanza
    error = domish.Element((None, 'error'))
    error['type'] = 'modify'
    error.addElement((ns.STANZA, 'not-acceptable'))
    error.addElement((ns.STANZA, 'text'), content="We don't believe in games here.")
    send_error_reply(stream, iq, error)

    ssc = q.expect('dbus-signal', signal='SearchStateChanged')
    new_state, reason, details = ssc.args

    assert new_state == cs.SEARCH_FAILED, new_state
    assert reason == cs.PERMISSION_DENIED, reason

    # We call stop after the search has failed; it should succeed and do nothing.
    call_async(q, c_search, 'Stop')
    event = q.expect('dbus-return', method='Stop')

    c.Close()

def returns_bees_from_search(q, stream, conn):
    server = 'hivemind.localhost'
    iq = call_create(q, conn, server)

    result = make_result_iq(stream, iq)
    query = result.firstChildElement()
    query.addElement("nick")
    stream.send(result)

    event = q.expect('dbus-return', method='CreateChannel')
    c = make_channel_proxy(conn, event.value[0], 'Channel')
    c_search = dbus.Interface(c, cs.CHANNEL_TYPE_CONTACT_SEARCH)

    call_async(q, c_search, 'Search', {'nickname': 'Buzzy'})
    iq_event, _ = q.expect_many(
        EventPattern('stream-iq', to=server, query_ns=ns.SEARCH),
        EventPattern('dbus-signal', signal='SearchStateChanged'),
        )
    iq = iq_event.stanza

    result = IQ(stream, 'result')
    result['id'] = iq['id']
    result['from'] = iq['to']
    result.addElement((ns.SEARCH, 'bees')).addElement('bzzzzzzz')
    stream.send(result)

    ssc = q.expect('dbus-signal', signal='SearchStateChanged')
    new_state, reason, details = ssc.args

    assert new_state == cs.SEARCH_FAILED, new_state
    assert reason == cs.NOT_AVAILABLE, reason

    # We call stop after the search has failed; it should succeed and do nothing.
    call_async(q, c_search, 'Stop')
    event = q.expect('dbus-return', method='Stop')

    c.Close()

def disconnected_before_reply(q, stream, conn):
    iq = call_create(q, conn, 'slow.localhost')

    call_async(q, conn, 'Disconnect')

    event = q.expect('dbus-error', method='CreateChannel')
    assertDBusError(cs.DISCONNECTED, event.error)

def forbidden(q, stream, conn):
    iq = call_create(q, conn, 'notforyou.localhost')

    e = domish.Element((None, 'error'))
    e['type'] = 'cancel'
    e.addElement((ns.STANZA, 'forbidden'))
    send_error_reply(stream, iq, e)

    event = q.expect('dbus-error', method='CreateChannel')
    assertDBusError(cs.PERMISSION_DENIED, event.error)

def invalid_jid(q, stream, conn):
    iq = call_create(q, conn, 'invalid.localhost')

    e = domish.Element((None, 'error'))
    e['type'] = 'cancel'
    e.addElement((ns.STANZA, 'jid-malformed'))
    send_error_reply(stream, iq, e)

    event = q.expect('dbus-error', method='CreateChannel')
    assertDBusError(cs.INVALID_ARGUMENT, event.error)

def really_invalid_jid(q, stream, conn):
    request = dbus.Dictionary(
        {
            cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_CONTACT_SEARCH,
            cs.CONTACT_SEARCH_SERVER: 'this is literally bullshit',
        }, signature='sv')
    call_async(q, conn.Requests, 'CreateChannel', request)

    # If the JID is actually malformed, we shouldn't even get as far as trying
    # to talk to it.
    event = q.expect('dbus-error', method='CreateChannel')

    assertDBusError(cs.INVALID_ARGUMENT, event.error)

def test(q, bus, conn, stream):
    not_a_search_server(q, stream, conn)
    returns_invalid_fields(q, stream, conn)
    returns_error_from_search(q, stream, conn)
    returns_bees_from_search(q, stream, conn)
    forbidden(q, stream, conn)
    invalid_jid(q, stream, conn)
    really_invalid_jid(q, stream, conn)
    disconnected_before_reply(q, stream, conn)

    stream.sendFooter()
    q.expect('dbus-return', method='Disconnect')

if __name__ == '__main__':
    exec_test(test)
