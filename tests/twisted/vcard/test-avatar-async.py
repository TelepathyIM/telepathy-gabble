
"""
Test support for retrieving avatars asynchronously using RequestAvatars.
"""

import base64
import hashlib

from twisted.words.xish import domish
from servicetest import EventPattern, sync_dbus, assertEquals
from gabbletest import (exec_test, acknowledge_iq, make_result_iq, 
    sync_stream, send_error_reply, make_presence)
import constants as cs
import ns
import dbus

avatar_retrieved_event = EventPattern('dbus-signal', signal='AvatarRetrieved')
avatar_request_event = EventPattern('stream-iq', query_ns='vcard-temp')

def test_get_avatar(q, bus, conn, stream, contact, handle, in_cache=False):
    conn.Avatars.RequestAvatars([handle])

    if in_cache:
        q.forbid_events([avatar_request_event])
    else:
        iq_event = q.expect('stream-iq', to=contact, query_ns='vcard-temp',
            query_name='vCard')
        iq = make_result_iq(stream, iq_event.stanza)
        vcard = iq.firstChildElement()
        photo = vcard.addElement('PHOTO')
        photo.addElement('TYPE', content='image/png')
        photo.addElement('BINVAL', content=base64.b64encode('hello'))
        stream.send(iq)

    event = q.expect('dbus-signal', signal='AvatarRetrieved')
    assertEquals(handle, event.args[0])
    assertEquals(hashlib.sha1('hello').hexdigest(), event.args[1])
    assertEquals('hello', event.args[2])
    assertEquals('image/png', event.args[3])

    if in_cache:
        sync_stream(q, stream)
        q.unforbid_events([avatar_request_event])

