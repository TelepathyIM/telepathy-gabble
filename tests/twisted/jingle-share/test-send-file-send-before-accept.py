from file_transfer_helper import SendFileTest, FileTransferTest, \
    ReceiveFileTest, exec_file_transfer_test

from config import JINGLE_FILE_TRANSFER_ENABLED

if not JINGLE_FILE_TRANSFER_ENABLED:
    print "NOTE: built with --disable-file-transfer or --disable-voip"
    raise SystemExit(77)

class SendFileBeforeAccept(SendFileTest):
    def __init__(self, file, address_type,
                 access_control, acces_control_param):
        FileTransferTest.__init__(self, file, address_type,
                                  access_control, acces_control_param)

        self._actions = [self.connect, self.set_ft_caps,
                         self.check_ft_available, None,

                         self.wait_for_ft_caps, None,

                         self.request_ft_channel, self.provide_file,
                         self.set_open, self.send_file, None,

                         self.wait_for_completion, None,

                         self.close_channel, self.done]

    def set_open(self):
        self.open = True
        self.offset_defined = True

if __name__ == '__main__':
    exec_file_transfer_test(SendFileBeforeAccept, ReceiveFileTest)
