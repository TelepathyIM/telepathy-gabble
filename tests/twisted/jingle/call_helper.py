"""
Base classes for Call tests
"""

import config

import dbus
from dbus.exceptions import DBusException

from functools import partial
from servicetest import (
    EventPattern, call_async, wrap_channel, wrap_content,
    assertEquals, assertDoesNotContain, assertContains, assertLength,
    assertNotEquals, DictionarySupersetOf)
from gabbletest import sync_stream, make_result_iq
from jingletest2 import JingleTest2, test_all_dialects
import constants as cs
import ns

from config import VOIP_ENABLED

if not VOIP_ENABLED:
    print "NOTE: built with --disable-voip"
    raise SystemExit(77)

class CallTest(object):

    SELF_JID = 'test@localhost'
    PEER_JID = 'foo@bar.com/Foo'

    # These can be changed as needed by base class
    initial_audio = True
    initial_video = False

    # The following will be set after initiate_call()
    chan = None

    audio_content = None
    audio_content_name = None
    audio_stream = None

    video_content = None
    video_content_name = None
    video_stream = None


    def __init__(self, jp, q, bus, conn, stream, incoming, params):
        self.jp = jp
        self.q = q
        self.bus = bus
        self.conn = conn
        self.stream = stream
        self.incoming = incoming
        self.params = params
        self.jt2 = JingleTest2(jp, conn, q, stream, self.SELF_JID,
                self.PEER_JID)
        self.can_change_direction = (jp.dialect not in ['gtalk-v0.3',
                'gtalk-v0.4'])
        self.self_handle = conn.Properties.Get(cs.CONN, "SelfHandle")
        self.peer_handle = conn.RequestHandles(1, [self.PEER_JID])[0]

    def check_channel_state(self, state, wait = False):
        """Optionnally wait for channel state to be reached and check that the
           property has the right value"""
        if wait:
            self.q.expect('dbus-signal', signal='CallStateChanged',
                    interface = cs.CHANNEL_TYPE_CALL,
                    predicate = lambda e: e.args[0] == state)

        assertEquals(state,
                self.chan.Get(cs.CHANNEL_TYPE_CALL, 'CallState',
                    dbus_interface=dbus.PROPERTIES_IFACE))


    def check_stream_recv_state(self, stream, state):
        assertEquals(state,
                stream.Get(cs.CALL_STREAM_IFACE_MEDIA, 'ReceivingState',
                    dbus_interface=dbus.PROPERTIES_IFACE))


    def check_stream_send_state(self, stream, state):
        assertEquals(state,
                stream.Get(cs.CALL_STREAM_IFACE_MEDIA, 'SendingState',
                    dbus_interface=dbus.PROPERTIES_IFACE))


    def complete_receiving_state(self, stream):
        if stream is None:
            return
        self.check_stream_recv_state(stream,
                cs.CALL_STREAM_FLOW_STATE_PENDING_START)
        stream.CompleteReceivingStateChange(cs.CALL_STREAM_FLOW_STATE_STARTED,
                dbus_interface = cs.CALL_STREAM_IFACE_MEDIA)
        self.q.expect('dbus-signal', signal='ReceivingStateChanged',
                args = [cs.CALL_STREAM_FLOW_STATE_STARTED],
                interface = cs.CALL_STREAM_IFACE_MEDIA)


    def check_and_accept_offer(self, content, md, md_changed = True,
            offer_path = None):
        [path, remote_md] = content.Get(cs.CALL_CONTENT_IFACE_MEDIA,
                "MediaDescriptionOffer", dbus_interface=dbus.PROPERTIES_IFACE)

        if offer_path != None:
            assertEquals(offer_path, path)

        assertNotEquals("/", path)

        offer = self.bus.get_object(self.conn.bus_name, path)
        codecmap_property = offer.Get(cs.CALL_CONTENT_MEDIADESCRIPTION,
                "Codecs", dbus_interface=dbus.PROPERTIES_IFACE)

        assertEquals(remote_md[cs.CALL_CONTENT_MEDIADESCRIPTION + '.Codecs'],
                codecmap_property)

        offer.Accept(md, dbus_interface=cs.CALL_CONTENT_MEDIADESCRIPTION)

        current_md = content.Get(cs.CALL_CONTENT_IFACE_MEDIA,
                "LocalMediaDescriptions", dbus_interface=dbus.PROPERTIES_IFACE)
        assertEquals(md,  current_md[self.peer_handle])

        if md_changed:
            o = self.q.expect('dbus-signal',
                    signal='LocalMediaDescriptionChanged')
            assertEquals([md], o.args)


    def store_content(self, content_path, initial = True, incoming = None):
        if incoming is None:
            incoming = self.incoming

        content = wrap_content(self.bus.get_object(self.conn.bus_name,
                    content_path), ['DTMF', 'Media'])
        content_props = content.GetAll(cs.CALL_CONTENT,
                dbus_interface=dbus.PROPERTIES_IFACE)

        # Has one stream
        assertLength(1, content_props["Streams"])
        if initial:
            assertEquals(cs.CALL_DISPOSITION_INITIAL,
                    content_props["Disposition"])
        else:
            assertEquals(cs.CALL_DISPOSITION_NONE, content_props["Disposition"])


        # Implements Content.Interface.Media
        assertContains(cs.CALL_CONTENT_IFACE_MEDIA, content_props["Interfaces"])

        if content_props['Type'] == cs.CALL_MEDIA_TYPE_AUDIO:
            # Implements Content.Interface.DTMF
            assertContains(cs.CALL_CONTENT_IFACE_DTMF,
                    content_props["Interfaces"])

        assertContains("Name", content_props.keys())
        content_name = content_props["Name"]

        stream = self.bus.get_object(self.conn.bus_name,
                content_props["Streams"][0])

        stream_props = stream.GetAll(cs.CALL_STREAM,
                dbus_interface = dbus.PROPERTIES_IFACE)

        assertDoesNotContain(self.self_handle,
                stream_props["RemoteMembers"].keys())
        assertContains(self.peer_handle, stream_props["RemoteMembers"].keys())
        assertEquals([cs.CALL_STREAM_IFACE_MEDIA], stream_props["Interfaces"])
        assertEquals(self.can_change_direction,
                stream_props["CanRequestReceiving"])

        if incoming:
            assertEquals(cs.CALL_SENDING_STATE_PENDING_SEND,
                         stream_props["LocalSendingState"])
            assertEquals(cs.CALL_SENDING_STATE_SENDING,
                         stream_props["RemoteMembers"][self.peer_handle])
        else:
            if initial:
                assertEquals(cs.CALL_SENDING_STATE_PENDING_SEND,
                             stream_props["RemoteMembers"][self.peer_handle])
            else:
                assertEquals(cs.CALL_SENDING_STATE_SENDING,
                             stream_props["RemoteMembers"][self.peer_handle])

            assertEquals(cs.CALL_SENDING_STATE_SENDING,
                         stream_props["LocalSendingState"])

        # Packetization should be RTP
        content_media_props = content.GetAll(cs.CALL_CONTENT_IFACE_MEDIA,
                dbus_interface=dbus.PROPERTIES_IFACE)
        assertEquals(cs.CALL_CONTENT_PACKETIZATION_RTP,
                content_media_props["Packetization"])

        # Check the directions
        stream_media_props = stream.GetAll(cs.CALL_STREAM_IFACE_MEDIA,
                dbus_interface=dbus.PROPERTIES_IFACE)
        if initial or incoming:
            assertEquals(cs.CALL_STREAM_FLOW_STATE_STOPPED,
                         stream_media_props["SendingState"])
        else:
            assertEquals(cs.CALL_STREAM_FLOW_STATE_PENDING_START,
                         stream_media_props["SendingState"])
        if initial:
            assertEquals(cs.CALL_STREAM_FLOW_STATE_STOPPED,
                         stream_media_props["ReceivingState"])
        else:
            assertEquals(cs.CALL_STREAM_FLOW_STATE_PENDING_START,
                         stream_media_props["ReceivingState"])
        assertEquals(False,  stream_media_props["ICERestartPending"])

        # Store the content and stream
        if content_props['Type'] == cs.CALL_MEDIA_TYPE_AUDIO:
            assert self.initial_audio == initial
            assert self.audio_content == None
            assert self.audio_stream == None
            self.audio_content = content
            self.audio_content_name = content_name
            self.audio_stream = stream
        elif content_props['Type'] == cs.CALL_MEDIA_TYPE_VIDEO:
            assert self.initial_video == initial
            assert self.video_content == None
            assert self.video_stream == None
            self.video_content = content
            self.video_content_name = content_name
            self.video_stream = stream
        else:
            assert not 'Bad content type value'


    def enable_endpoint(self, endpoint):
        endpoint.SetEndpointState(cs.CALL_STREAM_COMPONENT_DATA,
                cs.CALL_STREAM_ENDPOINT_STATE_FULLY_CONNECTED,
                dbus_interface=cs.CALL_STREAM_ENDPOINT)
        self.q.expect('dbus-signal', signal='EndpointStateChanged',
                interface=cs.CALL_STREAM_ENDPOINT)

        endpoint.SetEndpointState(cs.CALL_STREAM_COMPONENT_CONTROL,
                cs.CALL_STREAM_ENDPOINT_STATE_FULLY_CONNECTED,
                dbus_interface=cs.CALL_STREAM_ENDPOINT)
        self.q.expect('dbus-signal', signal='EndpointStateChanged',
                interface=cs.CALL_STREAM_ENDPOINT)

        state = endpoint.Get(cs.CALL_STREAM_ENDPOINT,
                "EndpointState",  dbus_interface=dbus.PROPERTIES_IFACE)
        assertEquals(cs.CALL_STREAM_ENDPOINT_STATE_FULLY_CONNECTED, state[1])
        assertEquals(cs.CALL_STREAM_ENDPOINT_STATE_FULLY_CONNECTED, state[2])


    def advertise(self, initial_audio = True, initial_video = True):
        """Advertise that Call is supported"""
        self.conn.ContactCapabilities.UpdateCapabilities([
        (cs.CLIENT + ".CallHandler", [
            { cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_CALL,
                cs.CALL_INITIAL_AUDIO: initial_audio},
            { cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_CALL,
                cs.CALL_INITIAL_VIDEO: initial_video},
            ], [
                cs.CHANNEL_TYPE_CALL + '/gtalk-p2p',
                cs.CHANNEL_TYPE_CALL + '/ice-udp',
                cs.CHANNEL_TYPE_CALL + '/video/h264',
            ]),
        ])


    def prepare(self, events=None):
        """Prepare the JingleTest2 object. This method can be override to trap
           special event linke jingleinfo."""
        self.jt2.prepare(events=events)


    def initiate(self):
        """Brind the call to INITIALISING state. This method will fill the
            channel, contents and streams members."""
        # Ensure a channel that doesn't exist yet.
        if self.incoming:
            if self.initial_audio and self.initial_video:
                self.jt2.incoming_call(audio='audio1', video='video1')
            elif self.initial_audio:
                self.jt2.incoming_call(audio='audio1', video=None)
            else:
                self.jt2.incoming_call(audio=None, video='video1')
        else:
            ret = self.conn.Requests.CreateChannel(
                { cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_CALL,
                  cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
                  cs.TARGET_HANDLE: self.peer_handle,
                  cs.CALL_INITIAL_AUDIO: self.initial_audio,
                  cs.CALL_INITIAL_VIDEO: self.initial_video,
                })

        signal = self.q.expect('dbus-signal', signal='NewChannels',
            predicate=lambda e:
                cs.CHANNEL_TYPE_CONTACT_LIST not in e.args[0][0][1].values())

        assertLength(1, signal.args)
        assertLength(1, signal.args[0])       # one channel
        assertLength(2, signal.args[0][0])    # two struct members
        emitted_props = signal.args[0][0][1]

        assertEquals(
            cs.CHANNEL_TYPE_CALL, emitted_props[cs.CHANNEL_TYPE])

        peer_bare_jid = self.PEER_JID.split('/')[0]
        assertEquals(self.peer_handle, emitted_props[cs.TARGET_HANDLE])
        assertEquals(cs.HT_CONTACT, emitted_props[cs.TARGET_HANDLE_TYPE])
        assertEquals(peer_bare_jid, emitted_props[cs.TARGET_ID])

        assertEquals(not self.incoming, emitted_props[cs.REQUESTED])
        if self.incoming:
            assertEquals(self.peer_handle, emitted_props[cs.INITIATOR_HANDLE])
            assertEquals(peer_bare_jid, emitted_props[cs.INITIATOR_ID])
        else:
            assertEquals(self.self_handle, emitted_props[cs.INITIATOR_HANDLE])
            assertEquals(self.SELF_JID, emitted_props[cs.INITIATOR_ID])

        assertEquals(self.initial_audio, emitted_props[cs.CALL_INITIAL_AUDIO])
        assertEquals(self.initial_video, emitted_props[cs.CALL_INITIAL_VIDEO])

        chan_path = signal.args[0][0][0]
        self.chan = wrap_channel(
                self.bus.get_object(self.conn.bus_name, chan_path),
                'Call', ['Hold'])

        properties = self.chan.GetAll(cs.CHANNEL_TYPE_CALL,
            dbus_interface=dbus.PROPERTIES_IFACE)

        # Check if all the properties are there
        assertEquals(sorted([ "Contents", "CallMembers",
            "CallState", "CallFlags", "CallStateReason", "CallStateDetails",
            "HardwareStreaming", "InitialAudio", "InitialAudioName",
            "InitialVideo", "InitialVideoName", "MutableContents",
            "InitialTransport", "MemberIdentifiers" ]),
            sorted(properties.keys()))

        # Remote member is the target
        assertEquals([self.peer_handle], properties["CallMembers"].keys())
        assertEquals(0, properties["CallMembers"][self.peer_handle])

        # No Hardware Streaming for you
        assertEquals(False, properties["HardwareStreaming"])

        # Store content and stream
        nb_contents = self.initial_audio + self.initial_video
        assertLength(nb_contents, properties["Contents"])

        for content_path in  properties["Contents"]:
            self.store_content(content_path)

        if self.initial_audio:
            assert self.audio_content
        if self.initial_video:
            assert self.video_content


    def accept_outgoing(self):
        """If call is incoming, accept the channel and complete the receiving
           state change. Then do state check. This method shall be called even
           if receiving a call to execute the state sanity checks."""
        # Check if the channel is in the right pending state
        if not self.incoming:
            self.check_channel_state(cs.CALL_STATE_PENDING_INITIATOR)
            self.chan.Accept(dbus_interface=cs.CHANNEL_TYPE_CALL)

            if self.initial_audio:
                self.complete_receiving_state(self.audio_stream)
                # Don't start sending before the call is accepted locally or
                # remotely
                self.check_stream_send_state(self.audio_stream,
                        cs.CALL_STREAM_FLOW_STATE_STOPPED)

            if self.initial_video:
                self.complete_receiving_state(self.video_stream)
                self.check_stream_send_state(self.video_stream,
                        cs.CALL_STREAM_FLOW_STATE_STOPPED)

        # All Direction should be both now for outgoing
        if self.initial_audio:
            stream_props = self.audio_stream.GetAll(cs.CALL_STREAM,
                    dbus_interface = dbus.PROPERTIES_IFACE)

            if self.incoming:
                assertEquals({self.peer_handle: cs.CALL_SENDING_STATE_SENDING},
                        stream_props["RemoteMembers"])
                assertEquals(cs.CALL_SENDING_STATE_PENDING_SEND,
                        stream_props["LocalSendingState"])
            else:
                assertEquals(
                        {self.peer_handle: cs.CALL_SENDING_STATE_PENDING_SEND},
                        stream_props["RemoteMembers"])
                assertEquals(cs.CALL_SENDING_STATE_SENDING,
                        stream_props["LocalSendingState"])

        if self.initial_video:
            stream_props = self.video_stream.GetAll(cs.CALL_STREAM,
                    dbus_interface = dbus.PROPERTIES_IFACE)

            if self.incoming:
                assertEquals({self.peer_handle: cs.CALL_SENDING_STATE_SENDING},
                        stream_props["RemoteMembers"])
                assertEquals(cs.CALL_SENDING_STATE_PENDING_SEND,
                        stream_props["LocalSendingState"])
            else:
                assertEquals(
                        {self.peer_handle: cs.CALL_SENDING_STATE_PENDING_SEND},
                        stream_props["RemoteMembers"])
                assertEquals(cs.CALL_SENDING_STATE_SENDING,
                        stream_props["LocalSendingState"])

        self.check_channel_state(cs.CALL_STATE_INITIALISING)

    def connect_streams(self, contents, streams, mds, expect_after_si=None):
        # Expected to fail since we did not said we are controlling side
        try:
            contents[0].UpdateLocalMediaDescription(mds[0],
                    dbus_interface=cs.CALL_CONTENT_IFACE_MEDIA)
        except DBusException, e:
            if e.get_dbus_name() != cs.NOT_AVAILABLE:
                raise e
        else:
            assert False

        expected = []
        candidates = self.jt2.get_call_remote_transports_dbus()

        for i in range(len(contents)):
            self.check_and_accept_offer(contents[i], mds[i], md_changed=False)
            expected.append(EventPattern('dbus-signal',
                        signal='LocalMediaDescriptionChanged', args=[mds[i]]))

            current_md = contents[i].Get(cs.CALL_CONTENT_IFACE_MEDIA,
                    "LocalMediaDescriptions",
                    dbus_interface=dbus.PROPERTIES_IFACE)
            assertEquals(mds[i],  current_md[self.peer_handle])

            streams[i].SetCredentials(self.jt2.ufrag, self.jt2.pwd,
                    dbus_interface=cs.CALL_STREAM_IFACE_MEDIA)

            expected.append(EventPattern('dbus-signal',
                        signal='LocalCredentialsChanged',
                        args=[self.jt2.ufrag, self.jt2.pwd]))

            credentials = streams[i].GetAll(cs.CALL_STREAM_IFACE_MEDIA,
                    dbus_interface=dbus.PROPERTIES_IFACE)["LocalCredentials"]
            assertEquals((self.jt2.ufrag, self.jt2.pwd), credentials)

            # Add candidates
            streams[i].AddCandidates(candidates,
                dbus_interface=cs.CALL_STREAM_IFACE_MEDIA)

            expected.append(EventPattern('dbus-signal',
                        signal='LocalCandidatesAdded'))

        if not self.incoming:
            expected.append(EventPattern('stream-iq',
                        predicate=self.jp.action_predicate('session-initiate')))

        ret = self.q.expect_many(*expected)
        # Check the first LocalCandidatesAdded signal (third in the array)
        assertEquals(candidates, ret[2].args[0])
        if not self.incoming:
            self.check_session_initiate_iq(ret[-1])

            if expect_after_si is not None:
                sync_stream(self.q, self.stream)
                self.q.unforbid_events(expect_after_si)
            
            self.stream.send(make_result_iq(self.stream, ret[-1].stanza))

            if expect_after_si is not None:
                self.q.expect_many(*expect_after_si)

            self.jt2.parse_session_initiate(ret[-1].query)
        
        endpoints = []

        for stream in streams:
            stream.FinishInitialCandidates(
                    dbus_interface=cs.CALL_STREAM_IFACE_MEDIA)

            local_candidates = stream.Get(cs.CALL_STREAM_IFACE_MEDIA,
                    "LocalCandidates", dbus_interface=dbus.PROPERTIES_IFACE)
            assertEquals(candidates,  local_candidates)

            endpoint_paths = stream.Get(cs.CALL_STREAM_IFACE_MEDIA,
                    "Endpoints", dbus_interface=dbus.PROPERTIES_IFACE)
            assertLength(1, endpoint_paths)

            # There doesn't seem to be a good way to get the transport type from
            # the JP used, for now assume we prefer gtalk p2p and always pick
            # that..
            transport = stream.Get(cs.CALL_STREAM_IFACE_MEDIA,
                    "Transport", dbus_interface=dbus.PROPERTIES_IFACE)
            assertEquals(cs.CALL_STREAM_TRANSPORT_GTALK_P2P, transport)

            endpoint = self.bus.get_object(self.conn.bus_name,
                    endpoint_paths[0])
            endpoints.append(endpoint)

            endpoint_props = endpoint.GetAll(cs.CALL_STREAM_ENDPOINT,
                    dbus_interface=dbus.PROPERTIES_IFACE)
            transport = endpoint_props["Transport"]
            assertEquals(cs.CALL_STREAM_TRANSPORT_GTALK_P2P, transport)

            remote_candidates = endpoint.Get(cs.CALL_STREAM_ENDPOINT,
                    "RemoteCandidates",  dbus_interface=dbus.PROPERTIES_IFACE)

            assertEquals([], remote_candidates)

            selected_candidate = endpoint.Get(cs.CALL_STREAM_ENDPOINT,
                    "SelectedCandidatePairs",
                    dbus_interface=dbus.PROPERTIES_IFACE)
            assertEquals([], selected_candidate)

            state = endpoint.Get(cs.CALL_STREAM_ENDPOINT,
                    "EndpointState",  dbus_interface=dbus.PROPERTIES_IFACE)
            assertEquals({}, state)

        names = []
        for content in contents:
            if content is self.audio_content:
                names.append(self.jt2.audio_names[0])
            else:
                names.append(self.jt2.video_names[0])

        for name in names:
            if self.jp.dialect == 'gtalk-v0.3':
                # Candidates must be sent one at a time.
                for candidate in self.jt2.get_call_remote_transports_dbus():
                    component, addr, port, props = candidate
                    self.jt2.send_remote_candidates_call_xmpp(
                            name, "initiator", [candidate])
                    self.q.expect('dbus-signal',
                            signal='RemoteCandidatesAdded',
                            interface=cs.CALL_STREAM_ENDPOINT,
                            args=[[(component, addr, port,
                                DictionarySupersetOf(props))]])
            elif self.jp.dialect == 'gtalk-v0.4' and not self.incoming:
                # Don't test this case at all.
                pass
            else:
                self.jt2.send_remote_candidates_call_xmpp(name, "initiator")

                candidates = []
                for component, addr, port, props in \
                        self.jt2.get_call_remote_transports_dbus():
                    candidates.append((component, addr, port,
                                DictionarySupersetOf(props)))

                self.q.expect('dbus-signal',
                        signal='RemoteCandidatesAdded',
                        interface=cs.CALL_STREAM_ENDPOINT,
                        args=[candidates])

        # FIXME: makes sense to have same local and remote candidate?
        candidate1 = self.jt2.get_call_remote_transports_dbus()[0]
        candidate2 = self.jt2.get_call_remote_transports_dbus()[1]

        for endpoint in endpoints:
            # Expected to fail since we did not said we are controlling side
            try:
                endpoint.SetSelectedCandidatePair(candidate1, candidate1,
                    dbus_interface=cs.CALL_STREAM_ENDPOINT)
            except DBusException, e:
                if e.get_dbus_name() != cs.INVALID_ARGUMENT:
                    raise e
            else:
                assert false

            endpoint.SetControlling(True,
                    dbus_interface=cs.CALL_STREAM_ENDPOINT)
            endpoint.SetSelectedCandidatePair(candidate1, candidate1,
                    dbus_interface=cs.CALL_STREAM_ENDPOINT)

            pair = self.q.expect('dbus-signal',
                    signal='CandidatePairSelected',
                    interface=cs.CALL_STREAM_ENDPOINT)
            assertEquals(candidate1, pair.args[0])
            assertEquals(candidate1, pair.args[1])

            endpoint.SetSelectedCandidatePair(candidate2, candidate2,
                    dbus_interface=cs.CALL_STREAM_ENDPOINT)

            # We have an RTCP candidate as well, so we should set this as
            # selected too.
            pair = self.q.expect('dbus-signal', signal='CandidatePairSelected',
                    interface=cs.CALL_STREAM_ENDPOINT)
            assertEquals(candidate2, pair.args[0])
            assertEquals(candidate2, pair.args[1])

            pairs = endpoint.Get(cs.CALL_STREAM_ENDPOINT,
                    "SelectedCandidatePairs",
                    dbus_interface=dbus.PROPERTIES_IFACE)
            assertEquals(len(pairs), 2)
            assertEquals(pairs[0][0], pairs[0][1])
            assertEquals(pairs[1][0], pairs[1][1])
            if pairs[0][0] == candidate1:
                assertEquals(pairs[1][0], candidate2)
            else:
                assertEquals(pairs[0][0], candidate2)
                assertEquals(pairs[1][0], candidate1)

            # setting endpoints to CONNECTED should make the call state move
            # from INITIALISING to INITIALISED
            self.enable_endpoint(endpoint)

        self.check_channel_state(cs.CALL_STATE_INITIALISED)

    def check_session_initiate_iq(self, e):
        """e is the session-initiate stream-iq event."""
        pass

    def connect(self, expect_after_si=None):
        """Negotiate all the codecs, bringing the channel to INITIALISED
           state"""

        contents = []
        streams = []
        mds = []

        if self.initial_audio:
            # Setup media description
            contents.append(self.audio_content)
            streams.append(self.audio_stream)
            mds.append(self.jt2.get_call_audio_md_dbus(self.peer_handle))

        if self.initial_video:
            contents.append(self.video_content)
            streams.append(self.video_stream)
            mds.append(self.jt2.get_call_video_md_dbus(self.peer_handle))

        self.connect_streams(contents, streams, mds,
                expect_after_si=expect_after_si)


    def pickup(self, held=False):
        if self.initial_audio:
            self.check_stream_send_state(self.audio_stream,
                    cs.CALL_STREAM_FLOW_STATE_STOPPED)
        if self.initial_video:
            self.check_stream_send_state(self.video_stream,
                    cs.CALL_STREAM_FLOW_STATE_STOPPED)

        if self.incoming:
            # Act as if we're ringing
            self.chan.SetRinging(dbus_interface=cs.CHANNEL_TYPE_CALL)
            signal = self.q.expect('dbus-signal', signal='CallStateChanged')
            assertEquals(cs.CALL_FLAG_LOCALLY_RINGING,
                    signal.args[1] & cs.CALL_FLAG_LOCALLY_RINGING)

            # And now pickup the call
            self.chan.Accept(dbus_interface=cs.CHANNEL_TYPE_CALL)

            expected = [
                EventPattern('dbus-signal', signal='CallStateChanged'),
                EventPattern('stream-iq',
                        predicate=self.jp.action_predicate('session-accept'))]
            if self.initial_audio:
                # SendingStateChanged is caused by chan.Accept
                expected.append(EventPattern('dbus-signal',
                            signal='SendingStateChanged'))
                recv_state = self.audio_stream.GetAll(
                        cs.CALL_STREAM_IFACE_MEDIA,
                        dbus_interface=dbus.PROPERTIES_IFACE)["ReceivingState"]
                assertEquals(cs.CALL_STREAM_FLOW_STATE_PENDING_START,
                        recv_state)
                self.audio_stream.CompleteReceivingStateChange(
                        cs.CALL_STREAM_FLOW_STATE_STARTED,
                        dbus_interface = cs.CALL_STREAM_IFACE_MEDIA)
                expected.append(EventPattern('dbus-signal',
                            signal='ReceivingStateChanged'))

            if self.initial_video:
                # SendingStateChanged is caused by chan.Accept
                expected.append(EventPattern('dbus-signal',
                            signal='SendingStateChanged'))
                recv_state = self.video_stream.GetAll(
                        cs.CALL_STREAM_IFACE_MEDIA,
                        dbus_interface=dbus.PROPERTIES_IFACE)["ReceivingState"]
                assertEquals(cs.CALL_STREAM_FLOW_STATE_PENDING_START,
                        recv_state)
                self.video_stream.CompleteReceivingStateChange(
                        cs.CALL_STREAM_FLOW_STATE_STARTED,
                        dbus_interface = cs.CALL_STREAM_IFACE_MEDIA)
                expected.append(EventPattern('dbus-signal',
                            signal='ReceivingStateChanged'))

            ret = self.q.expect_many(*expected)

            assertEquals(0, ret[0].args[1] & cs.CALL_FLAG_LOCALLY_RINGING)
            if self.initial_audio and self.initial_video:
                assertEquals(cs.CALL_STREAM_FLOW_STATE_PENDING_START,
                        ret[2].args[0])
                assertEquals(cs.CALL_STREAM_FLOW_STATE_PENDING_START,
                        ret[3].args[0])
            else:
                assertEquals(cs.CALL_STREAM_FLOW_STATE_PENDING_START,
                        ret[2].args[0])

            self.jt2.result_iq(ret[1])
        else:
            if self.jp.is_modern_jingle():
                # The other person's client starts ringing, and tells us so!
                node = self.jp.SetIq(self.jt2.peer, self.jt2.jid, [
                    self.jp.Jingle(self.jt2.sid, self.jt2.jid, 'session-info', [
                        ('ringing', ns.JINGLE_RTP_INFO_1, {}, []) ]) ])
                self.stream.send(self.jp.xml(node))

                o = self.q.expect('dbus-signal', signal="CallMembersChanged")
                assertEquals(cs.CALL_MEMBER_FLAG_RINGING,
                        o.args[0][self.peer_handle])

            self.jt2.accept()

            expected = [EventPattern('dbus-signal',
                    signal='NewMediaDescriptionOffer')]

            if not held:
                if self.initial_audio:
                    expected.append(EventPattern('dbus-signal',
                                signal='SendingStateChanged'))
                if self.initial_video:
                    expected.append(EventPattern('dbus-signal',
                                signal='SendingStateChanged'))
    
            ret = self.q.expect_many(*expected)

            if not held:
                # Checking one of sending states
                assertEquals(cs.CALL_STREAM_FLOW_STATE_PENDING_START,
                        ret[1].args[0])

            if self.initial_audio:
                md = self.jt2.get_call_audio_md_dbus(self.peer_handle)
                self.check_and_accept_offer(self.audio_content, md,
                        md_changed = False)
            if self.initial_video:
                md = self.jt2.get_call_video_md_dbus(self.peer_handle)
                self.check_and_accept_offer(self.video_content, md,
                        md_changed = False)

        self.check_channel_state(cs.CALL_STATE_ACTIVE)

        # All Direction should be both sending now

        if self.initial_audio and not held:
            stream_props = self.audio_stream.GetAll(cs.CALL_STREAM,
                    dbus_interface = dbus.PROPERTIES_IFACE)
            assertEquals({self.peer_handle: cs.CALL_SENDING_STATE_SENDING},
                    stream_props["RemoteMembers"])
            assertEquals(cs.CALL_SENDING_STATE_SENDING,
                    stream_props["LocalSendingState"])
            assertEquals(cs.CALL_STREAM_FLOW_STATE_PENDING_START,
                    self.audio_stream.Get(cs.CALL_STREAM_IFACE_MEDIA,
                        "SendingState",
                        dbus_interface = dbus.PROPERTIES_IFACE))

            self.audio_stream.CompleteSendingStateChange(
                    cs.CALL_STREAM_FLOW_STATE_STARTED,
                    dbus_interface = cs.CALL_STREAM_IFACE_MEDIA)
            o = self.q.expect('dbus-signal', signal='SendingStateChanged',
                    interface = cs.CALL_STREAM_IFACE_MEDIA)
            assertEquals(cs.CALL_STREAM_FLOW_STATE_STARTED, o.args[0])

        if self.initial_video and not held:
            stream_props = self.video_stream.GetAll(cs.CALL_STREAM,
                    dbus_interface = dbus.PROPERTIES_IFACE)
            assertEquals({self.peer_handle: cs.CALL_SENDING_STATE_SENDING},
                    stream_props["RemoteMembers"])
            assertEquals(cs.CALL_SENDING_STATE_SENDING,
                    stream_props["LocalSendingState"])
            assertEquals(cs.CALL_STREAM_FLOW_STATE_PENDING_START,
                    self.video_stream.Get(cs.CALL_STREAM_IFACE_MEDIA,
                        "SendingState",
                        dbus_interface = dbus.PROPERTIES_IFACE))

            self.video_stream.CompleteSendingStateChange(
                    cs.CALL_STREAM_FLOW_STATE_STARTED,
                    dbus_interface = cs.CALL_STREAM_IFACE_MEDIA)
            o = self.q.expect('dbus-signal', signal='SendingStateChanged',
                    interface = cs.CALL_STREAM_IFACE_MEDIA)
            assertEquals(cs.CALL_STREAM_FLOW_STATE_STARTED, o.args[0])

    def hangup(self):
        if self.incoming:
            self.jt2.terminate()
        else:
            self.chan.Hangup(0, "", "",
                dbus_interface=cs.CHANNEL_TYPE_CALL)

        self.check_channel_state(cs.CALL_STATE_ENDED, wait = True)


    def run(self):
        if self.initial_video:
            if not self.initial_audio and not self.jp.can_do_video_only():
                return
            elif not self.jp.can_do_video():
                return
        self.advertise()
        self.prepare()
        self.initiate()
        self.accept_outgoing()
        self.connect()
        self.pickup()
        self.hangup()


def run_call_test(jp, q, bus, conn, stream, klass=CallTest, incoming=False,
        params={}):
    test = klass(jp, q, bus, conn, stream, incoming, params)
    test.run()

if __name__ == '__main__':
    test_all_dialects(partial(run_call_test, incoming=False))
    test_all_dialects(partial(run_call_test, incoming=True))

