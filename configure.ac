AC_PREREQ([2.59])

# Making releases:
#   set the new version number:
#     odd minor -> development series
#     even minor -> stable series
#     increment micro for each release within a series
#   set gabble_nano_version to 0.

m4_define([gabble_major_version], [0])
m4_define([gabble_minor_version], [16])
m4_define([gabble_micro_version], [999])
m4_define([gabble_nano_version], [1])

# Some magic
m4_define([gabble_base_version],
          [gabble_major_version.gabble_minor_version.gabble_micro_version])
m4_define([gabble_version],
          [m4_if(gabble_nano_version, 0, [gabble_base_version],
              [gabble_base_version].[gabble_nano_version])])

AC_INIT([Telepathy Gabble], [gabble_version],
        [https://bugs.freedesktop.org/enter_bug.cgi?product=Telepathy&component=gabble])

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
AC_PROG_MKDIR_P

AC_CHECK_HEADERS_ONCE([
    arpa/inet.h
    arpa/nameser.h
    fcntl.h
    ifaddrs.h
    netdb.h
    netinet/in.h
    sys/ioctl.h
    sys/un.h
    unistd.h
    ])

# on Darwin, these headers are interdependent, according to autoconf.info
AC_CHECK_HEADERS([sys/socket.h], [], [],
[
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
])
AC_CHECK_HEADERS([net/if.h], [], [],
[
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#ifdef HAVE_SYS_SOCKET_H
# include <sys/socket.h>
#endif
])

# Autoconf has a handy macro for this, since it tends to have dependencies
AC_HEADER_RESOLV

COMPILER_OPTIMISATIONS
COMPILER_COVERAGE

ifelse(gabble_nano_version, 0,
    [ official_release=yes ],
    [ official_release=no ])

AM_CONDITIONAL([OFFICIAL_RELEASE], [test "x$official_release" = xyes])

TP_COMPILER_WARNINGS([ERROR_CFLAGS], [test "x$official_release" = xno],
  [all \
   extra \
   declaration-after-statement \
   shadow \
   strict-prototypes \
   missing-declarations \
   missing-prototypes \
   sign-compare \
   nested-externs \
   pointer-arith \
   format-security \
   init-self],
  [missing-field-initializers \
   unused-parameter])
AC_SUBST([ERROR_CFLAGS])


#------------------------------------------------------------
# Detect Operating system based on $host
#------------------------------------------------------------
AC_MSG_CHECKING([operating system])

case "$host" in
  *-*-*mingw*|*-*-*cygwin*)
    platform=win32
    AC_MSG_RESULT($platform)
    ;;
  *)
    platform=unix
    AC_MSG_RESULT($platform)
    ;;
esac

AM_CONDITIONAL([WINDOWS], [test "$platform" = "win32"])

# -----------------------------------------------------------
# Make CA certificates path configurable
# Stolen from GIO's TLS
# -----------------------------------------------------------
AC_MSG_CHECKING([location of system Certificate Authority list])
AC_ARG_WITH(ca-certificates,
            [AC_HELP_STRING([--with-ca-certificates=@<:@path@:>@],
                            [path to system Certificate Authority list])])
if test "$with_ca_certificates" = "no"; then
    AC_MSG_RESULT([disabled])
else
    if test -z "$with_ca_certificates"; then
        for f in /etc/pki/tls/certs/ca-bundle.crt \
                 /etc/ssl/certs/ca-certificates.crt; do
            if test -f "$f"; then
                with_ca_certificates="$f"
            fi
        done
        if test -z "$with_ca_certificates"; then
            AC_MSG_ERROR([could not find. Use --with-ca-certificates=path to set, or --without-ca-certificates to disable])
        fi
    fi

    AC_MSG_RESULT($with_ca_certificates)
    AC_DEFINE_UNQUOTED([GTLS_SYSTEM_CA_CERTIFICATES], ["$with_ca_certificates"], [path to system Certificate Authority list])
fi

if test -n "$with_ca_certificates"; then
    if ! test -f "$with_ca_certificates"; then
        AC_MSG_WARN([Specified certificate authority file '$with_ca_certificates' does not exist])
    fi
fi

# We are still using GValueArray which has been deprecated in GLib. Disable
# GLib deprecation warnings as the only way to fix this is to switch to GDBus.
AC_DEFINE(GLIB_DISABLE_DEPRECATION_WARNINGS, 1, [Build with GLib deprecated])

AC_ARG_ENABLE(debug,
  AC_HELP_STRING([--disable-debug],[compile without debug code]),
    enable_debug=$enableval, enable_debug=yes )

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

AC_SUBST([ENABLE_CODING_STYLE_CHECKS])

AC_ARG_ENABLE([installed-tests],
  AC_HELP_STRING([--enable-installed-tests],
    [make tests installable]),
    [installed_tests=$enableval], [installed_tests=no])

