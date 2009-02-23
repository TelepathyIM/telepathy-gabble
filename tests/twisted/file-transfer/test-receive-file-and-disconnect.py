import socket

from file_transfer_helper import exec_file_transfer_test, ReceiveFileTest,\
    BytestreamIBB, BytestreamS5B

class ReceiveFileAndDisconnectTest(ReceiveFileTest):
    def receive_file(self):
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        s.connect(self.address)

        # disconnect
        self.conn.Disconnect()
        self.q.expect('dbus-signal', signal='StatusChanged', args=[2, 1])
        return True

if __name__ == '__main__':
    exec_file_transfer_test(ReceiveFileAndDisconnectTest)
