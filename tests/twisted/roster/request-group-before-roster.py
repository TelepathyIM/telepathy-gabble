"""
Regression test for a bug where RequestChannel times out when requesting a
group channel if the roster hasn't been received at the time of the call.
"""

from gabbletest import exec_test, sync_stream
from servicetest import sync_dbus, call_async
import constants as cs
import ns

def test(q, bus, conn, stream):
    conn.Connect()

    roster_event = q.expect('stream-iq', query_ns=ns.ROSTER)
    roster_event.stanza['type'] = 'result'

    call_async(q, conn, "RequestHandles", cs.HT_GROUP, ['test'])

    event = q.expect('dbus-return', method='RequestHandles')
    test_handle = event.value[0][0]

    call_async(q, conn, 'RequestChannel', cs.CHANNEL_TYPE_CONTACT_LIST,
        cs.HT_GROUP, test_handle, True)

    # A previous incarnation of this test --- written with the intention that
    # RequestChannel would be called before the roster was received, to expose
    # a bug in Gabble triggered by that ordering --- was racy: if the D-Bus
    # daemon happened to be particularly busy, the call to RequestChannel
    # reached Gabble after the roster stanza. (The race was discovered when
    # that reversed order triggered a newly-introduced instance of the
    # opposite bug to the one the test was targetting!) So we sync the XMPP
    # stream and D-Bus queue here.
    sync_stream(q, stream)
    sync_dbus(bus, q, conn)

    # send an empty roster
    stream.send(roster_event.stanza)

    event = q.expect('dbus-return', method='RequestChannel')
    path = event.value[0]

    while True:
        event = q.expect('dbus-signal', signal='NewChannel')
        assert event.args[0] == path, (event.args, path)
        _, type, handle_type, handle, suppress_handler = event.args
        if handle_type == cs.HT_GROUP and handle == test_handle:
            break

if __name__ == '__main__':
    exec_test(test)
