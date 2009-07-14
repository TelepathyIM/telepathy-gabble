
from file_transfer_helper import exec_file_transfer_test, ReceiveFileTest

class ReceiveFileAndDisconnectTest(ReceiveFileTest):
    def receive_file(self):
        s = self.create_socket()
        s.connect(self.address)

        # return True so the test will be ended and the connection
        # disconnected
        return True

if __name__ == '__main__':
    exec_file_transfer_test(ReceiveFileAndDisconnectTest)
