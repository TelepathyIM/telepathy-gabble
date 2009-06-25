
"""
Test that requesting a caps set 1 time is enough with hash and that we need 5
confirmation without hash.
"""

from twisted.words.xish import xpath

from servicetest import EventPattern, assertEquals, assertContains
from gabbletest import exec_test, make_presence, sync_stream
import constants as cs
import ns
from caps_helper import (
    compute_caps_hash, make_caps_disco_reply, fake_client_dataforms,
    presence_and_disco, send_presence, expect_disco, send_disco_reply
    )

client = 'http://telepathy.freedesktop.org/fake-client'
features = [
    ns.JINGLE_015,
    ns.JINGLE_015_AUDIO,
    ns.JINGLE_015_VIDEO,
    ns.GOOGLE_P2P,
    ]


def expect_caps(q, conn, h):
    # we can now do audio and video calls
    event = q.expect('dbus-signal', signal='CapabilitiesChanged')
    check_caps(conn, h)

def check_caps(conn, h):
    assertContains((h, cs.CHANNEL_TYPE_STREAMED_MEDIA, 3,
            cs.MEDIA_CAP_AUDIO | cs.MEDIA_CAP_VIDEO),
        conn.Capabilities.GetCapabilities([h]))

def update_contact_caps(q, conn, stream, contact, caps, disco = True,
    dataforms = {}, initial = True):
    h = presence_and_disco (q, conn, stream, contact,
        disco, client, caps, features, dataforms=dataforms, initial = initial)
    expect_caps (q, conn, h)

def test(q, bus, conn, stream):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])

    caps = {
        'node': client,
        'ver':  '0.1',
        }

    update_contact_caps(q, conn, stream, 'bob1@foo.com/Foo', caps)
    update_contact_caps(q, conn, stream, 'bob2@foo.com/Foo', caps)

    # Meredith signs in from one resource.
    update_contact_caps(q, conn, stream, 'meredith@foo.com/One', caps)
    # Meredith signs in from another resource with the same client. We don't
    # need to disco her, even though we don't trust this caps node in general
    # yet, because she's already told us what it means.
    meredith_two = 'meredith@foo.com/Two'
    q.forbid_events([
        EventPattern('stream-iq', to=meredith_two, query_ns=ns.DISCO_INFO)
        ])
    stream.send(make_presence(meredith_two, 'hello', caps=caps))
    sync_stream(q, stream)

    # Jens signs in from one resource, which is slow to answer the disco query.
    jens_one = 'jens@foo.com/One'
    j = send_presence(q, conn, stream, jens_one, caps)
    j_stanza = expect_disco(q, jens_one, client, caps)

    # Jens now signs in elsewhere with the same client; we disco it (maybe
    # it'll reply sooner? Maybe his first client's network connection went away
    # and the server hasn't noticed yet?) and it replies immediately.
    update_contact_caps (q, conn, stream, 'jens@foo.com/Two', caps,
        initial=False)

    # Jens' first client replies. We don't expect any caps changes here, and
    # this shouldn't count as a second point towards the five we need to trust
    # this caps node.
    send_disco_reply(stream, j_stanza, features)
    check_caps (conn, j)

    update_contact_caps (q, conn, stream, 'bob5@foo.com/Foo', caps)

    # Now five distinct contacts have told us what this caps node means, we
    # trust it.
    update_contact_caps (q, conn, stream, 'bob6@foo.com/Foo', caps,
        disco = False)
    update_contact_caps (q, conn, stream, 'bob7@foo.com/Foo', caps,
        disco = False)

    caps = {
        'node': client,
        'ver':  compute_caps_hash([], features, fake_client_dataforms),
        'hash': 'sha-1',
        }

    update_contact_caps(q, conn, stream, 'bilbo1@foo.com/Foo',
       caps,  dataforms = fake_client_dataforms)
    # We can verify the reply for these caps against the hash, and thus never
    # need to disco it again.
    update_contact_caps(q, conn, stream, 'bilbo2@foo.com/Foo', caps,
        disco = False, dataforms = fake_client_dataforms)
    update_contact_caps(q, conn, stream, 'bilbo3@foo.com/Foo', caps,
        disco = False, dataforms = fake_client_dataforms)

if __name__ == '__main__':
    exec_test(test)
