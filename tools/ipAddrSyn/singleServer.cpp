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
#include "log.h"
#include "myerr.h"
#include "pub.h"
#include "xevent.h"
extern int errno;

const int MAXFD = 4096;
struct sockaddr_in s_locals[MAXFD];
struct sockaddr_in s_local;

int cb_proc_accept(int, int);
int cb_proc_recv(int, int);

int cb_proc_accept(int srvfd, int filter){
    LOG_I("process accept: %d", srvfd);
    int clifd = -1; 
    struct sockaddr_in cli;
    unsigned int clilen=sizeof(cli);
    if ((clifd = accept(srvfd, (sockaddr*)&cli, &clilen)) < 0){
        LOG_E("accept[clifd-%d-clilen-%d] error[%d-%s], ignored!", clifd, clilen, errno, strerror(errno));
        return -1;
    }
    setsockkeepalive(clifd);
    s_locals[clifd % MAXFD] = cli;
    regxevent(clifd, xfilter_read, cb_proc_recv);
    settimeout(clifd, 60);
    return 0;
}

int proc_close(int fd, int filter=-1){
  if (fd != -1){
    unregxevent(fd); 
    close(fd);
  }
  return 0;
}

int cb_proc_recv(int fd, int filter){
  char request[1024] = {0}; 
  char reply[256] = {0};
  int n = 0;
  if ((n=recv(fd,request,1024,0))<=0){
     proc_close(fd);
     return -1;
  }
  if (request[0] == 'u'){ // update store address
     s_local = s_locals[fd];
     sprintf(reply, "%s", inet_ntoa(s_local.sin_addr));
  }
  else if (request[0] == 'g'){ // get address message
     sprintf(reply, "%s", inet_ntoa(s_local.sin_addr));
  }
  else if (memcmp(request, "GET", 3) == 0){
     if (memcmp(request + 4, "/ip", 3) == 0 || memcmp(request+4, "/get", 4) == 0){ 
      sprintf(reply, "HTTP/1.1 200 OK\r\nContent-Type:text/html\r\nConnection:close\r\n\r\n");
      sprintf(reply+strlen(reply), "<h1>stored-%s</h1>", inet_ntoa(s_local.sin_addr));
      sprintf(reply+strlen(reply), "<h1>local-%s</h1>", inet_ntoa(s_locals[fd].sin_addr));
     }
     else if (memcmp(request + 4, "/set", 4) == 0 || memcmp(request+4, "/store", 6) == 0){
      sprintf(reply, "HTTP/1.1 200 OK\r\nContent-Type:text/html\r\nConnection:close\r\n\r\n");
      s_local = s_locals[fd];
      sprintf(reply+strlen(reply), "<h1>store-%s</h1>", inet_ntoa(s_local.sin_addr));
     }
  }
  else{
     proc_close(fd);
     return -1;
  }
  if (send(fd, reply, strlen(reply) + 1, 0) != strlen(reply) + 1){
    fprintf(stdout, "reply send error occur, ignored!\n");
  }
  proc_close(fd);
  return 0;
}


int main(int argc, char** argv){
    signal(SIGPIPE, procperr);
    signal(SIGCHLD, SIG_IGN);
    if (argc < 2){
        err_quit( "Usage: %s servport\n", argv[0]);
    }
    int srvfd, fsrvfd, clifd;
    if ((srvfd = socket(PF_INET, SOCK_STREAM, 0)) < 0){
        err_sys("socket error");
    }
    char* servport = argv[1];

    struct sockaddr_in serv = {0};
    serv.sin_addr.s_addr = htonl(INADDR_ANY);
    serv.sin_port = htons(atoi(servport));
    setsockreuse(srvfd); 
    if (bind(srvfd, (const sockaddr*)&serv, sizeof(serv)) < 0) {
        err_sys("bind error");
    }
    if (listen(srvfd, 1) <0){
        err_sys("listen error");
    }
    LOG_R("start listen port %s ...", servport);
    
	initxevent();
    regxevent(srvfd, xfilter_read, cb_proc_accept);
    while(true){
       dispatchxevent(30);
    }
    return 0;
}

