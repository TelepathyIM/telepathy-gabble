"""
Test that contacts we're subscribed to have their presence go from unknown to
offline when we get the roster, even if we've got (unavailable) presence for
them before we receive the roster; and that receiving available presence from a
contact before we get the roster also works.

This serves as a regression test for
<https://bugs.freedesktop.org/show_bug.cgi?id=38603>, among other bugs.
"""

from gabbletest import exec_test, make_presence, sync_stream, elem
from servicetest import assertEquals, EventPattern, sync_dbus

import constants as cs
import ns

from twisted.words.xish import domish

AVAILABLE = (cs.PRESENCE_AVAILABLE, u'available', u'')
OFFLINE = (cs.PRESENCE_OFFLINE, u'offline', u'')
UNKNOWN = (cs.PRESENCE_UNKNOWN, u'unknown', u'')

def make_roster_item(jid, subscription):
    item = domish.Element((None, 'item'))
    item['jid'] = jid
    item['subscription'] = subscription
    return item

def test(q, bus, conn, stream):
    event = q.expect('stream-iq', query_ns=ns.ROSTER)

    amy, bob, che, dre, eve = conn.RequestHandles(cs.HT_CONTACT,
        ['amy@foo.com', 'bob@foo.com', 'che@foo.com', 'dre@foo.com',
         'eve@foo.com'])
    assertEquals({amy: UNKNOWN,
                  bob: UNKNOWN,
                  che: UNKNOWN,
                  dre: UNKNOWN,
                  eve: UNKNOWN,
                 },
        conn.SimplePresence.GetPresences([amy, bob, che, dre, eve]))

    # Before the server sends Gabble the roster, it relays an 'unavailable'
    # presence for one of the contacts we're subscribed to. This seems to
    # happen in practice when using Prosody with a shared roster: the presence
    # probes start coming back negatively before the shared roster is retrieved
    # and returned to the client.
    stream.send(make_presence('dre@foo.com', type='unavailable'))

    # Dre's presence is still unknown, since we don't have the roster. This
    # isn't a change per se---we checked above, and Dre's presence was
    # unknown---so it shouldn't be signalled.
    q.forbid_events([EventPattern('dbus-signal', signal='PresencesChanged',
        args=[{dre: UNKNOWN}])])

    # We also receive an available presence from Eve before the roster arrives:
    # this presence should behave normally.
    stream.send(make_presence('eve@foo.com'))
    q.expect('dbus-signal', signal='PresencesChanged', args=[{eve: AVAILABLE}])

    # We also get a message from a contact before we get the roster (presumably
    # they sent this while we were offline?). This shouldn't affect the contact
    # being reported as offline when we finally do get the roster, but it used
    # to: <https://bugs.freedesktop.org/show_bug.cgi?id=41743>.
    stream.send(
        elem('message', from_='amy@foo.com', type='chat')(
          elem('body')(u'why are you never online?')
        ))
    q.expect('dbus-signal', signal='MessageReceived')

    event.stanza['type'] = 'result'
    event.query.addChild(make_roster_item('amy@foo.com', 'both'))
    event.query.addChild(make_roster_item('bob@foo.com', 'from'))
    event.query.addChild(make_roster_item('che@foo.com', 'to'))
    event.query.addChild(make_roster_item('dre@foo.com', 'both'))
    event.query.addChild(make_roster_item('eve@foo.com', 'both'))
    stream.send(event.stanza)

    # The presence for contacts on the roster whose subscription is 'to' or
    # 'both' but for whom we haven't already received presence should change
    # from 'unknown' (as checked above) to 'offline'.
    e = q.expect('dbus-signal', signal='PresencesChanged')
    changed_presences, = e.args
    assertEquals(
        {amy: OFFLINE,
         che: OFFLINE,
         dre: OFFLINE,
        },
        changed_presences)

    assertEquals({amy: OFFLINE,
                  bob: UNKNOWN,
                  che: OFFLINE,
                  dre: OFFLINE,
                  eve: AVAILABLE,
                 },
        conn.SimplePresence.GetPresences([amy, bob, che, dre, eve]))

if __name__ == '__main__':
    exec_test(test)
