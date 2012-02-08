"""
Utilities for the call channel class
"""

import dbus
from dbus.exceptions import DBusException

from twisted.words.xish import xpath

from gabbletest import exec_test
from servicetest import (
    make_channel_proxy, wrap_channel,
    EventPattern, call_async,
    assertEquals, assertContains, assertLength, assertNotEquals
    )
import constants as cs
from jingletest2 import JingleTest2, test_all_dialects
import ns

from mucutil import echo_muc_presence

def check_state (q, chan, state, wait = False):
    if wait:
        q.expect('dbus-signal', signal='CallStateChanged',
            interface = cs.CHANNEL_TYPE_CALL,
            predicate = lambda e: e.args[0] == state)

    properties = chan.GetAll(cs.CHANNEL_TYPE_CALL,
            dbus_interface=dbus.PROPERTIES_IFACE)

    assertEquals (state,
        properties["CallState"])

def check_and_accept_offer (q, bus, conn, content, md, remote_handle,
        offer_path = None, md_changed = True):

    [path, remote_md] = content.Get(cs.CALL_CONTENT_IFACE_MEDIA,
                "MediaDescriptionOffer", dbus_interface=dbus.PROPERTIES_IFACE)

    if offer_path != None:
        assertEquals (offer_path, path)

    assertNotEquals ("/", path)

    offer = bus.get_object (conn.bus_name, path)
    codecmap_property = offer.Get (cs.CALL_CONTENT_MEDIADESCRIPTION,
        "Codecs", dbus_interface=dbus.PROPERTIES_IFACE)

    assertEquals (remote_md[cs.CALL_CONTENT_MEDIADESCRIPTION + '.Codecs'], codecmap_property)

    offer.Accept (md, dbus_interface=cs.CALL_CONTENT_MEDIADESCRIPTION)

    current_md = content.Get(cs.CALL_CONTENT_IFACE_MEDIA,
                "LocalMediaDescriptions", dbus_interface=dbus.PROPERTIES_IFACE)
    assertEquals (md,  current_md[remote_handle])

    if md_changed:
        o = q.expect ('dbus-signal', signal='LocalMediaDescriptionChanged')
        assertEquals ([md], o.args)

def no_muji_presences (muc):
    return EventPattern ('stream-presence',
        to = muc + "/test",
        predicate = lambda x:
            xpath.queryForNodes("/presence/muji", x.stanza))

def create_muji_channel (q, conn, stream, muc, in_muc = False):
    call_async (q, conn.Requests, 'CreateChannel',
        { cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_CALL,
          cs.TARGET_HANDLE_TYPE: cs.HT_ROOM,
          cs.TARGET_ID: muc,
          cs.CALL_INITIAL_AUDIO: True,
          cs.CALL_INITIAL_AUDIO_NAME: "Audio",
         }, byte_arrays = True)

    if not in_muc:
        e = q.expect('stream-presence', to = muc + "/test")
        echo_muc_presence (q, stream, e.stanza, 'none', 'participant')

    e = q.expect ('dbus-return', method='CreateChannel')

    return e.value
