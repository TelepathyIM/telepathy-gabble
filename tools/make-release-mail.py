#!/usr/bin/env python
# vim: set fileencoding=utf-8 :
#
# Hello. This is make-release-mail.py from the Telepathy project. It's
# designed to turn an item from a NEWS file into a mail suitable for sending
# to <telepathy@lists.freedesktop.org>. I hope that you enjoy your stay.

import sys
import re

def looks_like_a_header(line, package, version=None):
    if version is None:
        pattern = "^%s .* \(.*\)$" % package
    else:
        pattern = "^%s %s \(.*\)$" % (package, version)

    return re.match(pattern, line) is not None

def extract_description(package, version, news_path):
    release_name = []
    details = []

    with open(news_path) as f:
        lines = (line for line in f.readlines())
        for line in lines:
            # Find the 'telepathy-foo 0.1.2' header
            if looks_like_a_header(line, package, version):
                break

        # Skip the ====== line, and the first blank line
        lines.next()
        lines.next()

        got_release_name = False

        for line in lines:
            line = line.rstrip()
            # If we hit the next version header, we're done
            if looks_like_a_header(line, package):
                break
            # Else, if we hit a blank line and we're still reading the release
            # name, we're done with the release name.
            elif not got_release_name and line == '':
                got_release_name = True
            # Otherwise, append this to the relevant list
            elif not got_release_name:
                release_name.append(line)
            else:
                details.append(line)

        assert got_release_name, (release_name, details)

    # We rstrip details because it picks up a trailing blank line
    return ('\n'.join(release_name), '\n'.join(details).rstrip())

BASE_URL = 'http://telepathy.freedesktop.org/releases'
GIT_URL = 'http://cgit.freedesktop.org/telepathy'

def main(package, version, news_path):
    release_name, details = extract_description(package, version, news_path)

    print """
%(release_name)s

tarball: %(base_url)s/%(package)s/%(package)s-%(version)s.tar.gz
signature: %(base_url)s/%(package)s/%(package)s-%(version)s.tar.gz.asc
git: %(git_url)s/%(package)s

%(details)s""".strip().rstrip() % {
        'base_url': BASE_URL,
        'git_url': GIT_URL,
        'package': package,
        'version': version,
        'release_name': release_name,
        'details': details,
    }

if __name__ == '__main__':
    try:
        package, version, news_path = sys.argv[1:]

        main(package, version, news_path)
    except ValueError, e:
        sys.stderr.write(
            'Usage: %s package-name package.version.number path/to/NEWS\n' %
            sys.argv[0])
        sys.stderr.flush()
        sys.exit(1)
