check-local::
	@fail=0; \
	if test -n "$(check_misc_sources)"; then \
		top_srcdir=$(top_srcdir) sh $(top_srcdir)/check-whitespace.sh \
			$(check_misc_sources) || fail=1; \
	fi; \
	if test -n "$(check_c_sources)"; then \
		top_srcdir=$(top_srcdir) sh $(top_srcdir)/check-c-style.sh \
			$(check_c_sources) || fail=1; \
	fi;\
	if test -n "$(CHECK_CODING_STYLE)"; then \
		exit "$$fail";\
	else \
		exit 0;\
	fi
