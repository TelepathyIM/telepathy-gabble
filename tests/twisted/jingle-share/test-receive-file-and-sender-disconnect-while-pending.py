import dbus

import constants as cs
from file_transfer_helper import SendFileTest, ReceiveFileTest, \
    FileTransferTest, exec_file_transfer_test

from config import JINGLE_FILE_TRANSFER_ENABLED

if not JINGLE_FILE_TRANSFER_ENABLED:
    print "NOTE: built with --disable-file-transfer or --disable-voip"
    raise SystemExit(77)

class ReceiveFileAndSenderDisconnectWhilePendingTest(ReceiveFileTest):
    def accept_file(self):

        e = self.q.expect('dbus-signal', signal='FileTransferStateChanged',
                          path = self.channel.object_path,
                          args=[cs.FT_STATE_CANCELLED, \
                                    cs.FT_STATE_CHANGE_REASON_REMOTE_STOPPED])

        # We can't accept the transfer now
        try:
            self.ft_channel.AcceptFile(cs.SOCKET_ADDRESS_TYPE_UNIX,
                cs.SOCKET_ACCESS_CONTROL_LOCALHOST, "", 0)
        except dbus.DBusException, e:
            assert e.get_dbus_name() == cs.NOT_AVAILABLE
        else:
            assert False

        self.close_channel()

        # stop the test
        return True


class SendFileAndDisconnect (SendFileTest):
    def __init__(self, file, address_type,
                 access_control, acces_control_param):
        FileTransferTest.__init__(self, file, address_type,
                                  access_control, acces_control_param)

        self._actions = [self.connect, self.set_ft_caps,
                         self.check_ft_available, None,

                         self.wait_for_ft_caps, None,

                         self.request_ft_channel, self.provide_file,
                         self.disconnect, None,

                         self.close_channel, self.done]

    def disconnect(self):
        self.conn.Disconnect()


if __name__ == '__main__':
    exec_file_transfer_test(SendFileAndDisconnect, \
                                ReceiveFileAndSenderDisconnectWhilePendingTest)
