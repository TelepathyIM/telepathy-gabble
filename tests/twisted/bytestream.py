import base64
import hashlib
import sys
import random
import socket

from twisted.internet.protocol import Factory, Protocol
from twisted.internet import reactor
from twisted.words.protocols.jabber.client import IQ
from twisted.words.xish import xpath, domish
from twisted.internet.error import CannotListenError

from servicetest import Event, EventPattern
from gabbletest import acknowledge_iq, make_result_iq, elem_iq, elem
import ns

def wait_events(q, expected, my_event):
    tmp = expected + [my_event]
    events = q.expect_many(*tmp)
    return events[:-1], events[-1]

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

def is_ipv4(address):
    try:
        socket.inet_pton(socket.AF_INET, address)
    except (ValueError, socket.error):
        return False
    return True

class Bytestream(object):
    def __init__(self, stream, q, sid, initiator, target, initiated):
        self.stream = stream
        self.q = q

        self.stream_id = sid
        self.initiator = initiator
        self.target = target
        self.initiated = initiated

    def open_bytestream(self, expected_before=[], expected_after=[]):
        raise NotImplemented

    def send_data(self, data):
        raise NotImplemented

    def get_ns(self):
        raise NotImplemented

    def wait_bytestream_open(self):
        raise NotImplemented

    def get_data(self, size=0):
        raise NotImplemented

    def wait_bytestream_closed(self, expected=[]):
        raise NotImplemented

    def check_si_offer(self, iq, bytestreams):
        assert self.get_ns() in bytestreams

    def close(self):
        raise NotImplemented

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
def listen_socks5(q):
    for port in range(5000, 5100):
        try:
            reactor.listenTCP(port, S5BFactory(q.append))
        except CannotListenError:
            continue
        else:
            return port

    assert False, "Can't find a free port"

def announce_socks5_proxy(q, stream, disco_stanza):
    reply = make_result_iq(stream, disco_stanza)
    query = xpath.queryForNodes('/iq/query', reply)[0]
    item = query.addElement((None, 'item'))
    item['jid'] = 'proxy.localhost'
    stream.send(reply)

    # wait for proxy disco#info query
    event = q.expect('stream-iq', to='proxy.localhost', query_ns=ns.DISCO_INFO,
        iq_type='get')

    reply = elem_iq(stream, 'result', from_='proxy.localhost', id=event.stanza['id'])(
        elem(ns.DISCO_INFO, 'query')(
            elem('identity', category='proxy', type='bytestreams', name='SOCKS5 Bytestreams')(),
            elem('feature', var=ns.BYTESTREAMS)()))
    stream.send(reply)

    # Gabble asks for SOCKS5 info
    event = q.expect('stream-iq', to='proxy.localhost', query_ns=ns.BYTESTREAMS,
        iq_type='get')

    port = listen_socks5(q)
    reply = elem_iq(stream, 'result', id=event.stanza['id'], from_='proxy.localhost')(
        elem(ns.BYTESTREAMS, 'query')(
            elem('streamhost', jid='proxy.localhost', host='127.0.0.1', port=str(port))()))
    stream.send(reply)

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
        return hashlib.sha1(unhashed_domain).hexdigest()

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

    def _check_s5b_reply(self, iq):
        streamhost = xpath.queryForNodes('/iq/query/streamhost-used', iq)[0]
        assert streamhost['jid'] == self.initiator

    def _socks5_expect_connection(self, expected_before, expected_after):
        events_before, _ = wait_events(self.q, expected_before,
            EventPattern('s5b-connected'))

        self._wait_auth_request()
        self._send_auth_reply()
        self._wait_connect_cmd()
        self._send_connect_reply()

        # wait for S5B IQ reply
        events_after, e = wait_events(self.q, expected_after,
            EventPattern('stream-iq', iq_type='result', to=self.initiator))

        self._check_s5b_reply(e.stanza)

        return events_before, events_after

    def open_bytestream(self, expected_before=[], expected_after=[]):
        port = listen_socks5(self.q)

        self._send_socks5_init(port)
        return self._socks5_expect_connection(expected_before, expected_after)

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

        assert mode == 'tcp'
        assert sid == self.stream_id

        stream_host_found = False

        for jid, host, port in hosts:
            if not is_ipv4(host):
                continue

            if jid == self.initiator:
                stream_host_found = True
                if self._socks5_connect(host, port):
                    self._send_socks5_reply(id, jid)
                else:
                    # Connection failed
                    self.send_not_found(id)
                break
        assert stream_host_found

    def get_data(self, size=0):
        binary = ''
        received = False
        while not received:
            e = self.q.expect('s5b-data-received', transport=self.transport)
            binary += e.data

            if len(binary) >= size or size == 0:
                received = True

        return binary

    def wait_bytestream_closed(self, expected=[]):
        events, _ = wait_events(self.q, expected,
            EventPattern('s5b-connection-lost'))
        return events

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

    def close(self):
        self.transport.loseConnection()

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

    def open_bytestream(self, expected_before=[], expected_after=[]):
        self._send_socks5_init(12345)

        events_before, iq_event = wait_events(self.q, expected_before,
            EventPattern('stream-iq', iq_type='error', to=self.initiator))

        self.check_error_stanza(iq_event.stanza)

        return events_before, []

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

    def _socks5_expect_connection(self, expected_before, expected_after):
        events_before, _ = wait_events(self.q, expected_before,
            EventPattern('s5b-connected'))

        self._wait_auth_request()
        self._send_auth_reply()
        self._wait_connect_cmd()

        # pretend the hash was wrong and close the transport
        self.transport.loseConnection()

        iq_event = self.q.expect('stream-iq', iq_type='error', to=self.initiator)
        self.check_error_stanza(iq_event.stanza)

        return events_before, []

