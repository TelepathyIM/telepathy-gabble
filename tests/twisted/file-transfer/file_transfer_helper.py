import dbus
import socket
import md5
import time

from servicetest import EventPattern
from gabbletest import acknowledge_iq, sync_stream
import ns
from bytestream import parse_si_offer, create_si_reply, parse_ibb_open, parse_ibb_msg_data,\
    create_si_offer, parse_si_reply, send_ibb_open, send_ibb_msg_data, listen_socks5, \
    send_socks5_init, socks5_expect_connection, expect_socks5_init, socks5_connect, \
    send_socks5_reply

from twisted.words.xish import domish, xpath
from twisted.words.protocols.jabber.client import IQ

from dbus import PROPERTIES_IFACE

CONNECTION_INTERFACE_REQUESTS = 'org.freedesktop.Telepathy.Connection.Interface.Requests'
CHANNEL_INTERFACE ='org.freedesktop.Telepathy.Channel'
CHANNEL_TYPE_FILE_TRANSFER = 'org.freedesktop.Telepathy.Channel.Type.FileTransfer'
HT_CONTACT = 1
HT_CONTACT_LIST = 3

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

SOCKET_ADDRESS_TYPE_UNIX = 0
SOCKET_ADDRESS_TYPE_ABSTRACT_UNIX = 1
SOCKET_ADDRESS_TYPE_IPV4 = 2
SOCKET_ADDRESS_TYPE_IPV6 = 3

SOCKET_ACCESS_CONTROL_LOCALHOST = 0
SOCKET_ACCESS_CONTROL_PORT = 1
SOCKET_ACCESS_CONTROL_NETMASK = 2
SOCKET_ACCESS_CONTROL_CREDENTIALS = 3

class File(object):
    DEFAULT_DATA = "What a nice file"
    DEFAULT_NAME = "The foo.txt"
    DEFAULT_CONTENT_TYPE = 'text/plain'
    DEFAULT_DESCRIPTION = "A nice file to test"

    def __init__(self, data=DEFAULT_DATA, name=DEFAULT_NAME,
            content_type=DEFAULT_CONTENT_TYPE, description=DEFAULT_DESCRIPTION,
            hash_type=FILE_HASH_TYPE_MD5):
        self.data = data
        self.size = len(self.data)
        self.name = name

        self.content_type = content_type
        self.description = description
        self.date = int(time.time())

        self.compute_hash(hash_type)

    def compute_hash(self, hash_type):
        assert hash_type == FILE_HASH_TYPE_MD5

        self.hash_type = hash_type
        m = md5.new()
        m.update(self.data)
        self.hash = m.hexdigest()

class FileTransferTest(object):
    CONTACT_NAME = 'test-ft@localhost'

    def __init__(self, bytestream_cls):
        self.file = File()
        self.bytestream_cls = bytestream_cls

    def connect(self):
        self.conn.Connect()

        _, vcard_event, roster_event = self.q.expect_many(
            EventPattern('dbus-signal', signal='StatusChanged', args=[0, 1]),
            EventPattern('stream-iq', to=None, query_ns='vcard-temp',
                query_name='vCard'),
            EventPattern('stream-iq', query_ns='jabber:iq:roster'))

        roster = roster_event.stanza
        roster['type'] = 'result'
        item = roster_event.query.addElement('item')
        item['jid'] = self.CONTACT_NAME
        item['subscription'] = 'both'
        self.stream.send(roster)

        self.self_handle = self.conn.GetSelfHandle()
        self.self_handle_name =  self.conn.InspectHandles(HT_CONTACT, [self.self_handle])[0]

    def announce_contact(self, name=CONTACT_NAME):
        self.contact_name = name
        self.contact_full_jid = '%s/Telepathy' % name
        self.handle = self.conn.RequestHandles(HT_CONTACT, [name])[0]

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

    def create_ft_channel(self):
        ft_chan = self.bus.get_object(self.conn.object.bus_name, self.ft_path)
        self.channel = dbus.Interface(ft_chan, CHANNEL_INTERFACE)
        self.ft_channel = dbus.Interface(ft_chan, CHANNEL_TYPE_FILE_TRANSFER)
        self.ft_props = dbus.Interface(ft_chan, PROPERTIES_IFACE)

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

        self.bytestream = self.bytestream_cls(stream, q)

        for fct in self._actions:
            # stop if a function returns True
            if fct():
                break

