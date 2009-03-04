"""
Test getting relay from Google jingleinfo
"""

from gabbletest import exec_test, make_result_iq, sync_stream, \
        GoogleXmlStream
from servicetest import make_channel_proxy, unwrap, tp_path_prefix, \
        EventPattern
import jingletest
import gabbletest
import constants as c
import dbus
import time
import BaseHTTPServer

http_req = 0

class HTTPHandler(BaseHTTPServer.BaseHTTPRequestHandler):

    def do_GET(self):
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

        global http_req

        assert self.path == '/create_session'
        self.send_response(200)
        self.send_header('Content-Type', 'text/plain')
        self.end_headers()
        self.wfile.write("""
c3
relay.ip=127.0.0.1
relay.udp_port=11111
relay.tcp_port=22222
relay.ssltcp_port=443
stun.ip=1.2.3.4
stun.port=12345
username=UUUUUUUU%d
password=PPPPPPPP%d
magic_cookie=MMMMMMMM
""" % (http_req, http_req))
        http_req += 1

def test(q, bus, conn, stream):
    jt = jingletest.JingleTest(stream, 'test@localhost', 'foo@bar.com/Foo')

    # If we need to override remote caps, feats, codecs or caps,
    # this is a good time to do it

    # Connecting
    conn.Connect()

    q.expect_many(
            EventPattern('dbus-signal', signal='StatusChanged', args=[1, 1]),
            EventPattern('stream-authenticated'),
            EventPattern('dbus-signal', signal='PresenceUpdate',
                args=[{1L: (0L, {u'available': {}})}]),
            EventPattern('dbus-signal', signal='StatusChanged', args=[0, 1]),
            )

    httpd = BaseHTTPServer.HTTPServer(('127.0.0.1', 0), HTTPHandler)

    # See: http://code.google.com/apis/talk/jep_extensions/jingleinfo.html
    event = q.expect('stream-iq', query_ns='google:jingleinfo',
            to='test@localhost')
    jingleinfo = make_result_iq(stream, event.stanza)
    stun = jingleinfo.firstChildElement().addElement('stun')
    server = stun.addElement('server')
    server['host'] = 'resolves-to-1.2.3.4'
    server['udp'] = '12345'

    expected_stun_server = '1.2.3.4'
    expected_stun_port = 12345

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
    server['host'] = 'localhost'
    server['udp'] = '11111'
    server['tcp'] = '22222'
    server['tcpssl'] = '443'
    # The special regression-test build of Gabble parses this attribute,
    # because we can't listen on port 80
    server['gabble-test-http-port'] = str(httpd.server_port)
    stream.send(jingleinfo)

    # We need remote end's presence for capabilities
    jt.send_remote_presence()

    # Gabble doesn't trust it, so makes a disco
    event = q.expect('stream-iq', query_ns='http://jabber.org/protocol/disco#info',
             to='foo@bar.com/Foo')

    jt.send_remote_disco_reply(event.stanza)

    # Force Gabble to process the caps before calling RequestChannel
    sync_stream(q, stream)

    remote_handle = conn.RequestHandles(1, ["foo@bar.com/Foo"])[0]

    # Remote end calls us
    jt.incoming_call()

    # The caller is in members
    e = q.expect('dbus-signal', signal='MembersChanged',
             args=[u'', [remote_handle], [], [], [], 0, 0])

    # We're pending because of remote_handle
    e = q.expect('dbus-signal', signal='MembersChanged',
             args=[u'', [], [], [1L], [], remote_handle, 0])

    # S-E gets notified about new session handler, and calls Ready on it
    e = q.expect('dbus-signal', signal='NewSessionHandler')
    assert e.args[1] == 'rtp'

    # In response to the call, we (should) have two http requests (one for
    # RTP and one for RTCP)
    httpd.handle_request()
    httpd.handle_request()

    session_handler = make_channel_proxy(conn, e.args[0], 'Media.SessionHandler')
    session_handler.Ready()

    e = q.expect('dbus-signal', signal='NewStreamHandler')
    stream_handler = make_channel_proxy(conn, e.args[0], 'Media.StreamHandler')

    media_chan = make_channel_proxy(conn, tp_path_prefix + e.path, 'Channel.Interface.Group')

    # Exercise channel properties
    channel_props = media_chan.GetAll(
            'org.freedesktop.Telepathy.Channel',
            dbus_interface=dbus.PROPERTIES_IFACE)
    assert channel_props['TargetHandle'] == remote_handle
    assert channel_props['TargetHandleType'] == 1
    assert channel_props['TargetID'] == 'foo@bar.com'
    assert channel_props['Requested'] == False
    assert channel_props['InitiatorID'] == 'foo@bar.com'
    assert channel_props['InitiatorHandle'] == remote_handle

    # The new API for STUN servers etc.
    sh_props = stream_handler.GetAll(
            'org.freedesktop.Telepathy.Media.StreamHandler',
            dbus_interface=dbus.PROPERTIES_IFACE)

    assert sh_props['NATTraversal'] == 'gtalk-p2p'
    assert sh_props['CreatedLocally'] == False
    assert sh_props['STUNServers'] == \
        [(expected_stun_server, expected_stun_port)], \
        sh_props['STUNServers']
    #assert sh_props['RelayInfo'] == expected_relays

    # consistency check, since we currently reimplement Get separately
    for k in sh_props:
        assert sh_props[k] == stream_handler.Get(
                'org.freedesktop.Telepathy.Media.StreamHandler', k,
                dbus_interface=dbus.PROPERTIES_IFACE), k

    media_chan.RemoveMembers([dbus.UInt32(1)], 'rejected')

    iq, signal = q.expect_many(
            EventPattern('stream-iq'),
            EventPattern('dbus-signal', signal='Closed'),
            )
    assert iq.query.name == 'jingle'
    assert iq.query['action'] == 'session-terminate'

    # Tests completed, close the connection

    conn.Disconnect()
    q.expect('dbus-signal', signal='StatusChanged', args=[2, 1])

    return True


if __name__ == '__main__':
    exec_test(test, protocol=GoogleXmlStream)