class BytestreamS5BRelay(BytestreamS5B):
    """Direct connection doesn't work so we use a relay"""
    def __init__(self, stream, q, sid, initiator, target, initiated):
        BytestreamS5B.__init__(self, stream, q, sid, initiator, target, initiated)

        self.hosts = [(self.initiator, 'invalid.invalid'),
                ('proxy.localhost', '127.0.0.1')]

    # This is the only thing we need to check to test the Target side as the
    # protocol is similar from this side.
    def _check_s5b_reply(self, iq):
        streamhost = xpath.queryForNodes('/iq/query/streamhost-used', iq)[0]
        assert streamhost['jid'] == 'proxy.localhost'

    def wait_bytestream_open(self):
        # The only difference of using a relay on the Target side is to
        # connect to another streamhost.
        id, mode, sid, hosts = self._expect_socks5_init()

        assert mode == 'tcp'
        assert sid == self.stream_id

        proxy_found = False

        for jid, host, port in hosts:
            if jid != self.initiator:
                proxy_found = True
                # connect to the (fake) relay
                if self._socks5_connect(host, port):
                    self._send_socks5_reply(id, jid)
                else:
                    assert False
                break
        assert proxy_found

        # The initiator (Gabble) is now supposed to connect to the proxy too
        self._wait_connect_to_proxy()

    def _wait_connect_to_proxy(self):
        e = self.q.expect('s5b-connected')
        self.transport = e.transport

        self._wait_auth_request()
        self._send_auth_reply()
        self._wait_connect_cmd()
        self._send_connect_reply()

        self._wait_activation_iq()

    def _wait_activation_iq(self):
        e = self.q.expect('stream-iq', iq_type='set', to='proxy.localhost',
            query_ns=ns.BYTESTREAMS)

        query = xpath.queryForNodes('/iq/query', e.stanza)[0]
        assert query['sid'] == self.stream_id
        activate = xpath.queryForNodes('/iq/query/activate', e.stanza)[0]
        assert str(activate) == self.target

        self._reply_activation_iq(e.stanza)

    def _reply_activation_iq(self, iq):
        reply = make_result_iq(self.stream, iq)
        reply.send()

    def _socks5_connect(self, host, port):
        # No point to emulate the proxy. Just pretend the Target properly
        # connects, auth and requests connection
        return True

    def wait_bytestream_closed(self, expected=[]):
        if expected == []:
            return []

        return self.q.expect_many(*expected)


