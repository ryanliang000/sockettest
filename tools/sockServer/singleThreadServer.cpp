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
#include "log.h"
#define BUFFER_LENGTH 65536
#define TIME_OUT 60
#define TIME_OUT_MSG 60
#define MAX_EVENT 256

struct sockaddr_in  cli, fserv;
unsigned int clilen = sizeof(cli);
void showmsg(char msg[], int len)
{
    for (int i=0; i<len; i++)
       printf("%02x ", (unsigned char)(msg[i]));
}
void encodebuffer(unsigned char* msg, int len, unsigned char key)
{
   for (int i=0; i<len; i++)
      msg[i] = msg[i] ^ key;
}
bool getsockaddrfromhost(char* msg, unsigned char bytes, sockaddr_in& serv)
{
   struct hostent* host;
   char hostname[256];
   memcpy(hostname, msg, bytes);
   hostname[bytes] = '\0';
   LOG_I("hostname: %s", hostname);
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
   if (flag != -1){	  
       setsockopt(sock, SOL_SOCKET, flag, (const char*)&tv, sizeof(timeval));
       return;
   }
   setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof(timeval));
   setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(timeval));
}
#define setsendtimeout(fd, sec) settimeout(fd, sec, SO_SNDTIMEO)
#define setrecvtimeout(fd, sec) settimeout(fd, sec, SOL_RCVTIMEO);
struct buffinfo{
	char buff[BUFFER_LENGTH];
	int recvn;
	int sendn;
	buffinfo():recvn(0),sendn(0){}
};
struct sockinfo{
   int fd;
   int dstfd;
   int state;
   buffinfo tbuf;
   sockinfo():fd(-1),dstfd(-1),state(-1){}
   sockinfo(int _fd, int _state):fd(_fd),state(_state),dstfd(-1){}
   sockinfo(int _fd, int _dstfd, int _state):fd(_fd),dstfd(_dstfd),state(_state){}
};
sockinfo sockinfos[2048];

