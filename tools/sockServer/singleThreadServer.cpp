#include <sys/socket.h>
#include <sys/select.h>
#include <sys/types.h>
#include <netdb.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/epoll.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "myerr.h"
#define BUFFER_LENGTH 65536
#define TIME_OUT 60
#define TIME_OUT_MSG 60
#define MAX_EVENT 256

struct sockaddr_in  cli, fserv;
unsigned int clilen = sizeof(cli);
char buffer[BUFFER_LENGTH];
void showmsg(char *buffer, int len)
{
    for (int i=0; i<len; i++)
       fprintf(stdout, "%02x ", (unsigned char)(buffer[i]));
    fprintf(stdout, "\n");
}
void encodebuffer(unsigned char* buffer, int len, unsigned char key)
{
   for (int i=0; i<len; i++)
      buffer[i] = buffer[i] ^ key;
}
bool getsockaddrfromhost(char* buffer, unsigned char bytes, sockaddr_in& serv)
{
   struct hostent* host;
   char hostname[256];
   memcpy(hostname, buffer, bytes);
   hostname[bytes] = '\0';
   //printf("hostname: %s\n", hostname);
   if ((host = gethostbyname(hostname)) == NULL) return false;
   memcpy(&serv.sin_addr.s_addr, host->h_addr, 4);
   //printf("host address: %x\n", serv.sin_addr.s_addr);
   return true;
}
void setnonblock(int fd) 
{
    int flag = fcntl(fd, F_GETFL);
    fcntl(fd, F_SETFL, flag | O_NONBLOCK);
}
void setblock(int fd)
{
	int flag = fcntl(fd, F_GETFL);
	fcntl(fd, F_SETFL, flag ^ O_NONBLOCK);
}
struct timeval tv;
void settimeout(int sock, int second, int flag = -1)
{
   tv.tv_sec = second;
   tv.tv_usec = 0;
   if (flag != -1)
   {	  
       setsockopt(sock, SOL_SOCKET, flag, (const char*)&tv, sizeof(timeval));
       return;
   }
   setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof(timeval));
   setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(timeval));
}
#define setsendtimeout(sock, second) settimeout(sock, second, SO_SNDTIMEO)

struct sockinfo{
   int fd;
   int dstfd;
   int state;
   sockinfo():fd(-1),dstfd(-1),state(-1){}
   sockinfo(int _fd, int _state):fd(_fd),state(_state),dstfd(-1){}
   sockinfo(int _fd, int _dstfd, int _state):fd(_fd),dstfd(_dstfd),state(_state){}
};
sockinfo sockinfos[2048];

