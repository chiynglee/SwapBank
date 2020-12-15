#ifndef _MEASURE_PAGECNT_H_
#define _MEASURE_PAGECNT_H_

#include <linux/timer.h>
#include <linux/proc_fs.h>
#include <linux/buffer_head.h>

#include <asm/atomic.h>
#include <asm/uaccess.h>

//#define MODEXIT 1

#define FREQ_MSEC 100

//#define MAXIMUM_WAIT_TIME_MSEC 60000
//#define MAXIMUM_WAIT_CNT (MAXIMUM_WAIT_TIME_MSEC / FREQ_MSEC)

#define EXFLAG_PROC_FILE_NAME "measure_exit"
//#define FREE_PGCNT_FILE_NAME "free_pgcnt"

void write_free_page_cnt(unsigned long data);
void start_free_pgcnt_mod(void);
void end_free_pgcnt_mod(void);

// For a proc file
void create_flag_in_proc(void);
void delete_flag_in_proc(void);

// For a device driver
int create_char_dev(void);
void remove_char_dev(void);

// For a file I/O
/*struct file* file_open(const char* path, int flags, int rights);
void file_close(struct file* file);
int file_read(struct file* file, unsigned long long offset, unsigned char* data, unsigned int size);
int file_write(struct file* file, unsigned long long offset, unsigned char* data, unsigned int size);
*/
#endif
