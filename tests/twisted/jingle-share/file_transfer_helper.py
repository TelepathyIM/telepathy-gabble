import dbus
import socket
import hashlib
import time
import datetime

from servicetest import EventPattern, assertEquals, assertLength, assertSameSets
from gabbletest import exec_test, sync_stream, make_result_iq, elem_iq, elem
import ns

from caps_helper import text_fixed_properties, text_allowed_properties, \
    stream_tube_fixed_properties, stream_tube_allowed_properties, \
    dbus_tube_fixed_properties, dbus_tube_allowed_properties, \
    ft_fixed_properties, ft_allowed_properties, compute_caps_hash, \
    extract_disco_parts

from twisted.words.xish import domish, xpath

import constants as cs
import sys


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
    caps_identities = None
    caps_features = None
    caps_dataforms = None
    caps_ft = None

    def __init__(self, file, address_type, access_control, access_control_param):
        self.file = file
        self.address_type = address_type
        self.access_control = access_control
        self.access_control_param = access_control_param
        self.closed = True

    def connect(self):
        self.conn.Connect()

        self.q.expect('dbus-signal', signal='StatusChanged',
                      args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED],
                      path=self.conn.object.object_path)

        self.self_handle = self.conn.Properties.Get(cs.CONN, "SelfHandle")
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
                 path=self.conn.object.object_path,
                 args=[{self.self_handle:generic_ft_caps}])

    def wait_for_ft_caps(self):
        conn_caps_iface = dbus.Interface(self.conn, cs.CONN_IFACE_CONTACT_CAPS)

        caps = conn_caps_iface.GetContactCapabilities([self.handle])
        if caps != dbus.Dictionary({self.handle:generic_ft_caps}):
            self.q.expect('dbus-signal',
                          signal='ContactCapabilitiesChanged',
                          path=self.conn.object.object_path,
                          args=[{self.handle:generic_ft_caps}])
            caps = conn_caps_iface.GetContactCapabilities([self.handle])
        assert caps == dbus.Dictionary({self.handle:generic_ft_caps}), caps

    def create_ft_channel(self):
        ft_chan = self.bus.get_object(self.conn.object.bus_name, self.ft_path)
        self.channel = dbus.Interface(ft_chan, cs.CHANNEL)
        self.ft_channel = dbus.Interface(ft_chan, cs.CHANNEL_TYPE_FILE_TRANSFER)
        self.ft_props = dbus.Interface(ft_chan, cs.PROPERTIES_IFACE)

        self.closed = False
        def channel_closed_cb():
            self.closed = True
        self.channel.connect_to_signal('Closed', channel_closed_cb)

    def close_channel(self):
        if self.closed is False:
            self.channel.Close()
            self.q.expect('dbus-signal', signal='Closed',
                          path=self.channel.object_path)

    def done(self):
        pass

    def test(self, q, bus, conn, stream):
        self.q = q
        self.bus = bus
        self.conn = conn
        self.stream = stream

        self.stream.addObserver(
            "//presence", self._cb_presence_iq, priority=1)
        self.stream.addObserver(
            "/iq/query[@xmlns='http://jabber.org/protocol/disco#info']",
            self._cb_disco_iq, priority=1)

    def _cb_presence_iq(self, stanza):
        nodes = xpath.queryForNodes("/presence/c", stanza)
        c = nodes[0]
        if 'share-v1' in c.getAttribute('ext'):
            assert FileTransferTest.caps_identities is not None and \
                FileTransferTest.caps_features is not None and \
                FileTransferTest.caps_dataforms is not None

            new_hash = compute_caps_hash(FileTransferTest.caps_identities,
                                         FileTransferTest.caps_features + \
                                             [ns.GOOGLE_FEAT_SHARE],
                                         FileTransferTest.caps_dataforms)
            # Replace ver hash from one with file-transfer ns to one without
            FileTransferTest.caps_ft = c.attributes['ver']
            c.attributes['ver'] = new_hash
        else:
            node = c.attributes['node']
            ver = c.attributes['ver']
            # ask for raw caps
            request = elem_iq(self.stream, 'get',
                              from_='fake_contact@jabber.org/resource')(
                elem(ns.DISCO_INFO, 'query', node=(node + '#' + ver)))
            self.stream.send(request)


    def _cb_disco_iq(self, iq):
        nodes = xpath.queryForNodes("/iq/query", iq)
        query = nodes[0]

        if query.getAttribute('node') is None:
            return

        node = query.attributes['node']
        ver = node.replace("http://telepathy.freedesktop.org/caps#", "")

        if iq.getAttribute('type') == 'result':

            if FileTransferTest.caps_identities is None or \
                    FileTransferTest.caps_features is None or \
                    FileTransferTest.caps_dataforms is None:
                # create our own identity
                identity_nodes = xpath.queryForNodes('/iq/query/identity', iq)
                assertLength(1, identity_nodes)
                identity_node = identity_nodes[0]

                identity_category = identity_node['category']
                identity_type = identity_node['type']
                identity_name = identity_node['name']
                identity = '%s/%s//%s' % (identity_category, identity_type,
                                          identity_name)

                _, features, dataforms = extract_disco_parts(iq)

                FileTransferTest.caps_identities = [identity]
                FileTransferTest.caps_features = features
                FileTransferTest.caps_dataforms = dataforms

                # Check if the hash matches the announced capabilities
                assertEquals(compute_caps_hash(FileTransferTest.caps_identities,
                                               FileTransferTest.caps_features,
                                               FileTransferTest.caps_dataforms), ver)

            if ver == FileTransferTest.caps_ft:
                caps_share = compute_caps_hash(FileTransferTest.caps_identities,
                                               FileTransferTest.caps_features + \
                                                   [ns.GOOGLE_FEAT_SHARE],
                                               FileTransferTest.caps_dataforms)
                n = query.attributes['node'].replace(ver, caps_share)
                query.attributes['node'] = n

                for feature in xpath.queryForNodes('/iq/query/feature', iq):
                        query.children.remove(feature)

                for f in FileTransferTest.caps_features + [ns.GOOGLE_FEAT_SHARE]:
                    el = domish.Element((None, 'feature'))
                    el['var'] = f
                    query.addChild(el)

        elif iq.getAttribute('type') == 'get':
            caps_share = compute_caps_hash(FileTransferTest.caps_identities,
                                           FileTransferTest.caps_features + \
                                               [ns.GOOGLE_FEAT_SHARE],
                                           FileTransferTest.caps_dataforms)

            if ver == caps_share:
                n = query.attributes['node'].replace(ver,
                                                     FileTransferTest.caps_ft)
                query.attributes['node'] = n

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
        FileTransferTest.__init__(self, file, address_type, access_control,
                                  access_control_param)

        self._actions = [self.connect, self.set_ft_caps, None,

                         self.wait_for_ft_caps, None,

                         self.check_new_channel,
                         self.accept_file, None,

                         self.receive_file, None,

                         self.close_channel, self.done]

    def check_new_channel(self):
        def is_ft_channel_event(event):
            channels, = event.args

            if len(channels) > 1:
                return False

            path, props = channels[0]
            return props[cs.CHANNEL_TYPE] == cs.CHANNEL_TYPE_FILE_TRANSFER

        e = self.q.expect('dbus-signal', signal='NewChannels',
            path=self.conn.object.object_path,
            predicate=is_ft_channel_event)

        channels, = e.args
        path, props = channels[0]

        # check channel properties
        # org.freedesktop.Telepathy.Channel D-Bus properties
        assert props[cs.CHANNEL_TYPE] == cs.CHANNEL_TYPE_FILE_TRANSFER, props
        assertSameSets(
            [ cs.CHANNEL_IFACE_FILE_TRANSFER_METADATA,
              cs.CHANNEL_TYPE_FILE_TRANSFER + '.FUTURE',
            ], props[cs.INTERFACES])
        assert props[cs.TARGET_HANDLE] == self.handle, props
        assert props[cs.TARGET_ID] == self.target, props
        assert props[cs.TARGET_HANDLE_TYPE] == cs.HT_CONTACT, props
        assert props[cs.REQUESTED] == False, props
        assert props[cs.INITIATOR_HANDLE] == self.handle, props
        assert props[cs.INITIATOR_ID] == self.target, props

        # org.freedesktop.Telepathy.Channel.Type.FileTransfer D-Bus properties
        assert props[cs.FT_STATE] == cs.FT_STATE_PENDING, props
        assert props[cs.FT_CONTENT_TYPE] == '', props
        assert props[cs.FT_FILENAME].encode('utf-8') == self.file.name, props
        assert props[cs.FT_SIZE] == self.file.size, props
        # FT's protocol doesn't allow us the send the hash info
        assert props[cs.FT_CONTENT_HASH_TYPE] == cs.FILE_HASH_TYPE_NONE, props
        assert props[cs.FT_CONTENT_HASH] == '', props
        assert props[cs.FT_DESCRIPTION] == '', props
        assert props[cs.FT_DATE] == 0, props
        assert props[cs.FT_AVAILABLE_SOCKET_TYPES] == \
            {cs.SOCKET_ADDRESS_TYPE_UNIX: [cs.SOCKET_ACCESS_CONTROL_LOCALHOST],
            cs.SOCKET_ADDRESS_TYPE_IPV4: [cs.SOCKET_ACCESS_CONTROL_LOCALHOST],
            cs.SOCKET_ADDRESS_TYPE_IPV6: [cs.SOCKET_ACCESS_CONTROL_LOCALHOST]}, \
            props[cs.FT_AVAILABLE_SOCKET_TYPES]
        assert props[cs.FT_TRANSFERRED_BYTES] == 0, props
        assert props[cs.FT_INITIAL_OFFSET] == 0, props

        self.ft_path = path

        self.create_ft_channel()

    def accept_file(self):
        self.address = self.ft_channel.AcceptFile(self.address_type,
                self.access_control, self.access_control_param, self.file.offset,
                byte_arrays=True)

        state_event = self.q.expect('dbus-signal',
                                    signal='FileTransferStateChanged',
                                    path=self.channel.object_path)

        state, reason = state_event.args
        assert state == cs.FT_STATE_ACCEPTED
        assert reason == cs.FT_STATE_CHANGE_REASON_REQUESTED


        state_event, offset_event = self.q.expect_many(
            EventPattern ('dbus-signal',
                          signal='FileTransferStateChanged',
                          path=self.channel.object_path),
            EventPattern ('dbus-signal',
                          signal='InitialOffsetDefined',
                          path=self.channel.object_path))

        offset = offset_event.args[0]
        assert offset == 0

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
        to_receive = self.file.size

        e = self.q.expect('dbus-signal', signal='TransferredBytesChanged',
                          path=self.channel.object_path)
        count = e.args[0]

        while True:
            received = s.recv(1024)
            if len(received) == 0:
                break
            data += received
        assert data == self.file.data

        while count < to_receive:
            # Catch TransferredBytesChanged until we transfered all the data
            e = self.q.expect('dbus-signal', signal='TransferredBytesChanged',
                              path=self.channel.object_path)
            count = e.args[0]

        e = self.q.expect('dbus-signal', signal='FileTransferStateChanged',
                          path=self.channel.object_path)
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

                         self.request_ft_channel, self.provide_file, None,

                         self.send_file, self.wait_for_completion, None,

                         self.close_channel, self.done]

    def check_ft_available(self):
        properties = self.conn.GetAll(cs.CONN_IFACE_REQUESTS,
                dbus_interface=cs.PROPERTIES_IFACE)

        # general FT class
        assert ({cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_FILE_TRANSFER,
                 cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT},
                [cs.FT_CONTENT_HASH_TYPE, cs.TARGET_HANDLE, cs.TARGET_ID, cs.FT_CONTENT_TYPE,
                 cs.FT_FILENAME, cs.FT_SIZE, cs.FT_CONTENT_HASH, cs.FT_DESCRIPTION, cs.FT_DATE,
                 cs.FT_URI, cs.FT_SERVICE_NAME, cs.FT_METADATA]
             ) in properties.get('RequestableChannelClasses'),\
                     properties['RequestableChannelClasses']

        # FT class with MD5 as HashType
        assert ({cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_FILE_TRANSFER,
                 cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
                 cs.FT_CONTENT_HASH_TYPE: cs.FILE_HASH_TYPE_MD5},
                [cs.TARGET_HANDLE, cs.TARGET_ID, cs.FT_CONTENT_TYPE, cs.FT_FILENAME,
                 cs.FT_SIZE, cs.FT_CONTENT_HASH, cs.FT_DESCRIPTION, cs.FT_DATE,
                 cs.FT_URI, cs.FT_SERVICE_NAME, cs.FT_METADATA]
             ) in properties.get('RequestableChannelClasses'),\
                     properties['RequestableChannelClasses']

    def request_ft_channel(self):
        self.ft_path, props = self.conn.Requests.CreateChannel({
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
        assertSameSets(
            [ cs.CHANNEL_IFACE_FILE_TRANSFER_METADATA,
              cs.CHANNEL_TYPE_FILE_TRANSFER + '.FUTURE',
            ], props[cs.INTERFACES])
        assert props[cs.TARGET_HANDLE] == self.handle
        assert props[cs.TARGET_ID] == self.target
        assert props[cs.TARGET_HANDLE_TYPE] == cs.HT_CONTACT
        assert props[cs.REQUESTED] == True
        assert props[cs.INITIATOR_HANDLE] == self.self_handle
        assert props[cs.INITIATOR_ID] == self.self_handle_name

        # org.freedesktop.Telepathy.Channel.Type.FileTransfer D-Bus properties
        assert props[cs.FT_STATE] == cs.FT_STATE_PENDING
        assert props[cs.FT_CONTENT_TYPE] == self.file.content_type
        assert props[cs.FT_FILENAME].encode('utf-8') == self.file.name, props
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

        self.create_ft_channel()

        self.open = False
        self.offset_defined = False

        def initial_offset_defined_cb(offset):
            self.offset_defined = True
            assert offset == 0, offset

        self.ft_channel.connect_to_signal('InitialOffsetDefined',
                                          initial_offset_defined_cb)


        # Make sure the file transfer is of type jingle-share
        event = self.q.expect('stream-iq', stream=self.stream,
                              query_name = 'session',
                              query_ns = ns.GOOGLE_SESSION)
        description_node = xpath.queryForNodes('/iq/session/description',
                                               event.stanza)[0]
        assert description_node.uri == ns.GOOGLE_SESSION_SHARE, \
            description_node.uri

    def provide_file(self):
        # try to accept our outgoing file transfer
        try:
            self.ft_channel.AcceptFile(self.address_type,
                self.access_control, self.access_control_param, self.file.offset,
                byte_arrays=True)
        except dbus.DBusException, e:
            assert e.get_dbus_name() == cs.NOT_AVAILABLE
        else:
            assert False

        # In case a unit test accepts the FT before we ProvideFile
        # then the ProvideFile will result in an OPEN state with reason
        state = self.ft_props.Get(cs.CHANNEL_TYPE_FILE_TRANSFER, 'State')
        if state == cs.FT_STATE_ACCEPTED:
            self.open_reason = cs.FT_STATE_CHANGE_REASON_REQUESTED
        else:
            self.open_reason = cs.FT_STATE_CHANGE_REASON_NONE

        self.address = self.ft_channel.ProvideFile(self.address_type,
                self.access_control, self.access_control_param,
                byte_arrays=True)


    def send_file(self):

        if self.open is False:
            self.q.expect('dbus-signal',
                          signal='FileTransferStateChanged',
                          path=self.channel.object_path,
                          args=[cs.FT_STATE_OPEN, self.open_reason])

        assert self.offset_defined == True

        s = self.create_socket()
        s.connect(self.address)
        s.send(self.file.data)

    def wait_for_completion(self):
        to_send = self.file.size
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
                          path=self.channel.object_path)

        assert self.count == to_send


def exec_file_transfer_test(send_cls, recv_cls, file = None):
        addr_type = cs.SOCKET_ADDRESS_TYPE_IPV4
        access_control = cs.SOCKET_ACCESS_CONTROL_LOCALHOST
        access_control_param = ""

        if file is None:
            file = File()

        def test(q, bus, conns, streams):
            q.timeout = 15
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
            done = False
            while send_action < len(send._actions) or \
                    recv_action < len(recv._actions):
                for i in range(send_action, len(send._actions)):
                    action = send._actions[i]
                    if action is None:
                        break
                    done = action()
                    if done is True:
                        break
                send_action = i + 1

                if done is True:
                    break

                for i in range(recv_action, len(recv._actions)):
                    action = recv._actions[i]
                    if action is None:
                        break
                    done =  action()
                    if done is True:
                        break
                recv_action = i + 1

                if done is True:
                    break

                if target_set == False:
                    send.set_target(recv.self_handle_name)
                    recv.set_target(send.self_handle_name)
                    target_set = True

        exec_test(test, num_instances=2, do_connect=False)
