import dbus
import socket
import hashlib
import time
import datetime

from servicetest import EventPattern
from gabbletest import exec_test, sync_stream, make_result_iq
import ns

from caps_helper import text_fixed_properties, text_allowed_properties, \
    stream_tube_fixed_properties, stream_tube_allowed_properties, \
    dbus_tube_fixed_properties, dbus_tube_allowed_properties, \
    ft_fixed_properties, ft_allowed_properties

from twisted.words.xish import domish, xpath

import constants as cs


class File(object):
    DEFAULT_DATA = "What a nice file"
    DEFAULT_NAME = "The foo.txt"
    DEFAULT_CONTENT_TYPE = 'text/plain'
    DEFAULT_DESCRIPTION = "A nice file to test"

    def __init__(self, data=DEFAULT_DATA, name=DEFAULT_NAME,
            content_type=DEFAULT_CONTENT_TYPE, description=DEFAULT_DESCRIPTION,
            hash_type=cs.FILE_HASH_TYPE_MD5):
        self.data = data
        self.size = len(self.data)
        self.name = name

        self.content_type = content_type
        self.description = description
        self.date = int(time.time())

        self.compute_hash(hash_type)

        self.offset = 0

    def compute_hash(self, hash_type):
        assert hash_type == cs.FILE_HASH_TYPE_MD5
        self.hash_type = hash_type
        self.hash = hashlib.md5(self.data).hexdigest()

generic_ft_caps = [(text_fixed_properties, text_allowed_properties),
                   (stream_tube_fixed_properties, \
                        stream_tube_allowed_properties),
                   (dbus_tube_fixed_properties, dbus_tube_allowed_properties),
                   (ft_fixed_properties, ft_allowed_properties)]

generic_caps = [(text_fixed_properties, text_allowed_properties),
                   (stream_tube_fixed_properties, \
                        stream_tube_allowed_properties),
                   (dbus_tube_fixed_properties, dbus_tube_allowed_properties)]

class FileTransferTest(object):
    def __init__(self, file, address_type, access_control, access_control_param):
        self.file = file
        self.address_type = address_type
        self.access_control = access_control
        self.access_control_param = access_control_param

    def connect(self):
        self.conn.Connect()

        self.q.expect('dbus-signal', signal='StatusChanged',
                      args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED],
                      path=self.conn.object.__dbus_object_path__)

        self.self_handle = self.conn.GetSelfHandle()
        self.self_handle_name =  self.conn.InspectHandles(cs.HT_CONTACT, [self.self_handle])[0]

    def set_target(self, jid):
        self.target = jid
        self.handle = self.conn.RequestHandles(cs.HT_CONTACT, [jid])[0]

    def set_ft_caps(self):
        caps_iface = dbus.Interface(self.conn, cs.CONN_IFACE_CONTACT_CAPS)
        caps_iface.UpdateCapabilities([("self",
                                        [ft_fixed_properties],
                                        dbus.Array([], signature="s"))])

        self.q.expect('dbus-signal', signal='ContactCapabilitiesChanged',
                 path=self.conn.object.__dbus_object_path__,
                 args=[{self.self_handle:generic_ft_caps}])

    def wait_for_ft_caps(self):
        conn_caps_iface = dbus.Interface(self.conn, cs.CONN_IFACE_CONTACT_CAPS)

        caps = conn_caps_iface.GetContactCapabilities([self.handle])
        if caps != dbus.Dictionary({self.handle:generic_ft_caps}):
            self.q.expect('dbus-signal',
                          signal='ContactCapabilitiesChanged',
                          path=self.conn.object.__dbus_object_path__,
                          args=[{self.handle:generic_ft_caps}])
            caps = conn_caps_iface.GetContactCapabilities([self.handle])
        assert caps == dbus.Dictionary({self.handle:generic_ft_caps}), caps

    def create_ft_channel(self):
        ft_chan = self.bus.get_object(self.conn.object.bus_name, self.ft_path)
        self.channel = dbus.Interface(ft_chan, cs.CHANNEL)
        self.ft_channel = dbus.Interface(ft_chan, cs.CHANNEL_TYPE_FILE_TRANSFER)
        self.ft_props = dbus.Interface(ft_chan, cs.PROPERTIES_IFACE)

    def close_channel(self):
        self.channel.Close()
        self.q.expect('dbus-signal', signal='Closed',
                      path=self.channel.__dbus_object_path__)

    def done(self):
        pass

    def test(self, q, bus, conn, stream):
        self.q = q
        self.bus = bus
        self.conn = conn
        self.stream = stream

    def create_socket(self):
        if self.address_type == cs.SOCKET_ADDRESS_TYPE_UNIX:
            return socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        elif self.address_type == cs.SOCKET_ADDRESS_TYPE_IPV4:
            return socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        elif self.address_type == cs.SOCKET_ADDRESS_TYPE_IPV6:
            return socket.socket(socket.AF_INET6, socket.SOCK_STREAM)
        else:
            assert False

