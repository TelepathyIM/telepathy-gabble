from file_transfer_helper import exec_file_transfer_test, ReceiveFileTest

from config import FILE_TRANSFER_ENABLED

if not FILE_TRANSFER_ENABLED:
    print "NOTE: built with --disable-file-transfer"
    raise SystemExit(77)

if __name__ == '__main__':
    exec_file_transfer_test(ReceiveFileTest)
