"""
Helper functions for writing tubes tests
"""

import errno
import os

import dbus
from dbus import PROPERTIES_IFACE

from servicetest import unwrap
from gabbletest import exec_test
from constants import *
from bytestream import BytestreamIBBMsg, BytestreamS5B, BytestreamSIFallback, BytestreamIBBIQ

from twisted.internet import reactor
from twisted.internet.protocol import Factory, Protocol

def check_tube_in_tubes(tube, tubes):
    """
    Check that 'tube' is in 'tubes', which should be the return value of
    ListTubes(). tube[0] may be None to check that a new-style tube is
    represented on the old interface (because you don't know what its id is in
    those cases)
    """

    utube = unwrap(tube)

    if tube[0] is None:
        for t in tubes:
            if tube[1:] == t[1:]:
                return
    else:
        for t in tubes:
            if tube[0] != t[0]:
                continue

            pair = "\n    %s,\n    %s" % (utube, unwrap(t))

            assert tube[1] == t[1], "self handles don't match: %s" % pair
            assert tube[2] == t[2], "tube types don't match: %s" % pair
            assert tube[3] == t[3], "services don't match: %s " % pair
            assert tube[4] == t[4], "parameters don't match: %s" % pair
            assert tube[5] == t[5], "states don't match: %s" % pair

            return

    assert False, "tube %s not in %s" % (unwrap (tube), unwrap (tubes))


def check_conn_properties(q, conn, channel_list=None):
    """
    Check that Connection.Interface.Requests.Channels matches channel_list, and
    that RequestableChannelClasses contains the expected tube types.
    """

    properties = conn.GetAll(
            CONN_IFACE_REQUESTS,
            dbus_interface=PROPERTIES_IFACE)

    if channel_list == None:
        assert properties.get('Channels') == [], properties['Channels']
    else:
        for i in channel_list:
            assert i in properties['Channels'], \
                (i, properties['Channels'])

    # 1-1 tubes channel (old API)
    assert ({CHANNEL_TYPE: CHANNEL_TYPE_TUBES,
             TARGET_HANDLE_TYPE: HT_CONTACT,
             },
             [TARGET_HANDLE, TARGET_ID
             ]
            ) in properties.get('RequestableChannelClasses'),\
                     properties['RequestableChannelClasses']

    # muc tubes channel (old API)
    assert ({CHANNEL_TYPE: CHANNEL_TYPE_TUBES,
             TARGET_HANDLE_TYPE: HT_ROOM,
             },
             [TARGET_HANDLE, TARGET_ID
             ]
            ) in properties.get('RequestableChannelClasses'),\
                     properties['RequestableChannelClasses']

    # 1-1 StreamTube channel
    assert ({CHANNEL_TYPE: CHANNEL_TYPE_STREAM_TUBE,
             TARGET_HANDLE_TYPE: HT_CONTACT
             },
             [TARGET_HANDLE, TARGET_ID, STREAM_TUBE_SERVICE]
            ) in properties.get('RequestableChannelClasses'),\
                     properties['RequestableChannelClasses']

    # muc StreamTube channel
    assert ({CHANNEL_TYPE: CHANNEL_TYPE_STREAM_TUBE,
             TARGET_HANDLE_TYPE: HT_ROOM
             },
             [TARGET_HANDLE, TARGET_ID, STREAM_TUBE_SERVICE]
            ) in properties.get('RequestableChannelClasses'),\
                     properties['RequestableChannelClasses']

    # 1-1 D-Bus tube channel
    assert ({CHANNEL_TYPE: CHANNEL_TYPE_DBUS_TUBE,
            TARGET_HANDLE_TYPE: HT_CONTACT},
             [TARGET_HANDLE, TARGET_ID, DBUS_TUBE_SERVICE_NAME]
            ) in properties.get('RequestableChannelClasses'),\
                     properties['RequestableChannelClasses']

    # muc D-Bus tube channel
    assert ({CHANNEL_TYPE: CHANNEL_TYPE_DBUS_TUBE,
            TARGET_HANDLE_TYPE: HT_ROOM},
             [TARGET_HANDLE, TARGET_ID, DBUS_TUBE_SERVICE_NAME]
            ) in properties.get('RequestableChannelClasses'),\
                     properties['RequestableChannelClasses']


def check_NewChannel_signal(args, channel_type, chan_path, contact_handle,
                            suppress_handler):
    """
    Checks the first argument, a tuple of arguments from NewChannel, matches
    the other arguments.
    """
    if chan_path is not None:
        assert args[0] == chan_path, (args, chan_path)
    assert args[1] == channel_type, (args, channel_type)
    assert args[2] == HT_CONTACT, (args, HT_CONTACT)
    assert args[3] == contact_handle, (args, contact_handle)
    assert args[4] == suppress_handler, (args, suppress_handler)

