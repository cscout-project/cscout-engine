#!/bin/bash
#
# Merge the generated database files into one
#

set -eu
set -o pipefail

# Try to limit the damage of a fault leading to a fork bomb
ulimit -u 500 2>/dev/null || true

LIB_DIR=$(dirname $0)/../lib/csmerge
LOG_FILE=csmerge.log

# Log the specified message with a timestamp
log()
{
  echo "$(date '+%Y-%m-%dT%H:%M:%S') $1" >>$LOG_FILE
}

# Create an empty database onto which to consolidate the parts
create_empty()
{
  local name="$1"
  log "Create empty $name"
  rm -f "$name"
  sqlite3 file-0000.db .schema | sqlite3 "$name"
  cat <<\EOF | sqlite3 "$name"
CREATE INDEX IF NOT EXISTS idx_definers_composite ON definers(cuid, basefileid, definerid);
CREATE INDEX IF NOT EXISTS idx_filemetrics_composite ON filemetrics(fid, precpp);
CREATE INDEX IF NOT EXISTS idx_files_name ON files(name);
CREATE INDEX IF NOT EXISTS idx_functions_id ON functions(id);
CREATE INDEX IF NOT EXISTS idx_includers_composite ON includers(cuid, basefileid, includerid);
CREATE INDEX IF NOT EXISTS idx_inctriggers_composite ON inctriggers(cuid, basefileid, definerid, foffset, len);
CREATE INDEX IF NOT EXISTS idx_tokens_eid ON tokens(eid);
CREATE INDEX IF NOT EXISTS idx_tokens_foffset ON tokens(foffset);


CREATE TABLE fileid_to_global_map(
  dbid INTEGER,         -- Unmerged database identifier
  fid INTEGER,          -- File identifier in an unmerged database
  global_fid INTEGER,    -- Corresponding fid used across all databases
  PRIMARY KEY(dbid, fid)
);
CREATE INDEX idx_fileid_to_global_map ON fileid_to_global_map(global_fid);

CREATE TABLE functionid_to_global_map(
  dbid INTEGER,         -- Unmerged database identifier
  id INTEGER,           -- Function identifier in an unmerged database
  global_id INTEGER,     -- Corresponding id used across all databases
  PRIMARY KEY(dbid, id)
);
CREATE INDEX idx_functionid_to_global_map ON functionid_to_global_map(global_id);
EOF
}

# Obtain a new database id by atomically incrementing the corresponding counter
get_dbid()
{
    (
        # Open the counter file in read/write mode and obtain a lock
        flock -x 200

        counter=$(<"$DBID_FILE")
        counter=$((counter + 1))
        echo $counter | tee "$DBID_FILE"
    ) 200<> "$DBID_FILE"
}

# Configure SQLite for fast operation, time logging, fast failure.
sqllite_config()
{
  cat <<\EOF
-- Configure fast SQLite operation without any durability guarantees.
PRAGMA journal_mode = MEMORY;
PRAGMA synchronous = OFF;
PRAGMA temp_store = MEMORY;
PRAGMA locking_mode = EXCLUSIVE;
PRAGMA cache_size = -80000;        -- ~80 MB cache in memory
PRAGMA mmap_size = 268435456;      -- 256 MB memory-mapped I/O
PRAGMA foreign_keys = OFF;         -- disable FK checks if not needed
PRAGMA wal_autocheckpoint = 0;     -- disable WAL checkpoints

-- Exit on errors
.bail on

-- Time issued commands
.timer on
EOF
}

