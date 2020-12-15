#include <linux/module.h>
#include "page_cnt.h"

#define CALL_DEV_NAME "expr_result"
#define CALL_DEV_MAJOR 240

int write_to_proc(struct file *file, const char __user *buffer,
			   unsigned long count, void *data);
int read_from_proc(char *buf, char **start, off_t off,
			  int count, int *eof, void *data);

static long call_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);
static ssize_t call_read(struct file *filp, char *buf, size_t count, loff_t *f_pos);
static ssize_t call_write(struct file *filp, const char *buf, size_t count, loff_t *f_pos);
static int call_open(struct inode *inode, struct file *filp);
static int call_release(struct inode *inode, struct file *filp);

struct timer_list freq_timer;

struct proc_dir_entry *proc_fp;
char exit_flag[16];

//int wait_cnt;
//unsigned long prev_flag;

long measure_res[32768];
int res_cnt = 0;
long prev_nr_free_pg = 0;


// callback function for timer
void write_free_page_cnt(unsigned long data)
{
/*	char buf[256];
	unsigned long flag = 0;
	unsigned int len = 0;

	if((flag = simple_strtoul(exit_flag, NULL, 10)) == MODEXIT) {
		if(prev_flag != flag || wait_cnt >= MAXIMUM_WAIT_CNT) {
			printk("Measurement : MODEXIT flag was on. (%ld)\n", flag);
			printk("Measurement : No more input to change the exit_flag.\n");
			file_close(fd);
			return ;
		}
	}
	else {
		len = sprintf(buf, "%ld\n", atomic_long_read(&vm_stat[NR_FREE_PAGES]));
		file_write(fd, 0, buf, len);
	}

	prev_flag = flag;

	if(mod_timer(&freq_timer, jiffies+msecs_to_jiffies(FREQ_MSEC))) {
		printk("Measurement : Error in mod_timer()\n");
		file_close(fd);
	}
*/
	long nr_free_pg;

	nr_free_pg = atomic_long_read(&vm_stat[NR_FREE_PAGES]);
	if(nr_free_pg != prev_nr_free_pg) {
		measure_res[res_cnt++] = nr_free_pg;
		prev_nr_free_pg = nr_free_pg;
	}

	if(mod_timer(&freq_timer, jiffies+msecs_to_jiffies(FREQ_MSEC))) {
		printk("Measurement : Error in mod_timer()\n");
	}
}

void start_free_pgcnt_mod(void)
{
	create_char_dev();
	create_flag_in_proc();
}

void end_free_pgcnt_mod(void)
{
//	del_timer(&freq_timer);
	delete_flag_in_proc();
	remove_char_dev();
//	file_close(fd);
}

int write_to_proc(struct file *file, const char __user *buffer,
			   unsigned long count, void *data)
{
	char buf[16];

	// write the value by echo to proc
	if(copy_from_user(buf, buffer, count)) {
		return -EFAULT;
	}
	strncpy(data, buf, count);

	// start the timer
	printk("Measurement : Start timer\n");

	res_cnt = 0;
	prev_nr_free_pg = 0;

	setup_timer(&freq_timer, write_free_page_cnt, 0);	
	mod_timer(&freq_timer, jiffies+msecs_to_jiffies(FREQ_MSEC));

	return count;
}

/*
	for(i=0; i<32768;i++) {
		sprintf(buf, "%09d\n", result[i]);
		buf = buf+10;
	}

*/

int read_from_proc(char *buf, char **start, off_t off,
			  int count, int *eof, void *data)
{
//	struct file *fd;
//	int i;
//	char res_buf[16];
//	int len;

	// read the value by cat
	strncpy(buf, data, count);
	*eof = 1;

	// stop the timer
	del_timer_sync(&freq_timer);
	printk("Measurement : End timer\n");

	// create a file and write the results to the file
/*	fd = file_open(FREE_PGCNT_FILE_NAME, O_RDWR, 0);

	for(i=0; i<res_cnt; i++) {
		len = sprintf(res_buf, "%ld\n", measure_res[i]);
		file_write(fd, 0, res_buf, len);
	}

	file_close(fd);
*/
	return count;
}

void create_flag_in_proc(void)
{
//	wait_cnt = 0;
//	prev_flag = MODEXIT;
//	sprintf(exit_flag, "%d", MODEXIT);

	proc_fp = create_proc_entry(EXFLAG_PROC_FILE_NAME, S_IFREG|S_IRWXU, NULL);
	if(proc_fp) {
		proc_fp->data = exit_flag;
		proc_fp->read_proc = read_from_proc;
		proc_fp->write_proc = write_to_proc;
	}
}

void delete_flag_in_proc(void)
{
	remove_proc_entry(EXFLAG_PROC_FILE_NAME, NULL);
}

// -------- device driver to create a result file using cat ---------------

static long call_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	return 0;
}

static ssize_t call_read(struct file *filp, char *buf, size_t count, loff_t *f_pos)
{
	int i;
	int len = 0;
	char *buf_addr = buf;
	ssize_t total_len = 0;
	
	for(i=0; i<res_cnt; i++) {
		len = sprintf(buf_addr, "%ld\n", measure_res[i]);
		buf_addr += len;
		total_len += len;
	}

	return total_len;
}

static ssize_t call_write(struct file *filp, const char *buf, size_t count, loff_t *f_pos)
{
	return 0;
}

//device open call
static int call_open(struct inode *inode, struct file *filp)
{
	return 0;
}

//device release call
static int call_release(struct inode *inode, struct file *filp)
{
	return 0;
}

//connect each device call to specific function
struct file_operations call_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = call_ioctl,
	.read = call_read,
	.write = call_write,
	.open = call_open,
	.release = call_release,
};

int create_char_dev(void)
{
	int result;
	
	//register char device
	result = register_chrdev(CALL_DEV_MAJOR, CALL_DEV_NAME, &call_fops);

	if (result < 0){
		printk("fail to init module: %d\n", result);
	}

	return result;
}

void remove_char_dev(void){
	unregister_chrdev(CALL_DEV_MAJOR, CALL_DEV_NAME);
}

int measure_init(void)
{
	start_free_pgcnt_mod();

	return 0;
}

static void __exit measure_exit(void)
{
	end_free_pgcnt_mod();
}

module_init(measure_init);
module_exit(measure_exit);

MODULE_LICENSE("Dual BSD/GPL");


/*
struct file* file_open(const char* path, int flags, int rights) {
	struct file* filp = NULL;
	mm_segment_t oldfs;
	int err = 0;

	oldfs = get_fs();

	set_fs(get_ds());
	filp = filp_open(path, flags, rights);

	set_fs(oldfs);

	if(IS_ERR(filp)) {
		err = PTR_ERR(filp);
		return NULL;
	}

	return filp;
}

void file_close(struct file* file) {
	if(file)
		filp_close(file, NULL);
}

int file_read(struct file* file, unsigned long long offset, unsigned char* data, unsigned int size)
{
	mm_segment_t oldfs;
	int ret;

	oldfs = get_fs();
	set_fs(get_ds());

	ret = vfs_read(file, data, size, &offset);

	set_fs(oldfs);
	
	return ret;
}

int file_write(struct file* file, unsigned long long offset, unsigned char* data, unsigned int size) {
	mm_segment_t oldfs;
	int ret;

	oldfs = get_fs();
	set_fs(get_ds());

	ret = vfs_write(file, data, size, &offset);

	set_fs(oldfs);

	return ret;
}
*/
