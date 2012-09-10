#!/usr/bin/python

"""Construct a GNU ld or Debian dpkg version-script from a set of
RFC822-style symbol lists.

Usage:
    make-version-script.py [--symbols SYMBOLS] [--unreleased-version VER]
        [--dpkg "LIBRARY.so.0 LIBRARY0 #MINVER#"]
        [--dpkg-build-depends-package LIBRARY-dev]
        [FILES...]

Each FILE starts with RFC822-style headers "Version:" (the name of the
symbol version, e.g. FOO_1.2.3) and "Extends:" (either the previous
version, or "-" if this is the first version). Next there is a blank
line, then a list of C symbols one per line.

Comments (lines starting with whitespace + "#") are allowed and ignored.

If --symbols is given, SYMBOLS lists the symbols actually exported by
the library (one per line). If --unreleased-version is given, any symbols
in SYMBOLS but not in FILES are assigned to that version; otherwise, any
such symbols cause an error.

If --dpkg is given, produce a Debian dpkg-gensymbols file instead of a
GNU ld version-script. The argument to --dpkg is the first line of the
resulting symbols file, and --dpkg-build-depends-package can optionally
be used to set the Build-Depends-Package field.

This script originates in telepathy-glib <http://telepathy.freedesktop.org/> -
please send us any changes that are needed.
"""

# Copyright (C) 2008-2010 Collabora Ltd. <http://www.collabora.co.uk/>
# Copyright (C) 2008 Nokia Corporation
#
# Copying and distribution of this file, with or without modification,
# are permitted in any medium without royalty provided the copyright
# notice and this notice are preserved.

import sys
from getopt import gnu_getopt


def e(format, *args):
    sys.stderr.write((format + '\n') % args)


def main(abifiles, symbols=None, unreleased_version=None,
         dpkg=False, dpkg_first_line=None, dpkg_build_depends_package=None):

    gnuld = not dpkg
    symbol_set = None

    if symbols is not None:
        symbol_set = open(symbols, 'r').readlines()
        symbol_set = map(str.strip, symbol_set)
        symbol_set = set(symbol_set)

    versioned_symbols = set()

    dpkg_symbols = []
    dpkg_versions = []

    if dpkg:
        assert dpkg_first_line is not None
        print dpkg_first_line
        if dpkg_build_depends_package is not None:
            print "* Build-Depends-Package: %s" % dpkg_build_depends_package

    for filename in abifiles:
        lines = open(filename, 'r').readlines()

        version = None
        extends = None
        release = None

        for i, line in enumerate(lines):
            line = line.strip()

            if line.startswith('#'):
                continue
            elif not line:
                # the transition betwen headers and symbols
                cut = i + 1
                break
            elif line.lower().startswith('version:'):
                line = line[8:].strip()
                version = line
                continue
            elif line.lower().startswith('extends:'):
                line = line[8:].strip()
                extends = line
                continue
            elif line.lower().startswith('release:'):
                release = line[8:].strip()
                continue
            else:
                e('Could not understand line in %s header: %s', filename, line)
                raise SystemExit(1)

        else:
            e('No symbols in %s', filename)
            raise SystemExit(1)

        if version is None:
            e('No Versions: header in %s', filename)
            raise SystemExit(1)

        if extends is None:
            e('No Extends: header in %s', filename)
            raise SystemExit(1)

        if release is None and dpkg:
            e('No Release: header in %s', filename)
            raise SystemExit(1)

        if dpkg:
            dpkg_versions.append('%s@%s %s' % (version, version, release))

        lines = lines[cut:]

        if gnuld:
            print "%s {" % version
            print "    global:"

        for symbol in lines:
            symbol = symbol.strip()

            if symbol.startswith('#'):
                continue

            if gnuld:
                print "        %s;" % symbol
            elif dpkg:
                dpkg_symbols.append('%s@%s %s' % (symbol, version, release))

            if symbol in versioned_symbols:
                raise AssertionError('Symbol %s is in version %s and an '
                                     'earlier version' % (symbol, version))

            versioned_symbols.add(symbol)

        if gnuld:
            if extends == '-':
                print "    local:"
                print "        *;"
                print "};"
            else:
                print "} %s;" % extends
                print

    if dpkg:
        dpkg_symbols.sort()
        dpkg_versions.sort()

        for x in dpkg_versions:
            print " %s" % x

        for x in dpkg_symbols:
            print " %s" % x

    if symbol_set is not None:
        missing = versioned_symbols - symbol_set

        if missing:
            e('These symbols have disappeared:')

            for symbol in missing:
                e('    %s', symbol)

            raise SystemExit(1)

        unreleased = symbol_set - versioned_symbols

        if unreleased:
            if unreleased_version is None:
                e('Unversioned symbols are not allowed in releases:')

                for symbol in unreleased:
                    e('    %s', symbol)

                raise SystemExit(1)

            if gnuld:
                print "%s {" % unreleased_version
                print "    global:"

                for symbol in unreleased:
                    print "        %s;" % symbol

                print "} %s;" % version


if __name__ == '__main__':
    options, argv = gnu_getopt (sys.argv[1:], '',
                                ['symbols=', 'unreleased-version=',
                                 'dpkg=', 'dpkg-build-depends-package='])

    opts = {'dpkg': False}

    for option, value in options:
        if option == '--dpkg':
            opts['dpkg'] = True
            opts['dpkg_first_line'] = value
        else:
            opts[option.lstrip('-').replace('-', '_')] = value

    main(argv, **opts)
