#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <sys/timerfd.h>
#include <clover_utils.h>
#include <clover_log.h>
#include <clover_signal.h>
#include <clover_event.h>

struct clv_event_source_idle {
	struct clv_event_source base;
	clv_event_loop_idle_cb_t cb;
};

struct clv_event_source_fd {
	struct clv_event_source base;
	clv_event_loop_fd_cb_t cb;
	s32 fd;
};

struct clv_event_source_timer {
	struct clv_event_source base;
	clv_event_loop_timer_cb_t cb;
};

struct clv_event_source_signal {
	struct clv_event_source base;
	clv_event_loop_signal_cb_t cb;
	s32 signal_number;
};

struct clv_event_source_interface idle_source_interface = {
	NULL,
};

static s32 epoll_create_cloexec(void)
{
	s32 fd;

#ifdef EPOLL_CLOEXEC
	fd = epoll_create1(EPOLL_CLOEXEC);
	if (fd >= 0)
		return fd;
	if (errno != EINVAL) {
		clv_err("failed to epoll_create1 %m");
		return -1;
	}
#endif
	fd = epoll_create(1);
	return clv_set_cloexec_or_close(fd);
}

static void process_destroy_list(struct clv_event_loop *loop)
{
	struct clv_event_source *source, *next;

	list_for_each_entry_safe(source, next, &loop->destroy_list, link)
		free(source);

	INIT_LIST_HEAD(&loop->destroy_list);
}

struct clv_event_loop * clv_event_loop_create(void)
{
	struct clv_event_loop *loop;

	loop = calloc(1, sizeof(*loop));
	if (!loop) {
		clv_err("not enough memory to alloc event loop.");
		return NULL;
	}

	loop->epoll_fd = epoll_create_cloexec();
	if (loop->epoll_fd < 0) {
		free(loop);
		return NULL;
	}

	INIT_LIST_HEAD(&loop->idle_list);
	INIT_LIST_HEAD(&loop->destroy_list);
	clv_signal_init(&loop->destroy_signal);

	return loop;
}

void clv_event_loop_destroy(struct clv_event_loop *loop)
{
	if (!loop)
		return;

	clv_signal_emit(&loop->destroy_signal, loop);
	process_destroy_list(loop);
	close(loop->epoll_fd);
	free(loop);
	loop = NULL;
}

void clv_event_source_remove(struct clv_event_source *source)
{
	if (!source)
		return;

	if (source->fd >= 0) {
		epoll_ctl(source->loop->epoll_fd, EPOLL_CTL_DEL, source->fd,
			  NULL);
		close(source->fd);
		source->fd = -1;
	}
	list_del(&source->link);
	list_add_tail(&source->link, &source->loop->destroy_list);
}

struct clv_event_source * clv_event_loop_add_idle(struct clv_event_loop *loop,
						  clv_event_loop_idle_cb_t cb,
						  void *data)
{
	struct clv_event_source_idle *source;

	source = calloc(1, sizeof(*source));
	if (!source)
		return NULL;

	source->base.interface = &idle_source_interface;
	source->base.loop = loop;
	source->base.fd = -1;
	source->cb = cb;
	source->base.data = data;
	list_add_tail(&source->base.link, &loop->idle_list);

	return &source->base;
}

void clv_event_loop_dispatch_idle(struct clv_event_loop *loop)
{
	struct clv_event_source_idle *source;

	while (!list_empty(&loop->idle_list)) {
		source = container_of(loop->idle_list.next,
				      struct clv_event_source_idle,
				      base.link);
		source->cb(source->base.data);
		clv_event_source_remove(&source->base);
	}
}

