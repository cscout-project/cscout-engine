-- Create empty idproj tables for tests where they aren't needed

CREATE TABLE IDPROJ(
  EID BIGINT,
  PID INTEGER
);

CREATE TABLE adb.IDPROJ(
  EID BIGINT,
  PID INTEGER
);

-- Also create the required command status table
CREATE TEMP TABLE command_status(status INTEGER NOT NULL CHECK (status = 0));
