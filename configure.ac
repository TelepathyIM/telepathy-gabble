AC_PREREQ([2.59])

# Making releases:
#   set the new version number:
#     odd minor -> development series
#     even minor -> stable series
#     increment micro for each release within a series
#   set gabble_nano_version to 0.

m4_define([gabble_major_version], [0])
m4_define([gabble_minor_version], [7])
m4_define([gabble_micro_version], [23])
m4_define([gabble_nano_version], [1])

# Some magic
m4_define([gabble_base_version],
          [gabble_major_version.gabble_minor_version.gabble_micro_version])
m4_define([gabble_version],
          [m4_if(gabble_nano_version, 0, [gabble_base_version],
              [gabble_base_version].[gabble_nano_version])])

AC_INIT([Telepathy Gabble], [gabble_version],
        [https://bugs.freedesktop.org/enter_bug.cgi?product=Telepathy])

AC_CONFIG_MACRO_DIR([m4])

AM_INIT_AUTOMAKE([1.9 -Wno-portability])
AM_CONFIG_HEADER(config.h)

AC_USE_SYSTEM_EXTENSIONS

dnl check for tools
AC_PROG_CC
AC_PROG_CC_STDC
AC_PROG_INSTALL
AC_PROG_LIBTOOL

COMPILER_OPTIMISATIONS
COMPILER_COVERAGE

dnl decide error flags
AS_COMPILER_FLAG(-Wall, ERROR_CFLAGS="-Wall", ERROR_CFLAGS="")
AS_COMPILER_FLAG(-Werror, werror=yes, werror=no)

AC_ARG_ENABLE(Werror,
  AC_HELP_STRING([--disable-Werror],[compile without -Werror (normally enabled in development builds)]),
    werror=$enableval, :)

AS_COMPILER_FLAG(-Wextra, wextra=yes, wextra=no)
AS_COMPILER_FLAG(-Wno-missing-field-initializers,
        wno_missing_field_initializers=yes,
        wno_missing_field_initializers=no)
AS_COMPILER_FLAG(-Wno-unused-parameter,
        wno_unused_parameter=yes,
        wno_unused_parameter=no)

ifelse(gabble_nano_version, 0, [],
    [
        if test x$werror = xyes; then
            ERROR_CFLAGS="$ERROR_CFLAGS -Werror"
        fi
        if test x$wextra = xyes -a \
            x$wno_missing_field_initializers = xyes -a \
            x$wno_unused_parameter = xyes; then
            ERROR_CFLAGS="$ERROR_CFLAGS -Wextra -Wno-missing-field-initializers -Wno-unused-parameter"
        fi
    ])

AS_COMPILER_FLAG(-Wdeclaration-after-statement, ERROR_CFLAGS="$ERROR_CFLAGS -Wdeclaration-after-statement")
AS_COMPILER_FLAG(-Wshadow, ERROR_CFLAGS="$ERROR_CFLAGS -Wshadow")
AS_COMPILER_FLAG(-Wstrict-prototypes, ERROR_CFLAGS="$ERROR_CFLAGS -Wstrict-prototypes")
AS_COMPILER_FLAG(-Wmissing-prototypes, ERROR_CFLAGS="$ERROR_CFLAGS -Wmissing-prototypes")
AS_COMPILER_FLAG(-Wmissing-declarations, ERROR_CFLAGS="$ERROR_CFLAGS -Wmissing-declarations")

AC_SUBST(ERROR_CFLAGS)

AC_ARG_ENABLE(debug,
  AC_HELP_STRING([--disable-debug],[compile without debug code]),
    enable_debug=$enableval, enable_debug=yes )

AC_ARG_ENABLE(handle-leak-debug,
  AC_HELP_STRING([--enable-handle-leak-debug],[compile with -rdynamic so telepathy-glib handle leak debugging code will work]),
    enable_handle_leak_debug=$enableval, enable_handle_leak_debug=no )

ifelse(gabble_nano_version, 0,
    [ # Gabble is version x.y.z - disable coding style checks by default
AC_ARG_ENABLE(coding-style-checks,
  AC_HELP_STRING([--enable-coding-style-checks],
                 [check coding style using grep]),
    [ENABLE_CODING_STYLE_CHECKS=$enableval], [ENABLE_CODING_STYLE_CHECKS=no] )
    ],
    [ # Gabble is version x.y.z.1 - enable coding style checks by default
AC_ARG_ENABLE(coding-style-checks,
  AC_HELP_STRING([--disable-coding-style-checks],
                 [don't check coding style using grep]),
    [ENABLE_CODING_STYLE_CHECKS=$enableval], [ENABLE_CODING_STYLE_CHECKS=yes])
    ])

if test x$enable_debug = xyes; then
  AC_DEFINE(ENABLE_DEBUG, [], [Enable debug code])
fi

if test x$enable_handle_leak_debug = xyes; then
  AC_DEFINE(ENABLE_HANDLE_LEAK_DEBUG, [], [Enable handle leak debug code])
  HANDLE_LEAK_DEBUG_CFLAGS="-rdynamic"
fi

AC_SUBST(HANDLE_LEAK_DEBUG_CFLAGS)

AC_SUBST([ENABLE_CODING_STYLE_CHECKS])

dnl Check for Glib
PKG_CHECK_MODULES(GLIB, [glib-2.0 >= 2.16, gobject-2.0 >= 2.16])

AC_SUBST(GLIB_CFLAGS)
AC_SUBST(GLIB_LIBS)

GLIB_GENMARSHAL=`$PKG_CONFIG --variable=glib_genmarshal glib-2.0`
AC_SUBST(GLIB_GENMARSHAL)

dnl Check for D-Bus
# dbus-glib 0.78 fixed the bug #17329. It is needed for the contact
# capabilities interface.
PKG_CHECK_MODULES(DBUS, [dbus-1 >= 1.1.0, dbus-glib-1 >= 0.78])

AC_SUBST(DBUS_CFLAGS)
AC_SUBST(DBUS_LIBS)

PKG_CHECK_MODULES(TP_GLIB, [telepathy-glib >= 0.7.26])

AC_SUBST(TP_GLIB_CFLAGS)
AC_SUBST(TP_GLIB_LIBS)


# Needed for hostname resolver
AC_CHECK_FUNC(res_query, ,
  [AC_CHECK_LIB(resolv, res_query, [ RESOLV_LIBS="-lresolv" ],
    [ save_libs="$LIBS"
      LIBS="-lresolv $LIBS"
      AC_MSG_CHECKING([for res_query in -lresolv (alternate version)])
      AC_LINK_IFELSE(
        [AC_LANG_PROGRAM([[#include <resolv.h>]], [[res_query(0,0,0,0,0)]])],
        [ AC_MSG_RESULT(yes)
          LIBS="$save_libs"
          RESOLV_LIBS="-lresolv"
        ],
        [ AC_MSG_RESULT(no)
          LIBS="$save_libs"
          AC_CHECK_LIB(bind, res_query,
            [ RESOLV_LIBS="-lbind" ],
            [ AC_MSG_ERROR(res_query not found) ] ) ] ) ] )
  ]
)

AC_SUBST(RESOLV_LIBS)

dnl Check for code generation tools
XSLTPROC=
AC_CHECK_PROGS([XSLTPROC], [xsltproc])
if test -z "$XSLTPROC"; then
  AC_MSG_ERROR([xsltproc (from the libxslt source package) is required])
fi
# This is for the code generation, not for the tests - any version of Python
# >= 2.3 with the standard library will do. We prefer older versions,
# to ensure that when we claim that 2.3 is sufficient we are telling the truth.
PYTHON=
AC_CHECK_PROGS([PYTHON], [python2.3 python2.4 python2.5 python])
if test -z "$PYTHON"; then
  AC_MSG_ERROR([Python is required to compile this package])
fi

# Check for a Python >= 2.5 with Twisted, to run the tests
AC_MSG_CHECKING([for Python >= 2.5 with Twisted and XMPP protocol support])
for TEST_PYTHON in python2.5 python2.6 python; do
  if $TEST_PYTHON -c "from sys import version_info; import dbus, dbus.mainloop.glib; raise SystemExit(version_info < (2, 5, 0, 'final', 0))" >/dev/null 2>&1; then
    if $TEST_PYTHON -c "import twisted.words.xish.domish, twisted.words.protocols.jabber, twisted.internet.reactor" >/dev/null 2>&1; then
      AC_MSG_RESULT([$TEST_PYTHON])
      AM_CONDITIONAL([WANT_TWISTED_TESTS], true)
      break
    else
      TEST_PYTHON=false
    fi
  fi
done
AC_SUBST(TEST_PYTHON)
AM_CONDITIONAL([WANT_TWISTED_TESTS], test false != "$TEST_PYTHON")

dnl Check for Loudmouth
PKG_CHECK_MODULES(LOUDMOUTH, loudmouth-1.0 >= 1.3.2)
AC_SUBST(LOUDMOUTH_CFLAGS)
AC_SUBST(LOUDMOUTH_LIBS)

dnl Check for libsoup
PKG_CHECK_MODULES(SOUP, libsoup-2.4)
AC_SUBST(SOUP_CFLAGS)
AC_SUBST(SOUP_LIBS)

PKG_CHECK_MODULES([UUID], [uuid], [HAVE_UUID=yes], [HAVE_UUID=no])
AC_SUBST([UUID_CFLAGS])
AC_SUBST([UUID_LIBS])
if test x"$HAVE_UUID" = xyes; then
  AC_DEFINE([HAVE_UUID], [1], [Define if libuuid is available])
else
  AC_MSG_WARN([libuuid not found, falling back to generating random IDs])
fi

AC_CHECK_FUNCS(getifaddrs memset select strndup setresuid setreuid strerror)

AC_OUTPUT( Makefile \
           docs/Makefile \
           extensions/Makefile \
           src/Makefile \
           m4/Makefile \
           data/Makefile \
           tests/Makefile \
           tools/Makefile \
           tests/twisted/Makefile \
           tests/twisted/tools/Makefile \
           lib/Makefile \
           lib/gibber/Makefile
)
