"""
Test that Gabble disconnects connection if it doesn't receive a response
to its initial service discovery request to the server
"""

from gabbletest import exec_test, XmppXmlStream
import constants as cs

def test(q, bus, conn, stream):
    conn.Connect()
    # connecting
    q.expect('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTING, cs.CSR_REQUESTED]),

    # We are disconnected with Connection_Status_Reason_Network_Error as the
    # disco request timed out
    q.expect('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_DISCONNECTED, cs.CSR_NETWORK_ERROR]),

class JabberXmlStreamNoDiscoReply(XmppXmlStream):
    """Subclass XmppXmlStream not to respond to disco requests to the server."""
    def _cb_disco_iq (self, iq):
        pass

if __name__ == '__main__':
    # telepathy-gabble-debug has been tweaked to time out after 3 seconds
    exec_test(test, protocol=JabberXmlStreamNoDiscoReply, do_connect=False)
