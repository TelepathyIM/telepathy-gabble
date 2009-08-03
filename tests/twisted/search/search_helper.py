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
