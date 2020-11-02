#include <sys/socket.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "myerr.h"
#define REQUEST 64
#define REPLY 160 

int main(int argc, char **argv)
{
    struct sockaddr_in serv, cli;
    char request[REQUEST], reply[REPLY];
    int sockfd, n, num, clifd;
    unsigned int clilen;
    
    // share memory bettwen process
    struct sockaddr_in *pLocal = (sockaddr_in*)mmap(0, sizeof(sockaddr_in), PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, -1, 0);
    struct sockaddr_in &local = *pLocal;

    if (argc < 2)
    {
        err_quit( "usage: %s port\n", argv[0]);
    }
    if ((sockfd = socket(PF_INET, SOCK_STREAM, 0)) < 0)
    {
        err_sys("socket error");
    }
    memset(&serv, 0, sizeof(serv));
    serv.sin_family = AF_INET;
    serv.sin_addr.s_addr = htonl(INADDR_ANY);
    serv.sin_port = htons(atoi(argv[1]));

    if (bind(sockfd, (const sockaddr*)&serv, sizeof(serv)) < 0)
    {
        err_sys("bind error");
    }
    if (listen(sockfd, 1) <0)
    {
        err_sys("listen error");
    }
    num=0;
    int recErrNum=0;
    int forkid = -1;
    clilen = sizeof(cli);
    for (;;)
    {
        fprintf(stdout, "wait for connection...\n");
        if ((clifd = accept(sockfd, (sockaddr*)&cli, &clilen)) < 0)
        {
            recErrNum++;
            fprintf(stdout, "[%d-%d]accept error[%d] occur, ignored!\n", num+1, recErrNum, errno);
            sleep(1);
            continue;
        }
        fprintf(stdout, "[accetp ok]\n");
        ++num;

        // muti processes
        forkid = fork();
        if (forkid == 0)
        {
           // main process
           close(clifd);
           continue;
        }
        else if (forkid < 0)
        {
           fprintf(stdout, "error occur on fork");
           continue;
        }
        close(sockfd);

        // set time out 
        struct timeval tv;
        tv.tv_sec = 6;
        tv.tv_usec = 0;
        setsockopt(clifd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
        
        // receive msg and send reply
        memset(&request, 0, sizeof(request));
        if ((n = recv(clifd, request, REQUEST, 0)) < 0)
        {
            fprintf(stdout, "[%d]recv error[%d] occur, ignored!\n", num, errno);
            close(clifd);
            break;
        }
        fprintf(stdout, "[recv ok]\n");
        request[REQUEST-1]='\0';
        fprintf(stdout, "[%d]recv: %s\n", num, request);
        fprintf(stdout, "[%d]reply addr: %s:%d\n", num, inet_ntoa(cli.sin_addr), htons(cli.sin_port));
	if (request[0] == 'u'){
	   // update message
	   local = cli;
           sprintf(reply, "%s", inet_ntoa(local.sin_addr));
	}
	else if (request[0] == 'g')
	{
	   // get address message
           sprintf(reply, "%s", inet_ntoa(local.sin_addr));
	}
        else if (request[0] == 'G' && request[5] == 'i' && request[6] == 'p')
        {
           sprintf(reply, "HTTP/1.1 200 OK\r\nContent-Type:text/html\r\nConnection:close\r\n\r\n<h1>%s</h1>", inet_ntoa(local.sin_addr));
        }
	else
	{
	   fprintf(stdout, "[%d]request not match any condition.\n", num, request);
           close(clifd);
           break;
	}
        reply[REPLY-1]='\0';
        fprintf(stdout, "[%d]send: %s\n", num, reply);
        if (send(clifd, reply, strlen(reply) + 1, 0) != strlen(reply) + 1)
        {
            fprintf(stdout, "[%d]reply error occur, ignored!\n", num);
        }
        fprintf(stdout, "[send ok]\n");
        shutdown(clifd, 2);
        close(clifd);
        munmap(pLocal, sizeof(sockaddr_in));
        break;
    }
    
    return 0;
}

