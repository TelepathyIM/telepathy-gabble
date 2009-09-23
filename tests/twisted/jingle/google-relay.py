"""
Test getting relay from Google jingleinfo
"""

from gabbletest import exec_test, make_result_iq, sync_stream, \
        GoogleXmlStream, disconnect_conn
from servicetest import make_channel_proxy, \
        EventPattern, call_async, sync_dbus, assertEquals, assertLength
import jingletest
import gabbletest
import constants as cs
import dbus

from twisted.web import http

from httptest import listen_http

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
    req.write(response_template % (n, n))
    req.finish()

TOO_SLOW_CLOSE = 1
TOO_SLOW_REMOVE_SELF = 2
TOO_SLOW_DISCONNECT = 3

def test(q, bus, conn, stream, incoming=True, too_slow=None):
    jt = jingletest.JingleTest(stream, 'test@localhost', 'foo@bar.com/Foo')

    # If we need to override remote caps, feats, codecs or caps,
    # this is a good time to do it

    # Connecting
    conn.Connect()

    ji_event = q.expect_many(
            EventPattern('dbus-signal', signal='StatusChanged',
                args=[cs.CONN_STATUS_CONNECTING, cs.CSR_REQUESTED]),
            EventPattern('stream-authenticated'),
            EventPattern('dbus-signal', signal='PresenceUpdate',
                args=[{1L: (0L, {u'available': {}})}]),
            EventPattern('dbus-signal', signal='StatusChanged',
                args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED]),

            # See: http://code.google.com/apis/talk/jep_extensions/jingleinfo.html
            EventPattern('stream-iq', query_ns='google:jingleinfo',
                to='test@localhost'),
            )[-1]

    listen_port = listen_http(q, 0)

    jingleinfo = make_result_iq(stream, ji_event.stanza)
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
    server['host'] = '127.0.0.1'
    server['udp'] = '11111'
    server['tcp'] = '22222'
    server['tcpssl'] = '443'
    # The special regression-test build of Gabble parses this attribute,
    # because we can't listen on port 80
    server['gabble-test-http-port'] = str(listen_port.getHost().port)
    stream.send(jingleinfo)

    # We need remote end's presence for capabilities
    jt.send_remote_presence()

    # Gabble doesn't trust it, so makes a disco
    event = q.expect('stream-iq', query_ns='http://jabber.org/protocol/disco#info',
             to='foo@bar.com/Foo')

    jt.send_remote_disco_reply(event.stanza)

    # Force Gabble to process the capabilities
    sync_stream(q, stream)

    remote_handle = conn.RequestHandles(cs.HT_CONTACT, ["foo@bar.com/Foo"])[0]
    self_handle = conn.GetSelfHandle()
    req_pattern = EventPattern('http-request', method='GET', path='/create_session')

    if incoming:
        # Remote end calls us
        jt.incoming_call()

        # FIXME: these signals are not observable by real clients, since they
        #        happen before NewChannels.
        # The caller is in members
        # We're pending because of remote_handle
        mc, _, e, req1, req2 = q.expect_many(
            EventPattern('dbus-signal', signal='MembersChanged',
                 args=[u'', [remote_handle], [], [], [], 0, 0]),
            EventPattern('dbus-signal', signal='MembersChanged',
                 args=[u'', [], [], [self_handle], [], remote_handle,
                       cs.GC_REASON_INVITED]),
            EventPattern('dbus-signal', signal='NewSessionHandler'),
            req_pattern,
            req_pattern)

        media_chan = make_channel_proxy(conn, mc.path,
            'Channel.Interface.Group')
        media_iface = make_channel_proxy(conn, mc.path,
            'Channel.Type.StreamedMedia')
    else:
        call_async(q, conn.Requests, 'CreateChannel',
                { cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_STREAMED_MEDIA,
                  cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
                  cs.TARGET_HANDLE: remote_handle,
                  })
        ret, old_sig, new_sig = q.expect_many(
            EventPattern('dbus-return', method='CreateChannel'),
            EventPattern('dbus-signal', signal='NewChannel'),
            EventPattern('dbus-signal', signal='NewChannels'),
            )
        path = ret.value[0]
        media_chan = make_channel_proxy(conn, path, 'Channel.Interface.Group')
        media_iface = make_channel_proxy(conn, path,
                'Channel.Type.StreamedMedia')
        call_async(q, media_iface, 'RequestStreams',
                remote_handle, [cs.MEDIA_STREAM_TYPE_AUDIO])
        e, req1, req2 = q.expect_many(
            EventPattern('dbus-signal', signal='NewSessionHandler'),
            req_pattern,
            req_pattern)

    # S-E gets notified about new session handler, and calls Ready on it
    assert e.args[1] == 'rtp'

    if too_slow is not None:
        test_too_slow(q, bus, conn, stream, req1, req2, media_chan, too_slow)
        return

    if incoming:
        assertLength(0, media_iface.ListStreams())
        # Accept the call.
        media_chan.AddMembers([self_handle], '')

    # In response to the streams call, we now have two HTTP requests
    # (for RTP and RTCP)
    handle_request(req1.request, 0)
    handle_request(req2.request, 1)

    if incoming:
        # We accepted the call, and it should get a new, bidirectional stream
        # now that the relay info request has finished. This tests against a
        # regression of bug #24023.
        q.expect('dbus-signal', signal='StreamAdded',
            args=[1, remote_handle, cs.MEDIA_STREAM_TYPE_AUDIO])
        q.expect('dbus-signal', signal='StreamDirectionChanged',
            args=[1, cs.MEDIA_STREAM_DIRECTION_BIDIRECTIONAL, 0])
    else:
        # Now that we have the relay info, RequestStreams can return
        q.expect('dbus-return', method='RequestStreams')

    session_handler = make_channel_proxy(conn, e.args[0], 'Media.SessionHandler')
    session_handler.Ready()

    e = q.expect('dbus-signal', signal='NewStreamHandler')
    stream_handler = make_channel_proxy(conn, e.args[0], 'Media.StreamHandler')

    # Exercise channel properties
    channel_props = media_chan.GetAll(
        cs.CHANNEL, dbus_interface=dbus.PROPERTIES_IFACE)
    assert channel_props['TargetHandle'] == remote_handle
    assert channel_props['TargetHandleType'] == cs.HT_CONTACT
    assert channel_props['TargetID'] == 'foo@bar.com'
    assert channel_props['Requested'] == (not incoming)

    # The new API for STUN servers etc.
    sh_props = stream_handler.GetAll(
        cs.STREAM_HANDLER, dbus_interface=dbus.PROPERTIES_IFACE)

    assert sh_props['NATTraversal'] == 'gtalk-p2p'
    assert sh_props['CreatedLocally'] == (not incoming)
    assert sh_props['STUNServers'] == \
        [(expected_stun_server, expected_stun_port)], \
        sh_props['STUNServers']

    credentials_used = {}
    credentials = {}

    for relay in sh_props['RelayInfo']:
        assert relay['ip'] == '127.0.0.1', sh_props['RelayInfo']
        assert relay['type'] in ('udp', 'tcp', 'tls')
        assert relay['component'] in (1, 2)

        if relay['type'] == 'udp':
            assert relay['port'] == 11111, sh_props['RelayInfo']
        elif relay['type'] == 'tcp':
            assert relay['port'] == 22222, sh_props['RelayInfo']
        elif relay['type'] == 'tls':
            assert relay['port'] == 443, sh_props['RelayInfo']

        assert relay['username'][:8] == 'UUUUUUUU', sh_props['RelayInfo']
        assert relay['password'][:8] == 'PPPPPPPP', sh_props['RelayInfo']
        assert relay['password'][8:] == relay['username'][8:], \
                sh_props['RelayInfo']
        assert (relay['password'][8:], relay['type']) not in credentials_used
        credentials_used[(relay['password'][8:], relay['type'])] = 1
        credentials[(relay['component'], relay['type'])] = relay['password'][8:]

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
    for k in sh_props:
        assert sh_props[k] == stream_handler.Get(
                'org.freedesktop.Telepathy.Media.StreamHandler', k,
                dbus_interface=dbus.PROPERTIES_IFACE), k

    media_chan.RemoveMembers([self_handle], '')

    if incoming:
        q.expect_many(
            EventPattern('stream-iq',
                predicate=lambda e: e.query is not None and
                    e.query.name == 'jingle' and
                    e.query['action'] == 'session-terminate'),
            EventPattern('dbus-signal', signal='Closed'),
            )
    else:
        # We haven't sent a session-initiate, so we shouldn't expect to send a
        # session-terminate.
        q.expect('dbus-signal', signal='Closed')

    # Tests completed, close the connection