def check_NewChannels_signal(args, channel_type, chan_path, contact_handle,
                             contact_id, initiator_handle):
    """
    Checks the first argument, a one-tuple of arguments from NewChannels,
    matches the other arguments.
    """
    assert len(args) == 1, args
    assert len(args[0]) == 1        # one channel
    path, props = args[0][0]

    assert path == chan_path, (emitted_path, chan_path)

    assert props[CHANNEL_TYPE] == channel_type, (props, channel_type)
    assert props[TARGET_HANDLE_TYPE] == HT_CONTACT, props
    assert props[TARGET_HANDLE] == contact_handle, (props, contact_handle)
    assert props[TARGET_ID] == contact_id, (props, contact_id)
    assert props[REQUESTED] == True, props
    assert props[INITIATOR_HANDLE] == initiator_handle, \
        (props, initiator_handle)
    assert props[INITIATOR_ID] == 'test@localhost', props

def check_channel_properties(q, bus, conn, channel, channel_type,
                             contact_handle, contact_id, state=None):
    """
    Checks the D-Bus properties of a 1-1 Tubes, StreamTube or DBusTube channel,
    initiated by the test user
    """

    # Check o.fd.T.Channel properties.
    channel_props = channel.GetAll(CHANNEL, dbus_interface=PROPERTIES_IFACE)
    assert channel_props.get('TargetHandle') == contact_handle, \
            (channel_props.get('TargetHandle'), contact_handle)
    assert channel_props.get('TargetHandleType') == HT_CONTACT, \
            channel_props.get('TargetHandleType')
    assert channel_props.get('ChannelType') == channel_type, \
            channel_props.get('ChannelType')
    assert 'Interfaces' in channel_props, channel_props
    assert CHANNEL_IFACE_GROUP not in \
            channel_props['Interfaces'], \
            channel_props['Interfaces']
    assert channel_props['TargetID'] == contact_id
    assert channel_props['Requested'] == True
    assert channel_props['InitiatorID'] == 'test@localhost'
    assert channel_props['InitiatorHandle'] == conn.GetSelfHandle()

    if channel_type == CHANNEL_TYPE_TUBES:
        assert state is None
        assert len(channel_props['Interfaces']) == 0, channel_props['Interfaces']
        supported_socket_types = channel.GetAvailableStreamTubeTypes()
    else:
        assert state is not None
        tube_props = channel.GetAll(CHANNEL_IFACE_TUBE,
                dbus_interface=PROPERTIES_IFACE)
        assert tube_props['State'] == state
        # no strict check but at least check the properties exist
        assert tube_props['Parameters'] is not None
        assert channel_props['Interfaces'] == \
            dbus.Array([CHANNEL_IFACE_TUBE], signature='s'), \
            channel_props['Interfaces']

        if channel_type == CHANNEL_TYPE_STREAM_TUBE:
            supported_socket_types = channel.Get(CHANNEL_TYPE_STREAM_TUBE,
                'SupportedSocketTypes', dbus_interface=PROPERTIES_IFACE)
        else:
            supported_socket_types = None

    if supported_socket_types is not None:
        # FIXME: this should check for particular types, not just a magic length
        assert len(supported_socket_types) == 3

class Echo(Protocol):
    """
    A trivial protocol that just echoes back whatever you send it, in lowercase.
    """
    def dataReceived(self, data):
        self.transport.write(data.lower())

def set_up_echo(name):
    """
    Sets up an instance of Echo listening on "%s/stream%s" % (cwd, name)
    """
    factory = Factory()
    factory.protocol = Echo
    full_path = os.getcwd() + '/stream' + name
    try:
        os.remove(full_path)
    except OSError, e:
        if e.errno != errno.ENOENT:
            raise
    reactor.listenUNIX(full_path, factory)
    return full_path

def exec_tube_test(test):
    def test_ibb_msg(q, bus, conn, stream):
        test(q, bus, conn, stream, BytestreamIBBMsg)

    def test_ibb_iq(q, bus, conn, stream):
        test(q, bus, conn, stream, BytestreamIBBIQ)

    def test_socks5(q, bus, conn, stream):
        test(q, bus, conn, stream, BytestreamS5B)

    def test_si_fallback(q, bus, conn, stream):
        test(q, bus, conn, stream, BytestreamSIFallback)

    exec_test(test_ibb_msg)
    exec_test(test_ibb_iq)
    exec_test(test_socks5)
    exec_test(test_si_fallback)
