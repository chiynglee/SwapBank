#include <stdio.h>
//#include <string.h>
//#include <sys/types.h>
//#include <sys/stat.h>
//#include <sys/ioctl.h>
#include <fcntl.h>
//#include <unistd.h>

#define DEVICE_FILENAME  "/dev/expr_result"

#define RESULT_FILE_NAME "result/res_free_pgcnt"

int main(){
	int dev, ret;	//device number, return value
	char buf[60000];
	FILE *fp;

	//open device file
	dev = open(DEVICE_FILENAME, O_RDWR|O_NDELAY);
	if(dev < 0){
		printf("dev open error\nmake dev file (mknod /dev/mydev c 240 32) or insmod module (insmod skel_dev.ko)\n");
		return 1;
	}
	
	read(dev, buf, 0);

	ret = close(dev);

	fp = fopen(RESULT_FILE_NAME, "w");

	fprintf(fp, "%s", buf);

	fclose(fp);

	printf("closing dev file... ret: %d\n", ret);

	return 0;
}