class BytestreamS5BRelayBugged(BytestreamS5BRelay):
    """Simulate bugged ejabberd (< 2.0.2) proxy sending wrong CONNECT reply"""
    def _send_connect_reply(self):
        # send a 6 bytes wrong reply
        connect_reply = '\x05\x00\x00\x00\x00\x00'
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

    def open_bytestream(self, expected_before=[], expected_after=[]):
        # open IBB bytestream
        iq = IQ(self.stream, 'set')
        iq['to'] = self.target
        iq['from'] = self.initiator
        open = iq.addElement((ns.IBB, 'open'))
        open['sid'] = self.stream_id
        # set a ridiculously small block size to stress test IBB buffering
        open['block-size'] = '1'
        self.stream.send(iq)

        events_before = self.q.expect_many(*expected_before)
        events_after = self.q.expect_many(*expected_after)

        return events_before, events_after

    def _send(self, from_, to, data):
        raise NotImplemented

    def send_data(self, data):
        if self.initiated:
            from_ = self.initiator
            to = self.target
        else:
            from_ = self.target
            to = self.initiator

        self._send(from_, to, data)
        self.seq += 1

    def wait_bytestream_open(self):
        # Wait IBB open iq
        event = self.q.expect('stream-iq', iq_type='set')
        open = xpath.queryForNodes('/iq/open', event.stanza)[0]
        assert open.uri == ns.IBB
        assert open['sid'] == self.stream_id

        # open IBB bytestream
        acknowledge_iq(self.stream, event.stanza)

    def get_data(self, size=0):
        # wait for IBB stanza. Gabble always uses IQ

        binary = ''
        received = False
        while not received:
            ibb_event = self.q.expect('stream-iq', query_ns=ns.IBB)

            data_nodes = xpath.queryForNodes('/iq/data[@xmlns="%s"]' % ns.IBB,
                ibb_event.stanza)
            assert data_nodes is not None
            assert len(data_nodes) == 1
            ibb_data = data_nodes[0]
            binary += base64.b64decode(str(ibb_data))

            assert ibb_data['sid'] == self.stream_id

            # ack the IQ
            result = make_result_iq(self.stream, ibb_event.stanza)
            result.send()

            if len(binary) >= size or size == 0:
                received = True

        return binary

    def wait_bytestream_closed(self, expected=[]):
        events, close_event = wait_events(self.q, expected,
            EventPattern('stream-iq', iq_type='set', query_name='close', query_ns=ns.IBB))

        # sender finish to send the file and so close the bytestream
        acknowledge_iq(self.stream, close_event.stanza)
        return events

    def close(self):
        if self.initiated:
            from_ = self.initiator
            to = self.target
        else:
            from_ = self.target
            to = self.initiator

        iq = elem_iq(self.stream, 'set', from_=from_, to=to, id=str(id))(
            elem('close', xmlns=ns.IBB, sid=self.stream_id)())

        self.stream.send(iq)

class BytestreamIBBMsg(BytestreamIBB):
    def _send(self, from_, to, data):
        message = domish.Element(('jabber:client', 'message'))
        message['to'] = to
        message['from'] = from_
        data_node = message.addElement((ns.IBB, 'data'))
        data_node['sid'] = self.stream_id
        data_node['seq'] = str(self.seq)
        data_node.addContent(base64.b64encode(data))
        self.stream.send(message)

    def _wait_data_event(self):
        ibb_event = self.q.expect('stream-message')

        data_nodes = xpath.queryForNodes('/message/data[@xmlns="%s"]' % ns.IBB,
            ibb_event.stanza)
        assert data_nodes is not None
        assert len(data_nodes) == 1
        ibb_data = data_nodes[0]
        assert ibb_data['sid'] == self.stream_id
        return str(ibb_data), ibb_data['sid']

class BytestreamIBBIQ(BytestreamIBB):
    def _send(self, from_, to, data):
        id = random.randint(0, sys.maxint)

        iq = elem_iq(self.stream, 'set', from_=from_, to=to, id=str(id))(
            elem('data', xmlns=ns.IBB, sid=self.stream_id, seq=str(self.seq))(
                (unicode(base64.b64encode(data)))))

        self.stream.send(iq)

