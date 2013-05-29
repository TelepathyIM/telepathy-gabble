"""
fd.o #65036: connecting to legacy Jabber servers should respect
    require-encryption
"""

from servicetest import assertEquals
from gabbletest import exec_test, JabberXmlStream, JabberAuthenticator
import constants as cs

JID = 'alice@example.com'
PASSWORD = 's3kr1t'

def test_require_encryption(q, bus, conn, stream):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged',
        args=[cs.CONN_STATUS_CONNECTING, cs.CSR_REQUESTED])

    # FIXME: arrange to get a better error
    new = q.expect('dbus-signal', signal='ConnectionError')
    assertEquals(cs.NETWORK_ERROR, new.args[0])

    q.expect('dbus-signal', signal='StatusChanged',
        args=[cs.CONN_STATUS_DISCONNECTED, cs.CSR_NETWORK_ERROR])

if __name__ == '__main__':
    exec_test(test_require_encryption,
            {
                'password': PASSWORD,
                'account': JID,
                'require-encryption': True,
                'old-ssl': False,
                'resource': 'legacy-require-encryption',
            },
        protocol=JabberXmlStream,
        authenticator=JabberAuthenticator(JID.split('@')[0], PASSWORD),
        do_connect=False)
