
from file_transfer_helper import  SendFileTest, ReceiveFileTest, \
    exec_file_transfer_test

from config import FILE_TRANSFER_ENABLED

if not FILE_TRANSFER_ENABLED:
    print "NOTE: built with --disable-file-transfer"
    raise SystemExit(77)

class ReceiveFileAndDisconnectTest(ReceiveFileTest):
    def receive_file(self):
        s = self.create_socket()
        s.connect(self.address)

        # return True so the test will be ended and the connection
        # disconnected
        return True

if __name__ == '__main__':
    exec_file_transfer_test(SendFileTest, ReceiveFileAndDisconnectTest)