if test x$installed_tests = xyes; then
  AC_DEFINE(ENABLE_INSTALLED_TESTS, [],
      [Make tests installable])
fi
AM_CONDITIONAL([ENABLE_INSTALLED_TESTS], [test "x$installed_tests" = xyes])

gabbletestsdir=${libdir}/telepathy-gabble-tests
AC_SUBST(gabbletestsdir)

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

# whether to assert when g_critical() is used
AC_ARG_ENABLE([fatal-criticals],
  AC_HELP_STRING([--disable-fatal-criticals],
    [do not assert because of critical warnings]),
    [fatal_criticals=$enableval], [fatal_criticals=yes])

if test x$fatal_criticals = xyes; then
  AC_DEFINE(ENABLE_FATAL_CRITICALS, [],
      [Critical warnings will result in an assertion])
fi
AM_CONDITIONAL([ENABLE_FATAL_CRITICALS], [test "x$fatal_criticals" = xyes])

dnl dummy check for gtk-doc
AC_ARG_ENABLE(gtk-doc,
  AC_HELP_STRING([--enable-gtk-doc],[does not actually do anything]),
  enable_gtk_doc=yes, enable_gtk_doc=no)
AM_CONDITIONAL([ENABLE_GTK_DOC], [test "x$enable_gtk_doc" = xyes])

dnl Check for Glib
PKG_CHECK_MODULES(GLIB,
    [glib-2.0 >= 2.24, gobject-2.0 >= 2.24, gthread-2.0 >= 2.24, gio-2.0])
PKG_CHECK_MODULES(GMODULE, [gmodule-2.0 >= 2.24])

AC_SUBST(GLIB_CFLAGS)
AC_SUBST(GLIB_LIBS)


GLIB_GENMARSHAL=`$PKG_CONFIG --variable=glib_genmarshal glib-2.0`
AC_SUBST(GLIB_GENMARSHAL)

# These must contain "exec" for automake to work right (install-exec,
# not install-data).
#
# Private directory for Wocky and the gabble-plugins library
if test "x$pluginexeclibdir" = x; then
  pluginexeclibdir='${libdir}/telepathy/gabble-0/lib'
fi
AC_ARG_VAR([pluginexeclibdir])
# The actual plugins
if test "x$pluginexecdir" = x; then
  pluginexecdir='${libdir}/telepathy/gabble-0/plugins'
fi
AC_ARG_VAR([pluginexecdir])

AC_ARG_ENABLE(plugins,
  AC_HELP_STRING([--disable-plugins],
                 [disable plugin loader]),
    [enable_plugins=$enableval], [enable_plugins=yes])

if test x$enable_plugins = xyes; then
  AC_DEFINE(ENABLE_PLUGINS, [], [Enable plugins])
  PKG_CHECK_MODULES(GMODULE, [gmodule-2.0])

  AC_ARG_ENABLE(plugin-api,
    AC_HELP_STRING([--enable-plugin-api],
                   [install headers for third-party plugins (experimental)]),
    [
      enable_plugin_api=$enableval
      wocky_install_headers_dir="${includedir}/telepathy-gabble-0"
    ],
    [enable_plugin_api=no])
fi

AC_SUBST(GMODULE_CFLAGS)
AC_SUBST(GMODULE_LIBS)

AM_CONDITIONAL(ENABLE_PLUGINS, test x$enable_plugins = xyes)
AC_SUBST(ENABLE_PLUGINS)

AM_CONDITIONAL(ENABLE_PLUGIN_API, test x$enable_plugin_api = xyes)

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

AC_DEFINE(TP_VERSION_MIN_REQUIRED, TP_VERSION_0_18, [Ignore post 0.18 deprecations])
PKG_CHECK_MODULES(TP_GLIB, [telepathy-glib >= 0.19.0])

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

# If you don't specify --prefix, it starts off as NONE. Autoconf
# would normally do this defaulting for us later, but that's too
# late to help Wocky.
if test "x${prefix}" = "xNONE"; then
  prefix=/usr/local
fi

# We tell Wocky to install its headers alongside gabble's so that an actual
# separate Wocky installation won't clash with them. This is a bit of a hack.
# AX_CONFIG_DIR doesn't make it very easy to pass extra arguments to the
# submodule's configure.
prev_ac_configure_args=$ac_configure_args

ac_configure_args="$ac_configure_args --with-installed-headers=${wocky_install_headers_dir} --enable-shared-suffix=${PACKAGE}-${VERSION} --libdir=${pluginexeclibdir}"

if test "x$ENABLE_CODING_STYLE_CHECKS" = xyes ; then
  ac_configure_args="$ac_configure_args --enable-coding-style-checks"
else
  ac_configure_args="$ac_configure_args --disable-coding-style-checks"
