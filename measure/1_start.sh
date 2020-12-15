#!/bin/bash

if [ ! -c /dev/expr_result ]
then
	echo "/dev/expr_result was not found."

	mknod /dev/expr_result c 240 32

	echo "/dev/expr_result created."
fi

echo "Start the timer for measurement"

echo 1 > /proc/measure_exit

