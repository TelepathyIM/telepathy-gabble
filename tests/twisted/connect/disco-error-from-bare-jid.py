"""
When we connect, Gabble sends a disco request both to the server, and to our
own bare JID (to find out whether we support PEP). Some servers return errors
from the latter; Gabble should ignore the error, and connect anyway.

This tests <https://bugs.freedesktop.org/show_bug.cgi?id=28599>
"""

from gabbletest import exec_test, XmppXmlStream, elem, send_error_reply
import constants as cs
import ns

def test(q, bus, conn, stream):
    conn.Connect()

    # We start connecting...
    q.expect('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTING, cs.CSR_REQUESTED]),

    # ...and then we finish connecting, despite the server having got upset
    # when we auto-discoed. Party on!
    q.expect('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])

class BareJidDiscoErrorXmlStream(XmppXmlStream):
    def _cb_bare_jid_disco_iq(self, iq):
        """
        \o\ /o/ <o/ \o> /o> ...
        "Crap! It's the cops! Turn the music off!"
        """
        send_error_reply(self, iq,
            error_stanza=elem('error')(
                elem(ns.STANZA, 'feature-not-implemented'),
                elem(ns.STANZA, 'text')(
                    u'No, officer! This is certainly not an illegal party.')
                ))

if __name__ == '__main__':
    exec_test(test, protocol=BareJidDiscoErrorXmlStream)
