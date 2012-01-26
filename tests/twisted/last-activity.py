"""
Trivial smoke-test for XEP-0012 support.
"""
from servicetest import assertEquals, assertContains
from gabbletest import exec_test, elem, elem_iq
import ns

def test(q, bus, conn, stream):
    e = q.expect('stream-iq', iq_type='get', query_ns=ns.ROSTER)
    e.stanza['type'] = 'result'
    e.query.addChild(
        elem('item', jid='romeo@montague.lit', subscription='both'))
    stream.send(e.stanza)

    # Romeo's on the roster.
    stream.send(
        elem_iq(stream, 'get', from_='romeo@montague.lit')(
          elem(ns.LAST, 'query')
        )
      )
    e = q.expect('stream-iq', iq_type='result',
        query_ns=ns.LAST, query_name='query')
    # No real assertions about the number of seconds; this is just a smoke
    # test.
    seconds = e.query['seconds']
    assert seconds >= 0

    # Juliet is not.
    stream.send(
        elem_iq(stream, 'get', from_='juliet@capulet.lit')(
          elem(ns.LAST, 'query')
        )
      )
    e = q.expect('stream-iq', iq_type='error',
        query_ns=ns.LAST, query_name='query')
    # Yuck.
    assertEquals('forbidden', e.stanza.children[1].children[0].name)

    # If the server asks, Gabble had better not crash.
    stream.send(
        elem_iq(stream, 'get')(
          elem(ns.LAST, 'query')
        )
      )
    e = q.expect('stream-iq', iq_type='result',
        query_ns=ns.LAST, query_name='query')

if __name__ == '__main__':
    exec_test(test)
