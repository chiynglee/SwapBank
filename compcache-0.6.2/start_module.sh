#! /bin/bash
modprobe lzo_compress
modprobe lzo_decompress
if [ $# != "1" ]
then
	insmod ramzswap.ko num_devices=1 disksize_kb=51200
else {
	size=$(expr $1 \* 1024)
	echo swap memory size = $size kb
	insmod ramzswap.ko num_devices=1 disksize_kb=$size
}
fi
