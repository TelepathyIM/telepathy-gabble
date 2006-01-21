#!/bin/sh

if [ `basename $PWD` == "generate" ]; then
  TP=${TELEPATHY_PYTHON:=$PWD/../../telepathy-python}
else
  TP=${TELEPATHY_PYTHON:=$PWD/../telepathy-python}
fi

export PYTHONPATH=$TP:$PYTHONPATH

test -d generate && cd generate
cd src

echo Generating GabbleConnectionManager files ...
$TP/tools/gengobject.py ../xml-modified/gabble-connection-manager.xml GabbleConnectionManager

echo Generating GabbleConnection files ...
$TP/tools/gengobject.py ../xml-modified/gabble-connection.xml GabbleConnection

echo Generating GabbleIMChannel files ...
$TP/tools/gengobject.py ../xml-modified/gabble-im-channel.xml GabbleIMChannel

echo Generating GabbleRosterChannel files ...
$TP/tools/gengobject.py ../xml-modified/gabble-roster-channel.xml GabbleRosterChannel

echo Generating error enums ...
$TP/tools/generrors.py
