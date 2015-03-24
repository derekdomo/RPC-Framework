#define _GNU_SOURCE
#define MAXMSGLEN 100
#define OFFSETfd 100000
#include <dirent.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <err.h>
#include <errno.h>
#include <string.h>
#include <dirtree.h>
 
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdarg.h>
//	global value for a tcp connection
int sockfd;
/*	
 *	According to the padding rule, deserialize the tree	
 *	padding for a dirtreenode:
 *		lengthName+name+#children+#children*4
*/
struct dirtreenode* dfs(void* base, int cur) {
	struct dirtreenode *s=(struct dirtreenode*)malloc(sizeof(struct dirtreenode));
	void* root=base+cur;
	int length_name=*(int*)(root);
	int num_child=*(int*)(root+4+length_name);
	s->name=(char*)malloc(length_name+1);
	memcpy(s->name, root+4, length_name);
	s->name[length_name]=0;
	s->num_subdirs=num_child;
	s->subdirs=(struct dirtreenode**)malloc(num_child*sizeof(struct dirtreenode*));
	int i;
	for (i=0; i<num_child; i++) {
		int child=*(int*)(base+cur+4+length_name+4+i*sizeof(int));
		s->subdirs[i]=dfs(base, child);
	}
	return s;
} 
/*
 *	Release the struct memory in post-order travers
 * */
void release(struct dirtreenode* root) {
	int num=root->num_subdirs;
	int i;
	for (i=0; i<num; i++) {
		release(root->subdirs[i]);
	}
	free(root->name);
	free(root->subdirs);
	free(root);
}

int (*orig_close)(int handle);
// Pass the name of the function to the server
void* clientProcess(char *func, ssize_t len) {
	int rv;
	//send the length of the following message
	char* param=(char*)malloc(len+sizeof(ssize_t));
	char* orig=param;
	*(ssize_t*)param=len;
	memcpy(param+sizeof(ssize_t), func, len);
	//send the parameter
	len+=sizeof(ssize_t);
	while (len>0) {
		rv=send(sockfd, param, len, 0);
		if (rv<1) break;
		param+=rv;
		len-=rv;
	}
	free(orig);
	char *res;
	res=malloc(100);
	rv=recv(sockfd, res, sizeof(ssize_t), 0);
	ssize_t incomingLength=*(ssize_t*)res;
	free(res);
	res=malloc(incomingLength);
	incomingLength-=sizeof(ssize_t);
	char* it=res;
	while (incomingLength>0) {
		rv=recv(sockfd, it, incomingLength, 0);
		if(rv<1) break;
		it+=rv;
		incomingLength-=rv;
	}
	return (void*)res;
}
int (*orig_open)(const char *pathname, int flags, ...);  // mode_t mode is needed when flags includes O_CREAT

