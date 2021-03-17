
"""
Test that gabble emits only one AvatarRetrieved for multiple queued
RequestAvatar calls for the same contact.
"""

import base64

from servicetest import EventPattern
from gabbletest import exec_test, acknowledge_iq, make_result_iq
import constants as cs

def test(q, bus, conn, stream):
    iq_event = q.expect('stream-iq', to=None, query_ns='vcard-temp',
            query_name='vCard')

    acknowledge_iq(stream, iq_event.stanza)

    handle = conn.get_contact_handle_sync('bob@foo.com')
    conn.Avatars.RequestAvatars([handle])
    conn.Avatars.RequestAvatars([handle])
    conn.Avatars.RequestAvatars([handle])
    conn.Avatars.RequestAvatars([handle])

    iq_event = q.expect('stream-iq', to='bob@foo.com', query_ns='vcard-temp',
        query_name='vCard')
    iq = make_result_iq(stream, iq_event.stanza)
    vcard = iq.firstChildElement()
    photo = vcard.addElement('PHOTO')
    photo.addElement('TYPE', content='image/png')
    photo.addElement('BINVAL', content=base64.b64encode(b'hello').decode())
    stream.send(iq)

    event = q.expect('dbus-signal', signal='AvatarRetrieved')

    q.forbid_events([EventPattern('dbus-signal', signal='AvatarRetrieved')])

if __name__ == '__main__':
    exec_test(test)

