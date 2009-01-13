"""
Some handy constants for other tests to share and enjoy.
"""

HT_CONTACT = 1

CHANNEL = "org.freedesktop.Telepathy.Channel"
CHANNEL_IFACE_GROUP = CHANNEL + ".Interface.Group"
CHANNEL_TYPE_TUBES = CHANNEL + ".Type.Tubes"
CHANNEL_IFACE_TUBE = CHANNEL + ".Interface.Tube.DRAFT"
CHANNEL_TYPE_STREAM_TUBE = CHANNEL + ".Type.StreamTube.DRAFT"

CHANNEL_TYPE = CHANNEL + '.ChannelType'
TARGET_HANDLE_TYPE = CHANNEL + '.TargetHandleType'
TARGET_HANDLE = CHANNEL + '.TargetHandle'
TARGET_ID = CHANNEL + '.TargetID'
REQUESTED = CHANNEL + '.Requested'
INITIATOR_HANDLE = CHANNEL + '.InitiatorHandle'
INITIATOR_ID = CHANNEL + '.InitiatorID'

CONN = "org.freedesktop.Telepathy.Connection"
CONN_IFACE_REQUESTS = CONN + '.Interface.Requests'

ERRORS = 'org.freedesktop.Telepathy.Errors'
INVALID_ARGUMENT = ERRORS + '.InvalidArgument'
NOT_IMPLEMENTED = ERRORS + '.NotImplemented'
NOT_AVAILABLE = ERRORS + '.NotAvailable'

TUBE_PARAMETERS = CHANNEL_IFACE_TUBE + '.Parameters'
TUBE_STATUS = CHANNEL_IFACE_TUBE + '.Status'
STREAM_TUBE_SERVICE = CHANNEL_TYPE_STREAM_TUBE + '.Service'

TUBE_CHANNEL_STATE_LOCAL_PENDING = 0
TUBE_CHANNEL_STATE_REMOTE_PENDING = 1
TUBE_CHANNEL_STATE_OPEN = 2
TUBE_CHANNEL_STATE_NOT_OFFERED = 3
