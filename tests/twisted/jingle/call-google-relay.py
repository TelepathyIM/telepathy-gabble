"""
Test getting relay from Google jingleinfo
"""

import config

if not config.GOOGLE_RELAY_ENABLED:
    print "NOTE: built with --disable-google-relay"
    raise SystemExit(77)

import dbus
from dbus.exceptions import DBusException
from functools import partial
from servicetest import call_async, EventPattern, assertEquals
from jingletest2 import GtalkProtocol04
from gabbletest import (exec_test, disconnect_conn, GoogleXmlStream,
     make_result_iq, sync_stream)
from call_helper import CallTest, run_call_test
import constants as cs
import ns

from twisted.words.protocols.jabber.client import IQ
from twisted.web import http
from httptest import listen_http

TOO_SLOW_CLOSE = 1
TOO_SLOW_REMOVE_SELF = 2
TOO_SLOW_DISCONNECT = 3

class CallGoogleRelayTest(CallTest):

    initial_audio = True
    initial_video = True

    # A real request/response looks like this:
    #
    # GET /create_session HTTP/1.1
    # Connection: Keep-Alive
    # Content-Length: 0
    # Host: relay.l.google.com
    # User-Agent: farsight-libjingle
    # X-Google-Relay-Auth: censored
    # X-Talk-Google-Relay-Auth: censored
    #
    # HTTP/1.1 200 OK
    # Content-Type: text/plain
    # Date: Tue, 03 Mar 2009 18:33:28 GMT
    # Server: MediaProxy
    # Cache-Control: private, x-gzip-ok=""
    # Transfer-Encoding: chunked
    #
    # c3
    # relay.ip=74.125.47.126
    # relay.udp_port=19295
    # relay.tcp_port=19294
    # relay.ssltcp_port=443
    # stun.ip=74.125.47.126
    # stun.port=19302
    # username=censored
    # password=censored
    # magic_cookie=censored
    #
    # 0
    response_template = """c3
    relay.ip=127.0.0.1
    relay.udp_port=11111
    relay.tcp_port=22222
    relay.ssltcp_port=443
    stun.ip=1.2.3.4
    stun.port=12345
    username=UUUUUUUU%d
    password=PPPPPPPP%d
    magic_cookie=MMMMMMMM
    """
 
    def handle_request(req, n):
        req.setResponseCode(http.OK)
        req.setHeader("Content-Type", "text/plain")
        req.write(self.response_template % (n, n))
        req.finish()


    def prepare(self):
        events = self.q.expect_many(
                EventPattern('stream-iq', query_ns=ns.GOOGLE_JINGLE_INFO),
                EventPattern('stream-iq', to=None, query_ns='vcard-temp',
                    query_name='vCard'),
                EventPattern('stream-iq', query_ns=ns.ROSTER),
                )

        CallTest.prepare(self, events=events)

        ji_event = events[0]
        listen_port = listen_http(self.q, 0)

        jingleinfo = make_result_iq(self.stream, ji_event.stanza)
        stun = jingleinfo.firstChildElement().addElement('stun')
        server = stun.addElement('server')
        server['host'] = 'resolves-to-1.2.3.4'
        server['udp'] = '12345'

        self.expected_stun_server = '1.2.3.4'
        self.expected_stun_port = 12345

        # This bit is undocumented... but it has the same format as what we get
        # from Google Talk servers:
        # <iq to="censored" from="censored" id="73930208084" type="result">
        #   <query xmlns="google:jingleinfo">
        #     <stun>
        #       <server host="stun.l.google.com" udp="19302"/>
        #       <server host="stun4.l.google.com" udp="19302"/>
        #       <server host="stun3.l.google.com" udp="19302"/>
        #       <server host="stun1.l.google.com" udp="19302"/>
        #       <server host="stun2.l.google.com" udp="19302"/>
        #     </stun>
        #     <relay>
        #       <token>censored</token>
        #       <server host="relay.google.com" udp="19295" tcp="19294"
        #         tcpssl="443"/>
        #     </relay>
        #   </query>
        # </iq>
        relay = jingleinfo.firstChildElement().addElement('relay')
        relay.addElement('token', content='jingle all the way')
        server = relay.addElement('server')
        server['host'] = '127.0.0.1'
        server['udp'] = '11111'
        server['tcp'] = '22222'
        server['tcpssl'] = '443'
        # The special regression-test build of Gabble parses this attribute,
        # because we can't listen on port 80
        server['gabble-test-http-port'] = str(listen_port.getHost().port)
        self.stream.send(jingleinfo)
        jingleinfo = None
    
        # Spoof some jingle info. This is a regression test for
        # <https://bugs.freedesktop.org/show_bug.cgi?id=34048>. We assert that
        # Gabble has ignored this stuff later.
        iq = IQ(self.stream, 'set')
        iq['from'] = "evil@evil.net"
        query = iq.addElement((ns.GOOGLE_JINGLE_INFO, "query"))
    
        stun = query.addElement('stun')
        server = stun.addElement('server')
        server['host'] = '6.6.6.6'
        server['udp'] = '6666'
    
        relay = query.addElement('relay')
        relay.addElement('token', content='mwohahahahaha')
        server = relay.addElement('server')
        server['host'] = '127.0.0.1'
        server['udp'] = '666'
        server['tcp'] = '999'
        server['tcpssl'] = '666'
    
        self.stream.send(iq)
    
        # Force Gabble to process the capabilities
        sync_stream(self.q, self.stream)


    def connect(self):
        CallTest.connect(self)

        req_pattern = EventPattern('http-request', method='GET',
                path='/create_session')
        req1, req2 = self.q.expect_many(req_pattern, req_pattern)

        if self.params['too-slow'] is not None:
            test_too_slow(req1, req2, too_slow)
            return

    def pickup(self):

        if self.params['too-slow'] is not None:
            return

        CallTest.pickup(self)

        # The new API for STUN servers etc.
        cstream_props = self.audio_stream.GetAll(
            cs.CALL_STREAM_IFACE_MEDIA, dbus_interface=dbus.PROPERTIES_IFACE)

        assert cstream_props['Transport'] == cs.CALL_STREAM_TRANSPORT_GTALK_P2P
    
        # If Gabble has erroneously paid attention to the contact
        # evil@evil.net who sent us a google:jingleinfo stanza, this assertion
        # will fail.
        assertEquals([(self.expected_stun_server, self.expected_stun_port)],
            cstream_props['STUNServers'])
    
        credentials_used = {}
        credentials = {}
    
        for relay in cstream_props['RelayInfo']:
            assert relay['ip'] == '127.0.0.1', cstream_props['RelayInfo']
            assert relay['type'] in ('udp', 'tcp', 'tls')
            assert relay['component'] in (1, 2)
    
            if relay['type'] == 'udp':
                assert relay['port'] == 11111, cstream_props['RelayInfo']
            elif relay['type'] == 'tcp':
                assert relay['port'] == 22222, cstream_props['RelayInfo']
            elif relay['type'] == 'tls':
                assert relay['port'] == 443, cstream_props['RelayInfo']
    
            assert relay['username'][:8] == 'UUUUUUUU', \
                    cstream_props['RelayInfo']
            assert relay['password'][:8] == 'PPPPPPPP', \
                    cstream_props['RelayInfo']
            assert relay['password'][8:] == relay['username'][8:], \
                    cstream_props['RelayInfo']
            assert (relay['password'][8:], relay['type']) \
                    not in credentials_used
            credentials_used[(relay['password'][8:], relay['type'])] = 1
            credentials[(relay['component'], relay['type'])] = \
                    relay['password'][8:]
    
        assert (1, 'udp') in credentials
        assert (1, 'tcp') in credentials
        assert (1, 'tls') in credentials
        assert (2, 'udp') in credentials
        assert (2, 'tcp') in credentials
        assert (2, 'tls') in credentials
    
        assert ('0', 'udp') in credentials_used
        assert ('0', 'tcp') in credentials_used
        assert ('0', 'tls') in credentials_used
        assert ('1', 'udp') in credentials_used
        assert ('1', 'tcp') in credentials_used
        assert ('1', 'tls') in credentials_used
    
        # consistency check, since we currently reimplement Get separately
        for k in cstream_props:
            assert cstream_props[k] == self.audio_stream.Get(
                    cs.CALL_STREAM_IFACE_MEDIA, k,
                    dbus_interface=dbus.PROPERTIES_IFACE)
    
    def test_too_slow(self, req1, req2, too_slow):
        """
        Regression test for a bug where if the channel was closed before the
        HTTP responses arrived, the responses finally arriving crashed Gabble.
        """
    
        # User gets bored, and ends the call.
        e = EventPattern('dbus-signal', signal='Closed',
            path=chan.object_path)
    
        if too_slow == TOO_SLOW_CLOSE:
            call_async(self.q, self.chan, 'Close', dbus_interface=cs.CHANNEL)
        elif too_slow == TOO_SLOW_REMOVE_SELF:
            self.chan.Hangup (0, "", "",
                dbus_interface=cs.CHANNEL_TYPE_CALL)
        elif too_slow == TOO_SLOW_DISCONNECT:
            disconnect_conn(q, conn, stream, [e])
    
            try:
                chan.GetMembers()
            except dbus.DBusException, e:
                # This should fail because the object's gone away, not because
                # Gabble's crashed.
                assert cs.UNKNOWN_METHOD == e.get_dbus_name(), \
                    "maybe Gabble crashed? %s" % e
            else:
                # Gabble will probably also crash in a moment, because the http
                # request callbacks will be called after the channel's meant to
                # have died, which will cause the channel to try to call
                # methods on the (finalized) connection.
                assert False, "the channel should be dead by now"
    
            return
    
        # Hangup does not cause the channel to close
        # except if local user hangup
        if too_slow != TOO_SLOW_REMOVE_SELF:
            q.expect_many(e)
    
        # Now Google answers!
        self.handle_request(req1.request, 2)
        self.handle_request(req2.request, 3)
    
        # Make a misc method call to check that Gabble's still alive.
        sync_dbus(self.bus, self.q, self.conn)

def exec_relay_test(incoming, too_slow=None):
    exec_test(partial(run_call_test, GtalkProtocol04(),
                incoming=incoming, klass=CallGoogleRelayTest,
                params={'too-slow': too_slow}), protocol=GoogleXmlStream)

if __name__ == '__main__':
    exec_relay_test(True)
    exec_relay_test(False)
    exec_relay_test(True,  TOO_SLOW_CLOSE)
    exec_relay_test(False, TOO_SLOW_CLOSE)
    exec_relay_test(True,  TOO_SLOW_REMOVE_SELF)
    exec_relay_test(False, TOO_SLOW_REMOVE_SELF)
    exec_relay_test(True,  TOO_SLOW_DISCONNECT)
    exec_relay_test(False, TOO_SLOW_DISCONNECT)

