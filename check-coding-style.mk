check-local::
	if test -n "$(check_misc_sources)"; then \
		top_srcdir=$(top_srcdir) sh $(top_srcdir)/check-whitespace.sh \
			$(check_misc_sources); \
	fi
	if test -n "$(check_c_sources)"; then \
		top_srcdir=$(top_srcdir) sh $(top_srcdir)/check-c-style.sh \
			$(check_c_sources); \
	fi
