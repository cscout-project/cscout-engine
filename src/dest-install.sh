#!/bin/sh
#
# (C) Copyright 2016-2025 Diomidis Spinellis
#
# This file is part of CScout.
#
# CScout is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# CScout is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with CScout.  If not, see <http://www.gnu.org/licenses/>.
#
# Install the files required for CScout to the destination directory
#

# The following definition files are created:
#stdc-defs: for compiling Standard C files, with CScout-provided headers
#host-defs: for compiling host-specific C files through CScout project files
#csmake-defs: for compiling arbitrary C files through csmake
#
# These are the files' contents:
#		STDC defs	GCC fixes	CScout defs	compiler defs
#stdc-defs	*				*
#host-defs			*		*		*
#csmake-defs			*		*

set -e

PREFIX="${1-/usr/local}"
TMPFILE="/tmp/$0-$$"
INC=../include
INCLUDE_DIR="$PREFIX/include/cscout"
# Permissions for header files
HMODE=644

# Workarounds for all compilers
WORKAROUNDS_PRE="$INC/template/gcc-pre-defs.h $INC/template/llvm-pre-defs.h $INC/template/cscout-pre-defs.h"
WORKAROUNDS_POST="$INC/template/gcc-post-defs.h $INC/template/llvm-post-defs.h"

# Create required directories
install -d "$INCLUDE_DIR/stdc" "$PREFIX/bin" "$PREFIX/lib/csmerge"

# Generic C definitions
install -m $HMODE $INC/stdc/*.h "$INCLUDE_DIR/stdc/"
cat $INC/template/cscout-pre-defs.h $INC/template/stdc-defs.h >$TMPFILE
install -m $HMODE $TMPFILE "$INCLUDE_DIR/stdc-defs.h"

# Generic C include path specification
cat <<EOF >$TMPFILE
#pragma includepath "$INCLUDE_DIR/stdc"

/* Avoid unused include file warnings */
static void _cscout_dummy2(void) { _cscout_dummy2(); }
EOF
install -m $HMODE $TMPFILE "$INCLUDE_DIR/stdc-incs.h"

# Host's C definitions
{
  cat $WORKAROUNDS_PRE
  echo "/* Definitions derived from cpp -O -dM on $(date) */"
  cpp -O -dM /dev/null | sort
  cat $WORKAROUNDS_POST
} >$TMPFILE
install -m $HMODE $TMPFILE "$INCLUDE_DIR/host-defs.h"

# Only CScout and compiler workarounds for csmake-generated projects
cat $WORKAROUNDS_PRE >$TMPFILE
install -m $HMODE $TMPFILE "$INCLUDE_DIR/csmake-pre-defs.h"
cat $WORKAROUNDS_POST >$TMPFILE
install -m $HMODE $TMPFILE "$INCLUDE_DIR/csmake-post-defs.h"

# Host's C include path specification
cpp -Wp,-v </dev/null 2>&1 |
sed -n -e '
1a\
/* Avoid unused include file warnings */
1a\
static void _cscout_dummy2(void) { _cscout_dummy2(); }
1a\

/^#include <\.\.\.> search starts here:/,/^End of search list\./ {
 /^ /!d
 s/ /#pragma includepath "/
 s/$/"/
 p
}' >$TMPFILE
install -m $HMODE $TMPFILE "$INCLUDE_DIR/host-incs.h"

# Perl scripts (keep csmake.pl in the end)
for f in cswc.pl csmake.pl ; do
  sed "s|INSTALL_INCLUDE|$INCLUDE_DIR|g" $f >$TMPFILE
  install $TMPFILE "$PREFIX/bin/$(basename $f .pl)"
done
# Install as cscc
install $TMPFILE "$PREFIX/bin/cscc"

install cscut.sh "$PREFIX/bin/cscut"
install cssplit.py "$PREFIX/bin/cssplit"
install csapi.py "$PREFIX/bin/csapi"
# Python script requiring include path replacement
sed "s|INSTALL_INCLUDE|$INCLUDE_DIR|g" cscoco.py >$TMPFILE
install $TMPFILE "$PREFIX/bin/cscoco"
install csreconst.sh "$PREFIX/bin/csreconst"

install csmerge/csmerge.sh "$PREFIX/bin/csmerge"
install -m 644 csmerge/*.sql "$PREFIX/lib/csmerge/"

rm -f $TMPFILE
