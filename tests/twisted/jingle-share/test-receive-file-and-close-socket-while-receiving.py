
import constants as cs
from file_transfer_helper import  SendFileTest, ReceiveFileTest, \
    exec_file_transfer_test

from config import JINGLE_FILE_TRANSFER_ENABLED

if not JINGLE_FILE_TRANSFER_ENABLED:
    print "NOTE: built with --disable-file-transfer or --disable-voip"
    raise SystemExit(77)

class ReceiveFileAndCancelWhileReceiving(ReceiveFileTest):
    def receive_file(self):
        # Connect to Gabble's socket
        s = self.create_socket()
        s.connect(self.address)

        # for some reason the socket is closed
        s.close()

        self.q.expect('dbus-signal', signal='FileTransferStateChanged',
            args=[cs.FT_STATE_CANCELLED, cs.FT_STATE_CHANGE_REASON_LOCAL_ERROR])

if __name__ == '__main__':
    exec_file_transfer_test(SendFileTest, ReceiveFileAndCancelWhileReceiving)