def test_too_slow(q, bus, conn, stream, req1, req2, media_chan, too_slow):
    """
    Regression test for a bug where if the channel was closed before the HTTP
    responses arrived, the responses finally arriving crashed Gabble.
    """

    # User gets bored, and ends the call.
    e = EventPattern('dbus-signal', signal='Closed',
        path=media_chan.object_path)

    if too_slow == TOO_SLOW_CLOSE:
        call_async(q, media_chan, 'Close', dbus_interface=cs.CHANNEL)
    elif too_slow == TOO_SLOW_REMOVE_SELF:
        media_chan.RemoveMembers([conn.GetSelfHandle()], "",
            dbus_interface=cs.CHANNEL_IFACE_GROUP)
    elif too_slow == TOO_SLOW_DISCONNECT:
        disconnect_conn(q, conn, stream, [e])

        try:
            media_chan.GetMembers()
        except dbus.DBusException, e:
            # This should fail because the object's gone away, not because
            # Gabble's crashed.
            assert cs.UNKNOWN_METHOD == e.get_dbus_name(), \
                "maybe Gabble crashed? %s" % e
        else:
            # Gabble will probably also crash in a moment, because the http
            # request callbacks will be called after the channel's meant to
            # have died, which will cause the channel to try to call methods on
            # the (finalized) connection.
            assert False, "the channel should be dead by now"

        return

    q.expect_many(e)

    # Now Google answers!
    handle_request(req1.request, 2)
    handle_request(req2.request, 3)

    # Make a misc method call to check that Gabble's still alive.
    sync_dbus(bus, q, conn)

def exec_relay_test(incoming, too_slow=None):
    exec_test(
        lambda q, b, c, s:
            test(q, b, c, s, incoming=incoming, too_slow=too_slow),
        protocol=GoogleXmlStream)

if __name__ == '__main__':
    exec_relay_test(True)
    exec_relay_test(False)
    exec_relay_test(True,  TOO_SLOW_CLOSE)
    exec_relay_test(False, TOO_SLOW_CLOSE)
    exec_relay_test(True,  TOO_SLOW_REMOVE_SELF)
    exec_relay_test(False, TOO_SLOW_REMOVE_SELF)
    exec_relay_test(True,  TOO_SLOW_DISCONNECT)
    exec_relay_test(False, TOO_SLOW_DISCONNECT)

