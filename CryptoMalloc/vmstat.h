/* This code checks if memory page is readable, writable using syscall*/
#ifndef _VMSTAT_
#define _VMSTAT_
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

static int fd = -2;

int check_read(void *address) {
	int stat = PROT_NONE;
	if (fd == -2)
		fd = open("/dev/null", O_RDWR);
	if (fd < 0)
		return -1;
	if (write(fd, address, 1))
		return stat;
	return PROT_READ;
}

// this will only check one page!, function implies that write also means read,
// hence it is not portable
int vmstat(void *address) {
	int stat = PROT_NONE;
	if (check_read(address) == PROT_NONE) {
		return stat;
	}
	stat |= PROT_READ;
	// here comes pure evil, the memory should be readable, however it can be
	// mprotected() while this is running, this function is by no means atomic
	char address_contents = *(char *)address;
	if (read(fd, address, 1))
		return stat;
	stat |= PROT_WRITE;
	*(char *)address = address_contents;
	return stat;
}

#endif //_VMSTAT_
