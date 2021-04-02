
from file_transfer_helper import  SendFileTest, ReceiveFileTest, \
    exec_file_transfer_test

from config import JINGLE_FILE_TRANSFER_ENABLED

if not JINGLE_FILE_TRANSFER_ENABLED:
    print("NOTE: built with --disable-file-transfer or --disable-voip")
    raise SystemExit(77)

print("FIXME: test is not working now and I have no idea how it was supposed to work.\n" +
      "    Needs porting to normal Jingle Session from google session.")
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

