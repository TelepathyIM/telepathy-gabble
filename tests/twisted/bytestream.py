import base64
import sha

from twisted.internet.protocol import Factory, Protocol
from twisted.internet import reactor
from twisted.words.protocols.jabber.client import IQ
from twisted.words.xish import xpath, domish
from twisted.internet.error import CannotListenError

from servicetest import Event, EventPattern
from gabbletest import acknowledge_iq, sync_stream
import ns

class Bytestream(object):
    def __init__(self, stream, q, sid, initiator, target, initiated):
        self.stream = stream
        self.q = q

        self.stream_id = sid
        self.initiator = initiator
        self.target = target
        self.initiated = initiated

    def open_bytestream(self, expected=None):
        raise NotImplemented

    def send_data(self, data):
        raise NotImplemented

    def get_ns(self):
        raise NotImplemented

    def wait_bytestream_open(self):
        raise NotImplemented

    def get_data(self):
        raise NotImplemented

    def wait_bytestream_closed(self):
        raise NotImplemented

    def create_si_offer(self, profile):
        assert self.initiated

        iq = IQ(self.stream, 'set')
        iq['from'] = self.initiator
        iq['to'] = self.target
        si = iq.addElement((ns.SI, 'si'))
        si['id'] = self.stream_id
        si['profile'] = profile
        feature = si.addElement((ns.FEATURE_NEG, 'feature'))
        x = feature.addElement((ns.X_DATA, 'x'))
        x['type'] = 'form'
        field = x.addElement((None, 'field'))
        field['var'] = 'stream-method'
        field['type'] = 'list-single'
        option = field.addElement((None, 'option'))
        value = option.addElement((None, 'value'))
        value.addContent(self.get_ns())

        return iq, si

##### XEP-0095: Stream Initiation #####

def parse_si_offer(iq):
    si_nodes = xpath.queryForNodes('/iq/si', iq)
    assert si_nodes is not None
    assert len(si_nodes) == 1
    si = si_nodes[0]

    feature = xpath.queryForNodes('/si/feature', si)[0]
    x = xpath.queryForNodes('/feature/x', feature)[0]
    assert x['type'] == 'form'
    field = xpath.queryForNodes('/x/field', x)[0]
    assert field['var'] == 'stream-method'
    assert field['type'] == 'list-single'

    bytestreams = []
    for value in xpath.queryForNodes('/field/option/value', field):
        bytestreams.append(str(value))

    return si['profile'], si['id'], bytestreams

def create_si_reply(stream, iq, to, bytestream):
    result = IQ(stream, 'result')
    result['id'] = iq['id']
    result['from'] = iq['to']
    result['to'] = to
    res_si = result.addElement((ns.SI, 'si'))
    res_feature = res_si.addElement((ns.FEATURE_NEG, 'feature'))
    res_x = res_feature.addElement((ns.X_DATA, 'x'))
    res_x['type'] = 'submit'
    res_field = res_x.addElement((None, 'field'))
    res_field['var'] = 'stream-method'
    res_value = res_field.addElement((None, 'value'))
    res_value.addContent(bytestream)

    return result, res_si

def parse_si_reply(iq):
    si = xpath.queryForNodes('/iq/si[@xmlns="%s"]' % ns.SI,
            iq)[0]
    value = xpath.queryForNodes('/si/feature/x/field/value', si)
    assert len(value) == 1
    proto = value[0]
    return str(proto)


##### XEP-0065: SOCKS5 Bytestreams #####

