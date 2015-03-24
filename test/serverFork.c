#include <stdio.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <string.h>
#include <unistd.h>
#include <err.h>
#include <errno.h>
#include <string.h>
#include <dirtree.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <dirent.h>
#include <pthread.h>

#define MAXMSGLEN 2000

int processOpen(char *param, int len) {
	char *pathname;
	int flags;
	mode_t m=0;
	//skip the function name
	param+=5;
	//assign the pathname
	pathname=param;
	param+=strlen(param)+1;
	//assign the flags
	flags = *(int*)param;
	param+=sizeof(int)+1;
	//assign the mode_t
	m=*(mode_t*)param;	
	return open(pathname, flags, m);
}
ssize_t processWrite(char *param, int len) {
	int fildes;
	void* buf;
	size_t nbytes;
	//skip the function name
	param+=6;
	//assign the file descriptor
	fildes=*(int*)param;
	param+=5;
	//assign the content to write
	nbytes=*(size_t*)param;
	param+=1+sizeof(size_t);
	buf=(void*)param;
	return write(fildes, buf, nbytes);	
}
int processClose(char *param, int len) {
	int handle;
	//skip the function name
	param+=6;
	handle=*(int*)param;
	return close(handle);	
}
ssize_t processRead(int handle, void* buf, size_t byte) {
	return read(handle, buf, byte);	
}
int processUnlink(char *param) {
	// skip the function name
	param+=7;
	return unlink(param);
}
int processStat(char *param, struct stat* ret) {
	param+=5;
	return stat(param, ret);
}
int processXstat(char *param, struct stat* ret) {
	param+=8;
	int ver = *(int*)param;
	param+=5;
	return __xstat(ver, param, ret);
}
int processLseek(char *param) {
	param+=6;
	int fd=*(int*)param;
	param+=5;
	off_t offset=*(off_t*)param;
	param+=sizeof(off_t);
	int whence=*(int*)param;
	return lseek(fd, offset, whence);
}
int processGetdirentries(char* param, char* buf) {
	param+=14;
	int fd=*(int*)param;
	param+=5;
	size_t nbytes=*(size_t*)param;
	param+=5;
	memcpy(buf, param, 4);
	char* ptr=param;
	ptr+=5;
	return getdirentries(fd, ptr, nbytes, (off_t*)buf);	
}
void *connection_handler(void *);
int main(int argc, char**argv) {
	char *serverport;
	unsigned short port;
	int sockfd, sessfd, rv, i;
	struct sockaddr_in srv, cli;
	socklen_t sa_size;
	
	// Get environment variable indicating the port of the server
	serverport = getenv("serverport15440");
	if (serverport) port = (unsigned short)atoi(serverport);
	else port=15440;
	
	// Create socket
	sockfd = socket(AF_INET, SOCK_STREAM, 0);	// TCP/IP socket
	if (sockfd<0) err(1, 0);			// in case of error
	
	// setup address structure to indicate server port
	memset(&srv, 0, sizeof(srv));			// clear it first
	srv.sin_family = AF_INET;			// IP family
	srv.sin_addr.s_addr = htonl(INADDR_ANY);	// don't care IP address
	srv.sin_port = htons(port);			// server port

	// bind to our port
	rv = bind(sockfd, (struct sockaddr*)&srv, sizeof(struct sockaddr));
	if (rv<0) err(1,0);
	
	// start listening for connections
	rv = listen(sockfd, 5);
	if (rv<0) err(1,0);
	// main server loop, handle clients one at a time, quit after 10 clients
	int pid;
	for( i=0; ; i++ ) {
		
		// wait for next client, get session socket
		sa_size = sizeof(struct sockaddr_in);
		sessfd = accept(sockfd, (struct sockaddr *)&cli, &sa_size);
		if ((pid=fork())==-1) {
			close(sessfd);
			continue;
		} else if (pid>0) {
			close(sessfd);
			continue;
		} else if (pid==0) {
			connection_handler((void*)&sessfd);
			break;
		}
		// either client closed connection, or error
	}	
	// close socket
	close(sockfd);

	return 0;
}
void* connection_handler(void *fd)
{
	int sessfd=*(int*)fd;
	char* buf = (char*)malloc(MAXMSGLEN);	
	int rv;
	// get messages and send replies to this client, until it goes away
	while ( (rv=recv(sessfd, buf, MAXMSGLEN, 0)) > 0) {
		buf[rv]=0;		// null terminate string to print
		if (rv==4) {
			int len = *(int*)buf;
			buf = (char *)malloc(len+10); 
			memset(buf, 0, len);
			send(sessfd, "message recieved", 17, 0);
			rv=recv(sessfd, buf, len+100, 0);
			buf[rv]=0;
		}
		if (strcmp(buf,"open")==0) {
			int res=processOpen(buf, rv);
			char *ptr = malloc(sizeof(int)*3+1);
            char *ret=ptr;
			*(int*)ptr=sizeof(int)*3+1;
			ptr+=4;
			*(int*)ptr=res;
			ptr=ptr+4;
			*ptr=0;
			ptr++;
			*(int*)ptr=errno;	
			send(sessfd, ret, sizeof(int)*3+1, 0);//return the int value
		} else if (strcmp(buf, "write")==0) {
			ssize_t res=processWrite(buf, rv);
			char *ptr = malloc(sizeof(ssize_t)+sizeof(int)*2+1);
            char *ret=ptr;
			*(int*)ptr=sizeof(ssize_t)+sizeof(int)*2+1;
			ptr+=4;
			*(ssize_t*)ptr=res;
			ptr+=sizeof(ssize_t);
			*ptr=0;
			ptr++;
			*(int*)ptr=errno;
			send(sessfd, ret, sizeof(ssize_t)+9, 0);
		} else if (strcmp(buf, "close")==0) {
			int res=processClose(buf, rv);
			char *ptr = malloc(sizeof(int)*3+1);
            char *ret=ptr;
			*(int*)ptr=sizeof(int)*3+1;
			ptr+=4;
			*(int*)ptr=res;
			ptr=ptr+4;
			*ptr=0;
			ptr++;
			*(int*)ptr=errno;
			send(sessfd, ret, sizeof(int)*2+1, 0);
		} else if (strcmp(buf, "read")==0) {
			size_t byte=*(size_t*)(buf+5);
			//padding: ssize_t+1+errno+1+buf
			void* buffer = (void*)malloc(byte+sizeof(ssize_t)+1+8+1);
			ssize_t res=processRead(*(int*)(buf+5+sizeof(ssize_t)+1), buffer+sizeof(ssize_t)+10, byte);	
			void* ptr=buffer;
			*(int*)ptr=byte+sizeof(ssize_t)+10;
			ptr+=4;
			*(ssize_t*)ptr=res;
			ptr+=sizeof(ssize_t);
			*(char*)ptr=0;
			ptr++;
			*(int*)ptr=errno;
			ptr+=4;
			*(char*)ptr=0;
			ptr++;
			printf("%s\n",(char*)(buffer+sizeof(ssize_t)+10));
			send(sessfd, buffer, byte+sizeof(ssize_t)+10, 0);	
		} else if (strcmp(buf, "unlink")==0) {
			int res=processUnlink(buf);					
			char *ptr = malloc(sizeof(int)*3+1);
			char *ret=ptr;
			*(int*)ptr=sizeof(int)*3+1;
			ptr+=4;
			*(int*)ptr=res;
			ptr=ptr+4;
			*ptr=0;
			ptr++;
			*(int*)ptr=errno;
			send(sessfd, ret, sizeof(int)*3+1,0);
		} else if (strcmp(buf, "stat")==0) {
			//incoming padding: function name+1+path
			struct stat ret;
			int res = processStat(buf, &ret);
			int len = ret.st_size;
			//outgoing padding: int+1+errno+1+size+1+stat
			char* ptr = malloc(sizeof(int)*4+4+len);
			char *t = ptr;
			*(int*)t=sizeof(int)*4+4+len;
			t+=4;	
			*(int*)t=res;
			t+=4;
			*t=0;
			t++;
			*(int*)t=errno;
			t+=4;
			*t=0;
			t++;
			*(int*)t=len;
			t+=4;
			*t=0;
			t++;
			memcpy(t, &ret, len);
			send(sessfd, ptr, sizeof(int)*4+4+len, 0);
		} else if (strcmp(buf, "__xstat")==0) {
			//incoming padding: function name+1+path
			struct stat ret;
			int res = processXstat(buf, &ret);
			int len = ret.st_size;
			//outgoing padding: int+1+errno+1+size+1+stat
			char* ptr = malloc(sizeof(int)*4+4+len);
			char *t = ptr;
			*(int*)t=sizeof(int)*4+4+len;
			t+=4;
			*(int*)t=res;
			t+=4;
			*t=0;
			t++;
			*(int*)t=errno;
			t+=4;
			*t=0;
			t++;
			*(int*)t=len;
			t+=4;
			*t=0;
			t++;
			memcpy(t, &ret, len);
			send(sessfd, ptr, sizeof(int)*4+4+len, 0);
		} else if (strcmp(buf, "lseek")==0) {
			//incoming padding: function name+1+fd+off_t+1+whence
			off_t ret = processLseek(buf);	
			char buffer[14];
			char* ptr=buffer;
			*(int*)ptr=14;
			ptr+=4;
			*(off_t*)ptr=ret;
			ptr+=4;
			*ptr=0;
			ptr++;
			*(int*)ptr=errno;
			send(sessfd, buffer, 14, 0);	
		} else if (strcmp(buf, "getdirentries")==0) {
			//incoming padding: function name+1+fd+1+nbytes+1+baseq
			int nbytes=*(size_t*)(buf+14+5);
			// temporally store the buffer and baseq
			char* temp=malloc(nbytes+5);
			int res=processGetdirentries(buf, temp);
			char* ret=malloc(nbytes+5+5+9);
			char* ptr=ret;
			*(int*)ptr=nbytes+19;
			ptr+=4;
			*(int*)(ptr)=res;
			ptr+=5;
			*(int*)ptr=errno;
			ptr+=5;
			memcpy(ptr, buf+5, nbytes);
			ptr+=nbytes+1;
			memcpy(ptr, buf, sizeof(off_t));
			send(sessfd, ret, nbytes+19, 0);	
		} else {
			printf("%s\b",buf);
		}
	}
	printf("Thread close\n");
	close(sessfd);
	if (rv<0) err(1,0);
	return 0;
}

