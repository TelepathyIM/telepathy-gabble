# vim: set fileencoding=utf-8
# Tests publishing an avatar in MUCs, and getting tokens for ourselves and
# others. Serves as a regression test for
# <https://bugs.freedesktop.org/show_bug.cgi?id=32017>, where our MUC-specific
# self handle would have an empty avatar token even though we're publishing our
# avatar on the wire correctly.

import hashlib
from servicetest import (
    call_async, EventPattern, assertEquals, assertLength, sync_dbus,
    wrap_channel,
    )
from gabbletest import (
    exec_test, expect_and_handle_get_vcard, expect_and_handle_set_vcard,
    make_muc_presence, elem,
    )
from twisted.words.xish import xpath
import ns
import constants as cs
from mucutil import try_to_join_muc

AVATAR_1_DATA = 'nyan'
AVATAR_1_SHA1 = hashlib.sha1(AVATAR_1_DATA).hexdigest()
AVATAR_1_MIME_TYPE = 'image/x-pop-tart'

AVATAR_2_DATA = 'NYAN'
AVATAR_2_SHA1 = hashlib.sha1(AVATAR_2_DATA).hexdigest()
AVATAR_2_MIME_TYPE = 'image/x-pop-tart'

MUC = 'taco-dog@nyan.cat'

def extract_hash_from_presence(stanza):
    return xpath.queryForString(
        '/presence/x[@xmlns="%s"]/photo' % ns.VCARD_TEMP_UPDATE,
        stanza)

def test(q, bus, conn, stream):
    self_handle = conn.Properties.Get(cs.CONN, "SelfHandle")

    # When Gabble initially requests its avatar from the server, it discovers
    # it has none.
    expect_and_handle_get_vcard(q, stream)
    handle, signalled_token = q.expect('dbus-signal', signal='AvatarUpdated').args

    assertEquals(self_handle, handle)
    assertEquals('', signalled_token)

    # The user sets an avatar.
    call_async(q, conn.Avatars, 'SetAvatar', AVATAR_1_DATA, AVATAR_1_MIME_TYPE)
    expect_and_handle_get_vcard(q, stream)
    expect_and_handle_set_vcard(q, stream)

    # It's signalled on D-Bus …
    set_ret, avatar_updated = q.expect_many(
        EventPattern('dbus-return', method='SetAvatar'),
        EventPattern('dbus-signal', signal='AvatarUpdated'),
        )

    returned_token, = set_ret.value
    handle, signalled_token = avatar_updated.args

    assertEquals(self_handle, handle)
    assertEquals(returned_token, signalled_token)

    # … and also on XMPP.
    broadcast = q.expect('stream-presence', to=None)
    broadcast_hash = extract_hash_from_presence(broadcast.stanza)
    assertEquals(AVATAR_1_SHA1, broadcast_hash)

    # If applications ask Gabble for information about the user's own avatar,
    # it should be able to answer. (Strictly speaking, expecting Gabble to know
    # the avatar data is risky because Gabble discards cached vCards after a
    # while, but we happen to know it takes 20 seconds or so for that to
    # happen.)
    known = conn.Avatars.GetKnownAvatarTokens([self_handle])
    assertEquals({self_handle: signalled_token}, known)

    conn.Avatars.RequestAvatars([self_handle])
    retrieved = q.expect('dbus-signal', signal='AvatarRetrieved')
    handle, token, data, mime_type = retrieved.args
    assertEquals(self_handle, handle)
    assertEquals(signalled_token, token)
    assertEquals(AVATAR_1_DATA, data)
    assertEquals(AVATAR_1_MIME_TYPE, mime_type)

    # Well, that was quite easy. How about we join a MUC? XEP-0153 §4.1 says:
    #     If a client supports the protocol defined herein, it […] SHOULD
    #     also include the update child in directed presence stanzas (e.g.,
    #     directed presence sent when joining Multi-User Chat [5] rooms).
    #         — http://xmpp.org/extensions/xep-0153.html#bizrules-presence
    join_event = try_to_join_muc(q, bus, conn, stream, MUC)
    directed_hash = extract_hash_from_presence(join_event.stanza)
    assertEquals(AVATAR_1_SHA1, directed_hash)

    # There are two others in the MUC: fredrik has no avatar, wendy has an
    # avatar. We, of course, have our own avatar.
    stream.send(make_muc_presence('none', 'participant', MUC, 'fredrik'))
    stream.send(make_muc_presence('none', 'participant', MUC, 'wendy',
          photo=AVATAR_2_SHA1))
    stream.send(make_muc_presence('owner', 'moderator', MUC, 'test',
          photo=AVATAR_1_SHA1))

    path, _ = q.expect('dbus-return', method='CreateChannel').value
    chan = wrap_channel(bus.get_object(conn.bus_name, path), 'Text',
        ['Messages'])

    members = chan.Properties.Get(cs.CHANNEL_IFACE_GROUP, 'Members')
    assertLength(3, members)

    fredrik, wendy, muc_self_handle = conn.RequestHandles(cs.HT_CONTACT,
        ['%s/%s' % (MUC, x) for x in ["fredrik", "wendy", "test"]])

    known = conn.Avatars.GetKnownAvatarTokens(members)
    # <https://bugs.freedesktop.org/show_bug.cgi?id=32017>: this assertion
    # failed, the MUC self handle's token was the empty string.
    assertEquals(AVATAR_1_SHA1, known[muc_self_handle])
    assertEquals(AVATAR_2_SHA1, known[wendy])
    assertEquals('', known[fredrik])

    # 'k, cool. Wendy loves our avatar and switches to it.
    stream.send(make_muc_presence('none', 'participant', MUC, 'wendy',
          photo=AVATAR_1_SHA1))
    # Okay this is technically assuming that we just expose the SHA1 sums
    # directly which is not guaranteed … but we do.
    q.expect('dbus-signal', signal='AvatarUpdated',
        args=[wendy, AVATAR_1_SHA1])

    # Fredrik switches too.
    stream.send(make_muc_presence('none', 'participant', MUC, 'fredrik',
          photo=AVATAR_1_SHA1))
    q.expect('dbus-signal', signal='AvatarUpdated',
        args=[fredrik, AVATAR_1_SHA1])

    # And we switch to some other avatar. Gabble should update its vCard, and
    # then update its MUC presence (which the test, acting as the MUC server,
    # must echo).
    call_async(q, conn.Avatars, 'SetAvatar', AVATAR_2_DATA, AVATAR_2_MIME_TYPE)
    expect_and_handle_get_vcard(q, stream)
    expect_and_handle_set_vcard(q, stream)

    muc_presence = q.expect('stream-presence', to=('%s/test' % MUC))
    directed_hash = extract_hash_from_presence(muc_presence.stanza)
    stream.send(make_muc_presence('owner', 'moderator', MUC, 'test',
          photo=directed_hash))

    # Gabble should signal an avatar update for both our global self-handle and
    # our MUC self-handle. (The first of these of course does not need to wait
    # for the MUC server to echo our presence.)
    q.expect_many(
        EventPattern('dbus-signal', signal='AvatarUpdated',
            args=[self_handle, AVATAR_2_SHA1]),
        EventPattern('dbus-signal', signal='AvatarUpdated',
            args=[muc_self_handle, AVATAR_2_SHA1]),
        )

if __name__ == '__main__':
    exec_test(test)
