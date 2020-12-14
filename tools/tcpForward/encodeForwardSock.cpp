#include <sys/socket.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "myerr.h"
#include "log.h"
#include "pub.h"
#define BUFFER_LENGTH 65536
#define TIME_OUT 60
int main(int argc, char **argv)
{
    struct sockaddr_in serv, cli, fserv;
    int srvfd, n, num, clifd, fsrvfd;
    char buffer[BUFFER_LENGTH];
    fd_set fdset;
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
    num=0;
    int forkid = -1;
    unsigned int clilen = sizeof(cli);
    signal(SIGCHLD,SIG_IGN);
    for (;;)
    {
        LOG_R("wait for connection...");
        if ((clifd = accept(srvfd, (sockaddr*)&cli, &clilen)) < 0)
        {
            num++;
			LOG_E("[%d] accept error[%d] occur, ignored!", num, errno);
            sleep(3000);
            continue;
        }
		num = 0;
        LOG_R("[accetp connect %d]", clifd);
        
        // muti processes
        forkid = fork();
        if (forkid == 0)
        { // main process - fork succ
           close(clifd);
           continue;
        }   
        else if (forkid < 0)
        { // main process - fork failed 
           LOG_E("error occur on fork");
		   close(clifd);
		   continue;
        }  
		// child process
		close(srvfd);
		
		if ((fsrvfd = socket(PF_INET, SOCK_STREAM, 0)) < 0)
        {
           LOG_E("forward socket return failed");
           close(clifd);
           break;
        } 
		if (connect(fsrvfd, (sockaddr*)(&fserv), sizeof(fserv)) < 0)
        {
           close(clifd);
		   close(fsrvfd);
           LOG_E("connect to forward server failed");
           break;
        }
       
        // identify sock message
        if ((n = recv(clifd, buffer, BUFFER_LENGTH, 0)) < 3){
            LOG_E("receive from client failed");
            close(clifd);
            close(fsrvfd);
            break;
        }
        if (n == 3 && buffer[0] == 5 && buffer[1] == 1 && buffer[2] == 0){
            send(clifd, acceptSockBuffer, sizeof(acceptSockBuffer), 0);
        }
        else{
           LOG_E("receive request sock type not 0x050100");
           close(clifd);
           close(fsrvfd);
           break;
        }
        
        // set time out
		settimeout(clifd, TIME_OUT);
		settimeout(fsrvfd, TIME_OUT);
        
		// wait for message
        LOG_I("clifd: %d, fsrvfd: %d", clifd, fsrvfd);
        int rt = 0;
        int maxfd = clifd > fsrvfd ? clifd+1 : fsrvfd+1;
        //printf("maxfd=%d\n", maxfd);
        while(true)
        {
            FD_ZERO(&fdset);
            FD_SET(clifd, &fdset);
            FD_SET(fsrvfd, &fdset);
            if ((rt=select(maxfd, &fdset, NULL, NULL, &tv)) == 0)
            {
                LOG_R("select timeout");
                break;
            }
            if (rt < 0)
            {
                LOG_E("select error occor");
                break;
            }
            n = 0;
            // receive message from client
            if (FD_ISSET(clifd, &fdset))
            {
               if ((n = recv(clifd, buffer, BUFFER_LENGTH, 0)) < 0)
               {
                  LOG_E("[n=%d]recv from client error[%d] occur, ignored!", n, errno);
                  break;
               }
			   LOG_I("recv from client:%d, length:%d", clifd,n);
               if (n == 0)
               {
                  LOG_R("close by client");
                  break;
               }
               encodebuffer((unsigned char*)buffer, n, key);
               if (send(fsrvfd, buffer, n, 0) != n)
               {
                   LOG_E("[n=%d]send to remote error[%d] occur, ignored!", num, errno);
                   break;
               }
               //showMsg(buffer, n);
            }

            // receive message from forward server
            if (FD_ISSET(fsrvfd, &fdset))
            {
                if ((n = recv(fsrvfd, buffer, BUFFER_LENGTH, 0)) < 0)
                {
                   LOG_E("[n=%d]recv from remote error[%d] occur", n, errno);
                   break;
                }
				LOG_I("recv from remote:%d, length:%d", fsrvfd,n);
                if (n == 0)
                {
                   LOG_R("close by remote");
                   break;
                }
                encodebuffer((unsigned char*)buffer, n, key);
                if (send(clifd, buffer, n, 0) != n)
                {
                   LOG_E("[n=%d]send to client error[%d] occur", num, errno);
                   break;
                }
                //showMsg(buffer, n);
            } 
        }
        close(clifd);
        close(fsrvfd);
        break;
    }
    
    return 0;
}

