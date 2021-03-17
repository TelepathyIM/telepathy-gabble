import dbus
import constants as cs
from file_transfer_helper import SendFileTest, exec_file_transfer_test

from twisted.words.xish import domish
import ns

from config import FILE_TRANSFER_ENABLED

if not FILE_TRANSFER_ENABLED:
    print("NOTE: built with --disable-file-transfer")
    raise SystemExit(77)

class SendFileDeclinedTest(SendFileTest):
    def got_send_iq(self):
        SendFileTest.got_send_iq(self)

        # Receiver declines the file offer
        reply = domish.Element(('jabber:client', 'iq'))
        reply['to'] = 'test@localhost/Resource'
        reply['from'] = self.iq['to']
        reply['type'] = 'error'
        reply['id'] = self.iq['id']
        error = reply.addElement((None, 'error'))
        error['code'] = '403'
        error['type'] = 'cancel'
        error.addElement((ns.STANZA, 'forbidden'))
        error.addElement((ns.STANZA, 'text'), content='Offer Declined')
        self.stream.send(reply)

        e = self.q.expect('dbus-signal', signal='FileTransferStateChanged')
        state, reason = e.args
        assert state == cs.FT_STATE_CANCELLED, state
        assert reason == cs.FT_STATE_CHANGE_REASON_REMOTE_STOPPED

        transferred = self.ft_props.Get(cs.CHANNEL_TYPE_FILE_TRANSFER, 'TransferredBytes')
        # no byte has been transferred as the file was declined
        assert transferred == 0

        # try to provide the file, assert that this finishes the test (e.g.
        # couldn't go further because of ipv6) or that it raises
        # cs.NOT_AVAILABLE
        try:
            assert self.provide_file()
        except dbus.DBusException as e:
            assert e.get_dbus_name() == cs.NOT_AVAILABLE

        # stop test
        return True

if __name__ == '__main__':
    exec_file_transfer_test(SendFileDeclinedTest)
