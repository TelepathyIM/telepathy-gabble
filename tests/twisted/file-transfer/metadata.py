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

class ReceiveFileNoMetadata(ReceiveFileTest):
    service_name = ''
    metadata = {}

class SendFileOddMetadata(SendFileTest):
    service_name = ''
    metadata = {'loldongs': []}

class ReceiveFileOddMetadata(ReceiveFileTest):
    service_name = ''
    metadata = {'loldongs': []}

class SendFileBadProps(SendFileTest):
    metadata = {'FORM_TYPE': ['this shouldnt be allowed']}

    def request_ft_channel(self):
        request = { cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_FILE_TRANSFER,
            cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
            cs.TARGET_HANDLE: self.handle,
            cs.FT_CONTENT_TYPE: self.file.content_type,
            cs.FT_FILENAME: self.file.name,
            cs.FT_SIZE: self.file.size,
            cs.FT_CONTENT_HASH_TYPE: self.file.hash_type,
            cs.FT_CONTENT_HASH: self.file.hash,
            cs.FT_DESCRIPTION: self.file.description,
            cs.FT_DATE:  self.file.date,
            cs.FT_INITIAL_OFFSET: 0,
            cs.FT_SERVICE_NAME: self.service_name,
            cs.FT_METADATA: dbus.Dictionary(self.metadata, signature='sas')}

        call_async(self.q, self.conn.Requests, 'CreateChannel', request)

        # FORM_TYPE is not allowed, soz
        self.q.expect('dbus-error', method='CreateChannel', name=cs.INVALID_ARGUMENT)

        return True

class SendFileBadContact(SendFileTest):
    def announce_contact(self):
        SendFileTest.announce_contact(self, metadata=False)

    def request_ft_channel(self):
        request = { cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_FILE_TRANSFER,
            cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
            cs.TARGET_HANDLE: self.handle,
            cs.FT_CONTENT_TYPE: self.file.content_type,
            cs.FT_FILENAME: self.file.name,
            cs.FT_SIZE: self.file.size,
            cs.FT_CONTENT_HASH_TYPE: self.file.hash_type,
            cs.FT_CONTENT_HASH: self.file.hash,
            cs.FT_DESCRIPTION: self.file.description,
            cs.FT_DATE:  self.file.date,
            cs.FT_INITIAL_OFFSET: 0,
            cs.FT_SERVICE_NAME: self.service_name,
            cs.FT_METADATA: dbus.Dictionary(self.metadata, signature='sas')}

        call_async(self.q, self.conn.Requests, 'CreateChannel', request)

        # no support for metadata, soz
        self.q.expect('dbus-error', method='CreateChannel', name=cs.NOT_CAPABLE)

        return True

if __name__ == '__main__':
    exec_file_transfer_test(SendFileNoMetadata, True)
    exec_file_transfer_test(ReceiveFileNoMetadata, True)
    exec_file_transfer_test(SendFileOddMetadata, True)
    exec_file_transfer_test(ReceiveFileOddMetadata, True)
    exec_file_transfer_test(SendFileBadProps, True)
    exec_file_transfer_test(SendFileBadContact, True)
