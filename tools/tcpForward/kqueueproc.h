#include <sys/event.h>
#define MAX_EVENT_POOL 2048
#define MAX_EVENT_RECV 128
int _fdnums = 0;
int _epfd = 0;
enum xevent_filter{
	xfilter_read = 0,
	xfilter_write= 1,
	xfilter_error= 2,
	xfilter_count= 3
};
enum xevent_action{
	xaction_add = 0,
	xaction_del = 1,
	xaction_count= 2
};
typedef int (*xevent_callback)(int fd, int filter);
struct xevent_func{
	int filter;
	xevent_callback func;
	xevent_func():filter(-1), func(NULL){}
	void reset(){filter=-1; func=NULL;}
};
struct xevent{
	int fd;
	xevent_func funcs[xfilter_count];
	xevent():fd(-1){}
	xevent(int infd):fd(infd){}
};
struct xevent xeventpool[MAX_EVENT_POOL];

int xfilter2kfilter(int xfilter)
{
	switch(xfilter){
	case xfilter_read:
		return EVFILT_READ;
	case xfilter_write:
		return EVFILT_WRITE;
	case xfilter_error:
		return EVFILT_EXCEPT;
	default:
		;
	}
	return EVFILT_EXCEPT;
}
xevent_filter kfilter2xfilter(int kfilter){
	switch(kfilter){
	case EVFILT_READ:
		return xfilter_read;
	case EVFILT_WRITE:
		return xfilter_write;
	case EVFILT_EXCEPT:
		return xfilter_error;
	default:
		;
	}
	return xfilter_error;
}
// build kevent from info
struct kevent buildkevent(int fd, int xfilter, xevent_action act)
{
	int flag = EV_ADD|EV_ENABLE;
	if (act == xaction_del)
		flag = EV_DELETE;
	struct kevent kevt;
	EV_SET(&kevt, fd, xfilter2kfilter(xfilter), flag, 0, 0, NULL);
	return kevt;
}
// get proc func from fd and filter
xevent_callback geteventcallback(int fd, int kfilter)
{
	xevent& evt = xeventpool[fd];
	if (evt.fd != fd)
		return NULL;
	xevent_filter xflt = kfilter2xfilter(kfilter);
	if (evt.funcs[xflt].filter == -1)
		return NULL;
	return xeventpool[fd].funcs[xflt].func;
}
xevent_callback geteventcallback(struct kevent& kevt)
{
	return geteventcallback(kevt.ident, kevt.filter);
}
#define initxevent() {\
	_epfd = kqueue();\
	if (_epfd == -1){\
		err_sys("init xevent with kqueue error");\
	}\
}
const char* xfilterdesc(int xfilter);

// 0-Succ, -1-failed
int regxeventfunc(int fd, xevent_filter filter, xevent_callback func)
{
	xevent& evt = xeventpool[fd];
	// only update func
	if (evt.funcs[filter].filter != -1){
		evt.funcs[filter].func = func;
		return 0;
	}
	// modify data 
	evt.fd = fd;
	evt.funcs[filter].filter = filter;
	evt.funcs[filter].func = func;
	// add event
	struct kevent kevt = buildkevent(fd, filter, xaction_add);
	kevent(_epfd, &kevt, 1, NULL, 0, NULL);
	LOG_D("regevent: fd-%d, filter-%s", fd, xfilterdesc(filter));
	_fdnums++;
	return 0;
};
int unregxevent(int fd, xevent_filter filter)
{
	xevent& evt = xeventpool[fd];
	if (evt.fd == -1 || evt.funcs[filter].filter == -1){
		LOG_D("remove fd-filter(%d-%d) event not exist", fd, filter);
		return 0;
	}
	// remove data
	evt.funcs[filter].reset();
	// remove event
	struct kevent kevt = buildkevent(fd, filter, xaction_del);
	kevent(_epfd, &kevt, 1, NULL, 0, NULL);
	LOG_D("unregevent: fd-%d, filter-%s", fd, xfilterdesc(filter));
	_fdnums--;
	return 0;
};
int unregxevent(int fd)
{
	xevent& evt = xeventpool[fd];
	if (evt.fd == -1){
		LOG_D("remove fd(%d) event not exist", fd);
		return 0;
	}
	struct kevent kevts[xfilter_count];
	int nums = 0;
	// modify data
	for (int i=0; i<xfilter_count; i++){
		if (evt.funcs[i].filter != -1){
			kevts[nums++] = buildkevent(fd, evt.funcs[i].filter, xaction_del);
			evt.funcs[i].reset();
		}
	}
	evt.fd = -1;
	// remove event
	kevent(_epfd, kevts, nums, NULL, 0, NULL);
	_fdnums -= nums;
	LOG_D("unregevent-fd: fd-%d, nums-%d, left-%d", fd, nums, _fdnums);
	return 0;
}
int call_event_func(struct kevent& kevt)
{
	xevent_callback func = geteventcallback(kevt);
	if (func == NULL){
		LOG_D("call_event_func return NULL, fd=%d", int(kevt.ident));
		return -1;
	}	
	return func(kevt.ident, kevt.filter);
}
struct kevent _events[MAX_EVENT_RECV];
struct timespec _tvs;
// 0-succ, -1-failed
int dispatchxevent(int timeoutsecond)
{
	_tvs.tv_sec = timeoutsecond;
	_tvs.tv_nsec = 0;
	int nfds = kevent(_epfd, NULL, 0, _events, MAX_EVENT_RECV, &_tvs);
	if (nfds < 0){
		LOG_E("epoll wait error[%d], ignored!", errno);
		return -1;
	}
	for (int i=0; i<nfds; i++){
		call_event_func(_events[i]);
	}
	return 0;
};

const char* xfilterdesc(int xfilter){
	static char _desc[64];
	switch(xfilter){
	case xfilter_read:
		return "READ";
	case xfilter_write:
		return "WRITE";
	case xfilter_error:
		return "ERROR";
	default:
		sprintf(_desc, "ev-%d", xfilter);
	}
	return _desc;
}
const char* evdesc(int flag){
	return xfilterdesc(kfilter2xfilter(flag));
}
