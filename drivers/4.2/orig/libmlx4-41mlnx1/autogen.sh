#! /bin/sh -eE

set -x
aclocal -I config
libtoolize --force --copy
autoheader
automake --foreign --add-missing --copy
autoconf
