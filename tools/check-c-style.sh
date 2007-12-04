#!/bin/sh
fail=0

( . "${tools_dir}"/check-misc.sh ) || fail=$?

if grep -n '^ *GError *\*[[:alpha:]_][[:alnum:]_]* *;' "$@"
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
if grep -n '^[^"]*[[:lower:]](' "$@" \
  | grep -v '^[-[:alnum:]_./]*:[[:digit:]]*: *\*' \
  | grep -v '^[-[:alnum:]_./]*:[[:digit:]]*: */\*' \
  | grep -v '^[-[:alnum:]_./]*:[[:digit:]]*: *#'
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