class ReceiveFileTest(FileTransferTest):
    def __init__(self, file, address_type, access_control, access_control_param):
        FileTransferTest.__init__(self, file, address_type, access_control, access_control_param)

        self._actions = [self.connect, self.set_ft_caps, None,
                         self.wait_for_ft_caps, None,
                         self.check_new_channel, self.create_ft_channel,
                         self.accept_file, None,
                         self.receive_file, None,
                         self.close_channel, self.done]

    def check_new_channel(self):
        e = self.q.expect('dbus-signal', signal='NewChannels',
                          path=self.conn.object.__dbus_object_path__)
        channels = e.args[0]
        assert len(channels) == 1
        path, props = channels[0]

        # check channel properties
        # org.freedesktop.Telepathy.Channel D-Bus properties
        assert props[cs.CHANNEL_TYPE] == cs.CHANNEL_TYPE_FILE_TRANSFER, props
        assert props[cs.INTERFACES] == [], props
        assert props[cs.TARGET_HANDLE] == self.handle, props
        assert props[cs.TARGET_ID] == self.target, props
        assert props[cs.TARGET_HANDLE_TYPE] == cs.HT_CONTACT, props
        assert props[cs.REQUESTED] == False, props
        assert props[cs.INITIATOR_HANDLE] == self.handle, props
        assert props[cs.INITIATOR_ID] == self.target, props

        # org.freedesktop.Telepathy.Channel.Type.FileTransfer D-Bus properties
        assert props[cs.FT_STATE] == cs.FT_STATE_PENDING, props
        assert props[cs.FT_CONTENT_TYPE] == self.file.content_type, props
        assert props[cs.FT_FILENAME] == self.file.name, props
        assert props[cs.FT_SIZE] == self.file.size, props
        # FT's protocol doesn't allow us the send the hash info
        assert props[cs.FT_CONTENT_HASH_TYPE] == cs.FILE_HASH_TYPE_MD5, props
        assert props[cs.FT_CONTENT_HASH] == self.file.hash, props
        assert props[cs.FT_DESCRIPTION] == self.file.description, props
        assert props[cs.FT_DATE] == self.file.date, props
        assert props[cs.FT_AVAILABLE_SOCKET_TYPES] == \
            {cs.SOCKET_ADDRESS_TYPE_UNIX: [cs.SOCKET_ACCESS_CONTROL_LOCALHOST],
            cs.SOCKET_ADDRESS_TYPE_IPV4: [cs.SOCKET_ACCESS_CONTROL_LOCALHOST],
            cs.SOCKET_ADDRESS_TYPE_IPV6: [cs.SOCKET_ACCESS_CONTROL_LOCALHOST]}, \
            props[cs.FT_AVAILABLE_SOCKET_TYPES]
        assert props[cs.FT_TRANSFERRED_BYTES] == 0, props
        assert props[cs.FT_INITIAL_OFFSET] == 0, props

        self.ft_path = path

    def accept_file(self):
        self.address = self.ft_channel.AcceptFile(self.address_type,
                self.access_control, self.access_control_param, self.file.offset,
                byte_arrays=True)

        state_event = self.q.expect('dbus-signal',
                                    signal='FileTransferStateChanged',
                                    path=self.channel.__dbus_object_path__)

        state, reason = state_event.args
        assert state == cs.FT_STATE_ACCEPTED
        assert reason == cs.FT_STATE_CHANGE_REASON_REQUESTED

        offset_event, state_event = self.q.expect_many(
            EventPattern('dbus-signal', signal='InitialOffsetDefined',
                         path=self.channel.__dbus_object_path__),
            EventPattern('dbus-signal', signal='FileTransferStateChanged',
                         path=self.channel.__dbus_object_path__,
                         args=[cs.FT_STATE_OPEN, cs.FT_STATE_CHANGE_REASON_NONE]))

        offset = offset_event.args[0]
        assert offset == self.file.offset

        state, reason = state_event.args
        assert state == cs.FT_STATE_OPEN
        assert reason == cs.FT_STATE_CHANGE_REASON_NONE

    def receive_file(self):
        # Connect to Gabble's socket
        s = self.create_socket()
        s.connect(self.address)

        self._read_file_from_socket(s)

    def _read_file_from_socket(self, s):
        # Read the file from Gabble's socket
        data = ''
        read = 0
        to_receive = self.file.size - self.file.offset

        e = self.q.expect('dbus-signal', signal='TransferredBytesChanged',
                          path=self.channel.__dbus_object_path__)
        count = e.args[0]

        while True:
            received = s.recv(1024)
            if len(received) == 0:
                break
            data += received
        assert data == self.file.data[self.file.offset:]

        while count < to_receive:
            # Catch TransferredBytesChanged until we transfered all the data
            e = self.q.expect('dbus-signal', signal='TransferredBytesChanged',
                              path=self.channel.__dbus_object_path__)
            count = e.args[0]

        e = self.q.expect('dbus-signal', signal='FileTransferStateChanged',
                          path=self.channel.__dbus_object_path__)
        state, reason = e.args
        assert state == cs.FT_STATE_COMPLETED
        assert reason == cs.FT_STATE_CHANGE_REASON_NONE