class ReceiveFileTest(FileTransferTest):
    def __init__(self, bytestream_cls):
        FileTransferTest.__init__(self, bytestream_cls)

        self._actions = [self.connect, self.announce_contact,
            self.send_ft_offer_iq, self.check_new_channel, self.create_ft_channel, self.accept_file,
            self.receive_file, self.close_channel, self.done]

    def send_ft_offer_iq(self):
        iq, si = create_si_offer(self.stream, self.contact_name, 'test@localhost/Resource', 'alpha',
            ns.FILE_TRANSFER, [self.bytestream.get_ns()])

        file_node = si.addElement((ns.FILE_TRANSFER,'file'))
        file_node['name'] = self.file.name
        file_node['size'] = str(self.file.size)
        file_node['mime-type'] = self.file.content_type
        file_node['hash'] = self.file.hash
        # TODO: date
        # TODO: intial offset

        file_node.addElement('desc', content=self.file.description)
        iq.send()

    def check_new_channel(self):
        e = self.q.expect('dbus-signal', signal='NewChannels')
        channels = e.args[0]
        assert len(channels) == 1
        path, props = channels[0]

        # check channel properties
        # org.freedesktop.Telepathy.Channel D-Bus properties
        assert props[CHANNEL_INTERFACE + '.ChannelType'] == CHANNEL_TYPE_FILE_TRANSFER
        assert props[CHANNEL_INTERFACE + '.Interfaces'] == []
        assert props[CHANNEL_INTERFACE + '.TargetHandle'] == self.handle
        assert props[CHANNEL_INTERFACE + '.TargetID'] == self.contact_name
        assert props[CHANNEL_INTERFACE + '.TargetHandleType'] == HT_CONTACT
        assert props[CHANNEL_INTERFACE + '.Requested'] == False
        assert props[CHANNEL_INTERFACE + '.InitiatorHandle'] == self.handle
        assert props[CHANNEL_INTERFACE + '.InitiatorID'] == self.contact_name

        # org.freedesktop.Telepathy.Channel.Type.FileTransfer D-Bus properties
        assert props[CHANNEL_TYPE_FILE_TRANSFER + '.State'] == FT_STATE_PENDING
        assert props[CHANNEL_TYPE_FILE_TRANSFER + '.ContentType'] == self.file.content_type
        assert props[CHANNEL_TYPE_FILE_TRANSFER + '.Filename'] == self.file.name
        assert props[CHANNEL_TYPE_FILE_TRANSFER + '.Size'] == self.file.size
        # FT's protocol doesn't allow us the send the hash info
        assert props[CHANNEL_TYPE_FILE_TRANSFER + '.ContentHashType'] == FILE_HASH_TYPE_MD5
        assert props[CHANNEL_TYPE_FILE_TRANSFER + '.ContentHash'] == self.file.hash
        assert props[CHANNEL_TYPE_FILE_TRANSFER + '.Description'] == self.file.description
        # FT's protocol doesn't allow us the send the date info
        assert props[CHANNEL_TYPE_FILE_TRANSFER + '.Date'] == 0
        assert props[CHANNEL_TYPE_FILE_TRANSFER + '.AvailableSocketTypes'] == \
            {SOCKET_ADDRESS_TYPE_UNIX: [SOCKET_ACCESS_CONTROL_LOCALHOST]}
        assert props[CHANNEL_TYPE_FILE_TRANSFER + '.TransferredBytes'] == 0
        assert props[CHANNEL_TYPE_FILE_TRANSFER + '.InitialOffset'] == 0

        self.ft_path = path

    def accept_file(self):
        self.address = self.ft_channel.AcceptFile(SOCKET_ADDRESS_TYPE_UNIX,
                SOCKET_ACCESS_CONTROL_LOCALHOST, "", 5)

        state_event, iq_event = self.q.expect_many(
            EventPattern('dbus-signal', signal='FileTransferStateChanged'),
            EventPattern('stream-iq', iq_type='result'))

        state, reason = state_event.args
        assert state == FT_STATE_ACCEPTED
        assert reason == FT_STATE_CHANGE_REASON_REQUESTED

        # Got SI reply
        bytestream = parse_si_reply(iq_event.stanza)
        assert bytestream == self.bytestream.get_ns()

        offset_event, state_event = self.bytestream.open_bytestream(
            self.contact_name, 'test@localhost/Resource')

        offset = offset_event.args[0]
        # We don't support resume
        assert offset == 0

        state, reason = state_event.args
        assert state == FT_STATE_OPEN
        assert reason == FT_STATE_CHANGE_REASON_NONE

        # send the beginning of the file (client didn't connect to socket yet)
        self.bytestream.send_data(self.contact_name, 'test@localhost/Resource', self.file.data[:2])

    def receive_file(self):
        # Connect to Salut's socket
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        s.connect(self.address)

        # send the rest of the file
        self.bytestream.send_data(self.contact_name, 'test@localhost/Resource', self.file.data[2:])

        self._read_file_from_socket(s)

    def _read_file_from_socket(self, s):
        # Read the file from Salut's socket
        data = ''
        read = 0

        e = self.q.expect('dbus-signal', signal='TransferredBytesChanged')
        count = e.args[0]

        while read < self.file.size:
            data += s.recv(self.file.size - read)
            read = len(data)
        assert data == self.file.data

        while count < self.file.size:
            # Catch TransferredBytesChanged until we transfered all the data
            e = self.q.expect('dbus-signal', signal='TransferredBytesChanged')
            count = e.args[0]

        e = self.q.expect('dbus-signal', signal='FileTransferStateChanged')
        state, reason = e.args
        assert state == FT_STATE_COMPLETED
        assert reason == FT_STATE_CHANGE_REASON_NONE

