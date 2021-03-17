import dbus

from twisted.words.protocols.jabber.client import IQ

from servicetest import assertEquals, assertSameSets, EventPattern
from gabbletest import exec_test, sync_stream
import constants as cs

from jingleshareutils import test_ft_caps_from_contact

from config import JINGLE_FILE_TRANSFER_ENABLED

if not JINGLE_FILE_TRANSFER_ENABLED:
    print("NOTE: built with --disable-file-transfer or --disable-voip")
    raise SystemExit(77)

def test(q, bus, conn, stream):
    client = 'http://telepathy.freedesktop.org/fake-client'
    contact = 'bilbo1@foo.com/Resource'
    files = [("file", "File.txt", 12345, False),
             ("file", "Image.txt", 54321, True),
             ("folder", "Folder", 123, False),
             ("folder", "Folder no size", None, True)]

    test_ft_caps_from_contact(q, bus, conn, stream, contact,
        2, client)

    self_handle = conn.Properties.Get(cs.CONN, "SelfHandle")
    jid =  conn.inspect_contact_sync(self_handle)

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

    patterns = []
    found = {}

    def get_predicate(name, found, i):
        # This needs to be a function so that name, found, i
        # are part of a closure.

        # /!\ This predicate has side-effects: it writes to 'found'
        def predicate(e):
            path, props = e.args[0][0]
            if props[cs.CHANNEL_TYPE] != cs.CHANNEL_TYPE_FILE_TRANSFER:
                return False

            if props[cs.FT_FILENAME] == name:
                found[i] = (path, props)
                return True
        return predicate

    for i, f in enumerate(files):
        type, name, size, image = f
        if type == "folder":
            name = "%s.tar" % name

            return False

        patterns.append(EventPattern('dbus-signal',
            signal='NewChannels',
            predicate=get_predicate(name, found, i)))

    # Make sure every file transfer has a channel associated with it
    file_collection = None
    q.expect_many(*patterns)
    assertLength(len(files), found)

    channels = []
    for i in found:
        assert found[i] is not None
        path, props = found[i]
        channels.append((path, props))

        # Get the FileCollection and make sure it exists
        if file_collection is None:
            file_collection = props[cs.FT_FILE_COLLECTION]
            assert file_collection != ''
        assert file_collection is not None

        # FileCollection must be the same for every channel
        assert props[cs.FT_FILE_COLLECTION] == file_collection, props

        type, name, size, image = files[i]
        if size is None:
            size = 0

        assertEquals(size, props[cs.FT_SIZE])

        assert props[cs.CHANNEL_TYPE] == cs.CHANNEL_TYPE_FILE_TRANSFER, props
        assertSameSets(
            [ cs.CHANNEL_IFACE_FILE_TRANSFER_METADATA,
            ], props[cs.INTERFACES])
        assert props[cs.TARGET_HANDLE] == 2, props
        assert props[cs.TARGET_ID] == contact.replace("/Resource", ""), props
        assert props[cs.TARGET_HANDLE_TYPE] == cs.HT_CONTACT, props
        assert props[cs.REQUESTED] == False, props
        assert props[cs.INITIATOR_HANDLE] == 2, props
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

    event = q.expect('stream-iq', to=contact,
                     iq_type='set', query_name='session')
    session_node = event.query
    assert session_node.attributes['type'] == 'transport-accept'

    # Close all but one of the channels, and make sure Gabble doesn't cancel
    # the multi-FT yet.
    terminate_pattern = EventPattern('stream-iq', to=contact, iq_type='set',
        query_name='session',
        predicate=lambda event: event.query['type'] == 'terminate')

    q.forbid_events([terminate_pattern])

    for path, props in channels[:-1]:
        ft_chan = bus.get_object(conn.object.bus_name, path)
        channel = dbus.Interface(ft_chan, cs.CHANNEL)
        channel.Close()
        q.expect('dbus-signal', signal='Closed', path=path)

    sync_stream(q, stream)
    q.unforbid_all()

    # Now close the final channel, and make sure Gabble terminates the session.
    last_path, props = channels[-1]

    ft_chan = bus.get_object(conn.object.bus_name, last_path)
    channel = dbus.Interface(ft_chan, cs.CHANNEL)
    channel.Close()

    q.expect_many(terminate_pattern)

if __name__ == '__main__':
    exec_test(test)
