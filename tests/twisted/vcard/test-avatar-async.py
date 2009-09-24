
"""
Test support for retrieving avatars asynchronously using RequestAvatars.
"""

import base64
import hashlib

from twisted.words.xish import domish
from servicetest import EventPattern, sync_dbus
from gabbletest import (exec_test, acknowledge_iq, make_result_iq, 
    sync_stream, send_error_reply)
import constants as cs
import ns

def test(q, bus, conn, stream):
    conn.Connect()
    _, iq_event = q.expect_many(
        EventPattern('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED]),
        EventPattern('stream-iq', to=None, query_ns='vcard-temp',
            query_name='vCard'))

    acknowledge_iq(stream, iq_event.stanza)

    handle = conn.RequestHandles(1, ['bob@foo.com'])[0]
    conn.Avatars.RequestAvatars([handle])

    iq_event = q.expect('stream-iq', to='bob@foo.com', query_ns='vcard-temp',
        query_name='vCard')
    iq = make_result_iq(stream, iq_event.stanza)
    vcard = iq.firstChildElement()
    photo = vcard.addElement('PHOTO')
    photo.addElement('TYPE', content='image/png')
    photo.addElement('BINVAL', content=base64.b64encode('hello'))
    stream.send(iq)

    event = q.expect('dbus-signal', signal='AvatarRetrieved')
    assert event.args[0] == handle
    assert event.args[1] == hashlib.sha1('hello').hexdigest()
    assert event.args[2] == 'hello'
    assert event.args[3] == 'image/png'

    # Request again; this request should be satisfied from the avatar cache.
    conn.Avatars.RequestAvatars([handle])

    event = q.demand('dbus-signal', signal='AvatarRetrieved')
    assert event.args[0] == handle
    assert event.args[1] == hashlib.sha1('hello').hexdigest()
    assert event.args[2] == 'hello'
    assert event.args[3] == 'image/png'

    # Request another vCard and get resource-constraint
    handle = conn.RequestHandles(1, ['jean@busy-server.com'])[0]
    conn.Avatars.RequestAvatars([handle])

    iq_event = q.expect('stream-iq', to='jean@busy-server.com', query_ns='vcard-temp',
        query_name='vCard')
    iq = iq_event.stanza
    error = domish.Element((None, 'error'))
    error['code'] = '500'
    error['type'] = 'wait'
    error.addElement((ns.STANZA, 'resource-constraint'))
    send_error_reply(stream, iq, error)

    avatar_retrieved_event = EventPattern('dbus-signal',
            signal='AvatarRetrieved')
    avatar_request_event = EventPattern('stream-iq', query_ns='vcard-temp')
    q.forbid_events([avatar_retrieved_event, avatar_request_event])

    # Request the same vCard again during the delay
    conn.Avatars.RequestAvatars([handle])
    sync_stream(q, stream)
    sync_dbus(bus, q, conn)
    q.unforbid_events([avatar_retrieved_event, avatar_request_event])
    
if __name__ == '__main__':
    exec_test(test)