class SendFileTest(FileTransferTest):
    def __init__(self, file, address_type,
                 access_control, acces_control_param):
        FileTransferTest.__init__(self, file, address_type,
                                  access_control, acces_control_param)

        self._actions = [self.connect, self.set_ft_caps,
                         self.check_ft_available, None,
                         self.wait_for_ft_caps, None,
                         self.request_ft_channel, self.create_ft_channel,
                         self.provide_file, None,
                         self.send_file, self.wait_for_completion, None,
                         self.close_channel, self.done]

    def check_ft_available(self):
        properties = self.conn.GetAll(cs.CONN_IFACE_REQUESTS,
                dbus_interface=cs.PROPERTIES_IFACE)

        # general FT class
        assert ({cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_FILE_TRANSFER,
                 cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT},
                [cs.FT_CONTENT_HASH_TYPE, cs.TARGET_HANDLE, cs.TARGET_ID, cs.FT_CONTENT_TYPE,
                 cs.FT_FILENAME, cs.FT_SIZE, cs.FT_CONTENT_HASH, cs.FT_DESCRIPTION, cs.FT_DATE]
             ) in properties.get('RequestableChannelClasses'),\
                     properties['RequestableChannelClasses']

        # FT class with MD5 as HashType
        assert ({cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_FILE_TRANSFER,
                 cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
                 cs.FT_CONTENT_HASH_TYPE: cs.FILE_HASH_TYPE_MD5},
                [cs.TARGET_HANDLE, cs.TARGET_ID, cs.FT_CONTENT_TYPE, cs.FT_FILENAME,
                 cs.FT_SIZE, cs.FT_CONTENT_HASH, cs.FT_DESCRIPTION, cs.FT_DATE]
             ) in properties.get('RequestableChannelClasses'),\
                     properties['RequestableChannelClasses']

    def request_ft_channel(self):
        requests_iface = dbus.Interface(self.conn, cs.CONN_IFACE_REQUESTS)

        self.ft_path, props = requests_iface.CreateChannel({
            cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_FILE_TRANSFER,
            cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
            cs.TARGET_HANDLE: self.handle,
            cs.FT_CONTENT_TYPE: self.file.content_type,
            cs.FT_FILENAME: self.file.name,
            cs.FT_SIZE: self.file.size,
            cs.FT_CONTENT_HASH_TYPE: self.file.hash_type,
            cs.FT_CONTENT_HASH: self.file.hash,
            cs.FT_DESCRIPTION: self.file.description,
            cs.FT_DATE:  self.file.date,
            cs.FT_INITIAL_OFFSET: 0,
            })

        # org.freedesktop.Telepathy.Channel D-Bus properties
        assert props[cs.CHANNEL_TYPE] == cs.CHANNEL_TYPE_FILE_TRANSFER
        assert props[cs.INTERFACES] == []
        assert props[cs.TARGET_HANDLE] == self.handle
        assert props[cs.TARGET_ID] == self.target
        assert props[cs.TARGET_HANDLE_TYPE] == cs.HT_CONTACT
        assert props[cs.REQUESTED] == True
        assert props[cs.INITIATOR_HANDLE] == self.self_handle
        assert props[cs.INITIATOR_ID] == self.self_handle_name

        # org.freedesktop.Telepathy.Channel.Type.FileTransfer D-Bus properties
        assert props[cs.FT_STATE] == cs.FT_STATE_PENDING
        assert props[cs.FT_CONTENT_TYPE] == self.file.content_type
        assert props[cs.FT_FILENAME] == self.file.name
        assert props[cs.FT_SIZE] == self.file.size
        assert props[cs.FT_CONTENT_HASH_TYPE] == self.file.hash_type
        assert props[cs.FT_CONTENT_HASH] == self.file.hash
        assert props[cs.FT_DESCRIPTION] == self.file.description
        assert props[cs.FT_DATE] == self.file.date
        assert props[cs.FT_AVAILABLE_SOCKET_TYPES] == \
            {cs.SOCKET_ADDRESS_TYPE_UNIX: [cs.SOCKET_ACCESS_CONTROL_LOCALHOST],
            cs.SOCKET_ADDRESS_TYPE_IPV4: [cs.SOCKET_ACCESS_CONTROL_LOCALHOST],
            cs.SOCKET_ADDRESS_TYPE_IPV6: [cs.SOCKET_ACCESS_CONTROL_LOCALHOST]}, \
            props[cs.FT_AVAILABLE_SOCKET_TYPES]
        assert props[cs.FT_TRANSFERRED_BYTES] == 0
        assert props[cs.FT_INITIAL_OFFSET] == 0


    def provide_file(self):
        self.address = self.ft_channel.ProvideFile(self.address_type,
                self.access_control, self.access_control_param,
                byte_arrays=True)

        self.open = False
        def ft_state_changed_cb(state, reason):
            if state == cs.FT_STATE_OPEN:
                self.open = True
        self.ft_channel.connect_to_signal('FileTransferStateChanged',
                                          ft_state_changed_cb)


    def send_file(self):

        if self.open is False:
            self.q.expect('dbus-signal', signal='FileTransferStateChanged',
                          path=self.channel.__dbus_object_path__,
                          args=[cs.FT_STATE_OPEN, cs.FT_STATE_CHANGE_REASON_NONE])
        s = self.create_socket()
        s.connect(self.address)
        s.send(self.file.data[self.file.offset:])

    def wait_for_completion(self):
        to_send = self.file.size - self.file.offset
        self.count = 0

        def bytes_changed_cb(bytes):
            self.count = bytes

        self.ft_channel.connect_to_signal('TransferredBytesChanged', bytes_changed_cb)


        # FileTransferStateChanged can be fired while we are receiving data
        self.completed = False
        def ft_state_changed_cb(state, reason):
            if state == cs.FT_STATE_COMPLETED:
                self.completed = True
        self.ft_channel.connect_to_signal('FileTransferStateChanged', ft_state_changed_cb)


        # If not all the bytes transferred have been announced using
        # TransferredBytesChanged, wait for them
        while self.count < to_send:
            self.q.expect('dbus-signal', signal='TransferredBytesChanged',
                          path=self.channel.__dbus_object_path__)

        assert self.count == to_send


