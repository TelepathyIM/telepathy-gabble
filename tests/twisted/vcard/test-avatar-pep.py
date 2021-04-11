
"""
Test PEP avatar support.
"""

import base64
import hashlib

from servicetest import call_async, EventPattern, assertEquals, assertLength
from gabbletest import exec_test, make_result_iq, acknowledge_iq, elem
from caps_helper import receive_presence_and_ask_caps

import constants as cs
import ns

NS_XMPP_AVATAR_META='urn:xmpp:avatar:metadata'
NS_XMPP_AVATAR_DATA='urn:xmpp:avatar:data'
png_pxl_b64=b'iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg=='

def test(q, bus, conn, stream):
    event, feats, forms, caps = receive_presence_and_ask_caps(q, stream, False)
    assert NS_XMPP_AVATAR_META in feats and NS_XMPP_AVATAR_META+'+notify' in feats, feats

    png_pxl_raw = base64.b64decode(png_pxl_b64)
    png_pxl_sha = hashlib.sha1(png_pxl_raw).hexdigest()

    msg = elem('message', from_='bob@foo.com')(
        elem((ns.PUBSUB_EVENT), 'event')(
            elem('items', node=NS_XMPP_AVATAR_META)(
                elem('item', id=png_pxl_sha)(
                    elem(NS_XMPP_AVATAR_META, 'metadata')(
                        elem('info', bytes='70', id=png_pxl_sha, type='image/png')
                    )
                )
            )
        )
    )
    stream.send(msg)

    handle = conn.get_contact_handle_sync('bob@foo.com')

    event = q.expect('dbus-signal', signal='AvatarUpdated', args=[handle, png_pxl_sha])
    conn.Avatars.RequestAvatars([handle])

    event = q.expect('stream-iq', to='bob@foo.com', iq_type='get',
        query_ns=ns.PUBSUB, query_name='pubsub')

    items = event.query.firstChildElement()
    assertEquals('items', items.name)
    assertEquals(NS_XMPP_AVATAR_DATA, items['node'])

    result = make_result_iq(stream, event.stanza)
    pubsub = result.firstChildElement()
    items = pubsub.addElement('items')
    items['node'] = NS_XMPP_AVATAR_DATA
    item = items.addElement('item')
    item['id'] = png_pxl_sha
    item.addElement('data', NS_XMPP_AVATAR_DATA, content=png_pxl_b64.decode())
    stream.send(result)

    q.expect('dbus-signal', signal='AvatarRetrieved',
            args=[handle, png_pxl_sha, png_pxl_raw, 'image/png'])

if __name__ == '__main__':
    exec_test(test)
