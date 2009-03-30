"""
Some handy constants for other tests to share and enjoy.
"""

from dbus import PROPERTIES_IFACE

HT_CONTACT = 1
HT_ROOM = 2

CHANNEL = "org.freedesktop.Telepathy.Channel"
CHANNEL_IFACE_GROUP = CHANNEL + ".Interface.Group"
CHANNEL_IFACE_HOLD = CHANNEL + ".Interface.Hold"
CHANNEL_IFACE_MEDIA_SIGNALLING = CHANNEL + ".Interface.MediaSignalling"
CHANNEL_TYPE_TEXT = CHANNEL + ".Type.Text"
CHANNEL_TYPE_TUBES = CHANNEL + ".Type.Tubes"
CHANNEL_IFACE_TUBE = CHANNEL + ".Interface.Tube.DRAFT"
CHANNEL_TYPE_STREAM_TUBE = CHANNEL + ".Type.StreamTube.DRAFT"
CHANNEL_TYPE_DBUS_TUBE = CHANNEL + ".Type.DBusTube.DRAFT"
CHANNEL_TYPE_STREAMED_MEDIA = CHANNEL + ".Type.StreamedMedia"
CHANNEL_TYPE_TEXT = CHANNEL + ".Type.Text"
CHANNEL_TYPE_FILE_TRANSFER = CHANNEL + ".Type.FileTransfer"

TP_AWKWARD_PROPERTIES = "org.freedesktop.Telepathy.Properties"
PROPERTY_FLAG_READ = 1
PROPERTY_FLAG_WRITE = 2

CHANNEL_TYPE = CHANNEL + '.ChannelType'
TARGET_HANDLE_TYPE = CHANNEL + '.TargetHandleType'
TARGET_HANDLE = CHANNEL + '.TargetHandle'
TARGET_ID = CHANNEL + '.TargetID'
REQUESTED = CHANNEL + '.Requested'
INITIATOR_HANDLE = CHANNEL + '.InitiatorHandle'
INITIATOR_ID = CHANNEL + '.InitiatorID'
INTERFACES = CHANNEL + '.Interfaces'

CONN = "org.freedesktop.Telepathy.Connection"
CONN_IFACE_CONTACTS = CONN + '.Interface.Contacts'
CONN_IFACE_CONTACT_CAPA = CONN + '.Interface.ContactCapabilities.DRAFT'
CONN_IFACE_REQUESTS = CONN + '.Interface.Requests'

ERROR = 'org.freedesktop.Telepathy.Error'
INVALID_ARGUMENT = ERROR + '.InvalidArgument'
NOT_IMPLEMENTED = ERROR + '.NotImplemented'
NOT_AVAILABLE = ERROR + '.NotAvailable'
PERMISSION_DENIED = ERROR + '.PermissionDenied'
OFFLINE = ERROR + '.Offline'

TUBE_PARAMETERS = CHANNEL_IFACE_TUBE + '.Parameters'
TUBE_STATE = CHANNEL_IFACE_TUBE + '.State'
STREAM_TUBE_SERVICE = CHANNEL_TYPE_STREAM_TUBE + '.Service'
DBUS_TUBE_SERVICE_NAME = CHANNEL_TYPE_DBUS_TUBE + '.ServiceName'
DBUS_TUBE_DBUS_NAMES = CHANNEL_TYPE_DBUS_TUBE + '.DBusNames'

TUBE_CHANNEL_STATE_LOCAL_PENDING = 0
TUBE_CHANNEL_STATE_REMOTE_PENDING = 1
TUBE_CHANNEL_STATE_OPEN = 2
TUBE_CHANNEL_STATE_NOT_OFFERED = 3

MEDIA_STREAM_TYPE_AUDIO = 0
MEDIA_STREAM_TYPE_VIDEO = 1

