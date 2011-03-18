"""
Tests the server refusing to let us join a MUC. This is a regression test for
<https://bugs.freedesktop.org/show_bug.cgi?id=35120> where Gabble would crash
in this situation.
"""

from gabbletest import exec_test, elem
from mucutil import try_to_join_muc
import constants as cs
import ns

MUC = 'deerhoof@evil.lit'

def test(q, bus, conn, stream):
    try_to_join_muc(q, bus, conn, stream, MUC)

    stream.send(
        elem('presence', from_=MUC, type='error')(
          elem(ns.MUC, 'x'),
          elem('error', code='403', type='auth')(
            elem(ns.STANZA, 'forbidden'),
            elem(ns.STANZA, 'text')(
              u'Access denied by service policy',
            )
          )
        ))

    q.expect('dbus-error', method='CreateChannel', name=cs.BANNED)

if __name__ == '__main__':
    exec_test(test)
