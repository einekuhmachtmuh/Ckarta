#define _GNU_SOURCE

#include "../../c/connection/ck_connection_registry.h"
#include "../../c/event/ck_event_loop.h"
#include "../../c/output/ck_http_response.h"

#include <assert.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int main(void)
{
	ck_connection_registry_t registry;
	ck_connection_registry_output_pin_t pin = {0};
	ck_http_response_t response;
	ck_event_loop_t loop = {0};
	ck_event_notification_t notifications[2] = {0};
	unsigned char body[48000];
	unsigned char headers[CK_HTTP_RESPONSE_HEADER_BUFFER_BYTES];
	unsigned char received[CK_HTTP_OUTPUT_WRITE_BUFFER_BYTES];
	int sockets[2];
	int count;
	int socket_fd;
	size_t header_length = 0;
	size_t expected_length;
	size_t received_length = 0;
	ck_connection_handle_t handle;
	ck_http_output_write_result_t write_result;

	memset(body, 'c', sizeof(body));
	memset(headers, 0, sizeof(headers));
	memset(received, 0, sizeof(received));

	assert(ck_connection_registry_init(&registry) == 0);
	assert(ck_connection_registry_register(
			&registry, 7, 70, 71, 72, &handle) == 0);
	assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
	assert(ck_connection_registry_attach_socket(
			&registry, handle, 70, 71, 72, sockets[0]) == 0);
	assert(ck_event_loop_init(&loop) == 0);
	assert(ck_connection_registry_start_async_cycle(
			&registry, handle, 70, 71, 72, 42) == 0);

	ck_http_response_init(&response);
	assert(ck_http_response_set_status(&response, 200U) == 0);
	assert(ck_http_response_set_content_length(
			&response, sizeof(body)) == 0);
	assert(ck_http_response_write_body(
			&response, body, sizeof(body)) == 0);
	assert(ck_http_response_finish(&response) == 0);
	assert(ck_http_response_serialize_headers(
			&response, headers, sizeof(headers), &header_length) == 0);
	expected_length = header_length + sizeof(body);

	assert(ck_connection_registry_output_acquire(
			&registry, handle, 70, 71, 72, &pin) == 0);
	assert(pin.connection != NULL);
	assert(pin.writer != NULL);
	assert(ck_http_output_writer_queue(
			pin.writer, headers, header_length) == 0);
	assert(ck_http_output_writer_queue(
			pin.writer, body, sizeof(body)) == 0);

	write_result = ck_http_output_writer_drive(pin.writer);
	assert(write_result == CK_HTTP_OUTPUT_WRITE_NEED_WRITE);
	assert(ck_http_output_writer_buffered_bytes(pin.writer)
			== expected_length - CK_HTTP_OUTPUT_WRITE_BUDGET_BYTES);

	count = (int)recv(sockets[1], received,
			CK_HTTP_OUTPUT_WRITE_BUDGET_BYTES, 0);
	assert(count == (int)CK_HTTP_OUTPUT_WRITE_BUDGET_BYTES);
	received_length = (size_t)count;

	assert(ck_connection_registry_output_release(&registry, &pin) == 0);
	assert(pin.connection == NULL);
	assert(pin.writer == NULL);

	socket_fd = ck_connection_registry_socket_fd(
			&registry, handle, 70, 71, 72);
	assert(socket_fd == sockets[0]);
	assert(ck_event_loop_add(
			&loop, socket_fd, handle, CK_EVENT_WRITE | CK_EVENT_ERROR) == 0);
	count = ck_event_loop_wait(&loop, notifications, 2, 1000);
	assert(count == 1);
	assert(notifications[0].cookie == handle);
	assert((notifications[0].events & CK_EVENT_WRITE) != 0);

	assert(ck_connection_registry_output_acquire(
			&registry, handle, 70, 71, 72, &pin) == 0);
	write_result = ck_http_output_writer_drive(pin.writer);
	assert(write_result == CK_HTTP_OUTPUT_WRITE_DRAINED);
	assert(ck_http_output_writer_buffered_bytes(pin.writer) == 0);
	assert(ck_connection_registry_output_release(&registry, &pin) == 0);

	while (received_length < expected_length)
	{
		ssize_t read_length = recv(sockets[1],
				received + received_length,
				sizeof(received) - received_length,
				0);
		assert(read_length > 0);
		received_length += (size_t)read_length;
	}

	assert(received_length == expected_length);
	assert(memcmp(received, headers, header_length) == 0);
	assert(memcmp(received + header_length, body, sizeof(body)) == 0);

	assert(ck_event_loop_remove(&loop, socket_fd) == 0);
	assert(ck_connection_registry_try_terminal(
			&registry, handle, 70, 71, 72, 42,
			CK_CONNECTION_TERMINAL_COMPLETE) == 0);
	assert(ck_connection_registry_close(
			&registry, handle, 70, 71, 72) == 0);
	assert(ck_connection_registry_retire(
			&registry, handle, 70, 71, 72) == 0);

	assert(close(sockets[1]) == 0);
	assert(ck_event_loop_destroy(&loop) == 0);
	assert(ck_connection_registry_destroy(&registry) == 0);
	return 0;
}
