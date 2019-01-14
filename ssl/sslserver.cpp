#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "myerr.h"
#include "openssl/ssl.h"
#include "openssl/err.h"
#define REQUEST 128
#define REPLY 160 

int StartListen(const char* port)
{
    struct sockaddr_in serv;
    int sockfd;
    if ((sockfd = socket(PF_INET, SOCK_STREAM, IPPROTO_IP)) < 0)
    {   
        err_sys("socket error");
    }   
    memset(&serv, 0, sizeof(serv));
    serv.sin_family = AF_INET;
    serv.sin_addr.s_addr = htonl(INADDR_ANY);
    serv.sin_port = htons(atoi(port));
    if (bind(sockfd, (const sockaddr*)&serv, sizeof(serv)) < 0)
    {
        err_sys("bind error");
    }
    if (listen(sockfd, 5) <0)
    {
        err_sys("listen error");
    }
    return sockfd;
}

int main(int argc, char **argv)
{
    struct sockaddr_in cli;
    char request[REQUEST], reply[REPLY];
    int sockfd, n, num, clifd;
    unsigned int clilen;
    SSL_CTX* ctx;
    SSL* ssl;

    if (argc < 2)
    {
        err_quit("usage: %s port\n", argv[0]);
    }
    
    //init openssl lib and create ssl context
    SSL_library_init();
    OpenSSL_add_all_algorithms();
    SSL_load_error_strings();
    ctx = SSL_CTX_new(SSLv23_server_method());
    if (ctx == NULL)
    {
        ERR_print_errors_fp(stdout);
        err_quit("SSL_CTX_new return NULL");
    }
    //load cert and privatekey file to context
    if (SSL_CTX_use_certificate_file(ctx, "./certs/cert.pem", SSL_FILETYPE_PEM) < 0)
    {
        ERR_print_errors_fp(stdout);
        err_quit("SSL_CTX_use_certificate_file return < 0");
    }
    if (SSL_CTX_use_PrivateKey_file(ctx, "./certs/key.pem", SSL_FILETYPE_PEM) < 0)
    {
        ERR_print_errors_fp(stdout);
        err_quit("SSL_CTX_use_PrivateKey_file return < 0");
    }
    if (!SSL_CTX_check_private_key(ctx))
    {
        ERR_print_errors_fp(stdout);
        err_quit("SSL_CTX_check_private_key return false");
    }
    
    sockfd = StartListen(argv[1]);

    num=0;
    for (;;)
    {
        fprintf(stdout, "wait for connection...\n");
        clilen = sizeof(cli);
        if ((clifd = accept(sockfd, (sockaddr*)&cli, &clilen)) < 0)
        {
            err_sys("accept error");
        }
        ssl = SSL_new(ctx);
        SSL_set_fd(ssl, clifd);
        if (SSL_accept(ssl) == -1)
        {
            fprintf(stderr, "SSL_accept return -1\n");
            close(clifd);
            break;
        }

        memset(&request, 0, sizeof(request));
        if ((n = SSL_read(ssl, request, REQUEST)) < 0)
        {
            err_sys("recv error");
        }
        request[REQUEST-1]='\0';
        fprintf(stdout, "recv: %s\n", request);
        fprintf(stdout, "reply addr: %s:%d\n", inet_ntoa(cli.sin_addr), ntohs(cli.sin_port));
        sprintf(reply, "No.=%d, msg=%s ", ++num, request);
        reply[REPLY-1]='\0';
        fprintf(stdout, "send: %s\n", reply);
        if (SSL_write(ssl, reply, sizeof(reply)) != REPLY)
        {
            err_sys("send error");
        }
        SSL_shutdown(ssl);
        SSL_free(ssl);
        close(clifd);
    }
   
    close(sockfd);
    SSL_CTX_free(ctx);
    return 0;
}

