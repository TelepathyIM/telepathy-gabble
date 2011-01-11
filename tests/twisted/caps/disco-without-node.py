"""
Test that Gabble responds to disco#info queries without a node='' attribute.
"""

from servicetest import assertEquals
from gabbletest import exec_test, elem_iq, elem
import constants as cs
import ns

def test(q, bus, conn, stream):
    jid = 'foo@bar.com'

    iq = elem_iq(stream, 'get', from_=jid)(elem(ns.DISCO_INFO, 'query'))
    stream.send(iq)

    event = q.expect('stream-iq', iq_type='result', to='foo@bar.com')
    assertEquals(iq['id'], event.stanza['id'])

if __name__ == '__main__':
    exec_test(test)
