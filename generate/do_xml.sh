#!/bin/sh

set -e

PYVER=2.4
PYTHON=python$PYVER

if [ `basename $PWD` == "generate" ]; then
  TP=${TELEPATHY_SPEC:=$PWD/../../telepathy-spec}
else
  TP=${TELEPATHY_SPEC:=$PWD/../telepathy-spec}
fi

export PYTHONPATH=$TP:$PYTHONPATH

if test -d generate; then cd generate; fi
cd xml-pristine

echo "Generating pristine XML in generate/xml-pristine..."
$PYTHON $TP/tools/genxml.py ../gabble.def
echo "Patching XML to incorporate async annotations..."
for x in *.xml; do
  $PYTHON ../async_annotate.py ../async_implementations $x ../xml-modified/$x
done
