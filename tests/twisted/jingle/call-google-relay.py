"""
Test getting relay from Google jingleinfo
"""

import config

if not config.GOOGLE_RELAY_ENABLED:
    print "NOTE: built with --disable-google-relay"
    raise SystemExit(77)

from functools import partial

from gabbletest import exec_test, make_result_iq, sync_stream, \
        GoogleXmlStream, disconnect_conn
from servicetest import make_channel_proxy, \
        EventPattern, call_async, sync_dbus, assertEquals, assertLength, \
        wrap_content
from jingletest2 import JingleTest2, test_dialects, GtalkProtocol04
import gabbletest
import constants as cs
import callutils as cu
import dbus
import ns
import config
from twisted.words.protocols.jabber.client import IQ

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

def test(jp, q, bus, conn, stream, incoming=True, too_slow=None):
    jt = JingleTest2(jp, conn, q, stream, 'test@localhost', 'foo@bar.com/Foo')
    events = q.expect_many(
            EventPattern('stream-iq', query_ns=ns.GOOGLE_JINGLE_INFO),
            EventPattern('stream-iq', to=None, query_ns='vcard-temp',
                query_name='vCard'),
            EventPattern('stream-iq', query_ns=ns.ROSTER),
            )
    jt.prepare(events=events)

    cu.advertise_call(conn)

    self_handle = conn.GetSelfHandle()
    remote_handle = conn.RequestHandles(cs.HT_CONTACT, ["foo@bar.com/Foo"])[0]

    ji_event = events[0]
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
    jingleinfo = None

    # Spoof some jingle info. This is a regression test for
    # <https://bugs.freedesktop.org/show_bug.cgi?id=34048>. We assert that
    # Gabble has ignored this stuff later.
    iq = IQ(stream, 'set')
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

    stream.send(iq)

    # Force Gabble to process the capabilities
    sync_stream(q, stream)
    
    req_pattern = EventPattern('http-request', method='GET', path='/create_session')

    if incoming:
        # Remote end calls us
        jt.incoming_call()
    else:
        # Ensure a channel that doesn't exist yet.
        conn.Requests.CreateChannel(
            { cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_CALL,
            cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
            cs.TARGET_HANDLE: remote_handle,
            cs.CALL_INITIAL_AUDIO: True,
            })

    signal = q.expect('dbus-signal', signal='NewChannels',
        predicate=lambda e:
            cs.CHANNEL_TYPE_CONTACT_LIST not in e.args[0][0][1].values())
    chan = bus.get_object (conn.bus_name, signal.args[0][0][0])

    # Get content/stream
    chan_properties = chan.GetAll(cs.CHANNEL_TYPE_CALL,
            dbus_interface=dbus.PROPERTIES_IFACE)
    content = bus.get_object (conn.bus_name, chan_properties["Contents"][0])
    content_properties = content.GetAll (cs.CALL_CONTENT,
            dbus_interface=dbus.PROPERTIES_IFACE)
    cstream = bus.get_object (conn.bus_name, content_properties["Streams"][0])

    if not incoming:
        chan.Accept(dbus_interface=cs.CHANNEL_TYPE_CALL)
        cstream.CompleteReceivingStateChange(
            cs.CALL_STREAM_FLOW_STATE_STARTED,
            dbus_interface = cs.CALL_STREAM_IFACE_MEDIA)
        q.expect('dbus-signal', signal='ReceivingStateChanged',
                 args = [cs.CALL_STREAM_FLOW_STATE_STARTED],
                 interface = cs.CALL_STREAM_IFACE_MEDIA)

    # Setup codecs
    md = jt.get_call_audio_md_dbus()

    [path, _] = content.Get(cs.CALL_CONTENT_IFACE_MEDIA,
            "MediaDescriptionOffer", dbus_interface=dbus.PROPERTIES_IFACE)
    offer = bus.get_object (conn.bus_name, path)
    offer.Accept (md, dbus_interface=cs.CALL_CONTENT_MEDIADESCRIPTION)

    # Add candidates
    candidates = jt.get_call_remote_transports_dbus ()

    cstream.AddCandidates (candidates,
        dbus_interface=cs.CALL_STREAM_IFACE_MEDIA)

    expected = [ EventPattern('dbus-signal', signal='LocalCandidatesAdded') ]

    if not incoming:
        expected.append (EventPattern('stream-iq',
            predicate=jp.action_predicate('session-initiate')))

    ret = q.expect_many (*expected)

    if not incoming:
        jt.parse_session_initiate(ret[1].query)

    cstream.FinishInitialCandidates (dbus_interface=cs.CALL_STREAM_IFACE_MEDIA)
    endpoints = cstream.Get(cs.CALL_STREAM_IFACE_MEDIA,
            "Endpoints", dbus_interface=dbus.PROPERTIES_IFACE)
    endpoint = bus.get_object (conn.bus_name, endpoints[0])
    endpoint.SetEndpointState (1, cs.CALL_STREAM_ENDPOINT_STATE_FULLY_CONNECTED,
            dbus_interface=cs.CALL_STREAM_ENDPOINT)
    q.expect('dbus-signal', signal='EndpointStateChanged',
            interface=cs.CALL_STREAM_ENDPOINT)
    
    req1 = q.expect('http-request', method='GET', path='/create_session')
    req2 = q.expect('http-request', method='GET', path='/create_session')

    if too_slow is not None:
        test_too_slow(q, bus, conn, stream, req1, req2, chan, too_slow)
        return

    if incoming:
        # Act as if we're ringing
        chan.SetRinging (dbus_interface=cs.CHANNEL_TYPE_CALL)
        signal = q.expect('dbus-signal', signal='CallStateChanged')

        # And now pickup the call
        chan.Accept (dbus_interface=cs.CHANNEL_TYPE_CALL)
        cstream.CompleteReceivingStateChange(
            cs.CALL_STREAM_FLOW_STATE_STARTED,
            dbus_interface = cs.CALL_STREAM_IFACE_MEDIA)
        ret = q.expect_many (
            EventPattern('dbus-signal', signal='CallStateChanged'),
            EventPattern('dbus-signal', signal='ReceivingStateChanged',
                         args = [cs.CALL_STREAM_FLOW_STATE_STARTED],
                         interface = cs.CALL_STREAM_IFACE_MEDIA),
            EventPattern('stream-iq',
                         predicate=jp.action_predicate('session-accept')),
            )
        jt.result_iq(ret[2])
    else:
        if jp.is_modern_jingle():
            # The other person's client starts ringing, and tells us so!
            node = jp.SetIq(jt.peer, jt.jid, [
               jp.Jingle(jt.sid, jt.jid, 'session-info', [
                   ('ringing', ns.JINGLE_RTP_INFO_1, {}, []) ]) ])
            stream.send(jp.xml(node))

            o = q.expect ('dbus-signal', signal="CallMembersChanged")
            assertEquals({ remote_handle: cs.CALL_MEMBER_FLAG_RINGING }, o.args[0])

        jt.accept()
        
        ret = q.expect_many (
            EventPattern('dbus-signal', signal='NewMediaDescriptionOffer'),
            EventPattern('dbus-signal', signal='SendingStateChanged'))

        [path, _ ] = ret[0].args
        md = jt.get_call_audio_md_dbus()
        cu.check_and_accept_offer (q, bus, conn, content, md, remote_handle, path,
                md_changed = False)

    cu.check_state (q, chan, cs.CALL_STATE_ACTIVE)

    # In response to the streams call, we now have two HTTP requests
    # (for RTP and RTCP)
    handle_request(req1.request, 0)
    handle_request(req2.request, 1)

    cstream.CompleteSendingStateChange(
        cs.CALL_STREAM_FLOW_STATE_STARTED,
        dbus_interface = cs.CALL_STREAM_IFACE_MEDIA)
    
    q.expect_many(
        EventPattern('dbus-signal', signal='SendingStateChanged',
                     args = [cs.CALL_STREAM_FLOW_STATE_STARTED],
                     interface = cs.CALL_STREAM_IFACE_MEDIA),
        EventPattern('dbus-signal', signal='RelayInfoChanged',
                     interface = cs.CALL_STREAM_IFACE_MEDIA),
        EventPattern('dbus-signal', signal='ServerInfoRetrieved',
                     interface = cs.CALL_STREAM_IFACE_MEDIA),
        )

    # Exercise channel properties
    channel_props = chan.GetAll(
        cs.CHANNEL, dbus_interface=dbus.PROPERTIES_IFACE)
    assert channel_props['TargetHandle'] == remote_handle
    assert channel_props['TargetHandleType'] == cs.HT_CONTACT
    assert channel_props['TargetID'] == 'foo@bar.com'
    assert channel_props['Requested'] == (not incoming)

    # The new API for STUN servers etc.
    cstream_props = cstream.GetAll(
        cs.CALL_STREAM_IFACE_MEDIA, dbus_interface=dbus.PROPERTIES_IFACE)

    assert cstream_props['Transport'] == cs.CALL_STREAM_TRANSPORT_GTALK_P2P

    # If Gabble has erroneously paid attention to the contact evil@evil.net who
    # sent us a google:jingleinfo stanza, this assertion will fail.
    assertEquals([(expected_stun_server, expected_stun_port)],
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

        assert relay['username'][:8] == 'UUUUUUUU', cstream_props['RelayInfo']
        assert relay['password'][:8] == 'PPPPPPPP', cstream_props['RelayInfo']
        assert relay['password'][8:] == relay['username'][8:], \
                cstream_props['RelayInfo']
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
    for k in cstream_props:
        assert cstream_props[k] == cstream.Get(
                cs.CALL_STREAM_IFACE_MEDIA, k,
                dbus_interface=dbus.PROPERTIES_IFACE)

    # ---- The end ----

    chan.Hangup (0, "", "",
            dbus_interface=cs.CHANNEL_TYPE_CALL)
  
    # Tests completed, close the connection

def test_too_slow(q, bus, conn, stream, req1, req2, chan, too_slow):
    """
    Regression test for a bug where if the channel was closed before the HTTP
    responses arrived, the responses finally arriving crashed Gabble.
    """

    # User gets bored, and ends the call.
    e = EventPattern('dbus-signal', signal='Closed',
        path=chan.object_path)

    if too_slow == TOO_SLOW_CLOSE:
        call_async(q, chan, 'Close', dbus_interface=cs.CHANNEL)
    elif too_slow == TOO_SLOW_REMOVE_SELF:
        chan.Hangup (0, "", "",
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
            # have died, which will cause the channel to try to call methods on
            # the (finalized) connection.
            assert False, "the channel should be dead by now"

        return

    # Hangup does not cause the channel to close
    # FIXME is this right ?
    if too_slow != TOO_SLOW_REMOVE_SELF:
        q.expect_many(e)

    # Now Google answers!
    handle_request(req1.request, 2)
    handle_request(req2.request, 3)

    # Make a misc method call to check that Gabble's still alive.
    sync_dbus(bus, q, conn)

def exec_relay_test(incoming, too_slow=None):
    exec_test(partial(test, GtalkProtocol04(), incoming=incoming,
                too_slow=too_slow), protocol=GoogleXmlStream)

if __name__ == '__main__':
    exec_relay_test(True)
    exec_relay_test(False)
    exec_relay_test(True,  TOO_SLOW_CLOSE)
    exec_relay_test(False, TOO_SLOW_CLOSE)
    exec_relay_test(True,  TOO_SLOW_REMOVE_SELF)
    exec_relay_test(False, TOO_SLOW_REMOVE_SELF)
    exec_relay_test(True,  TOO_SLOW_DISCONNECT)
    exec_relay_test(False, TOO_SLOW_DISCONNECT)

