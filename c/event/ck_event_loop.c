#include "ck_event_loop.h"

#include <errno.h>
#include <sys/epoll.h>
#include <unistd.h>

static int ck_event_to_epoll(uint32_t events, uint32_t *epoll_events)
{
	uint32_t value = 0;

	if (epoll_events == NULL || events == 0
			|| (events & ~(CK_EVENT_READ | CK_EVENT_WRITE
				| CK_EVENT_RDHUP | CK_EVENT_ERROR)) != 0)
	{
		return -1;
	}

	if ((events & CK_EVENT_READ) != 0)
	{
		value |= EPOLLIN;
	}
	if ((events & CK_EVENT_WRITE) != 0)
	{
		value |= EPOLLOUT;
	}
	if ((events & CK_EVENT_RDHUP) != 0)
	{
		value |= EPOLLRDHUP;
	}
	if ((events & CK_EVENT_ERROR) != 0)
	{
		value |= EPOLLERR | EPOLLHUP;
	}

	*epoll_events = value;
	return 0;
}

static uint32_t ck_epoll_to_event(uint32_t epoll_events)
{
	uint32_t events = 0;

	if ((epoll_events & EPOLLIN) != 0)
	{
		events |= CK_EVENT_READ;
	}
	if ((epoll_events & EPOLLOUT) != 0)
	{
		events |= CK_EVENT_WRITE;
	}
	if ((epoll_events & EPOLLRDHUP) != 0)
	{
		events |= CK_EVENT_RDHUP;
	}
	if ((epoll_events & (EPOLLERR | EPOLLHUP)) != 0)
	{
		events |= CK_EVENT_ERROR;
	}

	return events;
}

int ck_event_loop_init(ck_event_loop_t *loop)
{
	if (loop == NULL)
	{
		return EINVAL;
	}

	loop->epoll_fd = epoll_create1(EPOLL_CLOEXEC);
	if (loop->epoll_fd < 0)
	{
		loop->initialized = 0;
		return errno;
	}

	loop->initialized = 1;
	return 0;
}

int ck_event_loop_add(ck_event_loop_t *loop,
	int socket_fd,
	ck_event_cookie_t cookie,
	uint32_t events)
{
	struct epoll_event event = {0};
	uint32_t epoll_events;

	if (loop == NULL || !loop->initialized || socket_fd < 0 || cookie == 0)
	{
		return EINVAL;
	}
	if (ck_event_to_epoll(events, &epoll_events) != 0)
	{
		return EINVAL;
	}

	event.events = epoll_events;
	event.data.u64 = cookie;
	if (epoll_ctl(loop->epoll_fd, EPOLL_CTL_ADD, socket_fd, &event) != 0)
	{
		return errno;
	}

	return 0;
}

int ck_event_loop_modify(ck_event_loop_t *loop,
	int socket_fd,
	ck_event_cookie_t cookie,
	uint32_t events)
{
	struct epoll_event event = {0};
	uint32_t epoll_events;

	if (loop == NULL || !loop->initialized || socket_fd < 0 || cookie == 0)
	{
		return EINVAL;
	}
	if (ck_event_to_epoll(events, &epoll_events) != 0)
	{
		return EINVAL;
	}

	event.events = epoll_events;
	event.data.u64 = cookie;
	if (epoll_ctl(loop->epoll_fd, EPOLL_CTL_MOD, socket_fd, &event) != 0)
	{
		return errno;
	}

	return 0;
}

int ck_event_loop_remove(ck_event_loop_t *loop, int socket_fd)
{
	if (loop == NULL || !loop->initialized || socket_fd < 0)
	{
		return EINVAL;
	}

	if (epoll_ctl(loop->epoll_fd, EPOLL_CTL_DEL, socket_fd, NULL) != 0)
	{
		return errno;
	}

	return 0;
}

int ck_event_loop_wait(ck_event_loop_t *loop,
	ck_event_notification_t *notifications,
	size_t capacity,
	int timeout_ms)
{
	struct epoll_event events[64];
	size_t max_events;
	int count;
	int i;

	if (loop == NULL || !loop->initialized || notifications == NULL
			|| capacity == 0 || timeout_ms < -1)
	{
		return -EINVAL;
	}

	max_events = capacity < (sizeof(events) / sizeof(events[0]))
			? capacity : (sizeof(events) / sizeof(events[0]));
	count = epoll_wait(loop->epoll_fd, events, (int)max_events, timeout_ms);
	if (count < 0)
	{
		return -errno;
	}

	for (i = 0; i < count; ++i)
	{
		notifications[i].cookie = events[i].data.u64;
		notifications[i].events = ck_epoll_to_event(events[i].events);
	}

	return count;
}

int ck_event_loop_destroy(ck_event_loop_t *loop)
{
	int close_result;

	if (loop == NULL)
	{
		return EINVAL;
	}
	if (!loop->initialized)
	{
		return 0;
	}

	close_result = close(loop->epoll_fd);
	loop->epoll_fd = -1;
	loop->initialized = 0;
	if (close_result == 0)
	{
		return 0;
	}

	return errno == EINTR ? EINTR : errno;
}
