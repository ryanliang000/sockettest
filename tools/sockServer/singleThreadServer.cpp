#include <sys/socket.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/epoll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "myerr.h"
#include "log.h"
#include "pub.h"
#define BUFFER_LENGTH 65536
#define TIME_OUT 60
#define TIME_OUT_MSG 60
#define MAX_EVENT 256

struct sockaddr_in cli, fserv;
unsigned int clilen = sizeof(cli);
struct buffinfo{
	char buff[BUFFER_LENGTH];
	int recvn;
	int sendn;
	buffinfo():recvn(0),sendn(0){}
	void reset(){recvn=0;sendn=0;}
};
struct sockinfo{
   int fd;
   int dstfd;
   int state;
   buffinfo tbuf;
   sockinfo():fd(-1),dstfd(-1),state(-1){}
   void reset(){fd=-1,dstfd=-1,state=-1;}
   void reset(int _fd, int _state){fd=_fd,state=_state,dstfd=-1;}
   void reset(int _fd, int _dstfd, int _state){fd=_fd,dstfd=_dstfd,state=_state;}
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
       LOG_E("proc_sock %d: recv num[%d], error[%d]", clifd, tbuf.recvn, errno); 
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
	sockinfos[clifd].reset(clifd, remote, 2);
	sockinfos[remote].reset(remote, clifd, 3);
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
    return 0;
}
int proc_recv_sock2(int fd, int key)
{// not connect to remote, ignore
	return 0;
}
int proc_recv_sock3(int fd, int key)
{// connect to remote now, set flag to both client and remote
	int dstfd = sockinfos[fd].dstfd;
    setnonblock(fd);
	setnonblock(dstfd);
	sockinfos[fd].state    = 12;
	sockinfos[dstfd].state = 11;
    LOG_I("connection [%d-%d] established.", fd, dstfd);
    return 0;
}

//proc_reuslt: 0-normal, -1-error, -2-close, 1-part sended
int proc_accept(int srvfd, int& clifd, int key)
{
    if ((clifd = accept(srvfd, (sockaddr*)&cli, &clilen)) < 0){
        LOG_E("accept error[%d], ignored!", errno);
        return -1;
    }
	sockinfos[clifd].reset(clifd, 0);
    LOG_I("recv conn %d.", clifd); 
    return 0;
}

#define ISCLIENT(fd) (sockinfos[fd].state == 11)
//proc_reuslt: 0-normal, -1-error, -2-close, 1-part sended
int proc_recv(int fd, int key)
{
    int dstfd = sockinfos[fd].dstfd;
	buffinfo& tbuf = sockinfos[fd].tbuf;
    if ((tbuf.recvn = recv(fd, tbuf.buff, BUFFER_LENGTH, 0)) < 0)
    {
        LOG_E("---recv from %d error[%d] occur, ignored!", fd, errno);
        return -1;
    }
    const char* label = ISCLIENT(fd) ? "client" : "remote"; 
    LOG_I("recv from %s-%d, send to %d, length:%d", label, fd, dstfd, tbuf.recvn);
    if (tbuf.recvn == 0)
    {
       LOG_I("close by %s", label);
       return -2;
    }
    encodebuffer((unsigned char*)tbuf.buff, tbuf.recvn, key);
	
	int num = send(dstfd, tbuf.buff, tbuf.recvn, 0);
	if (num < 0){
		if (errno == EAGAIN || errno == EWOULDBLOCK){
			tbuf.sendn = 0;
			return 1;
		}
		LOG_E("proc_recv %s: %d->%d send error[%d]", label, fd, dstfd, errno);
		return -1;
	}
    else if (num < tbuf.recvn)
    {
       tbuf.sendn = num;
	   return 1;
    }
    return 0;
}
int proc_send(int fd, int key)
{
    const char *label = ISCLIENT(fd) ? "client":"remote";
    sockinfo& info = sockinfos[fd];
	sockinfo& dstinfo = sockinfos[info.dstfd];
	buffinfo& tbuf = dstinfo.tbuf;
	char* buff = tbuf.buff + tbuf.sendn;
	int leftn = tbuf.recvn - tbuf.sendn;
	int num = send(fd, buff, leftn, 0);
	if (num < 0){
		LOG_E("proc_send: %s-%d, send error[%d]", label, fd, errno);
		return -1;
	}
	else if (num < leftn){
		LOG_D("proc_send part: %s-%d, len=%d", label, fd, num);
        tbuf.sendn += num;
		return 1;
	}
    LOG_D("proc_send succ: %s-%d, len=%d", label, fd, num);
	return 0;
}

