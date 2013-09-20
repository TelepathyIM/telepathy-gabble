
"""
Test the case where the vCard get made prior to a vCard set fails.
"""

from servicetest import call_async, EventPattern
from gabbletest import (
    acknowledge_iq, elem, exec_test, make_result_iq, sync_stream)
import constants as cs
import ns

def test(q, bus, conn, stream):
    event = q.expect('stream-iq', to=None, query_ns='vcard-temp',
            query_name='vCard')

    acknowledge_iq(stream, event.stanza)
    # Force Gabble to process the vCard before calling any methods.
    sync_stream(q, stream)

    handle = conn.Properties.Get(cs.CONN, "SelfHandle")
    call_async(q, conn.Avatars, 'SetAvatar', 'william shatner',
        'image/x-actor-name')

    event = q.expect('stream-iq', iq_type='get', to=None,
        query_ns='vcard-temp', query_name='vCard')
    reply = make_result_iq(stream, event.stanza)
    reply['type'] = 'error'
    reply.addChild(elem('error')(
        elem(ns.STANZA, 'forbidden')(),
        elem(ns.STANZA, 'text')(u'zomg whoops')))
    stream.send(reply)

    event = q.expect('dbus-error', method='SetAvatar', name=cs.NOT_AVAILABLE)

if __name__ == '__main__':
    exec_test(test)
