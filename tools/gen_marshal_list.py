#!/bin/env python

import sys
import re

def gen_list(pre, src, out):
  proto = set()
  rex = re.compile('.*'+pre+'([A-Z0-9]*__[A-Z0-9_]*).*')

  for fn in src:
    f = open(fn)
    for line in f:
      for m in rex.finditer(line):
        proto.add(m.group(1))
    f.close()

  with open(out,'w') as f:
    for call in proto:
      f.write(call.replace('__',':').replace('_',',') + "\n")

if __name__ == '__main__':
  pre = sys.argv[1]
  src = sys.argv[2:-1]
  out = sys.argv[-1]

  gen_list(pre, src, out)

