"""
Regression test for <http://bugs.freedesktop.org/show_bug.cgi?id=25341>, where
Gabble redundantly re-set the user's own vCard even if nothing had changed.
"""

from servicetest import EventPattern, sync_dbus
from gabbletest import exec_test, make_result_iq, sync_stream, GoogleXmlStream
import constants as cs

def not_google(q, bus, conn, stream):
    test(q, bus, conn, stream, False)

def google(q, bus, conn, stream):
    test(q, bus, conn, stream, True)

def test(q, bus, conn, stream, is_google):
    conn.Connect()
    _, iq_event = q.expect_many(
        EventPattern('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED]),
        EventPattern('stream-iq', to=None, query_ns='vcard-temp',
            query_name='vCard'))

    result = make_result_iq(stream, iq_event.stanza)

    # Testing reveals that Google's vCard server does not actually support
    # NICKNAME (or indeed any fields beside FN, N and PHOTO): if you set a
    # vCard including it, it accepts the request but strips out the unsupported
    # fields. So if the server looks like Google, it's a redundant set
    # operation on FN that we want to avoid.
    if is_google:
        vcard = result.firstChildElement()
        vcard.addElement('FN', content='oh hello there')
    else:
        vcard = result.firstChildElement()
        vcard.addElement('NICKNAME', content='oh hello there')

    stream.send(result)

    q.forbid_events([
        EventPattern('stream-iq', iq_type='set', query_ns='vcard-temp',
            query_name='vCard')
        ])
    sync_stream(q, stream)
    sync_dbus(bus, q, conn)

if __name__ == '__main__':
    exec_test(not_google, params={ 'alias': 'oh hello there' })
    exec_test(google, params={ 'alias': 'oh hello there' },
        protocol=GoogleXmlStream)