class BytestreamS5B(Bytestream):
    def get_ns(self):
        return ns.BYTESTREAMS

    def _send_socks5_init(self, hosts):
        iq = IQ(self.stream, 'set')
        iq['to'] = self.target
        iq['from'] = self.initiator
        query = iq.addElement((ns.BYTESTREAMS, 'query'))
        query['sid'] = self.stream_id
        query['mode'] = 'tcp'
        for jid, host, port in hosts:
            streamhost = query.addElement('streamhost')
            streamhost['jid'] = jid
            streamhost['host'] = host
            streamhost['port'] = str(port)
        self.stream.send(iq)

    def _wait_auth_request(self):
        event = self.q.expect('s5b-data-received')
        assert event.data == '\x05\x01\x00' # version 5, 1 auth method, no auth
        self.transport = event.transport

    def _send_auth_reply(self):
        self.transport.write('\x05\x00') # version 5, no auth

    def _wait_connect_cmd(self):
        event = self.q.expect('s5b-data-received', transport=self.transport)
        # version 5, connect, reserved, domain type
        expected_connect = '\x05\x01\x00\x03'
        expected_connect += chr(40) # len (SHA-1)
        # sha-1(sid + initiator + target)
        unhashed_domain = self.stream_id + self.initiator + self.target
        expected_connect += sha.new(unhashed_domain).hexdigest()
        expected_connect += '\x00\x00' # port
        assert event.data == expected_connect

    def _send_connect_reply(self):
        # FIXME: This is wrong. Change once SOCKS5 is fixed
        self.transport.write('\x05\x00') #version 5, ok

    def _socks5_expect_connection(self, expected):
        if expected is not None:
            event, _ = self.q.expect_many(expected,
                EventPattern('s5b-connected'))
        else:
            event = None
            self.q.expect('s5b-connected')

        self._wait_auth_request()
        self._send_auth_reply()
        self._wait_connect_cmd()
        self._send_connect_reply()

        return event

    def _listen_socks5(self):
        for port in range(5000,5100):
            try:
                reactor.listenTCP(port, S5BFactory(self.q.append))
            except CannotListenError:
                continue
            else:
                return port

        assert False, "Can't find a free port"

    def open_bytestream(self, expected=None):
        port = self._listen_socks5()

        self._send_socks5_init([
            # Not working streamhost
            ('invalid.invalid', 'invalid.invalid', port),
            # Working streamhost
            (self.initiator, '127.0.0.1', port),
            # This works too but should not be tried as Gabble should just
            # connect to the previous one
            ('Not me', '127.0.0.1', port),
            ])

        return self._socks5_expect_connection(expected)

    def send_data(self, data):
        self.transport.write(data)

    def _expect_socks5_init(self):
        event = self.q.expect('stream-iq', iq_type='set')
        iq = event.stanza
        query = xpath.queryForNodes('/iq/query', iq)[0]
        assert query.uri == ns.BYTESTREAMS

        mode = query['mode']
        sid = query['sid']
        hosts = []

        for streamhost in xpath.queryForNodes('/query/streamhost', query):
            hosts.append((streamhost['jid'], streamhost['host'], int(streamhost['port'])))
        return iq['id'], mode, sid, hosts

    def _socks5_connect(self, host, port):
        reactor.connectTCP(host, port, S5BFactory(self.q.append))

        event = self.q.expect('s5b-connected')
        transport = event.transport
        transport.write('\x05\x01\x00') #version 5, 1 auth method, no auth

        event = self.q.expect('s5b-data-received')
        event.data == '\x05\x00' # version 5, no auth

        # version 5, connect, reserved, domain type
        connect = '\x05\x01\x00\x03'
        connect += chr(40) # len (SHA-1)
        # sha-1(sid + initiator + target)
        unhashed_domain = self.stream_id + self.initiator + self.target
        connect += sha.new(unhashed_domain).hexdigest()
        connect += '\x00\x00' # port
        transport.write(connect)

        event = self.q.expect('s5b-data-received')
        event.data == '\x05\x00' # version 5, ok

        return transport

    def _send_socks5_reply(self, id, stream_used):
        result = IQ(self.stream, 'result')
        result['id'] = id
        result['from'] = self.target
        result['to'] = self.initiator
        query = result.addElement((ns.BYTESTREAMS, 'query'))
        streamhost_used = query.addElement((None, 'streamhost-used'))
        streamhost_used['jid'] = stream_used
        result.send()

    def wait_bytestream_open(self):
        id, mode, sid, hosts = self._expect_socks5_init()

        for jid, host, port in hosts:
            pass
            # FIXME: re-enabled once SOCKS5 is fixed
            #assert jid == self.initiator, jid

        assert mode == 'tcp'
        assert sid == self.stream_id
        jid, host, port = hosts[0]

        self.transport = self._socks5_connect(host, port)

        self._send_socks5_reply(id, jid)

    def get_data(self):
       e = self.q.expect('s5b-data-received', transport=self.transport)
       return e.data

    def wait_bytestream_closed(self):
        self.q.expect('s5b-connection-lost')

