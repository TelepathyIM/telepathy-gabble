
"""
Test that Gabble disconnects connection if it doesn't receive a response
to its service discovery request
"""

from gabbletest import exec_test, JabberXmlStream

def test(q, bus, conn, stream):
    conn.Connect()
    # connecting
    q.expect('dbus-signal', signal='StatusChanged', args=[1, 1])

    # We are disconnected with Connection_Status_Reason_Network_Error as the
    # disco request timed out
    q.expect('dbus-signal', signal='StatusChanged', args=[2, 2])

class JabberXmlStreamNoDiscoReply (JabberXmlStream):
    """Subclass JabberXmlStream to don't automatically send a disco reply"""
    def _cb_disco_iq (self, iq):
        pass

if __name__ == '__main__':
    jabber_xml_stream_no_disco_reply = JabberXmlStreamNoDiscoReply
    # telepathy-gabble-debug have been tweaked to timeout after 3 seconds
    exec_test(test, protocol=jabber_xml_stream_no_disco_reply)
