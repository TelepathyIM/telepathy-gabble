AC_PREREQ([2.59])

AC_INIT
AC_CONFIG_MACRO_DIR([m4])

AS_VERSION(telepathy-gabble, TELEPATHY_GABBLE_VERSION, 0, 1, 41, 0, WERROR="no", WERROR="yes")

AM_INIT_AUTOMAKE($PACKAGE, $VERSION)

AM_PROG_LIBTOOL
AM_CONFIG_HEADER(config.h)

dnl check for tools
AC_PROG_CC
AC_PROG_CC_STDC
AM_PROG_AS

dnl decide on error flags
AS_COMPILER_FLAG(-Wall, WALL="yes", WALL="no")

if test "x$WALL" = "xyes"; then
  ERROR_CFLAGS="-Wall"

  if test "x$WERROR" = "xyes"; then
    AS_COMPILER_FLAG(-Werror,ERROR_CFLAGS="$ERROR_CFLAGS -Werror",ERROR_CFLAGS="$ERROR_CFLAGS")
  fi
fi

AC_SUBST(ERROR_CFLAGS)

AC_HEADER_STDC([])
AC_C_INLINE

dnl GTK docs
GTK_DOC_CHECK

dnl Check for Glib 
PKG_CHECK_MODULES(GLIB, [glib-2.0 >= 2.4, gobject-2.0 >= 2.4])

AC_SUBST(GLIB_CFLAGS)
AC_SUBST(GLIB_LIBS)

GLIB_GENMARSHAL=`$PKG_CONFIG --variable=glib_genmarshal glib-2.0`
AC_SUBST(GLIB_GENMARSHAL)

dnl Check for D-Bus
PKG_CHECK_MODULES(DBUS, [dbus-1 >= 0.61, dbus-glib-1 >= 0.61])

AC_SUBST(DBUS_CFLAGS)
AC_SUBST(DBUS_LIBS)

AS_AC_EXPAND(DATADIR, $datadir)
DBUS_SERVICES_DIR="$DATADIR/dbus-1/services"
AC_SUBST(DBUS_SERVICES_DIR)
AC_DEFINE_UNQUOTED(DBUS_SERVICES_DIR, "$DBUS_SERVICES_DIR", [DBus services directory])


dnl Check for Loudmouth
PKG_CHECK_MODULES(LOUDMOUTH, loudmouth-1.0)

AC_SUBST(LOUDMOUTH_CFLAGS)
AC_SUBST(LOUDMOUTH_LIBS)

dnl glibc backtrace functions
AC_CHECK_FUNCS(backtrace backtrace_symbols_fd)
AC_CHECK_HEADERS(execinfo.h)

dnl signal functions
AC_CHECK_FUNCS(signal)
AC_CHECK_HEADERS(signal.h)

AC_OUTPUT( Makefile \
           src/Makefile \
           m4/Makefile \
           data/Makefile \
           generate/Makefile \
           tests/Makefile
)
