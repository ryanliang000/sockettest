#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "myerr.h"

#define REQUEST 32 
#define REPLY REQUEST+1024 
#define UPDATE_INTERVAL 300
#define FAIL_RETRY_INTERVAL 60
int OpenConn(const char* hostname, const char* port)
{
    struct sockaddr_in serv;
    int sockfd;
    if ((sockfd = socket(PF_INET, SOCK_STREAM, IPPROTO_IP)) < 0)
    {
        return -1;
    }
    memset(&serv, 0, sizeof(serv));
    serv.sin_family = AF_INET;
    serv.sin_addr.s_addr = inet_addr(hostname);
    serv.sin_port = htons(atoi(port));
    if (connect(sockfd, (const sockaddr*)&serv, sizeof(serv)) < 0)
    {
        return -1;
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
        err_quit("usage: %s ipaddress port", argv[0]);
    }
    
    memset(request, 'u', REQUEST);
    for (;;)
    {
        sockfd = OpenConn(argv[1], argv[2]);
		if (sockfd == -1)
		{
		   fprintf(stdout, "connet failed, will try later.\n");
		   sleep(FAIL_RETRY_INTERVAL);
		   continue;
		}
		sprintf(request, "update");
		fprintf(stdout, "send: [%s]\n", request);
        if (send(sockfd, request, REQUEST, 0) != REQUEST)
        {
            shutdown(sockfd, 2);
			fprintf(stdout, "send message failed, will try later.");
			sleep(FAIL_RETRY_INTERVAL);
            continue;
		}
        
        memset(&reply, 0, sizeof(reply));
        if ((n = recv(sockfd, reply, REPLY, 0)) < 0)
        {
		    shutdown(sockfd, 2);
			fprintf(stdout, "recv message failed, will try later.");
			sleep(FAIL_RETRY_INTERVAL);
            continue;
		}
        fprintf(stdout, "recv: [%s]\n", reply);
        shutdown(sockfd, 2);
		fprintf(stdout, "will update after %d seconds\n", UPDATE_INTERVAL);
		sleep(UPDATE_INTERVAL);
    }
    if (shutdown(sockfd, 1) < 0)
    {
        err_sys("shutdown error");
    }

    exit(0);
}

