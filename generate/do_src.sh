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
cd src

echo Generating GabbleConnectionManager files ...
$PYTHON $TP/tools/gengobject.py ../xml-modified/gabble-connection-manager.xml GabbleConnectionManager

echo Generating GabbleConnection files ...
$PYTHON $TP/tools/gengobject.py ../xml-modified/gabble-connection.xml GabbleConnection

echo Generating GabbleIMChannel files ...
$PYTHON $TP/tools/gengobject.py ../xml-modified/gabble-im-channel.xml GabbleIMChannel

echo Generating GabbleMucChannel files ...
$PYTHON $TP/tools/gengobject.py ../xml-modified/gabble-muc-channel.xml GabbleMucChannel

echo Generating GabbleMediaChannel files ...
$PYTHON $TP/tools/gengobject.py ../xml-modified/gabble-media-channel.xml GabbleMediaChannel

echo Generating GabbleMediaSession files ...
$PYTHON $TP/tools/gengobject.py ../xml-modified/gabble-media-session.xml GabbleMediaSession

echo Generating GabbleMediaStream files ...
$PYTHON $TP/tools/gengobject.py ../xml-modified/gabble-media-stream.xml GabbleMediaStream

echo Generating GabbleRosterChannel files ...
$PYTHON $TP/tools/gengobject.py ../xml-modified/gabble-roster-channel.xml GabbleRosterChannel

echo Generating GabbleRoomlistChannel files ...
$PYTHON $TP/tools/gengobject.py ../xml-modified/gabble-roomlist-channel.xml GabbleRoomlistChannel

echo Generating error enums ...
$PYTHON $TP/tools/generrors.py
