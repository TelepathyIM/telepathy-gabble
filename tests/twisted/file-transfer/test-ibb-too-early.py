from gabbletest import exec_test

import bytestream
from file_transfer_helper import ReceiveFileTest, File
from servicetest import EventPattern

import constants as cs

class IbbTooEarlyTest (ReceiveFileTest):
    def __init__ (self):
        ReceiveFileTest.__init__ (self,
            bytestream.BytestreamIBBMsg,
            File (),
            cs.SOCKET_ADDRESS_TYPE_UNIX,
            cs.SOCKET_ACCESS_CONTROL_LOCALHOST,
            "")

    def accept_file (self):
        # Instead of us accepting the other side starts sending the iq open
        # skip the open step explicitely
        self.bytestream.checked = True
        event = self.bytestream.open_bytestream(
            expected_after = [ EventPattern ('stream-iq', iq_type = 'error') ] )
        return True



if __name__ == '__main__':
    exec_test (IbbTooEarlyTest().test)
