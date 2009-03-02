import base64
import sha

from twisted.internet.protocol import Factory, Protocol
from twisted.internet import reactor
from twisted.words.protocols.jabber.client import IQ
from twisted.words.xish import xpath, domish
from twisted.internet.error import CannotListenError

from servicetest import Event, EventPattern
from gabbletest import acknowledge_iq, sync_stream, make_result_iq
import ns

def create_from_si_offer(stream, q, bytestream_cls, iq, initiator):
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

    bytestream = bytestream_cls(stream, q, si['id'], initiator,
        iq['to'], False)

    bytestream.check_si_offer(iq, bytestreams)

    return bytestream, si['profile']

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

    def check_si_offer(self, iq, bytestreams):
        assert self.get_ns() in bytestreams

##### XEP-0095: Stream Initiation #####

    def _create_si_offer(self, profile, to=None):
        assert self.initiated

        iq = IQ(self.stream, 'set')
        iq['from'] = self.initiator
        if to is None:
            iq['to'] = self.target
        else:
            iq['to'] = to
        si = iq.addElement((ns.SI, 'si'))
        si['id'] = self.stream_id
        si['profile'] = profile
        feature = si.addElement((ns.FEATURE_NEG, 'feature'))
        x = feature.addElement((ns.X_DATA, 'x'))
        x['type'] = 'form'
        field = x.addElement((None, 'field'))
        field['var'] = 'stream-method'
        field['type'] = 'list-single'

        return iq, si, field

    def create_si_offer(self, profile, to=None):
        iq, si, field = self._create_si_offer(profile, to)
        option = field.addElement((None, 'option'))
        value = option.addElement((None, 'value'))
        value.addContent(self.get_ns())

        return iq, si

    def create_si_reply(self, iq, to=None):
        result = make_result_iq(self.stream, iq)
        result['from'] = iq['to']
        if to is None:
            result['to'] = self.initiator
        else:
            result['to'] = to
        res_si = result.firstChildElement()
        res_feature = res_si.addElement((ns.FEATURE_NEG, 'feature'))
        res_x = res_feature.addElement((ns.X_DATA, 'x'))
        res_x['type'] = 'submit'
        res_field = res_x.addElement((None, 'field'))
        res_field['var'] = 'stream-method'
        res_value = res_field.addElement((None, 'value'))
        res_value.addContent(self.get_ns())

        return result, res_si

    def check_si_reply(self, iq):
        si = xpath.queryForNodes('/iq/si[@xmlns="%s"]' % ns.SI,
                iq)[0]
        value = xpath.queryForNodes('/si/feature/x/field/value', si)
        assert len(value) == 1
        proto = value[0]
        assert str(proto) == self.get_ns()

##### XEP-0065: SOCKS5 Bytestreams #####

