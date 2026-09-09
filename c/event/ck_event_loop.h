#ifndef CKARTA_EVENT_LOOP_H
#define CKARTA_EVENT_LOOP_H

#include <stddef.h>
#include <stdint.h>

#define CK_EVENT_READ UINT32_C(1)
#define CK_EVENT_WRITE UINT32_C(2)
#define CK_EVENT_RDHUP UINT32_C(4)
#define CK_EVENT_ERROR UINT32_C(8)

typedef uint64_t ck_event_cookie_t;

typedef struct ck_event_notification
{
	ck_event_cookie_t cookie;
	uint32_t events;
} ck_event_notification_t;

typedef struct ck_event_loop
{
	int epoll_fd;
	int initialized;
} ck_event_loop_t;

int ck_event_loop_init(ck_event_loop_t *loop);
int ck_event_loop_add(ck_event_loop_t *loop,
		int socket_fd,
		ck_event_cookie_t cookie,
		uint32_t events);
int ck_event_loop_modify(ck_event_loop_t *loop,
		int socket_fd,
		ck_event_cookie_t cookie,
		uint32_t events);
int ck_event_loop_remove(ck_event_loop_t *loop, int socket_fd);
int ck_event_loop_wait(ck_event_loop_t *loop,
		ck_event_notification_t *notifications,
		size_t capacity,
		int timeout_ms);
int ck_event_loop_destroy(ck_event_loop_t *loop);

#endif