def exec_file_transfer_test(send_cls, recv_cls):
        for addr_type, access_control, access_control_param in [
                (cs.SOCKET_ADDRESS_TYPE_UNIX, cs.SOCKET_ACCESS_CONTROL_LOCALHOST, ""),
                (cs.SOCKET_ADDRESS_TYPE_IPV4, cs.SOCKET_ACCESS_CONTROL_LOCALHOST, ""),
                (cs.SOCKET_ADDRESS_TYPE_IPV6, cs.SOCKET_ACCESS_CONTROL_LOCALHOST, "")]:

            file = File()
            def test(q, bus, conns, streams):
                conn1, conn2 = conns
                stream1, stream2 = streams
                send = send_cls(file, addr_type, access_control,
                                access_control_param)
                recv = recv_cls(file, addr_type, access_control,
                                access_control_param)
                send.test(q, bus, conn1, stream1)
                recv.test(q, bus, conn2, stream2)

                send_action = 0
                recv_action = 0
                target_set = False
                while send_action < len(send._actions) or \
                        recv_action < len(recv._actions):
                    for i in range(send_action, len(send._actions)):
                        action = send._actions[i]
                        if action is None:
                            break
                        print "Running Send action %s" % action.__name__
                        action()
                        print "Action finished"
                    send_action = i + 1

                    for i in range(recv_action, len(recv._actions)):
                        action = recv._actions[i]
                        if action is None:
                            break
                        print "Running Recv action %s" % action.__name__
                        action()
                        print "Action finished"
                    recv_action = i + 1

                    if target_set == False:
                        print "Setting target"
                        send.set_target(recv.self_handle_name)
                        recv.set_target(send.self_handle_name)
                        target_set = True

            exec_test(test, num_instances=2)

            # test resume
            #file.offset = 5
            #test = test_cls(file, addr_type, access_control, access_control_param)
            #exec_test(test.test)
