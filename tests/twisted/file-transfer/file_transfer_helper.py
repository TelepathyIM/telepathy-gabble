import dbus
import socket
import md5
import time
import datetime

from servicetest import EventPattern
from gabbletest import exec_test, sync_stream
import ns
from bytestream import create_from_si_offer, announce_socks5_proxy
import bytestream

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
        m = md5.new()
        m.update(self.data)
        self.hash = m.hexdigest()

class FileTransferTest(object):
    CONTACT_NAME = 'test-ft@localhost'

    def __init__(self, bytestream_cls, file, address_type, access_control, access_control_param):
        self.file = file
        self.bytestream_cls = bytestream_cls
        self.address_type = address_type
        self.access_control = access_control
        self.access_control_param = access_control_param

    def connect(self):
        self.conn.Connect()

        _, vcard_event, roster_event, disco_event = self.q.expect_many(
            EventPattern('dbus-signal', signal='StatusChanged', args=[0, 1]),
            EventPattern('stream-iq', to=None, query_ns='vcard-temp',
                query_name='vCard'),
            EventPattern('stream-iq', query_ns='jabber:iq:roster'),
            EventPattern('stream-iq', to='localhost', query_ns=ns.DISCO_ITEMS))

        roster = roster_event.stanza
        roster['type'] = 'result'
        item = roster_event.query.addElement('item')
        item['jid'] = self.CONTACT_NAME
        item['subscription'] = 'both'
        self.stream.send(roster)

        announce_socks5_proxy(self.q, self.stream, disco_event.stanza)

        self.self_handle = self.conn.GetSelfHandle()
        self.self_handle_name =  self.conn.InspectHandles(cs.HT_CONTACT, [self.self_handle])[0]

    def announce_contact(self, name=CONTACT_NAME):
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
            EventPattern('dbus-signal', signal='PresencesChanged'))

        result = disco_event.stanza
        result['type'] = 'result'
        assert disco_event.query['node'] == \
            'http://example.com/ISupportFT#1.0'
        feature = disco_event.query.addElement('feature')
        feature['var'] = ns.FILE_TRANSFER
        self.stream.send(result)

        h = presence_event.args[0].keys()[0]
        assert h == self.handle

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
        self.conn.Disconnect()
        self.q.expect('dbus-signal', signal='StatusChanged', args=[2, 1])

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
            self.send_ft_offer_iq, self.check_new_channel, self.create_ft_channel, self.accept_file,
            self.receive_file, self.close_channel, self.done]

    def send_ft_offer_iq(self):
        self.bytestream = self.bytestream_cls(self.stream, self.q, 'alpha',
            self.contact_name, 'test@localhost/Resource', True)

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
        iq.send()

    def check_new_channel(self):
        e = self.q.expect('dbus-signal', signal='NewChannels')
        channels = e.args[0]
        assert len(channels) == 1
        path, props = channels[0]

        # check channel properties
        # org.freedesktop.Telepathy.Channel D-Bus properties
        assert props[cs.CHANNEL_TYPE] == cs.CHANNEL_TYPE_FILE_TRANSFER
        assert props[cs.INTERFACES] == []
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
        assert props[cs.FT_AVAILABLE_SOCKET_TYPES] == \
            {cs.SOCKET_ADDRESS_TYPE_UNIX: [cs.SOCKET_ACCESS_CONTROL_LOCALHOST],
            cs.SOCKET_ADDRESS_TYPE_IPV4: [cs.SOCKET_ACCESS_CONTROL_LOCALHOST],
            cs.SOCKET_ADDRESS_TYPE_IPV6: [cs.SOCKET_ACCESS_CONTROL_LOCALHOST]}, \
            props[cs.FT_AVAILABLE_SOCKET_TYPES]
        assert props[cs.FT_TRANSFERRED_BYTES] == 0
        assert props[cs.FT_INITIAL_OFFSET] == 0

        self.ft_path = path

    def accept_file(self):
        self.address = self.ft_channel.AcceptFile(self.address_type,
                self.access_control, self.access_control_param, self.file.offset,
                byte_arrays=True)

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

        while read < to_receive:
            data += s.recv(to_receive - read)
            read = len(data)
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
        assert props[cs.TARGET_ID] == self.contact_name
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

    def provide_file(self):
        self.address = self.ft_channel.ProvideFile(self.address_type,
                self.access_control, self.access_control_param,
                byte_arrays=True)

    def client_accept_file(self):
        # accept SI offer
        result, si = self.bytestream.create_si_reply(self.iq)
        self.stream.send(result)

        self.bytestream.wait_bytestream_open()

    def send_file(self):
        s = self.create_socket()
        s.connect(self.address)
        s.send(self.file.data)

        self.count = 0

        def bytes_changed_cb(bytes):
            self.count = bytes

        self.ft_channel.connect_to_signal('TransferredBytesChanged', bytes_changed_cb)

        # get data from bytestream
        data = ''
        while len(data) < self.file.size:
            data += self.bytestream.get_data()

        assert data == self.file.data

        # If not all the bytes transferred have been announced using
        # TransferredBytesChanged, wait for them
        while self.count < self.file.size:
            self.q.expect('dbus-signal', signal='TransferredBytesChanged')

        assert self.count == self.file.size

        # FileTransferStateChanged could have already been fired
        events = self.bytestream.wait_bytestream_closed(
            [EventPattern('dbus-signal', signal='FileTransferStateChanged')])

        state, reason = events[0].args
        assert state == cs.FT_STATE_COMPLETED
        assert reason == cs.FT_STATE_CHANGE_REASON_NONE

def exec_file_transfer_test(test_cls):
    for bytestream_cls  in [
            bytestream.BytestreamIBBMsg,
            bytestream.BytestreamS5B,
            bytestream.BytestreamS5BPidgin,
            bytestream.BytestreamSIFallbackS5CannotConnect,
            bytestream.BytestreamSIFallbackS5WrongHash,
            bytestream.BytestreamS5BRelay,
            bytestream.BytestreamS5BRelayBugged]:
        for addr_type, access_control, access_control_param in [
                (cs.SOCKET_ADDRESS_TYPE_UNIX, cs.SOCKET_ACCESS_CONTROL_LOCALHOST, ""),
                (cs.SOCKET_ADDRESS_TYPE_IPV4, cs.SOCKET_ACCESS_CONTROL_LOCALHOST, ""),
                (cs.SOCKET_ADDRESS_TYPE_IPV6, cs.SOCKET_ACCESS_CONTROL_LOCALHOST, "")]:

            file = File()
            test = test_cls(bytestream_cls, file, addr_type, access_control, access_control_param)
            exec_test(test.test)

            # test resume
            file.offset = 5
            test = test_cls(bytestream_cls, file, addr_type, access_control, access_control_param)
            exec_test(test.test)
