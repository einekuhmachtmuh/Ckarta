#include "../../c/event/ck_event_loop.h"

#include <assert.h>
#include <errno.h>
#include <sys/socket.h>
#include <unistd.h>

int main(void)
{
	ck_event_loop_t loop = {0};
	ck_event_notification_t notifications[2] = {0};
	int sockets[2];
	const char payload[] = "x";
	char buffer[sizeof(payload)];
	int count;

	assert(ck_event_loop_init(&loop) == 0);
	assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);

	assert(ck_event_loop_add(&loop, sockets[0], UINT64_C(0x000000010000002a),
			CK_EVENT_READ | CK_EVENT_RDHUP | CK_EVENT_ERROR) == 0);
	assert(ck_event_loop_wait(&loop, notifications, 2, 0) == 0);

	assert(write(sockets[1], payload, sizeof(payload)) == (ssize_t)sizeof(payload));
	count = ck_event_loop_wait(&loop, notifications, 2, 1000);
	assert(count == 1);
	assert(notifications[0].cookie == UINT64_C(0x000000010000002a));
	assert((notifications[0].events & CK_EVENT_READ) != 0);
	assert(read(sockets[0], buffer, sizeof(buffer)) == (ssize_t)sizeof(payload));

	assert(ck_event_loop_modify(&loop, sockets[0], UINT64_C(0x000000020000002a),
			CK_EVENT_READ | CK_EVENT_RDHUP | CK_EVENT_ERROR) == 0);
	assert(write(sockets[1], payload, sizeof(payload)) == (ssize_t)sizeof(payload));
	count = ck_event_loop_wait(&loop, notifications, 2, 1000);
	assert(count == 1);
	assert(notifications[0].cookie == UINT64_C(0x000000020000002a));
	assert((notifications[0].events & CK_EVENT_READ) != 0);
	assert(read(sockets[0], buffer, sizeof(buffer)) == (ssize_t)sizeof(payload));

	assert(ck_event_loop_remove(&loop, sockets[0]) == 0);
	assert(ck_event_loop_remove(&loop, sockets[0]) == ENOENT);
	assert(ck_event_loop_wait(&loop, notifications, 2, 0) == 0);

	assert(close(sockets[1]) == 0);
	assert(ck_event_loop_add(&loop, sockets[0], UINT64_C(0x000000030000002a),
			CK_EVENT_READ | CK_EVENT_RDHUP | CK_EVENT_ERROR) == 0);
	count = ck_event_loop_wait(&loop, notifications, 2, 1000);
	assert(count == 1);
	assert(notifications[0].cookie == UINT64_C(0x000000030000002a));
	assert((notifications[0].events & CK_EVENT_RDHUP) != 0
			|| (notifications[0].events & CK_EVENT_ERROR) != 0);
	assert(ck_event_loop_remove(&loop, sockets[0]) == 0);
	assert(close(sockets[0]) == 0);
	assert(ck_event_loop_destroy(&loop) == 0);

	return 0;
}
