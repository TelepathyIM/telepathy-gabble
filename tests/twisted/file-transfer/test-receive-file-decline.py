from servicetest import EventPattern
from twisted.words.xish import xpath

import constants as cs
from file_transfer_helper import ReceiveFileTest, exec_file_transfer_test

from config import FILE_TRANSFER_ENABLED

if not FILE_TRANSFER_ENABLED:
    print "NOTE: built with --disable-file-transfer"
    raise SystemExit(77)

class ReceiveFileDeclineTest(ReceiveFileTest):
    def accept_file(self):
        # decline FT
        self.channel.Close()

        state_event, iq_event, _ = self.q.expect_many(
            EventPattern('dbus-signal', signal='FileTransferStateChanged'),
            EventPattern('stream-iq', iq_type='error'),
            EventPattern('dbus-signal', signal='Closed'),
            )

        error_node = xpath.queryForNodes('/iq/error', iq_event.stanza)[0]
        assert error_node['code'] == '403'

        state, reason = state_event.args
        assert state == cs.FT_STATE_CANCELLED
        assert reason == cs.FT_STATE_CHANGE_REASON_LOCAL_STOPPED

        # stop test
        return True

if __name__ == '__main__':
    exec_file_transfer_test(ReceiveFileDeclineTest)
