"""
Check that Gabble always responds to disco for voice-v1 and video-v1. They have
hard-coded contents, because they only exist for compatibility with
Google Talk, Google Video Chat, and old versions of Gabble.

In particular, if the appropriate capabilities are not enabled (as in this
test), doing disco on the bundles still gives their contents.
"""

import dbus

from twisted.words.xish import xpath, domish

from servicetest import EventPattern, assertEquals
from gabbletest import exec_test, elem, elem_iq
import constants as cs
import ns

def disco_bundle(q, bus, conn, stream, node, features):
    request = \
        elem_iq(stream, 'get', from_='fake_contact@jabber.org/resource')(
          elem(ns.DISCO_INFO, 'query', node=node)
        )
    stream.send(request)

    disco_response = q.expect('stream-iq', query_ns=ns.DISCO_INFO,
            iq_id=request['id'])
    nodes = xpath.queryForNodes('/iq/query/feature', disco_response.stanza)
    vars = [n["var"] for n in nodes]
    assertEquals(set(features), set(vars))

def run_test(q, bus, conn, stream):
    conn.Connect()
    event_stream = q.expect('stream-presence')

    c_nodes = xpath.queryForNodes('/presence/c', event_stream.stanza)
    assert c_nodes is not None
    assert len(c_nodes) == 1
    node = c_nodes[0].attributes['node']

    disco_bundle(q, bus, conn, stream, node + '#voice-v1',
            set([ns.GOOGLE_FEAT_VOICE]))
    disco_bundle(q, bus, conn, stream, node + '#video-v1',
            set([ns.GOOGLE_FEAT_VIDEO]))

if __name__ == '__main__':
    exec_test(run_test)
