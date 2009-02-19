from gabbletest import exec_test
from file_transfer_helper import ReceiveFileTest

if __name__ == '__main__':
    test = ReceiveFileTest()
    exec_test(test.test)