SOCKET_ADDRESS_TYPE_UNIX = 0
SOCKET_ADDRESS_TYPE_ABSTRACT_UNIX = 1
SOCKET_ADDRESS_TYPE_IPV4 = 2
SOCKET_ADDRESS_TYPE_IPV6 = 3

SOCKET_ACCESS_CONTROL_LOCALHOST = 0
SOCKET_ACCESS_CONTROL_PORT = 1
SOCKET_ACCESS_CONTROL_NETMASK = 2
SOCKET_ACCESS_CONTROL_CREDENTIALS = 3

TUBE_STATE_LOCAL_PENDING = 0
TUBE_STATE_REMOTE_PENDING = 1
TUBE_STATE_OPEN = 2
TUBE_STATE_NOT_OFFERED = 3

TUBE_TYPE_DBUS = 0
TUBE_TYPE_STREAM = 1

MEDIA_STREAM_DIRECTION_NONE = 0
MEDIA_STREAM_DIRECTION_SEND = 1
MEDIA_STREAM_DIRECTION_RECEIVE = 2
MEDIA_STREAM_DIRECTION_BIDIRECTIONAL = 3

MEDIA_STREAM_PENDING_LOCAL_SEND = 1
MEDIA_STREAM_PENDING_REMOTE_SEND = 2

MEDIA_STREAM_TYPE_AUDIO = 0
MEDIA_STREAM_TYPE_VIDEO = 1

MEDIA_STREAM_STATE_DISCONNECTED = 0
MEDIA_STREAM_STATE_CONNECTING = 1
MEDIA_STREAM_STATE_CONNECTED = 2

MEDIA_STREAM_DIRECTION_NONE = 0
MEDIA_STREAM_DIRECTION_SEND = 1
MEDIA_STREAM_DIRECTION_RECEIVE = 2
MEDIA_STREAM_DIRECTION_BIDIRECTIONAL = 3

FT_STATE_NONE = 0
FT_STATE_PENDING = 1
FT_STATE_ACCEPTED = 2
FT_STATE_OPEN = 3
FT_STATE_COMPLETED = 4
FT_STATE_CANCELLED = 5

FT_STATE_CHANGE_REASON_NONE = 0
FT_STATE_CHANGE_REASON_REQUESTED = 1
FT_STATE_CHANGE_REASON_LOCAL_STOPPED = 2
FT_STATE_CHANGE_REASON_REMOTE_STOPPED = 3
FT_STATE_CHANGE_REASON_LOCAL_ERROR = 4
FT_STATE_CHANGE_REASON_REMOTE_ERROR = 5

FILE_HASH_TYPE_NONE = 0
FILE_HASH_TYPE_MD5 = 1
FILE_HASH_TYPE_SHA1 = 2
FILE_HASH_TYPE_SHA256 = 3

FT_STATE = CHANNEL_TYPE_FILE_TRANSFER + '.State'
FT_CONTENT_TYPE = CHANNEL_TYPE_FILE_TRANSFER + '.ContentType'
FT_FILENAME = CHANNEL_TYPE_FILE_TRANSFER + '.Filename'
FT_SIZE = CHANNEL_TYPE_FILE_TRANSFER + '.Size'
FT_CONTENT_HASH_TYPE = CHANNEL_TYPE_FILE_TRANSFER + '.ContentHashType'
FT_CONTENT_HASH = CHANNEL_TYPE_FILE_TRANSFER + '.ContentHash'
FT_DESCRIPTION = CHANNEL_TYPE_FILE_TRANSFER + '.Description'
FT_DATE = CHANNEL_TYPE_FILE_TRANSFER + '.Date'
FT_AVAILABLE_SOCKET_TYPES = CHANNEL_TYPE_FILE_TRANSFER + '.AvailableSocketTypes'
FT_TRANSFERRED_BYTES = CHANNEL_TYPE_FILE_TRANSFER + '.TransferredBytes'
FT_INITIAL_OFFSET = CHANNEL_TYPE_FILE_TRANSFER + '.InitialOffset'
