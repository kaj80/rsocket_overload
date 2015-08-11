#define _GNU_SOURCE
#include <dlfcn.h>
#include <time.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <stdarg.h>
#include <rdma/rsocket.h>


static ssize_t (*orig_rrecv)(int socket, void *buf, size_t len, int flags);
static ssize_t (*orig_rsend)(int socket, const void *buf, size_t len, int flags);
static int (*orig_rpoll)(struct pollfd *fds, nfds_t nfds, int timeout);


const char *month_str[12] = {
	"Jan",
	"Feb",
	"Mar",
	"Apr",
	"May",
	"Jun",
	"Jul",
	"Aug",
	"Sep",
	"Oct",
	"Nov",
	"Dec"
};

void write_date(FILE *stream, time_t tim, unsigned int usec)
{
	struct tm result;

	localtime_r(&tim, &result);
	fprintf(stream, "%s %02d %02d:%02d:%02d %06d",
		(result.tm_mon < 12 ? month_str[result.tm_mon] : "???"),
		result.tm_mday, result.tm_hour, result.tm_min,
		result.tm_sec, usec);
}

void get_thread_id(char *buff, int size)
{
	int ret = 1;
	pthread_t self;

	self = pthread_self();
	ret = pthread_getname_np(self, buff, size);
	if (!ret && !strncmp(buff, program_invocation_short_name, size))
		ret = 1;
	if (ret || !buff[0])
		ret = snprintf(buff, size, "%04X", (unsigned) self);
}
void write_log(const char *format, ...)
{
	va_list args;
	char tid[16] = {};
	struct timeval tv;
	time_t tim;

	gettimeofday(&tv, NULL);
	tim = tv.tv_sec;
	get_thread_id(tid, sizeof tid);
	va_start(args, format);
	write_date(stderr, tim, (unsigned int) tv.tv_usec);
	fprintf(stderr, " [%.16s]: ",tid);
	vfprintf(stderr, format, args);
	va_end(args);
}

struct poll_event {
	int val;
	const char *name;
};

static const struct poll_event poll_events[] = {
	{POLLHUP, "POLLHUP"},
	{POLLERR, "POLLERR"},
	{POLLNVAL, "POLLNVAL"},
	{POLLIN, "POLLIN"},
	{POLLPRI, "POLLPRI"},
	{POLLOUT, "POLLOUT"},
	{POLLRDHUP ,"POLLRDHUP"},
	{POLLRDBAND ,"POLLRDBAND"},
	{POLLWRBAND ,"POLLWRBAND"}
};

static void ssa_format_event(char *str,const size_t str_size, const int event)
{
	unsigned int i, n = 0;
	int ret;

	for (i = 0; n < str_size && i < sizeof(poll_events) / sizeof(poll_events[0]); ++i) {
		if (event & poll_events[i].val) {
			ret = snprintf(str + n, str_size - n, "%s|", poll_events[i].name);
			if (ret > 0)
				n += ret;
		}
	}
	n = strlen(str);
	if (n && str[n - 1] == '|')
		str[n -1] = '\0';
}


ssize_t rrecv(int socket, void *buf, size_t len, int flags)
{
	int ret, error;
	char descr[256] = {};

	ret = orig_rrecv(socket, buf, len, flags);
	if (ret >= 0) {
		write_log("rrecv rsocket %d len %d buf %p ret %d\n", socket, len, buf, ret);
	} else {
		error = errno;
		strerror_r(error, descr, sizeof(descr));
		write_log("rrecv rsocket %d len %d buf %p ret %d errno %d (%s)\n", socket, len, buf, ret, error, descr);
	}
	return ret;
}

ssize_t rsend(int socket, const void *buf, size_t len, int flags)
{
	int ret, error;
	char descr[256] = {};

	ret =  orig_rsend(socket, buf, len, flags);
	if (ret >= 0) {
		write_log("rsend rsocket %d len %d buf %p ret %d\n", socket, len, buf, ret);
	} else {
		error = errno;
		strerror_r(error, descr, sizeof(descr));
		write_log("rsend rsocket %d len %d buf %p ret %d errno %d (%s)\n", socket, len, buf, ret, error, descr);
	}
	return ret;
}

int rpoll(struct pollfd *fds, nfds_t nfds, int timeout)
{
	int i ,ret;
	char msg[128];

	write_log("rpoll n %d ", nfds);
	for (i = 0; i < nfds; ++i)
	{
		if (fds[i].fd > 0) {
			ssa_format_event(msg, sizeof(msg), fds[i].events);
			fprintf(stderr, "%d (%s) ", fds[i].fd, msg);
		}
	}
	fprintf(stderr,"\n");

	ret = orig_rpoll(fds, nfds, timeout);
	write_log("rpoll ret %d ", ret);

	for (i = 0; i < nfds; ++i)
	{
		if (fds[i].fd > 0 && fds[i].revents) {
			ssa_format_event(msg, sizeof(msg), fds[i].revents);
			fprintf(stderr, "%d (%s) ", fds[i].fd, msg);
		}
	}
	fprintf(stderr,"\n");

	return ret;
}

void _init(void)
{
//	write_log("Overloading rsocket. \n");

	orig_rrecv = dlsym(RTLD_NEXT, "rrecv");
	orig_rsend = dlsym(RTLD_NEXT, "rsend");
	orig_rpoll = dlsym(RTLD_NEXT, "rpoll");
}
