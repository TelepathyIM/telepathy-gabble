"""
Some handy constants for other tests to share and enjoy.
"""

HT_CONTACT = 1

CHANNEL = "org.freedesktop.Telepathy.Channel"
CHANNEL_TYPE_TUBES = CHANNEL + ".Type.Tubes"
CHANNEL_TYPE_STREAM_TUBE = CHANNEL + ".Type.StreamTube.DRAFT"

CHANNEL_TYPE = CHANNEL + '.ChannelType'
TARGET_HANDLE_TYPE = CHANNEL + '.TargetHandleType'
TARGET_HANDLE = CHANNEL + '.TargetHandle'

CONN = "org.freedesktop.Telepathy.Connection"
CONN_IFACE_REQUESTS = CONN + '.Interface.Requests'

ERRORS = 'org.freedesktop.Telepathy.Errors'
INVALID_ARGUMENT = ERRORS + '.InvalidArgument'
NOT_IMPLEMENTED = ERRORS + '.NotImplemented'
NOT_AVAILABLE = ERRORS + '.NotAvailable'

