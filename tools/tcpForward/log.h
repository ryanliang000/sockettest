#include <stdio.h>
#include <string.h>
#define LOG(fd, title, ...)\
	fprintf(fd, title);\
	fprintf(fd, __VA_ARGS__);\
	fprintf(fd, " -line:%d\n", __LINE__);

#ifdef RELEASE
	#define LOG_E(...) LOG(stderr, "[ERR]", __VA_ARGS__)
	#define LOG_R(...)
	#define LOG_I(...)
#elif defined RUN
	#define LOG_E(...) LOG(stderr, "[ERR]", __VA_ARGS__)
	#define LOG_R(...) LOG(stdout, "[RUN]", __VA_ARGS__)
	#define LOG_I(...)
#else
	#define LOG_E(...) LOG(stderr, "[ERR]", __VA_ARGS__)
	#define LOG_R(...) LOG(stdout, "[RUN]", __VA_ARGS__) 
	#define LOG_I(...) LOG(stdout, "[INF]", __VA_ARGS__)
#endif

#ifdef DEBUG
	#define LOG_D(...) LOG(stdout, "[DBG]", __VA_ARGS__)
#else
	#define LOG_D(...)
#endif
