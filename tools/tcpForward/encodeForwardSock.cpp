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
#define BUFFER_LENGTH 65536
#define TIME_OUT 60
void showMsg(char *buffer, int len)
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
    int recErrNum=0;
    int forkid = -1;
    unsigned int clilen = sizeof(cli);
    signal(SIGCHLD,SIG_IGN);
    for (;;)
    {
        fprintf(stdout, "wait for connection...\n");
		num++;
        if ((clifd = accept(srvfd, (sockaddr*)&cli, &clilen)) < 0)
        {
            fprintf(stdout, "[num=%d]accept error[%d] occur, ignored!\n", num,  errno);
            sleep(3);
            continue;
        }
        fprintf(stdout, "[accetp connect %d]\n", clifd);
        
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
		close(srvfd);
		
		if ((fsrvfd = socket(PF_INET, SOCK_STREAM, 0)) < 0)
        {
           fprintf(stdout, "forward socket return failed\n");
           close(clifd);
           continue;
        } 
		if (connect(fsrvfd, (sockaddr*)(&fserv), sizeof(fserv)) < 0)
        {
           close(clifd);
           fprintf(stdout, "connect to forward server failed\n");
           continue;
        }
        
        // identify sock message
        if ((n = recv(clifd, buffer, BUFFER_LENGTH, 0)) < 3){
            fprintf(stdout, "receive from client failed\n");
            close(clifd);
            close(fsrvfd);
            break;
        }
        if (n == 3 && buffer[0] == 5 && buffer[1] == 1 && buffer[2] == 0){
            send(clifd, acceptSockBuffer, sizeof(acceptSockBuffer), 0);
        }
        else{
           fprintf(stdout, "receive request sock type not 0x050100\n");
           close(clifd);
           close(fsrvfd);
           break;
        }
        
        // set time out
        struct timeval tv;
        tv.tv_sec = TIME_OUT;
        tv.tv_usec = 0;
        struct timeval clitv = tv, srvtv = tv;
        setsockopt(clifd, SOL_SOCKET, SO_SNDTIMEO, (const char*)&clitv, sizeof(tv));
        setsockopt(fsrvfd, SOL_SOCKET, SO_SNDTIMEO, (const char*)&srvtv, sizeof(tv));       
 
        // wait for message
        fprintf(stdout, "clifd: %d, fsrvfd: %d\n", clifd, fsrvfd);
        int rt = 0;
        int maxfd = clifd > fsrvfd ? clifd+1 : fsrvfd+1;
        //printf("maxfd=%d\n", maxfd);
        while(true)
        {
            tv.tv_sec = clitv.tv_sec = srvtv.tv_sec = TIME_OUT;
            FD_ZERO(&fdset);
            FD_SET(clifd, &fdset);
            FD_SET(fsrvfd, &fdset);
            if ((rt=select(maxfd, &fdset, NULL, NULL, &tv)) == 0)
            {
                fprintf(stdout, "select timeout\n");
                break;
            }
            if (rt < 0)
            {
                fprintf(stdout, "select error occor\n");
                break;
            }
            n = 0;
            // receive message from client
            if (FD_ISSET(clifd, &fdset))
            {
               if ((n = recv(clifd, buffer, BUFFER_LENGTH, 0)) < 0)
               {
                  fprintf(stdout, "[n=%d]recv from client error[%d] occur, ignored!\n", n, errno);
                  break;
               }
			   fprintf(stdout, "recv from client:%d, length:%d\n", clifd,n);
               if (n == 0)
               {
                  fprintf(stdout, "close by client\n");
                  break;
               }
               encodebuffer((unsigned char*)buffer, n, key);
               if (send(fsrvfd, buffer, n, 0) != n)
               {
                   fprintf(stdout, "[n=%d]send to remote error[%d] occur, ignored!\n", num, errno);
                   break;
               }
               //showMsg(buffer, n);
            }

            // receive message from forward server
            if (FD_ISSET(fsrvfd, &fdset))
            {
                if ((n = recv(fsrvfd, buffer, BUFFER_LENGTH, 0)) < 0)
                {
                   fprintf(stdout, "[n=%d]recv from remote error[%d] occur, ignored!\n", n, errno);
                   break;
                }
				fprintf(stdout, "recv from remote:%d, length:%d\n", fsrvfd,n);
                if (n == 0)
                {
                   fprintf(stdout, "close by remote\n");
                   break;
                }
                encodebuffer((unsigned char*)buffer, n, key);
                if (send(clifd, buffer, n, 0) != n)
                {
                   fprintf(stdout, "[n=%d]send to client error[%d] occur, ignored!\n", num, errno);
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