s32 clv_event_loop_dispatch(struct clv_event_loop *loop, s32 timeout)
{
	struct clv_event_source *source;
	struct epoll_event ep[32];
	s32 i, n;
	
	clv_event_loop_dispatch_idle(loop);

	n = epoll_wait(loop->epoll_fd, ep, ARRAY_SIZE(ep), timeout);
	if (n < 0)
		return -1;

	for (i = 0; i < n; i++) {
		source = ep[i].data.ptr;
		if (source->fd > 0)
			source->interface->dispatch(source, &ep[i]);
	}
	
	process_destroy_list(loop);

	clv_event_loop_dispatch_idle(loop);

	return 0;
}

static struct clv_event_source * clv_event_loop_add_source(
						struct clv_event_loop *loop,
						struct clv_event_source *source,
						u32 mask,
						void *data)
{
	struct epoll_event ep;

	if (source->fd < 0) {
		free(source);
		return NULL;
	}

	source->loop = loop;
	source->data = data;
	INIT_LIST_HEAD(&source->link);

	memset(&ep, 0, sizeof(ep));
	if (mask & CLV_EVT_READABLE)
		ep.events |= EPOLLIN;
	if (mask & CLV_EVT_WRITABLE)
		ep.events |= EPOLLOUT;
	ep.data.ptr = source;

	if (epoll_ctl(loop->epoll_fd, EPOLL_CTL_ADD, source->fd, &ep) < 0) {
		clv_err("epoll_ctl failed. %m");
		clv_err("source->fd = %d", source->fd);
		close(source->fd);
		free(source);
		return NULL;
	}

	return source;
}

static s32 clv_event_source_fd_dispatch(struct clv_event_source *source,
					struct epoll_event *ep)
{
	struct clv_event_source_fd *fd_source;
	u32 mask = 0;

	fd_source = container_of(source, struct clv_event_source_fd, base);
	if (ep->events & EPOLLIN)
		mask |= CLV_EVT_READABLE;
	if (ep->events & EPOLLOUT)
		mask |= CLV_EVT_WRITABLE;
	if (ep->events & EPOLLHUP)
		mask |= CLV_EVT_HANGUP;
	if (ep->events & EPOLLERR)
		mask |= CLV_EVT_ERROR;

	return fd_source->cb(fd_source->fd, mask, source->data);
}

static struct clv_event_source_interface fd_source_interface = {
	clv_event_source_fd_dispatch,
};

s32 clv_set_cloexec_or_close(s32 fd)
{
	s32 flags;

	if (fd == -1)
		return -1;

	flags = fcntl(fd, F_GETFD);
	if (flags == -1)
		goto err;

	if (fcntl(fd, F_SETFD, flags | FD_CLOEXEC) < 0) {
		clv_err("failed to fcntl setfd cloexec. %m");
		goto err;
	}

	return fd;

err:
	close(fd);
	return -1;
}

s32 clv_dupfd_cloexec(s32 fd, s32 minfd)
{
	s32 newfd;

	newfd = fcntl(fd, F_DUPFD_CLOEXEC, minfd);
	if (newfd >= 0)
		return newfd;

	if (errno != EINVAL) {
		clv_err("failed to fcntl dupfd. %m");
		return -1;
	}

	newfd = fcntl(fd, F_DUPFD, minfd);
	return clv_set_cloexec_or_close(newfd);
}

struct clv_event_source * clv_event_loop_add_fd(struct clv_event_loop *loop,
						s32 fd,
						u32 mask,
						clv_event_loop_fd_cb_t cb,
						void *data)
{
	struct clv_event_source_fd *source;
	
	source = calloc(1, sizeof(*source));
	if (!source)
		return NULL;

	source->base.interface = &fd_source_interface;
	source->fd = fd;
	source->base.fd = clv_dupfd_cloexec(fd, 0);
	source->cb = cb;
	return clv_event_loop_add_source(loop, &source->base, mask, data);
}

s32 clv_event_source_fd_update_mask(struct clv_event_source *source, u32 mask)
{
	struct epoll_event ep;

	memset(&ep, 0, sizeof(ep));
	if (mask & CLV_EVT_READABLE)
		ep.events |= EPOLLIN;
	if (mask & CLV_EVT_WRITABLE)
		ep.events |= EPOLLOUT;
	ep.data.ptr = source;

	return epoll_ctl(source->loop->epoll_fd, EPOLL_CTL_MOD, source->fd,&ep);
}

