#ifndef CLOVER_SIGNAL_H
#define CLOVER_SIGNAL_H

#include <clover_utils.h>

#ifdef __cplusplus
extern "C" {
#endif

struct clv_listener;

typedef void (*clv_notify_cb_t)(struct clv_listener *listener, void *data);

struct clv_listener {
	struct list_head link;
	clv_notify_cb_t notify;
};

struct clv_signal {
	struct list_head listener_list;
};

void clv_signal_init(struct clv_signal *signal);
void clv_signal_add(struct clv_signal *signal, struct clv_listener *listener);
struct clv_listener * clv_signal_get(struct clv_signal *signal,
				     clv_notify_cb_t notify);
void clv_signal_emit(struct clv_signal *signal, void *data);

#ifdef __cplusplus
}
#endif

#endif

