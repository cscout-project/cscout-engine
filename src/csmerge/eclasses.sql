-- Merge elements that are identified by equivalence class (EC) ids (eids).
-- This requires running CScout's token homogenization alrogithm
-- by writing out tables identifying ECs by fid/foffset, invoking CScout
-- to merge them, and then reading them back.
--
-- When the script is run from merge.sh the ././ sequence gets replaced
-- with $TMPDIR and 5 with the appropriate database identifier.

-- Remove any temporary output files, so that tests won't read previous ones
-- if CScout fails and the script bails out early.
.shell rm -f ././command-status-5.txt ././new-eclasses-5.csv ././new-ids-5.csv ././new-functionid-5.csv ././new-idproj-5.csv ././functionid-to-global-map-5.csv

-- A map from eids to an arbitrary identifying tokid

CREATE TEMP TABLE eid_to_tokid_map(
  eid INTEGER PRIMARY KEY,-- Equivalence class identifier in an unmerged database
  fid INTEGER,          -- File id representing eid
  foffset INTEGER       -- File offset representing eid
);

WITH ranked_rows AS (
  SELECT eid, fid, foffset,
      ROW_NUMBER() OVER (PARTITION BY eid ORDER BY (SELECT NULL)) AS rn
  FROM tokens
)
INSERT INTO eid_to_tokid_map
  SELECT eid, fid, foffset
    FROM ranked_rows
    WHERE rn = 1;

-- Same for attached database
CREATE TEMP TABLE aeid_to_tokid_map(
  eid INTEGER PRIMARY KEY,-- Equivalence class identifier in an unmerged database
  fid INTEGER,          -- File id representing eid
  foffset INTEGER       -- File offset representing eid
);

WITH ranked_rows AS (
  SELECT eid, fid, foffset,
      ROW_NUMBER() OVER (PARTITION BY eid ORDER BY (SELECT NULL)) AS rn
  FROM adb.tokens
),
eid_sentinel_tokens AS (
  SELECT eid, fid, foffset
    FROM ranked_rows
    WHERE rn = 1
)
INSERT INTO aeid_to_tokid_map
  SELECT eid, fid_map.global_fid AS fid, foffset
    FROM eid_sentinel_tokens AS est
    INNER JOIN fileid_to_global_map AS fid_map
      ON fid_map.dbid = 5 AND fid_map.fid = est.fid;

.separator ' '
-- Start with the attached database where Eclasses are created more efficiently.
-- When creating new databases the attached is by definition larger than the
-- (empty) original one.
-- This is read by read_eclasses_attached.
.output ././eclasses-a-5.txt
SELECT fid_map.global_fid AS fid, foffset, length(name) AS len, eid
  FROM adb.tokens AS at
  INNER JOIN fileid_to_global_map AS fid_map
    ON fid_map.dbid = 5 AND fid_map.fid = at.fid
  LEFT JOIN adb.ids USING(eid)
  ORDER BY eid;

-- Continue with the original classes, which must be processed twice.
-- This is read by read_eclasses_original.
.output ././eclasses-o-5.txt
SELECT fid, foffset, length(name) AS len, eid
  FROM tokens
  LEFT JOIN ids USING(eid)
  ORDER BY eid;

.output ././ids-5.txt
SELECT 0 AS dbid, fid, foffset, ids.*
  FROM ids
  INNER JOIN eid_to_tokid_map USING(eid);

SELECT 5 AS dbid, fid, foffset, ai.*
  FROM adb.ids AS ai
  INNER JOIN aeid_to_tokid_map USING(eid);

-- Separate file for the attached functionids
.output ././functionid-a-5.txt
  -- Ordinal implied by position
  SELECT 5 AS dbid,
        afunctionid.functionid,
        fid_map.global_fid,
        afunctionid.foffset,
        afunctionid.len,
        COALESCE(f.name, '')
    FROM adb.functionid AS afunctionid
    LEFT JOIN adb.functions AS f
      ON f.id = afunctionid.functionid
    INNER JOIN fileid_to_global_map AS fid_map
      ON fid_map.dbid = 5 AND fid_map.fid = afunctionid.fid
    ORDER BY functionid, ordinal;

-- Continue with the original functionids
.output ././functionid-o-5.txt
  SELECT 0 AS dbid,
        functionid.functionid,
        functionid.fid,
        functionid.foffset,
        functionid.len,
        COALESCE(f.name, '')
    FROM functionid
    LEFT JOIN functions AS f
      ON f.id = functionid.functionid
    ORDER BY functionid, ordinal;

.output ././idproj-5.txt
  SELECT etm.fid,
        etm.foffset,
        length(ids.name) AS len,
        pid
    FROM adb.idproj AS idproj
    INNER JOIN aeid_to_tokid_map AS etm USING(eid)
    LEFT JOIN adb.ids USING(eid);

  SELECT etm.fid,
        etm.foffset,
        length(ids.name) AS len,
        pid
    FROM idproj
    INNER JOIN eid_to_tokid_map AS etm USING(eid)
    LEFT JOIN ids USING(eid);

.output stdout

-- Invoke CScout to merge and unify the output elements.
-- *-o-.txt files are from the original database and *-a-.txt files
-- are from the attached database.
-- Write out exit code to verify it.  (SQLite's .bail doesn't fail
-- on non-zero shell commands.)  Avoid $? as this doesn't work reliably.
.shell sh -c 'if cscout -M ././eclasses-a-5.txt ././eclasses-o-5.txt ././ids-5.txt ././functionid-a-5.txt ././functionid-o-5.txt ././idproj-5.txt ././new-eclasses-5.csv ././new-ids-5.csv ././new-functionid-5.csv ././new-idproj-5.csv ././functionid-to-global-map-5.csv ; then echo 0 ; else echo 1 ; fi >././command-status-5.txt'

-- Due to integrity check this will fail and bail out on failures
.import ././command-status-5.txt command_status

DELETE FROM tokens;
DELETE FROM ids;
DELETE FROM functionid;
DELETE FROM idproj;
DELETE FROM functionid_to_global_map;

.mode csv
.import ././new-eclasses-5.csv tokens
.import ././new-ids-5.csv ids
.import ././new-functionid-5.csv functionid
.import ././new-idproj-5.csv idproj
.import ././functionid-to-global-map-5.csv functionid_to_global_map
.mode list

-- Remove temporary files
.shell rm ././command-status-5.txt ././eclasses-a-5.txt ././eclasses-o-5.txt ././ids-5.txt ././functionid-a-5.txt ././functionid-o-5.txt ././idproj-5.txt ././new-eclasses-5.csv ././new-ids-5.csv ././new-functionid-5.csv ././new-idproj-5.csv ././functionid-to-global-map-5.csv
-- Drop temporary tables
DROP TABLE aeid_to_tokid_map;
DROP TABLE eid_to_tokid_map;