class BytestreamS5BPidgin(BytestreamS5B):
    """Simulate buggy S5B implementation (as Pidgin's one)"""
    def _send_connect_reply(self):
        # version 5, ok, reserved, domain type
        connect_reply = '\x05\x00\x00\x03'
        # I'm Pidgin, why should I respect SOCKS5 XEP?
        domain = '127.0.0.1'
        connect_reply += chr(len(domain))
        connect_reply += domain
        connect_reply += '\x00\x00' # port
        self.transport.write(connect_reply)

class S5BProtocol(Protocol):
    def connectionMade(self):
        self.factory.event_func(Event('s5b-connected',
            transport=self.transport))

    def dataReceived(self, data):
        self.factory.event_func(Event('s5b-data-received', data=data,
            transport=self.transport))

class S5BFactory(Factory):
    protocol = S5BProtocol

    def __init__(self, event_func):
        self.event_func = event_func

    def buildProtocol(self, addr):
        protocol = Factory.buildProtocol(self, addr)
        return protocol

    def startedConnecting(self, connector):
        self.event_func(Event('s5b-started-connecting', connector=connector))

    def clientConnectionLost(self, connector, reason):
        self.event_func(Event('s5b-connection-lost', connector=connector,
            reason=reason))

    def clientConnectionFailed(self, connector, reason):
        self.event_func(Event('s5b-connection-failed', reason=reason))

def expect_socks5_reply(q):
    event = q.expect('stream-iq', iq_type='result')
    iq = event.stanza
    query = xpath.queryForNodes('/iq/query', iq)[0]
    assert query.uri == ns.BYTESTREAMS
    streamhost_used = xpath.queryForNodes('/query/streamhost-used', query)[0]
    return streamhost_used

##### XEP-0047: In-Band Bytestreams (IBB) #####

class BytestreamIBB(Bytestream):
    def __init__(self, stream, q, sid, initiator, target, initiated):
        Bytestream.__init__(self, stream, q, sid, initiator, target, initiated)

        self.seq = 0

    def get_ns(self):
        return ns.IBB

    def open_bytestream(self, expected=None):
        # open IBB bytestream
        send_ibb_open(self.stream, self.initiator, self.target, self.stream_id, 4096)

        if expected is not None:
            return self.q.expect_many(expected)[0]

    def send_data(self, data):
        if self.initiated:
            from_ = self.initiator
            to = self.target
        else:
            from_ = self.target
            to = self.initiator

        send_ibb_msg_data(self.stream, from_, to, self.stream_id,
            self.seq, data)
        sync_stream(self.q, self.stream)

        self.seq += 1

    def wait_bytestream_open(self):
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

def send_ibb_open(stream, from_, to, sid, block_size):
    iq = IQ(stream, 'set')
    iq['to'] = to
    iq['from'] = from_
    open = iq.addElement((ns.IBB, 'open'))
    open['sid'] = sid
    open['block-size'] = str(block_size)
    stream.send(iq)

def parse_ibb_open(iq):
    open = xpath.queryForNodes('/iq/open', iq)[0]
    assert open.uri == ns.IBB
    return open['sid']

def send_ibb_msg_data(stream, from_, to, sid, seq, data):
    message = domish.Element(('jabber:client', 'message'))
    message['to'] = to
    message['from'] = from_
    data_node = message.addElement((ns.IBB, 'data'))
    data_node['sid'] = sid
    data_node['seq'] = str(seq)
    data_node.addContent(base64.b64encode(data))
    stream.send(message)

def parse_ibb_msg_data(message):
    data_nodes = xpath.queryForNodes('/message/data[@xmlns="%s"]' % ns.IBB,
        message)
    assert data_nodes is not None
    assert len(data_nodes) == 1
    ibb_data = data_nodes[0]
    binary = base64.b64decode(str(ibb_data))

    return ibb_data['sid'], binary
