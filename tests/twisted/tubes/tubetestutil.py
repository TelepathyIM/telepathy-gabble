"""
Helper functions for writing tubes tests
"""

import errno
import os
import socket
import sys

import dbus

from servicetest import unwrap, assertContains, EventProtocolClientFactory,\
    EventProtocolFactory, assertEquals, EventProtocol, EventPattern
from gabbletest import exec_test
import constants as cs
import bytestream
import ns

from twisted.internet import reactor
from twisted.internet.protocol import Factory, Protocol
from twisted.internet.error import CannotListenError
from twisted.internet import tcp

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
            cs.CONN_IFACE_REQUESTS,
            dbus_interface=cs.PROPERTIES_IFACE)

    if channel_list == None:
        assert properties.get('Channels') == [], properties['Channels']
    else:
        for i in channel_list:
            assert i in properties['Channels'], \
                (i, properties['Channels'])

    # 1-1 tubes channel (old API)
    assert ({cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_TUBES,
             cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
             },
             [cs.TARGET_HANDLE, cs.TARGET_ID
             ]
            ) in properties.get('RequestableChannelClasses'),\
                     properties['RequestableChannelClasses']

    # muc tubes channel (old API)
    assert ({cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_TUBES,
             cs.TARGET_HANDLE_TYPE: cs.HT_ROOM,
             },
             [cs.TARGET_HANDLE, cs.TARGET_ID
             ]
            ) in properties.get('RequestableChannelClasses'),\
                     properties['RequestableChannelClasses']

    # 1-1 StreamTube channel
    assert ({cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_STREAM_TUBE,
             cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT
             },
             [cs.TARGET_HANDLE, cs.TARGET_ID, cs.STREAM_TUBE_SERVICE]
            ) in properties.get('RequestableChannelClasses'),\
                     properties['RequestableChannelClasses']

    # muc StreamTube channel
    assert ({cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_STREAM_TUBE,
             cs.TARGET_HANDLE_TYPE: cs.HT_ROOM
             },
             [cs.TARGET_HANDLE, cs.TARGET_ID, cs.STREAM_TUBE_SERVICE]
            ) in properties.get('RequestableChannelClasses'),\
                     properties['RequestableChannelClasses']

    # 1-1 D-Bus tube channel
    assert ({cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_DBUS_TUBE,
            cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT},
             [cs.TARGET_HANDLE, cs.TARGET_ID, cs.DBUS_TUBE_SERVICE_NAME]
            ) in properties.get('RequestableChannelClasses'),\
                     properties['RequestableChannelClasses']

    # muc D-Bus tube channel
    assert ({cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_DBUS_TUBE,
            cs.TARGET_HANDLE_TYPE: cs.HT_ROOM},
             [cs.TARGET_HANDLE, cs.TARGET_ID, cs.DBUS_TUBE_SERVICE_NAME]
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
    assert args[2] == cs.HT_CONTACT, (args, cs.HT_CONTACT)
    assert args[3] == contact_handle, (args, contact_handle)
    assert args[4] == suppress_handler, (args, suppress_handler)

def check_NewChannels_signal(conn, args, channel_type, chan_path, contact_handle,
                             contact_id, initiator_handle):
    """
    Checks the first argument, a one-tuple of arguments from NewChannels,
    matches the other arguments.
    """
    assert len(args) == 1, args
    assert len(args[0]) == 1        # one channel
    path, props = args[0][0]

    assert path == chan_path, (emitted_path, chan_path)

    assert props[cs.CHANNEL_TYPE] == channel_type, (props, channel_type)
    assert props[cs.TARGET_HANDLE_TYPE] == cs.HT_CONTACT, props
    assert props[cs.TARGET_HANDLE] == contact_handle, (props, contact_handle)
    assert props[cs.TARGET_ID] == contact_id, (props, contact_id)
    assert props[cs.REQUESTED] == True, props
    assert props[cs.INITIATOR_HANDLE] == initiator_handle, \
        (props, initiator_handle)
    assert props[cs.INITIATOR_ID] == 'test@localhost', props

    # check that the newly announced channel is in the channels list
    all_channels = conn.Get(cs.CONN_IFACE_REQUESTS, 'Channels',
        dbus_interface=cs.PROPERTIES_IFACE, byte_arrays=True)
    assertContains((path, props), all_channels)

def check_platform_socket_types(sock_types):
    assertContains(cs.SOCKET_ADDRESS_TYPE_IPV4, sock_types)
    assertContains(cs.SOCKET_ADDRESS_TYPE_IPV6, sock_types)

    if os.name == 'posix':
        # true on at least Linux
        assertContains(cs.SOCKET_ADDRESS_TYPE_UNIX, sock_types)

def check_channel_properties(q, bus, conn, channel, channel_type,
                             contact_handle, contact_id, state=None):
    """
    Checks the D-Bus properties of a 1-1 Tubes, StreamTube or DBusTube channel,
    initiated by the test user
    """

    # Check o.fd.T.Channel properties.
    channel_props = channel.GetAll(cs.CHANNEL, dbus_interface=cs.PROPERTIES_IFACE)
    assert channel_props.get('TargetHandle') == contact_handle, \
            (channel_props.get('TargetHandle'), contact_handle)
    assert channel_props.get('TargetHandleType') == cs.HT_CONTACT, \
            channel_props.get('TargetHandleType')
    assert channel_props.get('ChannelType') == channel_type, \
            channel_props.get('ChannelType')
    assert 'Interfaces' in channel_props, channel_props
    assert cs.CHANNEL_IFACE_GROUP not in \
            channel_props['Interfaces'], \
            channel_props['Interfaces']
    assert channel_props['TargetID'] == contact_id
    assert channel_props['Requested'] == True
    assert channel_props['InitiatorID'] == 'test@localhost'
    assert channel_props['InitiatorHandle'] == conn.GetSelfHandle()

    if channel_type == cs.CHANNEL_TYPE_TUBES:
        assert state is None
        assert len(channel_props['Interfaces']) == 0, channel_props['Interfaces']
        supported_socket_types = channel.GetAvailableStreamTubeTypes()
    else:
        assert state is not None
        tube_props = channel.GetAll(cs.CHANNEL_IFACE_TUBE,
                dbus_interface=cs.PROPERTIES_IFACE)
        assert tube_props['State'] == state
        # no strict check but at least check the properties exist
        assert tube_props['Parameters'] is not None
        assert channel_props['Interfaces'] == \
            dbus.Array([cs.CHANNEL_IFACE_TUBE], signature='s'), \
            channel_props['Interfaces']

        if channel_type == cs.CHANNEL_TYPE_STREAM_TUBE:
            supported_socket_types = channel.Get(cs.CHANNEL_TYPE_STREAM_TUBE,
                'SupportedSocketTypes', dbus_interface=cs.PROPERTIES_IFACE)
        else:
            supported_socket_types = None

    if supported_socket_types is not None:
        check_platform_socket_types(supported_socket_types)

class Echo(EventProtocol):
    """
    A trivial protocol that just echoes back whatever you send it, in lowercase.
    """
    def __init__(self, queue=None, block_reading=False):
        EventProtocol.__init__(self, queue, block_reading)

        self.echoed = True

    def dataReceived(self, data):
        EventProtocol.dataReceived(self, data)

        if self.echoed:
            self.transport.write(data.lower())

class EchoFactory(EventProtocolFactory):
    def _create_protocol(self):
        return Echo(self.queue, self.block_reading)

def set_up_echo(q, address_type, block_reading=False, streamfile='stream'):
    """
    Sets up an instance of Echo listening on a socket of type @address_type
    """
    factory = EchoFactory(q, block_reading)
    return create_server(q, address_type, factory, streamfile=streamfile)

# Twisted doesn't set the REUSEADDR option on client sockets.
# As we need this option to be able to bind successively on the same port
# during the tests, we define our own client and connector to be able to set
# this option.
class MyTCPClient(tcp.Client):
    def createInternetSocket(self):
        s = tcp.Client.createInternetSocket(self)
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        return s

class MyTCPConnector(tcp.Connector):
    def _makeTransport(self):
        return MyTCPClient(self.host, self.port, self.bindAddress, self, self.reactor)

def connect_socket(q, address_type, address, access_control, access_control_param):
    factory = EventProtocolClientFactory(q)
    if address_type == cs.SOCKET_ADDRESS_TYPE_UNIX:
        reactor.connectUNIX(address, factory)
    elif address_type == cs.SOCKET_ADDRESS_TYPE_IPV4:
        ip, port = address
        assert port > 0

        if access_control == cs.SOCKET_ACCESS_CONTROL_PORT:
            # connect from the ip/port specified
            # This means the test will fail if the port is already binded. It
            # would be better to bind the port before connecting but that's
            # not easily doable with twisted...
            c = MyTCPConnector(ip, port, factory, 30, access_control_param, reactor)
            c.connect()
        else:
            reactor.connectTCP(ip, port, factory)
    else:
        assert False

def create_server(q, address_type, factory=None, block_reading=False,
        streamfile='stream'):
    if factory is None:
        factory = EventProtocolFactory(q, block_reading)
    if address_type == cs.SOCKET_ADDRESS_TYPE_UNIX:
        path = os.getcwd() + '/' + streamfile
        try:
            os.remove(path)
        except OSError, e:
            if e.errno != errno.ENOENT:
                raise
        reactor.listenUNIX(path, factory)

        return dbus.ByteArray(path)

    elif address_type == cs.SOCKET_ADDRESS_TYPE_IPV4:
        for port in range(5000,6000):
            try:
                reactor.listenTCP(port, factory)
            except CannotListenError:
                continue
            else:
                return ('127.0.0.1', dbus.UInt16(port))

    else:
        assert False

def check_new_connection_access(q, access_control, access_control_param, protocol):
    if access_control == cs.SOCKET_ACCESS_CONTROL_LOCALHOST:
        # nothing to check
        return
    elif access_control == cs.SOCKET_ACCESS_CONTROL_PORT:
        ip, port = access_control_param
        address = protocol.transport.getPeer()
        assertEquals(ip, address.host)
        assertEquals(port, address.port)
    elif access_control == cs.SOCKET_ACCESS_CONTROL_CREDENTIALS:
        byte = access_control_param

        e = q.expect('socket-data', protocol=protocol)
        assert ord(e.data) == byte

        # FIXME: check if credentials are actually passed. This is actually
        # really hard to test because Python doesn't implement recvmsg().
        # Twisted's transport abstraction doesn't help either.
        # Credentials passing is tested in Gibber's tests so not testing it
        # here is actually not that bad.
    else:
        assert False


def connect_to_cm_socket(q, to, address_type, address, access_control,
    access_control_param):

    connect_socket(q, address_type, address, access_control, access_control_param)

    if access_control == cs.SOCKET_ACCESS_CONTROL_CREDENTIALS:
        socket_event = q.expect('socket-connected')

        # socket is connected. Let's send our credentials (the byte is
        # meaningless)
        socket_event.protocol.sendData('a')

        # once credentials have been sent, Gabble sends SI request
        si_event, sig = q.expect_many(
            EventPattern('stream-iq', to=to, query_ns=ns.SI, query_name='si'),
            EventPattern('dbus-signal', signal='NewLocalConnection'))

    else:
        socket_event, si_event, sig = q.expect_many(
            EventPattern('socket-connected'),
            # expect SI request
            EventPattern('stream-iq', to=to, query_ns=ns.SI, query_name='si'),
            EventPattern('dbus-signal', signal='NewLocalConnection'))

    connection_id = sig.args[0]
    assert connection_id != 0
    return socket_event, si_event, connection_id

def exec_tube_test(test, *args):
    for bytestream_cls in [
            bytestream.BytestreamIBBMsg,
            bytestream.BytestreamIBBIQ,
            bytestream.BytestreamS5B,
            bytestream.BytestreamSIFallbackS5CannotConnect,
            bytestream.BytestreamSIFallbackS5WrongHash,
            bytestream.BytestreamS5BRelay,
            bytestream.BytestreamS5BRelayBugged]:
        exec_test(lambda q, bus, conn, stream:
            test(q, bus, conn, stream, bytestream_cls, *args))

def exec_stream_tube_test(test):
    if os.name == 'posix':
        exec_tube_test(test, cs.SOCKET_ADDRESS_TYPE_UNIX, cs.SOCKET_ACCESS_CONTROL_LOCALHOST, "")
    exec_tube_test(test, cs.SOCKET_ADDRESS_TYPE_IPV4, cs.SOCKET_ACCESS_CONTROL_LOCALHOST, "")
    exec_tube_test(test, cs.SOCKET_ADDRESS_TYPE_IPV4, cs.SOCKET_ACCESS_CONTROL_PORT, ('127.0.0.1', dbus.UInt16(8631)))
    # we only guarantee to support credentials-passing on Linux
    if sys.platform == 'linux2':
        exec_tube_test(test, cs.SOCKET_ADDRESS_TYPE_UNIX, cs.SOCKET_ACCESS_CONTROL_CREDENTIALS, dbus.Byte(77))

def exec_dbus_tube_test(test):
    exec_tube_test(test, cs.SOCKET_ACCESS_CONTROL_CREDENTIALS)
    exec_tube_test(test, cs.SOCKET_ACCESS_CONTROL_LOCALHOST)
