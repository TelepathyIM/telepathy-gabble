
"""
Helper script for writing tests.

How it works:

 - Each event that occurs generates a Python function that expects exactly
   that event.
 - Each time an event occurs, you're dropped into a Python debugger. The
   commands you type will be inserted into the function that expects that
   event.
 - Executing "!return False" in the debugger will stop the script, as will
   SIGINT (Control-C).

Once your'e done, the test code is written to a file called test.dump.
"""

import pdb
import pprint

from twisted.internet import glib2reactor
glib2reactor.install()

from gabbletest import EventTest, run

class LoggingPdb(pdb.Pdb):
    def __init__(self):
        pdb.Pdb.__init__(self)
        self.log = []

    def default(self, line):
        self.log.append(line)
        return pdb.Pdb.default(self, line)

def expect(event, data):
    return True

class TestShell:
    def __init__(self):
        self.events = []

    def expect(self, event, data):
        print 'event:'
        for item in event:
            print '-', pprint.pformat(item)
        db = LoggingPdb()
        ret = db.runcall(expect, event, data)
        print (ret, db.log)
        self.events.append((event, db.log, ret))
        return False

    def make_expect_func(self, suffix, event, log, ret):
        lines = []
        lines.append('def expect_%s(event, data):\n' % suffix)
        lines.extend([
            '    if event[%d] != %r:\n        return False\n\n' % (i, item)
            for i, item in enumerate(event)])
        lines.extend(['    %s\n' % line for line in log])
        lines.append('    return %r\n' % ret)
        return ''.join(lines)

    def make_script(self):
        return '\n'.join([
            self.make_expect_func(i, event, log, ret)
            for i, (event, log, ret) in enumerate(self.events)])

class NoTimeoutEventTest(EventTest):
    def timeout_cb(self):
        pass

if __name__ == '__main__':
    test = NoTimeoutEventTest()
    shell = TestShell()
    test.expect(shell.expect)

    try:
        run(test)
    except KeyboardInterrupt:
        pass

    print 'writing test to test.dump'
    fh = file('test.dump', 'w')
    fh.write(shell.make_script())

