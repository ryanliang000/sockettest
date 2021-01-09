
#define insertepollfd(evpoll, epfd, f, flag) {\
    evpoll[f].data.fd = f;\
    evpoll[f].events = flag|EPOLLERR;\
    epoll_ctl(epfd, EPOLL_CTL_ADD, f, &evpoll[f]);\
    LOG_D("insertepollfd: fd-%d, flag-%d", f, evpoll[f].events^EPOLLERR);\
    fdnums++;\
}

#define delepollfd(evpoll, epfd, fd, flag) {\
	epoll_ctl(epfd, EPOLL_CTL_DEL, fd, flag);\
	evpoll[fd].events = 0;\
	LOG_D("delepollfd: fd-%d", fd);\
	fdnum--;\
}

#define resetepollflag(evpoll, epfd, fd, flag) {\
    evpoll[fd].events = flag|EPOLLERR;\
    epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &evpoll[fd]);\
    LOG_D("resetepollflag: fd-%d, flag-%d", fd, evpoll[fd].events^EPOLLERR);\
}

#define addepollflag(evpoll, epfd, fd, flag) resetepollflag(evpoll, epfd, fd, evpoll[fd].events | flag)

#define delepollflag(evpoll, epfd, fd, flag) resetepollflag(evpoll, epfd, fd, evpoll[fd].events ^ flag)

void removeepollfd(epoll_event* evpoll, int epfd, int fd, bool closefd=true)
{
    delepollfd(evpoll, epfd, fd, NULL);
    if (closefd) close(fd);
    int dstfd = sockinfos[fd].dstfd;
    if (dstfd != -1){
        delepollfd(evpoll, epfd, dstfd, NULL);
        if (closefd) close(dstfd);
        sockinfos[dstfd].reset();
    }
    sockinfos[fd].reset();
    LOG_R("close sock (%d,%d), curr epoll fds:%d", fd, dstfd, fdnums);
}

