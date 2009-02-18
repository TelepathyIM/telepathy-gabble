"""
Helper functions for writing tubes tests
"""

import errno
import os

from dbus import PROPERTIES_IFACE

from servicetest import unwrap
from constants import *

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
    try:
        os.remove(os.getcwd() + '/stream' + name)
    except OSError, e:
        if e.errno != errno.ENOENT:
            raise
    reactor.listenUNIX(os.getcwd() + '/stream' + name, factory)
