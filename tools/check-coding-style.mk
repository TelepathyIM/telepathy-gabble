check-local::
	@fail=0; \
	if test -n "$(check_misc_sources)"; then \
		tools_dir=$(top_srcdir)/tools \
		sh $(top_srcdir)/tools/check-misc.sh \
			$(check_misc_sources) || fail=1; \
	fi; \
	if test -n "$(check_c_sources)"; then \
		tools_dir=$(top_srcdir)/tools \
		sh $(top_srcdir)/tools/check-c-style.sh \
			$(check_c_sources) || fail=1; \
	fi;\
	if test yes = "$(ENABLE_CODING_STYLE_CHECKS)"; then \
		exit "$$fail";\
	else \
		exit 0;\
	fi
