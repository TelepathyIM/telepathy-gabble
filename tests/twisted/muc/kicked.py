"""
Tests the user being kicked from a MUC. Another symptom of the underlying bug
behind <https://bugs.freedesktop.org/show_bug.cgi?id=35120> was that this would
crash.
"""

from servicetest import assertEquals, assertContains
from gabbletest import exec_test, elem
from mucutil import join_muc
import constants as cs
import ns

MUC = 'deerhoof@evil.lit'

def test(q, bus, conn, stream):
    # The user happily joins a MUC
    _, chan, _, _ = join_muc(q, bus, conn, stream, MUC)
    muc_self_handle = chan.Properties.Get(cs.CHANNEL_IFACE_GROUP,
            "SelfHandle")
    muc_self_jid, = conn.InspectHandles(cs.HT_CONTACT, [muc_self_handle])

    # But then Bob kicks us.
    bob_jid = '%s/bob' % MUC
    bob_handle, = conn.RequestHandles(cs.HT_CONTACT, [bob_jid])
    stream.send(
        elem('presence', from_=muc_self_jid, type='unavailable')(
          elem(ns.MUC_USER, 'x')(
            elem('item', affiliation='none', role='none')(
              elem('actor', jid=bob_jid),
              elem('reason')(
                u'bye'
              )
            ),
            elem('status', code='307'),
          )
        ))

    mcd_event = q.expect('dbus-signal', signal='MembersChangedDetailed')
    added, removed, local_pending, remote_pending, details = mcd_event.args
    assertEquals([], added)
    assertEquals([muc_self_handle], removed)
    assertEquals([], local_pending)
    assertEquals([], remote_pending)
    assertContains('actor', details)
    assertEquals(bob_handle, details['actor'])
    assertEquals(cs.GC_REASON_KICKED, details['change-reason'])
    assertEquals('bye', details['message'])

    q.expect('dbus-signal', signal='ChannelClosed')

if __name__ == '__main__':
    exec_test(test)
