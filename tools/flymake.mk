check-syntax:
	$(CC) $(AM_CPPFLAGS) $(AM_CFLAGS) -fsyntax-only $(CHK_SOURCES)

.PHONY: check-syntax
