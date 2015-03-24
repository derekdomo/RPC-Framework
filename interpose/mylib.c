#define _GNU_SOURCE

#include <dlfcn.h>
#include <stdio.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <err.h>
#include <string.h>
 
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdarg.h>

int (*orig_close)(int handle);
// Pass the name of the function to the server
int clientProcess(char *func) {
	char *serverip;
	char *serverport;
	unsigned short port;
	int sockfd, rv;
	struct sockaddr_in srv;

	serverip = getenv("server15440");
	if (serverip) {
	}
	else
		serverip = "127.0.0.1";
	serverport = getenv("serverport15440");
	if (serverport) {
	}
	else {
		serverport = "15440";
	}
	port = (unsigned short)atoi(serverport);
	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd<0) err(1, 0);
	memset(&srv, 0, sizeof(srv));
	srv.sin_family = AF_INET;
	srv.sin_addr.s_addr = inet_addr(serverip);
	srv.sin_port = htons(port);
	
	rv = connect(sockfd, (struct sockaddr*)&srv, sizeof(struct sockaddr));
	if (rv<0) err(1,0);
	send(sockfd, func, strlen(func), 0);	
	orig_close(sockfd);
	return 1;
}
// The following line declares a function pointer with the same prototype as the open function.  
int (*orig_open)(const char *pathname, int flags, ...);  // mode_t mode is needed when flags includes O_CREAT

// This is our replacement for the open function from libc.
int open(const char *pathname, int flags, ...) {
	// we just print a message, then call through to the original open function (from libc)
	fprintf(stderr, "mylib: open called for path %s\n", pathname);
	
	int res;
	res = clientProcess("open");
	printf("finish open");
	return res;
}
// This is the replacement for the read function from libc
ssize_t read(int handle, void *buffer, size_t byte) {
	fprintf(stderr, "mylib: read called for handler %d\n", handle);
	int res;
	res = clientProcess("read");
	return res;
}
// This is the replacement for the close function from libc
int close(int handle) {
	fprintf(stderr, "mylib: close called for handler %d\n", handle);
	int res;
	res = clientProcess("close");
	return res;
}

// This is the replacement for the close function from libc
ssize_t getdirentries(int fd, char *buf, size_t nbytes , off_t *basep) {
	fprintf(stderr, "mylib: getdirentries called\n");
	int res;
	res = clientProcess("getdirentries");
	return res;
}

// This function is automatically called when program is started
void _init(void) {
	// set function pointer orig_open to point to the original open function
	orig_open = dlsym(RTLD_NEXT, "open");
	orig_close = dlsym(RTLD_NEXT, "close");
	fprintf(stderr, "Init mylib\n");
}


