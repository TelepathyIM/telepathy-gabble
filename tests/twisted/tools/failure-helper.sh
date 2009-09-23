#!/bin/sh
#
# Run a given test repeatedly until it fails at least once and passes at least
# once. Places logs from the test and from the service being tested in the
# given directory, and generates diffs between the fail and pass cases.

error()
{
    echo "$@" >&2
    exit 1
}

abspath()
{
    if echo "$1" | grep -q "^/"; then
        echo "$1"
    else
        echo "$PWD/$1"
    fi
}

stripdiff()
{
    a=`mktemp`
    b=`mktemp`
    python ../../tools/log-strip.py < "$1" > "$a"
    python ../../tools/log-strip.py < "$2" > "$b"
    diff -U40 "$a" "$b"
    rm "$a" "$b"
}

prog=gabble
test_name="$1"
log_dir="$2"

usage="usage: $0 test-name log-directory"
test -n "$test_name" || error "$usage"
test -n "$log_dir" || error "$usage"

cd `dirname $0`/..
test -f "servicetest.py" || error "can't find servicetest.py"
test -f "$test_name" || error "can't find that test"

if ! test -d "$log_dir"; then
    if ! test -e "$log_dir"; then
        mkdir "$log_dir"
    else
        error "not a directory: $log_dir"
    fi
fi

log_dir=`abspath "$log_dir"`
test_pass_log="$log_dir/test-pass.log"
test_fail_log="$log_dir/test-fail.log"
prog_pass_log="$log_dir/$prog-pass.log"
prog_fail_log="$log_dir/$prog-fail.log"

if test -e "$test_pass_log" -a -e "$prog_pass_log"; then
    echo "using existing pass"
    got_pass=true
else
    got_pass=false
fi

if test -e "$test_fail_log" -a -e "$prog_fail_log"; then
    echo "using existing fail"
    got_fail=true
else
    got_fail=false
fi

run=1

while test "$got_pass" != true -o "$got_fail" != true; do
    echo -n "run $run: "

    CHECK_TWISTED_VERBOSE=1 make check-twisted TWISTED_TESTS="$test_name" \
        > "$log_dir/test.log" 2>&1
    ret=$?

    if test $ret -eq 0; then
        echo "pass"
    else
        echo "fail"
    fi

    if test $ret -eq 0 -a "$got_pass" != true; then
        mv "$log_dir/test.log" "$test_pass_log"
        cp "tools/$prog-testing.log" "$prog_pass_log"
        got_pass=true
    elif test $ret -ne 0 -a "$got_fail" != true; then
        mv "$log_dir/test.log" "$test_fail_log"
        cp "tools/$prog-testing.log" "$prog_fail_log"
        got_fail=true
    else
        rm "$log_dir/test.log"
    fi

    run=`expr $run + 1`
done

stripdiff "$test_pass_log" "$test_fail_log" > "$log_dir/test-log.diff"
stripdiff "$prog_pass_log" "$prog_fail_log" > "$log_dir/$prog-log.diff"

echo done
