import dbus
import socket
import hashlib
import time
import datetime
import os

from servicetest import EventPattern, assertEquals, assertSameSets, call_async
from gabbletest import exec_test, sync_stream, make_result_iq
import ns
from bytestream import create_from_si_offer, announce_socks5_proxy
import bytestream

from caps_helper import extract_data_forms, add_data_forms

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

        self.uri = 'file:///tmp/%s' % self.name

    def compute_hash(self, hash_type):
        assert hash_type == cs.FILE_HASH_TYPE_MD5
        self.hash_type = hash_type
        self.hash = hashlib.md5(self.data).hexdigest()

class FileTransferTest(object):
    CONTACT_NAME = 'test-ft@localhost'
    CONTACT_FULL_JID = 'test-ft@localhost/Telepathy'

    service_name = 'a.wacky.service.name'
    metadata = {'loads': ['of', 'blahblah', 'stuff'],
                'mental': ['data', 'sidf']}

    def __init__(self, bytestream_cls, file, address_type, access_control, access_control_param):
        self.file = file
        self.bytestream_cls = bytestream_cls
        self.address_type = address_type
        self.access_control = access_control
        self.access_control_param = access_control_param

    def check_platform_socket_types(self, sock_types):
        assertEquals(sock_types.get(cs.SOCKET_ADDRESS_TYPE_IPV4),
                [cs.SOCKET_ACCESS_CONTROL_LOCALHOST])
        assertEquals(sock_types.get(cs.SOCKET_ADDRESS_TYPE_IPV6),
                [cs.SOCKET_ACCESS_CONTROL_LOCALHOST])

        if os.name == 'posix':
            # true on at least Linux
            assertEquals(sock_types.get(cs.SOCKET_ADDRESS_TYPE_UNIX),
                    [cs.SOCKET_ACCESS_CONTROL_LOCALHOST])

    def connect(self):
        vcard_event, roster_event, disco_event = self.q.expect_many(
            EventPattern('stream-iq', to=None, query_ns='vcard-temp',
                query_name='vCard'),
            EventPattern('stream-iq', query_ns=ns.ROSTER),
            EventPattern('stream-iq', to='localhost', query_ns=ns.DISCO_ITEMS))

        roster = make_result_iq(self.stream, roster_event.stanza)
        query = roster.firstChildElement()
        item = query.addElement('item')
        item['jid'] = self.CONTACT_FULL_JID
        item['subscription'] = 'both'
        self.stream.send(roster)

        announce_socks5_proxy(self.q, self.stream, disco_event.stanza)

        self.self_handle = self.conn.Properties.Get(cs.CONN, "SelfHandle")
        self.self_handle_name =  self.conn.inspect_contact_sync(self.self_handle)

    def announce_contact(self, name=CONTACT_NAME, metadata=True):
        self.contact_name = name
        self.contact_full_jid = '%s/Telepathy' % name
        self.handle = self.conn.RequestHandles(cs.HT_CONTACT, [name])[0]

        presence = domish.Element(('jabber:client', 'presence'))
        presence['from'] = self.contact_full_jid
        presence['to'] = 'test@localhost/Resource'
        c = presence.addElement('c')
        c['xmlns'] = 'http://jabber.org/protocol/caps'
        c['node'] = 'http://example.com/ISupportFT'
        c['ver'] = '1.0'
        self.stream.send(presence)

        disco_event, presence_event = self.q.expect_many(
            EventPattern('stream-iq', iq_type='get',
                query_ns='http://jabber.org/protocol/disco#info', to=self.contact_full_jid),
            EventPattern('dbus-signal', signal='PresencesChanged', args=[
                {self.handle: (cs.PRESENCE_AVAILABLE, u'available', u'')}]))

        assert disco_event.query['node'] == \
            'http://example.com/ISupportFT#1.0'
        result = make_result_iq(self.stream, disco_event.stanza)
        query = result.firstChildElement()
        feature = query.addElement('feature')
        feature['var'] = ns.FILE_TRANSFER
        if metadata:
            feature = query.addElement('feature')
            feature['var'] = ns.TP_FT_METADATA
        self.stream.send(result)

        sync_stream(self.q, self.stream)

    def create_ft_channel(self):
        ft_chan = self.bus.get_object(self.conn.object.bus_name, self.ft_path)
        self.channel = dbus.Interface(ft_chan, cs.CHANNEL)
        self.ft_channel = dbus.Interface(ft_chan, cs.CHANNEL_TYPE_FILE_TRANSFER)
        self.ft_props = dbus.Interface(ft_chan, cs.PROPERTIES_IFACE)

    def close_channel(self):
        self.channel.Close()
        self.q.expect('dbus-signal', signal='Closed')

    def done(self):
        pass

    def test(self, q, bus, conn, stream):
        self.q = q
        self.bus = bus
        self.conn = conn
        self.stream = stream

        for fct in self._actions:
            # stop if a function returns True
            if fct():
                break

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
    def __init__(self, bytestream_cls, file, address_type, access_control, access_control_param):
        FileTransferTest.__init__(self, bytestream_cls, file, address_type, access_control, access_control_param)

        self._actions = [self.connect, self.announce_contact,
            self.send_ft_offer_iq, self.check_new_channel, self.create_ft_channel,
            self.set_uri, self.accept_file,
            self.receive_file, self.close_channel, self.done]

    def send_ft_offer_iq(self):
        self.bytestream = self.bytestream_cls(self.stream, self.q, 'alpha',
            self.contact_full_jid, 'test@localhost/Resource', True)

        iq, si = self.bytestream.create_si_offer(ns.FILE_TRANSFER)

        file_node = si.addElement((ns.FILE_TRANSFER,'file'))
        file_node['name'] = self.file.name
        file_node['size'] = str(self.file.size)
        file_node['mime-type'] = self.file.content_type
        file_node['hash'] = self.file.hash
        date = datetime.datetime.utcfromtimestamp(self.file.date).strftime('%FT%H:%M:%SZ')
        file_node['date'] = date

        file_node.addElement('desc', content=self.file.description)
        # we support range transfer
        file_node.addElement('range')

        # Metadata
        if self.service_name:
            service_form = {ns.TP_FT_METADATA_SERVICE: {'ServiceName': [self.service_name]}}
            add_data_forms(file_node, service_form)

        if self.metadata:
            metadata_form = {ns.TP_FT_METADATA: self.metadata}
            add_data_forms(file_node, metadata_form)

        # so... lunch?
        iq.send()

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

        channels = e.args[0]
        assert len(channels) == 1
        path, props = channels[0]

        # check channel properties
        # org.freedesktop.Telepathy.Channel D-Bus properties
        assert props[cs.CHANNEL_TYPE] == cs.CHANNEL_TYPE_FILE_TRANSFER
        assertSameSets(
            [ cs.CHANNEL_IFACE_FILE_TRANSFER_METADATA,
              cs.CHANNEL_TYPE_FILE_TRANSFER + '.FUTURE',
            ], props[cs.INTERFACES])
        assert props[cs.TARGET_HANDLE] == self.handle
        assert props[cs.TARGET_ID] == self.contact_name
        assert props[cs.TARGET_HANDLE_TYPE] == cs.HT_CONTACT
        assert props[cs.REQUESTED] == False
        assert props[cs.INITIATOR_HANDLE] == self.handle
        assert props[cs.INITIATOR_ID] == self.contact_name

        # org.freedesktop.Telepathy.Channel.Type.FileTransfer D-Bus properties
        assert props[cs.FT_STATE] == cs.FT_STATE_PENDING
        assert props[cs.FT_CONTENT_TYPE] == self.file.content_type
        assert props[cs.FT_FILENAME] == self.file.name
        assert props[cs.FT_SIZE] == self.file.size
        # FT's protocol doesn't allow us the send the hash info
        assert props[cs.FT_CONTENT_HASH_TYPE] == cs.FILE_HASH_TYPE_MD5
        assert props[cs.FT_CONTENT_HASH] == self.file.hash
        assert props[cs.FT_DESCRIPTION] == self.file.description
        assert props[cs.FT_DATE] == self.file.date
        assert props[cs.FT_TRANSFERRED_BYTES] == 0
        assert props[cs.FT_INITIAL_OFFSET] == 0

        self.check_platform_socket_types(props[cs.FT_AVAILABLE_SOCKET_TYPES])

        assertEquals(self.service_name, props[cs.FT_SERVICE_NAME])
        assertEquals(self.metadata, props[cs.FT_METADATA])

        self.ft_path = path

    def set_uri(self):
        ft_props = dbus.Interface(self.ft_channel, cs.PROPERTIES_IFACE)

        # URI is not set yet
        uri = ft_props.Get(cs.CHANNEL_TYPE_FILE_TRANSFER, 'URI')
        assertEquals('', uri)

        # Setting URI
        call_async(self.q, ft_props, 'Set',
            cs.CHANNEL_TYPE_FILE_TRANSFER, 'URI', self.file.uri)

        self.q.expect('dbus-signal', signal='URIDefined', args=[self.file.uri])

        self.q.expect('dbus-return', method='Set')

        # Check it has the right value now
        uri = ft_props.Get(cs.CHANNEL_TYPE_FILE_TRANSFER, 'URI')
        assertEquals(self.file.uri, uri)

        # We can't change it once it has been set
        call_async(self.q, ft_props, 'Set',
            cs.CHANNEL_TYPE_FILE_TRANSFER, 'URI', 'badger://snake')
        self.q.expect('dbus-error', method='Set', name=cs.INVALID_ARGUMENT)

    def accept_file(self):
        try:
            self.address = self.ft_channel.AcceptFile(self.address_type,
                self.access_control, self.access_control_param,
                self.file.offset,
                byte_arrays=True)
        except dbus.DBusException, e:
            if self.address_type == cs.SOCKET_ADDRESS_TYPE_IPV6 and \
                e.get_dbus_name() == cs.NOT_AVAILABLE and \
                e.get_dbus_message() == "Could not set up local socket":
                print "Ignoring error for ipv6 address space"
                return True
            else:
                raise e


        state_event, iq_event = self.q.expect_many(
            EventPattern('dbus-signal', signal='FileTransferStateChanged'),
            EventPattern('stream-iq', iq_type='result'))

        state, reason = state_event.args
        assert state == cs.FT_STATE_ACCEPTED
        assert reason == cs.FT_STATE_CHANGE_REASON_REQUESTED

        # Got SI reply
        self.bytestream.check_si_reply(iq_event.stanza)

        if self.file.offset != 0:
            range = xpath.queryForNodes('/iq/si/file/range', iq_event.stanza)[0]
            assert range['offset'] == str(self.file.offset)

        _, events = self.bytestream.open_bytestream([], [
            EventPattern('dbus-signal', signal='InitialOffsetDefined'),
            EventPattern('dbus-signal', signal='FileTransferStateChanged')])

        offset_event, state_event = events

        offset = offset_event.args[0]
        assert offset == self.file.offset

        state, reason = state_event.args
        assert state == cs.FT_STATE_OPEN
        assert reason == cs.FT_STATE_CHANGE_REASON_NONE

        # send the beginning of the file (client didn't connect to socket yet)
        self.bytestream.send_data(self.file.data[self.file.offset:self.file.offset + 2])

    def receive_file(self):
        # Connect to Gabble's socket
        s = self.create_socket()
        s.connect(self.address)

        # send the rest of the file
        i = self.file.offset + 2
        self.bytestream.send_data(self.file.data[i:])

        self._read_file_from_socket(s)

    def _read_file_from_socket(self, s):
        # Read the file from Gabble's socket
        data = ''
        read = 0
        to_receive = self.file.size - self.file.offset

        e = self.q.expect('dbus-signal', signal='TransferredBytesChanged')
        count = e.args[0]

        while True:
            received = s.recv(1024)
            if len(received) == 0:
                break
            data += received
        assert data == self.file.data[self.file.offset:]

        while count < to_receive:
            # Catch TransferredBytesChanged until we transfered all the data
            e = self.q.expect('dbus-signal', signal='TransferredBytesChanged')
            count = e.args[0]

        e = self.q.expect('dbus-signal', signal='FileTransferStateChanged')
        state, reason = e.args
        assert state == cs.FT_STATE_COMPLETED
        assert reason == cs.FT_STATE_CHANGE_REASON_NONE

