#ifndef TBUFF_LENGTH 
#define TBUFF_LENGTH 65536
#endif
#include "log.h"
#include "pub.h"
struct tbuff{
	char buff[TBUFF_LENGTH];
	int recvn;
	int sendn;
	tbuff():recvn(0),sendn(0){}
	void reset(){recvn=0;sendn=0;}
	int leftlen(){return recvn > sendn ? recvn - sendn : 0;}
	char* leftbuff(){return recvn > sendn ? buff+sendn: NULL;}
};
enum sockflag
{
	sock_server = 0,
	sock_client = 1,
	sock_remote = 2
};
struct tsock{
	int fd;
	int dstfd;
	tbuff tbuf;
	int flag; // 0-server, 1-client, 2-remote
	tsock():fd(-1),dstfd(-1),flag(-1){}
	tsock(int _fd, int _dt, int _f):fd(_fd),dstfd(_dt),flag(_f){}
	void reset(){fd=-1;dstfd=-1;flag=-1;tbuf.reset();}
	void setfd(int _fd, int _dstfd, int _flag=-1){fd=_fd, dstfd=_dstfd;flag=_flag;}
	void setdst(int _dstfd){dstfd=_dstfd;}
	void setflag(int _flag){flag=_flag;}
	const char* desc(){
		if(flag==1) return "client";
		else if(flag==2) return "remote";
		else if(flag==0) return "server";
		return "undefine flag";
	}
};

// 1: recv message empty
// 0: recv message succ
//-1: recv message failed
//-2: close by peer 
int recvsock(tsock& info)
{
    tbuff& tbuf = info.tbuf;
	tbuf.reset();
	int fd = info.fd;
    tbuf.recvn = recv(fd, tbuf.buff, TBUFF_LENGTH, 0);
	if (tbuf.recvn < 0){ 
        if (errno == EAGAIN || errno == EWOULDBLOCK){
			tbuf.reset();
			return 1;
		}
		LOG_E("recvsock: recv fd[%d] rt[%d] error[%d-%s]", fd, tbuf.recvn, errno, strerror(errno));
        return -1; 
    }   
    if (tbuf.recvn == 0){ 
        if (errno == EAGAIN || errno == EWOULDBLOCK){
			tbuf.reset();
			return 1;
		}
		LOG_I("recvsockandsend: close by %s fd[%d]", info.desc(), fd);
		return -2; 
    }
	//LOG_I("recv msg: %s", buffer2hex(tbuf.buff, tbuf.recvn));
	return 0;
}
// 0: send message succ
// 1: send message not finished
//-1: send failed
int sendsock(tsock& info)
{
	tbuff& tbuf = info.tbuf;
	char* buff = tbuf.leftbuff();
	if (buff == NULL)
		return 0;
	int left = tbuf.leftlen();
	//LOG_I("send msg: %s", buffer2hex(buff, left));
	int num = send(info.dstfd, buff, left, 0);
	if (num == left){
		tbuf.sendn = tbuf.recvn;
		return 0;
	}
	else if (num < 0){
	    if (errno == EAGAIN || errno == EWOULDBLOCK){
            return 1;
        }
		LOG_E("sendsock: fd[%d->%d], len[%d], error[%d-%s]", info.fd, info.dstfd, left, errno, strerror(errno));
		return -1;
	}
	tbuf.sendn += num;
	return 1;
}

// 0: recv message and encode, then send to dstfd succ
// 1: recv message and encode, but send not finished
//-1: recv failed or send failed
//-2: close by current fd
int recvsockandsendencoded(tsock& info, int key)
{
    tbuff& tbuf = info.tbuf;
    int fd = info.fd;
    int dst = info.dstfd;
	int rt = 0;
	int n = 0;
    do{
		n++;
		if ((rt = recvsock(info)) < 0){ 
			return rt;
		}
        if (rt == 1 && n == 1){
			LOG_E("first recv msg empty(%d->%d), close conn", fd, dst);
			return -1;
		}
		LOG_D("recvandsend: (%d->%d) rt-%d len-%d n-%d", fd, dst, rt, tbuf.recvn,n);
		if (rt == 1) return 0; 
		encodebuffer(tbuf.buff, tbuf.recvn, key);
		if ((rt = sendsock(info)) != 0){
			return rt;
		}
    }while(tbuf.recvn == TBUFF_LENGTH);
    return 0;
}

// 0: recv sock1 message and reply to dstfd succ
//-1: recv failed or send failed
//-2: close by current fd
//-3: sock1 message explain failed
int recvsock1andreplyencoded(tsock& info, int key=0)
{
	tbuff& tbuf = info.tbuf;
	int fd = info.fd;
	int dst = info.dstfd;
	int rt = 0;
	tbuf.reset();
	if ((rt = recvsock(info)) < 0){
		return rt;
	}
	if (rt == 1){
		LOG_E("recvsock1andreply: recv empty");
		return -2;
	}
	int n = tbuf.recvn;
    char* buff = tbuf.buff;
	if (n < 3 || n >= 10){
		LOG_E("receive sock1 message len:%d not expected", n);
		return -3;
	}
	encodebuffer(tbuf.buff, tbuf.recvn, key);
	if (buff[0] == 5 && ((buff[1] == 1 && buff[2] == 0)|| buff[1] == n-2)){
		LOG_D("recv sock1: %s", buffer2hex(buff, n));
        char _acceptSockBuffer[2] = {5, 0};
        encodebuffer(_acceptSockBuffer, sizeof(_acceptSockBuffer), key);
		send(dst, _acceptSockBuffer, sizeof(_acceptSockBuffer), 0);
	}
	else {
		LOG_E("recv sock1 message not expected: %s", buffer2hex(buff, n));
		return -3;
	}
	return 0;
}
#define recvsock1andreply(info) recvsock1andreplyencoded(info, 0);

