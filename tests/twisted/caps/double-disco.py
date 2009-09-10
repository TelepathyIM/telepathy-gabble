
from twisted.words.xish import xpath

from servicetest import EventPattern
from gabbletest import exec_test, make_presence, sync_stream
import constants as cs
import ns

def test(q, bus, conn, stream):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])

    contact = 'grapes@graze.box/delicious'
    presence = make_presence(contact, type='available', status='eat me!',
        caps={ 'node': 'oh:hai',
               'ver':  'thar',
             })
    thar_disco = EventPattern('stream-iq', to=contact,
        query_ns=ns.DISCO_INFO, query_node='oh:hai#thar')

    stream.send(presence)
    q.expect_many(thar_disco)

    # Okay, all good so far. But if we get the same caps node again from the
    # same contact, we shouldn't disco it again: we won't get any more trust
    # that way. This matters in practice, because Google's clients send a whole
    # bunch of presence stanzas in quick succession when they sign on.
    q.forbid_events([thar_disco])

    stream.send(presence)
    sync_stream(q, stream)

    # If we get a presence update from this contact with some new ext=''
    # bundles, we should disco those, but not the nodes we're already querying.
    presence = make_presence(contact, type='available', status='eat me!',
        caps={ 'node': 'oh:hai',
               'ver':  'thar',
               'ext':  'good-sir',
             })
    good_sir_disco = EventPattern('stream-iq', to=contact,
        query_ns=ns.DISCO_INFO, query_node='oh:hai#good-sir')
    stream.send(presence)

    q.expect_many(good_sir_disco)
    sync_stream(q, stream)

    # We should only disco ext='' attributes once per jid, too.
    q.forbid_events([good_sir_disco])
    stream.send(presence)
    sync_stream(q, stream)

if __name__ == '__main__':
    exec_test(test)
