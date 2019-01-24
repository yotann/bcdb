#!/bin/sh

# compare-functions.sh: Opens an SQLite-backed BCDB and diffs all pairs of
# functions with the same name but different contents.

set -e

DB=$1
TMPDIR=$(mktemp -d)
trap "rm -r $TMPDIR" EXIT

if ! [ -f "$DB" ]; then
  echo "Missing database"
  exit 1
fi

dumpmodule() {
  ID=$1
  sqlite3 -readonly $DB "SELECT writefile('/dev/fd/3',content) FROM blob WHERE vid = $ID" 3>&1 1>/dev/null | llvm-dis
}

sqlite3 -readonly $DB <<EOF |
.separator " "
SELECT h0.name, h1.name, mm0.value, mm1.value, mm0.key
FROM head AS h0, map AS m0, map AS mm0, head AS h1, map AS m1, map AS mm1
WHERE h1.vid > h0.vid
AND h0.vid = m0.vid AND m0.key = "functions" AND m0.value = mm0.vid
AND h1.vid = m1.vid AND m1.key = "functions" AND m1.value = mm1.vid
AND mm0.key = mm1.key AND mm0.value != mm1.value
EOF
while read LINE
do
  set -- $LINE
  HEAD0=$1
  HEAD1=$2
  ID0=$3
  ID1=$4
  KEY=$5
  dumpmodule $ID0 > $TMPDIR/module0.ll
  dumpmodule $ID1 > $TMPDIR/module1.ll
  diff -su --label "$HEAD0/$KEY" --label "$HEAD1/$KEY" $TMPDIR/module{0,1}.ll || [ $? -eq 1 ]
done
