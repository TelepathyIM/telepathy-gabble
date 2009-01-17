"""
Helper functions for writing tubes tests
"""

from dbus import PROPERTIES_IFACE

from servicetest import unwrap
from constants import *

def check_tube_in_tubes(tube, tubes):
    """
    Check that 'tube' is in 'tubes', which should be the return value of
    ListTubes()
    """

    utube = unwrap(tube)
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


def check_conn_properties(q, bus, conn, stream, channel_list=None):
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

    assert ({CHANNEL_TYPE: CHANNEL_TYPE_TUBES,
             TARGET_HANDLE_TYPE: HT_CONTACT,
             },
             [TARGET_HANDLE, TARGET_ID
             ]
            ) in properties.get('RequestableChannelClasses'),\
                     properties['RequestableChannelClasses']
    assert ({CHANNEL_TYPE: CHANNEL_TYPE_STREAM_TUBE,
             TARGET_HANDLE_TYPE: HT_CONTACT
             },
             [TARGET_HANDLE, TARGET_ID, TUBE_PARAMETERS, STREAM_TUBE_SERVICE]
            ) in properties.get('RequestableChannelClasses'),\
                     properties['RequestableChannelClasses']