void removeepollfd(epoll_event* evpoll, int epfd, int fd, bool closefd=true)
{
    epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL);
    if (closefd) close(fd);
    sockinfos[fd].reset(); 
    int dstfd = sockinfos[fd].dstfd;
    if (dstfd != -1){
        epoll_ctl(epfd, EPOLL_CTL_DEL, dstfd, NULL);
        if (closefd) close(dstfd);
        sockinfos[dstfd].reset();
    }
}
void addepollflag(epoll_event* evpoll, int epfd, int fd, int flag)
{
    evpoll[fd].events |= flag;
    epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &evpoll[fd]);
}
void delepollflag(epoll_event* evpoll, int epfd, int fd, int flag)
{
    evpoll[fd].events ^= flag;
    epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &evpoll[fd]);
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
    sockinfos[srvfd].reset(srvfd, 11);
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
    struct epoll_event srvev, evpoll[2048], events[MAX_EVENT];
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
			int event = curr.events;
            LOG_I("epoll event %d, fd: %d, events: %d, state:%d", i, curr.data.fd, curr.events, state);
            if (curr.data.fd == srvfd){
                proc_result = proc_accept(srvfd, client, key);
                if (proc_result == 0){
                    evpoll[client].data.fd = client;
                    evpoll[client].events = EPOLLIN|EPOLLERR;
                    epoll_ctl(epfd, EPOLL_CTL_ADD, client, &evpoll[client]);
                }
                else {
                    LOG_E("accept return abnormal: %d", proc_result);
                }
				continue;
            }
            if (sockinfos[currfd].fd == -1){
               LOG_E("[fd-%d, event-%d]socket already closed!", currfd, event); 
               continue;
            }
            if (event & EPOLLERR){
				LOG_E("event error[%d], close conntions", event);
                removeepollfd(evpoll, epfd, currfd);
				continue;
			}
			if (state < 10){ // socks connection msg 
				remote = -1;
                proc_result = -1;
				if (sockinfos[currfd].fd == -1) 
					continue;
				if (state == 0){
					proc_result = proc_recv_sock0(currfd, remote, key);
                }
				else if (state == 1)
					proc_result = proc_recv_sock1(currfd, remote, key);
				else if (state == 2)
					proc_result = 0;
                else if (state == 3){
				    proc_result = proc_recv_sock3(currfd, key);
                    addepollflag(evpoll, epfd, sockinfos[currfd].dstfd, EPOLLIN);
                    evpoll[currfd].events = EPOLLIN|EPOLLERR;
                    epoll_ctl(epfd, EPOLL_CTL_MOD, currfd, &evpoll[currfd]);
                }
                if (proc_result == 0){ // proc finish and succ
				    if (remote != -1){
                        delepollflag(evpoll, epfd, currfd, EPOLLIN);
						evpoll[remote].data.fd = remote;
                    	evpoll[remote].events = EPOLLOUT|EPOLLERR;
                    	epoll_ctl(epfd, EPOLL_CTL_ADD, remote, &evpoll[remote]);	
				    }
				}
				else{
					removeepollfd(evpoll, epfd, currfd);
                }
				continue;
            }
			if (event & EPOLLIN){ // recv and send data
                proc_result = proc_recv(currfd, key);
                if (proc_result < 0)
                    removeepollfd(evpoll, epfd, currfd);
                if (proc_result == 1){ // send block then recv should block
                    int dstfd = sockinfos[currfd].dstfd;
                    delepollflag(evpoll, epfd, currfd, EPOLLIN);
                    addepollflag(evpoll, epfd, dstfd, EPOLLOUT);
                }
                else if (proc_result == 2){ // send succ, then remove recv block
                    int dstfd = sockinfos[currfd].dstfd;
                }
                if (event & EPOLLOUT){
                    proc_result = proc_send(currfd, key);
                }
            }
            if (event & EPOLLOUT){
				proc_result = proc_send(currfd, key);
				if (proc_result < 0)
                    removeepollfd(evpoll, epfd, currfd);
				else if (proc_result == 0){
					int dstfd = sockinfos[currfd].dstfd;
					delepollflag(evpoll, epfd, currfd, EPOLLOUT);
					addepollflag(evpoll, epfd, dstfd, EPOLLIN);
				}
            }
        }
    }
    return 0;
}
