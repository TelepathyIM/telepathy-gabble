
"""
Test MUC support.
"""

import dbus

from twisted.words.xish import domish

from gabbletest import exec_test, make_result_iq
from servicetest import (EventPattern, assertEquals, assertLength,
        assertContains)
import constants as cs
import ns

from mucutil import join_muc

def test(q, bus, conn, stream):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])

    # 3x2x2 possible combinations of change_subject, send_first, moderator:
    # unrolling the loop here so we'll get better Python tracebacks on failure
    test_subject(q, bus, conn, stream, None, False, False)
    test_subject(q, bus, conn, stream, None, False, True)
    test_subject(q, bus, conn, stream, None, True, False)
    test_subject(q, bus, conn, stream, None, False, True)
    test_subject(q, bus, conn, stream, True, False, False)
    test_subject(q, bus, conn, stream, True, False, True)
    test_subject(q, bus, conn, stream, True, True, False)
    test_subject(q, bus, conn, stream, True, False, True)
    test_subject(q, bus, conn, stream, False, False, False)
    test_subject(q, bus, conn, stream, False, False, True)
    test_subject(q, bus, conn, stream, False, True, False)
    test_subject(q, bus, conn, stream, False, False, True)

counter = 0

def test_subject(q, bus, conn, stream, change_subject, send_first,
        moderator):
    # FIXME: fd.o#21152: using many different rooms here because the join_muc()
    # utility function (via request_muc_handle()) only copes with requesting
    # the handle for the first time, due to having to expect the disco#info
    # query to the server and reply to it. Fixing fd.o#21152 will remove the
    # distinction between the first and nth time, at which point we can just
    # join the same room repeatedly.
    global counter
    room = 'test%d@conf.localhost' % counter
    counter += 1

    room_handle, chan, path, props, disco = join_muc(q, bus, conn, stream,
            room,
            also_capture=[EventPattern('stream-iq', iq_type='get',
                query_name='query', query_ns=ns.DISCO_INFO, to=room)],
            role=(moderator and 'moderator' or 'participant'))

    # Until the disco returns, we appear to have no properties except subject.

    prop_list = chan.TpProperties.ListProperties()
    props = dict([(name, id) for id, name, sig, flags in prop_list])
    prop_flags = dict([(name, flags) for id, name, sig, flags in prop_list])

    for name in props:
        if name == 'subject':
            # subject can always be changed, until fd.o#13157 is fixed
            assertEquals(cs.PROPERTY_FLAG_WRITE, prop_flags[name])
        else:
            assertEquals(0, prop_flags[name])

    if send_first:
        # Someone sets a subject.
        message = domish.Element((None, 'message'))
        message['from'] = room + '/bob'
        message['type'] = 'groupchat'
        message.addElement('subject', content='Testing')
        stream.send(message)

        q.expect('dbus-signal', signal='PropertiesChanged',
                predicate=lambda e: (props['subject'], 'Testing') in e.args[0])
        e = q.expect('dbus-signal', signal='PropertyFlagsChanged',
                predicate=lambda e:
                    (props['subject'], cs.PROPERTY_FLAGS_RW) in e.args[0])
        assertContains((props['subject-contact'], cs.PROPERTY_FLAG_READ),
                e.args[0])
        assertContains((props['subject-timestamp'], cs.PROPERTY_FLAG_READ),
                e.args[0])

    # Reply to the disco
    iq = make_result_iq(stream, disco.stanza)
    query = iq.firstChildElement()
    x = query.addElement((ns.X_DATA, 'x'))
    x['type'] = 'result'

    feat = x.addElement('feature')
    feat['var'] = 'muc_public'

    if change_subject is not None:
        # When fd.o #13157 has been fixed, this will actually do something.
        field = x.addElement('field')
        field['var'] = 'muc#roomconfig_changesubject'
        field.addElement('value',
                content=(change_subject and 'true' or 'false'))

    stream.send(iq)

    # Someone sets a subject.
    message = domish.Element((None, 'message'))
    message['from'] = room + '/bob'
    message['type'] = 'groupchat'
    message.addElement('subject', content='lalala')
    stream.send(message)

    q.expect('dbus-signal', signal='PropertiesChanged',
            predicate=lambda e: (props['subject'], 'lalala') in e.args[0])

    # if send_first was true, then we already got this
    if not send_first:
        e = q.expect('dbus-signal', signal='PropertyFlagsChanged',
                predicate=lambda e:
                    (props['subject'], cs.PROPERTY_FLAGS_RW) in e.args[0])
        assertContains((props['subject-contact'], cs.PROPERTY_FLAG_READ),
                e.args[0])
        assertContains((props['subject-timestamp'], cs.PROPERTY_FLAG_READ),
                e.args[0])

    chan.Close()

    event = q.expect('stream-presence', to=room + '/test')
    elem = event.stanza
    assertEquals('unavailable', elem['type'])

if __name__ == '__main__':
    exec_test(test)
