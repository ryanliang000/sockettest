#include <sys/socket.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "myerr.h"
#include "log.h"
#include "sockproc.h"

#define BUFFER_LENGTH 65536
#define TIME_OUT_MSG 30
#include "kqueueproc.h"


struct sockaddr_in fserv;
unsigned int clilen = sizeof(fserv);
char buffer[BUFFER_LENGTH];
unsigned char encodekey = 0xA9;
tsock tsocks[2048];

// accept connection
int cb_proc_accept(int, int);
// remote conn ok
int cb_proc_conn(int, int);
// recv from fd and send to dest
int cb_proc_recv(int, int);
// send msg to fd
int cb_proc_send(int, int);
// proc except
int cb_proc_error(int, int);

// 0-Succ
// 1-Wait remote connect
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
	setnonblock(clifd);
	setnonblock(remote);
    if (connect(remote, (sockaddr*)(&fserv), sizeof(fserv)) < 0){
       if (errno == EINPROGRESS){
	      return 1;	
	   }
	   else{
	   	  close(clifd);
       	  close(remote);
       	  LOG_R("connect to forward server failed");
       	  return -1;
	   }
    } 
    LOG_R("connection [%d-%d] established", clifd, remote); 
    return 0;
}
int cb_proc_accept(int fd, int filter)
{
	LOG_I("process accept: %d", fd);
	int clifd = -1, remfd = -1;
	int proc_result = proc_accept(fd, clifd, remfd);
	if (proc_result == 0){
        regxevent(clifd, xfilter_read, cb_proc_recv);
		regxevent(remfd, xfilter_read, cb_proc_recv);
		//regxevent(clifd, xfilter_error, cb_proc_error);
		//regxevent(remfd, xfilter_error, cb_proc_error);
		tsocks[clifd] = tsock(clifd, remfd, sock_client);
		tsocks[remfd] = tsock(remfd, clifd, sock_remote);
    }
    else if(proc_result == 1){
		tsocks[clifd] = tsock(clifd, remfd, sock_client);
		tsocks[remfd] = tsock(remfd, clifd, sock_remote);
		regxevent(remfd, xfilter_write, cb_proc_conn);
		//regxevent(remfd, xfilter_error, cb_proc_error);
		//regxevent(clifd, xfilter_error, cb_proc_error);
	}
	else{
        LOG_E("accept return abnormal: %d", proc_result);
    }
	return proc_result;
}
int cb_proc_conn(int fd, int filter)
{
	LOG_I("proc conn: %d", fd);
	int dstfd = tsocks[fd].dstfd;
	unregxevent(fd, xfilter_write);
	regxevent(fd, xfilter_read, cb_proc_recv);
	regxevent(dstfd, xfilter_read, cb_proc_recv);
	LOG_R("connection [%d-%d] established", dstfd, fd);
	return 0;
}
int cb_proc_close(int fd, int filter)
{
	int dstfd = tsocks[fd].dstfd;
	if (fd != -1){
		unregxevent(fd); close(fd);
	}
	if (dstfd != -1){
		unregxevent(dstfd);close(dstfd);
	}
	tsocks[fd].reset();
	tsocks[dstfd].reset();
	LOG_R("connection [%d-%d] closed.", fd, dstfd);
	return 0;
}
int cb_proc_error(int fd, int filter)
{
	LOG_R("proc error: %d", fd);
	return cb_proc_close(fd, filter);
}
int cb_proc_recv(int fd, int filter)
{
	//LOG_I("process recv: %d", fd);
    int proc_result = recvsockandsendencoded(tsocks[fd], encodekey);
    int dstfd = tsocks[fd].dstfd;;
	if (proc_result < 0){
		cb_proc_close(fd, filter);
    }
	else if (proc_result == 1){
		unregxevent(fd, xfilter_read);
		regxevent(dstfd, xfilter_write, cb_proc_send);
	}
	return proc_result;
}
int cb_proc_send(int fd, int filter)
{
	LOG_I("proc send: %d", fd);
	int proc_result = sendsock(tsocks[fd]);
	int dstfd = tsocks[fd].dstfd;
	if (proc_result < 0){
		cb_proc_close(fd, filter);
	}
	else if (proc_result == 0){
		unregxevent(fd, xfilter_write);
		regxevent(dstfd, xfilter_read, cb_proc_recv);
	}
	return proc_result;
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
	regxevent(srvfd, xfilter_read, cb_proc_accept);

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