// This is our replacement for the open function from libc.
int open(const char *pathname, int flags, ...) {
	mode_t m = 0;
	if (flags & O_CREAT) {
		va_list a;
		va_start(a, flags);
		m = va_arg(a, mode_t);
		va_end(a);
	}
	fprintf(stderr,"start open\n");
	char* buffer=(char*)malloc(strlen(pathname)+20);
	memcpy(buffer, "open", 4);
	buffer[4]=0;
	char* ptr=buffer+5;
	memcpy(ptr, pathname, strlen(pathname));
	ptr+=strlen(pathname);
	*ptr=0;
	ptr++;
	*(int*)ptr=flags;
	ptr+=sizeof(flags);
	*ptr=0;
	ptr++;	
	*(mode_t*)ptr=m;
	ptr+=sizeof(mode_t);
	*ptr=0;
	ptr++;
	//Send the parameter to server
	void* res;
	res=clientProcess(buffer, ptr-buffer);
	errno=*(int*)(res+5);
	int fd;
	if (*(int*)res<0) fd=*(int*)res; 
	else
		fd=*(int*)res+OFFSETfd;
	free(buffer);
	fprintf(stderr, "errno:%d,\t pathname:%s,\t fildes:%d\n",errno,pathname,fd);
	free(res);
	return fd; 
}
// This is the replacement for the read function from libc
ssize_t (*orig_read)(int handle, void *buffer, size_t byte);
ssize_t read(int handle, void *buffer, size_t byte) {
	if (handle<OFFSETfd)
		return orig_read(handle, buffer, byte);
	//outgoing padding: function name+1+byte+1+handle+1
	fprintf(stderr, "start read\t fildes:%d\n",handle);
	char *newBuf=malloc(20+sizeof(size_t));
	memcpy(newBuf, "read", 4);
	char* ptr=newBuf;
	ptr+=4;
	*ptr=0;
	ptr++;
	*(size_t*)ptr=byte;
	ptr+=sizeof(size_t);
	*ptr=0;
	ptr++;
	*(int*)ptr=handle;
	ptr+=sizeof(int);
	*ptr=0;
	ptr++;
	//Send parameter to server
	void* res;
	res=clientProcess(newBuf, ptr-newBuf);
	//incoming padding: ssize_t+1+errno+1+buf+1
	ssize_t s=*(ssize_t*)res;
	errno=*(int*)(res+sizeof(ssize_t)+1);
	if (s!=-1) {
		memcpy(buffer, res+sizeof(ssize_t)+6, s);
	}
	free(newBuf);
	fprintf(stderr,"errno:%d,\t fildes:%d,\t read:%ld\n",errno,handle,byte);
	free(res);
	return s;
}
// This is the replacement for the close function from libc
int (*orig_close)(int handle);
int close(int handle) {
	if (handle<OFFSETfd)
		return orig_close(handle);
	char* buffer=(char*)malloc(20);
	fprintf(stderr, "start close,\t fildes:%d\n",handle);
	char* ptr=buffer;
	memcpy(ptr, "close", 5);
	ptr+=5;
	*ptr=0;
	ptr++;
	*(int*)ptr=handle;
	ptr+=4;
	*ptr=0;
	//Send parameter to server
	void* res;
	res=clientProcess(buffer, ptr-buffer);
	errno=*(int*)(res+5);
	int f=*(int*)res;
	free(buffer);
	fprintf(stderr, "errno:%d,\t fildes:%d\n",errno,f);
	free(res);
	return f;	
}

