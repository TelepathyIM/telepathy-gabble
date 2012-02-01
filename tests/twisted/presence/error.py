"""
Tests that Gabble provides at least some useful information from error
presences.
"""

from gabbletest import exec_test, make_presence, elem
from servicetest import assertEquals
import ns
import constants as cs

def test(q, bus, conn, stream):
    jids = ['gregory@unreachable.example.com',
            'thehawk@unreachable.example.net',
           ]
    gregory, hawk = jids
    gregory_handle, hawk_handle = conn.RequestHandles(cs.HT_CONTACT, jids)

    event = q.expect('stream-iq', query_ns=ns.ROSTER)
    event.stanza['type'] = 'result'
    for jid in jids:
        item = event.query.addElement('item')
        item['jid'] = jid
        item['subscription'] = 'both'

    stream.send(event.stanza)
    q.expect('dbus-signal', signal='PresencesChanged',
        args=[{gregory_handle: (cs.PRESENCE_OFFLINE, 'offline', ''),
               hawk_handle:    (cs.PRESENCE_OFFLINE, 'offline', ''),
              }
             ])

    # Our server can't resolve unreachable.example.com so it sends us an error
    # presence for Gregory. (This is what Prosody actually does.)
    presence = make_presence(gregory, type='error')
    error_text = u'Connection failed: DNS resolution failed'
    presence.addChild(
        elem('error', type='cancel')(
          elem(ns.STANZA, 'remote-server-not-found'),
          elem(ns.STANZA, 'text')(
            error_text
          )
        ))

    stream.send(presence)

    e = q.expect('dbus-signal', signal='PresencesChanged')
    presences, = e.args
    type_, status, message = presences[gregory_handle]
    assertEquals(cs.PRESENCE_ERROR, type_)
    assertEquals('error', status)
    assertEquals(error_text, message)

    # How about maybe the hawk's server is busted?
    presence = make_presence(hawk, type='error')
    presence.addChild(
        elem('error', type='cancel')(
          elem(ns.STANZA, 'internal-server-error'),
        ))
    stream.send(presence)

    e = q.expect('dbus-signal', signal='PresencesChanged')
    presences, = e.args
    type_, status, message = presences[hawk_handle]
    assertEquals(cs.PRESENCE_ERROR, type_)
    assertEquals('error', status)
    # FIXME: It might be less user-hostile to give some kind of readable
    # description of the error in future.
    assertEquals('internal-server-error', message)

if __name__ == '__main__':
    exec_test(test)