static s32 clv_event_source_timer_dispatch(struct clv_event_source *source,
					   struct epoll_event *ep)
{
	struct clv_event_source_timer *timer_source;
	u64 expires;
	u32 len;

	timer_source = container_of(source,
				    struct clv_event_source_timer,
				    base);
	len = read(source->fd, &expires, sizeof(expires));
	if (!(len == -1 && errno == EAGAIN) && len != sizeof(expires))
		clv_err("failed to read timerfd: %m");

	return timer_source->cb(source->data);
}

static struct clv_event_source_interface timer_source_interface = {
	clv_event_source_timer_dispatch,
};

struct clv_event_source * clv_event_loop_add_timer(struct clv_event_loop *loop,
						   clv_event_loop_timer_cb_t cb,
						   void *data)
{
	struct clv_event_source_timer *source;

	source = calloc(1, sizeof(*source));
	if (!source)
		return NULL;

	source->base.fd = timerfd_create(CLOCK_MONOTONIC,
					TFD_CLOEXEC | TFD_NONBLOCK);
	source->cb = cb;
	source->base.interface = &timer_source_interface;
	return clv_event_loop_add_source(loop, &source->base,
					 CLV_EVT_READABLE, data);
}

s32 clv_event_source_timer_update(struct clv_event_source *source,
				  s32 ms, s32 us)
{
	struct itimerspec its;

	its.it_interval.tv_sec = 0;
	its.it_interval.tv_nsec = 0;
	its.it_value.tv_sec = ms / 1000;
	its.it_value.tv_nsec = (ms % 1000) * 1000 * 1000 + us * 1000;

	if (timerfd_settime(source->fd, 0, &its, NULL) < 0) {
		clv_err("failed to timerfd_settime: %m");
		return -1;
	}

	return 0;
}

static s32 clv_event_source_signal_dispatch(struct clv_event_source *source,
					    struct epoll_event *ep)
{
	struct clv_event_source_signal *signal_source;
	struct signalfd_siginfo siginfo;
	u32 len;

	signal_source = container_of(source,
				     struct clv_event_source_signal,
				     base);
	len = read(source->fd, &siginfo, sizeof(siginfo));
	if (!(len == -1 && errno == EAGAIN) && len != sizeof(siginfo))
		clv_err("failed to read signalfd: %m");

	return signal_source->cb(signal_source->signal_number, source->data);
}

static struct clv_event_source_interface signal_source_interface = {
	clv_event_source_signal_dispatch,
};

struct clv_event_source * clv_event_loop_add_signal(
				struct clv_event_loop *loop,
				s32 signal_number,
				clv_event_loop_signal_cb_t cb,
				void *data)
{
	struct clv_event_source_signal *source;
	sigset_t mask;

	source = calloc(1, sizeof(*source));
	if (!source)
		return NULL;

	sigemptyset(&mask);
	sigaddset(&mask, signal_number);
	source->base.fd = signalfd(-1, &mask, SFD_CLOEXEC | SFD_NONBLOCK);
	sigprocmask(SIG_BLOCK, &mask, NULL);
	source->cb = cb;
	source->base.interface = &signal_source_interface;
	source->signal_number = signal_number;

	return clv_event_loop_add_source(loop, &source->base,
					 CLV_EVT_READABLE, data);
}

void clv_event_loop_add_destroy_listener(struct clv_event_loop *loop,
					    struct clv_listener *listener)
{
	clv_signal_add(&loop->destroy_signal, listener);
}

struct clv_listener * clv_event_loop_get_destroy_listener(
					struct clv_event_loop *loop,
					clv_notify_cb_t notify)
{
	return clv_signal_get(&loop->destroy_signal, notify);
}

