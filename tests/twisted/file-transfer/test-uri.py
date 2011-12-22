# The 'normal' cases are tested with test-receive-file.py and test-send-file-provide-immediately.py
# This file tests some corner cases
import dbus

from file_transfer_helper import exec_file_transfer_test, ReceiveFileTest, SendFileTest
from servicetest import assertEquals, call_async

import constants as cs

from config import FILE_TRANSFER_ENABLED

if not FILE_TRANSFER_ENABLED:
    print "NOTE: built with --disable-file-transfer"
    raise SystemExit(77)

def assertNoURI(ft_channel):
    ft_props = dbus.Interface(ft_channel, cs.PROPERTIES_IFACE)

    uri = ft_props.Get(cs.CHANNEL_TYPE_FILE_TRANSFER, 'URI')
    assertEquals('', uri)

class SetURIAfterAccepting(ReceiveFileTest):
    def __init__(self, bytestream_cls, file, address_type, access_control, access_control_param):
        ReceiveFileTest.__init__(self, bytestream_cls, file, address_type, access_control, access_control_param)

        self._actions = [self.connect, self.announce_contact,
            self.send_ft_offer_iq, self.check_new_channel, self.create_ft_channel,
            self.accept_file, self.set_uri, self.done]

    def set_uri(self):
        ft_props = dbus.Interface(self.ft_channel, cs.PROPERTIES_IFACE)

        # URI is not set yet
        assertNoURI(self.ft_channel)

        # Setting URI
        call_async(self.q, ft_props, 'Set',
            cs.CHANNEL_TYPE_FILE_TRANSFER, 'URI', self.file.uri)

        # too late...
        self.q.expect('dbus-error', method='Set', name=cs.INVALID_ARGUMENT)


class ReceiveFileTestNoURI(ReceiveFileTest):
    def __init__(self, bytestream_cls, file, address_type, access_control, access_control_param):
        ReceiveFileTest.__init__(self, bytestream_cls, file, address_type, access_control, access_control_param)

        self._actions = [self.connect, self.announce_contact,
            self.send_ft_offer_iq, self.check_new_channel, self.create_ft_channel,
            self.accept_file, self.receive_file, self.close_channel, self.done]

    def accept_file(self):
        # URI is not set
        assertNoURI(self.ft_channel)

        ReceiveFileTest.accept_file(self)

    def close_channel(self):
        # Still no URI
        assertNoURI(self.ft_channel)

        ReceiveFileTest.close_channel(self)

class SendFileNoURI(SendFileTest):
    def request_ft_channel(self):
        SendFileTest.request_ft_channel(self, False)

    def close_channel(self):
        # Still no URI
        assertNoURI(self.ft_channel)

        SendFileTest.close_channel(self)

if __name__ == '__main__':
    # We don't define an URI before accepting the file and try to set it after
    exec_file_transfer_test(SetURIAfterAccepting, True)

    # Don't define any URI when receiving a file
    exec_file_transfer_test(ReceiveFileTestNoURI, True)

    # Don't define any URI when sending a file
    exec_file_transfer_test(SendFileNoURI, True)
