
gabbletestsdir = libdir / 'telepathy-gabble-tests'

test_list = [
  'test-dtube-unique-names',
  'test-gabble-idle-weak',
  'test-handles',
  'test-jid-decode',
  'test-parse-message',
  'test-presence',
  'tp-error-from-wocky'
]

#AM_CFLAGS = $(ERROR_CFLAGS) @DBUS_CFLAGS@ @GLIB_CFLAGS@ @WOCKY_CFLAGS@ \
#    @TP_GLIB_CFLAGS@ \
#    -I $(top_srcdir) -I $(top_builddir) \
#    -I $(top_srcdir)/lib -I $(top_builddir)/lib

#TESTS = $(tests_list)

#TESTS_ENVIRONMENT = \
#  abs_top_builddir=@abs_top_builddir@ \
#  G_DEBUG=fatal-warnings,fatal-criticals \
#  sh $(top_srcdir)/tests/twisted/tools/with-session-bus.sh \
#    --config-file=$(top_builddir)/tests/twisted/tools/servicedir-uninstalled/tmp-session-bus.conf --


#check-valgrind: $(TESTS)
#	G_SLICE=always-malloc \
#	G_DEBUG=gc-friendly \
#	$(MAKE) \
#		TESTS_ENVIRONMENT="$(TESTS_ENVIRONMENT) \
#		libtool --mode=execute valgrind \
#			--leak-check=full \
#			--show-reachable=no \
#			--gen-suppressions=all \
#			--num-callers=20 \
#			--suppressions=@abs_top_srcdir@/tests/suppressions/gabble.supp \
#			--suppressions=@abs_top_srcdir@/tests/suppressions/tp-glib.supp \
#			--error-exitcode=1" \
#		check-TESTS

subdir('twisted')

tests_src = []
foreach t: test_list
  t_c = t+'.c'
  tests_src += t_c
  t_exe = executable(t, t_c,
    enums_src, interfaces_src, gtypes_src,
    dependencies: gabble_deps,
    include_directories: [gabble_conf_inc],
    link_with: [gabble_lib, gabble_plugins_lib],
    install: get_option('install-tests')
  )
  test(t, t_exe)
endforeach

style_check_src += files(tests_src)
