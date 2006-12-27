#! /bin/sh

SCRIPTNAME=$0
WRAPPED_SCRIPT=$1
shift

die() 
{
    if ! test -z "$DBUS_SESSION_BUS_PID" ; then
        echo "killing message bus "$DBUS_SESSION_BUS_PID >&2
        kill -9 $DBUS_SESSION_BUS_PID
    fi
    echo $SCRIPTNAME: $* >&2
    exit 1
}

## convenient to be able to ctrl+C without leaking the message bus process
trap 'die "Received SIGINT"' INT

CONFIG_FILE=./run-with-tmp-session-bus.conf

unset DBUS_SESSION_BUS_ADDRESS
unset DBUS_SESSION_BUS_PID

echo "Running dbus-launch --sh-syntax --config-file=$CONFIG_FILE" >&2

eval `dbus-launch --sh-syntax --config-file=$CONFIG_FILE`

if test -z "$DBUS_SESSION_BUS_PID" ; then
    die "Failed to launch message bus for test script to run"
fi

echo "Started bus pid $DBUS_SESSION_BUS_PID at $DBUS_SESSION_BUS_ADDRESS" >&2

# Execute wrapped script
echo "Running $WRAPPED_SCRIPT $@" >&2
$WRAPPED_SCRIPT "$@" || die "script \"$WRAPPED_SCRIPT\" failed"

kill -TERM $DBUS_SESSION_BUS_PID || die "Message bus vanished! should not have happened" && echo "Killed daemon $DBUS_SESSION_BUS_PID" >&2

sleep 0.1

## be sure it really died 
kill -9 $DBUS_SESSION_BUS_PID > /dev/null 2>&1 || true

exit 0
