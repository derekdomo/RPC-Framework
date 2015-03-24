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
#define OFFSETfd 100000
/*
 *	Parse the parameter and execute the Open function
 * */
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
/*
 *	Parse the parameter and execute the Write function
 * */
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
	//modify the fd to make it normal
	fildes-=OFFSETfd;
	return write(fildes, buf, nbytes);	
}
/*
 *	Parse the parameter and execute the Close function	
 * */
int processClose(char *param, int len) {
	int handle;
	//skip the function name
	param+=6;
	handle=*(int*)param;
	//modify the fd to make it normal
	handle-=OFFSETfd;
	return close(handle);	
}
/*
 *	Execute the Read function
 * */
ssize_t processRead(int handle, void* buf, size_t byte) {
	handle-=OFFSETfd;
	return read(handle, buf, byte);	
}
/*
 *	Parse the parameter and execute the Unlink function
 * */
int processUnlink(char *param) {
	// skip the function name
	param+=7;
	return unlink(param);
}
/*
 *	Parse the parameter and execute the stat function
 *	Returned value stored in ret
 * */
int processStat(char *param, struct stat* ret) {
	//skip the function name
	param+=sizeof(int)+1;
	return stat(param, ret);
}
/*
 *	Parse the parameter and execute the stat function
 *	Returned value stored in ret
 * */
int processXstat(char *param, struct stat* ret) {
	//skip the function name
	param+=8;
	int ver = *(int*)param;
	param+=sizeof(int)+1;
	return __xstat(ver, param, ret);
}
/*
 *	Parse the parameter and execute the lseek function
 * */
int processLseek(char *param) {
	//skip the function name
	param+=6;
	int fd=*(int*)param;
	param+=sizeof(int)+1;
	off_t offset=*(off_t*)param;
	param+=sizeof(off_t)+1;
	int whence=*(int*)param;
	fd-=OFFSETfd;
	return lseek(fd, offset, whence);
}
/*
 *	Parse the parameter and execute Getdirentries function
 *	Returned value stored in buf
 * */
ssize_t processGetdirentries(char* param, char* buf) {
	//skip the function name
	param+=14;
	int fd=*(int*)param;
	param+=sizeof(int)+1;
	size_t nbytes=*(size_t*)param;
	param+=sizeof(size_t)+1;
	memcpy(buf, param, sizeof(off_t));
	fd-=OFFSETfd;
	return getdirentries(fd, buf+sizeof(off_t), nbytes, (off_t*)buf);	
}
/*
 *	Parse the parameter and execute getdirtree
 *	Return the struct dirtreenode
 * */
struct dirtreenode* processGetdirtree(char* param) {
	//skip the function name
	param+=11;
	struct dirtreenode* s= getdirtree(param);	
	return s;
}
/*
 *	Traverse the tree in pre-order using dfs 
 *	Serialize the tree into a chunk of memory
 * */
int dfs(struct dirtreenode* root, void **base, int* offset) {
	if (root==NULL)
		return 0;
	int length_name=strlen(root->name);
	int num_child=root->num_subdirs;
	int cur=*offset;
	// store the lengthOfTheName, name, numberOfChild, and offsets
	void* newBase=malloc(*offset+4+length_name+4+num_child*sizeof(int));
	memcpy(newBase, *base, *offset);
	free(*base);
	*base=newBase;
	//store the length of the name
	*(int*)(*base+*offset)=strlen(root->name);
	//store name
	memcpy(*base+*offset+4,root->name, strlen(root->name));
	//store num of children
	*(int*)(*base+*offset+4+strlen(root->name))=num_child;
	//current size of the memory
	*offset=*offset+4+length_name+4+num_child*sizeof(int);
	int i;
	for (i=0; i<num_child; i++) {
		int child=dfs(root->subdirs[i], base, offset);
		*(int*)(*base+cur+4+length_name+4+i*sizeof(int))=child;
	}
	return cur;	
}
/*
 *	Free the tree in post-order
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
/*
 *	Use robust IO to send the message
 * */
void sendAll(int sessfd, void* ret, ssize_t length) {
	while (length>0) {
		int i=send(sessfd, ret, length, 0);
		if (i<1) break;
		ret+=i;
		length-=i;
	}
}
/*
 *	Declaration of the service function
 * */
