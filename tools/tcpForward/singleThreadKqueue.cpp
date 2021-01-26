#include <sys/socket.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/wait.h>

//#include <sys/epoll.h>
#include <sys/event.h>

#include <arpa/inet.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "myerr.h"
#include "log.h"
#include "sockproc.h"

#define BUFFER_LENGTH 65536
#define TIME_OUT 30
#define TIME_OUT_MSG 30
#include "kqueueproc.h"


struct sockaddr_in fserv;
unsigned int clilen = sizeof(fserv);
char buffer[BUFFER_LENGTH];
unsigned char encodekey = 0xA9;
int fdmap[2048];

// 0-Succ
// 1-Close
// -1-Error Occure
int proc_accept(int srvfd, int& clifd, int& remote)
{
    LOG_R("proc accept");
	struct sockaddr_in cli;
	if ((clifd = accept(srvfd, (sockaddr*)&cli, &clilen)) < 0){
        LOG_E("accept error[%d], ignored!", errno);
        return -1;
    }
    if ((remote = socket(PF_INET, SOCK_STREAM, 0)) < 0){
       LOG_E("forward socket return failed");
       close(clifd);
       return -1;
    }
    if (connect(remote, (sockaddr*)(&fserv), sizeof(fserv)) < 0){
       close(clifd);
       close(remote);
       LOG_R("connect to forward server failed");
       return -1;
    } 
    LOG_I("connection [%d-%d] established", clifd, remote); 
    return 0;
}
int cb_proc_recv(int, int);
int cb_proc_accept(int fd, int filter)
{
	LOG_I("process accept: %d", fd);
	int clifd = -1, remfd = -1;
	int proc_result = proc_accept(fd, clifd, remfd);
	if (proc_result == 0){
        regxeventfunc(clifd, xfilter_read, cb_proc_recv);
		regxeventfunc(remfd, xfilter_read, cb_proc_recv);
        fdmap[clifd]=remfd;
		fdmap[remfd]=clifd;
    }
    else {
        LOG_E("accept return abnormal: %d", proc_result);
    }
	return proc_result;
}
int proc_recv(int curr, int remote, int key)
{
    int n = 0;
    LOG_I("proc recv param: %d,%d,%d", curr, remote, key);
    if ((n = recv(curr, buffer, BUFFER_LENGTH, 0)) < 0)
    {
        LOG_E("---recv error[%d] occur, ignored!", errno);
        return -1;
    }
    LOG_I("recv from sock:%d, length:%d", curr, n);
    if (n == 0)
    {
       LOG_R("close by client");
       return 1;
    }
    buffer2hex(buffer, n>20?20:n);
    encodebuffer((unsigned char*)buffer, n, key);

    if (send(remote, buffer, n, 0) != n)
    {
       LOG_E("---send error[%d] occur, ignored!", errno);
       return -1;
    }
    return 0;
}
int cb_proc_recv(int fd, int filter)
{
	LOG_I("process recv: %d", fd);
	int dstfd = fdmap[fd];
    int proc_result = proc_recv(fd, dstfd, encodekey);
    if (proc_result != 0){
        unregxevent(fd);
		close(fd);
		unregxevent(dstfd);
		close(dstfd);
    }
	return 0;
}
int main(int argc, char **argv)
{
    signal(SIGPIPE, procperr);
	struct sockaddr_in serv;
    int srvfd;
    if (argc < 5)
    {
        err_quit( "Usage: %s forwardip forwardport servport encodekey\n", argv[0]);
    }
    if ((srvfd = socket(PF_INET, SOCK_STREAM, 0)) < 0)
    {
        err_sys("socket error");
    }
    memset(&serv, 0, sizeof(serv));
    serv.sin_family = AF_INET;
    serv.sin_addr.s_addr = htonl(INADDR_ANY);
    serv.sin_port = htons(atoi(argv[3]));
    memset(&fserv, 0, sizeof(fserv));
    fserv.sin_family = AF_INET;
    fserv.sin_addr.s_addr = inet_addr(argv[1]);
    fserv.sin_port = htons(atoi(argv[2]));
    encodekey = atoi(argv[4]);
	setsockreuse(srvfd);
    if (bind(srvfd, (const sockaddr*)&serv, sizeof(serv)) < 0)
    {
        err_sys("bind error");
    }
    if (listen(srvfd, 6) <0)
    {
        err_sys("listen error");
    }
    LOG_R("start listen port %s ...", argv[3]);
    int forkid = -1;
    signal(SIGCHLD,SIG_IGN);

    // init xevent
	initxevent();
    // add sock to epoll
	regxeventfunc(srvfd, xfilter_read, cb_proc_accept);

    while(true){
		dispatchxevent(TIME_OUT_MSG);	
	}
	return -1;
	
	/*
		nfds = kevent(epfd, NULL, 0, events, MAX_EVENT, &tv);
        if (nfds == -1){
            LOG_E("epoll wait error[%d], ignored!", errno);
            continue;
        }
    return 0;
	*/
}
