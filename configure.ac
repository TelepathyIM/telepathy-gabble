AC_PREREQ([2.59])

# Making releases:
#   set the new version number:
#     odd minor -> development series
#     even minor -> stable series
#     increment micro for each release within a series
#   set gabble_release to 1.

m4_define([gabble_major_version], [0])
m4_define([gabble_minor_version], [5])
m4_define([gabble_micro_version], [12])
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

AM_INIT_AUTOMAKE([1.8])
AM_CONFIG_HEADER(config.h)

dnl check for tools
AC_PROG_CC
AC_PROG_CC_STDC
AC_PROG_INSTALL
AC_PROG_LIBTOOL

dnl decide error flags
AS_COMPILER_FLAG(-Wall, ERROR_CFLAGS="-Wall", ERROR_CFLAGS="")
AS_COMPILER_FLAG(-Werror, werror=yes, werror=no)
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

AC_SUBST(ERROR_CFLAGS)

AC_ARG_ENABLE(debug,
  AC_HELP_STRING([--disable-debug],[compile without debug code]),
    enable_debug=$enableval, enable_debug=yes )

AC_ARG_ENABLE(handle-leak-debug,
  AC_HELP_STRING([--enable-handle-leak-debug],[compile with -rdynamic so telepathy-glib handle leak debugging code will work]),
    enable_handle_leak_debug=$enableval, enable_handle_leak_debug=no )

AC_ARG_ENABLE(coverage,
  AC_HELP_STRING([--enable-coverage],[compile with coverage info]),
    enable_coverage=$enableval, enable_coverage=no )

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

if test x$enable_coverage = xyes; then
  COVERAGE_CFLAGS="-g -fprofile-arcs -ftest-coverage"
fi

AC_SUBST(HANDLE_LEAK_DEBUG_CFLAGS)
AC_SUBST(COVERAGE_CFLAGS)

AC_SUBST([ENABLE_CODING_STYLE_CHECKS])

dnl Endianness (for the sha1 implementation)
AC_C_BIGENDIAN

dnl Check for Glib 
PKG_CHECK_MODULES(GLIB, [glib-2.0 >= 2.4, gobject-2.0 >= 2.4])

AC_SUBST(GLIB_CFLAGS)
AC_SUBST(GLIB_LIBS)

GLIB_GENMARSHAL=`$PKG_CONFIG --variable=glib_genmarshal glib-2.0`
AC_SUBST(GLIB_GENMARSHAL)

dnl Check for D-Bus
PKG_CHECK_MODULES(DBUS, [dbus-1 >= 0.61, dbus-glib-1 >= 0.72])

AC_SUBST(DBUS_CFLAGS)
AC_SUBST(DBUS_LIBS)

PKG_CHECK_MODULES(TP_GLIB, [telepathy-glib >= 0.5.14])

AC_SUBST(TP_GLIB_CFLAGS)
AC_SUBST(TP_GLIB_LIBS)

AS_AC_EXPAND(DATADIR, $datadir)
DBUS_SERVICES_DIR="$DATADIR/dbus-1/services"
AC_SUBST(DBUS_SERVICES_DIR)
AC_DEFINE_UNQUOTED(DBUS_SERVICES_DIR, "$DBUS_SERVICES_DIR", [DBus services directory])

dnl Check for code generation tools
dnl - not actually needed in this release, but they'll be needed again
dnl   when the OLPC branch is merged
XSLTPROC=
AC_CHECK_PROGS([XSLTPROC], [xsltproc])
if test -z "$XSLTPROC"; then
  AC_MSG_ERROR([xsltproc (from the libxslt source package) is required])
fi
DBUS_BINDING_TOOL=
AC_CHECK_PROGS([DBUS_BINDING_TOOL], [dbus-binding-tool])
if test -z "$DBUS_BINDING_TOOL"; then
  AC_MSG_ERROR([dbus-binding-tool (from dbus-glib) is required])
fi
PYTHON=
AC_CHECK_PROGS([PYTHON], [python2.3 python2.4 python2.5 python])
if test -z "$PYTHON"; then
  AC_MSG_ERROR([Python is required to compile this package])
fi

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

AC_OUTPUT( Makefile \
           extensions/Makefile \
           extensions/tools/Makefile \
           src/Makefile \
           m4/Makefile \
           data/Makefile \
           tests/Makefile
)
