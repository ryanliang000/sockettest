#include <sys/socket.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "myerr.h"
#define REQUEST 1024
#define REPLY 1024

int main(int argc, char **argv)
{
    struct sockaddr_in serv, cli, fserv;
    char request[REQUEST+1], reply[REPLY+1];
    int sockfd, n, num, clifd, fsockfd;
    unsigned int clilen;
    fd_set fdset;
    FD_ZERO(&fdset);
    memset(&request, 0, sizeof(request));
    memset(&reply, 0, sizeof(reply));

    if (argc < 4)
    {
        err_quit( "Usage: %s forwardip forwardport servport\n", argv[0]);
    }
    if ((sockfd = socket(PF_INET, SOCK_STREAM, 0)) < 0)
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
        if ((fsockfd = socket(PF_INET, SOCK_STREAM, 0)) < 0)
        {
            fprintf(stdout, "socket return failed\n");
            break;
        }
        if (connect(fsockfd, (sockaddr*)(&fserv), sizeof(fserv)) < 0)
        {
           fprintf(stdout, "connect to forward server failed\n");
           break;
        }
        
        // set time out
        struct timeval tv;
        tv.tv_sec = 15;
        tv.tv_usec = 0;
        setsockopt(clifd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
        setsockopt(fsockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));       
 
        // wait for message
        FD_ZERO(&fdset);
        FD_SET(clifd, &fdset);
        FD_SET(fsockfd, &fdset);
        fprintf(stdout, "clifd: %d, fsockfd: %d\n", clifd, fsockfd);
        int rt = 0;
        int maxfd = clifd > fsockfd ? clifd : fsockfd;
        while(true)
        {
            if ((rt=select(maxfd+1, &fdset, NULL, NULL, &tv)) == 0)
            {
                fprintf(stdout, "select timeout\n");
                break;
            }
            if (rt < 0)
            {
                fprintf(stdout, "select error occor\n");
                break;
            }
            // receive message from client
            if (FD_ISSET(clifd, &fdset))
            {
               fprintf(stdout, "receive message from client\n");
               if ((n = recv(clifd, request, REQUEST, 0)) < 0)
               {
                  fprintf(stdout, "[%d]recv error[%d] occur, ignored!\n", num, errno);
                  break;
               }
               if (n == 0)
               {
                  fprintf(stdout, "close by client\n");
                  break;
               }
               if (send(fsockfd, request, n, 0) != n)
               {
                   fprintf(stdout, "[%d]send error[%d] occur, ignored!\n", num, errno);
                   break;
               }
            }

            // receive message from forward server
            if (FD_ISSET(fsockfd, &fdset))
            {
                fprintf(stdout, "receive message from forward server\n");
                if ((n = recv(fsockfd, request, REQUEST, 0)) < 0)
                {
                   fprintf(stdout, "[%d]recv error[%d] occur, ignored!\n", num, errno);
                   break;
                }
                if (n == 0)
                {
                   fprintf(stdout, "close by forward server\n");
                   break;
                }
                if (send(clifd, request, n, 0) != n)
                {
                   fprintf(stdout, "[%d]send error[%d] occur, ignored!\n", num, errno);
                   break;
                }
            } 
        }
        shutdown(clifd, 2);
        shutdown(fsockfd, 2);
        close(clifd);
        close(fsockfd);
        break;
    }
    
    return 0;
}

