AC_PREREQ([2.59])

# Making releases:
#   set the new version number:
#     odd minor -> development series
#     even minor -> stable series
#     increment micro for each release within a series
#   set gabble_nano_version to 0.

m4_define([gabble_major_version], [0])
m4_define([gabble_minor_version], [9])
m4_define([gabble_micro_version], [5])
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

AM_INIT_AUTOMAKE([1.9 -Wno-portability tar-ustar])
AM_CONFIG_HEADER(config.h)

AC_USE_SYSTEM_EXTENSIONS

m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES])

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
                 [do not check coding style using grep]),
    [ENABLE_CODING_STYLE_CHECKS=$enableval], [ENABLE_CODING_STYLE_CHECKS=yes])
    ])

if test x$enable_debug = xyes; then
  AC_DEFINE(ENABLE_DEBUG, [], [Enable debug code])
fi
AM_CONDITIONAL([ENABLE_DEBUG], [test "x$enable_debug" = xyes])

if test x$enable_handle_leak_debug = xyes; then
  AC_DEFINE(ENABLE_HANDLE_LEAK_DEBUG, [], [Enable handle leak debug code])
  HANDLE_LEAK_DEBUG_CFLAGS="-rdynamic"
fi

AC_SUBST(HANDLE_LEAK_DEBUG_CFLAGS)

AC_SUBST([ENABLE_CODING_STYLE_CHECKS])

AC_ARG_ENABLE([is-a-phone],
  AC_HELP_STRING([--enable-is-a-phone],
    [advertise that we are a phone, not a PC]),
    [is_a_phone=$enableval], [is_a_phone=no])

if test x$is_a_phone = xyes; then
  AC_DEFINE(CLIENT_TYPE, ["phone"],
      [Client type from http://xmpp.org/registrar/disco-categories.html#client])
  CLIENT_TYPE=phone
else
  AC_DEFINE(CLIENT_TYPE, ["pc"],
      [Client type from http://xmpp.org/registrar/disco-categories.html#client])
  CLIENT_TYPE=pc
fi

AC_SUBST(CLIENT_TYPE)

dnl Check for Glib
PKG_CHECK_MODULES(GLIB, [glib-2.0 >= 2.16, gobject-2.0 >= 2.16, gthread-2.0 >= 2.16])

AC_SUBST(GLIB_CFLAGS)
AC_SUBST(GLIB_LIBS)

GLIB_GENMARSHAL=`$PKG_CONFIG --variable=glib_genmarshal glib-2.0`
AC_SUBST(GLIB_GENMARSHAL)

AC_ARG_ENABLE(plugins,
  AC_HELP_STRING([--enable-plugins],
                 [enable experimental plugin support]),
    [enable_plugins=$enableval], [enable_plugins=no])

if test x$enable_plugins = xyes; then
  AC_DEFINE(ENABLE_PLUGINS, [], [Enable plugins])
  dnl If we want plugins, we need gmodule
  PKG_CHECK_MODULES(GMODULE, [gmodule-2.0])
fi

AC_SUBST(GMODULE_CFLAGS)
AC_SUBST(GMODULE_LIBS)

AM_CONDITIONAL(ENABLE_PLUGINS, test x$enable_plugins = xyes)
AC_SUBST(ENABLE_PLUGINS)

AC_ARG_ENABLE(channel-type-call,
  AC_HELP_STRING([--disable-channel-type-call],
                 [disable support for the draft Channel.Type.Call]),
    [enable_channel_type_call=$enableval], [enable_channel_type_call=yes])

if test x$enable_channel_type_call = xyes; then
  AC_DEFINE(ENABLE_CHANNEL_TYPE_CALL, [], [Enable Channel.Type.Call])
fi

AM_CONDITIONAL(ENABLE_CHANNEL_TYPE_CALL, test x$enable_channel_type_call = xyes)

dnl Check for D-Bus
PKG_CHECK_MODULES(DBUS, [dbus-1 >= 1.1.0, dbus-glib-1 >= 0.82])

AC_SUBST(DBUS_CFLAGS)
AC_SUBST(DBUS_LIBS)

PKG_CHECK_MODULES(TP_GLIB, [telepathy-glib >= 0.7.37])

AC_SUBST(TP_GLIB_CFLAGS)
AC_SUBST(TP_GLIB_LIBS)

dnl Check for code generation tools
XSLTPROC=
AC_CHECK_PROGS([XSLTPROC], [xsltproc])
if test -z "$XSLTPROC"; then
  AC_MSG_ERROR([xsltproc (from the libxslt source package) is required])
fi

AM_PATH_PYTHON([2.5])

# Check for a Python >= 2.5 with Twisted, to run the tests
AC_MSG_CHECKING([for Python with Twisted and XMPP protocol support])
if $PYTHON -c "import twisted.words.xish.domish, twisted.words.protocols.jabber, twisted.internet.reactor" >/dev/null 2>&1; then
    TEST_PYTHON="$PYTHON"
else
    TEST_PYTHON=false
fi
AC_MSG_RESULT([$TEST_PYTHON])
AC_SUBST(TEST_PYTHON)
AM_CONDITIONAL([WANT_TWISTED_TESTS], test false != "$TEST_PYTHON")

# We have to run Wocky's configure *before* looking for it with PKG_CHECK_MODULES so wocky-uninstalled.pc has been generated

prev_top_build_prefix=$ac_top_build_prefix
AX_CONFIG_DIR([lib/ext/wocky])
ac_top_build_prefix=$prev_top_build_prefix

dnl Check for Wocky
# re-enable once Wocky has been released as a lib
export PKG_CONFIG_PATH=$PKG_CONFIG_PATH:"$ac_top_build_prefix"lib/ext/wocky/wocky
PKG_CHECK_MODULES(WOCKY, wocky >= 0.0.0)
AC_SUBST(WOCKY_CFLAGS)
AC_SUBST(WOCKY_LIBS)

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
           tests/suppressions/Makefile \
           tests/twisted/Makefile \
           tests/twisted/tools/Makefile \
           lib/Makefile \
           lib/ext/Makefile \
           lib/gibber/Makefile \
           lib/loudmouth/Makefile \
           plugins/Makefile \
           gabble/Makefile \
           gabble/telepathy-gabble-uninstalled.pc \
           gabble/telepathy-gabble.pc
)
