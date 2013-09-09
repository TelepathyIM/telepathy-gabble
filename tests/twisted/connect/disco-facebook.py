"""
Test that Gabble is tolerant of non-RFC-compliance from Facebook.
https://bugs.freedesktop.org/show_bug.cgi?id=68829
"""

from gabbletest import exec_test, XmppXmlStream
import constants as cs
import ns

from twisted.words.xish import xpath

def test(q, bus, conn, stream):
    conn.Connect()

    # everything is fine and actually very boring
    q.expect('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTING, cs.CSR_REQUESTED]),
    q.expect('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])

class XmppXmlStreamFacebook201309(XmppXmlStream):
    """As of 2013-09, a new version of Facebook's XMPP server (used
    consistently for beta.chat.facebook.com, and gradually being rolled out
    for chat.facebook.com users) omits the 'from' attribute in its disco
    reply. The disco reply is otherwise correct.
    """

    def _cb_disco_iq(self, iq):
        nodes = xpath.queryForNodes(
            "/iq/query[@xmlns='" + ns.DISCO_INFO + "']", iq)
        query = nodes[0]

        for feature in self.disco_features:
            query.addChild(elem('feature', var=feature))

        iq['type'] = 'result'

        # The Facebook server's IQ responses have neither 'from' nor 'to'
        try:
            del iq['from']
        except KeyError:
            pass
        try:
            del iq['to']
        except KeyError:
            pass

        self.send(iq)

if __name__ == '__main__':
    exec_test(test, protocol=XmppXmlStreamFacebook201309, do_connect=False)
