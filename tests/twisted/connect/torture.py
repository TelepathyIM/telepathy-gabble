"""
This test does nothing besides connect, and then disconnect as soon as the
session is established, two thousand times. It was used to smoke out a bug
where connections were leaked (which ultimately meant that new connections
could not be established, since the file descriptors were leaked too); it may
also be useful for profiling the connection process (and test framework).
"""
from gabbletest import exec_test

def test(q, bus, conn, stream):
    pass

def main():
    for i in xrange(0, 2000):
        print i
        exec_test(test)
    print "we partied like it's %i" % i

if __name__ == '__main__':
    main()
