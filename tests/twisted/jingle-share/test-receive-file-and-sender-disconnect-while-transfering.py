import dbus

import constants as cs
from file_transfer_helper import SendFileTest, ReceiveFileTest, \
    FileTransferTest, exec_file_transfer_test

class ReceiveFileAndSenderDisconnectWhileTransfering(ReceiveFileTest):
    def receive_file(self):
        self.q.expect('dbus-signal', signal='FileTransferStateChanged',
                      path = self.channel.__dbus_object_path__,
                      args=[cs.FT_STATE_CANCELLED, \
                                cs.FT_STATE_CHANGE_REASON_REMOTE_STOPPED])

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

                         self.request_ft_channel, self.provide_file, None,

                         self.send_file, self.wait_for_completion,
                         self.disconnect, None,

                         self.close_channel, self.done]


    def disconnect(self):
        self.conn.Disconnect()


if __name__ == '__main__':
    exec_file_transfer_test(SendFileAndDisconnect, \
                                ReceiveFileAndSenderDisconnectWhileTransfering)