class SendFileTest(FileTransferTest):
    def __init__(self, bytestream_cls):
        FileTransferTest.__init__(self, bytestream_cls)

        self._actions = [self.connect, self.announce_contact,
            self.check_ft_available, self.request_ft_channel, self.create_ft_channel,
            self.got_send_iq, self.provide_file, self.client_accept_file, self.send_file,
            self.close_channel, self.done]

    def check_ft_available(self):
        properties = self.conn.GetAll(
                CONNECTION_INTERFACE_REQUESTS,
                dbus_interface=PROPERTIES_IFACE)

        assert ({CHANNEL_INTERFACE + '.ChannelType': CHANNEL_TYPE_FILE_TRANSFER,
                 CHANNEL_INTERFACE + '.TargetHandleType': HT_CONTACT},
                [CHANNEL_INTERFACE + '.TargetHandle',
                 CHANNEL_INTERFACE + '.TargetID',
                 CHANNEL_TYPE_FILE_TRANSFER + '.ContentType',
                 CHANNEL_TYPE_FILE_TRANSFER + '.Filename',
                 CHANNEL_TYPE_FILE_TRANSFER + '.Size',
                 CHANNEL_TYPE_FILE_TRANSFER + '.ContentHashType',
                 CHANNEL_TYPE_FILE_TRANSFER + '.ContentHash',
                 CHANNEL_TYPE_FILE_TRANSFER + '.Description',
                 CHANNEL_TYPE_FILE_TRANSFER + '.Date',
                 CHANNEL_TYPE_FILE_TRANSFER + '.InitialOffset'],
             ) in properties.get('RequestableChannelClasses'),\
                     properties['RequestableChannelClasses']

    def request_ft_channel(self):
        requests_iface = dbus.Interface(self.conn, CONNECTION_INTERFACE_REQUESTS)

        self.ft_path, props = requests_iface.CreateChannel({
            CHANNEL_INTERFACE + '.ChannelType': CHANNEL_TYPE_FILE_TRANSFER,
            CHANNEL_INTERFACE + '.TargetHandleType': HT_CONTACT,
            CHANNEL_INTERFACE + '.TargetHandle': self.handle,
            CHANNEL_TYPE_FILE_TRANSFER + '.ContentType': self.file.content_type,
            CHANNEL_TYPE_FILE_TRANSFER + '.Filename': self.file.name,
            CHANNEL_TYPE_FILE_TRANSFER + '.Size': self.file.size,
            CHANNEL_TYPE_FILE_TRANSFER + '.ContentHashType': self.file.hash_type,
            CHANNEL_TYPE_FILE_TRANSFER + '.ContentHash': self.file.hash,
            CHANNEL_TYPE_FILE_TRANSFER + '.Description': self.file.description,
            CHANNEL_TYPE_FILE_TRANSFER + '.Date':  self.file.date,
            CHANNEL_TYPE_FILE_TRANSFER + '.InitialOffset': 0,
            })

        # org.freedesktop.Telepathy.Channel D-Bus properties
        assert props[CHANNEL_INTERFACE + '.ChannelType'] == CHANNEL_TYPE_FILE_TRANSFER
        assert props[CHANNEL_INTERFACE + '.Interfaces'] == []
        assert props[CHANNEL_INTERFACE + '.TargetHandle'] == self.handle
        assert props[CHANNEL_INTERFACE + '.TargetID'] == self.contact_name
        assert props[CHANNEL_INTERFACE + '.TargetHandleType'] == HT_CONTACT
        assert props[CHANNEL_INTERFACE + '.Requested'] == True
        assert props[CHANNEL_INTERFACE + '.InitiatorHandle'] == self.self_handle
        assert props[CHANNEL_INTERFACE + '.InitiatorID'] == self.self_handle_name

        # org.freedesktop.Telepathy.Channel.Type.FileTransfer D-Bus properties
        assert props[CHANNEL_TYPE_FILE_TRANSFER + '.State'] == FT_STATE_PENDING
        assert props[CHANNEL_TYPE_FILE_TRANSFER + '.ContentType'] == self.file.content_type
        assert props[CHANNEL_TYPE_FILE_TRANSFER + '.Filename'] == self.file.name
        assert props[CHANNEL_TYPE_FILE_TRANSFER + '.Size'] == self.file.size
        assert props[CHANNEL_TYPE_FILE_TRANSFER + '.ContentHashType'] == self.file.hash_type
        assert props[CHANNEL_TYPE_FILE_TRANSFER + '.ContentHash'] == self.file.hash
        assert props[CHANNEL_TYPE_FILE_TRANSFER + '.Description'] == self.file.description
        assert props[CHANNEL_TYPE_FILE_TRANSFER + '.Date'] == self.file.date
        assert props[CHANNEL_TYPE_FILE_TRANSFER + '.AvailableSocketTypes'] == \
            {SOCKET_ADDRESS_TYPE_UNIX: [SOCKET_ACCESS_CONTROL_LOCALHOST]}
        assert props[CHANNEL_TYPE_FILE_TRANSFER + '.TransferredBytes'] == 0
        assert props[CHANNEL_TYPE_FILE_TRANSFER + '.InitialOffset'] == 0

    def got_send_iq(self):
        iq_event = self.q.expect('stream-iq', to=self.contact_full_jid)

        self._check_file_transfer_offer_iq(iq_event)

    def _check_file_transfer_offer_iq(self, iq_event):
        self.iq = iq_event.stanza
        profile, self.bytestream.stream_id, bytestreams = parse_si_offer(self.iq)
        assert self.iq['to'] == self.contact_full_jid
        assert profile == ns.FILE_TRANSFER
        assert bytestreams == [ns.BYTESTREAMS, ns.IBB]

        file_node = xpath.queryForNodes('/iq/si/file', self.iq)[0]
        assert file_node['name'] == self.file.name
        assert file_node['size'] == str(self.file.size)
        assert file_node['mime-type'] == self.file.content_type
        assert file_node['hash'] == self.file.hash

        # TODO: Date
        # TODO: InitialOffset

        desc_node = xpath.queryForNodes("/iq/si/file/desc", self.iq)[0]
        self.desc = desc_node.children[0]
        assert self.desc == self.file.description

    def provide_file(self):
        self.address = self.ft_channel.ProvideFile(SOCKET_ADDRESS_TYPE_UNIX,
                SOCKET_ACCESS_CONTROL_LOCALHOST, "")

    def client_accept_file(self):
        # accept SI offer
        result = create_si_reply(self.stream, self.iq, 'test@localhost/Resource',
            self.bytestream.get_ns())
        self.stream.send(result)

        self.bytestream.wait_bytestream_open(self.contact_name, 'test@localhost/Resource')

    def send_file(self):
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
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

        # The bytes transferred has been announced using
        # TransferredBytesChanged
        assert self.count == self.file.size

        # FileTransferStateChanged could have already been fired
        e = self.q.expect('dbus-signal', signal='FileTransferStateChanged')

        self.bytestream.wait_bytestream_closed()

        state, reason = e.args
        assert state == FT_STATE_COMPLETED
        assert reason == FT_STATE_CHANGE_REASON_NONE

