#!/bin/sh
fail=0

/bin/sh "${top_srcdir}"/check-whitespace.sh "$@" || fail=$?

if grep -n '^ *GError *\*[a-zA-Z_][a-zA-Z0-9_]* *;' "$@"
then
  echo "^^^ The above files contain uninitialized GError*s - they should be"
  echo "    initialized to NULL"
  fail=1
fi

# The first regex finds function calls like foo() (as opposed to foo ()).
#   It attempts to ignore string constants (may cause false negatives).
# The second and third ignore block comments (gtkdoc uses foo() as markup).
# The fourth ignores cpp so you can
#   #define foo(bar) (_real_foo (__FUNC__, bar)) (cpp insists on foo() style).
if grep -n '^[^"]*[a-z](' "$@" \
  | grep -v '^[-a-zA-Z0-9_./]*:[0-9]*:  *\*' \
  | grep -v '^[-a-zA-Z0-9_./]*:[0-9]*:  */\*' \
  | grep -v '^[-a-zA-Z0-9_./]*:[0-9]*: *#'
then
  echo "^^^ Our coding style is to use function calls like foo (), not foo()"
  fail=1
fi

if test -n "$CHECK_FOR_LONG_LINES"
then
  if egrep -n '.{80,}' "$@"
  then
    echo "^^^ The above files contain long lines"
    fail=1
  fi
fi

exit $fail
