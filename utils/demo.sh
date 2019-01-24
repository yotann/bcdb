#!/bin/sh

# demo.sh: Creates a new database demo.db, adds modules to it, and prints out
# how many unique functions are being added.

set -e

BCDB=bin/bcdb

rm -f demo.db
$BCDB init -uri sqlite:demo.db
echo file,total_functions,unique_functions,input_bytes,db_bytes,blob_bytes > demo.csv

CUMINPUTSIZE=0
for F in $@
do
  CUMINPUTSIZE=$(($CUMINPUTSIZE+$(stat -c%s $F)))

  printf "Adding %-22s" "$F..."
  $BCDB add -uri sqlite:demo.db "$F"

  DBSIZE=$(stat -c%s demo.db)
  BLOBSIZE=$(sqlite3 demo.db 'SELECT SUM(LENGTH(content)) FROM blob')
  ALLFUNCS=$(sqlite3 demo.db 'SELECT SUM((SELECT COUNT(*) FROM map AS m WHERE m.vid = map.value)) FROM map WHERE key = "functions"')
  UNIQFUNCS=$(sqlite3 demo.db 'SELECT COUNT(DISTINCT m2.value) FROM map AS m1, map AS m2 WHERE m1.key = "functions" AND m1.value = m2.vid')
  PERCUNIQ=$(bc <<<"scale=1; 100*$UNIQFUNCS/$ALLFUNCS")

  echo $F,$ALLFUNCS,$UNIQFUNCS,$CUMINPUTSIZE,$DBSIZE,$BLOBSIZE >> demo.csv
  printf "%6d total functions, %5d unique (%s%%)\n" $ALLFUNCS $UNIQFUNCS $PERCUNIQ
  gnuplot demo.gnuplot >/dev/null 2>&1 && mv demo.tmp.png demo.png
done