int n = 0;
char acceptSockBuffer[2] = {5, 0};
char startSockBuffer[10] = {5, 0, 0, 1, 0, 0, 0, 0, 0, 0};
int proc_recv_sock0(int clifd, int &remote, int key)
{// recv 5,1,0 or 5,2,1,0 reply 5,0
    //sock protocl identify
    if ((n = recv(clifd, buffer, BUFFER_LENGTH, 0)) < 3){ 
       fprintf(stdout, "---recv sock head from client failed, n:%d---\n", n); 
       close(clifd);
       return -1; 
    } 
	encodebuffer((unsigned char*)buffer,n,key);
	//showmsg(buffer, n<20?n:20);
    if (n < 10){// first version check msg
       if (buffer[0] == 5 && ((buffer[1] == 1 && buffer[2] == 0) || buffer[1] == n - 2)){
          encodebuffer((unsigned char*)acceptSockBuffer, sizeof(acceptSockBuffer),key);
          send(clifd, acceptSockBuffer, sizeof(acceptSockBuffer), 0);
       }
       else{
          fprintf(stdout, "receive request sock type not 0x050100\n");
          close(clifd);
          return -1;
       }
    }
	else{
		return proc_recv_sock1_with_buffer(clifd, remote, key);
	}
	sockinfos[clifd].state = 1;
	return 0;
}
int proc_recv_sock1(int clifd, int& remote, int key)
{// recv 5,1..host,port reply 5,0,0,1,ip,port
	if ((n = recv(clifd, buffer, BUFFER_LENGTH, 0)) < 10){
        fprintf(stdout, "receive start sock from client failed\n");
        close(clifd);
        return -1;
    }
    encodebuffer((unsigned char*)buffer,n,key);
    return proc_recv_sock1_with_buffer(clifd, remote, key);
}
int proc_recv_sock1_with_buffer(int clifd, int& remote, int key)
{
    if (buffer[0] == 5 && buffer[1] == 1 && buffer[2] == 0)
    {// second remote address send msg
       if (buffer[3] == 3)
       {
          unsigned char bytes = buffer[4];
          getsockaddrfromhost(buffer+5, bytes, fserv);
          memcpy(&fserv.sin_port, buffer+5+bytes, 2);
       }
       else if (buffer[3] == 1){
          memcpy(&fserv.sin_addr.s_addr, buffer+4, 4);
          memcpy(&fserv.sin_port, buffer+8, 2);
       }
    }
    else{
       fprintf(stdout, "receive sock start type not 0x050100\n");
       close(clifd);
       return -1;
    }
    memcpy(startSockBuffer+4, &fserv.sin_addr.s_addr, 4);
    memcpy(startSockBuffer+6, &fserv.sin_port, 2);
    // async connect to remote
    if ((remote = socket(PF_INET, SOCK_STREAM, 0)) < 0){
       fprintf(stdout, "---forward socket init failed---\n");
       close(clifd);
       return -1;
    }
	setnonblock(remote);
    if (connect(remote, (sockaddr*)(&fserv), sizeof(fserv)) < 0){
       close(clifd);
       close(remote);
       fprintf(stdout, "connect to forward address failed\n");
       return -1;
    }

	// send reply to client
	encodebuffer((unsigned char*)startSockBuffer, sizeof(startSockBuffer), key);
    if (sizeof(startSockBuffer) != send(clifd, startSockBuffer, sizeof(startSockBuffer), 0)){
       close(clifd);
       close(remote);
       fprintf(stdout, "send second msg to client failed\n");
       return -1;
    }
	sockinfos[clifd] = sockinfo(clifd, remote, 2);
    sockinfos[remote] = sockinfo(remote, clifd, 3);
    return 0;
}
int proc_recv_sock2(int fd, int key)
{// not connect to remote, ignore
	return 0;
}
int proc_recv_sock3(int fd, int key)
{// connect to remote now, set flag to both client and remote
	setblock(fd);
	settimeout(fd, TIME_OUT_MSG);
	sockinfos[fd].state = 9;
	sockinfos[sockinfos[fd].dstfd].state = 9;
}