class BytestreamS5B(Bytestream):
    def __init__(self, stream, q, sid, initiator, target, initiated):
        Bytestream.__init__(self, stream, q, sid, initiator, target, initiated)

        # hosts that will be announced when sending S5B open IQ
        self.hosts = [
            # Not working streamhost
            ('invalid.invalid', 'invalid.invalid'),
            # Working streamhost
            (self.initiator, '127.0.0.1'),
            # This works too but should not be tried as Gabble should just
            # connect to the previous one
            ('Not me', '127.0.0.1')]

    def get_ns(self):
        return ns.BYTESTREAMS

    def _send_socks5_init(self, port):
        iq = IQ(self.stream, 'set')
        iq['to'] = self.target
        iq['from'] = self.initiator
        query = iq.addElement((ns.BYTESTREAMS, 'query'))
        query['sid'] = self.stream_id
        query['mode'] = 'tcp'
        for jid, host in self.hosts:
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

    def _compute_hash_domain(self):
        # sha-1(sid + initiator + target)
        unhashed_domain = self.stream_id + self.initiator + self.target
        return sha.new(unhashed_domain).hexdigest()

    def _wait_connect_cmd(self):
        event = self.q.expect('s5b-data-received', transport=self.transport)
        # version 5, connect, reserved, domain type
        expected_connect = '\x05\x01\x00\x03'
        expected_connect += chr(40) # len (SHA-1)
        expected_connect += self._compute_hash_domain()
        expected_connect += '\x00\x00' # port
        assert event.data == expected_connect

    def _send_connect_reply(self):
        connect_reply = '\x05\x00\x00\x03'
        connect_reply += chr(40) # len (SHA-1)
        connect_reply += self._compute_hash_domain()
        connect_reply += '\x00\x00' # port
        self.transport.write(connect_reply)

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

        self._send_socks5_init(port)
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

    def _send_auth_cmd(self):
        #version 5, 1 auth method, no auth
        self.transport.write('\x05\x01\x00')

    def _wait_auth_reply(self):
        event = self.q.expect('s5b-data-received')
        assert event.data == '\x05\x00' # version 5, no auth

    def _send_connect_cmd(self):
        # version 5, connect, reserved, domain type
        connect = '\x05\x01\x00\x03'
        connect += chr(40) # len (SHA-1)
        connect += self._compute_hash_domain()
        connect += '\x00\x00' # port
        self.transport.write(connect)

    def _wait_connect_reply(self):
        event = self.q.expect('s5b-data-received')
        # version 5, succeed, reserved, domain type
        expected_reply = '\x05\x00\x00\x03'
        expected_reply += chr(40) # len (SHA-1)
        expected_reply += self._compute_hash_domain()
        expected_reply += '\x00\x00' # port
        assert event.data == expected_reply

    def _socks5_connect(self, host, port):
        reactor.connectTCP(host, port, S5BFactory(self.q.append))

        event = self.q.expect('s5b-connected')
        self.transport = event.transport

        self._send_auth_cmd()
        self._wait_auth_reply()
        self._send_connect_cmd()
        self._wait_connect_reply()
        return True

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
            assert jid == self.initiator, jid

        assert mode == 'tcp'
        assert sid == self.stream_id
        jid, host, port = hosts[0]

        if self._socks5_connect(host, port):
            self._send_socks5_reply(id, jid)
        else:
            # Connection failed
            self.send_not_found(id)

    def get_data(self):
       e = self.q.expect('s5b-data-received', transport=self.transport)
       return e.data

    def wait_bytestream_closed(self):
        self.q.expect('s5b-connection-lost')

    def check_error_stanza(self, iq):
        error = xpath.queryForNodes('/iq/error', iq)[0]
        assert error['code'] == '404'
        assert error['type'] == 'cancel'

    def send_not_found(self, id):
        iq = IQ(self.stream, 'error')
        iq['to'] = self.initiator
        iq['from'] = self.target
        iq['id'] = id
        error = iq.addElement(('', 'error'))
        error['type'] = 'cancel'
        error['code'] = '404'
        self.stream.send(iq)

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

class BytestreamS5BCannotConnect(BytestreamS5B):
    """SOCKS5 bytestream not working because target can't connect
    to initiator."""
    def __init__(self, stream, q, sid, initiator, target, initiated):
        BytestreamS5B.__init__(self, stream, q, sid, initiator, target, initiated)

        self.hosts = [('invalid.invalid', 'invalid.invalid')]

    def open_bytestream(self, expected=None):
        self._send_socks5_init(12345)

        if expected is not None:
            event, iq_event = self.q.expect_many(expected,
                EventPattern('stream-iq', iq_type='error', to=self.initiator))
        else:
            event = None
            iq_event = self.q.expect('stream-iq', iq_type='error', to=self.initiator)

        self.check_error_stanza(iq_event.stanza)

        return event

    def _socks5_connect(self, host, port):
        # Pretend we can't connect to it
        return False

class BytestreamS5BWrongHash(BytestreamS5B):
    """Connection is closed because target sends the wrong hash"""
    def __init__(self, stream, q, sid, initiator, target, initiated):
        BytestreamS5B.__init__(self, stream, q, sid, initiator, target, initiated)

        self.hosts = [(self.initiator, '127.0.0.1')]

    def _send_connect_cmd(self):
        # version 5, connect, reserved, domain type
        connect = '\x05\x01\x00\x03'
        # send wrong hash as domain
        domain = 'this is wrong'
        connect += chr(len(domain))
        connect += domain
        connect += '\x00\x00' # port
        self.transport.write(connect)

    def _socks5_connect(self, host, port):
        reactor.connectTCP(host, port, S5BFactory(self.q.append))

        event = self.q.expect('s5b-connected')
        self.transport = event.transport

        self._send_auth_cmd()
        self._wait_auth_reply()
        self._send_connect_cmd()

        # Gabble disconnects the connection because we sent a wrong hash
        self.q.expect('s5b-connection-lost')
        return False

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

        # pretend the hash was wrong and close the transport
        self.transport.loseConnection()

        iq_event = self.q.expect('stream-iq', iq_type='error', to=self.initiator)
        self.check_error_stanza(iq_event.stanza)

        return event

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

