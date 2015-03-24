#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
int main() {
	int fd = open("a", O_RDWR);
	void *buf=malloc(100);
	int n=write(fd, buf, 1);
	printf("%s\n",(char*)buf);
	close(fd);
}
