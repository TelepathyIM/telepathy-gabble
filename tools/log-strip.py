#!/usr/bin/python
"""
Strip varying data (PIDs, pointer values) from Gabble logs to make them
easier to compare.
"""

from __future__ import with_statement

import re
import sys

def sanitise(line):
    return (
        re.sub('^(\*\* )?\(telepathy-gabble:\d+\)', '',
        re.sub('\x1b\[0m', '',
        re.sub('^RECV \[\d+\]', 'RECV [???]',
        re.sub('0x[0-9A-Fa-f]{5,8}', '0x???????',
        re.sub("('?<[^ ]+ [^>]*id=)[\"'][^\"']+[\"']",
            lambda m: m.group(1) + '"?????"', line))))))

def process(file):
    for line in file:
        print sanitise(line),

def main():
    if len(sys.argv) > 1:
        for fn in sys.argv[1:]:
            with open(fn) as f:
                process(f)
    else:
        process(sys.stdin)

if __name__ == '__main__':
    main()

