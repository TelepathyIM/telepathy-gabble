
"""
Test that gabble emits only one AvatarRetrieved for multiple queued
RequestAvatar calls for the same contact.
"""

import base64
import sha

from servicetest import EventPattern
from gabbletest import exec_test, acknowledge_iq

def test(q, bus, conn, stream):
    conn.Connect()
    _, iq_event = q.expect_many(
        EventPattern('dbus-signal', signal='StatusChanged', args=[0, 1]),
        EventPattern('stream-iq', to=None, query_ns='vcard-temp',
            query_name='vCard'))

    acknowledge_iq(stream, iq_event.stanza)

    handle = conn.RequestHandles(1, ['bob@foo.com'])[0]
    conn.Avatars.RequestAvatars([handle])
    conn.Avatars.RequestAvatars([handle])
    conn.Avatars.RequestAvatars([handle])
    conn.Avatars.RequestAvatars([handle])

    iq_event = q.expect('stream-iq', to='bob@foo.com', query_ns='vcard-temp',
        query_name='vCard')
    iq = iq_event.stanza
    vcard = iq_event.query
    photo = vcard.addElement('PHOTO')
    photo.addElement('TYPE', content='image/png')
    photo.addElement('BINVAL', content=base64.b64encode('hello'))
    iq['type'] = 'result'
    stream.send(iq)

    event = q.expect('dbus-signal', signal='AvatarRetrieved')

    conn.Disconnect()

    e = q.expect('dbus-signal')

    # We really need q.forbid(...) somehow
    assert e.signal != 'AvatarRetrieved'

    assert e.signal == 'StatusChanged'
    assert e.args == [2, 1]


if __name__ == '__main__':
    exec_test(test)

