#include <sys/socket.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "myerr.h"
#include "xevent.h"
#include "sockproc.h"
#define BUFFER_LENGTH 1024 * 24
#define TIME_OUT 60*2
#define TIME_OUT_MSG 60
#define MAX_EVENT 256

struct sockaddr_in cli, fserv;
unsigned int clilen = sizeof(cli);
unsigned int fdnums = 0;
tsock tsocks[1024];
char acceptSockBuffer[2] = {5, 0};
unsigned char encodekey = 0xA9;
pthread_t threads[1024];

// accept connection
int cb_proc_accept(int, int);
// remote conn ok
int cb_proc_remote_conn(int, int);
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
    LOG_R("start connection message[%d-%d]", clifd, remote);
    return 0;
}

int proc_sock(int clifd, int remote, int key);
void* sock_message(void* pArg)
{
	tsock* p = (tsock*)pArg;
    int clifd = p->fd;
	int remote = p->dstfd;
	if (proc_sock(clifd, remote, encodekey) == 0){
	    regxevent(clifd, xfilter_read, cb_proc_recv);
    	regxevent(remote, xfilter_read, cb_proc_recv);
    	LOG_R("connection [%d-%d] established", clifd, remote);
	}
	else{
		tsocks[clifd].reset();
		tsocks[remote].reset();
		close(clifd);
		close(remote);
		LOG_R("connection [%d-%d] release", clifd, remote);
	}
	return NULL;
}
int cb_proc_accept(int fd, int filter)
{
    LOG_I("process accept: %d", fd);
    int clifd = -1, remfd = -1;
    int proc_result = proc_accept(fd, clifd, remfd);
    if (proc_result == 0){
        tsocks[clifd] = tsock(clifd, remfd, sock_client);
        tsocks[remfd] = tsock(remfd, clifd, sock_remote);
		// start a thread communicate with client
		tsock* pArg = &tsocks[clifd];
		if (pthread_create(&threads[clifd], NULL, sock_message, (void*)pArg)){
			close(clifd);
			close(remfd);
		}
    }
    return proc_result;
}

int cb_proc_remote_conn(int fd, int filter)
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

int proc_sock2_buff(int clifd, int remote, int key);
int proc_sock2(int clifd, int remote, int key);
int proc_sock(int clifd, int remote, int key)
{// recv 5,1,0 or 5,2,1,0 reply 5,0
    //sock protocl identify
	tbuff& tbuf = tsocks[clifd].tbuf;
    if ((tbuf.recvn = recv(clifd, tbuf.buff, BUFFER_LENGTH, 0)) < 3){ 
       LOG_E("proc_sock client%d: recv num %d, error[%d-%s]", clifd, tbuf.recvn, errno, strerror(errno)); 
       return -1; 
    } 
	encodebuffer((unsigned char*)tbuf.buff,tbuf.recvn,key);
	char* buff = tbuf.buff;
    if (tbuf.recvn < 10){// first version check msg
       if (buff[0] == 5 && ((buff[1] == 1 && buff[2] == 0) || buff[1] == tbuf.recvn - 2)){
          tbuf.sendn = send(clifd, acceptSockBuffer, sizeof(acceptSockBuffer), 0);
       }
       else{
          LOG_E("proc_sock:receive request sock type not 0x050100");
          return -1;
       }
    }
	else{
		LOG_I("recv sock0-sock1 bind msg");
        return proc_sock2_buff(clifd, remote, key);
	}
	return proc_sock2(clifd, remote, key);
}
int proc_sock2(int clifd, int remote, int key)
{// recv 5,1..host,port reply 5,0,0,1,ip,port
    tbuff& tbuf = tsocks[clifd].tbuf; 
	if ((tbuf.recvn = recv(clifd, tbuf.buff, BUFFER_LENGTH, 0)) < 10){
        LOG_E("proc_sock2:receive start sock from client failed, len:%d", tbuf.recvn);
        return -1;
    }
    encodebuffer((unsigned char*)tbuf.buff, tbuf.recvn,key);
    return proc_sock2_buff(clifd, remote, key);
}
int proc_sock2_buff(int clifd, int remote, int key)
{
    char startSockBuffer[10] = {5, 0, 0, 1, 0, 0, 0, 0, 0, 0};
    tbuff& tbuf = tsocks[clifd].tbuf;
	char* buff = tbuf.buff;
	// init remote
	struct sockaddr_in fserv;
    memset(&fserv, 0, sizeof(fserv));
    fserv.sin_family = AF_INET;

	// get remote ip addr
	if (buff[0] == 5 && buff[1] == 1 && buff[2] == 0){//second address send msg
       if (buff[3] == 3){
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
       LOG_E("proc_sock_:receive sock start type not 0x050100");
       return -1;
    }
    memcpy(startSockBuffer+4, &fserv.sin_addr.s_addr, 4);
    memcpy(startSockBuffer+8, &fserv.sin_port, 2);
	LOG_I("get remote ip addr succ");

    //reply
    encodebuffer((unsigned char*)startSockBuffer, sizeof(startSockBuffer), key);
    if (sizeof(startSockBuffer) != send(clifd, startSockBuffer, sizeof(startSockBuffer), 0)){
       LOG_E("proc_sock_:fd[%d-%d]send 2nd msg fail[%d-%s]", clifd, remote, errno, strerror(errno));
       return -1;
    }
    LOG_I("send reply 2 succ");

	// connect to remote
    int ret = connect(remote, (sockaddr*)(&fserv), sizeof(fserv));
    if (ret != 0){
        LOG_E("proc_sock_:fd[%d-%d] forward connect failed[%d-%s]", clifd, remote, errno, strerror(errno));
        return -1;
    }
    LOG_I("conn to remote succ");
	return 0;
}

int main(int argc, char **argv)
{
    signal(SIGPIPE, procperr);
    signal(SIGCHLD, SIG_IGN);

    struct sockaddr_in serv;
    int srvfd;
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
    encodekey = atoi(argv[2]);
    memset(&fserv, 0, sizeof(fserv));
    fserv.sin_family = AF_INET;
	
	// encode accept buffer
	encodebuffer(acceptSockBuffer, sizeof(acceptSockBuffer), encodekey);

    // set reuse addr and port
	int opt=1;
    setsockopt(srvfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    setsockopt(srvfd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
    
	// bind and listen
	if (bind(srvfd, (const sockaddr*)&serv, sizeof(serv)) < 0)
    {
        err_sys("bind error");
    }
    if (listen(srvfd, 6) <0)
    {
        err_sys("listen error");
    }
    
	// init xevent
    initxevent();
	
	// add sock to epoll
	regxevent(srvfd, xfilter_read, cb_proc_accept);
    
	while(true){
        dispatchxevent(TIME_OUT_MSG);
    }
    return -1;
}
