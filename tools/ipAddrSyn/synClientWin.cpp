//#include <sys/socket.h>
//#include <arpa/inet.h>
#include <winsock.h> 
#pragma comment(lib, "ws2_32.lib") 

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
//#include "myerr.h"

#define REQUEST 32 
#define REPLY REQUEST+1024 
#define UPDATE_INTERVAL 600
#define FAIL_RETRY_INTERVAL 60
#define sleep(var) Sleep(var * 1000)
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
    char request[REQUEST], reply[REPLY];
    int sockfd, n, errno;
    int maxSendCount = 1;    
    if (argc < 3)
    {
        fprintf(stdout, "usage: %s ipaddress port\n", argv[0]);
		exit(-1);
    }
	WSAData wsa;
	if (::WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
	{
		fprintf(stdout, "WSAStartup error\n");
		exit(-1);
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

    exit(0);
}

