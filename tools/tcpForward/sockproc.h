#ifndef TBUFF_LENGTH 
#define TBUFF_LENGTH 1240
#endif
#include "log.h"
#include "pub.h"
struct tbuff{
	char buff[TBUFF_LENGTH];
	char recvn;
	char sendn;
	tbuff():recvn(0),sendn(0){}
	void reset(){recvn=0;sendn=0;}
	int leftlen(){return recvn > sendn ? recvn - sendn : 0;}
	char* leftbuff(){return recvn > sendn ? buff+leftlen(): NULL;}
};
enum sock_flag
{
	server_flag = 0,
	client_flag = 1,
	remote_flag = 2
};
struct tsock{
	int fd;
	int dstfd;
	tbuff tbuf;
	int flag; // 0-server, 1-client, 2-remote
	tsock():fd(-1),dstfd(-1),flag(-1){}
	void reset(){fd=-1;dstfd=-1;flag=-1;tbuf.reset();}
	void setfd(int _fd, int _dstfd){fd=_fd, dstfd=_dstfd;}
	void setdst(int _dstfd){dstfd=_dstfd;}
	void setflag(int _flag){flag=_flag;}
	char* desc(){
		if(flag==1) return "client";
		else if(flag==2) return "remote";
		else if(flag==0} return "server";
		else return "";
};

// 0: recv message and encode, then send to dstfd succ
// 1: recv message and encode, but send not finished
//-1: recv failed or send failed
//-2: close by current fd
int recvsockandsend(tsock& info, int key)
{
	tbuff& tbuf = info.tbuf;
	int fd = info.fd;
	int dst = info.dstfd;
	int cnt = 0;
	do{
		++cnt;
		if ((tbuf.recvn = recv(fd, tbuf.buff, TBUFF_LENGTH, 0) < 0){
			LOG_E("recvsockandsend: recv fd[%d] error[%d-%s]", fd, errno, strerror(errno));
			return -1;
		}
		if (tbuf.recvn == 0){
			if (cnt == 1){
				LOG_I("recvsockandsend: close by %s fd[%d]", info.desc(), fd);
				return -2;
			}
			return 0;
		}
		encodebuffer(tbuf.buff, tbuf.recvn, key);
		
		tbuf.sendn = send(dstfd, tbuf.buff, tbuf.recvn, 0);
		if (tbuf.sendn == tbuf.recvn){
			continue;
		}
		if(tbuf.sendn < 0){
			if (errno == EAGAIN || errno == EWOULDBLOCK){
				tbuf.sendn = 0;
				return 1;
			}
			LOG_E("recvsockandsend: send fd[%d] error[%d-%s]", dstfd, errno, strerror(err    no));
			return -1;
		}
		return 1;
	}
	while(tbuf.recvn == tbuf.sendn);
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
	int num = send(info.dstfd, buff, left, 0);
	if (num == left){
		return 0;
	}
	else if (num < 0){
		LOG_E("sendsock: fd[%d->%d], len[%d], error[%d-%s]", info.fd, info.dstfd, left, errno, strerror(errno));
		return -1;
	}
	
	tbuf.sendn += num;
	return 1;
}

