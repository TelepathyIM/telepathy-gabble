from servicetest import EventPattern
from twisted.words.xish import xpath

import constants as cs
from file_transfer_helper import ReceiveFileTest, exec_file_transfer_test

class ReceiveFileDeclineTest(ReceiveFileTest):
    def accept_file(self):
        # decline FT
        self.channel.Close()

        state_event, iq_event = self.q.expect_many(
            EventPattern('dbus-signal', signal='FileTransferStateChanged'),
            EventPattern('stream-iq', iq_type='error'))

        error_node = xpath.queryForNodes('/iq/error', iq_event.stanza)[0]
        assert error_node['code'] == '403'

        state, reason = state_event.args
        assert state == cs.FT_STATE_CANCELLED
        assert reason == cs.FT_STATE_CHANGE_REASON_LOCAL_STOPPED
        self.q.expect('dbus-signal', signal='Closed')

        # stop test
        return True

if __name__ == '__main__':
    exec_file_transfer_test(ReceiveFileDeclineTest)