char acceptSockBuffer[2] = {5, 0};
char startSockBuffer[10] = {5, 0, 0, 1, 0, 0, 0, 0, 0, 0};
int proc_recv_sock1_with_buffer(int clifd, int& remote, int key);
int proc_recv_sock0(int clifd, int &remote, int key)
{// recv 5,1,0 or 5,2,1,0 reply 5,0
    //sock protocl identify
	buffinfo& tbuf = sockinfos[clifd].tbuf;
    if ((tbuf.recvn = recv(clifd, tbuf.buff, BUFFER_LENGTH, 0)) < 3){ 
       LOG_E("---recv sock head from client failed, n:%d---", tbuf.recvn); 
       close(clifd);
       return -1; 
    } 

	encodebuffer((unsigned char*)tbuf.buff,tbuf.recvn,key);
	char* buff = tbuf.buff;
    if (tbuf.recvn < 10){// first version check msg
       if (buff[0] == 5 && ((buff[1] == 1 && buff[2] == 0) || buff[1] == tbuf.recvn - 2)){
          tbuf.sendn = send(clifd, acceptSockBuffer, sizeof(acceptSockBuffer), 0);
       }
       else{
          LOG_E("receive request sock type not 0x050100");
          close(clifd);
          return -1;
       }
    }
	else{
		LOG_I("recv sock0-sock1 bind msg");
        return proc_recv_sock1_with_buffer(clifd, remote, key);
	}
	sockinfos[clifd].state = 1;
	return 0;
}
int proc_recv_sock1(int clifd, int& remote, int key)
{// recv 5,1..host,port reply 5,0,0,1,ip,port
    buffinfo& tbuf = sockinfos[clifd].tbuf; 
	if ((tbuf.recvn = recv(clifd, tbuf.buff, BUFFER_LENGTH, 0)) < 10){
        LOG_E("receive start sock from client failed, len:%d", tbuf.recvn);
        close(clifd);
        return -1;
    }
    encodebuffer((unsigned char*)tbuf.buff, tbuf.recvn,key);
    return proc_recv_sock1_with_buffer(clifd, remote, key);
}
int proc_recv_sock1_with_buffer(int clifd, int& remote, int key)
{
    buffinfo& tbuf = sockinfos[clifd].tbuf;
	char* buff = tbuf.buff;
	if (buff[0] == 5 && buff[1] == 1 && buff[2] == 0)
    {// second remote address send msg
       if (buff[3] == 3)
       {
          unsigned char bytes = buff[4];
          getsockaddrfromhost(buff+5, bytes, fserv);
          memcpy(&fserv.sin_port, buff+5+bytes, 2);
       }
       else if (buff[3] == 1){
          memcpy(&fserv.sin_addr.s_addr, buff+4, 4);
          memcpy(&fserv.sin_port, buff+8, 2);
       }
    }
    else{
       LOG_E("receive sock start type not 0x050100");
       close(clifd);
       return -1;
    }
    memcpy(startSockBuffer+4, &fserv.sin_addr.s_addr, 4);
    memcpy(startSockBuffer+6, &fserv.sin_port, 2);
    // async connect to remote
    if ((remote = socket(PF_INET, SOCK_STREAM, 0)) < 0){
       LOG_E("---forward socket init failed---");
       close(clifd);
       return -1;
    }
	setnonblock(remote);
    connect(remote, (sockaddr*)(&fserv), sizeof(fserv));
    //if (connect(remote, (sockaddr*)(&fserv), sizeof(fserv)) < 0){
    //   close(clifd);
    //   close(remote);
    //   LOG_E("connect to forward address failed");
    //   return -1;
    //}

	// send reply to client
	encodebuffer((unsigned char*)startSockBuffer, sizeof(startSockBuffer), key);
    if (sizeof(startSockBuffer) != send(clifd, startSockBuffer, sizeof(startSockBuffer), 0)){
       close(clifd);
       close(remote);
       LOG_E("send second msg to client failed");
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
    LOG_I("connection [%d-%d] established.", fd, sockinfos[fd].dstfd);
    return 0;
}

// 0-Succ
// 1-Close
// -1-Error Occure
int proc_accept(int srvfd, int& clifd, int key)
{
    if ((clifd = accept(srvfd, (sockaddr*)&cli, &clilen)) < 0){
        LOG_E("accept error[%d], ignored!", errno);
        return -1;
    }
	sockinfos[clifd] = sockinfo(clifd, 0);
	settimeout(clifd, TIME_OUT_MSG);
    LOG_I("recv conn %d.", clifd); 
    return 0;
}
int proc_recv(int fd, int key)
{
    int dstfd = sockinfos[fd].dstfd;
	buffinfo& tbuf = sockinfos[fd].tbuf;
    // LOG_E("proc recv param: %d,%d,%d", curr, remote, key);
    if ((tbuf.recvn = recv(fd, tbuf.buff, BUFFER_LENGTH, 0)) < 0)
    {
        LOG_E("---recv error[%d] occur, ignored!", errno);
        return -1;
    }
    LOG_I("recv from %d, send to %d, length:%d", fd, dstfd, tbuf.recvn);
    if (tbuf.recvn == 0)
    {
       LOG_R("close by client");
       return 1;
    }
    //showmsg(buffer, n>20?20:n);
    encodebuffer((unsigned char*)tbuf.buff, tbuf.recvn, key);

    if ((tbuf.sendn=send(dstfd, tbuf.buff, tbuf.recvn, 0)) != tbuf.recvn)
    {
       LOG_E("---send to sock:%d error[%d]", dstfd, errno);
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
    sockinfos[srvfd] = sockinfo(srvfd, 9);
    encodebuffer((unsigned char*)acceptSockBuffer, sizeof(acceptSockBuffer),key);

    LOG_R("start listen ... ");
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
            LOG_E("epoll wait error[%d], ignored!", errno);
            sleep(1);
            continue;
        }
        for (int i=0; i<nfds; i++){
            struct epoll_event curr = events[i];
			int currfd = curr.data.fd;
            int state = sockinfos[currfd].state;
            LOG_I("epoll event %d, fd: %d, events: %d, state:%d", i, curr.data.fd, curr.events, state);
            if (curr.data.fd == srvfd){
                // LOG_E("process accetp: %d", srvfd);
                proc_result = proc_accept(srvfd, client, key);
                if (proc_result == 0){
                    evpool[client].data.fd = client;
                    evpool[client].events = EPOLLIN;
                    epoll_ctl(epfd, EPOLL_CTL_ADD, client, &evpool[client]);
                }
                else {
                    LOG_E("accept return abnormal: %d", proc_result);
                }
            }
            else
            {
				remote = -1;
                proc_result = -1;
				if (sockinfos[currfd].fd == -1) 
					continue;
				if (state == 0)
					proc_result = proc_recv_sock0(currfd, remote, key);
				else if (state == 1)
					proc_result = proc_recv_sock1(currfd, remote, key);
                else if (state == 3){
				    proc_result = proc_recv_sock3(currfd, key);
                    evpool[currfd].events = EPOLLIN;
                    epoll_ctl(epfd, EPOLL_CTL_MOD, currfd, &evpool[currfd]);
                }
				else if (state == 9)
				    proc_result = proc_recv(currfd, key);
                else
                    proc_result = 0;

                if (proc_result == 0){
				    if (remote != -1){
						evpool[remote].data.fd = remote;
                    	evpool[remote].events = EPOLLOUT|EPOLLERR;
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
						sockinfos[remote].fd = -1;
					}
                }

            }
        }
    }
    return 0;
}