##### SI Fallback (Gabble specific extension) #####
class BytestreamSIFallback(Bytestream):
    """Abstract class used for all the SI fallback scenarios"""
    def __init__(self, stream, q, sid, initiator, target, initiated):
        Bytestream.__init__(self, stream, q, sid, initiator, target, initiated)

        self.socks5 = BytestreamS5B(stream, q, sid, initiator, target,
            initiated)

        self.ibb = BytestreamIBB(stream, q, sid, initiator, target,
            initiated)

    def create_si_offer(self, profile, to=None):
        iq, si, field = self._create_si_offer(profile, to)

        # add SOCKS5
        option = field.addElement((None, 'option'))
        value = option.addElement((None, 'value'))
        value.addContent(self.socks5.get_ns())
        # add IBB
        option = field.addElement((None, 'option'))
        value = option.addElement((None, 'value'))
        value.addContent(self.ibb.get_ns())

        si_multiple = si.addElement((ns.SI_MULTIPLE, 'si-multiple'))

        return iq, si

    def check_si_reply(self, iq):
        value = xpath.queryForNodes(
            '/iq/si[@xmlns="%s"]/si-multiple[@xmlns="%s"]/value' %
            (ns.SI, ns.SI_MULTIPLE), iq)
        assert len(value) == 2
        assert str(value[0]) == self.socks5.get_ns()
        assert str(value[1]) == self.ibb.get_ns()

    def create_si_reply(self, iq, to=None):
        result = make_result_iq(self.stream, iq)
        result['from'] = iq['to']
        if to is None:
            result['to'] = self.initiator
        else:
            result['to'] = to
        res_si = result.firstChildElement()
        si_multiple = res_si.addElement((ns.SI_MULTIPLE, 'si-multiple'))
        # add SOCKS5
        res_value = si_multiple.addElement((None, 'value'))
        res_value.addContent(self.socks5.get_ns())
        # add IBB
        res_value = si_multiple.addElement((None, 'value'))
        res_value.addContent(self.ibb.get_ns())

        return result, res_si

    def send_data(self, data):
        self.used.send_data(data)

    def get_data(self):
        return self.used.get_data()

    def wait_bytestream_closed(self):
        self.used.wait_bytestream_closed()

    def check_si_offer(self, iq, bytestreams):
        assert self.socks5.get_ns() in bytestreams
        assert self.ibb.get_ns() in bytestreams

        # check if si-multiple is supported
        si_multiple = xpath.queryForNodes(
            '/iq/si[@xmlns="%s"]/si-multiple[@xmlns="%s"]'
            % (ns.SI, ns.SI_MULTIPLE), iq)
        assert si_multiple is not None

class BytestreamSIFallbackS5CannotConnect(BytestreamSIFallback):
    """Try to use SOCKS5 and fallback to IBB because the target can't connect
    to the receiver."""
    def __init__(self, stream, q, sid, initiator, target, initiated):
        BytestreamSIFallback.__init__(self, stream, q, sid, initiator, target, initiated)

        self.socks5 = BytestreamS5BCannotConnect(stream, q, sid, initiator, target,
            initiated)

        self.used = self.ibb

    def open_bytestream(self, expected=None):
        # First propose to peer to connect using SOCKS5
        # That won't work as target can't connect
        event = self.socks5.open_bytestream(expected)

        # socks5 failed, let's try IBB
        self.ibb.open_bytestream()
        return event

    def wait_bytestream_open(self):
        # Gabble tries SOCKS5 first
        self.socks5.wait_bytestream_open()

        # Gabble now tries IBB
        self.ibb.wait_bytestream_open()

class BytestreamSIFallbackS5WrongHash(BytestreamSIFallback):
    """Try to use SOCKS5 and fallback to IBB because target send the wrong has
    as domain in the CONNECT command."""
    def __init__(self, stream, q, sid, initiator, target, initiated):
        BytestreamSIFallback.__init__(self, stream, q, sid, initiator, target, initiated)

        self.socks5 = BytestreamS5BWrongHash(stream, q, sid, initiator, target,
            initiated)

        self.used = self.ibb

    def open_bytestream(self, expected=None):
        # SOCKS5 won't work because we'll pretend the hash was wrong and
        # close the connection
        event = self.socks5.open_bytestream(expected)

        # socks5 failed, let's try IBB
        self.ibb.open_bytestream()
        return event

    def wait_bytestream_open(self):
        # BytestreamS5BWrongHash will send a wrong hash so Gabble will
        # disconnect the connection
        self.socks5.wait_bytestream_open()

        # Gabble now tries IBB
        self.ibb.wait_bytestream_open()
