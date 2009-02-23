import socket

from gabbletest import exec_test
from file_transfer_helper import ReceiveFileTest, BytestreamIBB, BytestreamS5B

class ReceiveFileAndDisconnectTest(ReceiveFileTest):
    def receive_file(self):
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        s.connect(self.address)

        # disconnect
        self.conn.Disconnect()
        self.q.expect('dbus-signal', signal='StatusChanged', args=[2, 1])
        return True

if __name__ == '__main__':
    test = ReceiveFileAndDisconnectTest(BytestreamIBB)
    test = ReceiveFileAndDisconnectTest(BytestreamS5B)
    exec_test(test.test)
