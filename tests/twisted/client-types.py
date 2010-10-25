"""
Test Conn.I.ClientTypes
"""
import random

from servicetest import EventPattern, assertEquals, assertLength, assertContains
from gabbletest import exec_test, make_presence, sync_stream
import constants as cs
import ns
from caps_helper import presence_and_disco

client_base = 'http://telepathy.freedesktop.org/fake-client/client-types-'
caps_base = {
   'ver': '0.1'
   }
features = [
    ns.JINGLE_015,
    ns.JINGLE_015_AUDIO,
    ns.JINGLE_015_VIDEO,
    ns.GOOGLE_P2P,
    ]

# our identities
BOT = ['client/bot/en/doesnotcompute']
CONSOLE = ['client/console/en/dumb']
GAME = ['client/game/en/wiibox3']
HANDHELD = ['client/handheld/en/lolpaq']
PC = ['client/pc/en/Lolclient 0.L0L']
PHONE = ['client/phone/en/gr8phone 101']
WEB = ['client/web/en/webcat']
SMS = ['client/phone/en/tlk 2 u l8r']

def contact_online(q, conn, stream, contact, identities,
    disco = True, dataforms = {}, initial = True, show = None):

    types = map(lambda x: x.split('/')[1], identities)

    handle = conn.RequestHandles(cs.HT_CONTACT, [contact])[0]

    # add something to the end of the client string so that the caps
    # hashes aren't all the same and so stop discoing
    client = client_base + types[0]
    caps = caps_base
    caps['node'] = client

    # make contact come online
    presence_and_disco (q, conn, stream, contact,
                        disco, client, caps, features, identities,
                        dataforms=dataforms, initial = initial,
                        show = show)

    if initial:
        event = q.expect('dbus-signal', signal='ClientTypesUpdated')
        assertEquals([handle, types], event.args)

def test(q, bus, conn, stream):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])

    # check all these types appear as they should
    contact_online(q, conn, stream, 'bot@bot.com/lol', BOT)
    contact_online(q, conn, stream, 'console@console.com/lol', CONSOLE)
    contact_online(q, conn, stream, 'game@game.com/lol', GAME)
    contact_online(q, conn, stream, 'handheld@handheld.com/lol', HANDHELD)
    contact_online(q, conn, stream, 'pc@pc.com/lol', PC)
    contact_online(q, conn, stream, 'phone@phone.com/lol', PHONE)
    contact_online(q, conn, stream, 'web@web.com/lol', WEB)
    contact_online(q, conn, stream, 'sms@sms.com/lol', SMS)

    meredith_one = 'meredith@foo.com/One'
    meredith_two = 'meredith@foo.com/Two'
    meredith_handle = conn.RequestHandles(cs.HT_CONTACT, [meredith_one])[0]

    # Meredith signs in from one resource
    contact_online(q, conn, stream, meredith_one, PC, show='chat')

    # Meredith signs in from another resource
    contact_online(q, conn, stream, meredith_two, PHONE, show='dnd', initial=False)

    # check we're still a PC
    types = conn.GetClientTypes([meredith_handle],
                                dbus_interface=cs.CONN_IFACE_CLIENT_TYPES)

    assertLength(1, types)
    assertLength(1, types[meredith_handle])
    assertEquals('pc', types[meredith_handle][0])

    # Two now becomes more available
    stream.send(make_presence(meredith_two, show='chat'))

    types = conn.GetClientTypes([meredith_handle],
                                dbus_interface=cs.CONN_IFACE_CLIENT_TYPES)
    assertEquals('pc', types[meredith_handle][0])

    # One now becomes less available
    stream.send(make_presence(meredith_one, show='away'))

    # wait for the presence change
    q.expect('dbus-signal', signal='PresencesChanged',
             args=[{meredith_handle: (cs.PRESENCE_AVAILABLE, 'chat', '')}])

    # now wait for the change in client type
    event = q.expect('dbus-signal', signal='ClientTypesUpdated')
    assertEquals([meredith_handle, ['phone']], event.args)

    # make One more available again
    stream.send(make_presence(meredith_one, show='chat', status='lawl'))

    # wait for the presence change
    q.expect('dbus-signal', signal='PresencesChanged',
             args=[{meredith_handle: (cs.PRESENCE_AVAILABLE, 'chat', 'lawl')}])

    # now wait for the change in client type
    event = q.expect('dbus-signal', signal='ClientTypesUpdated')
    assertEquals([meredith_handle, ['pc']], event.args)

    # that'll do

if __name__ == '__main__':
    exec_test(test)
