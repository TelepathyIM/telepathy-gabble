"""
Tests Contact Search channels to a simulated XEP-0055 service, without
passing the Server property
"""

import dbus

from twisted.words.xish import xpath

from gabbletest import exec_test, sync_stream, make_result_iq, acknowledge_iq, elem_iq, elem
from servicetest import EventPattern
from search_helper import call_create, answer_field_query

import constants as cs
import ns

JUD_SERVER = 'jud.localhost'

def test(q, bus, conn, stream):
    conn.Connect()

    _, iq_event, disco_event = q.expect_many(
        EventPattern('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED]),
        EventPattern('stream-iq', to=None, query_ns='vcard-temp',
            query_name='vCard'),
        EventPattern('stream-iq', to='localhost', query_ns=ns.DISCO_ITEMS))

    acknowledge_iq(stream, iq_event.stanza)

    requests = dbus.Interface(conn, cs.CONN_IFACE_REQUESTS)

    # no search server has been discovered yet. Requesting a search channel
    # without specifying the Server will fail
    call_create(q, requests, server=None)
    e = q.expect('dbus-error', method='CreateChannel')
    assert e.error.get_dbus_name() == cs.INVALID_ARGUMENT

    # reply to IQ query
    reply = make_result_iq(stream, disco_event.stanza)
    query = xpath.queryForNodes('/iq/query', reply)[0]
    item = query.addElement((None, 'item'))
    item['jid'] = JUD_SERVER
    stream.send(reply)

    # wait for the disco#info query
    event = q.expect('stream-iq', to=JUD_SERVER, query_ns=ns.DISCO_INFO)

    reply = elem_iq(stream, 'result', id=event.stanza['id'], from_=JUD_SERVER)(
        elem(ns.DISCO_INFO, 'query')(
            elem('identity', category='directory', type='user', name='vCard User Search')(),
            elem('feature', var=ns.SEARCH)()))

    stream.send(reply)

    # Make sure Gabble's received the reply
    sync_stream(q, stream)

    call_create(q, requests, server=None)

    # JUD_SERVER is used as default
    answer_field_query(q, stream, JUD_SERVER)

if __name__ == '__main__':
    exec_test(test)
