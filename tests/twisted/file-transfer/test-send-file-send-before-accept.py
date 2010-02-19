import dbus
import constants as cs
from servicetest import EventPattern
from file_transfer_helper import SendFileTest, exec_file_transfer_test

from twisted.words.xish import domish
import ns

class SendFileSendBeforeAccept(SendFileTest):

    def client_accept_file_empty(self):
        return False

    client_accept_file_orig = SendFileTest.client_accept_file
    client_accept_file = client_accept_file_empty


    def send_file(self):
        s = self.create_socket()
        s.connect(self.address)
        s.send(self.file.data[self.file.offset:])

        # Accept the file now that we've sent our data...
        self.client_accept_file_orig()

        to_receive = self.file.size - self.file.offset
        self.count = 0

        def bytes_changed_cb(bytes):
            self.count = bytes

        self.ft_channel.connect_to_signal('TransferredBytesChanged', bytes_changed_cb)

        # FileTransferStateChanged can be fired while we are receiving data
        # (in the SOCKS5 case for example)
        self.completed = False
        def ft_state_changed_cb(state, reason):
            if state == cs.FT_STATE_COMPLETED:
                self.completed = True
        self.ft_channel.connect_to_signal('FileTransferStateChanged', ft_state_changed_cb)

        # get data from bytestream
        data = ''
        while len(data) < to_receive:
            data += self.bytestream.get_data()

        assert data == self.file.data[self.file.offset:]

        if self.completed:
            # FileTransferStateChanged has already been received
            waiting = []
        else:
            waiting = [EventPattern('dbus-signal', signal='FileTransferStateChanged')]

        events = self.bytestream.wait_bytestream_closed(waiting)

        # If not all the bytes transferred have been announced using
        # TransferredBytesChanged, wait for them
        while self.count < to_receive:
            self.q.expect('dbus-signal', signal='TransferredBytesChanged')

        assert self.count == to_receive

        if len(waiting) > 1:
            state, reason = events[0].args
            assert state == cs.FT_STATE_COMPLETED
            assert reason == cs.FT_STATE_CHANGE_REASON_NONE

if __name__ == '__main__':
    exec_file_transfer_test(SendFileSendBeforeAccept)
