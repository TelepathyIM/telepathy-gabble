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

def check_state (q, chan, state, wait = False):
    if wait:
        q.expect('dbus-signal', signal='CallStateChanged',
            interface = cs.CHANNEL_TYPE_CALL,
            predicate = lambda e: e.args[0] == state)

    properties = chan.GetAll(cs.CHANNEL_TYPE_CALL,
            dbus_interface=dbus.PROPERTIES_IFACE)

    assertEquals (state,
        properties["CallState"])

def check_and_accept_offer (q, bus, conn, self_handle, remote_handle,
        content, codecs, offer_path = None):

    [path, codecmap] = content.Get(cs.CALL_CONTENT_IFACE_MEDIA,
                "CodecOffer", dbus_interface=dbus.PROPERTIES_IFACE)

    if offer_path != None:
        assertEquals (offer_path, path)

    assertNotEquals ("/", path)

    offer = bus.get_object (conn.bus_name, path)
    codecmap_property = offer.Get (cs.CALL_CONTENT_CODECOFFER,
        "RemoteContactCodecMap", dbus_interface=dbus.PROPERTIES_IFACE)

    assertEquals (codecmap, codecmap_property)

    offer.Accept (codecs, dbus_interface=cs.CALL_CONTENT_CODECOFFER)

    current_codecs = content.Get(cs.CALL_CONTENT_IFACE_MEDIA,
                "ContactCodecMap", dbus_interface=dbus.PROPERTIES_IFACE)

    assertEquals (codecs,  current_codecs[self_handle])

    o = q.expect ('dbus-signal', signal='CodecsChanged')

    codecmap[self_handle] = codecs

    assertEquals ([codecmap, []], o.args)
