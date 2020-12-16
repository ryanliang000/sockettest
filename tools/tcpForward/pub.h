#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/time.h>
#include "log.h"
void procperr(int signum)
{
    if (signum == SIGPIPE)
        LOG_E("fatal error sigpipe, port error[%d-%s]", errno, strerror(errno));
}
char hexbuffer[1024*3];
char* buffer2hex(char msg[], int len)
{
    len = len >= 1024 ? 1023 : len;
    for (int i=0; i<len; i++){
       sprintf(hexbuffer+3*i, "%02X ", (unsigned char)(msg[i]));
    }
    return hexbuffer;
}
void encodebuffer(unsigned char* msg, int len, unsigned char key)
{
   for (int i=0; i<len; i++)
      msg[i] = msg[i] ^ key;
}
bool getsockaddrfromhost(char* msg, unsigned char bytes, sockaddr_in& serv)
{
   struct hostent* host;
   char hostname[256];
   memcpy(hostname, msg, bytes);
   hostname[bytes] = '\0';
   LOG_I("hostname: %s", hostname);
   if ((host = gethostbyname(hostname)) == NULL) return false;
   memcpy(&serv.sin_addr.s_addr, host->h_addr, 4);
   //printf("host address: %x\n", serv.sin_addr.s_addr);
   return true;
}
void setnonblock(int fd)
{
    int flag = fcntl(fd, F_GETFL);
    fcntl(fd, F_SETFL, flag | O_NONBLOCK);
}
void setblock(int fd)
{
    int flag = fcntl(fd, F_GETFL);
    fcntl(fd, F_SETFL, flag ^ O_NONBLOCK);
}
struct timeval tv;
void settimeout(int sock, int second, int flag = -1)
{
   tv.tv_sec = second;
   tv.tv_usec = 0;
   if (flag != -1){
       setsockopt(sock, SOL_SOCKET, flag, (const char*)&tv, sizeof(timeval));
       return;
   }
   setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof(timeval));
   setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(timeval));
}
#define setsendtimeout(fd, sec) settimeout(fd, sec, SO_SNDTIMEO)
#define setrecvtimeout(fd, sec) settimeout(fd, sec, SOL_RCVTIMEO);

