#define _GNU_SOURCE

#include "../../c/connection/ck_connection_registry.h"
#include "../../c/event/ck_event_loop.h"
#include "../../c/net/ck_tcp_listener.h"

#include <assert.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static int connect_loopback(int port)
{
	struct sockaddr_in address = {0};
	int socket_fd;

	socket_fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
	assert(socket_fd >= 0);

	address.sin_family = AF_INET;
	address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	address.sin_port = htons((unsigned short)port);

	assert(connect(socket_fd, (const struct sockaddr *)&address,
			sizeof(address)) == 0);
	return socket_fd;
}

static void assert_peer_eof(int socket_fd)
{
	char buffer[1];
	assert(recv(socket_fd, buffer, sizeof(buffer), 0) == 0);
}

int main(void)
{
	ck_tcp_listener_t listener = {0};
	ck_event_loop_t loop = {0};
	ck_connection_registry_t registry = {0};
	ck_event_notification_t notifications[4] = {0};
	ck_connection_handle_t handle;
	const char payload[] = "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n";
	char request[sizeof(payload)];
	int client_fd;
	int accepted_fd;
	int count;
	int state;

	assert(ck_tcp_listener_init(&listener, 0) == 0);
	assert(ck_event_loop_init(&loop) == 0);
	assert(ck_connection_registry_init(&registry) == 0);

	client_fd = connect_loopback(ck_tcp_listener_port(&listener));
	assert(ck_event_loop_add(&loop, ck_tcp_listener_fd(&listener),
		UINT64_C(1), CK_EVENT_READ | CK_EVENT_ERROR) == 0);

	count = ck_event_loop_wait(&loop, notifications, 4, 1000);
	assert(count == 1);
	assert(notifications[0].cookie == UINT64_C(1));
	accepted_fd = ck_tcp_listener_accept(&listener);
	assert(accepted_fd >= 0);

	assert(ck_connection_registry_register(&registry,
		UINT64_C(1001), UINT64_C(2001), UINT64_C(3001), UINT64_C(4001),
		&handle) == 0);
	assert(ck_connection_registry_attach_socket(&registry, handle,
		UINT64_C(2001), UINT64_C(3001), UINT64_C(4001), accepted_fd) == 0);
	assert(ck_event_loop_add(&loop, accepted_fd, handle,
		CK_EVENT_READ | CK_EVENT_RDHUP | CK_EVENT_ERROR) == 0);

	assert(send(client_fd, payload, sizeof(payload), 0) == (ssize_t)sizeof(payload));
	count = ck_event_loop_wait(&loop, notifications, 4, 1000);
	assert(count == 1);
	assert(notifications[0].cookie == handle);
	assert((notifications[0].events & CK_EVENT_READ) != 0);

	assert(ck_connection_registry_socket_fd(&registry, notifications[0].cookie,
		UINT64_C(2001), UINT64_C(3001), UINT64_C(4001)) == accepted_fd);
	assert(recv(accepted_fd, request, sizeof(request), MSG_DONTWAIT)
			== (ssize_t)sizeof(payload));
	assert(memcmp(request, payload, sizeof(payload)) == 0);
	assert(recv(accepted_fd, request, sizeof(request), MSG_DONTWAIT) == -1);
	assert(errno == EAGAIN || errno == EWOULDBLOCK);

	assert(close(client_fd) == 0);
	count = ck_event_loop_wait(&loop, notifications, 4, 1000);
	assert(count == 1);
	assert(notifications[0].cookie == handle);
	assert((notifications[0].events & (CK_EVENT_RDHUP | CK_EVENT_ERROR)) != 0);

	assert(ck_event_loop_remove(&loop, accepted_fd) == 0);
	state = ck_connection_registry_try_terminal(&registry, handle,
		UINT64_C(2001), UINT64_C(3001), UINT64_C(4001), UINT64_C(0),
		CK_CONNECTION_TERMINAL_CLIENT_DISCONNECT);
	assert(state < 0);

	assert(ck_connection_registry_close(&registry, handle,
		UINT64_C(2001), UINT64_C(3001), UINT64_C(4001)) == 0);
	assert(ck_connection_registry_socket_fd(&registry, handle,
		UINT64_C(2001), UINT64_C(3001), UINT64_C(4001)) == -1);
	assert(ck_connection_registry_retire(&registry, handle,
		UINT64_C(2001), UINT64_C(3001), UINT64_C(4001)) == 0);

	assert(ck_event_loop_remove(&loop, ck_tcp_listener_fd(&listener)) == 0);
	assert(ck_tcp_listener_destroy(&listener) == 0);
	assert(ck_event_loop_destroy(&loop) == 0);
	assert(ck_connection_registry_destroy(&registry) == 0);

	return 0;
}
