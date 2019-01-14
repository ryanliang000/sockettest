#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "myerr.h"

#define REQUEST 32 
#define REPLY REQUEST+32 

int main(int argc, char **argv)
{
    struct sockaddr_in serv;
    char request[REQUEST], reply[REPLY];
    int sockfd, n, errno;
    int maxSendCount = 1;    
    if (argc < 3)
    {
        err_quit("Usage: %s ipaddress port", argv[0]);
    }
    if (argc == 4)
    {
        maxSendCount = atoi(argv[3]);
    }
    
    if ((sockfd = socket(PF_INET, SOCK_DGRAM, IPPROTO_IP)) < 0)
    {
        err_sys("socket error");
    }

    memset(&serv, 0, sizeof(serv));
    serv.sin_family = AF_INET;
    serv.sin_addr.s_addr = inet_addr(argv[1]);
    serv.sin_port = htons(atoi(argv[2]));
    
    for (int i=0; i<maxSendCount; i++)
    {
        sprintf(request, "hello world...%d", i);
        if (sendto(sockfd, request, REQUEST, 0, (const sockaddr*)&serv, sizeof(serv)) != REQUEST)
        {
            err_sys("sendto error");
        }
    }

    for (int i=0; i<maxSendCount; i++)
    {
        memset(&reply, 0, sizeof(reply));
        if ((n = recv(sockfd, reply, REPLY, 0)) < 0)
        {
            err_sys("recvfrom error");
        }
        fprintf(stdout, "recv: %s\n", reply);
    }


    return 0;
}

