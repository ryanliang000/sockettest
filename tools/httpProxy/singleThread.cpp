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
char acceptConnectBuffer[] = "HTTP/1.0 200 Connection Established\r\n\r\n";
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
	LOG_T("enter sock_message");
    pthread_detach(pthread_self());
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
    pthread_exit(0);
	return NULL;
}
int cb_proc_accept(int fd, int filter)
{
    LOG_I("process accept: %d", fd);
    int clifd = -1, remfd = -1;
    int proc_result = proc_accept(fd, clifd, remfd);
    if (proc_result == 0){
        //setsockkeepalive(clifd);
        tsocks[clifd] = tsock(clifd, remfd, sock_client);
        tsocks[remfd] = tsock(remfd, clifd, sock_remote);
		// start a thread communicate with client
		tsock* pArg = &tsocks[clifd];
		if (pthread_create(&threads[clifd], NULL, sock_message, (void*)pArg)){
			LOG_E("pthread create failed!");
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
        LOG_I("pause read [%d->%d]", fd, dstfd);
        unregxevent(fd, xfilter_read);
        regxevent(dstfd, xfilter_write, cb_proc_send);
    }
    return proc_result;
}

int cb_proc_send(int fd, int filter)
{
    LOG_I("proc send: %d", fd);
    int dstfd = tsocks[fd].dstfd;
    int proc_result = sendsock(tsocks[dstfd]);
    if (proc_result < 0){
        cb_proc_close(fd, filter);
    }
    else if (proc_result == 0){
        LOG_I("restore read [%d->%d]", dstfd, fd);
        unregxevent(fd, xfilter_write);
        regxevent(dstfd, xfilter_read, cb_proc_recv);
    }
    return proc_result;
}

struct connect_request{
  std::string requesttype;
  std::string requesthost;
  int port = 0;
  bool ishostip = false;
};

// CONNECT 116.128.163.191:80 HTTP/1.1
// Host: 116.128.163.191:80
// Proxy-Connection: keep-alive
// User-Agent: MicroMessenger Client
//
// 
bool get_http_request(bool isconnectmsg, char* buff, int length, connect_request& req){
  std::string cont(buff, length);
  if (isconnectmsg){
    int pos1 = cont.find_first_of(" ");
    req.requesttype = cont.substr(0, pos1);
    int pos2 = cont.find_first_of(":", pos1);
    req.requesthost = cont.substr(pos1+1, pos2-pos1-1);
    int pos3 = cont.find_first_of(" ", pos2);
    req.port = atoi(cont.substr(pos2+1, pos3-pos2-1).c_str());
  }
  else{
    int pos1 = cont.find_first_of(" ");
    req.requesttype = cont.substr(0, pos1);
    int pos2 = cont.find_first_of(" ", pos1+1);
    req.requesthost = cont.substr(pos1+1, pos2-pos1-1);
    int pos3 = req.requesthost.find_first_of("/", 8);
    if (pos3 != -1){
      req.requesthost = req.requesthost.substr(0, pos3);
      int pos4 = req.requesthost.find_last_of("/");
      if (pos4 != -1){
        req.requesthost = req.requesthost.substr(pos4+1);
      }
      LOG_T("pos3-%d, pos4-%d", pos3, pos4); 
   }
    req.port = 80;
  }
  if (req.port <= 0){
    LOG_E("http_request: [%s]-[%s]-[%d]", req.requesttype.c_str(), req.requesthost.c_str(), req.port);
    return false;
  }
  LOG_T("http_request: [%s]-[%s]-[%d]", req.requesttype.c_str(), req.requesthost.c_str(), req.port);
  if (req.requesthost[0]>='0' and req.requesthost[0]<='9'){
    req.ishostip = true;
  }
  return true;  
}

int proc_sock(int clifd, int remote, int key)
{// recv http contents 
    LOG_T("enter proc_sock");
    //sock protocl identify
	tbuff& tbuf = tsocks[clifd].tbuf;
    
    // receive the whole http request
    tbuf.recvn = 0;
    bool checkhead = true;
    bool isconnectmsg = false;
    while(true){
      int recvn = 0;;
      if ((recvn=recv(clifd, tbuf.buff + tbuf.recvn, BUFFER_LENGTH, 0)) < 0){
        LOG_E("proc_sock client%d: recv num %d, error[%d-%s]", clifd, tbuf.recvn, errno, strerror(errno));
        return -1;
      }
      encodebuffer(tbuf.buff + tbuf.recvn, recvn, key);
      tbuf.recvn = recvn;

      if (checkhead && tbuf.recvn > 10){
        if (strncmp(tbuf.buff, "CONNECT", strlen("CONNECT")) == 0){
          isconnectmsg = true;
        }
        else if(strncmp(tbuf.buff, "GET", strlen("GET")) == 0){}
        else if(strncmp(tbuf.buff, "HEAD", strlen("HEAD")) == 0){}
        else if(strncmp(tbuf.buff, "POST", strlen("POST")) == 0){}
        else if(strncmp(tbuf.buff, "PUT", strlen("PUT")) == 0){}
        else if(strncmp(tbuf.buff, "OPTIONS", strlen("OPTIONS")) == 0){}
        else{
          LOG_E("proc_sock client%d: recv head [%s](%s) not indentify",
            clifd, std::string(tbuf.buff, 20).c_str(), buffer2hex(tbuf.buff, 10));
          return -1;
        }
        checkhead = false;
      }
      if (tbuf.recvn > 10){
        if (strncmp(tbuf.buff + tbuf.recvn - 4, "\r\n\r\n", 4) == 0){
           LOG_R("client %d recv contents: %s", clifd, std::string(tbuf.buff, tbuf.recvn).c_str());
           break;
        }
      }
    }
    
    // connect to remote
    connect_request req;
    if (!get_http_request(isconnectmsg, tbuf.buff, tbuf.recvn, req)){
      LOG_E("analyse http head failed client %d", clifd);
      return -1;
    }
    // init remote sockaddr
    struct sockaddr_in fserv;
    memset(&fserv, 0, sizeof(fserv));
    fserv.sin_family = AF_INET;
    fserv.sin_port = htons(req.port);
    if (!req.ishostip){
      if (!getsockaddrfromhost((char*)req.requesthost.c_str(), req.requesthost.length(), fserv)){
        LOG_E("proc_sock: host resolve failed");
        return -1;
      }
    }
    else{
      fserv.sin_addr.s_addr = inet_addr(req.requesthost.c_str());
      LOG_T("connect by ipaddr: %s", std::string(inet_ntoa(fserv.sin_addr)).c_str());
    }
    if (connect(remote, (sockaddr*)(&fserv), sizeof(fserv)) < 0){
        LOG_R("connect to forward server failed");
        return -1;
    }
    
   // send first message 
   if (isconnectmsg){ 
      // send reply to client
      // char sendbuff[] =  "HTTP/1.0 200 Connection Established\r\n\r\n";
      // int sendn = send(clifd, sendbuff, strlen(sendbuff), 0);
      int sendn = send(clifd, acceptConnectBuffer, strlen(acceptConnectBuffer), 0);
      if (sendn == strlen(acceptConnectBuffer)){
        return 0;
      }   
   }
   else{
      // send first request to remote
      std::string sendcont(tbuf.buff, tbuf.recvn);
      int sendn = send(remote, tbuf.buff, tbuf.recvn, 0);
      if (sendn == tbuf.recvn){
        return 0;
      }
   }

   LOG_E("client %d todo", clifd);
   return -1;
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
	encodebuffer(acceptConnectBuffer, strlen(acceptConnectBuffer), encodekey);

    // set reuse addr and port
    setsockreuse(srvfd);
    setsockkeepalive(srvfd);

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
