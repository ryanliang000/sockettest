#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "myerr.h"
//#include "openssl/ssl.h"
//#include "openssl/err.h"

#define REQUEST 32 
#define REPLY REQUEST+1024 


int OpenConn(const char* hostname, const char* port)
{
    struct sockaddr_in serv;
    int sockfd;
    if ((sockfd = socket(PF_INET, SOCK_STREAM, IPPROTO_IP)) < 0)
    {
        err_sys("socket error");
    }
    memset(&serv, 0, sizeof(serv));
    serv.sin_family = AF_INET;
    serv.sin_addr.s_addr = inet_addr(hostname);
    serv.sin_port = htons(atoi(port));
    if (connect(sockfd, (const sockaddr*)&serv, sizeof(serv)) < 0)
    {
        err_sys("connect error");
    }
    return sockfd;
}

int main(int argc, char **argv)
{
    struct sockaddr_in serv;
    char request[REQUEST], reply[REPLY];
    int sockfd, n, errno;
    int maxSendCount = 1, i;    
    if (argc < 3)
    {
        err_quit("usage: %s ipaddress port (sendtimes)", argv[0]);
    }
    if (argc == 4)
    {
        maxSendCount = atoi(argv[3]);
    }
    sockfd = OpenConn(argv[1], argv[2]); 
    
    memset(request, '1', REQUEST);
    for (i=0; i<maxSendCount; i++)
    {
        sprintf(request, "hello world...%d", i);
        if (send(sockfd, request, REQUEST, 0) != REQUEST)
        {
            err_sys("sendto error");
        }
        
        memset(&reply, 0, sizeof(reply));
        if ((n = recv(sockfd, reply, REPLY, 0)) < 0)
        {
            err_sys("recvfrom error");
        }
        fprintf(stdout, "recv: %s\n", reply);
    }
    if (shutdown(sockfd, 1) < 0)
    {
        err_sys("shutdown error");
    }

    exit(0);
}

