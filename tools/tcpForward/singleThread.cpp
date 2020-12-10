#include <sys/socket.h>
#include <sys/select.h>
#include <sys/types.h>
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
#define TIME_OUT 30
#define TIME_OUT_MSG 15
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
#define setsendtimeout(sock, second) settimeout(sock, second, SO_SNDTIMEO)
void setnonblock(int fd)
{
  int flag = fcntl(fd, F_GETFL);
  fcntl(fd, F_SETFL, flag | O_NONBLOCK);
}

struct sockinfo{
   int current;
   int remote;
   int key;
   int (*ptr)(int, int&, int&);
};
// 0-Succ
// 1-Close
// -1-Error Occure
int proc_accept(int srvfd, int& clifd, int& remote)
{
    if ((clifd = accept(srvfd, (sockaddr*)&cli, &clilen)) < 0)
    {
        fprintf(stdout, "accept error[%d], ignored!\n", errno);
        return -1;
    }
    if ((remote = socket(PF_INET, SOCK_STREAM, 0)) < 0)
    {
       fprintf(stdout, "forward socket return failed\n");
       close(clifd);
       return -1;
    }
    if (connect(remote, (sockaddr*)(&fserv), sizeof(fserv)) < 0)
    {
       close(clifd);
       close(remote);
       fprintf(stdout, "connect to forward server failed\n");
       return -1;
    } 
    fprintf(stdout, "connection [%d-%d] established.\n", clifd, remote); 
    return 0;
}
int proc_recv(int curr, int remote, int key)
{
    int n = 0;
    fprintf(stdout, "proc recv param: %d,%d,%d\n", curr, remote, key);
    if ((n = recv(curr, buffer, BUFFER_LENGTH, 0)) < 0)
    {
        fprintf(stdout, "---recv error[%d] occur, ignored!\n", errno);
        return -1;
    }
    fprintf(stdout, "recv from sock:%d, length:%d\n", curr, n);
    if (n == 0)
    {
       fprintf(stdout, "close by client\n");
       return 1;
    }
    //showmsg(buffer, n>20?20:n);
    encodebuffer((unsigned char*)buffer, n, key);

    if (send(remote, buffer, n, 0) != n)
    {
       fprintf(stdout, "---send error[%d] occur, ignored!\n", errno);
       return -1;
    }
    return 0;
}

void setnonblock(int fd)
{
  int flag = fcntl(fd, F_GETFL);
  fcntl(fd, F_SETFL, flag | O_NONBLOCK);
}
int main(int argc, char **argv)
{
    struct sockaddr_in serv;
    int srvfd;
    unsigned char key = 0;
    char acceptSockBuffer[2] = {5, 0};
    char startSockBuffer[10] = {5, 0, 0, 1, 0, 0, 0, 0, 0, 0};
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
    key = atoi(argv[4]);
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
	struct timeval tv;
	tv.tv_sec = TIME_OUT_MSG;
	tv.tv_usec = 0;
    for (;;){
        nfds = epoll_wait(epfd, events, MAX_EVENT, TIME_OUT);
        if (nfds == -1){
            fprintf(stdout, "epoll wait error[%d], ignored!\n", errno);
            sleep(1);
            continue;
        }
        for (int i=0; i<nfds; i++){
            struct epoll_event curr = events[i];
            fprintf(stdout, "epoll event %d, fd: %d, events: %d\n", i, curr.data.fd, curr.events);
            if (curr.data.fd == srvfd){
                fprintf(stdout, "process accetp: %d\n", srvfd);
                proc_result = proc_accept(srvfd, client, remote);
                if (proc_result == 0){
                    evpool[client].data.fd = client;
                    fdmap[client] = remote;
                    fdmap[remote] = client;
		    setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof(tv));
		    setsockopt(remote, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof(tv));
                    evpool[client].events = EPOLLIN;
                    epoll_ctl(epfd, EPOLL_CTL_ADD, client, &evpool[client]);
                    evpool[remote].data.fd = remote;
                    evpool[remote].events = EPOLLIN;
                    epoll_ctl(epfd, EPOLL_CTL_ADD, remote, &evpool[remote]);
                }
                else {
                    fprintf(stdout, "accept return abnormal: %d\n", proc_result);
                }
            }
            else
            {
                fprintf(stdout, "process recv: %d\n", curr.data.fd);
                proc_result = proc_recv(curr.data.fd, fdmap[curr.data.fd], key); 
                if (proc_result != 0){
                    epoll_ctl(epfd, EPOLL_CTL_DEL, curr.data.fd, NULL);
                    epoll_ctl(epfd, EPOLL_CTL_DEL, fdmap[curr.data.fd], NULL);
					close(curr.data.fd);
					close(fdmap[curr.data.fd]);
                }
            }
        }
    }
    return 0;
}
