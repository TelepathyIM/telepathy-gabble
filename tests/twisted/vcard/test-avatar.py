
"""
Test avatar support.
"""

import base64

from servicetest import call_async, EventPattern
from gabbletest import exec_test, acknowledge_iq, make_result_iq
import constants as cs

def test(q, bus, conn, stream):
    conn.Connect()
    _, event = q.expect_many(
        EventPattern('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED]),
        EventPattern('stream-iq', to=None, query_ns='vcard-temp',
            query_name='vCard'))

    acknowledge_iq(stream, event.stanza)

    handle = conn.RequestHandles(1, ['bob@foo.com'])[0]
    call_async(q, conn.Avatars, 'RequestAvatar', handle, byte_arrays=True)

    event = q.expect('stream-iq', iq_type='get', to='bob@foo.com',
        query_ns='vcard-temp', query_name='vCard')
    result = make_result_iq(stream, event.stanza)
    photo = result.firstChildElement().addElement('PHOTO')
    photo.addElement('TYPE', content='image/png')
    photo.addElement('BINVAL', content=base64.b64encode('hello'))
    stream.send(result)

    q.expect('dbus-return', method='RequestAvatar',
        value=('hello', 'image/png'))

if __name__ == '__main__':
    exec_test(test)
