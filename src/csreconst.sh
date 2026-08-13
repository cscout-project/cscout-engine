#!/bin/bash
#
# (C) Copyright 2024 Diomidis Spinellis
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
# Reconstitute source code files from a CScout database.
#

set -eu

# Create a temporary directory and set up a trap for cleanup
temp_dir=$(mktemp -d) || exit 1
# Clean up on exit or signals
trap 'rm -rf "$temp_dir"' EXIT


# Display usage information and exit
usage()
{
  local message="$1"

  cat <<EOF 1>&2
$message
Usage: $(basename $0) -t [-ckqs] database.db
       $(basename $0) -f file [-o] [-b line] [-e line] ... database.db

Test the reconstitution of files in a CScout database

  -t        Test the reconstitution of all files.
            Available options:
  -c        Count correct and incorrect files
  -k        Keep comparing after finding a difference.
  -q        Run a quick diff; do not list the full diff output.
  -s        Provide summary of each file's result.

  -f file   Reconstitute the specified file.
            Available options:
  -b line   Beginning line number.
  -e line   Ending line number.
  -o        Output offset values.

EXAMPLES

Test all files providing a summary.c:
  csreconst -tckqs result.db

Reconstitute file.c:
  csreconst -f file.c result.db
EOF
  exit 1
}

opt_test=''
opt_keep_going=''
opt_quick=''
opt_summary=''
opt_count=''

opt_file=''
opt_begin='0'
opt_end='99999999'
opt_offset=''

# Process command-line arguments
while getopts "tckqsf:b:e:o" opt; do
  case $opt in
    c)
      opt_count=1
      ;;
    b)
      opt_begin="(SELECT foffset FROM linepos CROSS JOIN selected_file AS sf
        WHERE fid=sf.value AND lnum <= $OPTARG LIMIT 1)"
      ;;
    e)
      opt_end="(SELECT foffset FROM linepos CROSS JOIN selected_file AS sf
        WHERE fid=sf.value AND lnum >= $OPTARG LIMIT 1)"
      ;;
    f)
      opt_file="$OPTARG"
      ;;
    k)
      opt_keep_going=1
      ;;
    o)
      opt_offset='foffset, '
      ;;
    q)
      opt_quick=-q
      ;;
    s)
      opt_summary=1
      ;;
    t)
      opt_test=1
      ;;
    \?) # Illegal option
      usage ''
      ;;
    :)
      usage "Option -$OPTARG requires an argument."
      ;;
  esac
done

shift "$((OPTIND-1))"

# Database must be provided
test $# -ne 1 && usage "Database name not specified"
DB="$1"

# Both cannot be set
test -n "$opt_file" -a -n "$opt_test" && usage "-t and -f cannot both be set"

# At least one must be set
test -z "$opt_file" -a -z "$opt_test" && usage "At least one of -t or -f must be set"


# Run diff on the specified path with output controlled by opt_quick
run_diff()
{
  local path="$1"

  if [ -n "$opt_quick" ] ; then
    diff $opt_quick -w $path $temp_dir/reconstructed.c >/dev/null
  else
    diff $opt_quick -w $path $temp_dir/reconstructed.c
  fi
}

# Output the selection query for all elements of the specified file
all_elements()
{
  local fid="$1"

cat <<EOF
  SELECT foffset, name AS s
    FROM ids
    INNER JOIN tokens USING(eid)
    WHERE fid = $fid
  UNION
  SELECT foffset, code AS s
    FROM rest
    WHERE fid = $fid
  UNION
  SELECT foffset, comment AS s
    FROM comments
    WHERE fid = $fid
  UNION
  SELECT foffset, string AS s
    FROM strings
    WHERE fid = $fid
EOF
}

# Fix newlines
fix_nl()
{
  tr -d "\n\r" |
  perl -pe 's/\\u0000d\\u0000a/\n/g;s/\\u0000a/\n/g'
}

# Test reconstitution of all files
test_all()
{
  local nfiles=0
  local same=0
  local different=0
  local result_file="$temp_dir/results"
  local exit_status=0


  sqlite3 "$DB" 'select distinct fid, name from files;' |
  while IFS='|' read fid path ; do
    echo "SELECT s FROM ( $(all_elements $fid) ) ORDER BY foffset;" |
    sqlite3 "$DB" |
      fix_nl >$temp_dir/reconstructed.c

      test -n "$opt_count" && echo files >>$result_file
    if run_diff "$path" ; then
      test -n "$opt_summary" && echo ✓ $path
      test -n "$opt_count" && echo same >>$result_file
    else
      local result=$?
      test -n "$opt_summary" && echo ✗ $path
      test $result -ne 0 -a -z "$opt_keep_going" && exit 1
      test -n "$opt_count" && echo different >>$result_file
    fi
  done

  if [ -n "$opt_count" ] ; then
    awk 'BEGIN {s = d = 0 }
      /same/{s++}
      /different/{d++}
      END {
        print s + d " files "  s " same " d " different";
        exit d != 0
      }' $result_file
    exit_status=$?
  fi

  exit $exit_status
}

# List the contents of a single file
file_list()
{
  cat <<EOF |
WITH selected_file AS (
  SELECT fid AS value FROM files WHERE name='$opt_file'
),
elements AS (
  $(all_elements '(SELECT value from selected_file)')
)
SELECT $opt_offset s
  FROM elements
  WHERE foffset BETWEEN
    $opt_begin
  AND
    $opt_end
  ORDER BY foffset;
EOF
  sqlite3 "$DB" |
  if [ -n "$opt_offset" ] ; then
    cat
  else
    fix_nl
  fi
}

test -n "$opt_test" && test_all
test -n "$opt_file" && file_list "$opt_file"
