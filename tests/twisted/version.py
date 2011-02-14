# vim: set encoding=utf-8 :
"""
Tests Gabble's implementation of XEP-0092.
"""

from twisted.words.xish import xpath
from servicetest import assertLength, assertEquals
from gabbletest import exec_test, elem_iq, elem
import ns

def test(q, bus, conn, stream):
    request = elem_iq(stream, 'get')(
      elem(ns.VERSION, 'query')
    )

    stream.send(request)
    reply = q.expect('stream-iq', iq_id=request['id'],
        query_ns=ns.VERSION, query_name='query')

    # Both <name/> and <version/> are REQUIRED. What they actually contain is
    # someone else's problem™.
    names = xpath.queryForNodes('/query/name', reply.query)
    assertLength(1, names)

    versions = xpath.queryForNodes('/query/version', reply.query)
    assertLength(1, versions)

    # <os/> is OPTIONAL. “Revealing the application's underlying operating
    # system may open the user or system to attacks directed against that
    # operating system; therefore, an application MUST provide a way for a
    # human user or administrator to disable sharing of information about the
    # operating system.” The “way” that we provide is never to send it.
    oss = xpath.queryForNodes('/query/os', reply.query)
    assert oss is None

if __name__ == '__main__':
    exec_test(test)
