import dbus

from twisted.words.xish import xpath
from twisted.words.protocols.jabber.client import IQ

from servicetest import (assertEquals, EventPattern, TimeoutError)
from gabbletest import exec_test, make_result_iq, sync_stream, make_presence
import constants as cs

from caps_helper import compute_caps_hash, \
    text_fixed_properties, text_allowed_properties, \
    stream_tube_fixed_properties, stream_tube_allowed_properties, \
    dbus_tube_fixed_properties, dbus_tube_allowed_properties, \
    ft_fixed_properties, ft_allowed_properties

from jingleshareutils import test_ft_caps_from_contact

import ns

from config import JINGLE_FILE_TRANSFER_ENABLED

if not JINGLE_FILE_TRANSFER_ENABLED:
    print "NOTE: built with --disable-file-transfer or --disable-voip"
    raise SystemExit(77)

def test(q, bus, conn, stream):
    client = 'http://telepathy.freedesktop.org/fake-client'
    contact = 'bilbo1@foo.com/Resource'
    files = [("file", "File.txt", 12345, False),
             ("file", "Image.txt", 54321, True),
             ("folder", "Folder", 123, False),
             ("folder", "Folder no size", None, True)]

    test_ft_caps_from_contact(q, bus, conn, stream, contact,
        2L, client)

    self_handle = conn.GetSelfHandle()
    jid =  conn.InspectHandles(cs.HT_CONTACT, [self_handle])[0]

    iq = IQ(stream, "set")
    iq['to'] = jid
    iq['from'] = contact
    session = iq.addElement("session", "http://www.google.com/session")
    session['type'] = "initiate"
    session['id'] = "2156517633"
    session['initiator'] = contact
    session.addElement("transport", "http://www.google.com/transport/p2p")
    description = session.addElement("description",
                                     "http://www.google.com/session/share")

    manifest = description.addElement("manifest")
    for f in files:
        type, name, size, image = f
        file = manifest.addElement(type)
        if size is not None:
            file['size'] = str(size)
        file.addElement("name", None, name)
        if image:
            image = file.addElement("image")
            image['width'] = '1200'
            image['height'] = '1024'

    protocol = description.addElement("protocol")
    http = protocol.addElement("http")
    url = http.addElement("url", None, "/temporary/ade15194140cf7b7bceafe/")
    url['name'] = 'source-path'
    url = http.addElement("url", None, "/temporary/578d715be25ddc28870d3f/")
    url['name'] = 'preview-path'

    stream.send(iq)
    event = q.expect('dbus-signal', signal="NewChannels")
    channels = event.args[0]

    # Make sure we get the right amout of channels
    assert len(channels) == len(files)

    # Make sure every file transfer has a channel associated with it
    found = [False for i in files]
    file_collection = None
    for channel in channels:
        path, props = channel

        # Get the FileCollection and make sure it exists
        if file_collection is None:
            file_collection = props[cs.FT_FILE_COLLECTION]
            assert file_collection != ''
        assert file_collection is not None

        # FileCollection must be the same for every channel
        assert props[cs.FT_FILE_COLLECTION] == file_collection, props

        for i, f in enumerate(files):
            type, name, size, image = f
            if type == "folder":
                name = "%s.tar" % name
            if size is None:
                size = 0

            if props[cs.FT_FILENAME].encode('utf=8') == name:
                assert found[i] == False
                found[i] = True
                assert props[cs.FT_SIZE] == size, props

        assert props[cs.CHANNEL_TYPE] == cs.CHANNEL_TYPE_FILE_TRANSFER, props
        assert props[cs.INTERFACES] == [], props
        assert props[cs.TARGET_HANDLE] == 2L, props
        assert props[cs.TARGET_ID] == contact.replace("/Resource", ""), props
        assert props[cs.TARGET_HANDLE_TYPE] == cs.HT_CONTACT, props
        assert props[cs.REQUESTED] == False, props
        assert props[cs.INITIATOR_HANDLE] == 2L, props
        assert props[cs.INITIATOR_ID] == contact.replace("/Resource", ""), props
        assert props[cs.FT_STATE] == cs.FT_STATE_PENDING, props
        assert props[cs.FT_CONTENT_TYPE] == '', props
        # FT's protocol doesn't allow us the send the hash info
        assert props[cs.FT_CONTENT_HASH_TYPE] == cs.FILE_HASH_TYPE_NONE, props
        assert props[cs.FT_CONTENT_HASH] == '', props
        assert props[cs.FT_DESCRIPTION] == '', props
        assert props[cs.FT_DATE] == 0, props
        assert props[cs.FT_AVAILABLE_SOCKET_TYPES] == \
            {cs.SOCKET_ADDRESS_TYPE_UNIX: [cs.SOCKET_ACCESS_CONTROL_LOCALHOST],
            cs.SOCKET_ADDRESS_TYPE_IPV4: [cs.SOCKET_ACCESS_CONTROL_LOCALHOST],
            cs.SOCKET_ADDRESS_TYPE_IPV6: [cs.SOCKET_ACCESS_CONTROL_LOCALHOST]}, \
            props[cs.FT_AVAILABLE_SOCKET_TYPES]
        assert props[cs.FT_TRANSFERRED_BYTES] == 0, props
        assert props[cs.FT_INITIAL_OFFSET] == 0, props

    assert False not in found

    event = q.expect('stream-iq', to=contact,
                     iq_type='set', query_name='session')
    stanza = event.stanza
    session_node = xpath.queryForNodes('/iq/session', event.stanza)[0]
    assert session_node.attributes['type'] == 'transport-accept'

    # Lower the timeout because we will do a q.expect where we expect it to
    # timeout since we check for the *not* reception of the terminate
    q.timeout = 2

    # Cancel all the channels and make sure gabble cancels the multiFT only
    # once the last channel has been closed
    last_path, props = channels[-1]
    for i in range(len(channels)):
        path, props = channels[i]
        ft_chan = bus.get_object(conn.object.bus_name, path)
        channel = dbus.Interface(ft_chan, cs.CHANNEL)
        channel.Close()
        try:
            event = q.expect('stream-iq', to=contact,
                             iq_type='set', query_name='session')
            # If the iq is received, it must be for the last channel closed
            assert path == last_path, event
            # Make sure it's a terminate message
            stanza = event.stanza
            session_node = xpath.queryForNodes('/iq/session', event.stanza)[0]
            assert session_node.attributes['type'] == 'terminate'
        except TimeoutError, e:
            # Timeout only for the non last channel getting closed
            assert path != last_path

if __name__ == '__main__':
    exec_test(test)
