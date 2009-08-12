
"""
Strip varying data (PIDs, pointer values) from Gabble logs to make them
easier to compare.
"""

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

def main():
    for line in sys.stdin:
        print sanitise(line),

if __name__ == '__main__':
    main()

