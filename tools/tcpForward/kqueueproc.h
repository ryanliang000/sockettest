#include <sys/event.h>
#define addkqueuefd(evread, evwrite, epfd, fd, flag) {\
    if (flag == EVFILT_READ){\
		evread[fd].ident = fd;\
		evread[fd].filter = EVFILT_READ;\
    	EV_SET(&evread[fd], fd, EVFILT_READ, EV_ADD|EV_ENABLE, 0, 0, NULL);\
		kevent(epfd, &evread[fd], 1, NULL, 0, NULL);\
		LOG_D("insertkqueue: fd-%d, flag-read", fd);\
		fdnums++;\
	}\
	if (flag == EVFILT_WRITE){\
		evwrite[fd].ident = fd;\
		evwrite[fd].filter = EVFILT_WRITE;\
		EV_SET(&evwrite[fd], fd, EVFILT_WRITE, EV_ADD|EV_ENABLE, 0, 0, NULL);\
		kevent(epfd, &evwrite[fd], 1, NULL, 0, NULL);\
		LOG_D("insertkqueue: fd-%d, flag-write", fd);\
		fdnums++;\
	}\
}

#define delkqueuefd(evread, evwrite, epfd, fd, flag) {\
	if ((flag == 0 || flag == EVFILT_READ) && evread[fd].ident == fd){\
        EV_SET(&evread[fd], fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);\
		kevent(epfd, &evread[fd], 1, NULL, 0, NULL);\
		evread[fd].ident = -1;\
        LOG_D("deletekqueue: fd-%d, flag-read", fd);\
        fdnums--;\
    }\
    if ((flag == 0 || flag == EVFILT_WRITE) && evread[fd].ident == fd){\
        EV_SET(&evwrite[fd], fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);\
		kevent(epfd, &evwrite[fd], 1, NULL, 0, NULL);\
		evwrite[fd].ident = -1;\
        LOG_D("deletekqueue: fd-%d, flag-write", fd);\
        fdnums--;\
    }\
}

#define addkqueueflag addkqueuefd
#define delkqueueflag delkqueuefd

void removekqueuefd(struct kevent* evread, struct kevent* evwrite, int epfd, int fd, int dstfd=-1, bool closefd=true)
{
    delkqueuefd(evread, evwrite, epfd, fd, 0);
    if (closefd) close(fd);
    if (dstfd != -1){
        delkqueuefd(evread, evwrite, epfd, dstfd, 0);
        if (closefd) close(dstfd);
    }
    LOG_R("close sock (%d,%d), curr kevent fds:%d", fd, dstfd, fdnums);
}

const char* evdesc(int flag){
	static char _evdesc[64];
	switch(flag){
		case EVFILT_READ:
			return "READ";
		case EVFILT_WRITE:
			return "WRITE";
		default:
			{
				sprintf(_evdesc, "ev-%d", flag);
			}
	}
	return "Unknown";
}
