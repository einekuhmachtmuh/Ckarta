#define _GNU_SOURCE

#include "../../c/connection/ck_connection_registry.h"
#include "../../c/event/ck_event_loop.h"
#include "../../c/http/ck_http_connection_reader.h"
#include "../../c/net/ck_tcp_listener.h"

#include <assert.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

struct body_capture
{
	char data[5];
	size_t length;
};

static int capture_body(void *context, const unsigned char *data, size_t length)
{
	struct body_capture *capture = context;

	assert(capture != NULL);
	assert(capture->length + length <= sizeof(capture->data));
	memcpy(capture->data + capture->length, data, length);
	capture->length += length;
	return 0;
}

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
	ck_connection_registry_reader_pin_t pin = {0};
	ck_http_connection_reader_result_t reader_result;
	ck_http_connection_reader_t *reader;
	ck_event_notification_t notifications[4] = {0};
	ck_connection_handle_t handle;
	ck_connection_handle_t reused_handle;
	const char payload[] =
		"POST /upload HTTP/1.1\r\n"
		"Host: localhost\r\n"
		"Content-Length: 5\r\n"
		"\r\n"
		"hello"
		"GET /next HTTP/1.1\r\n"
		"Host: localhost\r\n\r\n";
	const size_t payload_length = sizeof(payload) - 1U;
	const char next_request[] =
		"GET /next HTTP/1.1\r\nHost: localhost\r\n\r\n";
	struct body_capture capture = {0};
	int client_fd;
	int accepted_fd;
	int count;

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

	for (;;)
	{
		count = ck_event_loop_wait(&loop, notifications, 4, 1000);
		assert(count == 1);
		assert(notifications[0].cookie == handle);
		if ((notifications[0].events & CK_EVENT_READ) == 0)
		{
			assert((notifications[0].events & (CK_EVENT_RDHUP | CK_EVENT_ERROR)) == 0);
			continue;
		}

		assert(ck_connection_registry_reader_acquire(
			&registry, handle, UINT64_C(2001), UINT64_C(3001),
			UINT64_C(4001), &pin) == 0);
		reader = pin.reader;
		reader_result = ck_http_connection_reader_drive(reader,
			accepted_fd, capture_body, &capture);
		assert(reader_result == CK_HTTP_CONNECTION_READ_REQUEST_COMPLETE
				|| reader_result == CK_HTTP_CONNECTION_READ_INCOMPLETE);
		assert(ck_connection_registry_reader_release(&registry, &pin) == 0);
		if (reader_result == CK_HTTP_CONNECTION_READ_REQUEST_COMPLETE)
		{
			break;
		}
	}

	assert(capture.length == sizeof(capture.data));
	assert(memcmp(capture.data, "hello", sizeof(capture.data)) == 0);
	assert(ck_connection_registry_reader_acquire(
			&registry, handle, UINT64_C(2001), UINT64_C(3001),
			UINT64_C(4001), &pin) == 0);
		reader = pin.reader;
	assert(ck_http_connection_reader_buffered_bytes(reader)
			== strlen(next_request));
	assert(memcmp(ck_http_connection_reader_buffer(reader),
		next_request, strlen(next_request)) == 0);
	assert(ck_http_connection_reader_next_request(reader) == 0);
	reader_result = ck_http_connection_reader_drive(reader,
		accepted_fd, NULL, NULL);
	assert(reader_result == CK_HTTP_CONNECTION_READ_REQUEST_COMPLETE);
	assert(ck_http_connection_reader_request(reader)->target.length == 5);
	assert(memcmp(ck_http_connection_reader_request(reader)->target.data,
		"/next", 5) == 0);
	assert(ck_connection_registry_reader_release(&registry, &pin) == 0);

	assert(recv(accepted_fd, &capture.data[0], 1, MSG_DONTWAIT) == -1);
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
