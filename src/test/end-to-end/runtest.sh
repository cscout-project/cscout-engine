#!/bin/sh
#
# End-to-end test of csmake, cssplit, csmerge, csreconst
#

set -eu

EXAMPLE=awk
TEST_DIR=$(pwd)
SRC_DIR="$TEST_DIR/../.."
SHARDS=4


# Setup a suitable testing environment
rm -rf $EXAMPLE bin include lib
(cd "$SRC_DIR" ; ./dest-install.sh "$TEST_DIR")
ln -s "$SRC_DIR/build/cscout" bin/
export CSCOUT_HOME="$TEST_DIR/include/cscout"
PATH="$TEST_DIR/bin:$PATH"

cp -r ../../../example/awk $EXAMPLE

cd $EXAMPLE

echo "Create make.cs by spying on the make process"
csmake

echo "Split the file into four CScout shards"
cssplit -s $SHARDS make.cs

echo "Process the CScout files (normally this is done in parallel)"
rm -f file-*.db

for i in *.cs ; do
  cscout -s sqlite $i | sqlite3 $(basename $i .cs).db
done

echo "Merge the databases into one"
csmerge $SHARDS make.db

echo "Reconstitute the files in the merged database"
csreconst -tcks make.db

cd ..
rm -rf $EXAMPLE bin include lib
