"""
Test handling of errors from StreamHandler during calls. This is a regression
test for a bug introduced by 54021cee0ad38 which removed an idle callback
masking refcounting assumptions.
"""

from functools import partial

from gabbletest import exec_test
from servicetest import make_channel_proxy
import jingletest

import constants as cs

def test(q, bus, conn, stream, call_error_on):
    jt = jingletest.JingleTest(stream, 'test@localhost', 'foo@bar.com/Foo')

    conn.Connect()

    q.expect('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])

    remote_handle = conn.RequestHandles(1, ["foo@bar.com/Foo"])[0]

    # Remote end calls us
    jt.incoming_call()

    # FIXME: these signals are not observable by real clients, since they
    #        happen before NewChannels.
    # The caller is in members
    e = q.expect('dbus-signal', signal='MembersChanged',
             args=[u'', [remote_handle], [], [], [], 0, 0])

    # We're pending because of remote_handle
    e = q.expect('dbus-signal', signal='MembersChanged',
             args=[u'', [], [], [1L], [], remote_handle, cs.GC_REASON_INVITED])

    media_chan_suffix = e.path

    e = q.expect('dbus-signal', signal='NewSessionHandler')
    session_handler = make_channel_proxy(conn, e.args[0], 'Media.SessionHandler')

    if call_error_on == 'session':
        session_handler.Error(0, "this has been deprecated for years")
    else:
        session_handler.Ready()

        e = q.expect('dbus-signal', signal='NewStreamHandler')

        # S-E gets notified about a newly-created stream
        stream_handler = make_channel_proxy(conn, e.args[0], 'Media.StreamHandler')

        # Something goes wrong immediately!
        stream_handler.Error(0, "i'll have the eggs tostada please")

    # Gabble doesn't fall over, and the channel closes nicely.
    e = q.expect('dbus-signal', signal='Closed', path=media_chan_suffix)

if __name__ == '__main__':
    exec_test(partial(test, call_error_on='stream'))
    exec_test(partial(test, call_error_on='session'))