merge_onto()
{
  local dest="$1"
  local source="$2"

  # Obtain the unique identifier for the database being merged,
  # for example, /tmp/csmerge.o48j/temp-6.db or file-0007.db.
  local dbid=$(
    basename $source |
      sed -E 's/^(file|temp)-([0-9]+)\.db$/\2/'
    )

  log "DB $dbid: BEGIN merge onto $dest $source"

  # Order matters here
  # eclasses populates: tokens, ids, functionid, functionid_to_global_map,
  # idproj.
  # We assume that projects are defined with the same names and order
  # in all shards.  This is the way csshard works.  So there's nothing to do
  # for the projects table.
  for i in \
    fileid_to_global_map.sql \
    files.sql \
    eclasses.sql \
    filemetrics.sql \
    definers.sql \
    includers.sql \
    providers.sql \
    inctriggers.sql \
    functions.sql \
    functiondefs.sql \
    functionmetrics.sql \
    fcalls.sql \
    fileproj.sql \
    lineproj.sql \
    comments.sql \
    strings.sql \
    rest.sql \
    linepos.sql
   do
     set -eu
     set -o pipefail
     {
       log "DB $dbid: running $i"

       # Disable all durability guarantees
       sqllite_config

       # Attach database to be merged.
       echo "ATTACH DATABASE '$source' AS adb;"

       # Configure script as needed
       sed "
         # Replace hard-coded database id 5 used for testing.
         s/\\([- ]\\)5/\\1xyzzy$dbid/g
         # Used to avoid having \10001 as a back-reference
         s/xyzzy//g

         # Replace ././ with $TEMP_DIR/.
         s|\./\./|$TEMP_DIR/|g

         # Keep temporary files if -k has been specified.
         ${DELETE_RM:-}
       " "$LIB_DIR/$i"
     } |
     sqlite3 "$dest" 2>&1 |
     while read -r line ; do log "DB $dbid: $line" ; done
  done

  if [[ ${source##*/} == temp-*.db && -z ${KEEP:-} ]]; then
    log "DB $dbid: delete $source"
    rm -f $source
  fi
  log "DB $dbid: END merge onto $dest $source"
}

merge()
{
  local files=("$@")

  if [ "${#files[@]}" -eq 2 ]; then
    output="$TEMP_DIR/temp-$(get_dbid).db"

    create_empty "$output"
    merge_onto "$output" "${files[0]}"
    merge_onto "$output" "${files[1]}"
    echo $output
    return
  fi

  midpoint=$((${#files[@]} / 2))

  local left=("${files[@]:0:midpoint}")
  local left_output=$(mktemp $TEMP_DIR/out.XXXXXX)
  merge "${left[@]}" >$left_output &
  pid_left=$!

  local right=("${files[@]:midpoint}")
  local right_output=$(mktemp $TEMP_DIR/out.XXXXXX)
  merge "${right[@]}" >$right_output &
  pid_right=$!

  wait $pid_left
  left_output_db=$(<$left_output)
  rm $left_output

  wait $pid_right
  right_output_db=$(<$right_output)
  rm $right_output

  merge_onto "$left_output_db" "$right_output_db"
  echo $left_output_db
}

# Create the final result
optimize_result()
{
  log "Finished merging into $result. Continuing with optimization."

  rm -f "$MERGED"

  cat <<EOF | sqlite3 "$result"
DROP TABLE fileid_to_global_map;
DROP TABLE functionid_to_global_map;

ATTACH 'file-0000.db' as adb;
INSERT INTO projects SELECT * FROM adb.projects;

ALTER TABLE filemetrics ADD COLUMN iscscout BOOLEAN;

UPDATE filemetrics SET iscscout = (
  SELECT (name LIKE '%.cs')
    FROM files
    WHERE filemetrics.fid = files.fid
);

CREATE TABLE cscout_files AS
  SELECT fid FROM files
  WHERE name LIKE '%.cs' OR name LIKE '%/csmake-%-defs.h';

VACUUM INTO '$MERGED';
ANALYZE;
EOF

  log "Finished vacuuming and optimizing $MERGED"
  log "Time taken (self u/s, children u/s):"
  # This cannot be put in log $(), because then it will report 0.
  times >>$LOG_FILE

  test -z "${KEEP:-}" && rm "$result"
}

# Display usage information and exit
usage()
{
  cat <<EOF 1>&2
Usage: $(basename $0) [OPTION] nfiles merged.db

  -k        Keep temporary files.
  -l file   Specify log file name (default csmerge.log).
  -T dir    Specify temporary directory to use (default \$TMPDIR, /tmp).
EOF
  exit 1
}

# Process command-line arguments
while getopts "kl:T:" opt; do
  case $opt in
    k)
      KEEP=1
      ;;
    l)
      LOG_FILE="$OPTARG"
      ;;
    T)
      TEMP_DIR_LOCATION="$OPTARG"
      ;;
    \?) # Illegal option
      usage
      ;;
    :)
      echo "Option -$OPTARG requires an argument." >&2
      usage
      ;;
  esac
done

# Create temporary directory in specified location, or $TMPDIR, or /tmp.
TEMP_DIR=$(mktemp -d --tmpdir=${TEMP_DIR_LOCATION:-} csmerge.XXXXXX)
export TEMP_DIR

DBID_FILE="$TEMP_DIR/dbid.txt"

if [ -z "${KEEP:-}" ] ; then
  # Clean up on exit or signals
  trap 'rm -rf "$TEMP_DIR"' 0 1 2 3 15
else
  # Sed command to delete SQLLite command that removes temporary files
  DELETE_RM='/^\.shell rm/d'
fi

shift "$((OPTIND-1))"

if [ $# -ne 2 ] ; then
  usage
fi

NFILES="$1"
MERGED="$2"

case "$NFILES" in
  ''|*[!0-9]*)
    echo "Error: nfiles must be a positive integer." 1>&2
    usage
    ;;
esac

if [ "$NFILES" -lt 2 ] ; then
  echo "Error: nfiles must be at least 2." 1>&2
  usage
fi

# Get dbids from N+1 onward. 1-N are the databases to merge.
echo $((NFILES + 1)) >$DBID_FILE

:>$LOG_FILE

# Create array of files to merge
files=($(seq 0 $(($NFILES - 1)) | xargs -n 1 printf 'file-%04d.db '))

result=$(merge "${files[@]}")

optimize_result

if [ -n "${KEEP:-}" ] ; then
  echo "Temporary files left on $TEMP_DIR" 1>&2
fi