class Bytestream(object):
    def __init__(self, stream, q):
        self.stream = stream
        self.q = q

        self.stream_id = None

    def open_bytestream(self, from_, to_):
        # Open the bytestream and return the InitialOffsetDefined and
        # FileTransferStateChanged events
        raise NotImplemented

    def send_data(self, from_, to, data):
        raise NotImplemented

    def get_ns(self):
        raise NotImplemented

    def wait_bytestream_open(self, from_, to):
        raise NotImplemented

    def get_data(self):
        raise NotImplemented

    def wait_bytestream_closed(self):
        raise NotImplemented

class BytestreamIBB(Bytestream):
    def __init__(self, stream, q):
        Bytestream.__init__(self, stream, q)

        self.seq = 0

    def get_ns(self):
        return ns.IBB

    def open_bytestream(self, from_, to):
        # open IBB bytestream
        send_ibb_open(self.stream, from_, to, 'alpha', 4096)

        _, offset_event, state_event = self.q.expect_many(
            EventPattern('stream-iq', iq_type='result'),
            EventPattern('dbus-signal', signal='InitialOffsetDefined'),
            EventPattern('dbus-signal', signal='FileTransferStateChanged'))

        return offset_event, state_event

    def send_data(self, from_, to, data):
        send_ibb_msg_data(self.stream, from_, to, 'alpha', self.seq, data)
        sync_stream(self.q, self.stream)

        self.seq += 1

    def wait_bytestream_open(self, from_, to):
        # Wait IBB open iq
        event = self.q.expect('stream-iq', iq_type='set')
        sid = parse_ibb_open(event.stanza)
        assert sid == self.stream_id

        # open IBB bytestream
        acknowledge_iq(self.stream, event.stanza)

    def get_data(self):
        # wait for IBB stanzas
        ibb_event = self.q.expect('stream-message')
        sid, binary = parse_ibb_msg_data(ibb_event.stanza)
        assert sid == self.stream_id
        return binary

    def wait_bytestream_closed(self):
        close_event = self.q.expect('stream-iq', iq_type='set', query_name='close', query_ns=ns.IBB)

        # sender finish to send the file and so close the bytestream
        acknowledge_iq(self.stream, close_event.stanza)

class BytestreamS5B(Bytestream):
    def get_ns(self):
        return ns.BYTESTREAMS

    def open_bytestream(self, from_, to):
        port = listen_socks5(self.q)

        send_socks5_init(self.stream, from_, to,
            'alpha', 'tcp', [(from_, '127.0.0.1', port)])

        self.transport = socks5_expect_connection(self.q, 'alpha',
            from_, 'test@localhost/Resource')

        offset_event, state_event = self.q.expect_many(
            EventPattern('dbus-signal', signal='InitialOffsetDefined'),
            EventPattern('dbus-signal', signal='FileTransferStateChanged'))

        return offset_event, state_event

    def send_data(self, from_, to, data):
        self.transport.write(data)

    def wait_bytestream_open(self, from_, to):
        id, mode, sid, hosts = expect_socks5_init(self.q)

        assert mode == 'tcp'
        assert sid == self.stream_id
        jid, host, port = hosts[0]

        self.transport = socks5_connect(self.q, host, port, sid, from_, to)

        send_socks5_reply(self.stream, from_, to, id, jid)

    def get_data(self):
       e = self.q.expect('s5b-data-received', transport=self.transport)
       return e.data

    def wait_bytestream_closed(self):
        self.q.expect('s5b-connection-lost')
