#!/bin/bash
rm -r ExecutorTest_db
rm output.txt
cp -r ExecutorTest_db_task2and3 ExecutorTest_db
cat input_orderbytest.sql | while read line
do
    if [ ${#line} -eq 0 ] || [ ${line:0:1} == "#" ]
    then
        echo "$line"
        continue
    fi
    echo ">> $line"
    ../../build/bin/exec_sql "$line"
    echo "------------------------------"
done | tee -a output.txt