fi

if test "x$tp_werror" = xyes && test "x$official_release" = xno; then
  ac_configure_args="$ac_configure_args --enable-Werror"
else
  ac_configure_args="$ac_configure_args --disable-Werror"
fi

dnl wocky
prev_top_build_prefix=$ac_top_build_prefix
AX_CONFIG_DIR([lib/ext/wocky])
ac_top_build_prefix=$prev_top_build_prefix

ac_configure_args=$prev_ac_configure_args

dnl Check if Android build
AC_ARG_ENABLE(submodules,
              AS_HELP_STRING([--disable-submodules],[Use system versions of Wocky, rather than submodules]),
              build_submodules=$enableval, build_submodules=yes )

dnl Check for Wocky
# re-enable once Wocky has been released as a lib
if test x$build_submodules = xyes; then
    export PKG_CONFIG_PATH=$PKG_CONFIG_PATH:"$ac_top_build_prefix"lib/ext/wocky/wocky
fi

PKG_CHECK_MODULES(WOCKY, wocky >= 0.0.0)
AC_SUBST(WOCKY_CFLAGS)
AC_SUBST(WOCKY_LIBS)

AC_ARG_ENABLE(google-relay,
  AC_HELP_STRING([--disable-google-relay],
                 [disable Google Jingle relay support]),
    [enable_google_relay=$enableval], [enable_google_relay=yes])

if test x$enable_google_relay = xyes; then
  AC_DEFINE(ENABLE_GOOGLE_RELAY, [], [Enable Google Jingle relay support])

  dnl Check for libsoup
  PKG_CHECK_MODULES(SOUP, libsoup-2.4)
else
  SOUP_CFLAGS=
  SOUP_LIBS=
fi
AC_SUBST(SOUP_CFLAGS)
AC_SUBST(SOUP_LIBS)
AM_CONDITIONAL([ENABLE_GOOGLE_RELAY], [test "x$enable_google_relay" = xyes])

AC_ARG_ENABLE(file-transfer,
  AC_HELP_STRING([--disable-file-transfer],
                 [disable file transfer support]),
    [enable_ft=$enableval], [enable_ft=yes])

if test x$enable_ft = xyes; then
    AC_DEFINE(ENABLE_FILE_TRANSFER, [], [Enable file transfer])
fi
AM_CONDITIONAL([ENABLE_FILE_TRANSFER], [test "x$enable_ft" = xyes])

AC_ARG_ENABLE(voip,
  AC_HELP_STRING([--disable-voip],
                 [disable VoIP support (and, consequently, Jingle-based file transfer support]),
    [enable_voip=$enableval], [enable_voip=yes])

if test x$enable_voip = xyes; then
    AC_DEFINE(ENABLE_VOIP, [], [Enable VoIP])
fi
AM_CONDITIONAL([ENABLE_VOIP], [test "x$enable_voip" = xyes])

if test x$enable_voip = xyes -a x$enable_ft = xyes; then
    enable_jingle_ft=yes
    AC_DEFINE(ENABLE_JINGLE_FILE_TRANSFER, [], [Enable file transfer])
    dnl Check for libnice
    PKG_CHECK_MODULES(NICE, nice >= 0.0.11)
else
    enable_jingle_ft=no
    NICE_CFLAGS=
    NICE_LIBS=
fi
AC_SUBST(NICE_CFLAGS)
AC_SUBST(NICE_LIBS)
AM_CONDITIONAL([ENABLE_JINGLE_FILE_TRANSFER], [test "x$enable_jingle_ft" = xyes])

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
           lib/Makefile \
           lib/ext/Makefile \
           lib/gibber/Makefile \
           plugins/Makefile \
           gabble/Makefile \
           gabble/telepathy-gabble-uninstalled.pc \
           gabble/telepathy-gabble.pc
)

if test false != "$TEST_PYTHON"; then
  tests_enabled=yes
else
  tests_enabled=no
fi

echo "
Configure summary:

        Compiler....................:  ${CC}
        Compiler Flags..............:  ${CFLAGS} ${ERROR_CFLAGS}
        Prefix......................:  ${prefix}
        Coding style checks.........:  ${ENABLE_CODING_STYLE_CHECKS}
        Enable debug................:  ${enable_debug}
        Python tests................:  ${tests_enabled}
        Install unit tests..........:  ${installed_tests}

    Features:

        Client type.................:  \"${CLIENT_TYPE}\"
        Plugin support..............:  ${enable_plugins}
        Plugin headers installed....:  ${enable_plugin_api}
        Channel.Type.Call support...:  ${enable_channel_type_call}
        Google relay support........:  ${enable_google_relay}
        File transfer support.......:  ${enable_ft}
        Jingle file transfer support:  ${enable_jingle_ft}
        VoIP support................:  ${enable_voip}

"
