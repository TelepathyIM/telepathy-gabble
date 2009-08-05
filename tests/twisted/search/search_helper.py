import dbus
from twisted.words.protocols.jabber.client import IQ

from servicetest import call_async, EventPattern
import constants as cs
import ns

def call_create(q, requests, server):
    request = dbus.Dictionary(
        {
            cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_CONTACT_SEARCH,
        }, signature='sv')

    if server is not None:
        request[cs.CONTACT_SEARCH_SERVER] = server

    call_async(q, requests, 'CreateChannel', request)

def _wait_for_server_query(q, stream, server):
    # Gabble asks the server what search fields it supports
    iq_event = q.expect('stream-iq', to=server, query_ns=ns.SEARCH)
    iq = iq_event.stanza

    result = IQ(stream, "result")
    result["id"] = iq["id"]
    query = result.addElement((ns.SEARCH, 'query'))
    query.addElement("instructions", content="cybar?")

    return result, query

def _send_server_reply(q, stream, result):
    stream.send(result)

    ret = q.expect('dbus-return', method='CreateChannel')
    nc_sig = q.expect('dbus-signal', signal='NewChannels')

    return (ret, nc_sig)

def answer_field_query(q, stream, server):
    result, query = _wait_for_server_query(q, stream, server)

    # The server says it supports all the fields in unextended XEP 0055
    for f in ["first", "last", "nick", "email"]:
        query.addElement(f)

    return _send_server_reply(q, stream, result)

def answer_extended_field_query(q, stream, server, fields):
    result, query = _wait_for_server_query(q, stream, server)

    x = query.addElement((ns.X_DATA, 'x'))
    x['type'] = 'form'
    x.addElement('title', content="User Directory Search")
    x.addElement('instructions', content="mooh?")
    # add FORM_TYPE
    field = x.addElement('field')
    field['type'] = 'hidden'
    field['var'] = 'FORM_TYPE'
    field.addElement('value', content=ns.SEARCH)

    # add fields
    for var, type, label, options in fields:
        field = x.addElement('field')
        field['var'] = var
        field['type'] = type
        field['label'] = label

        # add options (if any)
        for value, label in options:
            option = field.addElement('option')
            option['label'] = label
            v = option.addElement('value', content=value)

    return _send_server_reply(q, stream, result)

def make_search(q, c_search, c_props, server, terms):
    call_async(q, c_search, 'Search', terms)

    _, ssc_event, iq_event = q.expect_many(
        EventPattern('dbus-return', method='Search'),
        EventPattern('dbus-signal', signal='SearchStateChanged'),
        EventPattern('stream-iq', to=server, query_ns=ns.SEARCH, iq_type='set'),
        )

    assert ssc_event.args[0] == cs.SEARCH_IN_PROGRESS

    state = c_props.Get(cs.CHANNEL_TYPE_CONTACT_SEARCH, 'SearchState')
    assert state == cs.SEARCH_IN_PROGRESS, state

    return iq_event.stanza

def send_results(stream, iq, results):
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

def send_results_extended(stream, iq, results, fields):
    result = IQ(stream, 'result')
    result['id'] = iq['id']
    query = result.addElement((ns.SEARCH, 'query'))

    x = query.addElement((ns.X_DATA, 'x'))
    x['type'] = 'result'

    x.addElement('title', content='Search result')

    # add reported fields
    reported = x.addElement('reported')
    for var, type, label, options in fields:
        field = reported.addElement('field')
        field['var'] = var
        field['label'] = label

    # add results
    for r in results:
        item = x.addElement('item')
        for var, value in r.items():
            field = item.addElement('field')
            field['var'] = var
            field.addElement('value', content=value)

    stream.send(result)