class SendFileTest(FileTransferTest):
    def __init__(self, bytestream_cls, file, address_type, access_control, acces_control_param):
        FileTransferTest.__init__(self, bytestream_cls, file, address_type, access_control, acces_control_param)

        self._actions = [self.connect, self.announce_contact,
            self.check_ft_available, self.request_ft_channel, self.create_ft_channel,
            self.got_send_iq, self.provide_file, self.client_accept_file, self.send_file,
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

    def request_ft_channel(self, uri=True):
        request = { cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_FILE_TRANSFER,
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
            cs.FT_SERVICE_NAME: self.service_name,
            cs.FT_METADATA: dbus.Dictionary(self.metadata, signature='sas')}

        if uri:
            request[cs.FT_URI] = self.file.uri

        self.ft_path, props = self.conn.Requests.CreateChannel(request)

        # org.freedesktop.Telepathy.Channel D-Bus properties
        assertEquals(cs.CHANNEL_TYPE_FILE_TRANSFER, props[cs.CHANNEL_TYPE])
        assertSameSets(
            [ cs.CHANNEL_IFACE_FILE_TRANSFER_METADATA,
              cs.CHANNEL_TYPE_FILE_TRANSFER + '.FUTURE',
            ], props[cs.INTERFACES])
        assertEquals(self.handle, props[cs.TARGET_HANDLE])
        assertEquals(self.contact_name, props[cs.TARGET_ID])
        assertEquals(cs.HT_CONTACT, props[cs.TARGET_HANDLE_TYPE])
        assert props[cs.REQUESTED]
        assertEquals(self.self_handle, props[cs.INITIATOR_HANDLE])
        assertEquals(self.self_handle_name, props[cs.INITIATOR_ID])

        # org.freedesktop.Telepathy.Channel.Type.FileTransfer D-Bus properties
        assertEquals(cs.FT_STATE_PENDING, props[cs.FT_STATE])
        assertEquals(self.file.content_type, props[cs.FT_CONTENT_TYPE])
        assertEquals(self.file.name, props[cs.FT_FILENAME])
        assertEquals(self.file.size, props[cs.FT_SIZE])
        assertEquals(self.file.hash_type, props[cs.FT_CONTENT_HASH_TYPE])
        assertEquals(self.file.hash, props[cs.FT_CONTENT_HASH])
        assertEquals(self.file.description, props[cs.FT_DESCRIPTION])
        assertEquals(self.file.date, props[cs.FT_DATE])
        assertEquals(0, props[cs.FT_TRANSFERRED_BYTES])
        assertEquals(0, props[cs.FT_INITIAL_OFFSET])
        assertEquals(self.service_name, props[cs.FT_SERVICE_NAME])
        assertEquals(self.metadata, props[cs.FT_METADATA])
        if uri:
            assertEquals(self.file.uri, props[cs.FT_URI])
        else:
            assertEquals('', props[cs.FT_URI])

        self.check_platform_socket_types(props[cs.FT_AVAILABLE_SOCKET_TYPES])

    def got_send_iq(self):
        iq_event = self.q.expect('stream-iq', to=self.contact_full_jid)

        self._check_file_transfer_offer_iq(iq_event)

    def _check_file_transfer_offer_iq(self, iq_event):
        self.iq = iq_event.stanza
        self.bytestream, profile = create_from_si_offer(self.stream, self.q,
            self.bytestream_cls, iq_event.stanza, 'test@localhost/Resource')

        assert self.iq['to'] == self.contact_full_jid
        assert profile == ns.FILE_TRANSFER

        file_node = xpath.queryForNodes('/iq/si/file', self.iq)[0]
        assert file_node['name'] == self.file.name
        assert file_node['size'] == str(self.file.size)
        assert file_node['mime-type'] == self.file.content_type
        assert file_node['hash'] == self.file.hash

        date = datetime.datetime.utcfromtimestamp(self.file.date).strftime('%FT%H:%M:%SZ')
        assert file_node['date'] == date, file_node['date']

        desc_node = xpath.queryForNodes("/iq/si/file/desc", self.iq)[0]
        self.desc = desc_node.children[0]
        assert self.desc == self.file.description

        # Gabble supports resume
        range = xpath.queryForNodes('/iq/si/file/range', self.iq)[0]
        assert range is not None

        # Metadata forms
        forms = extract_data_forms(xpath.queryForNodes('/iq/si/file/x', self.iq))

        if self.service_name:
            assertEquals({'ServiceName': [self.service_name]},
                         forms[ns.TP_FT_METADATA_SERVICE])
        else:
            assert ns.TP_FT_METADATA_SERVICE not in forms

        if self.metadata:
            assertEquals(self.metadata, forms[ns.TP_FT_METADATA])
        else:
            assert ns.TP_FT_METADATA not in forms

    def provide_file(self):
        try:
            self.address = self.ft_channel.ProvideFile(self.address_type,
                self.access_control, self.access_control_param,
                byte_arrays=True)
        except dbus.DBusException, e:
            if self.address_type == cs.SOCKET_ADDRESS_TYPE_IPV6 and \
              e.get_dbus_name() == cs.NOT_AVAILABLE and \
              e.get_dbus_message() == "Could not set up local socket":
                print "Ignoring error for ipv6 address space"
                return True
            else:
                raise e

    def client_accept_file(self):
        # accept SI offer
        result, si = self.bytestream.create_si_reply(self.iq)
        file_node = si.addElement((ns.FILE_TRANSFER, 'file'))
        range = file_node.addElement('range')
        range['offset'] = str(self.file.offset)
        self.stream.send(result)

        self.bytestream.wait_bytestream_open()

    def send_file(self):
        s = self.create_socket()
        s.connect(self.address)
        s.send(self.file.data[self.file.offset:])

        to_receive = self.file.size - self.file.offset
        self.count = 0

        def bytes_changed_cb(bytes):
            self.count = bytes

        self.ft_channel.connect_to_signal('TransferredBytesChanged', bytes_changed_cb)

        # FileTransferStateChanged can be fired while we are receiving data
        # (in the SOCKS5 case for example)
        self.completed = False
        def ft_state_changed_cb(state, reason):
            if state == cs.FT_STATE_COMPLETED:
                self.completed = True
        self.ft_channel.connect_to_signal('FileTransferStateChanged', ft_state_changed_cb)

        # get data from bytestream
        data = ''
        while len(data) < to_receive:
            data += self.bytestream.get_data()

        assert data == self.file.data[self.file.offset:]

        if self.completed:
            # FileTransferStateChanged has already been received
            waiting = []
        else:
            waiting = [EventPattern('dbus-signal', signal='FileTransferStateChanged')]

        events = self.bytestream.wait_bytestream_closed(waiting)

        # If not all the bytes transferred have been announced using
        # TransferredBytesChanged, wait for them
        while self.count < to_receive:
            self.q.expect('dbus-signal', signal='TransferredBytesChanged')

        assert self.count == to_receive

        if len(waiting) > 1:
            state, reason = events[0].args
            assert state == cs.FT_STATE_COMPLETED
            assert reason == cs.FT_STATE_CHANGE_REASON_NONE

def platform_impls():
    impls = [
        (cs.SOCKET_ADDRESS_TYPE_IPV4, cs.SOCKET_ACCESS_CONTROL_LOCALHOST, ""),
        (cs.SOCKET_ADDRESS_TYPE_IPV6, cs.SOCKET_ACCESS_CONTROL_LOCALHOST, ""),
    ]

    if os.name == 'posix':
        impls.append((cs.SOCKET_ADDRESS_TYPE_UNIX,
            cs.SOCKET_ACCESS_CONTROL_LOCALHOST, ""))

    return impls

def exec_file_transfer_test(test_cls, one_run=False):
    for bytestream_cls  in [
            bytestream.BytestreamIBBMsg,
            bytestream.BytestreamS5B,
            bytestream.BytestreamS5BPidgin,
            bytestream.BytestreamSIFallbackS5CannotConnect,
            bytestream.BytestreamSIFallbackS5WrongHash,
            bytestream.BytestreamS5BRelay,
            bytestream.BytestreamS5BRelayBugged]:
        for addr_type, access_control, access_control_param in platform_impls():
            file = File()
            test = test_cls(bytestream_cls, file, addr_type, access_control, access_control_param)
            exec_test(test.test)

            # test resume
            file.offset = 5
            test = test_cls(bytestream_cls, file, addr_type, access_control, access_control_param)
            exec_test(test.test)

            if one_run:
                return
