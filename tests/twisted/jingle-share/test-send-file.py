# -*- coding: utf-8 -*-
from file_transfer_helper import SendFileTest, ReceiveFileTest, \
    exec_file_transfer_test, File

from config import JINGLE_FILE_TRANSFER_ENABLED

if not JINGLE_FILE_TRANSFER_ENABLED:
    print "NOTE: built with --disable-file-transfer or --disable-voip"
    raise SystemExit(77)

if __name__ == '__main__':
    file = File()
    file.offset = 5
    file.name = "The greek foo δοκιμή.txt"
    exec_file_transfer_test(SendFileTest, ReceiveFileTest, file)
