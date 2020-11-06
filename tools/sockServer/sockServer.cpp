#include <sys/socket.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "myerr.h"
#define BUFFER_LENGTH 1048576
#define TIME_OUT 30
void showMsg(char *buffer, int len)
{
    int nShowLen = len > 24 ? 24 : len;
    for (int i=0; i<nShowLen; i++)
    {
       fprintf(stdout, "%02x ", (unsigned char)(buffer[i]));
    }
    fprintf(stdout, "...\n");
}
void encodebuffer(unsigned char* buffer, int len, unsigned char key)
{
   for (int i=0; i<len; i++)
      buffer[i] = buffer[i] ^ key;
}
bool getsockaddrfromhost(char* buffer, unsigned char bytes, sockaddr_in& serv)
{
   struct hostent* host;
   char hostname[256];
   memcpy(hostname, buffer, bytes);
   hostname[bytes] = '\0';
   printf("hostname: %s\n", hostname); 
   if ((host = gethostbyname(hostname)) == NULL) return false;
   memcpy(&serv.sin_addr.s_addr, host->h_addr, 4);
   printf("host address: %x\n", serv.sin_addr.s_addr);
   return true; 
}

int main(int argc, char **argv)
{
    struct sockaddr_in serv, cli, fserv;
    int srvfd, n, num, clifd, fsrvfd;
    unsigned int clilen;
    char buffer[BUFFER_LENGTH];
    char acceptSockBuffer[2] = {5, 0};
    char startSockBuffer[10] = {5, 0, 0, 1, 0, 0, 0, 0, 0, 0};
    fd_set fdset;
    FD_ZERO(&fdset);
    unsigned char key = 0;
    if (argc < 3)
    {
        err_quit( "Usage: %s servport encodekey\n", argv[0]);
    }
    if ((srvfd = socket(PF_INET, SOCK_STREAM, 0)) < 0)
    {
        err_sys("socket error");
    }
    memset(&serv, 0, sizeof(serv));
    serv.sin_family = AF_INET;
    serv.sin_addr.s_addr = htonl(INADDR_ANY);
    serv.sin_port = htons(atoi(argv[1]));
    memset(&fserv, 0, sizeof(fserv));
    fserv.sin_family = AF_INET;
    key = atoi(argv[2]);
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
    clilen = sizeof(cli);
    signal(SIGCLD,SIG_IGN);
    for (;;)
    {
        fprintf(stdout, "wait for connection...\n");
        if ((clifd = accept(srvfd, (sockaddr*)&cli, &clilen)) < 0)
        {
            recErrNum++;
            fprintf(stdout, "[%d-%d]accept error[%d] occur, ignored!\n", num+1, recErrNum, errno);
            sleep(1);
            continue;
        }
        fprintf(stdout, "[accetp connect %d]\n", clifd);
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
        close(srvfd);
        
        //sock protocl identify
        if ((n = recv(clifd, buffer, BUFFER_LENGTH, 0)) < 3){
           fprintf(stdout, "receive from client failed\n");
           close(clifd);
           break;
        }
        showMsg(buffer, n);
        if (n == 3 && buffer[0] == 5 && buffer[1] == 1 && buffer[2] == 0){
           send(clifd, acceptSockBuffer, sizeof(acceptSockBuffer), 0);
        }
        else{
           fprintf(stdout, "receive request sock type not 0x050100\n");
           close(clifd);
           break;
        }
        if ((n = recv(clifd, buffer, BUFFER_LENGTH, 0)) < 10){
           fprintf(stdout, "receive start sock from client failed\n");
           close(clifd);
           break;
        }
        showMsg(buffer, n);
        if (buffer[0] == 5 && buffer[1] == 1 && buffer[2] == 0){
           if (buffer[3] == 3){
              unsigned char bytes = buffer[4];
              getsockaddrfromhost(buffer+5, bytes, fserv);
              memcpy(&fserv.sin_port, buffer+5+bytes, 2);
           }
           else if (buffer[3] == 1){
              memcpy(&fserv.sin_addr.s_addr, buffer+4, 4);
              memcpy(&fserv.sin_port, buffer+6, 2); 
           }
        }
        else{
           fprintf(stdout, "receive sock start type not 0x050100\n");
           close(clifd);
           break;
        }
        
        if ((fsrvfd = socket(PF_INET, SOCK_STREAM, 0)) < 0)
        {
           fprintf(stdout, "forward socket return failed\n");
           close(clifd);
           break;
        }
        if (connect(fsrvfd, (sockaddr*)(&fserv), sizeof(fserv)) < 0)
        {
           close(clifd);
           fprintf(stdout, "connect to forward server failed\n");
           break;
        }
        memcpy(startSockBuffer+4, &fserv.sin_addr.s_addr, 4);
        memcpy(startSockBuffer+6, &fserv.sin_port, 2);        
        send(clifd, startSockBuffer, sizeof(startSockBuffer), 0);        

        // set time out
        struct timeval tv;
        tv.tv_sec = TIME_OUT;
        tv.tv_usec = 0;
        struct timeval clitv = tv, srvtv = tv;
        setsockopt(clifd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&clitv, sizeof(tv));
        setsockopt(fsrvfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&srvtv, sizeof(tv));       
 
        // wait for message
        fprintf(stdout, "clifd: %d, fsrvfd: %d\n", clifd, fsrvfd);
        int rt = 0;
        int maxfd = clifd > fsrvfd ? clifd+1 : fsrvfd+1;
        printf("maxfd=%d\n", maxfd);
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
            memset(&buffer, 0, sizeof(buffer));
            n = 0;
            // receive message from client
            if (FD_ISSET(clifd, &fdset))
            {
               fprintf(stdout, "receive message from client: %d\n", clifd);
               if ((n = recv(clifd, buffer, BUFFER_LENGTH, 0)) < 0)
               {
                  fprintf(stdout, "[%d]recv error[%d] occur, ignored!\n", num, errno);
                  break;
               }
               if (n == 0)
               {
                  fprintf(stdout, "close by client\n");
                  break;
               }
               //showMsg(buffer, n);
               //encodebuffer((unsigned char*)buffer, n, key);
               if (send(fsrvfd, buffer, n, 0) != n)
               {
                   fprintf(stdout, "[%d]send error[%d] occur, ignored!\n", num, errno);
                   break;
               }
            }

            // receive message from forward server
            if (FD_ISSET(fsrvfd, &fdset))
            {
                fprintf(stdout, "receive message from forward server:%d\n", fsrvfd);
                if ((n = recv(fsrvfd, buffer, BUFFER_LENGTH, 0)) < 0)
                {
                   fprintf(stdout, "[%d]recv error[%d] occur, ignored!\n", num, errno);
                   break;
                }
                if (n == 0)
                {
                   fprintf(stdout, "close by forward server\n");
                   break;
                }
                //encodebuffer((unsigned char*)buffer, n, key);
                if (send(clifd, buffer, n, 0) != n)
                {
                   fprintf(stdout, "[%d]send error[%d] occur, ignored!\n", num, errno);
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

