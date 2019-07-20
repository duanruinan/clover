#include <clover_utils.h>
#include <clover_log.h>
#include <clover_signal.h>

void clv_signal_init(struct clv_signal *signal)
{
	INIT_LIST_HEAD(&signal->listener_list);
}

void clv_signal_add(struct clv_signal *signal, struct clv_listener *listener)
{
	list_add_tail(&listener->link, &signal->listener_list);
}

struct clv_listener * clv_signal_get(struct clv_signal *signal,
				     clv_notify_cb_t notify)
{
	struct clv_listener *l;

	list_for_each_entry(l, &signal->listener_list, link)
		if (l->notify == notify)
			return l;

	return NULL;
}

void clv_signal_emit(struct clv_signal *signal, void *data)
{
	struct clv_listener *l, *next;

	list_for_each_entry_safe(l, next, &signal->listener_list, link) {
		l->notify(l, data);
	}
}

