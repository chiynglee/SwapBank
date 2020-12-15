#!/bin/bash

echo "End the timer for measurement"

cat /proc/measure_exit

echo "create the result file (./result/res_free_pgcnt)"
./read_result

