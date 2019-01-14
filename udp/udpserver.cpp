#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "myerr.h"
#define REQUEST 128
#define REPLY 160 

int main(int argc, char **argv)
{
    struct sockaddr_in serv, cli;
    char request[REQUEST], reply[REPLY];
    int sockfd, n, num;
    unsigned int clilen;
    
    if (argc < 2)
    {
        err_quit( "usage: %s port\n", argv[0]);
    }

    if ((sockfd = socket(PF_INET, SOCK_DGRAM, 0)) < 0)
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
    
    num=0;
    for (;;)
    {
        fprintf(stdout, "wait for message...\n");
        clilen = sizeof(cli);
        memset(&request, 0, sizeof(request));
        if ((n = recvfrom(sockfd, request, REQUEST, 0, (sockaddr*)&cli, &clilen)) < 0)
        {
            err_sys("recvfrom error");
        }
        request[REQUEST-1]='\0';
        fprintf(stdout, "recv: %s\n", request);
        fprintf(stdout, "reply addr: %s:%d\n", inet_ntoa(cli.sin_addr), ntohs(cli.sin_port));
        sprintf(reply, "No.=%d, msg=%s ", ++num, request);
        reply[REPLY-1]='\0';
        fprintf(stdout, "send: %s\n", reply);
        if (sendto(sockfd, reply, sizeof(reply), 0, (const sockaddr*)&cli, clilen) != REPLY)
        {
            err_sys("sendto error");
        }
    }
    
    return 0;
}