def test(q, bus, conn, stream):
    conn.Connect()
    _, iq_event = q.expect_many(
        EventPattern('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED]),
        EventPattern('stream-iq', to=None, query_ns='vcard-temp',
            query_name='vCard'))

    # When we start, there is no avatar
    acknowledge_iq(stream, iq_event.stanza)
    self_handle = conn.GetSelfHandle()

    # Another resource confirms we have no avatar. We don't request our vCard
    # because we already know there is no avatar
    presence_stanza = make_presence('test@localhost/noavatar',
                                    to='test@localhost/Resource',
                                    show='away', status='At the pub',
                                    photo="")
    q.forbid_events([avatar_request_event, avatar_retrieved_event])
    # Gabble must resist temptation to send vCard requests even with several
    # presence stanza sent!
    stream.send(presence_stanza)
    stream.send(presence_stanza)
    sync_stream(q, stream) # Twice because the vCard request is done in
    sync_stream(q, stream) # g_idle_add
    q.unforbid_events([avatar_request_event, avatar_retrieved_event])

    # Request on the first contact. Test the cache.
    handle = conn.RequestHandles(cs.HT_CONTACT, ['bob@foo.com'])[0]
    test_get_avatar(q, bus, conn, stream, 'bob@foo.com', handle,
            in_cache=False)
    test_get_avatar(q, bus, conn, stream, 'bob@foo.com', handle,
            in_cache=True)

    # Request another vCard and get resource-constraint
    busy_contact = 'jean@busy-server.com'
    busy_handle = conn.RequestHandles(cs.HT_CONTACT, [busy_contact])[0]
    conn.Avatars.RequestAvatars([busy_handle])

    iq_event = q.expect('stream-iq', to=busy_contact, query_ns='vcard-temp',
        query_name='vCard')
    iq = iq_event.stanza
    error = domish.Element((None, 'error'))
    error['code'] = '500'
    error['type'] = 'wait'
    error.addElement((ns.STANZA, 'resource-constraint'))

    q.forbid_events([avatar_retrieved_event, avatar_request_event])
    send_error_reply(stream, iq, error)

    # Request the same vCard again during the suspended delay
    # We should not get the avatar
    conn.Avatars.RequestAvatars([busy_handle])
    sync_stream(q, stream)
    sync_dbus(bus, q, conn)
    q.unforbid_events([avatar_retrieved_event, avatar_request_event])
    
    # Request on a different contact, on another server
    # We should get the avatar
    handle = conn.RequestHandles(cs.HT_CONTACT, ['bob2@foo.com'])[0]
    test_get_avatar(q, bus, conn, stream, 'bob2@foo.com', handle)

    # Try again the contact on the busy server.
    # We should not get the avatar
    # Note: the timeout is 3 seconds for the test suites. We assume that
    # a few stanza with be processed fast enough to avoid the race.
    q.forbid_events([avatar_retrieved_event, avatar_request_event])
    conn.Avatars.RequestAvatars([busy_handle])
    sync_stream(q, stream)
    sync_dbus(bus, q, conn)
    q.unforbid_events([avatar_retrieved_event, avatar_request_event])

    # After 3 seconds, we receive a new vCard request on the busy server
    iq_event = q.expect('stream-iq', to=busy_contact, query_ns='vcard-temp',
        query_name='vCard')
    iq = make_result_iq(stream, iq_event.stanza)
    vcard = iq.firstChildElement()
    photo = vcard.addElement('PHOTO')
    photo.addElement('TYPE', content='image/png')
    photo.addElement('BINVAL', content=base64.b64encode('hello'))
    stream.send(iq)

    event = q.expect('dbus-signal', signal='AvatarRetrieved')
    assertEquals(busy_handle, event.args[0])
    assertEquals(hashlib.sha1('hello').hexdigest(), event.args[1])
    assertEquals('hello', event.args[2])
    assertEquals('image/png', event.args[3])

    # Test with our own avatar test@localhost/Resource2
    presence_stanza = make_presence('test@localhost/Resource2',
                                    to='test@localhost/Resource',
                                    show='away', status='At the pub',
                                    photo=hashlib.sha1(':-D').hexdigest())
    stream.send(presence_stanza)
    iq_event = q.expect('stream-iq', to=None, query_ns='vcard-temp',
        query_name='vCard')
    iq = make_result_iq(stream, iq_event.stanza)
    vcard = iq.firstChildElement()
    photo = vcard.addElement('PHOTO')
    photo.addElement('TYPE', content='image/png')
    photo.addElement('BINVAL', content=base64.b64encode(':-D'))

    # do not send the vCard reply now. First, send another presence.
    q.forbid_events([avatar_request_event])
    stream.send(presence_stanza)
    sync_stream(q, stream)

    # Now send the reply.
    stream.send(iq)
    # So Gabble has the right hash, and no need to ask the vCard again
    stream.send(presence_stanza)
    sync_stream(q, stream)
    q.unforbid_events([avatar_request_event])

    # But if the hash is different, the vCard is asked again
    presence_stanza = make_presence('test@localhost/Resource2',
                                    to='test@localhost/Resource',
                                    show='away', status='At the pub',
                                    photo=hashlib.sha1('\o/').hexdigest())
    stream.send(presence_stanza)
    iq_event = q.expect('stream-iq', to=None, query_ns='vcard-temp',
        query_name='vCard')
    iq = make_result_iq(stream, iq_event.stanza)
    vcard = iq.firstChildElement()
    photo = vcard.addElement('PHOTO')
    photo.addElement('TYPE', content='image/png')
    photo.addElement('BINVAL', content=base64.b64encode('\o/'))
    stream.send(iq)

    event = q.expect('dbus-signal', signal='AvatarUpdated')
    assertEquals(self_handle, event.args[0])
    assertEquals(hashlib.sha1('\o/').hexdigest(), event.args[1])

    # Gabble must reply without asking the vCard to the server because the
    # avatar must be in the cache
    q.forbid_events([avatar_request_event])
    data, mime = conn.Avatars.RequestAvatar(self_handle, byte_arrays=True)
    assertEquals('\o/', data)
    data, mime = conn.Avatars.RequestAvatar(handle, byte_arrays=True)
    assertEquals('hello', data)
    q.unforbid_events([avatar_request_event])

    # First, ensure the pipeline is full
    contacts = ['random_user_%s@bigserver.com' % i for i in range(1, 100) ]
    handles = conn.RequestHandles(cs.HT_CONTACT, contacts)
    conn.Avatars.RequestAvatars(handles)
    # Then, request yet another avatar. The request will time out before
    # the IQ is sent, which used to trigger a crash in Gabble
    # (LP#445847). So, we assert that the error is NotAvailable (rather
    # than the error returned when the service crashes).
    try:
        conn.Avatars.RequestAvatar(handles[-1])
    except dbus.DBusException, e:
        assertEquals(cs.NOT_AVAILABLE, e.get_dbus_name())
    else:
        assert False

if __name__ == '__main__':
    exec_test(test)