// 0-Succ
// 1-Close
// -1-Error Occure
int proc_accept(int srvfd, int& clifd, int key)
{
    if ((clifd = accept(srvfd, (sockaddr*)&cli, &clilen)) < 0){
        fprintf(stdout, "accept error[%d], ignored!\n", errno);
        return -1;
    }
	sockinfos[clifd] = sockinfo(clifd, 0);
	settimeout(clifd, TIME_OUT_MSG);
    fprintf(stdout, "connection [%d-%d] established.\n", clifd, remote); 
    return 0;
}
int proc_recv(int fd, int dstfd, int key)
{
    int n = 0;
    // fprintf(stdout, "proc recv param: %d,%d,%d\n", curr, remote, key);
    if ((n = recv(fd, buffer, BUFFER_LENGTH, 0)) < 0)
    {
        fprintf(stdout, "---recv error[%d] occur, ignored!\n", errno);
        return -1;
    }
    fprintf(stdout, "recv from %d, send to %d, length:%d\n", fd, dstfd, n);
    if (n == 0)
    {
       fprintf(stdout, "close by client\n");
       return 1;
    }
    //showmsg(buffer, n>20?20:n);
    encodebuffer((unsigned char*)buffer, n, key);

    if (send(dstfd, buffer, n, 0) != n)
    {
       fprintf(stdout, "---send to sock:%d error[%d]\n", dstfd, errno);
       return -1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    struct sockaddr_in serv;
    int srvfd;
    unsigned char key = 0;
    if (argc < 3)
    {
        err_quit( "Usage: %s servport encodekey\n", argv[0]);
    }
    if ((srvfd = socket(PF_INET, SOCK_STREAM, 0)) < 0)
    {
        err_sys("socket error");
    }
    memset(&serv, 0, sizeof(serv));
    serv.sin_family = AF_INET;
    serv.sin_addr.s_addr = htonl(INADDR_ANY);
    serv.sin_port = htons(atoi(argv[1]));
    key = atoi(argv[2]);
    memset(&fserv, 0, sizeof(fserv));
    fserv.sin_family = AF_INET;

    int opt=1;
    setsockopt(srvfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    setsockopt(srvfd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
    if (bind(srvfd, (const sockaddr*)&serv, sizeof(serv)) < 0)
    {
        err_sys("bind error");
    }
    if (listen(srvfd, 6) <0)
    {
        err_sys("listen error");
    }
    fprintf(stdout, "start listen ... \n");
    int forkid = -1;
    unsigned int clilen = sizeof(cli);
    signal(SIGCHLD,SIG_IGN);

    // create epoll
    int epfd = epoll_create(MAX_EVENT);
    if (epfd == -1){
        err_sys("epoll create error");
    }
    // add sock to epoll
    struct epoll_event srvev, evpool[2048], events[MAX_EVENT];
    int fdmap[2048];
    srvev.events = EPOLLIN;
    srvev.data.fd = srvfd;
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, srvfd, &srvev) == -1){
        err_sys("epoll ctrl add fd error");
    }
    int nfds = -1;
    int proc_result = 0;
    int client = -1;
    int remote = -1;
    for (;;){
        nfds = epoll_wait(epfd, events, MAX_EVENT, TIME_OUT);
        if (nfds == -1){
            fprintf(stdout, "epoll wait error[%d], ignored!\n", errno);
            sleep(1);
            continue;
        }
        for (int i=0; i<nfds; i++){
            struct epoll_event curr = events[i];
			int currfd = curr.data.fd;
            // fprintf(stdout, "epoll event %d, fd: %d, events: %d\n", i, curr.data.fd, curr.events);
            if (curr.data.fd == srvfd){
                // fprintf(stdout, "process accetp: %d\n", srvfd);
                proc_result = proc_accept(srvfd, client, key);
                if (proc_result == 0){
                    evpool[client].data.fd = client;
                    evpool[client].events = EPOLLIN;
                    epoll_ctl(epfd, EPOLL_CTL_ADD, client, &evpool[client]);
                }
                else {
                    fprintf(stdout, "accept return abnormal: %d\n", proc_result);
                }
            }
            else
            {
                int state = sockinfos[currfd].state;
				remote = -1;
				if (sockinfos[currfd].fd == -1) 
					continue;
				if (state == 0)
					proc_result = proc_recv_sock0(currfd, remote, key);
				else if (state == 1)
					proc_result = proc_recv_sock1(currfd, remote, key);
                else if (state == 3)
				    proc_result = proc_recv_sock3(currfd, key); 
				else if (state == 9)
				    proc_result = proc_recv(currfd, key);
                if (proc_result == 0){
				    if (remote != -1){
						evpool[remote].data.fd = remote;
                    	evpool[remote].events = EPOLLIN;
                    	epoll_ctl(epfd, EPOLL_CTL_ADD, remote, &evpool[remote]);	
					}
				}
				else {
                    epoll_ctl(epfd, EPOLL_CTL_DEL, currfd, NULL);
					close(currfd);
					sockinfos[currfd].fd = -1;
					remote = sockinfos[currfd].dstfd;
                    if (remote != -1){
                    	epoll_ctl(epfd, EPOLL_CTL_DEL, remote, NULL);
						close(remote);
						sockinfos[remote] = -1;
					}
                }

            }
        }
    }
    return 0;
}
