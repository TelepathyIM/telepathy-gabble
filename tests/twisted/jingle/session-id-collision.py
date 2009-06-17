"""
Regression test for a bug where Gabble did not namespace session IDs by the
peer, leading to hilarity (and possible DOSing) if two peers picked the same
sid.
"""

from jingletest2 import JingleTest2, test_all_dialects

def test(jp, q, bus, conn, stream):
    jt1 = JingleTest2(jp, conn, q, stream, 'test@localhost',
        'edgar@collabora.co.uk/Monitor')
    jt2 = JingleTest2(jp, conn, q, stream, 'test@localhost',
        'wcc@collabora.co.uk/Pillow')

    jt1.prepare()
    jt2.send_presence_and_caps()

    # Two peers happen to pick the same Jingle session ID
    jt1.sid = '1'
    jt2.sid = '1'

    jt1.incoming_call()
    q.expect('dbus-signal', signal='NewChannel')

    # If Gabble confuses the two sessions, it'll NAK the IQ rather than
    # realising this is a new call.
    jt2.incoming_call()
    q.expect('dbus-signal', signal='NewChannel')

    # On the other hand, if the same person calls twice with the same sid,
    # Gabble _should_ NAK the second s-i.
    jt2.incoming_call()
    q.expect('stream-iq', iq_type='error',
        predicate=jp.action_predicate('session-initiate'))

if __name__ == '__main__':
    test_all_dialects(test)
