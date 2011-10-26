# The 'normal' cases are tested with test-receive-file.py and test-send-file-provide-immediately.py
# This file tests some corner cases
import dbus

from file_transfer_helper import exec_file_transfer_test, ReceiveFileTest, SendFileTest
from servicetest import assertEquals, call_async

import constants as cs

class SendFileNoMetadata(SendFileTest):
    # this is basically the equivalent of calling CreateChannel
    # without these two properties
    service_name = ''
    metadata = {}

class ReceiveFileNoMetadata(SendFileTest):
    service_name = ''
    metadata = {}

if __name__ == '__main__':
    exec_file_transfer_test(SendFileNoMetadata, True)
    exec_file_transfer_test(ReceiveFileNoMetadata, True)