##### SI Fallback (Gabble specific extension) #####
class BytestreamSIFallback(Bytestream):
    """Abstract class used for all the SI fallback scenarios"""
    def __init__(self, stream, q, sid, initiator, target, initiated):
        Bytestream.__init__(self, stream, q, sid, initiator, target, initiated)

        self.socks5 = BytestreamS5B(stream, q, sid, initiator, target,
            initiated)

        self.ibb = BytestreamIBBMsg(stream, q, sid, initiator, target,
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

    def open_bytestream(self, expected_before=[], expected_after=[]):
        # first propose to peer to connect using SOCKS5
        # We set an invalid IP so that won't work
        self.socks5._send_socks5_init([
            # Not working streamhost
            (self.initiator, 'invalid.invalid', 12345),
            ])

        events_before, iq_event = wait_events(self.q, expected_before,
            EventPattern('stream-iq', iq_type='error', to=self.initiator))

        self.socks5.check_error_stanza(iq_event.stanza)

        # socks5 failed, let's try IBB
        _, events_after = self.ibb.open_bytestream([], expected_after)

        return events_before, events_after

    def send_data(self, data):
        self.used.send_data(data)

    def get_data(self, size=0):
        return self.used.get_data(size)

    def wait_bytestream_closed(self, expected=[]):
        return self.used.wait_bytestream_closed(expected)

    def check_si_offer(self, iq, bytestreams):
        assert self.socks5.get_ns() in bytestreams
        assert self.ibb.get_ns() in bytestreams

        # check if si-multiple is supported
        si_multiple = xpath.queryForNodes(
            '/iq/si[@xmlns="%s"]/si-multiple[@xmlns="%s"]'
            % (ns.SI, ns.SI_MULTIPLE), iq)
        assert si_multiple is not None

    def close(self):
        return self.used.close()

class BytestreamSIFallbackS5CannotConnect(BytestreamSIFallback):
    """Try to use SOCKS5 and fallback to IBB because the target can't connect
    to the receiver."""
    def __init__(self, stream, q, sid, initiator, target, initiated):
        BytestreamSIFallback.__init__(self, stream, q, sid, initiator, target, initiated)

        self.socks5 = BytestreamS5BCannotConnect(stream, q, sid, initiator, target,
            initiated)

        self.used = self.ibb

    def open_bytestream(self, expected_before=[], expected_after=[]):
        # First propose to peer to connect using SOCKS5
        # That won't work as target can't connect
        events_before, _ = self.socks5.open_bytestream(expected_before)

        # socks5 failed, let's try IBB
        _, events_after = self.ibb.open_bytestream([], expected_after)

        return events_before, events_after

    def wait_bytestream_open(self):
        # Gabble tries SOCKS5 first
        self.socks5.wait_bytestream_open()

        # Gabble now tries IBB
        self.ibb.wait_bytestream_open()

class BytestreamSIFallbackS5WrongHash(BytestreamSIFallback):
    """Try to use SOCKS5 and fallback to IBB because target sent the wrong hash
    as domain in the CONNECT command."""
    def __init__(self, stream, q, sid, initiator, target, initiated):
        BytestreamSIFallback.__init__(self, stream, q, sid, initiator, target, initiated)

        self.socks5 = BytestreamS5BWrongHash(stream, q, sid, initiator, target,
            initiated)

        self.used = self.ibb

    def open_bytestream(self, expected_before=[], expected_after=[]):
        # SOCKS5 won't work because we'll pretend the hash was wrong and
        # close the connection
        events_before, _ = self.socks5.open_bytestream(expected_before)

        # socks5 failed, let's try IBB
        _, events_after = self.ibb.open_bytestream([], expected_after)
        return events_before, events_after

    def wait_bytestream_open(self):
        # BytestreamS5BWrongHash will send a wrong hash so Gabble will
        # disconnect the connection
        self.socks5.wait_bytestream_open()

        # Gabble now tries IBB
        self.ibb.wait_bytestream_open()
