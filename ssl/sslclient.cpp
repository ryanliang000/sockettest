#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "myerr.h"
#include "openssl/ssl.h"
#include "openssl/err.h"

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
    char request[REQUEST], reply[REPLY];
    int sockfd, n, errno;
    int maxSendCount = 1, i;    
    SSL_CTX* ctx;
    SSL* ssl;
    if (argc < 3)
    {
        err_quit("usage: %s ipaddress port (sendtimes)", argv[0]);
    }
    if (argc == 4)
    {
        maxSendCount = atoi(argv[3]);
    }
    
    // init ssl library and create ssl context
    SSL_library_init();
    OpenSSL_add_all_algorithms();
    SSL_load_error_strings();
    ctx = SSL_CTX_new(SSLv23_client_method());
    if (ctx == NULL)
    {
        ERR_print_errors_fp(stdout);
        err_quit("SSL_CTX_new return null");
    }
    
    // create ssl connect
    sockfd = OpenConn(argv[1], argv[2]); 
    ssl = SSL_new(ctx);
    SSL_set_fd(ssl, sockfd);
    if (SSL_connect(ssl) == -1)
    {
        ERR_print_errors_fp(stdout);
        err_quit("SSL_connect retrun -1");
    }
    else
    {
        fprintf(stdout, "connected with %s encryption\n", SSL_get_cipher(ssl));
    }
    

    memset(request, '1', REQUEST);
    for (i=0; i<maxSendCount; i++)
    {
        sprintf(request, "hello world...%d", i);
        if (SSL_write(ssl, request, REQUEST) != REQUEST)
        {
            err_sys("sendto error");
        }
        
        memset(&reply, 0, sizeof(reply));
        if ((n = SSL_read(ssl, reply, REPLY)) < 0)
        {
            err_sys("recvfrom error");
        }
        fprintf(stdout, "recv: %s\n", reply);
    }
    
    SSL_shutdown(ssl);
    SSL_free(ssl);
    close(sockfd);
    SSL_CTX_free(ctx);

    exit(0);
}

