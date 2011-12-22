import constants as cs
from file_transfer_helper import SendFileTest, exec_file_transfer_test

from config import FILE_TRANSFER_ENABLED

if not FILE_TRANSFER_ENABLED:
    print "NOTE: built with --disable-file-transfer"
    raise SystemExit(77)

class SendFileAndCancelImmediatelyTest(SendFileTest):
    def provide_file(self):
        SendFileTest.provide_file(self)

        # cancel the transfer before the receiver accepts it
        self.channel.Close()

        e = self.q.expect('dbus-signal', signal='FileTransferStateChanged')
        state, reason = e.args
        assert state == cs.FT_STATE_CANCELLED
        assert reason == cs.FT_STATE_CHANGE_REASON_LOCAL_STOPPED

        self.q.expect('dbus-signal', signal='Closed')

        # XEP-0096 doesn't have a way to inform receiver we cancelled the
        # transfer...
        return True

if __name__ == '__main__':
    exec_file_transfer_test(SendFileAndCancelImmediatelyTest)
