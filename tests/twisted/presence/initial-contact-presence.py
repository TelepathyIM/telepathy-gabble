"""
Test that contacts we're subscribed to have their presence go from unknown to
offline when we get the roster.
"""

from gabbletest import exec_test
from servicetest import assertEquals

import constants as cs
import ns

from twisted.words.xish import domish

def make_roster_item(jid, subscription):
    item = domish.Element((None, 'item'))
    item['jid'] = jid
    item['subscription'] = subscription
    return item

def test(q, bus, conn, stream):
    event = q.expect('stream-iq', query_ns=ns.ROSTER)

    amy, bob, che = conn.RequestHandles(cs.HT_CONTACT,
        ['amy@foo.com', 'bob@foo.com', 'che@foo.com'])
    assertEquals({amy: (cs.PRESENCE_UNKNOWN, u'unknown', u''),
                  bob: (cs.PRESENCE_UNKNOWN, u'unknown', u''),
                  che: (cs.PRESENCE_UNKNOWN, u'unknown', u'')},
        conn.SimplePresence.GetPresences([amy, bob, che]))

    event.stanza['type'] = 'result'
    event.query.addChild(make_roster_item('amy@foo.com', 'both'))
    event.query.addChild(make_roster_item('bob@foo.com', 'from'))
    event.query.addChild(make_roster_item('che@foo.com', 'to'))
    stream.send(event.stanza)

    # The presence for contacts on the roster whose subscription is 'to' or
    # 'both' should change from 'unknown' (as checked above) to 'offline'.
    e = q.expect('dbus-signal', signal='PresencesChanged')
    changed_presences, = e.args
    assertEquals(
        {amy: (cs.PRESENCE_OFFLINE, u'offline', u''),
         che: (cs.PRESENCE_OFFLINE, u'offline', u''),
        },
        changed_presences)

    assertEquals({amy: (cs.PRESENCE_OFFLINE, u'offline', u''),
                  bob: (cs.PRESENCE_UNKNOWN, u'unknown', u''),
                  che: (cs.PRESENCE_OFFLINE, u'offline', u'')},
        conn.SimplePresence.GetPresences([amy, bob, che]))

if __name__ == '__main__':
    exec_test(test)