void *connection_handler(void *);
/*
 *	Run the server
 * */
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
	int pid;
	// main server loop, handle clients one at a time, quit after 10 clients
	for( i=0; ; i++ ) {
		
		// wait for next client, get session socket
		sa_size = sizeof(struct sockaddr_in);
		sessfd = accept(sockfd, (struct sockaddr *)&cli, &sa_size);
		if (sessfd<0) err(1,0);
		if ((pid=fork())==-1) {//fork failure
			close(sessfd);
			continue;
		}else if (pid>0) {//parent
			close(sessfd);
			continue;
		} else if (pid==0) {//child
			connection_handler((void*)&sessfd);
			close(sessfd);
			break;
		}
	}	
	// close socket
	close(sockfd);

	return 0;
}
/*
 *	Service function
 *		Communicate with the server
 * */
void* connection_handler(void *fd)
{
	int sessfd=*(int*)fd;
	char* buf = (char*)malloc(4);	
	int rv;
	// get messages and send replies to this client, until it goes away
	while ( (rv=recv(sessfd, buf, sizeof(ssize_t), 0)) > 0) {
		// get the length of the following message and allocate memory
		// then receive the following message
		if (rv==sizeof(ssize_t)) {
			ssize_t len = *(ssize_t*)buf;
			free(buf);
			buf = (char *)malloc(len+10); 
			memset(buf, 0, len);
			char* ptr=buf;
			while (len>0) {
				rv=recv(sessfd, ptr, len, 0);
				if (rv<1) break;
				ptr+=rv;
				len-=rv;
			}
		}
		// Check function name
		if (strcmp(buf,"open")==0) {
			int res=processOpen(buf, rv);
			//outgoing padding: length+fd+1+errno+1
			ssize_t length=sizeof(int)*2+1+sizeof(ssize_t);
			char *ptr = malloc(length);
            char *ret=ptr;
			*(ssize_t*)ptr=length;
			ptr+=sizeof(ssize_t);
			*(int*)ptr=res;
			ptr=ptr+4;
			*ptr=0;
			ptr++;
			*(int*)ptr=errno;	
			sendAll(sessfd, ret, length);
		} else if (strcmp(buf, "write")==0) {
			ssize_t res=processWrite(buf, rv);
			//outgoing padding: length+fd+1+errno+1
			ssize_t length=sizeof(ssize_t)+sizeof(int)*2+1;
			char *ret = malloc(length);
            char *ptr=ret;
			*(ssize_t*)ptr=length;
			ptr+=sizeof(ssize_t);
			*(ssize_t*)ptr=res;
			ptr+=sizeof(ssize_t);
			*ptr=0;
			ptr++;
			*(int*)ptr=errno;
			sendAll(sessfd, ret, length);
			free(ret);
		} else if (strcmp(buf, "close")==0) {
			int res=processClose(buf, rv);
			//outgoing padding: length+ret+1+errno+1
			ssize_t length=sizeof(int)*2+sizeof(ssize_t)+2;
			char *ret = malloc(length);
            char *ptr=ret;
			*(ssize_t*)ptr=length;
			ptr+=sizeof(ssize_t);
			*(int*)ptr=res;
			ptr=ptr+4;
			*ptr=0;
			ptr++;
			*(int*)ptr=errno;
			sendAll(sessfd, ret, length);
			free(ret);
		} else if (strcmp(buf, "read")==0) {
			size_t byte=*(size_t*)(buf+5);
			ssize_t length; 
			//outgoing padding: length+ssize_t+1+errno+1+buf+1
			length=byte+sizeof(ssize_t)*2+1+sizeof(int)+2;
			void *buffer = (void*)malloc(length);
			ssize_t res=processRead(*(int*)(buf+5+sizeof(ssize_t)+1), buffer+sizeof(ssize_t)*2+6, byte);	
			void* ptr=buffer;
			length-=byte;
			if (res!=-1){
				length+=res;
			}
			*(ssize_t*)ptr=length;
			ptr+=sizeof(ssize_t);
			*(ssize_t*)ptr=res;
			ptr+=sizeof(ssize_t);
			*(char*)ptr=0;
			ptr++;
			*(int*)ptr=errno;
			ptr+=4;
			*(char*)ptr=0;
			fprintf(stderr, "read\t errno:%d\n",errno);
			sendAll(sessfd, buffer, length);	
			free(buffer);
		} else if (strcmp(buf, "unlink")==0) {
			int res=processUnlink(buf);					
			//outgoing padding: length+fd+1+errno+1
			ssize_t length=sizeof(int)*2+sizeof(ssize_t)+2;
			char *ret = malloc(length);
			char *ptr=ret;
			*(ssize_t*)ptr=length;
			ptr+=sizeof(ssize_t);
			*(int*)ptr=res;
			ptr=ptr+4;
			*ptr=0;
			ptr++;
			*(int*)ptr=errno;
			fprintf(stderr, "unlink errno:%d\n",errno);
			sendAll(sessfd, ret, length);
			free(ret);
		} else if (strcmp(buf, "stat")==0) {
			//incoming padding: function name+1+path
			struct stat ret;
			int res = processStat(buf, &ret);
			int len = sizeof(struct stat);
			//outgoing padding: length+int+1+errno+1+size+1+stat+1
			ssize_t length=sizeof(int)*3+sizeof(ssize_t)+4+len;
			char* ptr = malloc(length);
			char *t = ptr;
			*(ssize_t*)t=length;
			t+=sizeof(ssize_t);	
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
			sendAll(sessfd, ptr, length);
			free(ptr);
		} else if (strcmp(buf, "__xstat")==0) {
			//incoming padding: function name+1+path
			struct stat ret;
			int res = processXstat(buf, &ret);
			int len = sizeof(struct stat);
			//outgoing padding: int+1+errno+1+size+1+stat
			int length=sizeof(int)*3+sizeof(ssize_t)+4+len;
			char* ptr = malloc(length);
			char *t = ptr;
			*(ssize_t*)t=length;
			t+=sizeof(ssize_t);
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
			sendAll(sessfd, ptr, length);
			free(ptr);
		} else if (strcmp(buf, "lseek")==0) {
			//incoming padding: function name+1+fd+off_t+1+whence
			off_t ret = processLseek(buf);	
			ssize_t length=4+4+1+1+sizeof(ssize_t);
			char* buffer=(char*)malloc(length);
			char* ptr=buffer;
			*(ssize_t*)ptr=length;
			ptr+=sizeof(ssize_t);
			*(off_t*)ptr=ret;
			ptr+=4;
			*ptr=0;
			ptr++;
			*(int*)ptr=errno;
			fprintf(stderr,"lseek\t errno:%d\n",errno);
			sendAll(sessfd, buffer, length);	
			free(buffer);
		} else if (strcmp(buf, "getdirentries")==0) {
			//incoming padding: function name+1+fd+1+nbytes+1+baseq
			size_t nbytes=*(size_t*)(buf+14+5);
			// temporally store the buffer and baseq
			char* temp=malloc(nbytes+sizeof(off_t));
			ssize_t res=processGetdirentries(buf, temp);
			// store total size+ssize_t+1+errno+1+buffer+1+basep
			char* ret=malloc(nbytes+2*sizeof(ssize_t)+4+sizeof(off_t)+4);
			char* ptr=ret;
			//store the size
			*(ssize_t*)ptr=nbytes+sizeof(ssize_t)*2+sizeof(off_t)+8;
			ptr+=sizeof(ssize_t);
			//store the return value 
			*(ssize_t*)(ptr)=res;
			ptr+=sizeof(ssize_t)+1;
			//store the errno
			*(int*)ptr=errno;
			ptr+=5;
			//store the buffer
			memcpy(ptr, temp+sizeof(off_t), nbytes);
			ptr+=nbytes+1;
			//store the basep
			memcpy(ptr, temp, sizeof(off_t));
			ptr+=sizeof(off_t)+1;
			sendAll(sessfd, ret, nbytes+sizeof(ssize_t)*2+sizeof(off_t)+8);	
			free(ret);
			free(temp);
		} else if (strcmp(buf, "getdirtree")==0) {
			//incoming padding: function name+1+path
			struct dirtreenode* root=processGetdirtree(buf);
			void* ret=malloc(1);
			int *off=(int*)malloc(sizeof(int));	
			*off=4+sizeof(ssize_t);
			dfs(root, &ret, off);
			*(ssize_t*)ret=*off;
			*(int*)(ret+sizeof(ssize_t))=errno;
			if (*off==4+sizeof(ssize_t)) {
				*off+=1;
				*(char*)(ret+4+sizeof(ssize_t))=0;
			} else {
				release(root);
			}
			sendAll(sessfd, ret, *off);	
			free(ret);
			free(off);
		} else {
			printf("%s\n",buf);
		}
	}
	if (rv<0) err(1,0);
	return 0;
}

