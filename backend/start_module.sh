#! /bin/bash

if [ $# != "1" ]
then
	insmod shcomp_dom0.ko size_kb=51200
else {
	size=$(expr $1 \* 1024)
	echo swap memory size = $size kb
	insmod shcomp_dom0.ko size_kb=$size
}
fi

