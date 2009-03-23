import socket

from file_transfer_helper import exec_file_transfer_test, ReceiveFileTest,\
    FT_STATE_CANCELLED, FT_STATE_CHANGE_REASON_LOCAL_ERROR

class ReceiveFileAndCancelWhileReceiving(ReceiveFileTest):
    def receive_file(self):
        # Connect to Salut's socket
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        s.connect(self.address)

        # for some reason the socket is closed
        s.close()

        # we receive one more byte from the sender
        self.bytestream.send_data(self.file.data[2:3])

        self.q.expect('dbus-signal', signal='FileTransferStateChanged',
            args=[FT_STATE_CANCELLED, FT_STATE_CHANGE_REASON_LOCAL_ERROR])

        self.channel.Close()
        self.q.expect('dbus-signal', signal='Closed')
        return True

if __name__ == '__main__':
    exec_file_transfer_test(ReceiveFileAndCancelWhileReceiving)
