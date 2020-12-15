#! /bin/bash
modprobe lzo_compress
modprobe lzo_decompress

if [ $# != "1" ]
then
	insmod shcomp_domu.ko num_devs=1 size_kb=51200
else {
	size=$(expr $1 \* 1024)
	echo swap memory size = $size kb
	insmod shcomp_domu.ko num_devs=1 size_kb=$size
}
fi
