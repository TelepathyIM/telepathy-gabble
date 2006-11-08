AC_PREREQ([2.59])

# Making releases:
#   set the new version number:
#     odd minor -> development series
#     even minor -> stable series
#     increment micro for each release within a series
#   set gabble_release to 1.

m4_define([gabble_major_version], [0])
m4_define([gabble_minor_version], [4])
m4_define([gabble_micro_version], [3])
m4_define([gabble_nano_version], [1])

# Some magic
m4_define([gabble_base_version],
          [gabble_major_version.gabble_minor_version.gabble_micro_version])
m4_define([gabble_version],
          [m4_if(gabble_nano_version, 0, [gabble_base_version], [gabble_base_version].[gabble_nano_version])])dnl

AC_INIT([Telepathy Gabble], [gabble_version], [https://bugs.freedesktop.org/enter_bug.cgi?product=Telepathy])


AC_CONFIG_MACRO_DIR([m4])

AM_INIT_AUTOMAKE([1.8])
AM_PROG_LIBTOOL
AM_CONFIG_HEADER(config.h)

dnl check for tools
AC_PROG_CC
AC_PROG_CC_STDC
AM_PROG_AS

dnl decide error flags
AS_COMPILER_FLAG(-Wall, ERROR_CFLAGS="-Wall", ERROR_CFLAGS="")

ifelse(gabble_nano_version, 0,
    [],
    [
        AS_COMPILER_FLAG(-Werror,ERROR_CFLAGS="$ERROR_CFLAGS -Werror",[])
        AS_COMPILER_FLAG(-Wextra,ERROR_CFLAGS="$ERROR_CFLAGS -Wextra",[])
        AS_COMPILER_FLAG(-Wno-missing-field-initializers,ERROR_CFLAGS="$ERROR_CFLAGS -Wno-missing-field-initializers",[])
        AS_COMPILER_FLAG(-Wno-unused-parameter,ERROR_CFLAGS="$ERROR_CFLAGS -Wno-unused-parameter",[])
    ])
AC_SUBST(ERROR_CFLAGS)

AC_ARG_ENABLE(debug,
  AC_HELP_STRING([--disable-debug],[compile without debug code]),
    enable_debug=$enableval, enable_debug=yes )

AC_ARG_ENABLE(handle-leak-debug,
  AC_HELP_STRING([--enable-handle-leak-debug],[compile with handle leak debugging code]),
    enable_handle_leak_debug=$enableval, enable_handle_leak_debug=no )

if test x$enable_debug = xyes; then
  AC_DEFINE(ENABLE_DEBUG, [], [Enable debug code])
fi

if test x$enable_handle_leak_debug = xyes; then
  AC_DEFINE(ENABLE_HANDLE_LEAK_DEBUG, [], [Enable handle leak debug code])
  HANDLE_LEAK_DEBUG_CFLAGS="-rdynamic"
fi

AC_SUBST(HANDLE_LEAK_DEBUG_CFLAGS)

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
AC_ARG_ENABLE(loudmouth-versioning,
  AC_HELP_STRING([--disable-loudmouth-versioning],
                 [Stops configure checking the version of loudmouth to use.]),
                 enable_loudmouth_versioning=$enableval,
                 enable_loudmouth_versioning=yes)
                 

if test x$enable_loudmouth_versioning = xyes; then
  LOUDMOUTH_VERSION_STR=" >= 1.1.1"
else
  LOUDMOUTH_VERSION_STR=""
fi

PKG_CHECK_MODULES(LOUDMOUTH, loudmouth-1.0$LOUDMOUTH_VERSION_STR)

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
