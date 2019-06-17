#ifndef CLOVER_EVENT_H
#define CLOVER_EVENT_H

#include <sys/epoll.h>
#include <clover_utils.h>
#include <clover_signal.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
	CLV_EVT_READABLE = 0x01,
	CLV_EVT_WRITABLE = 0x02,
	CLV_EVT_HANGUP   = 0x04,
	CLV_EVT_ERROR    = 0x08,
};

struct clv_event_loop {
	s32 epoll_fd;
	struct list_head idle_list;
	struct list_head destroy_list;
	struct clv_signal destroy_signal;
};

struct clv_event_source;

struct clv_event_source_interface {
	s32 (*dispatch)(struct clv_event_source *source,
			struct epoll_event *ep);
};

typedef void (*clv_event_loop_idle_cb_t)(void *data);
typedef s32 (*clv_event_loop_fd_cb_t)(s32 fd, u32 mask, void *data);
typedef s32 (*clv_event_loop_timer_cb_t)(void *data);
typedef s32 (*clv_event_loop_signal_cb_t)(s32 signal_number, void *data);

struct clv_event_source {
	struct clv_event_source_interface *interface;
	struct clv_event_loop *loop;
	struct list_head link;
	void *data;
	s32 fd;
};

s32 clv_set_cloexec_or_close(s32 fd);
s32 clv_dupfd_cloexec(s32 fd, s32 minfd);
struct clv_event_source * clv_event_loop_add_fd(struct clv_event_loop *loop,
						s32 fd, u32 mask,
						clv_event_loop_fd_cb_t cb,
						void *data);
s32 clv_event_source_fd_update_mask(struct clv_event_source *source, u32 mask);
struct clv_event_source * clv_event_loop_add_timer(
				struct clv_event_loop *loop,
				clv_event_loop_timer_cb_t cb,
				void *data);
s32 clv_event_source_timer_update(struct clv_event_source *source,
				  s32 ms, s32 us);
struct clv_event_source * clv_event_loop_add_signal(
				struct clv_event_loop *loop,
				s32 signal_number,
				clv_event_loop_signal_cb_t cb,
				void *data);

struct clv_event_loop * clv_event_loop_create(void);
void clv_event_loop_destroy(struct clv_event_loop *loop);
void clv_event_source_remove(struct clv_event_source *source);

struct clv_event_source * clv_event_loop_add_idle(struct clv_event_loop *loop,
						  clv_event_loop_idle_cb_t cb,
						  void *data);

s32 clv_event_loop_dispatch(struct clv_event_loop *loop, s32 timeout);

void clv_event_loop_add_destroy_listener(struct clv_event_loop *loop,
					 struct clv_listener *listener);

struct clv_listener * clv_event_loop_get_destroy_listener(
					struct clv_event_loop *loop,
					clv_notify_cb_t notify);

#ifdef __cplusplus
}
#endif

#endif