// This is the replacement for the close function from libc
ssize_t (*orig_getdirentries)(int fd, char *buf, size_t nbytes , off_t *basep);
ssize_t getdirentries(int fd, char *buf, size_t nbytes , off_t *basep) {
	if (fd<OFFSETfd)
		return orig_getdirentries(fd, buf, nbytes, basep);
	//outggoing padding: function name+1+fd+1+nbytes+1+baseq
	char* buffer=(char*)malloc(13+4+sizeof(size_t)+sizeof(off_t)+4);
	char* ptr=buffer;
	fprintf(stderr,"start getentries,\t fildes:%d\n",fd);
	memcpy(ptr, "getdirentries", 13);
	ptr+=13;
	*ptr=0;
	ptr++;
	*(int*)ptr=fd;
	ptr+=4;
	*ptr=0;
	ptr++;
	*(size_t*)ptr=nbytes;
	ptr+=sizeof(size_t);
	*ptr=0;
	ptr++;
	memcpy(ptr, basep, sizeof(off_t));
	ptr+=sizeof(off_t);
	*ptr=0;
	ptr++;
	//Send parameter to server
	void* res;
	//incoming padding: ssize_t+1+errno+1+buffer+1+baseq
	res=clientProcess(buffer, ptr-buffer);
	void* orig=res;
	ssize_t ret=*(ssize_t*)res;
	res+=sizeof(ssize_t)+1;
	errno=*(int*)res;
	res+=5;
	memcpy(buf, res, nbytes);
	res+=nbytes+1;
	memcpy(basep, res, sizeof(off_t));	
	fprintf(stderr, "errno:%d,\t fildes:%d,\t getdirentries:%ld\n",errno,fd,nbytes);
	free(buffer);
	free(orig);
	return ret;
}
// This is the replacement for the lseek
off_t (*orig_lseek)(int fd, off_t offset, int whence);
off_t lseek(int fd, off_t offset, int whence) {
	if (fd<OFFSETfd)
		return orig_lseek(fd, offset, whence);
	//outgoing padding:function name+1+fd+1+off_t+1+whence
	char* buffer=(char*)malloc(21+5+4+sizeof(off_t)+4+4);
	char* ptr=buffer;
	fprintf(stderr, "start lseek,\t fildes:%d\n", fd);
	memcpy(ptr, "lseek", 5);
	ptr+=5;
	*ptr=0;
	ptr++;
	*(int*)ptr=fd;
	ptr+=4;
	*ptr=0;
	ptr++;
	*(off_t*)ptr=offset;
	ptr+=sizeof(off_t);
	*ptr=0;
	ptr++;
	*(int*)ptr=whence;
	ptr+=4;
	*ptr=0;
	ptr++; 
	//Send parameter to server
	void* res = clientProcess(buffer, ptr-buffer);
	errno=*(int*)(res+5);
	off_t o=*(int*)res;
	free(buffer);
	fprintf(stderr,"errno:%d,\t fildes:%d,\t lseek:%ld\n",errno,fd,offset);
	free(res);
	return o;
}
int (*orig_stat)(const char *path, struct stat *buf); 
int stat(const char *path, struct stat *buf) {
	//fprintf(stderr, "mylib: stat called\n");
	//incoming padding: function name+1+path
	char *newBuf=malloc(10+strlen(path));
	memcpy(newBuf, "stat", 4);
	char* ptr=newBuf;
	ptr+=4;
	*ptr=0;
	ptr++;
	memcpy(ptr, path, strlen(path));
	ptr+=strlen(path);
	*ptr=0;
	ptr++;
	//Send parameter to server
	void *res;
	res=clientProcess(newBuf, ptr-newBuf);
	//outcoming padding: int+1+errno+1+size+1+stat
	errno=*(int*)(res+sizeof(int)+1);
	int length=*(int*)(res+sizeof(int)*2+2);
	memcpy((char*)buf, res+sizeof(int)*3+3, length);
	int s=*(int*)res;
	free(newBuf);
	free(res);
	return s;
}
ssize_t (*orig_write)(int fildes, const void *buf, size_t nbytes); 
ssize_t write(int fildes, const void *buf, size_t nbytes) {
	//fprintf(stderr, "mylib: write called\n");
	if (fildes<OFFSETfd)
		return orig_write(fildes, buf, nbytes);
	fprintf(stderr, "start write,\t fildes:%d\n", fildes);
	int len = 4+nbytes;
	char *buffer=malloc(len+10);
	char *ptr= buffer;
	memcpy(ptr, "write", 5);
	ptr+=5;
	*ptr=0;
	ptr++;
	*(int*)ptr=fildes;
	ptr+=sizeof(fildes);
	*ptr=0;
	ptr++;
	*(size_t*)ptr=nbytes;
	ptr+=sizeof(size_t);
	*ptr=0;
	ptr++;
	if (nbytes>0) {
		memcpy(ptr, (char*)buf, nbytes);
		ptr+=(int)nbytes;
	}
	*ptr=0;
	ptr++;
	//Send parameter to server
	void *res;
	res=clientProcess(buffer, ptr-buffer);
	errno=*(int*)(res+sizeof(ssize_t)+1);
	ssize_t s=*(ssize_t*)res;
	fprintf(stderr, "errno: %d,\t fildes: %d\n,\t write: %ld\n",errno,fildes,nbytes);
	free(buffer);
	free(res);
	return s;
}
int (*orig_unlink)(const char *path);
int unlink(const char *path) {
	fprintf(stderr, "start unlink\n");
	int len = strlen(path);
	char *buffer=malloc(len+10);
	char *ptr = buffer;
	memcpy(ptr, "unlink", 6);
	ptr+=6;
	*ptr=0;
	ptr++;
	memcpy(ptr, path, len);	
	ptr+=len;
	*ptr=0;
	ptr++;
	//Send parameter to server
	void* res;
	res = clientProcess(buffer, ptr-buffer);
	errno=*(int*)(res+5);
	fprintf(stderr, "errno:%d,\t path:%s\n", errno,path);
	int f=*(int*)res;
	free(buffer);
	free(res);
	return f;
}
struct dirtreenode* (*orig_getdirtree)( const char *path );
struct dirtreenode* getdirtree( const char *path ) {
	//outgoing padding: function name+1+path+1
	char *newBuf=malloc(strlen(path)+12);
	fprintf(stderr,"start getdirtree\n");
	memcpy(newBuf, "getdirtree", 10);
	char* ptr=newBuf;
	ptr+=10;
	*ptr=0;
	ptr++;
	memcpy(ptr, path, strlen(path));
	ptr+=strlen(path);
	*ptr=0;
	ptr++;	
	//Send parameter to server
	void* res;
	res=clientProcess(newBuf, ptr-newBuf);
	errno=*(int*)(res);
	fprintf(stderr,"errno:%d,\t path:%s\n",errno,path);
	if(*(char*)(res+4)==0) {
		free(newBuf);
		free(res);
		return NULL;
	}
	struct dirtreenode* s=dfs(res-sizeof(ssize_t), sizeof(ssize_t)+4);
	free(newBuf);
	free(res);
	return s;
}
void (*orig_freedirtree)( struct dirtreenode* dt );
void freedirtree( struct dirtreenode* dt ) {
	if (dt==NULL)
		return;
	release(dt);
	return;
}
int (*orig_xstat)(int ver, const char * path, struct stat * stat_buf);
int __xstat(int ver, const char * path, struct stat * stat_buf) {
	//incoming padding: function name+1+ver+1+path
	fprintf(stderr, "start xstat\n");
	char *newBuf=malloc(7+4+3+strlen(path));
	memcpy(newBuf, "__xstat", 7);
	char* ptr=newBuf;
	ptr+=7;
	*ptr=0;
	ptr++;
	*(int*)ptr=ver;
	ptr+=4;
	*ptr=0;
	ptr++;
	memcpy(ptr, path, strlen(path));
	ptr+=strlen(path);
	*ptr=0;
	ptr++;
	//Send parameter to server
	void *res;
	res=clientProcess(newBuf, ptr-newBuf);
	//outcoming padding: int+1+errno+1+size+1+stat
	errno=*(int*)(res+sizeof(int)+1);
	int length=*(int*)(res+sizeof(int)*2+2);
	memcpy((void*)stat_buf, res+sizeof(int)*3+3, length);
	int f=*(int*)res;
	fprintf(stderr, "errno:%d,\t path:%s\n",errno,path);
	free(res);
	free(newBuf);
	return f;
}
// This function is automatically called when program is started
void _init(void) {
	// set function pointer orig_open to point to the original open function
	orig_open = dlsym(RTLD_NEXT, "open");
	orig_close = dlsym(RTLD_NEXT, "close");
	orig_read = dlsym(RTLD_NEXT, "read");
	orig_lseek = dlsym(RTLD_NEXT, "lseek");
	orig_unlink = dlsym(RTLD_NEXT, "unlink");
	orig_write = dlsym(RTLD_NEXT, "write");
	orig_stat = dlsym(RTLD_NEXT, "stat");
	orig_xstat = dlsym(RTLD_NEXT, "__xstat");
	orig_getdirentries = dlsym(RTLD_NEXT, "getdirentries");
	orig_getdirtree = dlsym(RTLD_NEXT, "getdirtree");
	orig_freedirtree = dlsym(RTLD_NEXT, "freedirtree");
	fprintf(stderr, "Init mylib\n");
	//init tcp connection
	char *serverip;
	char *serverport;
	unsigned short port;
	int rv;
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
	
}
void _fini(void) {
	//close tcp connection
	orig_close(sockfd);
}


