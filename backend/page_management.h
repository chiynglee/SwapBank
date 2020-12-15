#ifndef _PAGE_MANAGEMENT_H
#define _PAGE_MANAGEMENT_H

int bpage_init(void);
int decrease_bpage(unsigned long nr_pages);
int increase_bpage(unsigned long nr_pages);

#endif // _PAGE_MANAGEMENT_H