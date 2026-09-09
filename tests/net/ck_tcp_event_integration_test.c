#define _GNU_SOURCE

#include "../../c/connection/ck_connection_registry.h"
#include "../../c/event/ck_event_loop.h"
#include "../../c/http/ck_http_parser.h"
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

int main(void)
{
	ck_tcp_listener_t listener = {0};
	ck_event_loop_t loop = {0};
	ck_connection_registry_t registry = {0};
	ck_http_parser_t parser = {0};
	ck_http_request_t parsed_request = {0};
	ck_event_notification_t notifications[4] = {0};
	ck_connection_handle_t handle;
	ck_connection_handle_t reused_handle;
	const char payload[] = "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n";
	const size_t payload_length = sizeof(payload) - 1U;
	char request[sizeof(payload)];
	int client_fd;
	int accepted_fd;
	int count;
	size_t consumed;

	assert(ck_tcp_listener_init(&listener, 0) == 0);
	assert(ck_event_loop_init(&loop) == 0);
	assert(ck_connection_registry_init(&registry) == 0);

	client_fd = connect_loopback(ck_tcp_listener_port(&listener));
	assert(client_fd >= 0);
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

	assert(send(client_fd, payload, payload_length, 0) == (ssize_t)payload_length);
	count = ck_event_loop_wait(&loop, notifications, 4, 1000);
	assert(count == 1);
	assert(notifications[0].cookie == handle);
	assert((notifications[0].events & CK_EVENT_READ) != 0);

	assert(ck_connection_registry_socket_fd(&registry, notifications[0].cookie,
		UINT64_C(2001), UINT64_C(3001), UINT64_C(4001)) == accepted_fd);
	assert(recv(accepted_fd, request, sizeof(request), MSG_DONTWAIT)
			== (ssize_t)payload_length);
	assert(memcmp(request, payload, payload_length) == 0);
	ck_http_parser_init(&parser);
	assert(ck_http_parser_feed(&parser, request, payload_length,
		&consumed, &parsed_request) == CK_HTTP_PARSE_COMPLETE);
	assert(consumed == payload_length);
	assert(parsed_request.method.length == 3);
	assert(memcmp(parsed_request.method.data, "GET", 3) == 0);
	assert(parsed_request.target.length == 1);
	assert(memcmp(parsed_request.target.data, "/", 1) == 0);
	assert(parsed_request.body_mode == CK_HTTP_BODY_NONE);
	assert(recv(accepted_fd, request, sizeof(request), MSG_DONTWAIT) == -1);
	assert(errno == EAGAIN || errno == EWOULDBLOCK);

	assert(close(client_fd) == 0);
	count = ck_event_loop_wait(&loop, notifications, 4, 1000);
	assert(count == 1);
	assert(notifications[0].cookie == handle);
	assert((notifications[0].events & (CK_EVENT_RDHUP | CK_EVENT_ERROR)) != 0);

	assert(ck_event_loop_remove(&loop, accepted_fd) == 0);
	assert(ck_connection_registry_start_async_cycle(&registry, handle,
		UINT64_C(2001), UINT64_C(3001), UINT64_C(4001), UINT64_C(1)) == 0);
	assert(ck_connection_registry_try_terminal(&registry, handle,
		UINT64_C(2001), UINT64_C(3001), UINT64_C(4001), UINT64_C(1),
		CK_CONNECTION_TERMINAL_CLIENT_DISCONNECT)
			== CK_CONNECTION_TERMINAL_CLAIMED);
	assert(ck_connection_registry_close(&registry, handle,
		UINT64_C(2001), UINT64_C(3001), UINT64_C(4001)) == 0);
	assert(ck_connection_registry_socket_fd(&registry, handle,
		UINT64_C(2001), UINT64_C(3001), UINT64_C(4001)) == -1);
	assert(ck_connection_registry_retire(&registry, handle,
		UINT64_C(2001), UINT64_C(3001), UINT64_C(4001)) == 0);

	assert(ck_connection_registry_socket_fd(&registry, handle,
		UINT64_C(2001), UINT64_C(3001), UINT64_C(4001)) == -2);
	assert(ck_connection_registry_register(&registry,
		UINT64_C(1002), UINT64_C(2002), UINT64_C(3002), UINT64_C(4002),
		&reused_handle) == 0);
	assert(reused_handle != handle);
	assert(ck_connection_registry_socket_fd(&registry, handle,
		UINT64_C(2001), UINT64_C(3001), UINT64_C(4001)) == -2);
	assert(ck_connection_registry_start_async_cycle(&registry, reused_handle,
		UINT64_C(2002), UINT64_C(3002), UINT64_C(4002), UINT64_C(1)) == 0);
	assert(ck_connection_registry_try_terminal(&registry, reused_handle,
		UINT64_C(2002), UINT64_C(3002), UINT64_C(4002), UINT64_C(1),
		CK_CONNECTION_TERMINAL_SHUTDOWN) == CK_CONNECTION_TERMINAL_CLAIMED);
	assert(ck_connection_registry_close(&registry, reused_handle,
		UINT64_C(2002), UINT64_C(3002), UINT64_C(4002)) == 0);
	assert(ck_connection_registry_retire(&registry, reused_handle,
		UINT64_C(2002), UINT64_C(3002), UINT64_C(4002)) == 0);

	assert(ck_event_loop_remove(&loop, ck_tcp_listener_fd(&listener)) == 0);
	assert(ck_tcp_listener_destroy(&listener) == 0);
	assert(ck_event_loop_destroy(&loop) == 0);
	assert(ck_connection_registry_destroy(&registry) == 0);

	return 0;
}
